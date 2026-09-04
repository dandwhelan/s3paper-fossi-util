/**
 * Battery Hardware Abstraction
 *
 * M5Paper S3 battery voltage to percentage conversion.
 * Reads through M5.Power where the board was detected correctly, and falls
 * back to reading the divider on GPIO3 (2:1) directly. Either way the raw
 * reading is noisy, so it is median-filtered and smoothed - see getVoltage().
 */

#ifndef BATTERY_H
#define BATTERY_H

#include <Arduino.h>
#include <M5Unified.h>
#include <math.h>

namespace Battery {

// Battery ADC pin (from M5Paper S3 schematic - BAT_ADC on G3)
constexpr int BAT_ADC_PIN = 3;

// Voltage divider ratio (typically 2:1 for LiPo monitoring)
constexpr float VOLTAGE_DIVIDER = 2.0f;

// ADC parameters for ESP32-S3 (raw-read fallback only; analogReadMilliVolts()
// applies the chip's factory calibration and is used in preference)
constexpr float ADC_REF_VOLTAGE = 3.3f;
constexpr int ADC_RESOLUTION = 4095;

// LiPo voltage range
constexpr float BATTERY_MAX = 4.20f; // Fully charged
constexpr float BATTERY_MIN = 3.00f; // Safe cutoff

// The battery ADC is a bare divider with no filter cap, so a single sample
// lands anywhere in a ~100mV band as the e-ink panel, the radio and the SD
// card load the rail. In the flat middle of the LiPo curve that is worth
// ~10 percentage points, which is why the reading used to jump around.
// Three things settle it: take several samples and use the median (kills
// single-sample spikes), smooth across calls, and only re-sample every couple
// of seconds — checkPowerManagement() asks for the voltage on every main-loop
// pass, and 15 ADC conversions every 10ms would be pure waste.
constexpr int SAMPLE_COUNT = 15;                  // odd: median is a real sample
constexpr unsigned long SAMPLE_INTERVAL_MS = 2000;
constexpr float SMOOTHING = 0.25f;    // EMA weight of each new reading
constexpr float RESYNC_DELTA = 0.25f; // beyond this, follow the change at once

// Percentage deadband: below this the reported figure holds, so the number
// stops flickering by a point between refreshes.
constexpr int PERCENT_DEADBAND = 2;

/**
 * Initialize battery ADC
 */
inline void init() {
  analogReadResolution(12);       // 12-bit resolution
  analogSetAttenuation(ADC_11db); // Full range (0-3.3V)
}

/**
 * One raw battery voltage reading, in volts.
 *
 * M5Unified knows this board's divider (pmic_adc, GPIO3, ratio 2.0) and its
 * reading is already calibration-corrected, so it is preferred — but only
 * when it lands in a plausible LiPo range, since a mis-detected board
 * silently returns nonsense.
 */
inline float readVoltageOnce() {
  float m5Voltage = M5.Power.getBatteryVoltage() / 1000.0f;
  if (m5Voltage > 2.5f && m5Voltage < 4.6f) {
    return m5Voltage;
  }

  // Fallback: read GPIO3 ourselves. Nothing else calls init(), so make sure
  // the pin is configured before the first read.
  static bool adcReady = false;
  if (!adcReady) {
    init();
    adcReady = true;
  }

  // analogReadMilliVolts() applies the eFuse
  // calibration curve; raw/4095*3.3 does not and is off by over 100mV on some
  // parts, which is a chunk of the LiPo range.
  uint32_t mv = analogReadMilliVolts(BAT_ADC_PIN);
  if (mv > 0) {
    return (mv / 1000.0f) * VOLTAGE_DIVIDER;
  }

  int rawADC = analogRead(BAT_ADC_PIN);
  return (rawADC / (float)ADC_RESOLUTION) * ADC_REF_VOLTAGE * VOLTAGE_DIVIDER;
}

/**
 * Median of SAMPLE_COUNT consecutive readings.
 */
inline float sampleVoltage() {
  float samples[SAMPLE_COUNT];
  for (int i = 0; i < SAMPLE_COUNT; i++) {
    samples[i] = readVoltageOnce();
  }
  // Insertion sort - SAMPLE_COUNT is tiny
  for (int i = 1; i < SAMPLE_COUNT; i++) {
    float v = samples[i];
    int j = i - 1;
    while (j >= 0 && samples[j] > v) {
      samples[j + 1] = samples[j];
      j--;
    }
    samples[j + 1] = v;
  }
  return samples[SAMPLE_COUNT / 2];
}

/**
 * Get current battery voltage in volts (median-filtered and smoothed).
 */
inline float getVoltage() {
  static float filtered = 0.0f;
  static unsigned long lastSample = 0;

  unsigned long now = millis();
  if (filtered > 0.0f && (now - lastSample) < SAMPLE_INTERVAL_MS) {
    return filtered;
  }
  lastSample = now;

  float v = sampleVoltage();
  if (v < 2.0f) {
    // Bad read (no battery / ADC not ready) - keep the last good value
    return filtered;
  }

  if (filtered <= 0.0f || fabsf(v - filtered) > RESYNC_DELTA) {
    filtered = v; // first reading, or a real step (charger plugged in)
  } else {
    filtered += SMOOTHING * (v - filtered);
  }

#ifdef SERIAL_DEBUG
  Serial.printf("Battery: sample=%.3fV filtered=%.3fV\n", v, filtered);
#endif

  return filtered;
}

/**
 * Convert battery voltage to percentage using LiPo discharge curve
 * @param voltage Battery voltage in volts
 * @return Battery percentage (0-100)
 */
inline int voltageToPercentage(float voltage) {
  // LiPo discharge curve lookup table
  // LiPo discharge curve lookup table (Calibrated: 4.13V+ = 100%)
  const float dischargeCurve[][2] = {
      {4.13f, 100}, {4.08f, 95}, {4.04f, 90}, {3.99f, 85}, {3.95f, 80},
      {3.90f, 75},  {3.86f, 70}, {3.81f, 65}, {3.76f, 60}, {3.71f, 55},
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
  static int reported = -1;
  int pct = voltageToPercentage(getVoltage());
  if (reported < 0 || pct == 0 || pct == 100 ||
      abs(pct - reported) >= PERCENT_DEADBAND) {
    reported = pct;
  }
  return reported;
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
