#pragma once

#include <Arduino.h>

// Core-0 network task: WiFi STA with a fallback setup AP, NTP, and ThingsBoard via
// TbLink (telemetry, RPC, OTA). Everything that can block on the network lives here.

enum class NetState : uint8_t { kSetupAp, kWifi, kCloud };

namespace Net {

void begin();
NetState state();
String apSsid();

// Applies changed WiFi/ThingsBoard settings by rebooting after a short delay, so the
// web response gets out first.
void scheduleRestart(uint32_t delay_ms = 1500);

}  // namespace Net
