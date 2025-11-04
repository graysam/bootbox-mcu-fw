#!/usr/bin/env bash
set -euo pipefail

# Upload the built sketch to the device.
# Usage: PORT=/dev/ttyUSB0 tools/arduino-upload.sh [--fqbn esp32:esp32:esp32]

if [[ -z "${PORT:-}" ]]; then
  echo "Set PORT (e.g., PORT=/dev/ttyUSB0)" >&2
  exit 1
fi

FQBN="esp32:esp32:esp32"
if [[ "${1:-}" == "--fqbn" ]]; then
  FQBN="$2"; shift 2
fi

SKETCH_DIR="firmware/arduino/bootbox_mcu_fw"

echo "Uploading $SKETCH_DIR to $PORT (board: $FQBN)"
arduino-cli upload -p "$PORT" --fqbn "$FQBN" "$SKETCH_DIR"

