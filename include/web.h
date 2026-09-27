#pragma once

// Local web UI + JSON API on port 80 (ESPAsyncWebServer). Reachable at
// http://powerstrip.local or via the setup AP. Live data and outlet commands go over a
// WebSocket at /ws: the page never polls. The server pushes a status message on every
// relay change (within ~50 ms) and once a second for the meter readings.

namespace Web {

void begin();

}  // namespace Web
