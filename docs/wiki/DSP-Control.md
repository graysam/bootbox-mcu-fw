# DSP Control Roadmap

## Current State
- Bundles live under LittleFS `/dsp/<bundle>/` with `program.bin` (SigmaStudio self-boot image) and `interface.xml`.
- The interface XML describes every control (id, min/max/step, units, ADAU address/format, etc.). Firmware parses the file and exposes the schema via `/api/dsp/schema`; the web UI renders matching sliders/toggles dynamically.
- `/api/dsp/bundles` + `/api/dsp/action` allow selecting, renaming, deleting, pushing bundles, and saving/loading presets.
- `push_bundle` writes `program.bin` into the ADAU1701 EEPROM over I²C (`ADAU_EEPROM_I2C_ADDR` in `config.h`) and pulses reset so the codec selfboots the selected image.
- Live control changes stream over the WebSocket (`set_dsp`) and immediately update the mapped ADAU parameter addresses (5.23 fixed-point by default).
- `/api/dsp/schema` also surface `schema_ready`, `hw_ready`, and the last `hw_error` so tooling/UIs can halt pushes or warn users when the ADAU link or reset line is unhealthy.

## Interface XML
```xml
<interface>
  <control id="master" label="Master Volume" type="slider" unit="dB"
           min="-60" max="12" step="0.5" default="-12" address="0x1180" bytes="4" format="fixed5.23" />
  <control id="mute" label="Mute" type="toggle" min="0" max="1" address="0x1190" bytes="1" format="u8" />
</interface>
```

- Supported `type`: `slider` (default) or `toggle`.
- `format`: `fixed5.23` (default), `u8`, `u16`, `u24`, `u32`, or `raw`. The firmware converts the UI float into the specified encoding before publishing it over I²C.
- `bytes` defaults to 4; set it explicitly for smaller registers (e.g., 1-byte toggles).

### Type Notes
- **Slider / Knob / Fader**: identical semantics today—UI chooses the widget style, firmware only sees numeric values. `step` controls both slider granularity and decimals shown in the UI.
- **Toggle**: clamp to `[0,1]` unless explicitly overridden. When `min < 0`, the midpoint still determines the “On/Off” labels, so keep it simple (0/1).
- **UI-only controls**: set `address="0"` to omit hardware writes. Values still persist in NVS/presets and flow over WebSocket updates (handy for TODO placeholders).

## REST / WS APIs
| Endpoint | Method | Purpose |
| --- | --- | --- |
| `/api/dsp/bundles` | GET | List bundles `{name, active, has_program, has_interface}`. |
| `/api/dsp/schema` | GET | Returns `{active, schema_ready, hw_ready, hw_error?, controls[], values{}, presets[]}`. |
| `/api/dsp/action` | POST JSON | Actions: `select_bundle`, `push_bundle`, `rename_bundle`, `delete_bundle`, `save_preset`, `load_preset`, `delete_preset`. |
| `/api/upload/adau` | POST multipart | Upload either `program` or `interface` (set via query string). |
| WebSocket `set_dsp` | JSON | `{type:"set_dsp", id:"master", value:-6.0}` updates a control live. |

Responses always include `ok` and, when failing, a descriptive `message`. For pushes/presets the message is propagated from `adau_last_error`, so automation can present the exact I²C fault.

## Preset Format
Each preset is a JSON object stored at `/dsp-presets/<bundle>/<name>.json`:
```json
{
  "master": -6.0,
  "stereo_gain": -3.5,
  "mute": 0,
  "sub_lo_gain": -2.0
}
```
Values are clamped to the schema bounds on load. If any hardware write fails, the firmware reports a `502` with the ADAU error string but keeps the in-memory values so nothing is lost.

## Generating Bundles Programmatically
1. Export SigmaStudio `Program.bin` + `ParameterData.xml`.
2. Parse `ParameterData.xml` (or the CSV exported from SigmaStudio) to extract module names, labels, addresses, and units.
3. Emit `interface.xml` by mapping each entry to the attribute table above. Suggested defaults:
   - Map `step` to `1` for coarse gain blocks, `0.1` / `0.25` for level controls, `1` Hz for crossover frequencies.
   - Use `format="u16"` for integer registers (e.g., enumerated mux controls).
4. Zip the bundle contents or upload via two sequential POSTs (program + interface). See [[DSP-Bundle-How-To]] for manual steps.

Automation hooks nicely into CI: after building a SigmaStudio project, run the generator, push the bundle to a staging controller, trigger `/api/dsp/schema`, and verify `hw_ready` stays true.

## Next Steps
- Extend the schema to describe grouped controls, enumerations, meters, and dependent logic.
- Add upload validation + checksum for SigmaStudio binaries to avoid corrupt bundles.
- Investigate auto-generating the interface XML directly from SigmaStudio exports in the helper tooling.
