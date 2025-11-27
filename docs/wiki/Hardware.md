# Hardware

## Target Platforms
- ESP32 DevKitC / generic NodeMCU-style boards (single-core or dual-core). Any module exposing GPIO21/22 and at least two 3v3 ADC-capable pins works.
- ESP32-S3 DevKitC-1 N16R8 (16 MB flash / 8 MB PSRAM). The firmware auto-detects the target via `CONFIG_IDF_TARGET` so pin layouts swap automatically.
- USB bridges verified:
  - **CH34x** — enumerates as `/dev/ttyUSB0`; requires `ch341` driver (default on most Linux distros).
  - **CP2102** — enumerates as `/dev/ttyUSB1`; no special driver on Linux/macOS.
- Supply 5 V via USB for bench testing; the on-board regulator feeds 3v3 peripherals. For permanent installs power the `5V` pin from a regulated rail capable of >500 mA.

## Wiring Summary
| Function | ESP32 DevKit (WROOM) | ESP32-S3 DevKitC-1 N16R8 | Notes |
| --- | --- | --- | --- |
| Thermistor 1 | GPIO34 (ADC1_6) | GPIO4 (ADC1_3) | 10 kΩ pull-up to 3 V3, probe to ground. |
| Thermistor 2 | GPIO35 (ADC1_7) | GPIO5 (ADC1_4) | Calibrate via the System tab UI to store β/nominal overrides. |
| Fan 1 PWM | GPIO25 | GPIO16 | Drives MOSFET gate (3-wire) or PWM pin (4-wire). |
| Fan 2 PWM | GPIO26 | GPIO17 | Optional mirror output; set to `-1` to disable. |
| Tach inputs (4-wire) | GPIO27 | GPIO18 (fan1), GPIO7 (fan2 optional) | Tach sampling only in 4-wire mode; set pins to `-1` to disable. |
| Status LED | GPIO2 (active-low) | `LED_BUILTIN` (RGB WS2812) | S3 onboard LED is WS2812 on GPIO48; current driver does not light it. |
| I²C SDA/SCL | GPIO21 / GPIO22 | GPIO8 / GPIO9 | Add 2.2–4.7 kΩ pull-ups close to the MCU, especially with long harnesses. |
| ADAU RESET | Configurable (default -1) | Configurable (default -1) | Tie to the ADAU reset net if you want automatic self-boot pulses. |
| BT link UART | TX17 / RX16 | TX12 / RX13 | UART2 @ 921600 baud; crosses to bt2i2s. |

## ADAU1701 Notes
- The ADAU EEPROM (e.g., 24LC256) must be wired to the same I²C bus the MCU uses. Confirm `ADAU_EEPROM_I2C_ADDR` (typically `0x50`).
- Self-boot pin should remain high; the MCU reprograms the EEPROM and toggles reset so the DSP restarts using the new image.
- Keep I²C wires short and twisted with ground where possible; at 400 kHz long runs without shielding can cause repeated `hw_error` logs.

## Sensors & Calibration
- Use β curve-compatible thermistors (10 kΩ or 100 kΩ) with matching bias resistors. The two-point calibration workflow compensates for wiring tolerances.
- Firmware drops thermistor samples outside the -20 °C to 120 °C window so a loose probe never drives the PID loop into an extreme state.
- For higher-voltage fans, place a logic MOSFET + flyback diode between PWM pin and load; ensure the gate reference is the MCU ground.

## Enclosure / EMI
- Keep the ESP32 antenna clear of large copper or nearby wiring harnesses; aim for a 5 mm clearance around the PCB antenna region.
- If mounting near class-D amplifier modules, add a grounded shield or place the MCU in a separate compartment to reduce RF coupling into thermistor traces.

## Not implemented / limitations
- S3 onboard RGB LED (GPIO48) is not driven by the current StatusLed helper; use an external LED or add WS2812 support if needed.
- ADAU reset is disabled by default (`PIN_ADAU_RESET = -1`); wire and enable if you require automatic self-boot toggling.

## TODO
- Publish final harness drawings (JST pinouts, wire colors, in-line fuses).
- Document tested ADAU daughterboards and any required pull-ups/level shifting.
