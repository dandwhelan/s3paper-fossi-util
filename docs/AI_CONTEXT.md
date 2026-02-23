# AI Context for S3Paper Fossibot Dashboard

This document provides a concise overview of the S3Paper Fossibot Dashboard project, intended for AI assistants to quickly understand its purpose, architecture, and deployment without needing to deep-dive into the entire codebase.

## 1. Project Purpose

The S3Paper Fossibot Dashboard is an embedded application designed for the M5Paper S3 device. It serves as a companion to the Fossibot Power Station, providing:

*   **Real-time Power Monitoring:** Displays battery status, input/output power, voltage, and estimated time remaining from the Fossibot via BLE.
*   **Energy History:** Logs and visualizes power usage over 7 days.
*   **Productivity Tools:** Includes an E-Reader for EPUB/TXT files, a clock, alarms, timers, Pomodoro timer, calculator, notes, and to-do lists.
*   **Distraction-Free Interface:** Utilizes the M5Paper's e-ink display for low power consumption and readability.

## 2. Key Features & Functionality

*   **BLE Communication:** Connects to the Fossibot Power Station using the NimBLE client to receive power data.
*   **E-Ink Display:** Optimized for the M5Paper's e-ink screen, with custom rendering for text and graphics.
*   **Power Management:** Implements deep sleep modes to conserve battery life, with wake-up triggers (e.g., touch, button presses, alarms).
*   **File System:** Utilizes an SD card for storing configuration, e-books, and power history logs.
*   **Hardware Integration:** Interacts with the M5Paper S3's components, including the display, touch controller, RTC, buzzer, and ADC for battery monitoring.

## 3. Core Architectural Components

*   **`main.cpp`**:
    *   Application entry point.
    *   Initializes hardware (M5Unified, I2C, RTC, BLE, SD Card).
    *   Manages the main application loop and transitions between UI screens.
    *   Handles global power management checks (e.g., deep sleep).
*   **`UIManager` (`ui/ui_manager.cpp/.h`)**:
    *   Manages the graphical user interface (GUI).
    *   Handles screen transitions, drawing, and touch input.
    *   Displays data (power metrics, timers, e-books, notes).
    *   Implements power-saving features like auto-sleep and deep sleep entry.
*   **`FossibotBLE` (`ble/ble_client.cpp/.h`)**:
    *   Handles BLE client functionality to communicate with the Fossibot Power Station.
    *   Receives and parses power data from the power station.
    *   Manages BLE connection status and data synchronization.
*   **`Battery` (`hardware/battery.h`)**:
    *   Provides functions to read the M5Paper's internal battery voltage and calculate its percentage.
    *   Handles ADC initialization and voltage conversion.
*   **`Buzzer` (`hardware/buzzer.h`)**:
    *   Abstraction for the M5Paper's passive buzzer.
    *   Provides functions to play simple beeps, alarms, and other sound effects.
*   **`SDManager` (`utils/sd_manager.cpp/.h`)**:
    *   Manages interactions with the SD card.
    *   Handles file operations (reading config, saving history, loading e-books, storing images).
*   **`Config` (`utils/config.cpp/.h`)**:
    *   Loads and saves application settings (e.g., auto-sleep duration, BLE settings, UI preferences) to the SD card.
*   **EPUB Reader (`src/epub/`)**:
    *   A submodule for parsing and rendering EPUB files. It includes components for HTML parsing, hyphenation, rendering, and ZIP file handling.

## 4. Deployment & Build Process

The project is built using **PlatformIO** with **VS Code**.

### Prerequisites:
*   VS Code IDE.
*   PlatformIO IDE extension for VS Code.
*   Git version control.

### Setup:
1.  Clone the repository:
    ```bash
    git clone https://github.com/dandwhelan/s3paper-fossi-util.git
    cd s3paper-fossi-util
    ```
2.  Open the project folder in VS Code.

### Building and Uploading:
Use the PlatformIO CLI or the PlatformIO sidebar in VS Code:

*   **Build the project:**
    ```bash
    pio run
    ```
*   **Upload to the M5Paper S3:**
    ```bash
    pio run -t upload
    ```
*   **Monitor Serial Output:**
    ```bash
    pio device monitor
    ```

## 5. Key Architectural Notes

*   **I2C Bus Management**: The M5Paper S3 shares an I2C bus for the RTC (BM8563) and Touch Controller (GT911). The code carefully initializes `Wire` on the correct pins (SDA:41, SCL:42) and uses appropriate timings to avoid conflicts.
*   **Memory Optimization**: PSRAM is used for large buffers (e.g., framebuffers, EPUB data) to avoid RAM limitations. Task stack sizes are tuned for stability.
*   **Task Management**: Critical operations like BLE communication and EPUB parsing may run on separate FreeRTOS tasks to prevent blocking the main UI thread and avoid watchdog timeouts.
*   **Power Saving**: The application aggressively uses deep sleep to conserve battery. Sleep is triggered by inactivity (BLE disconnected for an hour) or manually by the user. Wake-up can be initiated by touch, buttons, or alarms.

## 6. Future AI Considerations

*   **E-Reader Performance**: Parsing large EPUB files can be CPU-intensive. Future optimizations might involve pre-processing or more efficient parsing strategies.
*   **BLE Protocol**: The `fossibot_protocol.h` file defines the BLE characteristics and register addresses. Understanding this protocol is key to debugging or extending power station communication.
*   **UI State Management**: The `UIManager` manages screen states and refresh logic. Changes to UI elements or interactions should consider `_needsRefresh` and potential refresh rates.
*   **Hardware Abstraction**: Functions in `hardware/` and `utils/` provide abstractions for specific hardware and system features. These should be reused where appropriate.
*   **Error Handling**: Robust error handling, especially for file I/O (SD card) and BLE communication, is crucial.

This document aims to provide a foundational understanding for AI agents interacting with this codebase.
