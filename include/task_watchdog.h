#pragma once

#include <stdint.h>

enum class WatchedComponent : uint8_t {
  MainLoop = 0,
  Wifi = 1,
  Led = 2,
  Ota = 3,
  Count = 4,
};

bool taskWatchdogInit();
void taskWatchdogRegister(WatchedComponent component, uint32_t timeoutMs);
void taskWatchdogHeartbeat(WatchedComponent component);
void taskWatchdogUnregister(WatchedComponent component);
