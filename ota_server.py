import hashlib
import json
import logging
import os
import re
import threading
import time
from pathlib import Path

import paho.mqtt.client as mqtt
import requests
from flask import Flask, abort, send_from_directory


GITHUB_REPO = os.getenv("OTA_GITHUB_REPO", "ahmadfirmanALV/ESP-OTA2")
CHECK_INTERVAL_SECONDS = int(os.getenv("OTA_CHECK_INTERVAL_SECONDS", "300"))
MQTT_BROKER = os.getenv("OTA_MQTT_HOST", "127.0.0.1")
MQTT_PORT = int(os.getenv("OTA_MQTT_PORT", "1883"))
MQTT_USERNAME = os.getenv("OTA_MQTT_USERNAME", "")
MQTT_PASSWORD = os.getenv("OTA_MQTT_PASSWORD", "")
MQTT_TOPIC_OTA = "gateway/ota/update"
SERVER_IP = os.getenv("OTA_SERVER_IP", "192.168.211.251")
SERVER_PORT = int(os.getenv("OTA_SERVER_PORT", "5000"))
FIRMWARE_DIR = Path(__file__).resolve().parent / "firmware_files"
FIRMWARE_DIR.mkdir(parents=True, exist_ok=True)

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s %(levelname)s %(message)s",
)
logger = logging.getLogger("esp32-ota-server")
app = Flask(__name__)
last_processed_tag = None
last_update_payload = None


def make_mqtt_client():
    # Compatible with Paho MQTT 1.x and 2.x.
    callback_api = getattr(mqtt, "CallbackAPIVersion", None)
    if callback_api is not None:
        client = mqtt.Client(callback_api.VERSION2)
    else:
        client = mqtt.Client()
    if MQTT_USERNAME:
        client.username_pw_set(MQTT_USERNAME, MQTT_PASSWORD)
    return client


def publish_update(payload):
    client = make_mqtt_client()
    client.connect(MQTT_BROKER, MQTT_PORT, keepalive=60)
    client.loop_start()
    try:
        result = client.publish(MQTT_TOPIC_OTA, payload, qos=1, retain=True)
        result.wait_for_publish(timeout=15)
        if result.rc != mqtt.MQTT_ERR_SUCCESS or not result.is_published():
            raise RuntimeError(f"MQTT publish gagal, rc={result.rc}")
    finally:
        client.disconnect()
        client.loop_stop()


def get_latest_release():
    api_url = f"https://api.github.com/repos/{GITHUB_REPO}/releases/latest"
    response = requests.get(
        api_url,
        headers={"User-Agent": "ESP32-OTA-Server"},
        timeout=20,
    )
    response.raise_for_status()
    release = response.json()
    tag_name = release.get("tag_name")
    if not tag_name:
        raise RuntimeError("GitHub Release tidak memiliki tag_name.")

    assets = release.get("assets", [])
    asset = next(
        (
            item
            for item in assets
            if item.get("name") == "firmware.bin"
        ),
        None,
    )
    if asset is None:
        raise RuntimeError(f"Release {tag_name} tidak memiliki asset firmware.bin.")

    manifest_asset = next(
        (item for item in assets if item.get("name") == "ota_manifest.json"),
        None,
    )
    if manifest_asset is None:
        raise RuntimeError(
            f"Release {tag_name} belum memakai alur OTA server; manifest tidak ada."
        )
    manifest_response = requests.get(manifest_asset["browser_download_url"], timeout=20)
    manifest_response.raise_for_status()
    manifest = manifest_response.json()
    if manifest.get("ota_protocol") != 1 or manifest.get("version") != tag_name:
        raise RuntimeError(f"Manifest Release {tag_name} tidak cocok atau tidak didukung.")
    return tag_name, asset


def download_firmware(tag_name, asset):
    binary_response = requests.get(asset["browser_download_url"], timeout=60)
    binary_response.raise_for_status()
    firmware = binary_response.content
    if not firmware or firmware[0] != 0xE9:
        raise RuntimeError("Asset bukan image aplikasi ESP32 yang valid.")
    expected_size = asset.get("size")
    if expected_size is not None and len(firmware) != expected_size:
        raise RuntimeError("Ukuran firmware unduhan tidak cocok dengan metadata GitHub.")

    safe_tag = re.sub(r"[^A-Za-z0-9._-]", "_", tag_name)
    filename = f"firmware_{safe_tag}.bin"
    final_path = FIRMWARE_DIR / filename
    temp_path = FIRMWARE_DIR / f"{filename}.tmp"
    temp_path.write_bytes(firmware)
    temp_path.replace(final_path)

    md5 = hashlib.md5(firmware).hexdigest()
    local_url = f"http://{SERVER_IP}:{SERVER_PORT}/download/{filename}"
    payload = {
        "version": tag_name,
        "url": local_url,
        "md5": md5,
    }
    return filename, payload


def process_latest_release():
    global last_processed_tag, last_update_payload
    tag_name, asset = get_latest_release()
    if tag_name != last_processed_tag:
        filename, last_update_payload = download_firmware(tag_name, asset)
        last_processed_tag = tag_name
        logger.info("Firmware rilis %s disimpan sebagai %s.", tag_name, filename)

    # Re-publish retained state so a broker restart or offline ESP32 can recover it.
    if last_update_payload is not None:
        publish_update(json.dumps(last_update_payload, separators=(",", ":")))
        logger.info("Pengumuman OTA untuk rilis %s dikirim lewat MQTT.", tag_name)


def github_polling_worker():
    while True:
        try:
            process_latest_release()
        except Exception:
            logger.exception("Gagal memeriksa atau mengumumkan firmware terbaru.")
        time.sleep(CHECK_INTERVAL_SECONDS)


@app.get("/health")
def health():
    return {"status": "ok", "github_repo": GITHUB_REPO}


@app.get("/download/<path:filename>")
def serve_firmware(filename):
    if Path(filename).name != filename or not filename.endswith(".bin"):
        abort(404)
    return send_from_directory(
        FIRMWARE_DIR,
        filename,
        mimetype="application/octet-stream",
    )


if __name__ == "__main__":
    threading.Thread(target=github_polling_worker, daemon=True).start()
    logger.info(
        "OTA server aktif: repo=%s, broker=%s:%s, firmware URL host=%s:%s",
        GITHUB_REPO,
        MQTT_BROKER,
        MQTT_PORT,
        SERVER_IP,
        SERVER_PORT,
    )
    app.run(host="0.0.0.0", port=SERVER_PORT, debug=False, use_reloader=False)
