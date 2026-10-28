"""Publish random integer values to an MQTT topic every five seconds."""

import os
import random
import time

import paho.mqtt.client as mqtt


def env_bool(name: str, default: bool = False) -> bool:
    value = os.getenv(name)
    if value is None:
        return default
    return value.strip().lower() in {"1", "true", "yes", "on"}


BROKER_HOST = os.getenv("OTA_MQTT_HOST", "127.0.0.1")
BROKER_PORT = int(os.getenv("OTA_MQTT_PORT", "1883"))
USERNAME = os.getenv("OTA_MQTT_USERNAME", "")
PASSWORD = os.getenv("OTA_MQTT_PASSWORD", "")
USE_TLS = env_bool("OTA_MQTT_TLS")
TOPIC = os.getenv("MQTT_DUMMY_TOPIC", "gateway/dummy/value")
MIN_VALUE = int(os.getenv("MQTT_DUMMY_MIN", "0"))
MAX_VALUE = int(os.getenv("MQTT_DUMMY_MAX", "100"))
INTERVAL_SECONDS = float(os.getenv("MQTT_DUMMY_INTERVAL", "5"))


def make_client() -> mqtt.Client:
    callback_api = getattr(mqtt, "CallbackAPIVersion", None)
    if callback_api is not None:
        client = mqtt.Client(callback_api.VERSION2)
    else:
        client = mqtt.Client()

    if USERNAME:
        client.username_pw_set(USERNAME, PASSWORD)
    if USE_TLS:
        # Uses the system certificate store and validates the broker certificate.
        client.tls_set()
    return client


def main() -> None:
    if MIN_VALUE > MAX_VALUE:
        raise ValueError("MQTT_DUMMY_MIN harus lebih kecil atau sama dengan MQTT_DUMMY_MAX")
    if INTERVAL_SECONDS <= 0:
        raise ValueError("MQTT_DUMMY_INTERVAL harus lebih besar dari 0")

    client = make_client()
    client.connect(BROKER_HOST, BROKER_PORT, keepalive=60)
    client.loop_start()
    print(
        f"Terhubung ke {BROKER_HOST}:{BROKER_PORT}; topic={TOPIC}; "
        f"TLS={'on' if USE_TLS else 'off'}. Tekan Ctrl+C untuk berhenti."
    )

    try:
        while True:
            value = random.randint(MIN_VALUE, MAX_VALUE)
            result = client.publish(TOPIC, str(value), qos=0, retain=False)
            result.wait_for_publish()
            print(f"Publish {TOPIC}: {value}")
            time.sleep(INTERVAL_SECONDS)
    except KeyboardInterrupt:
        print("\nPublisher dihentikan.")
    finally:
        client.loop_stop()
        client.disconnect()


if __name__ == "__main__":
    main()
