#pragma once

#include <Arduino.h>

namespace AppConfig {

constexpr uint8_t LED_PIN = 2; // LED onboard D2 / GPIO2 pada DOIT ESP32 DEVKIT V1

// WiFiManager membuat access point ini saat ESP32 belum memiliki WiFi tersimpan.
constexpr char WIFI_AP_NAME[] = "ESP32-Setup";
constexpr char WIFI_AP_PASSWORD[] = "12345678";
constexpr uint16_t WIFI_PORTAL_TIMEOUT_SECONDS = 180;

// IP perangkat kedua; ESP pertama tetap memakai 192.168.211.107.
constexpr bool USE_STATIC_IP = true;

// Konfigurasi IP statis (hanya aktif jika USE_STATIC_IP = true).
constexpr uint8_t WIFI_STATIC_IP[] = {192, 168, 211, 251};
constexpr uint8_t WIFI_GATEWAY[] = {192, 168, 211, 15};
constexpr uint8_t WIFI_SUBNET[] = {255, 255, 255, 0};
constexpr uint8_t WIFI_DNS[] = {192, 168, 211, 15};

// Beri hostname berbeda pada setiap ESP agar OTA/mDNS tidak saling bentrok.
// Password dikosongkan ("") agar sistem OTA tidak memerlukan password.
constexpr char OTA_HOSTNAME[] = "esp32-ota-02";
constexpr char OTA_PASSWORD[] = "";

constexpr bool BLINK_MODE = false;
constexpr uint32_t BLINK_INTERVAL_MS = 500;

// Ubah ke true hanya untuk satu kali upload jika ingin menghapus kredensial WiFi
// yang tersimpan. Kembalikan ke false dan upload ulang setelah kredensial terhapus.
constexpr bool RESET_WIFI_SETTINGS_ON_BOOT = false;

constexpr uint32_t WIFI_TASK_STACK_BYTES = 8192;
constexpr uint32_t OTA_TASK_STACK_BYTES = 4096;
constexpr uint32_t LED_TASK_STACK_BYTES = 2048;

}  // namespace AppConfig
