// SHIV - on-device web server: control panel, script API, captive Wi-Fi setup, OTA update.
#pragma once
#include <WebServer.h>
#include <Update.h>
#include "app.h"
#include "web_ui.h"

namespace shiv {
namespace web {

static WebServer server(80);
static Canvas* canvas = nullptr;
static bool otaDenied = false, otaBegan = false, started = false;
static uint32_t rebootAt = 0;

inline bool authed() {
  if (!CFG.webPass[0]) return true;            // setup mode does NOT bypass this: only /, /api/scan and /api/setup are open there
  if (server.authenticate("shiv", CFG.webPass)) return true;
  server.requestAuthentication(BASIC_AUTH, "SHIV", "user: shiv");
  return false;
}

inline void sendGz(const uint8_t* page, size_t len) {
  server.sendHeader("Content-Encoding", "gzip");
  server.sendHeader("Cache-Control", "no-cache");
  server.send_P(200, "text/html", (const char*)page, len);
}

inline void sendJson(JsonDocument& d, int code = 200) {
  String out;
  serializeJson(d, out);
  server.send(code, "application/json", out);
}
inline void sendOk() { server.send(200, "application/json", "{\"ok\":true}"); }
inline void sendErr(int code, const char* msg) { server.send(code, "text/plain", msg); }

inline bool body(JsonDocument& d) {
  if (!server.hasArg("plain")) return false;
  return !deserializeJson(d, server.arg("plain"));
}

inline void asciiFold(String& s) {                 // the screen font is ASCII only
  for (size_t i = 0; i < s.length(); i++) {
    uint8_t ch = (uint8_t)s[i];
    if (ch < 32 && ch != '\n') s.setCharAt(i, ' ');
    else if (ch > 126) s.setCharAt(i, '?');
  }
}

// ---------------------------------------------------------------- handlers
inline void hRoot() {
  if (G.sys.portal) { sendGz(PAGE_SETUP, PAGE_SETUP_LEN); return; }
  if (!authed()) return;
  sendGz(PAGE_INDEX, PAGE_INDEX_LEN);
}

inline void hStatus() {
  if (!authed()) return;
  JsonDocument d;
  {
  Lock l;
  d["version"] = FW_VERSION; d["ip"] = G.sys.ip; d["ssid"] = G.sys.ssid; d["rssi"] = G.sys.rssi;
  d["batt"] = G.sys.battPct; d["mv"] = G.sys.battMv; d["usb"] = G.sys.usb; d["vbus"] = G.sys.vbusMv; d["pwrSrc"] = G.sys.pwrSrc; d["charging"] = G.sys.charging; d["heap"] = G.sys.heapKb; d["psram"] = G.sys.psramKb;
  d["uptime"] = G.sys.uptimeS; d["resetWhy"] = G.sys.resetWhy; d["heapInt"] = G.sys.heapIntKb; d["heapMin"] = G.sys.heapMinKb; d["heapBig"] = G.sys.heapBigKb;
  d["loopHwm"] = G.sys.loopHwm; d["netHwm"] = G.sys.netHwm; d["screen"] = (int)G.screen; d["screenName"] = SCREEN_NAMES[G.screen % SC_COUNT];
  d["piKnown"] = G.pi.at != 0 && G.pi.source == G.set.piTarget; d["piEnabled"] = G.pi.enabled; d["piTarget"] = G.set.piTarget;
  d["piOffHours"] = G.set.piOffHours; d["piOffUntil"] = G.set.piOffUntil; d["epoch"] = G.epoch;
  d["mqtt"] = G.mqtt.connected; d["mqttErr"] = G.mqtt.err;
  d["otaOn"] = G.set.otaOn; d["otaAvail"] = G.ota.avail; d["otaVer"] = G.ota.ver; d["otaInstalling"] = G.ota.installing; d["otaProg"] = G.ota.prog; d["otaErr"] = G.ota.err;
  d["unread"] = G.pager.unread; char mood[40]; snprintf(mood, sizeof(mood), "%s / FOOD %d / FUN %d", pet::MOOD_NAMES[G.dmn.mood % MD_N], G.dmn.food, G.dmn.fun); d["mood"] = mood; d["awake"] = hw::screenOn();
  if (G.sys.timeValid) { char t[24]; strftime(t, sizeof(t), "%a %H:%M:%S", &G.lt); d["time"] = t; }
  }
  sendJson(d);
}

// Self-update: GET = state, POST {"action":"check"|"install"|"snooze"}. Install only starts
// if an update is already on offer; the device then downloads + flashes + reboots on its own.
inline void hOta() {
  if (!authed()) return;
  if (server.method() == HTTP_POST) {
    JsonDocument d; body(d);
    const char* act = d["action"] | "";
    Lock l;
    if (!strcmp(act, "check")) fetch::dOta.force = true;                         // re-poll the manifest now
    else if (!strcmp(act, "install")) { if (G.ota.avail && !G.ota.installing) G.ota.want = true; }
    else if (!strcmp(act, "snooze")) G.ota.snoozeUntil = millis() + 4UL * 3600000UL;
  }
  JsonDocument r;
  { Lock l;
    r["ok"] = true; r["on"] = G.set.otaOn; r["cur"] = FW_VERSION;
    r["avail"] = G.ota.avail; r["ver"] = G.ota.ver; r["url"] = G.ota.url; r["size"] = G.ota.size;
    r["installing"] = G.ota.installing; r["prog"] = G.ota.prog; r["done"] = G.ota.done; r["err"] = G.ota.err;
  }
  sendJson(r);
}

inline void hConfigGet() {
  if (!authed()) return;
  JsonDocument d(psAlloc());
  {
    Lock l;
    configToJson(d, false);
    JsonArray sc = d["screens"].to<JsonArray>();
    for (int i = 0; i < SC_COUNT; i++) sc.add(SCREEN_NAMES[i]);
    JsonArray irs = d["ir"].to<JsonArray>();
    for (int i = 0; i < G.ir.n; i++) { JsonObject o = irs.add<JsonObject>(); o["name"] = G.ir.c[i].name; o["len"] = G.ir.c[i].len; }
  }
  sendJson(d);
}

inline void hConfigPost() {
  if (!authed()) return;
  JsonDocument d(psAlloc());
  if (!body(d)) { sendErr(400, "bad json"); return; }
  if (d["pet"].is<JsonObject>()) { JsonObject pt = d["pet"]; pt.remove("at"); pt.remove("seen"); }   // a restored backup must not rewind the daemon's clock
  {
    Lock l;
    uint8_t src = G.pi.source;
    configFromJson(d);
    if (G.pi.source != src) { uint8_t s = G.pi.source; G.pi = Pihole(); G.pi.source = s; fetch::piSid[0] = 0; }
    hw::applyVolume();
    setenv("TZ", CFG.tz, 1);
    tzset();
    if (hw::light == hw::L_ON) M5.Display.setBrightness(G.set.brightness);
    if (!screenEnabled(G, G.screen)) app::enterScreen(SC_CLOCK);
    configSave();
  }
  fetch::dWx.force = fetch::dPi.force = fetch::dHosts.force = fetch::dShop.force = fetch::dJobs.force = fetch::dHa.force = true;
  fetch::shopTok[0] = 0;
  fetch::haTplDenied = false;
  sendOk();
}

inline void hPage() {
  bool keyOk = CFG.apiKey[0] && server.hasArg("key") && server.arg("key") == CFG.apiKey;
  if (!keyOk && !authed()) return;
  String msg = server.arg("msg"), from = server.arg("from");
  JsonDocument d;
  if (!msg.length() && body(d)) { msg = d["msg"] | ""; from = d["from"] | ""; }
  msg.trim();
  if (!msg.length()) { sendErr(400, "msg is empty"); return; }
  if (!screenEnabled(G, SC_PAGER)) { sendErr(409, "the PAGER screen is switched off on this stick"); return; }
  for (size_t i = 0; i < msg.length(); i++) if ((uint8_t)msg[i] < 32 && msg[i] != '\n') msg.setCharAt(i, ' ');
  for (size_t i = 0; i < msg.length(); i++) if ((uint8_t)msg[i] > 126) msg.setCharAt(i, '?');   // the screen font is ASCII
  { Lock l; app::pageIn(msg.c_str(), from.c_str()); }
  sendOk();
}

// UniFi Protect Alarm Manager (or any script) POSTs a door/camera alert here.
// Simple contract: /api/doorbell?key=<apiKey> with cam=&what= (query or JSON {"cam","what"}).
// Also parses Protect's native body: triggers[0].key -> what, triggers[0].device -> cam.
inline void hDoorbell() {
  bool keyOk = CFG.apiKey[0] && server.hasArg("key") && server.arg("key") == CFG.apiKey;
  if (!keyOk && !authed()) return;
  String cam = server.arg("cam"), what = server.arg("what");
  if (!cam.length())  cam  = server.arg("camera");
  if (!cam.length())  cam  = server.arg("source");
  if (!what.length()) what = server.arg("event");
  if (!what.length()) what = server.arg("type");
  JsonDocument d;
  if (body(d)) {
    if (!cam.length())  cam  = (const char*)(d["cam"]  | d["camera"] | d["source"] | "");
    if (!what.length()) what = (const char*)(d["what"] | d["event"]  | d["type"] | d["msg"] | "");
    JsonVariant tr = d["triggers"];
    if (tr.is<JsonArray>() && tr.size() > 0) {
      if (!what.length()) what = (const char*)(tr[0]["key"] | "");
      if (!cam.length())  cam  = (const char*)(tr[0]["device"] | "");
    }
  }
  cam.trim(); what.trim();
  if (!cam.length() && !what.length()) { sendErr(400, "need cam or what"); return; }
  if (!screenEnabled(G, SC_DOORBELL)) { sendErr(409, "the DOORBELL screen is switched off on this stick"); return; }
  asciiFold(cam); asciiFold(what);
  { Lock l; app::doorbellIn(cam.c_str(), what.c_str()); }
  sendOk();
}

// Script / panel control of the Pi-hole on/off button: {"enable":true|false}, or {} to toggle.
inline void hPihole() {
  bool keyOk = CFG.apiKey[0] && server.hasArg("key") && server.arg("key") == CFG.apiKey;
  if (!keyOk && !authed()) return;
  JsonDocument d;
  body(d);
  uint8_t mode = d["enable"].is<bool>() ? (d["enable"].as<bool>() ? 1 : 2) : 0;
  if (!fetch::request(fetch::A_PIHOLE, mode)) { sendErr(503, "stick is busy - try again"); return; }
  { Lock l; G.pi.toggling = true; }
  sendOk();
}

// JOY: /api/joy?key=<apiKey>&stroke=LOW|MED|HIGH|OFF|0-255&air=0-3 (query or JSON), preset=i, program=i, stop=1,
// raw=HEX (2-20 bytes, LAB), or the protocol slots m1..m4=0-255 pump=0-7. GET with no values just reports.
inline int hexBytes(String s, uint8_t* out, int max) {
  int n = 0, hi = -1;
  for (size_t i = 0; i < s.length(); i++) {
    char ch = s[i];
    int d = ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : ch >= 'A' && ch <= 'F' ? ch - 'A' + 10 : -1;
    if (d < 0) { if (ch == ' ' || ch == ':' || ch == ',') continue; return -1; }
    if (hi < 0) hi = d; else { if (n >= max) return -1; out[n++] = (uint8_t)(hi * 16 + d); hi = -1; }
  }
  return hi < 0 ? n : -1;
}

inline void hHaTest() {
  if (!authed()) return;
  String msg;
  int code = fetch::haTest(msg);
  JsonDocument d;
  d["ok"] = code == 200; d["msg"] = msg;
  sendJson(d);
}

inline void hPagesClear() {
  if (!authed()) return;
  { Lock l; G.pager = Pager(); }
  sendOk();
}

inline void hScreen() {
  if (!authed()) return;
  JsonDocument d;
  if (!body(d)) { sendErr(400, "bad json"); return; }
  int id = d["id"] | -1;
  if (id < 0 || id >= SC_COUNT) { sendErr(400, "bad id"); return; }
  { Lock l; hw::poke(); app::enterScreen(id); }
  sendOk();
}

inline void hScreenRaw() {
  if (!authed()) return;
  if (!canvas || !canvas->getBuffer()) { sendErr(503, "no canvas"); return; }
  size_t len = (size_t)W * H * 2;
  server.sendHeader("Cache-Control", "no-store");
  server.setContentLength(len);
  server.send(200, "application/octet-stream", "");
  server.sendContent((const char*)canvas->getBuffer(), len);
}

inline void hIr(const char* action) {
  if (!authed()) return;
  JsonDocument d;
  if (!body(d)) { sendErr(400, "bad json"); return; }
  int i = d["i"] | -1;
  Lock l;
  if (i < 0 || i >= G.ir.n) { sendErr(404, "no such code"); return; }
  if (!strcmp(action, "send")) { ir::sendSaved(i); }
  else if (!strcmp(action, "delete")) { ir::remove(i); }
  else {
    String nm = d["name"] | "";
    nm.trim(); nm.toUpperCase();
    if (!nm.length()) { sendErr(400, "name is empty"); return; }
    scopy(G.ir.c[i].name, nm.c_str());
    ir::saveIndex();
  }
  sendOk();
}

inline void hRun(fetch::ActType t) {
  if (!authed()) return;
  JsonDocument d;
  if (!body(d)) { sendErr(400, "bad json"); return; }
  fetch::request(t, (uint8_t)(d["i"] | 0));
  sendOk();
}

// Entity picker: stream Home Assistant's (large) state list through a JSON filter, keep only what a
// favourite can drive, and hand the browser a short {id,name} list.
inline void hHaEntities() {
  if (!authed()) return;
  if (!CFG.haToken[0]) { sendErr(400, "no Home Assistant token saved yet"); return; }
  if (!net::online()) { sendErr(503, "stick is offline"); return; }
  NetworkClient plain;
  NetworkClientSecure sec;
  HTTPClient http;
  String url = String(CFG.haUrl) + "/api/states";
  bool okb;
  if (url.startsWith("https://")) { sec.setInsecure(); okb = http.begin(sec, url); } else okb = http.begin(plain, url);
  if (!okb) { sendErr(502, "bad address"); return; }
  http.useHTTP10(true);
  http.setTimeout(12000);
  http.addHeader("Authorization", String("Bearer ") + CFG.haToken);
  int code = http.GET();
  if (code != 200) { http.end(); sendErr(502, code == 401 ? "Home Assistant rejected the token" : "Home Assistant did not answer"); return; }
  JsonDocument filter, d(psAlloc());
  filter[0]["entity_id"] = true;
  filter[0]["attributes"]["friendly_name"] = true;
  DeserializationError e = deserializeJson(d, http.getStream(), DeserializationOption::Filter(filter), DeserializationOption::NestingLimit(24));
  http.end();
  if (e) { sendErr(502, "list too large for the stick"); return; }
  static const char* const OKD[] = {"light.", "switch.", "scene.", "script.", "fan.", "input_boolean.", "cover.", "media_player.", "button.", "input_button.", "automation.", "climate.", "humidifier.", "siren.", "vacuum."};
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");
  server.sendContent("[");
  bool first = true;
  for (JsonObjectConst o : d.as<JsonArrayConst>()) {
    const char* id = o["entity_id"] | "";
    bool keep = false;
    for (auto p : OKD) if (!strncmp(id, p, strlen(p))) { keep = true; break; }
    if (!keep) continue;
    JsonDocument one;
    one["id"] = id;
    one["name"] = o["attributes"]["friendly_name"] | id;
    String s;
    serializeJson(one, s);
    server.sendContent(first ? s : "," + s);
    first = false;
  }
  server.sendContent("]");
  server.sendContent("");
}

// ---- captive setup
inline void hScan() {
  if (!G.sys.portal && !authed()) return;
  JsonDocument d;
  {
    Lock l;
    if (!G.wifi.scanning && (!G.wifi.at || ago(millis(), G.wifi.at) > 12000UL)) app::startWifiScan();
    JsonArray a = d["nets"].to<JsonArray>();
    for (int i = 0; i < G.wifi.count; i++) {
      if (!G.wifi.n[i].ssid[0]) continue;
      bool dup = false;
      for (int j = 0; j < i; j++) if (!strcmp(G.wifi.n[j].ssid, G.wifi.n[i].ssid)) dup = true;
      if (dup) continue;
      JsonObject o = a.add<JsonObject>();
      o["ssid"] = G.wifi.n[i].ssid; o["rssi"] = G.wifi.n[i].rssi; o["open"] = G.wifi.n[i].open; o["ent"] = G.wifi.n[i].ent;
    }
    // what happened to each saved network on the last attempt, so the page can say why it is showing again
    JsonArray last = d["last"].to<JsonArray>();
    for (int i = 0; i < WIFI_SLOTS; i++) {
      if (!G.join.ssid[i][0] || G.join.res[i] == JR_NONE) continue;
      JsonObject o = last.add<JsonObject>();
      o["ssid"] = G.join.ssid[i]; o["res"] = G.join.res[i]; o["code"] = G.join.code[i];
    }
    d["mac"] = WiFi.macAddress();
  }
  sendJson(d);
}

inline void hSetup() {
  if (!G.sys.portal && !authed()) return;
  JsonDocument d;
  if (!body(d)) { sendErr(400, "bad json"); return; }
  const char* ssid = d["ssid"] | "";
  if (!ssid[0]) { sendErr(400, "no network name"); return; }
  {
    Lock l;
    wifiRemember(ssid, d["pass"] | "", d["user"] | "");
    const char* wp = d["webPass"] | "";
    if (wp[0] && !CFG.webPass[0]) scopy(CFG.webPass, wp);     // the open hotspot may set a first password, never replace one
    configSave();
  }
  sendOk();
  rebootAt = millis() + 1200;
}

inline void hNotFound() {
  if (G.sys.portal) {
    server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
    server.send(302, "text/plain", "");
  } else sendErr(404, "not found");
}

// ---- OTA
inline void otaScreen(const char* l1, const char* l2, float frac) {
  auto& dsp = M5.Display;
  const Theme& th = themeOf(G.set.theme);
  dsp.fillScreen(th.bg);
  dsp.setFont(ui::fontOf(ui::TITLE)); dsp.setTextDatum(lgfx::v1::middle_center); dsp.setTextColor(th.acc2);
  dsp.drawString(l1, W / 2, 40);
  dsp.setFont(ui::fontOf(ui::BODY)); dsp.setTextColor(th.fg);
  dsp.drawString(l2, W / 2, 82);
  dsp.drawRect(10, 104, 220, 18, th.acc);
  dsp.fillRect(13, 107, (int)(214 * frac), 12, th.acc);
}

inline void hUpdateDone() {
  if (otaDenied || (CFG.webPass[0] && !server.authenticate("shiv", CFG.webPass))) {
    otaBegan = false;
    server.requestAuthentication(BASIC_AUTH, "SHIV", "user: shiv");
    return;
  }
  if (!otaBegan) { sendErr(400, "no firmware file in the request"); return; }
  otaBegan = false;
  bool okb = !Update.hasError();
  server.send(okb ? 200 : 500, "text/plain", okb ? "OK" : Update.errorString());
  if (okb) { otaScreen("UPDATED", "RESTARTING", 1); rebootAt = millis() + 800; }
  else { otaScreen("FAILED", "KEEPING OLD FIRMWARE", 0); fetch::paused = false; delay(1500); }
}

inline void hUpdateUpload() {
  HTTPUpload& up = server.upload();
  static size_t total = 0;
  if (up.status == UPLOAD_FILE_START) {
    otaDenied = CFG.webPass[0] && !server.authenticate("shiv", CFG.webPass);
    if (otaDenied) return;
    otaBegan = true;
    fetch::paused = true;
    hw::poke();
    total = 0;
    otaScreen("UPDATING", "DO NOT UNPLUG", 0);
    Update.begin(UPDATE_SIZE_UNKNOWN);
  } else if (up.status == UPLOAD_FILE_WRITE && !otaDenied) {
    Update.write(up.buf, up.currentSize);
    total += up.currentSize;
    static uint32_t lastDraw = 0;
    if (millis() - lastDraw > 400) { lastDraw = millis(); otaScreen("UPDATING", "DO NOT UNPLUG", min(1.0f, total / 2000000.0f)); }
  } else if (up.status == UPLOAD_FILE_END && !otaDenied) {
    Update.end(true);
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
    fetch::paused = false;
  }
}

static const char FAVICON_SVG[] PROGMEM = "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 64 64'><rect width='64' height='64' rx='10' fill='#04050e'/><g fill='none' stroke-linecap='round' stroke-linejoin='round'><path d='M44 18c-3-5-22-6-22 4 0 11 22 5 22 17 0 9-19 9-24 3' stroke='#00f0ff' stroke-width='9' opacity='.35'/><path d='M42 16c-3-5-22-6-22 4 0 11 22 5 22 17 0 9-19 9-24 3' stroke='#00f0ff' stroke-width='9' opacity='.35'/><path d='M44 18c-3-5-22-6-22 4 0 11 22 5 22 17 0 9-19 9-24 3' stroke='#7cf6ff' stroke-width='4'/><path d='M42 16c-3-5-22-6-22 4 0 11 22 5 22 17 0 9-19 9-24 3' stroke='#00f0ff' stroke-width='4'/><path d='M10 54 54 10' stroke='#04050e' stroke-width='5'/><path d='M10 54 54 10' stroke='#eaf6ff' stroke-width='2'/><path d='M50 10l4 0 0 4' stroke='#eaf6ff' stroke-width='2'/></g></svg>";
inline void hFavicon() { server.sendHeader("Cache-Control", "max-age=86400"); server.send_P(200, "image/svg+xml", FAVICON_SVG); }

// ---------------------------------------------------------------- setup / loop
inline void begin(Canvas* c) {
  canvas = c;
  server.on("/", HTTP_GET, hRoot);
  server.on("/api/status", HTTP_GET, hStatus);
  server.on("/api/config", HTTP_GET, hConfigGet);
  server.on("/api/config", HTTP_POST, hConfigPost);
  server.on("/api/page", HTTP_POST, hPage);
  server.on("/api/page", HTTP_GET, hPage);
  server.on("/api/doorbell", HTTP_POST, hDoorbell);
  server.on("/api/doorbell", HTTP_GET, hDoorbell);
  server.on("/api/pages/clear", HTTP_POST, hPagesClear);
  server.on("/api/pihole", HTTP_POST, hPihole);
  server.on("/api/ha/test", HTTP_GET, hHaTest);
  server.on("/favicon.ico", HTTP_GET, hFavicon);
  server.on("/favicon.svg", HTTP_GET, hFavicon);
  server.on("/api/screen", HTTP_POST, hScreen);
  server.on("/api/screen.raw", HTTP_GET, hScreenRaw);
  server.on("/api/ir/send", HTTP_POST, []() { hIr("send"); });
  server.on("/api/ir/delete", HTTP_POST, []() { hIr("delete"); });
  server.on("/api/ir/rename", HTTP_POST, []() { hIr("rename"); });
  server.on("/api/macro/run", HTTP_POST, []() { hRun(fetch::A_MACRO); });
  server.on("/api/fav/run", HTTP_POST, []() { hRun(fetch::A_FAV); });
  server.on("/api/ha/entities", HTTP_GET, hHaEntities);
  server.on("/api/scan", HTTP_GET, hScan);
  server.on("/api/setup", HTTP_POST, hSetup);
  server.on("/api/ota", HTTP_GET, hOta);
  server.on("/api/ota", HTTP_POST, hOta);
  server.on("/api/reboot", HTTP_POST, []() { if (!authed()) return; sendOk(); rebootAt = millis() + 600; });
  server.on("/api/portal", HTTP_POST, []() { if (!authed()) return; sendOk(); delay(300); net::startPortal(); });
  server.on("/update", HTTP_POST, hUpdateDone, hUpdateUpload);
  server.onNotFound(hNotFound);
  server.begin();
  started = true;
}

inline void tick() {
  if (!started) return;
  server.handleClient();
  if (app::rebootReq && !rebootAt) rebootAt = millis() + 100;
  if (rebootAt && millis() > rebootAt) { fetch::paused = true; { Lock l; configSave(); } delay(300); ESP.restart(); }   // paused: the net task closes the PC link first
}

}  // namespace web
}  // namespace shiv
