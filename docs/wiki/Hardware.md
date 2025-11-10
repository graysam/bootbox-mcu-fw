# Hardware

## Target Platforms
- ESP32 DevKitC / generic NodeMCU-style boards (single-core or dual-core). Any module exposing GPIO21/22 and at least two 3v3 ADC-capable pins works.
- USB bridges verified:
  - **CH34x** — enumerates as `/dev/ttyUSB0`; requires `ch341` driver (default on most Linux distros).
  - **CP2102** — enumerates as `/dev/ttyUSB1`; no special driver on Linux/macOS.
- Supply 5 V via USB for bench testing; the on-board regulator feeds 3v3 peripherals. For permanent installs power the `5V` pin from a regulated rail capable of >500 mA.

## Wiring Summary
| Function | Default Pin(s) | Notes |
| --- | --- | --- |
| Thermistor 1 | GPIO34 | 10 kΩ pull-up to 3v3, probe to ground. |
| Thermistor 2 | GPIO35 | Same topology as channel 1; both feed into the calibration workflow. |
| Fan 1 PWM | GPIO25 | For 3-wire setups this drives a MOSFET gate; for 4-wire it connects to the PWM control lead. |
| Fan 2 PWM | GPIO26 | Optional second fan; disable by setting `PIN_FAN2_CTRL = -1`. |
| Tach inputs | GPIO27 / custom | Currently reserved; future RPM capture. Leave floating or tie to tach outputs via level shifter. |
| Status LED | GPIO2 (active-low) | Override via `STATUS_LED_PIN` / `STATUS_LED_ACTIVE_HIGH` in `config.h`. |
| I²C SDA/SCL | GPIO21 / GPIO22 | Pull-ups (2.2–4.7 kΩ) required near the MCU when cabling exceeds a few inches. |
| ADAU RESET | Configurable | Wire to the codec’s /RESET via a transistor if voltage domains differ. Optional but recommended for reliable self-boot pushes. |

## ADAU1701 Notes
- The ADAU EEPROM (e.g., 24LC256) must be wired to the same I²C bus the MCU uses. Confirm `ADAU_EEPROM_I2C_ADDR` (typically `0x50`).
- Self-boot pin should remain high; the MCU reprograms the EEPROM and toggles reset so the DSP restarts using the new image.
- Keep I²C wires short and twisted with ground where possible; at 400 kHz long runs without shielding can cause repeated `hw_error` logs.

## Sensors & Calibration
- Use β curve-compatible thermistors (10 kΩ or 100 kΩ) with matching bias resistors. The two-point calibration workflow compensates for wiring tolerances.
- For higher-voltage fans, place a logic MOSFET + flyback diode between PWM pin and load; ensure the gate reference is the MCU ground.

## Enclosure / EMI
- Keep the ESP32 antenna clear of large copper or nearby wiring harnesses; aim for a 5 mm clearance around the PCB antenna region.
- If mounting near class-D amplifier modules, add a grounded shield or place the MCU in a separate compartment to reduce RF coupling into thermistor traces.

## TODO
- Publish final harness drawings (JST pinouts, wire colors, in-line fuses).
- Document tested ADAU daughterboards and any required pull-ups/level shifting.
