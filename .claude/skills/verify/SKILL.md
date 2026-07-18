---
name: verify
description: Build-verify e-Rex firmware changes in a cloud session (no hardware attached).
---

# Verifying e-Rex changes

The real surface is a physical M5Paper S3 e-ink panel flashed over USB —
unreachable from a cloud session. The closest reachable handle is a full
firmware build:

```bash
pip install platformio    # pio is not preinstalled in remote sessions
pio run                   # ~3 min first run (downloads ESP32-S3 toolchain), then fast
```

A clean link plus stable RAM/Flash percentages (baseline ~16.6% RAM /
~51% Flash) is the strongest evidence available here. Anything touching
rendering, touch, BLE, or EPD refresh behavior still needs on-hardware
confirmation by the user — say so explicitly in the report.

Gotchas:
- `pio run` output is huge; pipe through `tail -40`.
- There are no automated tests or linters in this repo (per CLAUDE.md).
