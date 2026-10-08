#!/usr/bin/env python3
"""Build index.html from src/index.src.html.

The page version comes from VERSION and the firmware version from
SHIV/config.h on main, so the page cannot drift from either.
"""
import pathlib
import re
import subprocess

ROOT = pathlib.Path(__file__).resolve().parent.parent
CACHE = ROOT / "tools" / "fw_version.txt"


def fw_version():
    try:
        cfg = subprocess.run(
            ["git", "-C", str(ROOT), "show", "origin/main:SHIV/config.h"],
            capture_output=True, text=True, check=True).stdout
        CACHE.write_text(re.search(r'FW_VERSION\s*=\s*"([^"]+)"', cfg).group(1) + "\n")
    except (subprocess.CalledProcessError, FileNotFoundError, AttributeError):
        pass
    return CACHE.read_text().strip()


page = (ROOT / "src" / "index.src.html").read_text(encoding="utf-8")
page = page.replace("{{VERSION}}", (ROOT / "VERSION").read_text().strip())
page = page.replace("{{FW_VERSION}}", fw_version())
assert "{{" not in page, "unfilled token left in the page"
(ROOT / "index.html").write_text(page, encoding="utf-8")
print("built index.html")
