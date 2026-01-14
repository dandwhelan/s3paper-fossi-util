# M5Paper S3 Fossibot Dashboard

A "retro dashboard" interface for M5Paper S3 that displays and controls Fossibot power station status via Bluetooth Low Energy (BLE), now enhanced with productivity tools.

![M5Paper S3](https://static-cdn.m5stack.com/resource/docs/products/core/PaperS3/img-ae5e6b0a-f54c-4fa4-953b-fca2ed1e1a1d.webp)

## Features

### 🔋 Power Dashboard

- **Real-time Monitoring**: Battery %, Input/Output Watts, Time Remaining.
- **Wireless Control**: Toggle USB, DC, and AC outlets remotely via BLE.
- **Smart Refresh**: Configurable E-Ink refresh rates to save power.

### 📊 Power History
- **7-Day Tracking**: Detailed history of Battery %, Input Watts, and Output Watts.
- **Interactive Graph**: Multi-metric visualization with pinch-to-zoom (future) / easy navigation.
- **Data Persistence**: Data saved to SD card continuously.
- **Optimized UI**: Fast loading, high-contrast black/white design, and configurable filters.

### 📝 Notes (Scribble Pad)

*New in v2.0!*

- **Fast Low-Latency Drawing**: Optimized 15ms touch polling for smooth ink.
- **Tools**: Thin, Medium, Thick pens, and Eraser.
- **Smart Persistence**: Scribbles stay on screen even if you change tools.
- **Auto-Silence**: Battery updates are paused in Notes mode to prevent screen flashing.
- **Clean Exit**: Exiting wipes the screen pure white to remove ghosting.

### 🧮 Calculator

*New in v2.0!*

- Standard arithmetic operations (+, -, *, /).
- Clean Retro UI.

### 🍅 Pomodoro Timer

*New in v2.0!*

- Focus Timer (25 min) and Break Timer (5 min).
- Visual Progress Bar.
- Play/Pause/Reset controls.
- Background operation (timer continues even if you switch screens).

### 🎮 Games Center

*New in v2.1!*

#### **Sudoku (Enhanced)**
- **Professional UI**: Difficulty selector (Easy/Med/Hard), clean e-ink layout.
- **Features**: Random puzzle generation, bold black text for readability, confirmation dialogs.
- **Smart Refresh**: Full EPD quality refresh to eliminate ghosting.

#### **2048**
- **Classic Gameplay**: Swipe (tap arrows) to merge tiles up to 2048.
- **Auto-Save**: Progress is saved even if you exit the game.
- **Optimized**: Fast refresh rate for smooth animations on e-ink.

### 🛠️ System Improvements

- **Dual I2C Architecture**: Solved hardware conflict between Touch (GT911) and RTC (BM8563) by separating buses.
- **Enhanced Stability**: Fixed crashes related to stack overflow and I2C collisions.
- **Optimized UI**: Improved button responsiveness and layout.

---

## Hardware Requirements

| Component | Description |
|-----------|-------------|
| M5Paper S3 | ESP32-S3 based E-Ink device with touch |
| Fossibot Power Station | Compatible portable power station |

## Quick Start

### 1. Clone and Open

```bash
git clone <repository-url>
cd S3
```

### 2. Install PlatformIO

Install [PlatformIO](https://platformio.org/) extension in VS Code.

### 3. Build and Upload

```bash
pio run -t upload
```

### 4. Pair with Fossibot

Power on your Fossibot and the M5Paper S3 will automatically scan and connect.

## Development Plan & Roadmap

### Completed (v2.0 - v2.2) ✅

- [x] **I2C Conflict Fix**: Separated Touch (Wire1) and RTC (Wire).
- [x] **Pomodoro Timer**: Functional timer with background tracking.
- [x] **Calculator**: Basic UI and logic implementation.
- [x] **Sudoku**: Full game implementation with difficulty levels.
- [x] **2048**: Classic puzzle game with auto-save.
- [x] **Notes App**:
  - [x] Smooth Scribbling (Fast EPD mode).
  - [x] Canvas Persistence.
  - [x] Save to SD Card (as BMP).
  - [x] File Browser for past notes.
- [x] **History Graph**:
  - [x] 7-Day Power History.
  - [x] Multi-metric visualization.
  - [x] Persistent Storage.

### Upcoming 🚧

- [ ] **Wifi MQTT**: Publish stats to Home Assistant.

## Project Structure

```
S3/
├── src/
│   ├── main.cpp              # Entry point, touch handling, setup
│   ├── ble/
│   │   ├── ble_client.cpp    # BLE connection and Modbus protocol
│   └── ui/
│       ├── ui_manager.cpp    # Screen rendering and touch
│       └── ui_manager.h      # UI state and constants
└── platformio.ini            # Build configuration
```

## Dependencies

- [M5Unified](https://github.com/m5stack/M5Unified)
- [M5GFX](https://github.com/m5stack/M5GFX)
- [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino)
- [ArduinoJson](https://github.com/bblanchon/ArduinoJson)

## License

MIT License - See [LICENSE](LICENSE) for details.
