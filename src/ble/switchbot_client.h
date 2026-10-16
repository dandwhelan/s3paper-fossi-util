/**
 * BLE Client for a SwitchBot Bot (physical button pusher)
 *
 * A Fossibot that has been powered off also powers down its own BLE radio, so
 * nothing can wake it over the air - the Modbus POWER_OFF register (64) is a
 * one-way trip. A SwitchBot Bot parked over the station's physical power button
 * is the way back in: this client connects to the Bot and asks it to press.
 *
 * Protocol: github.com/OpenWonderLabs/SwitchBotAPI-BLE (devicetypes/bot.md).
 * Every request starts with magic byte 0x57; byte 1 packs protocol version,
 * encryption mode and command. A plain press is 57 01 00. When the Bot has a
 * password set in the SwitchBot app the command instead carries the CRC32 of
 * that password - 57 11 <crc32 big-endian> 00 - which is the variant
 * pySwitchbot and node-switchbot both use.
 *
 * The press is deliberately deferred to update(): a connect can block for
 * several seconds and touch handlers must stay responsive, the same reason
 * FossibotBLE::requestReconnectNow() defers its attempt.
 */

#ifndef SWITCHBOT_CLIENT_H
#define SWITCHBOT_CLIENT_H

#include <Arduino.h>
#include <NimBLEDevice.h>

/**
 * Outcome of the most recent press attempt.
 * Ordering is not significant; these map 1:1 onto the Bot's own result byte
 * where it reported one, and onto local failures where it did not.
 */
enum class SwitchBotResult {
  IDLE,          // nothing attempted yet this boot
  PENDING,       // press queued, waiting for update() to run it
  PRESSING,      // connect/write in flight
  OK,            // Bot acknowledged the press
  NO_MAC,        // no SwitchBot MAC in settings.json
  CONNECT_FAILED,// Bot did not answer (out of range / flat battery)
  NOT_A_BOT,     // connected, but no SwitchBot comms service
  WRITE_FAILED,  // GATT write rejected
  NO_RESPONSE,   // wrote, but the Bot never notified a result
  BOT_ERROR,     // 0x02 - Bot reported an error
  BOT_BUSY,      // 0x03 - Bot busy, retry shortly
  LOW_BATTERY,   // 0x06 - Bot battery too low to actuate
  PASSWORD_SET,  // 0x07 - Bot needs a password we do not have
  WRONG_PASSWORD // 0x09 - stored password rejected
};

class SwitchBotBLE {
public:
  SwitchBotBLE();
  ~SwitchBotBLE();

  /**
   * Set the Bot's BLE MAC address (e.g. "EC:6F:02:46:5F:64").
   * Web Bluetooth cannot address a device by MAC, but NimBLE can, so the
   * firmware connects straight to it with no scan.
   */
  void setTargetMAC(const String &mac);
  String getTargetMAC() const { return _targetMAC; }
  bool hasTarget() const { return _targetMAC.length() > 0; }

  /**
   * Set the password configured for the Bot in the SwitchBot app.
   * Only the CRC32 the protocol needs is retained, never the password itself.
   * Pass an empty string for a Bot with no password (the default).
   */
  void setPassword(const String &password);

  /**
   * Queue a press. Returns false if there is no MAC configured or a press is
   * already in flight. The press itself runs on the next update() so this is
   * safe to call straight from a touch handler.
   */
  bool requestPress();

  /**
   * Run any queued press. Call from the main loop.
   */
  void update();

  bool isBusy() const {
    return _result == SwitchBotResult::PENDING ||
           _result == SwitchBotResult::PRESSING;
  }

  SwitchBotResult getLastResult() const { return _result; }

  /**
   * Short, screen-ready description of the current state. Kept under 12
   * characters so it fits inside a dashboard button.
   */
  const char *getStatusText() const;

  /**
   * millis() when the result last changed, so callers can age out a
   * transient success/failure label. 0 = never.
   */
  unsigned long getResultTime() const { return _resultTime; }

  /**
   * True once (and only once) per result change, so the main loop can force a
   * display refresh without polling for equality itself.
   */
  bool takeStatusChanged();

private:
  static const uint32_t PRESS_DELAY_MS = 300;  // let the UI paint "PRESSING"
  static const uint32_t RESPONSE_TIMEOUT_MS = 3000;
  static const uint32_t CONNECT_TIMEOUT_S = 5;

  void doPress();
  void setResult(SwitchBotResult result);
  size_t buildPressCommand(uint8_t *out) const;
  static SwitchBotResult resultFromStatusByte(uint8_t status);

  NimBLEClient *_client;
  String _targetMAC;
  NimBLEAddress _targetAddress;
  bool _hasPassword;
  uint32_t _passwordCrc;

  SwitchBotResult _result;
  unsigned long _resultTime;
  bool _statusChanged;
  unsigned long _requestedAt;

  // Notification plumbing: the Bot answers on its own characteristic, so the
  // reply lands in a static slot the instance drains.
  static void notifyCallback(NimBLERemoteCharacteristic *characteristic,
                             uint8_t *data, size_t length, bool isNotify);
  static SwitchBotBLE *_instance;
  volatile bool _gotResponse;
  volatile uint8_t _responseStatus;
};

#endif // SWITCHBOT_CLIENT_H
