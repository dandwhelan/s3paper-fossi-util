/**
 * Display Hardware Abstraction
 * 
 * M5Paper S3 uses M5Unified/M5GFX for display control.
 * This header provides any additional display utilities.
 */

#ifndef DISPLAY_H
#define DISPLAY_H

#include <M5Unified.h>

namespace Display {

// Display dimensions
static const int WIDTH = 960;
static const int HEIGHT = 540;
static const int PPI = 235;

/**
 * Initialize display with optimal settings for eInk
 */
inline void init() {
    M5.Display.setRotation(1);  // Landscape
    M5.Display.setColorDepth(16);
    M5.Display.setBrightness(128);
}

/**
 * Clear screen to white
 */
inline void clear() {
    M5.Display.fillScreen(TFT_WHITE);
}

/**
 * Perform full refresh (clear ghosting)
 */
inline void fullRefresh() {
    M5.Display.display();
}

/**
 * Set text properties
 */
inline void setTextStyle(int size, uint16_t color = TFT_BLACK) {
    M5.Display.setTextSize(size);
    M5.Display.setTextColor(color);
}

} // namespace Display

#endif // DISPLAY_H
