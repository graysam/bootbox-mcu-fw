# DSP Bundle How-To

This guide walks through creating a SigmaStudio bundle, generating the companion interface definition, and pushing it to the controller.

## Prerequisites
- SigmaStudio project targeting ADAU1701 (self-boot enabled).
- Controller running current firmware (LittleFS mounted, DSP tab visible).
- I²C wiring between the MCU and the ADAU board (SDA/SCL + optional RESET).
- A text editor for writing the `interface.xml`.

## 1. Export the SigmaStudio Binary
1. In SigmaStudio, build your design as normal.
2. Use **Link-Compile-Download** once to ensure coefficient blocks are up to date.
3. Choose **File → Export System Files…** and enable *Program Data* + *Parameter Data*.
4. From the export folder, copy the `\_\_Program.bin` file. Rename it to `program.bin`.  
   - This image is exactly what the MCU writes into the ADAU EEPROM during `push_bundle`.

Keep the associated *parameter XML* open—its addresses help when mapping controls.

## 2. Describe the Interface
Create `interface.xml` in the same folder as `program.bin`. Each `<control/>` entry represents a slider, knob, fader, or toggle rendered in the UI.

### Attribute Reference
| Attribute | Required | Description |
| --- | --- | --- |
| `id` | ✅ | Unique identifier reused across presets/WebSocket messages. |
| `label` | | Friendly text shown in the UI (falls back to `id`). |
| `type` | | `slider`, `knob`, `fader`, or `toggle` (defaults to `slider`). |
| `unit` | | Text appended to formatted values (`dB`, `Hz`, `%`, etc.). |
| `min` / `max` | | Numeric bounds; toggles default to 0/1. |
| `step` | | Slider granularity; determines decimal precision in UI. |
| `default` | | Starting value when a bundle loads the first time. |
| `address` | | 16-bit ADAU parameter address (hex or decimal). `0` skips hardware writes for UI-only controls. |
| `bytes` | | Length of the register (1/2/3/4). Default = 4. |
| `format` | | Encoding: `fixed5.23` (default), `u8`, `u16`, `u24`, `u32`, or `raw`. |

### Example
```xml
<interface version="1">
  <control id="master_gain"  label="Master Volume" type="slider"
           unit="dB" min="-60" max="12" step="0.5"
           default="-6" address="0x1180" format="fixed5.23" />

  <control id="stereo_gain" label="Stereo Bus" type="knob"
           unit="dB" min="-24" max="12" step="0.25"
           default="0" address="0x11A0" format="fixed5.23" />

  <control id="sub_lo_gain" label="Sub Low Gain" type="slider"
           unit="dB" min="-30" max="10"
           default="-3" address="0x11C4" />

  <control id="mute_all" label="Mute" type="toggle"
           min="0" max="1" address="0x1200" bytes="1" format="u8" />

  <control id="ui_note" label="Notes" type="slider"
           min="0" max="100" default="50" address="0" />
</interface>
```
The last entry (`address="0"`) demonstrates a UI-only control—useful for placeholders or when the DSP algorithm is still being wired.

## 3. Upload the Bundle
### Via the Web UI
1. Open the **DSP** tab.
2. Enter a bundle name (e.g., `stereo-v1`).
3. Select `program.bin`, then `interface.xml`.
4. Click **Upload bundle**; the status line should show success.

### Via `curl`
```bash
curl -F file=@program.bin \
     "http://192.168.4.1/api/upload/adau?bundle=stereo-v1&kind=program"

curl -F file=@interface.xml \
     "http://192.168.4.1/api/upload/adau?bundle=stereo-v1&kind=interface"
```

Uploads land under LittleFS `/dsp/<bundle>/`.

## 4. Activate and Push
1. Click **Activate** beside the new bundle (or `POST /api/dsp/action` with `{"action":"select_bundle","name":"stereo-v1"}`).
2. Click **Push to DSP** (or `{"action":"push_bundle","name":"stereo-v1"}`). The MCU:
   - Streams `program.bin` into the EEPROM at `ADAU_EEPROM_I2C_ADDR`.
   - Pulses `PIN_ADAU_RESET` (if defined) to trigger self-boot.
   - Replays all saved control values over I²C.
3. Watch the DSP status line—`DSP link ready` confirms success; any I²C error is surfaced verbatim.

## 5. Save Presets
1. Tweak controls live; values mirror into the WebSocket feed and persist in NVS.
2. Provide a preset name (e.g., `Venue-A`) and click **Save current values**.
3. Presets are stored in `/dsp-presets/<bundle>/<name>.json`. You can back them up via LittleFS or REST (`/api/dsp/action`).

## Tips & Troubleshooting
- **Wrong controls mapping**: double-check the ADAU parameter address—SigmaStudio’s *Register Control* window shows hex offsets that match the firmware expectations.
- **Push fails with `ADAU link offline`**: confirm `Wire.begin()` pins match your board and that pull-ups are present. The DSP status line will stay red until the bus responds.
- **Bundle missing after reboot**: ensure both files were uploaded; the bundle list shows `Interface`/`No interface` next to each entry.
- **Automating generation**: script the XML creation by parsing SigmaStudio’s exported `ParameterData.xml`; map `<Parameter>` IDs to addresses and emit `<control/>` nodes accordingly.
