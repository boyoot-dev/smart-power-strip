#pragma once

// USB serial console for provisioning without the setup AP. One command per line:
//   wifi <ssid> <password>
//   tb <mqtt_host> <mqtt_port> <token> [ota_http_host] [ota_http_port]
//   tls on|off
//   status | restart
// SSIDs with spaces: wrap in double quotes.

namespace Console {

void begin();

}  // namespace Console
