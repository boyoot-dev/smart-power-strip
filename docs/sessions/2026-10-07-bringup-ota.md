# Bring-up, calibration and OTA policy — 2026-10-07

First session with the strip live on mains (at 10.0.0.234), updating it only over the air
through ThingsBoard (0.1.0 → 0.1.6). Decisions are also in [PLAN.md](../../PLAN.md); this
file records how we got there.

## Wiring vs. firmware
- The user wired current sensor 1 → GPIO 35, sensor 2 → GPIO 34, voltage → VP (36). The
  running 0.1.0 still read Out 1 from GPIO 39 (the broken VN pin), and the planned change
  had the two current pins swapped relative to the wiring. **Firmware was changed to match
  the wiring** (no mains rework). Out 1's phantom 90 W came from the floating GPIO 39.
- Confirmed with a kettle on each outlet: each sensor reads its own outlet.

## Readings that looked wrong
| Symptom | Cause | Fix |
|---|---|---|
| 106 V | `v_gain` was an uncalibrated placeholder | Multimeter: 230 V no-load / 220 V loaded → `v_gain` 1.126 |
| 0.1–0.27 A with nothing plugged in | ACS712-30A + ESP32 ADC noise floor (3–9 mV at the pin) | Count a load only at ≥ 5 W real power or ≥ 0.3 A |
| 55.6 Hz now and then | An ADC spike added a second zero crossing | Ignore crossings < 12 ms apart |
| Kettle at PF 0.92, watts ~8 % low | ZMPT101B phase shift, ~23° | Reactive power + `phase_deg` rotation, **Auto-calibrate phase** button → PF 1.00 |
| 40 W soldering iron read ~13 W | Real: the iron draws ~13 W (same on both outlets) | None; small loads are only reliable in watts |

The kettle (rated 1850–2200 W at 220–240 V → 26.2 Ω) served as the current reference:
8.50 A on Out 1 and 8.39 A on Out 2 vs. 8.40 A expected at 220 V. Current gains unchanged.

## OTA rollout — questions and answers
- *After delivery, how do I get the strip to take an update — can the customer unplug it?*
  Unplugging works but relies on the customer. ThingsBoard is the only remote access once
  a strip is behind a customer's router. Chosen plan:
  - the strip re-asks ThingsBoard for its assigned firmware every 30 min (TbLink
    `attribute_refresh_interval_ms`), plus a `fw_check` RPC;
  - it installs only inside a local-time window (default 03:00–05:00), or at once via
    `fw_install` / the local **Install now** button;
  - assign packages to a device profile so one assignment reaches every strip.
- *Customers with health equipment:* a per-strip **Install updates automatically** switch.
  Off = the strip never restarts by itself or on a remote command (`reboot` and
  `fw_install` RPCs refused); updates wait for **Install now** on the local page. A power
  cycle does not install a waiting update either.
- Relay hold through restart (`gpio_hold_en`) was offered and **deferred**.
- Every install restarts the strip: relays drop ~1 s and compressor-protected outlets then
  stay off for `min_off_s`.

## Observed
- ThingsBoard pushed the new assignment live in every test this session, so the 30-min
  re-check is a safety net, not the main path. Firmware without the gate (≤ 0.1.2)
  installs the moment it sees an assignment.
- A restarting strip leaves the browser's WebSocket "open" and the page frozen; the page
  now reconnects after 5 s of silence and reloads itself on a firmware change (0.1.7).
- TbLink pinned by commit hash in `lib_deps`; PlatformIO doesn't refresh an unpinned git
  dependency.
