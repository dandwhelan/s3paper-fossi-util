/**
 * UI Manager Implementation
 *
 * Design 1A "Classic Grid" layout for M5Paper S3 (960x540)
 */

#include "ui_manager.h"
#include "../ble/ble_client.h"
#include "../hardware/rtc.h"

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
        }

        // Check menu buttons (all screens)
        int menuHit = hitTestMenuButton(x, y);
        if (menuHit >= 0) {
          executeMenuButton(menuHit);
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
  _needsRefresh = true;
}

void UIManager::forceRefresh() { _needsRefresh = true; }

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
  int textLen = strlen(label) * 18; // Approx width for size 3
  if (w <= 60)
    textLen = strlen(label) * 12; // Smaller for +/- buttons maybe?
  M5.Display.setCursor(x + (w - textLen) / 2, y + (h - 24) / 2);
  M5.Display.print(label);
}

// Global reference to BLE client
extern FossibotBLE *bleClient;

void UIManager::handleHomeTouch(int x, int y, TouchEvent event) {
  if (event != TouchEvent::PRESS && event != TouchEvent::RELEASE)
    return; // Only act on complete taps (handled in calling function actually)

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
          M5.Speaker.tone(1000, 100);
        } else if (x >= dcX && x < dcXEnd) {
          Serial.println("UI: MATCH DC!");
          bleClient->toggleDC();
          _powerData.dcActive = !_powerData.dcActive;
          _needsRefresh = true;
          _lastRefresh = 0; // Force immediate refresh
          M5.Speaker.tone(1000, 100);
        } else if (x >= acX && x < acXEnd) {
          Serial.println("UI: MATCH AC!");
          bleClient->toggleAC();
          _powerData.acActive = !_powerData.acActive;
          _needsRefresh = true;
          _lastRefresh = 0; // Force immediate refresh
          M5.Speaker.tone(1000, 100);
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

  // --- Actions ---
  y = 440;
  drawButton(200, y, 200, 70, "SAVE", true);
  drawButton(560, y, 200, 70, "CANCEL");
}

void UIManager::handleSettingsTouch(int x, int y) {
  // Check buttons
  auto isHit = [&](int bx, int by, int bw, int bh) {
    return x >= bx && x < bx + bw && y >= by && y < by + bh;
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

  int row4 = 440;
  // Save
  if (isHit(200, row4, 200, 70)) {
    // Save to RTC via direct Wire access
    RTC::setDateTime(_editYear, _editMonth, _editDay, _editHour, _editMinute,
                     0);

    Serial.printf("RTC Time Set: %04d-%02d-%02d %02d:%02d:00\n", _editYear,
                  _editMonth, _editDay, _editHour, _editMinute);

    navigateTo(ScreenID::HOME);
  }

  // Cancel
  if (isHit(560, row4, 200, 70)) {
    navigateTo(ScreenID::HOME);
  }

  _needsRefresh = true;
  _lastRefresh = 0; // Force immediate refresh for Settings buttons
}
