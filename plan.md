# Plan: Remove Static Bottom Nav Bar & Move Options to Menu

## Overview

Remove the persistent 60px bottom navigation bar (READ, GAME, ALARM, CALC, NOTES, MENU) from all screens. Move all those navigation options into the existing Settings/Menu screen (renamed to "Menu"). Add a consistent "home" button mechanism so users can always get back to the home screen.

## Design Decisions

**How to access the menu (replacing the always-visible nav bar):**
- Add a **"MENU" button** in the top-right corner of the HOME screen (small, unobtrusive)
- The HOME screen becomes the central hub — all other screens navigate back to HOME via a "HOME" button in their header area
- The existing Settings screen becomes the **main Menu screen** with tiles for: Read, Games, Clock/Alarm, Calculator, Notes, Device Settings, Fossibot, SD Diag, History, Theme

**How to get back to HOME from any screen:**
- Screens that already have exit/back buttons (Reader, Notes, Games, History) — change their target to HOME
- Screens that relied on the nav bar (Clock, Calculator, Games Menu, Settings sub-screens) — add a "HOME" button in the top-left corner

## Step-by-Step Changes

### Step 1: Remove `drawMenuBar()` calls and nav bar touch handling

**File: `src/ui/ui_manager.cpp`**

1. Remove all `drawMenuBar()` calls from these 8 locations:
   - `drawHomeClassicGrid()` / `drawHomeCompactStatus()` / `drawHomeHorizontalBars()` / `drawHomeSector()` (via line ~602 area in each theme)
   - `drawSettingsScreen()` (line ~1732)
   - `drawDeviceSettingsScreen()` (line ~1857)
   - `drawFossibotSettingsScreen()` (line ~2086)
   - `drawFossibotTimersScreen()` (line ~2414)
   - `drawClockScreen()` (line ~2779)
   - `drawCalculatorScreen()` (line ~2969)
   - `drawGamesMenu()` (line ~4973)

2. Remove the menu button touch test block in `handleTouch()` (lines ~378-390) that checks `hitTestMenuButton()`

### Step 2: Update layout constants — content areas expand by 60px

Every screen that previously reserved `MENU_BAR_HEIGHT` (60px) at the bottom now gets that space back. Update content area calculations:

- **Home screen themes** (all 4): Change `contentHeight` calculations that subtract `MENU_BAR_HEIGHT` to use full height
  - `drawHomeClassicGrid()` / `handleHomeClassicGridTouch()`
  - `drawHomeCompactStatus()` / `handleHomeCompactStatusTouch()`
  - `drawHomeHorizontalBars()` / `handleHomeHorizontalBarsTouch()`
  - `drawHomeSector()` / `handleHomeSectorTouch()`
- **Clock screen**: sidebar and content area expand down
- **Calculator screen**: button grid expands down
- **Games menu**: more room for game tiles
- **Settings screens**: more room for buttons

### Step 3: Redesign the Settings screen into "Menu" screen

**File: `src/ui/ui_manager.cpp` — `drawSettingsScreen()` and `handleSettingsTouch()`**

Redesign the Settings screen to be the main **Menu** screen with all navigation options in a 3x3 grid:

```
┌─────────────────────────────────────────────┐
│  [HOME]           MENU                      │
├─────────────┬─────────────┬─────────────────┤
│   Read      │   Games     │   Clock         │
├─────────────┼─────────────┼─────────────────┤
│   Calculator│   Notes     │   Settings      │
├─────────────┼─────────────┼─────────────────┤
│   History   │  Theme: X   │                 │
└─────────────┴─────────────┴─────────────────┘
```

- Title: "MENU" instead of "Settings"
- "Settings" tile opens a sub-menu for Device/Fossibot/SD Diag (reusing existing settings sub-screens)
- HOME button top-left

### Step 4: Add HOME button to all non-home screens

Add a small "HOME" button (~80x40px) in the top-left corner of every screen that doesn't already have a way back to HOME:

| Screen | Current "back" method | Change needed |
|--------|----------------------|---------------|
| Menu (Settings) | "Back" button in grid | Replace with HOME btn top-left |
| Device Settings | Has back in nav bar | Add HOME btn top-left |
| Fossibot Settings | Has back in nav bar | Add HOME btn top-left |
| Fossibot Timers | Has back in nav bar | Add HOME btn top-left |
| Clock | Nav bar only | Add HOME btn top-left |
| Calculator | Nav bar only | Add HOME btn top-left |
| Games Menu | Nav bar only | Add HOME btn top-left |
| Notes | Has exit toolbar button | Already goes HOME ✓ |
| Notes Browse | Has X button | Goes to NOTES ✓ |
| Reader | Has EXIT button | Already goes HOME ✓ |
| History | Has back/X button | Already goes HOME ✓ |
| Game 2048 | Has exit | Goes to GAMES_MENU ✓ |
| Game Sudoku | Has exit | Goes to GAMES_MENU ✓ |
| Game Minesweeper | Has exit | Goes to GAMES_MENU ✓ |
| SD Diag | Check existing | Add HOME or back-to-settings btn |

### Step 5: Add MENU button to HOME screen

Add a "MENU" button on the home screen so users can access the menu. All 4 home screen themes need this:

- Draw a button in the top-right area (e.g., 80x35px near battery bar)
- Add touch handler for it in each theme's touch handler
- Navigates to `ScreenID::SETTINGS` (which is now the Menu screen)

### Step 6: Helper function for HOME button

Create a reusable `drawHomeButton()` function that draws a consistent HOME button in the top-left corner, and a corresponding `hitTestHomeButton()` for touch detection. This avoids duplicating the same code across 8+ screens.

```cpp
void drawHomeButton();              // Draws "HOME" at (10, 10, 80, 35)
bool hitTestHomeButton(int x, int y); // Returns true if touch is in HOME btn area
```

### Step 7: Full screen audit — verify every feature path

Complete audit of all 18 ScreenID values to ensure nothing is orphaned:

| Screen | How to reach | How to leave |
|--------|-------------|-------------|
| HOME | Default / HOME buttons | MENU button → SETTINGS |
| SETTINGS (Menu) | HOME → MENU btn | Tiles to features; HOME btn |
| SETTINGS_DEVICE | Menu → Settings tile | Back → SETTINGS |
| SETTINGS_FOSSIBOT | Menu → Settings tile | Back → SETTINGS |
| SETTINGS_FOSSIBOT_TIMERS | Fossibot → Timers | Back → SETTINGS_FOSSIBOT |
| CLOCK | Menu tile | HOME btn |
| CALCULATOR | Menu tile | HOME btn |
| NOTES | Menu tile | Exit toolbar → HOME |
| NOTES_BROWSE | Notes → Browse | X → NOTES |
| READER | Menu tile | EXIT → HOME |
| GAMES_MENU | Menu tile | HOME btn |
| GAME_2048 | Games → 2048 | Exit → GAMES_MENU |
| GAME_SUDOKU | Games → Sudoku | Exit → GAMES_MENU |
| GAME_MINESWEEPER | Games → Mine | Exit → GAMES_MENU |
| HISTORY | Menu tile | Back → HOME |
| SD_DIAG | Menu → Settings → SD | Back → SETTINGS |
| WEATHER | (if used) | Back → HOME |

### Step 8: Clean up header file

**File: `src/ui/ui_manager.h`**

- Remove or mark deprecated: `MenuButton` struct, `_menuButtons` array, `NUM_MENU_BUTTONS`, `initMenuButtons()`, `hitTestMenuButton()`, `executeMenuButton()`, `drawMenuBar()`
- Add declarations for new methods: `drawHomeButton()`, `hitTestHomeButton()`
- `MENU_BAR_HEIGHT` constant can be removed

## Files Modified

1. **`src/ui/ui_manager.cpp`** — Primary changes (remove nav bar calls, expand layouts, redesign menu screen, add HOME/MENU buttons, add helper functions)
2. **`src/ui/ui_manager.h`** — Remove nav bar declarations, add new method declarations
3. **`src/ui/ui_notes_browse.cpp`** — Verify no nav bar references (likely none already)

## Risk Assessment

- **Low risk**: All navigation paths are preserved — just reorganized
- **No BLE/hardware impact**: Pure UI change
- **Testable**: `pio run` to verify compilation; hardware test for touch targets
- **Reversible**: Single feature branch, easy to revert
