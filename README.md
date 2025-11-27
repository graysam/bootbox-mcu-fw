## BOOTBOX DSP MCU Firmware

Firmware for a Wi-Fi MCU system controller for a DIY car stereo using an ADAU1701 DSP. The controller hosts a local web UI, provides a robust WebSocket control channel, manages thermal control (PID/manual), and stores presets/assets in LittleFS. The repository also now houses the companion bt2i2s firmware so both sides of the link evolve together.

## Firmware modules
- `firmware/arduino/bootbox_mcu_fw/` — main controller (ESP32-S3 or ESP32). Build with `tools/target-build.sh --target esp32s3-devkitc --flash` (or esp32-devkitc) or direct `arduino-cli compile --fqbn ... firmware/arduino/bootbox_mcu_fw`.
- `firmware/arduino/bt2i2s/` — Bluetooth A2DP/AVRCP sink feeding ADAU via I2S (ESP32 WROOM DevKitC). Build with `arduino-cli compile --fqbn esp32:esp32:esp32 firmware/arduino/bt2i2s` (uses shared `arduino-cli.yaml` and vendored ESP32-A2DP library).
- When changing the UART link protocol or behavior, update both firmwares in tandem (see `docs/Link-Compatibility.md`) to keep interoperability.

## Quick Start
- Fast path: `tools/target-build.sh --target esp32s3-devkitc --flash` (or `esp32-devkitc`) reads `targets/<name>.def`, installs the right partition map, compiles, and uploads the image + LittleFS in one step.
- Board: ESP32 DevKit and ESP32-S3 DevKitC-1 (16MB flash / 8MB PSRAM) are auto-detected at build time; edit `config.h` if you wire a custom harness or add a new `.def` file.
- Build + package (Arduino CLI):
  - `ARDUINO_CLI_CONFIG=./arduino-cli.yaml tools/arduino-build.sh --fqbn esp32:esp32:esp32`
  - ESP32-S3 example: `ARDUINO_CLI_CONFIG=./arduino-cli.yaml tools/arduino-build.sh --fqbn 'esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,USBMode=hwcdc,PartitionScheme=custom' --build-path .arduino-build-s3`
- Upload firmware **and** LittleFS:
  - `PORT=/dev/ttyUSB0 ARDUINO_CLI_CONFIG=./arduino-cli.yaml tools/arduino-upload.sh --fqbn esp32:esp32:esp32`
  - ESP32-S3 example: `PORT=/dev/ttyACM0 ARDUINO_CLI_CONFIG=./arduino-cli.yaml tools/arduino-upload.sh --fqbn 'esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,USBMode=hwcdc,PartitionScheme=custom' --build-path .arduino-build-s3`
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
- `/api/state` reports PSRAM usage, IDF target, and filesystem stats so you can confirm whether an ESP32 or ESP32-S3 image is running.
- HTTP endpoints:
  - `/api/state` (GET): temps, fan data, current DSP values, system metadata, thermistor cal state.
  - `/api/dsp/bundles` (GET): enumerate bundles (name, active flag, file presence).
  - `/api/dsp/schema` (GET): fields include `active`, `schema_ready`, `hw_ready`, optional `hw_error`, sanitized `controls[]`, `values{}`, and `presets[]`.
  - `/api/dsp/action` (POST): JSON `{"action": ...}` for bundle select/delete/rename/push and preset save/load/delete.
  - `/api/upload/adau` (POST form): upload program/interface files (`bundle` + `kind=program|interface` query params).
  - `/api/logs` (GET): device log ring buffer.
  - `/api/therm/calibration` (GET/POST): manage calibration workflow (start, capture, solve, clear).

## Documentation
- The `docs/wiki/` folder mirrors the GitHub wiki. Key entries:
  - `Home.md` — sitemap of all topics.
  - `DSP-Bundle-How-To.md` — exporting SigmaStudio binaries, crafting interface XML, uploading/pushing bundles, and saving presets.
  - `Build-and-Flash.md` — helper scripts, unified flashing, and verification steps.
  - `Thermistor-Calibration.md` — two-point workflow and REST payload examples.
- Link protocol guardrails live in `docs/Link-Compatibility.md` so Bootbox and bt2i2s stay in sync.
- Push the same files to `https://github.com/graysam/bootbox-mcu-fw.wiki.git` when the public wiki is enabled.

## Hardware Connections

| Function | ESP32 DevKit (WROOM) | ESP32-S3 DevKitC-1 (N16R8) | Notes |
| --- | --- | --- | --- |
| Thermistor 1 | GPIO34 (ADC1_6) | GPIO4 (ADC1_3) | 10 kΩ pull-up to 3v3, probe to ground. |
| Thermistor 2 | GPIO35 (ADC1_7) | GPIO5 (ADC1_4) | Same harness as channel 1. |
| Fan 1 PWM | GPIO25 | GPIO16 | Drives MOSFET gate (3-wire) or PWM lead (4-wire). |
| Fan 2 PWM | GPIO26 | GPIO17 | Optional; set to `-1` to disable. |
| Tach input | GPIO27 | GPIO18 (fan1), GPIO7 (fan2 optional) | 4-wire fans only; disable with `-1` if unused. |
| Status LED | GPIO2 (active-low) | `LED_BUILTIN` (RGB, active-high) | Set `STATUS_LED_PIN = -1` to disable. |
| I²C SDA/SCL | GPIO21 / GPIO22 | GPIO8 / GPIO9 | Pull-ups (2.2–4.7 kΩ) recommended near the MCU. |
| ADAU RESET | Configurable (default -1) | Configurable (default -1) | Tie to codec reset through a transistor if voltages differ. |

- Thermistors (NTC) feed the ADC inputs above; the calibration wizard (System tab) solves for nominal resistance/β and persists to NVS so you rarely have to recompile.
- Readings below -20 °C or above 120 °C are ignored automatically so the PID loop never reacts to a floating/broken probe.
- Fan control:
  - `kFanType` (3-wire vs 4-wire) lives in `config.h`. Three-wire modes enforce a configurable minimum start duty.
  - PWM frequency defaults to 25 kHz (4-wire) or 1 kHz (3-wire). Adjust `FAN_PWM_FREQ_*` if your fans prefer different carriers.
  - Tach inputs are sampled for 4-wire fans; leave pins at `-1` to disable.
- ADAU1701 control bus:
  - `PIN_I2C_SDA`/`PIN_I2C_SCL` follow the table above and can be overridden if you relocate the bus.
  - `ADAU_I2C_ADDR` (0x34) streams live parameters; `ADAU_EEPROM_I2C_ADDR` (0x50) programs the self-boot EEPROM.
  - Hook `PIN_ADAU_RESET` to the codec reset net so bundle pushes can pulse the DSP automatically.
- USB serial: CH340 (ESP32) shows up as `/dev/ttyUSB0`; the ESP32-S3 hardware CDC target usually enumerates as `/dev/ttyACM0`. Override `PORT` accordingly.

## Target Definitions
- Each board gets a `.def` manifest under `targets/`. Fields include the base FQBN, board menu overrides (flash size, PSRAM, USB mode), LittleFS/partition CSV path, flash size sanity check, and upload metadata (port, baud, optional VID/PID/serial for identity checks).
- `tools/target-build.sh --target <name> [--flash]` reads the manifest, copies the matching partition CSV into the sketch before compiling, validates that the layout fills the advertised flash size, and optionally flashes via the recorded serial port. Running the script with no arguments opens an interactive menu that lets you pick/edit definitions, adjust ports/baud/strict checks, and trigger build/flash tasks.
- Use `tools/target-build.sh --list` to see available targets, or `--def-file path/to/custom.def` to point at new hardware without touching the repo.
- Identity enforcement is optional: leave `PORT_ID_*` blank in the `.def` file or pass `--skip-identity` to bypass the `udevadm` verification when pairing a board for the first time.

## Libraries
- ESPAsyncWebServer, AsyncTCP, ESP32-A2DP (vendored), ArduinoJson, LittleFS, Preferences (Arduino core). Install via Boards Manager/Library Manager where not vendored.

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
