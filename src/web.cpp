#include "web.h"

#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <WiFi.h>

#include "config.h"
#include "events.h"
#include "firmware_version.h"
#include "metering.h"
#include "net.h"
#include "outlets.h"
#include "thermal.h"
#include "time_sync.h"
#include "web_page.h"

namespace {

constexpr uint32_t kPushCheckMs = 50;
constexpr uint32_t kStatusEveryMs = 1000;

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

String param(AsyncWebServerRequest* req, const char* name) {
  return req->hasParam(name, true) ? req->getParam(name, true)->value() : String();
}

// Blank secret fields keep the saved value, so the UI never has to read them back.
void copyIfSet(char* dst, size_t len, const String& v, bool secret) {
  if (secret && v.isEmpty()) return;
  strlcpy(dst, v.c_str(), len);
}

void sendJson(AsyncWebServerRequest* req, const JsonDocument& doc) {
  String out;
  serializeJson(doc, out);
  req->send(200, "application/json", out);
}

void fillStatus(JsonDocument& doc) {
  Reading r = Metering::latest();
  doc["type"] = "status";
  doc["fw"] = kFwVersion;
  doc["time"] = TimeSync::now();
  doc["uptime_s"] = millis() / 1000;
  doc["wifi"] = WiFi.status() == WL_CONNECTED ? WiFi.SSID() : String();
  doc["ip"] = WiFi.localIP().toString();
  doc["rssi"] = WiFi.RSSI();
  doc["cloud"] = Net::state() == NetState::kCloud;
  doc["voltage"] = r.vrms;
  doc["frequency"] = r.freq;
  doc["total_current"] = r.total_irms;
  doc["total_power"] = r.total_power;
  doc["pairs"] = r.pairs;
  JsonArray outs = doc.createNestedArray("outlets");
  for (int n = 0; n < kOutletCount; n++) {
    OutletStatus st = Outlets::status(n);
    JsonObject o = outs.createNestedObject();
    o["name"] = Config::outlet(n).name;
    o["on"] = st.on;
    o["pending_s"] = st.pending_left_s;
    o["current"] = r.out[n].irms;
    o["power"] = r.out[n].power;
    o["pf"] = r.out[n].pf;
    o["kwh"] = r.out[n].kwh;
  }
  ThermalStatus th = Thermal::status();
  JsonObject t = doc.createNestedObject("thermal");
  t["state"] = Thermal::stateName(th.state);
  t["trip_c"] = Config::thermal().trip_c;
  t["reset_c"] = Config::thermal().reset_c;
  JsonArray probes = t.createNestedArray("probes");
  for (const ProbeReading& p : th.probes) {
    if (!p.used) continue;
    JsonObject o = probes.createNestedObject();
    o["id"] = p.id;
    o["name"] = p.name;
    o["ok"] = p.ok;
    if (p.ok) o["c"] = p.temp_c;
  }
}

String statusJson() {
  DynamicJsonDocument doc(3072);
  fillStatus(doc);
  String out;
  serializeJson(doc, out);
  return out;
}

void handleStatus(AsyncWebServerRequest* req) {
  DynamicJsonDocument doc(3072);
  fillStatus(doc);
  sendJson(req, doc);
}

void fillEvent(JsonObject e, const Event& ev) {
  e["t"] = ev.time;
  e["synced"] = ev.synced;
  e["outlet"] = ev.outlet + 1;
  e["on"] = ev.on;
  e["source"] = sourceName(ev.source);
}

void handleEvents(AsyncWebServerRequest* req) {
  static Event buf[50];
  size_t n = Events::latest(buf, 50);
  DynamicJsonDocument doc(6144);
  JsonArray arr = doc.to<JsonArray>();
  for (size_t i = 0; i < n; i++) {
    fillEvent(arr.createNestedObject(), buf[i]);
  }
  sendJson(req, doc);
}

Want parseWant(const String& s) {
  return s == "on" ? Want::kOn : s == "off" ? Want::kOff : Want::kToggle;
}

void handleOutlet(AsyncWebServerRequest* req) {
  int n = param(req, "n").toInt() - 1;
  String s = param(req, "state");
  Outlets::request(n, parseWant(s), Source::kWeb);
  req->send(200, "application/json", "{}");
}

void handleGetConfig(AsyncWebServerRequest* req) {
  DynamicJsonDocument doc(5120);
  const WifiConfig& w = Config::wifi();
  doc["wifi"]["ssid"] = w.ssid;
  doc["wifi"]["password_set"] = w.password[0] != '\0';
  const TbConfig& t = Config::tb();
  doc["tb"]["host"] = t.host;
  doc["tb"]["port"] = t.port;
  doc["tb"]["token_set"] = t.token[0] != '\0';
  doc["tb"]["http_host"] = t.http_host;
  doc["tb"]["http_port"] = t.http_port;
  doc["tb"]["use_tls"] = Config::tbTls().use_tls;
  doc["tb"]["ca_cert"] = Config::tbTls().ca_cert;
  JsonArray outs = doc.createNestedArray("outlets");
  for (int n = 0; n < kOutletCount; n++) {
    const OutletConfig& c = Config::outlet(n);
    JsonObject o = outs.createNestedObject();
    o["name"] = c.name;
    o["power_on"] = static_cast<int>(c.power_on);
    o["input"] = static_cast<int>(c.input);
    o["touch_hold_s"] = c.touch_hold_ms / 1000.0f;
    o["compressor_protect"] = c.compressor_protect;
    o["min_off_s"] = c.min_off_s;
  }
  const CalibConfig& cal = Config::calib();
  doc["calib"]["v_gain"] = cal.v_gain;
  doc["calib"]["i1_gain"] = cal.i_gain[0];
  doc["calib"]["i2_gain"] = cal.i_gain[1];
  doc["ap_ssid"] = Net::apSsid();
  doc["fw_title"] = kFwTitle;
  doc["fw"] = kFwVersion;
  sendJson(req, doc);
}

void handleSaveWifi(AsyncWebServerRequest* req) {
  WifiConfig c = Config::wifi();
  copyIfSet(c.ssid, sizeof(c.ssid), param(req, "ssid"), false);
  copyIfSet(c.password, sizeof(c.password), param(req, "password"), true);
  Config::saveWifi(c);
  req->send(200, "application/json", "{\"restart\":true}");
  Net::scheduleRestart();
}

void handleSaveTb(AsyncWebServerRequest* req) {
  TbConfig c = Config::tb();
  copyIfSet(c.host, sizeof(c.host), param(req, "host"), false);
  c.port = param(req, "port").toInt() ?: 1883;
  copyIfSet(c.token, sizeof(c.token), param(req, "token"), true);
  copyIfSet(c.http_host, sizeof(c.http_host), param(req, "http_host"), false);
  c.http_port = param(req, "http_port").toInt() ?: 8080;
  Config::saveTb(c);
  static TbTlsConfig tls;  // 2.5 KB, keep it off the AsyncTCP task stack
  tls = Config::tbTls();
  tls.use_tls = param(req, "use_tls") == "1";
  String ca = param(req, "ca_cert");
  ca.trim();
  strlcpy(tls.ca_cert, ca.c_str(), sizeof(tls.ca_cert));
  Config::saveTbTls(tls);
  req->send(200, "application/json", "{\"restart\":true}");
  Net::scheduleRestart();
}

void handleSaveOutlet(AsyncWebServerRequest* req) {
  int n = param(req, "n").toInt() - 1;
  if (n < 0 || n >= kOutletCount) {
    req->send(400, "application/json", "{\"error\":\"bad outlet\"}");
    return;
  }
  OutletConfig c = Config::outlet(n);
  copyIfSet(c.name, sizeof(c.name), param(req, "name"), false);
  c.power_on = static_cast<PowerOn>(constrain(param(req, "power_on").toInt(), 0, 2));
  c.input = static_cast<InputMode>(constrain(param(req, "input").toInt(), 0, 2));
  c.touch_hold_ms = constrain(param(req, "touch_hold_s").toFloat(), 0.0f, 10.0f) * 1000;
  c.compressor_protect = param(req, "compressor_protect") == "1";
  c.min_off_s = constrain(param(req, "min_off_s").toInt(), 0, 1800);
  Config::saveOutlet(n, c);
  req->send(200, "application/json", "{}");
}

void handleSaveThermal(AsyncWebServerRequest* req) {
  float trip = param(req, "trip_c").toFloat(), reset = param(req, "reset_c").toFloat();
  if (trip < 40 || trip > 110 || reset < 20 || reset > trip - 5) {
    req->send(400, "application/json",
              "{\"error\":\"Trip must be 40-110 C and reset at least 5 C below it\"}");
    return;
  }
  Thermal::setLimits(trip, reset);
  req->send(200, "application/json", "{}");
}

void handleProbe(AsyncWebServerRequest* req) {
  String id = param(req, "id");
  bool ok = param(req, "forget") == "1" ? Thermal::forget(id.c_str())
                                        : Thermal::rename(id.c_str(), param(req, "name").c_str());
  req->send(ok ? 200 : 404, "application/json", ok ? "{}" : "{\"error\":\"probe not found\"}");
}

void handleSaveCalib(AsyncWebServerRequest* req) {
  CalibConfig c = Config::calib();
  if (param(req, "v_gain").toFloat() > 0) c.v_gain = param(req, "v_gain").toFloat();
  if (param(req, "i1_gain").toFloat() > 0) c.i_gain[0] = param(req, "i1_gain").toFloat();
  if (param(req, "i2_gain").toFloat() > 0) c.i_gain[1] = param(req, "i2_gain").toFloat();
  Config::saveCalib(c);
  req->send(200, "application/json", "{}");
}

// Browser -> strip: {"cmd":"outlet","n":1,"state":"on"|"off"|"toggle"}.
void onWsEvent(AsyncWebSocket*, AsyncWebSocketClient* client, AwsEventType type, void* arg,
               uint8_t* data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    client->text(statusJson());
    return;
  }
  if (type != WS_EVT_DATA) return;
  AwsFrameInfo* info = static_cast<AwsFrameInfo*>(arg);
  if (!info->final || info->index != 0 || info->len != len || info->opcode != WS_TEXT) return;
  StaticJsonDocument<128> doc;
  if (deserializeJson(doc, data, len)) return;
  if (doc["cmd"] == "outlet") {
    Outlets::request((doc["n"] | 0) - 1, parseWant(doc["state"] | "toggle"), Source::kWeb);
    ws.textAll(statusJson());  // answer at once, e.g. a compressor-delay WAIT
  }
}

// Pushes status the moment a relay changes (new event) and otherwise once a second.
void pushTask(void*) {
  uint32_t lastSeq = Events::seq(), lastStatus = 0;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(kPushCheckMs));
    ws.cleanupClients();
    uint32_t seq = Events::seq();
    if (ws.count() == 0) {
      lastSeq = seq;
      continue;
    }
    uint32_t now = millis();
    bool changed = seq != lastSeq;
    if (changed) {
      Event buf[8];
      size_t n = Events::latest(buf, min<uint32_t>(seq - lastSeq, 8));
      for (size_t i = n; i-- > 0;) {  // oldest first
        StaticJsonDocument<192> doc;
        JsonObject o = doc.to<JsonObject>();
        o["type"] = "event";
        fillEvent(o, buf[i]);
        String out;
        serializeJson(doc, out);
        ws.textAll(out);
      }
      lastSeq = seq;
    }
    if (changed || now - lastStatus >= kStatusEveryMs) {
      lastStatus = now;
      ws.textAll(statusJson());
    }
  }
}

}  // namespace

namespace Web {

void begin() {
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->send(200, "text/html", kIndexHtml);
  });
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/events", HTTP_GET, handleEvents);
  server.on("/api/config", HTTP_GET, handleGetConfig);
  server.on("/api/outlet", HTTP_POST, handleOutlet);
  server.on("/api/wifi", HTTP_POST, handleSaveWifi);
  server.on("/api/tb", HTTP_POST, handleSaveTb);
  server.on("/api/outlet-config", HTTP_POST, handleSaveOutlet);
  server.on("/api/calib", HTTP_POST, handleSaveCalib);
  server.on("/api/thermal", HTTP_POST, handleSaveThermal);
  server.on("/api/probe", HTTP_POST, handleProbe);
  server.on("/api/reboot", HTTP_POST, [](AsyncWebServerRequest* req) {
    req->send(200, "application/json", "{}");
    Net::scheduleRestart();
  });
  server.onNotFound([](AsyncWebServerRequest* req) {
    // Captive-portal style: anything unknown on the setup AP lands on the UI.
    req->redirect("/");
  });
  server.begin();
  // Above net (2) so a 7 s TLS handshake doesn't freeze the page; the work itself is tiny.
  xTaskCreatePinnedToCore(pushTask, "webpush", 4096, nullptr, 3, nullptr, 0);
}

}  // namespace Web
