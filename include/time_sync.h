#pragma once

#include <Arduino.h>

namespace TimeSync {

// NTP with Egypt's timezone (EET, DST last Friday of April to last Thursday of October).
void begin();
bool synced();
String now();  // "YYYY-MM-DD HH:MM:SS" local, or "not synced"
int localHour();  // 0-23, or -1 if not synced

}  // namespace TimeSync
