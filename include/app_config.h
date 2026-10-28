#pragma once

#include <Arduino.h>

#if __has_include("ota_secrets.h")
#include "ota_secrets.h"
#endif

#ifndef OTA_MQTT_USERNAME
#define OTA_MQTT_USERNAME ""
#endif

#ifndef OTA_MQTT_PASSWORD
#define OTA_MQTT_PASSWORD ""
#endif

#ifndef OTA_MQTT_HOST
#define OTA_MQTT_HOST "192.168.211.251"
#endif

#ifndef OTA_MQTT_ROOT_CA_DEFINED
#define OTA_MQTT_ROOT_CA_DEFINED 0
#endif

namespace AppConfig {

constexpr uint8_t LED_PIN = 2; // LED onboard D2 / GPIO2 pada DOIT ESP32 DEVKIT V1

// WiFiManager membuat access point ini saat ESP32 belum memiliki WiFi tersimpan.
constexpr char WIFI_AP_NAME[] = "ESP32-Setup";
constexpr char WIFI_AP_PASSWORD[] = "12345678";
constexpr uint16_t WIFI_PORTAL_TIMEOUT_SECONDS = 180;

// IP perangkat kedua; server laptop memakai 192.168.211.251.
constexpr bool USE_STATIC_IP = true;

// Konfigurasi IP statis (hanya aktif jika USE_STATIC_IP = true).
constexpr uint8_t WIFI_STATIC_IP[] = {192, 168, 211, 252};
constexpr uint8_t WIFI_GATEWAY[] = {192, 168, 211, 15};
constexpr uint8_t WIFI_SUBNET[] = {255, 255, 255, 0};
constexpr uint8_t WIFI_DNS[] = {8, 8, 8, 8};

// Beri hostname berbeda pada setiap ESP agar OTA/mDNS tidak saling bentrok.
// Password dikosongkan ("") agar sistem OTA tidak memerlukan password.
constexpr char OTA_HOSTNAME[] = "esp32-ota-02";
constexpr char OTA_DEVICE_ID_PREFIX[] = "esp32-ota-";
constexpr char OTA_PASSWORD[] = "";

// Alamat HTTP server firmware lokal; harus dapat dijangkau ESP32 di LAN.
constexpr uint8_t OTA_SERVER_IP[] = {192, 168, 211, 251};
// Alamat broker boleh berupa IP lokal atau hostname broker eksternal.
constexpr char MQTT_BROKER_HOST[] = OTA_MQTT_HOST;
constexpr uint16_t MQTT_PORT = 8883;
constexpr uint16_t OTA_HTTP_PORT = 5000;
constexpr char MQTT_TOPIC_OTA_BROADCAST[] = "gateway/ota/update";
constexpr char MQTT_TOPIC_OTA_DEVICE_PREFIX[] = "gateway/ota/update/";
constexpr char MQTT_TOPIC_STATUS_PREFIX[] = "gateway/status/";
constexpr char MQTT_TOPIC_DUMMY_PREFIX[] = "gateway/dummy/";
constexpr char MQTT_TOPIC_DUMMY_SUFFIX[] = "/random";
constexpr char MQTT_USERNAME[] = OTA_MQTT_USERNAME;
constexpr char MQTT_PASSWORD[] = OTA_MQTT_PASSWORD;
#if OTA_MQTT_ROOT_CA_DEFINED
constexpr const char* MQTT_ROOT_CA_CERT = OTA_MQTT_ROOT_CA;
#else
constexpr const char* MQTT_ROOT_CA_CERT = "";
#endif
constexpr uint32_t MQTT_RECONNECT_INTERVAL_MS = 5000;
constexpr uint32_t MQTT_DUMMY_PUBLISH_INTERVAL_MS = 5000;
constexpr uint32_t OTA_RETRY_INTERVAL_MS = 30000;
constexpr uint32_t OTA_VALIDATION_TIMEOUT_MS = 240000;

constexpr bool BLINK_MODE = false;
constexpr uint32_t BLINK_INTERVAL_MS = 500;

// Ubah ke true hanya untuk satu kali upload jika ingin menghapus kredensial WiFi
// yang tersimpan. Kembalikan ke false dan upload ulang setelah kredensial terhapus.
constexpr bool RESET_WIFI_SETTINGS_ON_BOOT = false;

constexpr uint32_t WIFI_TASK_STACK_BYTES = 8192;
constexpr uint32_t OTA_TASK_STACK_BYTES = 8192;
constexpr uint32_t LED_TASK_STACK_BYTES = 2048;

}  // namespace AppConfig
