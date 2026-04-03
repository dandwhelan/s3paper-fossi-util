/**
 * MQTT Client for GivTCP3 / GivEnergy Integration
 */

#include "mqtt_client.h"
#include "../hardware/rtc.h"
#include <sys/time.h>
#include <time.h>

// Static instance pointer for callback routing
GivEnergyMQTT *GivEnergyMQTT::_instance = nullptr;

GivEnergyMQTT::GivEnergyMQTT() : _mqttClient(_wifiClient) {
  _instance = this;
}

GivEnergyMQTT::~GivEnergyMQTT() {
  if (_instance == this)
    _instance = nullptr;
  _mqttClient.disconnect();
  WiFi.disconnect(true);
}

void GivEnergyMQTT::init(const String &ssid, const String &password,
                          const String &broker, int port,
                          const String &username, const String &mqttPassword,
                          const String &inverterSN) {
  _ssid = ssid;
  _wifiPassword = password;
  _broker = broker;
  _port = port;
  _mqttUsername = username;
  _mqttPassword = mqttPassword;
  _inverterSN = inverterSN;

  _mqttClient.setServer(_broker.c_str(), _port);
  _mqttClient.setCallback(messageCallback);
  _mqttClient.setBufferSize(256); // GivTCP payloads are small

  _initialized = true;

#ifdef SERIAL_DEBUG
  Serial.printf("MQTT: Initialized - broker=%s:%d, inverter=%s\n",
                _broker.c_str(), _port, _inverterSN.c_str());
#endif

  // Start WiFi connection
  connectWiFi();
}

void GivEnergyMQTT::update() {
  if (!_initialized)
    return;

  // Handle WiFi connection
  if (!isWiFiConnected()) {
    if (_wifiConnecting) {
      // Check if connection attempt timed out
      if (millis() - _wifiConnectStart > WIFI_CONNECT_TIMEOUT) {
        _wifiConnecting = false;
        WiFi.disconnect(true);
#ifdef SERIAL_DEBUG
        Serial.println("MQTT: WiFi connection timed out");
#endif
      }
      return;
    }

    // Retry WiFi connection
    if (millis() - _lastWiFiAttempt > WIFI_RETRY_INTERVAL) {
      connectWiFi();
    }
    return;
  }

  // WiFi is connected - handle MQTT
  if (_wifiConnecting) {
    _wifiConnecting = false;
    _data.connected = true;
#ifdef SERIAL_DEBUG
    Serial.printf("MQTT: WiFi connected! IP=%s, RSSI=%d\n",
                  WiFi.localIP().toString().c_str(), WiFi.RSSI());
#endif

    // Sync time from NTP on first WiFi connection
    if (!_ntpSynced) {
      syncNTP();
    }
  }

  if (!_mqttClient.connected()) {
    if (millis() - _lastMQTTAttempt > MQTT_RETRY_INTERVAL) {
      connectMQTT();
    }
    return;
  }

  // Process incoming MQTT messages
  _mqttClient.loop();
}

bool GivEnergyMQTT::isWiFiConnected() const {
  return WiFi.status() == WL_CONNECTED;
}

bool GivEnergyMQTT::isMQTTConnected() {
  return _mqttClient.connected();
}

int GivEnergyMQTT::getRSSI() const {
  if (isWiFiConnected())
    return WiFi.RSSI();
  return 0;
}

void GivEnergyMQTT::connectWiFi() {
  _lastWiFiAttempt = millis();
  _wifiConnecting = true;
  _wifiConnectStart = millis();

#ifdef SERIAL_DEBUG
  Serial.printf("MQTT: Connecting to WiFi '%s'...\n", _ssid.c_str());
#endif

  WiFi.mode(WIFI_STA);
  WiFi.begin(_ssid.c_str(), _wifiPassword.c_str());
}

void GivEnergyMQTT::connectMQTT() {
  _lastMQTTAttempt = millis();

#ifdef SERIAL_DEBUG
  Serial.printf("MQTT: Connecting to broker %s:%d...\n", _broker.c_str(),
                _port);
#endif

  String clientId = "erex-" + String(random(0xFFFF), HEX);
  bool connected;

  if (_mqttUsername.length() > 0) {
    connected = _mqttClient.connect(clientId.c_str(), _mqttUsername.c_str(),
                                    _mqttPassword.c_str());
  } else {
    connected = _mqttClient.connect(clientId.c_str());
  }

  if (connected) {
#ifdef SERIAL_DEBUG
    Serial.println("MQTT: Connected to broker!");
#endif
    subscribeTopics();
  } else {
#ifdef SERIAL_DEBUG
    Serial.printf("MQTT: Connection failed, rc=%d\n", _mqttClient.state());
#endif
  }
}

void GivEnergyMQTT::subscribeTopics() {
  // Build topic prefix: GivEnergy/<SN>/
  String prefix = "GivEnergy/" + _inverterSN + "/";

  // Subscribe to power data with wildcard
  String powerTopic = prefix + "Power/Power/#";
  String flowsTopic = prefix + "Power/Flows/#";
  String energyTodayTopic = prefix + "Energy/Today/#";

  _mqttClient.subscribe(powerTopic.c_str());
  _mqttClient.subscribe(flowsTopic.c_str());
  _mqttClient.subscribe(energyTodayTopic.c_str());

#ifdef SERIAL_DEBUG
  Serial.printf("MQTT: Subscribed to:\n  %s\n  %s\n  %s\n",
                powerTopic.c_str(), flowsTopic.c_str(),
                energyTodayTopic.c_str());
#endif
}

void GivEnergyMQTT::messageCallback(char *topic, byte *payload,
                                     unsigned int length) {
  if (!_instance)
    return;

  // Parse payload as float value
  char buf[32];
  unsigned int copyLen = (length < sizeof(buf) - 1) ? length : sizeof(buf) - 1;
  memcpy(buf, payload, copyLen);
  buf[copyLen] = '\0';
  float value = atof(buf);

  // Extract the entity name from the topic
  // Topics: GivEnergy/<SN>/Power/Power/<entity>
  //         GivEnergy/<SN>/Power/Flows/<entity>
  //         GivEnergy/<SN>/Energy/Today/<entity>
  String topicStr(topic);
  GivEnergy::SolarData &data = _instance->_data;
  data.lastUpdateTime = millis();

  // Power/Power entities
  if (topicStr.endsWith("/PV_Power_String_1")) {
    data.pvPowerString1 = value;
  } else if (topicStr.endsWith("/PV_Power_String_2")) {
    data.pvPowerString2 = value;
  } else if (topicStr.endsWith("/PV_Power")) {
    data.pvPowerTotal = value;
  } else if (topicStr.endsWith("/SOC")) {
    data.batteryPercent = value;
  } else if (topicStr.endsWith("/Battery_Power")) {
    data.batteryPower = value;
  } else if (topicStr.endsWith("/Charge_Power")) {
    data.chargePower = value;
  } else if (topicStr.endsWith("/Discharge_Power")) {
    data.dischargePower = value;
  } else if (topicStr.endsWith("/Grid_Power")) {
    data.gridPower = value;
  } else if (topicStr.endsWith("/Import_Power")) {
    data.importPower = value;
  } else if (topicStr.endsWith("/Export_Power")) {
    data.exportPower = value;
  } else if (topicStr.endsWith("/Load_Power")) {
    data.loadPower = value;
  }
  // Power/Flows entities
  else if (topicStr.endsWith("/Solar_to_House")) {
    data.solarToHouse = value;
  } else if (topicStr.endsWith("/Solar_to_Battery")) {
    data.solarToBattery = value;
  } else if (topicStr.endsWith("/Solar_to_Grid")) {
    data.solarToGrid = value;
  } else if (topicStr.endsWith("/Battery_to_House")) {
    data.batteryToHouse = value;
  } else if (topicStr.endsWith("/Battery_to_Grid")) {
    data.batteryToGrid = value;
  } else if (topicStr.endsWith("/Grid_to_House")) {
    data.gridToHouse = value;
  } else if (topicStr.endsWith("/Grid_to_Battery")) {
    data.gridToBattery = value;
  }
  // Energy/Today entities
  else if (topicStr.endsWith("/PV_Energy_Today_kWh")) {
    data.pvEnergyToday = value;
  } else if (topicStr.endsWith("/Import_Energy_Today_kWh")) {
    data.importEnergyToday = value;
  } else if (topicStr.endsWith("/Export_Energy_Today_kWh")) {
    data.exportEnergyToday = value;
  } else if (topicStr.endsWith("/Load_Energy_Today_kWh")) {
    data.loadEnergyToday = value;
  } else if (topicStr.endsWith("/Battery_Charge_Energy_Today_kWh")) {
    data.batteryChargeToday = value;
  } else if (topicStr.endsWith("/Battery_Discharge_Energy_Today_kWh")) {
    data.batteryDischargeToday = value;
  }
}

void GivEnergyMQTT::syncNTP() {
#ifdef SERIAL_DEBUG
  Serial.println("NTP: Syncing time...");
#endif

  // Configure NTP (pool.ntp.org, no TZ offset - RTC stores UTC)
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");

  // Wait up to 5 seconds for NTP response
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 5000)) {
#ifdef SERIAL_DEBUG
    Serial.println("NTP: Failed to get time");
#endif
    return;
  }

  _ntpSynced = true;

  // Write back to RTC for persistence across reboots
  int year = timeinfo.tm_year + 1900;
  int month = timeinfo.tm_mon + 1;
  int day = timeinfo.tm_mday;
  int hour = timeinfo.tm_hour;
  int minute = timeinfo.tm_min;
  int sec = timeinfo.tm_sec;
  int dow = timeinfo.tm_wday;

  RTC::setDate(year, month, day);
  RTC::setTime(hour, minute, sec);

#ifdef SERIAL_DEBUG
  Serial.printf("NTP: Time synced: %04d-%02d-%02d %02d:%02d:%02d\n", year,
                month, day, hour, minute, sec);
#endif
}
