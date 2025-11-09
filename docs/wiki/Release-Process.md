# Release Process

## Checklist
- Ensure `main` is green (build + smoke tests pass).
- Run `tools/arduino-build.sh --config-file arduino-cli.yaml --build-path .arduino-build`.
- Package `bootbox_mcu_fw.unified.bin`, LittleFS image, and manifest into a zip tagged with the version.
- Tag the repo (`git tag 0.x.y && git push --tags`).
- `gh release create 0.x.y dist/bootbox-mcu-fw-0.x.y.zip -t "Release 0.x.y" -n "Summary"`.

## TODO
- Automate artifact packaging and checksum generation via CI.
