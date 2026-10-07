#include "config.h"

#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <stdio.h>
#include <string.h>

namespace {

Preferences prefs;
SemaphoreHandle_t prefsLock;  // Preferences isn't safe to write from several tasks
WifiConfig wifiCfg;
TbConfig tbCfg;
TbTlsConfig tbTlsCfg;
OutletConfig outletCfg[kOutletCount];
CalibConfig calibCfg;
ThermalConfig thermalCfg;
UpdateConfig updateCfg;
bool lastOn[kOutletCount];

template <typename T>
void loadBlob(const char* key, T* out) {
  if (prefs.isKey(key) && prefs.getBytesLength(key) == sizeof(T)) {
    prefs.getBytes(key, out, sizeof(T));
  }
}

template <typename T>
void saveBlob(const char* key, const T& value) {
  xSemaphoreTake(prefsLock, portMAX_DELAY);
  prefs.putBytes(key, &value, sizeof(T));
  xSemaphoreGive(prefsLock);
}

// Layout before touch support (no touch_hold_ms), migrated on load.
struct OutletConfigV1 {
  char name[25];
  PowerOn power_on;
  InputMode input;
  bool compressor_protect;
  uint16_t min_off_s;
};

void loadOutlet(const char* key, OutletConfig* out) {
  size_t len = prefs.isKey(key) ? prefs.getBytesLength(key) : 0;
  if (len == sizeof(OutletConfig)) {
    prefs.getBytes(key, out, sizeof(OutletConfig));
  } else if (len == sizeof(OutletConfigV1)) {
    OutletConfigV1 v1;
    prefs.getBytes(key, &v1, sizeof(v1));
    memcpy(out->name, v1.name, sizeof(v1.name));
    out->power_on = v1.power_on;
    out->input = v1.input;
    out->compressor_protect = v1.compressor_protect;
    out->min_off_s = v1.min_off_s;
  }
}

// Layout before phase correction, migrated on load.
struct CalibConfigV1 {
  float v_gain;
  float i_gain[kOutletCount];
};

void loadCalib(CalibConfig* out) {
  size_t len = prefs.isKey("calib") ? prefs.getBytesLength("calib") : 0;
  if (len == sizeof(CalibConfig)) {
    prefs.getBytes("calib", out, sizeof(CalibConfig));
  } else if (len == sizeof(CalibConfigV1)) {
    CalibConfigV1 v1;
    prefs.getBytes("calib", &v1, sizeof(v1));
    out->v_gain = v1.v_gain;
    memcpy(out->i_gain, v1.i_gain, sizeof(v1.i_gain));
  }
}

void outletKey(char* buf, size_t len, const char* prefix, int n) {
  snprintf(buf, len, "%s%d", prefix, n + 1);
}

}  // namespace

namespace Config {

void begin() {
  prefsLock = xSemaphoreCreateMutex();
  prefs.begin("strip", false);
  loadBlob("wifi", &wifiCfg);
  loadBlob("tb", &tbCfg);
  loadBlob("tbtls", &tbTlsCfg);
  loadCalib(&calibCfg);
  loadBlob("thermal", &thermalCfg);
  loadBlob("update", &updateCfg);
  for (int n = 0; n < kOutletCount; n++) {
    char key[8];
    outletKey(key, sizeof(key), "out", n);
    loadOutlet(key, &outletCfg[n]);
    if (outletCfg[n].name[0] == '\0') {
      snprintf(outletCfg[n].name, sizeof(outletCfg[n].name), "Out %d", n + 1);
    }
    outletKey(key, sizeof(key), "last", n);
    lastOn[n] = prefs.getBool(key, false);
  }
}

const WifiConfig& wifi() { return wifiCfg; }
const TbConfig& tb() { return tbCfg; }
const TbTlsConfig& tbTls() { return tbTlsCfg; }
const OutletConfig& outlet(int n) { return outletCfg[n]; }
const CalibConfig& calib() { return calibCfg; }
const ThermalConfig& thermal() { return thermalCfg; }
const UpdateConfig& update() { return updateCfg; }

void saveWifi(const WifiConfig& c) { wifiCfg = c; saveBlob("wifi", c); }
void saveTb(const TbConfig& c) { tbCfg = c; saveBlob("tb", c); }
void saveTbTls(const TbTlsConfig& c) { tbTlsCfg = c; saveBlob("tbtls", c); }
void saveCalib(const CalibConfig& c) { calibCfg = c; saveBlob("calib", c); }
void saveThermal(const ThermalConfig& c) { thermalCfg = c; saveBlob("thermal", c); }
void saveUpdate(const UpdateConfig& c) { updateCfg = c; saveBlob("update", c); }

void saveOutlet(int n, const OutletConfig& c) {
  char key[8];
  outletKey(key, sizeof(key), "out", n);
  outletCfg[n] = c;
  saveBlob(key, c);
}

bool lastState(int n) { return lastOn[n]; }

void saveLastState(int n, bool on) {
  if (lastOn[n] == on) {
    return;
  }
  char key[8];
  outletKey(key, sizeof(key), "last", n);
  lastOn[n] = on;
  xSemaphoreTake(prefsLock, portMAX_DELAY);
  prefs.putBool(key, on);
  xSemaphoreGive(prefsLock);
}

}  // namespace Config
