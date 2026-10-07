# CLAUDE.md

2-outlet smart power strip firmware (ESP32 DevKit V1, Arduino + PlatformIO, Egypt 220 V / 50 Hz).

- **[PLAN.md](PLAN.md) is the source of truth** — requirements, hardware on hand, pin map,
  wiring (incl. 10k/10k ADC dividers), firmware modules, milestones. Read it first and keep
  it updated when decisions change.
- [include/pins.h](include/pins.h) is the source of truth for GPIO assignments.
- [docs/sessions/](docs/sessions/) holds summaries of past planning sessions (the Q&A and
  reasoning behind PLAN.md) — read when the user refers to an earlier discussion.
- Outlets are generic (`out1` / `out2`). Never hardcode appliance-specific behaviour to an
  outlet — every protection (compressor delay, voltage window, shed priority, limits) is a
  per-outlet setting (PLAN.md R10).
- Build only from hardware already on hand unless the user says otherwise.
- Mains safety matters: flag anything that routes mains through perfboard/breadboard or
  weakens the isolation described in PLAN.md.
- Sibling repos: `../esp32-mqtt-generic-board` (borrow web UI, time manager, automation
  rules) and `../TbLink` (ThingsBoard connectivity + OTA library, pulled via lib_deps pinned to a commit hash: after pushing a TbLink change, bump the hash in platformio.ini or the old cached copy is used; see the TbLink README).

## Build
```bash
python -m platformio run -e esp32dev
python -m platformio run -e esp32dev -t upload
```
