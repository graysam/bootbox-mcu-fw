# Bootbox ↔ bt2i2s Link Compatibility

Keep the UART JSON link between the controller (Bootbox) and the Bluetooth sink (bt2i2s) in lockstep. When you change protocol, behavior, or timing on one side, update and test the other side before merging.

## Current expectations
- Protocol: newline-delimited JSON, `link_proto` currently `2`.
- Core frames: `hello`, `bt_state` (status/metadata), `bt_devices` (paired list + pairing state), `ack`, `error`.
- Commands: `play/pause/toggle/next/prev/volume`, `hello`, `state`, `pair_start/stop`, `connect`, `forget`, `priority`, `devices`.

## Change checklist
1. Update both firmwares together (Bootbox under `firmware/arduino/bootbox_mcu_fw/`, bt2i2s under `firmware/arduino/bt2i2s/`).
2. Bump `LINK_PROTO_VERSION` in bt2i2s and make Bootbox tolerate the new/old fields as needed during rollout.
3. Adjust WebSocket/UI handling if payloads change (state, bt_state, bt_devices).
4. Rebuild both targets (S3 Bootbox + WROOM bt2i2s) and validate:
   - Mutual `hello`/`bt_state` exchange after boot and after timeout recovery.
   - Commands (play/pause/next/prev/volume) ack and reflect in state.
   - Pairing/device list flows (start/stop, priority, connect/forget) still work.
5. Document any new fields briefly here or in the README to avoid divergence.
