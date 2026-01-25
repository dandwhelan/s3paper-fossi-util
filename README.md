# M5Paper S3 Fossibot Dashboard - e-Rex

A power user's companion for the Fossibot Power Station, built on the M5Paper S3. Combines detailed energy monitoring with a suite of distraction-free productivity tools.

![M5Paper S3](https://static-cdn.m5stack.com/resource/docs/products/core/PaperS3/img-ae5e6b0a-f54c-4fa4-953b-fca2ed1e1a1d.webp)

<img width="882" height="544" alt="image" src="https://github.com/user-attachments/assets/af15b15f-9177-44d4-866e-3424a500df9e" />


## 🌟 Key Features

### 🔋 [Power Monitor & Control](docs/POWER_MONITOR.md)

* **Real-time Dashboard**: Monitor Battery, Watts (In/Out), Voltage, and Time Remaining via BLE.
* **7-Day History**: Track your energy usage trends with built-in graphing.
* **Remote Control**: Toggle USB, DC, and AC ports wirelessly.
* **Power Optimization**: Intelligent CPU frequency scaling extends battery life by 15-20%.

### 📖 [Usage & EPUB Reader](docs/EPUB_READER.md)

* **E-Ink Reader**: Read `.epub` books comfortably with adjustable fonts (Medium/Large).
* **Progress Saving**: Never lose your page.

### 🛠️ [Productivity Tools](docs/PRODUCTIVITY.md)

* **Notes**: fast, low-latency scribbling with saving to SD card.
* **Timer & Pomodoro**: Focus tools that run in the background.
* **Calculator**: Quick arithmetic on the fly.
* **Games**: Relax with Sudoku and 2048.

### 🐈‍⬛ e-Rex

The device is named e-Rex named after my recently deceased cat. He lived to the ripe age of 13 1/2 and was very much loved by the family. This project was a nice distraction while mourning his loss. The boot screen needs to be set within the SDCard on a file called boot.png. Here is mine...

<img width="600" height="876" alt="Screenshot 2026-01-21 204024" src="https://github.com/user-attachments/assets/ef3c2c5b-698f-4131-8a9f-b0d3c9cd13ca" />

---

## 🚀 Quick Start

### Hardware Required

* **M5Paper S3** (ESP32-S3 E-Ink Display)
* **Fossibot Power Station** (Compatible model)
* **MicroSD Card** (Required for History, EPUBs, and Notes)

### Installation

1. **Clone the Repository**

    ```bash
    git clone https://github.com/your-username/FOSSI-S3PAP.git
    cd FOSSI-S3PAP
    ```

2. **Build with PlatformIO**
    * Open the folder in **VS Code**.
    * Install the **PlatformIO** extension.
    * Click **PlatformIO Icon** -> **Project Tasks** -> **Upload**.

3. **SD Card Setup**
    * Create a folder `/books` for your books.
    * Create a folder `/history` (optional, auto-created).

For detailed developer instructions, see the [Developer Guide](docs/DEVELOPMENT.md).

## 📄 Documentation

* [**Power Monitor Guide**](docs/POWER_MONITOR.md) - Dashboard, History, BLE pairing.
* [**EPUB Reader Guide**](docs/EPUB_READER.md) - Loading hooks, gestures, fonts.
* [**Productivity Tools**](docs/PRODUCTIVITY.md) - Notes, Calculator, Games.
* [**Developer Guide**](docs/DEVELOPMENT.md) - Architecture, building, contributing.

## License

MIT License - See [LICENSE](LICENSE) for details.
