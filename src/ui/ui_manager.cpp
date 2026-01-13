/**
 * UI Manager Implementation
 *
 * Design 1A "Classic Grid" layout for M5Paper S3 (960x540)
 */

#include "ui_manager.h"
#include "../ble/ble_client.h"
#include "../hardware/battery.h"
#include "../hardware/buzzer.h"
#include "../hardware/rtc.h"
#include "../utils/config.h"
#include "../utils/sd_manager.h"
#include <FS.h>
#include <SD.h>

// Colors for eInk (grayscale)
#define COLOR_BLACK 0x0000
#define COLOR_DARK_GRAY 0x4208
#define COLOR_GRAY 0x8410
#define COLOR_LIGHT_GRAY 0xC618
#define COLOR_WHITE 0xFFFF

UIManager::UIManager()
    : _currentScreen(ScreenID::HOME), _previousScreen(ScreenID::HOME),
      _needsRefresh(true), _lastRefresh(0), _clockMode(ClockMode::CLOCK),
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

  Serial.printf("UI: Display size %dx%d\n", M5.Display.width(),
                M5.Display.height());

  _needsRefresh = true;
  _lastActivityTime = millis();
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
  // Check power savings first (might enter deep sleep)
  // Check power savings first (might enter deep sleep)
  checkPowerManagement();

  // Check Alarm/Timer globally (regardless of screen)
  checkAlarm();

  // Always update notes logic for continuous drawing
  if (_currentScreen == ScreenID::NOTES) {
    updateNotes();
  }

  if (!_needsRefresh)
    return;

  unsigned long now = millis();

  // Limit refresh rate based on setting
  // But allow the first refresh (_lastRefresh == 0)
  if (_lastRefresh != 0 && now - _lastRefresh < (_refreshRateSeconds * 1000))
    return;

  switch (_currentScreen) {
  case ScreenID::HOME:
    drawHomeScreen();
    break;
  case ScreenID::SETTINGS:
    drawSettingsScreen();
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
  // Other screens will be implemented later
  default:
    drawHomeScreen(); // Fallback to home
    break;
  }

  // Force E-Ink Refresh
  M5.Display.display();

  _lastRefresh = now;
  _needsRefresh = false;
  _powerDataDirty = false;
}

// Dispatcher
void UIManager::handleTouch(int x, int y, TouchEvent event) {
  _lastActivityTime = millis(); // Reset idle timer

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
        } else if (_currentScreen == ScreenID::GAME_SUDOKU) {
          handleSudokuTouch(x, y, event);
        }
      }

      // GAME_2048 needs swipe detection, handle separately
      if (_currentScreen == ScreenID::GAME_2048) {
        handleGame2048Touch(x, y, event);
      }

      // Check menu buttons (excluding Notes and Games which have own controls)
      if (_currentScreen != ScreenID::NOTES &&
          _currentScreen != ScreenID::NOTES_BROWSE &&
          _currentScreen != ScreenID::GAME_2048 &&
          _currentScreen != ScreenID::GAME_SUDOKU &&
          _currentScreen != ScreenID::GAMES_MENU) {
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
}

void UIManager::goBack() { navigateTo(_previousScreen); }

void UIManager::updatePowerBankData(const Fossibot::PowerBankData &data) {
  _powerData = data;
  _powerDataDirty = true;

  // Block auto-refresh in Notes mode to prevent clearing scribbles
  if (_currentScreen == ScreenID::NOTES) {
    return;
  }

  // Smart Refresh: Only update if data changed significantly
  if (shouldUpdateDashboard(data)) {
    _needsRefresh = true;
    _lastRenderedData = data;
    _lastDashboardUpdate = millis();
  }
}

void UIManager::forceRefresh() {
  _needsRefresh = true;
  _lastRefresh = 0;
}

// ============================================================================
// Drawing Methods
// ============================================================================

void UIManager::drawHomeScreen() {
  Serial.println("UI: Drawing home screen");

  // Clear screen
  M5.Display.fillScreen(COLOR_WHITE);

  // Layout calculations
  int contentY = BATTERY_BAR_HEIGHT + PANEL_MARGIN;
  int contentHeight =
      SCREEN_HEIGHT - BATTERY_BAR_HEIGHT - MENU_BAR_HEIGHT - PANEL_MARGIN * 3;
  int panelWidth = (SCREEN_WIDTH - PANEL_MARGIN * 3) / 2;
  int panelHeight = (contentHeight - PANEL_MARGIN) / 2;

  // 1. Battery bar (top, 3x thick)
  drawBatteryBar(_powerData.batteryPercent);

  // 2. Power panels (top row)
  int topRowY = contentY;
  drawPowerPanel(PANEL_MARGIN, topRowY, panelWidth, panelHeight, "INPUT",
                 _powerData.inputPower, 1100.0f, "to full",
                 _powerData.minutesToFull, true);

  drawPowerPanel(PANEL_MARGIN * 2 + panelWidth, topRowY, panelWidth,
                 panelHeight, "OUTPUT", _powerData.outputPower, 3000.0f,
                 "remaining", _powerData.minutesToEmpty, false);

  // 3. Status panels (bottom row)
  int bottomRowY = topRowY + panelHeight + PANEL_MARGIN;
  drawStatusPanel(PANEL_MARGIN, bottomRowY, panelWidth, panelHeight);
  drawClockWeatherPanel(PANEL_MARGIN * 2 + panelWidth, bottomRowY, panelWidth,
                        panelHeight);

  // 4. Menu bar (bottom)
  drawMenuBar();

  // Push to display
  M5.Display.display();
}

void UIManager::drawBatteryBar(float percent) {
  int barY = 5;
  int barHeight = BATTERY_BAR_HEIGHT - 10;
  int barWidth = SCREEN_WIDTH - 10; // Full width with small margin

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
  M5.Display.setTextSize(3);
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

  // Title with arrow
  M5.Display.setTextColor(COLOR_BLACK);
  M5.Display.setTextSize(3); // Increased from 2
  M5.Display.setCursor(x + 20, y + 15);
  M5.Display.print(isInput ? "IN "
                           : "OUT "); // Start with arrows when font supports
  M5.Display.print(title);

  // Power value (large)
  M5.Display.setTextSize(5); // Increased from 3
  M5.Display.setCursor(x + 40, y + 55);
  M5.Display.printf("%.0f W", power);

  // Progress bar (2x thick)
  int barX = x + 20;
  int barY = y + 95;
  int barW = w - 40;
  drawProgressBar(barX, barY, barW, POWER_BAR_HEIGHT, power / maxPower, true);

  // Time remaining
  M5.Display.setTextSize(3); // Increased from 2
  M5.Display.setCursor(x + 20, y + 125);
  M5.Display.print(Fossibot::formatTime(minutes));
  M5.Display.print(" ");
  M5.Display.print(timeLabel);
}

void UIManager::drawStatusPanel(int x, int y, int w, int h) {
  // Panel border
  M5.Display.drawRect(x, y, w, h, COLOR_BLACK);
  M5.Display.drawRect(x + 1, y + 1, w - 2, h - 2, COLOR_GRAY);

  // Connection status
  M5.Display.setTextColor(COLOR_BLACK);
  M5.Display.setTextSize(3);
  M5.Display.setCursor(x + 20, y + 15);
  M5.Display.print("FOSSIBOT: ");
  M5.Display.print(_powerData.connected ? "Connected" : "X");

  // Separator line
  M5.Display.drawLine(x + 20, y + 55, x + w - 20, y + 55, COLOR_GRAY);

  // Output toggles
  int toggleY = y + 70;
  int toggleSpacing = (w - 40) / 3;

  drawToggle(x + 20, toggleY, "USB", _powerData.usbActive);
  drawToggle(x + 20 + toggleSpacing, toggleY, "DC", _powerData.dcActive);
  drawToggle(x + 20 + toggleSpacing * 2, toggleY, "AC", _powerData.acActive);
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

  // Draw time large (size 5)
  char timeStr[16];
  snprintf(timeStr, sizeof(timeStr), "%02d:%02d", displayHour, displayMinute);
  M5.Display.setTextColor(COLOR_BLACK);
  M5.Display.setTextSize(5);
  M5.Display.setCursor(x + 20, y + 15);
  M5.Display.print(timeStr);

  // Draw date below (size 3)
  char dateStr[32];
  snprintf(dateStr, sizeof(dateStr), "%s %d %s %d", dayNames[displayDow],
           displayDay, monthNames[mon], displayYear);
  M5.Display.setTextSize(3);
  M5.Display.setCursor(x + 20, y + 75);
  M5.Display.print(dateStr);
}

void UIManager::drawMenuBar() {
  int y = SCREEN_HEIGHT - MENU_BAR_HEIGHT;

  // Background
  M5.Display.fillRect(0, y, SCREEN_WIDTH, MENU_BAR_HEIGHT, COLOR_LIGHT_GRAY);
  M5.Display.drawLine(0, y, SCREEN_WIDTH, y, COLOR_BLACK);

  // Draw each button
  M5.Display.setTextSize(3); // Increased from 1 for visibility
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
    int textX = btn.x + (btn.w - textWidth) / 2;
    M5.Display.setCursor(textX, y + (MENU_BAR_HEIGHT - 24) / 2);
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
  M5.Display.setTextSize(4); // Increased from 2
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
                       y + (h - 24) / 2); // 24 is approx height for Size 3
  M5.Display.print(label);
}

// Global reference to BLE client
extern FossibotBLE *bleClient;

void UIManager::handleHomeTouch(int x, int y, TouchEvent event) {
  if (event != TouchEvent::PRESS && event != TouchEvent::RELEASE)
    return; // Only act on complete taps (handled in calling function
            // actually)

  // Recalculate layout to find touch zones
  int contentY = BATTERY_BAR_HEIGHT + PANEL_MARGIN;
  int contentHeight =
      SCREEN_HEIGHT - BATTERY_BAR_HEIGHT - MENU_BAR_HEIGHT - PANEL_MARGIN * 3;
  int panelWidth = (SCREEN_WIDTH - PANEL_MARGIN * 3) / 2;
  int panelHeight = (contentHeight - PANEL_MARGIN) / 2;

  // Status Panel is Bottom-Left
  int statusX = PANEL_MARGIN;
  int statusY = contentY + panelHeight + PANEL_MARGIN;

  // Calculate Toggle Positions (matching drawStatusPanel)
  int toggleY = statusY + 70;
  int toggleW = (panelWidth - 40) / 3;
  int toggleH = 80;

  int usbX = statusX + 20;
  int dcX = usbX + toggleW;
  int acX = dcX + toggleW;

  // DEBUG: ALWAYS PRINT
  Serial.printf("DEBUG UI: Touch(%d, %d) Screen=%d\n", x, y,
                (int)_currentScreen);
  Serial.printf("  StatusPanel: X[%d-%d] Y[%d-%d]\n", statusX,
                statusX + panelWidth, statusY, statusY + panelHeight);

  // Check if touch is within Status Panel
  if (x >= statusX && x < statusX + panelWidth && y >= statusY &&
      y < statusY + panelHeight) {

    // Verify HIT BOXES:
    Serial.printf("HIT TEST: Touch(%d, %d)\n", x, y);
    Serial.printf("  Boundaries: StatusPanel X[%d-%d] Y[%d-%d]\n", statusX,
                  statusX + panelWidth, statusY, statusY + panelHeight);
    Serial.printf("  Toggle Zone Y: %d to 500\n", toggleY);
    Serial.printf("  USB Zone X: %d to %d\n", usbX, usbX + toggleW);
    Serial.printf("  DC Zone X: %d to %d\n", dcX, dcX + toggleW);
    Serial.printf("  AC Zone X: %d to %d\n", acX, acX + toggleW);

    // EXPANDED HIT ZONES: Include the text label + indicator box below +
    // generous margins Text is at toggleY, box is at toggleY+40 with size 30.
    // Total interactive area: toggleY to toggleY+110
    int toggleBottom = toggleY + 110; // Cover text + box + margin

    // Also expand X zones to be more generous (add 20px padding each side)
    int usbXEnd = dcX - 10;            // USB ends before DC
    int dcXEnd = acX - 10;             // DC ends before AC
    int acXEnd = statusX + panelWidth; // AC goes to panel edge

    Serial.printf("  USB Zone: X[%d-%d] Y[%d-%d]\n", usbX, usbXEnd, toggleY,
                  toggleBottom);
    Serial.printf("  DC Zone: X[%d-%d] Y[%d-%d]\n", dcX, dcXEnd, toggleY,
                  toggleBottom);
    Serial.printf("  AC Zone: X[%d-%d] Y[%d-%d]\n", acX, acXEnd, toggleY,
                  toggleBottom);

    if (y >= toggleY && y < toggleBottom) {
      if (bleClient && bleClient->isConnected()) {
        if (x >= usbX && x < usbXEnd) {
          Serial.println("UI: MATCH USB!");
          bleClient->toggleUSB();
          _powerData.usbActive = !_powerData.usbActive;
          _needsRefresh = true;
          _lastRefresh = 0; // Force immediate refresh
          Buzzer::click();
        } else if (x >= dcX && x < dcXEnd) {
          Serial.println("UI: MATCH DC!");
          bleClient->toggleDC();
          _powerData.dcActive = !_powerData.dcActive;
          _needsRefresh = true;
          _lastRefresh = 0; // Force immediate refresh
          Buzzer::click();
        } else if (x >= acX && x < acXEnd) {
          Serial.println("UI: MATCH AC!");
          bleClient->toggleAC();
          _powerData.acActive = !_powerData.acActive;
          _needsRefresh = true;
          _lastRefresh = 0; // Force immediate refresh
          Buzzer::click();
        } else {
          Serial.println("UI: Missed X zone for toggles");
        }
      } else {
        Serial.println("UI: BLE not connected - toggle ignored");
      }
    } else {
      Serial.printf("UI: Missed Y zone (need %d-%d, got %d)\n", toggleY,
                    toggleBottom, y);
    }
  }
}

void UIManager::drawSettingsScreen() {
  M5.Display.fillScreen(COLOR_WHITE);
  drawMenuBar();

  // Title - centered
  M5.Display.setTextSize(4);
  M5.Display.setTextColor(COLOR_BLACK);
  M5.Display.setCursor(SCREEN_WIDTH / 2 - 80, 20);
  M5.Display.print("Settings");

  // --- Date ---
  int y = 120;
  M5.Display.setTextSize(3);
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
  y = 220;
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

  // --- Refresh Rate (Left Side) ---
  y = 320;
  M5.Display.setCursor(20, y + 15);
  M5.Display.print("Refresh:");

  // - Button
  drawButton(160, y, 60, 60, "-");
  // Value display
  char rateStr[32];
  snprintf(rateStr, sizeof(rateStr), "%ds", _refreshRateSeconds);
  M5.Display.setCursor(240, y + 15);
  M5.Display.print(rateStr);
  // + Button
  drawButton(320, y, 60, 60, "+");

  // --- Sleep Timeout (Right Side) ---
  // Uses remaining space on Row 3
  M5.Display.setCursor(450, y + 15);
  M5.Display.print("Sleep:");

  // - Button
  drawButton(560, y, 60, 60, "-");

  // Value display
  M5.Display.setCursor(640, y + 15);
  if (_editAutoSleep == 0) {
    M5.Display.print("Never");
  } else {
    M5.Display.printf("%dm", _editAutoSleep);
  }

  // + Button
  drawButton(740, y, 60, 60, "+");

  // --- Actions ---
  y = 400;
  drawButton(320, y, 200, 70, "SD Diag");
  drawButton(540, y, 200, 70, "SAVE", true);
  drawButton(750, y, 200, 70, "CANCEL");

  // --- Battery Status (Moved to Top Right) ---
  M5.Display.setTextSize(2);
  M5.Display.setCursor(SCREEN_WIDTH - 220, 15);
  float voltage = Battery::getVoltage();
  int percentage = Battery::getPercentage();
  M5.Display.printf("Bat: %d%% (%.2fV)", percentage, voltage);
}

void UIManager::handleSettingsTouch(int x, int y) {
  // Check buttons
  auto isHit = [&](int bx, int by, int bw, int bh) {
    if (x >= bx && x < bx + bw && y >= by && y < by + bh) {
      Buzzer::click();
      return true;
    }
    return false;
  };

  int row1 = 120;
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

  int row2 = 220;
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

  int row3 = 320;
  // Refresh Rate -5
  if (isHit(200, row3, 80, 60)) {
    _refreshRateSeconds -= 5;
    if (_refreshRateSeconds < 5)
      _refreshRateSeconds = 5; // Minimum 5s
  }
  // Refresh Rate +5
  if (isHit(320, row3, 60, 60)) {
    _refreshRateSeconds += 5;
    if (_refreshRateSeconds > 300)
      _refreshRateSeconds = 300; // Max 5 minutes
  }

  // --- Sleep Timeout ---
  // Minus Button
  if (isHit(560, row3, 60, 60)) {
    // 0, 5, 15, 30, 60
    if (_editAutoSleep > 30)
      _editAutoSleep = 30;
    else if (_editAutoSleep > 15)
      _editAutoSleep = 15;
    else if (_editAutoSleep > 5)
      _editAutoSleep = 5;
    else if (_editAutoSleep > 0)
      _editAutoSleep = 0;
    else
      _editAutoSleep = 60; // Wrap around
  }

  // Plus Button
  if (isHit(740, row3, 60, 60)) {
    if (_editAutoSleep == 0)
      _editAutoSleep = 5;
    else if (_editAutoSleep < 15)
      _editAutoSleep = 15;
    else if (_editAutoSleep < 30)
      _editAutoSleep = 30;
    else if (_editAutoSleep < 60)
      _editAutoSleep = 60;
    else
      _editAutoSleep = 0; // Wrap around
  }

  int row4 = 400;
  // SD Diag
  if (isHit(320, row4, 200, 70)) {
    Serial.println("Settings: Opening SD Diagnostics");
    navigateTo(ScreenID::SD_DIAG);
    return;
  }
  // Save
  if (isHit(540, row4, 200, 70)) {
    // Save to RTC via direct Wire access
    RTC::setDateTime(_editYear, _editMonth, _editDay, _editHour, _editMinute,
                     0);

    Serial.printf("RTC Time Set: %04d-%02d-%02d %02d:%02d:00\n", _editYear,
                  _editMonth, _editDay, _editHour, _editMinute);

    // Save Config (Sleep Timeout)
    extern Config *config;
    if (config) {
      config->setAutoSleepMinutes(_editAutoSleep);
      config->save("/config/settings.json");
      Serial.printf("Config Saved: Auto Sleep = %d min\n", _editAutoSleep);
    }

    Buzzer::click();
    navigateTo(ScreenID::HOME);
  }

  // Cancel
  if (isHit(750, row4, 200, 70)) {
    Buzzer::click();
    navigateTo(ScreenID::HOME);
  }
}

// ============================================================================
// Clock Screen (Side-Dock Layout) - Clock/Alarm/Pomodoro Hub
// ============================================================================

void UIManager::drawClockScreen() {
  Serial.println("UI: Drawing Clock screen");

  M5.Display.fillScreen(COLOR_WHITE);
  drawMenuBar();

  // Layout: Sidebar (160px) | Content Area (remaining)
  const int SIDEBAR_WIDTH = 160;
  const int CONTENT_X = SIDEBAR_WIDTH;
  const int CONTENT_WIDTH = SCREEN_WIDTH - SIDEBAR_WIDTH;
  const int CONTENT_HEIGHT = SCREEN_HEIGHT - MENU_BAR_HEIGHT;

  // Draw sidebar
  drawClockSidebar(0, 0, SIDEBAR_WIDTH, CONTENT_HEIGHT);

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
    M5.Display.setTextSize(3);
    M5.Display.setTextColor(COLOR_DARK_GRAY);
    M5.Display.setCursor(CONTENT_X + 100, 200);
    M5.Display.print("Clock Coming Soon...");
    break;
  }

  // Draw Exit/Back button (Top Right)
  drawButton(SCREEN_WIDTH - 80, 10, 70, 50, "X");
}

void UIManager::drawClockSidebar(int x, int y, int w, int h) {
  // Sidebar background
  M5.Display.fillRect(x, y, w, h, COLOR_LIGHT_GRAY);
  M5.Display.drawLine(x + w - 1, y, x + w - 1, y + h, COLOR_BLACK);

  // Title
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(COLOR_BLACK);
  M5.Display.setCursor(x + 20, y + 15);
  M5.Display.print("ALARMS");

  // Sidebar buttons (y positions)
  int btnY = y + 60;
  int btnH = 70;
  int btnSpacing = 10;

  // Clock button
  bool clockActive = (_clockMode == ClockMode::CLOCK);
  M5.Display.fillRect(x + 10, btnY, w - 20, btnH,
                      clockActive ? COLOR_BLACK : COLOR_WHITE);
  M5.Display.drawRect(x + 10, btnY, w - 20, btnH, COLOR_BLACK);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(clockActive ? COLOR_WHITE : COLOR_BLACK);
  M5.Display.setCursor(x + 30, btnY + 25);
  M5.Display.print("Clock");

  // Alarm button
  btnY += btnH + btnSpacing;
  bool alarmActive = (_clockMode == ClockMode::ALARM);
  M5.Display.fillRect(x + 10, btnY, w - 20, btnH,
                      alarmActive ? COLOR_BLACK : COLOR_WHITE);
  M5.Display.drawRect(x + 10, btnY, w - 20, btnH, COLOR_BLACK);
  M5.Display.setTextColor(alarmActive ? COLOR_WHITE : COLOR_BLACK);
  M5.Display.setCursor(x + 30, btnY + 25);
  M5.Display.print("Alarm");

  // Pomodoro button
  btnY += btnH + btnSpacing;
  bool pomodoroActive = (_clockMode == ClockMode::POMODORO);
  M5.Display.fillRect(x + 10, btnY, w - 20, btnH,
                      pomodoroActive ? COLOR_BLACK : COLOR_WHITE);
  M5.Display.drawRect(x + 10, btnY, w - 20, btnH, COLOR_BLACK);
  M5.Display.setTextColor(pomodoroActive ? COLOR_WHITE : COLOR_BLACK);
  M5.Display.setCursor(x + 20, btnY + 25);
  M5.Display.print("Pomodoro");

  // Timer button
  btnY += btnH + btnSpacing;
  bool timerActive = (_clockMode == ClockMode::TIMER);
  M5.Display.fillRect(x + 10, btnY, w - 20, btnH,
                      timerActive ? COLOR_BLACK : COLOR_WHITE);
  M5.Display.drawRect(x + 10, btnY, w - 20, btnH, COLOR_BLACK);
  M5.Display.setTextColor(timerActive ? COLOR_WHITE : COLOR_BLACK);
  M5.Display.setCursor(x + 30, btnY + 25);
  M5.Display.print("Timer");
}

void UIManager::drawPomodoroContent(int x, int y, int w, int h) {
  // Session type label
  M5.Display.setTextSize(3);
  M5.Display.setTextColor(COLOR_BLACK);
  const char *sessionLabel = "WORK";
  if (_pomodoroSession == PomodoroSession::SHORT_BREAK)
    sessionLabel = "SHORT BREAK";
  else if (_pomodoroSession == PomodoroSession::LONG_BREAK)
    sessionLabel = "LONG BREAK";

  int labelWidth = strlen(sessionLabel) * 18; // Approx width
  M5.Display.setCursor(x + (w - labelWidth) / 2, y + 40);
  M5.Display.print(sessionLabel);

  // Large timer display (MM:SS)
  int minutes = _pomodoroRemainingSeconds / 60;
  int seconds = _pomodoroRemainingSeconds % 60;
  char timerStr[10];
  snprintf(timerStr, sizeof(timerStr), "%02d:%02d", minutes, seconds);

  M5.Display.setTextSize(8);
  int timerWidth = 5 * 48; // 5 chars * approx char width
  M5.Display.setCursor(x + (w - timerWidth) / 2, y + 100);
  M5.Display.print(timerStr);

  // State indicator
  M5.Display.setTextSize(2);
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
  M5.Display.setCursor(x + (w - strlen(stateLabel) * 12) / 2, y + 200);
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

      // Only refresh display once per minute to save E-Ink
      int oldMinutes = (_pomodoroRemainingSeconds + secondsToSubtract) / 60;
      int newMinutes = _pomodoroRemainingSeconds / 60;
      if (oldMinutes != newMinutes || _pomodoroRemainingSeconds <= 10) {
        _needsRefresh = true;
      }
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
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(COLOR_BLACK);
  M5.Display.setCursor(100, 25); // Shifted right to avoid X button
  M5.Display.print("Expression:");

  // Expression box
  M5.Display.drawRect(20, 60, DISPLAY_WIDTH - 40, 80, COLOR_BLACK);
  M5.Display.setTextSize(3);
  M5.Display.setCursor(30, 85);
  M5.Display.print(_calcExpression);

  // Result box
  M5.Display.setTextSize(2);
  M5.Display.setCursor(20, 160);
  M5.Display.print("Result:");
  M5.Display.drawRect(20, 190, DISPLAY_WIDTH - 40, 100, COLOR_BLACK);
  M5.Display.setTextSize(5);
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
  M5.Display.setTextSize(3);
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

      M5.Display.setTextSize(4);
      int textW = strlen(label) * 24;
      M5.Display.setCursor(bx + (BTN_W - textW) / 2, by + (BTN_H - 32) / 2);
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
  drawButton(btnX, y, btnW, btnH, "< PREV");
  y += (btnH + gap);
  drawButton(btnX, y, btnW, btnH, "> NEXT");
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
    sdOk = sdManager->powerCycleAndReinit();
  } else {
    Serial.println("No SDManager, using direct power cycle...");
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
  M5.Display.setTextSize(3);
  M5.Display.setTextColor(COLOR_BLACK);
  M5.Display.setCursor(20, 15);
  M5.Display.print("Notes File Browser");

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
  M5.Display.setTextSize(2);
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

    M5.Display.setTextSize(2);
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
    int thumbX = RIGHT_PANEL_X + 80;
    int thumbY = CONTENT_Y + 20;
    int thumbW = 400;
    int thumbH = 250;

    M5.Display.drawRect(thumbX, thumbY, thumbW, thumbH, COLOR_BLACK);

    // Show preview canvas if loaded, otherwise placeholder
    if (_previewCanvas != nullptr && _previewFileIndex == _selectedFileIndex) {
      _previewCanvas->pushSprite(thumbX + 1, thumbY + 1);
    } else {
      M5.Display.fillRect(thumbX + 1, thumbY + 1, thumbW - 2, thumbH - 2,
                          COLOR_LIGHT_GRAY);
      M5.Display.setTextSize(2);
      M5.Display.setTextColor(COLOR_BLACK);
      M5.Display.setCursor(thumbX + 100, thumbY + 110);
      M5.Display.print("Tap file to preview");
    }

    // Metadata section
    int metaY = thumbY + thumbH + 20;

    // Parse filename for metadata
    if (selectedFile.startsWith("note_") && selectedFile.length() >= 24) {
      String dateStr = selectedFile.substring(5, 13);  // YYYYMMDD
      String timeStr = selectedFile.substring(14, 20); // HHMMSS

      M5.Display.setTextSize(2);
      M5.Display.setCursor(RIGHT_PANEL_X + 20, metaY);
      M5.Display.print("Created:");

      M5.Display.setTextSize(2); // Increased from 1 to 2
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
    M5.Display.setTextSize(3);
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

  // Create preview canvas if needed (400x250 scaled preview)
  if (_previewCanvas == nullptr) {
    _previewCanvas = new M5Canvas(&M5.Display);
    _previewCanvas->setColorDepth(4);
    if (!_previewCanvas->createSprite(400, 250)) {
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
  float scaleX = (float)origW / 400.0f;
  float scaleY = (float)origH / 250.0f;

  _previewCanvas->fillSprite(COLOR_WHITE);
  for (int py = 0; py < 250; py++) {
    for (int px = 0; px < 400; px++) {
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
  // Always update if we haven't rendered yet (first run)
  if (_lastDashboardUpdate == 0)
    return true;

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

  // 3. Power Jitter Filtering (Only update if power changes > 3 Watts)
  // Input Power
  if (abs(newData.inputPower - _lastRenderedData.inputPower) > 3)
    return true;

  // Output Power
  if (abs(newData.outputPower - _lastRenderedData.outputPower) > 3)
    return true;

  // 4. Force update every 30 seconds regardless of data to keep clock/time sync
  if (millis() - _lastDashboardUpdate > 30000)
    return true;

  return false;
}

void UIManager::checkPowerManagement() {
  // USER REQUEST: Disable Deep Sleep (Option 1)
  // To fix "stuck in sleep" issue, we disable the auto-sleep logic entirely.
  return;

  // 0. Pomodoro Lock (User Request)
  // Don't sleep if timer is ticking!
  if (_pomodoroState == PomodoroState::RUNNING) {
    _lastActivityTime = millis(); // Keep resetting idle timer
    return;
  }

  // 1. Check for Auto Sleep
  extern Config *config;
  if (!config)
    return;

  int sleepMin = config->getAutoSleepMinutes();
  if (sleepMin > 0) {
    unsigned long elapsed = millis() - _lastActivityTime;
    unsigned long timeout = sleepMin * 60 * 1000UL;

    if (elapsed > timeout) {
      Serial.printf("Idle for %d min, entering deep sleep...\n", sleepMin);
      enterDeepSleep();
    }
  }

  // 2. CPU Frequency Scaling (DFS)
  // If idle for > 5 seconds, drop to 80MHz to save power
  // Only if NOT in critical sections (like file ops)
  // Note: touch interrupt handling needs to be fast to wake up CPU freq

  static bool lowPowerMode = false;
  if (millis() - _lastActivityTime > 5000) {
    if (!lowPowerMode) {
      // setCpuFrequencyMhz(80); // Can cause instability with BLE?
      // For now, let's stick to deep sleep as primary saver
      // lowPowerMode = true;
    }
  } else {
    if (lowPowerMode) {
      // setCpuFrequencyMhz(240);
      // lowPowerMode = false;
    }
  }
}

void UIManager::enterDeepSleep() {
  // 1. Show sleep message overlay (User Request: Keep previous state)
  // Centered "Zzz..." banner
  int bannerW = 200;
  int bannerH = 80;
  int x = (SCREEN_WIDTH - bannerW) / 2;
  int y = (SCREEN_HEIGHT - bannerH) / 2;

  M5.Display.fillRect(x, y, bannerW, bannerH, COLOR_WHITE);
  M5.Display.drawRect(x, y, bannerW, bannerH, COLOR_BLACK);
  M5.Display.setTextColor(COLOR_BLACK);
  M5.Display.setTextSize(4);
  M5.Display.setCursor(x + 50, y + 25);
  M5.Display.print("Zzz");
  M5.Display.display(); // Force update
  delay(500);

  // 2. Turn off peripherals
  // M5.Power.setExtOutput(false); // CAUTION: If this powers Touch, wake fails.
  // M5PExt (Port B/C power) shouldn't affect main touch (usually internal)

  // 3. Configure Wakeup Sources
  // GT911 INT pin is GPIO 36 on M5Paper v1.

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
  M5.Display.setTextSize(3);
  M5.Display.setCursor(20, 15);
  M5.Display.print("SD Card Diagnostics");

  // Back Button (Top Right)
  M5.Display.fillRect(SCREEN_WIDTH - 130, 5, 120, 50, COLOR_WHITE);
  M5.Display.setTextColor(COLOR_BLACK);
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
  M5.Display.setTextSize(3);
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

  M5.Display.setTextSize(2);
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
    // Beep every 1 sec
    if (now % 1000 < 100)
      Buzzer::click();

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
    if (y > 60 && y < 130)
      _clockMode = ClockMode::CLOCK;
    else if (y > 140 && y < 210)
      _clockMode = ClockMode::ALARM;
    else if (y > 220 && y < 290)
      _clockMode = ClockMode::POMODORO;
    else if (y > 300 && y < 370)
      _clockMode = ClockMode::TIMER;

    Buzzer::click();
    _needsRefresh = true;
    _lastRefresh = 0; // Force immediate refresh
    return;
  }

  // 2. Exit Button (Top Right) area check - handled by global if mapped, but
  // safety:
  if (x > SCREEN_WIDTH - 80 && y < 60) {
    navigateTo(ScreenID::HOME);
    return;
  }

  // 3. Content Handling
  if (_clockMode == ClockMode::ALARM) {
    extern Config *config;
    if (!config)
      return;
    int ah = config->getAlarmHour();
    int am = config->getAlarmMinute();

    // Inline Layout Logic matches drawAlarmContent
    // Approx StartX = 265
    // Hit zones centered on buttons (80x70) at RowY=120

    int rowY = 120;
    int hitH = 90; // Generous

    if (y > rowY - 10 && y < rowY + hitH) {
      int startX = 265;
      // Hour Minus
      if (x > startX && x < startX + 90) {
        ah = (ah - 1 + 24) % 24;
        Buzzer::click();
      }
      // Hour Plus (approx +195px offset)
      if (x > startX + 180 && x < startX + 280) {
        ah = (ah + 1) % 24;
        Buzzer::click();
      }

      int minStartX = 580;
      // Min Minus
      if (x > minStartX && x < minStartX + 90) {
        am = (am - 1 + 60) % 60;
        Buzzer::click();
      }
      // Min Plus
      if (x > minStartX + 180 && x < minStartX + 280) {
        am = (am + 1) % 60;
        Buzzer::click();
      }

      Serial.printf("Alarm Touch: New Time %02d:%02d\n", ah, am);
    }

    // Toggle
    if (y > 280 && y < 380) {
      // Center X approx 560. Width 200.
      if (x > 460 && x < 660) {
        config->setAlarmEnabled(!config->getAlarmEnabled());
        Buzzer::click();
        Serial.println("Alarm Touch: Toggle");
      }
    }

    config->setAlarmHour(ah);
    config->setAlarmMinute(am);
    config->save("/config/settings.json");
    _needsRefresh = true;
    _lastRefresh = 0; // Force immediate refresh

  } else if (_clockMode == ClockMode::TIMER) {
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
  M5.Display.setTextSize(4);
  M5.Display.setTextColor(COLOR_BLACK);
  M5.Display.setCursor(SCREEN_WIDTH / 2 - 80, 30);
  M5.Display.print("GAMES");

  // Game grid (2x2 for now)
  int gridX = 200;
  int gridY = 120;
  int btnW = 240;
  int btnH = 150;
  int gap = 80;

  // 2048 Button
  drawButton(gridX, gridY, btnW, btnH, "2048", true);

  // Placeholder buttons
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(COLOR_GRAY);

  // SUDOKU Button - NOW ENABLED!
  M5.Display.setTextSize(3);
  drawButton(gridX + btnW + gap, gridY, btnW, btnH, "SUDOKU");

  drawButton(gridX, gridY + btnH + 50, btnW, btnH, "WORDLE");
  M5.Display.setCursor(gridX + 50, gridY + btnH + 50 + 90);
  M5.Display.print("(Coming Soon)");
}

void UIManager::handleGamesMenuTouch(int x, int y) {
  // 2048 button (200-440, 120-270)
  if (x >= 200 && x < 440 && y >= 120 && y < 270) {
    Buzzer::click();
    game2048Load(); // Load saved game or init new
    navigateTo(ScreenID::GAME_2048);
  }

  // SUDOKU button (520-760, 120-270)
  if (x >= 520 && x < 760 && y >= 120 && y < 270) {
    Buzzer::click();
    sudokuLoadPuzzle(0, 1); // Start with easy puzzle #1
    navigateTo(ScreenID::GAME_SUDOKU);
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
  M5.Display.setTextSize(3);
  M5.Display.setTextColor(COLOR_BLACK);
  M5.Display.setCursor(20, 15);
  M5.Display.print("2048");

  // Score display
  M5.Display.setTextSize(2);
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
        M5.Display.setTextSize(value >= 1000 ? 3 : 4); // Larger font (was 3/2)
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

    M5.Display.setTextSize(_game2048Won ? 4 : 3);
    M5.Display.setTextColor(COLOR_BLACK);
    const char *msg = _game2048Won ? "YOU WON!" : "GAME OVER!";
    int msgW = M5.Display.textWidth(msg);
    M5.Display.setCursor(boxX + (boxW - msgW) / 2, boxY + 60);
    M5.Display.print(msg);

    M5.Display.setTextSize(2);
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

  file.close();
  Serial.println("2048 game loaded");
}
// ============================================================================
// SUDOKU GAME (6x6) - Optimized for M5Paper S3
// ============================================================================

// Hardcoded 6x6 Sudoku Puzzles (3 per difficulty = 9 total)
// Format: 0 = empty, 1-6 = given numbers

// EASY PUZZLES
const byte sudoku_easy_puzzles[3][6][6] = {
    // Easy #1
    {{3, 0, 5, 2, 0, 0},
     {0, 4, 0, 0, 6, 1},
     {6, 0, 0, 0, 0, 2},
     {1, 0, 0, 0, 0, 6},
     {4, 1, 0, 0, 2, 0},
     {0, 0, 3, 5, 0, 4}},

    // Easy #2
    {{0, 6, 0, 4, 0, 0},
     {4, 0, 0, 0, 6, 0},
     {0, 0, 3, 0, 0, 5},
     {2, 0, 0, 6, 0, 0},
     {0, 3, 0, 0, 0, 2},
     {0, 0, 5, 0, 4, 0}},

    // Easy #3
    {{5, 0, 0, 0, 3, 0},
     {0, 3, 0, 5, 0, 0},
     {0, 0, 6, 0, 0, 4},
     {4, 0, 0, 3, 0, 0},
     {0, 0, 2, 0, 5, 0},
     {0, 6, 0, 0, 0, 1}}};

const byte sudoku_easy_solutions[3][6][6] = {{{3, 6, 5, 2, 1, 4},
                                              {2, 4, 1, 3, 6, 5},
                                              {6, 5, 4, 1, 3, 2},
                                              {1, 3, 2, 4, 5, 6},
                                              {4, 1, 6, 5, 2, 3},
                                              {5, 2, 3, 6, 4, 1}},

                                             {{1, 6, 2, 4, 5, 3},
                                              {4, 5, 1, 3, 6, 2},
                                              {3, 2, 3, 1, 2, 5},
                                              {2, 1, 4, 6, 3, 5},
                                              {5, 3, 6, 2, 1, 4},
                                              {6, 4, 5, 5, 4, 1}},

                                             {{5, 4, 1, 2, 3, 6},
                                              {2, 3, 4, 5, 6, 1},
                                              {1, 5, 6, 3, 2, 4},
                                              {4, 1, 5, 3, 4, 2},
                                              {3, 2, 2, 4, 5, 3},
                                              {6, 6, 3, 1, 1, 1}}};

// MEDIUM PUZZLES
const byte sudoku_medium_puzzles[3][6][6] = {
    // Medium #1
    {{0, 0, 5, 0, 0, 0},
     {0, 4, 0, 0, 0, 1},
     {6, 0, 0, 0, 0, 0},
     {0, 0, 0, 0, 0, 6},
     {4, 0, 0, 0, 2, 0},
     {0, 0, 3, 5, 0, 0}},

    // Medium #2
    {{0, 6, 0, 0, 0, 0},
     {0, 0, 0, 0, 6, 0},
     {0, 0, 3, 0, 0, 5},
     {2, 0, 0, 0, 0, 0},
     {0, 3, 0, 0, 0, 0},
     {0, 0, 5, 0, 4, 0}},

    // Medium #3
    {{5, 0, 0, 0, 0, 0},
     {0, 3, 0, 0, 0, 0},
     {0, 0, 6, 0, 0, 4},
     {0, 0, 0, 3, 0, 0},
     {0, 0, 2, 0, 5, 0},
     {0, 0, 0, 0, 0, 1}}};

const byte sudoku_medium_solutions[3][6][6] = {{{3, 6, 5, 2, 1, 4},
                                                {2, 4, 1, 3, 6, 5},
                                                {6, 5, 4, 1, 3, 2},
                                                {1, 3, 2, 4, 5, 6},
                                                {4, 1, 6, 5, 2, 3},
                                                {5, 2, 3, 6, 4, 1}},

                                               {{1, 6, 2, 4, 5, 3},
                                                {4, 5, 1, 3, 6, 2},
                                                {3, 2, 3, 1, 2, 5},
                                                {2, 1, 4, 6, 3, 5},
                                                {5, 3, 6, 2, 1, 4},
                                                {6, 4, 5, 5, 4, 1}},

                                               {{5, 4, 1, 2, 3, 6},
                                                {2, 3, 4, 5, 6, 1},
                                                {1, 5, 6, 3, 2, 4},
                                                {4, 1, 5, 3, 4, 2},
                                                {3, 2, 2, 4, 5, 3},
                                                {6, 6, 3, 1, 1, 1}}};

// HARD PUZZLES
const byte sudoku_hard_puzzles[3][6][6] = {
    // Hard #1
    {{0, 0, 5, 0, 0, 0},
     {0, 0, 0, 0, 0, 1},
     {6, 0, 0, 0, 0, 0},
     {0, 0, 0, 0, 0, 6},
     {4, 0, 0, 0, 0, 0},
     {0, 0, 3, 0, 0, 0}},

    // Hard #2
    {{0, 6, 0, 0, 0, 0},
     {0, 0, 0, 0, 6, 0},
     {0, 0, 3, 0, 0, 0},
     {2, 0, 0, 0, 0, 0},
     {0, 3, 0, 0, 0, 0},
     {0, 0, 0, 0, 4, 0}},

    // Hard #3
    {{5, 0, 0, 0, 0, 0},
     {0, 0, 0, 0, 0, 0},
     {0, 0, 6, 0, 0, 4},
     {0, 0, 0, 3, 0, 0},
     {0, 0, 0, 0, 5, 0},
     {0, 0, 0, 0, 0, 1}}};

const byte sudoku_hard_solutions[3][6][6] = {{{3, 6, 5, 2, 1, 4},
                                              {2, 4, 1, 3, 6, 5},
                                              {6, 5, 4, 1, 3, 2},
                                              {1, 3, 2, 4, 5, 6},
                                              {4, 1, 6, 5, 2, 3},
                                              {5, 2, 3, 6, 4, 1}},

                                             {{1, 6, 2, 4, 5, 3},
                                              {4, 5, 1, 3, 6, 2},
                                              {3, 2, 3, 1, 2, 5},
                                              {2, 1, 4, 6, 3, 5},
                                              {5, 3, 6, 2, 1, 4},
                                              {6, 4, 5, 5, 4, 1}},

                                             {{5, 4, 1, 2, 3, 6},
                                              {2, 3, 4, 5, 6, 1},
                                              {1, 5, 6, 3, 2, 4},
                                              {4, 1, 5, 3, 4, 2},
                                              {3, 2, 2, 4, 5, 3},
                                              {6, 6, 3, 1, 1, 1}}};

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

// Draw Sudoku game
void UIManager::drawSudokuGame() {
  M5.Display.setEpdMode(epd_mode_t::epd_fastest);
  M5.Display.fillScreen(COLOR_WHITE);

  // Title
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(COLOR_BLACK);
  M5.Display.setCursor(30, 20);
  M5.Display.print("SUDOKU");

  // Puzzle info
  M5.Display.setCursor(300, 20);
  const char *diff = (_sudokuDifficulty == 0)   ? "Easy"
                     : (_sudokuDifficulty == 1) ? "Medium"
                                                : "Hard";
  M5.Display.printf("Puzzle %d/3 (%s)", _sudokuPuzzleNum, diff);

  // Draw grid (6x6, 85px cells, starts at x=30, y=60)
  for (int i = 0; i <= 6; i++) {
    int thickness = (i % 3 == 0) ? 3 : 1; // Thicker for 2x3 blocks

    // Horizontal lines
    for (int t = 0; t < thickness; t++) {
      M5.Display.drawLine(30, 60 + i * 85 + t, 540, 60 + i * 85 + t,
                          COLOR_BLACK);
    }

    // Vertical lines
    for (int t = 0; t < thickness; t++) {
      M5.Display.drawLine(30 + i * 85 + t, 60, 30 + i * 85 + t, 570,
                          COLOR_BLACK);
    }
  }

  // Draw numbers
  M5.Display.setTextSize(4);
  for (int r = 0; r < 6; r++) {
    for (int c = 0; c < 6; c++) {
      if (_sudokuGrid[r][c] != 0) {
        // Color: given=black, user=green, error=red
        int color = _sudokuGiven[r][c] ? COLOR_BLACK : GREEN;
        if (!sudokuValidateCell(r, c))
          color = RED;

        M5.Display.setTextColor(color);
        int x = 30 + c * 85 + 28; // Center in cell
        int y = 60 + r * 85 + 22;
        M5.Display.setCursor(x, y);
        M5.Display.print(_sudokuGrid[r][c]);
      }
    }
  }

  // Highlight selected cell
  if (_sudokuSelectedRow >= 0) {
    int x = 30 + _sudokuSelectedCol * 85;
    int y = 60 + _sudokuSelectedRow * 85;
    M5.Display.drawRect(x + 2, y + 2, 81, 81, BLUE);
    M5.Display.drawRect(x + 3, y + 3, 79, 79, BLUE);
  }

  // Number buttons (1-6) at Y=520
  M5.Display.setTextSize(3);
  for (int i = 0; i < 6; i++) {
    char num[2] = {(char)('1' + i), 0};
    drawButton(30 + i * 75, 520, 70, 50, num);
  }

  // Control buttons
  drawButton(510, 520, 80, 50, "CLR");
  drawButton(600, 520, 90, 50, "CHECK");
  drawButton(700, 520, 80, 50, "NEW");

  // HOME button (top right)
  M5.Display.setTextSize(2);
  drawButton(880, 10, 70, 40, "HOME");
}

// Handle Sudoku touch
void UIManager::handleSudokuTouch(int x, int y, TouchEvent event) {
  if (event != TouchEvent::PRESS)
    return;

  Buzzer::click();

  // Grid cell selection (30-540 x, 60-570 y)
  if (x >= 30 && x < 540 && y >= 60 && y < 570) {
    int row = (y - 60) / 85;
    int col = (x - 30) / 85;

    if (row >= 0 && row < 6 && col >= 0 && col < 6) {
      if (!_sudokuGiven[row][col]) {
        _sudokuSelectedRow = row;
        _sudokuSelectedCol = col;
        _needsRefresh = true;
        _lastRefresh = 0;
      }
    }
    return;
  }

  // Number buttons (1-6) at Y=520-570
  if (y >= 520 && y < 570 && x >= 30 && x < 480) {
    int num = ((x - 30) / 75) + 1; // 1-6
    if (_sudokuSelectedRow >= 0 && num >= 1 && num <= 6) {
      _sudokuGrid[_sudokuSelectedRow][_sudokuSelectedCol] = num;
      _needsRefresh = true;
      _lastRefresh = 0;
    }
    return;
  }

  // CLR button (510-590, 520-570)
  if (x >= 510 && x < 590 && y >= 520 && y < 570) {
    sudokuClearCell();
    _needsRefresh = true;
    _lastRefresh = 0;
    return;
  }

  // CHECK button (600-690, 520-570)
  if (x >= 600 && x < 690 && y >= 520 && y < 570) {
    if (sudokuCheckWin()) {
      // Show win message
      M5.Display.fillRect(200, 250, 560, 100, COLOR_WHITE);
      M5.Display.drawRect(200, 250, 560, 100, COLOR_BLACK);
      M5.Display.setTextSize(3);
      M5.Display.setTextColor(GREEN);
      M5.Display.setCursor(300, 280);
      M5.Display.print("PUZZLE SOLVED!");
      delay(2000);
    }
    _needsRefresh = true;
    _lastRefresh = 0;
    return;
  }

  // NEW button (700-780, 520-570)
  if (x >= 700 && x < 780 && y >= 520 && y < 570) {
    // Cycle to next puzzle
    _sudokuPuzzleNum++;
    if (_sudokuPuzzleNum > 3) {
      _sudokuPuzzleNum = 1;
      _sudokuDifficulty = (_sudokuDifficulty + 1) % 3; // Cycle difficulty
    }
    sudokuLoadPuzzle(_sudokuDifficulty, _sudokuPuzzleNum);
    _needsRefresh = true;
    _lastRefresh = 0;
    return;
  }

  // HOME button (880-950, 10-50)
  if (x >= 880 && x < 950 && y >= 10 && y < 50) {
    navigateTo(ScreenID::HOME);
    return;
  }
}
