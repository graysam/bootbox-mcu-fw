# Troubleshooting

## Quick Reference
| Symptom | Likely Cause | Fix |
| --- | --- | --- |
| LittleFS mount failed | Image missing or wrong partition map | Rebuild (`arduino-build.sh`), ensure upload script flashes `bootbox_mcu_fw.spiffs.bin`, verify `flash-manifest.json` offsets match your board. |
| Web UI fields snap back | Pending dirty state or WS disconnect | Wait for ACK toast, ensure controller is online (connection chip should be green). Reload page to resync. |
| Thermistor shows `NaN` | Open circuit or ADC saturated | Check wiring, confirm divider resistors, rerun [[Thermistor-Calibration]]. |
| Status LED stuck off | Wrong polarity or pin | Update `STATUS_LED_ACTIVE_HIGH`/`STATUS_LED_PIN` in `config.h`, rebuild + flash. |
| DSP bundle push fails (`ADAU link offline`) | I²C wiring or reset pin missing | Check SDA/SCL connections + pull-ups; define `PIN_ADAU_RESET` if the board exposes it; confirm ADAU board has power. |
| DSP controls lag | Oversized schema or WS spam | Reduce control count, ensure browser tab stays focused during heavy slider drags, increase `DSP_SAVE_DELAY_MS` if needed. |
| Reboots with `heap_caps_free` assert | LittleFS fragmentation or bundle upload been interrupted | Reformat LittleFS (erase flash or flash `bootloader + partitions`), avoid power cycles during uploads. |
| Fans/thermistors dead after flashing ESP32-S3 | Sketch built with ESP32 pin map | Rebuild with `--fqbn esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,USBMode=hwcdc,PartitionScheme=custom`. `/api/state.sys.idf_target` should report `esp32s3`. |

## Serial Debug Tips
- Use `tools/arduino-monitor.sh` to capture panic traces. For Guru Meditation logs, grab the backtrace addresses and feed them to `xtensa-esp32-elf-addr2line -pfiaC bootbox_mcu_fw.ino.elf`.
- Enable `THERMISTOR_DEBUG_LOG` in `config.h` temporarily to stream raw ADC readings when diagnosing sensors.
- Add `DEBUG_WS` style logs near new WebSocket handlers; remember to disable them before releasing.

## Networking
- Open AP `BOOTBOXDSP` broadcasts on channel 1 by default. If clients struggle to connect, reboot the board or toggle airplane mode on the client to refresh the association.
- Browser shows “This page isn’t working”? Check serial logs for panics or ensure the LittleFS image flashed successfully (UI assets live there).

## DSP Specific
- If `hw_error` reports `reset pin not defined; power-cycle ADAU to selfboot`, either wire the reset pin or manually toggle power after each push.
- Bundles missing after reboot: confirm both `program.bin` and `interface.xml` exist under `/dsp/<bundle>/`. Deleting a bundle removes its presets as well.

Add findings here as testing expands.
