/**
 * SD Card Manager Implementation
 */

#include "sd_manager.h"
#include <Arduino.h>
#include <M5Unified.h>
#include <SPI.h>
#include <vector>

SDManager::SDManager() : _available(false) {}

SDManager::~SDManager() {
  if (_available) {
    SD.end();
  }
}

// Create dedicated SPI instance for SD card (FSPI - recommended for SD on S3)
static SPIClass sdSPI(FSPI);

bool SDManager::init() {
  // Hardcoded pins from M5Paper S3 documentation
  const int8_t SD_SCK = 39;
  const int8_t SD_MISO = 40;
  const int8_t SD_MOSI = 38;
  const int8_t SD_CS = 47;

  Serial.printf("SD SPI Pins: SCK=%d, MISO=%d, MOSI=%d, CS=%d\n", SD_SCK,
                SD_MISO, SD_MOSI, SD_CS);

  // End any existing SPI to release pins
  sdSPI.end();
  delay(50);

  // Configure CS pin - must be HIGH before SPI init
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
  delay(100);

  // Send 80 dummy clocks with CS high to put card in SPI mode
  // This is required per SD card spec
  sdSPI.begin(SD_SCK, SD_MISO, SD_MOSI, -1);
  sdSPI.beginTransaction(SPISettings(400000, MSBFIRST, SPI_MODE0));
  for (int i = 0; i < 10; i++) {
    sdSPI.transfer(0xFF);
  }
  sdSPI.endTransaction();
  delay(10);

  // Now try SD.begin with the SPI bus

  // Try multiple frequencies, starting low
  uint32_t frequencies[] = {1000000, 4000000, 10000000}; // 1MHz, 4MHz, 10MHz

  for (int i = 0; i < 3; i++) {
    Serial.printf("SD: Trying frequency %lu Hz...\n", frequencies[i]);

    if (SD.begin(SD_CS, sdSPI, frequencies[i])) {
      uint8_t cardType = SD.cardType();
      if (cardType != CARD_NONE) {
        Serial.printf("SD Card: Mounted at %lu Hz!\n", frequencies[i]);

        Serial.print("SD Card Type: ");
        if (cardType == CARD_MMC) {
          Serial.println("MMC");
        } else if (cardType == CARD_SD) {
          Serial.println("SDSC");
        } else if (cardType == CARD_SDHC) {
          Serial.println("SDHC");
        } else {
          Serial.println("UNKNOWN");
        }

        uint64_t cardSize = SD.cardSize() / (1024 * 1024);
        Serial.printf("SD Card Size: %lluMB\n", cardSize);

        _available = true;
        return true;
      }
    }

    SD.end(); // Clean up before retry
    delay(100);
  }

  Serial.println("SD Card: Mount failed at all frequencies");
  _available = false;
  return false;
}

bool SDManager::powerCycleAndReinit() {
  // Hardcoded pins from M5Paper S3 documentation
  const int8_t SD_SCK = 39;
  const int8_t SD_MISO = 40;
  const int8_t SD_MOSI = 38;
  const int8_t SD_CS = 47;

  Serial.println("=== SD Power Cycle START ===");

  // Step 1: Unmount SD card
  Serial.println("[1/6] Unmounting SD card...");
  SD.end();
  _available = false;
  delay(50);

  // Step 2: Cut power to SD card (and other peripherals)
  Serial.println("[2/6] Cutting external power (setExtOutput=false)...");
  M5.Power.setExtOutput(false);
  delay(300); // Hold power off for 300ms (reduced from 500ms)

  // Step 3: Restore power
  Serial.println("[3/6] Restoring external power (setExtOutput=true)...");
  M5.Power.setExtOutput(true);
  delay(300); // Wait for SD card to stabilize (reduced from 500ms)

  // Step 4: Reset SPI bus
  Serial.println("[4/6] Resetting SPI bus...");
  sdSPI.end();
  delay(50);

  // Configure CS pin
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
  delay(100);

  // Send dummy clocks to put card in SPI mode
  sdSPI.begin(SD_SCK, SD_MISO, SD_MOSI, -1);
  sdSPI.beginTransaction(SPISettings(400000, MSBFIRST, SPI_MODE0));
  for (int i = 0; i < 10; i++) {
    sdSPI.transfer(0xFF);
  }
  sdSPI.endTransaction();
  delay(10);

  // Step 5: Reinitialize SD card
  Serial.println("[5/6] Reinitializing SD card...");
  uint32_t frequencies[] = {1000000, 4000000, 10000000};

  for (int i = 0; i < 3; i++) {
    Serial.printf("  Trying frequency %lu Hz...\n", frequencies[i]);

    if (SD.begin(SD_CS, sdSPI, frequencies[i])) {
      uint8_t cardType = SD.cardType();
      if (cardType != CARD_NONE) {
        Serial.printf("[6/6] SUCCESS! SD mounted at %lu Hz\n", frequencies[i]);
        Serial.printf("  Card Type: %s\n", cardType == CARD_MMC    ? "MMC"
                                           : cardType == CARD_SD   ? "SDSC"
                                           : cardType == CARD_SDHC ? "SDHC"
                                                                   : "UNKNOWN");
        Serial.printf("  Card Size: %llu MB\n", SD.cardSize() / (1024 * 1024));
        Serial.println("=== SD Power Cycle END (SUCCESS) ===");
        _available = true;
        return true;
      }
    }

    SD.end();
    delay(50);
  }

  Serial.println("[6/6] FAILED - SD mount failed at all frequencies");
  Serial.println("=== SD Power Cycle END (FAILED) ===");
  _available = false;
  return false;
}

bool SDManager::ensureDirectory(const char *path) {
  if (!_available)
    return false;

  if (SD.exists(path)) {
    return true;
  }

  return SD.mkdir(path);
}

String SDManager::readFile(const char *path) {
  if (!_available)
    return "";

  File file = SD.open(path, FILE_READ);
  if (!file) {
    Serial.printf("Failed to open file: %s\n", path);
    return "";
  }

  String content = file.readString();
  file.close();
  return content;
}

bool SDManager::writeFile(const char *path, const String &content) {
  if (!_available)
    return false;

  File file = SD.open(path, FILE_WRITE);
  if (!file) {
    Serial.printf("Failed to open file for writing: %s\n", path);
    return false;
  }

  size_t written = file.print(content);
  file.close();

  return written == content.length();
}

bool SDManager::appendFile(const char *path, const String &content) {
  if (!_available)
    return false;

  File file = SD.open(path, FILE_APPEND);
  if (!file) {
    Serial.printf("Failed to open file for appending: %s\n", path);
    return false;
  }

  size_t written = file.print(content);
  file.close();

  return written == content.length();
}

bool SDManager::fileExists(const char *path) {
  if (!_available)
    return false;
  return SD.exists(path);
}

bool SDManager::deleteFile(const char *path) {
  if (!_available)
    return false;
  return SD.remove(path);
}

int64_t SDManager::getFileSize(const char *path) {
  if (!_available)
    return -1;

  File file = SD.open(path, FILE_READ);
  if (!file) {
    return -1;
  }

  int64_t size = file.size();
  file.close();
  return size;
}

uint64_t SDManager::getTotalBytes() {
  if (!_available)
    return 0;
  return SD.totalBytes();
}

uint64_t SDManager::getUsedBytes() {
  if (!_available)
    return 0;
  return SD.usedBytes();
}

int SDManager::listFiles(const char *dirPath, std::vector<String> &files,
                         const char *extensions) {
  files.clear();
  if (!_available)
    return 0;

  File root = SD.open(dirPath);
  if (!root || !root.isDirectory()) {
    Serial.printf("Failed to open directory: %s\n", dirPath);
    return 0;
  }

  File file = root.openNextFile();
  while (file) {
    if (!file.isDirectory()) {
      String name = file.name();
      if (extensions == nullptr || matchesExtension(name, extensions)) {
        files.push_back(name);
      }
    }
    file = root.openNextFile();
  }

  root.close();
  return files.size();
}

bool SDManager::matchesExtension(const String &filename,
                                 const char *extensions) {
  if (extensions == nullptr || strlen(extensions) == 0) {
    return true;
  }

  int dotIndex = filename.lastIndexOf('.');
  if (dotIndex < 0) {
    return false;
  }

  String ext = filename.substring(dotIndex + 1);
  ext.toLowerCase();

  String extList = extensions;
  extList.toLowerCase();

  int start = 0;
  while (start < (int)extList.length()) {
    int comma = extList.indexOf(',', start);
    if (comma < 0)
      comma = extList.length();

    String check = extList.substring(start, comma);
    check.trim();

    if (ext == check) {
      return true;
    }

    start = comma + 1;
  }

  return false;
}
