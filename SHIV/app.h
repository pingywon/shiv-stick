// SHIV - app logic: what the two buttons do on every screen, and everything that ticks.
//   SIDE click  = next screen (or next item while a list is focused); SIDE double-click = back one
//   SIDE hold   = launcher ("GO TO")        FRONT click = the action shown bottom-left
//   FRONT hold  = the action shown bottom-right (back / reset / mode ...)
#pragma once
#include "draw_sys.h"
#include "hw.h"
#include "net.h"
#include "fetch.h"
#include "ir.h"
#include "mic.h"

namespace shiv {
namespace app {

static uint32_t rng = 0xC0FFEE11;
static bool swallow = false;
static uint32_t frontDownAt = 0, ringAt = 0, lastTvAt = 0, wifiScanRetryAt = 0;
static uint8_t wifiScanTries = 0;
static bool wifiScanWait = false;
static bool oracleRevealed = true;

static nav::Side side;                            // SIDE double-click = back, see nav:: in state.h
static uint32_t screenAt = 0;
static const uint32_t DOUBLE_MS = nav::DOUBLE_MS;
using nav::wrapStep;
inline int sideStep(uint32_t now) { return nav::sideStep(side, now); }
inline void sideDisarm() { side.run = 0; }

static bool rebootReq = false;
inline bool inGame() { return G.screen == SC_GAMES && G.games.active >= 0; }
inline bool isRinging() { return G.cd.ringing || G.pomo.ringing; }
inline bool keepAwake() {
  if (G.ota.installing || G.ota.done) return true;   // flashing / restarting: keep the screen on
  if (isRinging() || G.sys.portal || G.join.show || G.tv.running || G.ir.mode == 1) return true;
  uint32_t quiet = ago(millis(), hw::lastButtonAt);  // time since a real button press
  if (G.sys.calib) return quiet < 180000UL;
  if (G.screen == SC_REFLEX) return G.games.reflex.phase == 1 || G.games.reflex.phase == 2;
  if (inGame()) {
    const Games& g = G.games;
    if (g.active == 0) return !g.run.dead && quiet < 600000UL;   // tilt-only game: it always ends by itself
    if (g.active == 1) return !g.snake.dead && quiet < 180000UL; // an idle snake can circle forever
    return g.reflex.phase == 1 || g.reflex.phase == 2;
  }
  return G.screen == SC_LEVEL && quiet < 120000UL;               // using the level: 2 min hands-free
}

inline void lightTimes(uint16_t& dimS, uint16_t& offS) {
  dimS = G.set.dimS; offS = G.set.offS;
}

// ---------------------------------------------------------------- wifi scan (async, runs on the UI thread)
inline void startWifiScan() {
  if (G.wifi.scanning) return;
  if (!net::scanBusy()) {                          // the Wi-Fi manager is already scanning: read its result instead of cancelling it
    if (WiFi.getMode() == WIFI_OFF) WiFi.mode(WIFI_STA);
    WiFi.scanDelete();
    WiFi.scanNetworks(true, true);
  }
  G.wifi.scanning = true;
  wifiScanTries = 0;
  wifiScanWait = false;
}

inline void pollWifiScan(uint32_t now) {
  if (!G.wifi.scanning) return;
  if (wifiScanWait) {
    if (ago(now, wifiScanRetryAt) < 1200) return;
    wifiScanWait = false;
    if (!net::scanBusy()) WiFi.scanNetworks(true, true);
    return;
  }
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) return;
  if (n < 0) {
    if (++wifiScanTries < 5) { wifiScanWait = true; wifiScanRetryAt = now; } else { G.wifi.scanning = false; setToast(G, "SCAN FAILED"); }
    return;
  }
  WifiScan& w = G.wifi;
  memset(w.chanLoad, 0, sizeof(w.chanLoad));
  memset(w.chanCount, 0, sizeof(w.chanCount));
  w.count = 0;
  for (int i = 0; i < n; i++) {
    int ch = WiFi.channel(i), rssi = WiFi.RSSI(i);
    if (ch >= 1 && ch <= 13) {
      int wgt = constrain(rssi + 100, 4, 70) / 2;
      w.chanLoad[ch] = (uint8_t)min(255, w.chanLoad[ch] + wgt);
      w.chanCount[ch]++;
    }
    if (w.count < MAX_NETS) {
      Net& e = w.n[w.count++];
      scopy(e.ssid, WiFi.SSID(i).c_str());
      e.rssi = (int8_t)constrain(rssi, -127, 0);
      e.ch = ch;
      e.open = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
      e.ent = net::isEnterprise(WiFi.encryptionType(i));
    }
  }
  for (int i = 0; i < w.count; i++)
    for (int j = i + 1; j < w.count; j++)
      if (w.n[j].rssi > w.n[i].rssi) { Net t = w.n[i]; w.n[i] = w.n[j]; w.n[j] = t; }
  if (!net::scanBusy()) WiFi.scanDelete();         // whoever reads last clears the result
  w.sel = 0; w.detail = false; w.scanning = false; w.at = now;
}

static uint32_t bleReqAt = 0;
inline void startBleScan() {
  if (G.ble.scanning) return;
  if (!fetch::request(fetch::A_BLE)) { setToast(G, "BUSY - TRY AGAIN"); return; }
  G.ble.scanning = true;
  G.ble.count = 0;
  bleReqAt = millis();
}

// ---------------------------------------------------------------- pi-hole view + HA browser helpers
inline void piholeView(uint8_t src) {
  Pihole& p = G.pi;
  p = Pihole();
  p.source = src == 5 ? 5 : 6;
  fetch::piSid[0] = 0;
  fetch::request(fetch::A_REFRESH, SC_PIHOLE);
  markDirty();
}

inline void haOpen(uint8_t kind) {
  HaBrowse& b = G.hab;
  if (b.kind != kind) { b.n = 0; b.sel = 0; b.more = false; }
  b.kind = kind; b.level = 2; b.err[0] = 0;
  if (fetch::request(fetch::A_HA_LIST, kind)) b.loading = true;
  else { b.loading = false; scopy(b.err, "BUSY - TRY AGAIN"); }
}

// ---------------------------------------------------------------- daemon
inline void petCue(pet::Cue c) {
  if (c == pet::CUE_NONE || !hw::audible()) return;
  switch (c) {
    case pet::CUE_GRUMBLE: hw::note(300, 70); break;
    case pet::CUE_GROWL:   hw::note(230, 110); hw::note(180, 170); break;
    case pet::CUE_RAGE:    hw::note(500, 80); hw::note(320, 120); hw::note(160, 320); break;
    case pet::CUE_NOM:     hw::note(1200, 40); hw::note(0, 40); hw::note(1500, 40); hw::note(0, 40); hw::note(1800, 70); break;
    case pet::CUE_REFUSE:  hw::note(400, 80); hw::note(300, 130); break;
    case pet::CUE_PLAY:    hw::note(1320, 60); hw::note(1760, 60); hw::note(2100, 60); hw::note(2640, 110); break;
    case pet::CUE_DIZZY:   hw::note(1800, 60); hw::note(1500, 60); hw::note(1200, 60); hw::note(1500, 60); hw::note(1800, 60); break;
    case pet::CUE_FORGIVE: hw::note(1400, 50); hw::note(2100, 70); break;
    case pet::CUE_EVENT:   hw::note(1900, 18); break;
    default: break;
  }
}

// ---------------------------------------------------------------- navigation
inline void clearFocus() {
  G.sys.focus = false; G.sys.calib = 0;
  G.home.focus = false; G.hab.level = 0; G.macros.focus = false; G.macros.holdStart = 0;
  G.ir.focus = false; G.wifi.detail = false;
  if (G.ir.mode == 1) ir::learnStop();
  G.ir.mode = 0;
  if (G.tv.running) { G.tv.running = false; }
  frontDownAt = 0;
}

inline void enterScreen(uint8_t id) {
  uint32_t now = millis();
  clearFocus();
  G.games.active = -1;
  G.launcher = false;
  G.launcherGrp = -1;
  G.screen = (ScreenId)id;
  screenAt = now;
  switch (id) {
    case SC_WIFI: case SC_CHAN: if (!G.wifi.at || ago(now, G.wifi.at) > 60000UL) startWifiScan(); break;
    case SC_BLE: if (!G.ble.at || ago(now, G.ble.at) > 60000UL) startBleScan(); break;
    case SC_PAGER: G.pager.sel = 0; G.pager.scroll = 0; break;
    case SC_DOORBELL: G.door.sel = 0; break;
    case SC_MQTT: G.mqtt.sel = 0; break;
    case SC_WEATHER: if (!G.wx.at) fetch::request(fetch::A_REFRESH, id); break;
    case SC_PIHOLE: case SC_HOSTS: case SC_JOBS: case SC_HOME: fetch::request(fetch::A_REFRESH, id); break;
    case SC_SHOP: if (!G.shop.at || ago(now, G.shop.at) > 120000UL) fetch::request(fetch::A_REFRESH, id); break;
    case SC_REFLEX: G.games.reflex.phase = 0; G.games.active = 2; break;
    default: break;
  }
}

inline void stepScreen(int step) { enterScreen(nav::stepScreen(G, step)); }

// ---------------------------------------------------------------- pager
inline void recountUnread() {
  int u = 0;
  for (int i = 0; i < G.pager.n; i++) if (G.pager.m[i].unread) u++;
  G.pager.unread = u;
}

inline void pageIn(const char* text, const char* from) {
  Pager& p = G.pager;
  for (int i = MAX_PAGES - 1; i > 0; i--) p.m[i] = p.m[i - 1];
  PageMsg& m = p.m[0];
  m = PageMsg();
  scopy(m.text, text);
  scopy(m.from, from ? from : "");
  if (G.sys.timeValid) snprintf(m.when, sizeof(m.when), "%02d:%02d", G.lt.tm_hour, G.lt.tm_min);
  m.unread = true;
  if (p.n < MAX_PAGES) p.n++;
  recountUnread();
  hw::poke();
  hw::pageTone();
  if (!inGame() && G.screen != SC_REFLEX && !isRinging() && !G.sys.portal && G.ir.mode != 1 && screenEnabled(G, SC_PAGER)) enterScreen(SC_PAGER);
}

inline void pageDelete(int idx) {
  Pager& p = G.pager;
  if (idx < 0 || idx >= p.n) return;
  for (int i = idx; i < p.n - 1; i++) p.m[i] = p.m[i + 1];
  p.n--;
  if (p.sel >= p.n) p.sel = p.n ? p.n - 1 : 0;
  p.scroll = 0;
  recountUnread();
}

// ---------------------------------------------------------------- doorbell
inline void doorRecount() {
  Doorbell& d = G.door;
  uint8_t u = 0;
  for (int i = 0; i < d.n; i++) if (d.e[i].unread) u++;
  d.unread = u;
}

inline void doorbellIn(const char* cam, const char* what) {
  Doorbell& d = G.door;
  for (int i = MAX_DOOR - 1; i > 0; i--) d.e[i] = d.e[i - 1];
  DoorEvent& e = d.e[0];
  e = DoorEvent();
  scopy(e.cam, cam && cam[0] ? cam : "ALERT");
  scopy(e.what, what ? what : "");
  if (G.sys.timeValid) snprintf(e.when, sizeof(e.when), "%02d:%02d", G.lt.tm_hour, G.lt.tm_min);
  e.unread = true;
  if (d.n < MAX_DOOR) d.n++;
  d.sel = 0;
  doorRecount();
  hw::poke();
  hw::doorbellTone();
  if (!inGame() && G.screen != SC_REFLEX && !isRinging() && !G.sys.portal && G.ir.mode != 1 && screenEnabled(G, SC_DOORBELL)) enterScreen(SC_DOORBELL);
}

inline void doorClear() {
  Doorbell& d = G.door;
  d.n = d.sel = d.unread = 0;
}

// ---------------------------------------------------------------- per-screen front button
inline void timerClick() {
  Countdown& t = G.cd;
  if (t.running) t.running = false;
  else { if (t.remainMs == 0) t.remainMs = t.totalMs; t.running = true; t.lastTick = millis(); }
}
inline void timerHold() {
  Countdown& t = G.cd;
  bool fresh = !t.running && t.remainMs == t.totalMs;
  if (fresh) { t.preset = (t.preset + 1) % TIMER_PRESET_N; t.totalMs = (uint32_t)TIMER_PRESETS_MIN[t.preset] * 60000UL; }
  t.running = false;
  t.remainMs = t.totalMs;
}

inline void pomoClick() {
  Pomo& p = G.pomo;
  if (p.phase == 0) { p.phase = 1; p.round = 0; p.totalMs = p.remainMs = (uint32_t)p.focusMin * 60000UL; p.running = true; p.lastTick = millis(); }
  else { p.running = !p.running; p.lastTick = millis(); }
}
inline void pomoHold() {
  Pomo& p = G.pomo;
  p.phase = 0; p.running = false; p.ringing = false; p.round = 0;
  p.totalMs = p.remainMs = (uint32_t)p.focusMin * 60000UL;
}

inline void systemChange(int row) {
  Settings& s = G.set;
  switch (row) {
    case SR_THEME: s.theme = (s.theme + 1) % THEME_N; break;
    case SR_SOUND: s.mute = !s.mute; hw::applyVolume(); break;
    case SR_VOLUME: s.volume = s.volume <= 40 ? 90 : s.volume <= 110 ? 180 : 30; s.mute = false; hw::applyVolume(); hw::ok(); break;
    case SR_BRIGHT: s.brightness = s.brightness <= 70 ? 130 : s.brightness <= 160 ? 230 : 50; M5.Display.setBrightness(s.brightness); break;
    case SR_FLIP: s.flip = (s.flip + 1) % 3; break;
    case SR_SLEEP: s.sleepMin = s.sleepMin == 0 ? 2 : s.sleepMin == 2 ? 5 : s.sleepMin == 5 ? 15 : 0; break;
    case SR_CALIB: G.sys.calib = 1; return;
    case SR_WIFI: net::startPortal(); G.sys.focus = false; return;
    case SR_REBOOT: configSave(); rebootReq = true; return;   // the web loop restarts after the PC link says goodbye
    default: return;
  }
  markDirty();
}

inline void calibClick() {
  if (G.sys.calib == 3) { G.sys.calib = 0; return; }
  if (hw::calibCapture(G.sys.calib)) {
    G.sys.calib++;
    hw::ok();
    if (G.sys.calib == 3) markDirty();
  } else { hw::bad(); setToast(G, G.sys.calib == 2 ? "STAND IT UP MORE" : "HOLD STILL"); }
}

inline void frontClick(uint32_t now) {
  switch (G.screen) {
    case SC_CLOCK: G.set.clockFace = (G.set.clockFace + 1) % draw::CLOCK_FACES; markDirty(); break;
    case SC_TIMER: timerClick(); break;
    case SC_POMO: pomoClick(); break;
    case SC_WEATHER: case SC_SHOP: case SC_JOBS:
      fetch::request(fetch::A_REFRESH, G.screen); setToast(G, G.sys.wifi ? "SYNCING" : "NO WIFI"); break;
    case SC_PIHOLE: {                            // the on/off button (Pi-hole's own timer turns it back on)
      if (!G.sys.wifi) { setToast(G, "NO WIFI"); hw::bad(); break; }
      if (G.pi.toggling) break;
      if (G.pi.source != G.set.piTarget) piholeView(G.set.piTarget);   // show the one being switched
      if (fetch::request(fetch::A_PIHOLE, 0)) { G.pi.toggling = true; setToast(G, "SENDING.."); }
      else { setToast(G, "BUSY - TRY AGAIN"); hw::bad(); }
      break;
    }
    case SC_HOSTS: {
      int pages = (G.hosts.n + draw::HOSTS_PER_PAGE - 1) / draw::HOSTS_PER_PAGE;
      if (pages > 1) G.hosts.page = (G.hosts.page + 1) % pages;
      break;
    }
    case SC_HOME: {                              // HA CONTROL: categories -> entities
      HaBrowse& b = G.hab;
      if (!CFG.haToken[0]) { setToast(G, "NO HA TOKEN"); break; }
      if (b.level == 0) { b.level = 1; if (b.cat >= haCatCount(G)) b.cat = 0; break; }
      if (b.level == 1) { haOpen(haCatAt(G, b.cat)); break; }
      if (b.n == 0) { if (!b.loading) haOpen(b.kind); break; }          // RETRY
      setToast(G, fetch::request(fetch::A_HA_ENT, b.sel) ? "SENDING.." : "BUSY - TRY AGAIN");
      break;
    }
    case SC_MACROS:
      if (!G.macros.n) break;
      G.macros.focus = !G.macros.focus;       // in focus a click means BACK; firing needs the long hold
      break;
    case SC_WIFI:
      if (!G.wifi.count) { startWifiScan(); break; }
      if (!G.sys.focus) G.sys.focus = true; else G.wifi.detail = !G.wifi.detail;
      break;
    case SC_CHAN: startWifiScan(); setToast(G, "SCANNING"); break;
    case SC_BLE:
      if (!G.ble.count) { startBleScan(); break; }
      if (!G.sys.focus) G.sys.focus = true;
      break;
    case SC_IR: {
      Ir& r = G.ir;
      if (r.mode == 1) { ir::learnStop(); r.mode = 0; break; }
      if (!r.focus) { r.focus = true; break; }
      if (r.sel < r.n) { bool okb = ir::sendSaved(r.sel); r.mode = okb ? 3 : 4; r.modeAt = now; if (okb) hw::ok(); else hw::bad(); }
      else if (ir::learnStart()) { r.mode = 1; r.modeAt = now; }
      else { r.mode = 4; r.modeAt = now; }
      break;
    }
    case SC_TVOFF:
      if (G.tv.running) { G.tv.running = false; G.tv.doneAt = 0; }
      else { G.tv.running = true; G.tv.idx = 0; G.tv.total = ir::TV_N; lastTvAt = 0; }
      break;
    case SC_LEVEL: hw::levelZero(); setToast(G, "ZEROED"); break;
    case SC_ORACLE: draw::oracleRoll(G.orc, rng, now); oracleRevealed = false; hw::blipLo(); break;
    case SC_GAMES:
      if (!G.sys.focus) { G.sys.focus = true; break; }
      G.games.active = G.games.sel;
      if (G.games.active == 0) game::runnerReset(G.games.run);
      else if (G.games.active == 1) game::snakeReset(G.games.snake, now, rng);
      else G.games.reflex.phase = 0;
      break;
    case SC_DAEMON:
      petCue(pet::poke(G, rng));
      if (G.dmn.pokes % 10 == 0) markDirty();
      break;
    case SC_DOORBELL:
      if (G.door.n) G.door.sel = (G.door.sel + 1) % G.door.n;
      break;
    case SC_MQTT:
      if (G.mqtt.n) G.mqtt.sel = (G.mqtt.sel + 1) % G.mqtt.n;
      break;
    case SC_SYSTEM:
      if (G.sys.calib) { calibClick(); break; }
      if (!G.sys.focus) G.sys.focus = true; else systemChange(G.sys.sel);
      break;
    default: break;
  }
}

inline void frontHold(uint32_t now) {
  switch (G.screen) {
    case SC_CLOCK: G.set.mute = !G.set.mute; hw::applyVolume(); setToast(G, G.set.mute ? "MUTED" : "SOUND ON"); markDirty(); break;
    case SC_TIMER: timerHold(); break;
    case SC_POMO: pomoHold(); break;
    case SC_HOSTS: fetch::request(fetch::A_HOSTS); setToast(G, "CHECKING"); break;
    case SC_PIHOLE: piholeView(G.pi.source == 5 ? 6 : 5); break;
    case SC_HOME: if (G.hab.level) G.hab.level--; break;
    case SC_PAGER: if (G.pager.n) { pageDelete(G.pager.sel); setToast(G, "DELETED"); } break;
    case SC_WIFI: if (G.wifi.detail) G.wifi.detail = false; else if (G.sys.focus) G.sys.focus = false; else { startWifiScan(); } break;
    case SC_BLE: if (G.sys.focus) G.sys.focus = false; else startBleScan(); break;
    case SC_IR: if (G.ir.mode == 1) { ir::learnStop(); G.ir.mode = 0; } G.ir.focus = false; break;
    case SC_LEVEL: hw::mo.zeroRoll = hw::mo.zeroPitch = 0; setToast(G, "ZERO CLEARED"); break;
    case SC_ORACLE: G.orc.mode = (G.orc.mode + 1) % 4; G.orc.rolledAt = 0; G.orc.rolling = false; break;
    case SC_GAMES: G.sys.focus = false; break;
    case SC_DAEMON: petCue(pet::feed(G, rng)); markDirty(); break;
    case SC_DOORBELL: if (G.door.n) { doorClear(); setToast(G, "CLEARED"); } break;
    case SC_MQTT: if (G.mqtt.n) { G.mqtt.n = G.mqtt.sel = 0; setToast(G, "CLEARED"); } break;
    case SC_SYSTEM: if (G.sys.calib) G.sys.calib = 0; else G.sys.focus = false; break;
    default: break;
  }
  (void)now;
}

// SIDE click while a list has focus. step is +1, or -2 for the double-click "back".
// Returns 0 = not used (change screen), 1 = moved the selection, 2 = used for something else.
inline int sideInFocus(int step) {
  switch (G.screen) {
    case SC_HOME:
      if (G.hab.level == 1) { G.hab.cat = wrapStep(G.hab.cat, step, haCatCount(G)); return 1; }
      if (G.hab.level == 2) { if (G.hab.n) G.hab.sel = wrapStep(G.hab.sel, step, G.hab.n); return 1; }
      break;
    case SC_MACROS: if (G.macros.focus && G.macros.n) { G.macros.sel = wrapStep(G.macros.sel, step, G.macros.n); return 1; } G.macros.focus = false; break;
    case SC_WIFI: if (G.wifi.detail) { G.wifi.detail = false; return 2; } if (G.sys.focus && G.wifi.count) { G.wifi.sel = wrapStep(G.wifi.sel, step, G.wifi.count); return 1; } break;
    case SC_BLE: if (G.sys.focus && G.ble.count) { G.ble.sel = wrapStep(G.ble.sel, step, G.ble.count); return 1; } break;
    case SC_IR: if (G.ir.mode == 1) return 2; if (G.ir.focus) { int cnt = G.ir.n + (G.ir.n < MAX_IR ? 1 : 0); G.ir.sel = wrapStep(G.ir.sel, step, cnt); return 1; } break;
    case SC_GAMES: if (G.sys.focus) { G.games.sel = wrapStep(G.games.sel, step, 3); return 1; } break;
    case SC_SYSTEM: if (G.sys.calib) return 2; if (G.sys.focus) { G.sys.sel = wrapStep(G.sys.sel, step, SR_COUNT); return 1; } break;
    default: break;
  }
  return 0;
}

inline void stopRinging() {
  if (G.cd.ringing) { G.cd.ringing = false; G.cd.remainMs = G.cd.totalMs; }
  if (G.pomo.ringing) { G.pomo.ringing = false; G.pomo.running = true; G.pomo.lastTick = millis(); }
  hw::sqHead = hw::sqTail;
}

// ---------------------------------------------------------------- input
inline void input(uint32_t now, Canvas& canvas) {
  bool fc = M5.BtnA.wasClicked(), fh = M5.BtnA.wasHold(), sc = M5.BtnB.wasClicked(), sh = M5.BtnB.wasHold();
  bool fDown = M5.BtnA.isPressed(), sDown = M5.BtnB.isPressed();
  bool any = fc || fh || sc || sh || fDown || sDown;
  uint8_t otaPh = otaui::phase(G, now);
  if (otaPh == otaui::INSTALLING || otaPh == otaui::DONE) { swallow = true; return; }   // mid-flash: ignore every button
  if (otaPh == otaui::OFFER || otaPh == otaui::FAILED) {
    bool wasLit = hw::light == hw::L_ON;              // capture before poke so the waking press only wakes
    if (any) { hw::poke(); hw::lastButtonAt = now; }
    if (otaPh == otaui::OFFER && wasLit && !swallow) {
      if (fc) { G.ota.want = true; hw::blipHi(); }                                        // FRONT = install now
      else if (sc) { G.ota.snoozeUntil = now + 4UL * 3600000UL; hw::blipLo(); setToast(G, "LATER"); }   // SIDE = remind in 4 h
    }
    swallow = true;
    if (!fDown && !sDown && !fc && !sc) swallow = false;
    return;
  }
  if (any) {
    if (hw::light != hw::L_ON) swallow = true;  // the press that wakes a dim or dark screen does nothing else
    hw::poke();
    hw::lastButtonAt = now;
  }
  if (swallow) {
    if (!fDown && !sDown && !fc && !sc) swallow = false;
    return;
  }

  if (isRinging()) { if (fc || sc || fh || sh) { stopRinging(); swallow = fDown || sDown; } return; }
  if (G.sys.portal) {                             // setup hotspot: with a network saved, one press carries on without it
    if ((fc || fh) && net::haveCreds()) { net::stopPortal(true); hw::blipLo(); setToast(G, "OFFLINE MODE"); swallow = fDown || sDown; }
    return;
  }
  if (G.join.show) {                              // JOINING WIFI: hold FRONT = setup hotspot now; any other press = get out of the way
    if (fh) { net::setupNow(); hw::blipHi(); swallow = true; }
    else if (fc || sc || sh) { net::dismissJoin(); hw::blipLo(); setToast(G, "WIFI IN BACKGROUND"); swallow = fDown || sDown; }
    return;
  }
  if (fc || fh || sh) sideDisarm();               // a SIDE double-click is two SIDE clicks with nothing in between

  // ---- launcher
  if (G.launcher) {                             // level 0 = groups, level 1 = the open group's screens
    bool top = G.launcherGrp < 0;
    int cnt = top ? nav::grpCount(G) : nav::grpScreens(G, G.launcherGrp);
    if (sc) { int st = sideStep(now); G.launcherSel = wrapStep(G.launcherSel, st, cnt); if (st < 0) hw::blipLo(); else hw::blip(); }
    if (fc) {
      hw::blipHi();
      if (top) { G.launcherGrp = (int8_t)nav::grpAt(G, G.launcherSel); G.launcherSel = 0; sideDisarm(); }
      else enterScreen(nav::grpScreenAt(G, G.launcherGrp, G.launcherSel));
    }
    if (fh) {
      hw::blipLo();
      if (top) G.launcher = false;
      else { int g = G.launcherGrp; G.launcherGrp = -1; G.launcherSel = (uint8_t)nav::grpIndex(G, g); sideDisarm(); }
    }
    if (sh) { hw::blipLo(); enterScreen(SC_CLOCK); }
    return;
  }

  // ---- games own both buttons
  if (inGame()) {
    Games& g = G.games;
    if (fh) { g.active = -1; markDirty(); hw::blipLo(); return; }
    if (g.active == 0 && g.run.dead && fc) { game::runnerReset(g.run); hw::blipHi(); }
    if (g.active == 1 && g.snake.dead && fc) { game::snakeReset(g.snake, now, rng); hw::blipHi(); }
    return;
  }

  // ---- macros: hold-to-fire with a visible progress bar
  if (G.screen == SC_MACROS && G.macros.focus && G.macros.n) {
    if (M5.BtnA.wasPressed()) frontDownAt = now;
    if (fDown && frontDownAt) {
      if (ago(now, frontDownAt) > 250) G.macros.holdStart = frontDownAt + 250;
      if (G.macros.holdStart && ago(now, G.macros.holdStart) >= draw::MACRO_HOLD_MS) {
        if (fetch::request(fetch::A_MACRO, G.macros.sel)) { setToast(G, "FIRED"); hw::ok(); }
        else { setToast(G, "BUSY - TRY AGAIN"); hw::bad(); }
        G.macros.holdStart = 0; frontDownAt = 0; swallow = true;
        return;
      }
    } else { G.macros.holdStart = 0; frontDownAt = 0; }
    fh = false;                                 // the generic hold event is not used here
  }

  if (sh) { hw::blipLo(); clearFocus(); G.launcher = true; G.launcherGrp = -1; G.launcherSel = 0; return; }
  if (sc) {
    int st = sideStep(now);
    if (st < 0) hw::blipLo(); else hw::blip();
    int used = sideInFocus(st);
    if (used == 0) stepScreen(st);
    else if (used == 2) sideDisarm();           // that click did not move anything, so there is nothing to step back from
    return;
  }
  if (fh) { hw::blipLo(); frontHold(now); return; }
  if (fc) {
    hw::blipHi();
    if (G.screen == SC_PAGER) {                 // needs the canvas to measure wrapped text
      Pager& p = G.pager;
      if (p.n) {
        int pages = draw::pagerPages(canvas, p.m[p.sel]);
        if (p.scroll + 1 < pages) p.scroll++;
        else { p.scroll = 0; p.sel = (p.sel + 1) % p.n; }
      }
      return;
    }
    frontClick(now);
  }
}

// ---------------------------------------------------------------- things that tick
inline uint32_t nextTimerDueMs() {
  uint32_t d = 0;
  if (G.cd.running) d = G.cd.remainMs;
  if (G.pomo.running && (!d || G.pomo.remainMs < d)) d = G.pomo.remainMs;
  return d ? d + 50 : 0;
}

inline void tick(uint32_t now) {
  // clock
  time_t t = time(nullptr);
  G.epoch = (uint32_t)t;
  localtime_r(&t, &G.lt);
  G.now = now;
  G.sys.uptimeS = now / 1000;
  if (G.ota.done && !rebootReq && ago(now, G.ota.at) > 1500) rebootReq = true;   // self-update flashed: show "RESTARTING" ~1.5 s, then reboot
  G.sys.heapKb = ESP.getFreeHeap() / 1024;
  G.sys.psramKb = ESP.getFreePsram() / 1024;
  G.sys.heapIntKb = heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024;
  G.sys.heapMinKb = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL) / 1024;
  G.sys.heapBigKb = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024;
  G.sys.loopHwm = uxTaskGetStackHighWaterMark(nullptr);

  // countdown
  Countdown& c = G.cd;
  if (c.running) {
    uint32_t dt = ago(now, c.lastTick);
    c.lastTick = now;
    if (dt >= c.remainMs) { c.remainMs = 0; c.running = false; c.ringing = true; ringAt = now; hw::poke(); G.launcher = false; }
    else c.remainMs -= dt;
  }
  // pomodoro
  Pomo& p = G.pomo;
  if (p.running) {
    uint32_t dt = ago(now, p.lastTick);
    p.lastTick = now;
    if (dt >= p.remainMs) {
      if (p.phase == 1) { p.round++; p.phase = 2; p.totalMs = p.remainMs = (uint32_t)p.breakMin * 60000UL; }
      else { p.phase = 1; p.totalMs = p.remainMs = (uint32_t)p.focusMin * 60000UL; }
      p.running = false; p.ringing = true; ringAt = now; hw::poke(); G.launcher = false;
    } else p.remainMs -= dt;
  }
  if (isRinging()) {
    static uint32_t lastTone = 0;
    if (now - lastTone > 1300) { lastTone = now; hw::alarmTone(); }
    if (ago(now, ringAt) > 60000UL) stopRinging();
  }

  // oracle
  if (G.screen == SC_ORACLE && hw::mo.shake && hw::screenOn() && !G.launcher) { draw::oracleRoll(G.orc, rng, now); oracleRevealed = false; hw::blipLo(); hw::poke(); }
  if (!oracleRevealed && ago(now, G.orc.rolledAt) >= 700) { oracleRevealed = true; G.orc.rolling = false; hw::ok(); }

  // games
  if (inGame() || G.screen == SC_REFLEX) {
    GameInput in;
    in.tiltX = constrain(hw::mo.tiltX * 2.4f, -1.0f, 1.0f);
    in.tiltY = constrain(hw::mo.tiltY * 2.4f, -1.0f, 1.0f);
    in.front = M5.BtnA.isPressed();
    in.frontClick = M5.BtnA.wasPressed();
    Games& g = G.games;
    if (g.active == 0) { bool was = g.run.dead; game::runnerStep(g.run, in, rng); if (!was && g.run.dead) { hw::bad(); markDirty(); } }
    else if (g.active == 1) { bool was = g.snake.dead; uint32_t sc0 = g.snake.score; game::snakeStep(g.snake, in, now, rng);
                              if (g.snake.score != sc0) hw::blipHi(); if (!was && g.snake.dead) { hw::bad(); markDirty(); } }
    else { uint8_t ph = g.reflex.phase; if (game::reflexStep(g.reflex, in, now, rng)) hw::note(2400, 70);
           if (ph == 2 && g.reflex.phase == 3) { hw::ok(); markDirty(); } if (ph == 1 && g.reflex.phase == 4) hw::bad(); }
  }

  // pi-hole: audible confirmation when blocking flips; an expired countdown triggers a re-read
  {
    static int8_t lastEn = -1;
    if (G.pi.at) {
      int8_t en = G.pi.enabled ? 1 : 0;
      if (lastEn >= 0 && en != lastEn) { if (en) hw::ok(); else hw::bad(); }
      lastEn = en;
    } else lastEn = -1;
    if (G.set.piOffUntil && G.sys.timeValid && G.epoch >= G.set.piOffUntil) {
      G.set.piOffUntil = 0;
      markDirty();
      fetch::request(fetch::A_REFRESH, SC_PIHOLE);
    }
  }

  // daemon: eyes follow the tilt, a shake makes him dizzy, and he notices what the stick sees
  {
    G.sys.lit = hw::screenOn();
    G.sys.wifiSet = net::haveCreds();
    bool onPet = pet::watching(G);
    G.dmn.lookX = onPet ? constrain(hw::mo.tiltX * 2.4f, -1.0f, 1.0f) : 0;
    G.dmn.lookY = onPet ? constrain(hw::mo.tiltY * 2.4f, -1.0f, 1.0f) : 0;
    if (onPet && hw::mo.shake) { petCue(pet::shake(G, rng)); hw::poke(); }
    bool petDirty = false;
    pet::Cue c = pet::tick(G, rng, petDirty);
    if (onPet) petCue(c);
    if (petDirty) markDirty();
    // SHIV-10: the daemon hears the room. Reactions are spoken lines (no fighting his
    // own mood engine), rate-limited so he does not chatter over every sound.
    if (G.set.petHears && G.mic.active && onPet) {
      static uint32_t nextHearAt = 0; static uint8_t prevLv = 0;
      uint8_t lv = G.mic.level;
      if (now >= nextHearAt) {
        if (lv >= 7 && prevLv >= 6)      { pet::react(G, "TOO LOUD IN HERE!"); nextHearAt = now + 6000; }
        else if (lv >= 4)                { pet::react(G, "OOH, A BEAT!");      nextHearAt = now + 5000; }
        else if (prevLv == 0 && lv >= 2) { pet::react(G, "I HEAR YOU!");       nextHearAt = now + 4000; }
      }
      prevLv = lv;
    }
  }

  // pager: showing a message marks it read
  if (G.screen == SC_PAGER && hw::screenOn() && !G.launcher && ago(now, screenAt) >= DOUBLE_MS && G.pager.n && G.pager.m[G.pager.sel].unread) {
    G.pager.m[G.pager.sel].unread = false;
    recountUnread();
  }

  // doorbell: looking at the alert log clears the unread badge
  if (G.screen == SC_DOORBELL && hw::screenOn() && !G.launcher && ago(now, screenAt) >= DOUBLE_MS && G.door.unread) {
    for (int i = 0; i < G.door.n; i++) G.door.e[i].unread = false;
    G.door.unread = 0;
  }

  // radio + IR
  pollWifiScan(now);
  if (G.ble.scanning && ago(now, bleReqAt) > 20000UL) G.ble.scanning = false;   // request lost: never stick on SNIFFING
  Ir& r = G.ir;
  if (r.mode == 1) {
    int got = ir::learnPoll();
    if (got == 1) {
      ir::learnStop();
      bool saved = ir::saveNew(ir::lastRaw, ir::lastLen);
      r.lastLen = ir::lastLen; r.mode = saved ? 2 : 4; r.modeAt = now;
      if (saved) { r.sel = r.n - 1; hw::ok(); } else hw::bad();
    } else if (ago(now, r.modeAt) > 10000) { ir::learnStop(); r.mode = 4; r.modeAt = now; hw::bad(); }
  } else if (r.mode >= 2 && ago(now, r.modeAt) > 1500) r.mode = 0;
  if (G.tv.running && ago(now, lastTvAt) > 380) { lastTvAt = now; if (!ir::tvStep()) hw::ok(); }
}

}  // namespace app
}  // namespace shiv
