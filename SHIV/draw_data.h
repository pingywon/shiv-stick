// SHIV - timers and live-data screens
#pragma once
#include "ui.h"
#include "draw_clock.h"

namespace shiv {
namespace draw {

using namespace ui;

inline void commas(uint32_t v, char* out, size_t n) {
  char raw[16];
  snprintf(raw, sizeof(raw), "%lu", (unsigned long)v);
  int len = (int)strlen(raw), o = 0;
  for (int i = 0; i < len && o < (int)n - 2; i++) {
    out[o++] = raw[i];
    int left = len - 1 - i;
    if (left > 0 && left % 3 == 0) out[o++] = ',';
  }
  out[o] = 0;
}

inline void mmss(uint32_t ms, char* out, size_t n) {
  uint32_t s = (ms + 999) / 1000;
  if (s >= 3600) snprintf(out, n, "%lu:%02lu:%02lu", (unsigned long)(s / 3600), (unsigned long)((s / 60) % 60), (unsigned long)(s % 60));
  else snprintf(out, n, "%02lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
}

// Animated "working" box shared by the radio scanners and the HA browser.
inline void scanningBox(Canvas& c, const State& st, const Theme& th, const char* what) {
  int cy = CONTENT_Y + 30;
  text(c, TITLE, what, W / 2, cy, lgfx::v1::middle_center, th.acc);
  int k = (st.now / 90) % 24;
  for (int i = 0; i < 24; i++) {
    int d = (i - k + 24) % 24;
    c.fillRect(12 + i * 9, cy + 28, 7, 8, d < 5 ? mix(th.acc, th.panel, d / 5.0f) : th.panel);
  }
}

// ---------------------------------------------------------------- countdown
inline void timer(Canvas& c, const State& st, const Theme& th) {
  const Countdown& t = st.cd;
  if (t.ringing) { ringing(c, st, th, "TIME!", "TIMER DONE"); return; }
  header(c, st, th, "TIMER");
  char b[12];
  mmss(t.remainMs, b, sizeof(b));
  bool fresh = !t.running && t.remainMs == t.totalMs;
  text(c, SEG, b, W / 2, 31, lgfx::v1::top_center, t.running ? th.acc : fresh ? th.fg : th.warn);
  float frac = t.totalMs ? (float)t.remainMs / t.totalMs : 0;
  bar(c, 8, 90, 224, 12, frac, th.acc2, th.panel);
  if (t.running) footer(c, th, "PAUSE", "RESET");
  else if (fresh) footer(c, th, "START", "PRESET");
  else footer(c, th, "RESUME", "RESET");
}

// ---------------------------------------------------------------- pomodoro
inline void pomo(Canvas& c, const State& st, const Theme& th) {
  const Pomo& p = st.pomo;
  if (p.ringing) { ringing(c, st, th, p.phase == 2 ? "BREAK!" : "FOCUS!", p.phase == 2 ? "STEP AWAY" : "BACK TO IT"); return; }
  header(c, st, th, "POMO");
  char b[12], l[20];
  const char* ph = p.phase == 1 ? "FOCUS" : p.phase == 2 ? "BREAK" : "READY";
  Col pc = p.phase == 1 ? th.acc2 : p.phase == 2 ? th.ok : th.dim;
  text(c, BODY, ph, 8, 40, lgfx::v1::middle_left, pc);
  snprintf(l, sizeof(l), "ROUND %d", p.round + (p.phase == 1 ? 1 : 0));
  if (p.phase) text(c, BODY, l, W - 8, 40, lgfx::v1::middle_right, th.dim);
  mmss(p.remainMs, b, sizeof(b));
  text(c, SEG, b, W / 2, 52, lgfx::v1::top_center, p.running ? th.acc : th.fg);
  float frac = p.totalMs ? (float)p.remainMs / p.totalMs : 0;
  bar(c, 8, 102, 224, 6, frac, pc, th.panel);
  char fb[12];
  snprintf(fb, sizeof(fb), "%d / %d", p.focusMin, p.breakMin);
  if (p.phase == 0) footer(c, th, "START", nullptr, fb);
  else footer(c, th, p.running ? "PAUSE" : "RESUME", "END");
}

// ---------------------------------------------------------------- weather
inline const char* wmoText(int code) {
  if (code == 0) return "CLEAR";
  if (code == 1) return "MOSTLY CLEAR";
  if (code == 2) return "PARTLY CLOUDY";
  if (code == 3) return "OVERCAST";
  if (code == 45 || code == 48) return "FOG";
  if (code >= 51 && code <= 57) return "DRIZZLE";
  if (code >= 61 && code <= 65) return "RAIN";
  if (code == 66 || code == 67) return "FREEZING RAIN";
  if (code >= 71 && code <= 77) return "SNOW";
  if (code >= 80 && code <= 82) return "SHOWERS";
  if (code == 85 || code == 86) return "SNOW SHOWERS";
  if (code >= 95) return "THUNDERSTORM";
  return "UNKNOWN";
}

inline void cloudShape(Canvas& c, int x, int y, Col col) {   // ~56 x 30
  c.fillCircle(x + 16, y + 18, 12, col);
  c.fillCircle(x + 30, y + 12, 14, col);
  c.fillCircle(x + 44, y + 19, 11, col);
  c.fillRect(x + 16, y + 18, 28, 13, col);
}

inline void wxIcon(Canvas& c, const Theme& th, int code, bool day, int x, int y, uint32_t now) {
  int cx = x + 30, cy = y + 30;
  bool clearish = code <= 2;
  if (clearish) {
    if (day) {
      int sx = code == 2 ? cx - 8 : cx, sy = code == 2 ? cy - 8 : cy;
      c.fillCircle(sx, sy, code == 2 ? 13 : 16, th.warn);
      for (int i = 0; i < 8; i++) {
        float a = i * 0.7854f + (now / 4000.0f);
        int r0 = code == 2 ? 17 : 21, r1 = code == 2 ? 23 : 28;
        c.drawLine(sx + (int)(cosf(a) * r0), sy + (int)(sinf(a) * r0), sx + (int)(cosf(a) * r1), sy + (int)(sinf(a) * r1), th.warn);
      }
    } else {
      c.fillCircle(cx - 2, cy - 2, 18, th.acc2);
      c.fillCircle(cx + 7, cy - 8, 15, th.bg);
    }
    if (code == 2) cloudShape(c, x + 4, y + 26, th.fg);
    return;
  }
  if (code == 45 || code == 48) {
    for (int i = 0; i < 5; i++) c.fillRect(x + 4 + (i & 1) * 8, y + 10 + i * 10, 46, 4, th.dim);
    return;
  }
  cloudShape(c, x + 2, y + 6, code == 3 ? th.fg : th.dim);
  if (code == 3) return;
  bool snow = (code >= 71 && code <= 77) || code == 85 || code == 86;
  bool storm = code >= 95;
  int ph = (now / 120) % 10;
  for (int i = 0; i < 4; i++) {
    int dx = x + 12 + i * 12, dy = y + 40 + ((ph + i * 3) % 10);
    if (snow) c.fillCircle(dx, dy + 2, 2, th.fg);
    else c.drawLine(dx + 3, dy, dx, dy + 8, th.acc), c.drawLine(dx + 4, dy, dx + 1, dy + 8, th.acc);
  }
  if (storm) {
    c.fillTriangle(x + 32, y + 30, x + 22, y + 46, x + 30, y + 46, th.warn);
    c.fillTriangle(x + 28, y + 60, x + 38, y + 42, x + 30, y + 42, th.warn);
  }
}

inline void weather(Canvas& c, const State& st, const Theme& th) {
  header(c, st, th, "WEATHER");
  char age[12];
  ageText(st, st.wx.at, age, sizeof(age));
  footer(c, th, "SYNC", nullptr, age);
  if (noData(c, st, th, st.wx.at, st.wx.err)) return;
  const Weather& w = st.wx;
  wxIcon(c, th, w.code, w.isDay, 2, 25, st.now);
  char b[16];
  snprintf(b, sizeof(b), "%d", (int)lroundf(w.tempF));
  int tw = text(c, XL, b, 68, 31, lgfx::v1::top_left, th.fg);
  c.drawCircle(68 + tw + 8, 38, 5, th.fg);
  c.drawCircle(68 + tw + 8, 38, 4, th.fg);
  snprintf(b, sizeof(b), "H %d", (int)lroundf(w.hiF));
  text(c, BODY, b, W - 6, 40, lgfx::v1::middle_right, th.bad);
  snprintf(b, sizeof(b), "L %d", (int)lroundf(w.loF));
  text(c, BODY, b, W - 6, 63, lgfx::v1::middle_right, th.acc);
  textFit(c, BODY, wmoText(w.code), W - 6, 97, W - 12, lgfx::v1::middle_right, th.acc2);
}

// ---------------------------------------------------------------- pi-hole
inline void pihole(Canvas& c, const State& st, const Theme& th) {
  header(c, st, th, "PI-HOLE");
  const Pihole& p = st.pi;
  char click[12];
  if (p.toggling) snprintf(click, sizeof(click), "WAIT..");
  else if (p.at && !p.enabled && p.source == st.set.piTarget) snprintf(click, sizeof(click), "TURN ON");
  else snprintf(click, sizeof(click), "OFF %dH", st.set.piOffHours);
  footer(c, th, click, p.source == 5 ? "v6" : "v5");
  if (noData(c, st, th, p.at, p.err)) return;
  char b[24], n[16];
  if (p.enabled) {
    snprintf(b, sizeof(b), "%.1f%%", p.pct);
    text(c, XL, b, 6, 25, lgfx::v1::top_left, th.acc);
    if (p.histN >= 2) {
      brackets(c, 150, 31, 86, 34, th.dim, 6);
      spark(c, 154, 35, 78, 26, p.hist, p.histN, 60, th.acc2);
    }
  } else {
    text(c, XL, "OFF", 6, 25, lgfx::v1::top_left, th.bad);
    uint32_t until = p.source == st.set.piTarget ? st.set.piOffUntil : 0;
    if (until && st.sys.timeValid && until > st.epoch) {
      uint32_t left = until - st.epoch;
      text(c, BODY, "BACK IN", W - 6, 38, lgfx::v1::middle_right, th.dim);
      if (left >= 3600) snprintf(b, sizeof(b), "%luh %02lum", (unsigned long)(left / 3600), (unsigned long)((left / 60) % 60));
      else snprintf(b, sizeof(b), "%lum %02lus", (unsigned long)(left / 60), (unsigned long)(left % 60));
      text(c, BODY, b, W - 6, 58, lgfx::v1::middle_right, th.warn);
    } else {
      text(c, BODY, "BLOCKING", W - 6, 38, lgfx::v1::middle_right, th.dim);
      text(c, BODY, "DISABLED", W - 6, 58, lgfx::v1::middle_right, th.bad);
    }
  }
  commas(p.blocked, n, sizeof(n));
  text(c, BODY, "BLOCKED", 6, 78, lgfx::v1::middle_left, th.dim);
  text(c, BODY, n, W - 6, 78, lgfx::v1::middle_right, th.fg);
  commas(p.queries, n, sizeof(n));
  text(c, BODY, "QUERIES", 6, 99, lgfx::v1::middle_left, th.dim);
  text(c, BODY, n, W - 6, 99, lgfx::v1::middle_right, th.fg);
}

// ---------------------------------------------------------------- LAN hosts
static const int HOSTS_PER_PAGE = 3;
inline void hosts(Canvas& c, const State& st, const Theme& th) {
  const Hosts& hs = st.hosts;
  int up = 0;
  for (int i = 0; i < hs.n; i++) if (hs.h[i].up == 1) up++;
  char t[20];
  snprintf(t, sizeof(t), "HOSTS %d/%d", up, (int)hs.n);
  header(c, st, th, t);
  if (hs.n == 0) {
    text(c, TITLE, "NO HOSTS", W / 2, 56, lgfx::v1::middle_center, th.dim);
    text(c, BODY, "ADD AT shiv.local", W / 2, 94, lgfx::v1::middle_center, th.warn);
    return;
  }
  int pages = (hs.n + HOSTS_PER_PAGE - 1) / HOSTS_PER_PAGE;
  int pg = hs.page % pages;
  for (int k = 0; k < HOSTS_PER_PAGE; k++) {
    int i = pg * HOSTS_PER_PAGE + k;
    if (i >= hs.n) break;
    const Host& h = hs.h[i];
    int y = CONTENT_Y + 1 + k * 27;
    Col col = h.up == 1 ? th.ok : h.up == 0 ? th.bad : th.dim;
    bool blink = h.up == 0 && ((st.now / 400) & 1);
    if (!blink) c.fillRect(5, y + 5, 13, 15, col);
    else c.drawRect(5, y + 5, 13, 15, col);
    const char* r = h.up == 1 ? "UP" : h.up == 0 ? "DOWN" : "--";
    int rw = text(c, BODY, r, W - 6, y + 12, lgfx::v1::middle_right, col);
    textFit(c, BODY, h.name, 25, y + 12, W - 25 - 6 - rw - 8, lgfx::v1::middle_left, h.up == 0 ? th.bad : th.fg);
  }
  footer(c, th, pages > 1 ? "MORE" : nullptr, "CHECK");
}

// ---------------------------------------------------------------- shopify today
inline void shop(Canvas& c, const State& st, const Theme& th) {
  header(c, st, th, "GC TODAY");
  char age[12];
  ageText(st, st.shop.at, age, sizeof(age));
  footer(c, th, "SYNC", nullptr, age);
  if (noData(c, st, th, st.shop.at, st.shop.err)) return;
  char b[24], n[16];
  commas((uint32_t)(st.shop.revenue + 0.5f), n, sizeof(n));
  snprintf(b, sizeof(b), "$%s", n);
  text(c, XL, b, 6, 27, lgfx::v1::top_left, th.ok);
  snprintf(b, sizeof(b), "%d ORDER%s", st.shop.orders, st.shop.orders == 1 ? "" : "S");
  text(c, BODY, b, 6, 96, lgfx::v1::middle_left, th.fg);
  if (st.shop.unfulfilled > 0) {
    snprintf(b, sizeof(b), "%d SHIP", st.shop.unfulfilled);
    text(c, BODY, b, W - 6, 96, lgfx::v1::middle_right, th.warn);
  }
}

// ---------------------------------------------------------------- claude jobs
inline void jobs(Canvas& c, const State& st, const Theme& th) {
  header(c, st, th, "JOBS");
  char age[12];
  ageText(st, st.jobs.at, age, sizeof(age));
  footer(c, th, "SYNC", nullptr, age);
  if (noData(c, st, th, st.jobs.at, st.jobs.err)) return;
  const Jobs& j = st.jobs;
  const char* lab[3] = {"RUN", "HELD", j.failed ? "FAIL" : "DONE"};
  int val[3] = {j.running + j.queued, j.held, j.failed ? j.failed : j.done};
  Col col[3] = {th.acc, th.warn, j.failed ? th.bad : th.ok};
  for (int i = 0; i < 3; i++) {
    int cx = 40 + i * 80;
    char b[8];
    snprintf(b, sizeof(b), "%d", val[i]);
    bool pulse = i == 0 && val[0] > 0 && ((st.now / 500) & 1);
    text(c, BIG, b, cx, 31, lgfx::v1::top_center, val[i] ? col[i] : th.dim);
    text(c, BODY, lab[i], cx, 70, lgfx::v1::middle_center, th.dim);
    if (pulse) c.fillRect(cx - 30, 58, 60, 2, th.acc);
  }
  textFit(c, BODY, j.lastTitle[0] ? j.lastTitle : "NO RECENT JOBS", 6, 96, W - 12, lgfx::v1::middle_left, th.fg);
}

// ---------------------------------------------------------------- HA CONTROL (categories -> entities)
inline void home(Canvas& c, const State& st, const Theme& th) {
  const HaBrowse& b = st.hab;
  if (b.level < 2) {
    header(c, st, th, "HA", b.level == 1);
    int cnt = haCatCount(st);
    list(c, th, CONTENT_Y + 1, 27, 3, cnt, b.cat < cnt ? b.cat : 0, b.level == 1,
         [&](int i, char* l, size_t ln, char* r, size_t rn, Col& col) {
           uint8_t k = haCatAt(st, i);
           snprintf(l, ln, "%s", HA_CAT_NAMES[k]);
           if (k == HC_FAVS) snprintf(r, rn, "%d", (int)st.home.n);
           (void)col;
         });
    if (b.level == 1) footer(c, th, "OPEN", "BACK");
    else footer(c, th, "ENTER", nullptr);
    return;
  }
  header(c, st, th, HA_CAT_HEAD[b.kind % HC_N], true);
  if (b.n == 0) {
    if (b.loading) { scanningBox(c, st, th, "LOADING"); footer(c, th, nullptr, "BACK"); return; }
    if (b.err[0]) {
      text(c, TITLE, "HA ERROR", W / 2, CONTENT_Y + 26, lgfx::v1::middle_center, th.bad);
      textFit(c, BODY, b.err, W / 2, CONTENT_Y + 62, W - 12, lgfx::v1::middle_center, th.warn);
    } else {
      text(c, TITLE, "NONE", W / 2, CONTENT_Y + 26, lgfx::v1::middle_center, th.dim);
      text(c, BODY, "NOTHING IN HERE", W / 2, CONTENT_Y + 62, lgfx::v1::middle_center, th.dim);
    }
    footer(c, th, "RETRY", "BACK");
    return;
  }
  list(c, th, CONTENT_Y + 1, 27, 3, b.n, b.sel, true,
       [&](int i, char* l, size_t ln, char* r, size_t rn, Col& col) {
         snprintf(l, ln, "%s", b.e[i].name);
         if (b.e[i].on == 1) { snprintf(r, rn, "@1"); col = th.warn; }
         else if (b.e[i].on == 0) snprintf(r, rn, "@0");
       });
  bool runs = b.kind == HC_SCENES || b.kind == HC_SCRIPTS;
  footer(c, th, runs ? "RUN" : b.kind == HC_FAVS ? "FIRE" : "TOGGLE", "BACK");
}

// ---------------------------------------------------------------- macros (confirm-hold)
static const uint32_t MACRO_HOLD_MS = 1200;
inline void macros(Canvas& c, const State& st, const Theme& th) {
  const Macros& m = st.macros;
  header(c, st, th, "MACROS", m.focus);
  if (m.n == 0) {
    text(c, TITLE, "NO MACROS", W / 2, 56, lgfx::v1::middle_center, th.dim);
    text(c, BODY, "ADD AT shiv.local", W / 2, 94, lgfx::v1::middle_center, th.warn);
    return;
  }
  if (m.focus && m.holdStart) {
    float k = (float)ago(st.now, m.holdStart) / MACRO_HOLD_MS;
    textFit(c, BODY, m.m[m.sel].label, W / 2, 48, W - 12, lgfx::v1::middle_center, th.acc2);
    bar(c, 8, 76, 224, 18, k, th.acc2, th.panel);
    footer(c, th, nullptr, nullptr, "KEEP HOLDING");
    return;
  }
  list(c, th, CONTENT_Y + 1, 27, 3, m.n, m.sel, m.focus,
       [&](int i, char* l, size_t ln, char* r, size_t rn, Col& col) {
         snprintf(l, ln, "%s", m.m[i].label);
         (void)r; (void)rn; (void)col;
       });
  if (m.focus) footer(c, th, "BACK", "FIRE");
  else footer(c, th, "ENTER", nullptr);
}

// ---------------------------------------------------------------- doorbell
inline void door(Canvas& c, const State& st, const Theme& th) {
  const Doorbell& d = st.door;
  if (d.n == 0) {
    header(c, st, th, "DOORBELL");
    text(c, TITLE, "NO ALERTS", W / 2, 56, lgfx::v1::middle_center, th.dim);
    text(c, BODY, "ALL QUIET", W / 2, 94, lgfx::v1::middle_center, th.dim);
    return;
  }
  char t[20];
  snprintf(t, sizeof(t), "DOORBELL %d/%d", d.sel + 1, (int)d.n);
  header(c, st, th, t);
  list(c, th, CONTENT_Y + 1, 27, 3, d.n, d.sel, true,
       [&](int i, char* l, size_t ln, char* r, size_t rn, Col& col) {
         const DoorEvent& e = d.e[i % MAX_DOOR];
         if (e.what[0]) snprintf(l, ln, "%s: %s", e.cam, e.what);
         else snprintf(l, ln, "%s", e.cam);
         snprintf(r, rn, "%s", e.when);
         if (!e.unread) col = th.dim;
       });
  footer(c, th, d.n > 1 ? "NEXT" : "OK", "CLEAR");
}

// ---------------------------------------------------------------- mqtt ticker
inline void mqtt(Canvas& c, const State& st, const Theme& th) {
  const MqttState& q = st.mqtt;
  header(c, st, th, "MQTT");
  if (!st.sys.wifi) {
    text(c, TITLE, "OFFLINE", W / 2, 56, lgfx::v1::middle_center, th.dim);
    text(c, BODY, "NO WI-FI", W / 2, 94, lgfx::v1::middle_center, th.dim);
    return;
  }
  if (q.n == 0) {
    const char* a = q.connected ? "LISTENING" : (q.err[0] ? "NO BROKER" : "MQTT OFF");
    const char* b = q.connected ? "NO MESSAGES YET" : (q.err[0] ? q.err : "SET ONE IN PANEL");
    text(c, TITLE, a, W / 2, 56, lgfx::v1::middle_center, q.connected ? th.ok : th.dim);
    text(c, BODY, b, W / 2, 94, lgfx::v1::middle_center, th.dim);
    return;
  }
  list(c, th, CONTENT_Y + 1, 27, 3, q.n, q.sel, true,
       [&](int i, char* l, size_t ln, char* r, size_t rn, Col& col) {
         const MqttMsg& e = q.m[i % MAX_MQTT];
         if (e.text[0]) snprintf(l, ln, "%s %s", e.topic, e.text);
         else snprintf(l, ln, "%s", e.topic);
         snprintf(r, rn, "%s", e.when);
         (void)col;
       });
  footer(c, th, q.n > 1 ? "NEXT" : "OK", "CLEAR");
}

// ---------------------------------------------------------------- pager
static const int PAGER_LINES = 3;
static const int PAGER_W = W - 22;
inline void pagerCompose(const PageMsg& m, char* out, size_t n) {
  if (m.when[0]) snprintf(out, n, "%s %s", m.when, m.text);
  else snprintf(out, n, "%s", m.text);
}
inline int pagerPages(Canvas& c, const PageMsg& m) {
  char full[176];
  pagerCompose(m, full, sizeof(full));
  static char lines[14][40];
  int n = wrap(c, BODY, full, PAGER_W, lines, 14);
  int p = (n + PAGER_LINES - 1) / PAGER_LINES;
  return p < 1 ? 1 : p;
}

inline void pager(Canvas& c, const State& st, const Theme& th) {
  const Pager& p = st.pager;
  char t[20];
  if (p.n == 0) {
    header(c, st, th, "PAGER");
    text(c, TITLE, "NO PAGES", W / 2, 56, lgfx::v1::middle_center, th.dim);
    text(c, BODY, "ALL QUIET", W / 2, 94, lgfx::v1::middle_center, th.dim);
    return;
  }
  snprintf(t, sizeof(t), "PAGE %d/%d", p.sel + 1, (int)p.n);
  header(c, st, th, t);
  const PageMsg& m = p.m[p.sel % MAX_PAGES];
  char full[176];
  pagerCompose(m, full, sizeof(full));
  static char lines[14][40];
  int n = wrap(c, BODY, full, PAGER_W, lines, 14);
  int pages = (n + PAGER_LINES - 1) / PAGER_LINES;
  if (pages < 1) pages = 1;
  int pg = p.scroll % pages;
  for (int i = 0; i < PAGER_LINES; i++) {
    int li = pg * PAGER_LINES + i;
    if (li >= n) break;
    text(c, BODY, lines[li], 6, CONTENT_Y + 13 + i * 26, lgfx::v1::middle_left, th.fg);
  }
  if (pages > 1)
    for (int i = 0; i < pages; i++) c.fillRect(W - 8, CONTENT_Y + 4 + i * 12, 5, 9, i == pg ? th.acc2 : th.panel);
  footer(c, th, (pg < pages - 1) ? "MORE" : (p.n > 1 ? "NEXT" : "OK"), "DEL");
}

}  // namespace draw
}  // namespace shiv
