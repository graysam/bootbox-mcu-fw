# Build and Upload (Arduino CLI)

This project targets an Arduino Wi-Fi MCU with Async Web Server + WebSocket UI. Follow these steps to bring a fresh machine to build/upload readiness.

## Prereqs
- Install Arduino CLI and the board support package defined in `arduino-cli.yaml`.
- Ensure the board FQBN recorded in `arduino-cli.yaml` (default `esp32:esp32:esp32`) is installed.

## Install Libraries

Run:

```
tools/arduino-libs.sh
```

Installs:
- ESP Async WebServer
- AsyncTCP
- ArduinoJson
- LittleFS filesystem shim (only needed for older cores; recent releases include LittleFS)

## Build + Package

```
ARDUINO_CLI_CONFIG=./arduino-cli.yaml tools/arduino-build.sh \
  --fqbn esp32:esp32:esp32 \
  --build-path .arduino-build
```

This will:
- Compile the sketch with Arduino CLI.
- Read the active partition table to size the LittleFS image automatically.
- Generate `flash-manifest.json` describing all flash segments.
- Produce `.unified.bin` (bootloader + app + LittleFS) for convenience.

For **ESP32-S3 DevKitC-1 (16MB flash / 8MB PSRAM)** pass the board options Arduino CLI expects and keep a dedicated build folder so the sdkconfig artifacts do not collide with the ESP32 build:

```
ARDUINO_CLI_CONFIG=./arduino-cli.yaml tools/arduino-build.sh \
  --fqbn 'esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,USBMode=hwcdc,PartitionScheme=custom' \
  --build-path .arduino-build-s3
```

The helper parses `sdkconfig` to detect the chip (`CONFIG_IDF_TARGET`) so the generated manifest + merged image carry the correct `--chip esp32s3` metadata.

## Target Definitions & Helper Script
- `targets/*.def` describe each board/variant. Fields cover: `FQBN` + menu options, build directory, full flash size (used to validate the matching `targets/*.csv` partition file), upload port/baud, and optional `PORT_ID_*` hints that let the helper confirm the `/dev/tty*` device via `udevadm`.
- `targets/*.partitions.csv` provide explicit layouts so the LittleFS/data partition uses every byte of flash advertised in the `.def`.
- `tools/target-build.sh --target esp32s3-devkitc --flash` copies the declared partition CSV into the sketch (temporarily), runs `arduino-build.sh` with the composed FQBN string, and flashes via the serialized port if requested. Use `--build-only` (default) to skip flashing, `--flash-only` to reuse an existing build, or `--list` to enumerate definitions. Launching the script without arguments opens an interactive UI where you can pick a .def, tweak serial/baud/strict validation, manage definition files, and then trigger build/flash actions.
- Use `--skip-identity` if you're pairing a new board and haven't filled out the VID/PID/serial in the `.def` file yet.

## Upload (firmware + LittleFS)

```
PORT=/dev/ttyUSB0 ARDUINO_CLI_CONFIG=./arduino-cli.yaml tools/arduino-upload.sh \
  --fqbn esp32:esp32:esp32 \
  --build-path .arduino-build
```

`arduino-upload.sh` rebuilds by default; add `--no-build` to reuse existing artifacts. The script flashes every segment listed in `flash-manifest.json`, so the web UI is always deployed alongside the firmware.

ESP32-S3 boards usually enumerate as `/dev/ttyACM0` when `USBMode=hwcdc`. Point the upload helper at the S3 build artifacts:

```
PORT=/dev/ttyACM0 ARDUINO_CLI_CONFIG=./arduino-cli.yaml tools/arduino-upload.sh \
  --fqbn 'esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,USBMode=hwcdc,PartitionScheme=custom' \
  --build-path .arduino-build-s3
```

> Tip: CP2102 USB bridges usually enumerate as `/dev/ttyUSB0`, while CH34x sometimes appear as `/dev/ttyUSB1`. Use `ls /dev/ttyUSB*` or `dmesg | tail` after plugging in to confirm.

## Serial Monitor (optional)

```
PORT=/dev/ttyUSB0 tools/arduino-monitor.sh
```

## First Boot
- Controller starts open AP `BOOTBOXDSP` (no password).
- Browse to `http://192.168.4.1/` to load the UI.

## Notes
- If LittleFS mount fails, check the Partition Scheme and ensure a LittleFS (or SPIFFS on older cores) partition is available.
- If build errors mention missing libraries, re-run `tools/arduino-libs.sh` or install via Arduino IDE Library Manager.
- Thermistor defaults live in `config.h`; after flashing you can fine-tune using the System tab calibration wizard, which writes curves to NVS without rebuilding.

## Release Checklist
1. Ensure `main` is clean and tests/build succeed.
2. Run `tools/arduino-build.sh --config-file arduino-cli.yaml --build-path .arduino-build` to generate fresh artifacts.
3. Package the files you want to ship (commonly:
   - `bootbox_mcu_fw.unified.bin`
   - `bootbox_mcu_fw.ino.bin`
   - `bootbox_mcu_fw.spiffs.bin`
   - `flash-manifest.json`
   - `bootbox_mcu_fw.ino.partitions.bin`
   ).
4. Tag the commit (`git tag 0.x.y && git push --tags`) and create the GitHub release:

```
gh release create 0.x.y dist/bootbox-mcu-fw-0.x.y.zip \
  --title "Release 0.x.y" \
  --notes-file docs/release-notes-0.x.y.md
```

5. Verify the release archive downloads and flashes via `tools/arduino-upload.sh --input-dir`.
