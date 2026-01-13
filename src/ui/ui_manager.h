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
#include <vector>

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
  SETTINGS,
  SD_DIAG,
  NOTES_BROWSE
};

// Clock screen sub-modes (Side-Dock navigation)
// Clock screen sub-modes (Side-Dock navigation)
enum class ClockMode { CLOCK, ALARM, POMODORO, TIMER };

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

#include "../ble/fossibot_protocol.h" // Needed for PowerBankData

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
  void drawHomeScreen(); // Helper for drawing content

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

  // --- Timer State ---
  bool _timerRunning = false;
  int _timerDurationSeconds = 0; // Initial setting
  int _timerRemainingSeconds = 0;
  unsigned long _timerLastTick = 0;
  bool _timerRinging = false;
  unsigned long _timerRingStart = 0;

  // --- External Data ---
  Fossibot::PowerBankData _lastRenderedData;

  // Touch state
  int _touchStartX, _touchStartY;
  unsigned long _touchStartTime;
  bool _isTouching;

  // eInk refresh tracking
  unsigned long _lastRefresh;
  bool _needsRefresh;

  // Drawing methods
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
  void handleHomeTouch(int x, int y, TouchEvent event);
  void handleSettingsTouch(int x, int y);

  // Settings edit state
  int _editYear;
  int _editMonth;
  int _editDay;
  int _editHour;
  int _editMinute;
  int _refreshRateSeconds = 30; // Default refresh rate for preview
  int _editAutoSleep;           // Auto sleep timeout in minutes

  // Clock screen state
  ClockMode _clockMode = ClockMode::POMODORO; // Default to Pomodoro
  bool _alarmRinging = false;
  unsigned long _alarmRingStart = 0;

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
  void drawAlarmContent(int x, int y, int w, int h);
  void drawTimerContent(int x, int y, int w, int h);
  void handleClockTouch(int x, int y, TouchEvent event = TouchEvent::RELEASE);
  void updatePomodoro();
  void checkAlarm();
  void drawAlertScreen(const char *label);

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

  // Note file browsing state
  std::vector<String> _noteFileList; // List of note files
  int _noteFileIndex = -1;      // Currently selected file index (-1 = none)
  String _currentNoteFile = ""; // Current note filename

  // Notes methods
  void drawNotesScreen();
  void handleNotesTouch(int x, int y);
  void updateNotes();
  void notesSave();
  void notesLoad();
  void notesScanFiles();   // Scan /notes/ directory for available files
  void notesLoadByIndex(); // Load file at _noteFileIndex
  void notesPrevFile();    // Navigate to previous file
  void notesNextFile();    // Navigate to next file

  // Notes file browser methods
  void drawNotesBrowseScreen();
  void handleNotesBrowseTouch(int x, int y);
  void notesDeleteFile(int index); // Delete file at index
  void loadNotePreview(int index); // Load note as preview thumbnail
  int _notesBrowseScroll = 0;      // Scroll offset for file list
  int _selectedFileIndex = 0;      // Currently selected file for preview
  int _previewFileIndex = -1;      // Which file's preview is currently loaded
  int _deleteConfirmIndex =
      -1; // Index of file to delete (-1 = no confirmation)
  M5Canvas *_previewCanvas = nullptr; // Preview thumbnail canvas

  // SD Diagnostics methods
  void drawSDDiagScreen();
  void handleSDDiagTouch(int x, int y);

  // Power Management & Smart Refresh
  unsigned long _lastDashboardUpdate = 0;
  unsigned long _lastActivityTime = 0; // Last user interaction time
  bool shouldUpdateDashboard(const Fossibot::PowerBankData &newData);
  void checkPowerManagement(); // Check idle time and CPU scaling
  void enterDeepSleep();       // Enter deep sleep mode
  void runSDMountTest();
  void runSDWriteTest();
  void runSDReadTest();
  char _sdDiagResult[256] = ""; // Stores test result text

  // 2048 Game state
  int _game2048Grid[4][4]; // Tile values (0 = empty, 2, 4, 8, ...)
  int _game2048Score;
  int _game2048HighScore;
  bool _game2048GameOver;
  bool _game2048Won;

  // 2048 Game methods
  void drawGamesMenu();
  void handleGamesMenuTouch(int x, int y);
  void drawGame2048();
  void handleGame2048Touch(int x, int y, TouchEvent event);
  void game2048Init();
  void game2048AddRandomTile();
  bool game2048Slide(int direction); // 0=up, 1=right, 2=down, 3=left
  bool game2048IsGameOver();
  void game2048Save();
  void game2048Load();

  // Sudoku Game state (6x6 for better fit!)
  byte _sudokuGrid[6][6];     // Current values (0-6, 0 = empty)
  byte _sudokuSolution[6][6]; // Correct solution
  bool _sudokuGiven[6][6];    // true = locked given number
  int8_t _sudokuSelectedRow;  // Currently selected cell (-1 = none)
  int8_t _sudokuSelectedCol;
  byte _sudokuPuzzleNum;  // Current puzzle number (1-based)
  byte _sudokuDifficulty; // 0=easy, 1=medium, 2=hard

  // Sudoku Game methods
  void drawSudokuGame();
  void handleSudokuTouch(int x, int y, TouchEvent event);
  void sudokuLoadPuzzle(byte difficulty, byte num);
  bool sudokuValidateCell(byte row, byte col);
  bool sudokuCheckWin();
  void sudokuClearCell();
};

#endif // UI_MANAGER_H
