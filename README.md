## BOOTBOX DSP MCU Firmware

Firmware for an ESP32-based system controller for a DIY car stereo using an ADAU1701 DSP. The ESP32 hosts a local web UI, provides a robust WebSocket control channel, manages thermal control (PID/manual), and stores presets/assets in LittleFS.

## Quick Start
- Board: ESP32 DevKitC / NodeMCU-32S (default pins in sketch)
- Build (Arduino CLI):
  - `arduino-cli compile --fqbn esp32:esp32:esp32 firmware/arduino/bootbox_mcu_fw`
  - `arduino-cli upload -p /dev/ttyUSB0 --fqbn esp32:esp32:esp32 firmware/arduino/bootbox_mcu_fw`
- First boot: ESP32 starts AP `BOOTBOXDSP` (pass `lollipop`). Open `http://192.168.4.1`.
- UI path: files under `firmware/arduino/bootbox_mcu_fw/data/` (served via LittleFS). Use Arduino ESP32 LittleFS data uploader to flash assets if needed.

## Features
- WebSocket protocol `{type,id,data}` with app-level ack/retry, periodic `state` broadcast.
- Simple settings storage (NVS/Preferences): PID enable, 2 setpoints, manual fan %, PID gains, upload toggles.
- HTTP endpoints:
  - `/api/state` (GET): current temps, RPM, target, settings (no secrets).
  - `/api/upload/adau` (POST): upload ADAU images to `/dsp/` (guarded by settings).
  - `/api/logs` (GET): device log ring buffer.

## Hardware Connections (prototype)
- Thermistors (NTC) to analog: `temp1 -> GPIO34`, `temp2 -> GPIO35`. Use voltage dividers to 3.3V; adjust ADC-to-temp curve in code.
- Fan control:
  - Global setting in `firmware/arduino/bootbox_mcu_fw/config.h` → `kFanType` (`Fan3Wire` or `Fan4Wire`).
  - Control pin: `PIN_FAN_CTRL` (PWM). For 4-wire, this is the control pin; for 3-wire, drive fan power via MOSFET.
  - Tach: `PIN_FAN_TACH` (open collector; add pull-up). RPM measurement pending (PCNT/RMT).
  - PWM freq: 4-wire uses 25 kHz; 3-wire default 1 kHz (configurable). 3-wire applies a minimum start duty to avoid stall.
- ADAU1701: I2C for control (future): `SDA -> GPIO21`, `SCL -> GPIO22`. SPI/I2S for audio as needed by your module (not yet used here).

## Libraries
- ESPAsyncWebServer, AsyncTCP, ArduinoJson, LittleFS, Preferences (ESP32 core). Install via Boards Manager/Library Manager.

## Notes
- Upload Security: toggle uploads and optional bearer token in the UI. Avoid leaving uploads enabled in production.
- Tuning: adjust PID gains in UI. Setpoints default to 45/55°C. Replace `adcToTempC()` with your NTC curve.
- Build-time config: edit `firmware/arduino/bootbox_mcu_fw/config.h` to set fan type, pins, and PWM parameters.
