# Web UI

## Structure
- Single-page dashboard served from LittleFS (`data/index.html` + `app.js`).
- Tabs: Cooling, DSP, System (logs, uploads, calibration tools).

## Behaviors
- Reliable WS messaging with acks + dirty-state guarding UI widgets.
- DSP control layer built with reusable knob/slider primitives.
- Thermistor calibration card orchestrates `/api/therm/calibration` calls, updates the table, and tracks sessions.

## TODO
- Sketch responsive layouts for 7" touch displays.
- Add accessibility notes (keyboard/ARIA).
