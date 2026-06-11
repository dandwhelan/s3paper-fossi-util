/**
 * Configuration Manager Implementation
 */

#include "config.h"
#include "sd_manager.h"
#include <SD.h>

extern SDManager *sdManager;

Config::Config() { setDefaults(); }

Config::~Config() {}

void Config::setDefaults() {
  _mode = "campervan";
  _wifiSSID = "";
  _wifiPassword = "";
  _theme = "classic_grid";
  _homeTheme = "energy_flow";
  _autoSleepMinutes = 60; // Default to 60 minutes (User Request)
  _timezoneOffset = 0;
  _weatherAPIKey = "";
  _weatherCity = "London";
  _weatherUnits = "metric";
  _socChangeThreshold = 1;   // Refresh on 1% SOC change
  _powerChangeThreshold = 5; // Refresh on 5W power change

  _alarmEnabled = false;
  _alarmHour = 7;
  _alarmMinute = 0;

  _bluetoothEnabled = true;
  _wifiEnabled = true;

  // MQTT defaults
  _mqttBroker = "";
  _mqttPort = 1883;
  _mqttUsername = "";
  _mqttPassword = "";
  _inverterSN = "";
}

bool Config::load(const char *path) {
  if (!sdManager || !sdManager->isAvailable()) {
    Serial.println("Config: SD card not available");
    return false;
  }

  String content = sdManager->readFile(path);
  if (content.isEmpty()) {
    Serial.println("Config: File empty or not found");
    return false;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, content);

  if (error) {
    Serial.printf("Config: JSON parse error: %s\n", error.c_str());
    return false;
  }

  // Device mode
  _mode = doc["mode"] | "campervan";
  Serial.printf("Config: Mode = '%s'\n", _mode.c_str());

  // WiFi
  if (doc["wifi"].is<JsonObject>()) {
    _wifiSSID = doc["wifi"]["ssid"].as<String>();
    _wifiPassword = doc["wifi"]["password"].as<String>();
  }

  // MQTT
  if (doc["mqtt"].is<JsonObject>()) {
    _mqttBroker = doc["mqtt"]["broker"].as<String>();
    _mqttPort = doc["mqtt"]["port"] | 1883;
    _mqttUsername = doc["mqtt"]["username"].as<String>();
    _mqttPassword = doc["mqtt"]["password"].as<String>();
    _inverterSN = doc["mqtt"]["inverter_sn"].as<String>();
    Serial.printf("Config: MQTT broker = '%s', inverter = '%s'\n",
                  _mqttBroker.c_str(), _inverterSN.c_str());
  }

  // Bluetooth - support both nested and flat format
  if (doc["bluetooth"].is<JsonObject>()) {
    _fossibotMAC = doc["bluetooth"]["fossibot_mac"].as<String>();
    Serial.printf("Config: Loaded fossibot_mac = '%s' (nested)\n",
                  _fossibotMAC.c_str());
  } else if (doc["fossibot_mac"]) {
    // Flat format fallback
    _fossibotMAC = doc["fossibot_mac"].as<String>();
    Serial.printf("Config: Loaded fossibot_mac = '%s' (flat)\n",
                  _fossibotMAC.c_str());
  } else {
    Serial.println("Config: No fossibot_mac found in config");
  }

  // Radio enable toggles
  if (doc["bluetooth"].is<JsonObject>()) {
    _bluetoothEnabled = doc["bluetooth"]["enabled"] | true;
  }
  if (doc["wifi"].is<JsonObject>()) {
    _wifiEnabled = doc["wifi"]["enabled"] | true;
  }

  // Display
  if (doc["display"].is<JsonObject>()) {
    _theme = doc["display"]["theme"] | "classic_grid";
    _homeTheme = doc["display"]["home_theme"] | "energy_flow";
    _autoSleepMinutes = doc["display"]["auto_sleep_minutes"] | 5;
  }

  // Timezone (flat key fallback for configs written by older save())
  if (doc["timezone"].is<JsonObject>()) {
    _timezoneOffset = doc["timezone"]["offset_hours"] | 0;
  } else if (doc["timezone_offset"].is<int>()) {
    _timezoneOffset = doc["timezone_offset"];
  }

  // Alarm
  if (doc["alarm"].is<JsonObject>()) {
    _alarmEnabled = doc["alarm"]["enabled"] | false;
    _alarmHour = doc["alarm"]["hour"] | 7;
    _alarmMinute = doc["alarm"]["minute"] | 0;
  } else if (doc["alarm_enabled"].is<bool>()) {
    // Flat key fallback for configs written by older save()
    _alarmEnabled = doc["alarm_enabled"] | false;
    _alarmHour = doc["alarm_hour"] | 7;
    _alarmMinute = doc["alarm_minute"] | 0;
  }

  // Weather (flat key fallback for configs written by older save())
  if (doc["weather"].is<JsonObject>()) {
    _weatherAPIKey = doc["weather"]["api_key"].as<String>();
    _weatherCity = doc["weather"]["city"] | "London";
    _weatherUnits = doc["weather"]["units"] | "metric";
  } else if (doc["weather_key"].is<const char *>()) {
    _weatherAPIKey = doc["weather_key"].as<String>();
    _weatherCity = doc["weather_city"] | "London";
    _weatherUnits = doc["weather_units"] | "metric";
  }

  // eInk thresholds
  if (doc["eink"].is<JsonObject>()) {
    _socChangeThreshold = doc["eink"]["soc_change_threshold"] | 1;
    _powerChangeThreshold = doc["eink"]["power_change_threshold"] | 5;
  }

  Serial.println("Config: Loaded successfully");
  return true;
}

bool Config::save(const char *path) {
  if (!sdManager || !sdManager->isAvailable()) {
    Serial.println("Config: SD card not available");
    return false;
  }

  JsonDocument doc;

  // Device mode
  doc["mode"] = _mode;

  // WiFi
  doc["wifi"]["ssid"] = _wifiSSID;
  doc["wifi"]["password"] = _wifiPassword;
  doc["wifi"]["enabled"] = _wifiEnabled;

  // Bluetooth
  doc["bluetooth"]["fossibot_mac"] = _fossibotMAC;
  doc["bluetooth"]["enabled"] = _bluetoothEnabled;

  // MQTT
  doc["mqtt"]["broker"] = _mqttBroker;
  doc["mqtt"]["port"] = _mqttPort;
  doc["mqtt"]["username"] = _mqttUsername;
  doc["mqtt"]["password"] = _mqttPassword;
  doc["mqtt"]["inverter_sn"] = _inverterSN;

  // Display (nested format to match load)
  doc["display"]["theme"] = _theme;
  doc["display"]["home_theme"] = _homeTheme;
  doc["display"]["auto_sleep_minutes"] = _autoSleepMinutes;

  // Timezone (nested format to match load)
  doc["timezone"]["offset_hours"] = _timezoneOffset;

  // Alarm (nested format to match load)
  doc["alarm"]["enabled"] = _alarmEnabled;
  doc["alarm"]["hour"] = _alarmHour;
  doc["alarm"]["minute"] = _alarmMinute;

  // Weather (nested format to match load)
  doc["weather"]["api_key"] = _weatherAPIKey;
  doc["weather"]["city"] = _weatherCity;
  doc["weather"]["units"] = _weatherUnits;

  // eInk thresholds (nested format to match load)
  doc["eink"]["soc_change_threshold"] = _socChangeThreshold;
  doc["eink"]["power_change_threshold"] = _powerChangeThreshold;

  String output;
  serializeJson(doc, output);

  if (sdManager->writeFile(path, output)) {
    Serial.println("Config: Saved successfully");
    return true;
  }

  Serial.println("Config: Failed to save");
  return false;
}

void Config::setWiFi(const String &ssid, const String &password) {
  _wifiSSID = ssid;
  _wifiPassword = password;
}

void Config::setFossibotMAC(const String &mac) { _fossibotMAC = mac; }

void Config::setTheme(const String &theme) { _theme = theme; }

void Config::setHomeTheme(const String &theme) { _homeTheme = theme; }

void Config::setAutoSleepMinutes(int minutes) { _autoSleepMinutes = minutes; }

void Config::setAlarmEnabled(bool enabled) { _alarmEnabled = enabled; }
void Config::setAlarmHour(int hour) { _alarmHour = hour; }
void Config::setAlarmMinute(int minute) { _alarmMinute = minute; }

void Config::setTimezoneOffset(int hours) { _timezoneOffset = hours; }

void Config::setWeather(const String &apiKey, const String &city,
                        const String &units) {
  _weatherAPIKey = apiKey;
  _weatherCity = city;
  _weatherUnits = units;
}

void Config::setMode(const String &mode) { _mode = mode; }

void Config::setMQTT(const String &broker, int port, const String &username,
                     const String &password) {
  _mqttBroker = broker;
  _mqttPort = port;
  _mqttUsername = username;
  _mqttPassword = password;
}

void Config::setInverterSN(const String &sn) { _inverterSN = sn; }
