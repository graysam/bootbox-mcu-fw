# Firmware

## Overview
The Arduino sketch (`firmware/arduino/bootbox_mcu_fw`) runs the entire controller:
- Hosts the HTTP API + WebSocket bridge on top of ESPAsyncWebServer.
- Serves the SPA from LittleFS and keeps the file system in sync with bundle uploads.
- Drives thermal control (dual-channel thermistors, PID/manual fan loop).
- Manages DSP bundles, presets, and ADAU1701 I²C traffic.
- Persists operator settings, calibration data, and DSP values using Preferences (NVS).

## Architecture Highlights
| Module | Responsibility |
| --- | --- |
| `config.h` | Compile-time knobs: pin map, fan type, LED polarity, thermistor defaults, ADAU I²C addresses. |
| `bootbox_mcu_fw.ino` | Main loop, state struct, PID/fan control, ADAU manager, REST/WS handlers, status LED scheduler. |
| `settings.h` | Structures + helpers for loading/saving setpoints, PID gains, manual fan %, and thermistor calibration parameters. |
| `/data/` | Web assets that get packed into LittleFS each build. |

### Runtime Flow
1. `setup()` initializes LittleFS, Preferences, Wi-Fi AP, Wire/I²C, LEDs, and Async services.
2. A 1 Hz task polls sensors (thermistors, fan RPM placeholder), runs the PID controller, and updates PWM outputs.
3. WebSocket messages (`set_mode`, `set_manual_pct`, `set_therm`, `set_dsp`, etc.) tweak the in-memory state and mark dirty flags so NVS flushes after a debounce window.
4. DSP control values persist independently; once `dsp_values_dirty` is set, they flush after `DSP_SAVE_DELAY_MS` to avoid NAND thrash.
5. Status LED patterns run from a lightweight scheduler so no blocking `delay()` usage is required for fault codes.

### ADAU Manager
- Uses `Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL)` at `I2C_FREQUENCY_HZ` (400 kHz default).
- `pushBundleToDsp()` streams `program.bin` into the external EEPROM (`ADAU_EEPROM_I2C_ADDR`) page by page, waiting 5 ms between writes for t_WR, then pulses `PIN_ADAU_RESET`.
- Live parameter writes go through `applyDspValueToHardware` → `adauWriteRegister`, which handles format/byte width conversion (5.23 fixed-point, unsigned ints, raw).
- Hardware status is tracked through `adau_ready`/`adau_last_error` and bubbled up to `/api/dsp/schema` + WebSocket frames.

### Persistence Layout
- Preferences namespace `bootbox` stores:
  - `settings` struct (PID enable/setpoints/gains + manual fan %).
  - Thermistor calibration structs per channel (nominal resistance, beta, solved flag).
  - `dspBundle` (active bundle name) + the serialized value map for all controls.
- `dsp/` and `dsp-presets/` are on LittleFS so bundles/presets survive firmware updates.

## Development Notes
- Keep ADC + thermistor math in `adcToTempC()`; when adding new probes, update `config.h` defaults and the calibration table.
- Watch heap usage via `/api/state.sys.free_heap`; enabling lots of DSP controls increases JSON document sizes—bump `StaticJsonDocument` capacities accordingly.
- For new status LED patterns, edit the `StatusCode` enum + `applyStatusPattern()` helper so they remain centralized.
