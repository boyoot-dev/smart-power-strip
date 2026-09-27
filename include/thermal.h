#pragma once

#include <Arduino.h>

#include "config.h"

// Overheat protection from DS18B20 probes on one 1-Wire bus (GPIO 32, PLAN.md R4).
// Runs in its own low-priority core-0 task: a conversion takes 750 ms and bit-banged
// 1-Wire must never hold up the switches or the metering on core 1.
//
// Probes found on the bus are registered automatically (ROM code saved to NVS). Once at
// least one probe is registered the protection is armed:
//   - any probe >= trip_c                         -> kTripped, all outlets off + locked
//   - a registered probe unreadable for ~6 s      -> kFault,   all outlets off + locked
//   - every probe readable and < reset_c again    -> kOk, lock released (outlets stay
//                                                    off until someone turns them on)

enum class ThermalState : uint8_t { kDisarmed, kOk, kTripped, kFault };

struct ProbeReading {
  bool used;       // slot holds a registered probe
  bool ok;         // last read succeeded
  float temp_c;
  char id[17];     // ROM code as hex
  char name[17];
};

struct ThermalStatus {
  ThermalState state;
  float max_c;
  ProbeReading probes[kMaxProbes];
};

namespace Thermal {

void begin();
ThermalStatus status();
const char* stateName(ThermalState s);

// Probe management from the web page; `id` is the ROM code as 16 hex chars.
bool rename(const char* id, const char* name);
bool forget(const char* id);  // e.g. after replacing a probe
void setLimits(float trip_c, float reset_c);

}  // namespace Thermal
