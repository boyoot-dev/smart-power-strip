#pragma once

#include <Arduino.h>

namespace TimeSync {

// NTP with Egypt's timezone (EET, DST last Friday of April to last Thursday of October).
void begin();
bool synced();
String now();  // "YYYY-MM-DD HH:MM:SS" local, or "not synced"

}  // namespace TimeSync
