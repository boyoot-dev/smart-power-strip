#include "thermal.h"

#include <DallasTemperature.h>
#include <OneWire.h>

#include "outlets.h"
#include "pins.h"

namespace {

constexpr uint32_t kCycleMs = 2000;
constexpr uint32_t kConvertMs = 400;       // 11-bit conversion needs 375 ms
constexpr uint32_t kDiscoverEveryMs = 30000;
constexpr int kBadReadsForFault = 3;       // ~6 s of failed reads

OneWire bus(kPinOneWire);
DallasTemperature sensors(&bus);

SemaphoreHandle_t cfgLock;       // serialises changes to Config::thermal()
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
ThermalStatus shared = {};
int badReads[kMaxProbes] = {};
ThermalState state = ThermalState::kDisarmed;

bool isUsed(const ProbeConfig& p) {
  for (uint8_t b : p.rom) if (b) return true;
  return false;
}

void romToHex(const uint8_t* rom, char* out) {
  for (int i = 0; i < 8; i++) sprintf(out + i * 2, "%02X", rom[i]);
}

// Registers any DS18B20 on the bus that isn't known yet.
void discover() {
  uint8_t rom[8];
  bus.reset_search();
  while (bus.search(rom)) {
    if (OneWire::crc8(rom, 7) != rom[7] || rom[0] != 0x28) continue;
    xSemaphoreTake(cfgLock, portMAX_DELAY);
    static ThermalConfig c;
    c = Config::thermal();
    bool known = false;
    int free = -1;
    for (int i = 0; i < kMaxProbes; i++) {
      if (isUsed(c.probes[i])) {
        if (memcmp(c.probes[i].rom, rom, 8) == 0) known = true;
      } else if (free < 0) {
        free = i;
      }
    }
    if (!known && free >= 0) {
      memcpy(c.probes[free].rom, rom, 8);
      snprintf(c.probes[free].name, sizeof(c.probes[free].name), "Probe %d", free + 1);
      Config::saveThermal(c);
      sensors.setResolution(rom, 11);
      char hex[17];
      romToHex(rom, hex);
      Serial.printf("Thermal: new probe %s registered as Probe %d\n", hex, free + 1);
    }
    xSemaphoreGive(cfgLock);
  }
}

void trip(ThermalState to, Source why) {
  state = to;
  Outlets::setLockout(true);
  for (int n = 0; n < kOutletCount; n++) Outlets::request(n, Want::kOff, why);
  Serial.printf("Thermal: %s - all outlets off and locked\n",
                to == ThermalState::kTripped ? "OVERHEAT" : "probe fault");
}

void evaluate(const ThermalConfig& c, const ThermalStatus& st) {
  bool armed = false, over = false, fault = false, allCool = true;
  for (int i = 0; i < kMaxProbes; i++) {
    const ProbeReading& p = st.probes[i];
    if (!p.used) continue;
    armed = true;
    if (badReads[i] >= kBadReadsForFault) fault = true;
    if (!p.ok) { allCool = false; continue; }
    if (p.temp_c >= c.trip_c) over = true;
    if (p.temp_c >= c.reset_c) allCool = false;
  }

  if (!armed) {
    state = ThermalState::kDisarmed;
    Outlets::setLockout(false);
    return;
  }
  bool latched = state == ThermalState::kTripped || state == ThermalState::kFault;
  if (over && state != ThermalState::kTripped) {
    trip(ThermalState::kTripped, Source::kOverheat);
  } else if (fault && !latched) {
    trip(ThermalState::kFault, Source::kSensorFault);
  } else if (latched && !fault && allCool) {
    state = ThermalState::kOk;
    Outlets::setLockout(false);  // outlets stay off until someone turns them on
    Serial.println("Thermal: cooled down, outlets may be turned on again");
  } else if (!latched) {
    state = ThermalState::kOk;
  }
}

void thermalTask(void*) {
  uint32_t lastDiscover = 0 - kDiscoverEveryMs;
  for (;;) {
    uint32_t now = millis();
    if (now - lastDiscover >= kDiscoverEveryMs) {
      lastDiscover = now;
      discover();
    }

    sensors.requestTemperatures();
    vTaskDelay(pdMS_TO_TICKS(kConvertMs));

    xSemaphoreTake(cfgLock, portMAX_DELAY);
    ThermalConfig c = Config::thermal();
    xSemaphoreGive(cfgLock);
    ThermalStatus st = {};
    st.max_c = -127;
    for (int i = 0; i < kMaxProbes; i++) {
      ProbeReading& p = st.probes[i];
      p.used = isUsed(c.probes[i]);
      if (!p.used) { badReads[i] = 0; continue; }
      romToHex(c.probes[i].rom, p.id);
      strlcpy(p.name, c.probes[i].name, sizeof(p.name));
      float t = sensors.getTempC(c.probes[i].rom);
      // -127 = no answer; exactly 85 = power-on value, never a real first reading.
      p.ok = t > -55.0f && t != DEVICE_DISCONNECTED_C && t != 85.0f;
      if (p.ok) {
        p.temp_c = t;
        badReads[i] = 0;
        if (t > st.max_c) st.max_c = t;
      } else if (badReads[i] < kBadReadsForFault) {
        badReads[i]++;
      }
    }
    evaluate(c, st);
    st.state = state;

    portENTER_CRITICAL(&mux);
    shared = st;
    portEXIT_CRITICAL(&mux);

    vTaskDelay(pdMS_TO_TICKS(kCycleMs - kConvertMs));
  }
}

}  // namespace

namespace Thermal {

void begin() {
  cfgLock = xSemaphoreCreateMutex();
  sensors.begin();
  sensors.setWaitForConversion(false);
  sensors.setResolution(11);
  xTaskCreatePinnedToCore(thermalTask, "thermal", 4096, nullptr, 1, nullptr, 0);
}

ThermalStatus status() {
  portENTER_CRITICAL(&mux);
  ThermalStatus st = shared;
  portEXIT_CRITICAL(&mux);
  return st;
}

const char* stateName(ThermalState s) {
  switch (s) {
    case ThermalState::kDisarmed: return "no_probes";
    case ThermalState::kOk: return "ok";
    case ThermalState::kTripped: return "overheat";
    case ThermalState::kFault: return "probe_fault";
  }
  return "?";
}

bool update(const char* id, const char* name, bool remove) {
  xSemaphoreTake(cfgLock, portMAX_DELAY);
  static ThermalConfig c;
  c = Config::thermal();
  bool found = false;
  for (int i = 0; i < kMaxProbes; i++) {
    char hex[17];
    romToHex(c.probes[i].rom, hex);
    if (!isUsed(c.probes[i]) || strcmp(hex, id) != 0) continue;
    if (remove) c.probes[i] = ProbeConfig();
    else strlcpy(c.probes[i].name, name, sizeof(c.probes[i].name));
    found = true;
  }
  if (found) Config::saveThermal(c);
  xSemaphoreGive(cfgLock);
  return found;
}

bool forget(const char* id) { return update(id, nullptr, true); }
bool rename(const char* id, const char* name) { return update(id, name, false); }

void setLimits(float trip_c, float reset_c) {
  xSemaphoreTake(cfgLock, portMAX_DELAY);
  static ThermalConfig c;
  c = Config::thermal();
  c.trip_c = trip_c;
  c.reset_c = reset_c;
  Config::saveThermal(c);
  xSemaphoreGive(cfgLock);
}

}  // namespace Thermal
