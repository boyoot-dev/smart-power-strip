#include <Arduino.h>

#include "config.h"
#include "console.h"
#include "control.h"
#include "events.h"
#include "firmware_version.h"
#include "metering.h"
#include "net.h"
#include "outlets.h"
#include "pins.h"
#include "thermal.h"
#include "web.h"

// Task layout (PLAN.md "Firmware Architecture"):
//   core 1: metering    - ADC sampling + RMS/power math, owns the core so WiFi never
//                         adds jitter to the sample timing
//   core 0: control     - switches, compressor-delay release, status LED (prio 4)
//           net         - WiFi, NTP, ThingsBoard telemetry/RPC/OTA (prio 2)
//           async_tcp   - web UI (pinned by build flag)
//           webpush     - local page WebSocket pushes (prio 3)
//           thermal     - DS18B20 overheat protection (prio 1)
//           console     - USB serial provisioning (prio 1)
// The Arduino loop task has nothing to do and deletes itself.

void setup() {
  // Relays off before anything else so outlets never click during boot.
  pinMode(kPinRelay1, OUTPUT);
  pinMode(kPinRelay2, OUTPUT);
  digitalWrite(kPinRelay1, LOW);
  digitalWrite(kPinRelay2, LOW);

  Serial.begin(115200);
  Serial.printf("\n%s v%s\n", kFwTitle, kFwVersion);

  Config::begin();
  Events::begin();
  Outlets::begin();
  Metering::begin();
  Control::begin();
  Thermal::begin();
  Net::begin();
  Web::begin();
  Console::begin();
}

void loop() { vTaskDelete(nullptr); }
