# Hardware

## Targets
- DevKit-style Wi-Fi MCU modules with on-board regulators.
- USB bridges tested: CH34x, CP2102 (adjust `PORT=/dev/ttyUSBx`).

## Pin Map
- Thermistors → GPIO34 / GPIO35 (10 kΩ divider by default).
- Fan PWM → configurable in `config.h` (supports dual-channel 3-wire/4-wire setups).
- Status LED → `STATUS_LED_PIN` (defaults to GPIO2, active-low).
- Reserved for ADAU1701 control (I2C on GPIO21/22).

## TODO
- Document final harness for production wiring.
- Add diagrams for fan MOSFET stage + ADAU1701 connections.
