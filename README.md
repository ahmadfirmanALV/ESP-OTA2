# ESP32 Central OTA

PlatformIO project for a DOIT ESP32 DEVKIT V1. The project supports both the existing PlatformIO ArduinoOTA upload and central OTA through a local server:

1. A version tag such as `v1.0.2` starts a GitHub Actions build.
2. The workflow creates a GitHub Release containing `firmware.bin` and an OTA protocol manifest.
3. `ota_server.py` downloads the latest Release and retains an MQTT update message.
4. The ESP32 downloads that firmware from the local server over HTTP, checks its MD5, writes the inactive OTA partition, and restarts.

The local server and ESP32 must be on the same reachable LAN. GitHub must be reachable from the local server.

## Project files

- `.github/workflows/build.yml`: builds and publishes firmware plus a protocol manifest on `v*` tags.
- `partitions.csv`: two application slots for OTA on a 4 MB ESP32.
- `ota_server.py`: polls public GitHub Releases, caches the binary, serves it over HTTP, and sends the MQTT announcement.
- `mosquitto.conf.example`: LAN-only prototype broker configuration.
- `src/tasks/ota/ota_task.cpp`: ArduinoOTA fallback plus MQTT/HTTP update receiver.
- `include/app_version.h`: local builds use `dev`; GitHub builds replace this with the Release tag.
- `include/ota_secrets.h.example`: template for MQTT host, credentials, and the broker CA certificate; the real `ota_secrets.h` is ignored by Git.

## Configure the LAN

The laptop/server is configured at `192.168.211.251`, with gateway `192.168.211.15`, subnet `255.255.255.0`, and DNS `8.8.8.8`. The ESP32 uses `192.168.211.252` with the same gateway and DNS; make sure `.252` is unused and reserve it in the router so DHCP will not assign it to another device. The laptop and ESP32 must connect to the same LAN. After connecting laptop Wi-Fi, verify `ipconfig` shows `192.168.211.251` on that adapter; a static setting alone does not connect a disconnected adapter.

`OTA_SERVER_IP` in `include/app_config.h` and the server environment variable below are set to `192.168.211.251` to match the laptop's static address.

MQTT uses TLS on port `8883`. The broker host must be reachable from the ESP32 and match a name or IP address in the broker certificate's Subject Alternative Name (SAN). For a laptop broker, `localhost` is not usable by the ESP32; use `192.168.211.251` only if the certificate includes that IP. For a hosted broker, use its hostname. The Python server and firmware must use the same broker endpoint. The ESP32 still needs `OTA_SERVER_IP` because firmware binaries are served by the local laptop over HTTP.

For a local broker, allow inbound TCP ports `8883` (MQTT/TLS) and `5000` (firmware download) through the computer's firewall on the private LAN. The `mosquitto.conf.example` file is a TLS listener template; replace its certificate paths and ensure its server certificate contains the broker host/IP SAN. For a hosted broker, the laptop and ESP32 need outbound access to that broker, while port `5000` must still be reachable from the ESP32.

Copy `include/ota_secrets.h.example` to `include/ota_secrets.h` for a local firmware build. Set `OTA_MQTT_HOST`, username, and password, then paste the broker's CA PEM into `OTA_MQTT_ROOT_CA` and set `OTA_MQTT_ROOT_CA_DEFINED` to `1`. Keep the CA PEM intact, including its BEGIN/END lines. The ESP32 validates the broker certificate and refuses MQTT connections if the CA is missing or invalid. The real file is excluded from Git. For GitHub Releases, add repository secrets named `OTA_MQTT_HOST` (optional; defaults to the laptop IP), `OTA_MQTT_USERNAME`, `OTA_MQTT_PASSWORD`, and `OTA_MQTT_ROOT_CA_CERT`; the release workflow requires the CA secret. For the Python server, set `OTA_MQTT_HOST`, `OTA_MQTT_PORT=8883`, `OTA_MQTT_TLS=true`, credentials, and optionally `OTA_MQTT_CA_CERT` to a CA PEM file path when the CA is not in the system trust store.

## Start Mosquitto on Windows

Install Mosquitto, update the certificate paths in `mosquitto.conf.example`, then open a PowerShell window in this project folder and run:

```powershell
mosquitto -c .\mosquitto.conf.example -v
```

Keep this window open. The certificate must be valid for the host the ESP32 uses (normally `192.168.211.251`). If `mosquitto` is not recognized, run `mosquitto.exe` using its installation path. If another Mosquitto service is already using port `8883`, stop it or configure it to use this file instead.

## ESP32 dummy MQTT values

The ESP32 derives a stable ID from its 48-bit MAC and prints it in Serial Monitor, for example `esp32-ota-14335c524234`. Its status topic is `gateway/status/<device-id>`, its dummy value topic is `gateway/dummy/<device-id>/random`, and its device-specific OTA topic is `gateway/ota/update/<device-id>`. It also listens to the shared `gateway/ota/update` topic for broadcast announcements. The dummy topic publishes a random integer from `0` to `100` every five seconds, for example `42`. In MQTT Explorer, expand `gateway` to see the topics for each device.

Use `OTA_DEVICE_REPOS` to map each ESP32 ID printed by its Serial Monitor to that device's GitHub repository. The server checks each repository's latest GitHub Release independently and publishes its `firmware.bin` only to that device's OTA topic. Do not map a device to a repository whose firmware has a different board, network, or partition configuration. Firmware that only listens to the shared topic must be migrated over USB before it can receive a per-device announcement. Restart the Python server after changing the mapping. The default GitHub check interval is five minutes; this checks Releases, not every commit pushed to a branch.

## Start the local OTA server

Open a second PowerShell window in the project folder:

```powershell
py -m venv .venv
.\.venv\Scripts\Activate.ps1
python -m pip install -r requirements.txt
$env:OTA_SERVER_IP = "192.168.211.251"
$env:OTA_MQTT_HOST = "192.168.211.251"
$env:OTA_MQTT_PORT = "8883"
$env:OTA_MQTT_TLS = "true"
$env:OTA_MQTT_CA_CERT = "C:\Program Files\Mosquitto\certs\ca.crt"
# For an authenticated broker, set these too; omit/leave empty for anonymous Mosquitto.
$env:OTA_MQTT_USERNAME = ""
$env:OTA_MQTT_PASSWORD = ""
$env:OTA_DEVICE_REPOS = "esp32-ota-14335c524234=ahmadfirmanALV/ESP-OTA;esp32-ota-082c8e215788=ahmadfirmanALV/ESP-OTA2"
python .\ota_server.py
```

Change the example certificate path if needed. For an external broker, use its hostname and CA certificate settings. Each mapped repository must have a latest GitHub Release containing `firmware.bin` and `ota_manifest.json`; each ESP32 must receive firmware built for its own configuration. Leave this server running. The health endpoint at `http://<server-ip>:5000/health` lists the configured repo-to-device mappings.

## Flash the ESP32 the first time

Upload the migration firmware once over USB because firmware that only subscribes to the shared OTA topic cannot receive a per-device topic. If this is also the first time applying the custom partition table, erase flash once before upload; this clears saved Wi-Fi credentials, which you will need to enter again through the `ESP32-Setup` Wi-Fi portal.

From the project folder, using the correct COM port:

```powershell
pio run -e doit_devkit_v1 -t erase --upload-port COM4
pio run -e doit_devkit_v1 -t upload --upload-port COM4
pio device monitor --port COM4
```

If the ESP32 opens `ESP32-Setup`, connect to that access point and enter the Wi-Fi credentials. Check the serial monitor for Wi-Fi and MQTT connection messages. Confirm that `OTA_SERVER_IP` matches the computer serving firmware and `MQTT_BROKER_HOST` matches the broker endpoint.

## Build and release a firmware version

Commit and push the workflow and firmware code to `main` first. Then use a new tag that has not been used before:

```powershell
git add .
git commit -m "Prepare central OTA"
git push origin main
git tag v1.0.2
git push origin v1.0.2
```

Replace `v1.0.2` with the next unused version. A tag push starts the GitHub Actions workflow; a normal push to `main` does not. In GitHub, check **Actions** for a successful build and **Releases** for `firmware.bin` and `ota_manifest.json`. The server ignores older releases that lack the OTA manifest. After a compatible Release exists, the local server checks GitHub every five minutes and publishes its retained MQTT message. A device that reconnects later will still receive the latest announcement.

## OTA and recovery notes

- The server release must contain an ESP32 application binary named exactly `firmware.bin`.
- The MQTT message contains the Release tag, local download URL, and MD5 checksum. MD5 detects transfer corruption; it does not authenticate firmware.
- OTA rollback depends on the Arduino-ESP32 bootloader supporting rollback and on the two OTA application slots in `partitions.csv`. The new firmware is confirmed after Wi-Fi and MQTT connect; it rolls back if that check times out or the new image crashes before validation.
- ArduinoOTA remains available as a local upload fallback. It currently has no password; set one before using it on an untrusted LAN.
- Keep Mosquitto and the Python server running at the location where the ESP32 is installed.
