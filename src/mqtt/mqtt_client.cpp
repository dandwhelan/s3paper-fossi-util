/**
 * MQTT Client for GivTCP3 / GivEnergy Integration
 */

#include "mqtt_client.h"
#include "../hardware/rtc.h"
#include <esp_sntp.h>
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
  // GivTCP payloads are small, but topic strings alone approach 80 bytes;
  // 512 gives safe headroom (PubSubClient drops oversized messages silently)
  _mqttClient.setBufferSize(512);

  _initialized = true;

#ifdef SERIAL_DEBUG
  Serial.printf("MQTT: Initialized - broker=%s:%d, inverter=%s\n",
                _broker.c_str(), _port, _inverterSN.c_str());
#endif

  // Start WiFi connection (unless the radio is user-disabled)
  if (_radioEnabled) {
    connectWiFi();
  }
}

void GivEnergyMQTT::update() {
  if (!_initialized || !_radioEnabled)
    return;

  // Handle WiFi connection
  if (!isWiFiConnected()) {
    // Clean up dead MQTT socket when WiFi drops
    if (_wasWiFiConnected) {
      _wasWiFiConnected = false;
      _data.connected = false;
      _mqttClient.disconnect();
#ifdef SERIAL_DEBUG
      Serial.println("MQTT: WiFi dropped - MQTT socket closed");
#endif
    }

    if (_wifiConnecting) {
      // Check if connection attempt timed out
      if (millis() - _wifiConnectStart > WIFI_CONNECT_TIMEOUT) {
        _wifiConnecting = false;
        WiFi.disconnect(true);
        // Exponential backoff: double interval up to the cap
        _wifiRetryInterval =
            min(_wifiRetryInterval * 2, (unsigned long)RETRY_INTERVAL_MAX);
#ifdef SERIAL_DEBUG
        Serial.printf("MQTT: WiFi connection timed out (next retry in %lus)\n",
                      _wifiRetryInterval / 1000);
#endif
      }
      return;
    }

    // Retry WiFi connection
    if (millis() - _lastWiFiAttempt > _wifiRetryInterval) {
      connectWiFi();
    }
    return;
  }

  // WiFi is connected - handle MQTT
  if (_wifiConnecting) {
    _wifiConnecting = false;
    _wasWiFiConnected = true;
    _data.connected = true;
    _wifiRetryInterval = WIFI_RETRY_INTERVAL; // Reset backoff on success

    // Modem power save: radio sleeps between DTIM beacons. Saves ~60-80mA;
    // MQTT subscribe latency impact is negligible for a 30s-throttled e-ink UI.
    WiFi.setSleep(true);

#ifdef SERIAL_DEBUG
    Serial.printf("MQTT: WiFi connected! IP=%s, RSSI=%d\n",
                  WiFi.localIP().toString().c_str(), WiFi.RSSI());
#endif

    // Kick off NTP sync on first WiFi connection (non-blocking; completion
    // is detected in checkNTP() below)
    if (!_ntpSynced) {
      startNTP();
    }
  }

  // Check for async NTP completion
  if (_ntpPending) {
    checkNTP();
  }

  if (!_mqttClient.connected()) {
    if (millis() - _lastMQTTAttempt > _mqttRetryInterval) {
      connectMQTT();
    }
    return;
  }

  // Process incoming MQTT messages
  _mqttClient.loop();
}

void GivEnergyMQTT::setRadioEnabled(bool enabled) {
  if (_radioEnabled == enabled)
    return;

  _radioEnabled = enabled;

  if (enabled) {
#ifdef SERIAL_DEBUG
    Serial.println("MQTT: WiFi radio enabled by user");
#endif
    _wifiRetryInterval = WIFI_RETRY_INTERVAL;
    _mqttRetryInterval = MQTT_RETRY_INTERVAL;
    if (_initialized)
      connectWiFi();
  } else {
#ifdef SERIAL_DEBUG
    Serial.println("MQTT: WiFi radio disabled by user");
#endif
    _mqttClient.disconnect();
    _wifiConnecting = false;
    _wasWiFiConnected = false;
    _data.connected = false;
    WiFi.disconnect(true); // Also erases in-RAM session state
    WiFi.mode(WIFI_OFF);   // Power the radio down completely
  }
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
    _mqttRetryInterval = MQTT_RETRY_INTERVAL; // Reset backoff on success
    subscribeTopics();
  } else {
    // Exponential backoff: double interval up to the cap
    _mqttRetryInterval = min(_mqttRetryInterval * 2, (unsigned long)RETRY_INTERVAL_MAX);
#ifdef SERIAL_DEBUG
    Serial.printf("MQTT: Connection failed, rc=%d (next retry in %lus)\n",
                  _mqttClient.state(), _mqttRetryInterval / 1000);
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

void GivEnergyMQTT::startNTP() {
#ifdef SERIAL_DEBUG
  Serial.println("NTP: Sync started (async)...");
#endif

  // Configure NTP with POSIX timezone string for UK (GMT/BST auto-switching).
  // BST runs from last Sunday of March @ 01:00 UTC to last Sunday of October @ 02:00.
  // configTzTime is asynchronous - SNTP fetches in the background and sets the
  // system clock when a response arrives. checkNTP() detects completion, so the
  // main loop is never blocked waiting for a server.
  configTzTime("GMT0BST,M3.5.0/1,M10.5.0", "pool.ntp.org", "time.nist.gov");
  _ntpPending = true;
}

void GivEnergyMQTT::checkNTP() {
  // The system clock is seeded from the battery-backed RTC at boot, so a
  // plausible timestamp alone doesn't prove NTP landed - ask SNTP directly.
  if (sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED)
    return; // Not synced yet - check again next loop

  time_t now = time(nullptr);
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);

  _ntpPending = false;
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
