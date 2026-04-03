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
  bool _wifiConnecting = false;
  bool _ntpSynced = false;
  unsigned long _lastWiFiAttempt = 0;
  unsigned long _lastMQTTAttempt = 0;
  unsigned long _wifiConnectStart = 0;

  // Reconnect intervals
  static const unsigned long WIFI_RETRY_INTERVAL = 30000;  // 30s
  static const unsigned long MQTT_RETRY_INTERVAL = 10000;  // 10s
  static const unsigned long WIFI_CONNECT_TIMEOUT = 15000;  // 15s

  void connectWiFi();
  void connectMQTT();
  void subscribeTopics();
  void syncNTP();

  /**
   * MQTT message callback - parses GivTCP3 topic payloads
   */
  static void messageCallback(char *topic, byte *payload, unsigned int length);
  static GivEnergyMQTT *_instance; // For static callback routing
};

#endif // MQTT_CLIENT_H
