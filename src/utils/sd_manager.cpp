/**
 * SD Card Manager Implementation
 */

#include "sd_manager.h"
#include <M5Unified.h>
#include <SPI.h>

SDManager::SDManager() : _available(false) {}

SDManager::~SDManager() {
  if (_available) {
    SD.end();
  }
}

bool SDManager::init() {
  // Hardcoded pins from user verification
  int8_t sck = 39;
  int8_t miso = 40;
  int8_t mosi = 38;
  int8_t cs = 47;

  Serial.printf("SD SPI Pins: SCK=%d, MISO=%d, MOSI=%d, CS=%d\n", sck, miso,
                mosi, cs);

  // If pins are invalid, try standard ESP32/S3 defaults or simple fallback
  if (sck == -1 || miso == -1 || mosi == -1 || cs == -1) {
    Serial.println(
        "SD: M5Unified did not report valid pins, trying defaults (VSPI)...");
    // Fallback for standard VSPI if M5Unified doesn't know the board
    // On S3 we might need to be specific if we are not using standard pins
  } else {
    // Initialize SPI with specific pins
    SPI.begin(sck, miso, mosi, cs);
  }

  // Try to mount
  // Note: SD.begin(CS, SPI, FREQ)
  if (!SD.begin(cs != -1 ? cs : SS, SPI, 25000000)) {
    Serial.println("SD Card: Mount failed");
    _available = false;
    return false;
  }

  uint8_t cardType = SD.cardType();
  if (cardType == CARD_NONE) {
    Serial.println("SD Card: No card attached");
    _available = false;
    return false;
  }

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
