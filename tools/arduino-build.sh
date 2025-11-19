#!/usr/bin/env bash
set -euo pipefail

# Comprehensive firmware + filesystem build helper.
# - Compiles the sketch with arduino-cli (respecting an optional config file).
# - Packages the LittleFS data directory into a binary sized to match the active partition table.
# - Produces a flash manifest and a merged "unified" image for esptool.py.
#
# Usage:
#   tools/arduino-build.sh [--fqbn esp32:esp32:esp32] [--build-path .arduino-build]
#                         [--config-file arduino-cli.yaml] [--skip-merge]
#
# Environment:
#   ARDUINO_CLI_CONFIG  - optional default config file (overridden by --config-file).
#   ARDUINO_CLI         - override the arduino-cli executable (defaults to "arduino-cli").

FQBN="esp32:esp32:esp32"
BUILD_DIR=".arduino-build"
CONFIG_ARG=()
SKIP_MERGE=0
SKIP_SPIFFS=0
CLI_BIN=${ARDUINO_CLI:-arduino-cli}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --fqbn)
      FQBN="$2"; shift 2 ;;
    --build-path)
      BUILD_DIR="$2"; shift 2 ;;
    --config-file)
      CONFIG_ARG=(--config-file "$2"); shift 2 ;;
    --skip-merge)
      SKIP_MERGE=1; shift ;;
    --skip-spiffs)
      SKIP_SPIFFS=1; shift ;;
    --help|-h)
      echo "Usage: $0 [--fqbn <fqbn>] [--build-path <dir>] [--config-file <file>] [--skip-merge] [--skip-spiffs]" >&2
      exit 0 ;;
    *)
      echo "Unknown argument: $1" >&2
      exit 1 ;;
  esac
done

if [[ ${#CONFIG_ARG[@]} -eq 0 && -n ${ARDUINO_CLI_CONFIG:-} ]]; then
  CONFIG_ARG=(--config-file "$ARDUINO_CLI_CONFIG")
fi

SKETCH_DIR="firmware/arduino/bootbox_mcu_fw"
DATA_DIR="$SKETCH_DIR/data"
SKETCH_BASENAME="$(basename "$SKETCH_DIR")"

mkdir -p "$BUILD_DIR"

CLI_CMD=("$CLI_BIN")
if [[ ${#CONFIG_ARG[@]} -gt 0 ]]; then
  CLI_CMD+=("${CONFIG_ARG[@]}")
fi

echo "Building $SKETCH_DIR for $FQBN (build dir: $BUILD_DIR)"
"${CLI_CMD[@]}" compile --fqbn "$FQBN" --build-path "$BUILD_DIR" --export-binaries "$SKETCH_DIR"

BUILD_OPTIONS="$BUILD_DIR/build.options.json"
PARTITIONS_CSV="$BUILD_DIR/partitions.csv"
SDKCONFIG="$BUILD_DIR/sdkconfig"

if [[ ! -f "$BUILD_OPTIONS" ]]; then
  echo "error: build.options.json not found in $BUILD_DIR (arduino-cli export failed?)" >&2
  exit 1
fi

if [[ ! -f "$PARTITIONS_CSV" ]]; then
  echo "error: partitions.csv not found in $BUILD_DIR. Unable to derive SPIFFS layout." >&2
  exit 1
fi

if [[ ! -f "$SDKCONFIG" ]]; then
  echo "error: sdkconfig not found in $BUILD_DIR. Unable to derive flash parameters." >&2
  exit 1
fi

export BUILD_DIR DATA_DIR BUILD_OPTIONS PARTITIONS_CSV SDKCONFIG
PY_INFO=$(python3 <<'PY'
import csv, glob, json, os, shlex, sys

build_dir = os.environ["BUILD_DIR"]
data_dir = os.environ["DATA_DIR"]
build_options_path = os.environ["BUILD_OPTIONS"]
partitions_csv = os.environ["PARTITIONS_CSV"]
sdkconfig_path = os.environ["SDKCONFIG"]

def to_paths(raw):
    raw = raw.strip()
    if not raw:
        return []
    return [os.path.expanduser(p.strip()) for p in raw.split(os.pathsep) if p.strip()]

with open(build_options_path, "r", encoding="utf-8") as f:
    opts = json.load(f)

hardware_folders = to_paths(opts.get("hardwareFolders", ""))
other_candidates = [
    os.path.expanduser("~/.arduino15/packages"),
    os.path.expanduser("~/Library/Arduino15/packages"),
    os.path.expanduser("~/AppData/Local/Arduino15/packages"),
]

def find_tool(tool_name, executable):
    if isinstance(executable, (list, tuple)):
        executables = executable
    else:
        executables = (executable,)
    search_roots = hardware_folders + other_candidates
    candidates = []
    for root in search_roots:
        if not root or not os.path.isdir(root):
            continue
        for exe in executables:
            pattern = os.path.join(root, "esp32", "tools", tool_name, "*", exe)
            candidates.extend(glob.glob(pattern))
    if not candidates:
        return None
    # use lexicographically highest (latest) version
    return sorted(candidates)[-1]

def parse_partitions(csv_path):
    spiffs_offset = spiffs_size = None
    app_offset = None
    with open(csv_path, newline="", encoding="utf-8") as f:
        reader = csv.reader(f)
        for row in reader:
            if not row or row[0].strip().startswith("#"):
                continue
            row = [col.strip() for col in row]
            if len(row) < 5:
                continue
            name, ptype, *_ = row
            offset = row[3]
            size = row[4]
            if name.lower() == "spiffs":
                spiffs_offset, spiffs_size = offset, size
            if ptype == "app" and app_offset is None:
                app_offset = offset
    if spiffs_offset is None or spiffs_size is None:
        raise SystemExit("SPIFFS partition not found in partitions.csv")
    if app_offset is None:
        raise SystemExit("Application partition not found in partitions.csv")
    return spiffs_offset, spiffs_size, app_offset

def parse_sdkconfig(path):
    values = {}
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            key, value = line.split("=", 1)
            if value.startswith('"') and value.endswith('"'):
                value = value[1:-1]
            values[key] = value
    return values

def parse_int(value):
    value = value.strip()
    if value.lower().startswith("0x"):
        return int(value, 16)
    if value.lower().endswith("kb"):
        return int(value[:-2]) * 1024
    if value.lower().endswith("mb"):
        return int(value[:-2]) * 1024 * 1024
    return int(value, 10)

spiffs_offset_str, spiffs_size_str, app_offset_str = parse_partitions(partitions_csv)
cfg = parse_sdkconfig(sdkconfig_path)

bootloader_offset = cfg.get("CONFIG_BOOTLOADER_OFFSET_IN_FLASH", "0x1000")
partition_offset = cfg.get("CONFIG_PARTITION_TABLE_OFFSET", "0x8000")
flash_mode = cfg.get("CONFIG_ESPTOOLPY_FLASHMODE", "dio")
flash_freq = cfg.get("CONFIG_ESPTOOLPY_FLASHFREQ", "80m")
flash_size = cfg.get("CONFIG_ESPTOOLPY_FLASHSIZE", "4MB")
chip_target = cfg.get("CONFIG_IDF_TARGET", "esp32") or "esp32"
chip_target = chip_target.lower()

data = {
    "SPIFFS_OFFSET": f"0x{parse_int(spiffs_offset_str):X}",
    "SPIFFS_SIZE": str(parse_int(spiffs_size_str)),
    "SPIFFS_SIZE_HEX": f"0x{parse_int(spiffs_size_str):X}",
    "APP_OFFSET": f"0x{parse_int(app_offset_str):X}",
    "BOOTLOADER_OFFSET": f"0x{parse_int(bootloader_offset):X}",
    "PARTITION_OFFSET": f"0x{parse_int(partition_offset):X}",
    "FLASH_MODE": flash_mode,
    "FLASH_FREQ": flash_freq,
    "FLASH_SIZE": flash_size,
    "MKLITTLEFS": find_tool("mklittlefs", "mklittlefs"),
    "ESPTOOL": find_tool("esptool_py", ("esptool.py", "esptool")),
    "CHIP_TARGET": chip_target,
}

for key, value in data.items():
    if value is None:
        print(f"{key}=", end="")
    else:
        if isinstance(value, str):
            value = shlex.quote(value)
        print(f"{key}={value}", end="")
    print()
PY
)

eval "$PY_INFO"

export FLASH_MODE FLASH_FREQ FLASH_SIZE BOOTLOADER_OFFSET PARTITION_OFFSET APP_OFFSET SPIFFS_OFFSET SPIFFS_SIZE ESPTOOL CHIP_TARGET

BOOT_BIN="$BUILD_DIR/${SKETCH_BASENAME}.ino.bootloader.bin"
APP_BIN="$BUILD_DIR/${SKETCH_BASENAME}.ino.bin"
PARTITION_BIN="$BUILD_DIR/${SKETCH_BASENAME}.ino.partitions.bin"
SPIFFS_BIN="$BUILD_DIR/${SKETCH_BASENAME}.spiffs.bin"
UNIFIED_BIN="$BUILD_DIR/${SKETCH_BASENAME}.unified.bin"
MANIFEST_JSON="$BUILD_DIR/flash-manifest.json"

if [[ -z ${ESPTOOL:-} ]]; then
  echo "error: esptool executable not found (esp32 core not installed?)." >&2
  exit 1
fi

if [[ "${ESPTOOL##*.}" == "py" ]]; then
  ESPTOOL_INVOKER="python3"
  MERGE_RUNNER=(python3 "$ESPTOOL")
else
  ESPTOOL_INVOKER=""
  MERGE_RUNNER=("$ESPTOOL")
fi

export ESPTOOL_INVOKER

for required in "$BOOT_BIN" "$APP_BIN" "$PARTITION_BIN"; do
  if [[ ! -f "$required" ]]; then
    echo "error: expected build artifact missing: $required" >&2
    exit 1
  fi
done

if [[ $SKIP_SPIFFS -eq 0 ]]; then
  if [[ -z ${MKLITTLEFS:-} || ! -x ${MKLITTLEFS:-/nonexistent} ]]; then
    echo "error: mklittlefs tool not found (esp32 core not installed?)." >&2
    exit 1
  fi
  if [[ ! -d "$DATA_DIR" ]]; then
    echo "error: LittleFS data directory not found at $DATA_DIR" >&2
    exit 1
  fi
  echo "Packing LittleFS image (${SPIFFS_SIZE} bytes) -> $SPIFFS_BIN"
  "$MKLITTLEFS" -c "$DATA_DIR" -b 4096 -p 256 -s "$SPIFFS_SIZE" "$SPIFFS_BIN"
else
  echo "Skipping LittleFS packaging (--skip-spiffs)"
fi

python3 - "$MANIFEST_JSON" "$BOOT_BIN" "$PARTITION_BIN" "$APP_BIN" "${SPIFFS_BIN:-}" <<'PY'
import json, os, sys

out_path, boot_bin, part_bin, app_bin, spiffs_bin = sys.argv[1:6]
manifest = {
    "flash_mode": os.environ["FLASH_MODE"],
    "flash_freq": os.environ["FLASH_FREQ"],
    "flash_size": os.environ["FLASH_SIZE"],
    "chip": os.environ.get("CHIP_TARGET", "esp32"),
    "esptool": os.environ.get("ESPTOOL", ""),
    "esptool_invoker": os.environ.get("ESPTOOL_INVOKER", ""),
    "segments": [
        {"offset": os.environ["BOOTLOADER_OFFSET"], "file": os.path.abspath(boot_bin)},
        {"offset": os.environ["PARTITION_OFFSET"], "file": os.path.abspath(part_bin)},
        {"offset": os.environ["APP_OFFSET"], "file": os.path.abspath(app_bin)},
    ],
}
if spiffs_bin and os.path.isfile(spiffs_bin):
    manifest["segments"].append({"offset": os.environ["SPIFFS_OFFSET"], "file": os.path.abspath(spiffs_bin)})

with open(out_path, "w", encoding="utf-8") as fh:
    json.dump(manifest, fh, indent=2)

print(f"Wrote flash manifest -> {out_path}")
PY

if [[ $SKIP_MERGE -eq 0 ]]; then
  echo "Generating unified flash image -> $UNIFIED_BIN"
  MERGE_CMD=("${MERGE_RUNNER[@]}" --chip "${CHIP_TARGET:-esp32}" merge-bin -o "$UNIFIED_BIN"
             --flash-mode "$FLASH_MODE" --flash-freq "$FLASH_FREQ" --flash-size "$FLASH_SIZE"
             "$BOOTLOADER_OFFSET" "$BOOT_BIN"
             "$PARTITION_OFFSET" "$PARTITION_BIN"
             "$APP_OFFSET" "$APP_BIN")
  if [[ -f "$SPIFFS_BIN" ]]; then
    MERGE_CMD+=("$SPIFFS_OFFSET" "$SPIFFS_BIN")
  fi
  "${MERGE_CMD[@]}"
else
  echo "Skipping unified merge (--skip-merge)"
fi

echo
echo "Artifacts written to $BUILD_DIR:"
ls -1 "$BUILD_DIR" | sed 's/^/  - /'
