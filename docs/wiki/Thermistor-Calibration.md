# Thermistor Calibration

## Goal
Calibrate each NTC probe + divider pair so the controller reports accurate temperatures despite component tolerances.

## UI Workflow
1. **Pick Channel** — choose `Thermistor 1` (GPIO34) or `Thermistor 2` (GPIO35).
2. **Start Session** — locks the channel so captures stay consistent. The UI shows a session ID.
3. **Prepare Reference Baths**
   - *Cold*: ice water (~0 °C) or a precision reference block.
   - *Hot*: boiling water (~100 °C) or any known temperature near the high end of interest.
4. **Capture Low**
   - Enter the measured temperature (e.g., `0.0`).
   - Immerse the probe, wait for readings to settle, click **Capture low**. The firmware averages several hundred ADC samples during the click.
5. **Capture High**
   - Repeat for the hot bath (e.g., `99.5`).
6. **Solve & Save**
   - Optional: tweak the nominal temperature field (defaults to 25 °C).
   - Click **Solve & save**. The firmware computes nominal resistance + β, stores them in Preferences, and refreshes `/api/state`.
7. **Done**
   - The table lists the computed values with a timestamp. Use **Clear** to revert to the defaults or **Cancel** mid-process to unlock the channel without saving.

## API Reference
Endpoint: `/api/therm/calibration`

| Action | Payload | Notes |
| --- | --- | --- |
| `status` | `GET` | Returns current calibration table + active session (if any). |
| `start` | `{"action":"start","channel":0}` | Channel = 0 or 1. |
| `capture` | `{"action":"capture","which":"low","temperature":0.0}` | `which` = `low` / `high`. |
| `solve` | `{"action":"solve","nominal_temp":25.0}` | Requires both captures; returns solved β/nominal resistance. |
| `clear` | `{"action":"clear","channel":0}` | Removes stored calibration. |
| `cancel` | `{"action":"cancel"}` | Drops the current session without changes. |

Sample capture:
```bash
curl -X POST http://192.168.4.1/api/therm/calibration \
  -H "Content-Type: application/json" \
  -d '{"action":"capture","channel":0,"which":"low","temperature":0.0}'
```

## Tips
- Stir the bath gently during captures so the thermistor sees uniform temperature.
- Keep the harness wires fully submerged to the same depth each time; partial immersion skews readings.
- If the UI reports `NaN` after calibration, re-check the divider resistors or rerun the workflow—saved coefficients assume the exact resistor pair you used during calibration.
- Defaults live in `config.h` and are only consulted when no calibration is stored.
