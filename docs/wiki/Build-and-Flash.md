# Build & Flash

## Toolchain
- Arduino CLI (see `arduino-cli.yaml`).
- Helper scripts under `tools/` for deps, build, upload, monitor.

## Typical Flow
1. `tools/arduino-libs.sh` (one time per machine).
2. `ARDUINO_CLI_CONFIG=./arduino-cli.yaml tools/arduino-build.sh`.
3. `PORT=/dev/ttyUSB0 tools/arduino-upload.sh --no-build` to flash.
4. `tools/arduino-monitor.sh` for serial logs.

## Notes
- Build script repacks LittleFS and generates `flash-manifest.json` describing each segment and offsets.
