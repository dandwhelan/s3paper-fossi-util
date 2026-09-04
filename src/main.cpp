/**
 * E-Rex - Multi-Feature Power Bank Display & Smart Assistant
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
#include "ble/switchbot_client.h"
#include "hardware/buzzer.h"
#include "hardware/display.h"
#include "hardware/rtc.h"
#include "hardware/touch.h"
#include "mqtt/mqtt_client.h"
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
void initMQTT();
void mainLoop();
void showBootScreen();
void resetGT911();
void recoverTouchBus();

// Consecutive I2C failures while talking to the GT911. The touch controller
// shares the bus with the RTC and can wedge (a NAK storm, a half-finished
// transaction); when it does, readTouchManual() returns false forever and the
// device looks crashed - the screen keeps showing whatever was last drawn and
// nothing responds to touch. Counted here so loop() can re-init the bus.
static uint32_t g_touchI2cErrors = 0;
static uint32_t g_touchBusRecoveries = 0;
static const uint32_t TOUCH_I2C_ERROR_LIMIT = 100;

// Global instances
UIManager *uiManager = nullptr;
FossibotBLE *bleClient = nullptr;
SwitchBotBLE *switchbotClient = nullptr;
GivEnergyMQTT *mqttClient = nullptr;
SDManager *sdManager = nullptr;
Config *config = nullptr;

void setup() {
#ifdef SERIAL_DEBUG
  // Wait for serial to be ready (important for S3 USB CDC)
  delay(1000);
  Serial.begin(115200);
  delay(500);
  Serial.println(" Booting E-Rex...");
#endif

  // Initialize M5Unified
  auto cfg = M5.config();
#ifdef SERIAL_DEBUG
  cfg.serial_baudrate = 115200;
#else
  // Production build: leave the port unopened. Most Serial.print calls in
  // this codebase are not wrapped in #ifdef SERIAL_DEBUG, so this (plus
  // ARDUINO_USB_CDC_ON_BOOT=0) is what actually silences them - writes to an
  // uninitialised UART are dropped without running the USB CDC stack.
  cfg.serial_baudrate = 0;
#endif
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

  // GT911 Reset moved to after boot screen to ensure freshness

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

  // Show boot screen (displays boot.png for 3 seconds)
  showBootScreen();

  // --- GT911 SOFTWARE RESET (Moved here to fix touch after 3s delay) ---
  Serial.println("Attempting GT911 Soft Reset (Post-Boot)...");
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

  // Load configuration
  config = new Config();
  bool configLoaded = true;
  if (!config->load("/config/settings.json")) {
    Serial.println("Failed to load /config/settings.json, trying /settings.json");
    if (!config->load("/settings.json")) {
      Serial.println("Using default configuration");
      config->setDefaults();
      configLoaded = false;
    }
  }

  // Show boot debug screen only when config didn't load - on a healthy boot
  // this just added 4 seconds of fixed delay to every wake
  if (!configLoaded) {
    M5.Display.fillScreen(TFT_WHITE);
    M5.Display.setTextColor(TFT_BLACK);
    M5.Display.setTextSize(2);
    M5.Display.setCursor(10, 30);
    M5.Display.println("--- Boot Debug Info ---");
    M5.Display.println("Loaded Config: NO (using defaults)");
    M5.Display.printf("Mode: %s\n", config->getMode().c_str());
    if (config->isHomeMode()) {
      M5.Display.printf("WiFi SSID: '%s'\n", config->getWiFiSSID().c_str());
      M5.Display.printf("MQTT Broker: '%s'\n", config->getMQTTBroker().c_str());
      M5.Display.printf("Inverter SN: '%s'\n", config->getInverterSN().c_str());
    } else {
      M5.Display.printf("Fossibot MAC: '%s'\n",
                        config->getFossibotMAC().c_str());
    }
    M5.Display.println("\nResuming in 4 seconds...");
    delay(4000);
  }

  // Initialize UI
  uiManager = new UIManager();
  uiManager->init();

  // Initialize connectivity based on mode. Clients are always created so the
  // Settings toggle can enable/disable the radio at runtime; the persisted
  // toggle decides whether the radio actually powers up at boot.
  if (config->isHomeMode()) {
    Serial.println("Mode: HOME (WiFi + MQTT / GivEnergy)");
    initMQTT();
  } else {
    Serial.println("Mode: CAMPERVAN (BLE / Fossibot)");
    initBLE();
  }

  // CRITICAL: Small delay for BLE/EPD coexistence stability
  // EPD display can crash if updated too soon after BLE radio ops
  delay(200);

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
  if (Wire.endTransmission() != 0) {
    g_touchI2cErrors++;
    return false;
  }

  if (Wire.requestFrom(0x5D, 1) != 1) {
    g_touchI2cErrors++;
    return false;
  }
  uint8_t status = Wire.read();

  // The controller answered, so the bus is healthy again.
  g_touchI2cErrors = 0;

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
    // A short read leaves Wire.read() returning -1, which would be parsed as
    // a wild coordinate - discard the sample instead.
    if (Wire.requestFrom(0x5D, 7) != 7) {
      g_touchI2cErrors++;
      return false;
    }
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
    if (*x > 959)
      *x = 959;
    if (*y < 0)
      *y = 0;
    if (*y > 539)
      *y = 539;

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
  // DEFERRED BLE CONNECTION: Only start after first screen draw is complete
  // This prevents EPD/BLE DMA contention crash
  static bool bleStarted = false;
  static unsigned long startupTime = millis();

  if (!bleStarted && bleClient && (millis() - startupTime > 5000)) {
    // Wait 2 seconds after boot for EPD to stabilize, then start BLE
    // DISABLED: BLE causes crash loop (Panel_EPD:611) - disabling to allow EPub
    // dev
    Serial.println("BLE: Auto-start enabled (2s delay elapsed).");
    bleClient->startScan();
    bleStarted = true;
  }

  // Heartbeat every 5 seconds
  static unsigned long lastHeartbeat = 0;
  if (millis() - lastHeartbeat > 5000) {
    Serial.println("--- System Alive (Heartbeat) ---");
    // Serial.printf("Raw INT Pin (48): %d\n", digitalRead(48));
    lastHeartbeat = millis();
  }

  // Update M5 (buttons, touch, etc.)
  // M5.update(); // DISABLED: Conflicts with manual I2C touch reading
  // (ESP_ERR_INVALID_STATE)

  // --- TOUCH POLLING MODE ---
  // Poll continuously instead of relying on the GT911 INT line. The project
  // docs note that the interrupt path has been unreliable on this hardware,
  // and if it never asserts the UI becomes completely untouchable.
  static unsigned long lastTouchPoll = 0;
  static bool wasTouching = false;
  static int lastTouchX = 0;
  static int lastTouchY = 0;

  // Poll at 15ms normally; relax to 30ms in eco mode (80MHz) to reduce I2C
  // traffic and CPU wakeups while idle. First touch exits eco mode, restoring
  // the fast poll rate.
  unsigned long touchPollInterval =
      (uiManager && uiManager->isEcoMode()) ? 30 : 15;
  if (millis() - lastTouchPoll > touchPollInterval) {
    lastTouchPoll = millis();

    // A wedged GT911 is indistinguishable from a crash to the user: the
    // dashboard freezes on its last frame and nothing responds. Re-init the
    // shared bus and soft-reset the controller rather than staying dead until
    // a power cycle.
    if (g_touchI2cErrors >= TOUCH_I2C_ERROR_LIMIT) {
      g_touchI2cErrors = 0;
      g_touchBusRecoveries++;
      Serial.printf("TOUCH: I2C wedged - recovering bus (recovery #%lu)\n",
                    (unsigned long)g_touchBusRecoveries);
      recoverTouchBus();
      wasTouching = false;
      if (uiManager)
        uiManager->forceRefresh();
    }

    int tx, ty;
    bool touching = readTouchManual(&tx, &ty);
    bool validTouch =
        touching && tx >= 0 && tx < 960 && ty >= 0 && ty < 540;

    if (validTouch) {
      // IMMEDIATE WAKEUP: Reduce input latency
      uiManager->exitEcoMode();
    }

    // Feed raw touch state to UI Manager for continuous drawing
    if (validTouch) {
      uiManager->setTouchState(tx, ty, true);
    } else {
      uiManager->setTouchState(lastTouchX, lastTouchY, false);
    }

    if (validTouch) {
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

  // Update connectivity based on mode
  if (bleClient) {
    // Campervan mode: BLE to Fossibot
    bleClient->update();

    // Push data every pass, connected or not: the struct carries the link
    // state, and the UI can only show a disconnect if it hears about it.
    // (Previously this was gated on isConnected(), so the dashboard kept
    // showing stale values as if the Fossibot were still linked.)
    uiManager->updatePowerBankData(bleClient->getData());

    // CRITICAL: Skip UI updates during BLE connection attempts to prevent
    // EPD/BLE crash
    if (bleClient->isConnecting()) {
      delay(50);
      return;
    }
  }

  // SwitchBot Bot over the Fossibot power button. Runs the queued press here
  // rather than in the touch handler, which must not block for a connect.
  if (switchbotClient) {
    switchbotClient->update();
    if (switchbotClient->takeStatusChanged()) {
      uiManager->forceRefresh();
    }
  }

  if (mqttClient) {
    // Home mode: WiFi + MQTT to GivEnergy
    mqttClient->update();

    if (mqttClient->isMQTTConnected()) {
      uiManager->updateSolarData(mqttClient->getData());
    }
  }

  // Update UI (handles its own refresh timing)
  uiManager->update();

  // Small delay to prevent tight loop. In eco mode the touch poll only runs
  // every 30ms, so waking three times in between is pure overhead - sleep up
  // to the next poll instead. delay() yields to FreeRTOS, so this is idle
  // time, not spin.
  if (uiManager && uiManager->isEcoMode()) {
    unsigned long sinceLastPoll = millis() - lastTouchPoll;
    delay(sinceLastPoll >= touchPollInterval
              ? 1
              : (touchPollInterval - sinceLastPoll));
  } else {
    delay(10);
  }
}

// Re-initialize the shared I2C bus and the touch controller after a wedge.
// Both the RTC (0x51) and the GT911 (0x5D) live on this bus, so the whole bus
// is torn down and brought back up.
void recoverTouchBus() {
  Wire.end();
  delay(10);
  Wire.begin(41, 42);
  Wire.setClock(400000);
  delay(10);
  resetGT911();
  delay(50);
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
  M5.Display.println("E-Rex Starting...");

  // Initialize buzzer (GPIO for buzzer on M5Paper S3)
  // Will be implemented in buzzer.cpp

  Serial.println("Hardware initialized");
}

void showBootScreen() {
  Serial.println("E-Rex: Showing boot screen...");

  if (!SD.exists("/boot.png")) {
    Serial.println("Boot screen: /boot.png not found, skipping");
    return;
  }

  // Read PNG from SD card into PSRAM to save internal RAM
  File bootFile = SD.open("/boot.png", FILE_READ);
  if (!bootFile) {
    Serial.println("Boot screen: Failed to open /boot.png");
    return;
  }

  size_t fileSize = bootFile.size();

  // Allocate buffer in PSRAM (SPIRAM) to avoid OOM in internal memory
  uint8_t *buffer = (uint8_t *)heap_caps_malloc(fileSize, MALLOC_CAP_SPIRAM);
  if (!buffer) {
    Serial.println("Boot screen: Failed to allocate PSRAM for boot image");
    bootFile.close();
    return;
  }

  bootFile.read(buffer, fileSize);
  bootFile.close();

  // Clear screen and draw boot image
  M5.Display.fillScreen(TFT_WHITE);
  M5.Display.drawPng(buffer, fileSize, 0, 0);

  free(buffer);

  // Wait 3 seconds
  delay(3000);

  Serial.println("E-Rex: Boot complete");
}

// Re-initialize Touch (Manual Soft Reset) after delay to ensure it's
// responsive This fixes touch issues if the panel went to sleep or lost sync
// during the 3s delay
void resetGT911() {
  Wire.beginTransmission(0x5D);
  Wire.write(0x80);
  Wire.write(0x40);
  Wire.write(0x02); // Soft Reset
  Wire.endTransmission();
  delay(10);
  Wire.beginTransmission(0x5D);
  Wire.write(0x80);
  Wire.write(0x40);
  Wire.write(0x00); // Clear Reset
  Wire.endTransmission();
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

  // SwitchBot Bot over the physical power button. Created before the
  // Fossibot MAC check below, because the power-on button is exactly what you
  // need when there is no Fossibot to talk to yet.
  switchbotClient = new SwitchBotBLE();
  switchbotClient->setTargetMAC(config->getSwitchbotMAC());
  switchbotClient->setPassword(config->getSwitchbotPassword());

  // Get MAC address from config
  String macAddress = config->getFossibotMAC();

  if (macAddress.length() == 0) {
    Serial.println("No Fossibot MAC configured. BLE disabled.");
    Serial.println("Configure MAC address in /config/settings.json");
    return;
  }

  Serial.printf("Fossibot MAC: %s\n", macAddress.c_str());
  bleClient->setTargetMAC(macAddress);

  if (!config->getBluetoothEnabled()) {
    // User turned Bluetooth off in Settings - leave the radio powered down.
    // The Settings toggle re-initializes it on demand.
    Serial.println("BLE: Disabled in settings - radio off");
    return;
  }

  // Small delay to let BLE radio stabilize on cold boot
  delay(500);
  bleClient->init();
  // NOTE: startScan() is now called in loop() AFTER first screen draw
  // to prevent EPD/BLE DMA contention crash
  Serial.println(
      "BLE: Init complete. Connection will start after first draw.");
}

void initMQTT() {
  Serial.println("Initializing WiFi + MQTT (Home Mode)...");

  String ssid = config->getWiFiSSID();
  String password = config->getWiFiPassword();
  String broker = config->getMQTTBroker();
  int port = config->getMQTTPort();
  String inverterSN = config->getInverterSN();

  if (ssid.isEmpty() || broker.isEmpty() || inverterSN.isEmpty()) {
    Serial.println("MQTT: Missing config (WiFi SSID, MQTT broker, or inverter SN)");
    Serial.println("Configure in /config/settings.json");
    return;
  }

  mqttClient = new GivEnergyMQTT();

  if (!config->getWiFiEnabled()) {
    // User turned WiFi off in Settings - keep the radio powered down.
    // The Settings toggle re-enables it on demand.
    mqttClient->setRadioEnabled(false);
  }

  mqttClient->init(ssid, password, broker, port, config->getMQTTUsername(),
                   config->getMQTTPassword(), inverterSN);

  Serial.println(config->getWiFiEnabled()
                     ? "MQTT: Init complete. WiFi connecting..."
                     : "MQTT: Init complete. WiFi disabled in settings.");
}
