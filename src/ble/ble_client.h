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
   * Check if actively attempting to connect (for EPD contention avoidance)
   */
  bool isConnecting() const { return _connecting; }

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
   * Pause BLE auto-reconnection attempts (use when entering Reader)
   */
  void pauseRetry() {
    _autoReconnect = false;
    Serial.println("BLE: Auto-retry PAUSED");
  }

  /**
   * Resume BLE auto-reconnection attempts (use when leaving Reader)
   * Resets failure counter and timeout to allow fresh retry attempts
   */
  void resumeRetry() {
    _autoReconnect = true;
    _consecutiveFailures = 0;  // Reset so we get full retry attempts
    _firstAttemptDone = false; // Allow immediate retry attempt
    _retryStartTime = 0;       // Reset 55 min timeout
    _retryInterval = RETRY_INTERVAL_BASE_MS; // Reset backoff
    Serial.println("BLE: Auto-retry RESUMED (counters reset)");
  }

  /**
   * Check if BLE auto-reconnect is enabled
   */
  bool isAutoReconnectEnabled() const { return _autoReconnect; }

  /**
   * Enable/disable the BLE radio entirely (user toggle).
   * Disabling deinitializes the BT controller to actually power it down;
   * pauseRetry() alone leaves the radio initialized and drawing power.
   */
  void setRadioEnabled(bool enabled);
  bool isRadioEnabled() const { return _initialized; }

  /**
   * Disconnect from the device
   */
  void disconnect();

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
   * Set AC charge speed setpoint (1-5, ~220W per step on EU units)
   */
  void setChargeSpeed(int level);

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
  bool _connecting;    // True during active connection attempt (EPD contention
                       // guard)
  bool _autoReconnect; // Flag to enable/disable auto-reconnection logic
  String _targetMAC;
  NimBLEAddress _targetAddress;

  // Data
  Fossibot::PowerBankData _data;

  // Timing
  unsigned long _lastPoll;
  unsigned long _lastSettingsPoll;

  // Retry state (moved from static locals to allow proper reset)
  unsigned long _lastReconnectAttempt;
  unsigned long _retryStartTime; // When retry cycle began (for 55 min timeout)
  unsigned long _retryInterval;  // Current backoff interval
  int _consecutiveFailures;
  bool _firstAttemptDone;

  // Boot auto-enable state: only triggers once within first 5 minutes
  bool _bootAutoEnableTriggered;

  // Auto-enable state machine
  enum class AutoEnableState {
    IDLE,
    WAITING_FOR_DATA,
    CHECK_AC,
    WAIT_AC,
    CHECK_DC,
    WAIT_DC,
    CHECK_USB,
    DONE
  };
  AutoEnableState _autoEnableState;
  unsigned long _autoEnableTimer;

  // Reconnect backoff: 30s base, doubling to a 5 min cap
  static const unsigned long RETRY_INTERVAL_BASE_MS = 30000;
  static const unsigned long RETRY_INTERVAL_MAX_MS = 300000;

  // Power optimized polling intervals (middle ground)
  static const unsigned long POLL_INTERVAL = 45000; // 45s (middle ground)
  static const unsigned long SETTINGS_POLL_INTERVAL =
      180000; // 3 min (middle ground)

  // Boot auto-enable window: auto-turn on AC/DC/USB on first connection within
  // this time
  static const unsigned long BOOT_AUTO_ENABLE_WINDOW_MS =
      5 * 60 * 1000; // 5 minutes

  // Change detection thresholds
  int _socThreshold;
  int _powerThreshold;

  // Internal methods
  bool connectToDevice();
  void cleanupClient();
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
