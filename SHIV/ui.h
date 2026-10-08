// SHIV - UI kit. Every piece of text goes through ui::text() so the PC harness can
// audit it: nothing the owner must read may use the DECO role, and nothing may run
// off the 240 px edge.
#pragma once
#include "state.h"
#include "theme.h"
#ifdef SHIV_HOST
#include <vector>
#include <string>
#endif

namespace shiv {
namespace ui {

// Font roles. capPx = measured capital/digit height in pixels on the real panel.
enum Role : uint8_t { DECO, BODY, HEAD, TITLE, BIG, XL, SEG, SEGXL, MONO, ROLE_N };
static const uint8_t CAP_PX[ROLE_N] = {7, 17, 17, 24, 25, 35, 46, 70, 23};
static const uint8_t FLOOR_PX = 16;

inline const lgfx::v1::IFont* fontOf(Role r) {
  switch (r) {
    case DECO:  return &FN::Font0;
    case BODY:  return &FN::FreeSansBold12pt7b;
    case HEAD:  return &FN::Orbitron_Light_24;
    case TITLE: return &FN::Orbitron_Light_32;
    case BIG:   return &FN::FreeSansBold18pt7b;
    case XL:    return &FN::FreeSansBold24pt7b;
    case SEG:   return &FN::Font7;
    case SEGXL: return &FN::Font8;
    case MONO:  return &FN::FreeMonoBold18pt7b;
    default:    return &FN::FreeSansBold12pt7b;
  }
}

#ifdef SHIV_HOST
struct AuditRec { int role; char s[96]; int x0, y0, x1, y1; };
inline std::vector<AuditRec>& audit() { static std::vector<AuditRec> v; return v; }
#endif

inline void useFont(Canvas& c, Role r) {
  c.setFont(fontOf(r));
  c.setTextSize(1);
}

inline int textW(Canvas& c, Role r, const char* s) {
  useFont(c, r);
  return c.textWidth(s);
}

inline int text(Canvas& c, Role r, const char* s, int x, int y, Datum d, Col col) {
  useFont(c, r);
  c.setTextDatum(d);
  c.setTextColor(col);
  int w = c.textWidth(s);
  c.drawString(s, x, y);
#ifdef SHIV_HOST
  if (s && s[0]) {
    // pixel-exact bounding box: redraw the string on a scratch sprite and scan it
    static Canvas* scr = nullptr;
    if (!scr) { scr = new Canvas(); scr->setColorDepth(16); scr->createSprite(W * 3, H * 3); }
    scr->fillScreen(0);
    scr->setFont(fontOf(r)); scr->setTextSize(1); scr->setTextDatum(d); scr->setTextColor(0xFFFFFFu);
    scr->drawString(s, x + W, y + H);
    AuditRec a;
    a.role = r;
    strncpy(a.s, s, sizeof(a.s) - 1);
    a.s[sizeof(a.s) - 1] = 0;
    a.x0 = 99999; a.y0 = 99999; a.x1 = -99999; a.y1 = -99999;
    for (int yy = 0; yy < H * 3; yy++)
      for (int xx = 0; xx < W * 3; xx++)
        if (scr->readPixelValue(xx, yy)) {
          if (xx - W < a.x0) a.x0 = xx - W;
          if (xx - W + 1 > a.x1) a.x1 = xx - W + 1;
          if (yy - H < a.y0) a.y0 = yy - H;
          if (yy - H + 1 > a.y1) a.y1 = yy - H + 1;
        }
    if (a.x1 > a.x0) audit().push_back(a);
  }
#endif
  return w;
}

// Draw text, shortening with ".." until it fits maxW.
inline int textFit(Canvas& c, Role r, const char* s, int x, int y, int maxW, Datum d, Col col) {
  useFont(c, r);
  if (c.textWidth(s) <= maxW) return text(c, r, s, x, y, d, col);
  char buf[96];
  scopy(buf, s);
  int n = (int)strlen(buf);
  while (n > 1) {
    n--;
    buf[n] = 0;
    char t[100];
    snprintf(t, sizeof(t), "%s..", buf);
    if (c.textWidth(t) <= maxW) return text(c, r, t, x, y, d, col);
  }
  return text(c, r, "..", x, y, d, col);
}

// Greedy word wrap into fixed line buffers. Returns number of lines produced.
inline int wrap(Canvas& c, Role r, const char* s, int maxW, char lines[][40], int maxLines) {
  useFont(c, r);
  int nl = 0;
  char cur[40] = "";
  const char* p = s;
  while (*p && nl < maxLines) {
    while (*p == ' ') p++;
    if (!*p) break;
    char word[40];
    int wl = 0;
    while (*p && *p != ' ' && *p != '\n' && wl < 38) word[wl++] = *p++;
    word[wl] = 0;
    bool nlBreak = (*p == '\n');
    if (nlBreak) p++;
    char trial[84];
    if (cur[0]) snprintf(trial, sizeof(trial), "%s %s", cur, word);
    else snprintf(trial, sizeof(trial), "%s", word);
    if (c.textWidth(trial) <= maxW && strlen(trial) < 39) {
      scopy(cur, trial);
    } else {
      if (cur[0]) { scopy(lines[nl], cur); nl++; }
      // hard-split a single word that is wider than the line
      while (c.textWidth(word) > maxW && strlen(word) > 1 && nl < maxLines) {
        int k = (int)strlen(word);
        char part[40];
        do { k--; strncpy(part, word, k); part[k] = 0; } while (k > 1 && c.textWidth(part) > maxW);
        scopy(lines[nl], part); nl++;
        memmove(word, word + k, strlen(word + k) + 1);
      }
      scopy(cur, word);
    }
    if (nlBreak && nl < maxLines) { scopy(lines[nl], cur); nl++; cur[0] = 0; }
  }
  if (cur[0] && nl < maxLines) { scopy(lines[nl], cur); nl++; }
  return nl;
}

// ---------------------------------------------------------------- primitives
inline void brackets(Canvas& c, int x, int y, int w, int h, Col col, int len = 8) {
  c.drawFastHLine(x, y, len, col);             c.drawFastVLine(x, y, len, col);
  c.drawFastHLine(x + w - len, y, len, col);   c.drawFastVLine(x + w - 1, y, len, col);
  c.drawFastHLine(x, y + h - 1, len, col);     c.drawFastVLine(x, y + h - len, len, col);
  c.drawFastHLine(x + w - len, y + h - 1, len, col); c.drawFastVLine(x + w - 1, y + h - len, len, col);
}

// Segmented progress bar.
inline void bar(Canvas& c, int x, int y, int w, int h, float frac, Col on, Col off, int seg = 8) {
  if (frac < 0) frac = 0;
  if (frac > 1) frac = 1;
  int n = w / seg;
  int lit = (int)(frac * n + 0.5f);
  for (int i = 0; i < n; i++) c.fillRect(x + i * seg, y, seg - 2, h, i < lit ? on : off);
}

inline void spark(Canvas& c, int x, int y, int w, int h, const uint8_t* v, int n, int vmax, Col col) {
  if (n < 2) return;
  if (vmax < 1) vmax = 1;
  int px = x, py = y + h - 1 - (int)v[0] * (h - 1) / vmax;
  for (int i = 1; i < n; i++) {
    int nx = x + i * (w - 1) / (n - 1);
    int ny = y + h - 1 - (int)v[i] * (h - 1) / vmax;
    c.drawLine(px, py, nx, ny, col);
    c.drawLine(px, py + 1, nx, ny + 1, col);
    px = nx; py = ny;
  }
}

// Decorative hex noise - never carries information.
inline void decoHex(Canvas& c, int x, int y, uint32_t seed, int groups, Col col) {
  char b[64] = "";
  int o = 0;
  for (int i = 0; i < groups && o < 56; i++) {
    seed = seed * 1664525UL + 1013904223UL;
    o += snprintf(b + o, sizeof(b) - o, "%04X ", (unsigned)((seed >> 12) & 0xFFFF));
  }
  useFont(c, DECO);
  c.setTextDatum(lgfx::v1::top_left);
  c.setTextColor(col);
  c.drawString(b, x, y);
}

inline void battery(Canvas& c, const Theme& th, const Sys& sys, int x, int y) {
  Col col = sys.battPct <= 15 ? th.bad : sys.battPct <= 35 ? th.warn : th.ok;
  c.drawRect(x, y, 24, 13, th.fg);
  c.fillRect(x + 24, y + 4, 2, 5, th.fg);
  int fw = (20 * sys.battPct + 50) / 100;
  if (fw < 2) fw = 2;
  c.fillRect(x + 2, y + 2, fw, 9, col);
  if (sys.charging || sys.usb) {  // bolt
    c.fillTriangle(x + 13, y + 1, x + 7, y + 7, x + 12, y + 7, th.bg);
    c.fillTriangle(x + 11, y + 11, x + 17, y + 5, x + 12, y + 5, th.bg);
  }
}

// Slim upright battery for the header (saves width for the title).
inline void batteryTall(Canvas& c, const Theme& th, const Sys& sys, int x, int y) {
  Col col = sys.battPct <= 15 ? th.bad : sys.battPct <= 35 ? th.warn : th.ok;
  c.fillRect(x + 2, y, 5, 2, th.fg);
  c.drawRect(x, y + 2, 9, 17, th.fg);
  int fh = (13 * sys.battPct + 50) / 100;
  if (fh < 2) fh = 2;
  c.fillRect(x + 2, y + 4 + (13 - fh), 5, fh, col);
  if (sys.charging || sys.usb) c.fillRect(x + 3, y + 8, 3, 5, th.bg);
}

static const int HEADER_H = 27;
static const int FOOTER_H = 25;
static const int CONTENT_Y = HEADER_H + 1;

#ifdef SHIV_HOST
inline std::vector<std::string>& notes() { static std::vector<std::string> v; return v; }
#endif

// Header: title left; clock + battery right. The title always wins the space: the
// no-link mark, the unread flag and finally the clock drop out if it would not fit.
inline void header(Canvas& c, const State& st, const Theme& th, const char* title, bool focus = false) {
  c.fillRect(0, 0, W, HEADER_H - 1, th.panel);
  bool unread = st.pager.unread > 0;
  c.drawFastHLine(0, HEADER_H - 1, W, unread ? th.acc2 : th.acc);
  c.fillRect(0, 0, 4, HEADER_H - 1, focus ? th.acc2 : th.acc);
  batteryTall(c, th, st.sys, W - 13, 3);
  int rx = W - 19;
  int tw = textW(c, BODY, title);
  int room = rx - (9 + tw + 8);
  char t[8] = "";
  if (st.sys.timeValid && st.screen != SC_CLOCK) snprintf(t, sizeof(t), "%02d:%02d", st.lt.tm_hour, st.lt.tm_min);
  int timeW = t[0] ? textW(c, BODY, t) + 6 : 0;
  if (timeW && room >= timeW) {
    text(c, BODY, t, rx, 13, lgfx::v1::middle_right, th.fg);
    rx -= timeW; room -= timeW;
  }
  if (unread && room >= 18) {
    if ((st.now / 500) & 1) {
      c.fillRect(rx - 14, 7, 14, 11, th.acc2);
      c.drawLine(rx - 14, 7, rx - 7, 13, th.bg);
      c.drawLine(rx - 1, 7, rx - 7, 13, th.bg);
    }
    rx -= 18; room -= 18;
  }
  if (!st.sys.wifi && room >= 16) {
    c.drawLine(rx - 12, 6, rx - 2, 18, th.bad);
    c.drawLine(rx - 2, 6, rx - 12, 18, th.bad);
    c.drawLine(rx - 11, 6, rx - 1, 18, th.bad);
    c.drawLine(rx - 1, 6, rx - 11, 18, th.bad);
    rx -= 16;
  }
  textFit(c, BODY, title, 9, 13, rx - 12, lgfx::v1::middle_left, focus ? th.acc2 : th.acc);
}

// Footer: what the FRONT button does. click = press, hold = long press, info = plain right text.
inline void footer(Canvas& c, const Theme& th, const char* click, const char* hold, const char* info = nullptr) {
  int y0 = H - FOOTER_H;
  c.fillRect(0, y0, W, FOOTER_H, th.panel);
  c.drawFastHLine(0, y0, W, th.dim);
  int cy = y0 + FOOTER_H / 2 + 1;
  if (click && click[0]) {
    c.fillCircle(11, cy, 7, th.acc);
    text(c, BODY, click, 23, cy, lgfx::v1::middle_left, th.fg);
  }
  if (hold && hold[0]) {
    int w = textW(c, BODY, hold);
    int x = W - 5 - w;
#ifdef SHIV_HOST
    if (click && click[0] && 23 + textW(c, BODY, click) + 10 > x - 20) notes().push_back(std::string("footer clash: ") + click + " / " + hold);
#endif
    text(c, BODY, hold, x, cy, lgfx::v1::middle_left, th.fg);
    c.drawCircle(x - 12, cy, 7, th.acc2);
    c.drawCircle(x - 12, cy, 6, th.acc2);
    c.fillCircle(x - 12, cy, 2, th.acc2);
  } else if (info && info[0]) {
#ifdef SHIV_HOST
    if (click && click[0] && 23 + textW(c, BODY, click) + 10 > W - 5 - textW(c, BODY, info)) notes().push_back(std::string("footer clash: ") + click + " / " + info);
#endif
    text(c, BODY, info, W - 5, cy, lgfx::v1::middle_right, th.dim);
  }
}

inline void toast(Canvas& c, const State& st, const Theme& th) {
  if (!st.toast[0] || ago(st.now, st.toastAt) > 1400) return;
  int w = textW(c, BODY, st.toast) + 24;
  if (w > W - 8) w = W - 8;
  int x = (W - w) / 2, y = H / 2 - 18;
  c.fillRect(x, y, w, 36, th.bg);
  c.drawRect(x, y, w, 36, th.acc2);
  c.drawRect(x + 1, y + 1, w - 2, 34, th.acc2);
  textFit(c, BODY, st.toast, W / 2, y + 18, w - 12, lgfx::v1::middle_center, th.fg);
}

// Age of a data block as short text: "NOW", "4m", "2h".
inline void ageText(const State& st, uint32_t at, char* out, size_t n) {
  if (!at) { snprintf(out, n, "--"); return; }
  uint32_t s = ago(st.now, at) / 1000;
  if (s < 90) snprintf(out, n, "LIVE");
  else if (s < 5400) snprintf(out, n, "%um AGO", (unsigned)(s / 60));
  else snprintf(out, n, "%uh AGO", (unsigned)(s / 3600));
}

// Full-content placeholder when a data screen has nothing yet. Returns true if drawn.
inline bool noData(Canvas& c, const State& st, const Theme& th, uint32_t at, const char* err) {
  if (at) return false;
  int cy = CONTENT_Y + (H - FOOTER_H - CONTENT_Y) / 2;
  const char* l1 = "NO DATA";
  const char* l2 = (err && err[0]) ? err : (st.sys.wifi ? "WAITING.." : "NO WIFI");
  text(c, TITLE, l1, W / 2, cy - 16, lgfx::v1::middle_center, th.dim);
  textFit(c, BODY, l2, W / 2, cy + 20, W - 12, lgfx::v1::middle_center, th.warn);
  return true;
}

// Clock for UI animation inside helpers that do not get the State (set once per frame by draw::screen).
inline uint32_t& uiNow() { static uint32_t v = 0; return v; }

// Generic scrolling list. rowH >= 27 keeps BODY text comfortable. label(i, buf, right, &col).
template <typename FL>
inline void list(Canvas& c, const Theme& th, int y0, int rowH, int rows, int count, int sel, bool focus, FL label) {
  if (count <= 0) return;
  int top = sel - rows / 2;
  if (top > count - rows) top = count - rows;
  if (top < 0) top = 0;
  for (int r = 0; r < rows && top + r < count; r++) {
    int i = top + r;
    int y = y0 + r * rowH;
    char l[64] = "", rt[24] = "";
    Col lc = th.fg;
    label(i, l, sizeof(l), rt, sizeof(rt), lc);
    bool hot = (i == sel);
    if (hot) {
      c.fillRect(0, y, W - 8, rowH - 2, focus ? th.acc : th.panel);
      if (!focus) c.drawRect(0, y, W - 8, rowH - 2, th.acc);
    }
    Col tc = hot && focus ? th.bg : lc;
    int rw = 0;
    if (rt[0] == '@') {               // state dot instead of words: @1 lit, @0 ring
      int dx = W - 24, dy = y + rowH / 2 - 1;
      if (rt[1] == '1') c.fillCircle(dx, dy, 7, hot && focus ? th.bg : th.warn);
      else { c.drawCircle(dx, dy, 7, tc); c.drawCircle(dx, dy, 6, tc); }
      rw = 24;
    } else if (rt[0]) rw = text(c, BODY, rt, W - 14, y + rowH / 2 - 1, lgfx::v1::middle_right, tc) + 8;
    int maxW = W - 8 - 14 - rw;
    useFont(c, BODY);
    int tw = c.textWidth(l);
    if (hot && focus && tw > maxW) {
      // the selected row scrolls sideways so long names can be told apart (pause, slide, pause)
      int span = tw - maxW + 6, cycle = span + 50;
      int off = (int)((uiNow() / 28) % (uint32_t)cycle) - 22;
      if (off < 0) off = 0;
      if (off > span) off = span;
      c.setClipRect(6, y, maxW + 1, rowH - 2);
      c.setTextDatum(lgfx::v1::middle_left);
      c.setTextColor(tc);
      c.drawString(l, 7 - off, y + rowH / 2 - 1);
      c.clearClipRect();
    } else textFit(c, BODY, l, 7, y + rowH / 2 - 1, maxW, lgfx::v1::middle_left, tc);
  }
  // scroll rail
  int railH = rows * rowH - 2;
  c.fillRect(W - 5, y0, 3, railH, th.panel);
  int kh = railH * rows / (count > rows ? count : rows);
  if (kh < 8) kh = 8;
  int ky = (count > rows) ? y0 + (railH - kh) * top / (count - rows) : y0;
  c.fillRect(W - 5, ky, 3, kh, th.acc);
}

}  // namespace ui
}  // namespace shiv
