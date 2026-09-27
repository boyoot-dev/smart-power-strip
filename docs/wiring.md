# Wiring Diagram

Full circuit for the 2-outlet strip, both mains and low-voltage sides. Derived from
[PLAN.md](../PLAN.md) (Wiring, Pin Map) and [include/pins.h](../include/pins.h) — if they
disagree, those win; update this file to match.

## 1. Overview — where the isolation barrier is

Every module straddles the barrier: its mains terminals on one side, its low-voltage pins
on the other. Nothing else crosses it.

```
          MAINS 220 V  (wire + screw terminals only)  ║  LOW VOLTAGE  (perfboard OK)
                                                      ║
  Plug L/N ──► HLK-20M05   [AC-L, AC-N]               ║  [+Vo, −Vo]      ──► 5 V rail / GND
  L        ──► ACS712 #1   [IP+ → IP−]                ║  [VCC, GND, OUT] ──► GPIO 39
  L        ──► ACS712 #2   [IP+ → IP−]                ║  [VCC, GND, OUT] ──► GPIO 34
  L/N      ──► ZMPT101B    [L, N]                     ║  [VCC, GND, OUT] ──► GPIO 36
  ACS #1   ──► Relay 1     [COM → NO] ──► Socket 1    ║  [VCC, GND, IN]  ◄── GPIO 26
  ACS #2   ──► Relay 2     [COM → NO] ──► Socket 2    ║  [VCC, GND, IN]  ◄── GPIO 27
                                                      ║
                                                      ║  Switch/touch 1  ──► GPIO 25
                                                      ║  Switch/touch 2  ──► GPIO 33
                                                      ║  Temp probes ×2  ◄─► GPIO 32
                                            isolation ║

Temperature probes are the one part that sits physically on the mains side: their steel
tips are zip-tied to wire insulation at the terminals, but they are sealed and their cable
carries only 3.3 V back to the ESP32 (section 3.5).
```

## 2. Mains side (220 V / 50 Hz)

≥1.5 mm² wire (2.5 mm² preferred). **Never through breadboard or perfboard.**

```
 PLUG

  L ──[ 16 A FUSE / BREAKER ]──┬──► ACS712 #1 [IP+ ─ IP−] ──► RELAY 1 [COM ─ NO] ──► SOCKET 1  L
                               │
                               ├──► ACS712 #2 [IP+ ─ IP−] ──► RELAY 2 [COM ─ NO] ──► SOCKET 2  L
                               │
                               ├──► HLK-20M05  AC-L
                               │
                               └──► ZMPT101B   L (input terminal)

  N ───────────────────────────┬──────────────────────────────────────────────────► SOCKET 1  N
                               ├──────────────────────────────────────────────────► SOCKET 2  N
                               ├──► HLK-20M05  AC-N
                               └──► ZMPT101B   N (input terminal)

  PE ──────────────────────────┬──────────────────────────────────────────────────► SOCKET 1  PE
                               └──────────────────────────────────────────────────► SOCKET 2  PE
```

- Relay **NC** terminals: leave unconnected (outlets fail **off** if the ESP32 dies).
- Wire both ACS712s the same way round (supply into IP+) so power has the same sign on
  both channels.
- PE is never switched or broken.
- **Temperature probes (T1, T2):** zip-tie one steel tip to the L wire's insulation where it
  enters each outlet's ACS712 / relay terminals, the spot most likely to overheat from a
  loose screw. Never on bare metal.
- **Wall switches / touch buttons** sit beside each socket but connect only to the ESP32
  (section 3.4), never to anything in this drawing.
- Schuko plugs are reversible: "L" above is only live about half the time, so an "off"
  outlet may still have live on one pin. Unplug before servicing an appliance.

## 3. Low-voltage side

### 3.1 Power rails (from HLK-20M05)

```
  HLK-20M05 +Vo (5 V) ──── 5 V RAIL ──┬──► ESP32 VIN
                                      ├──► RELAY 1   VCC (DC+)
                                      ├──► RELAY 2   VCC (DC+)
                                      ├──► ACS712 #1 VCC
                                      ├──► ACS712 #2 VCC
                                      └──► ZMPT101B  VCC

  HLK-20M05 −Vo ────────── GND RAIL ──┬──► ESP32 GND
                                      ├──► RELAY 1   GND (DC−)
                                      ├──► RELAY 2   GND (DC−)
                                      ├──► ACS712 #1 GND
                                      ├──► ACS712 #2 GND
                                      ├──► ZMPT101B  GND
                                      ├──► bottom of all 3× 15 kΩ divider resistors
                                      ├──► all 3× 10 nF ADC caps
                                      ├──► both 100 nF switch caps
                                      ├──► one leg of each wall switch (or touch module GND)
                                      └──► temperature probes' black wire

  ESP32 3V3 pin (its own regulator, not the 5 V rail)
                                      ├──► touch modules VCC, if used
                                      ├──► temperature probes' red wire
                                      └──► the 4.7 kΩ 1-Wire pull-up
```

One common ground for everything. Touch modules and probes run from **3V3**, because their
signal level equals their supply and 5 V would overdrive the ESP32 pins.

### 3.2 Analog inputs — same divider ×3

```
                     10 kΩ
  SENSOR OUT ──────[██████]──────●──────────────► ESP32 GPIO
                                 │
                        ┌────────┴────────┐
                        │                 │
                     [15 kΩ]           [10 nF]
                        │                 │
                       GND               GND
```

| Sensor OUT | → divider → | ESP32 pin | Measures |
|---|---|---|---|
| ZMPT101B OUT | 10k / 15k / 10 nF | **GPIO 36 (VP)** | Line voltage (shared) |
| ACS712 #1 OUT | 10k / 15k / 10 nF | **GPIO 39 (VN)** | Out 1 current |
| ACS712 #2 OUT | 10k / 15k / 10 nF | **GPIO 34** | Out 2 current |

- ×0.6 ratio keeps the signal at ~0.6–2.4 V, inside the ESP32 ADC's linear range even at
  the 16 A peak. Never connect a sensor OUT straight to a GPIO.
- 10 nF, not 100 nF — see PLAN.md (100 nF would attenuate load harmonics).
- Keep the 15 kΩ and 10 nF close to the ESP32 pin.
- **ZMPT101B trim pot:** with 220 V applied, adjust it so the module output swings about
  ±1 V around its midpoint (scope, or the firmware's raw-ADC view) — enough headroom for
  250 V without clipping. Do this before calibration.

### 3.3 Relays

```
  ESP32 GPIO 26 ────────────────► RELAY 1 IN
  ESP32 GPIO 27 ────────────────► RELAY 2 IN
```

Jumpers on **each** relay module:
- Trigger select → **H (high-level trigger)** — 3.3 V GPIO can't reliably release a
  low-level-trigger 5 V module.
- **JD-VCC ↔ VCC** jumper → leave fitted.

### 3.4 Wall switches or touch buttons (low voltage only — never mains)

Each outlet has one physical control, chosen on the local web page under
*Settings → Outlets → Physical control*: **Rocker switch**, **Touch button** or
**Disabled**.

**Rocker switch** (any flip toggles the outlet):

```
                        SW1 (2-terminal rocker)
  ESP32 GPIO 25 ──●──────────o/ o──────────── GND
                  │
               [100 nF]
                  │
                 GND

                        SW2 (2-terminal rocker)
  ESP32 GPIO 33 ──●──────────o/ o──────────── GND
                  │
               [100 nF]
                  │
                 GND
```

No external pull-up — the firmware enables the internal one. Keep switch wires away from
the relay/mains wiring, or twist each pair.

**Touch button** (TTP223 module behind a blank plate cover; hold a finger on it for the
hold time, 3 s by default, to toggle):

```
  TTP223 VCC ─────────► ESP32 3V3   (not 5 V)
  TTP223 GND ─────────► GND
  TTP223 SIG / I/O ───► GPIO 25 (Out 1) or GPIO 33 (Out 2)
```

- No 100 nF cap and no resistor; the firmware uses the internal pull-down.
- Leave the module's A/B solder pads open (momentary, active-high).
- Tape the pad flat against the inside of the cover, no air gap.
- Don't touch the pad while the strip powers up; the module calibrates then.

### 3.5 Temperature probes (overheat protection)

```
  ESP32 3V3 ──┬──────────── red   (probe 1) ── red   (probe 2)
              │
           [4.7 kΩ]   one resistor for all probes
              │
  ESP32 GPIO 32 ┴────────── data  (probe 1) ── data  (probe 2)   usually yellow
  ESP32 GND ─────────────── black (probe 1) ── black (probe 2)
```

Zip-tie each steel tip to the **insulation** of the wire where it enters an ACS712 or
relay screw terminal, never to bare metal. Check the wire colours on the probe's listing:
reversed power makes a DS18B20 very hot within seconds.

### 3.6 Other ESP32 pins

| Pin | Use |
|---|---|
| GPIO 2 | Onboard status LED — nothing to wire |
| GPIO 21 / 22 | I2C SDA / SCL — reserved for a future OLED, leave free |

## 4. ESP32 pin summary

| ESP32 pin | Connects to |
|---|---|
| VIN | HLK +5 V |
| GND | Common GND |
| GPIO 36 (VP) | ZMPT101B OUT via divider |
| GPIO 39 (VN) | ACS712 #1 OUT via divider |
| GPIO 34 | ACS712 #2 OUT via divider |
| GPIO 26 | Relay 1 IN |
| GPIO 27 | Relay 2 IN |
| GPIO 25 | Out 1 control: rocker (other leg to GND, 100 nF to GND) or touch module SIG |
| GPIO 33 | Out 2 control: rocker (other leg to GND, 100 nF to GND) or touch module SIG |
| GPIO 32 | Temperature probes' data wires (all), 4.7 kΩ to 3V3 |
| 3V3 | Temperature probes' red wires, 4.7 kΩ pull-up, touch modules VCC |
| GPIO 2 | Onboard LED |
| GPIO 21 / 22 | Reserved (I2C) |

## 5. Parts

| Part | Qty | Where |
|---|---|---|
| Waterproof DS18B20 probe | 2 | One per outlet, at its ACS712 / relay terminals |
| 4.7 kΩ | 1 | GPIO 32 → 3V3 (one for all probes) |
| TTP223 touch module (optional) | 0–2 | Behind a blank plate cover, instead of a rocker |
| 10 kΩ | 3 | Divider top (sensor OUT → ADC pin) |
| 15 kΩ | 3 | Divider bottom (ADC pin → GND) |
| 10 nF | 3 | ADC pin → GND |
| 100 nF | 2 | Rocker switch GPIO → GND (not needed for touch) |
| 16 A fuse / breaker | 1 | Plug L input (or reuse an existing strip's breaker) |

## 6. Bring-up order

1. **Low voltage only, no mains:** ESP32 on USB, 5 V from the HLK *not* connected to mains
   — power the rail from USB/bench 5 V instead. Check relays click, switches or touch
   buttons toggle.
2. Measure each divider: ACS712 OUT ≈ 2.5 V → ADC pin ≈ 1.5 V with no load.
3. **Probes:** connect them; within 30 s they appear under *Settings → Overheat
   protection*. Warm one in your hand to find which is which and name it. Only then
   zip-tie them in place.
4. Mains connected, enclosure closed, lamp/kettle as first load. Adjust ZMPT101B pot,
   then calibrate (PLAN.md R8).
5. Burn-in: microwave at full power 10+ min. The probe readings should rise only a few
   degrees; afterwards unplug and check ACS712/relay terminals and joints for heat.
