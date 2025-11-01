# Repository Guidelines

## Project Structure & Module Organization
- `firmware/arduino/` — Primary ESP32 Arduino sketch (`src/`, `lib/`, `include/`).
- `firmware/idf/` — Optional ESP-IDF components/prototypes (`main/`, `components/`).
- `web/` — Client UI (assets, WebSocket client, build tooling).
- `tests/` — Firmware smoke tests and web unit tests.
- `tools/` — Helper scripts (build, flash, formatting).
- `docs/` — Specs, diagrams, architecture notes.

## Build, Test, and Development Commands
- Arduino (CLI):
  - `arduino-cli compile --fqbn esp32:esp32:esp32 firmware/arduino`
  - `arduino-cli upload -p /dev/ttyUSB0 --fqbn esp32:esp32:esp32 firmware/arduino`
  - `arduino-cli monitor -p /dev/ttyUSB0 -b 115200`
- ESP-IDF: `get_idf && idf.py -C firmware/idf build flash monitor -p /dev/ttyUSB0`
- Web UI: `cd web && npm ci && npm run dev` (or `npm run build`)
- Optional (PIO): `pio run` and `pio run -t upload`

## Coding Style & Naming Conventions
- C/C++: 2-space indent, ~100 col limit, K&R braces. Files `lower_snake_case.(c|cpp|h|hpp)`. Classes `UpperCamelCase`, functions `lower_snake_case`, constants `UPPER_SNAKE_CASE`.
- JS/TS: Prettier defaults (2 spaces, semicolons). Folders `kebab-case`.
- Run formatters before commit (e.g., `clang-format -i`, `prettier -w web/`).

## Testing Guidelines
- Firmware: prefer lightweight Unity-style tests and hardware smoke checks (boot, sensor read, PWM, watchdog). Add under `tests/firmware/`.
- Web: unit tests in `web/` (e.g., Vitest/Jest). Aim for critical-path coverage; include WebSocket connect/retry tests.
- CI: add at least compile + smoke targets before major merges.

## Commit & Pull Request Guidelines
- Use Conventional Commits: `feat: ...`, `fix: ...`, `chore: ...`, `docs: ...`, `refactor: ...`.
- PRs: clear description, how-to-run, screenshots (UI), logs (firmware), and linked issues. Keep changes focused and small.

## Security & Configuration Tips
- Default AP: SSID `BOOTBOXDSP`, password `lollipop`. Persist runtime settings (e.g., NVS), avoid committing secrets.
- Serial port configurable (e.g., `PORT=/dev/ttyUSB0`). Prefer robust WebSocket backoff on the client.

## Agent-Specific Notes
- Always append changes to `journal.txt` (prompt + files + rationale). Avoid mass reformatting or unrelated edits. Keep diffs surgical and follow this guide.

