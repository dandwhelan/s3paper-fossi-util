# Roadmap — Future Features & Improvements

Candidate features for future releases, identified from a codebase review
(June 2026). Ordered roughly by value vs. effort.

## Done

- **Minesweeper difficulty levels** — Easy (10 mines), Medium (16), Hard (24)
  selectable in-game, mirroring the Sudoku selector.
- **GAME_NONOGRAM removed** — was a stub ScreenID with no implementation.
- **Home screen refresh gating** — the 60s auto-refresh and BLE-driven
  refreshes now skip the full e-ink redraw when SOC/power deltas are below
  the configured `eink` thresholds and the on-screen clock minute (where the
  theme shows one) hasn't changed. Last-drawn values are cached at draw time.
- **Eco-mode touch polling** — touch poll interval widens from 15 ms to
  30 ms while in eco mode (80 MHz) to cut idle I2C traffic.

## Tier 1 — High value, low effort

1. **Standby/scheduling UI (Campervan)** — BLE setters already exist for
   `SCREEN_TIMEOUT` (reg 59), AC/DC/USB standby (regs 60–62), `SYS_STANDBY`
   (reg 68), `SCHEDULE_CHARGE` (reg 63) and `MASTER_ENABLE` (reg 5) in
   `src/ble/fossibot_protocol.h`, but only some are surfaced in the Fossibot
   settings screens. Add the remaining control rows.
2. **Per-port USB power breakdown** — USB-A1/A2 and C1–C4 wattages
   (regs 30, 31, 34–37) are read but never displayed. Show a port detail
   popup and/or History tab.
3. **Battery voltage display + health warning** — `batteryVoltage` (reg 22)
   is parsed but only SOC% is shown. Add to the status panel with an
   out-of-range warning for LiFePO4 (48.0–51.5 V).
4. **Power history export/analytics** — generate a weekly summary CSV
   (daily averages, peaks, charge/discharge ratio) from existing
   `/history/*.csv` data.

## Tier 2 — Medium effort, high value

5. **Smart alarms** — buzzer alerts for low SOC, charge complete, sustained
   high output, and Fossibot error codes (currently errors only appear
   silently in the home-screen banner). Triggers stored in `settings.json`,
   evaluated in the existing alarm check loop.
6. **Home-mode energy history** — GivEnergy daily kWh totals arrive via MQTT
   (`src/mqtt/givenergy_data.h`) but are discarded. Log to
   `/history/solar_YYYY-MM-DD.csv` and add a 30-day production /
   self-consumption chart.
7. **Reader enhancements** — line-spacing and margin controls (currently
   hardcoded), manual bookmarks with notes, reading-time estimate.
8. **Fossibot error log** — append occurrences to `/history/error_log.csv`
   and show the last 10 in settings; flag recurring errors.

## Tier 3 — Nice to have

9. **Weather screen** — `ScreenID::WEATHER` and the `weather` config block
   (API key/city/units) exist with zero implementation. Implement via
   OpenWeatherMap with SD-cached responses (Home/WiFi mode).
10. **BLE signal strength indicator** — show RSSI bars and reconnection
    stats for diagnosing coverage dead zones.
11. **Note tagging & search** — sidecar JSON per note with tags; filter in
    the notes browser.
12. **Custom home tile layouts** — vertical/horizontal stack arrangements in
    addition to the existing four themes.
13. **Reader auto-scroll** — timed page advance for hands-free reading.
14. **Calculator history** — keep and recall the last 10 calculations.

## Performance improvements (separate workstream)

- Replace blocking `delay(50)` during BLE connect in `main.cpp` with a
  non-blocking early return.
- Allocate `_notesCanvas`/`_previewCanvas` once (in PSRAM) instead of
  `new`-ing per screen open without deletion (~600 KB each).
- Cache alarm/sleep config values in members instead of calling config
  getters every loop in `checkPowerManagement()`.
- Replace String-based CSV parsing in `power_history.cpp` (`indexOf` per
  line) with C-style `strtok` parsing into fixed buffers.
- Guard the SD power-cycle in `flushToSD()` against concurrent e-ink
  refreshes.
