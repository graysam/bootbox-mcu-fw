# Troubleshooting

## Common Issues
- **LittleFS mount failed**: verify partition scheme, rerun build to repack assets, ensure `flash-manifest.json` includes the FS image.
- **Web UI auto-resets fields**: confirm no pending dirty state (UI highlights unsaved inputs).
- **Thermistor reads NaN**: wiring open or ADC saturating; check divider values and rerun calibration.
- **Status LED dark**: ensure `STATUS_LED_ACTIVE_HIGH` matches your board (most DevKits are active-low).

Add findings here as testing expands.
