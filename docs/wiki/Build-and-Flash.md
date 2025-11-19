# Build & Flash

## Toolchain & Environment
- **Arduino CLI** (v1.3+) plus the ESP32 core defined in `arduino-cli.yaml`.
- Helper scripts live in `tools/`:
  - `arduino-libs.sh` — installs/upgrades required libraries.
  - `arduino-build.sh` — wraps `arduino-cli compile`, repacks LittleFS, and emits a manifest + unified image.
  - `arduino-upload.sh` — flashes the sketch + FS image over USB (honors `PORT`, `BAUD`, `--fqbn`, `--no-build`).
  - `arduino-monitor.sh` — opens a miniterm-style serial console.
- Set `ARDUINO_CLI_CONFIG=./arduino-cli.yaml` so every machine uses the repo-local cache paths instead of `$HOME/.arduino15`.

## Clean Build
```bash
./tools/arduino-libs.sh                         # one-time per workstation
ARDUINO_CLI_CONFIG=./arduino-cli.yaml \
  ./tools/arduino-build.sh --fqbn esp32:esp32:esp32
```
Artifacts land under `.arduino-build/`:
- `bootbox_mcu_fw.ino.bin` — app image.
- `bootbox_mcu_fw.spiffs.bin` — LittleFS payload (web UI + bundles).
- `bootbox_mcu_fw.unified.bin` — merged bootloader + app + partitions + FS for convenience flashing.
- `flash-manifest.json` — offsets/sizes for each segment (consumed by upload scripts or external tools).

For ESP32-S3 DevKitC-1 (16 MB flash / 8 MB PSRAM) pass Arduino’s menu options and keep a dedicated build cache:

```bash
ARDUINO_CLI_CONFIG=./arduino-cli.yaml \
  ./tools/arduino-build.sh \
  --fqbn 'esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,USBMode=hwcdc,PartitionScheme=custom' \
  --build-path .arduino-build-s3
```

The helper reads `sdkconfig` to tag `flash-manifest.json` with the correct chip so downstream tools automatically switch to `--chip esp32s3`.

## Target Definitions & Helper Script
- Reusable manifests live under `targets/*.def`. They define the base FQBN, board menu overrides, flash size, upload port/baud, and optional VID/PID/serial hints so the helper can confirm you plugged in the right board.
- Matching partition tables (`targets/*.partitions.csv`) ensure each board uses 100 % of the available flash for firmware + LittleFS.
- Run `./tools/target-build.sh --target esp32s3-devkitc --flash` to compile and upload end-to-end. Use `--build-only` (default) to stop before flashing, `--flash-only` to reuse a previous build, `--list` to see available targets, or `--skip-identity` to bypass udev sanity checks temporarily. Invoking the helper with no arguments enters an interactive mode where you can select/modify definitions, adjust serial port/baud/strict matching, manage .def files (edit/duplicate/delete), and then kick off build/flash operations.

## Flash Options
### 1. Standard Upload (USB)
```bash
PORT=/dev/ttyUSB0 \
ARDUINO_CLI_CONFIG=./arduino-cli.yaml \
  ./tools/arduino-upload.sh --fqbn esp32:esp32:esp32
```
By default this script rebuilds first; add `--no-build` to reuse the latest artifacts.

For ESP32-S3 (which usually enumerates as `/dev/ttyACM0` when `USBMode=hwcdc`):

```bash
PORT=/dev/ttyACM0 \
ARDUINO_CLI_CONFIG=./arduino-cli.yaml \
  ./tools/arduino-upload.sh \
  --fqbn 'esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,USBMode=hwcdc,PartitionScheme=custom' \
  --build-path .arduino-build-s3
```

### 2. Flash Unified Image (esptool)
Useful for production or recovery when the Arduino CLI is unavailable.
```bash
esptool.py --chip esp32 --port /dev/ttyUSB0 --baud 921600 \
  write_flash --flash_mode dio --flash_freq 80m --flash_size detect \
  0x0 .arduino-build/bootbox_mcu_fw.unified.bin
```
The same offsets appear in `flash-manifest.json` if you need finer control.
For ESP32-S3, swap `--chip esp32s3` (or let the manifest/`tools/arduino-upload.sh` pass the recorded `chip` value automatically).

### 3. OTA / External Programmer
Not yet implemented, but the manifest structure supports scripting your own uploader—parse it, then stream each `{offset, path}` pair over your transport of choice.

## Post-Flash Verification
1. Run `tools/arduino-monitor.sh` (default 115200 baud) to watch boot logs. Confirm `Booting BOOTBOXDSP controller...` and the AP IP.
2. Connect to the `BOOTBOXDSP` SSID and browse `http://192.168.4.1/`.
3. Hit `curl http://192.168.4.1/api/state` to verify the HTTP stack + JSON payloads.
4. If DSP bundles are required, upload a known-good package and push it once before closing the session.

## Tips
- The build script respects `--build-path`; point it at a RAM disk for faster iterations.
- When switching between board variants, update `firmware/arduino/bootbox_mcu_fw/config.h` first so the compile-time pinout matches the connected hardware.
- Each build folder retains its own `sdkconfig`, so keep separate directories (e.g. `.arduino-build` vs `.arduino-build-s3`) when you bounce between classic ESP32 and ESP32-S3 targets.
- If flashing stalls, power-cycle the DevKit and hold `BOOT` while resetting once; after a successful sync the helper scripts toggle RTS/DTR automatically.
