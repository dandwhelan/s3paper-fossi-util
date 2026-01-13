/**
 * UI Manager - Notes Extended Functions
 * File browsing and navigation for timestamped note files
 */

#include "../utils/sd_manager.h"
#include "ui_manager.h"
#include <FS.h>
#include <SD.h>

// ============================================================================
// Notes - File Browsing Functions
// ============================================================================

// Scan /notes/ directory for available note files
void UIManager::notesScanFiles() {
  _noteFileList.clear();
  _noteFileIndex = -1;

  if (!SD.exists("/notes")) {
    Serial.println("Notes: /notes directory does not exist");
    return;
  }

  File root = SD.open("/notes");
  if (!root || !root.isDirectory()) {
    Serial.println("Notes: Failed to open /notes directory");
    return;
  }

  // Scan all .bin files
  File file = root.openNextFile();
  while (file) {
    if (!file.isDirectory()) {
      String name = String(file.name());
      // Filter for .bin files
      if (name.endsWith(".bin")) {
        _noteFileList.push_back(name);
        Serial.printf("Found note: %s\n", name.c_str());
      }
    }
    file = root.openNextFile();
  }
  root.close();

  // Sort files descending (newest first) - simple bubble sort
  for (size_t i = 0; i < _noteFileList.size(); i++) {
    for (size_t j = i + 1; j < _noteFileList.size(); j++) {
      if (_noteFileList[i] < _noteFileList[j]) {
        String temp = _noteFileList[i];
        _noteFileList[i] = _noteFileList[j];
        _noteFileList[j] = temp;
      }
    }
  }

  Serial.printf("Found %d note files\n", _noteFileList.size());

  // Set index to current file or most recent
  if (_currentNoteFile.length() > 0) {
    for (size_t i = 0; i < _noteFileList.size(); i++) {
      if (_noteFileList[i] == _currentNoteFile ||
          _noteFileList[i].endsWith(_currentNoteFile)) {
        _noteFileIndex = i;
        Serial.printf("Current note index: %d\n", _noteFileIndex);
        break;
      }
    }
  }

  // Default to most recent if not found
  if (_noteFileIndex == -1 && !_noteFileList.empty()) {
    _noteFileIndex = 0;
    _currentNoteFile = _noteFileList[0];
  }
}

// Load note file at current index
void UIManager::notesLoadByIndex() {
  if (_noteFileIndex < 0 || _noteFileIndex >= (int)_noteFileList.size()) {
    Serial.println("Notes: Invalid file index");
    return;
  }

  if (!_notesCanvas) {
    Serial.println("Notes: Canvas not initialized");
    return;
  }

  String filename = _noteFileList[_noteFileIndex];
  _currentNoteFile = filename;

  Serial.printf("\n=== NOTES LOAD START ===\n");
  Serial.printf("Loading note: %s (%d/%d)\n", filename.c_str(),
                _noteFileIndex + 1, _noteFileList.size());

  // Show loading status
  M5.Display.setEpdMode(epd_mode_t::epd_fast);
  M5.Display.fillRect(SCREEN_WIDTH / 2 - 100, SCREEN_HEIGHT / 2 - 30, 200, 60,
                      0x0000);     // COLOR_BLACK
  M5.Display.setTextColor(0xFFFF); // COLOR_WHITE
  M5.Display.setTextSize(2);
  M5.Display.setCursor(SCREEN_WIDTH / 2 - 60, SCREEN_HEIGHT / 2 - 10);
  M5.Display.print("Loading...");
  M5.Display.display();

  // === POWER CYCLE SD CARD ===
  Serial.println("Stopping display for SD operations...");
  M5.Display.waitDisplay();
  M5.Display.endWrite();

  // Use global SDManager if available
  extern SDManager *sdManager;
  bool sdOk = false;
  if (sdManager) {
    sdOk = sdManager->powerCycleAndReinit();
  } else {
    Serial.println("No SDManager, using direct power cycle...");
    SD.end();
    M5.Power.setExtOutput(false);
    delay(500);
    M5.Power.setExtOutput(true);
    delay(500);
    sdOk = SD.begin(47);
  }

  if (!sdOk) {
    Serial.println("ERROR: SD power cycle failed!");
    M5.Display.fillRect(SCREEN_WIDTH / 2 - 100, SCREEN_HEIGHT / 2 - 30, 200, 60,
                        0xFFFF);     // COLOR_WHITE
    M5.Display.setTextColor(0x0000); // COLOR_BLACK
    M5.Display.setCursor(SCREEN_WIDTH / 2 - 60, SCREEN_HEIGHT / 2 - 10);
    M5.Display.print("SD Failed!");
    M5.Display.display();
    delay(2000);
    _needsRefresh = true;
    _lastRefresh = 0;
    Serial.println("=== NOTES LOAD END (FAILED) ===\n");
    return;
  }

  // Update status
  M5.Display.fillRect(SCREEN_WIDTH / 2 - 100, SCREEN_HEIGHT / 2 - 30, 200, 60,
                      0x0000);     // COLOR_BLACK
  M5.Display.setTextColor(0xFFFF); // COLOR_WHITE
  M5.Display.setCursor(SCREEN_WIDTH / 2 - 40, SCREEN_HEIGHT / 2 - 10);
  M5.Display.print("Loading...");
  M5.Display.display();

  Serial.printf("Opening file for read: /notes/%s\n", filename.c_str());
  String fullPath = "/notes/" + filename;
  File file = SD.open(fullPath, FILE_READ);
  if (file) {
    char header[7];
    file.read((uint8_t *)header, 6);
    header[6] = 0;

    Serial.printf("File header: %s\n", header);

    if (strcmp(header, "M5NOTE") == 0) {
      uint16_t w, h;
      uint8_t d;
      file.read((uint8_t *)&w, 2);
      file.read((uint8_t *)&h, 2);
      file.read(&d, 1);

      Serial.printf("File dimensions: %dx%d, depth=%d\n", w, h, d);
      Serial.printf("Canvas dimensions: %dx%d, depth=%d\n",
                    _notesCanvas->width(), _notesCanvas->height(),
                    _notesCanvas->getColorDepth());

      // Canvas depth: Expected 4-bit grayscale (getColorDepth may be corrupted
      // after power cycle)
      const uint8_t EXPECTED_DEPTH = 4;

      if (w == _notesCanvas->width() && h == _notesCanvas->height() &&
          d == EXPECTED_DEPTH) {
        size_t len = (w * h * d) / 8;
        if (d < 8 && (w * h * d) % 8 != 0)
          len++;

        Serial.printf("Reading %d bytes of pixel data...\n", len);
        size_t bytesRead = file.read((uint8_t *)_notesCanvas->getBuffer(), len);
        Serial.printf("Read complete: %d/%d bytes\n", bytesRead, len);
        if (bytesRead == len) {
          Serial.println("Loaded note successfully!");
        } else {
          Serial.println("WARNING: Incomplete read");
        }
      } else {
        Serial.printf("ERROR: Dimension mismatch (expected: %dx%d depth=%d)\n",
                      _notesCanvas->width(), _notesCanvas->height(),
                      EXPECTED_DEPTH);
      }
    } else {
      Serial.println("ERROR: Invalid file header");
    }
    file.close();
  } else {
    Serial.printf("ERROR: Failed to open %s\n", filename.c_str());
  }

  // Restore UI
  M5.Display.setEpdMode(epd_mode_t::epd_fastest);
  _needsRefresh = true;
  _lastRefresh = 0;
  Serial.println("=== NOTES LOAD END ===\n");
}

// Navigate to previous (newer) note
void UIManager::notesPrevFile() {
  if (_noteFileList.empty()) {
    Serial.println("Notes: No files available");
    return;
  }

  _noteFileIndex--;
  if (_noteFileIndex < 0) {
    _noteFileIndex = _noteFileList.size() - 1; // Wrap to last
  }

  Serial.printf("Navigating to previous note: %d\n", _noteFileIndex);
  notesLoadByIndex();
}

// Navigate to next (older) note
void UIManager::notesNextFile() {
  if (_noteFileList.empty()) {
    Serial.println("Notes: No files available");
    return;
  }

  _noteFileIndex++;
  if (_noteFileIndex >= (int)_noteFileList.size()) {
    _noteFileIndex = 0; // Wrap to first
  }

  Serial.printf("Navigating to next note: %d\n", _noteFileIndex);
  notesLoadByIndex();
}
