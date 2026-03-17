# Theme Selector - Implementation Plan

## Summary

Add a theme selector to the Settings screen that lets the user switch between different dashboard layouts. All themes display the **same data** (battery %, IN power, OUT power, time-to-full, time-remaining, USB/DC/AC toggles, clock/date, connection status) and keep the **bottom menu bar untouched**. The theme name is persisted in `settings.json` via the existing `config.getTheme()` / `config.setTheme()` infrastructure.

---

## Available Data Elements (unchanged across all themes)

| Element | Source |
|---|---|
| Battery % | `_powerData.batteryPercent` |
| Input Power (W) | `_powerData.inputPower` |
| Output Power (W) | `_powerData.outputPower` |
| Time to Full (min) | `_powerData.minutesToFull` |
| Time Remaining (min) | `_powerData.minutesToEmpty` |
| USB / DC / AC toggles | `_powerData.usbActive`, `.dcActive`, `.acActive` |
| BLE connection status | `_powerData.connected` |
| Error state / banner | `_powerData.hasError()` |
| Clock (HH:MM) | `RTC::getTime()` |
| Date | `RTC::getDate()` |
| Error pending | `_powerData.hasErrorPending()` |

---

## Theme Designs (3 themes)

### Theme 1: `classic_grid` (Current — no changes)

The existing 2x2 grid layout. This is the default.

```
┌──────────────────────────────────────────────────────────────┐
│ ████████████████████ Battery Bar 73% █████████████████████████│  80px
├────────────────────────────┬─────────────────────────────────┤
│  IN              450 W     │  [●] Connected                  │
│  ██████████░░░░░░░░░░░     │                                 │
│  2h 15m to full            │    USB    DC     AC              │ ~190px
│                            │    [■]    [□]    [■]             │
├────────────────────────────┼─────────────────────────────────┤
│  OUT             120 W     │  14:32                           │
│  ██░░░░░░░░░░░░░░░░░░     │                                 │
│  8h 30m remaining          │  Mon 8 Mar 2026                 │ ~190px
│                            │                                 │
├──────────────────────────────────────────────────────────────┤
│ READ │ GAME │ ALARM │ CALC │ NOTES │ MENU │                  │  60px
└──────────────────────────────────────────────────────────────┘
```

### Theme 2: `compact_status`

Battery-centric layout. Large battery percentage dominates the left, with all power I/O stacked vertically on the right. Toggles below. Good for quick glance.

```
┌──────────────────────────────────────────────────────────────┐
│                                                              │
│                          73%             IN   450W  2h 15m   │
│       ┌─────────────────────┐           ─────────────────    │
│       │                     │           OUT  120W  8h 30m    │
│       │   LARGE BATTERY     │           ─────────────────    │
│       │   CIRCLE / ARC      │                                │
│       │   with % centered   │           14:32                │
│       │                     │           Mon 8 Mar 2026       │
│       └─────────────────────┘                                │
│                                                              │
│              ┌─USB─┐  ┌─DC──┐  ┌─AC──┐     [●]              │
│              │ [■] │  │ [□] │  │ [■] │   connected           │
├──────────────────────────────────────────────────────────────┤
│ READ │ GAME │ ALARM │ CALC │ NOTES │ MENU │                  │
└──────────────────────────────────────────────────────────────┘
```

**Layout detail (960x540, menu=60px):**
- No separate battery bar — battery is the hero element
- Left half: Large battery arc/circle (~280px diameter), percentage in huge font (size 4) centered inside
- Right half, top section: Two rows for IN and OUT — each shows label, wattage (bold), time estimate
- Right half, mid section: Clock and date
- Bottom strip (above menu bar): Horizontal USB | DC | AC toggle buttons + connection indicator
- Error banner: Full-width strip replaces the bottom toggle area (same as current battery bar error logic)

### Theme 3: `horizontal_bars`

Wide horizontal layout. Each data element gets its own full-width row. Dense, information-forward, no wasted space. Feels like a terminal/HUD.

```
┌──────────────────────────────────────────────────────────────┐
│  BATTERY  ████████████████████████████████████░░░░░░░  73%   │  ~55px
├──────────────────────────────────────────────────────────────┤
│  IN  ████████████░░░░░░░░░░░░░░░░░░░  450W     2h 15m full  │  ~55px
├──────────────────────────────────────────────────────────────┤
│  OUT ██░░░░░░░░░░░░░░░░░░░░░░░░░░░░░  120W     8h 30m rem   │  ~55px
├──────────────────────────────────────────────────────────────┤
│                                                              │
│  14:32        Mon 8 Mar 2026        USB [■]  DC [□]  AC [■]  │  ~55px
│                                                       [●]    │
│                                                              │
├──────────────────────────────────────────────────────────────┤
│ READ │ GAME │ ALARM │ CALC │ NOTES │ MENU │                  │  60px
└──────────────────────────────────────────────────────────────┘
```

**Layout detail:**
- Row 1 (y=10, h=70): Battery bar — full-width progress bar with "BATTERY" label left, percentage right
- Row 2 (y=85, h=70): IN power — full-width progress bar with label, wattage, and time-to-full
- Row 3 (y=160, h=70): OUT power — full-width progress bar with label, wattage, and time-remaining
- Row 4 (y=235, h=remaining): Info strip — clock/date left, USB/DC/AC toggles + connection indicator right
- Panels use thin 1px borders, data is large and readable
- Error banner: Replaces Row 1 (battery row) same as current behavior

---

## Implementation Steps

### Step 1: Add theme enum and dispatcher in `drawHomeScreen()`

**File: `src/ui/ui_manager.h`**
- Add a private enum or use string comparison for theme routing
- Add new private drawing methods:
  - `void drawHomeClassicGrid()` — extract current `drawHomeScreen()` body
  - `void drawHomeCompactStatus()`
  - `void drawHomeHorizontalBars()`

**File: `src/ui/ui_manager.cpp`**
- Refactor `drawHomeScreen()` to be a dispatcher:
  ```cpp
  void UIManager::drawHomeScreen() {
    M5.Display.fillScreen(COLOR_WHITE);
    String theme = config->getTheme();
    if (theme == "compact_status") {
      drawHomeCompactStatus();
    } else if (theme == "horizontal_bars") {
      drawHomeHorizontalBars();
    } else {
      drawHomeClassicGrid();
    }
    drawMenuBar();
    M5.Display.display();
  }
  ```
- Move the existing body (battery bar + 4 panels) into `drawHomeClassicGrid()`
- Implement `drawHomeCompactStatus()` and `drawHomeHorizontalBars()`

### Step 2: Update touch handling for each theme

**File: `src/ui/ui_manager.cpp`**
- Refactor `handleHomeTouch()` to dispatch based on theme:
  - `handleHomeClassicGridTouch()` — current logic (unchanged)
  - `handleHomeCompactStatusTouch()` — toggle buttons are in bottom strip
  - `handleHomeHorizontalBarsTouch()` — toggle buttons are in Row 4
- Each handler must calculate its own toggle button positions matching the draw function

### Step 3: Add theme selector to Settings screen

**File: `src/ui/ui_manager.cpp`**
- Add a "Theme" row to the existing Settings screen (`drawSettingsScreen()`)
- Shows current theme name, tap to cycle: classic_grid → compact_status → horizontal_bars → classic_grid
- Calls `config->setTheme()` and `config->save()` on change
- Triggers a refresh when returning to HOME

### Step 4: Error banner handling per theme

Each theme needs to handle `_powerData.hasError()`:
- `classic_grid`: Current behavior (replaces battery bar)
- `compact_status`: Replace battery circle area with error banner
- `horizontal_bars`: Replace battery row with error banner

The error rendering can reuse the existing `drawBatteryBar()` error path or a shared `drawErrorBanner(x, y, w, h)` helper.

---

## Files Modified

| File | Changes |
|---|---|
| `src/ui/ui_manager.h` | Add 5 new private method declarations (3 draw + 2 touch) |
| `src/ui/ui_manager.cpp` | Refactor `drawHomeScreen()` into dispatcher + 2 new theme draw functions, refactor `handleHomeTouch()` into dispatcher + 2 new theme touch handlers, add theme option to settings screen |
| `data/config/settings.json` | No change needed (already has `"theme": "classic_grid"`) |
| `src/utils/config.cpp` | No change needed (already has get/setTheme) |

## What stays unchanged

- Bottom menu bar (`drawMenuBar()`) — called identically by all themes
- Menu bar touch handling (`hitTestMenuButton()`)
- All sub-pages/screens (settings, games, reader, etc.)
- BLE communication, power data, config infrastructure
- Error banner content/logic (just repositioned per theme)
