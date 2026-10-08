#!/bin/bash
# Build + run the SHIV screen render harness, then assemble contact sheets.
# Usage: tools/render/run.sh            (needs g++, python3 + Pillow, M5GFX in ~/Arduino/libraries)
set -e
cd "$(dirname "$0")"
S="${M5GFX_SRC:-$HOME/Arduino/libraries/M5GFX/src}"
mkdir -p build/obj
if [ ! -f build/liblgfx.a ]; then
  echo "building M5GFX for the PC (one time)..."
  SRCS="$(ls $S/lgfx/v1/*.cpp $S/lgfx/v1/misc/*.cpp $S/lgfx/v1/panel/Panel_Device.cpp $S/lgfx/utility/*.c $S/lgfx/utility/*.cpp $S/lgfx/Fonts/efont/*.c $S/lgfx/Fonts/IPA/*.c) $S/lgfx/v1/platforms/framebuffer/common.cpp"
  for f in $SRCS; do
    o=build/obj/$(echo "$f" | md5sum | cut -c1-8)_$(basename "$f").o
    case "$f" in
      *.c) gcc -O1 -w -DLGFX_LINUX_FB -I"$S" -c "$f" -o "$o" ;;
      *)   g++ -std=c++17 -O1 -w -DLGFX_LINUX_FB -I"$S" -c "$f" -o "$o" ;;
    esac
  done
  ar rcs build/liblgfx.a build/obj/*.o
fi
g++ -std=c++17 -O1 -Wall -Wno-unused-function -Wno-unused-variable -DSHIV_HOST -DLGFX_LINUX_FB -I"$S" render.cpp build/liblgfx.a -o build/render
rm -rf build/shots && mkdir -p build/shots
set +e
./build/render build/shots
RC=$?
set -e
python3 sheet.py build/shots build/sheets
echo "---- audit ----"
grep -E "FAIL|WARN" build/shots/audit.txt | sort | uniq -c | sort -rn | head -60 || true
tail -1 build/shots/audit.txt
exit $RC
