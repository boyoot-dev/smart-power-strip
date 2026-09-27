#pragma once

#include <stdint.h>

#include "config.h"

// Runs alone on core 1 (PLAN.md "Metering approach"): alternates ~10 mains cycles of
// V+I1 sample pairs, then V+I2, so WiFi/web/MQTT work on core 0 never adds jitter.

struct OutletReading {
  float irms;     // A
  float power;    // W (real)
  float va;       // VA (apparent)
  float pf;
  double kwh;
};

struct Reading {
  float vrms;
  float freq;
  OutletReading out[kOutletCount];
  float total_irms;
  float total_power;
  uint32_t windows;  // increments per completed window; 0 = no data yet
  uint16_t pairs;    // sample pairs in the last window (diagnostic)
};

namespace Metering {

void begin();  // starts the task pinned to core 1
Reading latest();
void resetEnergy(int outlet);

}  // namespace Metering
