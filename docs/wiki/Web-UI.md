# Web UI

## Structure
- Single-page dashboard served from LittleFS (`data/index.html` + `app.js`).
- Tabs: Cooling, DSP, System (logs, uploads, calibration tools).

## Behaviors
- Reliable WS messaging with acks + dirty-state guarding UI widgets.
- DSP tab:
  - Bundle manager lists `/dsp/<bundle>/` contents, supports activate/push/rename/delete, and uploads `program.bin`/`interface.xml` pairs.
  - Controls render directly from the uploaded XML schema (slider/toggle). Values stream over the WebSocket via `set_dsp`.
  - Presets display as chips per bundle; actions call `/api/dsp/action` (`save_preset`, `load_preset`, `delete_preset`).
- Thermistor calibration card orchestrates `/api/therm/calibration` calls, updates the table, and tracks sessions.

## TODO
- Sketch responsive layouts for 7" touch displays.
- Add accessibility notes (keyboard/ARIA).
- Add visual grouping/headers for complex DSP layouts.
