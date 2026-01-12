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
      _powerDataDirty(true), _touchStartX(0), _touchStartY(0),
      _touchStartTime(0), _isTouching(false), _lastRefresh(0),
      _needsRefresh(true) {
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
                     "CLOCK",         "CL",    ScreenID::CLOCK};
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

void UIManager::handleTouch(int x, int y, TouchEvent event) {
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

      if (dx < 20 && dy < 20 && duration < 500) {
        // It's a tap
        if (_currentScreen == ScreenID::HOME) {
          handleHomeTouch(x, y, event);
        } else if (_currentScreen == ScreenID::SETTINGS) {
          handleSettingsTouch(x, y);
        } else if (_currentScreen == ScreenID::CLOCK) {
          handleClockTouch(x, y);
        } else if (_currentScreen == ScreenID::CALCULATOR) {
          handleCalculatorTouch(x, y);
        } else if (_currentScreen == ScreenID::NOTES) {
          handleNotesTouch(x, y);
        }

        // Check menu buttons (excluding Notes screen which has own toolbar)
        if (_currentScreen != ScreenID::NOTES) {
          int menuHit = hitTestMenuButton(x, y);
          if (menuHit >= 0) {
            executeMenuButton(menuHit);
          }
        }
      }
    }
    _isTouching = false;
    break;

  case TouchEvent::DRAG:
    // Handle swipe gestures for games, reader, etc.
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
  }
}

void UIManager::goBack() { navigateTo(_previousScreen); }

void UIManager::updatePowerBankData(const Fossibot::PowerBankData &data) {
  _powerData = data;
  _powerDataDirty = true;
  // Block auto-refresh in Notes mode to prevent clearing scribbles
  if (_currentScreen != ScreenID::NOTES) {
    _needsRefresh = true;
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

  // --- Refresh Rate ---
  y = 320;
  M5.Display.setCursor(20, y + 15);
  M5.Display.print("Refresh:");

  // - Button
  drawButton(200, y, 80, 60, "-5");
  // Value display
  char rateStr[32];
  snprintf(rateStr, sizeof(rateStr), "%ds", _refreshRateSeconds);
  M5.Display.setCursor(310, y + 15);
  M5.Display.print(rateStr);
  // + Button
  drawButton(420, y, 80, 60, "+5");

  // --- Battery Status ---
  M5.Display.setTextSize(3);
  M5.Display.setCursor(600, 320 + 15);
  float voltage = Battery::getVoltage();
  int percentage = Battery::getPercentage();
  M5.Display.printf("Bat: %d%%", percentage);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(600, 320 + 50);
  M5.Display.printf("(%.2fV)", voltage);

  // --- Actions ---
  // Moved to right side and up to avoid menu overlap
  y = 400;
  drawButton(540, y, 200, 70, "SAVE", true);
  drawButton(750, y, 200, 70, "CANCEL");
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
  if (isHit(420, row3, 80, 60)) {
    _refreshRateSeconds += 5;
    if (_refreshRateSeconds > 300)
      _refreshRateSeconds = 300; // Max 5 minutes
  }

  int row4 = 400;
  // Save
  if (isHit(540, row4, 200, 70)) {
    // Save to RTC via direct Wire access
    RTC::setDateTime(_editYear, _editMonth, _editDay, _editHour, _editMinute,
                     0);

    Serial.printf("RTC Time Set: %04d-%02d-%02d %02d:%02d:00\n", _editYear,
                  _editMonth, _editDay, _editHour, _editMinute);

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
  case ClockMode::CLOCK:
  case ClockMode::ALARM:
  default:
    // Placeholder for Clock/Alarm modes
    M5.Display.setTextSize(3);
    M5.Display.setTextColor(COLOR_DARK_GRAY);
    M5.Display.setCursor(CONTENT_X + 100, 200);
    M5.Display.print("Coming Soon...");
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
  M5.Display.print("CLOCKS");

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

void UIManager::handleClockTouch(int x, int y) {
  // Check Exit Button (Top Right)
  if (x >= SCREEN_WIDTH - 80 && x < SCREEN_WIDTH - 10 && y >= 10 && y < 60) {
    Buzzer::click();
    navigateTo(ScreenID::HOME);
    return;
  }

  const int SIDEBAR_WIDTH = 160;

  // Sidebar button hit detection
  if (x < SIDEBAR_WIDTH) {
    int btnY = 60;
    int btnH = 70;
    int btnSpacing = 10;

    // Clock button
    if (y >= btnY && y < btnY + btnH) {
      _clockMode = ClockMode::CLOCK;
      _needsRefresh = true;
      _lastRefresh = 0;
      return;
    }
    // Alarm button
    btnY += btnH + btnSpacing;
    if (y >= btnY && y < btnY + btnH) {
      _clockMode = ClockMode::ALARM;
      _needsRefresh = true;
      _lastRefresh = 0;
      return;
    }
    // Pomodoro button
    btnY += btnH + btnSpacing;
    if (y >= btnY && y < btnY + btnH) {
      _clockMode = ClockMode::POMODORO;
      _needsRefresh = true;
      _lastRefresh = 0;
      return;
    }
  }

  // Pomodoro content area touch handling
  if (_clockMode == ClockMode::POMODORO && x >= SIDEBAR_WIDTH) {
    int contentX = SIDEBAR_WIDTH;
    int contentW = SCREEN_WIDTH - SIDEBAR_WIDTH;

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
        Buzzer::click(); // Audio feedback
      } else if (_pomodoroState == PomodoroState::COMPLETED) {
        // Reset and start new session
        _pomodoroState = PomodoroState::RUNNING;
        _pomodoroRemainingSeconds = POMODORO_WORK_SECONDS;
        _pomodoroSession = PomodoroSession::WORK;
        _pomodoroLastTick = millis();
      }
      _needsRefresh = true;
      _lastRefresh = 0;
      return;
    }

    // Reset button
    int resetBtnX = contentX + (contentW / 2) + (btnSpacing / 2);
    if (x >= resetBtnX && x < resetBtnX + btnW && y >= btnY &&
        y < btnY + btnH) {
      _pomodoroState = PomodoroState::STOPPED;
      // Reset to current session type duration
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
      _lastRefresh = 0;
      return;
    }

    // Session type selection buttons
    int modeY = 350;
    int modeBtnW = 100;
    int modeSpacing = 20;
    int modeStartX =
        contentX + (contentW - (3 * modeBtnW + 2 * modeSpacing)) / 2;

    // WORK button
    if (x >= modeStartX && x < modeStartX + modeBtnW && y >= modeY &&
        y < modeY + 50) {
      _pomodoroSession = PomodoroSession::WORK;
      _pomodoroRemainingSeconds = POMODORO_WORK_SECONDS;
      _pomodoroState = PomodoroState::STOPPED;
      Buzzer::click();
      _needsRefresh = true;
      _lastRefresh = 0;
      return;
    }

    // SHORT button
    int shortBtnX = modeStartX + modeBtnW + modeSpacing;
    if (x >= shortBtnX && x < shortBtnX + modeBtnW && y >= modeY &&
        y < modeY + 50) {
      _pomodoroSession = PomodoroSession::SHORT_BREAK;
      _pomodoroRemainingSeconds = POMODORO_SHORT_BREAK_SECONDS;
      _pomodoroState = PomodoroState::STOPPED;
      Buzzer::click();
      _needsRefresh = true;
      _lastRefresh = 0;
      return;
    }

    // LONG button
    int longBtnX = modeStartX + 2 * (modeBtnW + modeSpacing);
    if (x >= longBtnX && x < longBtnX + modeBtnW && y >= modeY &&
        y < modeY + 50) {
      _pomodoroSession = PomodoroSession::LONG_BREAK;
      _pomodoroRemainingSeconds = POMODORO_LONG_BREAK_SECONDS;
      _pomodoroState = PomodoroState::STOPPED;
      Buzzer::click();
      _needsRefresh = true;
      _lastRefresh = 0;
      return;
    }
  }
}

void UIManager::updatePomodoro() {
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
  drawButton(btnX, y, btnW, btnH, "LOAD");
  y += (btnH + gap);

  drawButton(btnX, y, btnW, btnH, "CLR");

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
    } else if (checkBtn(5)) {
      Buzzer::click();
      notesLoad();
    } else if (checkBtn(6)) {
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

  // Show saving status
  M5.Display.setEpdMode(epd_mode_t::epd_fast);
  M5.Display.fillRect(SCREEN_WIDTH / 2 - 100, SCREEN_HEIGHT / 2 - 30, 200, 60,
                      COLOR_BLACK);
  M5.Display.setTextColor(COLOR_WHITE);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(SCREEN_WIDTH / 2 - 40, SCREEN_HEIGHT / 2 - 10);
  M5.Display.print("Saving...");
  M5.Display.display();

  File f = SD.open("/note.bin", FILE_WRITE);
  if (f) {
    uint16_t w = _notesCanvas->width();
    uint16_t h = _notesCanvas->height();
    uint8_t d = _notesCanvas->getColorDepth();

    // Header
    f.write((uint8_t *)"M5NOTE", 6);
    f.write((uint8_t *)&w, 2);
    f.write((uint8_t *)&h, 2);
    f.write(&d, 1);

    // Data
    // Calculate size in bytes
    size_t len = (w * h * d) / 8;
    if (d < 8 && (w * h * d) % 8 != 0)
      len++; // Round up bits

    f.write((uint8_t *)_notesCanvas->getBuffer(), len);
    f.close();
  } else {
    Serial.println("Failed to open file for writing");
  }

  // Restore UI
  _needsRefresh = true;
  _lastRefresh = 0;
}

void UIManager::notesLoad() {
  if (!_notesCanvas)
    return;

  // Show loading status
  M5.Display.setEpdMode(epd_mode_t::epd_fast);
  M5.Display.fillRect(SCREEN_WIDTH / 2 - 100, SCREEN_HEIGHT / 2 - 30, 200, 60,
                      COLOR_BLACK);
  M5.Display.setTextColor(COLOR_WHITE);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(SCREEN_WIDTH / 2 - 40, SCREEN_HEIGHT / 2 - 10);
  M5.Display.print("Loading...");
  M5.Display.display();

  if (SD.exists("/note.bin")) {
    File f = SD.open("/note.bin", FILE_READ);
    if (f) {
      char header[7];
      f.read((uint8_t *)header, 6);
      header[6] = 0;

      if (strcmp(header, "M5NOTE") == 0) {
        uint16_t w, h;
        uint8_t d;
        f.read((uint8_t *)&w, 2);
        f.read((uint8_t *)&h, 2);
        f.read(&d, 1);

        if (w == _notesCanvas->width() && h == _notesCanvas->height() &&
            d == _notesCanvas->getColorDepth()) {
          size_t len = (w * h * d) / 8;
          if (d < 8 && (w * h * d) % 8 != 0)
            len++;

          f.read((uint8_t *)_notesCanvas->getBuffer(), len);
          // Push to display immediately handled by refresh below
        }
      }
      f.close();
    }
  }

  // Restore UI
  _needsRefresh = true;
  _lastRefresh = 0;
}
