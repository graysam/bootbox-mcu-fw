# BB-Builder

Desktop companion application for crafting BOOTBOX DSP bundles. Built with Qt 6 (Widgets) + C++17 for a native experience on Windows, macOS, and Linux.

## Features (planned)
- Import SigmaStudio exports, parse program binaries + parameter metadata.
- Visual layout designer with drag-and-drop panels representing DSP modules.
- Preview pane that mimics the ESP32 web UI bindings.
- Bundle exporter that packages `program.bin` + generated `interface.xml` into a `.bbx` archive (tar/zip without compression).
- Project persistence via `.bbproj` files so layouts can be revisited or updated.

This repository currently contains the scaffolding (Qt main window + core data model stubs). Parsing, layout editing, and archive generation will be implemented iteratively.

## Building
```bash
cd BB-Builder
cmake -S . -B build -DCMAKE_PREFIX_PATH="/path/to/Qt/6.x"  # adjust Qt path
cmake --build build
```

Run the app with:
```bash
build/BBBuilder
```

## Development Notes
- `include/BBBuilder/` hosts shared headers for the `core/` components.
- `src/ui/` contains all Qt widgets/views. `MainWindow` exposes Import (module explorer), Layout (drag/drop order + preset editing), and Preview (dark-themed live list mirroring the MCU UI).
- `SigmaParser` parses SigmaStudio `.params` + `.xml` combos: it extracts module names, descriptions, addresses, byte widths, default values, and applies heuristics (gain/freq/delay/toggle detection) so the preview data mirrors the MCU schema. Drop exports in `../etc/` and use *File → Import SigmaStudio Export* in the app to inspect them.
- The detected program binary path is surfaced in the status bar; verify that a corresponding `.bin/.dat/.hex` file is present before creating bundles.
- Layout tab lets you add modules (double-click or “Add to layout”) and reorder/remove them via drag-and-drop; the preview tab updates automatically.
- `BundleWriter` is currently a stub—future work will add `.bbx` archive generation and validation.
