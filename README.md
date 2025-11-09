## BOOTBOX DSP MCU Firmware

Firmware for a Wi-Fi MCU system controller for a DIY car stereo using an ADAU1701 DSP. The controller hosts a local web UI, provides a robust WebSocket control channel, manages thermal control (PID/manual), and stores presets/assets in LittleFS.

## Quick Start
- Board: target pinout matches DevKit-style layouts by default (adjust in `config.h` as needed).
- Build + package (Arduino CLI):
  - `ARDUINO_CLI_CONFIG=./arduino-cli.yaml tools/arduino-build.sh --fqbn esp32:esp32:esp32`
- Upload firmware **and** LittleFS:
  - `PORT=/dev/ttyUSB0 ARDUINO_CLI_CONFIG=./arduino-cli.yaml tools/arduino-upload.sh --fqbn esp32:esp32:esp32`
- Optional serial monitor: `PORT=/dev/ttyUSB0 tools/arduino-monitor.sh`
- First boot: controller starts open AP `BOOTBOXDSP` (no password). Open `http://192.168.4.1`.
- UI assets live in `firmware/arduino/bootbox_mcu_fw/data/`; the build script repacks them into the LittleFS image automatically.

## Features
- WebSocket protocol `{type,id,data}` with app-level ack/retry, periodic `state` broadcast, and dirty-state aware UI widgets.
- Persistent settings (Preferences/NVS): PID enable, dual setpoints, manual fan %, PID gains, thermistor calibration curves, DSP control values.
- System tab with thermistor calibration wizard (capture cold/hot reference baths, solve curve, store to NVS) plus live system metrics.
- DSP management:
  - Upload/manage SigmaStudio “bundles” (program + interface XML) stored under LittleFS `/dsp/&lt;bundle&gt;/`.
  - Dynamic DSP controls rendered from the uploaded interface schema.
  - Preset system per bundle (save/load/delete) persisted under `/dsp-presets/`.
  - REST/WS API to select bundles, rename/delete, and (for now) request a “push” while keeping the ADAU in self-boot mode.
- Status LED pattern driver with distinct blink codes for boot, OK, logs, thermal, network, DSP, and critical faults.
- HTTP endpoints:
  - `/api/state` (GET): temps, fan data, current DSP values, system metadata, thermistor cal state.
  - `/api/dsp/bundles` (GET): enumerate bundles (name, active flag, file presence).
  - `/api/dsp/schema` (GET): active bundle + parsed controls + current values + presets.
  - `/api/dsp/action` (POST): JSON `{"action": ...}` for bundle select/delete/rename/push and preset save/load/delete.
  - `/api/upload/adau` (POST form): upload program/interface files (`bundle` + `kind=program|interface` query params).
  - `/api/logs` (GET): device log ring buffer.
  - `/api/therm/calibration` (GET/POST): manage calibration workflow (start, capture, solve, clear).

## Hardware Connections (prototype)
- Thermistors (NTC) to analog: `temp1 -> GPIO34`, `temp2 -> GPIO35`. Use voltage dividers to 3.3V; adjust ADC-to-temp curve in code.
- Default config ships with dual 10k/10k dividers; if you swap probes or bias resistors, update `THERMISTOR*_PARAMS` in `config.h` or run the calibration wizard (System tab) to solve for nominal resistance/beta.
- Fan control:
  - Global setting in `firmware/arduino/bootbox_mcu_fw/config.h` → `kFanType` (`Fan3Wire` or `Fan4Wire`).
  - Control pins: `PIN_FAN1_CTRL` (required) and `PIN_FAN2_CTRL` (optional second fan, share duty). Set the secondary pin/channel to `-1` to disable.
  - Tach inputs (`PIN_FAN1_TACH`, `PIN_FAN2_TACH`) are reserved for future RPM capture.
  - PWM freq: 4-wire uses 25 kHz; 3-wire default 1 kHz (configurable). 3-wire applies a minimum start duty to avoid stall.
- ADAU1701: I2C for control (future): `SDA -> GPIO21`, `SCL -> GPIO22`. SPI/I2S for audio as needed by your module (not yet used here).
- USB serial: tested with CH340 and CP2102 bridges; adjust `PORT=/dev/ttyUSBx` accordingly.

## Libraries
- ESPAsyncWebServer, AsyncTCP, ArduinoJson, LittleFS, Preferences (Arduino core). Install via Boards Manager/Library Manager.

## Thermistor calibration workflow
1. Wire your probes + divider, update `config.h` with approximate defaults (e.g. 10k/10k, β=3950).
2. Open the web UI → *System* tab → *Thermistor Calibration* card.
3. Pick the channel, click **Start session**, then capture cold/hot reference points (e.g. iced water ≈0 °C, boiling ≈100 °C) using the buttons. Each capture uses averaged ADC samples at the moment you click.
4. Optionally set the datasheet nominal temperature (defaults to 25 °C) and hit **Solve & save** to persist the curve in NVS. You can clear or cancel from the same card.

## Status LED codes
| Pattern | Meaning |
| --- | --- |
| Fast double blink | Booting |
| 50 ms flash every 2 s | System OK |
| Two short pulses | Check logs (recent watchdog/panic) |
| Triple pulse | Thermal error |
| Long-short-long | Network error |
| Slow solid | Critical error |

Patterns automatically adjust for the configured LED polarity.

## Notes
- Tuning: adjust PID gains/setpoints in UI; manual fan target persists while PID is disabled.
- Build-time config: edit `firmware/arduino/bootbox_mcu_fw/config.h` for fan type, pin map, PWM frequency, thermistor defaults, LED polarity, etc.
- `tools/arduino-build.sh` packs LittleFS alongside the firmware so uploads always deliver both the sketch and UI assets.
- DSP bundles: store SigmaStudio binaries + interface schemas in `/dsp/&lt;bundle&gt;/{program.bin,interface.xml}`; presets live in `/dsp-presets/&lt;bundle&gt;/`.

## Releases
1. Build with `tools/arduino-build.sh --config-file arduino-cli.yaml` (optionally pass `--build-path`).
2. Package artifacts from `.arduino-build/` (e.g. `bootbox_mcu_fw.unified.bin`, `flash-manifest.json`, LittleFS image) into a zip/tarball.
3. Tag and publish via GitHub releases (see `docs/BUILD.md` for command snippets). Each release should note the matching commit SHA and include the generated archive for flashing without the toolchain.

## DSP Bundles & Interface Schema
- Each bundle resides under LittleFS `/dsp/&lt;bundle&gt;/` and consists of:
  - `program.bin` – SigmaStudio self-boot image staged for future pushes.
  - `interface.xml` – control definition file consumed by the web UI/firmware.
- Interface XML format (simplified example):

```xml
<interface>
  <control id="master" label="Master Volume" type="slider" unit="dB"
           min="-60" max="12" step="0.5" default="-10" address="0x1000" />
  <control id="xover" label="Crossover" type="slider" unit="Hz"
           min="40" max="240" step="1" default="120" address="0x1010" />
  <control id="mute" label="Mute Outputs" type="toggle"
           min="0" max="1" address="0x1020" />
</interface>
```

Supported control attributes: `id`, `label`, `type` (`slider` default, `toggle`), `unit`, `min`, `max`, `step`, `default`, `address`, `bytes`, `format`. Firmware parses this file on upload/activation and exposes the controls via `/api/dsp/schema`; the UI renders matching widgets and streams changes over the WebSocket (`set_dsp` messages).
