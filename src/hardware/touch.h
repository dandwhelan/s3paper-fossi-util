/**
 * Touch Hardware Abstraction
 * 
 * M5Paper S3 uses GT911 capacitive touch via M5Unified.
 */

#ifndef TOUCH_H
#define TOUCH_H

#include <M5Unified.h>

namespace Touch {

/**
 * Update touch state (call in main loop)
 */
inline void update() {
    M5.update();
}

/**
 * Check if screen was touched
 */
inline bool wasPressed() {
    return M5.Touch.getDetail().wasPressed();
}

/**
 * Check if touch was released
 */
inline bool wasReleased() {
    return M5.Touch.getDetail().wasReleased();
}

/**
 * Check if currently touching
 */
inline bool isPressed() {
    return M5.Touch.getDetail().isPressed();
}

/**
 * Get touch X coordinate
 */
inline int getX() {
    return M5.Touch.getDetail().x;
}

/**
 * Get touch Y coordinate
 */
inline int getY() {
    return M5.Touch.getDetail().y;
}

/**
 * Detect swipe gesture
 * @param startX Start X position
 * @param startY Start Y position
 * @param endX End X position
 * @param endY End Y position
 * @return 0=none, 1=left, 2=right, 3=up, 4=down
 */
inline int detectSwipe(int startX, int startY, int endX, int endY, int threshold = 50) {
    int dx = endX - startX;
    int dy = endY - startY;
    
    if (abs(dx) > abs(dy)) {
        // Horizontal swipe
        if (dx > threshold) return 2;  // Right
        if (dx < -threshold) return 1; // Left
    } else {
        // Vertical swipe
        if (dy > threshold) return 4;  // Down
        if (dy < -threshold) return 3; // Up
    }
    
    return 0;  // No swipe
}

} // namespace Touch

#endif // TOUCH_H
