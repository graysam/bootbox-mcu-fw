# DSP Control Roadmap

## Current State
- Bundles live under LittleFS `/dsp/<bundle>/` with `program.bin` (SigmaStudio self-boot image) and `interface.xml`.
- The interface XML describes every control (id, min/max/step, units, ADAU address, etc.). Firmware parses the file and exposes the schema via `/api/dsp/schema`; the web UI renders matching sliders/toggles dynamically.
- `/api/dsp/bundles` + `/api/dsp/action` allow selecting, renaming, deleting, and (stub) pushing bundles.
- Presets are stored under `/dsp-presets/<bundle>/<name>.json` and can be saved/loaded from the UI.

## Interface XML
```xml
<interface>
  <control id="master" label="Master Volume" type="slider" unit="dB"
           min="-60" max="12" step="0.5" default="-12" address="0x1180" bytes="4" format="float" />
  <control id="mute" label="Mute" type="toggle" min="0" max="1" address="0x1190" bytes="1" />
</interface>
```

- Supported `type`: `slider` (default) or `toggle`.
- `address` is currently informational; ADAU writes remain TODO.
- `format` hints at how future ADAU transactions should encode the value (float/fixed).

## Next Steps
- Implement ADAU1701 comms (SigmaDSP safeload + parameter writes) so `push_bundle` flashes the ADAU without breaking self-boot fallback.
- Extend the schema to describe grouped controls, enumerations, and on-change scripts.
- Add upload validation + checksum for SigmaStudio binaries to avoid corrupt bundles.
