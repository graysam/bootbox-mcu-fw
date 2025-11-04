#!/usr/bin/env bash
set -euo pipefail

# Build the Arduino firmware using arduino-cli.
# Usage: tools/arduino-build.sh [--fqbn esp32:esp32:esp32]

FQBN="esp32:esp32:esp32"
if [[ "${1:-}" == "--fqbn" ]]; then
  FQBN="$2"; shift 2
fi

SKETCH_DIR="firmware/arduino/bootbox_mcu_fw"

echo "Building $SKETCH_DIR for $FQBN"
arduino-cli compile --fqbn "$FQBN" "$SKETCH_DIR"

