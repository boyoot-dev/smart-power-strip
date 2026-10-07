#pragma once

// Single source of truth for the firmware identity handed to TbLink for OTA.
// kFwVersion must match the ThingsBoard OTA package version byte-for-byte; bump it on
// every release meant to be pushed via OTA. kFwTitle must match the package title, or
// TbLink refuses the package (stops a binary for another board being flashed here).
constexpr char kFwTitle[] = "smart-power-strip";
constexpr char kFwVersion[] = "0.1.7";
