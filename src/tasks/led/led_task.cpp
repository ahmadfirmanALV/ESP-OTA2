#include "led_task.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "app_config.h"
#include "task_watchdog.h"

namespace {

void ledTask(void* parameter) {
  (void)parameter;

  pinMode(AppConfig::LED_PIN, OUTPUT);
  digitalWrite(AppConfig::LED_PIN, HIGH);

  if (!AppConfig::BLINK_MODE) {
    for (;;) {
      taskWatchdogHeartbeat(WatchedComponent::Led);
      vTaskDelay(pdMS_TO_TICKS(1000));
    }
  }

  bool ledState = false;
  TickType_t lastWakeTime = xTaskGetTickCount();
  const TickType_t interval = pdMS_TO_TICKS(AppConfig::BLINK_INTERVAL_MS);

  for (;;) {
    vTaskDelayUntil(&lastWakeTime, interval);
    ledState = !ledState;
    digitalWrite(AppConfig::LED_PIN, ledState ? HIGH : LOW);
    taskWatchdogHeartbeat(WatchedComponent::Led);
  }
}

}  // namespace

bool ledTaskStart() {
  taskWatchdogRegister(WatchedComponent::Led,
                       AppConfig::LED_TASK_WATCHDOG_TIMEOUT_MS);
  const BaseType_t result = xTaskCreatePinnedToCore(
      ledTask,
      "led_task",
      AppConfig::LED_TASK_STACK_BYTES,
      nullptr,
      1,
      nullptr,
      1);
  if (result != pdPASS) {
    taskWatchdogUnregister(WatchedComponent::Led);
    return false;
  }
  return true;
}
