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
- `include/ota_secrets.h.example`: template for optional MQTT credentials; the real `ota_secrets.h` is ignored by Git.

## Configure the LAN

The laptop/server is configured at `192.168.211.251`, with gateway `192.168.211.15`, subnet `255.255.255.0`, and DNS `8.8.8.8`. The ESP32 uses `192.168.211.252` with the same gateway and DNS; make sure `.252` is unused and reserve it in the router so DHCP will not assign it to another device. The laptop and ESP32 must connect to the same LAN. After connecting laptop Wi-Fi, verify `ipconfig` shows `192.168.211.251` on that adapter; a static setting alone does not connect a disconnected adapter.

`OTA_SERVER_IP` in `include/app_config.h` and the server environment variable below are set to `192.168.211.251` to match the laptop's static address.

The broker address is a separate setting: `MQTT_BROKER_HOST` in `include/app_config.h`. It defaults to `192.168.211.251` for Mosquitto running on this laptop. For a hosted broker, set it to the broker's hostname and port; the Python server must use the same broker endpoint. The ESP32 still needs `OTA_SERVER_IP` because firmware binaries are served by the local laptop over HTTP.

For a local broker, allow inbound TCP ports `1883` (MQTT) and `5000` (firmware download) through the computer's firewall on the private LAN. For a hosted broker, the laptop and ESP32 need outbound access to that broker, while port `5000` must still be reachable from the ESP32. The sample Mosquitto configuration allows anonymous MQTT clients, so use it only on a trusted private network and do not forward it to the internet.

If the broker requires a username and password, copy `include/ota_secrets.h.example` to `include/ota_secrets.h` and enter the credentials there for a local build. The real file is excluded from Git. Add GitHub Actions repository secrets named `OTA_MQTT_USERNAME` and `OTA_MQTT_PASSWORD` so Release builds contain the same credentials. Set matching PowerShell environment variables `OTA_MQTT_USERNAME` and `OTA_MQTT_PASSWORD` for `ota_server.py`. If the broker is anonymous, leave all four values empty. The current ESP32 client uses plain MQTT over TCP; a broker that requires TLS/port 8883 needs an additional TLS configuration.

## Start Mosquitto on Windows

Install Mosquitto, then open a PowerShell window in this project folder and run:

```powershell
mosquitto -c .\mosquitto.conf.example -v
```

Keep this window open. If `mosquitto` is not recognized, run `mosquitto.exe` using its installation path.
If the Mosquitto Windows service is already using port `1883`, stop that service or configure the service to use this file instead; do not start a second broker on the same port.

## Publish dummy MQTT values

This optional publisher sends a random integer to `gateway/dummy/value` every five seconds. In MQTT Explorer, connect to the same broker and subscribe to that topic. MQTT topics do not need to be created in advance; they appear when the first message is published.

Install the project dependencies once, then configure the connection in PowerShell to match MQTT Explorer. For the local Mosquitto example above:

```powershell
python -m pip install -r requirements.txt
$env:OTA_MQTT_HOST = "127.0.0.1"
$env:OTA_MQTT_PORT = "1883"
$env:OTA_MQTT_TLS = "false"
$env:OTA_MQTT_USERNAME = ""
$env:OTA_MQTT_PASSWORD = ""
python .\mqtt_dummy_publisher.py
```

For the selected MQTT Explorer connection in the screenshot (`localhost`, port `8883`, TLS enabled), use `127.0.0.1`, port `8883`, and set `$env:OTA_MQTT_TLS = "true"`. If that broker requires login, set the same username and password in the two PowerShell variables. If you use `test.mosquitto.org`, set the host to `test.mosquitto.org`, port to `1883`, and TLS to `false`. The laptop's `192.168.2.4` address is only needed by other devices, such as the ESP32, to reach a broker/server running on this laptop; a script running on the laptop itself can use `127.0.0.1`.

## Start the local OTA server

Open a second PowerShell window in the project folder:

```powershell
py -m venv .venv
.\.venv\Scripts\Activate.ps1
python -m pip install -r requirements.txt
$env:OTA_GITHUB_REPO = "ahmadfirmanALV/ESP-OTA2"
$env:OTA_SERVER_IP = "192.168.211.251"
$env:OTA_MQTT_HOST = "127.0.0.1"
# For an authenticated broker, set these too; omit/leave empty for anonymous Mosquitto.
$env:OTA_MQTT_USERNAME = ""
$env:OTA_MQTT_PASSWORD = ""
python .\ota_server.py
```

Change the example IP to the server address configured above. For an external broker, set `OTA_MQTT_HOST` to its hostname instead of `127.0.0.1`. Leave this server running. The health endpoint should respond at `http://<server-ip>:5000/health`.

## Flash the ESP32 the first time

The firmware currently on the ESP32 does not understand MQTT OTA. Install this updated firmware once over USB before expecting server updates. The partition table has changed, so erase the ESP32 flash once before the first upload; this also clears saved Wi-Fi credentials, which you will need to enter again through the `ESP32-Setup` Wi-Fi portal.

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
