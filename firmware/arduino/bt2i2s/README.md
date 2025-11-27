## BT2I2S Firmware

Bluetooth A2DP/AVRCP sink firmware for an ESP32 DevKitC (WROOM) that forwards phone audio over I2S into the ADAU1701 DSP. It also keeps a UART link to the Bootbox orchestration MCU so transport state and volume changes can stay in sync with the web UI and duplicated controls.

### Quick start
- Install the toolchain deps used in bootbox: `arduino-cli` + ESP32 core (see `arduino-cli.yaml`).
- Optional helper to pull needed libs: `tools/arduino-libs.sh` (vendors ESP32-A2DP + ArduinoJson). The ESP32-A2DP library is also vendored under `firmware/arduino/libraries` for offline builds.
- Build/flash like bootbox: `tools/target-build.sh --target esp32-devkitc --flash` (or `--build-only` + `tools/arduino-upload.sh`).
- Debug serial (USB): 115200 baud. Link UART to Bootbox: TX=17, RX=16 @ 921600 baud by default.

### Hardware wiring (defaults in `firmware/arduino/bt2i2s/config.h`)
- I2S out to ADAU1701: BCLK=26, LRCLK=25, DOUT=27, MCLK unused (-1). Set `I2S_MASTER_MODE=false` if the ADAU drives BCLK/LRCLK and the ESP32 should be an I2S slave.
- Bootbox link UART: TX=17 -> Bootbox RX, RX=16 <- Bootbox TX. Set pins to -1 to disable the link for bring-up.
- ESP32 DevKitC (WROOM) target/partition map lives in `targets/esp32-devkitc.*`.

### What works now
- ESP32-A2DP sink with AVRCP callbacks wired for connection/audio/metadata/volume/playback notifications.
- I2S pipeline preconfigured for external DAC/DSP (ADAU1701) using the legacy I2S path in ESP32-A2DP; sample rate updates propagate from the Bluetooth stream.
- UART link protocol (JSON lines) with hello/ack/error frames, optional `id` correlation, and commands: `play/pause/toggle/next/prev/volume`, plus `state` and `hello` requests.
- State broadcasts (`type:"bt_state"`) sent on events and heartbeat include connection, AVRCP, audio activity, playing flag, volume, sample rate, and optional metadata (title/artist/album).

### Near-term TODO
- Decide master/slave role vs. ADAU1701 and test clocking; add MCLK handling if ADAU needs it.
- Tighten the Bootbox link protocol (acks already present, but add version negotiation, ownership of volume, track position, richer errors).
- Add OTA/pairing management and a way to clear bonded devices from Bootbox.
- Wire DSP gain/mute alignment so phone volume, AVRCP volume, and ADAU gain track predictably.

### Link protocol (v1 snapshot)
- Frames are newline-delimited JSON. Optional `id` is echoed back for correlation.
- `{"type":"hello","fw":"bt2i2s","link_proto":1,"cmds":[...],...}` sent at boot and on request.
- `{"type":"bt_state","reason":"<event>",...}` pushed on events/heartbeat; includes connection flags, volume/sample rate, and metadata.
- Commands (either `{type:"cmd",cmd:"play"}` or legacy `{cmd:"play"}`): `play/pause/toggle/next/prev/volume` (`pct` 0-100), `state`, `hello`.
- Responses: `{"type":"ack","cmd":"play","status":"ok"}` for most commands, or `{"type":"error","reason":"unknown_cmd","detail":"..."}` on failure. State/hello requests return their respective frames instead of an ack.
