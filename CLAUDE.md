# CLAUDE.md - AI Assistant Guide for e-Rex

## Project Overview

**e-Rex** is an embedded C++ application for the **M5Paper S3** (ESP32-S3 e-ink device) that operates in two distinct modes:

- **Campervan Mode** — Companion dashboard for the **Fossibot F3600 Pro** portable power station via **Bluetooth Low Energy (BLE)**. No WiFi.
- **Home Mode** — Solar energy dashboard for the **GivEnergy** home inverter/battery system via **WiFi + MQTT** (GivTCP3). No Fossibot, no BLE.

Both modes share a suite of productivity tools: EPUB reader, games, clock/timer/pomodoro, calculator, and notes.

## Dual-Mode Architecture

### Mode Selection

Set via `"mode"` in `/config/settings.json` on the SD card. Changeable from Settings screen (requires restart).

| | Campervan Mode | Home Mode |
|---|---|---|
| **Config value** | `"campervan"` (default) | `"home"` |
| **Connectivity** | BLE only (NimBLE) | WiFi + MQTT only |
| **Target device** | Fossibot F3600 Pro power bank | GivEnergy inverter via GivTCP3 |
| **Data direction** | Bidirectional (read + write) | Read-only (MQTT subscribe) |
| **Deep sleep** | Yes, after inactivity | Disabled (mains-powered) |
| **Home screen themes** | 5 themes (classic_grid, compact_status, horizontal_bars, sector, live_graph) | Energy Flow Diagram (single layout) |
| **Fossibot settings** | Available (Quick Actions, Power Limits, Timers) | Not available |
| **NTP time sync** | No (RTC only) | Yes, on WiFi connect |

### Campervan Mode Details

Communicates with the Fossibot via **modified Modbus RTU over BLE** (device address `0x11`). Runs a FreeRTOS task on Core 0 for BLE to avoid blocking the UI on Core 1.

**What it monitors:** Battery SOC%, voltage, input/output power, USB port power (6 ports), AC voltage/frequency, error codes, protection flags.

**What it controls:** USB/DC/AC output toggles, charge speed (1-5), charge/discharge limits, standby timers, silent charging, LED light mode, buzzer, schedule charge, power off.

**Critical:** CRC bytes are **high-byte first** (opposite to standard Modbus). See `docs/ble-protocol.md` and `src/ble/fossibot_protocol.h`.

**Link feedback:** `FossibotBLE::getConnState()` returns a `BleConnState` (`LINK_UP`, `LINKING`, `RETRY_WAIT`, `RETRY_PAUSED`, `RETRY_STOPPED`, `RADIO_OFF`, `NO_MAC`) plus retry count, countdown to the next attempt (`getMsUntilRetry()`) and time since the link was last up (`getMsSinceLastSeen()`). The home screen renders this as an inverted status strip whenever the link is down (see UI Architecture below). Enum names deliberately avoid `DISABLED`/`CONNECTED` — the Arduino core defines those as macros.

**Reconnect policy:** 30s base interval, doubling to a 5 min cap, abandoned after 55 min (deep sleep takes over at 60 min). A full client cleanup runs every 10 failures. `requestReconnectNow()` resets the backoff and arms an attempt on the next `update()` — never connect from a touch handler, the attempt blocks for up to 3s.

### Home Mode Details

Connects to WiFi and subscribes to GivTCP3 MQTT topics for real-time GivEnergy data. Data arrives via three topic groups:

- `GivEnergy/<SN>/Power/Power/#` — PV power (string 1/2/total), battery power, grid power, load power
- `GivEnergy/<SN>/Power/Flows/#` — Solar/battery/grid flow routing (solar-to-house, battery-to-grid, etc.)
- `GivEnergy/<SN>/Energy/Today/#` — Daily kWh totals (PV, import, export, load, battery charge/discharge)

**Home screen:** Energy Flow Diagram — central hub with 4 branches (Solar top, Battery left, Grid right, House bottom) connected by dashed flow lines with thickness proportional to power magnitude.

**Data struct:** `GivEnergy::SolarData` in `src/mqtt/givenergy_data.h`
**MQTT client:** `src/mqtt/mqtt_client.cpp/h`

## Build System

- **Framework**: PlatformIO with Arduino framework
- **Target**: ESP32-S3 (`m5stack-stamps3` board)
- **Config**: `platformio.ini`

```bash
pio run                  # Build (development env, serial logging on)
pio run -t upload        # Flash to device via USB
pio device monitor       # Serial monitor (115200 baud)
pio run -t clean         # Clean build artifacts

pio run -e m5paper_s3_release -t upload   # Production: no serial, no USB CDC
```

Two envs share `[common]` flags: `m5paper_s3` (default, `SERIAL_DEBUG` +
`ARDUINO_USB_CDC_ON_BOOT=1`) and `m5paper_s3_release` for battery use. Most
`Serial.print` calls are *not* wrapped in `#ifdef SERIAL_DEBUG`, so the release
env silences them by never opening the port (`cfg.serial_baudrate = 0` in
`main.cpp`) plus `ARDUINO_USB_CDC_ON_BOOT=0` — dropping the flag alone would
not. No serial log means no exception decoder: flash the debug env to chase a
crash.

There are **no automated tests or linters**. Verification is done by building (`pio run`) and testing on hardware.

### Build flags

- `-DSERIAL_DEBUG` — gates `Serial.begin()` and `cfg.serial_baudrate`; use the
  `m5paper_s3_release` env rather than editing this flag by hand
- `-DARDUINO_USB_CDC_ON_BOOT` — 1 in the dev env, 0 in release (points `Serial`
  at UART0 instead of running the USB CDC stack)
- `-DBOARD_HAS_PSRAM` — enables PSRAM for large allocations
- `-Os` — optimize for size (flash is limited)

## Project Structure

```
├── platformio.ini          # Build config and dependencies
├── src/
│   ├── main.cpp            # Entry point: mode dispatch, hardware init, main loop
│   ├── power_history.cpp/h # Power data logging to SD card
│   ├── ble/                        # CAMPERVAN MODE ONLY
│   │   ├── ble_client.cpp/h        # NimBLE BLE client for Fossibot
│   │   └── fossibot_protocol.h     # BLE register map and protocol constants
│   ├── mqtt/                       # HOME MODE ONLY
│   │   ├── mqtt_client.cpp/h       # WiFi + MQTT client for GivTCP3
│   │   └── givenergy_data.h        # SolarData struct + formatting helpers
│   ├── ui/
│   │   ├── ui_manager.cpp/h        # All UI: screens, touch, rendering (~8000 lines)
│   │   └── ui_notes_browse.cpp     # Notes browsing sub-screen
│   ├── epub/               # EPUB reader subsystem
│   │   ├── Renderer/       # E-ink text/image rendering
│   │   ├── EpubList/       # EPUB parsing, TOC, bookmarks
│   │   ├── RubbishHtmlParser/  # HTML-to-blocks parser
│   │   ├── ZipFile/        # ZIP extraction (epub = zip)
│   │   ├── Hyphenator.cpp/h    # Word hyphenation
│   │   └── M5GFXRenderer.cpp/h # M5GFX display backend
│   ├── fonts/              # Font header files (VCR OSD, Terminus, Reader fonts)
│   ├── hardware/           # Hardware abstraction headers
│   │   ├── rtc.h           # BM8563 RTC direct I2C driver
│   │   ├── battery.h       # ADC battery voltage reading
│   │   ├── buzzer.h        # Passive buzzer tones
│   │   ├── display.h       # E-ink display helpers
│   │   └── touch.h         # GT911 touch controller polling
│   └── utils/
│       ├── config.cpp/h    # JSON settings load/save (ArduinoJson)
│       └── sd_manager.cpp/h # SD card file operations
├── lib/                    # Local library copies
│   ├── tinyxml2/           # XML parser (for EPUB)
│   └── miniz/              # ZIP decompression
├── data/
│   └── config/settings.json # Default device config (uploaded to SD)
└── docs/                   # Documentation
```

## Key Dependencies

| Library | Purpose |
|---------|---------|
| M5GFX | Graphics rendering for e-ink display |
| M5Unified | M5Stack hardware abstraction |
| NimBLE-Arduino | BLE client for Fossibot (Campervan mode) |
| PubSubClient | MQTT client for GivTCP3 (Home mode) |
| ArduinoJson | JSON config parsing |
| SD | SD card filesystem |
| tinyxml2 (local) | EPUB XML parsing |
| miniz (local) | EPUB ZIP decompression |

## Hardware Constraints

- **Display**: 960x540 e-ink (landscape), slow refresh — rate-limited via `_needsRefresh` flag
- **Color palette**: E-ink safe — BLACK, DARK_GRAY, GRAY, LIGHT_GRAY, WHITE (plus RED for errors)
- **I2C bus sharing**: RTC (BM8563 @ 0x51) and touch (GT911 @ 0x5D) share `Wire` on SDA=41, SCL=42. Custom RTC driver in `src/hardware/rtc.h` (M5Unified's is bypassed)
- **Touch coordinates**: GT911 reports portrait (540x960); transformed via `screen_x = raw_y; screen_y = 540 - raw_x`
- **Memory**: Large buffers (framebuffer, EPUB data) must use PSRAM (`ps_malloc` / `heap_caps_malloc`)
- **Flash**: 16MB with `huge_app.csv` partition scheme — watch binary size

## Task Model

- **Core 1 (main loop)**: UI updates, touch polling (every 100ms)
- **Core 0 (BLE task)**: FreeRTOS task for BLE communication (Campervan only)
- **Separate tasks**: EPUB parsing runs in its own task to prevent watchdog timeouts

## Power Management

- **CPU scaling**: 240 MHz on touch → 80 MHz after 5s idle (eco mode)
- **Campervan**: Deep sleep after configurable inactivity. Blocked while timer/pomodoro running. Wake: touch (GPIO 48)
- **Home**: Deep sleep disabled (device is mains-powered, always-on monitoring)
- **Battery cutoff**: Both modes sleep at 3.3V to protect Li-Po

### Where the battery actually goes

No power measurements have been taken on this hardware — the figures below are
order-of-magnitude estimates from the ESP32-S3 and e-ink datasheets, useful for
ranking optimisations, not for predicting runtime. Measure before trusting.

**Constraint that shapes everything:** the Fossibot link must stay up, and deep
sleep powers down the BLE controller. Deep sleep is therefore a shutdown
strategy (inactivity timeout, 60-min disconnect), not an idle strategy.

Ranked, biggest first, for a campervan device sitting on the dashboard:

1. **Being awake at all.** The main loop never light-sleeps: it spins on
   `delay(10)` and polls the GT911 over I2C every 15ms (30ms in eco mode). At
   80 MHz that is a continuous ~25-35 mA; at 240 MHz ~40-55 mA. Idle current
   dominates everything else because it is paid 100% of the time — and the
   usual fix is unavailable: the Arduino core here is built with
   `# CONFIG_PM_ENABLE is not set` and `# CONFIG_BT_CTRL_MODEM_SLEEP is not
   set`, so there is no tickless idle and the BT controller does not idle its
   radio between connection events. See `docs/roadmap.md` → Battery life.
2. **BLE.** As *central* we transmit an anchor packet every connection interval
   whether or not there is data, so the interval is the lever (now 200-400ms;
   it was 30-60ms, carrying data requested only every 45s). Failed *connection
   attempts* cost much more than the connection itself: `connectToDevice()`
   boosts the CPU to 240 MHz and holds the radio for up to a 3s timeout. Ten
   retries a minute would cost more than the link ever did — hence the
   exponential backoff and the 55 min give-up.
3. **WiFi (Home mode only).** ~80-120 mA average while associated. Irrelevant on
   battery, which is why Home mode assumes mains and disables deep sleep.
4. **E-ink refreshes.** Only significant if they are frequent — see below.
5. **SD card.** Tens of mA in bursts during note saves, history flushes and
   EPUB reads; negligible at rest.

### E-ink refresh cost (`epd_mode_t`)

Energy per refresh ≈ panel drive current × drive time, and the drive time is
what the mode changes. Rough relative cost per full-screen update:

| Mode | Waveform | Approx. drive time | Relative energy |
|---|---|---|---|
| `epd_quality` | 16-level greyscale, full clear | ~1-2 s | ~5-10x |
| `epd_text` (default) | greyscale, shorter waveform | ~0.6-1 s | ~3-5x |
| `epd_fast` | reduced levels | ~0.3-0.5 s | ~2x |
| `epd_fastest` | 1-bit, no greys | ~0.1-0.3 s | 1x (baseline) |

So fast refresh is worth roughly **3-10x less energy per update** than a quality
refresh — but a refresh is a sub-second event on a device that redraws at most
once a minute. Cutting refresh *frequency* (the change-threshold gating in
`shouldUpdateDashboard()` and `bleStatusSignature()`) saves far more than
changing the mode does, and the panel draws nothing between updates. Pick modes
for legibility, not battery: `epd_fastest` loses the greys (Live Graph accepts
this deliberately; the other themes rely on them).

Two caveats that matter more than the mode choice:

- E-ink refreshes contend with BLE for DMA on this board — see the EPD/BLE
  pitfall below. Refresh frequency is a stability question as well as a power one.
- Anything that keeps the CPU at 240 MHz (rendering, BLE attempts, EPUB
  parsing) costs more than the panel does while it runs.

## UI Architecture

### Home Screen Dispatch (`drawHomeScreen()`)

```
if (config->isHomeMode())
    drawHomeGivEnergy()      // Energy Flow Diagram
else
    dispatch to theme:       // classic_grid | compact_status | horizontal_bars | sector
```

### Bluetooth Status Strip (Campervan only)

The dashboard shows the last values received, so a dropped link would otherwise
look identical to a live one. Whenever `bleStatusVisible()` is true (campervan
mode and `getConnState() != LINK_UP`), each theme draws an inverted strip via
`drawBleStatusStrip()`:

- **classic_grid / sector / live_graph** — top 30px of the battery bar area
  (`drawBatteryBar()` shrinks the bar; the error banner drops its detail line)
- **compact_status** — top of screen between the clock and MENU (below the error
  banner if one is showing)
- **horizontal_bars** — lower part of row 4, clear of the toggles and date

Text is `bleStatusHeadline()` + `bleStatusDetail()`, e.g. *FOSSIBOT DISCONNECTED
— retrying: next try in 45s (3 failed), last seen 12m ago*. Tapping the strip
calls `requestReconnectNow()` (or opens Settings when the radio is off); the
handler runs from `handleHomeTouch()` before theme dispatch, because the strip
can overlap output toggles that are inert while offline.

Redraws are deliberately rationed: a state change refreshes immediately, while
countdown/attempt/"last seen" changes go through `bleStatusSignature()` on the
existing 60s pass. Elapsed time is bucketed (per minute under 10 min, then 5
min, then hourly) so a long outage stops costing refreshes.

`main.cpp` pushes `updatePowerBankData()` on **every** loop pass, connected or
not — gating it on `isConnected()` was what left the UI believing it was still
linked.

### Screens Available (Both Modes)

| Screen | Description |
|--------|-------------|
| HOME | Power dashboard (mode-specific layout) |
| SETTINGS | 3x3 navigation menu |
| SETTINGS_DEVICE | Date/time, refresh rate, sleep |
| CLOCK | Clock + Alarm + Pomodoro + Timer |
| CALCULATOR | Basic calculator |
| NOTES | Freehand drawing canvas |
| NOTES_BROWSE | Browse saved notes |
| READER | EPUB/TXT e-reader |
| GAMES_MENU | Game selection |
| GAME_2048 / GAME_SUDOKU / GAME_MINESWEEPER | Individual games |
| HISTORY | Power data history charts |

### Screens (Campervan Only)

| Screen | Description |
|--------|-------------|
| SETTINGS_FOSSIBOT | Quick Actions, Power Limits |
| SETTINGS_FOSSIBOT_TIMERS | Standby timers, Schedule Charge |

## Configuration

`/config/settings.json` on SD card:

```json
{
    "mode": "campervan",              // "campervan" or "home"
    "wifi": { "ssid": "", "password": "", "enabled": true },            // Home mode only
    "bluetooth": { "fossibot_mac": "XX:XX:XX:XX:XX:XX", "enabled": true }, // Campervan only
    "mqtt": {                                           // Home mode only
        "broker": "", "port": 1883,
        "username": "", "password": "",
        "inverter_sn": ""
    },
    "display": {
        "theme": "classic_grid",      // Campervan only (classic_grid|compact_status|horizontal_bars|sector|live_graph)
        "auto_sleep_minutes": 5
    },
    "timezone": { "offset_hours": 0 },
    "weather": { "api_key": "", "city": "London", "units": "metric" },
    "eink": { "soc_change_threshold": 1, "power_change_threshold": 50 }
}
```

## Code Conventions

- **Language**: C++17 (Arduino framework dialect)
- **Naming**: camelCase for functions/variables, PascalCase for classes/enums, UPPER_SNAKE for constants/macros
- **Member variables**: prefixed with underscore (`_currentScreen`, `_needsRefresh`)
- **Includes**: project headers use quotes (`"ble/ble_client.h"`), library headers use angle brackets (`<M5Unified.h>`)
- **Conditional logging**: wrap serial output in `#ifdef SERIAL_DEBUG`
- **No exceptions**: embedded target; use return codes and null checks
- **Large files**: `ui_manager.cpp` contains most UI logic (~8000 lines); sub-screens broken out only when very large

## Common Pitfalls

1. **CRC byte order** (Campervan): Fossibot expects high-byte first CRC — getting this wrong causes silent command failures
2. **I2C conflicts**: Never reinitialize Wire without `Wire.end()` first; RTC and touch share the bus
3. **E-ink ghosting**: Always do `fillScreen(WHITE) + display()` before major redraws
4. **PSRAM allocation**: Large allocations (>10KB) should go to PSRAM, not heap RAM
5. **Watchdog**: Long-running operations (EPUB parsing, BLE scans) must yield or run in separate FreeRTOS tasks
6. **Register 42** (Campervan): Mixes MOSFET status with fault bits. Use bitmask `(reg42 & 0x6000)` for fault detection
7. **EPD/BLE contention** (Campervan): Skip UI updates during active BLE operations to avoid display corruption
8. **MQTT update rate** (Home): Solar data updates throttled to 30-second minimum to prevent e-ink flicker
9. **Mode requires restart**: Changing mode in settings only takes effect after device restart
10. **Arduino macro collisions**: The ESP32 core `#define`s common all-caps words (`DISABLED`, `INPUT`, `LOW`…). Enum values like `BleConnState::RADIO_OFF` are named around them — a plain `DISABLED` will not compile
11. **Stale UI state** (Campervan): The UI caches the last `PowerBankData` it was handed. Anything that stops feeding it (an `isConnected()` gate, a paused task) leaves the dashboard silently showing old numbers as if they were live

## SD Card Structure (runtime)

```
/books/              # .epub files
/config/settings.json  # Device configuration
/games/              # Game save files (auto-created)
/history/            # Power log CSVs (auto-created)
/notes/              # Handwritten note images (auto-created)
/reader/bookmarks/   # EPUB bookmarks (auto-created)
/boot.png            # Optional custom boot splash (960x540 grayscale)
```
