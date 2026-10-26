#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

constexpr EventBits_t WIFI_CONNECTED_BIT = BIT0;

extern EventGroupHandle_t gAppEvents;

bool appEventsInit();
void setWifiConnected(bool connected);
