#include "metering.h"

#include <Arduino.h>
#include <math.h>

#include "pins.h"

namespace {

constexpr uint32_t kWindowUs = 200000;     // 10 cycles at 50 Hz
constexpr uint32_t kPairPeriodUs = 200;    // ~1000 V/I pairs per window
constexpr float kCurrentDeadbandA = 0.1f;  // below this reads as 0 (ACS712 noise floor)
constexpr float kZeroCrossHystMv = 20.0f;

const uint8_t kCurrentPins[kOutletCount] = {kPinCurrent1, kPinCurrent2};

portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
Reading shared = {};
double energyWh[kOutletCount] = {};
uint32_t lastUpdateMs[kOutletCount] = {};
float vMeanMv = 1500.0f;  // previous window's DC offset, used for zero-crossing detection

struct Window {
  float vrms, irms, power, freq;
  uint16_t pairs;
};

// Offsets are removed from the sums afterwards (variance = E[x^2] - E[x]^2), so one
// pass is enough and the ACS712 zero drift with temperature/supply is tracked for free.
Window sampleWindow(uint8_t currentPin, const CalibConfig& cal, int n) {
  double sv = 0, si = 0, svv = 0, sii = 0, svi = 0;
  uint32_t pairs = 0, crossings = 0, firstCrossUs = 0, lastCrossUs = 0;
  bool above = false, armed = false;

  uint32_t start = micros(), next = start;
  while (micros() - start < kWindowUs) {
    while (static_cast<int32_t>(micros() - next) < 0) {
    }
    next += kPairPeriodUs;

    float v = analogReadMilliVolts(kPinVoltageSense);
    float i = analogReadMilliVolts(currentPin);
    sv += v; si += i; svv += v * v; sii += i * i; svi += v * i;
    pairs++;

    float dv = v - vMeanMv;
    if (!armed) {
      above = dv > 0;
      armed = true;
    } else if (!above && dv > kZeroCrossHystMv) {
      above = true;
      uint32_t now = micros();
      if (crossings == 0) firstCrossUs = now;
      lastCrossUs = now;
      crossings++;
    } else if (above && dv < -kZeroCrossHystMv) {
      above = false;
    }
  }

  Window w = {};
  w.pairs = pairs;
  if (pairs == 0) return w;
  double mv = sv / pairs, mi = si / pairs;
  double varV = svv / pairs - mv * mv;
  double varI = sii / pairs - mi * mi;
  double cov = svi / pairs - mv * mi;
  vMeanMv = mv;

  w.vrms = sqrt(varV > 0 ? varV : 0) * cal.v_gain;
  w.irms = sqrt(varI > 0 ? varI : 0) * cal.i_gain[n];
  w.power = cov * cal.v_gain * cal.i_gain[n];
  if (crossings >= 2 && lastCrossUs != firstCrossUs) {
    w.freq = (crossings - 1) * 1e6f / (lastCrossUs - firstCrossUs);
  }
  return w;
}

void meteringTask(void*) {
  analogSetAttenuation(ADC_11db);
  for (int n = 0;; n = (n + 1) % kOutletCount) {
    CalibConfig cal = Config::calib();
    Window w = sampleWindow(kCurrentPins[n], cal, n);

    OutletReading r = {};
    if (w.irms >= kCurrentDeadbandA) {
      r.irms = w.irms;
      r.power = fabsf(w.power) < 1.0f ? 0 : w.power;
      r.va = w.vrms * w.irms;
      r.pf = r.va > 0 ? r.power / r.va : 0;
    }

    // Each outlet is measured half the time; its reading stands for the whole cycle.
    uint32_t now = millis();
    double deltaWh = lastUpdateMs[n] != 0 && r.power > 0
                         ? r.power * (now - lastUpdateMs[n]) / 3600000.0
                         : 0;
    lastUpdateMs[n] = now;

    portENTER_CRITICAL(&mux);
    energyWh[n] += deltaWh;
    r.kwh = energyWh[n] / 1000.0;
    shared.vrms = w.vrms;
    shared.freq = w.freq;
    shared.out[n] = r;
    shared.total_irms = shared.out[0].irms + shared.out[1].irms;
    shared.total_power = shared.out[0].power + shared.out[1].power;
    shared.pairs = w.pairs;
    shared.windows++;
    portEXIT_CRITICAL(&mux);

    vTaskDelay(1);  // let core 1's idle task run so the task watchdog stays happy
  }
}

}  // namespace

namespace Metering {

void begin() {
  xTaskCreatePinnedToCore(meteringTask, "metering", 4096, nullptr, 3, nullptr, 1);
}

Reading latest() {
  portENTER_CRITICAL(&mux);
  Reading r = shared;
  portEXIT_CRITICAL(&mux);
  return r;
}

void resetEnergy(int n) {
  if (n < 0 || n >= kOutletCount) return;
  portENTER_CRITICAL(&mux);
  energyWh[n] = 0;
  shared.out[n].kwh = 0;
  portEXIT_CRITICAL(&mux);
}

}  // namespace Metering
