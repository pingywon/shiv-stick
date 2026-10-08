#!/usr/bin/env python3
"""Build the self-contained download zip:
   SHIV-<ver>/READ-ME-FIRST.txt
   SHIV-<ver>/SHIV/               Arduino sketch (open SHIV.ino)
   SHIV-<ver>/FLASH-NO-COMPILE/   prebuilt firmware parts + flash.bat (Windows, uses Arduino's esptool.exe)
   SHIV-<ver>/extras/             README, web sources, dev tools
Usage: python3 tools/make_zip.py <dir with SHIV.ino.bin etc> <output dir>"""
import os, re, sys, zipfile, glob
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
fw, outdir = sys.argv[1], sys.argv[2]
ver = re.search(r'FW_VERSION = "([^"]+)"', open(os.path.join(ROOT, "SHIV", "config.h")).read()).group(1)
top = f"SHIV-{ver}"
os.makedirs(outdir, exist_ok=True)
out = os.path.join(outdir, f"{top}.zip")
boot_app0 = sorted(glob.glob(os.path.expanduser("~/.arduino15/packages/m5stack/hardware/esp32/*/tools/partitions/boot_app0.bin")))[-1]

def crlf(path):
    return open(path, "rb").read().replace(b"\r\n", b"\n").replace(b"\n", b"\r\n")

with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    z.writestr(f"{top}/READ-ME-FIRST.txt", crlf(os.path.join(HERE, "flashkit", "READ-ME-FIRST.txt")))
    for f in sorted(os.listdir(os.path.join(ROOT, "SHIV"))):
        p = os.path.join(ROOT, "SHIV", f)
        if os.path.isfile(p) and f.endswith((".h", ".ino")):
            z.write(p, f"{top}/SHIV/{f}")
    kit = f"{top}/FLASH-NO-COMPILE"
    z.writestr(f"{kit}/flash.bat", crlf(os.path.join(HERE, "flashkit", "flash.bat")))
    for f in ("SHIV.ino.bin", "SHIV.ino.bootloader.bin", "SHIV.ino.partitions.bin"):
        z.write(os.path.join(fw, f), f"{kit}/{f}")
    z.write(boot_app0, f"{kit}/boot_app0.bin")
    z.write(os.path.join(ROOT, "README.md"), f"{top}/extras/README.md")
    for sub in ("web", "tools"):
        for r, dirs, files in os.walk(os.path.join(ROOT, sub)):
            if os.sep + "build" in r or "__pycache__" in r:
                continue
            for f in files:
                p = os.path.join(r, f)
                z.write(p, f"{top}/extras/" + os.path.relpath(p, ROOT))
    names = z.namelist()
print(out, len(names), "files", round(os.path.getsize(out) / 1024), "KB")
