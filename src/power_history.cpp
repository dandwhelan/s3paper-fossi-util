#include "power_history.h"
#include "utils/sd_manager.h" // Include SDManager
#include <Arduino.h>
#include <M5Unified.h>
#include <SD.h>
#include <time.h>

// Global SDManager instance
extern SDManager *sdManager;

PowerHistory::PowerHistory()
    : _currentDayIndex(0), _currentSampleIndex(0), _lastFlushTime(0),
      _lastFlushedSample(0) {
  // Initialize buffer to zero
  memset(_historyData, 0, sizeof(_historyData));
}

void PowerHistory::init() {
  Serial.println("[PowerHistory] Initializing...");

  // Get current time to determine which day we're on
  time_t now = time(nullptr);
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);

  // Calculate which day of week (0-6)
  _currentDayIndex = timeinfo.tm_wday;

  // Calculate minute of day (0-1439)
  _currentSampleIndex = timeinfo.tm_hour * 60 + timeinfo.tm_min;

  Serial.printf("[PowerHistory] Day index: %d, Sample index: %d\n",
                _currentDayIndex, _currentSampleIndex);

  // Initialize tracking for flushing
  // CRITICAL: Set to current + 1 to avoid re-flushing pre-loaded data
  _lastFlushedSample = _currentSampleIndex + 1;

  // Try to load existing history from SD card
  loadFromSD();
}

void PowerHistory::addSample(uint8_t batteryPct, uint16_t inputW,
                             uint16_t outputW) {
  time_t now = time(nullptr);
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);

  // Calculate day index (0-6)
  uint8_t dayIndex = timeinfo.tm_wday;

  // If we just rolled over to a new day (e.g. was Tuesday, now Wednesday)
  if (dayIndex != _currentDayIndex) {
    advanceToNextDay();
    _currentDayIndex = dayIndex;
  }

  // Calculate minute of day (0-1439)
  uint16_t minuteIndex = timeinfo.tm_hour * 60 + timeinfo.tm_min;

  // Safety check
  if (minuteIndex >= SAMPLES_PER_DAY)
    return;

  _currentSampleIndex = minuteIndex;

  // Store sample
  PowerSample &sample = _historyData[_currentDayIndex][_currentSampleIndex];
  sample.timestamp = now;
  sample.batteryPct = batteryPct;
  sample.inputW = inputW;
  sample.outputW = outputW;

  // Track max index for flushing efficiency (optional, but good)
  // We don't advance _currentSampleIndex blindly anymore
}

PowerSample PowerHistory::getSample(uint8_t dayOffset, uint16_t sampleIndex) {
  // dayOffset: 0 = today, 1 = yesterday, etc.
  uint8_t dayIndex =
      (_currentDayIndex - dayOffset + HISTORY_DAYS) % HISTORY_DAYS;

  if (sampleIndex >= SAMPLES_PER_DAY) {
    return PowerSample{0, 0, 0, 0};
  }

  return _historyData[dayIndex][sampleIndex];
}

const PowerSample *PowerHistory::getDaySamples(uint8_t dayOffset) {
  uint8_t dayIndex =
      (_currentDayIndex - dayOffset + HISTORY_DAYS) % HISTORY_DAYS;
  return _historyData[dayIndex];
}

uint16_t PowerHistory::getSampleCount(uint8_t dayOffset) {
  // For today (dayOffset=0), return actual count
  // For past days, return full day (1440) or count non-zero timestamps
  if (dayOffset == 0) {
    return _currentSampleIndex;
  }

  // For past days, count valid samples
  uint8_t dayIndex =
      (_currentDayIndex - dayOffset + HISTORY_DAYS) % HISTORY_DAYS;
  uint16_t count = 0;
  for (uint16_t i = 0; i < SAMPLES_PER_DAY; i++) {
    if (_historyData[dayIndex][i].timestamp > 0) {
      count++;
    }
  }
  return count;
}

bool PowerHistory::shouldFlush() {
  // Flush every 5 minutes
  time_t now = time(nullptr);
  return (now - _lastFlushTime) >= (FLUSH_INTERVAL_MINS * 60);
}

bool PowerHistory::flushToSD() {
  Serial.println("[PowerHistory] Flushing to SD...");

  // CRITICAL: Power cycle SD card before write to ensure reliability (The
  // "Reset Trick")
  if (sdManager) {
    if (!sdManager->powerCycleAndReinit()) {
      Serial.println("[PowerHistory] SD Reset Failed! Aborting flush.");
      return false;
    }
  } else {
    Serial.println("[PowerHistory] Warning: sdManager is NULL");
  }

  // Create /history directory if it doesn't exist
  if (!SD.exists("/history")) {
    SD.mkdir("/history");
  }

  // Get filename for today
  String filename = getFilenameForDay(0);

  // Open file in append mode
  File file = SD.open(filename, FILE_APPEND);
  if (!file) {
    Serial.printf("[PowerHistory] Failed to open %s\n", filename.c_str());
    return false;
  }

  // If file is new, write CSV header
  if (file.size() == 0) {
    file.println("timestamp,battery,input,output");
  }

  // Write all samples since last flush
  // CRITICAL: Write UP TO and INCLUDING _currentSampleIndex
  // Because _currentSampleIndex is now the time index we just wrote to
  uint16_t flushEndIndex = _currentSampleIndex + 1;

  if (flushEndIndex > SAMPLES_PER_DAY)
    flushEndIndex = SAMPLES_PER_DAY;

  if (flushEndIndex > _lastFlushedSample) {
    uint16_t samplesToWrite = flushEndIndex - _lastFlushedSample;
    for (uint16_t i = 0; i < samplesToWrite; i++) {
      uint16_t sampleIdx = _lastFlushedSample + i;
      if (sampleIdx < SAMPLES_PER_DAY) {
        const PowerSample &sample = _historyData[_currentDayIndex][sampleIdx];
        if (sample.timestamp > 0) { // Only write valid samples
          writeSampleToCSV(file, sample);
        }
      }
    }

    Serial.printf("[PowerHistory] Flushed %d slots (%d to %d) to %s\n",
                  samplesToWrite, _lastFlushedSample, flushEndIndex - 1,
                  filename.c_str());
  }

  file.close();

  // Update flush tracking
  _lastFlushTime = time(nullptr);
  _lastFlushedSample = flushEndIndex;

  return true;
}

bool PowerHistory::loadFromSD() {
  Serial.println("[PowerHistory] Loading history from SD...");

  if (!SD.exists("/history")) {
    Serial.println("[PowerHistory] No history directory found");
    return false;
  }

  // Load last 7 days
  int samplesLoaded = 0;
  for (uint8_t dayOffset = 0; dayOffset < HISTORY_DAYS; dayOffset++) {
    String filename = getFilenameForDay(dayOffset);

    if (!SD.exists(filename)) {
      continue;
    }

    File file = SD.open(filename, FILE_READ);
    if (!file) {
      continue;
    }

    // Skip CSV header
    file.readStringUntil('\n');

    // Read samples
    uint8_t dayIndex =
        (_currentDayIndex - dayOffset + HISTORY_DAYS) % HISTORY_DAYS;
    uint16_t sampleIdx = 0;

    while (file.available() && sampleIdx < SAMPLES_PER_DAY) {
      String line = file.readStringUntil('\n');
      if (line.length() == 0)
        break;

      // Parse CSV: timestamp,battery,input,output
      int comma1 = line.indexOf(',');
      int comma2 = line.indexOf(',', comma1 + 1);
      int comma3 = line.indexOf(',', comma2 + 1);

      if (comma1 > 0 && comma2 > 0 && comma3 > 0) {
        PowerSample &sample = _historyData[dayIndex][sampleIdx];
        sample.timestamp = line.substring(0, comma1).toInt();
        sample.batteryPct = line.substring(comma1 + 1, comma2).toInt();
        sample.inputW = line.substring(comma2 + 1, comma3).toInt();
        sample.outputW = line.substring(comma3 + 1).toInt();
        sampleIdx++;
        samplesLoaded++;
      }
    }

    file.close();
  }

  Serial.printf("[PowerHistory] Loaded %d samples from SD\n", samplesLoaded);
  return samplesLoaded > 0;
}

void PowerHistory::advanceToNextDay() {
  Serial.println("[PowerHistory] Advancing to next day");

  // Flush any remaining samples from TODAY before switching
  // Force flush by artificially extending the range if needed
  _currentSampleIndex = SAMPLES_PER_DAY - 1;
  flushToSD();

  // Reset tracking for new day (will be set in addSample or init)
  _lastFlushedSample = 0;

  // Note: Actual _currentDayIndex update happens in addSample now
  // to ensure sync with time()
}

String PowerHistory::getFilenameForDay(uint8_t dayOffset) {
  // Calculate date for the day
  time_t now = time(nullptr);
  time_t targetDay = now - (dayOffset * 86400); // Subtract days in seconds

  struct tm timeinfo;
  localtime_r(&targetDay, &timeinfo);

  char filename[32];
  snprintf(filename, sizeof(filename), "/history/%04d-%02d-%02d.csv",
           timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);

  return String(filename);
}

bool PowerHistory::writeSampleToCSV(fs::File &file, const PowerSample &sample) {
  char line[64];
  snprintf(line, sizeof(line), "%u,%u,%u,%u\n", sample.timestamp,
           sample.batteryPct, sample.inputW, sample.outputW);
  file.print(line);
  return true;
}
