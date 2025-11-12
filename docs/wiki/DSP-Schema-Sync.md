# DSP Module Schema Sync

Keeping the BB-Builder desktop app and the MCU firmware aligned requires a shared source of truth for available modules, controls, and their capabilities.

## Proposed Strategy
1. **Shared Catalog:** Store a versioned JSON catalog (e.g., `shared/dsp-catalog.json`) in this repo. It describes every control type (range, formatting, compatible widget types) and module template.
2. **Builder Consumption:** BB-Builder reads the catalog at startup, pairing SigmaStudio exports with known module IDs to suggest correct control types and validation rules.
3. **Firmware Consumption:** The MCU firmware pulls the same catalog (compiled into flash or converted to C++ headers) so `/api/dsp/schema` validation matches the desktop tool.
4. **Version Handshake:** Bundles exported by BB-Builder embed the `catalog_version`. Firmware compares it against its compiled version and can reject or warn if major versions differ.
5. **Extensibility:** When new modules ship (e.g., EQs, filters, compressors), add them to the catalog first. CI can lint the JSON to ensure both firmware and BB-Builder build with the latest data.

## Next Steps
- Define `shared/dsp-catalog.json` structure (module id, description, control array, allowed widget types).
- Add lightweight loaders on both sides (C++17 for MCU, Qt/C++ for BB-Builder).
- Extend bundle export to embed catalog metadata alongside the generated `interface.xml`.
