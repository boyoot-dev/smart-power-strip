#include "control.h"

#include <Arduino.h>

#include "net.h"
#include "outlets.h"
#include "pins.h"

namespace {

constexpr uint32_t kTickMs = 10;
constexpr uint32_t kDebounceMs = 50;

const uint8_t kSwitchPins[kOutletCount] = {kPinSwitch1, kPinSwitch2};

struct Input {
  InputMode mode;      // mode the pin is currently configured for
  bool stable;         // debounced level
  bool last_raw;
  uint32_t changed_ms;
  uint32_t touched_ms; // touch: when the current touch began
  bool fired;          // touch: already toggled during this touch
};

Input in[kOutletCount];

// Rocker: pull-up, switch shorts to GND. Touch (TTP223, momentary, active-high):
// pull-down, so an unplugged module reads "not touched" instead of a phantom press.
void configure(int n, InputMode mode) {
  pinMode(kSwitchPins[n], mode == InputMode::kTouch ? INPUT_PULLDOWN : INPUT_PULLUP);
  delayMicroseconds(50);
  Input& s = in[n];
  s.mode = mode;
  s.stable = s.last_raw = digitalRead(kSwitchPins[n]);
  s.changed_ms = millis();
  s.fired = s.stable;  // a finger already on the pad must lift before the first toggle
}

void pollInputs(uint32_t now) {
  for (int n = 0; n < kOutletCount; n++) {
    const OutletConfig& cfg = Config::outlet(n);
    Input& s = in[n];
    if (cfg.input != s.mode) configure(n, cfg.input);  // changed from the web page
    if (s.mode == InputMode::kDisabled) continue;

    bool raw = digitalRead(kSwitchPins[n]);
    if (raw != s.last_raw) {
      s.last_raw = raw;
      s.changed_ms = now;
    } else if (raw != s.stable && now - s.changed_ms >= kDebounceMs) {
      s.stable = raw;
      if (s.mode == InputMode::kRocker) {
        // Maintained rocker: any movement toggles, its position means nothing.
        Outlets::request(n, Want::kToggle, Source::kButton);
      } else if (raw) {
        s.touched_ms = now;
      } else {
        s.fired = false;  // finger lifted
      }
    }

    // Touch toggles once the finger has stayed for the hold time.
    if (s.mode == InputMode::kTouch && s.stable && !s.fired &&
        now - s.touched_ms >= cfg.touch_hold_ms) {
      s.fired = true;
      Outlets::request(n, Want::kToggle, Source::kButton);
    }
  }
}

// Solid = ThingsBoard connected, slow blink = WiFi only, fast blink = setup AP.
void updateLed(uint32_t now) {
  bool on;
  switch (Net::state()) {
    case NetState::kCloud: on = true; break;
    case NetState::kWifi: on = (now / 1000) % 2; break;
    default: on = (now / 150) % 2; break;
  }
  digitalWrite(kPinStatusLed, on);
}

void controlTask(void*) {
  TickType_t wake = xTaskGetTickCount();
  for (;;) {
    uint32_t now = millis();
    pollInputs(now);
    Outlets::tick();
    updateLed(now);
    vTaskDelayUntil(&wake, pdMS_TO_TICKS(kTickMs));
  }
}

}  // namespace

namespace Control {

void begin() {
  pinMode(kPinStatusLed, OUTPUT);
  for (int n = 0; n < kOutletCount; n++) {
    configure(n, Config::outlet(n).input);
  }
  xTaskCreatePinnedToCore(controlTask, "control", 4096, nullptr, 4, nullptr, 0);
}

}  // namespace Control
