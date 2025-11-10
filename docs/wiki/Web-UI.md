# Web UI

## Structure
- Single-page dashboard served from LittleFS (`data/index.html` + `app.js`).
- Tabs:
  1. **Cooling** — fan mode, setpoints, PID gains, log viewer.
  2. **DSP** — bundle manager, schema-driven controls, presets.
  3. **System** — controller metrics, logs, thermistor calibration.

Every widget tracks pending vs applied values so you always know when the controller has acked a change.

## Behaviors
- Reliable WebSocket messaging with sequence/ack flow control and dirty-state guards. Offline writes surface toast notifications and revert to the last applied value.
- Thermals: PID/manual controls send JSON via `set_mode`, `set_manual_pct`, or `set_therm`. Pending fields glow until `/api/state` reflects the new numbers.
- DSP tab:
  - Bundle manager lists `/dsp/<bundle>/` contents, supports activate/push/rename/delete, and uploads `program.bin`/`interface.xml` pairs.
  - Controls render directly from the uploaded XML schema (sliders, knobs, faders, toggles). Values stream over the WebSocket via `set_dsp`.
  - Presets display as chips per bundle; actions call `/api/dsp/action` (`save_preset`, `load_preset`, `delete_preset`).
  - Hardware status line consumes `hw_ready`/`hw_error` to show “ready”, “offline”, or the exact ADAU error (I²C fault, missing reset pin, etc.) without opening logs.
- System tab:
  - Thermistor calibration card orchestrates `/api/therm/calibration` calls (start → capture → solve) and lists stored coefficients.
  - Metrics grid shows uptime, heap, CPU clock, LittleFS usage, firmware build, and connected WebSocket clients.

## Tab Tips
- **Cooling**
  - PID gains accept decimals; keyboard input works with arrow-key nudges.
  - Manual slider throttles fans immediately but remembers the last applied value when you switch back to PID.
- **DSP**
  - Upload both files before pressing **Upload bundle**; partial uploads leave the bundle in “No interface” state.
  - After a push, controls refresh automatically; if you rename/delete bundles, the UI re-requests `/api/dsp/bundles`.
  - Toggle controls show “On/Off”; sliders display formatted values using the schema’s `unit` + decimal precision.
- **System**
  - `Get logs` fetches the ring buffer; enable auto-scroll before stress testing so new lines stay in view.
  - Thermistor sessions lock the channel until you hit **Solve** or **Cancel**, preventing cross-channel contamination.

## Accessibility & Responsiveness
- Cards collapse/expand individually; states persist in `localStorage` (`bootbox:cards`).
- Tabs remember the last selection (`bootbox:tab`) per browser.
- Keyboard navigation works for all sliders/toggles; keep future controls within native HTML elements (no custom div sliders).
- For touchscreen deployments, target 44 px tap areas and avoid hover-only affordances.

## TODO
- Sketch optimized layouts for 7" touch displays (more vertical spacing, simplified cards).
- Add ARIA labels/roles for bundle actions and preset chips.
- Provide inline validation for DSP uploads (file type/size) before issuing POSTs.
