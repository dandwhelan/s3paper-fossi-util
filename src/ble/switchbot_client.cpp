/**
 * SwitchBot Bot BLE Client Implementation
 */

#include "switchbot_client.h"
#include <M5Unified.h> // waitDisplay() guard, same as the Fossibot client

namespace {

// Comms service and characteristics, identical across every SwitchBot device.
const char *SWITCHBOT_SERVICE_UUID = "cba20d00-224d-11e6-9fb8-0002a5d5c51b";
const char *SWITCHBOT_WRITE_UUID = "cba20002-224d-11e6-9fb8-0002a5d5c51b";
const char *SWITCHBOT_NOTIFY_UUID = "cba20003-224d-11e6-9fb8-0002a5d5c51b";

// Byte 1 of a request: version 0, encryption mode 0, command 0x01 (execute
// action). 0x11 is the same command with the password-bearing encryption mode.
const uint8_t CMD_ACTION = 0x01;
const uint8_t CMD_ACTION_WITH_PASSWORD = 0x11;
const uint8_t MAGIC = 0x57;
const uint8_t ACTION_PRESS = 0x00; // push and retract

/**
 * CRC32 (IEEE 802.3, reflected, polynomial 0xEDB88320) over the password
 * bytes - the digest the Bot expects in a password-bearing command.
 */
uint32_t crc32Bytes(const String &s) {
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < s.length(); i++) {
    crc ^= (uint8_t)s[i];
    for (int bit = 0; bit < 8; bit++) {
      if (crc & 1u) {
        crc = (crc >> 1) ^ 0xEDB88320u;
      } else {
        crc >>= 1;
      }
    }
  }
  return crc ^ 0xFFFFFFFFu;
}

} // namespace

SwitchBotBLE *SwitchBotBLE::_instance = nullptr;

SwitchBotBLE::SwitchBotBLE()
    : _client(nullptr), _addressResolved(false), _hasPassword(false),
      _passwordCrc(0), _result(SwitchBotResult::IDLE), _resultTime(0),
      _statusChanged(false), _requestedAt(0), _gotResponse(false),
      _responseStatus(0) {
  _instance = this;
}

SwitchBotBLE::~SwitchBotBLE() {
  if (_client && _client->isConnected()) {
    _client->disconnect();
  }
  if (_instance == this) {
    _instance = nullptr;
  }
}

void SwitchBotBLE::setTargetMAC(const String &mac) {
  _targetMAC = mac;
  _targetMAC.trim();
  _addressResolved = false;
  if (_targetMAC.length() > 0) {
    // Provisional only - the type is confirmed by resolveAddressByScan().
    _targetAddress = NimBLEAddress(_targetMAC.c_str());
    Serial.printf("SwitchBot: Target MAC set to %s\n", _targetMAC.c_str());
  }
}

void SwitchBotBLE::setPassword(const String &password) {
  if (password.length() == 0) {
    _hasPassword = false;
    _passwordCrc = 0;
    return;
  }
  _hasPassword = true;
  _passwordCrc = crc32Bytes(password);
  Serial.println("SwitchBot: Password digest stored");
}

void SwitchBotBLE::setResult(SwitchBotResult result) {
  if (_result == result) {
    return;
  }
  _result = result;
  _resultTime = millis();
  _statusChanged = true;
}

bool SwitchBotBLE::takeStatusChanged() {
  bool changed = _statusChanged;
  _statusChanged = false;
  return changed;
}

const char *SwitchBotBLE::getStatusText() const {
  switch (_result) {
  case SwitchBotResult::IDLE:
    return "POWER ON";
  case SwitchBotResult::PENDING:
  case SwitchBotResult::PRESSING:
    return "PRESSING";
  case SwitchBotResult::OK:
    return "PRESSED";
  case SwitchBotResult::NO_MAC:
    return "NO BOT SET";
  case SwitchBotResult::CONNECT_FAILED:
    return "NO BOT";
  case SwitchBotResult::NOT_A_BOT:
    return "NOT A BOT";
  case SwitchBotResult::WRITE_FAILED:
    return "SEND FAIL";
  case SwitchBotResult::NO_RESPONSE:
    return "NO REPLY";
  case SwitchBotResult::BOT_ERROR:
    return "BOT ERROR";
  case SwitchBotResult::BOT_BUSY:
    return "BOT BUSY";
  case SwitchBotResult::LOW_BATTERY:
    return "BOT BATT";
  case SwitchBotResult::PASSWORD_SET:
    return "NEEDS PIN";
  case SwitchBotResult::WRONG_PASSWORD:
    return "BAD PIN";
  }
  return "POWER ON";
}

bool SwitchBotBLE::requestPress() {
  if (isBusy()) {
    return false;
  }
  if (!hasTarget()) {
    Serial.println("SwitchBot: No MAC configured - cannot press");
    setResult(SwitchBotResult::NO_MAC);
    return false;
  }
  Serial.println("SwitchBot: Press queued");
  setResult(SwitchBotResult::PENDING);
  _requestedAt = millis();
  return true;
}

size_t SwitchBotBLE::buildPressCommand(uint8_t *out) const {
  out[0] = MAGIC;
  if (!_hasPassword) {
    out[1] = CMD_ACTION;
    out[2] = ACTION_PRESS;
    return 3;
  }
  out[1] = CMD_ACTION_WITH_PASSWORD;
  out[2] = (uint8_t)(_passwordCrc >> 24);
  out[3] = (uint8_t)(_passwordCrc >> 16);
  out[4] = (uint8_t)(_passwordCrc >> 8);
  out[5] = (uint8_t)(_passwordCrc);
  out[6] = ACTION_PRESS;
  return 7;
}

SwitchBotResult SwitchBotBLE::resultFromStatusByte(uint8_t status) {
  // 0x01 is the documented success code; the Bot answers 01 FF 00 on a press.
  switch (status) {
  case 0x01:
    return SwitchBotResult::OK;
  case 0x02:
    return SwitchBotResult::BOT_ERROR;
  case 0x03:
    return SwitchBotResult::BOT_BUSY;
  case 0x06:
    return SwitchBotResult::LOW_BATTERY;
  case 0x07:
    return SwitchBotResult::PASSWORD_SET;
  case 0x09:
    return SwitchBotResult::WRONG_PASSWORD;
  default:
    // Some firmware revisions answer 0x00 for "done". Treat anything else as
    // success rather than alarming the user about a press that did happen.
    return SwitchBotResult::OK;
  }
}

void SwitchBotBLE::notifyCallback(NimBLERemoteCharacteristic *characteristic,
                                  uint8_t *data, size_t length, bool isNotify) {
  (void)characteristic;
  (void)isNotify;
  if (!_instance || length == 0) {
    return;
  }
  _instance->_responseStatus = data[0];
  _instance->_gotResponse = true;
  Serial.printf("SwitchBot: Response status 0x%02X\n", data[0]);
}

bool SwitchBotBLE::resolveAddressByScan() {
  NimBLEScan *scan = NimBLEDevice::getScan();
  if (!scan) {
    return false;
  }
  scan->stop(); // the Fossibot client shares this singleton
  scan->setActiveScan(true); // ask for the scan response, which carries the name
  scan->setInterval(100);
  scan->setWindow(99);

  Serial.printf("SwitchBot: Scanning %us for %s...\n", (unsigned)SCAN_SECONDS,
                _targetMAC.c_str());
  NimBLEScanResults results = scan->start(SCAN_SECONDS, false);

  String wanted = _targetMAC;
  wanted.toLowerCase();

  bool found = false;
  int byName = -1;

  for (int i = 0; i < results.getCount(); i++) {
    NimBLEAdvertisedDevice device = results.getDevice(i);
    String address = String(device.getAddress().toString().c_str());
    address.toLowerCase();

    if (address == wanted) {
      _targetAddress = device.getAddress();
      _addressResolved = true;
      found = true;
      Serial.printf("SwitchBot: Found %s - address type %u, RSSI %d\n",
                    _targetMAC.c_str(), (unsigned)_targetAddress.getType(),
                    device.getRSSI());
      break;
    }

    // Bots advertise as "WoHand". Remember one in case the configured MAC
    // never shows up - a stale MAC is far more likely than a stray Bot.
    if (byName < 0 && device.haveName() && device.getName() == "WoHand") {
      byName = i;
    }
  }

  if (!found && byName >= 0) {
    NimBLEAdvertisedDevice device = results.getDevice(byName);
    _targetAddress = device.getAddress();
    _addressResolved = true;
    found = true;
    Serial.printf("SwitchBot: %s not seen, but a WoHand advertised at %s "
                  "(type %u, RSSI %d) - using it. Update switchbot_mac.\n",
                  _targetMAC.c_str(), device.getAddress().toString().c_str(),
                  (unsigned)_targetAddress.getType(), device.getRSSI());
  }

  if (!found) {
    Serial.printf("SwitchBot: Bot not among %d scan results - out of range, "
                  "or already connected to a hub/phone\n",
                  results.getCount());
  }

  scan->clearResults();
  return found;
}

bool SwitchBotBLE::tryConnect(const NimBLEAddress &address, const char *label) {
  Serial.printf("SwitchBot: Connecting to %s (%s address type %u)...\n",
                address.toString().c_str(), label,
                (unsigned)address.getType());
  if (_client->connect(address, false)) {
    return true;
  }
  Serial.printf("SwitchBot: Connect failed (%s)\n", label);
  return false;
}

void SwitchBotBLE::update() {
  if (_result != SwitchBotResult::PENDING) {
    return;
  }
  // Hold off briefly so the dashboard can repaint with "PRESSING" before the
  // radio work blocks the loop.
  if (millis() - _requestedAt < PRESS_DELAY_MS) {
    return;
  }
  doPress();
}

void SwitchBotBLE::doPress() {
  setResult(SwitchBotResult::PRESSING);

  // NimBLE is a singleton shared with the Fossibot client. It is normally
  // already up, but the user may have powered the radio down in Settings.
  if (!NimBLEDevice::getInitialized()) {
    Serial.println("SwitchBot: NimBLE not initialized - bringing it up");
    NimBLEDevice::init("M5PaperS3");
    NimBLEDevice::setPower(ESP_PWR_LVL_P3);
    NimBLEDevice::setSecurityAuth(false, false, false);
    delay(100);
  }

  // Same two guards the Fossibot connect path needs: 80MHz eco mode makes
  // connections fail, and the EPD must be idle before the radio starts.
  uint32_t previousCpuMhz = getCpuFrequencyMhz();
  if (previousCpuMhz < 240) {
    setCpuFrequencyMhz(240);
    Serial.println("SwitchBot: Boosted CPU to 240MHz");
  }
  M5.Display.waitDisplay();

  if (!_client) {
    _client = NimBLEDevice::createClient();
    _client->setConnectTimeout(CONNECT_TIMEOUT_S);
  }

  SwitchBotResult outcome = SwitchBotResult::CONNECT_FAILED;

  // A MAC alone does not name a peer: the same six bytes address a different
  // device depending on whether they are a public or a random static address,
  // and NimBLEAddress defaults to public while SwitchBot Bots use random
  // static. (Web Bluetooth never hits this - the chooser hands the browser an
  // already-discovered device, address type included.) So scan once to learn
  // the real type, and if the Bot did not advertise in that window, still try
  // both types rather than giving up on the guess we started with.
  if (!_addressResolved) {
    resolveAddressByScan();
  }

  bool connected =
      tryConnect(_targetAddress, _addressResolved ? "scanned" : "assumed");

  if (!connected && !_addressResolved) {
    uint8_t otherType = _targetAddress.getType() == BLE_ADDR_PUBLIC
                            ? BLE_ADDR_RANDOM
                            : BLE_ADDR_PUBLIC;
    NimBLEAddress alternate(_targetMAC.c_str(), otherType);
    connected = tryConnect(alternate, "fallback");
    if (connected) {
      _targetAddress = alternate;
      _addressResolved = true;
    }
  }

  if (connected) {
    NimBLERemoteService *service =
        _client->getService(NimBLEUUID(SWITCHBOT_SERVICE_UUID));
    if (!service) {
      Serial.println("SwitchBot: Comms service not found");
      outcome = SwitchBotResult::NOT_A_BOT;
    } else {
      NimBLERemoteCharacteristic *writeChar =
          service->getCharacteristic(NimBLEUUID(SWITCHBOT_WRITE_UUID));
      NimBLERemoteCharacteristic *notifyChar =
          service->getCharacteristic(NimBLEUUID(SWITCHBOT_NOTIFY_UUID));

      if (!writeChar) {
        Serial.println("SwitchBot: Write characteristic not found");
        outcome = SwitchBotResult::NOT_A_BOT;
      } else {
        _gotResponse = false;
        _responseStatus = 0;
        if (notifyChar && notifyChar->canNotify()) {
          notifyChar->subscribe(true, notifyCallback);
        }

        uint8_t command[7];
        size_t length = buildPressCommand(command);
        Serial.printf("SwitchBot: Writing press command (%u bytes)\n",
                      (unsigned)length);

        if (!writeChar->writeValue(command, length, true)) {
          Serial.println("SwitchBot: Write failed");
          outcome = SwitchBotResult::WRITE_FAILED;
        } else {
          unsigned long waitStart = millis();
          while (!_gotResponse && millis() - waitStart < RESPONSE_TIMEOUT_MS) {
            delay(20);
          }
          if (_gotResponse) {
            outcome = resultFromStatusByte(_responseStatus);
          } else {
            // The write was accepted, so the Bot almost certainly actuated -
            // some units simply never notify.
            Serial.println("SwitchBot: No response within timeout");
            outcome = SwitchBotResult::NO_RESPONSE;
          }
        }
      }
    }
    _client->disconnect();
  } else {
    // tryConnect() has already logged each attempt. Drop the cached address so
    // the next press rescans rather than retrying a type that just failed.
    _addressResolved = false;
  }

  // Leave the client allocated: NimBLE's deleteClient() can hang on the
  // ESP32-S3 after a failed connection (see FossibotBLE::connectToDevice).

  if (getCpuFrequencyMhz() != previousCpuMhz) {
    setCpuFrequencyMhz(previousCpuMhz);
  }

  setResult(outcome);
  Serial.printf("SwitchBot: Press finished - %s\n", getStatusText());
}
