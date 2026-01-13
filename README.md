# M5Paper S3 Fossibot Dashboard

A "retro dashboard" interface for M5Paper S3 that displays and controls Fossibot power station status via Bluetooth Low Energy (BLE), now enhanced with productivity tools.

![M5Paper S3](https://static-cdn.m5stack.com/resource/docs/products/core/PaperS3/img-ae5e6b0a-f54c-4fa4-953b-fca2ed1e1a1d.webp)

## Features

### 🔋 Power Dashboard

- **Real-time Monitoring**: Battery %, Input/Output Watts, Time Remaining.
- **Wireless Control**: Toggle USB, DC, and AC outlets remotely via BLE.
- **Smart Refresh**: Configurable E-Ink refresh rates to save power.

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
- **NEW**: ±5 minute adjustment buttons for quick timer changes.

### 🎮 Games

*New in v2.1!*

#### 2048
- Classic tile-merging puzzle game
- Fast EPD refresh mode for smooth gameplay
- Score tracking with high score persistence
- Auto-save/load game state to SD card
- Swipe controls (up/down/left/right)
- Optimized for e-ink display

#### 6x6 Sudoku
- Perfect size for e-ink (85px cells vs cramped 9x9)
- Numbers 1-6 with 2x3 block rules
- 9 handcrafted puzzles (3 easy, 3 medium, 3 hard)
- Real-time validation (errors shown in red)
- Win detection with success message
- Controls: Number input, Clear, Check, New Puzzle

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

### Completed (v2.0) ✅

- [x] **I2C Conflict Fix**: Separated Touch (Wire1) and RTC (Wire).
- [x] **Pomodoro Timer**: Functional timer with background tracking.
- [x] **Calculator**: Basic UI and logic implementation.
- [x] **Notes App**:
  - [x] Smooth Scribbling (Fast EPD mode).
  - [x] Canvas Persistence.
  - [x] Layout Optimizations (Centered Text, logical button flow).
  - [x] Ghost Touch Fixes.

### Upcoming 🚧

- [ ] **Notes Saving**: Save scribbles to SD Card (BMP/PNG).
- [ ] **History Graph**: Plot power usage over time.
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
