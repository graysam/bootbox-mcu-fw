# Build and Upload (Arduino CLI)

This project uses the Arduino-ESP32 core with Async Web Server + WebSocket UI. These steps bring a fresh machine to build/upload readiness.

## Prereqs
- Install Arduino CLI and ESP32 core (per Arduino docs).
- Ensure the ESP32 board FQBN (default `esp32:esp32:esp32`) is installed.

## Install Libraries

Run:

```
tools/arduino-libs.sh
```

Installs:
- ESP Async WebServer
- AsyncTCP
- ArduinoJson
- LittleFS_esp32 (only needed for older ESP32 cores; 3.x has LittleFS built-in)

## Build

```
tools/arduino-build.sh --fqbn esp32:esp32:esp32
```

## Upload

```
PORT=/dev/ttyUSB0 tools/arduino-upload.sh --fqbn esp32:esp32:esp32
```

Then open the serial monitor to watch logs:

```
PORT=/dev/ttyUSB0 tools/arduino-monitor.sh
```

## First Boot
- ESP32 starts open AP `BOOTBOXDSP` (no password).
- Browse to `http://192.168.4.1/` to load the UI.

## Notes
- If LittleFS mount fails, check the Partition Scheme and ensure a LittleFS (or SPIFFS on older cores) partition is available.
- If build errors mention missing libraries, re-run `tools/arduino-libs.sh` or install via Arduino IDE Library Manager.
