/**
 * Battery Hardware Abstraction
 *
 * M5Paper S3 battery voltage to percentage conversion.
 * Uses direct ADC reading since M5.Power doesn't work on this board.
 * Battery ADC is on GPIO3 with a voltage divider (2:1 ratio assumed).
 */

#ifndef BATTERY_H
#define BATTERY_H

#include <Arduino.h>
#include <M5Unified.h>

namespace Battery {

// Battery ADC pin (from M5Paper S3 schematic - BAT_ADC on G3)
constexpr int BAT_ADC_PIN = 3;

// Voltage divider ratio (typically 2:1 for LiPo monitoring)
constexpr float VOLTAGE_DIVIDER = 2.0f;

// ADC parameters for ESP32-S3
constexpr float ADC_REF_VOLTAGE = 3.3f;
constexpr int ADC_RESOLUTION = 4095;

// LiPo voltage range
constexpr float BATTERY_MAX = 4.20f; // Fully charged
constexpr float BATTERY_MIN = 3.00f; // Safe cutoff

/**
 * Initialize battery ADC
 */
inline void init() {
  analogReadResolution(12);       // 12-bit resolution
  analogSetAttenuation(ADC_11db); // Full range (0-3.3V)
}

/**
 * Get current battery voltage in volts using direct ADC reading
 */
inline float getVoltage() {
  // First try M5.Power (in case it works on some units)
  float m5Voltage = M5.Power.getBatteryVoltage() / 1000.0f;
  if (m5Voltage > 0.5f) {
    return m5Voltage;
  }

  // Fallback to direct ADC reading
  int rawADC = analogRead(BAT_ADC_PIN);
  float adcVoltage = (rawADC / (float)ADC_RESOLUTION) * ADC_REF_VOLTAGE;
  float batteryVoltage = adcVoltage * VOLTAGE_DIVIDER;

  // Debug output
  Serial.printf("Battery ADC: raw=%d, adcV=%.2f, batV=%.2f\n", rawADC,
                adcVoltage, batteryVoltage);

  return batteryVoltage;
}

/**
 * Convert battery voltage to percentage using LiPo discharge curve
 * @param voltage Battery voltage in volts
 * @return Battery percentage (0-100)
 */
inline int voltageToPercentage(float voltage) {
  // LiPo discharge curve lookup table
  // LiPo discharge curve lookup table (Calibrated: 3.93V+ = 100%)
  const float dischargeCurve[][2] = {
      {3.93f, 100}, {3.90f, 95}, {3.87f, 90}, {3.84f, 85}, {3.81f, 80},
      {3.79f, 75},  {3.77f, 70}, {3.75f, 65}, {3.73f, 60}, {3.71f, 55},
      {3.69f, 50},  {3.67f, 45}, {3.65f, 40}, {3.62f, 35}, {3.60f, 30},
      {3.57f, 25},  {3.53f, 20}, {3.48f, 15}, {3.40f, 10}, {3.30f, 5},
      {3.20f, 0}};

  const int curveSize = sizeof(dischargeCurve) / sizeof(dischargeCurve[0]);

  // If above max, return 100%
  if (voltage >= dischargeCurve[0][0])
    return 100;

  // If below min, return 0%
  if (voltage <= dischargeCurve[curveSize - 1][0])
    return 0;

  // Interpolate between points
  for (int i = 0; i < curveSize - 1; i++) {
    if (voltage >= dischargeCurve[i + 1][0]) {
      float v1 = dischargeCurve[i][0];
      float p1 = dischargeCurve[i][1];
      float v2 = dischargeCurve[i + 1][0];
      float p2 = dischargeCurve[i + 1][1];

      // Linear interpolation
      float percentage = p1 + (voltage - v1) * (p2 - p1) / (v2 - v1);
      return (int)percentage;
    }
  }

  return 0;
}

/**
 * Get current battery percentage
 */
inline int getPercentage() {
  float voltage = getVoltage();
  return voltageToPercentage(voltage);
}

/**
 * Check if battery is charging (may not work on all units)
 */
inline bool isCharging() { return M5.Power.isCharging(); }

/**
 * Get battery status string
 */
inline const char *getStatusString() {
  if (isCharging())
    return "Charging";
  int pct = getPercentage();
  if (pct > 75)
    return "Good";
  if (pct > 25)
    return "OK";
  if (pct > 10)
    return "Low";
  return "Critical";
}

} // namespace Battery

#endif // BATTERY_H
