#include "ota_task.h"

#include <Arduino.h>
#include <ArduinoOTA.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>

#include "app_config.h"
#include "app_events.h"
#include "app_log.h"

namespace {

void configureArduinoOta() {
  ArduinoOTA.setHostname(AppConfig::OTA_HOSTNAME);
  if (AppConfig::OTA_PASSWORD[0] != '\0') {
    ArduinoOTA.setPassword(AppConfig::OTA_PASSWORD);
  }

  ArduinoOTA.onStart([]() {
    const char* type = ArduinoOTA.getCommand() == U_FLASH ? "sketch" : "filesystem";
    appLogf("OTA mulai: %s", type);
  });

  ArduinoOTA.onEnd([]() {
    appLog("OTA selesai; perangkat akan restart.");
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    if (total == 0) {
      return;
    }

    const unsigned int percent = static_cast<unsigned int>(
        (static_cast<uint64_t>(progress) * 100U) / total);
    static unsigned int lastPercent = 101;
    if (percent != lastPercent) {
      lastPercent = percent;
      appLogf("OTA progress: %u%%", percent);
    }
  });

  ArduinoOTA.onError([](ota_error_t error) {
    switch (error) {
      case OTA_AUTH_ERROR:
        appLog("OTA error: autentikasi gagal.");
        break;
      case OTA_BEGIN_ERROR:
        appLog("OTA error: proses begin gagal.");
        break;
      case OTA_CONNECT_ERROR:
        appLog("OTA error: koneksi gagal.");
        break;
      case OTA_RECEIVE_ERROR:
        appLog("OTA error: data gagal diterima.");
        break;
      case OTA_END_ERROR:
        appLog("OTA error: proses akhir gagal.");
        break;
      default:
        appLogf("OTA error dengan kode %u.", static_cast<unsigned int>(error));
        break;
    }
  });

  ArduinoOTA.begin();
  appLog("ArduinoOTA siap menerima upload.");
}

void otaTask(void* parameter) {
  (void)parameter;
  bool otaStarted = false;

  for (;;) {
    xEventGroupWaitBits(
        gAppEvents,
        WIFI_CONNECTED_BIT,
        pdFALSE,
        pdTRUE,
        portMAX_DELAY);

    if (!otaStarted) {
      configureArduinoOta();
      otaStarted = true;
    }

    while ((xEventGroupGetBits(gAppEvents) & WIFI_CONNECTED_BIT) != 0) {
      // ArduinoOTA hanya dipanggil dari task ini agar tidak ada pemanggilan bersamaan.
      ArduinoOTA.handle();
      vTaskDelay(pdMS_TO_TICKS(10));
    }

    appLog("WiFi terputus; task OTA menunggu koneksi kembali.");
  }
}

}  // namespace

bool otaTaskStart() {
  return xTaskCreatePinnedToCore(
             otaTask,
             "ota_task",
             AppConfig::OTA_TASK_STACK_BYTES,
             nullptr,
             2,
             nullptr,
             1) == pdPASS;
}
