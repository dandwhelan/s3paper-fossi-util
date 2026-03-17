/**
 * UI Manager Implementation
 *
 * Design 1A "Classic Grid" layout for M5Paper S3 (960x540)
 */

#include "ui_manager.h"
#include "../ble/ble_client.h"
#include "../fonts/ReaderFonts.h"  // Reader font family registry
#include "../fonts/TerminusBold.h" // Terminus Bold font
#include "../hardware/battery.h"
#include "../hardware/buzzer.h"
#include "../hardware/rtc.h"
#include "../utils/config.h"
#include "../utils/sd_manager.h"
#include <FS.h>
#include <SD.h>
#include <Wire.h>

// Colors for eInk (grayscale)
#define COLOR_BLACK 0x0000
#define COLOR_DARK_GRAY 0x4208
#define COLOR_GRAY 0x8410
#define COLOR_LIGHT_GRAY 0xC618
#define COLOR_WHITE 0xFFFF
#define COLOR_RED 0xF800

// ============================================================================
// READER FONT CONFIGURATION
// ============================================================================
// Reader fonts are now handled via ReaderFonts.h with font family selection.
// Use getReaderFont() method to get the current font based on family + size.
// Available families: Serif (Bookerly), Sans-serif (Noto Sans), Dyslexic

// Line spacing (pixels added between lines)
// You can adjust these values, or the logic in drawReaderScreen uses
// _readerLineSpacing variable which currently defaults to 1.

// Static flags for Task Synchronization
static volatile bool g_bookLoadingComplete = false;
static volatile bool g_bookLoadSuccess = false;
static volatile bool g_readerBusy =
    false; // Prevent stack overflow by serializing reader tasks
static volatile bool g_relayoutInProgress =
    false; // Track if re-layout task is running
static volatile unsigned long g_taskStartTime = 0; // Watchdog timer

UIManager::UIManager()
    : _currentScreen(ScreenID::HOME), _previousScreen(ScreenID::HOME),
      _needsRefresh(true), _lastRefresh(0), _clockMode(ClockMode::POMODORO),
      _pomodoroState(PomodoroState::STOPPED),
      _pomodoroSession(PomodoroSession::WORK),
      _pomodoroRemainingSeconds(POMODORO_WORK_SECONDS), _alarmRinging(false),
      _timerRunning(false), _timerRinging(false),
      _timerDurationSeconds(30 * 60), _timerRemainingSeconds(30 * 60) {
  initMenuButtons();
}

UIManager::~UIManager() {}

void UIManager::init() {
  Serial.println("UI: Initializing...");

  // Configure display
  M5.Display.setRotation(1); // Landscape
  M5.Display.setColorDepth(16);
  M5.Display.setBrightness(128);
  M5.Display.setFont(&fonts::DejaVu24); // System-wide font
  M5.Display.setTextSize(1);
  M5.Display.setEpdMode(
      epd_mode_t::epd_text); // Default to Fast Mode for UI navigation

  Serial.printf("UI: Display size %dx%d\n", M5.Display.width(),
                M5.Display.height());

  // Initialize M5GFX Renderer for EPub
  epubRenderer = new M5GFXRenderer();
  epubRenderer->set_margin_top(55);    // Room for EXIT/MENU buttons
  epubRenderer->set_margin_bottom(50); // Room for page number
  epubRenderer->set_margin_left(15);
  epubRenderer->set_margin_right(15);

  // Power System Init
  _powerHistory.init();

  _needsRefresh = true;
  _lastActivityTime = millis();
}

const m5gfx::IFont *UIManager::getReaderFont() {
  // Return font based on current family and size selection
  if (_readerFontSize == 2) {
    // Medium size
    return getReaderFontMedium(_readerFontFamily);
  } else {
    // Large size (default)
    return getReaderFontLarge(_readerFontFamily);
  }
}

void UIManager::initMenuButtons() {
  // Calculate button positions for bottom menu bar
  int buttonWidth = SCREEN_WIDTH / NUM_MENU_BUTTONS;
  int buttonY = SCREEN_HEIGHT - MENU_BAR_HEIGHT;

  _menuButtons[0] = {0 * buttonWidth, buttonY, buttonWidth,     MENU_BAR_HEIGHT,
                     "READ",          "BK",    ScreenID::READER};
  _menuButtons[1] = {1 * buttonWidth,     buttonY, buttonWidth,
                     MENU_BAR_HEIGHT,     "GAME",  "GM",
                     ScreenID::GAMES_MENU};
  _menuButtons[2] = {2 * buttonWidth, buttonY, buttonWidth,    MENU_BAR_HEIGHT,
                     "ALARM",         "AL",    ScreenID::CLOCK};
  _menuButtons[3] = {3 * buttonWidth,     buttonY, buttonWidth,
                     MENU_BAR_HEIGHT,     "CALC",  "CA",
                     ScreenID::CALCULATOR};
  _menuButtons[4] = {4 * buttonWidth, buttonY, buttonWidth,    MENU_BAR_HEIGHT,
                     "NOTES",         "NT",    ScreenID::NOTES};
  _menuButtons[5] = {5 * buttonWidth,   buttonY, buttonWidth,
                     MENU_BAR_HEIGHT,   "MENU",  "MN",
                     ScreenID::SETTINGS};
}

void UIManager::update() {
  // Serial.printf("UI::update() START - screen=%d needsRefresh=%d\n",
  //               (int)_currentScreen, _needsRefresh ? 1 : 0);
  // Check power savings first (might enter deep sleep)
  // Check power savings first (might enter deep sleep)
  checkPowerManagement();

  // Check Alarm/Timer globally (regardless of screen)
  checkAlarm();

  // Power History: Sample every 5 minutes and flush
  if (millis() - _lastHistorySample >= 300000) { // 5 minutes
    _lastHistorySample = millis();
    // Sample current power data (cast floats to uint16 for storage)
    _powerHistory.addSample((uint8_t)_powerData.batteryPercent,
                            (uint16_t)_powerData.inputPower,
                            (uint16_t)_powerData.outputPower);

    // Check if we should flush to SD (likely yes, since interval matches)
    if (_powerHistory.shouldFlush()) {
      _powerHistory.flushToSD();
    }
  }

  // Check for Book Loading Task completion
  if (g_bookLoadingComplete) {
    g_bookLoadingComplete = false;
    if (g_bookLoadSuccess) {
      Serial.println("UI: Book loaded successfully. Drawing screen.");
      drawReaderScreen();
    } else {
      Serial.println("UI: Book load failed.");
      _readerFilePath = "";
      M5.Display.fillScreen(COLOR_RED);
      delay(1000);         // Show error
      drawReaderBrowser(); // Go back to browser
    }
  }

  // Always update notes logic for continuous drawing
  if (_currentScreen == ScreenID::NOTES) {
    updateNotes();
  }

  // Auto-refresh Home Screen every 60 seconds (User Request)
  if (_currentScreen == ScreenID::HOME && (millis() - _lastRefresh > 60000)) {
    Serial.println("UI: 60s Auto-refresh trigger");
    _needsRefresh = true;
  }

  // Auto-refresh Timers screen every 15 seconds when a timer is active
  if (_currentScreen == ScreenID::SETTINGS_FOSSIBOT_TIMERS &&
      _fossiScheduleChargeRemaining > 0 && (millis() - _lastRefresh > 15000)) {
    Serial.println("UI: 15s Timers auto-refresh trigger");
    _needsRefresh = true;
  }

  // Auto-refresh Clock screen every second when Pomodoro or Timer is running
  if (_currentScreen == ScreenID::CLOCK && (millis() - _lastRefresh > 1000)) {
    bool pomodoroRunning = (_clockMode == ClockMode::POMODORO &&
                            _pomodoroState == PomodoroState::RUNNING);
    bool timerRunning = (_clockMode == ClockMode::TIMER && _timerRunning);
    if (pomodoroRunning || timerRunning) {
      _needsRefresh = true;
    }
  }

  // Watchdog: Clear stuck task after 30 seconds (Large fonts can be slow)
  unsigned long now = millis();
  if (g_readerBusy) {
    if (now - g_taskStartTime > 30000) {
      Serial.println("Watchdog: Reader task stuck! Forcing reset.");
      g_readerBusy = false;
      g_relayoutInProgress = false;
    } else {
      // CRITICAL: Yield to background task!
      // If we just spin here, we starve the task (if on same core) and flood
      // logs.
      delay(10);
      return;
    }
  }

  if (!_needsRefresh) {
    // Serial.println("UI::update() EXIT - !_needsRefresh");
    return;
  }
  Serial.printf("UI::update() - refreshing! lastRefresh=%lu now=%lu\n",
                _lastRefresh, millis());

  // Limit refresh rate based on setting
  // But allow the first refresh (_lastRefresh == 0)
  if (_lastRefresh != 0 && now - _lastRefresh < (_refreshRateSeconds * 1000)) {
    return;
  }

  switch (_currentScreen) {
  case ScreenID::HOME:
    drawHomeScreen();
    break;
  case ScreenID::SETTINGS:
    drawSettingsScreen();
    break;
  case ScreenID::SETTINGS_DEVICE:
    drawDeviceSettingsScreen();
    break;
  case ScreenID::SETTINGS_FOSSIBOT:
    drawFossibotSettingsScreen();
    break;
  case ScreenID::SETTINGS_FOSSIBOT_TIMERS:
    drawFossibotTimersScreen();
    break;
  case ScreenID::CLOCK:
    updatePomodoro(); // Update timer logic before drawing
    drawClockScreen();
    break;
  case ScreenID::CALCULATOR:
    drawCalculatorScreen();
    break;
  case ScreenID::NOTES:
    drawNotesScreen();
    break;
  case ScreenID::SD_DIAG:
    drawSDDiagScreen();
    break;
  case ScreenID::NOTES_BROWSE:
    drawNotesBrowseScreen();
    break;
  case ScreenID::GAMES_MENU:
    drawGamesMenu();
    break;
  case ScreenID::GAME_2048:
    drawGame2048();
    break;
  case ScreenID::GAME_SUDOKU:
    drawSudokuGame();
    break;
  case ScreenID::GAME_MINESWEEPER:
    drawMinesweeperGame();
    break;
  case ScreenID::HISTORY:
    drawHistoryScreen();
    break;
  case ScreenID::READER:
    drawReaderScreen();
    break;
  // Other screens will be implemented later
  default:
    drawHomeScreen(); // Fallback to home
    break;
  }

  // Check if Reader became busy during update (e.g. drawReaderScreen spawned a
  // task)
  if (g_readerBusy) {
    // If busy, DO NOT call display() locally, as task might be updating it
    Serial.println(
        "UI: Reader busy after update loop - Skipping main display flush");
    return;
  }

  Serial.println("UI: Calling M5.Display.display()");
  // Force E-Ink Refresh
  M5.Display.display();
  Serial.println("UI: Display refresh complete");

  _lastRefresh = now;
  _needsRefresh = false;
  _powerDataDirty = false;
}

// Dispatcher
void UIManager::handleTouch(int x, int y, TouchEvent event) {
  _lastActivityTime = millis(); // Reset idle timer

  // TAP TO DISMISS ALARM/TIMER (Any touch dismisses)
  if ((_alarmRinging || _timerRinging) && event == TouchEvent::RELEASE) {
    _alarmRinging = false;
    _timerRinging = false;
    Buzzer::stop(); // STOP the sound immediately
    forceRefresh();
    return; // Don't process other touches
  }

  // Clock Mode handles its own events for better responsiveness
  if (_currentScreen == ScreenID::CLOCK) {
    handleClockTouch(x, y, event);
    // Don't return! Let it fall through to handle menu buttons (on Release)
  }

  // ... (rest of function as before, but without CLOCK check inside tap block)
  // ...
  switch (event) {
  case TouchEvent::PRESS:
    _touchStartX = x;
    _touchStartY = y;
    _touchStartTime = millis();
    _isTouching = true;
    break;

  case TouchEvent::RELEASE:
    if (_isTouching) {
      // Check if it was a tap (not a drag)
      int dx = abs(x - _touchStartX);
      int dy = abs(y - _touchStartY);
      unsigned long duration = millis() - _touchStartTime;

      if (dx < 20 && dy < 20 &&
          duration < 800) { // Increased duration tolerance
        // It's a tap
        if (_currentScreen == ScreenID::HOME) {
          handleHomeTouch(x, y, event);
        } else if (_currentScreen == ScreenID::SETTINGS) {
          handleSettingsTouch(x, y);
          // CLOCK handled above
        } else if (_currentScreen == ScreenID::CALCULATOR) {
          handleCalculatorTouch(x, y);
          // ...

        } else if (_currentScreen == ScreenID::NOTES) {
          handleNotesTouch(x, y);
        } else if (_currentScreen == ScreenID::SD_DIAG) {
          handleSDDiagTouch(x, y);
        } else if (_currentScreen == ScreenID::NOTES_BROWSE) {
          handleNotesBrowseTouch(x, y);
        } else if (_currentScreen == ScreenID::GAMES_MENU) {
          handleGamesMenuTouch(x, y);
        } else if (_currentScreen == ScreenID::HISTORY) {
          handleHistoryTouch(x, y, event);
        } else if (_currentScreen == ScreenID::SETTINGS_DEVICE) {
          handleDeviceSettingsTouch(x, y);
        } else if (_currentScreen == ScreenID::SETTINGS_FOSSIBOT) {
          handleFossibotSettingsTouch(x, y);
        } else if (_currentScreen == ScreenID::SETTINGS_FOSSIBOT_TIMERS) {
          handleFossibotTimersTouch(x, y);
        } else if (_currentScreen == ScreenID::READER) {
          handleReaderTouch(x, y, event);
        }
      }

      // GAME_2048 needs swipe detection, handle separately
      if (_currentScreen == ScreenID::GAME_2048) {
        handleGame2048Touch(x, y, event);
      }

      // GAME_SUDOKU touch handling
      if (_currentScreen == ScreenID::GAME_SUDOKU) {
        handleSudokuTouch(x, y, event);
      }

      // GAME_MINESWEEPER touch handling
      if (_currentScreen == ScreenID::GAME_MINESWEEPER) {
        handleMinesweeperTouch(x, y, event);
      }

      // Check menu buttons (Allow navigation from all screens as requested)
      if (_currentScreen != ScreenID::NOTES &&
          _currentScreen != ScreenID::NOTES_BROWSE &&
          _currentScreen != ScreenID::GAME_2048 &&
          _currentScreen != ScreenID::GAME_SUDOKU &&
          _currentScreen != ScreenID::GAME_MINESWEEPER &&
          _currentScreen != ScreenID::HISTORY &&
          _currentScreen != ScreenID::READER) { // Disable nav bar in Reader
        int menuHit = hitTestMenuButton(x, y);
        if (menuHit >= 0) {
          executeMenuButton(menuHit);
        }
      }
    }
    break;
  }
}

int UIManager::hitTestMenuButton(int x, int y) {
  for (int i = 0; i < NUM_MENU_BUTTONS; i++) {
    const MenuButton &btn = _menuButtons[i];
    if (x >= btn.x && x < btn.x + btn.w && y >= btn.y && y < btn.y + btn.h) {
      return i;
    }
  }
  return -1;
}

void UIManager::executeMenuButton(int index) {
  if (index < 0 || index >= NUM_MENU_BUTTONS)
    return;

  Buzzer::click(); // Add feedback for menu buttons
  ScreenID target = _menuButtons[index].targetScreen;
  if (target != _currentScreen) {
    navigateTo(target);
  }
}

void UIManager::showHomeScreen() { navigateTo(ScreenID::HOME); }

void UIManager::navigateTo(ScreenID screen) {
  _previousScreen = _currentScreen;
  _currentScreen = screen;
  _needsRefresh = true;
  _lastRefresh = 0; // Force immediate refresh on navigation
  Serial.printf("UI: Navigate to screen %d\n", (int)screen);

  // GHOSTING FIX: Force full EPD quality refresh on screen transition
  // M5.Display.setEpdMode(epd_mode_t::epd_quality); // DISABLED: Causes DMA
  // contention with BLE M5.Display.fillScreen(COLOR_WHITE); // DISABLED: Causes
  // contention with BLE

  // SMART REFRESH: Switch modes based on content requirements
  if (screen == ScreenID::NOTES || screen == ScreenID::READER ||
      screen == ScreenID::GAMES_MENU || screen == ScreenID::GAME_2048 ||
      screen == ScreenID::GAME_SUDOKU || screen == ScreenID::GAME_MINESWEEPER ||
      screen == ScreenID::NOTES_BROWSE) {
    // High Quality Mode for content creation/reading
    M5.Display.setEpdMode(epd_mode_t::epd_quality);
    Serial.println("UI: Switched to EPD Quality Mode");
  } else {
    // Fast Mode for general UI (Battery Saving)
    M5.Display.setEpdMode(epd_mode_t::epd_text);
    Serial.println("UI: Switched to EPD Text Mode");
  }

  if (screen == ScreenID::SETTINGS) {
    // Load current time from RTC for editing
    int hours, minutes, seconds;
    RTC::getTime(hours, minutes, seconds);
    int year, month, day, weekday;
    RTC::getDate(year, month, day, weekday);
    _editYear = year;
    _editMonth = month;
    _editDay = day;
    _editHour = hours;
    _editMinute = minutes;

    // Load Auto Sleep setting
    extern Config *config;
    if (config) {
      _editAutoSleep = config->getAutoSleepMinutes();
    } else {
      _editAutoSleep = 60; // Default fallback
    }
  }

  // POWER OPTIMIZATION: Pause BLE when entering intensive screens
  // This stops polling and saves ~10-20mA while in Reader/Games/Notes
  bool enteringIntensiveScreen =
      (screen == ScreenID::READER || screen == ScreenID::NOTES ||
       screen == ScreenID::GAMES_MENU || screen == ScreenID::GAME_2048 ||
       screen == ScreenID::GAME_SUDOKU || screen == ScreenID::GAME_MINESWEEPER);
  bool leavingIntensiveScreen = (_previousScreen == ScreenID::READER ||
                                 _previousScreen == ScreenID::NOTES ||
                                 _previousScreen == ScreenID::GAMES_MENU ||
                                 _previousScreen == ScreenID::GAME_2048 ||
                                 _previousScreen == ScreenID::GAME_SUDOKU);

  if (enteringIntensiveScreen && !leavingIntensiveScreen) {
    extern FossibotBLE *bleClient;
    if (bleClient) {
      bleClient->pauseRetry();
      Serial.println("BLE: Paused for intensive screen (power saving)");
    }
  }

  // Scan books when entering Reader
  if (screen == ScreenID::READER) {
    readerScanBooks();
  }

  // Resume BLE when leaving intensive screens
  if (leavingIntensiveScreen && !enteringIntensiveScreen) {
    // Save progress before leaving
    readerSaveSettings();

    // Reset to landscape orientation (in case user navigated away without
    // closing book) Reset to landscape orientation (in case user navigated away
    // without closing book)
    M5.Display.setRotation(1);

    extern FossibotBLE *bleClient;
    if (bleClient) {
      bleClient->resumeRetry();
    }
  }
}

void UIManager::goBack() { navigateTo(_previousScreen); }

void UIManager::updatePowerBankData(const Fossibot::PowerBankData &data) {
  // Serial.printf("UI::updatePowerBankData() called - soc=%.1f connected=%d\n",
  //               data.batteryPercent, data.connected ? 1 : 0);
  bool wasSimulated = _powerData.simulatedError;
  _powerData = data;
  // Preserve simulated error state across BLE updates
  if (wasSimulated) {
    _powerData.simulatedError = true;
    _powerData.errorCode = 79;
    _powerData.protectionFlags = 0xE000;
    _powerData.systemStatusFlags |= Fossibot::SystemStatusBits::ERROR_PENDING;
  }
  _powerDataDirty = true;

  // Sync Fossibot settings to local UI variables (for preset highlighting)
  // Block sync if user recently interacted (5s) to prevent value bouncing
  bool suppressSync = (millis() - _lastTimerSetTime < 5000);

  if (data.settingsReceived && !suppressSync) {
    _fossiBuzzerEnabled = data.buzzerEnabled;
    _fossiSilentCharging = data.silentCharging;
    _fossiLightMode = data.lightMode;
    _fossiChargeLimit = data.chargeLimit;
    _fossiDischargeLimit = data.dischargeLimit;
    _fossiScreenTimeout = data.screenTimeout;
    _fossiACStandby = data.acStandby;
    _fossiDCStandby = data.dcStandby;
    _fossiUSBStandby = data.usbStandby;
    _fossiSysStandby = data.sysStandby;
    _fossiScheduleChargeRemaining = data.scheduleCharge;
  }

  // Only trigger auto-refresh on HOME screen or Timers screen (when timer
  // active)
  bool onTimersWithActiveTimer =
      (_currentScreen == ScreenID::SETTINGS_FOSSIBOT_TIMERS &&
       _fossiScheduleChargeRemaining > 0);
  if (_currentScreen != ScreenID::HOME && !onTimersWithActiveTimer) {
    return;
  }

  // Smart Refresh: Only update if data changed significantly
  if (shouldUpdateDashboard(data) ||
      (data.connected != _lastRenderedData.connected) ||
      (_powerData.hasError() != _lastRenderedData.hasError()) ||
      (onTimersWithActiveTimer &&
       data.scheduleCharge != _lastRenderedData.scheduleCharge)) {
    // Serial.println("UI::updatePowerBankData() - SETTING _needsRefresh =
    // true!");
    _needsRefresh = true;
    _lastRefresh = 0; // Bypass rate limiting for BLE updates
    // Store _powerData (includes simulated error state) to prevent
    // constant refresh loop when simulated error is active
    _lastRenderedData = _powerData;
    _lastDashboardUpdate = millis();
  } else {
    // Serial.println("UI::updatePowerBankData() - shouldUpdateDashboard
    // returned "
    //                "FALSE, not refreshing");
  }

  // Note: Power History sampling happens in update() on a 1-minute timer,
  // not here (to avoid sampling on every BLE update)
}

void UIManager::forceRefresh() {
  _needsRefresh = true;
  _lastRefresh = 0;
}

// ============================================================================
// Drawing Methods
// ============================================================================

void UIManager::drawHomeScreen() {
  extern Config *config;
  Serial.println("UI: Drawing home screen");

  M5.Display.fillScreen(COLOR_WHITE);

  // Dispatch to active theme
  String theme = config ? config->getTheme() : "classic_grid";
  if (theme == "compact_status") {
    drawHomeCompactStatus();
  } else if (theme == "horizontal_bars") {
    drawHomeHorizontalBars();
  } else if (theme == "sector") {
    drawHomeSector();
  } else {
    drawHomeClassicGrid();
  }

  drawMenuBar();
  M5.Display.display();
}

// ============================================================================
// Theme: Classic Grid (original 2x2 layout)
// ============================================================================
void UIManager::drawHomeClassicGrid() {
  int contentY = BATTERY_BAR_HEIGHT + PANEL_MARGIN;
  int contentHeight =
      SCREEN_HEIGHT - BATTERY_BAR_HEIGHT - MENU_BAR_HEIGHT - PANEL_MARGIN * 3;
  int panelWidth = (SCREEN_WIDTH - PANEL_MARGIN * 3) / 2;
  int panelHeight = (contentHeight - PANEL_MARGIN) / 2;

  drawBatteryBar(_powerData.batteryPercent);

  int topRowY = contentY;
  int bottomRowY = topRowY + panelHeight + PANEL_MARGIN;

  drawPowerPanel(PANEL_MARGIN, topRowY, panelWidth, panelHeight, "IN",
                 _powerData.inputPower, 1100.0f, "to full",
                 _powerData.minutesToFull, true);

  drawPowerPanel(PANEL_MARGIN, bottomRowY, panelWidth, panelHeight, "OUT",
                 _powerData.outputPower, 3000.0f, "remaining",
                 _powerData.minutesToEmpty, false);

  drawStatusPanel(PANEL_MARGIN * 2 + panelWidth, topRowY, panelWidth,
                  panelHeight);

  drawClockWeatherPanel(PANEL_MARGIN * 2 + panelWidth, bottomRowY, panelWidth,
                        panelHeight);
}

// ============================================================================
// Theme: Compact Status (large battery circle left, data stacked right)
// ============================================================================
void UIManager::drawHomeCompactStatus() {
  int contentBottom = SCREEN_HEIGHT - MENU_BAR_HEIGHT;

  // --- Toggle strip at the very bottom, just above menu bar ---
  int toggleStripH = 60;
  int toggleY = contentBottom - toggleStripH;
  M5.Display.drawFastHLine(0, toggleY, SCREEN_WIDTH, COLOR_GRAY);

  int toggleSpacing = SCREEN_WIDTH / 4;
  M5.Display.setFont(&fonts::DejaVu24);
  M5.Display.setTextColor(COLOR_BLACK);
  M5.Display.setTextSize(1);

  // USB toggle
  int usbCx = toggleSpacing;
  int labelTw = M5.Display.textWidth("USB");
  M5.Display.setCursor(usbCx - labelTw / 2, toggleY + 5);
  M5.Display.print("USB");
  drawToggle(usbCx - 15, toggleY + 10, "", _powerData.usbActive);

  // DC toggle
  int dcCx = toggleSpacing * 2;
  labelTw = M5.Display.textWidth("DC");
  M5.Display.setCursor(dcCx - labelTw / 2, toggleY + 5);
  M5.Display.print("DC");
  drawToggle(dcCx - 15, toggleY + 10, "", _powerData.dcActive);

  // AC toggle
  int acCx = toggleSpacing * 3;
  labelTw = M5.Display.textWidth("AC");
  M5.Display.setCursor(acCx - labelTw / 2, toggleY + 5);
  M5.Display.print("AC");
  drawToggle(acCx - 15, toggleY + 10, "", _powerData.acActive);

  // Connection indicator (far right of toggle strip)
  int connX = SCREEN_WIDTH - 40;
  int connDotY = toggleY + 30;
  if (_powerData.connected) {
    M5.Display.fillCircle(connX, connDotY, 6, COLOR_BLACK);
  } else {
    M5.Display.drawCircle(connX, connDotY, 6, COLOR_BLACK);
    M5.Display.drawLine(connX - 6, connDotY - 6, connX + 6, connDotY + 6, COLOR_BLACK);
  }

  // --- Main content area (above toggle strip) ---
  int mainH = toggleY; // available height for battery + data

  // --- Error banner (replaces battery area when error active) ---
  if (_powerData.hasError()) {
    drawErrorBanner(10, 10, SCREEN_WIDTH - 20, 80);
  } else {
    // --- Battery square ring (left half) ---
    // Square ring whose border thickness scales with battery %
    // At 100% the ring is thick (maxThick), at 0% it's thin (minThick)
    // The ring fills clockwise from 12 o'clock
    int sqSize = 240; // outer square side length
    int sqX = (SCREEN_WIDTH / 2 - sqSize) / 2; // centered in left half
    int sqY = (mainH - sqSize) / 2;

    float pct = _powerData.batteryPercent / 100.0f;
    if (pct > 1.0f) pct = 1.0f;
    if (pct < 0.0f) pct = 0.0f;

    int minThick = 3;   // thickness at 0%
    int maxThick = 30;  // thickness at 100%
    int thick = minThick + (int)((maxThick - minThick) * pct);

    // Draw thin outline always (the frame)
    M5.Display.drawRect(sqX, sqY, sqSize, sqSize, COLOR_BLACK);
    M5.Display.drawRect(sqX + 1, sqY + 1, sqSize - 2, sqSize - 2, COLOR_BLACK);

    // Draw the thick filled ring clockwise from top-center
    // Total perimeter = 4 * sqSize. Fill pct of it clockwise from top-center.
    int perim = 4 * sqSize;
    int fillLen = (int)(perim * pct);
    int drawn = 0;

    // Segment 1: Top edge, from center to right
    int seg1Len = sqSize / 2;
    if (drawn < fillLen) {
      int len = fillLen - drawn;
      if (len > seg1Len) len = seg1Len;
      M5.Display.fillRect(sqX + sqSize / 2, sqY, len, thick, COLOR_BLACK);
      drawn += len;
    }

    // Segment 2: Right edge, top to bottom
    int seg2Len = sqSize;
    if (drawn < fillLen) {
      int len = fillLen - drawn;
      if (len > seg2Len) len = seg2Len;
      M5.Display.fillRect(sqX + sqSize - thick, sqY, thick, len, COLOR_BLACK);
      drawn += len;
    }

    // Segment 3: Bottom edge, right to left
    int seg3Len = sqSize;
    if (drawn < fillLen) {
      int len = fillLen - drawn;
      if (len > seg3Len) len = seg3Len;
      M5.Display.fillRect(sqX + sqSize - len, sqY + sqSize - thick, len, thick, COLOR_BLACK);
      drawn += len;
    }

    // Segment 4: Left edge, bottom to top
    int seg4Len = sqSize;
    if (drawn < fillLen) {
      int len = fillLen - drawn;
      if (len > seg4Len) len = seg4Len;
      M5.Display.fillRect(sqX, sqY + sqSize - len, thick, len, COLOR_BLACK);
      drawn += len;
    }

    // Segment 5: Top edge, left to center
    int seg5Len = sqSize / 2;
    if (drawn < fillLen) {
      int len = fillLen - drawn;
      if (len > seg5Len) len = seg5Len;
      M5.Display.fillRect(sqX, sqY, len, thick, COLOR_BLACK);
      drawn += len;
    }

    // Percentage text centered in the square
    char pctStr[8];
    snprintf(pctStr, sizeof(pctStr), "%.0f%%", _powerData.batteryPercent);
    M5.Display.setTextColor(COLOR_BLACK);
    M5.Display.setFont(&fonts::DejaVu24);
    M5.Display.setTextSize(3);
    int tw = M5.Display.textWidth(pctStr);
    int th = M5.Display.fontHeight();
    M5.Display.setCursor(sqX + (sqSize - tw) / 2, sqY + (sqSize - th) / 2);
    M5.Display.print(pctStr);
  }

  // --- Right half: Power data stacked ---
  int rightX = SCREEN_WIDTH / 2 + 20;
  int rightEdge = SCREEN_WIDTH - 20;
  int dataY = 20;

  M5.Display.setTextColor(COLOR_BLACK);
  M5.Display.setFont(&fonts::DejaVu24);

  // IN row — label left, watts far right
  M5.Display.setTextSize(2);
  M5.Display.setCursor(rightX, dataY + 8);
  M5.Display.print("IN");
  char wBuf[16];
  snprintf(wBuf, sizeof(wBuf), "%.0fW", _powerData.inputPower);
  M5.Display.setTextSize(3);
  int ww = M5.Display.textWidth(wBuf);
  M5.Display.setCursor(rightEdge - ww, dataY);
  M5.Display.print(wBuf);
  M5.Display.setTextSize(1);
  M5.Display.setCursor(rightX, dataY + 60);
  M5.Display.print(Fossibot::formatTime(_powerData.minutesToFull));
  M5.Display.print(" to full");

  // Separator
  int sepY = dataY + 90;
  M5.Display.drawFastHLine(rightX, sepY, rightEdge - rightX, COLOR_GRAY);

  // OUT row — label left, watts far right
  int outY = sepY + 15;
  M5.Display.setTextSize(2);
  M5.Display.setCursor(rightX, outY + 8);
  M5.Display.print("OUT");
  snprintf(wBuf, sizeof(wBuf), "%.0fW", _powerData.outputPower);
  M5.Display.setTextSize(3);
  ww = M5.Display.textWidth(wBuf);
  M5.Display.setCursor(rightEdge - ww, outY);
  M5.Display.print(wBuf);
  M5.Display.setTextSize(1);
  M5.Display.setCursor(rightX, outY + 60);
  M5.Display.print(Fossibot::formatTime(_powerData.minutesToEmpty));
  M5.Display.print(" remaining");

  // Separator
  int sep2Y = outY + 90;
  M5.Display.drawFastHLine(rightX, sep2Y, rightEdge - rightX, COLOR_GRAY);

  // Clock — time far right, date below left
  int clockY = sep2Y + 15;
  int displayHour, displayMinute, displaySecond;
  RTC::getTime(displayHour, displayMinute, displaySecond);
  int displayYear, displayMonth, displayDay, displayDow;
  RTC::getDate(displayYear, displayMonth, displayDay, displayDow);

  const char *dayNames[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  const char *monthNames[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                              "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  int mon = displayMonth - 1;
  if (mon < 0 || mon > 11) mon = 0;
  if (displayDow < 0 || displayDow > 6) displayDow = 0;

  char timeStr[16];
  snprintf(timeStr, sizeof(timeStr), "%02d:%02d", displayHour, displayMinute);
  M5.Display.setTextSize(3);
  int ttw = M5.Display.textWidth(timeStr);
  M5.Display.setCursor(rightEdge - ttw, clockY);
  M5.Display.print(timeStr);

  char dateStr[32];
  snprintf(dateStr, sizeof(dateStr), "%s %d %s %d", dayNames[displayDow],
           displayDay, monthNames[mon], displayYear);
  M5.Display.setTextSize(1);
  M5.Display.setCursor(rightX, clockY + 55);
  M5.Display.print(dateStr);
}

// ============================================================================
// Theme: Horizontal Bars (full-width rows, HUD style)
// ============================================================================
void UIManager::drawHomeHorizontalBars() {
  int contentBottom = SCREEN_HEIGHT - MENU_BAR_HEIGHT;
  int margin = 10;
  int barW = SCREEN_WIDTH - margin * 2;
  int gap = 5;

  // Layout: 4 rows filling available space
  // Row 1 (battery): shorter, Row 2/3 (IN/OUT): taller, Row 4 (clock+toggles): remainder
  int battRowH = 55;
  int powerRowH = 105; // 1.5x original 70
  int usedH = battRowH + powerRowH * 2 + gap * 3;
  int row4H = contentBottom - margin - usedH - gap - margin;

  // Row 1: Battery — full-width bar, no label, % right-aligned
  int row1Y = margin;
  if (_powerData.hasError()) {
    drawErrorBanner(margin, row1Y, barW, battRowH);
  } else {
    M5.Display.drawRect(margin, row1Y, barW, battRowH, COLOR_BLACK);
    // Full-width progress bar with padding
    int pbX = margin + 10;
    int pbW = barW - 120; // leave room for % text
    int pbH = battRowH - 16;
    drawProgressBar(pbX, row1Y + 8, pbW, pbH,
                    _powerData.batteryPercent / 100.0f, true);
    // Percentage right-aligned
    M5.Display.setTextColor(COLOR_BLACK);
    M5.Display.setTextSize(2);
    char pctStr[8];
    snprintf(pctStr, sizeof(pctStr), "%.0f%%", _powerData.batteryPercent);
    int pw = M5.Display.textWidth(pctStr);
    M5.Display.setCursor(margin + barW - pw - 15, row1Y + (battRowH - 16) / 2);
    M5.Display.print(pctStr);
  }

  // Row 2: IN — label and watts inline, bar below
  int row2Y = row1Y + battRowH + gap;
  M5.Display.drawRect(margin, row2Y, barW, powerRowH, COLOR_BLACK);
  M5.Display.setTextColor(COLOR_BLACK);
  // Label top-left, watts right-aligned — same text size, inline
  M5.Display.setTextSize(2);
  M5.Display.setCursor(margin + 15, row2Y + 10);
  M5.Display.print("IN");
  char wBuf[16];
  snprintf(wBuf, sizeof(wBuf), "%.0f", _powerData.inputPower);
  int ww = M5.Display.textWidth(wBuf);
  M5.Display.setCursor(margin + barW - ww - 15, row2Y + 10);
  M5.Display.print(wBuf);
  // Progress bar below the text line
  int pbX = margin + 15;
  int pbW = barW - 30;
  drawProgressBar(pbX, row2Y + 45, pbW, 25,
                  _powerData.inputPower / 1100.0f, true);
  // Time bottom-left
  M5.Display.setTextSize(1);
  M5.Display.setCursor(margin + 15, row2Y + powerRowH - 22);
  M5.Display.print(Fossibot::formatTime(_powerData.minutesToFull));
  M5.Display.print(" to full");

  // Row 3: OUT — label and watts inline, bar below
  int row3Y = row2Y + powerRowH + gap;
  M5.Display.drawRect(margin, row3Y, barW, powerRowH, COLOR_BLACK);
  M5.Display.setTextColor(COLOR_BLACK);
  // Label top-left, watts right-aligned — same text size, inline
  M5.Display.setTextSize(2);
  M5.Display.setCursor(margin + 15, row3Y + 10);
  M5.Display.print("OUT");
  snprintf(wBuf, sizeof(wBuf), "%.0f", _powerData.outputPower);
  ww = M5.Display.textWidth(wBuf);
  M5.Display.setCursor(margin + barW - ww - 15, row3Y + 10);
  M5.Display.print(wBuf);
  // Progress bar below the text line
  drawProgressBar(pbX, row3Y + 45, pbW, 25,
                  _powerData.outputPower / 3000.0f, true);
  // Time bottom-left
  M5.Display.setTextSize(1);
  M5.Display.setCursor(margin + 15, row3Y + powerRowH - 22);
  M5.Display.print(Fossibot::formatTime(_powerData.minutesToEmpty));
  M5.Display.print(" remaining");

  // Row 4: Clock + Toggles
  int row4Y = row3Y + powerRowH + gap;
  M5.Display.drawRect(margin, row4Y, barW, row4H, COLOR_BLACK);

  // Clock (left side) — time large, date well below
  int displayHour, displayMinute, displaySecond;
  RTC::getTime(displayHour, displayMinute, displaySecond);
  int displayYear, displayMonth, displayDay, displayDow;
  RTC::getDate(displayYear, displayMonth, displayDay, displayDow);

  const char *dayNames[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  const char *monthNames[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                              "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  int mon = displayMonth - 1;
  if (mon < 0 || mon > 11) mon = 0;
  if (displayDow < 0 || displayDow > 6) displayDow = 0;

  char timeStr[16];
  snprintf(timeStr, sizeof(timeStr), "%02d:%02d", displayHour, displayMinute);
  M5.Display.setTextColor(COLOR_BLACK);
  M5.Display.setTextSize(4);
  M5.Display.setCursor(margin + 20, row4Y + 8);
  M5.Display.print(timeStr);

  char dateStr[32];
  snprintf(dateStr, sizeof(dateStr), "%s %d %s %d", dayNames[displayDow],
           displayDay, monthNames[mon], displayYear);
  M5.Display.setTextSize(1);
  M5.Display.setCursor(margin + 20, row4Y + row4H - 27);
  M5.Display.print(dateStr);

  // Toggles (right side)
  int toggleBaseX = SCREEN_WIDTH / 2 + 80;
  int toggleCenterY = row4Y + row4H / 2;
  M5.Display.setFont(&fonts::DejaVu24);

  // USB
  M5.Display.setTextColor(COLOR_BLACK);
  M5.Display.setTextSize(1.5);
  int tw = M5.Display.textWidth("USB");
  M5.Display.setCursor(toggleBaseX - tw / 2, toggleCenterY - 25);
  M5.Display.print("USB");
  drawToggle(toggleBaseX - 15, toggleCenterY - 18, "", _powerData.usbActive);

  // DC
  int dcX = toggleBaseX + 130;
  tw = M5.Display.textWidth("DC");
  M5.Display.setCursor(dcX - tw / 2, toggleCenterY - 25);
  M5.Display.print("DC");
  drawToggle(dcX - 15, toggleCenterY - 18, "", _powerData.dcActive);

  // AC
  int acX = dcX + 130;
  tw = M5.Display.textWidth("AC");
  M5.Display.setCursor(acX - tw / 2, toggleCenterY - 25);
  M5.Display.print("AC");
  drawToggle(acX - 15, toggleCenterY - 18, "", _powerData.acActive);

  // Connection indicator
  int connX = SCREEN_WIDTH - 40;
  if (_powerData.connected) {
    M5.Display.fillCircle(connX, toggleCenterY, 6, COLOR_BLACK);
  } else {
    M5.Display.drawCircle(connX, toggleCenterY, 6, COLOR_BLACK);
    M5.Display.drawLine(connX - 6, toggleCenterY - 6, connX + 6, toggleCenterY + 6, COLOR_BLACK);
  }
}

// ============================================================================
// Theme: Sector (output power breakdown by USB / AC / DC)
// ============================================================================
void UIManager::drawHomeSector() {
  int contentBottom = SCREEN_HEIGHT - MENU_BAR_HEIGHT;
  int margin = 10;

  drawBatteryBar(_powerData.batteryPercent);

  int topY = BATTERY_BAR_HEIGHT + margin;
  int availH = contentBottom - topY - margin;

  // --- Left half: Total IN / Total OUT with time ---
  int leftW = SCREEN_WIDTH / 2 - margin * 2;
  int panelH = (availH - margin) / 2;

  // IN panel
  drawPowerPanel(margin, topY, leftW, panelH, "IN",
                 _powerData.inputPower, 1100.0f, "to full",
                 _powerData.minutesToFull, true);

  // OUT panel
  drawPowerPanel(margin, topY + panelH + margin, leftW, panelH, "OUT",
                 _powerData.outputPower, 3000.0f, "remaining",
                 _powerData.minutesToEmpty, false);

  // --- Right half: Output breakdown sectors ---
  int rightX = SCREEN_WIDTH / 2 + margin;
  int rightW = SCREEN_WIDTH / 2 - margin * 2;
  int sectorH = (availH - margin * 2) / 3; // 3 rows: USB, AC, DC

  M5.Display.setTextColor(COLOR_BLACK);

  // Determine what label to show for the non-USB power
  // If only AC active: label it "AC"
  // If only DC active: label it "DC"
  // If both: label it "AC+DC"
  // If neither: show 0 for both
  bool acOn = _powerData.acActive;
  bool dcOn = _powerData.dcActive;

  // --- USB Sector ---
  int usbY = topY;
  M5.Display.drawRect(rightX, usbY, rightW, sectorH, COLOR_BLACK);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(rightX + 15, usbY + 12);
  M5.Display.print("USB");
  M5.Display.setTextSize(3);
  char buf[16];
  snprintf(buf, sizeof(buf), "%.1fW", _powerData.usbOutputPower);
  int tw = M5.Display.textWidth(buf);
  M5.Display.setCursor(rightX + rightW - tw - 15, usbY + 10);
  M5.Display.print(buf);
  // Progress bar
  int pbY = usbY + sectorH - 20;
  drawProgressBar(rightX + 15, pbY, rightW - 30, 12,
                  _powerData.usbOutputPower / 500.0f, false);

  // --- AC Sector ---
  int acY = usbY + sectorH + margin;
  M5.Display.drawRect(rightX, acY, rightW, sectorH, COLOR_BLACK);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(rightX + 15, acY + 12);
  M5.Display.print("AC");

  float acPower = 0.0f;
  if (acOn && !dcOn) {
    acPower = _powerData.acDcOutputPower;
  } else if (acOn && dcOn) {
    // Both on — can't split, show nothing here (combined shown in DC row)
    acPower = 0.0f;
  }
  M5.Display.setTextSize(3);
  snprintf(buf, sizeof(buf), "%.0fW", acPower);
  tw = M5.Display.textWidth(buf);
  M5.Display.setCursor(rightX + rightW - tw - 15, acY + 10);
  M5.Display.print(buf);
  if (!acOn) {
    M5.Display.setTextSize(1);
    M5.Display.setCursor(rightX + 15, acY + sectorH - 22);
    M5.Display.print("OFF");
  } else {
    drawProgressBar(rightX + 15, acY + sectorH - 20, rightW - 30, 12,
                    acPower / 3000.0f, false);
  }

  // --- DC Sector ---
  int dcY = acY + sectorH + margin;
  M5.Display.drawRect(rightX, dcY, rightW, sectorH, COLOR_BLACK);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(rightX + 15, dcY + 12);
  if (acOn && dcOn) {
    M5.Display.print("AC+DC");
  } else {
    M5.Display.print("DC");
  }

  float dcPower = 0.0f;
  if (dcOn && !acOn) {
    dcPower = _powerData.acDcOutputPower;
  } else if (acOn && dcOn) {
    // Both on — show combined non-USB power here
    dcPower = _powerData.acDcOutputPower;
  }
  M5.Display.setTextSize(3);
  snprintf(buf, sizeof(buf), "%.0fW", dcPower);
  tw = M5.Display.textWidth(buf);
  M5.Display.setCursor(rightX + rightW - tw - 15, dcY + 10);
  M5.Display.print(buf);
  if (!dcOn && !acOn) {
    M5.Display.setTextSize(1);
    M5.Display.setCursor(rightX + 15, dcY + sectorH - 22);
    M5.Display.print("OFF");
  } else {
    float maxDc = (acOn && dcOn) ? 3000.0f : 1000.0f;
    drawProgressBar(rightX + 15, dcY + sectorH - 20, rightW - 30, 12,
                    dcPower / maxDc, false);
  }

  // Connection indicator (bottom-right corner)
  int connX = SCREEN_WIDTH - 25;
  int connY = contentBottom - 20;
  if (_powerData.connected) {
    M5.Display.fillCircle(connX, connY, 6, COLOR_BLACK);
  } else {
    M5.Display.drawCircle(connX, connY, 6, COLOR_BLACK);
    M5.Display.drawLine(connX - 6, connY - 6, connX + 6, connY + 6, COLOR_BLACK);
  }
}

void UIManager::drawErrorBanner(int x, int y, int w, int h) {
  M5.Display.fillRect(x, y, w, h, COLOR_BLACK);

  M5.Display.setTextColor(COLOR_WHITE);
  M5.Display.setTextSize(2);

  const char *errMsg;
  if (_powerData.isEnvironmentalProtection()) {
    errMsg = "TEMP/SAFETY PROTECTION";
  } else if (_powerData.errorCode == 78) {
    errMsg = "INVERTER FAULT - DC OK";
  } else {
    errMsg = "DEVICE ERROR - GOTO FOSSI";
  }
  int textW = M5.Display.textWidth(errMsg);
  int textX = x + (w - textW) / 2;
  int textY = y + (h / 2) - 16;
  M5.Display.setCursor(textX, textY);
  M5.Display.print(errMsg);

  // Detail line
  M5.Display.setTextSize(1);
  char detailStr[64];
  const char *classification =
      _powerData.isEnvironmentalProtection()  ? "Temp/Safety"
      : _powerData.hasCriticalHardwareFault() ? "Hardware"
                                              : "Fault";
  snprintf(detailStr, sizeof(detailStr), "Code: %d  Flags: 0x%04X  [%s]",
           _powerData.errorCode, _powerData.protectionFlags, classification);
  int detailW = M5.Display.textWidth(detailStr);
  M5.Display.setCursor(x + (w - detailW) / 2, textY + 30);
  M5.Display.print(detailStr);
}

void UIManager::drawBatteryBar(float percent) {
  int barY = 5;
  int barHeight = BATTERY_BAR_HEIGHT - 10;
  int barWidth = SCREEN_WIDTH - 10; // Full width with small margin

  // === ERROR BANNER: Replace battery bar when device has active fault ===
  if (_powerData.hasError()) {
    // Solid black background - inverted for maximum visibility on e-ink
    M5.Display.fillRect(5, barY, barWidth, barHeight, COLOR_BLACK);
    M5.Display.drawRect(5, barY, barWidth, barHeight, COLOR_BLACK);

    M5.Display.setTextColor(COLOR_WHITE);
    M5.Display.setTextSize(2);

    // Error message varies by type:
    // Error 79 with NO critical bits (0x6000) in Reg 42 = Temp/Safety
    // Protection Error 79 WITH critical bits = Hardware Failure Error 78 =
    // Inverter Fault (DC charging via solar still works)
    const char *errMsg;
    if (_powerData.isEnvironmentalProtection()) {
      errMsg = "TEMP/SAFETY PROTECTION";
    } else if (_powerData.errorCode == 78) {
      errMsg = "INVERTER FAULT - DC OK";
    } else {
      errMsg = "DEVICE ERROR - GOTO FOSSI";
    }
    int textW = M5.Display.textWidth(errMsg);
    int textX = (SCREEN_WIDTH - textW) / 2;
    int textY = barY + (barHeight / 2) - 16;

    // Draw warning triangles and message
    // Left warning triangle (manual draw for e-ink compatibility)
    int triX = textX - 45;
    int triCY = barY + barHeight / 2;
    M5.Display.fillTriangle(triX, triCY + 12, triX + 12, triCY - 12, triX + 24,
                            triCY + 12, COLOR_WHITE);
    M5.Display.fillTriangle(triX + 4, triCY + 8, triX + 12, triCY - 6,
                            triX + 20, triCY + 8, COLOR_BLACK);
    // Exclamation mark in triangle
    M5.Display.fillRect(triX + 11, triCY - 3, 3, 8, COLOR_WHITE);
    M5.Display.fillRect(triX + 11, triCY + 7, 3, 3, COLOR_WHITE);

    M5.Display.setCursor(textX, textY);
    M5.Display.print(errMsg);

    // Right warning triangle
    int triX2 = textX + textW + 20;
    M5.Display.fillTriangle(triX2, triCY + 12, triX2 + 12, triCY - 12,
                            triX2 + 24, triCY + 12, COLOR_WHITE);
    M5.Display.fillTriangle(triX2 + 4, triCY + 8, triX2 + 12, triCY - 6,
                            triX2 + 20, triCY + 8, COLOR_BLACK);
    M5.Display.fillRect(triX2 + 11, triCY - 3, 3, 8, COLOR_WHITE);
    M5.Display.fillRect(triX2 + 11, triCY + 7, 3, 3, COLOR_WHITE);

    // Show error code, protection flags, and classification on second line
    M5.Display.setTextSize(1);
    char detailStr[64];
    const char *classification =
        _powerData.isEnvironmentalProtection()  ? "Temp/Safety"
        : _powerData.hasCriticalHardwareFault() ? "Hardware"
                                                : "Fault";
    snprintf(detailStr, sizeof(detailStr), "Code: %d  Flags: 0x%04X  [%s]",
             _powerData.errorCode, _powerData.protectionFlags, classification);
    int detailW = M5.Display.textWidth(detailStr);
    M5.Display.setCursor((SCREEN_WIDTH - detailW) / 2, textY + 35);
    M5.Display.print(detailStr);

    Serial.printf("UI: Drawing ERROR banner - Code=%d Flags=0x%04X [%s]\n",
                  _powerData.errorCode, _powerData.protectionFlags,
                  classification);
    return;
  }

  // === Normal battery bar ===
  // Draw border
  M5.Display.drawRect(5, barY, barWidth, barHeight, COLOR_BLACK);
  M5.Display.drawRect(6, barY + 1, barWidth - 2, barHeight - 2, COLOR_BLACK);

  // Fill based on percentage
  int fillWidth = (int)((barWidth - 8) * (percent / 100.0f));
  if (fillWidth > 0) {
    M5.Display.fillRect(8, barY + 4, fillWidth, barHeight - 8, COLOR_BLACK);
  }

  // Draw percentage text overlaid on bar (white on filled area, or black on
  // empty)
  char percentStr[8];
  snprintf(percentStr, sizeof(percentStr), "%.0f%%", percent);

  // Position text in center of bar
  int textX = SCREEN_WIDTH / 2 - 40;
  int textY = barY + (barHeight / 2) - 12;

  // Draw text with contrasting color
  if (percent > 50) {
    M5.Display.setTextColor(COLOR_WHITE);
  } else {
    M5.Display.setTextColor(COLOR_BLACK);
  }
  M5.Display.setTextSize(1);
  M5.Display.setCursor(textX, textY);
  M5.Display.print(percentStr);
}

void UIManager::drawPowerPanel(int x, int y, int w, int h, const char *title,
                               float power, float maxPower,
                               const char *timeLabel, int minutes,
                               bool isInput) {
  // Panel border
  M5.Display.drawRect(x, y, w, h, COLOR_BLACK);
  M5.Display.drawRect(x + 1, y + 1, w - 2, h - 2, COLOR_GRAY);

  // 1. Label and Power (Top)
  M5.Display.setTextColor(COLOR_BLACK);
  M5.Display.setTextSize(1);
  M5.Display.setCursor(x + 20, y + 25);
  M5.Display.print(title);

  // Power value (large bold) on SAME LINE
  // Moved +15px right as requested (x + 85) -> +40px more (x + 125)
  // Moved +3px down as requested (y + 10 -> y + 13)
  M5.Display.setTextSize(3);
  M5.Display.setCursor(x + 125, y + 13);
  M5.Display.printf("%.0f W", power);

  // 2. Time Remaining (Bottom)
  // Moved -3px up as requested (y + h - 25 -> y + h - 28)
  int timeY = y + h - 28;
  M5.Display.setTextSize(1);
  M5.Display.setCursor(x + 20, timeY);
  M5.Display.print(Fossibot::formatTime(minutes));
  M5.Display.print(" ");
  M5.Display.print(timeLabel);

  // 3. Progress Bar (Centered in middle space)
  // Top area ends approx y+50 (Wattage height)
  // Bottom area starts at timeY (approx y+h-28)
  // Available vertical space center:
  int topY = y + 50;
  int bottomY = timeY;
  int barH = 30;                                 // Thickness
  int barY = topY + (bottomY - topY - barH) / 2; // Vertically centered

  int barX = x + 20;
  int barW = w - 40;
  drawProgressBar(barX, barY, barW, barH, power / maxPower, true);
}

void UIManager::drawStatusPanel(int x, int y, int w, int h) {
  // Panel border
  M5.Display.drawRect(x, y, w, h, COLOR_BLACK);
  M5.Display.drawRect(x + 1, y + 1, w - 2, h - 2, COLOR_GRAY);

  // Connection status (Symbol only - Top Right)
  if (_powerData.connected) {
    M5.Display.fillCircle(x + w - 30, y + 25, 6, COLOR_BLACK);
  } else {
    M5.Display.drawCircle(x + w - 30, y + 25, 6, COLOR_BLACK);
    M5.Display.drawLine(x + w - 36, y + 19, x + w - 24, y + 31, COLOR_BLACK);
  }

  // Error pending indicator (Reg 48 bit 0x0008) - small warning triangle
  if (_powerData.hasErrorPending()) {
    int warnX = x + w - 60;
    int warnY = y + 18;
    M5.Display.fillTriangle(warnX, warnY + 14, warnX + 7, warnY, warnX + 14,
                            warnY + 14, COLOR_BLACK);
    M5.Display.setTextColor(COLOR_WHITE);
    M5.Display.setTextSize(1);
    M5.Display.setCursor(warnX + 5, warnY + 5);
    M5.Display.print("!");
  }

  // Output toggles - HORIZONTAL layout centered in panel
  // 3 buttons side-by-side: USB | DC | AC
  int toggleSpacing = (w - 40) / 3;
  // NOW: Strictly centered vertically, moved up slightly (removed +10 offset)
  int startY = y + (h - 50) / 2;

  // Set standard font for consistency
  M5.Display.setFont(&fonts::DejaVu24);

  // USB
  int usbX = x + 20 + (toggleSpacing - 70) / 2; // Center in first third
  M5.Display.setTextColor(COLOR_BLACK);
  M5.Display.setTextSize(1.5); // Increased from 1 to 1.5
  int textW = M5.Display.textWidth("USB");
  M5.Display.setCursor(usbX + (70 - textW) / 2, startY);
  M5.Display.print("USB");
  drawToggle(usbX + 10, startY + 15, "", _powerData.usbActive);

  // DC
  int dcX = x + 20 + toggleSpacing +
            (toggleSpacing - 70) / 2; // Center in second third
  M5.Display.setTextSize(1.5);        // Increased from 1 to 1.5
  textW = M5.Display.textWidth("DC");
  M5.Display.setCursor(dcX + (70 - textW) / 2, startY);
  M5.Display.print("DC");
  drawToggle(dcX + 10, startY + 15, "", _powerData.dcActive);

  // AC
  int acX = x + 20 + toggleSpacing * 2 +
            (toggleSpacing - 70) / 2; // Center in third third
  M5.Display.setTextSize(1.5);        // Increased from 1 to 1.5
  textW = M5.Display.textWidth("AC");
  M5.Display.setCursor(acX + (70 - textW) / 2, startY);
  M5.Display.print("AC");
  drawToggle(acX + 10, startY + 15, "", _powerData.acActive);
}

void UIManager::drawClockWeatherPanel(int x, int y, int w, int h) {
  // Panel border
  M5.Display.drawRect(x, y, w, h, COLOR_BLACK);
  M5.Display.drawRect(x + 1, y + 1, w - 2, h - 2, COLOR_GRAY);

  // Get current time from RTC via direct Wire access
  int displayHour, displayMinute, displaySecond;
  RTC::getTime(displayHour, displayMinute, displaySecond);
  int displayYear, displayMonth, displayDay, displayDow;
  RTC::getDate(displayYear, displayMonth, displayDay, displayDow);

  // Day of week names
  const char *dayNames[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  const char *monthNames[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                              "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

  int mon = displayMonth - 1;
  if (mon < 0 || mon > 11)
    mon = 0;
  if (displayDow < 0 || displayDow > 6)
    displayDow = 0;

  // Draw time large bold (size 3)
  char timeStr[16];
  snprintf(timeStr, sizeof(timeStr), "%02d:%02d", displayHour, displayMinute);
  M5.Display.setTextColor(COLOR_BLACK);
  M5.Display.setTextSize(3);
  // Center vertically approx
  int timeY = y + 25; // Adjusted slightly up to make room
  M5.Display.setCursor(x + 20, timeY);
  M5.Display.print(timeStr);

  // Draw date below (size 1)
  // Added +15px spacing (68 -> 83)
  char dateStr[32];
  snprintf(dateStr, sizeof(dateStr), "%s %d %s %d", dayNames[displayDow],
           displayDay, monthNames[mon], displayYear);
  M5.Display.setTextSize(1);
  M5.Display.setCursor(x + 20, timeY + 83);
  M5.Display.print(dateStr);
}

void UIManager::drawMenuBar() {
  int y = SCREEN_HEIGHT - MENU_BAR_HEIGHT;

  // Background
  M5.Display.fillRect(0, y, SCREEN_WIDTH, MENU_BAR_HEIGHT, COLOR_LIGHT_GRAY);
  M5.Display.drawLine(0, y, SCREEN_WIDTH, y, COLOR_BLACK);

  // Draw each button
  M5.Display.setTextSize(1);
  for (int i = 0; i < NUM_MENU_BUTTONS; i++) {
    const MenuButton &btn = _menuButtons[i];

    // Button separator
    if (i > 0) {
      M5.Display.drawLine(btn.x, y + 5, btn.x, y + MENU_BAR_HEIGHT - 5,
                          COLOR_GRAY);
    }

    // Label - centered in button
    M5.Display.setTextColor(COLOR_BLACK);
    int textWidth = strlen(btn.label) * 18; // Approximate width at size 3
    M5.Display.setCursor(btn.x + (btn.w - M5.Display.textWidth(btn.label)) / 2,
                         y + (MENU_BAR_HEIGHT - 24) / 2);
    M5.Display.print(btn.label);
  }
}

void UIManager::drawProgressBar(int x, int y, int w, int h, float percent,
                                bool thick) {
  if (percent < 0)
    percent = 0;
  if (percent > 1)
    percent = 1;

  // Border
  M5.Display.drawRect(x, y, w, h, COLOR_BLACK);

  // Fill
  int fillWidth = (int)((w - 4) * percent);
  if (fillWidth > 0) {
    M5.Display.fillRect(x + 2, y + 2, fillWidth, h - 4, COLOR_BLACK);
  }

  // If thick, draw second line
  if (thick && h >= 16) {
    M5.Display.drawRect(x, y + h / 2, w, h / 2, COLOR_BLACK);
    if (fillWidth > 0) {
      M5.Display.fillRect(x + 2, y + h / 2 + 2, fillWidth, h / 2 - 4,
                          COLOR_BLACK);
    }
  }
}

void UIManager::drawToggle(int x, int y, const char *label, bool active) {
  M5.Display.setTextColor(COLOR_BLACK);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(x, y);
  M5.Display.print(label);

  // Draw simplified state indicator below
  int indX = x;
  int indY = y + 40;
  int boxSize = 30;

  M5.Display.drawRect(indX, indY, boxSize, boxSize, COLOR_BLACK);
  if (active) {
    // Fill the whole box for "Black when On"
    M5.Display.fillRect(indX, indY, boxSize, boxSize, COLOR_BLACK);
  } else {
    // Empty when off (already drawn rect)
  }
}

void UIManager::drawButton(int x, int y, int w, int h, const char *label,
                           bool selected) {
  M5.Display.setTextSize(1);
  if (selected) {
    M5.Display.fillRect(x, y, w, h, COLOR_BLACK);
    M5.Display.setTextColor(COLOR_WHITE);
  } else {
    M5.Display.drawRect(x, y, w, h, COLOR_BLACK);
    M5.Display.setTextColor(COLOR_BLACK);
  }

  // Center text
  int textLen = M5.Display.textWidth(label);
  M5.Display.setCursor(x + (w - textLen) / 2,
                       y + (h - 8) / 2); // 8 is approx height for Size 1
  M5.Display.print(label);
}

// Global reference to BLE client
extern FossibotBLE *bleClient;

void UIManager::handleHomeTouch(int x, int y, TouchEvent event) {
  if (event != TouchEvent::PRESS && event != TouchEvent::RELEASE)
    return;

  extern Config *config;
  String theme = config ? config->getTheme() : "classic_grid";
  if (theme == "compact_status") {
    handleHomeCompactStatusTouch(x, y);
  } else if (theme == "horizontal_bars") {
    handleHomeHorizontalBarsTouch(x, y);
  } else if (theme == "sector") {
    handleHomeSectorTouch(x, y);
  } else {
    handleHomeClassicGridTouch(x, y);
  }
}

void UIManager::handleHomeClassicGridTouch(int x, int y) {
  int contentY = BATTERY_BAR_HEIGHT + PANEL_MARGIN;
  int contentHeight =
      SCREEN_HEIGHT - BATTERY_BAR_HEIGHT - MENU_BAR_HEIGHT - PANEL_MARGIN * 3;
  int panelWidth = (SCREEN_WIDTH - PANEL_MARGIN * 3) / 2;
  int panelHeight = (contentHeight - PANEL_MARGIN) / 2;

  // Status Panel (top-right)
  int statusX = PANEL_MARGIN * 2 + panelWidth;
  int statusY = contentY;

  if (x >= statusX && x < statusX + panelWidth && y >= statusY &&
      y < statusY + panelHeight) {
    int toggleSpacing = (panelWidth - 40) / 3;
    int startY = statusY + (panelHeight - 50) / 2;
    int buttonY = startY + 15;

    int usbCenter = statusX + 20 + (toggleSpacing - 70) / 2 + 35;
    int dcCenter = statusX + 20 + toggleSpacing + (toggleSpacing - 70) / 2 + 35;
    int acCenter =
        statusX + 20 + toggleSpacing * 2 + (toggleSpacing - 70) / 2 + 35;

    if (y >= buttonY - 30 && y <= buttonY + 80) {
      if (abs(x - usbCenter) < 50) {
        if (bleClient && bleClient->isConnected()) {
          Serial.println("Toggle USB");
          bleClient->toggleUSB();
          Buzzer::click();
          _powerData.usbActive = !_powerData.usbActive;
          _needsRefresh = true;
          _lastRefresh = 0;
        }
      } else if (abs(x - dcCenter) < 50) {
        if (bleClient && bleClient->isConnected()) {
          Serial.println("Toggle DC");
          bleClient->toggleDC();
          Buzzer::click();
          _powerData.dcActive = !_powerData.dcActive;
          _needsRefresh = true;
          _lastRefresh = 0;
        }
      } else if (abs(x - acCenter) < 50) {
        if (bleClient && bleClient->isConnected()) {
          Serial.println("Toggle AC");
          bleClient->toggleAC();
          Buzzer::click();
          _powerData.acActive = !_powerData.acActive;
          _needsRefresh = true;
          _lastRefresh = 0;
        }
      }
    }
  }

  // Clock/Weather Panel is Bottom-Right (HISTORY button)
  int clockX = PANEL_MARGIN * 2 + panelWidth;
  int clockY = contentY + panelHeight + PANEL_MARGIN;
  int histBtnX = clockX + 10;
  int histBtnY = clockY + panelHeight - 45;
  if (x >= histBtnX && x < histBtnX + 90 && y >= histBtnY &&
      y < histBtnY + 35) {
    Serial.println("UI: HISTORY button pressed!");
    Buzzer::click();
    navigateTo(ScreenID::HISTORY);
    return;
  }
}

void UIManager::handleHomeCompactStatusTouch(int x, int y) {
  // Toggle strip is at bottom, just above menu bar
  int contentBottom = SCREEN_HEIGHT - MENU_BAR_HEIGHT;
  int toggleStripH = 60;
  int toggleY = contentBottom - toggleStripH;

  if (y >= toggleY && y < contentBottom) {
    int toggleSpacing = SCREEN_WIDTH / 4;
    int usbCx = toggleSpacing;
    int dcCx = toggleSpacing * 2;
    int acCx = toggleSpacing * 3;

    if (abs(x - usbCx) < 60) {
      if (bleClient && bleClient->isConnected()) {
        Serial.println("Toggle USB");
        bleClient->toggleUSB();
        Buzzer::click();
        _powerData.usbActive = !_powerData.usbActive;
        _needsRefresh = true;
        _lastRefresh = 0;
      }
    } else if (abs(x - dcCx) < 60) {
      if (bleClient && bleClient->isConnected()) {
        Serial.println("Toggle DC");
        bleClient->toggleDC();
        Buzzer::click();
        _powerData.dcActive = !_powerData.dcActive;
        _needsRefresh = true;
        _lastRefresh = 0;
      }
    } else if (abs(x - acCx) < 60) {
      if (bleClient && bleClient->isConnected()) {
        Serial.println("Toggle AC");
        bleClient->toggleAC();
        Buzzer::click();
        _powerData.acActive = !_powerData.acActive;
        _needsRefresh = true;
        _lastRefresh = 0;
      }
    }
  }
}

void UIManager::handleHomeHorizontalBarsTouch(int x, int y) {
  // Row 4 toggles: match positions from drawHomeHorizontalBars
  int margin = 10;
  int gap = 5;
  int battRowH = 55;
  int powerRowH = 105;
  int contentBottom = SCREEN_HEIGHT - MENU_BAR_HEIGHT;
  int row4Y = margin + battRowH + gap + powerRowH * 2 + gap * 2;
  int row4H = contentBottom - row4Y - margin;

  if (y >= row4Y && y < row4Y + row4H) {
    int toggleBaseX = SCREEN_WIDTH / 2 + 80;
    int dcX = toggleBaseX + 130;
    int acX = dcX + 130;

    if (abs(x - toggleBaseX) < 60) {
      if (bleClient && bleClient->isConnected()) {
        Serial.println("Toggle USB");
        bleClient->toggleUSB();
        Buzzer::click();
        _powerData.usbActive = !_powerData.usbActive;
        _needsRefresh = true;
        _lastRefresh = 0;
      }
    } else if (abs(x - dcX) < 60) {
      if (bleClient && bleClient->isConnected()) {
        Serial.println("Toggle DC");
        bleClient->toggleDC();
        Buzzer::click();
        _powerData.dcActive = !_powerData.dcActive;
        _needsRefresh = true;
        _lastRefresh = 0;
      }
    } else if (abs(x - acX) < 60) {
      if (bleClient && bleClient->isConnected()) {
        Serial.println("Toggle AC");
        bleClient->toggleAC();
        Buzzer::click();
        _powerData.acActive = !_powerData.acActive;
        _needsRefresh = true;
        _lastRefresh = 0;
      }
    }
  }
}

void UIManager::handleHomeSectorTouch(int x, int y) {
  // Right-side sector panels act as toggle buttons
  int margin = 10;
  int topY = BATTERY_BAR_HEIGHT + margin;
  int contentBottom = SCREEN_HEIGHT - MENU_BAR_HEIGHT;
  int availH = contentBottom - topY - margin;
  int rightX = SCREEN_WIDTH / 2 + margin;
  int rightW = SCREEN_WIDTH / 2 - margin * 2;
  int sectorH = (availH - margin * 2) / 3;

  if (x < rightX || x > rightX + rightW) return;

  int usbY = topY;
  int acY = usbY + sectorH + margin;
  int dcY = acY + sectorH + margin;

  if (y >= usbY && y < usbY + sectorH) {
    if (bleClient && bleClient->isConnected()) {
      Serial.println("Toggle USB");
      bleClient->toggleUSB();
      Buzzer::click();
      _powerData.usbActive = !_powerData.usbActive;
      _needsRefresh = true;
      _lastRefresh = 0;
    }
  } else if (y >= acY && y < acY + sectorH) {
    if (bleClient && bleClient->isConnected()) {
      Serial.println("Toggle AC");
      bleClient->toggleAC();
      Buzzer::click();
      _powerData.acActive = !_powerData.acActive;
      _needsRefresh = true;
      _lastRefresh = 0;
    }
  } else if (y >= dcY && y < dcY + sectorH) {
    if (bleClient && bleClient->isConnected()) {
      Serial.println("Toggle DC");
      bleClient->toggleDC();
      Buzzer::click();
      _powerData.dcActive = !_powerData.dcActive;
      _needsRefresh = true;
      _lastRefresh = 0;
    }
  }
}

void UIManager::drawSettingsScreen() {
  // Main Settings Menu - tile navigation to sub-screens
  M5.Display.fillScreen(COLOR_WHITE);
  drawMenuBar();

  // Title - centered
  M5.Display.setTextSize(2); // Reduced 4 -> 2
  M5.Display.setTextColor(COLOR_BLACK);
  M5.Display.setCursor(SCREEN_WIDTH / 2 - 80, 20);
  M5.Display.print("Settings");

  // Navigation tiles (using 200x70 buttons like SD Diag, History)
  int btnW = 200;
  int btnH = 70;
  int startY = 120; // Moved down 20px (was 100)
  int spacing = 20;
  int col1X = SCREEN_WIDTH / 2 - btnW - spacing / 2;
  int col2X = SCREEN_WIDTH / 2 + spacing / 2;

  // Row 1: Device Settings | Fossibot
  drawButton(col1X, startY, btnW, btnH, "Device", true);
  drawButton(col2X, startY, btnW, btnH, "Fossibot", true);

  // Row 2: SD Diag | History
  int row2Y = startY + btnH + spacing;
  drawButton(col1X, row2Y, btnW, btnH, "SD Diag");
  drawButton(col2X, row2Y, btnW, btnH, "History");

  // Row 3: Theme | Back
  int row3Y = row2Y + btnH + spacing;
  {
    extern Config *config;
    String theme = config ? config->getTheme() : "classic_grid";
    // Display friendly theme name on button
    const char *themeName = "Classic";
    if (theme == "compact_status") themeName = "Compact";
    else if (theme == "horizontal_bars") themeName = "H-Bars";
    else if (theme == "sector") themeName = "Sector";
    char themeLabel[24];
    snprintf(themeLabel, sizeof(themeLabel), "Theme: %s", themeName);
    drawButton(col1X, row3Y, btnW, btnH, themeLabel);
  }
  drawButton(col2X, row3Y, btnW, btnH, "Back");

  // --- Battery Status (Top Right) ---
  M5.Display.setTextSize(0.7); // Reduced 1 -> 0.7 per user request
  M5.Display.setCursor(SCREEN_WIDTH - 220, 15);
  float voltage = Battery::getVoltage();
  int percentage = Battery::getPercentage();
  M5.Display.printf("Bat: %d%% (%.2fV)", percentage, voltage);
}

void UIManager::handleSettingsTouch(int x, int y) {
  // Touch handler for main settings menu
  auto isHit = [&](int bx, int by, int bw, int bh) {
    if (x >= bx && x < bx + bw && y >= by && y < by + bh) {
      Buzzer::click();
      return true;
    }
    return false;
  };

  int btnW = 200;
  int btnH = 70;
  int startY = 120; // Updated to match moved buttons (was 100)
  int spacing = 20;
  int col1X = SCREEN_WIDTH / 2 - btnW - spacing / 2;
  int col2X = SCREEN_WIDTH / 2 + spacing / 2;
  int row2Y = startY + btnH + spacing;
  int row3Y = row2Y + btnH + spacing;

  // Device Settings
  if (isHit(col1X, startY, btnW, btnH)) {
    navigateTo(ScreenID::SETTINGS_DEVICE);
    return;
  }
  // Fossibot
  if (isHit(col2X, startY, btnW, btnH)) {
    navigateTo(ScreenID::SETTINGS_FOSSIBOT);
    return;
  }
  // SD Diag
  if (isHit(col1X, row2Y, btnW, btnH)) {
    navigateTo(ScreenID::SD_DIAG);
    return;
  }
  // History
  if (isHit(col2X, row2Y, btnW, btnH)) {
    navigateTo(ScreenID::HISTORY);
    return;
  }
  // Theme (cycle)
  if (isHit(col1X, row3Y, btnW, btnH)) {
    extern Config *config;
    if (config) {
      String theme = config->getTheme();
      if (theme == "classic_grid") {
        config->setTheme("compact_status");
      } else if (theme == "compact_status") {
        config->setTheme("horizontal_bars");
      } else if (theme == "horizontal_bars") {
        config->setTheme("sector");
      } else {
        config->setTheme("classic_grid");
      }
      config->save("/config/settings.json");
      Serial.printf("UI: Theme changed to %s\n", config->getTheme().c_str());
    }
    _needsRefresh = true;
    _lastRefresh = 0;
    return;
  }
  // Back
  if (isHit(col2X, row3Y, btnW, btnH)) {
    navigateTo(ScreenID::HOME);
    return;
  }
}

// ============================================================================
// Device Settings Screen (Date/Time/Refresh/Sleep)
// ============================================================================

void UIManager::drawDeviceSettingsScreen() {
  // Ultra-fast refresh for responsive button feedback (like Timer/Notes
  // screens)
  M5.Display.setEpdMode(epd_mode_t::epd_fastest);
  M5.Display.fillScreen(COLOR_WHITE);
  drawMenuBar();

  // Title - centered
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(COLOR_BLACK);
  String title = "Device Settings";
  M5.Display.setCursor((SCREEN_WIDTH - M5.Display.textWidth(title)) / 2, 20);
  M5.Display.print(title);

  // --- Date ---
  int y = 80;

  M5.Display.setTextSize(2);
  M5.Display.setCursor(20, y + 15);
  M5.Display.print("Date:");

  // Year
  drawButton(160, y, 90, 60, "-");
  M5.Display.setCursor(260, y + 15);
  M5.Display.printf("%04d", _editYear);
  drawButton(350, y, 90, 60, "+");

  // Month
  drawButton(460, y, 60, 60, "-");
  M5.Display.setCursor(530, y + 15);
  M5.Display.printf("%02d", _editMonth);
  drawButton(590, y, 60, 60, "+");

  // Day
  drawButton(670, y, 60, 60, "-");
  M5.Display.setCursor(750, y + 15);
  M5.Display.printf("%02d", _editDay);
  drawButton(810, y, 60, 60, "+");

  // --- Time ---
  y = 160;
  M5.Display.setCursor(20, y + 15);
  M5.Display.print("Time:");

  // Hour
  drawButton(160, y, 70, 60, "-");
  M5.Display.setCursor(250, y + 15);
  M5.Display.printf("%02d", _editHour);
  drawButton(330, y, 70, 60, "+");

  // Minute
  drawButton(430, y, 70, 60, "-");
  M5.Display.setCursor(520, y + 15);
  M5.Display.printf("%02d", _editMinute);
  drawButton(570, y, 70, 60, "+");

  // --- Sleep Timeout ---
  y = 240;
  M5.Display.setCursor(20, y + 15);
  M5.Display.print("Sleep:");

  drawButton(160, y, 60, 60, "-");
  M5.Display.setCursor(240, y + 15);
  if (_editAutoSleep == 0) {
    M5.Display.print("Never");
  } else {
    M5.Display.printf("%dm", _editAutoSleep);
  }
  drawButton(340, y, 60, 60, "+");

  // --- Bluetooth & Deep Sleep Test Row ---
  y = 320;

  // Bluetooth toggle
  extern FossibotBLE *bleClient;
  bool bleEnabled = (bleClient && bleClient->isAutoReconnectEnabled());
  M5.Display.setCursor(20, y + 15);
  M5.Display.print("Bluetooth:");
  drawButton(160, y, 120, 50, bleEnabled ? "ON" : "OFF", bleEnabled);

  // Test Deep Sleep button
  drawButton(350, y, 180, 50, "Test Sleep");

  // --- Actions (bottom right, above nav bar) ---
  y = 400;
  drawButton(560, y, 140, 55, "SAVE", true);
  drawButton(720, y, 140, 55, "CANCEL");
}

void UIManager::handleDeviceSettingsTouch(int x, int y) {
  auto isHit = [&](int bx, int by, int bw, int bh) {
    if (x >= bx && x < bx + bw && y >= by && y < by + bh) {
      Buzzer::click();
      return true;
    }
    return false;
  };

  int row1 = 80; // Date
  // Year
  if (isHit(160, row1, 90, 60))
    _editYear--;
  if (isHit(350, row1, 90, 60))
    _editYear++;

  // Month
  if (isHit(460, row1, 60, 60)) {
    _editMonth--;
    if (_editMonth < 1)
      _editMonth = 12;
  }
  if (isHit(590, row1, 60, 60)) {
    _editMonth++;
    if (_editMonth > 12)
      _editMonth = 1;
  }

  // Day
  if (isHit(670, row1, 60, 60)) {
    _editDay--;
    if (_editDay < 1)
      _editDay = 31;
  }
  if (isHit(810, row1, 60, 60)) {
    _editDay++;
    if (_editDay > 31)
      _editDay = 1;
  }

  int row2 = 160; // Time
  // Hour
  if (isHit(160, row2, 70, 60)) {
    _editHour--;
    if (_editHour < 0)
      _editHour = 23;
  }
  if (isHit(330, row2, 70, 60)) {
    _editHour++;
    if (_editHour > 23)
      _editHour = 0;
  }

  // Minute
  if (isHit(430, row2, 70, 60)) {
    _editMinute--;
    if (_editMinute < 0)
      _editMinute = 59;
  }
  if (isHit(570, row2, 70, 60)) {
    _editMinute++;
    if (_editMinute > 59)
      _editMinute = 0;
  }

  int row3 = 240; // Sleep
  // Sleep -10
  if (isHit(160, row3, 60, 60)) {
    _editAutoSleep -= 10;
    if (_editAutoSleep < 0)
      _editAutoSleep = 0;
  }
  // Sleep +10
  if (isHit(340, row3, 60, 60)) {
    _editAutoSleep += 10;
    if (_editAutoSleep > 120)
      _editAutoSleep = 120;
  }

  // Trigger refresh for any date/time/sleep value change above
  _needsRefresh = true;

  // --- Bluetooth & Deep Sleep Test Row ---
  int rowBT = 320;

  // Bluetooth toggle
  if (isHit(160, rowBT, 120, 50)) {
    extern FossibotBLE *bleClient;
    if (bleClient) {
      if (bleClient->isAutoReconnectEnabled()) {
        // Disable Bluetooth
        bleClient->pauseRetry();
        if (bleClient->isConnected()) {
          bleClient->disconnect();
        }
        Serial.println("Bluetooth disabled by user");
      } else {
        // Enable Bluetooth
        bleClient->resumeRetry();
        Serial.println("Bluetooth enabled by user");
      }
    }
    _needsRefresh = true;
    return;
  }

  // Test Deep Sleep
  if (isHit(350, rowBT, 180, 50)) {
    Serial.println("User initiated deep sleep test...");
    enterDeepSleep();
    return; // Won't reach here - device is asleep
  }

  int row4 = 400; // Save/Cancel (bottom right)
  // Save
  if (isHit(560, row4, 140, 55)) {
    RTC::setDateTime(_editYear, _editMonth, _editDay, _editHour, _editMinute,
                     0);
    Serial.printf("RTC Time Set: %04d-%02d-%02d %02d:%02d:00\n", _editYear,
                  _editMonth, _editDay, _editHour, _editMinute);

    extern Config *config;
    if (config) {
      config->setAutoSleepMinutes(_editAutoSleep);
      config->save("/config/settings.json");
      Serial.printf("Config Saved: Auto Sleep = %d min\n", _editAutoSleep);
    }

    Buzzer::click();
    navigateTo(ScreenID::SETTINGS);
  }

  // Cancel
  if (isHit(720, row4, 140, 55)) {
    Buzzer::click();
    navigateTo(ScreenID::SETTINGS);
  }
}

// ============================================================================
// Fossibot Settings Screen (Quick Actions, Power Limits, Timers)
// ============================================================================

void UIManager::drawFossibotSettingsScreen() {
  M5.Display.fillScreen(COLOR_WHITE);
  drawMenuBar();

  // Title
  // Title
  M5.Display.setTextSize(2); // Reverted 3 -> 2
  M5.Display.setTextColor(COLOR_BLACK);
  String title = "Fossibot Settings";
  M5.Display.setCursor((SCREEN_WIDTH - M5.Display.textWidth(title)) / 2, 15);
  M5.Display.print(title);

  // Sync values from BLE data if available
  // Suppress sync for 5 seconds after user interaction to prevent
  // overwriting user's changes with stale BLE data
  bool suppressSettingsSync = (millis() - _lastTimerSetTime < 5000);
  if (!suppressSettingsSync && bleClient && bleClient->isConnected()) {
    const auto &data = bleClient->getData();
    if (data.settingsReceived) {
      _fossiBuzzerEnabled = data.buzzerEnabled;
      _fossiSilentCharging = data.silentCharging;
      _fossiLightMode = data.lightMode;
      _fossiDischargeLimit = data.dischargeLimit;
      _fossiChargeLimit = data.chargeLimit;
      _fossiScreenTimeout = data.screenTimeout;
      _fossiSysStandby = data.sysStandby;
      _fossiACStandby = data.acStandby;
      _fossiDCStandby = data.dcStandby;
      _fossiUSBStandby = data.usbStandby;
    }
  }

  M5.Display.setTextSize(1);
  int y = 60;
  int labelX = 30;
  int toggleX = 400;
  int rowH = 45;

  // === Quick Actions Section ===
  M5.Display.setTextColor(COLOR_DARK_GRAY);
  M5.Display.setCursor(labelX, y);
  M5.Display.print("-- Quick Actions --");
  y += rowH;

  // Buzzer
  M5.Display.setTextColor(COLOR_BLACK);
  M5.Display.setCursor(labelX, y);
  M5.Display.print("Buzzer (Key Sound)");
  drawButton(toggleX, y - 10, 100, 40, _fossiBuzzerEnabled ? "ON" : "OFF",
             _fossiBuzzerEnabled);
  y += rowH;

  // Silent Charging
  M5.Display.setTextColor(COLOR_BLACK); // Ensure text is visible
  M5.Display.setCursor(labelX, y);
  M5.Display.print("Silent Charging");
  drawButton(toggleX, y - 10, 100, 40, _fossiSilentCharging ? "ON" : "OFF",
             _fossiSilentCharging);
  y += rowH;

  // LED Light
  M5.Display.setCursor(labelX, y);
  M5.Display.print("LED Light");
  const char *lightLabels[] = {"OFF", "ON", "FLASH", "SOS"};
  drawButton(toggleX, y - 10, 100, 40, lightLabels[_fossiLightMode],
             _fossiLightMode > 0);
  y += rowH + 10;

  // === Power Limits Section ===
  M5.Display.setTextColor(COLOR_DARK_GRAY);
  M5.Display.setCursor(labelX, y);
  M5.Display.print("-- Power Limits --");
  y += rowH;

  // Charge Limit
  M5.Display.setTextColor(COLOR_BLACK);
  M5.Display.setCursor(labelX, y);
  M5.Display.print("Charge Limit (EPS)");
  char limitStr[16];
  snprintf(limitStr, sizeof(limitStr), "%d%%", _fossiChargeLimit);
  drawButton(toggleX - 60, y - 10, 50, 40, "-");
  M5.Display.setCursor(toggleX + 5, y);
  M5.Display.print(limitStr);
  drawButton(toggleX + 70, y - 10, 50, 40, "+");
  y += rowH;

  // Discharge Limit
  M5.Display.setCursor(labelX, y);
  M5.Display.print("Discharge Limit");
  snprintf(limitStr, sizeof(limitStr), "%d%%", _fossiDischargeLimit);
  drawButton(toggleX - 60, y - 10, 50, 40, "-");
  M5.Display.setCursor(toggleX + 5, y);
  M5.Display.print(limitStr);
  drawButton(toggleX + 70, y - 10, 50, 40, "+");
  y += rowH + 10;

  // === Timers Section (Right Column) - Navigate to sub-screen ===
  int col2X = 550;
  int col2Y = 120;

  drawButton(col2X, col2Y, 200, 60, "TIMERS", true);

  M5.Display.setTextSize(1);
  M5.Display.setTextColor(COLOR_DARK_GRAY);
  M5.Display.setCursor(col2X + 10, col2Y + 70);
  M5.Display.print("Standby & Schedule");

  // === Power Off Button ===
  drawButton(col2X, col2Y + 120, 200, 60, "POWER OFF", true);

  // === Simulate Error Button (for testing error banner) ===
  drawButton(col2X, col2Y + 200, 200, 60,
             _powerData.simulatedError ? "CLR ERROR" : "SIM ERROR",
             _powerData.simulatedError);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(COLOR_DARK_GRAY);
  M5.Display.setCursor(col2X + 10, col2Y + 270);
  M5.Display.print("Test error banner");

  // --- Action Buttons ---
  y = 420;
  drawButton(320, y, 200, 55, "SAVE", true);
  drawButton(540, y, 200, 55, "BACK");

  // Connection status indicator
  M5.Display.setTextSize(1);
  M5.Display.setCursor(SCREEN_WIDTH - 200, 20);
  if (bleClient && bleClient->isConnected()) {
    M5.Display.setTextColor(COLOR_BLACK);
    M5.Display.print("Connected");
  } else {
    M5.Display.setTextColor(COLOR_GRAY);
    M5.Display.print("Not Connected");
  }

  // === Power Off Confirmation Dialog ===
  if (_showPowerOffConfirmation) {
    // Draw modal overlay
    M5.Display.fillRect(200, 150, 560, 240, COLOR_WHITE);
    M5.Display.drawRect(200, 150, 560, 240, COLOR_BLACK);
    M5.Display.drawRect(201, 151, 558, 238, COLOR_BLACK);

    M5.Display.setTextSize(3);
    M5.Display.setTextColor(COLOR_BLACK);
    M5.Display.setCursor(240, 180);
    M5.Display.print("Are you sure you want");
    M5.Display.setCursor(320, 220);
    M5.Display.print("to power off?");

    drawButton(280, 290, 150, 60, "YES", true);
    drawButton(530, 290, 150, 60, "NO");
  }
}

void UIManager::handleFossibotSettingsTouch(int x, int y) {
  auto isHit = [&](int bx, int by, int bw, int bh) {
    if (x >= bx && x < bx + bw && y >= by && y < by + bh) {
      Buzzer::click();
      return true;
    }
    return false;
  };

  // === Power Off Confirmation ===
  if (_showPowerOffConfirmation) {
    // YES button
    if (isHit(280, 290, 150, 60)) {
      if (bleClient && bleClient->isConnected()) {
        bleClient->powerOff();
      }
      _showPowerOffConfirmation = false;
      navigateTo(ScreenID::HOME); // Assume power off creates disconnect
      return;
    }
    // NO button
    if (isHit(530, 290, 150, 60)) {
      _showPowerOffConfirmation = false;
      forceRefresh();
      return;
    }
    return; // Block other inputs
  }

  int toggleX = 400;
  int rowH = 45;
  int baseY = 60 + rowH; // After section header

  // Buzzer toggle
  if (isHit(toggleX, baseY - 10, 100, 40)) {
    _fossiBuzzerEnabled = !_fossiBuzzerEnabled;
    _lastTimerSetTime = millis(); // Prevent BLE sync from overwriting
    if (bleClient && bleClient->isConnected()) {
      bleClient->setBuzzerEnabled(_fossiBuzzerEnabled);
    }
    forceRefresh();
    return;
  }
  baseY += rowH;

  // Silent Charging toggle
  if (isHit(toggleX, baseY - 10, 100, 40)) {
    _fossiSilentCharging = !_fossiSilentCharging;
    _lastTimerSetTime = millis(); // Prevent BLE sync from overwriting
    if (bleClient && bleClient->isConnected()) {
      bleClient->setSilentCharging(_fossiSilentCharging);
    }
    forceRefresh();
    return;
  }
  baseY += rowH;

  // LED Light cycle (0->1->2->3->0)
  if (isHit(toggleX, baseY - 10, 100, 40)) {
    _fossiLightMode = (_fossiLightMode + 1) % 4;
    _lastTimerSetTime = millis(); // Prevent BLE sync from overwriting
    if (bleClient && bleClient->isConnected()) {
      bleClient->setLightMode(_fossiLightMode);
    }
    forceRefresh();
    return;
  }
  baseY += rowH + 10 + rowH; // Skip Power Limits header

  // Charge Limit -
  if (isHit(toggleX - 60, baseY - 10, 50, 40)) {
    _fossiChargeLimit -= 10;
    if (_fossiChargeLimit < 60)
      _fossiChargeLimit = 60;
    _lastTimerSetTime = millis(); // Prevent BLE sync from overwriting
    if (bleClient && bleClient->isConnected()) {
      bleClient->setChargeLimit(_fossiChargeLimit);
    }
    forceRefresh();
    return;
  }
  // Charge Limit +
  if (isHit(toggleX + 70, baseY - 10, 50, 40)) {
    _fossiChargeLimit += 10;
    if (_fossiChargeLimit > 100)
      _fossiChargeLimit = 100;
    _lastTimerSetTime = millis(); // Prevent BLE sync from overwriting
    if (bleClient && bleClient->isConnected()) {
      bleClient->setChargeLimit(_fossiChargeLimit);
    }
    forceRefresh();
    return;
  }
  baseY += rowH;

  // Discharge Limit -
  if (isHit(toggleX - 60, baseY - 10, 50, 40)) {
    _fossiDischargeLimit -= 5;
    if (_fossiDischargeLimit < 0)
      _fossiDischargeLimit = 0;
    _lastTimerSetTime = millis(); // Prevent BLE sync from overwriting
    if (bleClient && bleClient->isConnected()) {
      bleClient->setDischargeLimit(_fossiDischargeLimit);
    }
    forceRefresh();
    return;
  }
  // Discharge Limit +
  if (isHit(toggleX + 70, baseY - 10, 50, 40)) {
    _fossiDischargeLimit += 5;
    if (_fossiDischargeLimit > 30)
      _fossiDischargeLimit = 30;
    _lastTimerSetTime = millis(); // Prevent BLE sync from overwriting
    if (bleClient && bleClient->isConnected()) {
      bleClient->setDischargeLimit(_fossiDischargeLimit);
    }
    forceRefresh();
    return;
  }

  // === TIMERS button (Right Column) - Navigate to Timers sub-screen ===
  int col2X = 550;
  int col2Y = 120;
  if (isHit(col2X, col2Y, 200, 60)) {
    navigateTo(ScreenID::SETTINGS_FOSSIBOT_TIMERS);
    return;
  }

  // === Power Off Button ===
  if (isHit(col2X, col2Y + 120, 200, 60)) {
    _showPowerOffConfirmation = true;
    forceRefresh();
    return;
  }

  // === Simulate Error Button ===
  if (isHit(col2X, col2Y + 200, 200, 60)) {
    _powerData.simulatedError = !_powerData.simulatedError;
    if (_powerData.simulatedError) {
      // Inject fake error values matching real-world fault pattern
      _powerData.errorCode = 79;
      _powerData.protectionFlags = 0xE000;
      _powerData.systemStatusFlags |= Fossibot::SystemStatusBits::ERROR_PENDING;
      Serial.println("UI: Simulated error ENABLED (Code=79, Flags=0xE000)");
    } else {
      _powerData.errorCode = 0;
      _powerData.protectionFlags = 0;
      _powerData.systemStatusFlags &=
          ~Fossibot::SystemStatusBits::ERROR_PENDING;
      Serial.println("UI: Simulated error CLEARED");
    }
    forceRefresh();
    return;
  }

  // Action buttons (Y updated to match moved buttons)
  int actionY = 420;
  // Save - just refresh to confirm
  if (isHit(320, actionY, 200, 55)) {
    // Values are sent immediately on change, just go back
    navigateTo(ScreenID::SETTINGS);
    return;
  }
  // Back
  if (isHit(540, actionY, 200, 55)) {
    navigateTo(ScreenID::SETTINGS);
    return;
  }
}

// ============================================================================
// Fossibot Timers Sub-Screen (Output standby timers + Schedule Charge)
// ============================================================================

void UIManager::drawFossibotTimersScreen() {
  M5.Display.fillScreen(COLOR_WHITE);
  drawMenuBar();

  // === Header ===
  M5.Display.setTextColor(COLOR_BLACK);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(SCREEN_WIDTH / 2 - 120, 15);
  M5.Display.print("Fossibot Timers");

  M5.Display.setTextSize(1);
  int y = 65;
  int labelX = 20;
  int btnStartX = 280;
  int btnW = 60;
  int btnH = 35;
  int btnGap = 10;
  int rowGap = 45;

  // --- PRESETS DEFINITIONS ---
  const int acPresets[] = {60, 480, 960, 1440, 0}; // Minutes
  const char *acLabels[] = {"1h", "8h", "16h", "24h", "OFF"};

  const int usbPresets[] = {180, 300, 600, 1800, 0}; // Seconds
  const char *usbLabels[] = {"3m", "5m", "10m", "30m", "OFF"};

  const int screenPresets[] = {3, 5, 10, 30, 0}; // Minutes
  const char *screenLabels[] = {"3m", "5m", "10m", "30m", "OFF"};

  const int sysPresets[] = {60, 480, 1440, 0}; // Minutes
  const char *sysLabels[] = {"1h", "8h", "24h", "OFF"};

  // === AC Standby (Minutes) ===
  M5.Display.setTextColor(COLOR_BLACK);
  M5.Display.setCursor(labelX, y + 8);
  M5.Display.print("AC Standby:");
  for (int i = 0; i < 5; i++) {
    bool active = (_fossiACStandby == acPresets[i]);
    drawButton(btnStartX + i * (btnW + btnGap), y, btnW, btnH, acLabels[i],
               active);
  }
  y += rowGap;

  // === DC Standby (Size 1 to match AC) ===
  M5.Display.setTextSize(1); // Fixed: was Size 2
  M5.Display.setCursor(labelX, y + 8);
  M5.Display.print("DC Standby:");
  for (int i = 0; i < 5; i++) {
    bool active = (_fossiDCStandby == acPresets[i]);
    drawButton(btnStartX + i * (btnW + btnGap), y, btnW, btnH, acLabels[i],
               active);
  }
  y += rowGap;

  // === USB Standby (Ensure consistent drawing) ===
  M5.Display.setTextSize(1);
  M5.Display.setCursor(labelX, y + 8);
  M5.Display.setTextColor(COLOR_BLACK); // Reinforce color
  M5.Display.print("USB Standby:");
  for (int i = 0; i < 5; i++) {
    bool active = (_fossiUSBStandby == usbPresets[i]);
    drawButton(btnStartX + i * (btnW + btnGap), y, btnW, btnH, usbLabels[i],
               active);
  }
  y += rowGap;

  // === Screen Timeout (Minutes) ===
  M5.Display.setCursor(labelX, y + 8);
  M5.Display.print("Screen Off:");
  for (int i = 0; i < 5; i++) {
    bool active = (_fossiScreenTimeout == screenPresets[i]);
    drawButton(btnStartX + i * (btnW + btnGap), y, btnW, btnH, screenLabels[i],
               active);
  }
  y += rowGap;

  // === Sys Standby (Minutes) ===
  M5.Display.setCursor(labelX, y + 8);
  M5.Display.print("Sys Idle:");
  for (int i = 0; i < 4; i++) {
    bool active = (_fossiSysStandby == sysPresets[i]);
    drawButton(btnStartX + i * (btnW + btnGap), y, btnW, btnH, sysLabels[i],
               active);
  }
  y += rowGap + 10;

  // === Schedule Charge ===
  M5.Display.setTextColor(COLOR_DARK_GRAY);
  M5.Display.setCursor(labelX, y);
  M5.Display.print("-- Schedule Charge --");

  // Show minutes until charge
  if (_fossiScheduleChargeRemaining > 0) {
    char timeStr[32];
    snprintf(timeStr, sizeof(timeStr), "(in %d min)",
             _fossiScheduleChargeRemaining);
    M5.Display.setCursor(labelX + 220, y);
    M5.Display.setTextColor(COLOR_BLACK);
    M5.Display.print(timeStr);
  }
  y += 35;

  M5.Display.setTextColor(COLOR_BLACK);
  M5.Display.setCursor(labelX, y + 8);
  M5.Display.print("Start at:");

  // Time picker: Hour and Minute
  int timeX = 180;
  drawButton(timeX, y, 40, btnH, "-");
  char hourStr[8];
  snprintf(hourStr, sizeof(hourStr), "%02d",
           _fossiScheduleChargeHour >= 0 ? _fossiScheduleChargeHour : 0);
  M5.Display.setCursor(timeX + 50, y + 8);
  M5.Display.print(hourStr);
  drawButton(timeX + 90, y, 40, btnH, "+");

  M5.Display.setCursor(timeX + 140, y + 8);
  M5.Display.print(":");

  drawButton(timeX + 160, y, 40, btnH, "-");
  char minStr[8];
  snprintf(minStr, sizeof(minStr), "%02d", _fossiScheduleChargeMin);
  M5.Display.setCursor(timeX + 210, y + 8);
  M5.Display.print(minStr);
  drawButton(timeX + 250, y, 40, btnH, "+");

  // Set button
  if (_fossiScheduleChargeHour >= 0) {
    drawButton(timeX + 320, y, 80, btnH, "SET", true);
    drawButton(timeX + 410, y, 80, btnH, "CLEAR");
  } else {
    M5.Display.setCursor(timeX + 320, y + 8);
    M5.Display.setTextColor(COLOR_GRAY);
    M5.Display.print("(disabled)");
  }

  // --- Action Buttons ---
  y = 420;
  drawButton(SCREEN_WIDTH / 2 - 100, y, 200, 55, "BACK");

  // Connection status
  M5.Display.setTextSize(1);
  M5.Display.setCursor(SCREEN_WIDTH - 200, 20);
  if (bleClient && bleClient->isConnected()) {
    M5.Display.setTextColor(COLOR_BLACK);
    M5.Display.print("Connected");
  } else {
    M5.Display.setTextColor(COLOR_GRAY);
    M5.Display.print("Not Connected");
  }
}

void UIManager::handleFossibotTimersTouch(int x, int y) {
  auto isHit = [&](int bx, int by, int bw, int bh) {
    if (x >= bx && x < bx + bw && y >= by && y < by + bh) {
      Buzzer::click();
      _lastTimerSetTime = millis(); // Track interaction time
      return true;
    }
    return false;
  };

  int btnStartX = 280;
  int btnW = 60;
  int btnH = 35;
  int btnGap = 10;
  int baseY = 65;
  int rowGap = 45;

  // Same presets as draw
  const int acPresets[] = {60, 480, 960, 1440, 0};   // Minutes
  const int usbPresets[] = {180, 300, 600, 1800, 0}; // Seconds
  const int screenPresets[] = {3, 5, 10, 30, 0};     // Minutes
  const int sysPresets[] = {60, 480, 1440, 0};       // Minutes

  // === AC Standby presets ===
  for (int i = 0; i < 5; i++) {
    if (isHit(btnStartX + i * (btnW + btnGap), baseY, btnW, btnH)) {
      _fossiACStandby = acPresets[i];
      if (bleClient && bleClient->isConnected()) {
        bleClient->setACStandby(_fossiACStandby);
      }
      forceRefresh();
      return;
    }
  }
  baseY += rowGap;

  // === DC Standby presets ===
  for (int i = 0; i < 5; i++) {
    if (isHit(btnStartX + i * (btnW + btnGap), baseY, btnW, btnH)) {
      _fossiDCStandby = acPresets[i];
      if (bleClient && bleClient->isConnected()) {
        bleClient->setDCStandby(_fossiDCStandby);
      }
      forceRefresh();
      return;
    }
  }
  baseY += rowGap;

  // === USB Standby presets ===
  for (int i = 0; i < 5; i++) {
    if (isHit(btnStartX + i * (btnW + btnGap), baseY, btnW, btnH)) {
      _fossiUSBStandby = usbPresets[i];
      if (bleClient && bleClient->isConnected()) {
        bleClient->setUSBStandby(_fossiUSBStandby);
      }
      forceRefresh();
      return;
    }
  }
  baseY += rowGap;

  // === Screen Timeout presets ===
  for (int i = 0; i < 5; i++) {
    if (isHit(btnStartX + i * (btnW + btnGap), baseY, btnW, btnH)) {
      _fossiScreenTimeout = screenPresets[i];
      if (bleClient && bleClient->isConnected()) {
        bleClient->setScreenTimeout(_fossiScreenTimeout);
      }
      forceRefresh();
      return;
    }
  }
  baseY += rowGap;

  // === Sys Standby presets (4 items) ===
  for (int i = 0; i < 4; i++) {
    if (isHit(btnStartX + i * (btnW + btnGap), baseY, btnW, btnH)) {
      _fossiSysStandby = sysPresets[i];
      if (bleClient && bleClient->isConnected()) {
        bleClient->setSysStandby(_fossiSysStandby);
      }
      forceRefresh();
      return;
    }
  }
  baseY += rowGap + 10 + 35; // Skip extra gap + header

  // === Schedule Charge time picker ===
  int timeX = 180;

  // Hour -
  if (isHit(timeX, baseY, 40, btnH)) {
    if (_fossiScheduleChargeHour < 0)
      _fossiScheduleChargeHour = 23;
    else {
      _fossiScheduleChargeHour--;
      if (_fossiScheduleChargeHour < 0)
        _fossiScheduleChargeHour = 23;
    }
    forceRefresh();
    return;
  }
  // Hour +
  if (isHit(timeX + 90, baseY, 40, btnH)) {
    if (_fossiScheduleChargeHour < 0)
      _fossiScheduleChargeHour = 0;
    else {
      _fossiScheduleChargeHour++;
      if (_fossiScheduleChargeHour > 23)
        _fossiScheduleChargeHour = 0;
    }
    forceRefresh();
    return;
  }
  // Minute -
  if (isHit(timeX + 160, baseY, 40, btnH)) {
    _fossiScheduleChargeMin -= 15;
    if (_fossiScheduleChargeMin < 0)
      _fossiScheduleChargeMin = 45;
    forceRefresh();
    return;
  }
  // Minute +
  if (isHit(timeX + 250, baseY, 40, btnH)) {
    _fossiScheduleChargeMin += 15;
    if (_fossiScheduleChargeMin >= 60)
      _fossiScheduleChargeMin = 0;
    forceRefresh();
    return;
  }

  // SET button - calculates minutes from now and sends to device
  if (_fossiScheduleChargeHour >= 0 && isHit(timeX + 320, baseY, 80, btnH)) {
    // Get current time from RTC
    int currentHour, currentMin, currentSec;
    RTC::getTime(currentHour, currentMin, currentSec);

    // Calculate target time in minutes from midnight
    int targetMins = _fossiScheduleChargeHour * 60 + _fossiScheduleChargeMin;
    int currentMins = currentHour * 60 + currentMin;

    // Calculate minutes until target (handles next day wrap)
    int minsUntil = targetMins - currentMins;
    if (minsUntil <= 0) {
      minsUntil += 24 * 60; // Add 24 hours if target is tomorrow
    }

    Serial.printf("Schedule Charge: Target %02d:%02d, Current %02d:%02d, "
                  "Minutes until: %d\n",
                  _fossiScheduleChargeHour, _fossiScheduleChargeMin,
                  currentHour, currentMin, minsUntil);

    if (bleClient && bleClient->isConnected()) {
      bleClient->setScheduleCharge(minsUntil);
    }
    _fossiScheduleChargeRemaining = minsUntil; // Update immediately
    forceRefresh();
    return;
  }

  // CLEAR button
  if (_fossiScheduleChargeHour >= 0 && isHit(timeX + 410, baseY, 80, btnH)) {
    _fossiScheduleChargeHour = -1;
    _fossiScheduleChargeMin = 0;
    _fossiScheduleChargeRemaining = 0;
    if (bleClient && bleClient->isConnected()) {
      bleClient->setScheduleCharge(0); // 0 = disable
    }
    forceRefresh();
    return;
  }

  // === Back button ===
  int actionY = 420;
  if (isHit(SCREEN_WIDTH / 2 - 100, actionY, 200, 55)) {
    navigateTo(ScreenID::SETTINGS_FOSSIBOT);
    return;
  }
}

// ============================================================================
// Clock Screen (Side-Dock Layout) - Clock/Alarm/Pomodoro Hub
// ============================================================================

void UIManager::drawClockScreen() {
  Serial.println("UI: Drawing Clock screen");

  // Layout constants
  const int SIDEBAR_WIDTH = 160;
  const int CONTENT_X = SIDEBAR_WIDTH;
  const int CONTENT_WIDTH = SCREEN_WIDTH - SIDEBAR_WIDTH;
  const int CONTENT_HEIGHT = SCREEN_HEIGHT - MENU_BAR_HEIGHT;

  // Check if this is a timer auto-refresh (partial update only)
  bool pomodoroRunning = (_clockMode == ClockMode::POMODORO &&
                          _pomodoroState == PomodoroState::RUNNING);
  bool timerRunning = (_clockMode == ClockMode::TIMER && _timerRunning);
  bool isTimerAutoRefresh =
      (_lastRefresh != 0) && (pomodoroRunning || timerRunning);

  if (isTimerAutoRefresh) {
    // PARTIAL REFRESH: Only update content area with fast mode (no blink)
    M5.Display.setEpdMode(epd_mode_t::epd_fastest);
    // Clear only the content area
    M5.Display.fillRect(CONTENT_X, 0, CONTENT_WIDTH, CONTENT_HEIGHT,
                        COLOR_WHITE);
  } else {
    // FULL REFRESH: Redraw everything
    if (_lastRefresh == 0) {
      M5.Display.setEpdMode(epd_mode_t::epd_quality);
    } else {
      M5.Display.setEpdMode(epd_mode_t::epd_fastest);
    }
    M5.Display.fillScreen(COLOR_WHITE);
    drawMenuBar();
    drawClockSidebar(0, 0, SIDEBAR_WIDTH, CONTENT_HEIGHT);
    // Draw Exit/Back button (Top Right)
    drawButton(SCREEN_WIDTH - 80, 10, 70, 50, "X");
  }

  // Draw content based on current clock mode
  switch (_clockMode) {
  case ClockMode::POMODORO:
    drawPomodoroContent(CONTENT_X, 0, CONTENT_WIDTH, CONTENT_HEIGHT);
    break;
  case ClockMode::ALARM:
    drawAlarmContent(CONTENT_X, 0, CONTENT_WIDTH, CONTENT_HEIGHT);
    break;
  case ClockMode::TIMER:
    drawTimerContent(CONTENT_X, 0, CONTENT_WIDTH, CONTENT_HEIGHT);
    break;
  case ClockMode::CLOCK:
  default:
    // Placeholder for Clock mode
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(COLOR_DARK_GRAY);
    M5.Display.setCursor(CONTENT_X + 100, 200);
    M5.Display.print("Clock Coming Soon...");
    break;
  }
}

void UIManager::drawClockSidebar(int x, int y, int w, int h) {
  // Sidebar background
  M5.Display.fillRect(x, y, w, h, COLOR_LIGHT_GRAY);
  M5.Display.drawLine(x + w - 1, y, x + w - 1, y + h, COLOR_BLACK);

  // Title
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(COLOR_BLACK);
  M5.Display.setCursor(x + 20, y + 15);
  M5.Display.print("MODES");

  // Sidebar buttons parameters
  int btnY = y + 60;
  int btnH = 70;
  int btnSpacing = 10;

  // Timer button (Top)
  bool timerActive = (_clockMode == ClockMode::TIMER);
  M5.Display.fillRect(x + 10, btnY, w - 20, btnH,
                      timerActive ? COLOR_BLACK : COLOR_WHITE);
  M5.Display.drawRect(x + 10, btnY, w - 20, btnH, COLOR_BLACK);
  M5.Display.setTextColor(timerActive ? COLOR_WHITE : COLOR_BLACK);
  M5.Display.setCursor(x + 30, btnY + 25);
  M5.Display.print("Timer");

  // Pomodoro button (Second)
  btnY += btnH + btnSpacing;
  bool pomodoroActive = (_clockMode == ClockMode::POMODORO);
  M5.Display.fillRect(x + 10, btnY, w - 20, btnH,
                      pomodoroActive ? COLOR_BLACK : COLOR_WHITE);
  M5.Display.drawRect(x + 10, btnY, w - 20, btnH, COLOR_BLACK);
  M5.Display.setTextColor(pomodoroActive ? COLOR_WHITE : COLOR_BLACK);
  M5.Display.setCursor(x + 20, btnY + 25);
  M5.Display.print("Pomodoro");
}

void UIManager::drawPomodoroContent(int x, int y, int w, int h) {
  // Session type label
  M5.Display.setTextSize(2);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(COLOR_BLACK);
  const char *sessionLabel = "WORK";
  if (_pomodoroSession == PomodoroSession::SHORT_BREAK)
    sessionLabel = "SHORT BREAK";
  else if (_pomodoroSession == PomodoroSession::LONG_BREAK)
    sessionLabel = "LONG BREAK";

  int labelWidth = M5.Display.textWidth(sessionLabel);
  M5.Display.setCursor(x + (w - labelWidth) / 2, y + 40);
  M5.Display.print(sessionLabel);

  // Large timer display (MM:SS)
  int minutes = _pomodoroRemainingSeconds / 60;
  int seconds = _pomodoroRemainingSeconds % 60;
  char timerStr[10];
  snprintf(timerStr, sizeof(timerStr), "%02d:%02d", minutes, seconds);

  M5.Display.setTextSize(3); // Reduced from 6 -> 4 -> 3
  int timerWidth = M5.Display.textWidth(timerStr);
  M5.Display.setCursor(x + (w - timerWidth) / 2, y + 100);
  M5.Display.print(timerStr);

  // State indicator
  M5.Display.setTextSize(1);
  const char *stateLabel = "";
  switch (_pomodoroState) {
  case PomodoroState::STOPPED:
    stateLabel = "Ready";
    break;
  case PomodoroState::RUNNING:
    stateLabel = "Running";
    break;
  case PomodoroState::PAUSED:
    stateLabel = "Paused";
    break;
  case PomodoroState::COMPLETED:
    stateLabel = "COMPLETE!";
    break;
  }
  M5.Display.setCursor(x + (w - M5.Display.textWidth(stateLabel)) / 2, y + 200);
  M5.Display.print(stateLabel);

  // Control buttons: Start/Pause, Reset
  int btnW = 150;
  int btnH = 60;
  int btnY = y + 250;
  int btnSpacing = 40;

  // Start/Pause button
  const char *startLabel =
      (_pomodoroState == PomodoroState::RUNNING) ? "PAUSE" : "START";
  drawButton(x + (w / 2) - btnW - (btnSpacing / 2), btnY, btnW, btnH,
             startLabel, true);

  // Reset button
  drawButton(x + (w / 2) + (btnSpacing / 2), btnY, btnW, btnH, "RESET", false);

  // Session type selection buttons
  int modeY = y + 350;
  int modeBtnW = 100;
  int modeSpacing = 20;
  int modeStartX = x + (w - (3 * modeBtnW + 2 * modeSpacing)) / 2;

  drawButton(modeStartX, modeY, modeBtnW, 50, "WORK",
             _pomodoroSession == PomodoroSession::WORK);
  drawButton(modeStartX + modeBtnW + modeSpacing, modeY, modeBtnW, 50, "SHORT",
             _pomodoroSession == PomodoroSession::SHORT_BREAK);
  drawButton(modeStartX + 2 * (modeBtnW + modeSpacing), modeY, modeBtnW, 50,
             "LONG", _pomodoroSession == PomodoroSession::LONG_BREAK);
}

void UIManager::updatePomodoro() {
  checkAlarm(); // Check Alarm and Timer status

  if (_clockMode == ClockMode::TIMER && _timerRunning) {
    if (millis() - _timerLastTick >= 1000) {
      _timerLastTick = millis();
      if (_timerRemainingSeconds > 0) {
        _timerRemainingSeconds--;
        _needsRefresh = true;
      } else {
        _timerRunning = false;
        _timerRinging = true;
        _timerRingStart = millis();
        Buzzer::click(); // Initial beep
        _needsRefresh = true;
      }
    }
  }

  if (_pomodoroState != PomodoroState::RUNNING)
    return;

  unsigned long now = millis();
  unsigned long elapsed = now - _pomodoroLastTick;

  // Update every second
  if (elapsed >= 1000) {
    int secondsToSubtract = elapsed / 1000;
    _pomodoroLastTick += secondsToSubtract * 1000;

    if (_pomodoroRemainingSeconds > secondsToSubtract) {
      _pomodoroRemainingSeconds -= secondsToSubtract;

      // Refresh every second so user sees the timer counting down
      _needsRefresh = true;
    } else {
      // Timer completed!
      _pomodoroRemainingSeconds = 0;
      _pomodoroState = PomodoroState::COMPLETED;
      Buzzer::alarm(); // Play completion alarm
      _needsRefresh = true;
    }
  }
}

// ============================================================================
// Calculator Screen (Variation A: Balanced Split)
// ============================================================================

void UIManager::drawCalculatorScreen() {
  M5.Display.fillScreen(COLOR_WHITE);
  drawMenuBar();

  // Layout: Left (Display) | Right (Keypad)
  const int DISPLAY_WIDTH = 480;
  const int KEYPAD_X = DISPLAY_WIDTH;
  const int KEYPAD_WIDTH = SCREEN_WIDTH - DISPLAY_WIDTH;
  const int CONTENT_HEIGHT = SCREEN_HEIGHT - MENU_BAR_HEIGHT;

  // Exit button "X" (Top Left)
  drawButton(20, 10, 60, 50, "X");

  // --- Left Side: Display ---
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(COLOR_BLACK);
  M5.Display.setCursor(100, 25); // Shifted right to avoid X button
  M5.Display.print("Expression:");

  // Expression box
  M5.Display.drawRect(20, 60, DISPLAY_WIDTH - 40, 80, COLOR_BLACK);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(30, 85);
  M5.Display.print(_calcExpression);

  // Result box
  M5.Display.setTextSize(1);
  M5.Display.setCursor(20, 160);
  M5.Display.print("Result:");
  M5.Display.drawRect(20, 190, DISPLAY_WIDTH - 40, 100, COLOR_BLACK);
  M5.Display.setTextSize(3);
  char resultStr[32];
  if (_calcResult == (int)_calcResult) {
    snprintf(resultStr, sizeof(resultStr), "%d", (int)_calcResult);
  } else {
    snprintf(resultStr, sizeof(resultStr), "%.4f", _calcResult);
  }
  int resultWidth = strlen(resultStr) * 30;
  M5.Display.setCursor(DISPLAY_WIDTH - 50 - resultWidth, 220);
  M5.Display.print(resultStr);

  // C button (Medium Gray)
  M5.Display.fillRect(30, 320, 100, 70, COLOR_GRAY);
  M5.Display.drawRect(30, 320, 100, 70, COLOR_BLACK);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(COLOR_WHITE);
  M5.Display.setCursor(60, 340);
  M5.Display.print("C");

  // Backspace button
  M5.Display.setTextColor(COLOR_BLACK);
  drawButton(150, 320, 100, 70, "<-");

  // --- Right Side: Keypad ---
  const int BTN_W = 100;
  const int BTN_H = 85;
  const int BTN_GAP = 10;
  const int START_X = KEYPAD_X + 20;
  const int START_Y = 20;

  // Button labels in 4x4 grid
  const char *btnLabels[4][4] = {{"7", "8", "9", "/"},
                                 {"4", "5", "6", "*"},
                                 {"1", "2", "3", "-"},
                                 {"0", ".", "+", "="}};

  for (int row = 0; row < 4; row++) {
    for (int col = 0; col < 4; col++) {
      int bx = START_X + col * (BTN_W + BTN_GAP);
      int by = START_Y + row * (BTN_H + BTN_GAP);
      const char *label = btnLabels[row][col];

      // Determine button style
      bool isOperator = (col == 3 && row < 3); // /, *, -
      bool isEquals = (strcmp(label, "=") == 0);

      if (isEquals) {
        // Dark gray for =
        M5.Display.fillRect(bx, by, BTN_W, BTN_H, COLOR_DARK_GRAY);
        M5.Display.drawRect(bx, by, BTN_W, BTN_H, COLOR_BLACK);
        M5.Display.setTextColor(COLOR_WHITE);
      } else if (isOperator || strcmp(label, "+") == 0) {
        // Light gray for operators
        M5.Display.fillRect(bx, by, BTN_W, BTN_H, COLOR_LIGHT_GRAY);
        M5.Display.drawRect(bx, by, BTN_W, BTN_H, COLOR_BLACK);
        M5.Display.setTextColor(COLOR_BLACK);
      } else {
        // White for numbers
        M5.Display.fillRect(bx, by, BTN_W, BTN_H, COLOR_WHITE);
        M5.Display.drawRect(bx, by, BTN_W, BTN_H, COLOR_BLACK);
        M5.Display.setTextColor(COLOR_BLACK);
      }

      M5.Display.setTextSize(3);
      int textW = M5.Display.textWidth(label);
      // Adjust vertical center: (BTN_H - textH)/2. Approx textH for Size 3 is
      // ~24px User says "too low", so we move it UP by decreasing Y
      M5.Display.setCursor(bx + (BTN_W - textW) / 2, by + (BTN_H - 24) / 2 - 6);
      M5.Display.print(label);
    }
  }
}

void UIManager::handleCalculatorTouch(int x, int y) {
  // Exit button "X" (Top Left)
  if (x >= 20 && x < 80 && y >= 10 && y < 60) {
    Buzzer::click();
    navigateTo(ScreenID::HOME);
    return;
  }

  // C button
  if (x >= 30 && x < 130 && y >= 320 && y < 390) {
    Buzzer::click();
    calcClear();
    _needsRefresh = true;
    _lastRefresh = 0;
    return;
  }

  // Backspace button
  if (x >= 150 && x < 250 && y >= 320 && y < 390) {
    Buzzer::click();
    calcBackspace();
    _needsRefresh = true;
    _lastRefresh = 0;
    return;
  }

  // Keypad
  const int KEYPAD_X = 480;
  const int BTN_W = 100;
  const int BTN_H = 85;
  const int BTN_GAP = 10;
  const int START_X = KEYPAD_X + 20;
  const int START_Y = 20;

  const char *btnLabels[4][4] = {{"7", "8", "9", "/"},
                                 {"4", "5", "6", "*"},
                                 {"1", "2", "3", "-"},
                                 {"0", ".", "+", "="}};

  for (int row = 0; row < 4; row++) {
    for (int col = 0; col < 4; col++) {
      int bx = START_X + col * (BTN_W + BTN_GAP);
      int by = START_Y + row * (BTN_H + BTN_GAP);

      if (x >= bx && x < bx + BTN_W && y >= by && y < by + BTN_H) {
        Buzzer::click();
        const char *label = btnLabels[row][col];

        if (label[0] >= '0' && label[0] <= '9') {
          calcAppendDigit(label[0]);
        } else if (label[0] == '.') {
          calcAppendDigit('.');
        } else if (label[0] == '=') {
          calcCalculate();
        } else {
          calcSetOperator(label[0]);
        }

        _needsRefresh = true;
        _lastRefresh = 0;
        return;
      }
    }
  }
}

void UIManager::calcAppendDigit(char digit) {
  if (_calcNewInput) {
    _calcExpression[0] = '\0';
    _calcNewInput = false;
  }
  int len = strlen(_calcExpression);
  if (len < 60) {
    _calcExpression[len] = digit;
    _calcExpression[len + 1] = '\0';
  }
}

void UIManager::calcSetOperator(char op) {
  _calcOperand1 = atof(_calcExpression);
  _calcOperator = op;
  int len = strlen(_calcExpression);
  if (len < 60) {
    _calcExpression[len] = ' ';
    _calcExpression[len + 1] = op;
    _calcExpression[len + 2] = ' ';
    _calcExpression[len + 3] = '\0';
  }
  _calcNewInput = false;
}

void UIManager::calcCalculate() {
  // Find the last operator
  char *lastOp = nullptr;
  for (char *p = _calcExpression; *p; p++) {
    if (*p == '+' || *p == '-' || *p == '*' || *p == '/') {
      lastOp = p;
    }
  }

  if (lastOp && _calcOperator) {
    double operand2 = atof(lastOp + 1);
    switch (_calcOperator) {
    case '+':
      _calcResult = _calcOperand1 + operand2;
      break;
    case '-':
      _calcResult = _calcOperand1 - operand2;
      break;
    case '*':
      _calcResult = _calcOperand1 * operand2;
      break;
    case '/':
      _calcResult = (operand2 != 0) ? _calcOperand1 / operand2 : 0;
      break;
    }

    // Update expression to show result for chaining
    if (_calcResult == (int)_calcResult) {
      snprintf(_calcExpression, sizeof(_calcExpression), "%d",
               (int)_calcResult);
    } else {
      snprintf(_calcExpression, sizeof(_calcExpression), "%.4f", _calcResult);
    }

    _calcNewInput = true;
  }
}

void UIManager::calcClear() {
  _calcExpression[0] = '\0';
  _calcResult = 0;
  _calcOperand1 = 0;
  _calcOperator = 0;
  _calcNewInput = true;
}

void UIManager::calcBackspace() {
  int len = strlen(_calcExpression);
  if (len > 0) {
    _calcExpression[len - 1] = '\0';
  }
}

// ============================================================================
// Notes (Scribble Pad)
// ============================================================================

void UIManager::drawNotesScreen() {
  // Use Quality mode for drawing the UI
  M5.Display.setEpdMode(epd_mode_t::epd_quality);
  M5.Display.setTextSize(2); // Set default size for buttons
  M5.Display.fillScreen(COLOR_WHITE);

  // Toolbar Background (Right Side)
  int toolbarW = 100;
  int toolbarX = SCREEN_WIDTH - toolbarW;
  M5.Display.fillRect(toolbarX, 0, toolbarW, SCREEN_HEIGHT, COLOR_LIGHT_GRAY);

  // Initialize Canvas for scribbling (Left Side)
  if (_notesCanvas == nullptr) {
    _notesCanvas = new M5Canvas(&M5.Display);
    _notesCanvas->setColorDepth(4); // 4-bit grayscale for EPD
    _notesCanvas->createSprite(toolbarX, SCREEN_HEIGHT);
    _notesCanvas->fillSprite(WHITE);
  }
  _notesCanvas->pushSprite(0, 0);

  // Toolbar Buttons
  int btnX = toolbarX + 10;
  int btnW = 80;
  int btnH = 50;
  int gap = 10;
  int y = 10;

  drawButton(btnX, y, btnW, btnH, "THIN");
  y += (btnH + gap);
  drawButton(btnX, y, btnW, btnH, "MED");
  y += (btnH + gap);
  drawButton(btnX, y, btnW, btnH, "THICK");
  y += (btnH + gap);
  drawButton(btnX, y, btnW, btnH, "ERASE");
  y += (btnH + gap);

  y += 10; // Extra gap
  drawButton(btnX, y, btnW, btnH, "SAVE");
  y += (btnH + gap);

  // File browser button
  drawButton(btnX, y, btnW, btnH, "FILES");
  y += (btnH + gap);

  // Navigation buttons
  drawButton(btnX, y, btnW, btnH, "PREV");
  y += (btnH + gap);
  drawButton(btnX, y, btnW, btnH, "NEXT");
  y += (btnH + gap);

  drawButton(btnX, y, btnW, btnH, "CLR");

  // File count indicator (if files exist)
  if (!_noteFileList.empty() && _noteFileIndex >= 0) {
    y += (btnH + gap + 5);
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(COLOR_BLACK);
    char fileInfo[16];
    snprintf(fileInfo, sizeof(fileInfo), "%d/%d", _noteFileIndex + 1,
             _noteFileList.size());
    M5.Display.setCursor(btnX + 25, y);
    M5.Display.print(fileInfo);
  }

  // "X" Exit (Top Left)
  drawButton(10, 10, 60, 50, "X");

  // Hint
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(COLOR_BLACK);
  M5.Display.setCursor(80, 20);
  M5.Display.print("Draw Mode");

  // Switch to Fastest mode for drawing responsiveness
  M5.Display.setEpdMode(epd_mode_t::epd_fastest);

  // Reset pen state if needed
  if (_penSize == 0)
    _penSize = 2;
  _penColor = 0; // Black

  // Initial scan for files if list is empty
  if (_noteFileList.empty()) {
    notesScanFiles();
  }
}

void UIManager::handleNotesTouch(int x, int y) {
  int toolbarX = SCREEN_WIDTH - 100;

  // Exit Button
  if (x >= 10 && x < 70 && y >= 10 && y < 60) {
    Buzzer::click();
    // Force clean exit to remove doodles
    M5.Display.setEpdMode(epd_mode_t::epd_quality);
    M5.Display.fillScreen(COLOR_WHITE);
    M5.Display.display();
    navigateTo(ScreenID::HOME);
    return;
  }

  // Toolbar Interaction
  if (x > toolbarX) {
    int btnW = 80;
    int btnH = 50;
    int gap = 10;
    int startY = 10;
    int btnX = toolbarX + 10;

    auto checkBtn = [&](int index) {
      int by = startY + index * (btnH + gap);
      if (index >= 4)
        by += 10; // Extra gap after ERASE
      return (y >= by && y < by + btnH);
    };

    if (checkBtn(0)) {
      Buzzer::click();
      _penSize = 2;
      _penColor = 0;
    } // THIN
    else if (checkBtn(1)) {
      Buzzer::click();
      _penSize = 5;
      _penColor = 0;
    } // MED
    else if (checkBtn(2)) {
      Buzzer::click();
      _penSize = 10;
      _penColor = 0;
    } // THICK
    else if (checkBtn(3)) {
      Buzzer::click();
      _penSize = 10;
      _penColor = 0xFFFF;
    } // ERASE (White)
    else if (checkBtn(4)) {
      Buzzer::click();
      notesSave();
    } // SAVE
    else if (checkBtn(5)) {
      // FILES button - open file browser
      Buzzer::click();

      // Power cycle SD card before scanning
      extern SDManager *sdManager;
      if (sdManager) {
        Serial.println("FILES: Power cycling SD before scan...");
        if (sdManager->powerCycleAndReinit()) {
          notesScanFiles(); // Refresh file list
          navigateTo(ScreenID::NOTES_BROWSE);
        } else {
          Serial.println("FILES: SD power cycle failed");
        }
      } else {
        // Fallback without power cycle (shouldn't happen)
        notesScanFiles();
        navigateTo(ScreenID::NOTES_BROWSE);
      }
    } else if (checkBtn(6)) {
      // PREV button
      Buzzer::click();
      notesPrevFile();
    } else if (checkBtn(7)) {
      // NEXT button
      Buzzer::click();
      notesNextFile();
    } else if (checkBtn(8)) {
      // Clear Button
      Buzzer::click();
      if (_notesCanvas)
        _notesCanvas->fillSprite(WHITE);
      _needsRefresh = true;
      _lastRefresh = 0;
    }
  }
}

void UIManager::setTouchState(int x, int y, bool pressed) {
  _currentTouchX = x;
  _currentTouchY = y;
  _currentTouchPressed = pressed;
}

void UIManager::updateNotes() {
  // Use manual touch state from main loop
  if (_currentTouchPressed) {
    int x = _currentTouchX;
    int y = _currentTouchY;

    // Ignore touch on toolbar or Exit button
    int toolbarX = SCREEN_WIDTH - 100;
    bool inToolbar = (x > toolbarX);
    bool inExit = (x >= 10 && x < 70 && y >= 10 && y < 60);

    if (!inToolbar && !inExit) {
      M5.Display.startWrite();

      if (_isDrawing && _lastDrawX != -1) {
        // Draw continuous line
        M5.Display.fillCircle(x, y, _penSize / 2, _penColor);
        if (_notesCanvas)
          _notesCanvas->fillCircle(x, y, _penSize / 2, _penColor);

        // Basic interpolation
        float dist = sqrt(pow(x - _lastDrawX, 2) + pow(y - _lastDrawY, 2));
        if (dist > _penSize) {
          int steps = dist / (_penSize / 2.0);
          for (int i = 1; i <= steps; i++) {
            int ix = _lastDrawX + (x - _lastDrawX) * i / steps;
            int iy = _lastDrawY + (y - _lastDrawY) * i / steps;
            M5.Display.fillCircle(ix, iy, _penSize / 2, _penColor);
            if (_notesCanvas)
              _notesCanvas->fillCircle(ix, iy, _penSize / 2, _penColor);
          }
        }
      } else {
        // Start point
        M5.Display.fillCircle(x, y, _penSize / 2, _penColor);
        if (_notesCanvas)
          _notesCanvas->fillCircle(x, y, _penSize / 2, _penColor);
      }

      M5.Display.endWrite();

      _lastDrawX = x;
      _lastDrawY = y;
      _isDrawing = true;
    } else {
      _isDrawing = false;
      _lastDrawX = -1;
    }
  } else {
    _isDrawing = false;
    _lastDrawX = -1;
  }
}

void UIManager::notesSave() {
  if (!_notesCanvas)
    return;

  Serial.println("\n=== NOTES SAVE START ===");
  Buzzer::click();

  // Show saving status
  M5.Display.setEpdMode(epd_mode_t::epd_fast);
  M5.Display.fillRect(SCREEN_WIDTH / 2 - 100, SCREEN_HEIGHT / 2 - 30, 200, 60,
                      COLOR_BLACK);
  M5.Display.setTextColor(COLOR_WHITE);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(SCREEN_WIDTH / 2 - 60, SCREEN_HEIGHT / 2 - 10);
  M5.Display.print("Saving...");
  M5.Display.display();

  // === POWER CYCLE SD CARD ===
  Serial.println("Stopping display for SD operations...");
  M5.Display.waitDisplay();
  M5.Display.endWrite();

  // Use global SDManager if available, otherwise do manual power cycle
  extern SDManager *sdManager;
  bool sdOk = false;
  if (sdManager) {
    Serial.println("FILES: Power cycling SD before scan...");
    sdOk = sdManager->powerCycleAndReinit();
  } else {
    Serial.println("FILES: No SDManager, using direct power cycle...");
    SD.end();
    M5.Power.setExtOutput(false);
    delay(500);
    M5.Power.setExtOutput(true);
    delay(500);
    sdOk = SD.begin(47);
  }

  if (!sdOk) {
    Serial.println("ERROR: SD power cycle failed!");
    M5.Display.fillRect(SCREEN_WIDTH / 2 - 100, SCREEN_HEIGHT / 2 - 30, 200, 60,
                        COLOR_WHITE);
    M5.Display.setTextColor(COLOR_BLACK);
    M5.Display.setCursor(SCREEN_WIDTH / 2 - 60, SCREEN_HEIGHT / 2 - 10);
    M5.Display.print("SD Failed!");
    M5.Display.display();
    delay(2000);
    _needsRefresh = true;
    _lastRefresh = 0;
    Serial.println("=== NOTES SAVE END (FAILED) ===\n");
    return;
  }

  // Update status
  M5.Display.fillRect(SCREEN_WIDTH / 2 - 100, SCREEN_HEIGHT / 2 - 30, 200, 60,
                      COLOR_BLACK);
  M5.Display.setTextColor(COLOR_WHITE);
  M5.Display.setCursor(SCREEN_WIDTH / 2 - 40, SCREEN_HEIGHT / 2 - 10);
  M5.Display.print("Saving...");
  M5.Display.display();

  // Ensure /notes directory exists
  Serial.println("Checking /notes directory...");
  if (!SD.exists("/notes")) {
    Serial.println("Creating /notes directory...");
    SD.mkdir("/notes");
  } else {
    Serial.println("/notes directory exists");
  }

  // Get timestamp from RTC
  int year, month, day, weekday;
  int hours, minutes, seconds;
  RTC::getDate(year, month, day, weekday);
  RTC::getTime(hours, minutes, seconds);

  // Generate timestamped filename: /notes/note_YYYYMMDD_HHMMSS.bin
  char filename[50];
  snprintf(filename, sizeof(filename),
           "/notes/note_%04d%02d%02d_%02d%02d%02d.bin", year, month, day, hours,
           minutes, seconds);

  Serial.printf("Opening file for write: %s\n", filename);

  File f = SD.open(filename, FILE_WRITE);
  if (f) {
    uint16_t w = _notesCanvas->width();
    uint16_t h = _notesCanvas->height();
    uint8_t d = _notesCanvas->getColorDepth();

    Serial.printf("Canvas: %dx%d, depth=%d\n", w, h, d);

    // Header
    f.write((uint8_t *)"M5NOTE", 6);
    f.write((uint8_t *)&w, 2);
    f.write((uint8_t *)&h, 2);
    f.write(&d, 1);

    // Data - calculate size in bytes
    size_t len = (w * h * d) / 8;
    if (d < 8 && (w * h * d) % 8 != 0)
      len++; // Round up bits

    Serial.printf("Writing %d bytes of pixel data...\n", len);
    size_t written = f.write((uint8_t *)_notesCanvas->getBuffer(), len);
    f.flush();
    f.close();

    Serial.printf("Write complete: %d/%d bytes\n", written, len);

    if (written == len) {
      // Store as current file and rescan
      _currentNoteFile = String(filename);
      notesScanFiles();
      Serial.println("Note saved successfully!");

      // Show success
      M5.Display.fillRect(SCREEN_WIDTH / 2 - 100, SCREEN_HEIGHT / 2 - 30, 200,
                          60, COLOR_WHITE);
      M5.Display.setTextColor(COLOR_BLACK);
      M5.Display.setCursor(SCREEN_WIDTH / 2 - 40, SCREEN_HEIGHT / 2 - 10);
      M5.Display.print("Saved!");
      M5.Display.display();
      delay(1000);
    } else {
      Serial.println("ERROR: Incomplete write!");
    }
  } else {
    Serial.println("ERROR: Failed to open file for writing");
  }

  // Restore UI
  M5.Display.setEpdMode(epd_mode_t::epd_fastest);
  _needsRefresh = true;
  _lastRefresh = 0;
  Serial.println("=== NOTES SAVE END ===\n");
}

void UIManager::notesLoad() {
  // Scan for files if list is empty
  if (_noteFileList.empty()) {
    notesScanFiles();
  }

  // Load the file at current index
  if (!_noteFileList.empty()) {
    notesLoadByIndex();
  } else {
    Serial.println("Notes: No saved notes found");
  }
}

// ============================================================================

// ============================================================================
// Notes File Browser Screen
// ============================================================================

void UIManager::drawNotesBrowseScreen() {
  M5.Display.fillScreen(COLOR_WHITE);

  // === HEADER ===
  // === HEADER ===
  M5.Display.setTextSize(2); // Reverted 3 -> 2
  M5.Display.setTextColor(COLOR_BLACK);
  M5.Display.setCursor(20, 15);
  M5.Display.print("Notes File Browser");

  // Navigation buttons (Next to X)
  // Navigation buttons (Next to X) - Widened to 100px to fit text
  drawButton(SCREEN_WIDTH - 290, 10, 100, 40, "UP");
  drawButton(SCREEN_WIDTH - 185, 10, 100, 40, "DOWN");

  // Close button
  drawButton(SCREEN_WIDTH - 80, 10, 70, 40, "X");

  // Divider under header
  M5.Display.drawLine(0, 60, SCREEN_WIDTH, 60, COLOR_BLACK);

  // === LAYOUT CONSTANTS ===
  const int LEFT_PANEL_X = 0;
  const int LEFT_PANEL_W = 400;
  const int RIGHT_PANEL_X = LEFT_PANEL_W;
  const int RIGHT_PANEL_W = SCREEN_WIDTH - LEFT_PANEL_W;
  const int CONTENT_Y = 70;
  const int CONTENT_H =
      SCREEN_HEIGHT - CONTENT_Y - 80; // Leave space for buttons

  // Vertical divider between panels
  M5.Display.drawLine(LEFT_PANEL_W, 60, LEFT_PANEL_W, SCREEN_HEIGHT - 80,
                      COLOR_BLACK);

  // === LEFT PANEL: FILE LIST ===
  // === LEFT PANEL: FILE LIST ===
  M5.Display.setTextSize(1); // Reverted 2 -> 1
  M5.Display.setCursor(10, CONTENT_Y);
  M5.Display.printf("Files (%d)", _noteFileList.size());

  int listY = CONTENT_Y + 30;
  int fileEntryH = 60;
  int maxVisible = 6;

  for (int i = 0; i < min((int)_noteFileList.size(), maxVisible); i++) {
    int fileIdx = i + _notesBrowseScroll;
    if (fileIdx >= (int)_noteFileList.size())
      break;

    String filename = _noteFileList[fileIdx];
    bool isSelected = (fileIdx == _selectedFileIndex);

    // Highlight selected file
    if (isSelected) {
      M5.Display.fillRect(5, listY, LEFT_PANEL_W - 10, fileEntryH,
                          COLOR_LIGHT_GRAY);
    }

    M5.Display.drawRect(5, listY, LEFT_PANEL_W - 10, fileEntryH, COLOR_BLACK);

    // Parse time from filename
    String timeStr = "??:??";
    if (filename.startsWith("note_") && filename.length() >= 24) {
      String t = filename.substring(14, 20); // HHMMSS
      timeStr = String(t[0]) + t[1] + ":" + t[2] + t[3];
    }

    M5.Display.setTextSize(1); // Reverted 2 -> 1
    M5.Display.setTextColor(COLOR_BLACK);
    M5.Display.setCursor(15, listY + 10);
    M5.Display.printf("Note #%d", fileIdx + 1);
    M5.Display.setTextSize(1);
    M5.Display.setCursor(15, listY + 35);
    M5.Display.print(timeStr);

    listY += fileEntryH + 5;
  }

  // Scroll indicators
  if (_notesBrowseScroll > 0) {
    M5.Display.fillTriangle(LEFT_PANEL_W / 2 - 10, CONTENT_Y + 35,
                            LEFT_PANEL_W / 2 + 10, CONTENT_Y + 35,
                            LEFT_PANEL_W / 2, CONTENT_Y + 25,
                            COLOR_BLACK); // Up arrow
  }
  if (_notesBrowseScroll + maxVisible < (int)_noteFileList.size()) {
    M5.Display.fillTriangle(LEFT_PANEL_W / 2 - 10, listY + 5,
                            LEFT_PANEL_W / 2 + 10, listY + 5, LEFT_PANEL_W / 2,
                            listY + 15, COLOR_BLACK); // Down arrow
  }

  // === RIGHT PANEL: PREVIEW & METADATA ===
  if (_selectedFileIndex >= 0 &&
      _selectedFileIndex < (int)_noteFileList.size()) {
    String selectedFile = _noteFileList[_selectedFileIndex];

    // Preview thumbnail area
    // Preview thumbnail area - Increased by 30% (approx 360x225)
    int thumbX = RIGHT_PANEL_X + 40; // Shifted left to center better
    int thumbY = CONTENT_Y + 20;
    int thumbW = 360;
    int thumbH = 225;

    M5.Display.drawRect(thumbX, thumbY, thumbW, thumbH, COLOR_BLACK);

    // Show preview canvas if loaded, otherwise placeholder
    if (_previewCanvas != nullptr && _previewFileIndex == _selectedFileIndex) {
      _previewCanvas->pushSprite(thumbX + 1, thumbY + 1);
    } else {
      M5.Display.fillRect(thumbX + 1, thumbY + 1, thumbW - 2, thumbH - 2,
                          COLOR_LIGHT_GRAY);
      M5.Display.setTextSize(1); // Reduced further to fit
      M5.Display.setTextColor(COLOR_BLACK);
      int txtW = M5.Display.textWidth("Tap file to preview");
      M5.Display.setCursor(thumbX + (thumbW - txtW) / 2,
                           thumbY + (thumbH - 8) / 2);
      M5.Display.print("Tap file to preview");
    }

    // Metadata section
    int metaY = thumbY + thumbH + 20;

    // Parse filename for metadata
    if (selectedFile.startsWith("note_") && selectedFile.length() >= 24) {
      String dateStr = selectedFile.substring(5, 13);  // YYYYMMDD
      String timeStr = selectedFile.substring(14, 20); // HHMMSS

      M5.Display.setTextSize(1);
      M5.Display.setCursor(RIGHT_PANEL_X + 20, metaY);
      M5.Display.print("Created:");
      M5.Display.setTextSize(1); // Reduced from 2 to 1
      M5.Display.setCursor(RIGHT_PANEL_X + 20, metaY + 30);
      M5.Display.printf("%c%c%c%c-%c%c-%c%c  %c%c:%c%c", dateStr[0], dateStr[1],
                        dateStr[2], dateStr[3], dateStr[4], dateStr[5],
                        dateStr[6], dateStr[7], timeStr[0], timeStr[1],
                        timeStr[2], timeStr[3]);
    }
  }

  // === BOTTOM: ACTION BUTTONS ===
  int btnY = SCREEN_HEIGHT - 70;
  int btnW = (SCREEN_WIDTH - 30) / 2;
  int btnH = 60;

  M5.Display.drawLine(0, btnY - 10, SCREEN_WIDTH, btnY - 10, COLOR_BLACK);

  drawButton(10, btnY, btnW, btnH, "LOAD", true);
  drawButton(20 + btnW, btnY, btnW, btnH, "DELETE");

  // Delete confirmation popup
  if (_deleteConfirmIndex >= 0) {
    M5.Display.fillRect(200, 200, 560, 200, COLOR_WHITE);
    M5.Display.drawRect(200, 200, 560, 200, COLOR_BLACK);
    M5.Display.setTextSize(1.5); // Reduced from 3 to 1.5 (50%)
    M5.Display.setCursor(250, 230);
    M5.Display.print("Delete this note?");
    drawButton(260, 290, 200, 60, "YES");
    drawButton(500, 290, 200, 60, "NO");
  }
}

void UIManager::handleNotesBrowseTouch(int x, int y) {
  auto isHit = [&](int bx, int by, int bw, int bh) {
    if (x >= bx && x < bx + bw && y >= by && y < by + bh) {
      Buzzer::click();
      return true;
    }
    return false;
  };

  // Handle delete confirmation popup
  if (_deleteConfirmIndex >= 0) {
    if (isHit(260, 290, 200, 60)) {
      // YES - delete file
      notesDeleteFile(_deleteConfirmIndex);
      _deleteConfirmIndex = -1;
      _needsRefresh = true;
      _lastRefresh = 0;
    } else if (isHit(500, 290, 200, 60)) {
      // NO - cancel
      _deleteConfirmIndex = -1;
      _needsRefresh = true;
      _lastRefresh = 0;
    }
    return;
  }

  // Close button
  if (isHit(SCREEN_WIDTH - 80, 10, 70, 40)) {
    navigateTo(ScreenID::NOTES);
    return;
  }

  // Scroll UP button
  if (isHit(SCREEN_WIDTH - 290, 10, 100, 40)) {
    if (_notesBrowseScroll > 0) {
      _notesBrowseScroll--;
      _needsRefresh = true;
      _lastRefresh = 0;
    }
    return;
  }

  // Scroll DOWN button
  if (isHit(SCREEN_WIDTH - 185, 10, 100, 40)) {
    int maxVisible = 6;
    if (_notesBrowseScroll + maxVisible < (int)_noteFileList.size()) {
      _notesBrowseScroll++;
      _needsRefresh = true;
      _lastRefresh = 0;
    }
    return;
  }

  // === LEFT PANEL: FILE LIST SELECTION ===
  const int LEFT_PANEL_W = 400;
  const int CONTENT_Y = 70;
  int listY = CONTENT_Y + 30;
  int fileEntryH = 60;
  int maxVisible = 6;

  for (int i = 0; i < min((int)_noteFileList.size(), maxVisible); i++) {
    int fileIdx = i + _notesBrowseScroll;
    if (fileIdx >= (int)_noteFileList.size())
      break;

    // Check if file entry was clicked
    if (isHit(5, listY, LEFT_PANEL_W - 10, fileEntryH)) {
      _selectedFileIndex = fileIdx;
      // Load preview for selected file
      loadNotePreview(fileIdx);
      _needsRefresh = true;
      _lastRefresh = 0;
      return;
    }

    listY += fileEntryH + 5;
  }

  // Scroll arrows
  if (_notesBrowseScroll > 0) {
    // Up arrow area
    if (isHit(LEFT_PANEL_W / 2 - 20, CONTENT_Y + 20, 40, 20)) {
      _notesBrowseScroll--;
      _needsRefresh = true;
      _lastRefresh = 0;
      return;
    }
  }
  if (_notesBrowseScroll + maxVisible < (int)_noteFileList.size()) {
    // Down arrow area
    if (isHit(LEFT_PANEL_W / 2 - 20, listY, 40, 20)) {
      _notesBrowseScroll++;
      _needsRefresh = true;
      _lastRefresh = 0;
      return;
    }
  }

  // === BOTTOM: ACTION BUTTONS ===
  int btnY = SCREEN_HEIGHT - 70;
  int btnW = (SCREEN_WIDTH - 30) / 2;
  int btnH = 60;

  // LOAD button
  if (isHit(10, btnY, btnW, btnH)) {
    _noteFileIndex = _selectedFileIndex;
    notesLoadByIndex();
    navigateTo(ScreenID::NOTES);
    return;
  }

  // DELETE button
  if (isHit(20 + btnW, btnY, btnW, btnH)) {
    Serial.printf("DELETE button pressed for file index %d\n",
                  _selectedFileIndex);
    _deleteConfirmIndex = _selectedFileIndex;
    // Force immediate redraw to show confirmation popup
    M5.Display.setEpdMode(epd_mode_t::epd_fast);
    drawNotesBrowseScreen();
    M5.Display.display();
    Serial.println("Delete confirmation shown");
    return;
  }
}

void UIManager::notesDeleteFile(int index) {
  if (index < 0 || index >= (int)_noteFileList.size()) {
    Serial.println("Invalid file index for deletion");
    return;
  }

  String filename = _noteFileList[index];
  String fullPath = "/notes/" + filename;

  Serial.printf("Deleting file: %s\n", fullPath.c_str());

  // Power cycle SD card before delete operation
  extern SDManager *sdManager;
  bool sdOk = false;
  if (sdManager) {
    Serial.println("DELETE: Power cycling SD before delete...");
    sdOk = sdManager->powerCycleAndReinit();
  } else {
    Serial.println("DELETE: No SDManager, attempting direct delete...");
    sdOk = true; // Try anyway
  }

  if (!sdOk) {
    Serial.println("DELETE: SD power cycle failed");
    return;
  }

  if (SD.remove(fullPath)) {
    Serial.println("File deleted successfully");

    // Refresh file list
    notesScanFiles();

    // Adjust current index if needed
    if (_noteFileIndex >= (int)_noteFileList.size() && _noteFileIndex > 0) {
      _noteFileIndex--;
    }
  } else {
    Serial.println("Failed to delete file");
  }
}

// ============================================================================
// Load Note Preview Thumbnail
// ============================================================================
void UIManager::loadNotePreview(int index) {
  if (index < 0 || index >= (int)_noteFileList.size()) {
    Serial.println("Invalid preview index");
    return;
  }

  // Skip if already loaded
  if (index == _previewFileIndex && _previewCanvas != nullptr) {
    return;
  }

  Serial.printf("Loading preview for file %d\n", index);

  // Create preview canvas if needed (360x225 scaled preview)
  if (_previewCanvas == nullptr) {
    _previewCanvas = new M5Canvas(&M5.Display);
    _previewCanvas->setColorDepth(4);
    if (!_previewCanvas->createSprite(360, 225)) {
      Serial.println("Failed to create preview canvas");
      delete _previewCanvas;
      _previewCanvas = nullptr;
      return;
    }
  }

  // Power cycle SD and load
  extern SDManager *sdManager;
  if (sdManager && !sdManager->powerCycleAndReinit()) {
    Serial.println("Preview: SD power cycle failed");
    return;
  }

  String filename = _noteFileList[index];
  String fullPath = "/notes/" + filename;

  File file = SD.open(fullPath, FILE_READ);
  if (!file) {
    Serial.println("Cannot open file for preview");
    _previewCanvas->fillSprite(COLOR_WHITE);
    _previewCanvas->setTextSize(2);
    _previewCanvas->setCursor(100, 100);
    _previewCanvas->print("Cannot load");
    _previewFileIndex = index;
    return;
  }

  // Read header
  char header[7] = {0};
  file.read((uint8_t *)header, 6);

  if (strcmp(header, "M5NOTE") != 0) {
    Serial.println("Invalid note format");
    file.close();
    _previewCanvas->fillSprite(COLOR_WHITE);
    _previewCanvas->setTextSize(2);
    _previewCanvas->setCursor(80, 100);
    _previewCanvas->print("Invalid format");
    _previewFileIndex = index;
    return;
  }

  // Read dimensions
  uint16_t origW, origH;
  uint8_t origDepth;
  file.read((uint8_t *)&origW, 2);
  file.read((uint8_t *)&origH, 2);
  file.read(&origDepth, 1);

  Serial.printf("Preview: Original %dx%d depth=%d\n", origW, origH, origDepth);

  // Create temporary canvas to load original data
  M5Canvas tempCanvas(&M5.Display);
  tempCanvas.setColorDepth(4);
  if (!tempCanvas.createSprite(origW, origH)) {
    Serial.println("Cannot create temp canvas for preview");
    file.close();
    _previewFileIndex = index;
    return;
  }

  // Read pixel data
  size_t dataSize = (origW * origH * 4) / 8;
  file.read((uint8_t *)tempCanvas.getBuffer(), dataSize);
  file.close();

  // Scale down to preview size (simple nearest-neighbor)
  // Target: 360x225
  float scaleX = (float)origW / 360.0f;
  float scaleY = (float)origH / 225.0f;

  _previewCanvas->fillSprite(COLOR_WHITE);
  for (int py = 0; py < 225; py++) {
    for (int px = 0; px < 360; px++) {
      int sx = (int)(px * scaleX);
      int sy = (int)(py * scaleY);
      uint16_t color = tempCanvas.readPixel(sx, sy);
      _previewCanvas->drawPixel(px, py, color);
    }
  }

  tempCanvas.deleteSprite();
  _previewFileIndex = index;
  Serial.println("Preview loaded successfully");
}

// ============================================================================
// Power Management & Smart Refresh
// ============================================================================

bool UIManager::shouldUpdateDashboard(const Fossibot::PowerBankData &newData) {
  // DEBUG: Bypass 7W filtering - always update
  // return true; // DISABLED: Re-enabling logic below to prevent refresh spam

  // 1. Critical Status Changes (Always Update)
  if (newData.usbActive != _lastRenderedData.usbActive)
    return true;
  if (newData.dcActive != _lastRenderedData.dcActive)
    return true;
  if (newData.acActive != _lastRenderedData.acActive)
    return true;

  // 2. Battery Percentage Change (Any change is significant for user)
  if (newData.batteryPercent != _lastRenderedData.batteryPercent)
    return true;

  // 3. Power Jitter Filtering (Only update if power changes > 50 Watts)
  // Input Power
  if (abs(newData.inputPower - _lastRenderedData.inputPower) > 50)
    return true;

  // Output Power
  if (abs(newData.outputPower - _lastRenderedData.outputPower) > 50)
    return true;

  // 4. Force update every 30 seconds regardless of data to keep clock/time sync
  if (millis() - _lastDashboardUpdate > 30000)
    return true;

  return false;
}

void UIManager::checkPowerManagement() {
  // 1. CRITICAL: Check Device Battery (3.3V Cutoff)
  // Low battery shutdown to protect LiPo and ensure clean state
  float vBat = Battery::getVoltage();
  if (vBat > 0.5f && vBat <= 3.30f) {
    Serial.printf("CRITICAL: Battery Low (%.2fV). Shutting down.\n", vBat);

    // Play distinctive alarm beep
    Buzzer::alarm(3);

    // Visual feedback
    M5.Display.clear(COLOR_WHITE);
    M5.Display.setTextColor(COLOR_BLACK);
    M5.Display.setTextDatum(middle_center);
    M5.Display.drawString("LOW POWER", SCREEN_WIDTH / 2,
                          SCREEN_HEIGHT / 2 - 40);
    M5.Display.drawString("Shutting down...", SCREEN_WIDTH / 2,
                          SCREEN_HEIGHT / 2 + 40);
    M5.Display.display();

    delay(3000); // Give user time to see message

    // Skip showSleepImage() to save power and go directly to sleep
    Serial.println("Entering Deep Sleep (Low Power)...");
    M5.Display.sleep();
    esp_deep_sleep_start();
    return;
  }

  // BLE connection status
  extern FossibotBLE *bleClient;
  bool bleConnected = (bleClient && bleClient->isConnected());

  // Track BLE disconnection time
  if (bleConnected) {
    _bleDisconnectedTime = 0; // Reset when connected
  } else if (_bleDisconnectedTime == 0) {
    _bleDisconnectedTime = millis(); // Record when first disconnected
  }

  // 1. Deep Sleep Logic: Only if BLE disconnected for 1 hour
  // Check config just in case user set it to 0 (disabled)
  extern Config *config;
  bool sleepDisabled = (config && config->getAutoSleepMinutes() == 0);

  // Never sleep while Timer or Pomodoro is actively running
  bool timerActive = (_clockMode == ClockMode::TIMER && _timerRunning);
  bool pomodoroActive = (_clockMode == ClockMode::POMODORO &&
                         _pomodoroState == PomodoroState::RUNNING);

  unsigned long bleDisconnectDuration = 60 * 60 * 1000UL; // 1 hour
  if (!sleepDisabled && !timerActive && !pomodoroActive && !bleConnected &&
      _bleDisconnectedTime > 0 &&
      (millis() - _bleDisconnectedTime > bleDisconnectDuration)) {
    Serial.println("BLE disconnected for 1 hour, entering deep sleep...");
    enterDeepSleep();
  }

  // Check if the current screen requires high performance
  // With "Instant Wakeup" in main.cpp, we can allow ALL screens to sleep!
  bool isHighPerfScreen = false;

  // Check external inhibition (e.g. BLE connecting need stable clock)
  extern FossibotBLE *bleClient;
  bool bleBusy = (bleClient && bleClient->isConnecting());

  // NOTE: We intentionally do NOT exit eco mode for _needsRefresh.
  // Auto-refresh (60s timer, BLE data) should happen at 80MHz.
  // Only actual user touch (handled in main.cpp) exits eco mode.

  if (!isHighPerfScreen && !bleBusy && (millis() - _lastActivityTime > 5000)) {
    // Enter Eco Mode (80MHz)
    if (!_lowPowerMode) {
      setCpuFrequencyMhz(80);
      Serial.printf("Power: Entered Eco Mode (80MHz). Active=%lu\n",
                    millis() - _lastActivityTime);
      _lowPowerMode = true;
    }
  } else if (bleBusy || isHighPerfScreen) {
    // Only exit eco mode for BLE operations or high-perf screens
    // NOT for auto-refresh
    exitEcoMode();
  }

  // DEBUG: Periodic Power Status
  /*
  static unsigned long lastPowerLog = 0;
  if (millis() - lastPowerLog > 1000) {
    lastPowerLog = millis();
    Serial.printf("PM Debug: Screen=%d HighPerf=%d BLE=%d Render=%d Idle=%lu "
                  "ms Mode=%s Freq=%u MHz\n",
                  (int)_currentScreen, isHighPerfScreen, bleBusy, rendering,
                  (millis() - _lastActivityTime),
                  _lowPowerMode ? "ECO" : "PERF", getCpuFrequencyMhz());
  }
  */
}

void UIManager::exitEcoMode() {
  if (_lowPowerMode) {
    // Only set if we are currently in Eco Mode (avoid redundant calls)
    if (getCpuFrequencyMhz() < 240) {
      setCpuFrequencyMhz(240);
      Serial.println("Power: Exited Eco Mode (240MHz). Reason: Activity");
    } else {
      Serial.println(
          "Power: Exited Eco Mode (Already 240MHz). Reason: Activity");
    }
    _lowPowerMode = false;
  }
}

void UIManager::showSleepImage() {
  // GHOSTING FIX: Use quality EPD mode for deep sleep image
  // E-ink needs a full quality refresh cycle to properly set pixels
  // before entering deep sleep (no further updates possible after sleep)
  M5.Display.setEpdMode(epd_mode_t::epd_quality);

  extern SDManager *sdManager;
  if (!sdManager || !sdManager->isAvailable()) {
    // Even without an image, do a clean white refresh to clear ghosting
    M5.Display.fillScreen(COLOR_WHITE);
    M5.Display.display();
    return;
  }

  String imagePath = sdManager->getRandomPictureForSleep();
  if (imagePath.isEmpty()) {
    // No image available, do a clean white refresh
    M5.Display.fillScreen(COLOR_WHITE);
    M5.Display.display();
    return;
  }

  // Clear screen
  M5.Display.fillScreen(COLOR_WHITE);

  // Load and draw image based on extension
  String ext = imagePath.substring(imagePath.lastIndexOf('.') + 1);
  ext.toLowerCase();

  File imgFile = SD.open(imagePath.c_str(), FILE_READ);
  if (!imgFile) {
    Serial.printf("Failed to open image: %s\n", imagePath.c_str());
    return;
  }

  size_t fileSize = imgFile.size();
  uint8_t *imgBuf = (uint8_t *)ps_malloc(fileSize);
  if (!imgBuf) {
    Serial.println("Failed to allocate PSRAM for sleep image");
    imgFile.close();
    return;
  }

  imgFile.read(imgBuf, fileSize);
  imgFile.close();

  // Detect image dimensions to set correct rotation
  int imgWidth = 0, imgHeight = 0;

  if (ext == "png" && fileSize > 24) {
    // PNG: width at bytes 16-19, height at bytes 20-23 (big-endian)
    imgWidth = (imgBuf[16] << 24) | (imgBuf[17] << 16) | (imgBuf[18] << 8) |
               imgBuf[19];
    imgHeight = (imgBuf[20] << 24) | (imgBuf[21] << 16) | (imgBuf[22] << 8) |
                imgBuf[23];
  } else if ((ext == "jpg" || ext == "jpeg") && fileSize > 2) {
    // JPEG: Need to parse markers to find SOF0/SOF2
    for (size_t i = 2; i < fileSize - 10; i++) {
      if (imgBuf[i] == 0xFF &&
          (imgBuf[i + 1] == 0xC0 || imgBuf[i + 1] == 0xC2)) {
        // SOF marker found: height at i+5, width at i+7 (big-endian, 2 bytes
        // each)
        imgHeight = (imgBuf[i + 5] << 8) | imgBuf[i + 6];
        imgWidth = (imgBuf[i + 7] << 8) | imgBuf[i + 8];
        break;
      }
    }
  } else if (ext == "bmp" && fileSize > 26) {
    // BMP: width at bytes 18-21, height at bytes 22-25 (little-endian)
    imgWidth = imgBuf[18] | (imgBuf[19] << 8) | (imgBuf[20] << 16) |
               (imgBuf[21] << 24);
    imgHeight = imgBuf[22] | (imgBuf[23] << 8) | (imgBuf[24] << 16) |
                (imgBuf[25] << 24);
    if (imgHeight < 0)
      imgHeight = -imgHeight; // BMP can have negative height
  }

  Serial.printf("Sleep image: %dx%d\n", imgWidth, imgHeight);

  // Set rotation based on image orientation
  if (imgWidth > 0 && imgHeight > 0) {
    if (imgWidth > imgHeight) {
      // Landscape image
      M5.Display.setRotation(1);
    } else {
      // Portrait image
      M5.Display.setRotation(0);
    }
  }

  // Clear and draw
  M5.Display.fillScreen(COLOR_WHITE);

  if (ext == "png") {
    M5.Display.drawPng(imgBuf, fileSize, 0, 0);
  } else if (ext == "jpg" || ext == "jpeg") {
    M5.Display.drawJpg(imgBuf, fileSize, 0, 0);
  } else if (ext == "bmp") {
    M5.Display.drawBmp(imgBuf, fileSize, 0, 0);
  }

  free(imgBuf);

  // Overlay "zzz" text in bottom-left corner
  M5.Display.setTextColor(COLOR_BLACK);
  M5.Display.setTextSize(1);
  M5.Display.setCursor(10, M5.Display.height() - 20);
  M5.Display.print("zzz");

  M5.Display.display();
}

void UIManager::enterDeepSleep() {
  // 1. Show sleep image with "zzz" overlay
  showSleepImage();

  // Wait 5 seconds for e-ink to fully refresh before sleeping
  Serial.println("Deep sleep: Waiting 5s for display...");
  delay(5000);

  // 2. Turn off peripherals
  // M5.Power.setExtOutput(false); // CAUTION: If this powers Touch, wake fails.
  // M5PExt (Port B/C power) shouldn't affect main touch (usually internal)

  // 3. Configure Wakeup Sources
  // GT911 INT pin is GPIO 36 on M5Paper v1, GPIO 48 on M5Paper S3.

  // CRITICAL: Clear GT911 Status Register to release INT line (make it HIGH)
  // If we don't do this, the INT line might stay LOW (if last touch wasn't
  // cleared), causing an immediate wake-up or preventing detection of a NEW low
  // pulse.
  Wire.beginTransmission(0x5D);
  Wire.write(0x81);
  Wire.write(0x4E); // Status Register
  Wire.write(0x00); // Clear to 0
  Wire.endTransmission();

  // Ensure the pin is pulled HIGH (so touch pulling it LOW triggers wake)
  pinMode(GPIO_NUM_48, INPUT_PULLUP);

  // Try wake up on TOUCH (Touch controller pulls INT low)
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_48, 0);

  // Wake up for ALARM if enabled
  extern Config *config;
  if (config && config->getAlarmEnabled()) {
    int h, m, s;
    RTC::getTime(h, m, s);
    int ah = config->getAlarmHour();
    int am = config->getAlarmMinute();

    long currentSec = h * 3600 + m * 60 + s;
    long alarmSec = ah * 3600 + am * 60;

    long diffSec = alarmSec - currentSec;
    if (diffSec <= 0)
      diffSec += 24 * 3600; // Next day

    // Safety: Don't sleep if alarm is < 1 min away (might miss it during
    // transition)
    if (diffSec < 60) {
      Serial.println("Alarm imminent! Aborting sleep.");
      return;
    }

    uint64_t wakeUs = (uint64_t)diffSec * 1000000ULL;
    esp_sleep_enable_timer_wakeup(wakeUs);
    Serial.printf("Alarm set for %ld sec from now\n", diffSec);
  }

  // 4. Shutdown Display Controller (M5EPD)
  // M5Unified's sleep() puts the panel into low power maintain mode.
  // It should NOT kill the ESP32 or Touch power itself.
  M5.Display.sleep();

  // 5. Enter Deep Sleep
  Serial.println("Entering Deep Sleep...");
  Serial.flush();
  esp_deep_sleep_start();

  // Dead code
}

// ============================================================================
// SD Diagnostics Screen
// ============================================================================

void UIManager::drawSDDiagScreen() {
  M5.Display.fillScreen(COLOR_WHITE);

  // Header
  M5.Display.fillRect(0, 0, SCREEN_WIDTH, MENU_BAR_HEIGHT, COLOR_BLACK);
  M5.Display.setTextColor(COLOR_WHITE);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(20, 15);
  M5.Display.print("SD Card Diagnostics");

  // Back Button (Top Right)
  M5.Display.fillRect(SCREEN_WIDTH - 130, 5, 120, 50, COLOR_WHITE);
  M5.Display.setTextColor(COLOR_BLACK);
  M5.Display.setTextSize(1); // Set size 1 for Back Button
  M5.Display.drawRect(SCREEN_WIDTH - 130, 5, 120, 50, COLOR_BLACK);
  M5.Display.setCursor(SCREEN_WIDTH - 110, 15);
  M5.Display.print("BACK");

  extern SDManager *sdManager;
  if (!sdManager || !sdManager->isAvailable()) {
    M5.Display.setTextColor(COLOR_BLACK);
    M5.Display.setCursor(50, 200);
    M5.Display.print("SD Card Not Detected!");
    return;
  }

  // Card Info
  SDManager::SDCardInfo info = sdManager->getCardInfo();

  // Retry if info is invalid (User Feedback: "Unknown or 0")
  if (info.totalBytes == 0) {
    Serial.println("SD Diag: Info invalid, attempting re-init...");
    sdManager->powerCycleAndReinit();
    info = sdManager->getCardInfo();
  }

  M5.Display.setTextColor(COLOR_BLACK);
  M5.Display.setTextSize(1);
  int y = 100;

  M5.Display.setCursor(50, y);
  M5.Display.printf("Type: %s", info.type.c_str());

  y += 50;
  float totalSize = info.totalBytes / (1024.0 * 1024.0); // Start in MB
  const char *totalUnit = "MB";
  if (totalSize > 1024.0) {
    totalSize /= 1024.0;
    totalUnit = "GB";
  }

  float usedSize = info.usedBytes / (1024.0 * 1024.0); // Start in MB
  const char *usedUnit = "MB";
  if (usedSize > 1024.0) {
    usedSize /= 1024.0;
    usedUnit = "GB";
  }

  float freeBytes = (float)(info.totalBytes - info.usedBytes);
  float freeSize = freeBytes / (1024.0 * 1024.0);
  const char *freeUnit = "MB";
  if (freeSize > 1024.0) {
    freeSize /= 1024.0;
    freeUnit = "GB";
  }

  M5.Display.setCursor(50, y);
  M5.Display.printf("Size: %.2f %s", totalSize, totalUnit);

  y += 50;
  M5.Display.setCursor(50, y);
  M5.Display.printf("Used: %.2f %s (Free: %.2f %s)", usedSize, usedUnit,
                    freeSize, freeUnit);

  // Usage Bar
  y += 60;
  M5.Display.drawRect(50, y, 860, 40, COLOR_BLACK);
  if (info.totalBytes > 0) {
    // Use raw bytes for accurate percentage, casting to double for precision
    double ratio = (double)info.usedBytes / (double)info.totalBytes;
    int usedWidth = (int)(ratio * 860);

    // Ensure at least 1px visible if there is ANY data
    if (usedWidth == 0 && info.usedBytes > 0)
      usedWidth = 1;

    M5.Display.fillRect(50, y, usedWidth, 40, COLOR_GRAY);
  }

  // Benchmark Section
  y += 80;
  drawButton(50, y, 300, 80, "RUN TEST");

  M5.Display.setTextSize(1);
  M5.Display.setCursor(380, y + 15);
  M5.Display.print("Test read/write speeds.");
  M5.Display.setCursor(380, y + 45);
  M5.Display.print("(Writes ISOLATED temp file)");
}

void UIManager::handleSDDiagTouch(int x, int y) {
  // Back Button (Top Right)
  if (x > SCREEN_WIDTH - 140 && y < 60) {
    Buzzer::click();
    navigateTo(ScreenID::SETTINGS);
    return;
  }

  // Run Test Button
  if (x >= 50 && x <= 350 && y >= 340 && y <= 420) {
    Buzzer::click();

    // Draw "Running..."
    M5.Display.fillRect(380, 340, 500, 80, COLOR_WHITE);
    M5.Display.setTextSize(3);
    M5.Display.setTextColor(COLOR_BLACK);
    M5.Display.setCursor(380, 360);
    M5.Display.print("Running Benchmark...");
    // M5.Display.display();

    // Run Benchmark
    extern SDManager *sdManager;
    if (sdManager) {
      float writeSpeed, readSpeed;
      // Force power cycle before test for best stability
      bool reinitSuccess = sdManager->powerCycleAndReinit();
      if (!reinitSuccess) {
        Serial.println("Warning: Power cycle failed before benchmark");
      }

      bool success = sdManager->runBenchmark(writeSpeed, readSpeed);

      // Clear area
      M5.Display.fillRect(50, 340, 860, 150, COLOR_WHITE);
      M5.Display.setTextColor(COLOR_BLACK);

      if (success) {
        // Show Results
        M5.Display.setTextSize(3);
        M5.Display.setCursor(50, 350);
        M5.Display.print("Result:");
        M5.Display.setTextSize(4);
        M5.Display.setCursor(50, 400);
        M5.Display.printf("Write: %.2f MB/s", writeSpeed);
        M5.Display.setCursor(50, 460);
        M5.Display.printf("Read:  %.2f MB/s", readSpeed);
      } else {
        M5.Display.setTextSize(3);
        M5.Display.setCursor(50, 360);
        M5.Display.print("Benchmark Failed!");
        M5.Display.setCursor(50, 400);
        M5.Display.setTextSize(2);
        M5.Display.print("Check serial log. Re-insert card?");
      }
    }
  }
}

// ============================================================================
// ALARM & TIMER & HOME SCREEN IMPLEMENTATIONS
// ============================================================================

void UIManager::drawAlarmContent(int x, int y, int w, int h) {
  // 1. Header
  M5.Display.setTextColor(COLOR_BLACK);
  M5.Display.setTextSize(3);
  const char *title = "Alarm Time";
  int titleW = M5.Display.textWidth(title);
  M5.Display.setCursor(x + (w - titleW) / 2, y + 40);
  M5.Display.print(title);

  // 2. Prepare Data
  extern Config *config;
  int ah = config ? config->getAlarmHour() : 7;
  int am = config ? config->getAlarmMinute() : 0;
  char hourStr[4], minStr[4];
  snprintf(hourStr, sizeof(hourStr), "%02d", ah);
  snprintf(minStr, sizeof(minStr), "%02d", am);

  // 3. Calculate Layout for Inline Controls: [ - ] HH [ + ] : [ - ] MM [ + ]
  // Reduced time size for better proportions
  int timeSize = 5; // Reduced from 7 to fit better

  M5.Display.setTextSize(timeSize);
  int digitW = M5.Display.textWidth("00"); // Approx width of 2 digits

  int btnW = 90;         // Slightly larger buttons
  int btnH = 80;         // Taller for better touch
  int spacing = 20;      // More spacing
  int groupSpacing = 50; // More space for colon

  // Group = Btn + Sp + Digits + Sp + Btn
  int groupW = btnW + spacing + digitW + spacing + btnW;
  int totalW = groupW * 2 + groupSpacing;

  int startX = x + (w - totalW) / 2;
  int rowY = y + 120;

  // --- HOUR GROUP ---
  int curX = startX;

  // Hour Minus
  drawButton(curX, rowY, btnW, btnH, "-");
  curX += btnW + spacing;

  // Hour Digits
  M5.Display.setTextSize(timeSize);
  M5.Display.setCursor(curX, rowY + (btnH - 40) / 2); // Better centering
  M5.Display.print(hourStr);
  curX += digitW + spacing;

  // Hour Plus
  drawButton(curX, rowY, btnW, btnH, "+");
  curX += btnW + groupSpacing / 2;

  // --- COLON ---
  M5.Display.setTextSize(timeSize);
  M5.Display.setCursor(curX - 10, rowY + (btnH - 40) / 2);
  M5.Display.print(":");
  curX += groupSpacing / 2;

  // --- MINUTE GROUP ---
  // Min Minus
  drawButton(curX, rowY, btnW, btnH, "-");
  curX += btnW + spacing;

  // Min Digits
  M5.Display.setTextSize(timeSize);
  M5.Display.setCursor(curX, rowY + (btnH - 40) / 2);
  M5.Display.print(minStr);
  curX += digitW + spacing;

  // Min Plus
  drawButton(curX, rowY, btnW, btnH, "+");

  // 4. Toggle (Centered)
  bool enabled = config ? config->getAlarmEnabled() : false;
  const char *toggleLabel = "Enable";
  int toggleW = 200;
  int toggleX = x + (w - toggleW) / 2 + 30;
  drawToggle(toggleX, y + 300, toggleLabel, enabled);
}

void UIManager::drawTimerContent(int x, int y, int w, int h) {
  // 1. Header
  M5.Display.setTextColor(COLOR_BLACK);
  M5.Display.setTextSize(3);
  const char *title = "Countdown Timer";
  int titleW = M5.Display.textWidth(title);
  M5.Display.setCursor(x + (w - titleW) / 2, y + 40);
  M5.Display.print(title);

  // 2. Timer Display
  int min = _timerRemainingSeconds / 60;
  int sec = _timerRemainingSeconds % 60;
  char timeStr[32];
  snprintf(timeStr, sizeof(timeStr), "%02d:%02d", min, sec);

  // Large, readable timer display
  M5.Display.setTextSize(6);
  int timeW = M5.Display.textWidth(timeStr);
  M5.Display.setCursor(x + (w - timeW) / 2, y + 100);
  M5.Display.print(timeStr);

  // 3. Controls (Centered Row) - Better sized and positioned
  int btnY = y + 230;
  int btnW = 140; // Wider to fit text better
  int btnH = 60;  // Taller for easier touch
  int spacing = 40;
  int totalBtnW = btnW * 2 + spacing;
  int startX = x + (w - totalBtnW) / 2;

  // Reset text size for buttons (was 6)
  M5.Display.setTextSize(3);

  drawButton(startX, btnY, btnW, btnH, _timerRunning ? "PAUSE" : "START");
  drawButton(startX + btnW + spacing, btnY, btnW, btnH, "RESET");

  // 4. Adjustments (Centered Row below) - 4 buttons
  int adjY = y + 320;
  int adjBtnW = 75; // Narrower to fit 4 buttons
  int adjBtnH = 50;
  int adjSpacing = 20;
  int totalAdjW = adjBtnW * 4 + adjSpacing * 3;
  int adjX = x + (w - totalAdjW) / 2;

  drawButton(adjX, adjY, adjBtnW, adjBtnH, "-5m");
  drawButton(adjX + adjBtnW + adjSpacing, adjY, adjBtnW, adjBtnH, "-1m");
  drawButton(adjX + (adjBtnW + adjSpacing) * 2, adjY, adjBtnW, adjBtnH, "+1m");
  drawButton(adjX + (adjBtnW + adjSpacing) * 3, adjY, adjBtnW, adjBtnH, "+5m");
}

void UIManager::checkAlarm() {
  unsigned long now = millis();

  // Timer Logic
  if (_timerRunning && (now - _timerLastTick >= 1000)) {
    _timerLastTick = now;
    if (_timerRemainingSeconds > 0) {
      _timerRemainingSeconds--;
      // Update UI if valid
      if (_currentScreen == ScreenID::CLOCK && _clockMode == ClockMode::TIMER) {
        // We might want to force partial redraw, but for now just mark dirty
        // _needsRefresh = true; // Don't spam refresh on eInk every second
        // unless fast mode
      }
    } else {
      _timerRunning = false;
      _timerRinging = true;
      _timerRingStart = now;
      _lastActivityTime = now; // Wake up logic
      forceRefresh();
    }
  }

  // Alarm Logic
  extern Config *config;
  if (config && config->getAlarmEnabled()) {
    int h, m, s;
    RTC::getTime(h, m, s);
    // Trigger at 00 seconds
    if (h == config->getAlarmHour() && m == config->getAlarmMinute() &&
        s == 0) {
      // Debounce: ensure we haven't already started ringing clearly recently
      if (!_alarmRinging && (now - _alarmRingStart > 65000)) {
        _alarmRinging = true;
        _alarmRingStart = now;
        _lastActivityTime = now;
        forceRefresh();
      }
    }
  }

  // Ringing Processing
  if (_alarmRinging || _timerRinging) {
    // BEEP MORE FREQUENTLY AND LOUDER - Make it annoying!
    // Beep 3 times per second instead of once
    unsigned long beepCycle = now % 1000;
    if (beepCycle < 100 || (beepCycle >= 250 && beepCycle < 350) ||
        (beepCycle >= 500 && beepCycle < 600)) {
      Buzzer::alarm(); // Use louder alarm() instead of click()
    }

    const char *label = _alarmRinging ? "ALARM!" : "TIME UP!";
    drawAlertScreen(label);

    // Auto Dismiss after 60s
    unsigned long start = _alarmRinging ? _alarmRingStart : _timerRingStart;
    if (now - start > 60000) {
      _alarmRinging = false;
      _timerRinging = false;
      forceRefresh(); // Clear alert
    }
  }
}

void UIManager::drawAlertScreen(const char *label) {
  // Simple Modal Overlay
  // We draw directly to framebuffer to override whatever is there
  // Center Box
  int boxW = 500;
  int boxH = 300;
  int x = (SCREEN_WIDTH - boxW) / 2;
  int y = (SCREEN_HEIGHT - boxH) / 2;

  M5.Display.fillRect(x, y, boxW, boxH, COLOR_WHITE);
  M5.Display.drawRect(x, y, boxW, boxH, COLOR_BLACK);
  M5.Display.drawRect(x + 5, y + 5, boxW - 10, boxH - 10, COLOR_BLACK);

  M5.Display.setTextSize(5);
  M5.Display.setTextColor(COLOR_BLACK);

  int textW = strlen(label) * 30; // Approx
  M5.Display.setCursor(x + (boxW - textW) / 2, y + 80);
  M5.Display.print(label);

  M5.Display.setTextSize(3);
  const char *sub = "Tap to Dismiss";
  int subW = strlen(sub) * 18;
  M5.Display.setCursor(x + (boxW - subW) / 2, y + 200);
  M5.Display.print(sub);
  M5.Display.display(); // Force update
}

void UIManager::handleClockTouch(int x, int y, TouchEvent event) {
  // Handle RELEASE events for reliable tap detection
  if (event != TouchEvent::RELEASE) {
    return;
  }

  Serial.printf("CLOCK TOUCH: %d, %d\n", x, y);

  // 1. Sidebar Handling (Left 160px)
  if (x < 160) {
    // Timer (Row 1)
    if (y > 60 && y < 130) {
      _clockMode = ClockMode::TIMER;
    }
    // Pomodoro (Row 2)
    else if (y > 140 && y < 210) {
      _clockMode = ClockMode::POMODORO;
    }

    Buzzer::click();
    _needsRefresh = true;
    _lastRefresh = 0;
    return;
  }

  // 2. Exit Button (Top Right)
  if (x > SCREEN_WIDTH - 80 && y < 60) {
    navigateTo(ScreenID::HOME);
    return;
  }

  // 3. Content Handling
  if (_clockMode == ClockMode::TIMER) {
    // Timer Controls - Match drawTimerContent layout exactly
    // Content area starts at x=160
    int contentX = 160;
    int contentW = SCREEN_WIDTH - 160;

    // Main control buttons (START/PAUSE and RESET)
    int btnY = 230;
    int btnW = 140;
    int btnH = 60;
    int spacing = 40;
    int totalBtnW = btnW * 2 + spacing;
    int startX = contentX + (contentW - totalBtnW) / 2;

    // START/PAUSE button zone
    if (y >= btnY && y < btnY + btnH) {
      if (x >= startX && x < startX + btnW) {
        _timerRunning = !_timerRunning;
        if (_timerRunning)
          _timerLastTick = millis();
        Buzzer::click();
      }
      // RESET button zone
      else if (x >= startX + btnW + spacing &&
               x < startX + btnW + spacing + btnW) {
        _timerRunning = false;
        _timerRemainingSeconds = _timerDurationSeconds;
        Buzzer::click();
      }
    }

    // Adjustment buttons (-5m, -1m, +1m, +5m) - 4 buttons
    int adjY = 320;
    int adjBtnW = 75;
    int adjBtnH = 50;
    int adjSpacing = 20;
    int totalAdjW = adjBtnW * 4 + adjSpacing * 3;
    int adjX = contentX + (contentW - totalAdjW) / 2;

    if (y >= adjY && y < adjY + adjBtnH) {
      // -5m button
      if (x >= adjX && x < adjX + adjBtnW) {
        _timerRemainingSeconds -= 300;
        Buzzer::click();
      }
      // -1m button
      else if (x >= adjX + adjBtnW + adjSpacing &&
               x < adjX + (adjBtnW + adjSpacing) * 2) {
        _timerRemainingSeconds -= 60;
        Buzzer::click();
      }
      // +1m button
      else if (x >= adjX + (adjBtnW + adjSpacing) * 2 &&
               x < adjX + (adjBtnW + adjSpacing) * 3) {
        _timerRemainingSeconds += 60;
        Buzzer::click();
      }
      // +5m button
      else if (x >= adjX + (adjBtnW + adjSpacing) * 3 && x < adjX + totalAdjW) {
        _timerRemainingSeconds += 300;
        Buzzer::click();
      }

      if (_timerRemainingSeconds < 0)
        _timerRemainingSeconds = 0;
      _timerDurationSeconds = _timerRemainingSeconds;
    }
    _needsRefresh = true;
    _lastRefresh = 0; // Force immediate refresh
  } else if (_clockMode == ClockMode::POMODORO) {
    // Pomodoro Full Logic
    int contentX = 160;
    int contentW = SCREEN_WIDTH - 160;

    // Control buttons
    int btnW = 150;
    int btnH = 60;
    int btnY = 250;
    int btnSpacing = 40;

    // Start/Pause button
    int startBtnX = contentX + (contentW / 2) - btnW - (btnSpacing / 2);
    if (x >= startBtnX && x < startBtnX + btnW && y >= btnY &&
        y < btnY + btnH) {
      if (_pomodoroState == PomodoroState::RUNNING) {
        _pomodoroState = PomodoroState::PAUSED;
      } else if (_pomodoroState == PomodoroState::PAUSED ||
                 _pomodoroState == PomodoroState::STOPPED) {
        _pomodoroState = PomodoroState::RUNNING;
        _pomodoroLastTick = millis();
        Buzzer::click();
      } else if (_pomodoroState == PomodoroState::COMPLETED) {
        _pomodoroState = PomodoroState::RUNNING;
        _pomodoroRemainingSeconds = POMODORO_WORK_SECONDS;
        _pomodoroSession = PomodoroSession::WORK;
        _pomodoroLastTick = millis();
      }
      _needsRefresh = true;
      _lastRefresh = 0; // Force immediate refresh
      return;
    }

    // Reset button
    int resetBtnX = contentX + (contentW / 2) + (btnSpacing / 2);
    if (x >= resetBtnX && x < resetBtnX + btnW && y >= btnY &&
        y < btnY + btnH) {
      _pomodoroState = PomodoroState::STOPPED;
      switch (_pomodoroSession) {
      case PomodoroSession::WORK:
        _pomodoroRemainingSeconds = POMODORO_WORK_SECONDS;
        break;
      case PomodoroSession::SHORT_BREAK:
        _pomodoroRemainingSeconds = POMODORO_SHORT_BREAK_SECONDS;
        break;
      case PomodoroSession::LONG_BREAK:
        _pomodoroRemainingSeconds = POMODORO_LONG_BREAK_SECONDS;
        break;
      }
      _needsRefresh = true;
      _lastRefresh = 0; // Force immediate refresh
      return;
    }

    // Session type selection buttons
    int modeY = 350;
    int modeBtnW = 100;
    int modeSpacing = 20;
    int modeStartX =
        contentX + (contentW - (3 * modeBtnW + 2 * modeSpacing)) / 2;

    if (y >= modeY && y < modeY + 50) {
      // WORK
      if (x >= modeStartX && x < modeStartX + modeBtnW) {
        _pomodoroSession = PomodoroSession::WORK;
        _pomodoroRemainingSeconds = POMODORO_WORK_SECONDS;
        _pomodoroState = PomodoroState::STOPPED;
        Buzzer::click();
        _needsRefresh = true;
        _lastRefresh = 0; // Force immediate refresh
        return;
      }
      // SHORT
      int shortBtnX = modeStartX + modeBtnW + modeSpacing;
      if (x >= shortBtnX && x < shortBtnX + modeBtnW) {
        _pomodoroSession = PomodoroSession::SHORT_BREAK;
        _pomodoroRemainingSeconds = POMODORO_SHORT_BREAK_SECONDS;
        _pomodoroState = PomodoroState::STOPPED;
        Buzzer::click();
        _needsRefresh = true;
        _lastRefresh = 0; // Force immediate refresh
        return;
      }
      // LONG
      int longBtnX = modeStartX + 2 * (modeBtnW + modeSpacing);
      if (x >= longBtnX && x < longBtnX + modeBtnW) {
        _pomodoroSession = PomodoroSession::LONG_BREAK;
        _pomodoroRemainingSeconds = POMODORO_LONG_BREAK_SECONDS;
        _pomodoroState = PomodoroState::STOPPED;
        Buzzer::click();
        _needsRefresh = true;
        _lastRefresh = 0; // Force immediate refresh
        return;
      }
    }
  }
}
// ============================================================================
// Games Menu
// ============================================================================

void UIManager::drawGamesMenu() {
  M5.Display.fillScreen(COLOR_WHITE);
  drawMenuBar();

  // Title
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(COLOR_BLACK);
  M5.Display.setCursor(SCREEN_WIDTH / 2 - 80, 30);
  M5.Display.print("GAMES");

  // Game grid (2x2)
  int gridX = 200;
  int gridY = 120;
  int btnW = 240;
  int btnH = 150;
  int gap = 80;

  // Row 1
  // 2048 Button
  drawButton(gridX, gridY, btnW, btnH, "2048", true);

  // Sudoku Button
  drawButton(gridX + btnW + gap, gridY, btnW, btnH, "SUDOKU", true);

  // Row 2
  // Minesweeper Button (Moved to left)
  drawButton(gridX, gridY + btnH + 50, btnW, btnH, "MINESWEEPER", true);
}

void UIManager::handleGamesMenuTouch(int x, int y) {
  int gridX = 200;
  int gridY = 120;
  int btnW = 240;
  int btnH = 150;
  int gap = 80;

  // 2048 button
  if (x >= gridX && x < gridX + btnW && y >= gridY && y < gridY + btnH) {
    Buzzer::click();
    game2048Load(); // Load saved game or init new
    // Full quality clear to remove ghosting
    M5.Display.setEpdMode(epd_mode_t::epd_quality);
    M5.Display.fillScreen(COLOR_WHITE);
    M5.Display.display();
    navigateTo(ScreenID::GAME_2048);
    return;
  }

  // Sudoku button
  if (x >= gridX + btnW + gap && x < gridX + 2 * btnW + gap && y >= gridY &&
      y < gridY + btnH) {
    Buzzer::click();
    sudokuInit();
    M5.Display.setEpdMode(epd_mode_t::epd_quality);
    M5.Display.fillScreen(COLOR_WHITE);
    M5.Display.display();
    navigateTo(ScreenID::GAME_SUDOKU);
    return;
  }

  // Minesweeper button (Moved to left)
  if (x >= gridX && x < gridX + btnW && y >= gridY + btnH + 50 &&
      y < gridY + btnH + 50 + btnH) {
    Buzzer::click();
    minesweeperInit();
    M5.Display.setEpdMode(epd_mode_t::epd_quality);
    M5.Display.fillScreen(COLOR_WHITE);
    M5.Display.display();
    navigateTo(ScreenID::GAME_MINESWEEPER);
    return;
  }
}

// ============================================================================
// 2048 Game
// ============================================================================

void UIManager::drawGame2048() {
  // Use fast EPD mode for smooth updates (like Notes)
  M5.Display.setEpdMode(epd_mode_t::epd_fastest);

  M5.Display.fillScreen(COLOR_WHITE);

  // Header
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(COLOR_BLACK);
  M5.Display.setCursor(20, 15);
  M5.Display.print("2048");

  // Score display
  M5.Display.setTextSize(1);
  M5.Display.setCursor(SCREEN_WIDTH - 350, 20);
  M5.Display.printf("Score: %d", _game2048Score);
  M5.Display.setCursor(SCREEN_WIDTH - 180, 20);
  M5.Display.printf("Best: %d", _game2048HighScore);

  // Draw grid (4x4)
  int gridSize = 400;
  int gridX = (SCREEN_WIDTH - gridSize) / 2;
  int gridY = 80;
  int tileSize = 95;
  int gap = 5;

  for (int row = 0; row < 4; row++) {
    for (int col = 0; col < 4; col++) {
      int x = gridX + col * (tileSize + gap);
      int y = gridY + row * (tileSize + gap);
      int value = _game2048Grid[row][col];

      // Tile background (different shades for different values)
      uint16_t bgColor;
      if (value == 0)
        bgColor = COLOR_LIGHT_GRAY;
      else if (value == 2)
        bgColor = COLOR_WHITE;
      else if (value == 4)
        bgColor = 0xEF7D; // Very light gray
      else if (value <= 16)
        bgColor = 0xDEFB;
      else if (value <= 64)
        bgColor = 0xCE79;
      else
        bgColor = COLOR_GRAY;

      M5.Display.fillRect(x, y, tileSize, tileSize, bgColor);
      M5.Display.drawRect(x, y, tileSize, tileSize, COLOR_BLACK);

      // Tile value
      if (value > 0) {
        M5.Display.setTextSize(value >= 1000 ? 1 : 2); // Larger font (was 3/2)
        M5.Display.setTextColor(COLOR_BLACK); // All numbers black for e-ink

        char valueStr[8];
        snprintf(valueStr, sizeof(valueStr), "%d", value);
        int textW = M5.Display.textWidth(valueStr);
        M5.Display.setCursor(x + (tileSize - textW) / 2,
                             y + (value >= 1000 ? 35 : 28)); // Adjusted Y
        M5.Display.print(valueStr);
      }
    }
  }

  // Buttons (above menu bar)
  int btnY = SCREEN_HEIGHT - 140;
  drawButton(60, btnY, 180, 50, "NEW GAME"); // Wider button
  drawButton(SCREEN_WIDTH - 210, btnY, 150, 50, "HOME");

  // Game over overlay
  if (_game2048GameOver) {
    int boxW = 400;
    int boxH = 200;
    int boxX = (SCREEN_WIDTH - boxW) / 2;
    int boxY = (SCREEN_HEIGHT - boxH) / 2;

    M5.Display.fillRect(boxX, boxY, boxW, boxH, COLOR_WHITE);
    M5.Display.drawRect(boxX, boxY, boxW, boxH, COLOR_BLACK);
    M5.Display.drawRect(boxX + 5, boxY + 5, boxW - 10, boxH - 10, COLOR_BLACK);

    M5.Display.setTextSize(_game2048Won ? 2 : 2);
    M5.Display.setTextColor(COLOR_BLACK);
    const char *msg = _game2048Won ? "YOU WON!" : "GAME OVER!";
    int msgW = M5.Display.textWidth(msg);
    M5.Display.setCursor(boxX + (boxW - msgW) / 2, boxY + 60);
    M5.Display.print(msg);

    M5.Display.setTextSize(1);
    M5.Display.setCursor(boxX + 80, boxY + 130);
    M5.Display.print("Tap to continue");
  }
}

void UIManager::handleGame2048Touch(int x, int y, TouchEvent event) {
  if (event == TouchEvent::PRESS) {
    _touchStartX = x;
    _touchStartY = y;
    return;
  }

  if (event != TouchEvent::RELEASE)
    return;

  // Game over - any tap restarts
  if (_game2048GameOver) {
    game2048Init();
    _needsRefresh = true;
    _lastRefresh = 0;
    return;
  }

  // Check buttons (above menu bar)
  int btnY = SCREEN_HEIGHT - 140;
  if (y >= btnY && y < btnY + 50) {
    if (x >= 60 && x < 240) { // Wider touch zone for NEW GAME
      // New Game - do full quality refresh to clear ghosting
      Buzzer::click();
      M5.Display.setEpdMode(epd_mode_t::epd_quality);
      game2048Init();
      _needsRefresh = true;
      _lastRefresh = 0;
      return;
    }
    if (x >= SCREEN_WIDTH - 210 && x < SCREEN_WIDTH - 60) {
      // Home
      Buzzer::click();
      game2048Save();
      M5.Display.fillScreen(COLOR_WHITE); // Clear ghosting
      navigateTo(ScreenID::HOME);
      return;
    }
  }

  // Detect swipe
  int dx = x - _touchStartX;
  int dy = y - _touchStartY;

  if (abs(dx) < 50 && abs(dy) < 50)
    return; // Not a swipe

  int direction;
  if (abs(dx) > abs(dy)) {
    direction = (dx > 0) ? 1 : 3; // Right or Left
  } else {
    direction = (dy > 0) ? 2 : 0; // Down or Up
  }

  bool moved = game2048Slide(direction);
  if (moved) {
    game2048AddRandomTile();
    game2048Save();

    if (_game2048Score > _game2048HighScore) {
      _game2048HighScore = _game2048Score;
    }

    // Check win/lose
    if (!_game2048Won) {
      for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
          if (_game2048Grid[r][c] >= 2048) {
            _game2048Won = true;
            _game2048GameOver = true;
          }
        }
      }
    }

    if (game2048IsGameOver()) {
      _game2048GameOver = true;
    }

    Buzzer::click();
    _needsRefresh = true;
    _lastRefresh = 0;
  }
}

// ============================================================================
// 2048 Game Logic
// ============================================================================

void UIManager::game2048Init() {
  // Clear grid
  for (int r = 0; r < 4; r++) {
    for (int c = 0; c < 4; c++) {
      _game2048Grid[r][c] = 0;
    }
  }

  _game2048Score = 0;
  _game2048GameOver = false;
  _game2048Won = false;

  // Add 2 starting tiles
  game2048AddRandomTile();
  game2048AddRandomTile();
}

void UIManager::game2048AddRandomTile() {
  // Find empty cells
  int emptyCount = 0;
  for (int r = 0; r < 4; r++) {
    for (int c = 0; c < 4; c++) {
      if (_game2048Grid[r][c] == 0)
        emptyCount++;
    }
  }

  if (emptyCount == 0)
    return;

  // Pick random empty cell
  int targetIndex = random(emptyCount);
  int currentIndex = 0;

  for (int r = 0; r < 4; r++) {
    for (int c = 0; c < 4; c++) {
      if (_game2048Grid[r][c] == 0) {
        if (currentIndex == targetIndex) {
          // 90% chance of 2, 10% chance of 4
          _game2048Grid[r][c] = (random(10) < 9) ? 2 : 4;
          return;
        }
        currentIndex++;
      }
    }
  }
}

bool UIManager::game2048Slide(int direction) {
  bool moved = false;

  // Correct compress and merge algorithm
  auto compressAndMerge = [&](int values[4]) -> bool {
    bool changed = false;
    int result[4] = {0, 0, 0, 0};
    int pos = 0;

    // Process tiles from start to end
    for (int i = 0; i < 4; i++) {
      if (values[i] == 0)
        continue; // Skip empty tiles

      if (pos > 0 && result[pos - 1] == values[i] && result[pos - 1] > 0) {
        // Can merge with previous tile (and it hasn't been merged yet this
        // turn)
        result[pos - 1] *= 2;
        _game2048Score += result[pos - 1];
        result[pos - 1] = -result[pos - 1]; // Mark as merged (negative)
        changed = true;
      } else {
        // Can't merge, add as new tile
        result[pos] = values[i];
        pos++;
      }
    }

    // Convert negative (merged) values back to positive
    for (int i = 0; i < 4; i++) {
      if (result[i] < 0)
        result[i] = -result[i];
      if (values[i] != result[i])
        changed = true;
      values[i] = result[i];
    }

    return changed;
  };

  if (direction == 0 || direction == 2) { // Up or Down
    for (int c = 0; c < 4; c++) {
      int column[4];

      // Extract column
      if (direction == 0) { // Up
        for (int r = 0; r < 4; r++)
          column[r] = _game2048Grid[r][c];
      } else { // Down
        for (int r = 0; r < 4; r++)
          column[r] = _game2048Grid[3 - r][c];
      }

      if (compressAndMerge(column))
        moved = true;

      // Write back
      if (direction == 0) {
        for (int r = 0; r < 4; r++)
          _game2048Grid[r][c] = column[r];
      } else {
        for (int r = 0; r < 4; r++)
          _game2048Grid[3 - r][c] = column[r];
      }
    }
  } else { // Left or Right
    for (int r = 0; r < 4; r++) {
      int row[4];

      // Extract row
      if (direction == 3) { // Left
        for (int c = 0; c < 4; c++)
          row[c] = _game2048Grid[r][c];
      } else { // Right
        for (int c = 0; c < 4; c++)
          row[c] = _game2048Grid[r][3 - c];
      }

      if (compressAndMerge(row))
        moved = true;

      // Write back
      if (direction == 3) {
        for (int c = 0; c < 4; c++)
          _game2048Grid[r][c] = row[c];
      } else {
        for (int c = 0; c < 4; c++)
          _game2048Grid[r][3 - c] = row[c];
      }
    }
  }

  return moved;
}

bool UIManager::game2048IsGameOver() {
  // Check for empty cells
  for (int r = 0; r < 4; r++) {
    for (int c = 0; c < 4; c++) {
      if (_game2048Grid[r][c] == 0)
        return false;
    }
  }

  // Check for possible merges
  for (int r = 0; r < 4; r++) {
    for (int c = 0; c < 4; c++) {
      int val = _game2048Grid[r][c];
      if (c < 3 && _game2048Grid[r][c + 1] == val)
        return false;
      if (r < 3 && _game2048Grid[r + 1][c] == val)
        return false;
    }
  }

  return true;
}

void UIManager::game2048Save() {
  extern SDManager *sdManager;
  if (!sdManager || !sdManager->isAvailable())
    return;

  File file = SD.open("/games/2048_save.txt", FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open 2048 save file");
    return;
  }

  // Write grid
  for (int r = 0; r < 4; r++) {
    for (int c = 0; c < 4; c++) {
      file.printf("%d", _game2048Grid[r][c]);
      if (c < 3)
        file.print(",");
    }
    file.println();
  }

  file.printf("%d\n", _game2048Score);
  file.printf("%d\n", _game2048HighScore);
  file.printf("%d\n", _game2048GameOver ? 1 : 0);
  file.printf("%d\n", _game2048Won ? 1 : 0);

  file.close();
  Serial.println("2048 game saved");
}

void UIManager::game2048Load() {
  extern SDManager *sdManager;
  if (!sdManager || !sdManager->isAvailable()) {
    game2048Init();
    return;
  }

  File file = SD.open("/games/2048_save.txt", FILE_READ);
  if (!file) {
    Serial.println("No save file, starting new game");
    game2048Init();
    return;
  }

  // Read grid
  for (int r = 0; r < 4; r++) {
    String line = file.readStringUntil('\n');
    int colIdx = 0;
    int startIdx = 0;
    for (int c = 0; c < 4; c++) {
      int commaIdx = line.indexOf(',', startIdx);
      if (commaIdx == -1)
        commaIdx = line.length();
      String valStr = line.substring(startIdx, commaIdx);
      _game2048Grid[r][c] = valStr.toInt();
      startIdx = commaIdx + 1;
    }
  }

  _game2048Score = file.readStringUntil('\n').toInt();
  _game2048HighScore = file.readStringUntil('\n').toInt();
  _game2048GameOver = file.readStringUntil('\n').toInt() == 1;
  _game2048Won = file.readStringUntil('\n').toInt() == 1;

  // Validate loaded values (protect against corrupted save file)
  if (_game2048Score < 0)
    _game2048Score = 0;
  if (_game2048HighScore < 0)
    _game2048HighScore = 0;

  file.close();
  Serial.println("2048 game loaded");
}
// ============================================================================
// SUDOKU GAME (6x6) - Optimized for M5Paper S3
// ============================================================================

// Hardcoded 6x6 Sudoku Puzzles (3 per difficulty = 9 total)
// Format: 0 = empty, 1-6 = given numbers

// EASY PUZZLES (More given numbers)
const byte sudoku_easy_puzzles[3][6][6] = {
    // Easy #1 (Strictly Valid)
    {{1, 2, 0, 4, 5, 0},
     {0, 5, 6, 1, 0, 3},
     {2, 0, 1, 5, 0, 4},
     {5, 6, 4, 0, 3, 1},
     {0, 1, 2, 6, 0, 0},
     {6, 0, 5, 3, 1, 2}},

    // Easy #2 (Strictly Valid)
    {{3, 0, 1, 6, 0, 4},
     {6, 5, 0, 3, 2, 1},
     {0, 3, 2, 0, 6, 0},
     {4, 6, 0, 1, 3, 2},
     {2, 1, 3, 0, 4, 6},
     {5, 0, 6, 2, 0, 3}},

    // Easy #3 (Strictly Valid)
    {{1, 0, 3, 4, 0, 6},
     {4, 5, 0, 0, 2, 3},
     {3, 1, 2, 6, 4, 0},
     {0, 4, 5, 3, 1, 2},
     {2, 3, 0, 0, 6, 4},
     {5, 0, 4, 2, 0, 1}}};

const byte sudoku_easy_solutions[3][6][6] = {{{1, 2, 3, 4, 5, 6},
                                              {4, 5, 6, 1, 2, 3},
                                              {2, 3, 1, 5, 6, 4},
                                              {5, 6, 4, 2, 3, 1},
                                              {3, 1, 2, 6, 4, 5},
                                              {6, 4, 5, 3, 1, 2}},

                                             {{3, 2, 1, 6, 5, 4},
                                              {6, 5, 4, 3, 2, 1},
                                              {1, 3, 2, 4, 6, 5},
                                              {4, 6, 5, 1, 3, 2},
                                              {2, 1, 3, 5, 4, 6},
                                              {5, 4, 6, 2, 1, 3}},

                                             {{1, 2, 3, 4, 5, 6},
                                              {4, 5, 6, 1, 2, 3},
                                              {3, 1, 2, 6, 4, 5},
                                              {6, 4, 5, 3, 1, 2},
                                              {2, 3, 1, 5, 6, 4},
                                              {5, 6, 4, 2, 3, 1}}};

// MEDIUM PUZZLES
const byte sudoku_medium_puzzles[3][6][6] = {
    // Medium #1
    {{0, 2, 0, 0, 5, 0},
     {0, 0, 6, 1, 0, 0},
     {2, 0, 0, 0, 0, 4},
     {5, 0, 0, 0, 0, 1},
     {0, 0, 2, 6, 0, 0},
     {0, 4, 0, 0, 1, 0}},

    // Medium #2
    {{0, 0, 1, 6, 0, 0},
     {0, 5, 0, 0, 2, 0},
     {1, 0, 0, 0, 0, 5},
     {4, 0, 0, 0, 0, 2},
     {0, 1, 0, 0, 4, 0},
     {0, 0, 6, 2, 0, 0}},

    // Medium #3
    {{0, 0, 3, 4, 0, 0},
     {4, 0, 0, 0, 0, 3},
     {0, 1, 0, 0, 4, 0},
     {0, 4, 0, 0, 1, 0},
     {2, 0, 0, 0, 0, 4},
     {0, 0, 4, 2, 0, 0}}};

const byte sudoku_medium_solutions[3][6][6] = {{{1, 2, 3, 4, 5, 6},
                                                {4, 5, 6, 1, 2, 3},
                                                {2, 3, 1, 5, 6, 4},
                                                {5, 6, 4, 2, 3, 1},
                                                {3, 1, 2, 6, 4, 5},
                                                {6, 4, 5, 3, 1, 2}},

                                               {{3, 2, 1, 6, 5, 4},
                                                {6, 5, 4, 3, 2, 1},
                                                {1, 3, 2, 4, 6, 5},
                                                {4, 6, 5, 1, 3, 2},
                                                {2, 1, 3, 5, 4, 6},
                                                {5, 4, 6, 2, 1, 3}},

                                               {{1, 2, 3, 4, 5, 6},
                                                {4, 5, 6, 1, 2, 3},
                                                {3, 1, 2, 6, 4, 5},
                                                {6, 4, 5, 3, 1, 2},
                                                {2, 3, 1, 5, 6, 4},
                                                {5, 6, 4, 2, 3, 1}}};

// HARD PUZZLES
const byte sudoku_hard_puzzles[3][6][6] = {
    // Hard #1
    {{0, 0, 0, 0, 5, 0},
     {0, 5, 0, 0, 0, 0},
     {0, 0, 0, 0, 0, 4},
     {5, 0, 0, 0, 0, 0},
     {0, 0, 0, 0, 4, 0},
     {0, 4, 0, 0, 0, 0}},

    // Hard #2
    {{0, 0, 0, 6, 0, 0},
     {0, 5, 0, 0, 0, 0},
     {0, 0, 0, 0, 0, 5},
     {4, 0, 0, 0, 0, 0},
     {0, 0, 0, 0, 4, 0},
     {0, 0, 6, 0, 0, 0}},

    // Hard #3
    {{0, 0, 0, 4, 0, 0},
     {0, 5, 0, 0, 0, 0},
     {0, 0, 0, 0, 0, 0},
     {0, 0, 0, 0, 0, 0},
     {0, 0, 0, 0, 6, 0},
     {0, 0, 4, 0, 0, 0}}};

const byte sudoku_hard_solutions[3][6][6] = {{{1, 2, 3, 4, 5, 6},
                                              {4, 5, 6, 1, 2, 3},
                                              {2, 3, 1, 5, 6, 4},
                                              {5, 6, 4, 2, 3, 1},
                                              {3, 1, 2, 6, 4, 5},
                                              {6, 4, 5, 3, 1, 2}},

                                             {{3, 2, 1, 6, 5, 4},
                                              {6, 5, 4, 3, 2, 1},
                                              {1, 3, 2, 4, 6, 5},
                                              {4, 6, 5, 1, 3, 2},
                                              {2, 1, 3, 5, 4, 6},
                                              {5, 4, 6, 2, 1, 3}},

                                             {{1, 2, 3, 4, 5, 6},
                                              {4, 5, 6, 1, 2, 3},
                                              {3, 1, 2, 6, 4, 5},
                                              {6, 4, 5, 3, 1, 2},
                                              {2, 3, 1, 5, 6, 4},
                                              {5, 6, 4, 2, 3, 1}}};

// Initialize Sudoku game
void UIManager::sudokuInit() {
  _sudokuSelectedRow = -1;
  _sudokuSelectedCol = -1;
  _sudokuPuzzleNum = 1;
  _sudokuDifficulty = 0;      // Start with Easy
  _sudokuShowConfirm = false; // No confirmation dialog
  sudokuLoadPuzzle(_sudokuDifficulty, _sudokuPuzzleNum);
}

// Load a puzzle
void UIManager::sudokuLoadPuzzle(byte difficulty, byte num) {
  _sudokuDifficulty = difficulty;
  _sudokuPuzzleNum = num;

  const byte(*puzzle)[6][6];
  const byte(*solution)[6][6];

  // Select difficulty
  if (difficulty == 0) { // Easy
    puzzle = &sudoku_easy_puzzles[num - 1];
    solution = &sudoku_easy_solutions[num - 1];
  } else if (difficulty == 1) { // Medium
    puzzle = &sudoku_medium_puzzles[num - 1];
    solution = &sudoku_medium_solutions[num - 1];
  } else { // Hard
    puzzle = &sudoku_hard_puzzles[num - 1];
    solution = &sudoku_hard_solutions[num - 1];
  }

  // Copy to grids
  for (int r = 0; r < 6; r++) {
    for (int c = 0; c < 6; c++) {
      _sudokuGrid[r][c] = (*puzzle)[r][c];
      _sudokuSolution[r][c] = (*solution)[r][c];
      _sudokuGiven[r][c] = ((*puzzle)[r][c] != 0);
    }
  }

  _sudokuSelectedRow = -1;
  _sudokuSelectedCol = -1;
}

// Load a random puzzle for the given difficulty
void UIManager::sudokuLoadRandomPuzzle(byte difficulty) {
  // Generate random puzzle number (1 to 3 for now)
  byte num = (random(3)) + 1; // random(3) gives 0-2, +1 for 1-3
  sudokuLoadPuzzle(difficulty, num);
}

// Validate a cell (check row/col/block for duplicates)
bool UIManager::sudokuValidateCell(byte row, byte col) {
  byte val = _sudokuGrid[row][col];
  if (val == 0)
    return true; //  Empty is valid

  // Check row
  for (int c = 0; c < 6; c++) {
    if (c != col && _sudokuGrid[row][c] == val)
      return false;
  }

  // Check column
  for (int r = 0; r < 6; r++) {
    if (r != row && _sudokuGrid[r][col] == val)
      return false;
  }

  // Check 2x3 block
  int blockRow = (row / 2) * 2;
  int blockCol = (col / 3) * 3;
  for (int r = blockRow; r < blockRow + 2; r++) {
    for (int c = blockCol; c < blockCol + 3; c++) {
      if (r != row || c != col) {
        if (_sudokuGrid[r][c] == val)
          return false;
      }
    }
  }

  return true;
}

// Check if puzzle is solved
bool UIManager::sudokuCheckWin() {
  for (int r = 0; r < 6; r++) {
    for (int c = 0; c < 6; c++) {
      if (_sudokuGrid[r][c] == 0)
        return false; // Empty cell
      if (!sudokuValidateCell(r, c))
        return false; // Invalid
    }
  }
  return true;
}

// Clear selected cell
void UIManager::sudokuClearCell() {
  if (_sudokuSelectedRow >= 0 && _sudokuSelectedCol >= 0) {
    if (!_sudokuGiven[_sudokuSelectedRow][_sudokuSelectedCol]) {
      _sudokuGrid[_sudokuSelectedRow][_sudokuSelectedCol] = 0;
    }
  }
}

// Draw Sudoku game - ENHANCED with difficulty selector and better layout
void UIManager::drawSudokuGame() {
  M5.Display.setEpdMode(epd_mode_t::epd_fastest);
  M5.Display.fillScreen(COLOR_WHITE);

  // Layout constants
  const int GRID_X = 30;
  const int GRID_Y = 80; // Moved down to make room for difficulty buttons
  const int CELL_SIZE = 75;
  const int BTN_X = 520;

  // Title with puzzle number
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(COLOR_BLACK);
  M5.Display.setCursor(30, 15);
  const char *diff = (_sudokuDifficulty == 0)   ? "Easy"
                     : (_sudokuDifficulty == 1) ? "Med"
                                                : "Hard";
  M5.Display.printf("SUDOKU  #%d/3 (%s)", _sudokuPuzzleNum, diff);

  // HOME button (top right)
  drawButton(850, 10, 100, 40, "HOME");

  // ===== DIFFICULTY SELECTOR (top right, below title) =====
  int diffY = 15;
  int diffBtnW = 80;
  int diffGap = 5;

  // Draw difficulty buttons with highlight for current selection
  bool isEasy = (_sudokuDifficulty == 0);
  bool isMed = (_sudokuDifficulty == 1);
  bool isHard = (_sudokuDifficulty == 2);

  drawButton(BTN_X, diffY, diffBtnW, 40, "EASY", isEasy);
  drawButton(BTN_X + diffBtnW + diffGap, diffY, diffBtnW, 40, "MED", isMed);
  drawButton(BTN_X + 2 * (diffBtnW + diffGap), diffY, diffBtnW, 40, "HARD",
             isHard);

  // Draw grid (6x6, 75px cells)
  for (int i = 0; i <= 6; i++) {
    bool thickH = (i == 0 || i == 2 || i == 4 || i == 6);
    bool thickV = (i == 0 || i == 3 || i == 6);

    for (int t = 0; t < (thickH ? 3 : 1); t++) {
      M5.Display.drawLine(GRID_X, GRID_Y + i * CELL_SIZE + t,
                          GRID_X + 6 * CELL_SIZE, GRID_Y + i * CELL_SIZE + t,
                          COLOR_BLACK);
    }
    for (int t = 0; t < (thickV ? 3 : 1); t++) {
      M5.Display.drawLine(GRID_X + i * CELL_SIZE + t, GRID_Y,
                          GRID_X + i * CELL_SIZE + t, GRID_Y + 6 * CELL_SIZE,
                          COLOR_BLACK);
    }
  }

  // Draw numbers - ALL BLACK for better e-ink visibility
  M5.Display.setTextSize(2);
  for (int r = 0; r < 6; r++) {
    for (int c = 0; c < 6; c++) {
      if (_sudokuGrid[r][c] != 0) {
        int color = COLOR_BLACK; // All numbers black
        if (!sudokuValidateCell(r, c))
          color = 0xF800; // RED for errors

        M5.Display.setTextColor(color);
        M5.Display.setCursor(GRID_X + c * CELL_SIZE + 22,
                             GRID_Y + r * CELL_SIZE + 18);
        M5.Display.print(_sudokuGrid[r][c]);
      }
    }
  }

  // Highlight selected cell
  if (_sudokuSelectedRow >= 0) {
    int x = GRID_X + _sudokuSelectedCol * CELL_SIZE;
    int y = GRID_Y + _sudokuSelectedRow * CELL_SIZE;
    for (int t = 0; t < 3; t++) {
      M5.Display.drawRect(x + 2 + t, y + 2 + t, CELL_SIZE - 4 - 2 * t,
                          CELL_SIZE - 4 - 2 * t, 0x001F);
    }
  }

  // ===== RIGHT SIDE CONTROLS =====

  // Number buttons (1-6) in 2 rows of 3
  int numY = 80;
  int numBtnW = 85;
  int numBtnH = 70;
  int numGap = 5;

  M5.Display.setTextSize(2);
  for (int i = 0; i < 6; i++) {
    int row = i / 3;
    int col = i % 3;
    int bx = BTN_X + col * (numBtnW + numGap);
    int by = numY + row * (numBtnH + numGap);
    char label[2] = {(char)('1' + i), '\0'};
    drawButton(bx, by, numBtnW, numBtnH, label);
  }

  // Control buttons at BOTTOM of right panel (moved down significantly)
  int ctrlY = 380; // Bottom of screen area
  int ctrlBtnW = 130;

  M5.Display.setTextSize(1);
  drawButton(BTN_X, ctrlY, ctrlBtnW, 50, "CLEAR");
  drawButton(BTN_X + ctrlBtnW + 10, ctrlY, ctrlBtnW, 50, "CHECK");

  // NEW PUZZLE button (full width) at very bottom
  int newY = ctrlY + 65;
  drawButton(BTN_X, newY, 2 * ctrlBtnW + 10, 50, "NEW PUZZLE");

  // Confirmation dialog overlay (if active)
  if (_sudokuShowConfirm) {
    // Semi-transparent background effect (just a white box for e-ink)
    M5.Display.fillRoundRect(150, 200, 350, 160, 10, COLOR_WHITE);
    M5.Display.drawRoundRect(150, 200, 350, 160, 10, COLOR_BLACK);
    M5.Display.drawRoundRect(152, 202, 346, 156, 10, COLOR_BLACK);

    M5.Display.setTextSize(1);
    M5.Display.setTextColor(COLOR_BLACK);
    M5.Display.setCursor(180, 220);
    M5.Display.print("Start new puzzle?");
    M5.Display.setCursor(170, 250);
    M5.Display.print("Progress will be lost");

    // YES / NO buttons
    drawButton(180, 300, 110, 45, "YES");
    drawButton(310, 300, 110, 45, "NO");
  }
}

// Handle Sudoku touch - ENHANCED
void UIManager::handleSudokuTouch(int x, int y, TouchEvent event) {
  if (event != TouchEvent::RELEASE)
    return;

  const int GRID_X = 30;
  const int GRID_Y = 80;
  const int CELL_SIZE = 75;
  const int BTN_X = 520;

  Buzzer::click();

  // Handle confirmation dialog FIRST (blocks all other input)
  if (_sudokuShowConfirm) {
    // YES button (180-290, 300-345)
    if (x >= 180 && x < 290 && y >= 300 && y < 345) {
      _sudokuShowConfirm = false;
      sudokuLoadRandomPuzzle(
          _sudokuDifficulty); // Load random puzzle of current difficulty
      M5.Display.setEpdMode(epd_mode_t::epd_quality);
      _needsRefresh = true;
      _lastRefresh = 0;
      return;
    }
    // NO button (310-420, 300-345)
    if (x >= 310 && x < 420 && y >= 300 && y < 345) {
      _sudokuShowConfirm = false;
      _needsRefresh = true;
      _lastRefresh = 0;
      return;
    }
    return; // Block all other touches when dialog is shown
  }

  // HOME button (850-950, 10-50)
  if (x >= 850 && x < 950 && y >= 10 && y < 50) {
    M5.Display.fillScreen(COLOR_WHITE); // Clear ghosting
    navigateTo(ScreenID::HOME);
    return;
  }

  // Difficulty selector buttons (top right, below title)
  int diffY = 15;
  int diffBtnW = 80;
  int diffGap = 5;

  if (y >= diffY && y < diffY + 40) {
    // EASY button
    if (x >= BTN_X && x < BTN_X + diffBtnW) {
      if (_sudokuDifficulty != 0) { // Only reload if changing
        _sudokuDifficulty = 0;
        sudokuLoadRandomPuzzle(0);
        M5.Display.setEpdMode(epd_mode_t::epd_quality);
        _needsRefresh = true;
        _lastRefresh = 0;
      }
      return;
    }
    // MED button
    if (x >= BTN_X + diffBtnW + diffGap && x < BTN_X + 2 * diffBtnW + diffGap) {
      if (_sudokuDifficulty != 1) {
        _sudokuDifficulty = 1;
        sudokuLoadRandomPuzzle(1);
        M5.Display.setEpdMode(epd_mode_t::epd_quality);
        _needsRefresh = true;
        _lastRefresh = 0;
      }
      return;
    }
    // HARD button
    if (x >= BTN_X + 2 * (diffBtnW + diffGap) &&
        x < BTN_X + 3 * diffBtnW + 2 * diffGap) {
      if (_sudokuDifficulty != 2) {
        _sudokuDifficulty = 2;
        sudokuLoadRandomPuzzle(2);
        M5.Display.setEpdMode(epd_mode_t::epd_quality);
        _needsRefresh = true;
        _lastRefresh = 0;
      }
      return;
    }
  }

  // Grid cell selection (30-480 x, 80-530 y)
  if (x >= GRID_X && x < GRID_X + 6 * CELL_SIZE && y >= GRID_Y &&
      y < GRID_Y + 6 * CELL_SIZE) {
    int row = (y - GRID_Y) / CELL_SIZE;
    int col = (x - GRID_X) / CELL_SIZE;
    if (row >= 0 && row < 6 && col >= 0 && col < 6 && !_sudokuGiven[row][col]) {
      _sudokuSelectedRow = row;
      _sudokuSelectedCol = col;
      _needsRefresh = true;
      _lastRefresh = 0;
    }
    return;
  }

  // Number buttons (1-6) in 2 rows of 3
  int numY = 80;
  int numBtnW = 85;
  int numBtnH = 70;
  int numGap = 5;

  for (int i = 0; i < 6; i++) {
    int row = i / 3;
    int col = i % 3;
    int bx = BTN_X + col * (numBtnW + numGap);
    int by = numY + row * (numBtnH + numGap);
    if (x >= bx && x < bx + numBtnW && y >= by && y < by + numBtnH) {
      if (_sudokuSelectedRow >= 0) {
        _sudokuGrid[_sudokuSelectedRow][_sudokuSelectedCol] = i + 1;
        _needsRefresh = true;
        _lastRefresh = 0;
      }
      return;
    }
  }

  // Control buttons at bottom
  int ctrlY = 380;
  int ctrlBtnW = 130;

  // CLEAR button
  if (x >= BTN_X && x < BTN_X + ctrlBtnW && y >= ctrlY && y < ctrlY + 50) {
    sudokuClearCell();
    M5.Display.setEpdMode(epd_mode_t::epd_quality);
    _needsRefresh = true;
    _lastRefresh = 0;
    return;
  }

  // CHECK button
  if (x >= BTN_X + ctrlBtnW + 10 && x < BTN_X + 2 * ctrlBtnW + 10 &&
      y >= ctrlY && y < ctrlY + 50) {
    if (sudokuCheckWin()) {
      M5.Display.fillRoundRect(150, 220, 250, 80, 10, COLOR_WHITE);
      M5.Display.drawRoundRect(150, 220, 250, 80, 10, COLOR_BLACK);
      M5.Display.setTextSize(2);
      M5.Display.setTextColor(0x07E0);
      M5.Display.setCursor(180, 250);
      M5.Display.print("SOLVED!");
      M5.Display.display();
      delay(2000);
    }
    _needsRefresh = true;
    _lastRefresh = 0;
    return;
  }

  // NEW PUZZLE button - show confirmation dialog
  int newY = ctrlY + 65;
  if (x >= BTN_X && x < BTN_X + 2 * ctrlBtnW + 10 && y >= newY &&
      y < newY + 50) {
    _sudokuShowConfirm = true; // Show confirmation dialog
    _needsRefresh = true;
    _lastRefresh = 0;
    return;
  }
}

// ============================================================================
// Power History Screen (Design C - Multi-Line 24H Graph)
// ============================================================================

void UIManager::drawHistoryScreen() {
  M5.Display.setEpdMode(epd_mode_t::epd_quality);
  M5.Display.fillScreen(COLOR_WHITE);

  // --- Header ---
  M5.Display.fillRect(0, 0, SCREEN_WIDTH, 60, COLOR_BLACK);
  M5.Display.setTextColor(COLOR_WHITE);
  M5.Display.setTextSize(2);

  // Get date string for viewing day
  time_t now = time(nullptr);
  time_t viewDay = now - (_historyViewDay * 86400);
  struct tm ti;
  localtime_r(&viewDay, &ti);
  char dateStr[32];
  strftime(dateStr, sizeof(dateStr), "HISTORY - %b %d, %Y", &ti);
  M5.Display.setCursor(20, 18);
  M5.Display.print(dateStr);

  // EXIT button (White X in top right)
  int cx = SCREEN_WIDTH - 40;
  int cy = 30;
  for (int i = -1; i <= 1; i++) {
    M5.Display.drawLine(cx - 15 + i, cy - 15, cx + 15 + i, cy + 15,
                        COLOR_WHITE);
    M5.Display.drawLine(cx + 15 + i, cy - 15, cx - 15 + i, cy + 15,
                        COLOR_WHITE);
  }

  // --- Data Analysis for Scaling ---
  uint16_t sampleCount = _powerHistory.getSampleCount(_historyViewDay);
  float maxW = 50.0f; // Minimum scale set to 50W

  // If only Battery is selected, force scale to 100
  if (_historyFilter == 0x01) {
    maxW = 100.0f;
  } else if (sampleCount > 0) {
    // Determine max value based on selected filters
    for (uint16_t i = 0; i < sampleCount; i++) {
      PowerSample s = _powerHistory.getSample(_historyViewDay, i);

      // Check Input if selected
      if ((_historyFilter & 0x02) && s.inputW > maxW)
        maxW = s.inputW;

      // Check Output if selected
      if ((_historyFilter & 0x04) && s.outputW > maxW)
        maxW = s.outputW;
    }
  }

  // Round maxW up to nice number (nearest 50)
  int graphMax = (int)ceil(maxW / 50.0) * 50;

  // Special case for Battery Only: Force exactly 100 if calculated max is close
  // (e.g. min 50 -> 100)
  if (_historyFilter == 0x01) {
    graphMax = 100;
  }

  // --- Graph Area (expanded) ---
  const int graphX = 80;
  const int graphY = 100; // Moved down 20px
  const int graphW = SCREEN_WIDTH - 120;
  const int graphH = 300; // Reduced from 420 to prevent overlap

  // Draw axes
  M5.Display.drawRect(graphX, graphY, graphW, graphH, COLOR_BLACK);

  // Y-axis labels (Watts or %) - Left side
  M5.Display.setTextColor(COLOR_BLACK);
  M5.Display.setTextSize(1);
  for (int i = 0; i <= 4; i++) {
    int yPos = graphY + graphH - (i * graphH / 4);
    int val = i * (graphMax / 4);
    M5.Display.setCursor(graphX - 60, yPos - 8);
    M5.Display.printf("%d", val);
    // Grid line
    if (i > 0 && i < 4) {
      for (int x = graphX; x < graphX + graphW; x += 10) {
        M5.Display.drawPixel(x, yPos, COLOR_LIGHT_GRAY);
      }
    }
  }
  M5.Display.setCursor(graphX - 60, graphY - 30);
  if (_historyFilter == 0x01) {
    M5.Display.print("Percent");
  } else {
    M5.Display.print("Watts");
  }

  // X-axis labels (larger, black text)
  const char *timeLabels[] = {"00", "04", "08", "12", "16", "20", "24"};
  for (int i = 0; i <= 6; i++) {
    int xPos = graphX + (i * graphW / 6);
    M5.Display.setCursor(xPos - 10, graphY + graphH + 8);
    M5.Display.print(timeLabels[i]);
    // Vertical grid line
    if (i > 0 && i < 6) {
      for (int y = graphY; y < graphY + graphH; y += 10) {
        M5.Display.drawPixel(xPos, y, COLOR_LIGHT_GRAY);
      }
    }
  }

  // --- Draw Data Lines (THICK 3x, all BLACK) ---
  if (sampleCount == 0) {
    M5.Display.setTextColor(COLOR_BLACK);
    M5.Display.setTextSize(2);
    M5.Display.setCursor(graphX + graphW / 2 - 140, graphY + graphH / 2 - 15);
    M5.Display.print("No data for this day");
  } else {
    // Plot each metric if filter is enabled
    int prevX = -1, prevYBatt = -1, prevYIn = -1, prevYOut = -1;

    // Draw every sample to avoid missing power spikes
    for (uint16_t i = 0; i < sampleCount; i++) {
      PowerSample sample = _powerHistory.getSample(_historyViewDay, i);
      if (sample.timestamp == 0)
        continue;

      // Calculate X position based on time of day
      struct tm st;
      time_t ts = sample.timestamp;
      localtime_r(&ts, &st);
      int minuteOfDay = st.tm_hour * 60 + st.tm_min;
      int x = graphX + (minuteOfDay * graphW / 1440);

      // Calculate Y positions
      // Battery: Scaled 0-100% of height
      int yBatt = graphY + graphH - (sample.batteryPct * graphH / 100);
      // Power: Scaled 0-graphMax of height
      int safeIn = (int)sample.inputW;
      if (safeIn > graphMax)
        safeIn = graphMax;
      int yIn = graphY + graphH - (safeIn * graphH / graphMax);

      int safeOut = (int)sample.outputW;
      if (safeOut > graphMax)
        safeOut = graphMax;
      int yOut = graphY + graphH - (safeOut * graphH / graphMax);

      // Draw THICK lines (3px) - all BLACK
      if (prevX >= 0) {
        // Battery % line (3px thick)
        if (_historyFilter & 0x01) {
          M5.Display.drawLine(prevX, prevYBatt, x, yBatt, COLOR_BLACK);
          M5.Display.drawLine(prevX, prevYBatt + 1, x, yBatt + 1, COLOR_BLACK);
          M5.Display.drawLine(prevX, prevYBatt - 1, x, yBatt - 1, COLOR_BLACK);
        }
        // Input W line (3px thick)
        if (_historyFilter & 0x02) {
          M5.Display.drawLine(prevX, prevYIn, x, yIn, COLOR_BLACK);
          M5.Display.drawLine(prevX, prevYIn + 1, x, yIn + 1, COLOR_BLACK);
          M5.Display.drawLine(prevX, prevYIn - 1, x, yIn - 1, COLOR_BLACK);
        }
        // Output W line (3px thick)
        if (_historyFilter & 0x04) {
          M5.Display.drawLine(prevX, prevYOut, x, yOut, COLOR_BLACK);
          M5.Display.drawLine(prevX, prevYOut + 1, x, yOut + 1, COLOR_BLACK);
          M5.Display.drawLine(prevX, prevYOut - 1, x, yOut - 1, COLOR_BLACK);
        }
      }

      prevX = x;
      prevYBatt = yBatt;
      prevYIn = yIn;
      prevYOut = yOut;
    }
  }

  // --- Draw Tooltip if selected ---
  if (_historySelectedMinute >= 0 && sampleCount > 0) {
    int closestDiff = 9999;
    PowerSample bestSample = {0, 0, 0, 0};
    int bestMinute = -1;

    // Find closest sample to the touched X coordinate minute
    for (uint16_t i = 0; i < sampleCount; i++) {
      PowerSample s = _powerHistory.getSample(_historyViewDay, i);
      if (s.timestamp == 0)
        continue;

      struct tm st;
      time_t ts = s.timestamp;
      localtime_r(&ts, &st);
      int m = st.tm_hour * 60 + st.tm_min;

      int diff = abs(m - _historySelectedMinute);
      if (diff < closestDiff) {
        closestDiff = diff;
        bestSample = s;
        bestMinute = m;
      }
    }

    if (closestDiff <= 30 && bestMinute >= 0) { // Limit snap distance
      int x = graphX + (bestMinute * graphW / 1440);

      // Draw vertical indicator line
      M5.Display.drawLine(x, graphY, x, graphY + graphH, COLOR_BLACK);
      M5.Display.drawLine(x - 1, graphY, x - 1, graphY + graphH, COLOR_BLACK);
      M5.Display.drawLine(x + 1, graphY, x + 1, graphY + graphH, COLOR_BLACK);

      // Calculate Y positions for labels
      int yBatt = graphY + graphH - (bestSample.batteryPct * graphH / 100);

      int safeIn = (int)bestSample.inputW;
      if (safeIn > graphMax)
        safeIn = graphMax;
      int yIn = graphY + graphH - (safeIn * graphH / graphMax);

      int safeOut = (int)bestSample.outputW;
      if (safeOut > graphMax)
        safeOut = graphMax;
      int yOut = graphY + graphH - (safeOut * graphH / graphMax);

      M5.Display.setTextSize(2);

      // Helper lambda to draw a text label with a white background
      auto drawLabel = [&](int lx, int ly, const char *text) {
        int tw = M5.Display.textWidth(text);
        int th = 16; // height for text size 2

        // Prevent drawing off right edge
        if (lx + tw + 4 > SCREEN_WIDTH) {
          lx = lx - tw - 12; // flip to left side of the line
        }

        M5.Display.fillRect(lx, ly - th / 2, tw + 4, th + 4, COLOR_WHITE);
        M5.Display.drawRect(lx, ly - th / 2, tw + 4, th + 4, COLOR_BLACK);
        M5.Display.setTextColor(COLOR_BLACK);
        M5.Display.setCursor(lx + 2, ly - th / 2 + 2);
        M5.Display.print(text);
      };

      char buf[32];

      // Time label at the top of the line
      snprintf(buf, sizeof(buf), "%02d:%02d", bestMinute / 60, bestMinute % 60);
      drawLabel(x + 6, graphY + 10, buf);

      // Draw labels near their respective lines if the filter allows it
      if ((_historyFilter & 0x01) || _historyFilter == 0x07) {
        snprintf(buf, sizeof(buf), "%d%%", bestSample.batteryPct);
        drawLabel(x + 6, yBatt, buf);
      }

      if ((_historyFilter & 0x02) || _historyFilter == 0x07) {
        snprintf(buf, sizeof(buf), "%dW In", bestSample.inputW);
        drawLabel(x + 6, yIn, buf);
      }

      if ((_historyFilter & 0x04) || _historyFilter == 0x07) {
        snprintf(buf, sizeof(buf), "%dW Out", bestSample.outputW);
        drawLabel(x + 6, yOut, buf);
      }
    }
  }

  // --- Navigation Bar (User Request: Replaces filters to allow navigation) ---
  // Draw Navigation/Filter Bar
  int btnY = SCREEN_HEIGHT - 60;
  int btnW = SCREEN_WIDTH / 6;
  int btnH = 60;

  M5.Display.setTextSize(1);
  drawButton(0 * btnW, btnY, btnW, btnH, "BATT", (_historyFilter & 0x01));
  drawButton(1 * btnW, btnY, btnW, btnH, "INPUT", (_historyFilter & 0x02));
  drawButton(2 * btnW, btnY, btnW, btnH, "OUTPUT", (_historyFilter & 0x04));
  drawButton(3 * btnW, btnY, btnW, btnH, "ALL", (_historyFilter == 0x07));
  drawButton(4 * btnW, btnY, btnW, btnH, "< DAY");
  drawButton(5 * btnW, btnY, btnW, btnH, "DAY >");
}

void UIManager::handleHistoryTouch(int x, int y, TouchEvent event) {
  if (event != TouchEvent::RELEASE)
    return;

  // --- Graph Touch & Tooltip ---
  const int graphX = 80;
  const int graphY = 100;
  const int graphW = SCREEN_WIDTH - 120;
  const int graphH = 300;

  // Flag to know if we should clear the tooltip
  bool clearSelected = true;

  if (y >= graphY && y <= graphY + graphH && x >= graphX &&
      x <= graphX + graphW) {
    Buzzer::click();
    int minuteOfDay = (x - graphX) * 1440 / graphW;
    _historySelectedMinute = minuteOfDay;
    clearSelected = false; // Kept tooltip alive
    forceRefresh();
  }

  // --- Filter Buttons (bottom bar layout) ---
  int btnY = SCREEN_HEIGHT - MENU_BAR_HEIGHT;
  int btnW = SCREEN_WIDTH / 6;

  // Check if touch is in the bottom bar area
  if (y >= btnY) {
    if (x >= 0 && x < btnW) { // BATT
      Buzzer::click();
      _historyFilter ^= 0x01;
      forceRefresh();
    } else if (x >= btnW && x < btnW * 2) { // INPUT
      Buzzer::click();
      _historyFilter ^= 0x02;
      forceRefresh();
    } else if (x >= btnW * 2 && x < btnW * 3) { // OUTPUT
      Buzzer::click();
      _historyFilter ^= 0x04;
      forceRefresh();
    } else if (x >= btnW * 3 && x < btnW * 4) { // ALL
      Buzzer::click();
      _historyFilter = 0x07;
      forceRefresh();
    } else if (x >= btnW * 4 && x < btnW * 5) { // PREV DAY
      Buzzer::click();
      if (_historyViewDay < 6) { // Max 7 days back
        _historyViewDay++;
        forceRefresh();
      }
    } else if (x >= btnW * 5 && x < SCREEN_WIDTH) { // NEXT DAY
      Buzzer::click();
      if (_historyViewDay > 0) {
        _historyViewDay--;
        forceRefresh();
      }
    }
  }

  // HOME button (top right - invisible touch zone if drawn in header,
  // currently we are NOT drawing a home button to give more space,
  // but user might want a way back?
  // User said "disable the nav bar as sometimes i go back to the homepage".
  // This implies accidental touches. I'll add a specific HOME button
  // in the top Header area just in case they get stuck).
  if (y < 50 && x > SCREEN_WIDTH - 100) {
    Buzzer::click();
    navigateTo(ScreenID::HOME);
    clearSelected = true;
  }

  if (clearSelected && _historySelectedMinute >= 0) {
    _historySelectedMinute = -1;
    forceRefresh(); // Force refresh to erase the tooltip
  }
}

// ============================================================================
// READER (E-Book) Implementation
// ============================================================================

void UIManager::readerScanBooks() {
  Serial.println("Reader: Scanning for ePub files...");

  extern SDManager *sdManager;
  // SD reset trick for reliability (Restored unconditional as requested)
  sdManager->powerCycleAndReinit();

  _readerBookList.clear();

  // 1. Scan /books directory
  // Create /books if it still doesn't exist (after reset)
  if (!SD.exists("/books")) {
    SD.mkdir("/books");
    Serial.println("Reader: Created /books directory");
  }

  File dir = SD.open("/books");
  if (dir && dir.isDirectory()) {
    while (File entry = dir.openNextFile()) {
      String name = entry.name();
      String lowerName = name;
      lowerName.toLowerCase();
      // Ignore hidden files (._) and check extension
      if (!name.startsWith(".") && !entry.isDirectory() &&
          lowerName.endsWith(".epub")) {
        _readerBookList.push_back(String("/books/") + name);
        Serial.printf("Reader: Found book in /books: %s\n", name.c_str());
      }
      entry.close();
    }
    dir.close();
  } else {
    Serial.println("Reader: Failed to open /books directory");
  }

  // 2. Scan Root Directory (for loose files)
  File root = SD.open("/");
  if (root && root.isDirectory()) {
    while (File entry = root.openNextFile()) {
      String name = entry.name();
      String lowerName = name;
      lowerName.toLowerCase();
      // Ignore hidden files, directories, and System Volume Info
      if (!name.startsWith(".") && !entry.isDirectory() &&
          lowerName.endsWith(".epub")) {
        _readerBookList.push_back(String("/") + name);
        Serial.printf("Reader: Found book in Root: %s\n", name.c_str());
      }
      entry.close();
    }
    root.close();
  }

  Serial.printf("Reader: Total books found: %d\n", _readerBookList.size());
}

void UIManager::drawReaderBrowser() {
  // File browser when no book is open
  M5.Display.setEpdMode(epd_mode_t::epd_quality);
  M5.Display.fillScreen(COLOR_WHITE);

  // Header
  M5.Display.fillRect(0, 0, M5.Display.width(), 60, COLOR_BLACK);
  M5.Display.setTextColor(COLOR_WHITE);
  M5.Display.setTextSize(1); // DejaVu24 at size 1 is plenty big
  M5.Display.setCursor(20, 18);
  M5.Display.print("SELECT A BOOK");

  int btnW = 110;
  int btnH = 40;

  // HOME button (top right)
  M5.Display.fillRect(M5.Display.width() - 120, 10, btnW, btnH, COLOR_WHITE);
  M5.Display.setTextColor(COLOR_BLACK);
  M5.Display.setTextSize(0.6); // Smaller for buttons
  int homeW = M5.Display.textWidth("HOME");
  M5.Display.setCursor(M5.Display.width() - 120 + (btnW - homeW) / 2,
                       10 + (btnH - 15) / 2);
  M5.Display.print("HOME");

  // REFRESH button (top right, left of HOME)
  int refreshBtnX = M5.Display.width() - 240;
  M5.Display.fillRect(refreshBtnX, 10, 110, 40, COLOR_WHITE);
  M5.Display.drawRect(refreshBtnX, 10, 110, 40, COLOR_BLACK);
  int refreshW = M5.Display.textWidth("REFRESH");
  M5.Display.setCursor(refreshBtnX + (110 - refreshW) / 2,
                       10 + (btnH - 15) / 2);
  M5.Display.print("REFRESH");

  // List books
  M5.Display.setTextColor(COLOR_BLACK);
  M5.Display.setTextSize(1);

  if (_readerBookList.empty()) {
    M5.Display.setCursor(50, 150);
    M5.Display.print("No .epub files found in /books/"); // Changed to .epub
    M5.Display.setCursor(50, 190);
    M5.Display.print("Copy EPUB files to the SD card");
  } else {
    int y = 80;
    int itemHeight = 50;
    for (size_t i = 0; i < _readerBookList.size() && i < 8; i++) {
      // Extract just filename from path
      String path = _readerBookList[i];
      int lastSlash = path.lastIndexOf('/');
      String filename = path.substring(lastSlash + 1);

      // Draw item
      M5.Display.drawRect(20, y, M5.Display.width() - 40, itemHeight - 5,
                          COLOR_BLACK);
      M5.Display.setCursor(30, y + 12);
      M5.Display.print(filename);
      y += itemHeight;
    }
  }
}

void UIManager::drawReaderScreen() {
  if (_readerFilePath.length() == 0) {
    drawReaderBrowser();
    return;
  }

  // Ensure Portrait Mode for Reader
  M5.Display.setRotation(0);

  // Guard against concurrent access from background task
  if (g_readerBusy) {
    // CRITICAL: Do NOT touch M5.Display while background task is running!
    // Task uses Display for textWidth metrics. Concurrent access causes
    // deadlock/watchdog. We already drew "Loading..." once before spawning.
    return;
  }

  // Draw EPub Content
  if (epubReader) {
    // Check if we need to Re-Layout (e.g. font/spacing changed)
    if (!epubReader->isLayoutValid()) {
      Serial.println("Reader: Layout invalid, rebuilding in task...");
      g_readerBusy = true;
      g_taskStartTime = millis();
      drawReaderLoading();
      M5.Display
          .display(); // FORCE display update before losing control to task

      // Spawn Task for Re-Layout (Needs large stack - 64KB for complex books)
      xTaskCreatePinnedToCore(
          [](void *param) {
            UIManager *self = (UIManager *)param;

            Serial.printf(
                "Relayout task: Starting (fontFamily=%d, fontSize=%d)\n",
                self->_readerFontFamily, self->_readerFontSize);

            // SET RENDERER STATE FIRST - use font based on family + size
            const m5gfx::IFont *layoutFont = self->getReaderFont();
            if (!layoutFont) {
              Serial.println("Relayout task: ERROR - null font pointer!");
              g_readerBusy = false;
              g_relayoutInProgress = false;
              self->_needsRefresh = true;
              vTaskDelete(NULL);
              return;
            }

            if (self->epubRenderer) {
              self->epubRenderer->setFont(layoutFont);
              self->epubRenderer->setFontSize(1.0);
              self->epubRenderer->setLineSpacing((int)self->_readerLineSpacing);
            }

            // Sync global display state
            M5.Display.setRotation(0);
            M5.Display.setFont(layoutFont);
            M5.Display.setTextSize(1.0);

            // HEAP MONITORING
            Serial.printf("HEAP: Before re-layout: %d bytes free\n",
                          esp_get_free_heap_size());

            // This function does the heavy lifting (zip extraction + parsing)
            if (self->epubReader) {
              self->epubReader->parse_and_layout_current_section();
              Serial.println("Relayout task: Layout complete, rendering...");
              self->epubReader->render();
            }

            Serial.printf("HEAP: After re-layout: %d bytes free\n",
                          esp_get_free_heap_size());

            // Signal completion BEFORE task cleanup
            g_readerBusy = false;
            g_relayoutInProgress = false;
            self->_needsRefresh = true;

            Serial.println("Relayout task: Complete!");

            // Give IDLE task time to clean up before task deletion
            vTaskDelay(pdMS_TO_TICKS(50));
            vTaskDelete(NULL);
          },
          "Relayout", 65536, this, 1, NULL, 1);
      return; // Exit and let task complete
    }

    // Prepare Display/Renderer for actual draw

    M5.Display.setRotation(0);

    // Use font based on family + size selection
    const m5gfx::IFont *font = getReaderFont();

    M5.Display.setFont(font);
    M5.Display.setTextSize(1.0);

    if (epubRenderer) {
      epubRenderer->setFont(font);
      epubRenderer->setFontSize(1.0);
      epubRenderer->setLineSpacing((int)_readerLineSpacing);
    }

    epubReader->render();
    // Sync state
    _readerPage = currentBookState.current_page;
    _readerTotalPages = currentBookState.pages_in_current_section;
  } else {
    M5.Display.fillScreen(TFT_WHITE);
    M5.Display.setTextColor(TFT_BLACK);
    M5.Display.drawCenterString("EPub Reader Error", 540 / 2, 960 / 2);
  }

  // Draw Header (Overlay)
  int screenW = M5.Display.width();
  int screenH = M5.Display.height();

  // EXIT button (left)
  int btnW = 100; // Wider for DejaVu24
  int btnH = 45;
  M5.Display.fillRect(10, 5, btnW, btnH, COLOR_WHITE);
  M5.Display.drawRect(10, 5, btnW, btnH, COLOR_BLACK);
  M5.Display.setTextColor(COLOR_BLACK);
  M5.Display.setTextSize(0.7); // Smaller for labels
  int exitW = M5.Display.textWidth("EXIT");
  M5.Display.setCursor(10 + (btnW - exitW) / 2, 5 + (btnH - 18) / 2); // Center
  M5.Display.print("EXIT");

  // MENU button (right)
  M5.Display.fillRect(screenW - btnW - 10, 5, btnW, btnH, COLOR_WHITE);
  M5.Display.drawRect(screenW - btnW - 10, 5, btnW, btnH, COLOR_BLACK);
  int menuTxtW = M5.Display.textWidth("MENU");
  M5.Display.setCursor(screenW - btnW - 10 + (btnW - menuTxtW) / 2,
                       5 + (btnH - 18) / 2);
  M5.Display.print("MENU");

  // Footer navigation (Overlay)
  int footerY = screenH - 50;
  // Clear footer area? No, EPub might use it. Just draw buttons.

  // Page number (Center)
  M5.Display.setTextSize(0.6); // Extra small for footer
  char pageStr[30];
  snprintf(pageStr, sizeof(pageStr), "%d/%d", _readerPage + 1,
           _readerTotalPages);
  int pageStrWidth = M5.Display.textWidth(pageStr);
  // Box for page number
  M5.Display.fillRect((screenW - pageStrWidth) / 2 - 10, footerY + 10,
                      pageStrWidth + 20, 30, COLOR_WHITE);
  M5.Display.setCursor((screenW - pageStrWidth) / 2, footerY + 15);
  M5.Display.print(pageStr);

  // PREV button area (invisible touch target reflected by UI?)
  // Draw simple arrows if needed

  // Header/Footer drawing (previous code)
  // ...

  // NOTE: Nav bar removed to prevent covering book content
  // Only header (EXIT/MENU) and footer (page #) overlays are shown

  // OVERLAY: Menu (Must be last to be on top)
  // CRITICAL: Ensure we check if menu is open!
  if (_readerMenuOpen) {
    drawReaderMenu();
  }
}

void UIManager::drawReaderMenu() {
  int screenW = M5.Display.width();
  int screenH = M5.Display.height();

  int menuW = 280;
  int menuH = 420; // Taller for vertical layout
  int menuX = (screenW - menuW) / 2;
  int menuY = (screenH - menuH) / 2;

  M5.Display.fillRect(menuX, menuY, menuW, menuH, COLOR_WHITE);
  M5.Display.drawRect(menuX, menuY, menuW, menuH, COLOR_BLACK);
  M5.Display.drawRect(menuX + 2, menuY + 2, menuW - 4, menuH - 4, COLOR_BLACK);

  M5.Display.setTextSize(0.8);
  M5.Display.setTextColor(COLOR_BLACK);

  int y = menuY + 20;
  int btnW = 200;
  int btnH = 45;
  int btnX = menuX + (menuW - btnW) / 2;

  // === Font Size Row - M and L side by side ===
  M5.Display.setCursor(menuX + 20, y + 12);
  M5.Display.print("Size:");

  int sizeBtn = 80;
  int mX = menuX + 100;
  int lX = mX + sizeBtn + 10;

  bool isMedium = (_readerFontSize == 2);
  if (isMedium) {
    M5.Display.fillRect(mX, y, sizeBtn, btnH, COLOR_BLACK);
    M5.Display.setTextColor(COLOR_WHITE);
  } else {
    M5.Display.drawRect(mX, y, sizeBtn, btnH, COLOR_BLACK);
    M5.Display.setTextColor(COLOR_BLACK);
  }
  M5.Display.setCursor(mX + 30, y + 14);
  M5.Display.print("M");

  bool isLarge = (_readerFontSize == 3);
  if (isLarge) {
    M5.Display.fillRect(lX, y, sizeBtn, btnH, COLOR_BLACK);
    M5.Display.setTextColor(COLOR_WHITE);
  } else {
    M5.Display.drawRect(lX, y, sizeBtn, btnH, COLOR_BLACK);
    M5.Display.setTextColor(COLOR_BLACK);
  }
  M5.Display.setCursor(lX + 30, y + 14);
  M5.Display.print("L");

  M5.Display.setTextColor(COLOR_BLACK);
  y += btnH + 15;

  // === Font Family - Stacked vertically ===
  // Serif button
  bool isSerif = (_readerFontFamily == 0);
  if (isSerif) {
    M5.Display.fillRect(btnX, y, btnW, btnH, COLOR_BLACK);
    M5.Display.setTextColor(COLOR_WHITE);
  } else {
    M5.Display.drawRect(btnX, y, btnW, btnH, COLOR_BLACK);
    M5.Display.setTextColor(COLOR_BLACK);
  }
  M5.Display.setCursor(btnX + 70, y + 14);
  M5.Display.print("Serif");
  y += btnH + 8;

  // Sans button
  M5.Display.setTextColor(COLOR_BLACK);
  bool isSans = (_readerFontFamily == 1);
  if (isSans) {
    M5.Display.fillRect(btnX, y, btnW, btnH, COLOR_BLACK);
    M5.Display.setTextColor(COLOR_WHITE);
  } else {
    M5.Display.drawRect(btnX, y, btnW, btnH, COLOR_BLACK);
    M5.Display.setTextColor(COLOR_BLACK);
  }
  M5.Display.setCursor(btnX + 70, y + 14);
  M5.Display.print("Sans");
  y += btnH + 8;

  // Dyslexic button
  M5.Display.setTextColor(COLOR_BLACK);
  bool isDys = (_readerFontFamily == 2);
  if (isDys) {
    M5.Display.fillRect(btnX, y, btnW, btnH, COLOR_BLACK);
    M5.Display.setTextColor(COLOR_WHITE);
  } else {
    M5.Display.drawRect(btnX, y, btnW, btnH, COLOR_BLACK);
    M5.Display.setTextColor(COLOR_BLACK);
  }
  M5.Display.setCursor(btnX + 55, y + 14);
  M5.Display.print("Dyslexic");
  y += btnH + 15;

  // === Action buttons - Stacked vertically ===
  M5.Display.setTextColor(COLOR_BLACK);
  drawButton(btnX, y, btnW, btnH, "DONE");
  y += btnH + 8;
  drawButton(btnX, y, btnW, btnH, "CLOSE");
}

void UIManager::readerOpenFile(const String &path) {
  // CRITICAL: Reset busy flags before opening new book
  // This ensures we don't get stuck if previous task didn't cleanup
  g_readerBusy = false;
  g_relayoutInProgress = false;

  _readerFilePath = path;

  // Cleanup old reader
  if (epubReader) {
    delete epubReader;
    epubReader = nullptr;
  }

  // Ensure SD is ready before opening
  extern SDManager *sdManager;
  if (sdManager) {
    sdManager->powerCycleAndReinit();
  }

  // MINIZ requires standard VFS path with mount point!
  // Arduino SD library usually mounts at "/sd" by default.
  // So "/books/file.epub" must becomes "/sd/books/file.epub" for fopen().
  String vfsPath = String("/sd") + path;
  Serial.printf("EPUB: VFS Path for fopen: %s\n", vfsPath.c_str());

  // Setup state (use VFS path for core library, but track original path if
  // needed?) Actually, Epub class stores path and uses it for ZipFile, so we
  // MUST pass the VFS path.
  snprintf(currentBookState.path, MAX_PATH_SIZE, "%s", vfsPath.c_str());

  currentBookState.current_section = 0;
  currentBookState.current_page = 0;

  // Load saved progress (Overwrite 0/0 if found)
  readerLoadSettings(); // Will parse into currentBookState if matches
                        // _readerFilePath

  // Create and load
  Serial.printf("EPUB: Loading %s\n", path.c_str());
  epubReader = new EpubReader(currentBookState, epubRenderer);

  // Force PORTRAIT mode for Reading
  M5.Display.setRotation(0);

  // Offload to task to prevent Stack Overflow (Main loop stack is too small)
  drawReaderLoading();

  // Reset flags
  g_bookLoadingComplete = false;
  g_bookLoadSuccess = false;

  static UIManager *myself = this;
  xTaskCreatePinnedToCore(
      [](void *param) {
        Serial.println("Reader: Loading Task Started (Stack 32KB)");

        // Perform the heavy blocking load
        bool success = myself->epubReader->load();

        if (success) {
          // ALSO parse the first section here (needs 32KB stack for miniz)
          Serial.println("Reader: Parsing first section in background task...");

          // CRITICAL: Synchronize display state for layout calculation
          // Use font based on family + size selection
          const m5gfx::IFont *font = myself->getReaderFont();

          // Apply to BOTH displays (M5 and Renderer) to be safe
          M5.Display.setFont(font);
          M5.Display.setTextSize(1.0);

          myself->epubRenderer->setFont(font);
          myself->epubRenderer->setFontSize(1.0);
          myself->epubRenderer->setLineSpacing((int)myself->_readerLineSpacing);

          myself->epubReader->parse_and_layout_current_section();
          Serial.println("Reader: First section parsed successfully!");
        }

        // Update flags
        g_bookLoadSuccess = success;
        g_bookLoadingComplete = true; // Signal UI helper

        vTaskDelete(NULL);
      },
      "LoadBook", 32768, NULL, 1, NULL, 1);
}

void UIManager::drawReaderLoading() {
  M5.Display.fillScreen(COLOR_WHITE);
  M5.Display.setTextColor(COLOR_BLACK);
  M5.Display.setTextSize(
      1); // Use Size 1 to avoid state corruption for layout task

  // Use actual display dimensions (handles both portrait and landscape)
  int screenW = M5.Display.width();
  int screenH = M5.Display.height();

  // Center the "Loading..." text
  // Approx 10 chars * 6px (Size 1) = 60px wide
  M5.Display.setCursor(screenW / 2 - 30, screenH / 2 - 10);
  M5.Display.print("Loading...");
}

void UIManager::readerCloseFile() {
  // CRITICAL: Reset busy flag to allow opening a new book
  g_readerBusy = false;
  g_relayoutInProgress = false;

  // Save settings before closing
  extern SDManager *sdManager;
  if (sdManager) {
    sdManager->powerCycleAndReinit(); // SD reset before save
  }
  readerSaveSettings();

  // Reset state
  _readerFilePath = "";
  _readerPage = 0;
  _readerTotalPages = 0;
  _readerPageOffsets.clear();

  // Reset to landscape when exiting Reader
  M5.Display.setRotation(1);

  // CRITICAL: Reset font to system default (DejaVu24)
  // Reader uses DejaVu40/24 which affects other screens if not reset
  M5.Display.setFont(&fonts::DejaVu24);
  M5.Display.setTextSize(1);

  // Cleanup epubReader
  if (epubReader) {
    delete epubReader;
    epubReader = nullptr;
  }

  Serial.println("Reader: Closed book");
}

void UIManager::readerPaginate() {
  // This method is no longer used for EPUB rendering, as pagination is handled
  // internally by EpubReader. It's kept as a placeholder or for potential
  // future plain text reader fallback.
  _readerPageOffsets.clear();
  _readerTotalPages = 0;

  // If an EPUB is open, get page count from state
  if (epubReader) {
    _readerTotalPages = currentBookState.pages_in_current_section;
    _readerPage = currentBookState.current_page;
    // We don't need offsets for EPUB, as EpubReader handles seeking.
    // For compatibility with old UI elements that might use _readerPageOffsets,
    // we can add a dummy entry if needed, but it's better to refactor those.
    if (_readerTotalPages > 0) {
      _readerPageOffsets.push_back(
          0); // Dummy entry to avoid empty vector issues
    }
  } else {
    // Fallback for plain text or if epubReader is not initialized
    extern SDManager *sdManager;
    if (!sdManager || !sdManager->isAvailable())
      return;

    // SD reset trick
    sdManager->powerCycleAndReinit();

    File file = SD.open(_readerFilePath.c_str());
    if (!file) {
      Serial.println("Reader: Failed to open file for pagination (fallback)");
      return;
    }

    // Calculate dimensions for PORTRAIT (forced)
    M5.Display.setRotation(0);
    int screenW = M5.Display.width();
    int screenH = M5.Display.height();

    int textW = screenW - 30;
    int textH = screenH - 120;

    int fontSize = _readerFontSize == 1 ? 2 : (_readerFontSize == 2 ? 3 : 4);
    int charWidth = fontSize * 6;
    int lineHeight = fontSize * 8 + _readerLineSpacing;
    int charsPerLine = textW / charWidth;
    int linesPerPage = textH / lineHeight;
    // int charsPerPage = charsPerLine * linesPerPage; // Not directly used for
    // offset calculation

    // Build page offsets
    uint32_t offset = 0;
    uint32_t fileSize = file.size();

    _readerPageOffsets.push_back(0); // First page starts at 0

    while (offset < fileSize) {
      // Skip forward by approximately one page worth of characters
      // This is simplified - a more accurate version would parse line breaks
      int charsRead = 0;
      int linesRead = 0;

      while (file.available() && linesRead < linesPerPage) {
        char ch = file.read();
        offset++;
        charsRead++;

        if (ch == '\n') {
          linesRead++;
          charsRead = 0;
        } else if (charsRead >= charsPerLine) {
          linesRead++;
          charsRead = 0;
        }
      }

      if (file.available()) {
        _readerPageOffsets.push_back(offset);
      }
    }

    _readerTotalPages = _readerPageOffsets.size();
    file.close();
  }

  Serial.printf("Reader: Paginated into %d pages\n", _readerTotalPages);
}

void UIManager::readerNextPage() {
  if (epubReader && !g_readerBusy) {
    Serial.println("Reader: NEXT page (Task)");
    g_readerBusy = true;
    drawReaderLoading();

    // Spawn task to handle Next Page (which might trigger heavy Parsing)
    xTaskCreatePinnedToCore(
        [](void *param) {
          UIManager *self = (UIManager *)param;
          self->epubReader->next();
          self->epubReader->render(); // Force parsing/rendering on this stack
          g_readerBusy = false;

          // Request UI refresh to update overlays (Header/Footer)
          // Note: render() above already updated content, so screen might
          // flicker slightly when overlay is drawn, but it's safer.
          self->forceRefresh();
          vTaskDelete(NULL);
        },
        "ReaderNext", 32768, this, 1, NULL, 1);
  }
}

void UIManager::readerPrevPage() {
  if (epubReader && !g_readerBusy) {
    Serial.println("Reader: PREV page (Task)");
    g_readerBusy = true;
    drawReaderLoading();

    // Spawn task to handle Prev Page
    xTaskCreatePinnedToCore(
        [](void *param) {
          UIManager *self = (UIManager *)param;
          self->epubReader->prev();
          self->epubReader->render(); // Force parsing/rendering on this stack
          g_readerBusy = false;

          self->forceRefresh();
          vTaskDelete(NULL);
        },
        "ReaderPrev", 32768, this, 1, NULL, 1);
  }
}

void UIManager::readerLoadSettings() {
  extern SDManager *sdManager;
  if (!sdManager || !sdManager->isAvailable())
    return;

  // 1. Load Global Settings
  File file = SD.open("/reader/settings.txt", FILE_READ);
  if (file) {
    while (file.available()) {
      String line = file.readStringUntil('\n');
      line.trim();
      if (line.startsWith("fontSize=")) {
        _readerFontSize = line.substring(9).toInt();
        // Validate: 2=Medium, 3=Large (default to Large if invalid)
        if (_readerFontSize < 2 || _readerFontSize > 3) {
          _readerFontSize = 3;
        }
      } else if (line.startsWith("fontFamily=")) {
        _readerFontFamily = line.substring(11).toInt();
        // Validate: 0=Serif, 1=Sans, 2=Dyslexic
        if (_readerFontFamily < 0 ||
            _readerFontFamily >= READER_FONT_FAMILY_COUNT) {
          _readerFontFamily = 0;
        }
      } else if (line.startsWith("lineSpacing=")) {
        _readerLineSpacing = line.substring(12).toInt();
      }
    }
    file.close();
  }

  // 2. Load Book-Specific Bookmark (if a book is target)
  if (_readerFilePath.length() > 0) {
    int lastSlash = _readerFilePath.lastIndexOf('/');
    String filename = (lastSlash >= 0)
                          ? _readerFilePath.substring(lastSlash + 1)
                          : _readerFilePath;
    String bkmPath = "/reader/bookmarks/" + filename + ".bkm";

    if (SD.exists(bkmPath)) {
      File bkmFile = SD.open(bkmPath, FILE_READ);
      if (bkmFile) {
        while (bkmFile.available()) {
          String line = bkmFile.readStringUntil('\n');
          line.trim();
          if (line.startsWith("section=")) {
            currentBookState.current_section = line.substring(8).toInt();
          } else if (line.startsWith("page=")) {
            currentBookState.current_page = line.substring(5).toInt();
          }
        }
        bkmFile.close();
        Serial.printf("Reader: Restored bookmark from %s -> Sect %d, Page %d\n",
                      bkmPath.c_str(), currentBookState.current_section,
                      currentBookState.current_page);

        // Sync local UI var
        _readerPage = currentBookState.current_page;
      }
    } else {
      Serial.println("Reader: No bookmark found for this file.");
    }
  }
}

void UIManager::readerSaveSettings() {
  extern SDManager *sdManager;
  if (!sdManager || !sdManager->isAvailable())
    return;

  // Ensure directories exist
  if (!SD.exists("/reader"))
    SD.mkdir("/reader");
  if (!SD.exists("/reader/bookmarks"))
    SD.mkdir("/reader/bookmarks");

  // 1. Save Global Settings
  SD.remove("/reader/settings.txt");
  File file = SD.open("/reader/settings.txt", FILE_WRITE);
  if (file) {
    file.printf("fontSize=%d\n", _readerFontSize);
    file.printf("fontFamily=%d\n", _readerFontFamily);
    file.printf("lineSpacing=%d\n", _readerLineSpacing);
    file.close();
  }

  // 2. Save Book-Specific Bookmark
  if (_readerFilePath.length() > 0) {
    int lastSlash = _readerFilePath.lastIndexOf('/');
    String filename = (lastSlash >= 0)
                          ? _readerFilePath.substring(lastSlash + 1)
                          : _readerFilePath;
    String bkmPath = "/reader/bookmarks/" + filename + ".bkm";

    SD.remove(bkmPath);
    File bkmFile = SD.open(bkmPath, FILE_WRITE);
    if (bkmFile) {
      // Use currentBookState for latest truth
      // (Assuming currentBookState is kept up to date during nav)
      int section = currentBookState.current_section;
      int page = currentBookState.current_page;

      bkmFile.printf("section=%d\n", section);
      bkmFile.printf("page=%d\n", page);
      bkmFile.close();

      Serial.printf("Reader: Saved bookmark to %s -> Sect %d, Page %d\n",
                    bkmPath.c_str(), section, page);
    } else {
      Serial.printf("Reader: Failed to save bookmark to %s\n", bkmPath.c_str());
    }
  }
}
void UIManager::handleReaderTouch(int x, int y, TouchEvent event) {
  if (event != TouchEvent::RELEASE)
    return;

  // If no file open, we are in Browser (Landscape)
  if (_readerFilePath.length() == 0) {
    Serial.printf("Reader Browser Touch: x=%d, y=%d\n", x, y);
    Serial.printf("  Book list size: %d\n", _readerBookList.size());
    Buzzer::click();

    // HOME button (top right)
    if (y > 10 && y < 50 && x > M5.Display.width() - 120) {
      Serial.println("  -> HOME button hit");
      navigateTo(ScreenID::HOME);
      return;
    }

    // REFRESH button (left of HOME)
    if (y > 10 && y < 50 && x > M5.Display.width() - 240 &&
        x < M5.Display.width() - 130) {
      Serial.println("  -> REFRESH button hit");
      readerScanBooks();
      forceRefresh();
      return;
    }

    // Book list items (start at y=80, itemHeight=50)
    // Max 8 items visible, touch zone extends to y=80 + 8*50 = y=480
    int maxY = 80 + (int)_readerBookList.size() * 50;
    if (y > 80 && y < maxY && !_readerBookList.empty()) {
      int index = (y - 80) / 50;
      // Clamp index to valid range (handles edge case of touch on gap)
      if (index >= (int)_readerBookList.size()) {
        index = (int)_readerBookList.size() - 1;
      }
      Serial.printf("  -> Book zone hit, index=%d\n", index);
      if (index >= 0 && index < (int)_readerBookList.size()) {
        // _readerBookList already contains full paths like "/books/file.txt"
        Serial.printf("  -> Opening book: %s\n",
                      _readerBookList[index].c_str());
        readerOpenFile(_readerBookList[index]);
        return;
      } else {
        Serial.println("  -> Index out of range!");
      }
    } else {
      Serial.printf("  -> Missed book zone (y=%d, maxY=%d, books empty=%d)\n",
                    y, maxY, _readerBookList.empty());
    }
    return;
  }

  // BOOK IS OPEN - PORTRAIT MODE (540x960)
  // M5Unified does NOT auto-rotate touch coords - we must transform manually
  // Based on logs: Bottom Right (Next) gives HW(953, 56).
  // This implies: pY = x, pX = 540 - y

  int pX = 540 - y; // y=56 -> pX=484 (Right)
  int pY = x;       // x=953 -> pY=953 (Bottom)

  Buzzer::click();

  Serial.printf("Reader Touch: HW(x=%d,y=%d) -> Portrait(x=%d,y=%d)\n", x, y,
                pX, pY);

  // --- 1. HANDLE MENU OVERLAY (Priority) ---
  if (_readerMenuOpen) {
    // Menu geometry: 280w x 420h, Centered in 540x960
    int menuW = 280;
    int menuH = 420;
    int menuX = (540 - menuW) / 2;
    int menuY = (960 - menuH) / 2;

    // Check if touch inside menu box
    if (pX >= menuX && pX <= menuX + menuW && pY >= menuY &&
        pY <= menuY + menuH) {
      int relY = pY - menuY;
      int relX = pX - menuX;

      Buzzer::click();
      Serial.printf("Menu touch: relX=%d, relY=%d\n", relX, relY);

      // Block font changes while re-layout task is running
      if (g_readerBusy) {
        Serial.println("Reader: Font change blocked - layout in progress");
        return;
      }

      // Font Size Row (Y 20-65) - M at x=100-180, L at x=190-270
      if (relY >= 20 && relY < 65) {
        bool fontChanged = false;
        if (relX >= 100 && relX < 180) {
          _readerFontSize = 2; // Medium
          fontChanged = true;
          Serial.println("Reader: Font Size -> Medium");
        } else if (relX >= 190 && relX < 270) {
          _readerFontSize = 3; // Large
          fontChanged = true;
          Serial.println("Reader: Font Size -> Large");
        }

        if (fontChanged) {
          readerSaveSettings();
          _needsRefresh = true;
          if (epubReader) {
            epubReader->invalidateLayout();
          }
          _readerMenuOpen = false;
          forceRefresh();
        }
        return;
      }

      // Font buttons stacked vertically (x=40-240 for all)
      if (relX >= 40 && relX < 240) {
        bool familyChanged = false;

        // Serif button (Y 80-125)
        if (relY >= 80 && relY < 125) {
          _readerFontFamily = 0;
          familyChanged = true;
          Serial.println("Reader: Font Family -> Serif");
        }
        // Sans button (Y 133-178)
        else if (relY >= 133 && relY < 178) {
          _readerFontFamily = 1;
          familyChanged = true;
          Serial.println("Reader: Font Family -> Sans");
        }
        // Dyslexic button (Y 186-231)
        else if (relY >= 186 && relY < 231) {
          _readerFontFamily = 2;
          familyChanged = true;
          Serial.println("Reader: Font Family -> Dyslexic");
        }
        // DONE button (Y 246-291)
        else if (relY >= 246 && relY < 291) {
          Serial.println("Reader: DONE button pressed");
          _readerMenuOpen = false;
          forceRefresh();
          return;
        }
        // CLOSE button (Y 299-344)
        else if (relY >= 299 && relY < 344) {
          Serial.println("Reader: CLOSE button pressed");
          readerCloseFile();
          navigateTo(ScreenID::HOME);
          return;
        }

        if (familyChanged) {
          readerSaveSettings();
          _needsRefresh = true;
          if (epubReader) {
            epubReader->invalidateLayout();
          }
          _readerMenuOpen = false;
          forceRefresh();
        }
        return;
      }

      return; // Touch inside menu but no button
    } else {
      // Tapped OUTSIDE menu -> Dismiss
      Serial.println("  -> Tapped outside menu, dismissing.");
      _readerMenuOpen = false;
      forceRefresh();
      return;
    }
  }

  // Debug: Check which zone
  if (pY < 50)
    Serial.println("  -> Header zone");
  else if (pY > 915)
    Serial.println("  -> Footer zone");
  else
    Serial.println("  -> Content zone");

  // Debug: Check button hits
  if (pY < 55 && pX < 120)
    Serial.println("  -> EXIT button!");
  if (pY < 55 && pX > 420)
    Serial.println("  -> MENU button!");
  if (pY > 910 && pX < 150)
    Serial.println("  -> PREV button!");
  if (pY > 910 && pX > 390)
    Serial.println("  -> NEXT button!");

  // Header (Y < 55)
  if (pY < 55) {
    // EXIT
    if (pX < 120) {
      readerCloseFile();
      navigateTo(ScreenID::HOME);
      return;
    }
    // MENU
    if (pX > 420) {
      _readerMenuOpen = true;
      forceRefresh();
      return;
    }
  }

  // Footer buttons (Y > 915, actual button area)
  if (pY > 915 && pY < 960) {
    // PREV (< 120)
    if (pX < 120) {
      readerPrevPage();
      return;
    }
    // NEXT (> 420)
    if (pX > 420) {
      readerNextPage();
      return;
    }
  }

  // Page Turn (Content Area)
  if (pY >= 50 && pY <= 915) {
    if (pX < 180)
      readerPrevPage();
    else if (pX > 360)
      readerNextPage();
  }

  // MENU OVERLAY
  if (_readerMenuOpen) {
    // Menu geometry: 340w x 250h, Centered in 540x960
    int menuW = 340;
    int menuH = 250;
    int menuX = (540 - menuW) / 2; // 100
    int menuY = (960 - menuH) / 2; // 355

    // Check if touch inside menu box
    if (pX >= menuX && pX <= menuX + menuW && pY >= menuY &&
        pY <= menuY + menuH) {
      // Relative Y in menu
      int relY = pY - menuY; // 0 to 250
      int relX = pX - menuX; // 0 to 340

      // Font Row (Y approx 30-70)
      if (relY >= 30 && relY < 70) {
        // Block font changes while re-layout task is running
        if (g_readerBusy) {
          Serial.println("Reader: Font change blocked - layout in progress");
          return;
        }

        if (relX >= 90 && relX < 150)
          _readerFontSize = 2; // M (shifted to S position)
        else if (relX >= 160 && relX < 220)
          _readerFontSize = 3; // L (shifted to M position)

        readerPaginate();
        forceRefresh(); // Keep menu open to adjust more
        return;
      }

      // Spacing Row REMOVED - User requested fixed Normal
      /*
      if (relY >= 90 && relY < 130) {
         ...
      }
      */

      // Bottom Row - Moved UP (Was 150-190, now starts at Y+60 = 90?)
      // Font row was at y=30. btnH 40.
      // Next row starts at y += 60 -> 90.
      // So Close/Done are now at 90-130 range.
      if (relY >= 90 && relY < 130) {
        // Close Book (30-160)
        if (relX >= 30 && relX < 160) {
          readerCloseFile();
          navigateTo(ScreenID::HOME);
          return;
        }
        // Done (180-310)
        if (relX >= 180 && relX < 310) {
          _readerMenuOpen = false;
          forceRefresh();
          return;
        }
      }
    } else {
      // Tap outside menu to close
      _readerMenuOpen = false;
      forceRefresh();
    }
  }
}

// ============================================================================
// Minesweeper Game
// ============================================================================

void UIManager::minesweeperInit() {
  memset(_mineState, 0, sizeof(_mineState));
  memset(_mineGrid, 0, sizeof(_mineGrid));
  _mineInputMode = false; // Reveal
  _mineGameOver = false;

  // Place 12 mines (for 7x12 grid)
  int minesPlaced = 0;
  while (minesPlaced < 12) {
    int r = random(7);
    int c = random(12);
    if (!_mineGrid[r][c]) {
      _mineGrid[r][c] = true;
      minesPlaced++;
    }
  }
}

int UIManager::minesweeperCountNeighbors(int r, int c) {
  int count = 0;
  for (int dr = -1; dr <= 1; dr++) {
    for (int dc = -1; dc <= 1; dc++) {
      if (dr == 0 && dc == 0)
        continue;
      int nr = r + dr;
      int nc = c + dc;
      if (nr >= 0 && nr < 7 && nc >= 0 && nc < 12) {
        if (_mineGrid[nr][nc])
          count++;
      }
    }
  }
  return count;
}

void UIManager::minesweeperReveal(int r, int c) {
  if (r < 0 || r >= 7 || c < 0 || c >= 12)
    return;
  if (_mineState[r][c] != 0)
    return; // Already revealed or flagged

  _mineState[r][c] = 1; // Reveal

  int neighbors = minesweeperCountNeighbors(r, c);
  if (neighbors == 0) {
    // Flood fill
    for (int dr = -1; dr <= 1; dr++) {
      for (int dc = -1; dc <= 1; dc++) {
        minesweeperReveal(r + dr, c + dc);
      }
    }
  }
}

void UIManager::minesweeperCheckWin() {
  if (_mineGameOver)
    return;

  bool win = true;
  for (int r = 0; r < 7; r++) {
    for (int c = 0; c < 12; c++) {
      if (!_mineGrid[r][c] && _mineState[r][c] != 1) {
        win = false; // Non-mine not revealed
        break;
      }
    }
    if (!win)
      break;
  }

  if (win) {
    _mineGameOver = true;
    M5.Display.fillRect(300, 200, 360, 140, COLOR_WHITE);
    M5.Display.drawRect(300, 200, 360, 140, COLOR_BLACK);
    M5.Display.setTextSize(3);
    M5.Display.setTextColor(COLOR_BLACK);
    M5.Display.setCursor(380, 250);
    M5.Display.print("YOU WIN!");
    M5.Display.display();
  }
}

void UIManager::drawMinesweeperGame() {
  // NOTE: We do NOT call drawMenuBar() to hide the navbar.

  M5.Display.fillScreen(COLOR_WHITE);

  M5.Display.setTextSize(2);
  M5.Display.setTextColor(COLOR_BLACK);
  M5.Display.setCursor(20, 20);
  M5.Display.print("MINESWEEPER");

  // Mode Toggle
  int modeBtnX = 600;
  int modeBtnY = 10;
  drawButton(modeBtnX, modeBtnY, 100, 50, _mineInputMode ? "FLAG" : "DIG",
             true);

  // Reset
  drawButton(modeBtnX - 120, modeBtnY, 100, 50, "RESET");

  // Quit (X) Button
  drawButton(900, 10, 50, 50, "X");

  int cellSize = 60; // Increased size for better touch
  int gridX = (SCREEN_WIDTH - (12 * cellSize)) / 2; // Center horizontal
  int gridY = 80;

  // Draw Grid
  for (int r = 0; r < 7; r++) {
    for (int c = 0; c < 12; c++) {
      int x = gridX + c * cellSize;
      int y = gridY + r * cellSize;

      // Draw cell border
      M5.Display.drawRect(x, y, cellSize, cellSize, COLOR_BLACK);

      if (_mineState[r][c] == 0) {
        // Covered
        M5.Display.fillRect(x + 2, y + 2, cellSize - 4, cellSize - 4,
                            COLOR_LIGHT_GRAY);
      } else if (_mineState[r][c] == 2) {
        // Flagged
        M5.Display.fillRect(x + 2, y + 2, cellSize - 4, cellSize - 4,
                            COLOR_LIGHT_GRAY);
        M5.Display.setTextColor(COLOR_RED);
        M5.Display.setTextSize(2); // Reduced from 3
        // Center 'F'
        int tw = M5.Display.textWidth("F");
        M5.Display.setCursor(x + (cellSize - tw) / 2,
                             y + (cellSize - 16) /
                                     2); // Approx vertical center for size 2
        M5.Display.print("F");
        M5.Display.setTextColor(COLOR_BLACK);
      } else if (_mineState[r][c] == 1) {
        // Revealed
        if (_mineGrid[r][c]) {
          // Mine
          M5.Display.fillCircle(x + cellSize / 2, y + cellSize / 2,
                                cellSize / 3, COLOR_BLACK);
        } else {
          int n = minesweeperCountNeighbors(r, c);
          if (n > 0) {
            M5.Display.setTextSize(2); // Reduced from 3
            char numStr[2];
            itoa(n, numStr, 10);
            int tw = M5.Display.textWidth(numStr);
            // Center number
            M5.Display.setCursor(x + (cellSize - tw) / 2,
                                 y + (cellSize - 16) / 2);
            M5.Display.print(numStr);
          }
        }
      }
    }
  }
}

void UIManager::handleMinesweeperTouch(int x, int y, TouchEvent event) {
  if (event != TouchEvent::RELEASE)
    return;

  int modeBtnX = 600;
  int modeBtnY = 10;

  // Mode Toggle
  if (x >= modeBtnX && x < modeBtnX + 100 && y >= modeBtnY &&
      y < modeBtnY + 50) {
    Buzzer::click();
    _mineInputMode = !_mineInputMode;
    // Just refresh the button area for speed?
    // For now, full refresh is safer for logic, but let's try to be smart if
    // requested. User asked for "quick refresh". We can just redraw the button.
    drawButton(modeBtnX, modeBtnY, 100, 50, _mineInputMode ? "FLAG" : "DIG",
               true);
    M5.Display.display();
    return;
  }

  // Reset
  if (x >= modeBtnX - 120 && x < modeBtnX - 20 && y >= 10 && y < 60) {
    Buzzer::click();
    minesweeperInit();
    // Full refresh needed for new game
    M5.Display.setEpdMode(epd_mode_t::epd_quality);
    forceRefresh();
    return;
  }

  // Quit (X)
  if (x >= 900 && x < 950 && y >= 10 && y < 60) {
    Buzzer::click();
    navigateTo(ScreenID::GAMES_MENU);
    return;
  }

  if (_mineGameOver) {
    // Allow quit or reset
    return;
  }

  // Grid
  int cellSize = 60;
  int gridX = (SCREEN_WIDTH - (12 * cellSize)) / 2;
  int gridY = 80;

  int r = (y - gridY) / cellSize;
  int c = (x - gridX) / cellSize;

  if (r >= 0 && r < 7 && c >= 0 && c < 12) {
    bool changed = false;

    if (_mineInputMode) {
      // Flagging
      if (_mineState[r][c] == 0) {
        _mineState[r][c] = 2;
        changed = true;
      } else if (_mineState[r][c] == 2) {
        _mineState[r][c] = 0;
        changed = true;
      }
    } else {
      // Digging
      if (_mineState[r][c] == 0) {
        if (_mineGrid[r][c]) {
          // Boom
          _mineState[r][c] = 1;
          _mineGameOver = true;
          // Reveal all mines... logic same as before, but updated loops
          for (int i = 0; i < 7; i++)
            for (int j = 0; j < 12; j++)
              if (_mineGrid[i][j])
                _mineState[i][j] = 1;

          forceRefresh(); // Full refresh for game over
          return;
        } else {
          minesweeperReveal(r, c);
          changed = true;
          minesweeperCheckWin();
        }
      }
    }

    if (changed) {
      // Fast partial refresh!
      // We could just redraw the changed cells, but flood fill changes many.
      // For simplicity with speed, we use fast mode and full redraw, OR
      // optimized partial. Let's use fast mode for playing.
      M5.Display.setEpdMode(epd_mode_t::epd_fast); // Faster updates
      drawMinesweeperGame();
      M5.Display.display();
      // Restore quality for text/static elements if needed, but for game
      // repeated inputs fast is better.
    }
  }
}
