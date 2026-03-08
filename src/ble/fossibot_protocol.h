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

// STATUS Registers (OpCode 0x1104 / Read Input Registers) - Read only
//
// FINAL VERIFIED REGISTER MAP:
//   Reg 02: AC Charge Speed Status (1-5)
//   Reg 03: AC Input Watts (actual)
//   Reg 04: DC Input Watts (Solar/Car)
//   Reg 06: Total Input Watts
//   Reg 08: Active Error Code (0=OK, 78=Inverter Fault, 79=Safety Lockout)
//   Reg 18: AC Output Voltage (×10, e.g. 1200 = 120.0V)
//   Reg 19: AC Output Frequency (×10, e.g. 600 = 60.0Hz)
//   Reg 20: Total Output Watts
//   Reg 21: System Bus Voltage (MULTIPLEXED: charging=AC input V×10, discharging=DC bus V)
//   Reg 22: Battery Voltage (÷100 for volts)
//   Reg 30: USB-A1 Watts (×10)
//   Reg 31: USB-A2 Watts (×10)
//   Reg 34: USB-C1 Watts (×10)
//   Reg 35: USB-C2 Watts (×10)
//   Reg 36: USB-C3 Watts (×10)
//   Reg 37: USB-C4 Watts (×10)
//   Reg 39: Output Power (W)
//   Reg 41: Active Port Flags (bitmask for USB/DC/AC icons)
//   Reg 42: Protection Flags (BITMASK - check 0x6000 for critical faults)
//   Reg 47: Protocol Version (always 12288)
//   Reg 48: System Status Flags (0x8000=Charging, 0x4000=Standby, 0x0008=Error)
//   Reg 52: Model Constant (180=Fossibot, 0=Aferiy) - NOT temperature!
//   Reg 54: Battery Full Capacity (0.1 Ah)
//   Reg 56: Main SoC (÷10 for %)
//   Reg 58: Time to Full (minutes)
//   Reg 59: Time to Empty (minutes)
//
namespace StatusReg {
static const uint8_t AC_CHARGE_SPEED = 2;      // AC charge speed status (1-5)
static const uint8_t AC_INPUT_WATTS = 3;        // AC Input power (W)
static const uint8_t DC_INPUT_WATTS = 4;        // Solar/DC Input (W)
static const uint8_t TOTAL_INPUT_WATTS = 6;     // Sum of AC + DC (W) - Max 1100W
static const uint8_t ERROR_CODE = 8;            // Error code: 0=OK, 78=Inverter, 79=Safety Lockout
static const uint8_t AC_OUTPUT_VOLTAGE = 18;    // AC Output Voltage (×10)
static const uint8_t AC_OUTPUT_FREQ = 19;       // AC Output Frequency (×10)
static const uint8_t TOTAL_OUTPUT_POWER = 20;   // Sum of all outputs (W) - Max 3000W
static const uint8_t BUS_VOLTAGE = 21;          // Multiplexed: charging=AC input V×10, discharging=DC bus V
static const uint8_t BATTERY_VOLTAGE = 22;      // V × 100 (4900 = 49.00V)
static const uint8_t USB_A1_WATTS = 30;         // USB-A1 power (×10)
static const uint8_t USB_A2_WATTS = 31;         // USB-A2 power (×10)
static const uint8_t USB_C1_WATTS = 34;         // USB-C1 power (×10)
static const uint8_t USB_C2_WATTS = 35;         // USB-C2 power (×10)
static const uint8_t USB_C3_WATTS = 36;         // USB-C3 power (×10)
static const uint8_t USB_C4_WATTS = 37;         // USB-C4 power (×10)
static const uint8_t ACTIVE_OUTPUTS = 41;       // Bitmask for USB/DC/AC states
static const uint8_t PROTECTION_FLAGS = 42;     // Bitmask: 0x6000=Critical Fault, 0x8000=Warning latch
static const uint8_t PROTOCOL_VERSION = 47;     // Always 12288
static const uint8_t SYSTEM_STATUS_FLAGS = 48;  // 0x8000=Charging, 0x4000=Standby, 0x0008=Error
static const uint8_t MODEL_CONSTANT = 52;       // 180=Fossibot, 0=Aferiy (NOT temperature)
static const uint8_t BATTERY_FULL_CAPACITY = 54;// Battery full capacity (0.1 Ah)
static const uint8_t MAIN_SOC = 56;             // State of Charge (0.1%)
static const uint8_t TIME_TO_FULL = 58;         // Minutes until full
static const uint8_t TIME_TO_EMPTY = 59;        // Minutes until empty
} // namespace StatusReg

// State flag bit masks for register 41 (Active Outputs)
namespace StateBits {
static const uint16_t USB_BIT = 512; // bit 9
static const uint16_t DC_BIT = 1024; // bit 10
static const uint16_t AC_BIT = 2048; // bit 11
} // namespace StateBits

// Protection flag bit masks for register 42
// Reg 42 is "Hardware GPIO & Fault Mask" - mixes status bits with fault bits.
// You CANNOT check `if (Reg42 > 0)` - must use bitmask!
//   Bit 15 (0x8000): System Warning / Non-Critical Latch (often always on)
//   Bits 13 & 14 (0x6000): CRITICAL FAULT MASK
//   Bits 0-12: Output MOSFET Status (e.g., +984 when USB/DC is On)
namespace ProtectionBits {
static const uint16_t CRITICAL_FAULT = 0x6000;   // bits 13,14 - ACTUAL critical faults
static const uint16_t WARNING_LATCH = 0x8000;    // bit 15 - non-critical, often always on
static const uint16_t CRITICAL_HARDWARE = 0xE000; // bits 13,14,15 (legacy compat)
} // namespace ProtectionBits

// System status flag bit masks for register 48
namespace SystemStatusBits {
static const uint16_t AC_CHARGING = 0x8000;      // AC Charging active
static const uint16_t INVERTER_STANDBY = 0x4000; // Inverter Standby/Ready
static const uint16_t ERROR_PENDING = 0x0008;    // Error Pending condition
} // namespace SystemStatusBits

// CONTROL / HOLDING Registers (OpCode 0x1103 / Write via 0x06)
//
// FINAL VERIFIED HOLDING REGISTER MAP:
//   Reg 05: Master System Enable (0=Off, 1=On)
//   Reg 11: Hardware ID (1536=US Aferiy, 512=EU Fossibot)
//   Reg 13: AC Charge Speed Setpoint (1-5)
//   Reg 14: Max Charge Wattage (1500=US, 1100=EU)
//   Reg 19: Max AC Input Current (1600=US, 500=EU)
//   Reg 24: USB Toggle (0/1)
//   Reg 25: DC Toggle (0/1)
//   Reg 26: AC Toggle (0/1)
//   Reg 27: Light Mode (0-3: off/on/flash/sos)
//   Reg 56: Key Sound (0/1)
//   Reg 57: Silent Charging (0/1)
//   Reg 66: Discharge Limit (Min SoC % × 10)
//   Reg 67: Charge Limit (Max SoC % × 10)
//
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

// Read-only holding registers (parsed from 0x1103 response)
static const uint8_t MASTER_ENABLE = 5;        // Master system enable (0/1)
static const uint8_t HARDWARE_ID = 11;         // 1536=US Aferiy, 512=EU Fossibot
static const uint8_t AC_CHARGE_SPEED_SET = 13; // AC charge speed setpoint (1-5)
static const uint8_t MAX_CHARGE_WATTAGE = 14;  // 1500=US, 1100=EU
static const uint8_t MAX_AC_INPUT_CURRENT = 19;// 1600=US, 500=EU
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

  // Output breakdown (derived from per-port registers)
  float usbOutputPower = 0.0f;  // Sum of all 6 USB ports (regs 30,31,34-37 ÷10)
  float acDcOutputPower = 0.0f; // Derived: totalOutput - usbTotal (AC+DC combined)

  // Output states
  bool usbActive = false;
  bool dcActive = false;
  bool acActive = false;

  // Error & Protection state (from status registers)
  uint16_t errorCode = 0;         // Reg 8: 0=OK, 78=Inverter Fault, 79=Safety Lockout
  uint16_t protectionFlags = 0;   // Reg 42: bitmask (check 0x6000 for critical faults)
  uint16_t systemStatusFlags = 0; // Reg 48: 0x8000=Charging, 0x4000=Standby, 0x0008=Error
  bool simulatedError = false;    // Debug: simulated error injection

  /**
   * Check if device has a critical fault.
   * Only triggers on explicit error codes from Reg 8 (78/79).
   * Reg 42 protection flags are NOT checked independently because
   * MOSFET status bits (0-12) can extend into bits 13-14 when outputs
   * (DC/USB/AC) are active, causing false positives. Protection flags
   * are used only for error classification (see hasCriticalHardwareFault).
   */
  bool hasError() const {
    if (simulatedError) return true;
    // Active error codes (78=inverter fault, 79=safety lockout)
    if (errorCode == 78 || errorCode == 79) return true;
    return false;
  }

  /**
   * Check if Reg 42 critical fault bits are set (hardware failure).
   * If Error 79 is present WITHOUT critical bits, it's environmental
   * protection (cold/hot temp) rather than hardware failure.
   */
  bool hasCriticalHardwareFault() const {
    return (protectionFlags & ProtectionBits::CRITICAL_FAULT) != 0;
  }

  /**
   * Check if error is environmental protection (temp safety) vs hardware.
   * Error 79 with no critical fault bits in Reg 42 = Temp/Safety Protection.
   */
  bool isEnvironmentalProtection() const {
    return errorCode == 79 && !hasCriticalHardwareFault();
  }

  bool hasErrorPending() const {
    return (systemStatusFlags & SystemStatusBits::ERROR_PENDING) != 0;
  }

  /**
   * Check if device is actively charging (Reg 48 bit 15).
   */
  bool isCharging() const {
    return (systemStatusFlags & SystemStatusBits::AC_CHARGING) != 0;
  }

  /**
   * Check if inverter is in standby (Reg 48 bit 14).
   */
  bool isStandby() const {
    return (systemStatusFlags & SystemStatusBits::INVERTER_STANDBY) != 0;
  }

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
  uint16_t lastErrorCode = 0;
  uint16_t lastProtectionFlags = 0;
  uint16_t lastSystemStatusFlags = 0;

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
    if (errorCode != lastErrorCode)
      return true;
    if (protectionFlags != lastProtectionFlags)
      return true;
    if (systemStatusFlags != lastSystemStatusFlags)
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
    lastErrorCode = errorCode;
    lastProtectionFlags = protectionFlags;
    lastSystemStatusFlags = systemStatusFlags;
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
