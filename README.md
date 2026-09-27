# Smart Power Strip

ESP32 firmware for a 2-outlet smart power strip (220 V / 50 Hz): per-outlet switching and
energy monitoring (V, A, W, PF, kWh), controlled from a local web UI, ThingsBoard, and
physical wall switches — with local overcurrent, compressor and voltage protection.

**Status: planning / M0 scaffold.** See [PLAN.md](PLAN.md) for requirements, hardware,
wiring, and milestones.

> ⚠️ This project involves mains voltage. Read the Safety section of PLAN.md before
> building anything.

## Build

```bash
python -m platformio run -e esp32dev
python -m platformio run -e esp32dev -t upload
```

## License

[MIT](LICENSE)
