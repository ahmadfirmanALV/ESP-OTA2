#include "ota_task.h"

#include <Arduino.h>
#include <ArduinoOTA.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <Update.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_system.h>
#include <esp_ota_ops.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>

#include "app_config.h"
#include "app_events.h"
#include "app_log.h"
#include "app_version.h"
#include "task_watchdog.h"

namespace {

struct OtaRequest {
  char version[64] = {};
  char url[320] = {};
  char md5[33] = {};
  bool pending = false;
};

WiFiClientSecure mqttNetworkClient;
PubSubClient mqttClient(mqttNetworkClient);
OtaRequest queuedOtaRequest;
char otaDeviceId[32] = {};
char otaDeviceTopic[96] = {};
char mqttStatusTopic[96] = {};
char mqttCurrentVersionTopic[112] = {};
char mqttDummyTopic[112] = {};

void configureDeviceIdentity() {
  const uint64_t baseMac = ESP.getEfuseMac() & 0xFFFFFFFFFFFFULL;
  snprintf(otaDeviceId, sizeof(otaDeviceId), "%s%012llx",
           AppConfig::OTA_DEVICE_ID_PREFIX,
           static_cast<unsigned long long>(baseMac));
  snprintf(otaDeviceTopic, sizeof(otaDeviceTopic), "%s%s",
           AppConfig::MQTT_TOPIC_OTA_DEVICE_PREFIX, otaDeviceId);
  snprintf(mqttStatusTopic, sizeof(mqttStatusTopic), "%s%s",
           AppConfig::MQTT_TOPIC_STATUS_PREFIX, otaDeviceId);
  snprintf(mqttCurrentVersionTopic, sizeof(mqttCurrentVersionTopic), "%s%s",
           AppConfig::MQTT_TOPIC_VERSION_CURRENT_PREFIX, otaDeviceId);
  snprintf(mqttDummyTopic, sizeof(mqttDummyTopic), "%s%s%s",
           AppConfig::MQTT_TOPIC_DUMMY_PREFIX, otaDeviceId,
           AppConfig::MQTT_TOPIC_DUMMY_SUFFIX);
  appLogf("ESP device ID: %s", otaDeviceId);
  appLogf("MQTT OTA topic: %s", otaDeviceTopic);
}

IPAddress otaServerAddress() {
  return IPAddress(
      AppConfig::OTA_SERVER_IP[0], AppConfig::OTA_SERVER_IP[1],
      AppConfig::OTA_SERVER_IP[2], AppConfig::OTA_SERVER_IP[3]);
}

bool isRunningImagePendingVerify() {
  const esp_partition_t* running = esp_ota_get_running_partition();
  if (running == nullptr) {
    return false;
  }

  esp_ota_img_states_t state;
  return esp_ota_get_state_partition(running, &state) == ESP_OK &&
         state == ESP_OTA_IMG_PENDING_VERIFY;
}

bool markRunningImageValidIfPending() {
  if (!isRunningImagePendingVerify()) {
    return true;
  }

  const esp_err_t result = esp_ota_mark_app_valid_cancel_rollback();
  if (result == ESP_OK) {
    appLog("Firmware baru lolos verifikasi WiFi dan MQTT.");
    return true;
  }

  appLogf("Gagal mengesahkan firmware OTA, error: %d", static_cast<int>(result));
  return false;
}

void recordBootedVersion() {
  Preferences preferences;
  if (!preferences.begin("ota-state", false)) {
    appLog("NVS OTA tidak dapat dibuka untuk memeriksa versi sebelumnya.");
    return;
  }

  String pendingVersion;
  if (preferences.isKey("pending")) {
    pendingVersion = preferences.getString("pending", "");
  }
  if (pendingVersion.length() > 0 && pendingVersion != APP_VERSION) {
    preferences.putString("failed", pendingVersion);
    preferences.remove("pending");
    appLogf("Rilis OTA %s gagal boot dan ditandai agar tidak dicoba ulang.",
            pendingVersion.c_str());
  }
  preferences.end();
}

bool isVersionMarkedFailed(const char* version) {
  Preferences preferences;
  if (!preferences.begin("ota-state", true)) {
    return false;
  }

  String failedVersion;
  if (preferences.isKey("failed")) {
    failedVersion = preferences.getString("failed", "");
  }
  preferences.end();
  return failedVersion == version;
}

void savePendingVersion(const char* version) {
  Preferences preferences;
  if (!preferences.begin("ota-state", false)) {
    appLog("NVS OTA tidak dapat dibuka untuk menyimpan versi target.");
    return;
  }

  preferences.putString("pending", version);
  preferences.end();
}

void clearPendingVersion(const char* version) {
  Preferences preferences;
  if (!preferences.begin("ota-state", false)) {
    return;
  }

  if (preferences.isKey("pending") &&
      preferences.getString("pending", "") == version) {
    preferences.remove("pending");
  }
  preferences.end();
}

bool isAllowedFirmwareUrl(const char* url) {
  char expectedPrefix[96];
  snprintf(expectedPrefix, sizeof(expectedPrefix), "http://%u.%u.%u.%u:%u/download/",
           static_cast<unsigned int>(AppConfig::OTA_SERVER_IP[0]),
           static_cast<unsigned int>(AppConfig::OTA_SERVER_IP[1]),
           static_cast<unsigned int>(AppConfig::OTA_SERVER_IP[2]),
           static_cast<unsigned int>(AppConfig::OTA_SERVER_IP[3]),
           static_cast<unsigned int>(AppConfig::OTA_HTTP_PORT));
  return strncmp(url, expectedPrefix, strlen(expectedPrefix)) == 0;
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  if (strcmp(topic, AppConfig::MQTT_TOPIC_OTA_BROADCAST) != 0 &&
      strcmp(topic, otaDeviceTopic) != 0) {
    return;
  }

  StaticJsonDocument<512> doc;
  const DeserializationError error = deserializeJson(doc, payload, length);
  if (error) {
    appLogf("Pesan MQTT OTA bukan JSON yang valid: %s", error.c_str());
    return;
  }

  const char* version = doc["version"] | "";
  const char* url = doc["url"] | "";
  const char* md5 = doc["md5"] | "";
  const char* targetDeviceId = doc["target_device_id"] | "";

  if (targetDeviceId[0] != '\0' && strcmp(targetDeviceId, "*") != 0 &&
      strcmp(targetDeviceId, otaDeviceId) != 0) {
    appLogf("OTA untuk perangkat %s; ESP ini (%s) melewati pesan.",
            targetDeviceId, otaDeviceId);
    return;
  }

  if (version[0] == '\0' || url[0] == '\0' || strlen(md5) != 32 ||
      strlen(version) >= sizeof(queuedOtaRequest.version) ||
      strlen(url) >= sizeof(queuedOtaRequest.url)) {
    appLog("Pesan MQTT OTA harus berisi version, url, dan MD5 32 karakter.");
    return;
  }

  if (strcmp(version, APP_VERSION) == 0) {
    appLogf("Firmware sudah versi %s.", APP_VERSION);
    return;
  }

  if (isVersionMarkedFailed(version)) {
    appLogf("Rilis %s sebelumnya gagal boot; OTA otomatis dilewati.", version);
    return;
  }

  if (!isAllowedFirmwareUrl(url)) {
    appLog("URL OTA ditolak; URL harus berasal dari server lokal yang dikonfigurasi.");
    return;
  }

  strlcpy(queuedOtaRequest.version, version, sizeof(queuedOtaRequest.version));
  strlcpy(queuedOtaRequest.url, url, sizeof(queuedOtaRequest.url));
  strlcpy(queuedOtaRequest.md5, md5, sizeof(queuedOtaRequest.md5));
  queuedOtaRequest.pending = true;
  appLogf("Rilis OTA %s diterima; unduhan akan dimulai.", version);
}

bool connectMqtt() {
  bool connected = false;
  if (AppConfig::MQTT_USERNAME[0] == '\0') {
    connected = mqttClient.connect(otaDeviceId);
  } else {
    connected = mqttClient.connect(otaDeviceId, AppConfig::MQTT_USERNAME,
                                   AppConfig::MQTT_PASSWORD);
  }

  if (!connected) {
    appLogf("Koneksi MQTT ke %s:%u gagal, state: %d",
            AppConfig::MQTT_BROKER_HOST,
            static_cast<unsigned int>(AppConfig::MQTT_PORT),
            mqttClient.state());
    return false;
  }

  if (!mqttClient.subscribe(otaDeviceTopic, 1)) {
    appLogf("Subscribe topik OTA perangkat gagal: %s", otaDeviceTopic);
    mqttClient.disconnect();
    return false;
  }
  if (!mqttClient.subscribe(AppConfig::MQTT_TOPIC_OTA_BROADCAST, 1)) {
    appLog("Subscribe topic OTA broadcast gagal; topic perangkat tetap aktif.");
  }

  appLogf("Terhubung ke broker; ID=%s, OTA topic=%s",
          otaDeviceId, otaDeviceTopic);
  if (markRunningImageValidIfPending()) {
    clearPendingVersion(APP_VERSION);
  }
  mqttClient.publish(mqttStatusTopic, APP_VERSION, true);
  if (!mqttClient.publish(mqttCurrentVersionTopic, APP_VERSION, true)) {
    appLogf("Gagal mengirim versi aktif ke topic %s.", mqttCurrentVersionTopic);
  } else {
    appLogf("Versi firmware aktif: %s = %s",
            mqttCurrentVersionTopic, APP_VERSION);
  }
  return true;
}

void publishDummyValue() {
  const uint32_t value = esp_random() % 101U;
  char payload[4];
  snprintf(payload, sizeof(payload), "%u", static_cast<unsigned int>(value));

  if (mqttClient.publish(mqttDummyTopic, payload, false)) {
    appLogf("Dummy MQTT terkirim: %s = %s",
            mqttDummyTopic, payload);
  } else {
    appLog("Publish angka dummy ke MQTT gagal.");
  }
}

bool executeOta(const OtaRequest& request) {
  if (!isAllowedFirmwareUrl(request.url)) {
    appLog("URL OTA ditolak; URL harus berasal dari server lokal yang dikonfigurasi.");
    return false;
  }

  appLogf("Mengunduh firmware %s dari server lokal.", request.version);
  HTTPClient http;
  http.setTimeout(30000);
  if (!http.begin(request.url)) {
    appLog("HTTP client gagal membuka URL firmware.");
    return false;
  }

  const int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    appLogf("Unduhan firmware gagal, HTTP status: %d", httpCode);
    http.end();
    return false;
  }

  const int contentLength = http.getSize();
  if (contentLength <= 0) {
    appLog("Server firmware tidak mengirim Content-Length yang valid.");
    http.end();
    return false;
  }

  if (!Update.begin(static_cast<size_t>(contentLength))) {
    appLogf("Partisi OTA tidak cukup atau Update.begin gagal: %s",
            Update.errorString());
    http.end();
    return false;
  }

  if (!Update.setMD5(request.md5)) {
    appLog("Checksum MD5 dari pesan OTA tidak valid.");
    Update.abort();
    http.end();
    return false;
  }

  WiFiClient* stream = http.getStreamPtr();
  const size_t written = Update.writeStream(*stream);
  if (written != static_cast<size_t>(contentLength)) {
    appLogf("Firmware terunduh sebagian: %u dari %d byte.",
            static_cast<unsigned int>(written), contentLength);
    Update.abort();
    http.end();
    return false;
  }

  if (!Update.end() || !Update.isFinished()) {
    appLogf("Firmware gagal divalidasi saat Update.end(): %s",
            Update.errorString());
    http.end();
    return false;
  }

  savePendingVersion(request.version);
  http.end();
  appLogf("Firmware %s berhasil ditulis; ESP32 akan restart.", request.version);
  ESP.restart();
  return true;
}

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

bool configureMqtt() {
  if (AppConfig::MQTT_ROOT_CA_CERT[0] == '\0') {
    appLog("MQTT TLS belum siap: CA broker belum dikonfigurasi.");
    return false;
  }

  mqttNetworkClient.setCACert(AppConfig::MQTT_ROOT_CA_CERT);
  mqttClient.setServer(AppConfig::MQTT_BROKER_HOST, AppConfig::MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setBufferSize(512);
  mqttClient.setKeepAlive(30);
  mqttClient.setSocketTimeout(5);
  return true;
}

void otaTask(void* parameter) {
  (void)parameter;
  configureDeviceIdentity();
  bool otaStarted = false;
  bool mqttConfigured = false;
  bool mqttConfigurationChecked = false;
  bool pendingVerify = isRunningImagePendingVerify();
  uint32_t lastMqttAttempt = 0;
  uint32_t lastDummyPublishAt = 0;
  uint32_t nextOtaAttemptAt = 0;
  const uint32_t validationStartedAt = millis();
  recordBootedVersion();

  for (;;) {
    const bool wifiConnected =
        (xEventGroupGetBits(gAppEvents) & WIFI_CONNECTED_BIT) != 0;

    if (wifiConnected) {
      if (!otaStarted) {
        configureArduinoOta();
        otaStarted = true;
      }
      if (!mqttConfigurationChecked) {
        mqttConfigured = configureMqtt();
        mqttConfigurationChecked = true;
      }

      ArduinoOTA.handle();
      if (mqttConfigured) {
        if (mqttClient.connected()) {
          mqttClient.loop();
          if (millis() - lastDummyPublishAt >=
              AppConfig::MQTT_DUMMY_PUBLISH_INTERVAL_MS) {
            lastDummyPublishAt = millis();
            publishDummyValue();
          }
        } else if (millis() - lastMqttAttempt >=
                   AppConfig::MQTT_RECONNECT_INTERVAL_MS) {
          lastMqttAttempt = millis();
          if (connectMqtt()) {
            lastDummyPublishAt = millis();
          }
          if (!isRunningImagePendingVerify()) {
            pendingVerify = false;
          }
        }
      }

      if (queuedOtaRequest.pending &&
          static_cast<int32_t>(millis() - nextOtaAttemptAt) >= 0) {
        OtaRequest request = queuedOtaRequest;
        queuedOtaRequest.pending = false;
        if (!executeOta(request)) {
          queuedOtaRequest = request;
          queuedOtaRequest.pending = true;
          nextOtaAttemptAt = millis() + AppConfig::OTA_RETRY_INTERVAL_MS;
          appLog("OTA akan dicoba ulang setelah jeda 30 detik.");
        }
      }
    } else if (mqttClient.connected()) {
      mqttClient.disconnect();
      appLog("WiFi terputus; koneksi MQTT ditutup sementara.");
    }

    if (pendingVerify &&
        millis() - validationStartedAt >= AppConfig::OTA_VALIDATION_TIMEOUT_MS) {
      appLog("Verifikasi firmware baru timeout; rollback ke firmware sebelumnya.");
      const esp_err_t result = esp_ota_mark_app_invalid_rollback_and_reboot();
      if (result != ESP_OK) {
        appLogf("Rollback gagal, error: %d; ESP32 akan restart.",
                static_cast<int>(result));
      }
      vTaskDelay(pdMS_TO_TICKS(1000));
      ESP.restart();
    }

    taskWatchdogHeartbeat(WatchedComponent::Ota);
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

}  // namespace

bool otaTaskStart() {
  taskWatchdogRegister(WatchedComponent::Ota,
                       AppConfig::OTA_TASK_WATCHDOG_TIMEOUT_MS);
  const BaseType_t result = xTaskCreatePinnedToCore(
      otaTask,
      "ota_task",
      AppConfig::OTA_TASK_STACK_BYTES,
      nullptr,
      2,
      nullptr,
      1);
  if (result != pdPASS) {
    taskWatchdogUnregister(WatchedComponent::Ota);
    return false;
  }
  return true;
}

// Tunda verifikasi otomatis Arduino-ESP32 sampai WiFi dan MQTT lolos pemeriksaan.
extern "C" bool verifyRollbackLater() { return true; }
