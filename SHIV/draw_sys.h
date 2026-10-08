// SHIV - system/settings screen, launcher, and the top-level draw dispatcher
#pragma once
#include "ui.h"
#include "draw_clock.h"
#include "draw_data.h"
#include "draw_radio.h"
#include "draw_fun.h"
#include "draw_ota.h"

namespace shiv {

// System rows are shared by the drawing code and the button logic.
enum SysRow : uint8_t {
  SR_IP, SR_THEME, SR_SOUND, SR_VOLUME, SR_BRIGHT, SR_FLIP, SR_SLEEP, SR_CALIB, SR_WIFI, SR_BATT, SR_MEM, SR_VER, SR_REBOOT, SR_COUNT
};

inline const char* level3(uint8_t v, uint8_t lo, uint8_t mid) { return v <= lo ? "LOW" : v <= mid ? "MED" : "HIGH"; }

inline void sysRowText(const State& st, int i, char* l, size_t ln, char* r, size_t rn) {
  const Settings& s = st.set;
  r[0] = 0;
  switch (i) {
    case SR_IP:
      if (st.sys.portal) snprintf(l, ln, "SETUP MODE");
      else if (st.sys.wifi) snprintf(l, ln, "%s", st.sys.ip);
      else snprintf(l, ln, "OFFLINE");
      break;
    case SR_THEME:  snprintf(l, ln, "THEME");  snprintf(r, rn, "%s", themeOf(s.theme).name); break;
    case SR_SOUND:  snprintf(l, ln, "SOUND");  snprintf(r, rn, "%s", s.mute ? "MUTED" : "ON"); break;
    case SR_VOLUME: snprintf(l, ln, "VOLUME"); snprintf(r, rn, "%s", level3(s.volume, 40, 110)); break;
    case SR_BRIGHT: snprintf(l, ln, "BRIGHT"); snprintf(r, rn, "%s", level3(s.brightness, 70, 160)); break;
    case SR_FLIP:   snprintf(l, ln, "ROTATE"); snprintf(r, rn, "%s", s.flip == 0 ? "AUTO" : s.flip == 1 ? "NORMAL" : "FLIPPED"); break;
    case SR_SLEEP:  snprintf(l, ln, "SLEEP");
      if (s.sleepMin) snprintf(r, rn, "%d MIN", s.sleepMin); else snprintf(r, rn, "NEVER");
      break;
    case SR_CALIB:  snprintf(l, ln, "CALIBRATE"); snprintf(r, rn, ">"); break;
    case SR_WIFI:   snprintf(l, ln, "WIFI SETUP"); snprintf(r, rn, ">"); break;
    case SR_BATT:   snprintf(l, ln, "BATT %d%%", st.sys.battPct); snprintf(r, rn, "%d.%02dV", st.sys.battMv / 1000, (st.sys.battMv % 1000) / 10); break;
    case SR_MEM:    snprintf(l, ln, "FREE MEM"); snprintf(r, rn, "%luK", (unsigned long)st.sys.heapKb); break;
    case SR_VER:    snprintf(l, ln, "VERSION"); snprintf(r, rn, "%s", st.sys.version); break;
    case SR_REBOOT: snprintf(l, ln, "REBOOT"); snprintf(r, rn, ">"); break;
    default: l[0] = 0;
  }
}

inline bool sysRowActs(int i) {
  return i == SR_THEME || i == SR_SOUND || i == SR_VOLUME || i == SR_BRIGHT || i == SR_FLIP || i == SR_SLEEP || i == SR_CALIB || i == SR_WIFI || i == SR_REBOOT;
}

namespace draw {

using namespace ui;

// Motion calibration wizard (SYSTEM > CALIBRATE)
inline void calib(Canvas& c, const State& st, const Theme& th) {
  header(c, st, th, "CALIBRATE", true);
  int step = st.sys.calib;
  if (step == 1) {
    text(c, BODY, "1  LAY ME FLAT", 6, CONTENT_Y + 14, lgfx::v1::middle_left, th.fg);
    text(c, BODY, "SCREEN FACE UP", 6, CONTENT_Y + 40, lgfx::v1::middle_left, th.acc);
    text(c, BODY, "THEN PRESS", 6, CONTENT_Y + 66, lgfx::v1::middle_left, th.dim);
    footer(c, th, "SET", "QUIT");
  } else if (step == 2) {
    text(c, BODY, "2  STAND ME UP", 6, CONTENT_Y + 14, lgfx::v1::middle_left, th.fg);
    text(c, BODY, "TEXT UPRIGHT", 6, CONTENT_Y + 40, lgfx::v1::middle_left, th.acc);
    text(c, BODY, "THEN PRESS", 6, CONTENT_Y + 66, lgfx::v1::middle_left, th.dim);
    footer(c, th, "SET", "QUIT");
  } else {
    text(c, TITLE, "SAVED", W / 2, CONTENT_Y + 28, lgfx::v1::middle_center, th.ok);
    text(c, BODY, "MOTION IS TUNED", W / 2, CONTENT_Y + 64, lgfx::v1::middle_center, th.fg);
    footer(c, th, "OK", nullptr);
  }
}

inline void system(Canvas& c, const State& st, const Theme& th) {
  if (st.sys.calib) { calib(c, st, th); return; }
  bool focus = st.sys.focus && st.screen == SC_SYSTEM;
  header(c, st, th, "SYSTEM", focus);
  list(c, th, CONTENT_Y + 1, 27, 3, SR_COUNT, st.sys.sel, focus,
       [&](int i, char* l, size_t ln, char* r, size_t rn, Col& col) {
         sysRowText(st, i, l, ln, r, rn);
         if (!sysRowActs(i)) col = th.dim;
         if (i == SR_IP) col = st.sys.wifi ? th.ok : th.warn;
       });
  if (focus) footer(c, th, sysRowActs(st.sys.sel) ? "CHANGE" : nullptr, "BACK");
  else footer(c, th, "ENTER", nullptr);
}

// Launcher: hold the side button anywhere. Groups first, then the screens in the open group.
inline void launcher(Canvas& c, const State& st, const Theme& th) {
  bool top = st.launcherGrp < 0;
  int cnt = top ? nav::grpCount(st) : nav::grpScreens(st, st.launcherGrp);
  c.fillRect(0, 0, W, HEADER_H - 1, th.acc2);
  text(c, BODY, top ? "GO TO" : GROUP_NAMES[st.launcherGrp], 8, 13, lgfx::v1::middle_left, th.bg);
  char n[24];
  snprintf(n, sizeof(n), "%d/%d", st.launcherSel + 1, cnt);
  text(c, BODY, n, W - 8, 13, lgfx::v1::middle_right, th.bg);
  list(c, th, CONTENT_Y + 1, 27, 3, cnt, st.launcherSel, true,
       [&](int i, char* l, size_t ln, char* r, size_t rn, Col& col) {
         if (top) {
           int g = nav::grpAt(st, i);
           snprintf(l, ln, "%s", GROUP_NAMES[g]);
           int b = nav::grpBadge(st, g);
           if (b) snprintf(r, rn, "%d NEW", b);
         } else {
           int sc = nav::grpScreenAt(st, st.launcherGrp, i);
           snprintf(l, ln, "%s", SCREEN_NAMES[sc]);
           if (sc == SC_PAGER && st.pager.unread) snprintf(r, rn, "%d NEW", st.pager.unread);
           if (sc == SC_DOORBELL && st.door.unread) snprintf(r, rn, "%d NEW", st.door.unread);
         }
         (void)col;
       });
  footer(c, th, "OPEN", top ? "CLOSE" : "BACK");
}

// ---------------------------------------------------------------- dispatcher
inline void screen(Canvas& c, const State& st) {
  const Theme& th = themeOf(st.set.theme);
  ui::uiNow() = st.now;
  c.fillScreen(th.bg);
  uint8_t otaPh = otaui::phase(st, st.now);
  if (otaPh == otaui::INSTALLING || otaPh == otaui::DONE) { otaCard(c, st, th); return; }   // mid-flash: nothing else may draw
  if (st.cd.ringing) { ringing(c, st, th, "TIME!", "TIMER DONE"); return; }
  if (st.pomo.ringing) { ringing(c, st, th, st.pomo.phase == 2 ? "BREAK!" : "FOCUS!", st.pomo.phase == 2 ? "STEP AWAY" : "BACK TO IT"); return; }
  if (st.join.show) { joining(c, st, th); toast(c, st, th); return; }
  if (st.launcher) { launcher(c, st, th); toast(c, st, th); return; }
  if (otaPh == otaui::OFFER || otaPh == otaui::FAILED) { otaCard(c, st, th); toast(c, st, th); return; }
  switch (st.screen) {
    case SC_CLOCK:   clock(c, st, th); break;
    case SC_TIMER:   timer(c, st, th); break;
    case SC_POMO:    pomo(c, st, th); break;
    case SC_WEATHER: weather(c, st, th); break;
    case SC_PIHOLE:  pihole(c, st, th); break;
    case SC_HOSTS:   hosts(c, st, th); break;
    case SC_SHOP:    shop(c, st, th); break;
    case SC_JOBS:    jobs(c, st, th); break;
    case SC_HOME:    home(c, st, th); break;
    case SC_MACROS:  macros(c, st, th); break;
    case SC_PAGER:   pager(c, st, th); break;
    case SC_DOORBELL: door(c, st, th); break;
    case SC_MQTT:    mqtt(c, st, th); break;
    case SC_WIFI:    if (st.wifi.detail) wifiDetail(c, st, th); else wifi(c, st, th); break;
    case SC_CHAN:    channels(c, st, th); break;
    case SC_BLE:     ble(c, st, th); break;
    case SC_IR:      ir(c, st, th); break;
    case SC_TVOFF:   tvoff(c, st, th); break;
    case SC_LEVEL:   level(c, st, th); break;
    case SC_ORACLE:  oracle(c, st, th); break;
    case SC_GAMES:   games(c, st, th); break;
    case SC_REFLEX:  reflex(c, st, th); break;
    case SC_SPECTRUM: spectrum(c, st, th); break;
    case SC_DAEMON:  daemon(c, st, th); break;
    case SC_SYSTEM:  system(c, st, th); break;
    default: break;
  }
  toast(c, st, th);
}

}  // namespace draw
}  // namespace shiv
