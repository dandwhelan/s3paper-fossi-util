/**
 * Buzzer Hardware Abstraction
 * 
 * M5Paper S3 has a passive buzzer.
 */

#ifndef BUZZER_H
#define BUZZER_H

#include <Arduino.h>

namespace Buzzer {

// Buzzer pin (check M5Paper S3 schematic for actual pin)
static const int PIN = 2;

/**
 * Initialize buzzer
 */
inline void init() {
    pinMode(PIN, OUTPUT);
    digitalWrite(PIN, LOW);
}

/**
 * Play a beep
 * @param frequency Frequency in Hz
 * @param duration Duration in ms
 */
inline void beep(int frequency = 1000, int duration = 100) {
    tone(PIN, frequency, duration);
}

/**
 * Stop buzzer
 */
inline void stop() {
    noTone(PIN);
}

/**
 * Play success sound
 */
inline void success() {
    beep(1000, 50);
    delay(60);
    beep(1500, 100);
}

/**
 * Play error sound
 */
inline void error() {
    beep(300, 200);
}

/**
 * Play alarm sound
 * @param count Number of beeps
 */
inline void alarm(int count = 3) {
    for (int i = 0; i < count; i++) {
        beep(2000, 200);
        delay(200);
    }
}

/**
 * Play click sound (for button feedback)
 */
inline void click() {
    beep(800, 10);
}

} // namespace Buzzer

#endif // BUZZER_H
