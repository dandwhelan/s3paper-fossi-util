/**
 * Configuration Manager
 *
 * Handles loading and saving application configuration from SD card.
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <ArduinoJson.h>

class Config {
public:
  Config();
  ~Config();

  /**
   * Load configuration from JSON file
   * @param path Path to config file
   * @return true if loaded successfully
   */
  bool load(const char *path);

  /**
   * Save configuration to JSON file
   * @param path Path to config file
   * @return true if saved successfully
   */
  bool save(const char *path);

  /**
   * Set default configuration values
   */
  void setDefaults();

  // Device mode: "campervan" (BLE/Fossibot) or "home" (WiFi/MQTT/GivEnergy)
  String getMode() const { return _mode; }
  void setMode(const String &mode);
  bool isHomeMode() const { return _mode == "home"; }
  bool isCampervanMode() const { return _mode != "home"; }

  // WiFi settings
  String getWiFiSSID() const { return _wifiSSID; }
  String getWiFiPassword() const { return _wifiPassword; }
  void setWiFi(const String &ssid, const String &password);

  // Bluetooth settings
  String getFossibotMAC() const { return _fossibotMAC; }
  void setFossibotMAC(const String &mac);

  // Radio enable toggles (persisted so an OFF radio stays off across reboots)
  bool getBluetoothEnabled() const { return _bluetoothEnabled; }
  void setBluetoothEnabled(bool enabled) { _bluetoothEnabled = enabled; }
  bool getWiFiEnabled() const { return _wifiEnabled; }
  void setWiFiEnabled(bool enabled) { _wifiEnabled = enabled; }

  // MQTT settings (Home mode)
  String getMQTTBroker() const { return _mqttBroker; }
  int getMQTTPort() const { return _mqttPort; }
  String getMQTTUsername() const { return _mqttUsername; }
  String getMQTTPassword() const { return _mqttPassword; }
  String getInverterSN() const { return _inverterSN; }
  void setMQTT(const String &broker, int port, const String &username,
               const String &password);
  void setInverterSN(const String &sn);

  // Display settings
  String getTheme() const { return _theme; }          // Campervan themes
  void setTheme(const String &theme);
  String getHomeTheme() const { return _homeTheme; }  // Home mode themes
  void setHomeTheme(const String &theme);
  int getAutoSleepMinutes() const { return _autoSleepMinutes; }
  void setAutoSleepMinutes(int minutes);

  // Timezone
  int getTimezoneOffset() const { return _timezoneOffset; }
  void setTimezoneOffset(int hours);

  // Weather
  String getWeatherAPIKey() const { return _weatherAPIKey; }
  String getWeatherCity() const { return _weatherCity; }
  String getWeatherUnits() const { return _weatherUnits; }
  void setWeather(const String &apiKey, const String &city,
                  const String &units);

  // Power bank thresholds for significant change detection
  int getSOCChangeThreshold() const { return _socChangeThreshold; }
  int getPowerChangeThreshold() const { return _powerChangeThreshold; }

  // Alarm settings
  bool getAlarmEnabled() const { return _alarmEnabled; }
  void setAlarmEnabled(bool enabled);
  int getAlarmHour() const { return _alarmHour; }
  void setAlarmHour(int hour);
  int getAlarmMinute() const { return _alarmMinute; }
  void setAlarmMinute(int minute);

private:
  // Settings variables
  // Device mode
  String _mode;

  // WiFi
  String _wifiSSID;
  String _wifiPassword;

  // Bluetooth
  String _fossibotMAC;

  // Radio enable toggles
  bool _bluetoothEnabled;
  bool _wifiEnabled;

  // MQTT
  String _mqttBroker;
  int _mqttPort;
  String _mqttUsername;
  String _mqttPassword;
  String _inverterSN;

  // Display
  String _theme;      // Campervan themes: classic_grid|compact_status|horizontal_bars|sector
  String _homeTheme;  // Home themes: energy_flow|battery_focus|todays_story
  int _autoSleepMinutes;

  // Timezone
  int _timezoneOffset;

  // Alarm
  bool _alarmEnabled;
  int _alarmHour;
  int _alarmMinute;

  // Weather
  String _weatherAPIKey;
  String _weatherCity;
  String _weatherUnits;

  // eInk refresh thresholds
  int _socChangeThreshold;   // SOC change % to trigger refresh
  int _powerChangeThreshold; // Power change W to trigger refresh
};

#endif // CONFIG_H
