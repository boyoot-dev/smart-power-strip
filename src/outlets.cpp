#include "outlets.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "pins.h"

namespace {

const uint8_t kRelayPins[kOutletCount] = {kPinRelay1, kPinRelay2};

struct State {
  bool on = false;
  bool pending_on = false;
  Source pending_source = Source::kBoot;
  uint32_t off_since_ms = 0;  // boot counts as "just turned off" (mains-blip safety)
};

State state[kOutletCount];
SemaphoreHandle_t lock;
volatile bool lockout = false;

uint32_t minOffMs(int n) {
  const OutletConfig& c = Config::outlet(n);
  return c.compressor_protect ? static_cast<uint32_t>(c.min_off_s) * 1000 : 0;
}

uint32_t offElapsedMs(int n) { return millis() - state[n].off_since_ms; }

// Caller holds the lock.
void apply(int n, bool on, Source source) {
  State& s = state[n];
  s.pending_on = false;
  if (s.on == on) {
    return;
  }
  s.on = on;
  if (!on) {
    s.off_since_ms = millis();
  }
  digitalWrite(kRelayPins[n], on ? HIGH : LOW);  // modules jumpered to high-level trigger
  Config::saveLastState(n, on);
  Events::record(n, on, source);
}

}  // namespace

namespace Outlets {

void begin() {
  lock = xSemaphoreCreateRecursiveMutex();
  for (int n = 0; n < kOutletCount; n++) {
    pinMode(kRelayPins[n], OUTPUT);
    digitalWrite(kRelayPins[n], LOW);
    state[n].off_since_ms = millis();
  }
  for (int n = 0; n < kOutletCount; n++) {
    PowerOn p = Config::outlet(n).power_on;
    bool want = p == PowerOn::kOn || (p == PowerOn::kRestore && Config::lastState(n));
    if (want) {
      request(n, Want::kOn, Source::kBoot);
    }
  }
}

bool request(int n, Want want, Source source) {
  if (n < 0 || n >= kOutletCount) {
    return false;
  }
  xSemaphoreTakeRecursive(lock, portMAX_DELAY);
  State& s = state[n];
  bool on = want == Want::kToggle ? !(s.on || s.pending_on) : want == Want::kOn;
  if (on && lockout) {
    Serial.printf("out%d: turn-on refused, overheat protection active\n", n + 1);
  } else if (on && !s.on && offElapsedMs(n) < minOffMs(n)) {
    s.pending_on = true;
    s.pending_source = source;
    Serial.printf("out%d: compressor delay, on in %lus\n", n + 1,
                  (minOffMs(n) - offElapsedMs(n)) / 1000);
  } else {
    apply(n, on, source);
  }
  bool result = s.on || s.pending_on;
  xSemaphoreGiveRecursive(lock);
  return result;
}

void tick() {
  xSemaphoreTakeRecursive(lock, portMAX_DELAY);
  for (int n = 0; n < kOutletCount; n++) {
    if (state[n].pending_on && lockout) {
      state[n].pending_on = false;
    } else if (state[n].pending_on && offElapsedMs(n) >= minOffMs(n)) {
      apply(n, true, Source::kCompressorRelease);
    }
  }
  xSemaphoreGiveRecursive(lock);
}

OutletStatus status(int n) {
  xSemaphoreTakeRecursive(lock, portMAX_DELAY);
  OutletStatus st;
  st.on = state[n].on;
  st.pending_on = state[n].pending_on;
  uint32_t min = minOffMs(n), el = offElapsedMs(n);
  st.pending_left_s = st.pending_on && el < min ? (min - el + 999) / 1000 : 0;
  xSemaphoreGiveRecursive(lock);
  return st;
}

void setLockout(bool locked) { lockout = locked; }
bool lockedOut() { return lockout; }

}  // namespace Outlets
