/**
 * Fossibot BLE Protocol Definitions
 *
 * Based on:
 * - https://github.com/Ylianst/ESP-FBot
 * - https://github.com/dandwhelan/fossibot-bluetooth
 */

#ifndef FOSSIBOT_PROTOCOL_H
#define FOSSIBOT_PROTOCOL_H

#include <Arduino.h>

namespace Fossibot {

// BLE Service and Characteristic UUIDs (from ESP-FBot)
static const char *SERVICE_UUID = "0000a002-0000-1000-8000-00805f9b34fb";
static const char *WRITE_CHAR_UUID = "0000c304-0000-1000-8000-00805f9b34fb";
static const char *NOTIFY_CHAR_UUID = "0000c305-0000-1000-8000-00805f9b34fb";

// OpCodes
static const uint16_t OPCODE_STATUS = 0x1104;   // Real-time telemetry
static const uint16_t OPCODE_SETTINGS = 0x1103; // Device configuration

// STATUS Registers (OpCode 0x1104) - Read only
namespace StatusReg {
static const uint8_t AC_INPUT_WATTS = 3;    // AC Input power (W)
static const uint8_t DC_INPUT_WATTS = 4;    // Solar/DC Input (W)
static const uint8_t TOTAL_INPUT_WATTS = 6; // Sum of AC + DC (W) - Max 1100W
static const uint8_t TOTAL_OUTPUT_POWER =
    20;                                    // Sum of all outputs (W) - Max 3000W
static const uint8_t BATTERY_VOLTAGE = 22; // V × 100 (4900 = 49.00V)
static const uint8_t ACTIVE_OUTPUTS = 41;  // Bitmask for USB/DC/AC states
static const uint8_t MAIN_SOC = 56;        // State of Charge (0.1%)
} // namespace StatusReg

// State flag bit masks for register 41 (Active Outputs)
namespace StateBits {
static const uint16_t USB_BIT = 512; // bit 9
static const uint16_t DC_BIT = 1024; // bit 10
static const uint16_t AC_BIT = 2048; // bit 11
} // namespace StateBits

// CONTROL Registers (Write)
namespace ControlReg {
static const uint8_t USB_TOGGLE = 24;      // 0/1 - Enable USB ports
static const uint8_t DC_TOGGLE = 25;       // 0/1 - Enable 12V DC
static const uint8_t AC_TOGGLE = 26;       // 0/1 - Enable Inverter
static const uint8_t LIGHT_MODE = 27;      // 0-3 (off/on/flash/sos)
static const uint8_t KEY_SOUND = 56;       // 0/1 - Button beep
static const uint8_t SILENT_CHARGING = 57; // 0/1 - Quiet mode
static const uint8_t SCREEN_TIMEOUT = 59;  // Minutes
static const uint8_t AC_STANDBY = 60;      // Minutes (0=never)
static const uint8_t DC_STANDBY = 61;      // Minutes (0=never)
static const uint8_t USB_STANDBY = 62;     // Seconds
static const uint8_t SCHEDULE_CHARGE = 63; // Minutes until charge start
static const uint8_t POWER_OFF = 64;       // Write 1 to shutdown
static const uint8_t DISCHARGE_LIMIT = 66; // % × 10 - Lower SOC limit
static const uint8_t CHARGE_LIMIT = 67;    // % × 10 - Target charge %
static const uint8_t SYS_STANDBY = 68;     // Minutes (0=never)
} // namespace ControlReg

// Power limits for progress bar scaling
static const uint16_t MAX_INPUT_POWER = 1100;  // Max input watts
static const uint16_t MAX_OUTPUT_POWER = 3000; // Max output watts

/**
 * Power bank data structure
 */
struct PowerBankData {
  // Connection
  bool connected = false;

  // Battery
  float batteryPercent = 0.0f; // 0-100%
  float batteryVoltage = 0.0f; // Volts

  // Power
  float inputPower = 0.0f;   // Input watts (0-1100W)
  float outputPower = 0.0f;  // Output watts (0-3000W)
  float acInputPower = 0.0f; // AC input component
  float dcInputPower = 0.0f; // DC/Solar input component

  // Output states
  bool usbActive = false;
  bool dcActive = false;
  bool acActive = false;

  // Calculated times
  int minutesToFull = -1;  // -1 = not charging
  int minutesToEmpty = -1; // -1 = not discharging

  // Fossibot Settings (from 0x1103 response)
  bool settingsReceived = false; // True if settings packet received
  bool buzzerEnabled = true;     // Key sound (reg 56)
  bool silentCharging = false;   // AC Silent mode (reg 57)
  int lightMode = 0;             // 0-3 (off/on/flash/sos) (reg 27)
  int dischargeLimit = 0;        // 0-30% (reg 66, stored as %)
  int chargeLimit = 100;         // 60-100% (reg 67, stored as %)
  int screenTimeout = 60;        // Minutes (reg 59)
  int sysStandby = 5;            // Minutes (reg 68)
  int acStandby = 60;            // Minutes (reg 60)
  int dcStandby = 60;            // Minutes (reg 61)
  int usbStandby = 300;          // Seconds (reg 62)
  int scheduleCharge = 0;        // Minutes remaining (reg 63)

  // For change detection
  float lastBatteryPercent = -1.0f;
  float lastInputPower = -1.0f;
  float lastOutputPower = -1.0f;

  /**
   * Calculate time remaining
   * @param capacityWh Total battery capacity in Wh
   */
  void calculateTimes(float capacityWh = 3600.0f) {
    // Time to full (charging)
    if (inputPower > outputPower && inputPower > 0) {
      float netPower = inputPower - outputPower;
      float remainingCapacity = capacityWh * (1.0f - batteryPercent / 100.0f);
      minutesToFull = (int)(remainingCapacity / netPower * 60.0f);
    } else {
      minutesToFull = -1;
    }

    // Time to empty (discharging)
    if (outputPower > inputPower && outputPower > 0) {
      float netDraw = outputPower - inputPower;
      float currentCapacity = capacityWh * batteryPercent / 100.0f;
      minutesToEmpty = (int)(currentCapacity / netDraw * 60.0f);
    } else {
      minutesToEmpty = -1;
    }
  }

  /**
   * Check if data changed significantly (for eInk refresh optimization)
   * @param socThreshold Minimum SOC change (default 1%)
   * @param powerThreshold Minimum power change (default 5W)
   */
  bool hasSignificantChange(int socThreshold = 1,
                            int powerThreshold = 5) const {
    if (lastBatteryPercent < 0)
      return true; // First data

    if (abs(batteryPercent - lastBatteryPercent) >= socThreshold)
      return true;
    if (abs(inputPower - lastInputPower) >= powerThreshold)
      return true;
    if (abs(outputPower - lastOutputPower) >= powerThreshold)
      return true;

    return false;
  }

  /**
   * Update "last" values after refresh
   */
  void markRefreshed() {
    lastBatteryPercent = batteryPercent;
    lastInputPower = inputPower;
    lastOutputPower = outputPower;
  }
};

/**
 * Format minutes as "Xh Ym" string
 */
inline String formatTime(int minutes) {
  if (minutes < 0)
    return "--";
  if (minutes < 60)
    return String(minutes) + "m";

  int hours = minutes / 60;
  int mins = minutes % 60;
  return String(hours) + "h " + String(mins) + "m";
}

} // namespace Fossibot

#endif // FOSSIBOT_PROTOCOL_H
