#!/bin/bash
# Automated control-panel test: mock stick + headless Chromium. Prints PASS/FAIL per check
# and saves a screenshot of every tab to tools/render/build/panel/.
cd "$(dirname "$0")/.."
PORT=${1:-8179}
OUT=tools/render/build/panel
mkdir -p "$OUT"
python3 tools/mock_stick.py "$PORT" >/dev/null 2>&1 &
PID=$!
trap 'kill $PID 2>/dev/null' EXIT
sleep 1.2
CH="chromium --headless --no-sandbox --disable-gpu --hide-scrollbars --user-data-dir=$OUT/profile"
$CH --virtual-time-budget=20000 --dump-dom "http://127.0.0.1:$PORT/test" 2>/dev/null > "$OUT/test_dom.html"
python3 - "$OUT/test_dom.html" <<'EOF'
import sys, json, html, re
dom = open(sys.argv[1], encoding="utf-8", errors="replace").read()
m = re.search(r"TESTOUT(.*?)TESTEND", dom, re.S)
if not m:
    print("FAIL: test driver produced no output"); sys.exit(2)
r = json.loads(html.unescape(m.group(1)))
bad = 0
for k, v in r["checks"].items():
    print(("  ok   " if v == "PASS" else "  FAIL ") + k + ("" if v == "PASS" else "   " + v)); bad += v != "PASS"
for e in r["errors"]:
    print("  JS ERROR:", e); bad += 1
print(f"{len(r['checks'])} checks, {bad} problems")
sys.exit(1 if bad else 0)
EOF
RC=$?
for t in status joy stats screens data macros system; do
  $CH --window-size=1000,1500 --virtual-time-budget=6000 --screenshot="$OUT/tab_$t.png" "http://127.0.0.1:$PORT/#$t" >/dev/null 2>&1
done
$CH --window-size=420,900 --virtual-time-budget=6000 --screenshot="$OUT/phone_status.png" "http://127.0.0.1:$PORT/#status" >/dev/null 2>&1
rm -rf "$OUT/profile"
exit $RC
