# Strip Overview widget (ThingsBoard)

One ThingsBoard "HTML Card" widget for the whole power strip:

- **Header:** device name, Online/Offline dot, mains voltage and frequency, total power
  and current, firmware version, IP and WiFi signal.
- **One card per outlet:** name (set in the strip's local web page), an on/off switch,
  live watts, amps, power factor and kWh. An outlet waiting out its compressor delay
  shows in amber with a "Turning on in m:ss" countdown.
- **Click a card** for its history over 24 hours, 7 days or 30 days: average/peak power,
  approximate kWh used, time on, a power chart, an on/off timeline and every switching
  event with who caused it (wall switch, dashboard, local page, power returning,
  compressor delay). The panel also has a two-click "Reset energy counter" button.

## Setup

1. In ThingsBoard, open the strip's device and click **Copy device Id**.
2. Dashboard → **Add widget** → **HTML widgets** bundle → **HTML Card**.
3. Paste [strip-overview.html](strip-overview.html) into the HTML tab and
   [strip-overview.css](strip-overview.css) into the CSS tab.
4. Replace `PUT-DEVICE-ID-HERE` in **both** places in the HTML (the `data-device-id`
   attribute and `DEVICE_ID` in the script) with the copied id.
5. Size the widget generously (about 12 columns × 10 rows); it scrolls inside if smaller.

## What it talks to

| Firmware key | Kind | Used for |
|---|---|---|
| `heartbeat` | telemetry, every 8 s (TbLink) | Online/offline (offline after 25 s without one) |
| `voltage`, `frequency`, `total_power`, `total_current` | telemetry, every 10 s | Header |
| `outN_state`, `_current`, `_power`, `_pf`, `_energy`, `_pending_s` | telemetry, every 10 s + on change | Outlet cards, history |
| `outN_event` | telemetry, on every switch | Switching list in the history panel (value like `on by button`) |
| `outN_name`, `ip`, `rssi` | client attributes | Names, header |
| `current_fw_version` | telemetry (TbLink) | Header |
| `temp_1`…`temp_4`, `thermal_state` | telemetry, every 10 s | "Hottest point" (hover for each probe) and the red overheat / probe-fault banner |
| `temp_N_name` | client attributes | Probe names in the hover text |
| RPC `setOutlet {outlet, state}`, `resetEnergy {outlet}` | one-way RPC | Switch, reset button |

## Why it's built this way

Same approach as `esp32-mqtt-generic-board/dashboard/board-overview-widget`, whose
protocol details were verified against this ThingsBoard 4.2 server:

- DOM lookups are scoped with `data-device-id`, because `self.ctx` is undefined and
  `document.currentScript` is null inside static HTML Card widgets.
- Live values come from one `ENTITY_DATA` WebSocket subscription. `query.latestValues`
  must be present (even empty) or the server throws. Pushes carry only changed keys, so
  the widget keeps the last value of everything else.
- Online/offline uses the heartbeat point's own timestamp, not ThingsBoard's `active`
  attribute (checked only every ~60 s) or MQTT last-will (not supported by core
  ThingsBoard).
- History uses server-side averaging (`agg=AVG`, 5 min / 1 h / 6 h buckets) so every
  range stays at a few hundred points. "kWh used" is estimated from those averages,
  because the firmware's `outN_energy` counter restarts from 0 after a reboot until
  energy persistence lands (M4).
