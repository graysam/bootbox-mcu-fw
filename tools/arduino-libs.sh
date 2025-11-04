#!/usr/bin/env bash
set -euo pipefail

# Install required Arduino libraries for this project.
# Requires: arduino-cli configured and network access.

echo "Installing libraries: ESP Async WebServer, AsyncTCP, ArduinoJson, (optional) LittleFS_esp32"
arduino-cli lib install "ESP Async WebServer" || true
arduino-cli lib install "AsyncTCP" || true
arduino-cli lib install "ArduinoJson" || true

# LittleFS for ESP32: on ESP32 core 3.x LittleFS is built-in; on older cores use this library.
arduino-cli lib install "LittleFS_esp32" || true

echo "Done."

