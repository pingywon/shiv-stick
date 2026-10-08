#!/usr/bin/env python3
"""Turn the harness PPM frames into PNGs and labelled contact sheets (3x scale)."""
import sys, os, glob
from PIL import Image, ImageDraw

src, out = sys.argv[1], sys.argv[2]
os.makedirs(out, exist_ok=True)
SCALE, COLS, PER = 2, 4, 24
for theme in (0, 1, 2):
    files = sorted(glob.glob(os.path.join(src, f"t{theme}_*.ppm")), key=os.path.getmtime)
    if theme:  # other themes: a representative subset is enough for the sheets
        keep = {"clock_face0", "clock_face3", "weather_partly", "pihole", "home_focus", "wifi_list", "level_tilted", "daemon_m1_0", "system_row1", "game_runner", "channels", "pager_p1", "presets_list", "launcher_groups", "joy_linked"}
        files = [f for f in files if os.path.basename(f)[3:-4] in keep]
    for f in files:
        Image.open(f).save(f[:-4] + ".png")
    for page in range(0, len(files), PER):
        chunk = files[page:page + PER]
        rows = (len(chunk) + COLS - 1) // COLS
        cw, ch = 240 * SCALE + 8, 135 * SCALE + 22
        sheet = Image.new("RGB", (cw * COLS, ch * rows), (30, 30, 34))
        d = ImageDraw.Draw(sheet)
        for i, f in enumerate(chunk):
            im = Image.open(f).resize((240 * SCALE, 135 * SCALE), Image.NEAREST)
            x, y = (i % COLS) * cw + 4, (i // COLS) * ch + 18
            sheet.paste(im, (x, y))
            d.text((x, y - 14), os.path.basename(f)[3:-4], fill=(220, 220, 220))
        sheet.save(os.path.join(out, f"theme{theme}_sheet{page // PER + 1}.png"))
for f in glob.glob(os.path.join(src, "*.ppm")):
    os.remove(f)
print("sheets:", sorted(os.listdir(out)))
