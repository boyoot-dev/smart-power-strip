#include "net.h"

#include <ESPmDNS.h>
#include <TbLink.h>
#include <WiFi.h>
#include <esp_task_wdt.h>
#include <ping/ping_sock.h>

#include "ca_certs.h"
#include "config.h"
#include "events.h"
#include "firmware_version.h"
#include "metering.h"
#include "outlets.h"
#include "thermal.h"
#include "time_sync.h"

namespace {

constexpr uint32_t kTelemetryMs = 10000;
constexpr uint32_t kInfoMs = 60000;
constexpr uint32_t kStaRetryMs = 15000;
constexpr uint32_t kApAfterStaLossMs = 60000;  // bring the setup AP up after this
constexpr uint32_t kApOffAfterStaMs = 120000;  // and drop it again once STA is stable
constexpr float kPowerJumpRatio = 0.10f;
// Dead-link watchdog: the ESP32 WiFi driver can keep reporting WL_CONNECTED after the
// link has silently died (seen on this strip: no ARP reply, TLS connect "errno 113").
// While ThingsBoard isn't connected, ping the router every minute; two failed checks in
// a row force a WiFi reconnect. Never a reboot: that would blip the relays.
constexpr uint32_t kLinkCheckMs = 60000;
constexpr int kLinkFailsBeforeReset = 2;
// ThingsBoard doesn't reliably push a new firmware assignment to a connected device, so
// ask for it again this often (one small message, no restart).
constexpr uint32_t kFwCheckMs = 30UL * 60 * 1000;

TbLink tb;
bool tbConfigured = false;
// Only the net task may touch the MQTT client: asking a TLS client whether it is
// connected from another task blocks while a handshake is in progress (this froze the
// web server long enough to trip the watchdog). Other tasks read this flag instead.
volatile bool cloudUp = false;
bool apUp = false;
uint32_t staLostMs = 0, staUpMs = 0, lastStaTry = 0;
uint32_t lastTelemetry = 0, lastInfo = 0 - kInfoMs;
uint32_t restartAt = 0;
uint32_t lastLinkCheck = 0;
int linkFails = 0;
// Written by the net task only, read by the web task under pendingMux.
portMUX_TYPE pendingMux = portMUX_INITIALIZER_UNLOCKED;
char pendingFw[32] = "";
volatile bool installRequested = false;
float lastSentPower[kOutletCount];

const char* const kOutKeys[kOutletCount] = {"out1", "out2"};

void startAp() {
  if (apUp) return;
  WiFi.softAP(Net::apSsid().c_str(), Config::wifi().ap_password);
  apUp = true;
  Serial.printf("Setup AP up: %s (password %s) at %s\n", Net::apSsid().c_str(),
                Config::wifi().ap_password, WiFi.softAPIP().toString().c_str());
}

void stopAp() {
  if (!apUp) return;
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  apUp = false;
  Serial.println("Setup AP down (STA stable)");
}

void manageWifi(uint32_t now) {
  bool haveCreds = Config::wifi().ssid[0] != '\0';
  if (WiFi.status() == WL_CONNECTED) {
    staLostMs = 0;
    if (staUpMs == 0) staUpMs = now;
    if (apUp && now - staUpMs > kApOffAfterStaMs) stopAp();
    return;
  }
  staUpMs = 0;
  if (staLostMs == 0) staLostMs = now;
  if (!haveCreds || now - staLostMs > kApAfterStaLossMs) startAp();
  if (haveCreds && now - lastStaTry > kStaRetryMs) {
    lastStaTry = now;
    WiFi.begin(Config::wifi().ssid, Config::wifi().password);
  }
}

// Sends 3 pings to the default gateway; true if any comes back.
bool gatewayReachable() {
  IPAddress gw = WiFi.gatewayIP();
  if (gw == IPAddress(0, 0, 0, 0)) return false;
  esp_ping_config_t cfg = ESP_PING_DEFAULT_CONFIG();
  IP_ADDR4(&cfg.target_addr, gw[0], gw[1], gw[2], gw[3]);
  cfg.count = 3;
  cfg.interval_ms = 300;
  cfg.timeout_ms = 1000;
  volatile int replies = 0;
  esp_ping_callbacks_t cbs = {};
  cbs.cb_args = (void*)&replies;
  cbs.on_ping_success = [](esp_ping_handle_t, void* arg) { (*(volatile int*)arg)++; };
  esp_ping_handle_t ping;
  if (esp_ping_new_session(&cfg, &cbs, &ping) != ESP_OK) return true;  // can't tell
  esp_ping_start(ping);
  vTaskDelay(pdMS_TO_TICKS(3 * 300 + 1500));
  esp_ping_stop(ping);
  esp_ping_delete_session(ping);
  return replies > 0;
}

void linkWatchdog(uint32_t now) {
  if (WiFi.status() != WL_CONNECTED || cloudUp) {
    linkFails = 0;
    lastLinkCheck = now;
    return;
  }
  if (now - lastLinkCheck < kLinkCheckMs) return;
  lastLinkCheck = now;
  if (gatewayReachable()) {
    linkFails = 0;
    return;
  }
  Serial.printf("net: router not answering pings (%d/%d)\n", ++linkFails, kLinkFailsBeforeReset);
  if (linkFails >= kLinkFailsBeforeReset) {
    Serial.println("net: WiFi link is dead although the driver says connected - reconnecting");
    linkFails = 0;
    WiFi.disconnect(false);
    vTaskDelay(pdMS_TO_TICKS(200));
    WiFi.begin(Config::wifi().ssid, Config::wifi().password);
    lastStaTry = millis();
  }
}

// ---- ThingsBoard ----

bool inUpdateWindow() {
  const UpdateConfig& u = Config::update();
  int h = TimeSync::localHour();
  if (h < 0) return false;  // clock not synced: the window can't be known
  if (u.window_start_h == u.window_end_h) return true;
  if (u.window_start_h < u.window_end_h) return h >= u.window_start_h && h < u.window_end_h;
  return h >= u.window_start_h || h < u.window_end_h;  // window crosses midnight
}

bool mayInstallUpdate() {
  if (installRequested) return true;
  return Config::update().auto_update && inUpdateWindow();
}

void publishPendingFw() {
  String v = tb.pendingOtaVersion();
  portENTER_CRITICAL(&pendingMux);
  strlcpy(pendingFw, v.c_str(), sizeof(pendingFw));
  portEXIT_CRITICAL(&pendingMux);
}

bool handleRpc(const String& method, JsonVariantConst params, JsonDocument& response) {
  if (method == "setOutlet") {
    int n = params["outlet"] | 0;
    bool on = params["state"] | false;
    response["state"] = Outlets::request(n - 1, on ? Want::kOn : Want::kOff,
                                         Source::kThingsBoard);
    return true;
  }
  // Switch-widget style: "setOut1"/"setOut2" with a plain boolean param.
  for (int n = 0; n < kOutletCount; n++) {
    char name[12];
    snprintf(name, sizeof(name), "setOut%d", n + 1);
    if (method == name) {
      bool on = params.as<bool>();
      Outlets::request(n, on ? Want::kOn : Want::kOff, Source::kThingsBoard);
      return true;
    }
    snprintf(name, sizeof(name), "getOut%d", n + 1);
    if (method == name) {
      OutletStatus st = Outlets::status(n);
      response.set(st.on || st.pending_on);
      return true;
    }
  }
  if (method == "getState") {
    for (int n = 0; n < kOutletCount; n++) {
      response[kOutKeys[n]] = Outlets::status(n).on;
    }
    return true;
  }
  if (method == "resetEnergy") {
    Metering::resetEnergy((params["outlet"] | 0) - 1);
    return true;
  }
  // Remote restarts are refused while auto_update is off: that setting promises the
  // strip never restarts unless someone at the strip asks for it.
  if (method == "reboot") {
    if (!Config::update().auto_update) {
      response["error"] = "auto_update is off on this strip; restart it from its local page";
      return true;
    }
    Net::scheduleRestart(500);
    return true;
  }
  if (method == "fw_check") {  // re-ask ThingsBoard for the assigned firmware, no restart
    tb.refreshSharedAttributes();
    response["current"] = kFwVersion;
    response["waiting"] = tb.pendingOtaVersion();
    return true;
  }
  if (method == "fw_install") {  // install a waiting update now, outside the window
    if (!Config::update().auto_update) {
      response["error"] = "auto_update is off on this strip; install it from its local page";
    } else if (tb.pendingOtaVersion().isEmpty()) {
      response["error"] = "no update waiting (try fw_check first)";
    } else {
      installRequested = true;
      response["installing"] = tb.pendingOtaVersion();
    }
    return true;
  }
  return false;
}

void sendTelemetry() {
  Reading r = Metering::latest();
  tb.sendTelemetry("voltage", r.vrms);
  tb.sendTelemetry("frequency", r.freq);
  tb.sendTelemetry("total_current", r.total_irms);
  tb.sendTelemetry("total_power", r.total_power);
  for (int n = 0; n < kOutletCount; n++) {
    char key[20];
    const OutletReading& o = r.out[n];
    OutletStatus st = Outlets::status(n);
    snprintf(key, sizeof(key), "%s_state", kOutKeys[n]);
    tb.sendTelemetry(key, st.on);
    snprintf(key, sizeof(key), "%s_pending_s", kOutKeys[n]);  // compressor delay left
    tb.sendTelemetry(key, static_cast<int>(st.pending_left_s));
    snprintf(key, sizeof(key), "%s_current", kOutKeys[n]);
    tb.sendTelemetry(key, o.irms);
    snprintf(key, sizeof(key), "%s_power", kOutKeys[n]);
    tb.sendTelemetry(key, o.power);
    snprintf(key, sizeof(key), "%s_pf", kOutKeys[n]);
    tb.sendTelemetry(key, o.pf);
    snprintf(key, sizeof(key), "%s_energy", kOutKeys[n]);
    tb.sendTelemetry(key, static_cast<float>(o.kwh));
    lastSentPower[n] = o.power;
  }
  ThermalStatus th = Thermal::status();
  tb.sendTelemetry("thermal_state", Thermal::stateName(th.state));
  for (int i = 0; i < kMaxProbes; i++) {
    if (!th.probes[i].used || !th.probes[i].ok) continue;
    char key[12];
    snprintf(key, sizeof(key), "temp_%d", i + 1);
    tb.sendTelemetry(key, th.probes[i].temp_c);
  }
}

void sendInfo() {
  tb.sendAttribute("ip", WiFi.localIP().toString().c_str());
  tb.sendAttribute("rssi", static_cast<int>(WiFi.RSSI()));
  tb.sendAttribute("uptime_s", static_cast<int>(millis() / 1000));
  tb.sendAttribute("auto_update", Config::update().auto_update);
  tb.sendAttribute("fw_waiting", tb.pendingOtaVersion().c_str());
  for (int n = 0; n < kOutletCount; n++) {
    char key[16];
    snprintf(key, sizeof(key), "%s_name", kOutKeys[n]);
    tb.sendAttribute(key, Config::outlet(n).name);
  }
  for (int i = 0; i < kMaxProbes; i++) {
    const ProbeConfig& p = Config::thermal().probes[i];
    if (p.name[0] == '\0') continue;
    char key[16];
    snprintf(key, sizeof(key), "temp_%d_name", i + 1);
    tb.sendAttribute(key, p.name);
  }
}

void sendEvents() {
  Event e;
  while (Events::nextUnsent(&e)) {
    char key[16], val[64];
    snprintf(key, sizeof(key), "%s_event", kOutKeys[e.outlet]);
    snprintf(val, sizeof(val), "%s by %s", e.on ? "on" : "off", sourceName(e.source));
    tb.sendTelemetry(key, val);
    snprintf(key, sizeof(key), "%s_state", kOutKeys[e.outlet]);
    tb.sendTelemetry(key, e.on);
  }
}

// Telemetry every 10 s, plus immediately on a >10% power jump (PLAN.md R9).
bool powerJumped() {
  Reading r = Metering::latest();
  for (int n = 0; n < kOutletCount; n++) {
    float prev = lastSentPower[n], now = r.out[n].power;
    float base = fabsf(prev) > 20.0f ? fabsf(prev) : 20.0f;
    if (fabsf(now - prev) > base * kPowerJumpRatio) return true;
  }
  return false;
}

void beginTb() {
  const TbConfig& c = Config::tb();
  if (c.host[0] == '\0' || c.token[0] == '\0') {
    Serial.println("ThingsBoard not configured - set it in the web UI");
    return;
  }
  TbMqttConfig mqtt;
  mqtt.host = c.host;
  mqtt.port = c.port;
  mqtt.access_token = c.token;
  const TbTlsConfig& tls = Config::tbTls();
  mqtt.use_tls = tls.use_tls;
  mqtt.ca_cert = tls.ca_cert[0] != '\0' ? tls.ca_cert : kIsrgRootX1;
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char id[24];
  snprintf(id, sizeof(id), "powerstrip-%02x%02x%02x", mac[3], mac[4], mac[5]);
  mqtt.client_id = id;
  mqtt.attribute_refresh_interval_ms = kFwCheckMs;

  TbOtaConfig ota;
  ota.http_host = c.http_host;
  ota.http_port = c.http_port;
  ota.force_plain_http = true;  // Cloudflare-fronted server, see TbLink README
  ota.current_fw_version = kFwVersion;
  ota.current_fw_title = kFwTitle;
  ota.expected_fw_title = kFwTitle;

  tb.onRpc(handleRpc);
  tb.onOtaResult([](bool ok, const String& err) {
    installRequested = false;
    if (!ok) Serial.printf("OTA failed: %s\n", err.c_str());
  });
  tb.onOtaGate(mayInstallUpdate);
  tb.begin(mqtt, ota);
  tbConfigured = true;
}

void netTask(void*) {
  for (;;) {
    uint32_t now = millis();
    if (restartAt != 0 && static_cast<int32_t>(now - restartAt) >= 0) {
      ESP.restart();
    }
    manageWifi(now);
    linkWatchdog(now);

    // TLS checks certificate dates, so wait for NTP: with the clock still at 1970 every
    // certificate looks "not yet valid" and verification fails.
    bool clockOk = !Config::tbTls().use_tls || TimeSync::synced();
    if (tbConfigured && WiFi.status() == WL_CONNECTED && clockOk) {
      uint32_t t0 = millis();
      tb.loop();  // may block for a reconnect attempt or an OTA download
      uint32_t took = millis() - t0;
      if (took > 1000) Serial.printf("net: TbLink loop took %lu ms\n", took);
      cloudUp = tb.connected();
      publishPendingFw();
      if (cloudUp) {
        sendEvents();
        if (now - lastInfo >= kInfoMs) {
          lastInfo = now;
          sendInfo();
        }
        if (now - lastTelemetry >= kTelemetryMs || powerJumped()) {
          lastTelemetry = now;
          sendTelemetry();
        }
      }
    } else {
      cloudUp = false;
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

}  // namespace

namespace Net {

void begin() {
  // A TLS handshake verifies an ECDSA P-384 certificate chain in software and holds
  // core 0 for several seconds without yielding, starving IDLE0; the default 5 s task
  // watchdog then resets the chip. Allow 30 s instead.
  esp_task_wdt_init(30, true);
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);
  WiFi.setHostname("powerstrip");
  WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t info) {
    Serial.printf("WiFi connected, IP %s\n", WiFi.localIP().toString().c_str());
    MDNS.begin("powerstrip");
    MDNS.addService("http", "tcp", 80);
  }, ARDUINO_EVENT_WIFI_STA_GOT_IP);

  if (Config::wifi().ssid[0] != '\0') {
    Serial.printf("Joining WiFi %s\n", Config::wifi().ssid);
    WiFi.begin(Config::wifi().ssid, Config::wifi().password);
    lastStaTry = millis();
  } else {
    startAp();
  }
  TimeSync::begin();
  beginTb();
  xTaskCreatePinnedToCore(netTask, "net", 8192, nullptr, 2, nullptr, 0);
}

NetState state() {
  if (WiFi.status() != WL_CONNECTED) return NetState::kSetupAp;
  return cloudUp ? NetState::kCloud : NetState::kWifi;
}

String apSsid() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char buf[24];
  snprintf(buf, sizeof(buf), "PowerStrip-%02X%02X", mac[4], mac[5]);
  return buf;
}

String pendingUpdate() {
  char v[sizeof(pendingFw)];
  portENTER_CRITICAL(&pendingMux);
  strlcpy(v, pendingFw, sizeof(v));
  portEXIT_CRITICAL(&pendingMux);
  return v;
}

bool installUpdate() {
  if (pendingUpdate().isEmpty()) return false;
  installRequested = true;
  return true;
}

void scheduleRestart(uint32_t delay_ms) {
  restartAt = millis() + delay_ms;
  if (restartAt == 0) restartAt = 1;
}

}  // namespace Net
