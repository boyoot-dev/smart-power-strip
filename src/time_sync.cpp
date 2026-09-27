#include "time_sync.h"

#include <time.h>

namespace {

// Any synced clock is past this; an unsynced ESP32 clock starts at 1970.
constexpr time_t kMinValidEpoch = 1700000000;

}  // namespace

namespace TimeSync {

void begin() {
  configTzTime("EET-2EEST,M4.5.5/0,M10.5.4/24", "pool.ntp.org", "time.google.com");
}

bool synced() { return time(nullptr) >= kMinValidEpoch; }

String now() {
  if (!synced()) {
    return "not synced";
  }
  time_t t = time(nullptr);
  struct tm local;
  localtime_r(&t, &local);
  char buf[24];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &local);
  return buf;
}

}  // namespace TimeSync
