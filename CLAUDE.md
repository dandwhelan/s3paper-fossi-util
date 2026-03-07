# CLAUDE.md - AI Assistant Guide for S3Paper Fossibot Dashboard

## Project Overview

**e-Rex** is an embedded C++ application for the **M5Paper S3** (ESP32-S3 e-ink device). It serves as a companion dashboard for the **Fossibot Power Station** via BLE, plus a suite of distraction-free productivity tools (EPUB reader, games, timer, calculator, notes).

## Build System

- **Framework**: PlatformIO with Arduino framework
- **Target**: ESP32-S3 (`m5stack-stamps3` board)
- **Config**: `platformio.ini`

### Commands

```bash
pio run                  # Build
pio run -t upload        # Flash to device via USB
pio device monitor       # Serial monitor (115200 baud)
pio run -t clean         # Clean build artifacts
```

There are **no automated tests or linters** in this project. Verification is done by building successfully (`pio run`) and testing on hardware.

### Build flags of note

- `-DSERIAL_DEBUG` — enables serial logging; comment out for production
- `-DBOARD_HAS_PSRAM` — enables PSRAM for large allocations
- `-Os` — optimize for size (flash is limited)

## Project Structure

```
├── platformio.ini          # Build configuration and dependencies
├── src/
│   ├── main.cpp            # Entry point: hardware init, main loop
│   ├── power_history.cpp/h # Power data logging to SD card
│   ├── ble/
│   │   ├── ble_client.cpp/h      # NimBLE BLE client for Fossibot
│   │   └── fossibot_protocol.h   # BLE register map and protocol constants
│   ├── ui/
│   │   ├── ui_manager.cpp/h      # Main UI: screens, touch, rendering
│   │   └── ui_notes_browse.cpp   # Notes browsing sub-screen
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
    ├── AI_CONTEXT.md       # AI-oriented architecture overview
    ├── DEVELOPMENT.md      # Developer setup guide
    ├── ble-protocol.md     # Fossibot BLE/Modbus protocol spec
    ├── ui-architecture.md  # UI screens, touch, rendering
    ├── hardware-guide.md   # I2C, peripherals
    ├── known-issues.md     # Known bugs and workarounds
    ├── POWER_MONITOR.md    # Power monitoring user guide
    ├── EPUB_READER.md      # E-reader user guide
    └── PRODUCTIVITY.md     # Productivity tools user guide
```

## Key Dependencies

Defined in `platformio.ini` under `lib_deps`:

| Library | Purpose |
|---------|---------|
| M5GFX | Graphics rendering for e-ink display |
| M5Unified | M5Stack hardware abstraction |
| NimBLE-Arduino | BLE client for Fossibot communication |
| ArduinoJson | JSON config parsing |
| SD | SD card filesystem |
| tinyxml2 (local) | EPUB XML parsing |
| miniz (local) | EPUB ZIP decompression |

## Architecture Key Points

### Hardware constraints

- **Display**: 960x540 e-ink (landscape), slow refresh — updates are rate-limited via `_needsRefresh` flag
- **I2C bus sharing**: RTC (BM8563 @ 0x51) and touch (GT911 @ 0x5D) share `Wire` on SDA=41, SCL=42. M5Unified's RTC driver is bypassed; a custom driver in `src/hardware/rtc.h` is used instead
- **Touch coordinates**: GT911 reports portrait (540x960); transformed via `screen_x = raw_y; screen_y = 540 - raw_x`
- **Memory**: Large buffers (framebuffer, EPUB data) must use PSRAM (`ps_malloc` / `heap_caps_malloc`)
- **Flash**: 16MB with `huge_app.csv` partition scheme — watch binary size

### Task model

- **Core 1 (main loop)**: UI updates, touch polling (every 100ms)
- **Core 0 (BLE task)**: FreeRTOS task for BLE communication to avoid blocking UI
- **Separate tasks**: EPUB parsing runs in its own task to prevent watchdog timeouts

### BLE protocol

The Fossibot uses a **modified Modbus RTU over BLE** (device address `0x11`). Critical detail: **CRC bytes are high-byte first** (opposite to standard Modbus). See `docs/ble-protocol.md` and `src/ble/fossibot_protocol.h` for the full register map.

### Power management

The app aggressively enters deep sleep on inactivity. Sleep is blocked while timers/pomodoro are running. Wake sources: touch, button, alarm.

## Code Conventions

- **Language**: C++17 (Arduino framework dialect)
- **Naming**: camelCase for functions/variables, PascalCase for classes/enums, UPPER_SNAKE for constants/macros
- **Member variables**: prefixed with underscore (`_currentScreen`, `_needsRefresh`)
- **Includes**: project headers use quotes (`"ble/ble_client.h"`), library headers use angle brackets (`<M5Unified.h>`)
- **Conditional logging**: wrap serial output in `#ifdef SERIAL_DEBUG`
- **No exceptions**: embedded target; use return codes and null checks
- **Single large files**: `ui_manager.cpp` contains most UI logic in one file; sub-screens are broken out only when large enough (e.g., `ui_notes_browse.cpp`)

## Common Pitfalls

1. **CRC byte order**: Fossibot expects high-byte first CRC. Getting this wrong causes silent command failures
2. **I2C conflicts**: Never reinitialize Wire without `Wire.end()` first; both RTC and touch share the bus
3. **E-ink ghosting**: Always do a full screen clear (`fillScreen(WHITE) + display()`) before major redraws
4. **PSRAM allocation**: Large allocations (>10KB) should go to PSRAM, not heap RAM
5. **Watchdog**: Long-running operations (EPUB parsing, BLE scans) must yield or run in separate FreeRTOS tasks
6. **Register 42 (Protection Flags)**: This register mixes MOSFET status bits with fault bits. Must use bitmask `(reg42 & 0x6000)` for fault detection — never check `reg42 > 0`
7. **Backup files**: The repo contains `.backup_*` and `.bak` files in `src/` — these are historical snapshots, not active code

## SD Card Structure (runtime)

The device expects this folder structure on the SD card:

```
/books/          # .epub files
/config/settings.json  # WiFi, BLE MAC, weather API key
/games/          # Game save files (auto-created)
/history/        # Power log CSVs (auto-created)
/notes/          # Handwritten note images (auto-created)
/reader/bookmarks/  # EPUB bookmarks (auto-created)
/boot.png        # Optional custom boot splash (960x540 grayscale)
```
