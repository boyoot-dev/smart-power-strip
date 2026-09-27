#pragma once

// Core-0 control task, 10 ms tick: wall-switch / touch-button input (PLAN.md R3),
// compressor-delay release and the status LED. Kept apart from the network task so a
// blocking MQTT reconnect or OTA download never delays a switch press.

namespace Control {

void begin();

}  // namespace Control
