/**
 * Reader Fonts Registry
 *
 * Font family selection for EPUB reader (does not affect system UI).
 * Uses M5GFX built-in fonts as placeholders - can be replaced with
 * actual Bookerly/NotoSans/OpenDyslexic fonts converted from TTF.
 *
 * To convert custom TTF fonts:
 * 1. Use Adafruit's fontconvert tool
 * 2. Generate headers (e.g., Bookerly24.h, Bookerly40.h)
 * 3. Include them here and update READER_FONTS arrays
 */

#ifndef READER_FONTS_H
#define READER_FONTS_H

#include <M5Unified.h>

// Font family enum for reader settings
enum class ReaderFontFamily {
  SERIF = 0,       // Bookerly or similar serif font
  SANS_SERIF = 1,  // Noto Sans or similar sans-serif
  DYSLEXIC = 2     // OpenDyslexic or accessibility-friendly font
};

// Number of font families available
constexpr int READER_FONT_FAMILY_COUNT = 3;

// Font family names (for UI display)
constexpr const char* READER_FONT_NAMES[] = {
  "Serif",     // Index 0
  "Sans",      // Index 1
  "Dyslexic"   // Index 2
};

// Medium size fonts (24pt equivalent) - one per family
// Using M5GFX built-in fonts as placeholders:
// - FreeSerif for Bookerly (serif)
// - FreeSans for Noto Sans (sans-serif)
// - FreeSansBold for OpenDyslexic (bold is more readable - placeholder until real font added)
//
// To add real OpenDyslexic: Download TTF from https://opendyslexic.org/
// Convert with: fontconvert OpenDyslexic-Regular.ttf 18 > OpenDyslexic18.h
inline const m5gfx::IFont* getReaderFontMedium(int family) {
  // Validate family range
  if (family < 0 || family >= READER_FONT_FAMILY_COUNT) {
    family = 0;
  }
  switch (family) {
    case 0:  // SERIF (Bookerly placeholder)
      return &fonts::FreeSerif18pt7b;
    case 1:  // SANS_SERIF (Noto Sans placeholder)
      return &fonts::FreeSans18pt7b;
    case 2:  // DYSLEXIC (OpenDyslexic placeholder - bold sans is more readable)
      return &fonts::FreeSansBold18pt7b;
    default:
      return &fonts::FreeSerif18pt7b;
  }
}

// Large size fonts (40pt equivalent) - one per family
inline const m5gfx::IFont* getReaderFontLarge(int family) {
  // Validate family range
  if (family < 0 || family >= READER_FONT_FAMILY_COUNT) {
    family = 0;
  }
  switch (family) {
    case 0:  // SERIF (Bookerly placeholder)
      return &fonts::FreeSerif24pt7b;
    case 1:  // SANS_SERIF (Noto Sans placeholder)
      return &fonts::FreeSans24pt7b;
    case 2:  // DYSLEXIC (OpenDyslexic placeholder - bold sans is more readable)
      return &fonts::FreeSansBold24pt7b;
    default:
      return &fonts::FreeSerif24pt7b;
  }
}

#endif // READER_FONTS_H
