# Smart Power Strip — Project Plan

## Overview
A 2-outlet smart power strip for Egyptian mains (220 V / 50 Hz, Schuko-style sockets).
Each outlet can be switched on/off and reports voltage, current, power, power factor and
energy. Control and monitoring from a self-hosted local web UI **and** ThingsBoard, plus a
physical wall-switch per outlet. Built from hardware already on hand — no new purchases
beyond basic safety items (see "Safety").

Intended loads: **refrigerator** and **microwave** — but the firmware makes no assumption
about which appliance is on which outlet. Outlets are generic (**Out 1**, **Out 2**) and
every appliance-specific behaviour is a per-outlet setting in the local UI (see R10).

Sibling projects: [esp32-mqtt-generic-board](../esp32-mqtt-generic-board) (web UI,
automation rules, time manager to borrow from) and [TbLink](../TbLink) (ThingsBoard
connectivity + OTA, used as a library).

## Functional Requirements

### R1 — Outlet switching
- 2 independent outlets (Out 1, Out 2), each on its own relay. Display names are
  user-editable (e.g. "Fridge"); internal/telemetry keys stay `out1` / `out2`.
- Controlled from: local web UI, ThingsBoard RPC, physical switch, automation rules.
- Power-on behaviour configurable per outlet: `off` / `on` / `restore last` (default:
  restore last).

### R2 — Metering
- Shared line voltage (V rms) and frequency (Hz).
- Per outlet: current (A rms), real power (W), apparent power (VA), power factor,
  accumulated energy (kWh, resettable).
- Totals: combined current and power.

### R3 — Physical switches
- One household wall-switch module (same series as the sockets) per outlet, wired as a
  **low-voltage dry contact** to the ESP32 — never to mains.
- Any change of switch position **toggles** the outlet ("flip" semantics), so the switch
  never disagrees with remote control — its position has no meaning, only its movement.
  Same logic as a two-way (staircase / "ديفاتير") lamp circuit, with the web UI and
  ThingsBoard acting as the "other" switch.
- Switches on hand are maintained rockers: 2-terminal (on/off) and 3-terminal (two-way).
  **Use the 2-terminal ones**: one terminal to GPIO, one to GND. A 3-terminal switch also
  works using only COM + one leg; wiring both legs to two GPIOs would add wire-break
  detection but costs a GPIO per outlet — not worth it for v1.
- **Touch button alternative** (per-outlet setting "Physical control"): a TTP223 touch
  module taped behind a blank wall-plate cover. Powered from **3V3** (its output equals
  its supply, so 5 V would overdrive the GPIO), SIG to the same GPIO, A/B pads left open
  (momentary, active-high). Firmware reads it with a pull-down and toggles once a finger
  has stayed on the pad for the hold time (default 3 s), so a brush or wet cloth does
  nothing. Chosen over the ESP32's native touch pins, whose long leads next to relays and
  mains would pick up noise.
- Every manual toggle is logged with a timestamp (see R6).

### R4 — Protection (runs locally, works with no network)
All thresholds and flags below are per-outlet settings (R10); nothing is tied to a
specific outlet in firmware.
- **Total overcurrent**: combined current sustained above the strip limit (default 16 A)
  → shed outlets in **shed priority** order (per-outlet setting; the lowest-priority
  outlet is cut first, and a second outlet is cut only if still over the limit). Ties are
  broken by cutting the outlet drawing more current.
- **Per-outlet overcurrent**: configurable limit per outlet.
- Both use RMS over a configurable window (default 2 s) so compressor/transformer inrush
  doesn't trip them.
- **Compressor protection** (per-outlet flag, default off): enforce a minimum off-time
  (default 3 min, configurable) before the outlet can be re-energised, from any source —
  including restore after a mains blip. The UI shows "turning on in m:ss".
- **Voltage protection** (per-outlet flag, default off): cut the outlet when voltage
  leaves a safe window (default 180–250 V, configurable), restore after it has been back
  in range for a configurable delay. Same job as a plug-in "fridge protector".
- **Overheat protection** (strip-wide): DS18B20 probes (waterproof steel-tip type) on
  one 1-Wire bus on GPIO 32, one 4.7 kΩ pull-up to 3V3, each probe zip-tied to the wire
  **insulation** where it enters an ACS712/relay terminal (the terminals are where loose
  connections overheat). Any probe ≥ 70 °C (settable) → every outlet off and locked;
  unlocked once all probes are < 50 °C, outlets then stay off until turned on. A
  registered probe that stops answering (~6 s) also trips (fail-safe). Probes register
  themselves by ROM code and are named/forgotten on the local page. The ESP32's internal
  sensor was rejected: the D0WD-V3 has no usable one and it measures the chip itself.
  Bought for this (exception to "on-hand only"): 2× waterproof DS18B20, 1× 4.7 kΩ.
- A protection trip is latched or auto-restored per the outlet's **after-trip** setting
  (`stay off` / `auto-restore`); overcurrent defaults to `stay off`, voltage to
  `auto-restore`.

### R5 — Local web UI
- Live dashboard: per-outlet state + toggle, V/A/W/PF/kWh, totals.
- Settings: WiFi, ThingsBoard, strip-wide limits, and a per-outlet settings page (R10).
- Calibration page (R8).
- History charts (R7) and event log (R6).
- Arabic (default, right to left) and English, switched by a button on the page and
  remembered per browser. The ThingsBoard widget does the same.

### R6 — Event log
- Every relay change is recorded with timestamp, outlet, new state and **source**:
  `boot`, `button`, `web`, `thingsboard`, `rule`, `overcurrent`, `voltage`,
  `compressor_delay_release`.
- Stored locally (ring buffer, last ~500 events, in LittleFS) and sent to ThingsBoard as
  an event telemetry key.
- Events before NTP sync are stamped with uptime and flagged as such.

### R7 — Local history
- Hourly energy + average/peak power per outlet for **7 days** (flash, written once per
  hour).
- Daily energy totals for 90 days (flash).
- Last 24 h at 1-minute resolution (RAM only, lost on reboot).
- Budget: ~15 KB RAM, <20 KB flash, one small write per hour — negligible for the ESP32.

### R8 — Calibration
- Per-channel gain for voltage and each current channel, plus a phase-correction factor,
  entered or auto-computed from the web UI against a reference (multimeter for voltage,
  clamp meter or known resistive load for current).
- Stored in NVS.
- **First unit, 2026-10-07:** `v_gain` 1.126 (multimeter: 230 V no-load, 220 V under
  load), current gains left at the 1/33 default (a 26.2 Ω kettle read within 1.2 % on
  both outlets), `phase_deg` 23.4 (auto-calibrated on the kettle: PF 0.917 → 1.00).

### R9 — ThingsBoard integration (via TbLink)
- Telemetry, RPC control, shared-attribute config, OTA updates.
- Dashboard widget(s) per outlet (adapted from the generic board's widgets).
- **OTA rollout (decided 2026-10-07):** assign the package in ThingsBoard (to a
  `smart-power-strip` device profile, so one assignment covers every customer). The strip
  re-asks ThingsBoard for its assigned firmware every 30 min (ThingsBoard doesn't reliably
  push a new assignment to a connected device), and the `fw_check` RPC does it on demand.
  Installing restarts the strip (relays drop ~1 s; compressor outlets then wait
  `min_off_s`), so it only installs inside a local-time window (default 03:00–05:00), or
  at once via the `fw_install` RPC or the local Install now button.
- **Auto-update off (per strip, local UI)** — for customers with equipment that must never
  lose power unexpectedly (e.g. medical): the strip never restarts by itself or on a
  remote command (`reboot` / `fw_install` RPCs are refused). Updates wait until someone
  clicks Install now on the local page. Local Restart, settings that need a restart, and
  pulling the plug still work. A crash/watchdog reset can't be ruled out by a setting.
- ThingsBoard client attributes `auto_update` and `fw_waiting` show each strip's state.

### R10 — Per-outlet configuration (local UI)
Every appliance-specific behaviour lives here, so the user decides after plugging things
in. Stored in NVS, also exposed as ThingsBoard shared attributes.

| Setting | Values | Default |
|---|---|---|
| Name | free text | "Out 1" / "Out 2" |
| Power-on behaviour | off / on / restore last | restore last |
| Current limit | A | 16 |
| Shed priority | 1 (cut last) … 2 (cut first) | Out 1 = 1, Out 2 = 2 |
| Compressor protection | on/off + min off-time | off, 3 min |
| Voltage protection | on/off + min/max V + restore delay | off, 180–250 V, 3 min |
| After overcurrent trip | stay off / auto-restore | stay off |
| Physical control | rocker switch / touch button / disabled | rocker switch |
| Touch hold time | 0–10 s | 3 s |

**Presets** (one-click fill-ins, every field still editable afterwards):
- *Fridge / freezer / AC*: compressor + voltage protection on, shed priority "cut last".
- *Microwave / kettle / heater*: protections off, shed priority "cut first".
- *Generic*: the defaults above.

Strip-wide settings: total current limit (default 16 A), overcurrent window, telemetry
interval.

### Later (not v1)
- **Small display** on the strip (SSD1306 OLED on I2C — pins reserved now).
- Time-of-day schedules / automation rules (port from the generic board).
- Idle auto-off (e.g. outlet drawing standby power only for N hours).

## Hardware (on hand)

| Part | Qty | Role | Notes |
|---|---|---|---|
| ESP32 DevKit V1, 30-pin (classic ESP32) | 1 | Controller | Same board as generic board's `esp32dev` profile |
| Hi-Link HLK-20M05 (5 V / 4 A, isolated) | 1 | Power supply | Far more than needed (~0.7 A worst case) |
| Relay module, TCERL TC90-05D-S-C (30 A contacts, 5 V coil, opto-isolated input) | 2 | Outlet switching | |
| ACS712ELC-30A current sensor module | 2 | Per-outlet current | 66 mV/A, ±30 A range |
| ZMPT101B voltage sensor module | 1 | Line voltage (shared) | Transformer-isolated |
| Wall-switch modules | 2 | Manual toggle | Dry-contact only |
| Schuko socket modules | 2 | Outlets | |
| Multimeter + clamp meter | — | Calibration reference | |

### Isolation
With this set of parts the whole low-voltage side is **galvanically isolated** from mains:
HLK-20M05 (isolated SMPS), ACS712 (Hall-effect, isolated), ZMPT101B (transformer), relay
module (optocoupler + relay coil). The ESP32 can be on USB while mains is connected, *as
long as nothing mains-side is exposed* — the module screw terminals are live and bare.

### Current limit
16 A total is the ceiling, set by the plug, cord and wall circuit — not by the relays
(30 A). The ACS712 module's screw terminals and PCB traces are the weakest link on the
board and must be validated by burn-in (see Milestones). Fridge + microwave together draw
roughly 7–10 A, so there's plenty of margin in practice.

## Pin Map (ESP32 DevKit V1, 30-pin)

| GPIO | Function | Why this pin |
|---|---|---|
| 36 (VP) | ZMPT101B out (via divider) | ADC1, input-only |
| 35 | ACS712 1 out (via divider) | ADC1, input-only (moved from 39 on 2026-10-04: VN pin broken on this board) |
| 34 | ACS712 2 out (via divider) | ADC1, input-only |
| 26 | Relay 1 IN | No boot glitch, not a strapping pin |
| 27 | Relay 2 IN | No boot glitch, not a strapping pin |
| 25 | Switch 1 (to GND, internal pull-up) | Safe general-purpose I/O |
| 33 | Switch 2 (to GND, internal pull-up) | Safe general-purpose I/O |
| 32 | DS18B20 1-Wire bus (4.7 kΩ pull-up to 3V3) | Free, bidirectional, not strapping |
| 21 / 22 | I2C SDA / SCL — **reserved for display** | Default I2C pins |
| 2 | Onboard LED — status | |

ADC2 pins are avoided entirely (unusable while WiFi is active). Strapping pins
(0, 2 as input, 5, 12, 15) and UART0 (1, 3) are avoided for relays so outlets don't
click during boot.

The same table lives in [include/pins.h](include/pins.h) — that file is the source of
truth.

## Wiring

Full diagrams (both sides, per-pin tables, bring-up order): [docs/wiring.md](docs/wiring.md).

### Mains side (≥1.5 mm² wire, 2.5 mm² preferred; never through breadboard/perfboard)
```
Plug L ──[fuse/breaker 16A]──┬── ACS712 1 ── Relay 1 COM ; Relay 1 NO ── Out 1 L
                             ├── ACS712 2 ── Relay 2 COM ; Relay 2 NO ── Out 2 L
                             ├── HLK-20M05 AC-L
                             └── ZMPT101B L
Plug N ──────────────────────┬── Out 1 N, Out 2 N
                             ├── HLK-20M05 AC-N
                             └── ZMPT101B N
Plug PE ─────────────────────── Out 1 PE, Out 2 PE   (never switched, never broken)
```
- Use the relay's **NO** contact: if the ESP32 dies or reboots, outlets go off. The
  "restore last" policy turns them back on after boot, with the compressor delay applied
  to any outlet that has compressor protection enabled.
- Schuko plugs are reversible, so the "L" above is only live about half the time; the
  other half the relay switches neutral. It works the same electrically, but **an "off"
  outlet may still have live on one pin** — always unplug before servicing an appliance.

### Low-voltage side (perfboard OK)
- HLK +5 V → ESP32 VIN, relay modules VCC, ACS712 VCC, ZMPT101B VCC. Common GND.
- ACS712 and ZMPT101B outputs swing around 2.5 V on a 5 V supply, up to ~4 V — **too
  much for the ESP32 ADC** (3.3 V max, linear to ~2.5 V). Each output goes through a
  divider: **10 kΩ top / 10 kΩ bottom** (×0.5) → roughly 0.5–2.0 V, inside the ADC's
  linear range even at the 16 A peak (a 10k/20k "5 V→3.3 V" divider would push peaks to
  ~2.7 V, into the non-linear zone). Originally 10k/15k (×0.6, 0.6–2.4 V); switched to
  10k/10k on 2026-10-04 to use parts on hand — ~17 % less signal, more headroom. Add a **10 nF** cap from ADC pin to GND: it feeds
  the ADC's sample-and-hold when the metering task switches channels and filters relay
  noise. Not 100 nF — with the divider's ~5 kΩ source impedance that makes a ~320 Hz
  low-pass that eats 13–30 % of the 3rd–5th harmonics, so non-sinusoidal loads
  (microwave) would read low; 10 nF puts the corner at ~3.2 kHz.
- Relay modules: set the trigger jumper to **High-level trigger** so a 3.3 V GPIO drives
  them reliably (in low-level mode a 3.3 V "HIGH" against a 5 V module often fails to
  release the relay). Keep JD-VCC jumpered to VCC unless separating the coil supply.
- Switches: one leg to GPIO, the other to GND. Add a 100 nF cap at the GPIO for noise
  immunity (the relays are right next to these wires).

## Firmware Architecture

PlatformIO + Arduino (same stack as the generic board). Modules:

| Module | Responsibility |
|---|---|
| `metering` | Dedicated FreeRTOS task (core 1). Interleaved V/I sampling over a whole number of mains cycles, per outlet; DC-offset removal in software (ACS712 zero drifts with the 5 V rail and temperature); Vrms, Irms, P, S, PF, frequency (zero crossings); energy integration; noise-floor gate (reports 0 A unless the load draws ≥ 5 W real power or ≥ 0.3 A; no-load noise is 0.1–0.27 A RMS); zero crossings < 12 ms apart ignored (ADC spikes); reactive power from V delayed a quarter cycle, and real power rotated by a calibrated `phase_deg` to cancel the ZMPT101B phase shift (kettle read PF 0.92 without it; Auto-calibrate phase button with a heater running) |
| `outlets` | Owns relay state. Single entry point `request(outlet, state, source)` that applies compressor delay / protection locks and emits an event |
| `protection` | Overcurrent (total + per outlet, with shed-priority ordering) and voltage window, evaluated on every metering cycle, driven entirely by per-outlet settings |
| `switches` | Debounced edge detection → `outlets.request(toggle, button)`; any edge toggles (maintained rockers) |
| `event_log` | Ring buffer in LittleFS + forwarding to ThingsBoard |
| `history` | 1-min RAM buffer, hourly/daily buckets in LittleFS |
| `config` | NVS via `Preferences` |
| `network`, `time` | Copied/adapted from the generic board (AP setup mode, STA, static IP, NTP) |
| `tb` | Thin glue over TbLink: telemetry, RPC, attributes, OTA |
| `web` | ESPAsyncWebServer + JSON API + static pages |

### Task layout (dual core)
- **Core 1:** `metering` alone. It samples the ADC in tight 200 ms windows, so WiFi
  interrupts (which run on core 0) never disturb the sample timing.
- **Core 0:** `control` (prio 4, 10 ms tick: switches, compressor-delay release, LED),
  `net` (prio 2: WiFi, NTP, TbLink telemetry/RPC/OTA — the only task that can block),
  and a USB serial `console` for provisioning (`wifi …`, `tb …`, `tls on|off`,
  `status`). AsyncTCP (web UI, prio 10) is left unpinned, same as the generic board.
- **MQTT over TLS:** the handshake verifies the Let's Encrypt ECDSA P-384 chain in
  software and holds core 0 for ~7 s. So the task watchdog timeout is raised to 30 s
  (the default 5 s reset the chip), only `net` ever touches the MQTT client (other tasks
  read a `cloudUp` flag), and TLS waits for NTP (certificate dates fail at 1970).
- Partition table is `min_spiffs.csv` (2× 1.9 MB OTA app slots). It can't be changed
  over OTA, so it was fixed before the first deployment.

### Metering approach
The classic EmonLib "calcVI" method: sample voltage and current as close together as
possible, multiply the instantaneous values for real power, and take RMS of each. With a
2-outlet strip and one ADC, the task alternates: ~10 mains cycles (200 ms) of V+I_1 pairs,
then ~10 cycles of V+I_2. Each outlet updates ~2.5×/s — plenty for switching decisions,
and telemetry is averaged down from that anyway.

**Expected accuracy** after calibration: roughly ±3–5 % on power for loads above ~0.5 A
(measured: ~1 % on a 1.9 kW kettle). Loads under ~5 W / 0.3 A (phone chargers, standby)
read as 0; a 13 W soldering iron read correctly in watts but its amps/PF are noise. This is a household monitor, not a
billing meter — fine for a fridge and a microwave.

### ThingsBoard keys (draft)
- Telemetry: `voltage`, `frequency`, `out1_state`, `out1_current`, `out1_power`,
  `out1_pf`, `out1_energy`, same for `out2_`, `total_current`, `total_power`,
  `out1_event` / `out2_event`.
- RPC: `setOutlet {outlet, state}`, `setOut1`/`setOut2` (bool), `getOut1`/`getOut2`,
  `getState`, `resetEnergy {outlet}`, `reboot`, `fw_check`, `fw_install` (the last two
  and `reboot` see R9; `reboot`/`fw_install` are refused while auto-update is off).
- Client attributes: `ip`, `rssi`, `uptime_s`, `out1_name`/`out2_name`, `temp_N_name`,
  `auto_update`, `fw_waiting`.
- Shared attributes: limits, protection flags, telemetry interval.
- Telemetry cadence: every 10 s, plus immediately on relay change or a >10 % power jump.

## Safety
- **Mains never touches breadboard or perfboard.** Only wire + screw terminals.
- **One exception to "no new hardware":** a 16 A fuse or breaker on the input.
  Firmware overcurrent protection is not a substitute — it can't help if a relay welds
  shut or the ESP32 crashes. Easiest path: build inside an existing power strip that has
  a built-in overload breaker, reusing its enclosure, cord and breaker.
- Non-conductive enclosure; modules mounted on standoffs; strain relief on the input cord.
- Earth goes straight through to both sockets — the fridge and microwave need it.
- Do low-voltage development first (M1) with mains disconnected.
- Burn-in before real use: microwave at full power for 10+ min, then check the ACS712
  terminals, relay terminals and wire joints for heat.

## Milestones

- **M0 — Repo scaffold** *(this commit)*: PLAN, pin map, PlatformIO project that builds.
- **M1 — Bench, no mains**: relays + switches + web UI + ThingsBoard control + event log.
  Measure the ACS712 zero-current noise floor. *(Measured: 0.1–0.27 A RMS.)*
- **M2 — Metering**: sampling task, RMS/power math, calibration page. First mains test
  with a lamp/kettle behind the enclosure. *(Done 2026-10-07: calibrated, see R8; OTA
  update flow tested end to end, 0.1.1 → 0.1.6.)*
- **M3 — Protection + per-outlet settings**: overcurrent with shed priority, compressor
  delay, voltage window, presets.
- **M4 — History + dashboard**: local charts, ThingsBoard widgets.
- **M5 — Enclosure + burn-in**, then real use with the fridge and microwave.
- **v2**: display, schedules, idle auto-off.

## Open Questions
- None blocking. Resolved: both ACS712 are 30A, 2 relay modules on hand, clamp meter
  available for calibration, outlet roles configured from the UI rather than fixed.
