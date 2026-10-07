#pragma once

#include <stdint.h>

// Pin map for ESP32 DevKit V1 (30-pin, classic ESP32). Single source of truth - see
// PLAN.md "Pin Map" for why each pin was chosen. Analog inputs are ADC1 only (ADC2 is
// unusable while WiFi is active); relays avoid strapping pins so outlets don't click
// during boot.

constexpr uint8_t kPinVoltageSense = 36;  // ZMPT101B via 10k/10k divider
constexpr uint8_t kPinCurrent1     = 35;  // ACS712 Out 1 via 10k/10k divider
constexpr uint8_t kPinCurrent2     = 34;  // ACS712 Out 2 via 10k/10k divider
// GPIO 39 (VN) unused: the header pin on this board is broken.

constexpr uint8_t kPinRelay1  = 26;  // relay module IN, High-level trigger
constexpr uint8_t kPinRelay2  = 27;
constexpr uint8_t kPinSwitch1 = 25;  // rocker to GND (pull-up) or TTP223 touch (pull-down)
constexpr uint8_t kPinSwitch2 = 33;

constexpr uint8_t kPinOneWire = 32;  // DS18B20 probes, all on one bus, 4.7k pull-up to 3V3

constexpr uint8_t kPinI2cSda = 21;  // reserved for v2 display
constexpr uint8_t kPinI2cScl = 22;

constexpr uint8_t kPinStatusLed = 2;
