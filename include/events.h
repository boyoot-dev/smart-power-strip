#pragma once

#include <stdint.h>
#include <time.h>

// Relay-change event log (PLAN.md R6). RAM ring for now; LittleFS persistence is M4.

enum class Source : uint8_t {
  kBoot, kButton, kWeb, kThingsBoard, kRule, kOvercurrent, kVoltage, kCompressorRelease,
  kOverheat, kSensorFault
};

const char* sourceName(Source s);

struct Event {
  time_t time;        // epoch seconds, or uptime seconds when !synced
  bool synced;        // false = stamped before NTP sync
  uint8_t outlet;     // 0-based
  bool on;
  Source source;
};

namespace Events {

void begin();

// Called by Outlets on every relay change. Thread-safe.
void record(int outlet, bool on, Source source);

// Copies up to `max` newest events into `out`, newest first. Thread-safe.
size_t latest(Event* out, size_t max);

// Total events recorded since boot; changes whenever a new event lands. Thread-safe.
uint32_t seq();

// Pops the next event not yet sent to ThingsBoard. Thread-safe.
bool nextUnsent(Event* out);

}  // namespace Events
