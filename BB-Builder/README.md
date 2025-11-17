# BB-Builder

Desktop companion application for crafting BOOTBOX DSP bundles. Built with Qt 6 (Widgets) + C++17 for a native experience on Windows, macOS, and Linux.

## Features
- Import SigmaStudio exports (`.params + schematic XML`) and automatically detect the matching DSP program binary for later bundle creation.
- Visual layout designer with drag-and-drop panels that mirror the MCU dashboard cards, including a dark theme, toolbar/menu scaffolding, and live preview canvas.
- Inspector sidebar for modules/controls:
  - Edit display labels, descriptions, widget types, ranges, units, and step sizes.
  - Remap controls to other SigmaStudio parameters with compatibility checks (byte width/format/read-only guards automatically disable invalid targets).
  - Click cards/controls in the preview to jump focus inside the inspector.
- Import browser uses a collapsible tree so large SigmaStudio projects stay manageable—double-click a module entry to drop it into the layout, expand only when you need to review individual parameters, and remove controls directly from the inspector when you don’t want them exposed in the UI.
- Layout tab now exposes an “Unassigned modules & controls” panel plus a drag-ready control palette, making it easier to keep track of DSP blocks that still need UI coverage (and laying the groundwork for drag-to-place workflows).
- Live QtWebEngine preview tab renders the exact dark-themed cards the MCU serves, so every palette/drag change can be sanity-checked instantly without reflashing hardware.
- Project metadata card (name/author/notes) + `.bbproj` persistence so layouts can be reopened, tweaked, and exported later. File → Save/Open is fully wired up.
- Status bar + module list summaries update live as you edit, so you always know which binary, project file, and module count you’re working with.
- Undo stack hooks exist for inspector edits (module/control updates) so future undo/redo UX can plug in rapidly.
- Bundle writer now produces `.bbx` archives (uncompressed tar) that contain the selected `program.bin` and the generated `interface.xml`, matching the MCU’s upload expectations.

## Building
```bash
cd BB-Builder
cmake -S . -B build -DCMAKE_PREFIX_PATH="/path/to/Qt/6.x"  # needs QtWebEngineWidgets installed
cmake --build build
```

Run the app with:
```bash
build/BBBuilder
```

## Workflow
1. **Import** – Use *File → Import SigmaStudio Export* and point to a folder containing your `.params`, `.xml`, and generated binaries. The parser groups the discovered modules/controls, fills in addresses + formats, and surfaces the detected binary path in the status bar.
2. **Layout** – Select modules from the import list, click *Add to layout* (or double-click) to drop them into the layout stack, and reorder via drag-and-drop. Selecting a module populates the inspector where you can rename cards, adjust descriptions, and fine-tune each control.
3. **Preview** – Hop to the *Preview* tab to see the MCU-style cards. Clicking cards or individual controls snaps the inspector to that element for faster iteration.
4. **Persist** – Save your work to a `.bbproj` so you can revisit the layout later or share it with collaborators. Projects capture meta info, module/control overrides, layout order, and the associated program binary.
5. **Export** – File → *Create Bundle* writes a `.bbx` (ustar) archive that bundles `program.bin` + the generated `interface.xml`. Upload it through the MCU UI as-is.

## Development Notes
- `include/BBBuilder/` hosts shared headers for the `core/` components.
- `src/ui/` contains all Qt widgets/views. `MainWindow` exposes Import (module explorer), Layout (drag/drop order + preset editing), and Preview (dark-themed live list mirroring the MCU UI).
- `SigmaParser` parses SigmaStudio `.params` + `.xml` combos: it extracts module names, descriptions, addresses, byte widths, default values, and applies heuristics (gain/freq/delay/toggle detection) so the preview data mirrors the MCU schema. Drop exports in `../etc/` and use *File → Import SigmaStudio Export* in the app to inspect them.
- The detected program binary path is surfaced in the status bar; verify that a corresponding `.bin/.dat/.hex` file is present before creating bundles.
- Layout tab lets you add modules (double-click or “Add to layout”), reorder/remove them via drag-and-drop, and edit module/control properties (labels, descriptions, ranges, widget types, parameter remaps) in the Inspector sidebar; the preview tab renders the cards using the same dark styling as the MCU dashboard.
- Menus and toolbar placeholders (auto layout, smart linking, grid/snapping, automation lanes) are already scaffolded so future UX-heavy features have a home even if they’re currently disabled.
- `BundleWriter` emits `.bbx` bundles using an uncompressed tar layout (`program.bin`, `interface.xml`) so client-side JS or tooling can unpack them easily before uploading to the MCU.
