# Debugging & Logs

## Serial consoles
- Bootbox (ESP32-S3 DevKitC-1): `/dev/ttyACM0`, 115200 baud (USB CDC).
- bt2i2s (ESP32 WROOM DevKitC): `/dev/ttyUSB0`, 115200 baud (USB-UART bridge).
- UART link between boards: 921600 baud on UART2 (see `common/link_config.h`).

## Verbose logging flags
- Bootbox:
  - `THERMISTOR_DEBUG_LOG` (config.h): print periodic therm readings.
  - `[bt-link]` logs always emitted for link events/errors.
- bt2i2s:
  - `DEBUG_LOG_STATE` (config.h): prints link TX/RX frames and state transitions.

## File formats & upload surfaces
- DSP bundle: `/dsp/<bundle>/program.bin` (binary) + `interface.xml` (schema). Upload via `/api/upload/adau` or UI.
- Presets: `/dsp-presets/<bundle>/*.json` storing control values.
- LittleFS UI assets: `firmware/arduino/bootbox_mcu_fw/data/` are auto-packed by build scripts.

## Serial log capture
Use `arduino-cli monitor -p /dev/ttyACM0 -b 115200` for Bootbox and `-p /dev/ttyUSB0` for bt2i2s. Capture both in parallel when diagnosing link issues.

## Basic diag flow
1) Power both boards; watch for `hello` / `bt_state` on Bootbox serial.
2) From the UI, send `Resync` in the Bluetooth card; confirm acks in the console.
3) If link offline: verify UART pins/cross, baud = 921600, and no other device on UART2.
4) For audio issues: confirm I2S pins and master/slave mode, check sample rate in `bt_state`.

## Not implemented / known gaps
- Track position telemetry (AVRCP play position) not surfaced.
- S3 onboard RGB LED not driven by StatusLed; use external LED if needed.
- ADAU reset pin is disabled by default; self-boot pulses require wiring and enabling `PIN_ADAU_RESET`.
