#include "metering.h"

#include <Arduino.h>
#include <math.h>

#include "pins.h"

namespace {

constexpr uint32_t kWindowUs = 200000;     // 10 cycles at 50 Hz
constexpr uint32_t kPairPeriodUs = 200;    // ~1000 V/I pairs per window
// ACS712-30A + ESP32 ADC noise reads 0.1-0.27 A RMS with no load (3-9 mV at the pin), so
// Irms alone can't tell a small load from nothing. Noise is uncorrelated with the voltage
// and averages out of real power, so a load counts if it draws real power or clearly
// more current than the noise.
constexpr float kLoadMinPowerW = 5.0f;
constexpr float kLoadMinCurrentA = 0.3f;
constexpr float kZeroCrossHystMv = 20.0f;
// ADC spikes can add a second rising crossing within a cycle (read as ~55.6 Hz instead of
// 50); crossings closer than this are ignored. Allows mains up to ~83 Hz.
constexpr uint32_t kMinCyclePeriodUs = 12000;
// Reactive power uses voltage delayed by a quarter mains cycle; this history must hold
// more than that many samples (5 ms at ~220 us per pair = ~23).
constexpr int kVHistory = 48;
constexpr float kQuarterCycleUs = 5000.0f;  // 50 Hz
constexpr float kPhaseCalMinA = 2.0f;       // below this the angle is too noisy to trust
constexpr float kPhaseCalMaxDeg = 60.0f;
constexpr float kAngleSmoothing = 0.2f;

const uint8_t kCurrentPins[kOutletCount] = {kPinCurrent1, kPinCurrent2};

portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
Reading shared = {};
double energyWh[kOutletCount] = {};
uint32_t lastUpdateMs[kOutletCount] = {};
float vMeanMv = 1500.0f;  // previous window's DC offset, used for zero-crossing detection
float pairUs = kPairPeriodUs;  // previous window's actual mean pair spacing (ADC-bound)
float angleEma[kOutletCount] = {};

struct Window {
  float vrms, irms, power, freq, angle_deg;
  uint16_t pairs;
};

// Offsets are removed from the sums afterwards (variance = E[x^2] - E[x]^2), so one
// pass is enough and the ACS712 zero drift with temperature/supply is tracked for free.
// Alongside P = cov(V, I) it sums Q = cov(V delayed a quarter cycle, I): together they
// give the V-to-I angle, and rotating by the calibrated phase_deg cancels the voltage
// sensor's own phase shift (the ZMPT101B made a kettle read PF 0.92).
Window sampleWindow(uint8_t currentPin, const CalibConfig& cal, int n) {
  double sv = 0, si = 0, svv = 0, sii = 0, svi = 0;
  double sq = 0, sqi = 0, siq = 0;
  uint32_t pairs = 0, qPairs = 0, crossings = 0, firstCrossUs = 0, lastCrossUs = 0;
  bool above = false, armed = false;
  float vHist[kVHistory];
  float lag = constrain(kQuarterCycleUs / pairUs, 1.0f, kVHistory - 2.0f);
  uint32_t lagInt = static_cast<uint32_t>(lag);
  float lagFrac = lag - lagInt;

  uint32_t start = micros(), next = start;
  while (micros() - start < kWindowUs) {
    while (static_cast<int32_t>(micros() - next) < 0) {
    }
    next += kPairPeriodUs;

    float v = analogReadMilliVolts(kPinVoltageSense);
    float i = analogReadMilliVolts(currentPin);
    sv += v; si += i; svv += v * v; sii += i * i; svi += v * i;
    vHist[pairs % kVHistory] = v;
    if (pairs > lagInt) {
      float a = vHist[(pairs - lagInt) % kVHistory];
      float b = vHist[(pairs - lagInt - 1) % kVHistory];
      float vq = a + (b - a) * lagFrac;
      sq += vq; sqi += vq * i; siq += i;
      qPairs++;
    }
    pairs++;

    float dv = v - vMeanMv;
    if (!armed) {
      above = dv > 0;
      armed = true;
    } else if (!above && dv > kZeroCrossHystMv) {
      above = true;
      uint32_t now = micros();
      if (crossings > 0 && now - lastCrossUs < kMinCyclePeriodUs) continue;
      if (crossings == 0) firstCrossUs = now;
      lastCrossUs = now;
      crossings++;
    } else if (above && dv < -kZeroCrossHystMv) {
      above = false;
    }
  }

  Window w = {};
  w.pairs = pairs;
  if (pairs == 0 || qPairs == 0) return w;
  pairUs = static_cast<float>(micros() - start) / pairs;
  double mv = sv / pairs, mi = si / pairs;
  double varV = svv / pairs - mv * mv;
  double varI = sii / pairs - mi * mi;
  double p = svi / pairs - mv * mi;
  double q = sqi / qPairs - (sq / qPairs) * (siq / qPairs);
  vMeanMv = mv;

  double phi = cal.phase_deg * M_PI / 180.0;
  w.vrms = sqrt(varV > 0 ? varV : 0) * cal.v_gain;
  w.irms = sqrt(varI > 0 ? varI : 0) * cal.i_gain[n];
  w.power = (p * cos(phi) + q * sin(phi)) * cal.v_gain * cal.i_gain[n];
  w.angle_deg = atan2(q, p) * 180.0 / M_PI;
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
    if (fabsf(w.power) >= kLoadMinPowerW || w.irms >= kLoadMinCurrentA) {
      r.irms = w.irms;
      r.power = fabsf(w.power) < kLoadMinPowerW ? 0 : w.power;
      r.va = w.vrms * w.irms;
      r.pf = r.va > 0 ? r.power / r.va : 0;
      angleEma[n] += (w.angle_deg - angleEma[n]) * kAngleSmoothing;
      r.raw_angle_deg = angleEma[n];
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

bool calibratePhase(const char** err) {
  Reading r = latest();
  int best = 0;
  for (int n = 1; n < kOutletCount; n++) {
    if (r.out[n].irms > r.out[best].irms) best = n;
  }
  if (r.out[best].irms < kPhaseCalMinA) {
    *err = "Run a heater or kettle (at least 2 A) on one outlet first";
    return false;
  }
  float angle = r.out[best].raw_angle_deg;
  if (fabsf(angle) > kPhaseCalMaxDeg) {
    *err = "Angle out of range - is the load a heater? Motors and chargers won't do";
    return false;
  }
  CalibConfig c = Config::calib();
  c.phase_deg = angle;
  Config::saveCalib(c);
  return true;
}

void resetEnergy(int n) {
  if (n < 0 || n >= kOutletCount) return;
  portENTER_CRITICAL(&mux);
  energyWh[n] = 0;
  shared.out[n].kwh = 0;
  portEXIT_CRITICAL(&mux);
}

}  // namespace Metering
