// SHIV - passive radio scanners + infrared screens
#pragma once
#include "ui.h"
#include "draw_data.h"

namespace shiv {
namespace draw {

using namespace ui;

inline void sigBars(Canvas& c, const Theme& th, int x, int y, int rssi, Col on) {
  int lvl = rssi >= -55 ? 4 : rssi >= -67 ? 3 : rssi >= -78 ? 2 : rssi >= -88 ? 1 : 0;
  for (int i = 0; i < 4; i++) {
    int h = 5 + i * 4;
    c.fillRect(x + i * 5, y + 17 - h, 4, h, i < lvl ? on : th.panel);
  }
}

// ---------------------------------------------------------------- wifi list
inline void wifi(Canvas& c, const State& st, const Theme& th) {
  const WifiScan& w = st.wifi;
  char t[16];
  snprintf(t, sizeof(t), "WIFI %d", (int)w.count);
  bool focus = st.sys.focus && st.screen == SC_WIFI;
  header(c, st, th, t, focus);
  if (w.scanning && w.count == 0) { scanningBox(c, st, th, "SCANNING"); footer(c, th, nullptr, nullptr, "LISTEN ONLY"); return; }
  if (w.count == 0) {
    text(c, TITLE, "NO SCAN", W / 2, 56, lgfx::v1::middle_center, th.dim);
    footer(c, th, "SCAN", nullptr);
    return;
  }
  int rows = 3, rowH = 27, y0 = CONTENT_Y + 1;
  int top = w.sel - 1;
  if (top > w.count - rows) top = w.count - rows;
  if (top < 0) top = 0;
  for (int r = 0; r < rows && top + r < w.count; r++) {
    int i = top + r, y = y0 + r * rowH;
    const Net& n = w.n[i];
    bool hot = focus && i == w.sel;
    if (hot) c.fillRect(0, y, W - 8, rowH - 2, th.acc);
    Col tc = hot ? th.bg : n.open ? th.warn : th.fg;
    sigBars(c, th, 4, y + 3, n.rssi, hot ? th.bg : th.ok);
    char ch[8];
    snprintf(ch, sizeof(ch), "%d", n.ch);
    int rw = text(c, BODY, ch, W - 14, y + rowH / 2 - 1, lgfx::v1::middle_right, hot ? th.bg : th.acc2);
    textFit(c, BODY, n.ssid[0] ? n.ssid : "(hidden)", 28, y + rowH / 2 - 1, W - 28 - 22 - rw, lgfx::v1::middle_left, tc);
  }
  int railH = rows * rowH - 2;
  c.fillRect(W - 5, y0, 3, railH, th.panel);
  int kh = railH * rows / (w.count > rows ? w.count : rows);
  if (kh < 8) kh = 8;
  int ky = (w.count > rows) ? y0 + (railH - kh) * top / (w.count - rows) : y0;
  c.fillRect(W - 5, ky, 3, kh, th.acc);
  if (focus) footer(c, th, "INFO", "BACK");
  else footer(c, th, "LIST", "SCAN");
}

// Detail card for the selected network (long names wrap onto two lines).
inline void wifiDetail(Canvas& c, const State& st, const Theme& th) {
  const Net& n = st.wifi.n[st.wifi.sel % MAX_NETS];
  header(c, st, th, "NETWORK", true);
  static char lines[3][40];
  int nl = wrap(c, BODY, n.ssid[0] ? n.ssid : "(hidden)", W - 12, lines, 2);
  for (int i = 0; i < nl; i++) text(c, BODY, lines[i], 6, CONTENT_Y + 13 + i * 24, lgfx::v1::middle_left, th.acc);
  char b[24];
  snprintf(b, sizeof(b), "CH %d", n.ch);
  text(c, BODY, b, 6, CONTENT_Y + 65, lgfx::v1::middle_left, th.fg);
  snprintf(b, sizeof(b), "%d dBm", n.rssi);
  text(c, BODY, b, W - 6, CONTENT_Y + 65, lgfx::v1::middle_right, th.fg);
  footer(c, th, "CLOSE", nullptr, n.open ? "OPEN!" : "LOCKED");
}

// ---------------------------------------------------------------- channel congestion
inline void channels(Canvas& c, const State& st, const Theme& th) {
  const WifiScan& w = st.wifi;
  header(c, st, th, "AIR 2.4G");
  if (w.scanning && w.count == 0) { scanningBox(c, st, th, "SCANNING"); return; }
  if (w.count == 0) {
    text(c, TITLE, "NO SCAN", W / 2, 56, lgfx::v1::middle_center, th.dim);
    footer(c, th, "SCAN", nullptr);
    return;
  }
  int best = 1, bestLoad = 9999, maxLoad = 1;
  for (int ch = 1; ch <= 13; ch++) if (w.chanLoad[ch] > maxLoad) maxLoad = w.chanLoad[ch];
  static const int CAND[3] = {1, 6, 11};
  for (int k = 0; k < 3; k++) {
    int ch = CAND[k];
    int load = w.chanLoad[ch] * 2 + (ch > 1 ? w.chanLoad[ch - 1] : 0) + (ch < 13 ? w.chanLoad[ch + 1] : 0) +
               (ch > 2 ? w.chanLoad[ch - 2] / 2 : 0) + (ch < 12 ? w.chanLoad[ch + 2] / 2 : 0);
    if (load < bestLoad) { bestLoad = load; best = ch; }
  }
  char b[20];
  snprintf(b, sizeof(b), "BEST %d", best);
  text(c, BODY, b, 6, CONTENT_Y + 12, lgfx::v1::middle_left, th.ok);
  int base = 110, maxH = 56;
  for (int ch = 1; ch <= 13; ch++) {
    int x = 6 + (ch - 1) * 18;
    int h = w.chanLoad[ch] * maxH / maxLoad;
    Col col = ch == best ? th.ok : (ch == 1 || ch == 6 || ch == 11) ? th.acc : th.acc2;
    c.fillRect(x, base - 2, 14, 2, th.dim);
    if (h > 0) c.fillRect(x, base - h, 14, h, col);
  }
  for (int k = 0; k < 3; k++) {
    int ch = CAND[k];
    snprintf(b, sizeof(b), "%d", ch);
    text(c, BODY, b, 6 + (ch - 1) * 18 + 7, 124, lgfx::v1::middle_center, ch == best ? th.ok : th.fg);
  }
  int sw = text(c, BODY, "SCAN", W - 6, CONTENT_Y + 12, lgfx::v1::middle_right, th.dim);
  c.fillCircle(W - 6 - sw - 11, CONTENT_Y + 12, 6, th.acc);
}

// ---------------------------------------------------------------- bluetooth LE
inline void ble(Canvas& c, const State& st, const Theme& th) {
  const BleScan& b = st.ble;
  char t[16];
  snprintf(t, sizeof(t), "BLE %d", (int)b.total);
  bool focus = st.sys.focus && st.screen == SC_BLE;
  header(c, st, th, t, focus);
  if (b.scanning && b.count == 0) { scanningBox(c, st, th, "SNIFFING"); footer(c, th, nullptr, nullptr, "LISTEN ONLY"); return; }
  if (b.count == 0) {
    text(c, TITLE, "NO SCAN", W / 2, 56, lgfx::v1::middle_center, th.dim);
    footer(c, th, "SCAN", nullptr);
    return;
  }
  int rows = 3, rowH = 27, y0 = CONTENT_Y + 1;
  int top = b.sel - 1;
  if (top > b.count - rows) top = b.count - rows;
  if (top < 0) top = 0;
  for (int r = 0; r < rows && top + r < b.count; r++) {
    int i = top + r, y = y0 + r * rowH;
    const BleDev& d = b.d[i];
    bool hot = focus && i == b.sel;
    if (hot) c.fillRect(0, y, W - 8, rowH - 2, th.acc);
    sigBars(c, th, 4, y + 3, d.rssi, hot ? th.bg : th.acc2);
    const char* label = d.name[0] ? d.name : d.mac + 9;   // unnamed: last 3 octets
    textFit(c, BODY, label, 28, y + rowH / 2 - 1, W - 28 - 14, lgfx::v1::middle_left, hot ? th.bg : d.name[0] ? th.fg : th.dim);
  }
  int railH = rows * rowH - 2;
  c.fillRect(W - 5, y0, 3, railH, th.panel);
  int kh = railH * rows / (b.count > rows ? b.count : rows);
  if (kh < 8) kh = 8;
  int ky = (b.count > rows) ? y0 + (railH - kh) * top / (b.count - rows) : y0;
  c.fillRect(W - 5, ky, 3, kh, th.acc);
  if (focus) footer(c, th, nullptr, "BACK", nullptr);
  else footer(c, th, "LIST", "SCAN");
}

// ---------------------------------------------------------------- IR learn / replay
inline void ir(Canvas& c, const State& st, const Theme& th) {
  const Ir& r = st.ir;
  header(c, st, th, "REMOTE", r.focus);
  if (r.mode == 1) {
    text(c, BODY, "AIM REMOTE HERE", W / 2, CONTENT_Y + 14, lgfx::v1::middle_center, th.fg);
    text(c, BODY, "PRESS A BUTTON", W / 2, CONTENT_Y + 40, lgfx::v1::middle_center, th.acc);
    uint32_t el = ago(st.now, r.modeAt);
    bar(c, 8, CONTENT_Y + 60, 224, 12, 1.0f - el / 10000.0f, th.acc2, th.panel);
    footer(c, th, "CANCEL", nullptr);
    return;
  }
  if (r.mode >= 2 && ago(st.now, r.modeAt) < 1500) {
    const char* m = r.mode == 2 ? "LEARNED" : r.mode == 3 ? "SENT" : "NO SIGNAL";
    text(c, TITLE, m, W / 2, CONTENT_Y + 26, lgfx::v1::middle_center, r.mode == 4 ? th.bad : th.ok);
    if (r.mode == 2) {
      char b[24];
      snprintf(b, sizeof(b), "%d PULSES", r.lastLen);
      text(c, BODY, b, W / 2, CONTENT_Y + 62, lgfx::v1::middle_center, th.dim);
    }
    return;
  }
  int count = r.n + (r.n < MAX_IR ? 1 : 0);
  list(c, th, CONTENT_Y + 1, 27, 3, count, r.sel, r.focus,
       [&](int i, char* l, size_t ln, char* rt, size_t rn, Col& col) {
         if (i < r.n) snprintf(l, ln, "%s", r.c[i].name);
         else { snprintf(l, ln, "+ LEARN NEW"); col = th.acc2; }
         (void)rt; (void)rn;
       });
  if (r.focus) footer(c, th, r.sel < r.n ? "SEND" : "LEARN", "BACK");
  else footer(c, th, "ENTER", nullptr);
}

// ---------------------------------------------------------------- TV power sweep
inline void tvoff(Canvas& c, const State& st, const Theme& th) {
  const TvOff& t = st.tv;
  header(c, st, th, "TV-OFF");
  if (t.running) {
    char b[24];
    snprintf(b, sizeof(b), "%d/%d", t.idx, t.total);
    text(c, XL, b, W / 2, 30, lgfx::v1::top_center, th.acc2);
    textFit(c, BODY, t.brand, W / 2, 80, W - 12, lgfx::v1::middle_center, th.fg);
    bar(c, 8, 94, 224, 10, t.total ? (float)t.idx / t.total : 0, th.acc2, th.panel);
    footer(c, th, "STOP", nullptr);
  } else {
    bool justDone = t.doneAt && ago(st.now, t.doneAt) < 2500;
    text(c, TITLE, justDone ? "DONE" : "READY", W / 2, 50, lgfx::v1::middle_center, justDone ? th.ok : th.fg);
    text(c, BODY, "AIM AT THE TV", W / 2, 90, lgfx::v1::middle_center, th.dim);
    footer(c, th, "SWEEP", nullptr);
  }
}

}  // namespace draw
}  // namespace shiv
