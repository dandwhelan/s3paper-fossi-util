# 💻 Developer Guide

Technical information for building, modifying, and contributing to the project.

## Development Environment

The project is built using **PlatformIO** with **VS Code**.

### 1. Prerequisites
- **VS Code**: [Download](https://code.visualstudio.com/)
- **PlatformIO Extension**: Install from the VS Code Marketplace.
- **Git**: For version control.

### 2. Setup
```bash
git clone https://github.com/your-repo/FOSSI-S3PAP.git
cd FOSSI-S3PAP
# Open this folder in VS Code
```

### 3. Build & Upload
Use the PlatformIO sidebar or terminal:
```bash
# Build
pio run

# Upload to M5Paper S3
pio run -t upload

# Monitor Serial Output
pio device monitor
```

## Project Structure

```
src/
├── main.cpp              # Entry point, setup(), loop()
├── power_history.cpp     # Power data logging/management
├── ble/
│   └── ble_client.cpp    # NimBLE client logic for Fossibot
├── ui/
│   ├── ui_manager.cpp    # Main GUI definitions (M5GFX)
│   └── ...               # Sub-screens (Notes, Epub, etc.)
├── epub/                 # E-book parsing logic
└── utils/                # SD card, helper functions
```

## Key Architectural Notes

### I2C Bus Conflict Resolution
The M5Paper S3 has shared I2C lines which can cause conflicts between the Touch Controller (GT911) and RTC (BM8563).
- **Fix**: We use the standard `Wire` instance on pins 41 (SDA) and 42 (SCL) for both RTC and Touch.
- **Touch**: Accessed via `Wire` with careful timing to avoid conflicts.

### Task Management
- **Main Loop**: Handles UI updates and touch polling.
- **BLE Task**: Runs on Core 0 to avoid blocking the UI (Core 1).
- **E-Reader**: Uses a separate task for heavy parsing to prevent Watchdog timeouts.

### Memory Optimization
- Large buffers (like the framebuffer or EPUB chunks) are allocated in **PSRAM** (`malloc_caps(..., MALLOC_CAP_SPIRAM)`).
- Stack sizes for FreeRTOS tasks are tuned to prevent overflows during recursion (e.g., recursive JSON parsing or EPUB navigation).
