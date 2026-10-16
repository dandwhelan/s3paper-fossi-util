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
// Register map correlated across Fossibot + Aferiy units (US 120V and EU 230V)
// in dandwhelan/fossibot-bluetooth PROTOCOL.md. Registers not listed here are
// either always-constant (5, 47, 49, 50, 62, 63) or unused by this firmware.
namespace StatusReg {
static const uint8_t AC_CHARGE_SPEED = 2;      // AC charge speed status (1-5)
static const uint8_t AC_INPUT_WATTS = 3;        // AC Input power (W)
static const uint8_t DC_INPUT_WATTS = 4;        // Solar/DC Input (W)
static const uint8_t TOTAL_INPUT_WATTS = 6;     // Sum of AC + DC (W)
static const uint8_t AC_GRID_POWER = 7;         // Signed: +import / -export (W)
static const uint8_t ERROR_CODE = 8;            // 0=OK, 78=Inverter, 79=Safety Lockout
static const uint8_t AC_CHARGE_RATE = 13;       // Active charge rate level (1-5)
static const uint8_t MAX_AC_INPUT = 14;         // Max AC charge limit (W)
static const uint8_t FREQ_SETTING = 16;         // Output frequency setting (Hz x10)
static const uint8_t AC_OUTPUT_VOLTAGE = 18;    // AC Output Voltage (x10)
static const uint8_t AC_OUTPUT_FREQ = 19;       // AC Output Frequency (x10)
static const uint8_t TOTAL_OUTPUT_POWER = 20;   // Sum of all outputs (W)
static const uint8_t AC_INPUT_VOLTAGE = 21;     // AC input V x10 while on mains;
                                                // multiplexed state code otherwise
                                                // (15 = cold-temp charge lockout)
static const uint8_t BATTERY_VOLTAGE = 22;      // V x 100 (4900 = 49.00V)
static const uint8_t USB_STATE = 24;            // 0/1 USB ports on
static const uint8_t DC_STATE = 25;             // 0/1 DC (12V/car) on
static const uint8_t AC_STATE = 26;             // 0/1 inverter on
static const uint8_t LIGHT_STATE = 27;          // 0-3 (off/on/flash/sos)
static const uint8_t USB_A1_WATTS = 30;         // USB-A1 power (x10)
static const uint8_t USB_A2_WATTS = 31;         // USB-A2 power (x10)
static const uint8_t USB_C1_WATTS = 34;         // USB-C1 power (x10)
static const uint8_t USB_C2_WATTS = 35;         // USB-C2 power (x10)
static const uint8_t USB_C3_WATTS = 36;         // USB-C3 power (x10)
static const uint8_t USB_C4_WATTS = 37;         // USB-C4 power (x10)
static const uint8_t OUTPUT_POWER = 39;         // Output watts (vendor dashboard gauge)
static const uint8_t PACK_CONFIG_VOLTAGE = 40;  // Pack voltage calibration (x10)
static const uint8_t ACTIVE_OUTPUTS = 41;       // Port & subsystem active bitmask
static const uint8_t PROTECTION_FLAGS = 42;     // Bitmask: 0x6000=Critical Fault
static const uint8_t PROTOCOL_VERSION = 47;     // Always 12288 (0x3000)
static const uint8_t SYSTEM_STATUS_FLAGS = 48;  // 0x8000=Charging, 0x4000=Standby
static const uint8_t MODEL_CONSTANT = 52;       // 180=Aferiy, 0=Fossibot (NOT temperature)
static const uint8_t EXT1_SOC = 53;             // Expansion battery 1: 0=absent, else (v-10)/10 %
static const uint8_t BATTERY_FULL_CAPACITY = 54;// Battery full capacity (0.1 Ah)
static const uint8_t EXT2_SOC = 55;             // Expansion battery 2
static const uint8_t MAIN_SOC = 56;             // State of Charge (0.1%)
static const uint8_t BOOKING_CHARGE_DELAY = 57; // Scheduled charge countdown (min)
static const uint8_t TIME_TO_FULL = 58;         // Minutes until full
static const uint8_t TIME_TO_EMPTY = 59;        // Minutes until empty
static const uint8_t AC_STANDBY_COUNTER = 60;   // AC standby countdown (min)
static const uint8_t DC_STANDBY_COUNTER = 61;   // DC standby countdown (min)
static const uint8_t EXT3_SOC = 66;             // Expansion battery 3
static const uint8_t EXT4_SOC = 67;             // Expansion battery 4
static const uint8_t SHUTDOWN_TIMER = 68;       // Machine shutdown countdown
static const uint8_t FAN_LEVEL = 69;            // Fan speed 0-5
} // namespace StatusReg

/**
 * Decode an expansion-battery SOC register (53, 55, 66, 67).
 * 0 means no pack fitted; otherwise the value is (percent * 10) + 10.
 * Returns -1 when absent.
 */
inline float decodeExtSoc(uint16_t raw) {
  if (raw == 0)
    return -1.0f;
  return (raw - 10) / 10.0f;
}

// Port & subsystem active flags for register 41.
// These are the "is this subsystem live" bits, not the user toggle state -
// the toggles are read from status registers 24/25/26 instead, which is what
// the vendor app uses. Kept for charge-source display and as a fallback.
namespace StateBits {
static const uint16_t AC_OUT_ACTIVE = 0x0004;    // bit 2
static const uint16_t AC_IN_PRESENT = 0x0008;    // bit 3 (bits 2+3 = UPS bypass)
static const uint16_t AC_IN_CHARGING = 0x0010;   // bit 4
static const uint16_t LOW_PV_PRESENT = 0x0020;   // bit 5
static const uint16_t LOW_PV_CHARGING = 0x0040;  // bit 6
static const uint16_t DC_OUT_ACTIVE = 0x0080;    // bit 7
static const uint16_t CAR_IN_PRESENT = 0x0100;   // bit 8
static const uint16_t CAR_IN_CHARGING = 0x2000;  // bit 13
static const uint16_t HIGH_PV_PRESENT = 0x4000;  // bit 14
static const uint16_t HIGH_PV_CHARGING = 0x8000; // bit 15

// Legacy fallback bits used before status regs 24-26 were mapped.
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

// CONTROL / HOLDING Registers (OpCode 0x1103 / write via 0x06)
//
// Units matter here and two of them used to be wrong: register 59 is the USB
// standby timer in MINUTES and register 62 is the screen timeout in SECONDS.
// This firmware had them swapped, so "screen off after 3 minutes" was writing
// 3 to the USB standby timer and every screen-timeout write landed on the
// wrong register.
namespace ControlReg {
static const uint8_t FACTORY_RESET = 0;    // Write 1 = factory reset + unbind
static const uint8_t USB_TOGGLE = 24;      // 0/1 - Enable USB ports
static const uint8_t DC_TOGGLE = 25;       // 0/1 - Enable 12V DC
static const uint8_t AC_TOGGLE = 26;       // 0/1 - Enable Inverter
static const uint8_t LIGHT_MODE = 27;      // 0-3 (off/on/flash/sos)
static const uint8_t CHARGE_CURRENT = 20;  // AC charge current limit (Amps)
static const uint8_t KEY_SOUND = 56;       // 0/1 - Button beep
static const uint8_t SILENT_CHARGING = 57; // 0/1 - Quiet mode
static const uint8_t USB_STANDBY = 59;     // Minutes (0=never)
static const uint8_t AC_STANDBY = 60;      // Minutes (0=never)
static const uint8_t DC_STANDBY = 61;      // Minutes (0=never)
static const uint8_t SCREEN_TIMEOUT = 62;  // Seconds (0=never)
static const uint8_t SCHEDULE_CHARGE = 63; // Minutes until charge start
static const uint8_t POWER_OFF = 64;       // Write 1 to shutdown
static const uint8_t DISCHARGE_LIMIT = 66; // % x 10 - Lower SOC limit
static const uint8_t CHARGE_LIMIT = 67;    // % x 10 - Target charge %
static const uint8_t SYS_STANDBY = 68;     // Minutes - NEVER write 0 (bricks device)

// Read-only holding registers (parsed from the 0x1103 response)
static const uint8_t MASTER_ENABLE = 5;          // Master system enable (0/1)
static const uint8_t HARDWARE_ID = 11;           // 0x0600=US, 0x0200/0x0D00=EU
static const uint8_t AC_CHARGE_SPEED_SET = 13;   // AC charge speed setpoint (1-5)
static const uint8_t MAX_CHARGE_WATTAGE = 14;    // 1500=US, 1100=EU
static const uint8_t MAX_CHARGE_CURRENT = 17;    // Hardware ceiling for reg 20 (A)
static const uint8_t MAX_AC_INPUT_CURRENT = 19;  // Deci-amps: 1600=US, 500=EU
static const uint8_t FIRMWARE_VERSION = 32;      // Device firmware identifier
static const uint8_t MCU_VERSION_AC = 47;        // AC inverter sub-MCU
static const uint8_t MCU_VERSION_BMS = 48;       // BMS sub-MCU
static const uint8_t MCU_VERSION_PV = 49;        // Solar MPPT sub-MCU
static const uint8_t MCU_VERSION_DC = 50;        // Front panel / DC sub-MCU
} // namespace ControlReg

/**
 * Decode a sub-MCU firmware register (47-50) to its displayed version number.
 * The vendor app renders (value & 0xFF) / 10 to one decimal place.
 */
inline float decodeMcuVersion(uint16_t raw) { return (raw & 0xFF) / 10.0f; }

/**
 * Write safety whitelist.
 *
 * The Fossibot firmware does NOT validate register writes, and an out-of-range
 * value can brick the unit permanently. Writing 0 to register 68 (whole-machine
 * idle shutdown) is a confirmed field brick - see PROTOCOL.md in
 * dandwhelan/fossibot-bluetooth. Every write goes through here.
 *
 * @return true if (reg, value) is safe to send.
 */
inline bool isWriteAllowed(uint8_t reg, uint16_t value) {
  auto oneOf = [](uint16_t v, const uint16_t *set, size_t n) {
    for (size_t i = 0; i < n; i++)
      if (v == set[i])
        return true;
    return false;
  };

  switch (reg) {
  case ControlReg::CHARGE_CURRENT:
    return value >= 1 && value <= 20;
  case ControlReg::USB_TOGGLE:
  case ControlReg::DC_TOGGLE:
  case ControlReg::AC_TOGGLE:
  case ControlReg::KEY_SOUND:
  case ControlReg::SILENT_CHARGING:
    return value <= 1;
  case ControlReg::LIGHT_MODE:
    return value <= 3;
  case ControlReg::AC_CHARGE_SPEED_SET:
    return value >= 1 && value <= 5;
  case ControlReg::USB_STANDBY:
  case ControlReg::AC_STANDBY:
  case ControlReg::DC_STANDBY:
    return value <= 1440; // minutes, 0 = never
  case ControlReg::SCREEN_TIMEOUT:
    return value <= 14400; // seconds, 0 = never
  case ControlReg::SCHEDULE_CHARGE:
    return value <= 1440;
  case ControlReg::DISCHARGE_LIMIT:
  case ControlReg::CHARGE_LIMIT:
    return value <= 1000; // 0.1% units
  case ControlReg::SYS_STANDBY: {
    // 0 is a confirmed brick. Only the vendor app's own presets are allowed.
    static const uint16_t kAllowed[] = {5, 10, 30, 60, 480};
    return oneOf(value, kAllowed, sizeof(kAllowed) / sizeof(kAllowed[0]));
  }
  case ControlReg::POWER_OFF:
    return value == 1;
  case ControlReg::FACTORY_RESET:
    return false; // never written by this firmware
  default:
    return false; // unknown register: refuse rather than guess
  }
}

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

  // Grid / mains telemetry
  float acInputVoltage = 0.0f;  // Reg 21: volts while on mains (0 when off-grid)
  int acInputStateCode = 0;     // Reg 21 when off-grid: 15 = cold-temp lockout
  float acOutputVoltage = 0.0f; // Reg 18: volts
  float acOutputFreq = 0.0f;    // Reg 19: Hz
  int acGridPower = 0;          // Reg 7: signed, +import / -export (W)

  // Thermal / cooling
  int fanLevel = 0; // Reg 69: 0-5

  // Expansion batteries (regs 53, 55, 66, 67). -1 = pack not fitted.
  float extSoc[4] = {-1.0f, -1.0f, -1.0f, -1.0f};

  // Capacity & countdowns
  float batteryCapacityAh = 0.0f;   // Reg 54 (0.1 Ah)
  int bookingChargeRemaining = -1;  // Reg 57: minutes, -1 = no schedule armed
  int activeChargeRate = 0;         // Reg 13 status: level actually in use

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

  /**
   * Reg 21 doubles as a state code when the station is off mains. Value 15 is
   * the sub-zero charge lockout the panel shows as a thermometer icon.
   */
  bool isColdChargeLockout() const {
    return acInputVoltage <= 0.0f && acInputStateCode == 15;
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
  int acChargeSpeed = 3;         // AC charge speed setpoint 1-5 (reg 13)
  int chargeCurrent = 0;         // AC charge current limit in amps (reg 20)
  int maxChargeCurrent = 20;     // Hardware ceiling for chargeCurrent (reg 17)
  int maxChargeWattage = 0;      // Max AC charge watts (reg 14)
  float mcuVersionAC = 0.0f;     // Inverter sub-MCU firmware (reg 47)
  float mcuVersionBMS = 0.0f;    // BMS sub-MCU firmware (reg 48)
  float mcuVersionPV = 0.0f;     // Solar MPPT sub-MCU firmware (reg 49)
  float mcuVersionDC = 0.0f;     // Front panel sub-MCU firmware (reg 50)
  uint16_t hardwareId = 0;       // Regional hardware variant (reg 11)

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
