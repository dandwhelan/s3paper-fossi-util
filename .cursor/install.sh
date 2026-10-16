#!/usr/bin/env bash
# Cloud Agent install script for the e-Rex M5Paper S3 firmware.
#
# This project is PlatformIO/ESP32-S3 firmware. There is no runnable service in
# a cloud session (the real target is a physical e-ink device flashed over USB),
# so "running the app" here means producing a full firmware build. This script
# installs PlatformIO and warms the ESP32-S3 toolchain, platform and library
# caches by doing a complete `pio run`, so later builds in a booted agent are
# fast. It is idempotent: re-running only refreshes pip and recompiles.
set -euo pipefail

# PlatformIO is not preinstalled in cloud sessions; install it for the current
# user (pip falls back to ~/.local/bin on this externally-managed base image).
pip install --user --upgrade platformio

# Make the `pio` CLI available now and in future interactive agent shells.
export PATH="$HOME/.local/bin:$PATH"
if ! grep -qxF 'export PATH="$HOME/.local/bin:$PATH"' "$HOME/.bashrc" 2>/dev/null; then
  echo 'export PATH="$HOME/.local/bin:$PATH"' >> "$HOME/.bashrc"
fi

# Build the default (development) environment. The first run downloads the
# espressif32 platform, xtensa/riscv toolchains and the Arduino framework
# (~8 min, cached into the snapshot); subsequent runs just recompile.
cd "$(dirname "$0")/.."
pio run
