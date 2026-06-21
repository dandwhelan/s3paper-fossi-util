/**
 * MQTT Client for GivTCP3 / GivEnergy Integration
 *
 * Connects to WiFi and subscribes to GivTCP3 MQTT topics to receive
 * real-time solar, battery, grid, and house load data.
 */

#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H

#include "givenergy_data.h"
#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>

class GivEnergyMQTT {
public:
  GivEnergyMQTT();
  ~GivEnergyMQTT();

  /**
   * Initialize WiFi and MQTT connection
   * @param ssid WiFi network name
   * @param password WiFi password
   * @param broker MQTT broker IP/hostname
   * @param port MQTT broker port
   * @param username MQTT username (empty = no auth)
   * @param mqttPassword MQTT password
   * @param inverterSN GivEnergy inverter serial number
   */
  void init(const String &ssid, const String &password, const String &broker,
            int port, const String &username, const String &mqttPassword,
            const String &inverterSN);

  /**
   * Call in main loop - handles WiFi/MQTT reconnection and message processing
   */
  void update();

  /**
   * Get current solar data (read-only reference)
   */
  const GivEnergy::SolarData &getData() const { return _data; }

  /**
   * Connection state
   */
  bool isWiFiConnected() const;
  bool isMQTTConnected();
  bool isConnecting() const { return _wifiConnecting; }

  /**
   * Get WiFi signal strength
   */
  int getRSSI() const;

  /**
   * Enable/disable the WiFi radio entirely (user toggle).
   * Disabling drops MQTT and powers the radio off; enabling reconnects.
   */
  void setRadioEnabled(bool enabled);
  bool isRadioEnabled() const { return _radioEnabled; }

private:
  WiFiClient _wifiClient;
  PubSubClient _mqttClient;
  GivEnergy::SolarData _data;

  // Config
  String _ssid;
  String _wifiPassword;
  String _broker;
  int _port;
  String _mqttUsername;
  String _mqttPassword;
  String _inverterSN;

  // State
  bool _initialized = false;
  bool _radioEnabled = true;
  bool _wifiConnecting = false;
  bool _wasWiFiConnected = false;
  bool _ntpSynced = false;
  bool _ntpPending = false;
  unsigned long _lastWiFiAttempt = 0;
  unsigned long _lastMQTTAttempt = 0;
  unsigned long _wifiConnectStart = 0;

  // Reconnect intervals (exponential backoff between base and max)
  static const unsigned long WIFI_RETRY_INTERVAL = 30000;  // 30s base
  static const unsigned long MQTT_RETRY_INTERVAL = 10000;  // 10s base
  static const unsigned long RETRY_INTERVAL_MAX = 300000;  // 5 min cap
  static const unsigned long WIFI_CONNECT_TIMEOUT = 15000;  // 15s
  unsigned long _wifiRetryInterval = WIFI_RETRY_INTERVAL;
  unsigned long _mqttRetryInterval = MQTT_RETRY_INTERVAL;

  void connectWiFi();
  void connectMQTT();
  void subscribeTopics();
  void startNTP();
  void checkNTP();

  /**
   * MQTT message callback - parses GivTCP3 topic payloads
   */
  static void messageCallback(char *topic, byte *payload, unsigned int length);
  static GivEnergyMQTT *_instance; // For static callback routing
};

#endif // MQTT_CLIENT_H
