/**
 * Buzzer Hardware Abstraction
 *
 * M5Paper S3 has a passive buzzer.
 */

#ifndef BUZZER_H
#define BUZZER_H

#include <Arduino.h>

namespace Buzzer {

// Buzzer pin (GPIO 21 per M5Paper S3 schematic)
static const int PIN = 21;

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
inline void stop() { noTone(PIN); }

/**
 * Play success sound
 */
inline void success() {
  // Classic 8-bit victory climb
  beep(1319, 120); // E6
  delay(20);
  beep(1568, 120); // G6
  delay(20);
  beep(1760, 120); // A6
  delay(20);
  beep(2093, 180); // C7
  delay(40);

  // Signature Mario-style ending
  beep(2637, 250); // E7
  delay(30);
  beep(2093, 400); // C7 (final resolve)
}

/**
 * Play error sound
 */
inline void error() { beep(300, 200); }

/**
 * Play alarm sound (Mario-style victory tune)
 */
inline void alarm(int count = 1) {
  for (int i = 0; i < count; i++) {
    // Classic 8-bit victory climb
    beep(1319, 120); // E6
    delay(20);
    beep(1568, 120); // G6
    delay(20);
    beep(1760, 120); // A6
    delay(20);
    beep(2093, 180); // C7
    delay(40);
    // Signature Mario-style ending
    beep(2637, 250); // E7
    delay(30);
    beep(2093, 400); // C7 (final resolve)
    if (i < count - 1)
      delay(200);
  }
}

/**
 * Play click sound (for button feedback)
 */
inline void click() { beep(800, 10); }

} // namespace Buzzer

#endif // BUZZER_H
