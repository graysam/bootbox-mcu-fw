# BOOTBOX DSP MCU Wiki

Welcome to the project knowledge base. Everything you need to build, flash, configure, and operate the controller lives here.

## Reference Sections
- [[Hardware]] — harness pinout, sensor/fan wiring, ADAU bus notes.
- [[Firmware]] — architectural overview, subsystems, and compile-time switches.
- [[Web-UI]] — tab-by-tab behavior, WebSocket contract, and UX guidelines.
- [[DSP-Control]] — bundle format, interface schema, I²C transport details.
- [[bt2i2s]] — Bluetooth bridge firmware, wiring, and I2S/AVRCP specifics.
- [[Link-Protocol]] — Bootbox↔bt2i2s UART protocol, frame formats, and compatibility rules.
- [[Debugging]] — serial logs, debug flags, file formats, and diagnostics.
- [[Build-and-Flash]] — toolchain setup and flashing workflows.
- [[Release-Process]] — tagging, packaging, and publishing artifacts.
- [[Troubleshooting]] — quick fixes for common faults.

## How-To Guides
- [[DSP-Bundle-How-To]] — export SigmaStudio binaries, craft interface XML, upload/push, and verify control sync.
- [[Thermistor-Calibration]] — two-point calibration workflow plus tips for reliable ADC sampling.

Each page is a living spec; add diagrams, measurements, and test logs as work continues.
