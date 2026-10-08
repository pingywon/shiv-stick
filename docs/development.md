# Development

Everything below runs on a PC; none of it needs the stick.

## The checks (all should pass before a release)
| # | Command | Pass |
|---|---|---|
| 1 | `arduino-cli compile --fqbn "m5stack:esp32:m5stack_sticks3:PartitionScheme=default_8MB,PSRAM=opi,CDCOnBoot=cdc" --warnings all SHIV` | 0 warnings from SHIV files |
| 2 | `tools/render/run.sh` | 0 FAIL (every screen state × every theme; WARN = long dynamic names only) |
| 3 | `tools/test_panel.sh` | all panel checks pass (headless Chromium vs the mock) |
| 4 | `g++ -std=c++17 -o /tmp/t tools/test_pet.cpp && /tmp/t` | all pass (daemon, navigation, microphone DSP, version compare) |
| 5 | `g++ -std=c++17 -o /tmp/a tools/test_ago.cpp && /tmp/a` | pass |

## Rules of the codebase
- Never write `now - x` for elapsed time: use `shiv::ago(now, x)` (millis wraps every ~49 days).
- New screens go at the **end** of the `ScreenId` enum (saved masks use the ids); `SCREEN_ORDER` sets the position,
  `SCREEN_GROUP` the GO TO group. `knownScreens` makes new screens switch on after an update.
- Drawing code is pure (`draw_*.h` take `const State&`), so the PC harness renders the real fonts and layout.
  Add a render shot for every new screen state.
- Anything that talks to Home Assistant or Pi-hole: reproduce against the real service on the PC first.
- Keep comments minimal.

## Tools
| Tool | What |
|---|---|
| `tools/render/run.sh` | renders every screen with the real M5GFX fonts, audits text size / clipping / overlap, writes contact sheets |
| `tools/mock_stick.py [port]` | a fake stick that serves the real panel, for the panel test |
| `tools/test_panel.sh` + `tools/panel_test.js` | panel checks. **Every new config key must also go in the mock's `cfg{}`** |
| `tools/embed_web.py` | packs `web/*.html` into `SHIV/web_ui.h` (gzip) — run it after any `web/` edit |
| `tools/export.sh` | compiles and copies a ready-to-flash sketch + `extras/` into an Arduino sketchbook |
| `tools/make_zip.py <fwdir> <outdir>` | builds the no-compile flash-kit zip |
| `tools/secret_scan.sh` | refuses to let credentials into a commit (run before every push) |

## Release steps
1. Bump `FW_VERSION` in `SHIV/config.h` and update `CHANGELOG.md`.
2. Run the five checks.
3. `tools/export.sh`, then `python3 tools/make_zip.py <sketchbook>/SHIV/extras/firmware <outdir>` and copy
   `SHIV.ino.bin` to `<outdir>/SHIV-<ver>.bin`. Host `<outdir>` wherever you serve downloads; point the stick's
   AUTO-UPDATE manifest at it for over-the-air installs.
4. `tools/secret_scan.sh`, then commit and push.

## Self-update manifest
The AUTO-UPDATE feature fetches a small JSON manifest (default `otaUrl` in `SHIV/config.h`) shaped like:
```json
{ "version": "1.1.0", "bin": "https://example/SHIV-1.1.0.bin", "size": 1923559 }
```
The stick offers the update when `version` is newer than its own; install downloads `bin` and flashes it.
