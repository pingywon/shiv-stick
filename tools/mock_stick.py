#!/usr/bin/env python3
"""Mock SHIV stick: serves the REAL web/index.html + web/setup.html and fakes the firmware API,
so the control panel can be previewed and tested with no hardware.
    python3 tools/mock_stick.py [port]           -> http://<this-box>:8161/
    python3 tools/mock_stick.py 8161 --portal    -> first-boot Wi-Fi setup page
"""
import json, os, sys, time, glob
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
PORT = int(sys.argv[1]) if len(sys.argv) > 1 and sys.argv[1].isdigit() else 8161
PORTAL = "--portal" in sys.argv
T0 = time.time()
SCREENS = ["CLOCK", "TIMER", "POMODORO", "WEATHER", "PI-HOLE", "HOSTS", "SHOP", "JOBS", "HA CONTROL", "MACROS", "PAGER",
           "WIFI", "CHANNELS", "BLE", "IR REMOTE", "TV-OFF", "LEVEL", "ORACLE", "GAMES", "DAEMON", "SYSTEM", "REFLEX", "DOORBELL", "MQTT", "SPECTRUM"]
SHOT = {0: "clock_face0", 1: "timer_running", 2: "pomo_focus", 3: "weather_partly", 4: "pihole", 5: "hosts", 6: "shop", 7: "jobs",
        8: "ha_list", 9: "macros", 10: "pager_p1", 11: "wifi_list", 12: "channels", 13: "ble_list", 14: "ir_list",
        15: "tvoff_ready", 16: "level_tilted", 17: "oracle_8ball_7", 18: "games_menu", 19: "daemon_m1_0", 20: "system_row1",
        21: "reflex_solo_1", 22: "door_list", 23: "mqtt_list", 24: "spectrum_live"}
THEMES = [{"name": n, "c": c} for n, c in (
    ("NEON", [0x04050E, 0x121A36, 0xEAF6FF, 0x8FA3CC, 0x00F0FF, 0xFF2BD6, 0x7A5CFF, 0xFFE14A, 0xFF4058, 0x3DFF8B]),
    ("MATRIX", [0x010A03, 0x07240F, 0x9DFFB4, 0x4FBF6C, 0x39FF6A, 0xD6FFE0, 0x1FBF4F, 0xE6FF5A, 0xFF6A4A, 0x39FF6A]),
    ("AMBER", [0x0B0500, 0x2A1600, 0xFFCF80, 0xC98A2E, 0xFFAB00, 0xFFE6B0, 0xFF7A1A, 0xFFF07A, 0xFF5030, 0xFFD040]))]

SECRETS = ["webPass", "pi5Token", "pi6Pass", "haToken", "shopToken", "shopClientSecret", "mqttPass"]
state = {"screen": 0, "pages": [], "log": [], "piEnabled": True, "piOffUntil": 0, "haOk": True,
         "ota": {"avail": False, "ver": "", "url": "", "size": 0, "want": False, "installing": False, "prog": 0, "done": False, "err": ""}}
cfg = {
    "wifi": [{"ssid": "HomeNet", "pass": "x"}, {"ssid": "Workshop", "pass": "y"}, {"ssid": "", "pass": ""}, {"ssid": "", "pass": ""}, {"ssid": "", "pass": ""}], "host": "shiv", "tz": "EST5EDT,M3.2.0,M11.1.0",
    "lat": 40.0, "lon": -75.0, "place": "Your City", "pi5Url": "http://<pi-hole-v5-ip>:81", "pi6Url": "http://<pi-hole-v6-ip>",
    "haUrl": "http://<home-assistant-ip>:8123", "shopDomain": "", "shopClientId": "", "shopApiVer": "2025-10",
    "jobsUrl": "", "otaUrl": "http://<your-server>/shiv/latest.json", "apiKey": "mock0123456789ab",
    "mqttHost": "", "mqttPort": 1883, "mqttUser": "", "mqttTopics": "#",
    "themes": THEMES,
    "webPass": "", "pi5Token": "", "pi6Pass": "secret", "haToken": "", "shopToken": "", "shopClientSecret": "", "mqttPass": "",
    "set": {"theme": 0, "volume": 60, "mute": False, "brightness": 160, "flip": 0, "dimS": 20, "offS": 45, "sleepMin": 5,
            "clockFace": 0, "screenMask": 0xFFFFFFFF, "piSource": 6, "focusMin": 25, "breakMin": 5, "piTarget": 5, "piOffHours": 10,
            "favOrder": [0, 15, 10, 3, 20], "cfgVer": 1,
            "micOn": False, "micSens": 5, "spectrumIdle": False, "petHears": False, "otaOn": True},
    "hosts": [{"name": "NAS", "addr": "192.168.1.10", "port": 445}, {"name": "SERVER", "addr": "192.168.1.11", "port": 80}],
    "favs": [{"label": "DESK LAMP", "entity": "light.desk_lamp"}], "macros": [{"label": "PING ROUTER", "url": "http://192.168.1.1/", "method": "GET", "body": ""}],
}
ir = [{"name": "TV POWER", "len": 67}, {"name": "IR-02", "len": 71}]


def ota_json():
    o = state["ota"]
    return {"ok": True, "on": cfg["set"]["otaOn"], "cur": "1.0.0-mock", "avail": o["avail"], "ver": o["ver"],
            "url": o["url"], "size": o["size"], "installing": o["installing"], "prog": o["prog"], "done": o["done"], "err": o["err"]}


def public_cfg():
    c = {k: v for k, v in cfg.items() if k not in SECRETS and k != "wifi"}
    c["wifi"] = [{"ssid": w["ssid"], "passSet": bool(w.get("pass"))} for w in cfg["wifi"]]
    for s in SECRETS:
        c[s + "Set"] = bool(cfg[s])
    c["screens"] = SCREENS
    c["ir"] = ir
    return c


def apply_cfg(d):
    for k, v in d.items():
        if k in SECRETS:
            if v == "-": cfg[k] = ""
            elif v: cfg[k] = v
        elif k == "wifi":
            for i, w in enumerate(v[:2]):
                if "ssid" in w: cfg["wifi"][i]["ssid"] = w["ssid"]
                if w.get("pass") == "-": cfg["wifi"][i]["pass"] = ""
                elif w.get("pass"): cfg["wifi"][i]["pass"] = w["pass"]
                if not cfg["wifi"][i]["ssid"]: cfg["wifi"][i]["pass"] = ""
        elif k == "set": cfg["set"].update(v)
        elif k in ("hosts", "favs", "macros"): cfg[k] = v[:{"hosts": 8}.get(k, 6)]
        elif k in cfg: cfg[k] = v


def screen_raw():
    """Current screen as big-endian RGB565, taken from the render harness output if it exists."""
    from PIL import Image
    name = SHOT.get(state["screen"], "clock_face0")
    hits = glob.glob(os.path.join(ROOT, "tools", "render", "build", "shots", f"t{cfg['set']['theme']}_{name}.png")) or \
           glob.glob(os.path.join(ROOT, "tools", "render", "build", "shots", f"t0_{name}.png"))
    im = Image.open(hits[0]).convert("RGB") if hits else Image.new("RGB", (240, 135), (4, 5, 14))
    out = bytearray()
    for r, g, b in im.getdata():
        v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
        out += bytes((v >> 8, v & 255))
    return bytes(out)


class H(BaseHTTPRequestHandler):
    def log_message(self, *a): pass

    def send(self, code, body, ctype="application/json"):
        if isinstance(body, (dict, list)): body = json.dumps(body)
        if isinstance(body, str): body = body.encode()
        self.send_response(code); self.send_header("Content-Type", ctype); self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store"); self.end_headers(); self.wfile.write(body)

    def body(self):
        n = int(self.headers.get("Content-Length") or 0)
        raw = self.rfile.read(n) if n else b""
        try: return json.loads(raw or b"{}"), raw
        except Exception: return {}, raw

    def do_GET(self):
        u = urlparse(self.path)
        if u.path == "/":
            page = "setup.html" if PORTAL else "index.html"
            return self.send(200, open(os.path.join(ROOT, "web", page), "rb").read(), "text/html")
        if u.path == "/gallery":    # every rendered device screen, straight from the render harness
            sheets = sorted(glob.glob(os.path.join(ROOT, "tools", "render", "build", "sheets", "theme*_sheet*.png")))
            body = "<!doctype html><meta name=viewport content='width=device-width'><title>SHIV screens</title>" \
                   "<body style='background:#04050e;color:#eaf6ff;font:18px ui-monospace,monospace;padding:16px'>" \
                   "<h1 style='color:#00f0ff;letter-spacing:6px'>SHIV // every screen</h1>" \
                   "<p style='color:#8fa3cc'>Rendered by the real firmware drawing code at 240x135, shown 2x. " \
                   "<a style='color:#ff2bd6' href='/'>Open the control panel preview</a></p>" + \
                   "".join(f"<h2 style='color:#ff2bd6'>{os.path.basename(s)[:-4]}</h2><img style='max-width:100%;image-rendering:pixelated' src='/sheets/{os.path.basename(s)}'>" for s in sheets)
            return self.send(200, body, "text/html")
        if u.path.startswith("/sheets/"):
            p = os.path.join(ROOT, "tools", "render", "build", "sheets", os.path.basename(u.path))
            if os.path.isfile(p): return self.send(200, open(p, "rb").read(), "image/png")
            return self.send(404, "not found", "text/plain")
        if u.path == "/test":       # the real page + the automated test driver
            raw = open(os.path.join(ROOT, "web", "index.html"), "rb").read()
            head, sep, tail = raw.rpartition(b"</body>")      # only the real one: the page's JS mentions </body> inside a string
            html = head + b'<script src="/test.js"></script>' + sep + tail
            return self.send(200, html, "text/html")
        if u.path == "/test.js": return self.send(200, open(os.path.join(HERE, "panel_test.js"), "rb").read(), "application/javascript")
        if u.path == "/api/status":
            up = int(time.time() - T0)
            return self.send(200, {"version": "1.0.0-mock", "ip": "192.168.1.42", "ssid": "HomeNet", "rssi": -58, "batt": 82, "usb": True, "vbus": 5040, "pwrSrc": 0, "charging": True,
                                   "heap": 214, "psram": 7900, "uptime": up + 11565, "screen": state["screen"], "screenName": SCREENS[state["screen"]],
                                   "piKnown": True, "piEnabled": state["piEnabled"], "piTarget": cfg["set"]["piTarget"], "piOffHours": cfg["set"]["piOffHours"],
                                   "piOffUntil": state["piOffUntil"], "epoch": int(time.time()),
                                   "unread": len(state["pages"]), "mood": "HAPPY / FOOD 72 / FUN 85", "awake": True, "time": time.strftime("%a %H:%M:%S"),
                                   "resetWhy": "POWER ON", "heapInt": 180, "heapMin": 150, "heapBig": 90, "loopHwm": 3100, "netHwm": 9000,
                                   "mqtt": False, "mqttErr": "",
                                   "otaOn": cfg["set"]["otaOn"], "otaAvail": state["ota"]["avail"], "otaVer": state["ota"]["ver"],
                                   "otaInstalling": state["ota"]["installing"], "otaProg": state["ota"]["prog"], "otaErr": state["ota"]["err"]})
        if u.path == "/api/config": return self.send(200, public_cfg())
        if u.path == "/api/ota": return self.send(200, ota_json())
        if u.path in ("/favicon.ico", "/favicon.svg"): return self.send(200, "<svg xmlns='http://www.w3.org/2000/svg'/>", "image/svg+xml")
        if u.path == "/api/screen.raw": return self.send(200, screen_raw(), "application/octet-stream")
        if u.path == "/api/ha/test":
            if cfg["haToken"] == "bad": return self.send(200, {"ok": False, "msg": "Home Assistant rejected the token (401). Make a new long-lived token and paste it again."})
            return self.send(200, {"ok": True, "msg": 'Connected to "Home" - Home Assistant 2026.9.1. Open HA CONTROL on the stick.'})
        if u.path == "/api/ha/entities":
            return self.send(200, [{"id": "light.desk_lamp", "name": "Desk Lamp"}, {"id": "scene.movie", "name": "Movie <b>Night</b>"},
                                   {"id": "switch.porch", "name": "Porch \"Light\""}])
        if u.path == "/api/scan":
            return self.send(200, {"nets": [{"ssid": "HomeNet", "rssi": -48, "open": False}, {"ssid": "<img src=x onerror=alert(1)>", "rssi": -70, "open": True}]})
        if u.path == "/api/page": return self.do_POST()
        if u.path == "/api/log": return self.send(200, state["log"])
        return self.send(404, "not found", "text/plain")

    def do_POST(self):
        u = urlparse(self.path)
        d, raw = self.body()
        state["log"].append({"path": u.path, "body": d if d else raw.decode("latin1")[:200]})
        if u.path == "/api/config": apply_cfg(d); return self.send(200, {"ok": True})
        if u.path == "/api/page":
            q = parse_qs(u.query); form = parse_qs(raw.decode("latin1")) if raw and not d else {}
            msg = d.get("msg") or (form.get("msg") or q.get("msg") or [""])[0]
            if not msg: return self.send(400, "msg is empty", "text/plain")
            state["pages"].insert(0, msg); state["screen"] = 10
            return self.send(200, {"ok": True})
        if u.path == "/api/pihole":
            en = d.get("enable", not state["piEnabled"])
            state["piEnabled"] = bool(en)
            state["piOffUntil"] = 0 if en else int(time.time()) + cfg["set"]["piOffHours"] * 3600
            return self.send(200, {"ok": True})
        if u.path == "/api/pages/clear": state["pages"].clear(); return self.send(200, {"ok": True})
        if u.path == "/api/screen": state["screen"] = int(d.get("id", 0)) % len(SCREENS); return self.send(200, {"ok": True})
        if u.path.startswith("/api/ir/"):
            i = int(d.get("i", -1))
            if not 0 <= i < len(ir): return self.send(404, "no such code", "text/plain")
            if u.path.endswith("delete"): ir.pop(i)
            elif u.path.endswith("rename"): ir[i]["name"] = str(d.get("name", "")).upper()[:13]
            return self.send(200, {"ok": True})
        if u.path == "/api/ota":
            act = (d or {}).get("action", "")
            o = state["ota"]
            if act == "check": o.update({"avail": True, "ver": "9.9.9", "url": cfg["otaUrl"], "size": 2000000, "err": ""})
            elif act == "install" and o["avail"]: o.update({"want": True, "installing": True, "prog": 0})
            elif act == "snooze": o["avail"] = False
            return self.send(200, ota_json())
        if u.path in ("/api/macro/run", "/api/fav/run", "/api/reboot", "/api/portal", "/api/setup"): return self.send(200, {"ok": True})
        if u.path == "/update": return self.send(200, "OK", "text/plain")
        return self.send(404, "not found", "text/plain")


if __name__ == "__main__":
    print(f"mock SHIV on http://0.0.0.0:{PORT}/  ({'portal' if PORTAL else 'panel'} mode)")
    ThreadingHTTPServer(("0.0.0.0", PORT), H).serve_forever()
