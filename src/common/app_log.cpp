#include "app_log.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <cstdarg>
#include <cstdio>

namespace {

SemaphoreHandle_t sLogMutex = nullptr;

}  // namespace

bool appLogInit() {
  sLogMutex = xSemaphoreCreateMutex();
  return sLogMutex != nullptr;
}

void appLog(const char* message) {
  if (sLogMutex != nullptr) {
    xSemaphoreTake(sLogMutex, portMAX_DELAY);
  }

  Serial.println(message != nullptr ? message : "");

  if (sLogMutex != nullptr) {
    xSemaphoreGive(sLogMutex);
  }
}

void appLogf(const char* format, ...) {
  char message[192];
  va_list args;
  va_start(args, format);
  vsnprintf(message, sizeof(message), format, args);
  va_end(args);

  appLog(message);
}
