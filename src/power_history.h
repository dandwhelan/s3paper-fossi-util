#ifndef POWER_HISTORY_H
#define POWER_HISTORY_H

#include <Arduino.h>
#include <time.h>

// Forward declaration for SD File class
namespace fs {
class File;
}

// Power sample structure (9 bytes)
struct PowerSample {
  uint32_t timestamp; // Unix time (4 bytes)
  uint8_t batteryPct; // 0-100 (1 byte)
  uint16_t inputW;    // 0-2000W (2 bytes)
  uint16_t outputW;   // 0-2000W (2 bytes)
};

// History buffer configuration
#define SAMPLES_PER_DAY 1440  // 1 minute intervals = 1440 samples/day
#define HISTORY_DAYS 7        // Keep 7 days of history
#define FLUSH_INTERVAL_MINS 5 // Flush to SD every 5 minutes

class PowerHistory {
public:
  PowerHistory();

  // Initialize history system
  void init();

  // Add a new sample (called every minute)
  void addSample(uint8_t batteryPct, uint16_t inputW, uint16_t outputW);

  // Get sample for specific day and index (not minute!)
  PowerSample getSample(uint8_t dayOffset, uint16_t sampleIndex);

  // Get samples for a specific day (returns pointer to day array)
  const PowerSample *getDaySamples(uint8_t dayOffset);

  // Get number of samples for a specific day
  uint16_t getSampleCount(uint8_t dayOffset);

  // Should we flush to SD? (every 5 minutes)
  bool shouldFlush();

  // Flush pending samples to SD card
  bool flushToSD();

  // Load history from SD card on boot
  bool loadFromSD();

  // Get current day index (0-6, circular)
  uint8_t getCurrentDayIndex() const { return _currentDayIndex; }

  // Get total samples collected today
  uint16_t getTodaySampleCount() const { return _currentSampleIndex; }

private:
  // 7 days × 1440 samples = 91 KB RAM buffer
  PowerSample _historyData[HISTORY_DAYS][SAMPLES_PER_DAY];

  // Current position in circular buffer
  uint8_t _currentDayIndex;     // 0-6 (today)
  uint16_t _currentSampleIndex; // 0-1439 (minute of day)

  // Last flush timestamp
  uint32_t _lastFlushTime;
  uint16_t _lastFlushedSample; // Last sample index flushed to SD

  // Helper functions
  void advanceToNextDay();
  String getFilenameForDay(uint8_t dayOffset);
  bool writeSampleToCSV(fs::File &file, const PowerSample &sample);
};

#endif // POWER_HISTORY_H
