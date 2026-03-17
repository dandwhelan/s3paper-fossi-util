/**
 * BLE Client Implementation
 */

#include "ble_client.h"
#include <M5Unified.h> // Added for waitDisplay() guard
#include <cstring>

// Static instance for callbacks
FossibotBLE *FossibotBLE::_instance = nullptr;

FossibotBLE::FossibotBLE()
    : _client(nullptr), _service(nullptr), _writeChar(nullptr),
      _notifyChar(nullptr), _initialized(false), _connected(false),
      _scanning(false), _connecting(false), _autoReconnect(false), _lastPoll(0),
      _lastSettingsPoll(0), _lastReconnectAttempt(0), _retryStartTime(0),
      _consecutiveFailures(0), _firstAttemptDone(false),
      _bootAutoEnableTriggered(false), _socThreshold(1), _powerThreshold(5),
      _autoEnableState(AutoEnableState::IDLE), _autoEnableTimer(0) {
  _instance = this;
}

FossibotBLE::~FossibotBLE() {
  disconnect();
  if (_instance == this) {
    _instance = nullptr;
  }
}

void FossibotBLE::init() {
  if (_initialized)
    return;

  // CRITICAL: Wait for system to fully settle before BLE init
  // This prevents stack overflow crash in btdm_intr_alloc
  Serial.println("BLE: Waiting for system to settle...");
  delay(500);

  Serial.println("BLE: Initializing NimBLE...");
  NimBLEDevice::init("M5PaperS3");
  NimBLEDevice::setPower(ESP_PWR_LVL_P3);
  NimBLEDevice::setSecurityAuth(false, false, false);

  delay(100); // Brief settle after init

  _initialized = true;
  Serial.println("BLE: Initialized");
}

void FossibotBLE::setTargetMAC(const String &mac) {
  _targetMAC = mac;
  _targetAddress = NimBLEAddress(_targetMAC.c_str());
  Serial.printf("BLE: Target MAC set to %s\n", _targetMAC.c_str());
}

void FossibotBLE::startScan() {
  // startScan implementation removed/deferred in main.cpp, but kept here for
  // API compat if needed
  if (!_initialized || _targetMAC.length() == 0) {
    Serial.println("BLE: Cannot scan - not initialized or no target MAC");
    return;
  }

  Serial.println("BLE: startScan() called - enabling autoReconnect");
  Serial.printf("BLE: State before connect: autoReconnect=%d, connected=%d, "
                "firstAttemptDone=%d\n",
                _autoReconnect, _connected, _firstAttemptDone);

  // Directly call connect logic which now handles guards
  _autoReconnect = true;
  bool result = connectToDevice();

  Serial.printf("BLE: startScan() initial connect result: %s\n",
                result ? "SUCCESS" : "FAILED");
  Serial.printf("BLE: State after connect: autoReconnect=%d, connected=%d, "
                "firstAttemptDone=%d\n",
                _autoReconnect, _connected, _firstAttemptDone);
}

void FossibotBLE::stopScan() {
  NimBLEDevice::getScan()->stop();
  _scanning = false;
  _autoReconnect = false;
}

bool FossibotBLE::connectToDevice() {
  if (_connected)
    return true;

  // CRITICAL: Boost CPU for BLE operations - 80MHz Eco Mode causes connection
  // failures
  if (getCpuFrequencyMhz() < 240) {
    setCpuFrequencyMhz(240);
    Serial.println("BLE: Boosted CPU to 240MHz for connection");
  }

  // CRITICAL FIX: Wait for EPD to be fully idle before starting BLE radio ops
  Serial.println("BLE: Waiting for EPD idle...");
  M5.Display.waitDisplay();
  Serial.println("BLE: EPD idle. Starting connection...");

  _connecting = true; // EPD contention guard - set BEFORE connection attempt

  Serial.printf("BLE: Connecting to %s...\n", _targetMAC.c_str());

  // Create client only if needed
  if (!_client) {
    _client = NimBLEDevice::createClient();
    _client->setClientCallbacks(this);
    _client->setConnectionParams(24, 48, 0,
                                 400); // 24-48ms interval (power optimized)
    _client->setConnectTimeout(
        3); // 3 second timeout - keep short for touch responsiveness
  }

  // Direct connect without scanning (we know the MAC)
  Serial.println("BLE: Connecting...");
  bool connected = _client->connect(_targetAddress, false);

  if (!connected) {
    Serial.println("BLE: Failed to connect");
    _connecting = false; // Clear flag FIRST

    // DON'T delete the client - just leave it for reuse on next attempt
    // NimBLE deleteClient() can hang on ESP32-S3 after failed connection
    Serial.println("BLE: Keeping client for retry");

    // Restore CPU to save power during retry wait
    setCpuFrequencyMhz(80);
    Serial.println("BLE: Restored CPU to 80MHz");

    return false;
  }

  // Discover services
  if (!discoverServices()) {
    _connecting = false; // Connection attempt finished
    disconnect();
    return false;
  }

  _connected = true;
  _data.connected = true;
  _connecting =
      false; // CRITICAL FIX: Allow UI updates to resume after connection

  // Request initial data
  requestStatusData();

  // Boot auto-enable: Initialize state machine if within window and not
  // triggered yet
  if (!_bootAutoEnableTriggered && millis() < BOOT_AUTO_ENABLE_WINDOW_MS) {
    Serial.println("BLE: Boot auto-enable - initializing state machine "
                   "(waiting for data)");
    _autoEnableState = AutoEnableState::WAITING_FOR_DATA;
  } else if (_bootAutoEnableTriggered) {
    Serial.println("BLE: Boot auto-enable already triggered - skipping");
  } else {
    Serial.printf(
        "BLE: Boot auto-enable window expired (uptime: %lu ms > %lu ms)\n",
        millis(), BOOT_AUTO_ENABLE_WINDOW_MS);
  }

  return true;
}

bool FossibotBLE::discoverServices() {
  Serial.println("BLE: Discovering services...");

  // Get service
  _service = _client->getService(Fossibot::SERVICE_UUID);
  if (!_service) {
    Serial.println("BLE: Service not found!");
    return false;
  }
  Serial.println("BLE: Service found");

  // Get write characteristic
  _writeChar = _service->getCharacteristic(Fossibot::WRITE_CHAR_UUID);
  if (!_writeChar) {
    Serial.println("BLE: Write characteristic not found!");
    return false;
  }
  Serial.println("BLE: Write characteristic found");

  // Get notify characteristic
  _notifyChar = _service->getCharacteristic(Fossibot::NOTIFY_CHAR_UUID);
  if (!_notifyChar) {
    Serial.println("BLE: Notify characteristic not found!");
    return false;
  }
  Serial.println("BLE: Notify characteristic found");

  // Subscribe to notifications
  if (_notifyChar->canNotify()) {
    _notifyChar->subscribe(true, notifyCallback);
    Serial.println("BLE: Subscribed to notifications");
  }

  return true;
}

void FossibotBLE::disconnect() {
  if (_client && _client->isConnected()) {
    _client->disconnect();
  }

  _connected = false;
  _data.connected = false;
  _service = nullptr;
  _writeChar = nullptr;
  _notifyChar = nullptr;
}

void FossibotBLE::cleanupClient() {
  Serial.println("BLE: Soft cleanup (keeping client)...");
  // DON'T delete the client - NimBLE deleteClient() can hang on ESP32-S3
  // Just reset the connection state
  _connected = false;
  _data.connected = false;
  _service = nullptr;
  _writeChar = nullptr;
  _notifyChar = nullptr;
  Serial.println("BLE: Cleanup complete");
}

void FossibotBLE::update() {
  if (!_initialized)
    return;

  // Yield to system periodically
  yield();

  // Auto-enable state machine
  if (_autoEnableState != AutoEnableState::IDLE) {
    // Safety check: abort if disconnected
    if (!_connected) {
      Serial.println("BLE: Disconnected during auto-enable, aborting");
      _autoEnableState = AutoEnableState::IDLE;
    } else {
      switch (_autoEnableState) {
      case AutoEnableState::WAITING_FOR_DATA:
        // Check if we have received valid data (battery voltage > 0 is a good
        // proxy)
        if (_data.batteryVoltage > 0.0f) {
          Serial.println("BLE: Status received, processing auto-enable...");
          _autoEnableState = AutoEnableState::CHECK_AC;
        }
        break;

      case AutoEnableState::CHECK_AC:
        if (!_data.acActive) {
          Serial.println("BLE: Auto-enable AC (turning ON)");
          toggleAC();
          _autoEnableTimer = millis();
          _autoEnableState = AutoEnableState::WAIT_AC;
        } else {
          Serial.println("BLE: Auto-enable AC (already ON, skipping)");
          _autoEnableState = AutoEnableState::CHECK_DC;
        }
        break;

      case AutoEnableState::WAIT_AC:
        if (millis() - _autoEnableTimer >= 2000) {
          _autoEnableState = AutoEnableState::CHECK_DC;
        }
        break;

      case AutoEnableState::CHECK_DC:
        if (!_data.dcActive) {
          Serial.println("BLE: Auto-enable DC (turning ON)");
          toggleDC();
          _autoEnableTimer = millis();
          _autoEnableState = AutoEnableState::WAIT_DC;
        } else {
          Serial.println("BLE: Auto-enable DC (already ON, skipping)");
          _autoEnableState = AutoEnableState::CHECK_USB;
        }
        break;

      case AutoEnableState::WAIT_DC:
        if (millis() - _autoEnableTimer >= 2000) {
          _autoEnableState = AutoEnableState::CHECK_USB;
        }
        break;

      case AutoEnableState::CHECK_USB:
        if (!_data.usbActive) {
          Serial.println("BLE: Auto-enable USB (turning ON)");
          toggleUSB();
        } else {
          Serial.println("BLE: Auto-enable USB (already ON, skipping)");
        }
        _autoEnableState = AutoEnableState::DONE;
        break;

      case AutoEnableState::DONE:
        Serial.println("BLE: Boot auto-enable complete");
        _bootAutoEnableTriggered = true;
        _autoEnableState = AutoEnableState::IDLE;
        break;

      default:
        break;
      }
    }
  }

  // Try to reconnect if disconnected
  if (_autoReconnect && !_connected) {
    const unsigned long RETRY_TIMEOUT_MS =
        55 * 60 * 1000; // 55 minutes (before 60 min deep sleep)
    const unsigned long RETRY_INTERVAL_MS =
        30000; // 30 seconds between attempts

    // Initialize retry cycle timing on first attempt
    if (!_firstAttemptDone) {
      Serial.println("BLE: First retry cycle - initializing timers");
      _firstAttemptDone = true;
      _retryStartTime = millis(); // Start the 55 min timeout
      _lastReconnectAttempt = millis();
      return; // Let the main loop run before retrying
    }

    // Stop retrying after 55 minutes (deep sleep kicks in at 60 min)
    if (millis() - _retryStartTime > RETRY_TIMEOUT_MS) {
      Serial.println("BLE: Retry timeout (55 min) - stopping until deep sleep");
      return;
    }

    unsigned long timeSinceLastAttempt = millis() - _lastReconnectAttempt;
    if (timeSinceLastAttempt > RETRY_INTERVAL_MS) {
      _lastReconnectAttempt = millis();
      unsigned long elapsedMin = (millis() - _retryStartTime) / 60000;
      unsigned long elapsedSec = ((millis() - _retryStartTime) / 1000) % 60;
      Serial.printf("BLE: Retry attempt #%d (elapsed: %lu:%02lu / 55:00)\n",
                    _consecutiveFailures + 1, elapsedMin, elapsedSec);

      // Every 10 failures, do a full cleanup
      if (_consecutiveFailures > 0 && _consecutiveFailures % 10 == 0) {
        Serial.println("BLE: Performing client cleanup after 10 failures...");
        cleanupClient();
        delay(500);
        yield();
      }

      if (!connectToDevice()) {
        _consecutiveFailures++;
        Serial.printf("BLE: Failed (%d total). Next retry in %lu seconds\n",
                      _consecutiveFailures, RETRY_INTERVAL_MS / 1000);
      } else {
        _consecutiveFailures = 0; // Reset on success
        Serial.println("BLE: Connection successful!");
      }
    }
    return;
  } else if (!_autoReconnect && !_connected) {
    // Debug: Log why we're not retrying
    static unsigned long lastDebugLog = 0;
    if (millis() - lastDebugLog > 10000) { // Log every 10s
      Serial.println("BLE: Not retrying - autoReconnect is disabled");
      lastDebugLog = millis();
    }
  }

  unsigned long now = millis();

  // Poll status data (every 30s)
  if (now - _lastPoll >= POLL_INTERVAL) {
    _lastPoll = now;
    requestStatusData();
  }

  // Poll settings data (every 60s)
  if (now - _lastSettingsPoll >= SETTINGS_POLL_INTERVAL) {
    _lastSettingsPoll = now;
    requestSettingsData();
  }
}

bool FossibotBLE::hasSignificantChange() const {
  return _data.hasSignificantChange(_socThreshold, _powerThreshold);
}

void FossibotBLE::requestStatusData() {
  if (!_connected || !_writeChar)
    return;

  // Read 80 input registers starting from 0: [0x11, 0x04, 0x00, 0x00, 0x00,
  // 0x50] Based on ESP-FBot source code
  uint8_t payload[6] = {0x11, 0x04, 0x00, 0x00, 0x00, 0x50};

  // CRC-16 Modbus calculation (from ESP-FBot)
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < 6; i++) {
    crc ^= payload[i];
    for (int j = 0; j < 8; j++) {
      if (crc & 1) {
        crc = (crc >> 1) ^ 0xA001;
      } else {
        crc >>= 1;
      }
    }
  }

  uint8_t command[8];
  memcpy(command, payload, 6);
  command[6] = (crc >> 8) & 0xFF; // CRC high byte
  command[7] = crc & 0xFF;        // CRC low byte

  _writeChar->writeValue(command, sizeof(command), false);
  Serial.println("BLE: Requested status data");
}

void FossibotBLE::requestSettingsData() {
  if (!_connected || !_writeChar)
    return;

  // Read 80 holding registers starting from 0: [0x11, 0x03, 0x00, 0x00, 0x00,
  // 0x50]
  uint8_t payload[6] = {0x11, 0x03, 0x00, 0x00, 0x00, 0x50};

  // CRC-16 Modbus calculation
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < 6; i++) {
    crc ^= payload[i];
    for (int j = 0; j < 8; j++) {
      if (crc & 1) {
        crc = (crc >> 1) ^ 0xA001;
      } else {
        crc >>= 1;
      }
    }
  }

  uint8_t command[8];
  memcpy(command, payload, 6);
  command[6] = (crc >> 8) & 0xFF;
  command[7] = crc & 0xFF;

  _writeChar->writeValue(command, sizeof(command), false);
  Serial.println("BLE: Requested settings data");
}

void FossibotBLE::sendCommand(uint8_t reg, uint16_t value) {
  if (!_connected || !_writeChar)
    return;

  // Build command packet using Modbus Write Single Register format
  // Format: [0x11, 0x06, RegHigh, RegLow, ValueHigh, ValueLow, CRC_Low,
  // CRC_High]
  uint8_t payload[6] = {
      0x11,                   // Device address
      0x06,                   // Function code: Write Single Register
      0x00,                   // Register high byte (always 0 for low registers)
      reg,                    // Register low byte
      (uint8_t)(value >> 8),  // Value high byte
      (uint8_t)(value & 0xFF) // Value low byte
  };

  // CRC-16 Modbus calculation (same as requestStatusData)
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < 6; i++) {
    crc ^= payload[i];
    for (int j = 0; j < 8; j++) {
      if (crc & 1) {
        crc = (crc >> 1) ^ 0xA001;
      } else {
        crc >>= 1;
      }
    }
  }

  uint8_t command[8];
  memcpy(command, payload, 6);
  command[6] = (crc >> 8) & 0xFF; // CRC high byte first (Fossibot protocol)
  command[7] = crc & 0xFF;        // CRC low byte second

  bool success = _writeChar->writeValue(command, sizeof(command), false);
  if (success) {
    Serial.printf("BLE: Sent command reg=%d value=%d (CRC=0x%04X)\n", reg,
                  value, crc);
  } else {
    Serial.printf("BLE: ERROR Failed to send command reg=%d value=%d\n", reg,
                  value);
  }
}

void FossibotBLE::toggleUSB() {
  sendCommand(Fossibot::ControlReg::USB_TOGGLE, _data.usbActive ? 0 : 1);
}

void FossibotBLE::toggleDC() {
  sendCommand(Fossibot::ControlReg::DC_TOGGLE, _data.dcActive ? 0 : 1);
}

void FossibotBLE::toggleAC() {
  sendCommand(Fossibot::ControlReg::AC_TOGGLE, _data.acActive ? 0 : 1);
}

// ============================================================
// Fossibot Settings Commands Implementation
// ============================================================

void FossibotBLE::setBuzzerEnabled(bool enabled) {
  sendCommand(Fossibot::ControlReg::KEY_SOUND, enabled ? 1 : 0);
  Serial.printf("BLE: Buzzer %s\n", enabled ? "enabled" : "disabled");
}

void FossibotBLE::setSilentCharging(bool enabled) {
  sendCommand(Fossibot::ControlReg::SILENT_CHARGING, enabled ? 1 : 0);
  Serial.printf("BLE: Silent charging %s\n", enabled ? "enabled" : "disabled");
}

void FossibotBLE::setLightMode(int mode) {
  if (mode < 0)
    mode = 0;
  if (mode > 3)
    mode = 3;
  sendCommand(Fossibot::ControlReg::LIGHT_MODE, mode);
  const char *modeNames[] = {"OFF", "ON", "FLASH", "SOS"};
  Serial.printf("BLE: Light mode set to %s\n", modeNames[mode]);
}

void FossibotBLE::setDischargeLimit(int percent) {
  if (percent < 0)
    percent = 0;
  if (percent > 30)
    percent = 30;
  // Value is in 0.1% units (e.g., 100 = 10%)
  sendCommand(Fossibot::ControlReg::DISCHARGE_LIMIT, percent * 10);
  Serial.printf("BLE: Discharge limit set to %d%%\n", percent);
}

void FossibotBLE::setChargeLimit(int percent) {
  if (percent < 60)
    percent = 60;
  if (percent > 100)
    percent = 100;
  // Value is in 0.1% units (e.g., 1000 = 100%)
  sendCommand(Fossibot::ControlReg::CHARGE_LIMIT, percent * 10);
  Serial.printf("BLE: Charge limit set to %d%%\n", percent);
}

void FossibotBLE::setScreenTimeout(int minutes) {
  if (minutes < 0)
    minutes = 0;
  sendCommand(Fossibot::ControlReg::SCREEN_TIMEOUT, minutes);
  Serial.printf("BLE: Screen timeout set to %d minutes\n", minutes);
}

void FossibotBLE::setSysStandby(int minutes) {
  if (minutes < 0)
    minutes = 0;
  sendCommand(Fossibot::ControlReg::SYS_STANDBY, minutes);
  Serial.printf("BLE: System standby set to %d minutes\n", minutes);
}

void FossibotBLE::setACStandby(int minutes) {
  if (minutes < 0)
    minutes = 0;
  sendCommand(Fossibot::ControlReg::AC_STANDBY, minutes);
  Serial.printf("BLE: AC standby set to %d minutes\n", minutes);
}

void FossibotBLE::setDCStandby(int minutes) {
  if (minutes < 0)
    minutes = 0;
  sendCommand(Fossibot::ControlReg::DC_STANDBY, minutes);
  Serial.printf("BLE: DC standby set to %d minutes\n", minutes);
}

void FossibotBLE::setUSBStandby(int seconds) {
  if (seconds < 0)
    seconds = 0;
  sendCommand(Fossibot::ControlReg::USB_STANDBY, seconds);
  Serial.printf("BLE: USB standby set to %d seconds\n", seconds);
}

void FossibotBLE::powerOff() {
  Serial.println("BLE: Sending Power OFF command (1)...");
  sendCommand(Fossibot::ControlReg::POWER_OFF, 1);
  delay(200); // Ensure command goes out before any state change
  Serial.println("BLE: Power off command sent!");
}

void FossibotBLE::setScheduleCharge(int minutes) {
  if (minutes < 0)
    minutes = 0;
  sendCommand(Fossibot::ControlReg::SCHEDULE_CHARGE, minutes);
  Serial.printf("BLE: Schedule charge set to %d minutes from now\n", minutes);
}

void FossibotBLE::notifyCallback(NimBLERemoteCharacteristic *characteristic,
                                 uint8_t *data, size_t length, bool isNotify) {
  if (!_instance)
    return;

  Serial.printf("BLE: Received %d bytes\n", length);

  // Check opcode
  if (length >= 2) {
    uint16_t opcode = (data[0] << 8) | data[1];

    if (opcode == Fossibot::OPCODE_STATUS) {
      _instance->parseStatusData(data, length);
    } else if (opcode == Fossibot::OPCODE_SETTINGS) {
      _instance->parseSettingsData(data, length);
    }
  }
}

void FossibotBLE::parseStatusData(const uint8_t *data, size_t length) {
  // Parse status registers from response
  // Based on ESP-FBot source: registers start at byte offset 6
  // Each register is 2 bytes, big-endian

  if (length < 10)
    return; // Minimum valid response

  // ESP-FBot shows: registers start at byte 6 (after header bytes)
  // getRegValue helper to extract register at given index
  auto getRegValue = [data, length](uint16_t regIndex) -> uint16_t {
    uint16_t offset = 6 + (regIndex * 2);
    if (offset + 1 >= length)
      return 0;
    return (data[offset] << 8) | data[offset + 1];
  };

  // Parse values using ESP-FBot register mappings:
  // Register 3 = AC input watts
  // Register 4 = DC input watts
  // Register 6 = Total input watts
  // Register 39 = Output watts (NOT 20 - that's total_watts which includes
  // system) Register 22 = Battery voltage Register 41 = State flags Register 56
  // = Battery percent (divide by 10)

  _data.acInputPower = getRegValue(3);
  _data.dcInputPower = getRegValue(4);
  _data.inputPower = getRegValue(6);
  _data.outputPower = getRegValue(39); // Fixed: was using reg 20
  _data.batteryVoltage = getRegValue(22) / 100.0f;
  _data.batteryPercent = getRegValue(56) / 10.0f;

  // Error & protection registers
  _data.errorCode = getRegValue(Fossibot::StatusReg::ERROR_CODE);
  _data.protectionFlags = getRegValue(Fossibot::StatusReg::PROTECTION_FLAGS);
  _data.systemStatusFlags = getRegValue(Fossibot::StatusReg::SYSTEM_STATUS_FLAGS);

  // Parse USB per-port watts (all ÷10) and sum for total USB output
  float usbTotal = 0.0f;
  usbTotal += getRegValue(Fossibot::StatusReg::USB_A1_WATTS) / 10.0f;
  usbTotal += getRegValue(Fossibot::StatusReg::USB_A2_WATTS) / 10.0f;
  usbTotal += getRegValue(Fossibot::StatusReg::USB_C1_WATTS) / 10.0f;
  usbTotal += getRegValue(Fossibot::StatusReg::USB_C2_WATTS) / 10.0f;
  usbTotal += getRegValue(Fossibot::StatusReg::USB_C3_WATTS) / 10.0f;
  usbTotal += getRegValue(Fossibot::StatusReg::USB_C4_WATTS) / 10.0f;
  _data.usbOutputPower = usbTotal;

  // Derive AC+DC output: total minus USB (clamped to 0)
  float nonUsb = _data.outputPower - usbTotal;
  _data.acDcOutputPower = (nonUsb > 0.0f) ? nonUsb : 0.0f;

  // Parse output states from bitmask (register 41)
  uint16_t states = getRegValue(41);
  _data.usbActive = (states & Fossibot::StateBits::USB_BIT) != 0;
  _data.dcActive = (states & Fossibot::StateBits::DC_BIT) != 0;
  _data.acActive = (states & Fossibot::StateBits::AC_BIT) != 0;

  // Get time values from device (not calculated)
  // Register 58 = time to full (minutes)
  // Register 59 = remaining time / time to empty (minutes)
  _data.minutesToFull = getRegValue(58);
  _data.minutesToEmpty = getRegValue(59);

  Serial.printf("BLE: SOC=%.1f%% IN=%.0fW OUT=%.0fW TTF=%dm TTE=%dm\n",
                _data.batteryPercent, _data.inputPower, _data.outputPower,
                _data.minutesToFull, _data.minutesToEmpty);

  // Log error state with detailed classification
  if (_data.errorCode != 0) {
    bool isCriticalFault = (_data.protectionFlags & Fossibot::ProtectionBits::CRITICAL_FAULT) != 0;
    bool isEnvProtection = _data.isEnvironmentalProtection();
    Serial.printf("BLE: ERROR DETECTED! ErrCode=%d ProtFlags=0x%04X SysFlags=0x%04X [%s]\n",
                  _data.errorCode, _data.protectionFlags, _data.systemStatusFlags,
                  isEnvProtection ? "TEMP/SAFETY" :
                  isCriticalFault ? "HARDWARE FAULT" : "INVERTER FAULT");
  }
}

void FossibotBLE::parseSettingsData(const uint8_t *data, size_t length) {
  // Settings data parsing - same structure as status (0x1103)
  if (length < 10)
    return;

  // Helper to extract register value
  auto getRegValue = [data, length](uint16_t regIndex) -> uint16_t {
    uint16_t offset = 6 + (regIndex * 2);
    if (offset + 1 >= length)
      return 0;
    return (data[offset] << 8) | data[offset + 1];
  };

  // Parse Fossibot settings registers
  _data.lightMode = getRegValue(27);
  _data.buzzerEnabled = (getRegValue(56) == 1);
  _data.silentCharging = (getRegValue(57) == 1);
  _data.screenTimeout = getRegValue(59);
  _data.acStandby = getRegValue(60);
  _data.dcStandby = getRegValue(61);
  _data.usbStandby = getRegValue(62);
  _data.scheduleCharge = getRegValue(63);
  _data.dischargeLimit = getRegValue(66) / 10; // Convert from 0.1% to %
  _data.chargeLimit = getRegValue(67) / 10;    // Convert from 0.1% to %
  _data.sysStandby = getRegValue(68);
  _data.settingsReceived = true;

  Serial.printf("BLE: Settings received - Buzzer:%d Silent:%d Light:%d "
                "Charge:%d%% Discharge:%d%%\n",
                _data.buzzerEnabled, _data.silentCharging, _data.lightMode,
                _data.chargeLimit, _data.dischargeLimit);
}

// NimBLE callbacks
void FossibotBLE::onConnect(NimBLEClient *client) {
  Serial.println("BLE: Connected callback");
  _connected = true;
  _data.connected = true;
}

void FossibotBLE::onDisconnect(NimBLEClient *client) {
  // Only reset retry state if we were actually connected (not just a failed
  // attempt)
  bool wasConnected = _connected;
  Serial.printf("BLE: Disconnected callback (wasConnected=%d)\n", wasConnected);

  _connected = false;
  _data.connected = false;
  _service = nullptr;
  _writeChar = nullptr;
  _notifyChar = nullptr;

  // Only reset retry timers if we had an established connection that dropped
  // This prevents failed connection attempts from resetting the retry cycle
  if (wasConnected) {
    Serial.println(
        "BLE: Was connected - resetting retry state for fresh cycle");
    _consecutiveFailures = 0;
    _firstAttemptDone = false;
    _retryStartTime = 0;
  } else {
    Serial.println("BLE: Was not connected - keeping retry state");
  }
}
