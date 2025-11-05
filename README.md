## BOOTBOX DSP MCU Firmware

Firmware for an ESP32-based system controller for a DIY car stereo using an ADAU1701 DSP. The ESP32 hosts a local web UI, provides a robust WebSocket control channel, manages thermal control (PID/manual), and stores presets/assets in LittleFS.

## Quick Start
- Board: ESP32 DevKitC / NodeMCU-32S (default pins in sketch)
- Build + package (Arduino CLI):
  - `ARDUINO_CLI_CONFIG=./arduino-cli.yaml tools/arduino-build.sh --fqbn esp32:esp32:esp32`
- Upload firmware **and** LittleFS:
  - `PORT=/dev/ttyUSB0 ARDUINO_CLI_CONFIG=./arduino-cli.yaml tools/arduino-upload.sh --fqbn esp32:esp32:esp32`
- Optional serial monitor: `PORT=/dev/ttyUSB0 tools/arduino-monitor.sh`
- First boot: ESP32 starts open AP `BOOTBOXDSP` (no password). Open `http://192.168.4.1`.
- UI assets live in `firmware/arduino/bootbox_mcu_fw/data/`; the build script repacks them into the LittleFS image automatically.

## Features
- WebSocket protocol `{type,id,data}` with app-level ack/retry, periodic `state` broadcast.
- Simple settings storage (NVS/Preferences): PID enable, 2 setpoints, manual fan %, PID gains, upload toggles.
- DSP preview workspace with live knobs/faders for master/stereo/sub gains and crossover points (values persisted in NVS; wiring to ADAU1701 to follow).
- HTTP endpoints:
  - `/api/state` (GET): current temps, RPM, target, settings.
  - `/api/upload/adau` (POST): upload ADAU images to `/dsp/` (unrestricted).
  - `/api/logs` (GET): device log ring buffer.

## Hardware Connections (prototype)
- Thermistors (NTC) to analog: `temp1 -> GPIO34`, `temp2 -> GPIO35`. Use voltage dividers to 3.3V; adjust ADC-to-temp curve in code.
- Fan control:
  - Global setting in `firmware/arduino/bootbox_mcu_fw/config.h` → `kFanType` (`Fan3Wire` or `Fan4Wire`).
  - Control pins: `PIN_FAN1_CTRL` (required) and `PIN_FAN2_CTRL` (optional second fan, share duty). Set the secondary pin/channel to `-1` to disable.
  - Tach inputs (`PIN_FAN1_TACH`, `PIN_FAN2_TACH`) are reserved for future RPM capture.
  - PWM freq: 4-wire uses 25 kHz; 3-wire default 1 kHz (configurable). 3-wire applies a minimum start duty to avoid stall.
- ADAU1701: I2C for control (future): `SDA -> GPIO21`, `SCL -> GPIO22`. SPI/I2S for audio as needed by your module (not yet used here).

## Libraries
- ESPAsyncWebServer, AsyncTCP, ArduinoJson, LittleFS, Preferences (ESP32 core). Install via Boards Manager/Library Manager.

## Notes
- Tuning: adjust PID gains in UI. Setpoints default to 45/55°C. Replace `adcToTempC()` with your NTC curve.
- Build-time config: edit `firmware/arduino/bootbox_mcu_fw/config.h` to set fan type, pins, and PWM parameters.
