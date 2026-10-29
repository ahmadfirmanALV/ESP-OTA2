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


CHECK_INTERVAL_SECONDS = int(os.getenv("OTA_CHECK_INTERVAL_SECONDS", "300"))
MQTT_BROKER = os.getenv("OTA_MQTT_HOST", "localhost")
MQTT_PORT = int(os.getenv("OTA_MQTT_PORT", "8883"))
MQTT_USERNAME = os.getenv("OTA_MQTT_USERNAME", "")
MQTT_PASSWORD = os.getenv("OTA_MQTT_PASSWORD", "")
MQTT_TLS = os.getenv("OTA_MQTT_TLS", "true").strip().lower() in {"1", "true", "yes", "on"}
MQTT_CA_CERT = os.getenv("OTA_MQTT_CA_CERT") or None
MQTT_TOPIC_OTA = "gateway/ota/update"
MQTT_TOPIC_VERSION_LATEST = "gateway/version/latest"
DEFAULT_GITHUB_REPO = os.getenv("OTA_GITHUB_REPO", "ahmadfirmanALV/ESP-OTA2").strip()
DEVICE_REPOS_TEXT = os.getenv("OTA_DEVICE_REPOS", "").strip()
LEGACY_TARGET_TEXT = os.getenv("OTA_TARGET_DEVICE_IDS", "").strip()
if not LEGACY_TARGET_TEXT:
    LEGACY_TARGET_TEXT = os.getenv("OTA_TARGET_DEVICE_ID", "").strip()
LEGACY_TARGET_IDS = tuple(
    dict.fromkeys(device_id.strip() for device_id in LEGACY_TARGET_TEXT.split(",") if device_id.strip())
)


def parse_device_repos():
    """Return (device_id, owner/repo) pairs; device_id=None means broadcast."""
    if DEVICE_REPOS_TEXT:
        mappings = []
        seen_ids = set()
        for item in DEVICE_REPOS_TEXT.split(";"):
            item = item.strip()
            if not item:
                continue
            if "=" not in item:
                raise ValueError("OTA_DEVICE_REPOS harus berformat device-id=owner/repo")
            device_id, repo = (part.strip() for part in item.split("=", 1))
            if not re.fullmatch(r"[A-Za-z0-9_-]{1,64}", device_id):
                raise ValueError(f"ID perangkat tidak valid di OTA_DEVICE_REPOS: {device_id}")
            if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", repo):
                raise ValueError(f"Repo GitHub tidak valid di OTA_DEVICE_REPOS: {repo}")
            if device_id in seen_ids:
                raise ValueError(f"ID perangkat duplikat di OTA_DEVICE_REPOS: {device_id}")
            seen_ids.add(device_id)
            mappings.append((device_id, repo))
        if not mappings:
            raise ValueError("OTA_DEVICE_REPOS tidak berisi mapping perangkat")
        return tuple(mappings)

    if any(not re.fullmatch(r"[A-Za-z0-9_-]{1,64}", device_id) for device_id in LEGACY_TARGET_IDS):
        raise ValueError("Setiap ID target hanya boleh berisi huruf, angka, _ atau -")
    if LEGACY_TARGET_IDS:
        return tuple((device_id, DEFAULT_GITHUB_REPO) for device_id in LEGACY_TARGET_IDS)
    return ((None, DEFAULT_GITHUB_REPO),)


DEVICE_REPOS = parse_device_repos()
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
release_state = {}


def make_mqtt_client():
    # Compatible with Paho MQTT 1.x and 2.x.
    callback_api = getattr(mqtt, "CallbackAPIVersion", None)
    if callback_api is not None:
        client = mqtt.Client(callback_api.VERSION2)
    else:
        client = mqtt.Client()
    if MQTT_USERNAME:
        client.username_pw_set(MQTT_USERNAME, MQTT_PASSWORD)
    if MQTT_TLS:
        # Verifies the broker certificate against system roots or the configured CA file.
        client.tls_set(ca_certs=MQTT_CA_CERT)
    return client


def publish_update(payload, device_id, version):
    client = make_mqtt_client()
    client.connect(MQTT_BROKER, MQTT_PORT, keepalive=60)
    client.loop_start()
    try:
        update_topic = f"{MQTT_TOPIC_OTA}/{device_id}" if device_id else MQTT_TOPIC_OTA
        version_topic = (
            f"{MQTT_TOPIC_VERSION_LATEST}/{device_id}"
            if device_id
            else MQTT_TOPIC_VERSION_LATEST
        )
        # A broker ACL may allow OTA announcements but deny the informational
        # version topic. Keep those publications independent so version reporting
        # cannot prevent a firmware update from being announced.
        for topic, message, required in (
            (update_topic, payload, True),
            (version_topic, version, False),
        ):
            try:
                result = client.publish(topic, message, qos=1, retain=True)
                result.wait_for_publish(timeout=15)
                if result.rc != mqtt.MQTT_ERR_SUCCESS or not result.is_published():
                    raise RuntimeError(f"MQTT publish gagal ke {topic}, rc={result.rc}")
            except Exception:
                if required:
                    raise
                logger.exception("Gagal mempublikasikan versi terbaru ke %s.", topic)
                continue
            logger.info("Pesan OTA dipublikasikan ke topic %s.", topic)
    finally:
        client.disconnect()
        client.loop_stop()


def get_latest_release(repo):
    api_url = f"https://api.github.com/repos/{repo}/releases/latest"
    response = requests.get(
        api_url,
        headers={"User-Agent": "ESP32-OTA-Server"},
        timeout=20,
    )
    response.raise_for_status()
    release = response.json()
    tag_name = release.get("tag_name")
    if not tag_name:
        raise RuntimeError(f"GitHub Release di {repo} tidak memiliki tag_name.")

    assets = release.get("assets", [])
    asset = next((item for item in assets if item.get("name") == "firmware.bin"), None)
    if asset is None:
        raise RuntimeError(f"Release {tag_name} di {repo} tidak memiliki asset firmware.bin.")

    manifest_asset = next((item for item in assets if item.get("name") == "ota_manifest.json"), None)
    if manifest_asset is None:
        raise RuntimeError(f"Release {tag_name} di {repo} belum memiliki ota_manifest.json.")
    manifest_response = requests.get(manifest_asset["browser_download_url"], timeout=20)
    manifest_response.raise_for_status()
    manifest = manifest_response.json()
    if manifest.get("ota_protocol") != 1 or manifest.get("version") != tag_name:
        raise RuntimeError(f"Manifest Release {tag_name} di {repo} tidak cocok atau tidak didukung.")
    return tag_name, asset


def download_firmware(repo, tag_name, asset):
    binary_response = requests.get(asset["browser_download_url"], timeout=60)
    binary_response.raise_for_status()
    firmware = binary_response.content
    if not firmware or firmware[0] != 0xE9:
        raise RuntimeError(f"Asset firmware dari {repo} bukan image aplikasi ESP32 yang valid.")
    expected_size = asset.get("size")
    if expected_size is not None and len(firmware) != expected_size:
        raise RuntimeError(f"Ukuran firmware unduhan dari {repo} tidak cocok dengan metadata GitHub.")

    safe_repo = re.sub(r"[^A-Za-z0-9._-]", "_", repo)
    safe_tag = re.sub(r"[^A-Za-z0-9._-]", "_", tag_name)
    filename = f"firmware_{safe_repo}_{safe_tag}.bin"
    final_path = FIRMWARE_DIR / filename
    temp_path = FIRMWARE_DIR / f"{filename}.tmp"
    temp_path.write_bytes(firmware)
    temp_path.replace(final_path)

    md5 = hashlib.md5(firmware).hexdigest()
    local_url = f"http://{SERVER_IP}:{SERVER_PORT}/download/{filename}"
    payload = {"version": tag_name, "url": local_url, "md5": md5}
    return filename, payload


def process_latest_release(device_id, repo):
    key = (device_id, repo)
    tag_name, asset = get_latest_release(repo)
    state = release_state.get(key)
    if state is None or tag_name != state["tag"]:
        filename, payload = download_firmware(repo, tag_name, asset)
        state = {"tag": tag_name, "payload": payload}
        release_state[key] = state
        logger.info("Rilis %s dari %s disimpan sebagai %s.", tag_name, repo, filename)

    # Re-publish retained state so an offline ESP32 can receive it after reconnecting.
    publish_update(
        json.dumps(state["payload"], separators=(",", ":")),
        device_id,
        tag_name,
    )
    logger.info("Rilis %s dari %s diumumkan untuk %s.", tag_name, repo, device_id or "broadcast")


def github_polling_worker():
    while True:
        for device_id, repo in DEVICE_REPOS:
            try:
                process_latest_release(device_id, repo)
            except Exception:
                logger.exception("Gagal memeriksa atau mengumumkan release dari %s.", repo)
        time.sleep(CHECK_INTERVAL_SECONDS)


@app.get("/health")
def health():
    mappings = [
        {"github_repo": repo, "target_device_id": device_id or "broadcast"}
        for device_id, repo in DEVICE_REPOS
    ]
    return {"status": "ok", "mappings": mappings}


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
    mapping_log = ", ".join(f"{repo} -> {device_id or 'broadcast'}" for device_id, repo in DEVICE_REPOS)
    logger.info(
        "OTA server aktif: %s; broker=%s:%s; firmware URL host=%s:%s; interval=%ss",
        mapping_log,
        MQTT_BROKER,
        MQTT_PORT,
        SERVER_IP,
        SERVER_PORT,
        CHECK_INTERVAL_SECONDS,
    )
    app.run(host="0.0.0.0", port=SERVER_PORT, debug=False, use_reloader=False)
