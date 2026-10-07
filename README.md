# Smart Power Strip

ESP32 firmware for a 2-outlet smart power strip (220 V / 50 Hz): per-outlet switching and
energy monitoring (V, A, W, PF, kWh), controlled from a local web UI, ThingsBoard, and
physical wall switches — with local overcurrent, compressor and voltage protection.

**Status: M1 + M2 done** (switching, web UI, ThingsBoard, metering, overheat lockout,
scheduled OTA updates).
Overcurrent and voltage protection (M3) and history (M4) are not implemented yet. See
[PLAN.md](PLAN.md) for requirements, hardware, wiring, and milestones.

> ⚠️ This project involves mains voltage. Read the Safety section of PLAN.md before
> building anything.

## Build

```bash
python -m platformio run -e esp32dev
python -m platformio run -e esp32dev -t upload
```

## First-time setup

1. Flash over USB, join the `PowerStrip-XXXX` WiFi (password `powerstrip`), and enter
   your WiFi and ThingsBoard details on the page that opens.
2. Calibrate under *Settings → Calibration* with a kettle running: set the voltage gain
   from a multimeter reading, then click **Auto-calibrate phase**. See
   [docs/wiring.md](docs/wiring.md) for the full bring-up checklist.

## Releasing firmware (OTA via ThingsBoard)

1. Bump `kFwVersion` in [include/firmware_version.h](include/firmware_version.h) and build.
2. In ThingsBoard, add an OTA package with title `smart-power-strip` and the same version,
   byte-for-byte, uploading `.pio/build/esp32dev/firmware.bin`.
3. Assign it, ideally to the strips' device profile so one assignment covers them all.

Strips notice a new assignment within 30 minutes (or at once with the `fw_check` RPC) and
install it inside their update window, 03:00–05:00 local by default; `fw_install` installs
right away. Installing restarts the strip, which drops both outlets for about a second.
A strip with **Install updates automatically** turned off (for equipment that must never
lose power unexpectedly) never restarts by itself or on a remote command: the update shows
as ready on its local page and installs only when someone clicks **Install now**.

## License

[MIT](LICENSE)
