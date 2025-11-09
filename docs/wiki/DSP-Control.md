# DSP Control Roadmap

## Current State
- UI exposes mixer + crossover controls and persists them via NVS.
- Firmware stubs `handleDspUpdate` for validation/clamping and logging.

## Next Steps
- Implement ADAU1701 comms (SigmaDSP safeload + parameter writes).
- Map UI controls to DSP coefficients + presets.
- Add upload validation + checksum for SigmaStudio binaries.
