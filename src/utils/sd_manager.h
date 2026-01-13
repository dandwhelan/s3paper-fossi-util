/**
 * SD Card Manager
 *
 * Handles SD card initialization and common file operations.
 */

#ifndef SD_MANAGER_H
#define SD_MANAGER_H

#include <Arduino.h>
#undef min
#undef max
#include <FS.h>
#include <SD.h>
#include <vector>

class SDManager {
public:
  SDManager();
  ~SDManager();

  /**
   * Initialize SD card
   * @return true if successful
   */
  bool init();

  /**
   * Power cycle the SD card and reinitialize
   * Use before critical read/write operations to avoid SPI conflicts
   * @return true if reinitialization successful
   */
  bool powerCycleAndReinit();

  /**
   * Check if SD card is available
   */
  bool isAvailable() const { return _available; }

  /**
   * Ensure a directory exists (creates if needed)
   * @param path Directory path
   * @return true if directory exists or was created
   */
  bool ensureDirectory(const char *path);

  /**
   * Read entire file content
   * @param path File path
   * @return File content as String (empty if failed)
   */
  String readFile(const char *path);

  /**
   * Write content to file
   * @param path File path
   * @param content Content to write
   * @return true if successful
   */
  bool writeFile(const char *path, const String &content);

  /**
   * Append content to file
   * @param path File path
   * @param content Content to append
   * @return true if successful
   */
  bool appendFile(const char *path, const String &content);

  /**
   * Check if file exists
   * @param path File path
   */
  bool fileExists(const char *path);

  /**
   * Delete a file
   * @param path File path
   * @return true if successful
  /**
   * Delete a file
   * @param path File path
   * @return true if successful
   */
  bool deleteFile(const char *path);

  // --- Diagnostics ---

  struct SDCardInfo {
    uint64_t totalBytes;
    uint64_t usedBytes;
    String type; // SD, SDHC, SDXC, etc.
  };

  /**
   * Get SD Card Information
   */
  SDCardInfo getCardInfo();

  /**
   * Run Read/Write Benchmark
   * @param writeSpeedMBps Output write speed
   * @param readSpeedMBps Output read speed
   * @return true if benchmark completed successfully
   */
  bool runBenchmark(float &writeSpeedMBps, float &readSpeedMBps);

  /**
   * List files in directory
   * @param dirPath Directory path
   * @param files Output vector of file names
   * @param extensions Optional filter by extensions (comma separated, e.g.
   * "epub,txt")
   * @return Number of files found
   */
  int listFiles(const char *dirPath, std::vector<String> &files,
                const char *extensions = nullptr);

private:
  bool _available;

  bool matchesExtension(const String &filename, const char *extensions);
};

#endif // SD_MANAGER_H
