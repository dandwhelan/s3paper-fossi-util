# M5Paper S3 Fossibot Dashboard

A "retro dashboard" interface for M5Paper S3 that displays and controls Fossibot power station status via Bluetooth Low Energy (BLE).

![M5Paper S3](https://static-cdn.m5stack.com/resource/docs/products/core/PaperS3/img-ae5e6b0a-f54c-4fa4-953b-fca2ed1e1a1d.webp)

## Features

- 📊 **Real-time Power Monitoring** - Battery %, input/output power, time remaining
- 🔌 **Output Control** - Toggle USB, DC, and AC outputs via touch
- 🕐 **Clock Display** - Time and date with software-based timekeeping
- 📱 **Touch Interface** - Full touch control on 540x960 E-Ink display
- 🔄 **Configurable Refresh** - 5-300 second refresh rate for E-Ink

## Hardware Requirements

| Component | Description |
|-----------|-------------|
| M5Paper S3 | ESP32-S3 based E-Ink device with touch |
| Fossibot Power Station | Compatible portable power station |
| USB-C Cable | For programming and charging |

## Quick Start

### 1. Clone and Open

```bash
git clone <repository-url>
cd S3
```

### 2. Install PlatformIO

Install [PlatformIO](https://platformio.org/) extension in VS Code.

### 3. Configure WiFi (Optional)

Edit `src/config/config.h` if NTP time sync is desired.

### 4. Build and Upload

```bash
pio run -t upload
```

### 5. Pair with Fossibot

Power on your Fossibot and the M5Paper S3 will automatically scan and connect.

## Project Structure

```
S3/
├── src/
│   ├── main.cpp              # Entry point, touch handling, setup
│   ├── ble/
│   │   ├── ble_client.cpp    # BLE connection and Modbus protocol
│   │   └── fossibot_protocol.h # Register definitions
│   ├── ui/
│   │   ├── ui_manager.cpp    # Screen rendering and touch
│   │   └── ui_manager.h      # UI state and constants
│   └── config/
│       └── config.cpp        # Settings persistence
├── docs/
│   ├── hardware-guide.md     # I2C, pins, peripherals
│   ├── ble-protocol.md       # Fossibot Modbus protocol
│   ├── ui-architecture.md    # Screens and touch handling
│   └── known-issues.md       # Limitations and workarounds
└── platformio.ini            # Build configuration
```

## Documentation

| Document | Description |
|----------|-------------|
| [Hardware Guide](docs/hardware-guide.md) | I2C buses, pin mapping, M5Paper S3 specs |
| [BLE Protocol](docs/ble-protocol.md) | Fossibot registers, CRC calculation, commands |
| [UI Architecture](docs/ui-architecture.md) | Screen navigation, touch handling |
| [Known Issues](docs/known-issues.md) | Limitations and workarounds |

## Screenshots

### Home Screen

- Battery percentage with large display
- Input/Output power bars
- USB/DC/AC toggle buttons
- Time and date display

### Settings Screen

- Date/time adjustment
- Refresh rate configuration (5-300 seconds)
- Save/Cancel buttons

## Dependencies

- [M5Unified](https://github.com/m5stack/M5Unified) - M5Stack hardware abstraction
- [M5GFX](https://github.com/m5stack/M5GFX) - Graphics library
- [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino) - BLE client
- [ArduinoJson](https://github.com/bblanchon/ArduinoJson) - JSON parsing

## Credits

- Fossibot protocol reverse engineering based on:
  - [ESP-FBot](https://github.com/Pat-Laugh/ESP-FBot)
  - [fossibot-reverse-engineering](https://github.com/DirkH78/fossibot-reverse-engineering)

## License

MIT License - See [LICENSE](LICENSE) for details.
