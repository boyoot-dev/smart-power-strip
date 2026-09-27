#include "console.h"

#include <Arduino.h>
#include <WiFi.h>

#include "config.h"
#include "firmware_version.h"
#include "net.h"
#include "outlets.h"

namespace {

constexpr int kMaxArgs = 6;

// Splits on spaces, honouring "double quoted" arguments.
int tokenize(String& line, String* args) {
  int n = 0;
  int i = 0, len = line.length();
  while (i < len && n < kMaxArgs) {
    while (i < len && line[i] == ' ') i++;
    if (i >= len) break;
    int start;
    if (line[i] == '"') {
      start = ++i;
      while (i < len && line[i] != '"') i++;
      args[n++] = line.substring(start, i++);
    } else {
      start = i;
      while (i < len && line[i] != ' ') i++;
      args[n++] = line.substring(start, i);
    }
  }
  return n;
}

void run(String line) {
  String a[kMaxArgs];
  int n = tokenize(line, a);
  if (n == 0) return;

  if (a[0] == "wifi" && n >= 2) {
    WifiConfig c = Config::wifi();
    strlcpy(c.ssid, a[1].c_str(), sizeof(c.ssid));
    strlcpy(c.password, n >= 3 ? a[2].c_str() : "", sizeof(c.password));
    Config::saveWifi(c);
    Serial.println("WiFi saved, restarting");
    Net::scheduleRestart();
  } else if (a[0] == "tb" && n >= 4) {
    TbConfig c = Config::tb();
    strlcpy(c.host, a[1].c_str(), sizeof(c.host));
    c.port = a[2].toInt() ?: 1883;
    strlcpy(c.token, a[3].c_str(), sizeof(c.token));
    strlcpy(c.http_host, n >= 5 ? a[4].c_str() : "", sizeof(c.http_host));
    c.http_port = n >= 6 ? (a[5].toInt() ?: 8080) : 8080;
    Config::saveTb(c);
    Serial.println("ThingsBoard saved, restarting");
    Net::scheduleRestart();
  } else if (a[0] == "tls" && n >= 2) {
    static TbTlsConfig c;  // 2.5 KB, keep it off the console task stack
    c = Config::tbTls();
    c.use_tls = a[1] == "on";
    Config::saveTbTls(c);
    Serial.printf("MQTT TLS %s, restarting\n", c.use_tls ? "on" : "off");
    Net::scheduleRestart();
  } else if (a[0] == "status") {
    const TbConfig& t = Config::tb();
    Serial.printf("%s v%s | WiFi '%s' %s %s | TB %s:%u%s token %s, OTA %s:%u | net state %d\n",
                  kFwTitle, kFwVersion, Config::wifi().ssid,
                  WiFi.status() == WL_CONNECTED ? "connected" : "not connected",
                  WiFi.localIP().toString().c_str(), t.host, t.port, Config::tbTls().use_tls ? " (TLS)" : "",
                  t.token[0] ? "set" : "unset", t.http_host[0] ? t.http_host : t.host,
                  t.http_port, static_cast<int>(Net::state()));
    for (int i = 0; i < kOutletCount; i++) {
      Serial.printf("  out%d '%s': %s\n", i + 1, Config::outlet(i).name,
                    Outlets::status(i).on ? "ON" : "OFF");
    }
  } else if (a[0] == "restart") {
    Net::scheduleRestart(100);
  } else {
    Serial.println("commands: wifi <ssid> <pass> | tb <host> <port> <token> [ota_host] "
                   "[ota_port] | tls on|off | status | restart");
  }
}

void consoleTask(void*) {
  String line;
  for (;;) {
    while (Serial.available()) {
      char ch = Serial.read();
      if (ch == '\n' || ch == '\r') {
        if (line.length()) run(line);
        line = "";
      } else if (line.length() < 200) {
        line += ch;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

}  // namespace

namespace Console {

void begin() {
  xTaskCreatePinnedToCore(consoleTask, "console", 4096, nullptr, 1, nullptr, 0);
}

}  // namespace Console
