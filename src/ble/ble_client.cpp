/**
 * BLE Client Implementation
 */

#include "ble_client.h"
#include <cstring>

// Static instance for callbacks
FossibotBLE *FossibotBLE::_instance = nullptr;

FossibotBLE::FossibotBLE()
    : _client(nullptr), _service(nullptr), _writeChar(nullptr),
      _notifyChar(nullptr), _initialized(false), _connected(false),
      _scanning(false), _lastPoll(0), _lastSettingsPoll(0), _socThreshold(1),
      _powerThreshold(5) {
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

  Serial.println("BLE: Initializing NimBLE...");

  NimBLEDevice::init("M5PaperS3");
  NimBLEDevice::setPower(ESP_PWR_LVL_P9); // Max power for range
  NimBLEDevice::setSecurityAuth(false, false, false);
  // NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT); // Reverted

  _initialized = true;
  Serial.println("BLE: Initialized");
}

void FossibotBLE::setTargetMAC(const String &mac) {
  _targetMAC = mac;
  _targetAddress = NimBLEAddress(_targetMAC.c_str());
  Serial.printf("BLE: Target MAC set to %s\n", _targetMAC.c_str());
}

void FossibotBLE::startScan() {
  if (!_initialized || _targetMAC.length() == 0) {
    Serial.println("BLE: Cannot scan - not initialized or no target MAC");
    return;
  }

  Serial.println("BLE: Starting connection attempt...");

  // Connect directly to the known address (no scanning needed)
  if (connectToDevice()) {
    Serial.println("BLE: Connected successfully!");
  } else {
    Serial.println("BLE: Connection failed, will retry...");
  }
}

void FossibotBLE::stopScan() {
  NimBLEDevice::getScan()->stop();
  _scanning = false;
}

bool FossibotBLE::connectToDevice() {
  if (_connected)
    return true;

  Serial.printf("BLE: Connecting to %s...\n", _targetMAC.c_str());

  // Create client if needed
  if (!_client) {
    _client = NimBLEDevice::createClient();
    _client->setClientCallbacks(this);

    // Connection parameters
    // Connection parameters (Strict for performance/compat)
    _client->setConnectionParams(12, 12, 0,
                                 150); // min, max interval, latency, timeout
    _client->setConnectTimeout(
        5); // 5 seconds (reduced from 10 to minimize UI freeze)
  }

  // Connect
  // User requested removal of Random logic. Using default Public Address.
  Serial.println("BLE: Connecting with AddrType: PUBLIC (Default)");

  if (!_client->connect(_targetAddress)) {
    Serial.println("BLE: Failed to connect");
    return false;
  }

  // Discover services
  if (!discoverServices()) {
    disconnect();
    return false;
  }

  _connected = true;
  _data.connected = true;

  // Request initial data
  requestStatusData();

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

void FossibotBLE::update() {
  if (!_initialized)
    return;

  // Try to reconnect if disconnected
  if (!_connected) {
    static unsigned long lastReconnectAttempt = 0;
    static int consecutiveFailures = 0;
    const int MAX_FAILURES = 5; // Stop trying after 5 failures

    // Exponential backoff: 60s, 120s, 240s, then stop
    unsigned long retryInterval = 60000 * (1 << min(consecutiveFailures, 2));

    if (consecutiveFailures >= MAX_FAILURES) {
      // Give up after max failures - user can restart device to retry
      return;
    }

    if (millis() - lastReconnectAttempt > retryInterval) {
      lastReconnectAttempt = millis();
      Serial.printf("BLE: Retry attempt %d/%d\n", consecutiveFailures + 1,
                    MAX_FAILURES);

      if (!connectToDevice()) {
        consecutiveFailures++;
        Serial.printf("BLE: Failed. Next retry in %lu seconds\n",
                      retryInterval / 1000);
      } else {
        consecutiveFailures = 0; // Reset on success
      }
    }
    return;
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

  _writeChar->writeValue(command, sizeof(command), false);
  Serial.printf("BLE: Sent command reg=%d value=%d (CRC=0x%04X)\n", reg, value,
                crc);
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
}

void FossibotBLE::parseSettingsData(const uint8_t *data, size_t length) {
  // Settings data parsing - similar structure to status
  // Currently just log it
  Serial.printf("BLE: Settings data received (%d bytes)\n", length);
}

// NimBLE callbacks
void FossibotBLE::onConnect(NimBLEClient *client) {
  Serial.println("BLE: Connected callback");
  _connected = true;
  _data.connected = true;
}

void FossibotBLE::onDisconnect(NimBLEClient *client) {
  Serial.println("BLE: Disconnected callback");
  _connected = false;
  _data.connected = false;
  _service = nullptr;
  _writeChar = nullptr;
  _notifyChar = nullptr;
}
