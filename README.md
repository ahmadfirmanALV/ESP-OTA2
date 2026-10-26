# ESP32 OTA WiFiManager — PlatformIO + FreeRTOS

Project ini memigrasikan sketch `esp32_ota_wifimanager.ino` ke PlatformIO. Firmware menggunakan framework Arduino untuk ESP32, dengan FreeRTOS bawaan ESP32 Arduino Core. Arduino IDE tidak diperlukan untuk build, upload, atau serial monitor.

## Fungsi program

- Menghubungkan ESP32 ke WiFi yang tersimpan. Jika belum ada kredensial, WiFiManager membuat access point `ESP32-Setup` dengan password `12345678`; buka `http://192.168.4.1` untuk mengatur WiFi.
- Menyediakan pembaruan firmware melalui ArduinoOTA tanpa password (hostname perangkat kedua: `esp32-ota-02`).
- Konfigurasi proyek ini memakai IP statis `192.168.211.251` untuk perangkat kedua, dengan gateway `192.168.211.15` dan subnet `/24`. Perangkat pertama tetap memakai `192.168.211.107`.
- Men-toggle LED onboard D2 (GPIO2) setiap 500 ms melalui task LED FreeRTOS; firmware tidak menggunakan LED eksternal.
- Jika portal konfigurasi WiFi tidak berhasil dalam 180 detik, ESP32 restart, sesuai perilaku sketch awal.

## Struktur proyek

```text
.
├── .vscode/
│   └── extensions.json
├── include/
│   ├── app_config.h
│   ├── app_events.h
│   └── app_log.h
├── legacy/
│   └── esp32_ota_wifimanager.ino
├── src/
│   ├── common/
│   │   ├── app_events.cpp
│   │   └── app_log.cpp
│   ├── tasks/
│   │   ├── led/
│   │   ├── ota/
│   │   └── wifi/
│   └── main.cpp
├── platformio.ini
└── README.md
```

Sketch asli disimpan di `legacy/esp32_ota_wifimanager.ino` untuk referensi; PlatformIO hanya mengompilasi sumber di `src/`.

## Alur program

Pada sketch awal, `setup()` menyiapkan Serial dan LED, lalu `WiFiManager.autoConnect()` mencoba WiFi tersimpan atau membuka portal konfigurasi. Setelah konek, ArduinoOTA disiapkan. `loop()` harus terus memanggil `ArduinoOTA.handle()` dan juga mengubah LED sesuai mode yang dipilih.

Di hasil migrasi, `main.cpp` hanya menyiapkan event group/logger dan memulai task. Task WiFi menjalankan proses koneksi yang dapat blocking tanpa menahan task lain. Setelah koneksi tersedia, event group membangunkan task OTA. Task LED berjalan sendiri dengan delay FreeRTOS periodik. Status koneksi disebarkan oleh event callback WiFi, dan akses ke Serial dilindungi mutex.

## Pemisahan task

| Task | Core | Tanggung jawab |
| --- | --- | --- |
| `wifi_task` | 0 | Menjalankan WiFiManager dan portal konfigurasi. Setelah tersambung, task ini selesai; event WiFi tetap memperbarui status koneksi. |
| `ota_task` | 1 | Menunggu event koneksi WiFi, menginisialisasi ArduinoOTA, lalu memanggil `ArduinoOTA.handle()` hanya dari task ini. |
| `led_task` | 1 | Mengendalikan GPIO2 secara independen dengan `vTaskDelayUntil()` saat mode kedip aktif. |

Event group FreeRTOS menyampaikan status WiFi dari callback jaringan ke task OTA. Callback disconnect menghapus bit koneksi sehingga task OTA berhenti melayani sampai koneksi pulih. Mutex logger mencegah pesan dari beberapa task bercampur. Task menggunakan prioritas, core, dan stack yang ditetapkan masing-masing; tidak ada loop task aplikasi yang memakai `delay()` blocking.

## Persiapan

1. Pasang PlatformIO IDE di VS Code atau PlatformIO Core CLI. Rekomendasi ekstensi VS Code tersedia di `.vscode/extensions.json`.
2. Sambungkan DOIT ESP32 DEVKIT V1 dengan USB. Environment PlatformIO bernama `doit_devkit_v1`; board ID PlatformIO-nya `esp32doit-devkit-v1`.
3. Edit konfigurasi pada `include/app_config.h` sebelum upload, terutama password access point konfigurasi dan password OTA.
4. OTA saat ini tidak memakai password. Jika `OTA_PASSWORD` diisi, tambahkan password yang sama sebagai `--auth=...` pada environment `doit_devkit_v1_ota` di `platformio.ini`.

Task VS Code tersedia lewat **Terminal → Run Task**. **PlatformIO: Build DOIT ESP32 DEVKIT V1** juga menjadi task build default (`Ctrl+Shift+B`); upload USB diarahkan ke COM4, dan tersedia task upload OTA serta serial monitor. Gunakan tombol **PlatformIO: Upload**, bukan tombol umum **Run Code**. Jika PlatformIO dipasang di lokasi berbeda dari instalasi standar Windows, sesuaikan path `pio.exe` di `.vscode/tasks.json`.

Dependency WiFiManager akan diunduh otomatis dari `platformio.ini`. WiFi dan ArduinoOTA disediakan oleh framework Arduino ESP32. Partisi `default.csv` dipilih untuk mendukung OTA pada DOIT ESP32 DEVKIT V1 4 MB.

## Build dan upload lewat USB

Jalankan dari folder proyek:

```sh
pio run -e doit_devkit_v1
pio run -e doit_devkit_v1 -t upload
pio device monitor
```

Jika ada beberapa port serial, tentukan port, misalnya `pio run -t upload --upload-port COM5`.

Upload pertama harus lewat USB. Setelah firmware berjalan dan ESP32 tersambung ke jaringan yang sama dengan komputer, serial monitor menampilkan alamat IP perangkat.

## Upload OTA dari PlatformIO

Environment `doit_devkit_v1_ota` menggunakan protokol `espota` dan mengunggah ke IP statis ESP kedua, `192.168.211.251`:

```sh
pio run -e doit_devkit_v1_ota -t upload
```

Setiap ESP harus memakai IP statis dan hostname OTA yang berbeda. Proyek ini ditujukan ke ESP kedua (`192.168.211.251`, `esp32-ota-02`). ESP pertama memakai `192.168.211.107` dan `esp32-ota-01`. Untuk perangkat tambahan, ubah `WIFI_STATIC_IP` serta `OTA_HOSTNAME` di `include/app_config.h`, lalu samakan `upload_port` di environment OTA dengan IP perangkat itu.

## Mengubah perilaku

- LED: ubah `BLINK_MODE` dan `BLINK_INTERVAL_MS` di `include/app_config.h`.
- WiFi portal: ubah `WIFI_AP_NAME`, `WIFI_AP_PASSWORD`, dan `WIFI_PORTAL_TIMEOUT_SECONDS`.
- IP jaringan: konfigurasi proyek saat ini untuk ESP kedua memakai `192.168.211.251`; ESP pertama memakai `192.168.211.107`. Untuk perangkat tambahan, ubah `WIFI_STATIC_IP` ke alamat unik. Gateway `192.168.211.15`, subnet `255.255.255.0`, dan DNS `192.168.211.15` mengikuti jaringan saat ini. Pastikan alamat tidak dipakai perangkat lain; sebaiknya reservasi alamat tersebut di router atau keluarkan dari rentang DHCP.
- OTA: gunakan hostname unik untuk tiap ESP, arahkan `upload_port` ke IP ESP yang sedang diunggah, dan samakan `OTA_PASSWORD` dengan `upload_flags` jika password OTA diaktifkan.
- Hapus WiFi tersimpan: set `RESET_WIFI_SETTINGS_ON_BOOT = true`, upload lewat USB, lalu kembalikan ke `false` dan upload sekali lagi. Jangan biarkan bernilai `true` untuk penggunaan normal.

## Catatan migrasi

Sketch lama telah diarsipkan dan tidak dipakai oleh build PlatformIO. Arduino IDE tidak lagi menjadi bagian dari alur proyek. Proyek menargetkan DOIT ESP32 DEVKIT V1 melalui board ID `esp32doit-devkit-v1`.
