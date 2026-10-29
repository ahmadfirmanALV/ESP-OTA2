#include "task_watchdog.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "app_config.h"

namespace {

constexpr uint8_t kComponentCount =
    static_cast<uint8_t>(WatchedComponent::Count);
const char* const kComponentNames[kComponentCount] = {
    "main_loop", "wifi_task", "led_task", "ota_task"};

portMUX_TYPE sWatchdogMux = portMUX_INITIALIZER_UNLOCKED;
uint32_t sLastHeartbeatMs[kComponentCount] = {};
uint32_t sTimeoutMs[kComponentCount] = {};
bool sComponentActive[kComponentCount] = {};
TaskHandle_t sMonitorTask = nullptr;

bool componentIndex(WatchedComponent component, uint8_t& index) {
  index = static_cast<uint8_t>(component);
  return index < kComponentCount;
}

void monitorTasks(void* parameter) {
  (void)parameter;

  for (;;) {
    const uint32_t now = millis();
    int staleIndex = -1;
    uint32_t staleElapsedMs = 0;
    uint32_t staleTimeoutMs = 0;

    portENTER_CRITICAL(&sWatchdogMux);
    for (uint8_t i = 0; i < kComponentCount; ++i) {
      if (!sComponentActive[i]) {
        continue;
      }

      const uint32_t elapsedMs = now - sLastHeartbeatMs[i];
      if (elapsedMs >= sTimeoutMs[i]) {
        staleIndex = i;
        staleElapsedMs = elapsedMs;
        staleTimeoutMs = sTimeoutMs[i];
        break;
      }
    }
    portEXIT_CRITICAL(&sWatchdogMux);

    if (staleIndex >= 0) {
      Serial.printf("WATCHDOG: %s heartbeat timeout %lu ms (limit %lu ms); restarting ESP32.\n",
                    kComponentNames[staleIndex],
                    static_cast<unsigned long>(staleElapsedMs),
                    static_cast<unsigned long>(staleTimeoutMs));
      vTaskDelay(pdMS_TO_TICKS(100));
      ESP.restart();
      vTaskDelete(nullptr);
    }

    vTaskDelay(pdMS_TO_TICKS(AppConfig::TASK_WATCHDOG_CHECK_INTERVAL_MS));
  }
}

}  // namespace

bool taskWatchdogInit() {
  if (sMonitorTask != nullptr) {
    return true;
  }

  const BaseType_t result = xTaskCreatePinnedToCore(
      monitorTasks,
      "task_watchdog",
      AppConfig::TASK_WATCHDOG_STACK_BYTES,
      nullptr,
      AppConfig::TASK_WATCHDOG_PRIORITY,
      &sMonitorTask,
      0);
  if (result != pdPASS) {
    sMonitorTask = nullptr;
    return false;
  }
  return true;
}

void taskWatchdogRegister(WatchedComponent component, uint32_t timeoutMs) {
  uint8_t index = 0;
  if (!componentIndex(component, index) || timeoutMs == 0) {
    return;
  }

  const uint32_t now = millis();
  portENTER_CRITICAL(&sWatchdogMux);
  sLastHeartbeatMs[index] = now;
  sTimeoutMs[index] = timeoutMs;
  sComponentActive[index] = true;
  portEXIT_CRITICAL(&sWatchdogMux);
}

void taskWatchdogHeartbeat(WatchedComponent component) {
  uint8_t index = 0;
  if (!componentIndex(component, index)) {
    return;
  }

  const uint32_t now = millis();
  portENTER_CRITICAL(&sWatchdogMux);
  if (sComponentActive[index]) {
    sLastHeartbeatMs[index] = now;
  }
  portEXIT_CRITICAL(&sWatchdogMux);
}

void taskWatchdogUnregister(WatchedComponent component) {
  uint8_t index = 0;
  if (!componentIndex(component, index)) {
    return;
  }

  portENTER_CRITICAL(&sWatchdogMux);
  sComponentActive[index] = false;
  portEXIT_CRITICAL(&sWatchdogMux);
}
