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
  - Upload/manage SigmaStudio “bundles” (program + interface XML) stored under LittleFS `/dsp/<bundle>/`.
  - Dynamic DSP controls rendered from the uploaded interface schema (no hard-coded UI).
  - Preset system per bundle (save/load/delete) persisted under `/dsp-presets/`.
  - Bundle pushes write `program.bin` into the ADAU1701 self-boot EEPROM over I²C and pulse reset so the codec boots the selected SigmaStudio image without reflashing the MCU.
  - Control changes stream over the WebSocket (`set_dsp`) and immediately hit the mapped ADAU addresses (5.23 fixed-point by default) while remaining persistent in NVS.
  - Hardware link telemetry (`hw_ready` + `hw_error`) is broadcast alongside the schema/state so the UI can warn about ADAU reset issues or I²C write failures in real time.
- Status LED pattern driver with distinct blink codes for boot, OK, logs, thermal, network, DSP, and critical faults.
- HTTP endpoints:
  - `/api/state` (GET): temps, fan data, current DSP values, system metadata, thermistor cal state.
  - `/api/dsp/bundles` (GET): enumerate bundles (name, active flag, file presence).
  - `/api/dsp/schema` (GET): fields include `active`, `schema_ready`, `hw_ready`, optional `hw_error`, sanitized `controls[]`, `values{}`, and `presets[]`.
  - `/api/dsp/action` (POST): JSON `{"action": ...}` for bundle select/delete/rename/push and preset save/load/delete.
  - `/api/upload/adau` (POST form): upload program/interface files (`bundle` + `kind=program|interface` query params).
- `/api/logs` (GET): device log ring buffer.
- `/api/therm/calibration` (GET/POST): manage calibration workflow (start, capture, solve, clear).

## Companion Tools
- **BB-Builder** (`BB-Builder/`): cross-platform Qt desktop app (in progress) for importing SigmaStudio projects, arranging UI layouts, and exporting `.bbx` bundles (program + interface) for upload via the ESP32 web UI.
  - Current build parses `.params` + `.xml` exports, surfaces module/control metadata, identifies the matching program binary, and lets you assemble layout presets with a drag-and-drop UI that mirrors the MCU dashboard styling.

## Documentation
- The `docs/wiki/` folder mirrors the GitHub wiki. Key entries:
  - `Home.md` — sitemap of all topics.
  - `DSP-Bundle-How-To.md` — exporting SigmaStudio binaries, crafting interface XML, uploading/pushing bundles, and saving presets.
  - `Build-and-Flash.md` — helper scripts, unified flashing, and verification steps.
  - `Thermistor-Calibration.md` — two-point workflow and REST payload examples.
- Push the same files to `https://github.com/graysam/bootbox-mcu-fw.wiki.git` when the public wiki is enabled.

## Hardware Connections (prototype)
- Thermistors (NTC) to analog: `temp1 -> GPIO34`, `temp2 -> GPIO35`. Use voltage dividers to 3.3V; adjust ADC-to-temp curve in code.
- Default config ships with dual 10k/10k dividers; if you swap probes or bias resistors, update `THERMISTOR*_PARAMS` in `config.h` or run the calibration wizard (System tab) to solve for nominal resistance/beta.
- Fan control:
  - Global setting in `firmware/arduino/bootbox_mcu_fw/config.h` → `kFanType` (`Fan3Wire` or `Fan4Wire`).
  - Control pins: `PIN_FAN1_CTRL` (required) and `PIN_FAN2_CTRL` (optional second fan, share duty). Set the secondary pin/channel to `-1` to disable.
  - Tach inputs (`PIN_FAN1_TACH`, `PIN_FAN2_TACH`) are reserved for future RPM capture.
  - PWM freq: 4-wire uses 25 kHz; 3-wire default 1 kHz (configurable). 3-wire applies a minimum start duty to avoid stall.
- ADAU1701 control bus:
  - Default I²C pins are `GPIO21 (SDA)` / `GPIO22 (SCL)` (`PIN_I2C_SDA`/`PIN_I2C_SCL` in `config.h`).
  - `ADAU_I2C_ADDR` (0x34 by default) is used for live parameter writes; `ADAU_EEPROM_I2C_ADDR` (0x50) targets the external self-boot EEPROM.
  - If you expose the ADAU reset pin, set `PIN_ADAU_RESET` so the firmware can pulse it after programming the EEPROM/self-boot cycle.
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
- ADAU transport: `PIN_I2C_SDA`/`PIN_I2C_SCL`, `ADAU_I2C_ADDR`, and optional `PIN_ADAU_RESET` live in `config.h`. Set them to match your wiring so bundle pushes and live control writes function correctly.

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

Supported control attributes: `id`, `label`, `type` (`slider` default, `toggle`), `unit`, `min`, `max`, `step`, `default`, `address`, `bytes`, `format`. `format` accepts `fixed5.23` (default), `u8`, `u16`, `u24`, `u32`, or `raw`. Firmware parses this file on upload/activation and exposes the controls via `/api/dsp/schema`; the UI renders matching widgets and streams changes over the WebSocket (`set_dsp` messages).
- `push_bundle` writes `program.bin` into the ADAU EEPROM (I²C) and toggles reset so the codec selfboots the new image; no manual flashing steps required.
- Presets (`/dsp-presets/<bundle>/*.json`) capture the current control values for quick recall.
