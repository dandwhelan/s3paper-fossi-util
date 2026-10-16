# 🔋 Power Monitor & History

The core function of the dashboard is to monitor and control your Fossibot power station.

## Dashboard Overview

The main screen provides real-time telemetry from the device via BLE.

### Real-Time Metrics

- **Battery %**: Large, easy-to-read percentage.
- **Input (Watts)**: Current solar or AC charging power.
- **Output (Watts)**: Total power draw from all ports.
- **Voltage**: Internal battery voltage (calibrated for LiFePO4 curve).
- **Time Remaining**: Estimated time to empty or full based on current load.

### Controls

- **USB**: Toggle USB-A and USB-C ports.
- **DC**: Toggle 12V DC ports (Car port, DC5521).
- **AC**: Toggle AC Inverter (Long press may be required on some units).

---

## 📊 History & Analytics

Keep track of your energy usage over time with the integrated history logger.

### Features

- **7-Day History**: The device stores detailed logs for the last 7 days.
- **On-Device Graphing**: View trends directly on the screen.
- **Zero-Config**: Logging starts automatically as soon as the device is running.

### Using the Graph

1. Tap **MENU** on the bottom bar to open Settings.
2. Tap **History** to view the graph.
3. **Toggle Metrics**: Tap `Batt`, `In`, or `Out` at the top to show/hide specific lines.
4. **Navigation**:
    - Tap `<` or `>` to move between days.
    - The graph auto-scales to show detailed variations.

### SD Card Data Access

For deeper analysis, you can access the raw CSV files on the SD card.

- **Path**: `/history/`
- **Format**: `YYYY-MM-DD.csv`
- **Columns**: `timestamp,battery_percent,input_watts,output_watts`
  - *Timestamp is Unix epoch time.*

---

## Bluetooth (BLE) Connection

The dashboard connects to the Fossibot via Bluetooth Low Energy.

- **Auto-Connect**: The device scans for "Fossibot" or compatible identifiers on startup.
- **Reconnection**: If connection is lost (out of range), it will retry automatically.
- **Status Icon**: A small Bluetooth icon in the status bar indicates connection state (Solid = Connected, Blinking = Scanning).

---

## ⚡ Power Optimization

The M5Paper S3 firmware includes intelligent power management to maximize battery life while maintaining responsiveness.

### CPU Frequency Scaling (Eco Mode)

The device automatically adjusts CPU frequency based on activity:

- **Active (240MHz)**: Full performance when you're interacting with the device.
- **Eco Mode (80MHz)**: Low power mode after 5 seconds of inactivity.
- **Instant Wakeup**: CPU jumps to 240MHz immediately on touch to eliminate lag.

**Battery Impact**: Eco Mode reduces active current by ~20-30mA, extending runtime by approximately **15-20%** compared to always running at full speed.

### Bluetooth Power Settings

BLE radio is configured for optimal battery life:

- **TX Power**: Level 3 (+3dBm) - balanced range and consumption.
- **Connection Intervals**: Relaxed timing (100-200ms) to reduce radio activity.
- **Smart Sleep**: Eco Mode remains active even when BLE is connected (deep sleep is disabled to maintain connection).

### Best Practices

1. **Keep BLE Connected**: The device is optimized to run efficiently while connected.
2. **Use Auto-Refresh**: The 60-second dashboard refresh minimizes unnecessary screen updates.
3. **SD Card**: History logging is batched to reduce write cycles.
