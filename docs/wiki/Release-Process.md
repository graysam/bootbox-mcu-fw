# Release Process

## Pre-flight
1. **Sync main** — `git pull origin main`.
2. **Changelog** — summarize notable changes since the last tag (README + release notes).
3. **Build & Test** — run the full build script and, if hardware is available, flash + quick smoke test.

```bash
ARDUINO_CLI_CONFIG=./arduino-cli.yaml \
  ./tools/arduino-build.sh --build-path .arduino-build --fqbn esp32:esp32:esp32
```

## Package Artifacts
Create a release bundle inside `dist/`.
```bash
mkdir -p dist
cp .arduino-build/bootbox_mcu_fw.unified.bin dist/
cp .arduino-build/bootbox_mcu_fw.spiffs.bin dist/
cp .arduino-build/flash-manifest.json dist/

cd dist
zip bootbox-mcu-fw-0.1.0.zip \
  bootbox_mcu_fw.unified.bin \
  bootbox_mcu_fw.spiffs.bin \
  flash-manifest.json
cd ..
```
Optional: add checksums (`sha256sum dist/*.zip > dist/bootbox-mcu-fw-0.1.0.sha256`).

## Tag & Publish
```bash
git tag 0.1.0
git push origin main --tags

gh release create 0.1.0 dist/bootbox-mcu-fw-0.1.0.zip \
  --title "Release 0.1.0" \
  --notes-file docs/release-notes/0.1.0.md   # or --notes "Summary..."
```
When the wiki is ready for the public repo, copy any new pages to the GitHub wiki (`git push https://github.com/<user>/bootbox-mcu-fw.wiki.git`).

## Post-release
- Update `README.md` and `docs/wiki/Home.md` with the new version if needed.
- Archive serial logs, test evidence, or bundle presets under `docs/releases/`.
- Start the next milestone by opening issues for TODO items discovered during packaging.

## TODO
- Automate artifact packaging, checksum generation, and GitHub release creation via CI.
