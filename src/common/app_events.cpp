#include "app_events.h"

EventGroupHandle_t gAppEvents = nullptr;

bool appEventsInit() {
  gAppEvents = xEventGroupCreate();
  return gAppEvents != nullptr;
}

void setWifiConnected(bool connected) {
  if (gAppEvents == nullptr) {
    return;
  }

  if (connected) {
    xEventGroupSetBits(gAppEvents, WIFI_CONNECTED_BIT);
  } else {
    xEventGroupClearBits(gAppEvents, WIFI_CONNECTED_BIT);
  }
}
