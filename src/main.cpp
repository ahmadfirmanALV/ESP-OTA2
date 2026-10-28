#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "app_events.h"
#include "app_log.h"
#include "tasks/led/led_task.h"
#include "tasks/ota/ota_task.h"
#include "tasks/wifi/wifi_task.h"

void setup() {
  Serial.begin(115200);
  vTaskDelay(pdMS_TO_TICKS(500));

  if (!appLogInit()) {
    Serial.println("Gagal membuat mutex logger.");
    delay(1000);
    ESP.restart();
    return;
  }

  if (!appEventsInit()) {
    appLog("Gagal membuat event group FreeRTOS.");
    delay(1000);
    ESP.restart();
    return;
  }

  appLog("=== ESP32 PlatformIO + FreeRTOS + WiFiManager + OTA ===");

  // LED langsung aktif saat boot; task jaringan tidak menahan task ini.
  if (!ledTaskStart()) {
    appLog("Gagal memulai task LED.");
  }

  if (!wifiTaskStart()) {
    appLog("Gagal memulai task WiFi.");
    delay(1000);
    ESP.restart();
    return;
  }

  // Task OTA menunggu WIFI_CONNECTED_BIT sebelum memulai ArduinoOTA.
  if (!otaTaskStart()) {
    appLog("Gagal memulai task OTA.");
    delay(1000);
    ESP.restart();
    return;
  }
}

void loop() {
  // Seluruh pekerjaan aplikasi berjalan di task masing-masing.
  vTaskDelay(pdMS_TO_TICKS(1000));
}
