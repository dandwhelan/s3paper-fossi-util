/**
 * M5Paper S3 Multi-Feature Power Bank Display & Smart Assistant
 *
 * Main entry point for the application.
 *
 * Features:
 * - Fossibot Power Bank BLE monitoring
 * - Brain training games (2048, Wordle, Sudoku, etc.)
 * - E-Reader for EPUB/TXT files
 * - Clock, Alarm, Timer, Pomodoro
 * - Calculator
 * - Notes & To-Do
 * - Weather Dashboard
 */

#include "ble/ble_client.h"
#include "hardware/buzzer.h"
#include "hardware/display.h"
#include "hardware/rtc.h"
#include "hardware/touch.h"
#include "ui/ui_manager.h"
#include "utils/config.h"
#include "utils/sd_manager.h"
#include <M5Unified.h>
#include <SD.h>
#include <sys/time.h>
#include <time.h>

// Forward declarations
void initHardware();
void initSD();
void initBLE();
void mainLoop();

// Global instances
UIManager *uiManager = nullptr;
FossibotBLE *bleClient = nullptr;
SDManager *sdManager = nullptr;
Config *config = nullptr;

void setup() {
  // Wait for serial to be ready (important for S3 USB CDC)
  delay(1000);
  Serial.begin(115200);
  delay(500);

  Serial.println(" Booting M5Paper S3...");

  // Initialize M5Unified
  auto cfg = M5.config();
  cfg.serial_baudrate = 115200;
  // Disable some internal modules to prevent auto-I2C init on wrong pins
  // but keep RTC enabled for timekeeping
  cfg.internal_rtc = true; // ENABLE RTC for time/date
  cfg.internal_imu = false;
  cfg.internal_spk = true; // Enable Speaker for Beep Feedback
  cfg.internal_mic = false;

  M5.begin(cfg);

  // OPTION 1: Disable Auto-Sleep to prevent "stuck in sleep" issue
  // M5Unified doesn't have setAutoSleep directly exposed in Power_Class in some
  // versions. We can try to just not engage it or set it to 0. However, since
  // we enter deep sleep MANUALLY in ui_manager.cpp, we just need to make sure
  // we don't call it if we want to debug. But wait, the USER asked to add
  // `M5.Power.setAutoSleep(false)`. If that doesn't exist, we'll comment it out
  // and rely on our manual logic. Actually, M5Unified handles sleep via
  // M5.Power.setSleep(). Let's comment this out to fix the build first, then
  // address logic. M5.Power.setAutoSleep(false);
  Serial.println("Auto-sleep logic controlled by app.");

  // --- I2C Configuration ---
  // Both BM8563 RTC (0x51) and GT911 touch (0x5D) are on the SAME I2C bus
  // M5Paper S3 external I2C: SDA=41, SCL=42
  Wire.end(); // End any M5Unified default Wire config
  delay(10);
  Wire.begin(41, 42);    // Initialize Wire on the correct pins
  Wire.setClock(400000); // 400kHz
  delay(10);

  // Power up peripherals (if needed)
  M5.Power.setExtOutput(true);
  delay(100);

  // Scan I2C bus (SDA:41, SCL:42) - should find RTC (0x51) and GT911 (0x5D)
  Serial.println("--- I2C Scan (SDA:41, SCL:42) ---");
  int i2c_devices = 0;
  for (byte address = 1; address < 127; ++address) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0) {
      Serial.printf("Device at 0x%02X\n", address);
      i2c_devices++;
    }
  }

  // --- GT911 SOFTWARE RESET ---
  Serial.println("Attempting GT911 Soft Reset...");
  Wire.beginTransmission(0x5D);
  Wire.write(0x80);
  Wire.write(0x40);
  Wire.write(0x02); // 2 = Soft Reset
  Wire.endTransmission();
  delay(50);

  // Reset Configuration (0x8040 = 0)
  Wire.beginTransmission(0x5D);
  Wire.write(0x80);
  Wire.write(0x40);
  Wire.write(0x00);
  Wire.endTransmission();
  delay(100);

  if (i2c_devices == 0) {
    Serial.println("No I2C devices found.");
  } else {
    Serial.println("--- I2C Scan Complete ---");
  }

  Serial.printf("Touch Enabled: %s\n", M5.Touch.isEnabled() ? "YES" : "NO");
  if (!M5.Touch.isEnabled()) {
    Serial.println("WARNING: Touch not enabled by M5Unified!");
  }

  // Check if RTC is enabled
  Serial.printf("RTC Enabled: %s\n", M5.Rtc.isEnabled() ? "YES" : "NO");

  // Check if BM8563 (0x51) is on the internal I2C bus (Wire)
  Wire.beginTransmission(0x51);
  if (Wire.endTransmission() == 0) {
    Serial.println("BM8563 RTC found at 0x51 (Wire)");
  } else {
    Serial.println("BM8563 RTC NOT found at 0x51 on Wire!");
  }

  // RTC time is battery-backed - use existing time
  int year = 0, month = 0, day = 0, dow = 0;
  int hour = 0, min = 0, sec = 0;

  // Try reading multiple times if it returns 0 (bus might be busy)
  for (int retry = 0; retry < 3; retry++) {
    RTC::getDate(year, month, day, dow);
    RTC::getTime(hour, min, sec);
    if (year >= 2000 && month >= 1 && day >= 1)
      break;
    delay(100);
  }

  Serial.printf("RTC time (Direct): %04d-%02d-%02d %02d:%02d:%02d\n", year,
                month, day, hour, min, sec);

  // Sync system time from RTC
  // CRITICAL: Only sync if we have a valid date!
  // (Month/Day = 0 causes mktime to underflow to 1999)
  if (year >= 2000 && year < 2100 && month >= 1 && day >= 1) {
    struct tm tm;
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min = min;
    tm.tm_sec = sec;
    tm.tm_isdst = -1;
    time_t t = mktime(&tm);
    if (t != (time_t)-1) {
      struct timeval now_tv = {.tv_sec = t, .tv_usec = 0};
      settimeofday(&now_tv, NULL);
      Serial.printf("System time synced from RTC: %ld\n", (long)t);
    } else {
      Serial.println("Error: mktime failed!");
    }
  } else {
    Serial.printf("Warning: RTC date invalid or not set yet: %d-%d-%d\n", year,
                  month, day);
    Serial.println("System time NOT synced. Using epoch.");
  }

  // Initialize hardware components
  initHardware();

  // Initialize SD card
  initSD();

  // Load configuration
  config = new Config();
  if (!config->load("/config/settings.json")) {
    Serial.println("Using default configuration");
    config->setDefaults();
  }

  // Initialize UI
  uiManager = new UIManager();
  uiManager->init();

  // Initialize BLE client for Fossibot
  initBLE();

  // Show home screen
  uiManager->showHomeScreen();

  // Force immediate UI update (don't wait for loop)
  uiManager->update();

  Serial.println("Initialization complete!");
}

// Manual GT911 Reader (Wire on SDA:41, SCL:42 - shared with RTC)
bool readTouchManual(int *x, int *y) {
  uint8_t raw[7];

  // Read Status Register 0x814E
  Wire.beginTransmission(0x5D);
  Wire.write(0x81);
  Wire.write(0x4E);
  if (Wire.endTransmission() != 0)
    return false;

  if (Wire.requestFrom(0x5D, 1) != 1)
    return false;
  uint8_t status = Wire.read();

  // Debug: Print status if it's not 0 (idle) or 0x80 (touched but 0 points?
  // unlikely)
  if (status != 0) {
    Serial.printf("GT911 Status: 0x%02X\n", status);
  }

  if ((status & 0x80) && (status & 0x07) > 0) {
    // Touch detected, read point 1 (starts at 0x8150)
    Wire.beginTransmission(0x5D);
    Wire.write(0x81);
    Wire.write(0x50);
    Wire.endTransmission();

    // Read 7 bytes (TrackID, XL, XH, YL, YH, SizeL, SizeH)
    Wire.requestFrom(0x5D, 7);
    for (int i = 0; i < 7; i++)
      raw[i] = Wire.read();

    // Parse Raw GT911 Data
    // Offset 0 is XL, Offset 1 is XH
    int raw_x = raw[0] + (raw[1] << 8);
    // Offset 2 is YL, Offset 3 is YH
    int raw_y = raw[2] + (raw[3] << 8);

    // --- COORDINATE TRANSFORMATION ---
    // M5Paper S3 Touch Panel is 540x960 (Portrait)
    // Screen is 960x540 (Landscape)
    // Mapping derived from logs:
    // Screen X = Raw Y (Long Axis) - Low Y is Left (USB), High Y is Right
    // (Menu) Screen Y = 540 - Raw X (Inverted Short Axis) - Low X is Top, High
    // X is Bottom

    *x = raw_y;
    *y = 540 - raw_x;

    // Clamp to boundaries
    if (*x < 0)
      *x = 0;
    if (*x > 960)
      *x = 960;
    if (*y < 0)
      *y = 0;
    if (*y > 540)
      *y = 540;

    Serial.printf("TOUCH: Mapped(%d, %d) Raw(%d, %d)\n", *x, *y, raw_x, raw_y);

    // Clear Status Register
    Wire.beginTransmission(0x5D);
    Wire.write(0x81);
    Wire.write(0x4E);
    Wire.write(0x00);
    Wire.endTransmission();
    return true;
  }

  // Clear Status even if invalid to release INT line
  Wire.beginTransmission(0x5D);
  Wire.write(0x81);
  Wire.write(0x4E);
  Wire.write(0x00);
  Wire.endTransmission();

  return false;
}

void loop() {
  // Heartbeat every 5 seconds
  static unsigned long lastHeartbeat = 0;
  if (millis() - lastHeartbeat > 5000) {
    Serial.println("--- System Alive (Heartbeat) ---");
    // Serial.printf("Raw INT Pin (48): %d\n", digitalRead(48));
    lastHeartbeat = millis();
  }

  // Update M5 (buttons, touch, etc.)
  M5.update();

  // --- TOUCH POLLING MODE (Bypass INT pin) ---
  // Try to read touch every ~15ms for smooth drawing
  static unsigned long lastTouchPoll = 0;
  static bool wasTouching = false;
  static int lastTouchX = 0;
  static int lastTouchY = 0;

  if (millis() - lastTouchPoll > 15) {
    lastTouchPoll = millis();

    int tx, ty;
    bool touching = readTouchManual(&tx, &ty);

    // Feed raw touch state to UI Manager for continuous drawing
    if (touching && tx > 0 && tx < 960 && ty > 0 && ty < 540) {
      uiManager->setTouchState(tx, ty, true);
    } else {
      uiManager->setTouchState(lastTouchX, lastTouchY, false);
    }

    if (touching && tx > 0 && tx < 960 && ty > 0 && ty < 540) {
      // Serial.printf("POLL TOUCH: %d, %d (INT Pin: %d)\n", tx, ty,
      //               digitalRead(48));

      if (!wasTouching) {
        // New touch started - send PRESS
        uiManager->handleTouch(tx, ty, TouchEvent::PRESS);
        Serial.println("EVENT: PRESS");
      }
      wasTouching = true;
      lastTouchX = tx;
      lastTouchY = ty;
    } else {
      // No touch detected
      if (wasTouching) {
        // Touch just ended - send RELEASE (this triggers the action!)
        uiManager->handleTouch(lastTouchX, lastTouchY, TouchEvent::RELEASE);
        Serial.println("EVENT: RELEASE");
      }
      wasTouching = false;
    }
  }

  // Update BLE data (handles reconnection)
  if (bleClient) {
    bleClient->update();

    // Update UI data if connected
    if (bleClient->isConnected()) {
      uiManager->updatePowerBankData(bleClient->getData());
    }
  }

  // Update UI (handles its own refresh timing)
  uiManager->update();

  // Small delay to prevent tight loop
  delay(10);
}

void initHardware() {
  Serial.println("Initializing hardware...");

  // Display is already initialized by M5Unified
  Serial.printf("Display: %dx%d\n", M5.Display.width(), M5.Display.height());

  // Clear display
  M5.Display.fillScreen(TFT_WHITE);
  M5.Display.setTextColor(TFT_BLACK);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(10, 10);
  M5.Display.println("M5Paper S3 Starting...");

  // Initialize buzzer (GPIO for buzzer on M5Paper S3)
  // Will be implemented in buzzer.cpp

  Serial.println("Hardware initialized");
}

void initSD() {
  Serial.println("Initializing SD card...");

  sdManager = new SDManager();
  if (sdManager->init()) {
    Serial.println("SD card initialized successfully");

    // Create required directories if they don't exist
    sdManager->ensureDirectory("/config");
    sdManager->ensureDirectory("/books");
    sdManager->ensureDirectory("/notes");
    sdManager->ensureDirectory("/games");
    sdManager->ensureDirectory("/games/saves");
    sdManager->ensureDirectory("/fonts");
  } else {
    Serial.println("WARNING: SD card initialization failed!");
    Serial.println("Some features may not work properly.");
  }
}

void initBLE() {
  Serial.println("Initializing BLE...");

  bleClient = new FossibotBLE();

  // Get MAC address from config
  String macAddress = config->getFossibotMAC();

  if (macAddress.length() > 0) {
    Serial.printf("Fossibot MAC: %s\n", macAddress.c_str());
    bleClient->setTargetMAC(macAddress);
    bleClient->init();
    bleClient->startScan();
  } else {
    Serial.println("No Fossibot MAC configured. BLE disabled.");
    Serial.println("Configure MAC address in /config/settings.json");
  }
}
