/**
 * RTC Hardware Abstraction
 *
 * Direct BM8563 RTC driver using Wire I2C on SDA=41, SCL=42.
 * M5Unified's RTC driver doesn't work when Wire is reconfigured.
 */

#ifndef RTC_H
#define RTC_H

#include <Arduino.h>
#include <Wire.h>
#include <cstdio>
#include <cstring>

namespace RTC {

// BM8563 I2C address
constexpr uint8_t BM8563_ADDR = 0x51;

// BM8563 Register addresses
constexpr uint8_t REG_CONTROL1 = 0x00;
constexpr uint8_t REG_CONTROL2 = 0x01;
constexpr uint8_t REG_SECONDS = 0x02;
constexpr uint8_t REG_MINUTES = 0x03;
constexpr uint8_t REG_HOURS = 0x04;
constexpr uint8_t REG_DAYS = 0x05;
constexpr uint8_t REG_WEEKDAYS = 0x06;
constexpr uint8_t REG_MONTHS = 0x07;
constexpr uint8_t REG_YEARS = 0x08;

// BCD conversion helpers
inline uint8_t bcdToDec(uint8_t bcd) { return (bcd >> 4) * 10 + (bcd & 0x0F); }

inline uint8_t decToBcd(uint8_t dec) { return ((dec / 10) << 4) | (dec % 10); }

// Read a single register
inline uint8_t readReg(uint8_t reg) {
  Wire.beginTransmission(BM8563_ADDR);
  Wire.write(reg);
  Wire.endTransmission();
  Wire.requestFrom(BM8563_ADDR, (uint8_t)1);
  return Wire.available() ? Wire.read() : 0;
}

// Write a single register
inline void writeReg(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(BM8563_ADDR);
  Wire.write(reg);
  Wire.write(value);
  Wire.endTransmission();
}

/**
 * Initialize RTC (clear any alarm/timer flags)
 */
inline void init() {
  writeReg(REG_CONTROL1, 0x00);
  writeReg(REG_CONTROL2, 0x00);
}

/**
 * Check if RTC is accessible
 */
inline bool isPresent() {
  Wire.beginTransmission(BM8563_ADDR);
  return Wire.endTransmission() == 0;
}

/**
 * Get current time components
 */
inline void getTime(int &hours, int &minutes, int &seconds) {
  seconds = bcdToDec(readReg(REG_SECONDS) & 0x7F);
  minutes = bcdToDec(readReg(REG_MINUTES) & 0x7F);
  hours = bcdToDec(readReg(REG_HOURS) & 0x3F);
}

/**
 * Get current date components
 */
inline void getDate(int &year, int &month, int &day, int &weekday) {
  day = bcdToDec(readReg(REG_DAYS) & 0x3F);
  weekday = readReg(REG_WEEKDAYS) & 0x07;
  month = bcdToDec(readReg(REG_MONTHS) & 0x1F);
  year = 2000 + bcdToDec(readReg(REG_YEARS));
}

/**
 * Set time
 */
inline void setTime(int hours, int minutes, int seconds) {
  writeReg(REG_SECONDS, decToBcd(seconds));
  writeReg(REG_MINUTES, decToBcd(minutes));
  writeReg(REG_HOURS, decToBcd(hours));
}

/**
 * Set date
 */
inline void setDate(int year, int month, int day) {
  writeReg(REG_DAYS, decToBcd(day));
  writeReg(REG_MONTHS, decToBcd(month));
  writeReg(REG_YEARS, decToBcd(year - 2000));
}

/**
 * Set date and time together
 */
inline void setDateTime(int year, int month, int day, int hours, int minutes,
                        int seconds) {
  writeReg(REG_SECONDS, decToBcd(seconds));
  writeReg(REG_MINUTES, decToBcd(minutes));
  writeReg(REG_HOURS, decToBcd(hours));
  writeReg(REG_DAYS, decToBcd(day));
  writeReg(REG_MONTHS, decToBcd(month));
  writeReg(REG_YEARS, decToBcd(year - 2000));
}

/**
 * Firmware build date/time, parsed from __DATE__/__TIME__.
 *
 * Used as the fallback when the RTC has never been set (dead coin cell,
 * first boot, chip replaced) - always closer to "now" than the BM8563
 * power-on default of 2000-01-01, and unlike a hardcoded date it doesn't
 * go stale as the firmware is rebuilt over time.
 */
inline void getBuildDateTime(int &year, int &month, int &day, int &hour,
                              int &minute, int &second) {
  static const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  char monStr[4] = {0};
  int d = 1, y = 2026, h = 0, mi = 0, s = 0;
  sscanf(__DATE__, "%3s %d %d", monStr, &d, &y);
  sscanf(__TIME__, "%d:%d:%d", &h, &mi, &s);
  month = 1;
  for (int i = 0; i < 12; i++) {
    if (strncmp(monStr, months[i], 3) == 0) {
      month = i + 1;
      break;
    }
  }
  year = y;
  day = d;
  hour = h;
  minute = mi;
  second = s;
}

/**
 * Get current time as formatted string
 */
inline void getTimeString(char *buffer, size_t size,
                          const char *format = "%H:%M") {
  int hours, minutes, seconds;
  getTime(hours, minutes, seconds);

  int year, month, day, weekday;
  getDate(year, month, day, weekday);

  struct tm timeinfo = {0};
  timeinfo.tm_year = year - 1900;
  timeinfo.tm_mon = month - 1;
  timeinfo.tm_mday = day;
  timeinfo.tm_hour = hours;
  timeinfo.tm_min = minutes;
  timeinfo.tm_sec = seconds;
  strftime(buffer, size, format, &timeinfo);
}

/**
 * Get day of week name (short)
 */
inline const char *getDayName(int weekday) {
  static const char *days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  if (weekday >= 0 && weekday < 7) {
    return days[weekday];
  }
  return "???";
}

/**
 * Get day of week single letter
 */
inline char getDayLetter(int weekday) {
  static const char letters[] = {'S', 'M', 'T', 'W', 'T', 'F', 'S'};
  if (weekday >= 0 && weekday < 7) {
    return letters[weekday];
  }
  return '?';
}

} // namespace RTC

#endif // RTC_H
