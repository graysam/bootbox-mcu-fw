#!/usr/bin/env bash
set -euo pipefail

# Open serial monitor at 115200 baud.
# Usage: PORT=/dev/ttyUSB0 tools/arduino-monitor.sh

if [[ -z "${PORT:-}" ]]; then
  echo "Set PORT (e.g., PORT=/dev/ttyUSB0)" >&2
  exit 1
fi

arduino-cli monitor -p "$PORT" --config baudrate=115200
