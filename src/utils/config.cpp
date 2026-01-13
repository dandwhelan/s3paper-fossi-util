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
  _wifiSSID = "";
  _wifiPassword = "";
  _theme = "classic_grid";
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

  // WiFi
  if (doc["wifi"].is<JsonObject>()) {
    _wifiSSID = doc["wifi"]["ssid"].as<String>();
    _wifiPassword = doc["wifi"]["password"].as<String>();
  }

  // Bluetooth
  if (doc["bluetooth"].is<JsonObject>()) {
    _fossibotMAC = doc["bluetooth"]["fossibot_mac"].as<String>();
  }

  // Display
  if (doc["display"].is<JsonObject>()) {
    _theme = doc["display"]["theme"] | "classic_grid";
    _autoSleepMinutes = doc["display"]["auto_sleep_minutes"] | 5;
  }

  // Timezone
  if (doc["timezone"].is<JsonObject>()) {
    _timezoneOffset = doc["timezone"]["offset_hours"] | 0;
  }

  // Weather
  if (doc["weather"].is<JsonObject>()) {
    _weatherAPIKey = doc["weather"]["api_key"].as<String>();
    _weatherCity = doc["weather"]["city"] | "London";
    _weatherUnits = doc["weather"]["units"] | "metric";
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

  // WiFi
  doc["wifi"]["ssid"] = _wifiSSID;
  doc["wifi_ssid"] = _wifiSSID;
  doc["wifi_pass"] = _wifiPassword;

  // Bluetooth
  doc["fossibot_mac"] = _fossibotMAC;

  // Display
  doc["theme"] = _theme;
  doc["auto_sleep"] = _autoSleepMinutes;

  // Timezone
  doc["timezone_offset"] = _timezoneOffset;

  // Alarm
  doc["alarm_enabled"] = _alarmEnabled;
  doc["alarm_hour"] = _alarmHour;
  doc["alarm_minute"] = _alarmMinute;

  // Weather
  doc["weather_key"] = _weatherAPIKey;
  doc["weather_city"] = _weatherCity;
  doc["weather_units"] = _weatherUnits;

  // eInk thresholds (removed as per instruction's implied flattening)

  String output;
  serializeJson(doc, output); // Changed to serializeJson as per instruction

  if (sdManager->writeFile(path, output)) { // Changed to use output string
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
