#!/usr/bin/env bash
set -euo pipefail

# Upload firmware + LittleFS image using the artifacts produced by tools/arduino-build.sh.
# The helper will rebuild by default (unless --no-build is supplied), then flash all segments
# in a single esptool.py invocation based on the generated flash-manifest.json.
#
# Usage:
#   PORT=/dev/ttyUSB0 tools/arduino-upload.sh [--fqbn esp32:esp32:esp32]
#                                          [--build-path .arduino-build]
#                                          [--config-file arduino-cli.yaml]
#                                          [--baud 921600] [--no-build]

if [[ -z "${PORT:-}" ]]; then
  echo "Set PORT (e.g., PORT=/dev/ttyUSB0)" >&2
  exit 1
fi

FQBN="esp32:esp32:esp32"
BUILD_DIR=".arduino-build"
CONFIG_ARG=()
BAUD="${BAUD:-921600}"
RUN_BUILD=1

while [[ $# -gt 0 ]]; do
  case "$1" in
    --fqbn)
      FQBN="$2"; shift 2 ;;
    --build-path)
      BUILD_DIR="$2"; shift 2 ;;
    --config-file)
      CONFIG_ARG=(--config-file "$2"); shift 2 ;;
    --baud)
      BAUD="$2"; shift 2 ;;
    --no-build)
      RUN_BUILD=0; shift ;;
    --help|-h)
      echo "Usage: PORT=/dev/ttyUSB0 $0 [--fqbn <fqbn>] [--build-path <dir>] [--config-file <file>] [--baud <rate>] [--no-build]" >&2
      exit 0 ;;
    *)
      echo "Unknown argument: $1" >&2
      exit 1 ;;
  esac
done

if [[ ${#CONFIG_ARG[@]} -eq 0 && -n ${ARDUINO_CLI_CONFIG:-} ]]; then
  CONFIG_ARG=(--config-file "$ARDUINO_CLI_CONFIG")
fi

if [[ $RUN_BUILD -eq 1 ]]; then
  BUILD_ARGS=(--fqbn "$FQBN" --build-path "$BUILD_DIR")
  if [[ ${#CONFIG_ARG[@]} -gt 0 ]]; then
    BUILD_ARGS+=("${CONFIG_ARG[@]}")
  fi
  echo "Running tools/arduino-build.sh ${BUILD_ARGS[*]}"
  "$(dirname "$0")/arduino-build.sh" "${BUILD_ARGS[@]}"
fi

MANIFEST="$BUILD_DIR/flash-manifest.json"

if [[ ! -f "$MANIFEST" ]]; then
  echo "error: flash manifest not found at $MANIFEST. Run tools/arduino-build.sh first." >&2
  exit 1
fi

export MANIFEST PORT BAUD

python3 <<'PY'
import json
import os
import subprocess
import sys

manifest_path = os.environ["MANIFEST"]
port = os.environ["PORT"]
baud = os.environ.get("BAUD", "")

with open(manifest_path, "r", encoding="utf-8") as f:
    manifest = json.load(f)

esptool_path = manifest.get("esptool")
if not esptool_path:
    raise SystemExit("esptool path missing from manifest. Re-run tools/arduino-build.sh to regenerate.")
if not os.path.isfile(esptool_path):
    raise SystemExit(f"esptool executable not found at recorded path: {esptool_path}")

invoker = manifest.get("esptool_invoker") or ""
if invoker:
    cmd = [invoker, esptool_path]
else:
    cmd = [esptool_path]
cmd += ["--chip", "esp32", "--port", port]
if baud:
    cmd += ["--baud", baud]
cmd.append("write_flash" if invoker else "write-flash")
cmd += [
    "--flash_mode" if invoker else "--flash-mode", manifest["flash_mode"],
    "--flash_freq" if invoker else "--flash-freq", manifest["flash_freq"],
    "--flash_size" if invoker else "--flash-size", manifest["flash_size"],
]

for segment in manifest["segments"]:
    cmd += [segment["offset"], segment["file"]]

print("Flashing via:", " ".join(cmd), flush=True)
subprocess.check_call(cmd)
PY

echo "Upload complete."
