#pragma once

#include <stdint.h>

// Persistent settings (NVS via Preferences). Each struct is its own NVS key stored as a
// raw blob with a size check, so adding a field to one struct resets only that struct
// to defaults, never the others.

constexpr int kOutletCount = 2;

enum class PowerOn : uint8_t { kOff = 0, kOn = 1, kRestore = 2 };

// What is wired to an outlet's control GPIO (PLAN.md R3). Values 0/1 line up with the
// old `bool switch_enabled`, so saved settings migrate unchanged.
enum class InputMode : uint8_t { kDisabled = 0, kRocker = 1, kTouch = 2 };

struct WifiConfig {
  char ssid[33] = "";
  char password[65] = "";
  char ap_password[65] = "powerstrip";  // setup AP, only up while STA is not connected
};

struct TbConfig {
  char host[65] = "";
  uint16_t port = 1883;
  char token[65] = "";
  char http_host[65] = "";  // OTA download host, falls back to host
  uint16_t http_port = 8080;
};

// Kept apart from TbConfig so enabling TLS never resets the saved host/token.
struct TbTlsConfig {
  bool use_tls = false;
  char ca_cert[2560] = "";  // PEM root CA; empty = built-in ISRG Root X1 (Let's Encrypt)
};

// Everything appliance-specific is here, per outlet (PLAN.md R10).
struct OutletConfig {
  char name[25] = "";
  PowerOn power_on = PowerOn::kRestore;
  InputMode input = InputMode::kRocker;
  bool compressor_protect = false;
  uint16_t min_off_s = 180;
  uint16_t touch_hold_ms = 3000;  // finger must stay this long before a touch toggles
};

// Overheat protection (PLAN.md R4). Probes are DS18B20s on one 1-Wire bus, known by
// their factory ROM codes; any probe over trip_c (or a known probe going missing) cuts
// every outlet until all probes read below reset_c.
constexpr int kMaxProbes = 4;

struct ProbeConfig {
  uint8_t rom[8] = {};  // all zero = unused slot
  char name[17] = "";
};

struct ThermalConfig {
  float trip_c = 70.0f;
  float reset_c = 50.0f;
  ProbeConfig probes[kMaxProbes];
};

// Gains convert ADC pin millivolts (after DC offset removal) to volts / amps.
// Current default: ACS712-30A 66 mV/A x 0.5 divider (10k/10k) = 33 mV/A at the pin.
// Voltage default fits the trim pot as set on the first unit (2026-10-07, checked against
// a multimeter: 230 V no-load); recalibrate after touching the pot.
// phase_deg cancels the ZMPT101B's phase shift (it read PF 0.92 on a kettle, ~23 deg);
// set it with Auto-calibrate phase while a heater runs.
struct CalibConfig {
  float v_gain = 1.126f;
  float i_gain[kOutletCount] = {1.0f / 33.0f, 1.0f / 33.0f};
  float phase_deg = 0.0f;
};

// Firmware updates from ThingsBoard. Installing restarts the strip, which drops both
// relays for about a second (and keeps compressor outlets off for min_off_s). With
// auto_update off the strip never restarts by itself or on a remote command: an update
// waits until someone clicks Install on the local page, or it isn't installed at all.
struct UpdateConfig {
  bool auto_update = true;
  uint8_t window_start_h = 3;  // local hours [start, end); start == end means any time
  uint8_t window_end_h = 5;
};

namespace Config {

void begin();

const WifiConfig& wifi();
const TbConfig& tb();
const TbTlsConfig& tbTls();
const OutletConfig& outlet(int n);
const CalibConfig& calib();
const ThermalConfig& thermal();
const UpdateConfig& update();

void saveWifi(const WifiConfig& c);
void saveTb(const TbConfig& c);
void saveTbTls(const TbTlsConfig& c);
void saveOutlet(int n, const OutletConfig& c);
void saveCalib(const CalibConfig& c);
void saveThermal(const ThermalConfig& c);
void saveUpdate(const UpdateConfig& c);

// Last relay state, for PowerOn::kRestore.
bool lastState(int n);
void saveLastState(int n, bool on);

}  // namespace Config
