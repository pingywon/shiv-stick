#!/bin/bash
# Build the firmware and publish a ready-to-flash copy into the Arduino sketchbook:
#   ~/Arduino/SHIV/            <- open SHIV.ino from here in the Arduino IDE
#   ~/Arduino/SHIV/extras/     <- README, web sources, tools, rendered screens, firmware .bin files
set -e
cd "$(dirname "$0")/.."
DEST="${1:-$HOME/Arduino/SHIV}"
FQBN="m5stack:esp32:m5stack_sticks3:PartitionScheme=default_8MB,PSRAM=opi,CDCOnBoot=cdc"
python3 tools/embed_web.py
mkdir -p firmware
arduino-cli compile --fqbn "$FQBN" --output-dir firmware SHIV | tail -2
mkdir -p "$DEST/extras"
rm -f "$DEST"/*.h "$DEST"/*.ino
cp SHIV/*.h SHIV/*.ino "$DEST/"
rm -rf "$DEST/extras/web" "$DEST/extras/tools" "$DEST/extras/firmware"
mkdir -p "$DEST/extras/firmware" "$DEST/extras/tools/render/build"
cp README.md "$DEST/extras/"
cp -r web "$DEST/extras/web"
cp tools/*.py tools/*.sh tools/*.js "$DEST/extras/tools/"
cp tools/render/*.cpp tools/render/*.sh tools/render/*.py "$DEST/extras/tools/render/"
[ -d tools/render/build/sheets ] && cp -r tools/render/build/sheets "$DEST/extras/tools/render/build/sheets"
[ -d tools/render/build/shots ] && cp -r tools/render/build/shots "$DEST/extras/tools/render/build/shots"
cp firmware/SHIV.ino.bin firmware/SHIV.ino.merged.bin firmware/SHIV.ino.bootloader.bin firmware/SHIV.ino.partitions.bin "$DEST/extras/firmware/" 2>/dev/null || cp firmware/*.bin "$DEST/extras/firmware/"
cp SHIV/build/m5stack.esp32.m5stack_sticks3/boot_app0.bin "$DEST/extras/firmware/" 2>/dev/null || true   # the flash kit (make_zip) needs all four images
ls -la "$DEST" | head -40
ls -la "$DEST/extras/firmware"
