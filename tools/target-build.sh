#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT"

usage() {
  cat <<'EOF'
Usage: tools/target-build.sh [--target <name> | --def-file <path>] [options]

Options:
  --target NAME        Load targets/NAME.def (default directory: targets/)
  --def-file PATH      Use an explicit definition file instead of --target
  --defs-dir DIR       Override the directory that holds *.def files
  --flash              Build and flash (upload) the selected target
  --flash-only         Flash using existing artifacts (skip build)
  --build-only         Only build (default behaviour)
  --skip-identity      Skip udev identity checks before flashing
  --list               List available target definition files
  -h, --help           Display this help

Invoking the script without a valid target definition launches an interactive
menu that guides you through selecting/editing definitions, tweaking upload
settings, and running build/flash tasks.
EOF
}

DEF_DIR_REL="targets"
DEF_FILE=""
TARGET_KEY=""
DO_BUILD=1
DO_UPLOAD=0
LIST_ONLY=0
STRICT_MODE=1
INTERACTIVE_MODE=0
SESSION_UPLOAD_PORT=""
SESSION_UPLOAD_BAUD=""
CURRENT_DEF_PATH=""
CURRENT_DEF_NAME=""
SKIP_ID_CHECK=0
LAST_STATUS=""

cls() {
  if command -v clear >/dev/null 2>&1; then
    clear
  else
    printf '\033c'
  fi
}

pause_for_user() {
  read -rp "Press Enter to return to the menu..." _junk || true
}

set_status() {
  LAST_STATUS="$1"
}

fail_or_return() {
  local message="$1"
  echo "$message" >&2
  if [[ "$INTERACTIVE_MODE" -eq 1 ]]; then
    set_status "$message"
    pause_for_user
    return 1
  else
    exit 1
  fi
}

# Definition-derived globals
TARGET_ID=""
TARGET_NAME=""
FQBN=""
FQBN_OPTIONS=""
FQBN_FULL=""
PARTITIONS_FILE=""
PARTITIONS_PATH=""
FLASH_SIZE_BYTES=0
BUILD_DIR_ARG=""
DEF_UPLOAD_PORT=""
DEF_UPLOAD_BAUD=""
PORT_ID_VENDOR=""
PORT_ID_MODEL=""
PORT_ID_SERIAL=""

parse_args() {
  while [[ $# -gt 0 ]]; do
    case "$1" in
      --target)
        TARGET_KEY="${2:-}"
        shift 2
        ;;
      --def-file)
        DEF_FILE="${2:-}"
        shift 2
        ;;
      --defs-dir)
        DEF_DIR_REL="${2:-targets}"
        shift 2
        ;;
      --flash)
        DO_UPLOAD=1
        shift
        ;;
      --flash-only|--upload-only)
        DO_BUILD=0
        DO_UPLOAD=1
        shift
        ;;
      --build-only)
        DO_BUILD=1
        DO_UPLOAD=0
        shift
        ;;
      --skip-identity)
        SKIP_ID_CHECK=1
        STRICT_MODE=0
        shift
        ;;
      --list)
        LIST_ONLY=1
        shift
        ;;
      --help|-h)
        usage
        exit 0
        ;;
      *)
        echo "error: unknown argument '$1'" >&2
        usage
        exit 1
        ;;
    esac
  done
}

ensure_targets_dir() {
  local dir="$1"
  if [[ ! -d "$dir" ]]; then
    mkdir -p "$dir"
    chmod 755 "$dir"
  fi
}

list_def_files() {
  local dir="$1"
  shopt -s nullglob
  local files=("$dir"/*.def)
  shopt -u nullglob
  printf '%s\n' "${files[@]}"
}

print_def_list() {
  local dir="$1"
  local idx=1
  mapfile -t defs < <(list_def_files "$dir")
  if [[ ${#defs[@]} -eq 0 ]]; then
    echo "  (no definition files found in $dir)"
    return 1
  fi
  for def in "${defs[@]}"; do
    local key name
    key="$(basename "$def" .def)"
    name="$(awk -F= '/^TARGET_NAME=/{print substr($0, index($0,$2))}' "$def" | sed 's/^"//;s/"$//')"
    printf '  %2d) %s%s\n' "$idx" "$key" "${name:+  ($name)}"
    idx=$((idx + 1))
  done
  return 0
}

prompt() {
  local message="$1"
  read -rp "$message" __ans || true
  printf '%s' "$__ans"
}

confirm() {
  local message="$1"
  read -rp "$message [y/N]: " ans || true
  [[ "$ans" =~ ^[Yy]([Ee][Ss])?$ ]]
}

select_def_interactive() {
  local dir="$1"
  while true; do
    cls
    echo "Available target definitions:"
    if ! print_def_list "$dir"; then
      echo "Create or duplicate a .def file via the definition manager first."
      return 1
    fi
    read -rp "Select definition number (or press Enter to cancel): " choice || true
    [[ -z "$choice" ]] && return 1
    if [[ "$choice" =~ ^[0-9]+$ ]]; then
      mapfile -t defs < <(list_def_files "$dir")
      local idx=$((choice - 1))
      if (( idx >=0 && idx < ${#defs[@]} )); then
        DEF_FILE="${defs[$idx]}"
        CURRENT_DEF_PATH="$DEF_FILE"
        CURRENT_DEF_NAME="$(basename "$DEF_FILE")"
        return 0
      fi
    fi
    echo "Invalid selection."
  done
}

load_definition() {
  local path="$1"
  [[ "$path" = /* ]] || path="$REPO_ROOT/$path"
  if [[ ! -f "$path" ]]; then
    echo "error: definition file not found: $path" >&2
    exit 1
  fi
  CURRENT_DEF_PATH="$path"
  CURRENT_DEF_NAME="$(basename "$path")"
  # shellcheck disable=SC1090
  source "$path"
  : "${TARGET_ID:?definition missing TARGET_ID}"
  : "${TARGET_NAME:?definition missing TARGET_NAME}"
  : "${FQBN:?definition missing FQBN}"
  : "${PARTITIONS_FILE:?definition missing PARTITIONS_FILE}"
  : "${FLASH_SIZE_BYTES:?definition missing FLASH_SIZE_BYTES}"
  FQBN_FULL="$FQBN"
  if [[ -n "${FQBN_OPTIONS:-}" ]]; then
    FQBN_FULL+=":${FQBN_OPTIONS}"
  fi
  PARTITIONS_PATH="$PARTITIONS_FILE"
  [[ "$PARTITIONS_PATH" = /* ]] || PARTITIONS_PATH="$REPO_ROOT/$PARTITIONS_PATH"
  if [[ ! -f "$PARTITIONS_PATH" ]]; then
    echo "error: partitions file not found: $PARTITIONS_PATH" >&2
    exit 1
  fi
  local build_dir_rel="${BUILD_DIR:-".arduino-build-${TARGET_ID}"}"
  BUILD_DIR_ARG="$build_dir_rel"
  DEF_UPLOAD_PORT="${UPLOAD_PORT:-}"
  DEF_UPLOAD_BAUD="${UPLOAD_BAUD:-}"
  SESSION_UPLOAD_PORT=""
  SESSION_UPLOAD_BAUD=""
}

effective_upload_port() {
  local value="${SESSION_UPLOAD_PORT:-$DEF_UPLOAD_PORT}"
  printf '%s' "$value"
}

effective_upload_baud() {
  local value="${SESSION_UPLOAD_BAUD:-$DEF_UPLOAD_BAUD}"
  printf '%s' "$value"
}

SKETCH_DIR="$REPO_ROOT/firmware/arduino/bootbox_mcu_fw"
PARTITION_DEST="$SKETCH_DIR/partitions.csv"
PARTITIONS_BACKUP=""
CLEANUP_NEEDED=0

cleanup_partitions() {
  if [[ "$CLEANUP_NEEDED" -eq 0 ]]; then
    return
  fi
  if [[ -n "$PARTITIONS_BACKUP" && -f "$PARTITIONS_BACKUP" ]]; then
    mv "$PARTITIONS_BACKUP" "$PARTITION_DEST"
  else
    rm -f "$PARTITION_DEST"
  fi
  CLEANUP_NEEDED=0
}

prepare_partitions() {
  if [[ -f "$PARTITION_DEST" ]]; then
    PARTITIONS_BACKUP="$(mktemp "${PARTITION_DEST}.XXXX")"
    cp "$PARTITION_DEST" "$PARTITIONS_BACKUP"
  else
    PARTITIONS_BACKUP=""
  fi
  cp "$PARTITIONS_PATH" "$PARTITION_DEST"
  CLEANUP_NEEDED=1
  trap cleanup_partitions EXIT
}

validate_partitions() {
  python3 - "$PARTITIONS_PATH" "$FLASH_SIZE_BYTES" <<'PY'
import csv, sys

path = sys.argv[1]
expected = int(sys.argv[2])
max_end = 0

def parse_size(value: str) -> int:
    v = value.strip().lower()
    if not v:
        return 0
    if v.startswith("0x"):
        return int(v, 16)
    if v.endswith("kb"):
        return int(v[:-2], 10) * 1024
    if v.endswith("mb"):
        return int(v[:-2], 10) * 1024 * 1024
    return int(v, 10)

with open(path, newline="", encoding="utf-8") as fh:
    reader = csv.reader(fh)
    for row in reader:
        row = [col.strip() for col in row]
        if not row or row[0].startswith("#") or len(row) < 5:
            continue
        if not row[3] or not row[4]:
            continue
        offset = parse_size(row[3])
        size = parse_size(row[4])
        max_end = max(max_end, offset + size)

if max_end != expected:
    raise SystemExit(
        f"{path} spans 0x{max_end:X} bytes but target expects 0x{expected:X}"
    )
PY
}

gather_device_props() {
  local port="$1"
  udevadm info -q property -n "$port" 2>/dev/null || return 1
}

write_updated_def() {
  local template="$1" output="$2" new_port="$3" new_vendor="$4" new_model="$5" new_serial="$6"
  python3 - "$template" "$output" "$new_port" "$new_vendor" "$new_model" "$new_serial" <<'PY'
import sys
src, dst, port, vendor, model, serial = sys.argv[1:7]
updates = {
    "UPLOAD_PORT": f'"{port}"' if port else "",
    "PORT_ID_VENDOR": f'"{vendor}"' if vendor else "",
    "PORT_ID_MODEL": f'"{model}"' if model else "",
    "PORT_ID_SERIAL": f'"{serial}"' if serial else "",
}
with open(src, "r", encoding="utf-8") as sf, open(dst, "w", encoding="utf-8") as df:
    for line in sf:
        stripped = line.strip()
        if not stripped or stripped.startswith("#") or "=" not in stripped:
            df.write(line)
            continue
        key = stripped.split("=", 1)[0].strip()
        if key in updates and updates[key] != "":
            df.write(f"{key}={updates[key]}\n")
            updates[key] = ""
        else:
            df.write(line)
    for key, value in updates.items():
        if value:
            df.write(f"{key}={value}\n")
PY
}

maybe_generate_new_def() {
  local port="$1" vendor="$2" model="$3" serial="$4"
  echo "Connected device on ${port} has VID=${vendor}, PID=${model}, Serial=${serial:-N/A}."
  if ! confirm "Generate a new .def file using ${CURRENT_DEF_NAME} as a template?"; then
    return 1
  fi
  local base default_name new_name new_path
  base="$(basename "$CURRENT_DEF_NAME" .def)"
  default_name="${base}-$(date +%Y%m%d%H%M%S)"
  while true; do
    read -rp "Enter new definition name (default ${default_name}): " new_name || true
    [[ -z "$new_name" ]] && new_name="$default_name"
    new_name="${new_name%.def}"
    new_path="$DEF_DIR_ABS/${new_name}.def"
    if [[ -e "$new_path" ]]; then
      echo "File ${new_name}.def already exists."
    else
      break
    fi
  done
  write_updated_def "$CURRENT_DEF_PATH" "$new_path" "$port" "$vendor" "$model" "$serial"
  echo "Created $new_path"
  if confirm "Use this new definition for the current session?"; then
    DEF_FILE="$new_path"
    load_definition "$DEF_FILE"
    return 0
  fi
  return 1
}

verify_port_identity() {
  [[ "$DO_UPLOAD" -eq 1 ]] || return
  [[ "$STRICT_MODE" -eq 1 ]] || return
  [[ "$SKIP_ID_CHECK" -eq 1 ]] && return
  if [[ -z "${PORT_ID_VENDOR:-}" && -z "${PORT_ID_MODEL:-}" && -z "${PORT_ID_SERIAL:-}" ]]; then
    return
  fi
  if ! command -v udevadm >/dev/null 2>&1; then
    echo "warning: udevadm not available; skipping identity check" >&2
    return
  fi
  local port props
  port="$(effective_upload_port)"
  if [[ -z "$port" ]]; then
    fail_or_return "error: no upload port defined; set one before flashing." || return 1
    return 1
  fi
  if ! props="$(gather_device_props "$port")"; then
    fail_or_return "error: unable to read udev info for ${port}" || return 1
    return 1
  fi
  check_prop() {
    local key expected
    key="$1"
    expected="$2"
    [[ -z "$expected" ]] && return 0
    if ! grep -q "^${key}=${expected}$" <<<"$props"; then
      echo "error: ${key} mismatch for ${port} (expected ${expected})" >&2
      if [[ "$INTERACTIVE_MODE" -eq 1 && "$STRICT_MODE" -eq 1 ]]; then
        local actual_vendor actual_model actual_serial
        actual_vendor="$(grep '^ID_VENDOR_ID=' <<<"$props" | head -n1 | cut -d= -f2)"
        actual_model="$(grep '^ID_MODEL_ID=' <<<"$props" | head -n1 | cut -d= -f2)"
        actual_serial="$(grep '^ID_SERIAL_SHORT=' <<<"$props" | head -n1 | cut -d= -f2)"
        if maybe_generate_new_def "$port" "$actual_vendor" "$actual_model" "$actual_serial"; then
          return 1
        fi
      fi
      fail_or_return "error: ${key} mismatch for ${port} (expected ${expected})" || return 1
      return 1
    fi
  }
  check_prop "ID_VENDOR_ID" "${PORT_ID_VENDOR:-}" || return 1
  check_prop "ID_MODEL_ID" "${PORT_ID_MODEL:-}" || return 1
  if [[ -n "${PORT_ID_SERIAL:-}" ]]; then
    if ! grep -q "^ID_SERIAL_SHORT=${PORT_ID_SERIAL}$" <<<"$props" \
       && ! grep -q "^ID_SERIAL=${PORT_ID_SERIAL}$" <<<"$props"; then
      echo "error: serial mismatch for ${port}" >&2
      if [[ "$INTERACTIVE_MODE" -eq 1 && "$STRICT_MODE" -eq 1 ]]; then
        local actual_vendor actual_model actual_serial
        actual_vendor="$(grep '^ID_VENDOR_ID=' <<<"$props" | head -n1 | cut -d= -f2)"
        actual_model="$(grep '^ID_MODEL_ID=' <<<"$props" | head -n1 | cut -d= -f2)"
        actual_serial="$(grep '^ID_SERIAL_SHORT=' <<<"$props" | head -n1 | cut -d= -f2)"
        if maybe_generate_new_def "$port" "$actual_vendor" "$actual_model" "$actual_serial"; then
          return 1
        fi
      fi
      fail_or_return "error: serial mismatch for ${port}" || return 1
      return 1
    fi
  fi
  echo "✓ Verified identity for ${port}"
}

run_actions() {
  local do_build="$1" do_upload="$2"
  [[ -n "$CURRENT_DEF_PATH" ]] || { echo "error: no definition file loaded" >&2; return 1; }
  local cli_config="${ARDUINO_CLI_CONFIG:-$REPO_ROOT/arduino-cli.yaml}"
  if [[ "$do_build" -eq 1 ]]; then
    echo "-- Preparing custom partition table from ${PARTITIONS_PATH}"
    validate_partitions
    prepare_partitions
    echo "-- Building firmware + filesystem for ${TARGET_NAME}..."
    if ! ARDUINO_CLI_CONFIG="$cli_config" "$SCRIPT_DIR/arduino-build.sh" \
        --fqbn "$FQBN_FULL" \
        --build-path "$BUILD_DIR_ARG"; then
      cleanup_partitions
      trap - EXIT
      fail_or_return "error: build failed" || return 1
      return 1
    fi
    cleanup_partitions
    trap - EXIT
  fi
  if [[ "$do_upload" -eq 1 ]]; then
    local port baud
    port="$(effective_upload_port)"
    if [[ -z "$port" ]]; then
      fail_or_return "error: definition missing UPLOAD_PORT or override for flashing" || return 1
      return 1
    fi
    if ! verify_port_identity; then
      load_definition "$CURRENT_DEF_PATH"
      return 1
    fi
    baud="$(effective_upload_baud)"
    local manifest="$BUILD_DIR_ARG/flash-manifest.json"
    if [[ ! -f "$manifest" ]]; then
      if [[ "$INTERACTIVE_MODE" -eq 1 ]]; then
        if confirm "flash-manifest.json not found. Build now?"; then
          local saved_status="$LAST_STATUS"
          if ! run_actions 1 0; then
            LAST_STATUS="$saved_status"
            return 1
          fi
          LAST_STATUS="$saved_status"
          manifest="$BUILD_DIR_ARG/flash-manifest.json"
          if [[ ! -f "$manifest" ]]; then
            fail_or_return "error: flash manifest still not found at $manifest" || return 1
            return 1
          fi
        else
          fail_or_return "error: flash manifest not found at $manifest" || return 1
          return 1
        fi
      else
        fail_or_return "error: flash manifest not found at $manifest. Run tools/arduino-build.sh first." || return 1
        return 1
      fi
    fi
    echo "-- Flashing ${TARGET_NAME} via ${port}"
    local cmd=( "$SCRIPT_DIR/arduino-upload.sh" --fqbn "$FQBN_FULL" --build-path "$BUILD_DIR_ARG" --no-build )
    if [[ -n "$baud" ]]; then
      cmd+=( --baud "$baud" )
    fi
    if ! PORT="$port" ARDUINO_CLI_CONFIG="$cli_config" "${cmd[@]}"; then
      fail_or_return "error: upload failed" || return 1
      return 1
    fi
  fi
  echo "All done."
  if [[ "$do_build" -eq 1 && "$do_upload" -eq 1 ]]; then
    set_status "✅ Build + flash completed for ${TARGET_NAME}."
  elif [[ "$do_build" -eq 1 ]]; then
    set_status "✅ Build completed for ${TARGET_NAME}."
  elif [[ "$do_upload" -eq 1 ]]; then
    set_status "✅ Flash completed for ${TARGET_NAME}."
  fi
  if [[ "$INTERACTIVE_MODE" -eq 1 ]]; then
    pause_for_user
  fi
}

connection_settings_menu() {
  cls
  echo "Connection overrides (leave blank to keep current values):"
  local current_port current_baud strict_label
  current_port="$(effective_upload_port)"
  current_baud="$(effective_upload_baud)"
  strict_label=$([[ "$STRICT_MODE" -eq 1 ]] && echo "ON" || echo "OFF")
  echo "  Current port : ${current_port:-'(from .def)'}"
  echo "  Current baud : ${current_baud:-'(from .def)'}"
  echo "  Strict check : ${strict_label}"
  local new_port new_baud
  new_port=$(prompt "Override serial device [/dev/tty... or blank]: ")
  if [[ -n "$new_port" ]]; then
    SESSION_UPLOAD_PORT="$new_port"
  fi
  new_baud=$(prompt "Override baud rate (e.g., 921600) or blank: ")
  if [[ -n "$new_baud" ]]; then
    SESSION_UPLOAD_BAUD="$new_baud"
  fi
  if confirm "Toggle strict serial validation? (currently ${strict_label})"; then
    if [[ "$STRICT_MODE" -eq 1 ]]; then
      STRICT_MODE=0
      echo "Strict validation disabled."
    else
      STRICT_MODE=1
      echo "Strict validation enabled."
    fi
  fi
  local display_port="${SESSION_UPLOAD_PORT:-${DEF_UPLOAD_PORT:-'(from .def)'}}"
  local display_baud="${SESSION_UPLOAD_BAUD:-${DEF_UPLOAD_BAUD:-'(from .def)'}}"
  local display_strict=$([[ "$STRICT_MODE" -eq 1 ]] && echo "ON" || echo "OFF")
  set_status "Overrides - Port: ${display_port}, Baud: ${display_baud}, Strict: ${display_strict}"
  pause_for_user
}

definition_manager_menu() {
  while true; do
    cls
    echo "Definition Manager (${DEF_DIR_ABS}):"
    echo "  1) List definitions"
    echo "  2) Edit definition"
    echo "  3) Duplicate definition"
    echo "  4) Delete definition"
    echo "  5) Back"
    read -rp "Select option: " dm_choice || true
    case "$dm_choice" in
      1)
        print_def_list "$DEF_DIR_ABS"
        pause_for_user
        ;;
      2)
        if select_def_interactive "$DEF_DIR_ABS"; then
          "${EDITOR:-nano}" "$DEF_FILE"
          load_definition "$DEF_FILE"
          set_status "Edited $(basename "$DEF_FILE")."
          pause_for_user
        fi
        ;;
      3)
        if select_def_interactive "$DEF_DIR_ABS"; then
          local base new_name new_path
          base="$(basename "$DEF_FILE" .def)"
          read -rp "New definition name (default ${base}-copy): " new_name || true
          [[ -z "$new_name" ]] && new_name="${base}-copy"
          new_name="${new_name%.def}"
          new_path="$DEF_DIR_ABS/${new_name}.def"
          cp "$DEF_FILE" "$new_path"
          set_status "Duplicated to ${new_name}.def"
          pause_for_user
        fi
        ;;
      4)
        if select_def_interactive "$DEF_DIR_ABS"; then
          if confirm "Delete $(basename "$DEF_FILE")?"; then
            rm -f "$DEF_FILE"
            set_status "Deleted $(basename "$DEF_FILE")."
            if [[ "$CURRENT_DEF_PATH" == "$DEF_FILE" ]]; then
              CURRENT_DEF_PATH=""
              CURRENT_DEF_NAME=""
            fi
            DEF_FILE=""
            pause_for_user
          fi
        fi
        ;;
      5|"")
        return
        ;;
      *)
        echo "Invalid choice."
        ;;
    esac
  done
}

interactive_menu() {
  INTERACTIVE_MODE=1
  ensure_targets_dir "$DEF_DIR_ABS"
  while true; do
    cls
    if [[ -n "$LAST_STATUS" ]]; then
      echo "$LAST_STATUS"
      echo
    fi
    echo "=== Target Build Helper ==="
    echo "Definition dir : $DEF_DIR_ABS"
    if [[ -n "$CURRENT_DEF_PATH" ]]; then
      echo "Current target : ${TARGET_NAME} (${CURRENT_DEF_NAME})"
    else
      echo "Current target : (none selected)"
    fi
    echo "Serial port     : $(effective_upload_port || echo '(from .def)')"
    local strict_label=$([[ "$STRICT_MODE" -eq 1 ]] && echo "ON" || echo "OFF")
    echo "Strict validation: ${strict_label}"
    echo
    cat <<'MENU'
  1) Select target definition
  2) Build firmware
  3) Flash firmware
  4) Build + Flash
  5) Connection overrides
  6) Definition manager
  7) Quit
MENU
    read -rp "Choose an option: " choice || true
    case "$choice" in
      1)
        if select_def_interactive "$DEF_DIR_ABS"; then
          load_definition "$DEF_FILE"
          set_status "Loaded $(basename "$DEF_FILE")."
          pause_for_user
        fi
        ;;
      2)
        if [[ -n "$CURRENT_DEF_PATH" ]]; then
          run_actions 1 0
        else
          set_status "Select a definition first."
          pause_for_user
        fi
        ;;
      3)
        if [[ -n "$CURRENT_DEF_PATH" ]]; then
          run_actions 0 1
        else
          set_status "Select a definition first."
          pause_for_user
        fi
        ;;
      4)
        if [[ -n "$CURRENT_DEF_PATH" ]]; then
          run_actions 1 1
        else
          set_status "Select a definition first."
          pause_for_user
        fi
        ;;
      5)
        connection_settings_menu
        ;;
      6)
        definition_manager_menu
        ;;
      7|"")
        echo "Goodbye."
        exit 0
        ;;
      *)
        echo "Invalid selection."
        ;;
    esac
  done
}

parse_args "$@"

DEF_DIR_ABS="$DEF_DIR_REL"
[[ "$DEF_DIR_ABS" = /* ]] || DEF_DIR_ABS="$REPO_ROOT/$DEF_DIR_ABS"

if [[ "$LIST_ONLY" -eq 1 ]]; then
  ensure_targets_dir "$DEF_DIR_ABS"
  echo "Available targets under $DEF_DIR_ABS:"
  print_def_list "$DEF_DIR_ABS" || true
  exit 0
fi

if [[ -z "$TARGET_KEY" && -z "$DEF_FILE" ]]; then
  interactive_menu
  exit 0
fi

if [[ "$DO_BUILD" -eq 0 && "$DO_UPLOAD" -eq 0 ]]; then
  echo "error: nothing to do (enable build and/or flash)" >&2
  usage
  exit 1
fi

if [[ -n "$TARGET_KEY" && -n "$DEF_FILE" ]]; then
  echo "error: specify either --target or --def-file (not both)" >&2
  exit 1
fi

ensure_targets_dir "$DEF_DIR_ABS"

if [[ -z "$DEF_FILE" ]]; then
  DEF_FILE="$DEF_DIR_ABS/${TARGET_KEY}.def"
else
  [[ "$DEF_FILE" = /* ]] || DEF_FILE="$REPO_ROOT/$DEF_FILE"
fi

if [[ ! -f "$DEF_FILE" ]]; then
  echo "error: definition file not found: $DEF_FILE" >&2
  exit 1
fi

CURRENT_DEF_PATH="$DEF_FILE"
load_definition "$DEF_FILE"

echo "==> Target: ${TARGET_NAME} (${TARGET_ID})"
echo "    Definition: ${CURRENT_DEF_PATH}"
echo "    FQBN: ${FQBN_FULL}"
echo "    Build dir: ${BUILD_DIR_ARG}"

run_actions "$DO_BUILD" "$DO_UPLOAD"
