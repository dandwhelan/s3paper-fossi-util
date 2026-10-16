/**
 * GivEnergy Data Structure
 *
 * Holds real-time solar/battery/grid data received from GivTCP3 via MQTT.
 * Designed to mirror the Fossibot::PowerBankData pattern for UI consistency.
 */

#ifndef GIVENERGY_DATA_H
#define GIVENERGY_DATA_H

#include <Arduino.h>

namespace GivEnergy {

struct SolarData {
  bool connected = false;

  // Solar PV
  float pvPowerString1 = 0.0f;  // String 1 (Front) watts
  float pvPowerString2 = 0.0f;  // String 2 (Back) watts
  float pvPowerTotal = 0.0f;    // Total PV watts

  // Battery
  float batteryPercent = 0.0f;  // SOC 0-100%
  float batteryPower = 0.0f;    // Watts (positive=discharging, negative=charging)
  float chargePower = 0.0f;     // Always >= 0
  float dischargePower = 0.0f;  // Always >= 0

  // Grid
  float gridPower = 0.0f;       // Net grid (negative=importing, positive=exporting)
  float importPower = 0.0f;     // Always >= 0
  float exportPower = 0.0f;     // Always >= 0

  // House/Load
  float loadPower = 0.0f;       // Total house consumption

  // Power Flows (where energy is going)
  float solarToHouse = 0.0f;
  float solarToBattery = 0.0f;
  float solarToGrid = 0.0f;
  float batteryToHouse = 0.0f;
  float batteryToGrid = 0.0f;
  float gridToHouse = 0.0f;
  float gridToBattery = 0.0f;

  // Energy totals (today, kWh)
  float pvEnergyToday = 0.0f;
  float importEnergyToday = 0.0f;
  float exportEnergyToday = 0.0f;
  float loadEnergyToday = 0.0f;
  float batteryChargeToday = 0.0f;
  float batteryDischargeToday = 0.0f;

  // For change detection
  float lastBatteryPercent = -1.0f;
  float lastPvPowerTotal = -1.0f;
  float lastLoadPower = -1.0f;
  float lastGridPower = -1.0f;

  // Timestamp of last MQTT message
  unsigned long lastUpdateTime = 0;

  bool hasSignificantChange(int socThreshold = 1,
                            int powerThreshold = 10) const {
    if (lastBatteryPercent < 0)
      return true; // First data
    if (fabsf(batteryPercent - lastBatteryPercent) >= socThreshold)
      return true;
    if (fabsf(pvPowerTotal - lastPvPowerTotal) >= powerThreshold)
      return true;
    if (fabsf(loadPower - lastLoadPower) >= powerThreshold)
      return true;
    if (fabsf(gridPower - lastGridPower) >= powerThreshold)
      return true;
    return false;
  }

  void markRefreshed() {
    lastBatteryPercent = batteryPercent;
    lastPvPowerTotal = pvPowerTotal;
    lastLoadPower = loadPower;
    lastGridPower = gridPower;
  }

  /**
   * Check if data is stale (no updates for > timeout ms)
   */
  bool isStale(unsigned long timeoutMs = 120000) const {
    if (lastUpdateTime == 0)
      return true;
    return (millis() - lastUpdateTime) > timeoutMs;
  }
};

/**
 * Format watts for display: "1,250W" or "0W"
 */
inline String formatWatts(float watts) {
  int w = (int)fabsf(watts);
  if (w >= 1000) {
    return String(w / 1000) + "," + String((w % 1000) / 100) +
           String((w % 100) / 10) + String(w % 10) + "W";
  }
  return String(w) + "W";
}

/**
 * Format kWh for display: "4.2 kWh"
 */
inline String formatKWh(float kwh) {
  char buf[16];
  snprintf(buf, sizeof(buf), "%.1f kWh", kwh);
  return String(buf);
}

} // namespace GivEnergy

#endif // GIVENERGY_DATA_H
