# Thermistor Calibration

## Workflow
1. Enter cold/hot reference temps (iced + boiling water recommended).
2. Capture both points from the UI; firmware averages ADC samples automatically.
3. Solve to compute β + nominal resistance; values stored in Preferences per channel.
4. Clear or re-run whenever probes or dividers change.

## Reference
- Defaults live in `config.h` and only act as fallbacks when no calibration is stored.
- `/api/therm/calibration` supports `start`, `capture`, `solve`, `clear`, and `status` payloads.
