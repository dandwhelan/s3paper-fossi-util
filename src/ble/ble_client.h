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
