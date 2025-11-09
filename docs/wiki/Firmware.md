# Firmware

## Overview
- Arduino core sketch under `firmware/arduino/bootbox_mcu_fw`.
- Async HTTP + WebSocket server, LittleFS assets, Preferences-based settings.

## Key Components
- `config.h`: build-time pin/fan/thermistor/LED configuration.
- `bootbox_mcu_fw.ino`: runtime state machine, PID loop, WebSocket handlers, LED driver, thermistor calibration backend.
- `settings.h`: persisted tuning knobs, DSP placeholders, thermistor calibration slots.

## TODO
- Document ADAU1701 transport once the DSP control path is implemented.
- Capture heap/flash footprints per release.
