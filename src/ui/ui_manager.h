/**
 * UI Manager
 *
 * Handles screen navigation, touch events, and rendering.
 */

#ifndef UI_MANAGER_H
#define UI_MANAGER_H

#include "../ble/fossibot_protocol.h"
#include <Arduino.h>
#include <M5Unified.h>

// Touch event types
enum class TouchEvent { PRESS, RELEASE, DRAG };

// Screen IDs
enum class ScreenID {
  HOME,
  GAMES_MENU,
  GAME_2048,
  GAME_WORDLE,
  GAME_SUDOKU,
  READER,
  CLOCK,
  CALCULATOR,
  NOTES,
  WEATHER,
  SETTINGS
};

// Clock screen sub-modes (Side-Dock navigation)
enum class ClockMode { CLOCK, ALARM, POMODORO };

// Pomodoro timer states
enum class PomodoroState { STOPPED, RUNNING, PAUSED, COMPLETED };

// Pomodoro session types
enum class PomodoroSession { WORK, SHORT_BREAK, LONG_BREAK };

// Menu button definition
struct MenuButton {
  int x, y, w, h;
  const char *label;
  const char *icon;
  ScreenID targetScreen;
};

class UIManager {
public:
  UIManager();
  ~UIManager();

  /**
   * Initialize UI system
   */
  void init();

  /**
   * Update UI (call in main loop)
   */
  void update();

  /**
   * Feed manual touch data (from main loop)
   */
  void setTouchState(int x, int y, bool pressed);

  /**
   * Handle touch event
   */
  void handleTouch(int x, int y, TouchEvent event);

  /**
   * Show home screen
   */
  void showHomeScreen();

  /**
   * Draw settings screen
   */
  void drawSettingsScreen();

  /**
   * Navigate to a screen
   */
  void navigateTo(ScreenID screen);

  /**
   * Go back to previous screen
   */
  void goBack();

  /**
   * Update power bank data on display
   */
  void updatePowerBankData(const Fossibot::PowerBankData &data);

  /**
   * Force full screen refresh
   */
  void forceRefresh();

  /**
   * Get current screen
   */
  ScreenID getCurrentScreen() const { return _currentScreen; }

private:
  ScreenID _currentScreen;
  ScreenID _previousScreen;

  // Power bank data cache
  Fossibot::PowerBankData _powerData;
  bool _powerDataDirty;

  // Display dimensions
  static const int SCREEN_WIDTH = 960;
  static const int SCREEN_HEIGHT = 540;

  // Layout constants
  static const int BATTERY_BAR_HEIGHT =
      80; // Increased height (approx 2x previous)
  static const int POWER_BAR_HEIGHT = 16; // 2x thicker (2 lines)
  static const int MENU_BAR_HEIGHT = 60;  // Increased to match visual weight
  static const int PANEL_MARGIN = 10;

  // Menu buttons
  static const int NUM_MENU_BUTTONS = 6;
  MenuButton _menuButtons[NUM_MENU_BUTTONS];

  // Touch state
  int _touchStartX, _touchStartY;
  unsigned long _touchStartTime;
  bool _isTouching;

  // eInk refresh tracking
  unsigned long _lastRefresh;
  bool _needsRefresh;

  // Drawing methods
  void drawHomeScreen();
  void drawBatteryBar(float percent);
  void drawPowerPanel(int x, int y, int w, int h, const char *title,
                      float power, float maxPower, const char *timeLabel,
                      int minutes, bool isInput);
  void drawStatusPanel(int x, int y, int w, int h);
  void drawClockWeatherPanel(int x, int y, int w, int h);
  void drawMenuBar();
  void drawButton(int x, int y, int w, int h, const char *label,
                  bool selected = false);
  void drawProgressBar(int x, int y, int w, int h, float percent,
                       bool thick = false);
  void drawToggle(int x, int y, const char *label, bool active);

  // Helper methods
  void initMenuButtons();
  int hitTestMenuButton(int x, int y);
  void executeMenuButton(int index);

  // Screen-specific handlers
  // Screen-specific handlers
  void handleHomeTouch(int x, int y, TouchEvent event);
  void handleSettingsTouch(int x, int y);

  // Settings edit state
  int _editYear;
  int _editMonth;
  int _editDay;
  int _editHour;
  int _editMinute;
  int _refreshRateSeconds = 5; // Default 5s for testing

  // Clock screen state
  ClockMode _clockMode = ClockMode::POMODORO; // Default to Pomodoro

  // Pomodoro state
  PomodoroState _pomodoroState = PomodoroState::STOPPED;
  PomodoroSession _pomodoroSession = PomodoroSession::WORK;
  int _pomodoroRemainingSeconds = 25 * 60; // 25 minutes default
  unsigned long _pomodoroLastTick = 0;

  // Pomodoro constants
  static const int POMODORO_WORK_SECONDS = 25 * 60;
  static const int POMODORO_SHORT_BREAK_SECONDS = 5 * 60;
  static const int POMODORO_LONG_BREAK_SECONDS = 15 * 60;

  // Clock screen drawing methods
  void drawClockScreen();
  void drawClockSidebar(int x, int y, int w, int h);
  void drawPomodoroContent(int x, int y, int w, int h);
  void handleClockTouch(int x, int y);
  void updatePomodoro();

  // Calculator state
  char _calcExpression[64] = "";
  double _calcResult = 0;
  double _calcOperand1 = 0;
  char _calcOperator = 0;
  bool _calcNewInput = true;

  // Calculator methods
  void drawCalculatorScreen();
  void handleCalculatorTouch(int x, int y);
  void calcAppendDigit(char digit);
  void calcSetOperator(char op);
  void calcCalculate();
  void calcClear();
  void calcBackspace();

  // Notes state
  int _lastDrawX = -1;
  int _lastDrawY = -1;
  bool _isDrawing = false;
  int _penSize = 2;
  uint16_t _penColor = 0; // BLACK (0) or WHITE (0xFFFF)

  // Touch state from main loop
  int _currentTouchX = -1;
  int _currentTouchY = -1;
  bool _currentTouchPressed = false;

  M5Canvas *_notesCanvas = nullptr; // Pointer to dynamic canvas

  // Notes methods
  void drawNotesScreen();
  void handleNotesTouch(int x, int y);
  void updateNotes();
  void notesSave();
  void notesLoad();
};

#endif // UI_MANAGER_H
