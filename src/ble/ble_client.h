/**
 * BLE Client for Fossibot Power Bank
 *
 * Connects to Fossibot power bank via BLE and reads power data.
 * Based on ESP-FBot protocol and working other.yaml configuration.
 */

#ifndef BLE_CLIENT_H
#define BLE_CLIENT_H

#include "fossibot_protocol.h"
#include <Arduino.h>
#include <NimBLEDevice.h>

class FossibotBLE : public NimBLEClientCallbacks {
public:
  FossibotBLE();
  ~FossibotBLE();

  /**
   * Initialize BLE
   */
  void init();

  /**
   * Set target device MAC address
   */
  void setTargetMAC(const String &mac);

  /**
   * Start scanning for device
   */
  void startScan();

  /**
   * Stop scanning
   */
  void stopScan();

  /**
   * Update - call in main loop
   */
  void update();

  /**
   * Check if connected to power bank
   */
  bool isConnected() const { return _connected; }

  /**
   * Check if data changed significantly (for eInk refresh)
   */
  bool hasSignificantChange() const;

  /**
   * Get current power bank data
   */
  const Fossibot::PowerBankData &getData() const { return _data; }

  /**
   * Mark data as refreshed (call after UI update)
   */
  void markRefreshed() { _data.markRefreshed(); }

  /**
   * Toggle USB output
   */
  void toggleUSB();

  /**
   * Toggle DC output
   */
  void toggleDC();

  /**
   * Toggle AC output
   */
  void toggleAC();

  // ============================================================
  // Fossibot Settings Commands
  // ============================================================

  /**
   * Enable/disable button beep sound on Fossibot
   */
  void setBuzzerEnabled(bool enabled);

  /**
   * Enable/disable silent (quiet) charging mode
   */
  void setSilentCharging(bool enabled);

  /**
   * Set LED light mode: 0=off, 1=on, 2=flash, 3=sos
   */
  void setLightMode(int mode);

  /**
   * Set discharge lower limit (0-30%)
   */
  void setDischargeLimit(int percent);

  /**
   * Set charge upper limit / EPS (60-100%)
   */
  void setChargeLimit(int percent);

  /**
   * Set screen timeout in minutes (0=never)
   */
  void setScreenTimeout(int minutes);

  /**
   * Set system idle shutdown timer in minutes (0=never)
   */
  void setSysStandby(int minutes);

  /**
   * Set AC standby timeout in minutes (0=never)
   */
  void setACStandby(int minutes);

  /**
   * Set DC standby timeout in minutes (0=never)
   */
  void setDCStandby(int minutes);

  /**
   * Set USB standby timeout in seconds (0=never)
   */
  void setUSBStandby(int seconds);

  /**
   * Power off the Fossibot device
   */
  void powerOff();

  /**
   * Set schedule charge - sends minutes from now until charge starts
   */
  void setScheduleCharge(int minutes);

  // NimBLE callbacks
  void onConnect(NimBLEClient *client) override;
  void onDisconnect(NimBLEClient *client) override;

private:
  // BLE components
  NimBLEClient *_client;
  NimBLERemoteService *_service;
  NimBLERemoteCharacteristic *_writeChar;
  NimBLERemoteCharacteristic *_notifyChar;

  // State
  bool _initialized;
  bool _connected;
  bool _scanning;
  String _targetMAC;
  NimBLEAddress _targetAddress;

  // Data
  Fossibot::PowerBankData _data;

  // Timing
  unsigned long _lastPoll;
  unsigned long _lastSettingsPoll;
  static const unsigned long POLL_INTERVAL =
      30000; // 30s - matching ESPHome config
  static const unsigned long SETTINGS_POLL_INTERVAL = 60000; // 60s

  // Change detection thresholds
  int _socThreshold;
  int _powerThreshold;

  // Internal methods
  bool connectToDevice();
  void disconnect();
  bool discoverServices();
  void requestStatusData();
  void requestSettingsData();
  void sendCommand(uint8_t reg, uint16_t value);
  void parseStatusData(const uint8_t *data, size_t length);
  void parseSettingsData(const uint8_t *data, size_t length);

  // Notification callback
  static void notifyCallback(NimBLERemoteCharacteristic *characteristic,
                             uint8_t *data, size_t length, bool isNotify);

  // Static instance for callback
  static FossibotBLE *_instance;
};

#endif // BLE_CLIENT_H
