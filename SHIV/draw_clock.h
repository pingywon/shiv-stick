// SHIV - clock faces, boot sequence, setup-mode screen, alarm overlay
#pragma once
#include "ui.h"

namespace shiv {
namespace draw {

using namespace ui;
static const char* const DOW[7] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
static const char* const MON[12] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
static const int CLOCK_FACES = 4;

inline void dateStr(const State& st, char* out, size_t n) {
  snprintf(out, n, "%s %02d %s", DOW[st.lt.tm_wday % 7], st.lt.tm_mday, MON[st.lt.tm_mon % 12]);
}

inline void noTime(Canvas& c, const Theme& th) {
  text(c, TITLE, "NO TIME", W / 2, 48, lgfx::v1::middle_center, th.dim);
  text(c, BODY, "NEEDS WIFI ONCE", W / 2, 92, lgfx::v1::middle_center, th.warn);
}

// Face 0 - giant digits, date, seconds rail
inline void clockGiant(Canvas& c, const State& st, const Theme& th) {
  char t[8];
  snprintf(t, sizeof(t), "%02d", st.lt.tm_hour);
  text(c, SEGXL, t, W / 2 - 9, 4, lgfx::v1::top_right, th.acc);
  snprintf(t, sizeof(t), "%02d", st.lt.tm_min);
  text(c, SEGXL, t, W / 2 + 9, 4, lgfx::v1::top_left, th.acc);
  bool tick = st.lt.tm_sec & 1;
  c.fillRect(W / 2 - 4, 26, 8, 8, tick ? th.acc : th.acc2);
  c.fillRect(W / 2 - 4, 50, 8, 8, tick ? th.acc : th.acc2);
  char d[20];
  dateStr(st, d, sizeof(d));
  text(c, BODY, d, 6, 98, lgfx::v1::middle_left, th.fg);
  battery(c, th, st.sys, W - 32, 92);
  if (st.pager.unread) {
    char u[8];
    snprintf(u, sizeof(u), "%d", st.pager.unread);
    int bx = W - 40;
    int w = textW(c, BODY, u);
    c.fillRect(bx - w - 22, 89, w + 20, 19, th.acc2);
    text(c, BODY, u, bx - 6, 98, lgfx::v1::middle_right, th.bg);
    c.drawLine(bx - w - 19, 92, bx - w - 13, 98, th.bg);
    c.drawLine(bx - w - 13, 98, bx - w - 8, 92, th.bg);
  }
  // 60-tick seconds rail
  int s = st.lt.tm_sec;
  for (int i = 0; i < 60; i++) {
    Col col = i <= s ? ((i % 5 == 0) ? th.acc2 : th.acc) : th.panel;
    c.fillRect(i * 4, 116, 3, (i % 5 == 0) ? 14 : 9, col);
  }
}

// Face 1 - BCD binary with the plain time underneath
inline void clockBinary(Canvas& c, const State& st, const Theme& th) {
  int digs[6] = {st.lt.tm_hour / 10, st.lt.tm_hour % 10, st.lt.tm_min / 10, st.lt.tm_min % 10,
                 st.lt.tm_sec / 10, st.lt.tm_sec % 10};
  int xs[6] = {28, 58, 106, 136, 184, 214};
  for (int k = 0; k < 6; k++) {
    for (int b = 0; b < 4; b++) {
      int cy = 14 + (3 - b) * 22;
      bool on = (digs[k] >> b) & 1;
      Col col = (k < 2) ? th.acc : (k < 4) ? th.acc2 : th.warn;
      if (on) c.fillCircle(xs[k], cy, 8, col);
      else { c.drawCircle(xs[k], cy, 8, th.panel); c.drawCircle(xs[k], cy, 7, th.panel); }
    }
  }
  char t[12];
  snprintf(t, sizeof(t), "%02d:%02d:%02d", st.lt.tm_hour, st.lt.tm_min, st.lt.tm_sec);
  text(c, BIG, t, W / 2, 116, lgfx::v1::middle_center, th.fg);
}

// Face 2 - unix epoch + hex time
inline void clockEpoch(Canvas& c, const State& st, const Theme& th) {
  char t[16];
  text(c, BODY, "UNIX EPOCH", 6, 13, lgfx::v1::middle_left, th.dim);
  snprintf(t, sizeof(t), "%02d:%02d", st.lt.tm_hour, st.lt.tm_min);
  text(c, BODY, t, W - 6, 13, lgfx::v1::middle_right, th.fg);
  snprintf(t, sizeof(t), "%lu", (unsigned long)st.epoch);
  text(c, MONO, t, W / 2, 44, lgfx::v1::middle_center, th.acc);
  text(c, BODY, "HEX TIME", 6, 80, lgfx::v1::middle_left, th.dim);
  snprintf(t, sizeof(t), "%02X:%02X:%02X", st.lt.tm_hour, st.lt.tm_min, st.lt.tm_sec);
  text(c, MONO, t, W / 2, 111, lgfx::v1::middle_center, th.acc2);
  brackets(c, 1, 26, W - 2, 38, th.panel, 10);
  brackets(c, 1, 93, W - 2, 38, th.panel, 10);
}

// Face 3 - fake system monitor
inline void clockSysmon(Canvas& c, const State& st, const Theme& th) {
  char t[24];
  c.fillRect(0, 0, W, 26, th.panel);
  text(c, BODY, "SHIV//SYS", 6, 13, lgfx::v1::middle_left, th.acc);
  snprintf(t, sizeof(t), "%02d:%02d:%02d", st.lt.tm_hour, st.lt.tm_min, st.lt.tm_sec);
  text(c, BODY, t, W - 6, 13, lgfx::v1::middle_right, th.fg);

  int y = 41;
  text(c, BODY, "BAT", 6, y, lgfx::v1::middle_left, th.dim);
  bar(c, 68, y - 8, 96, 15, st.sys.battPct / 100.0f, st.sys.battPct <= 15 ? th.bad : th.ok, th.panel);
  snprintf(t, sizeof(t), "%d%%", st.sys.battPct);
  text(c, BODY, t, W - 6, y, lgfx::v1::middle_right, th.fg);

  y = 68;
  text(c, BODY, "NET", 6, y, lgfx::v1::middle_left, th.dim);
  float sig = st.sys.wifi ? (st.sys.rssi + 95) / 55.0f : 0;
  bar(c, 68, y - 8, 96, 15, sig, th.acc, th.panel);
  if (st.sys.wifi) snprintf(t, sizeof(t), "%d", st.sys.rssi);
  else snprintf(t, sizeof(t), "OFF");
  text(c, BODY, t, W - 6, y, lgfx::v1::middle_right, st.sys.wifi ? th.fg : th.bad);

  y = 95;
  text(c, BODY, "MEM", 6, y, lgfx::v1::middle_left, th.dim);
  float mem = st.sys.heapKb / 400.0f;
  bar(c, 68, y - 8, 96, 15, mem, th.acc2, th.panel);
  snprintf(t, sizeof(t), "%luK", (unsigned long)st.sys.heapKb);
  text(c, BODY, t, W - 6, y, lgfx::v1::middle_right, th.fg);

  y = 122;
  text(c, BODY, "UP", 6, y, lgfx::v1::middle_left, th.dim);
  uint32_t u = st.sys.uptimeS;
  if (u >= 86400) snprintf(t, sizeof(t), "%lud %luh", (unsigned long)(u / 86400), (unsigned long)((u / 3600) % 24));
  else snprintf(t, sizeof(t), "%luh %02lum", (unsigned long)(u / 3600), (unsigned long)((u / 60) % 60));
  text(c, BODY, t, 68, y, lgfx::v1::middle_left, th.fg);
}

inline void clock(Canvas& c, const State& st, const Theme& th) {
  if (!st.sys.timeValid) { noTime(c, th); return; }
  switch (st.set.clockFace % CLOCK_FACES) {
    case 0: clockGiant(c, st, th); break;
    case 1: clockBinary(c, st, th); break;
    case 2: clockEpoch(c, st, th); break;
    default: clockSysmon(c, st, th); break;
  }
  // SHIV-16: when the spectrum idle face is on, a slim strip along the bottom pulses to the room.
  if (st.set.spectrumIdle && st.mic.active && ago(st.now, st.mic.frameAt) < 1500) {
    const int B = micdsp::BANDS, base = H - 1, maxH = 7, gap = 2;
    const int bw = (W - gap * (B + 1)) / B;
    for (int i = 0; i < B; i++) {
      int x = gap + i * (bw + gap);
      int h = (int)((uint32_t)st.mic.band[i] * maxH / 255);
      if (h < 1) h = 1;
      c.fillRect(x, base - h, bw, h, (i & 1) ? th.acc : th.acc2);
    }
  }
}

// ---------------------------------------------------------------- boot
static const int BOOT_MS = 3200;
inline void boot(Canvas& c, const Theme& th, uint32_t t, uint32_t seed, const char* ver) {
  c.fillScreen(th.bg);
  if (t < 1300) {
    // glitching logo
    float k = t / 1300.0f;
    int jitter = (int)((1.0f - k) * 10);
    uint32_t r = seed + t / 60;
    r = r * 1664525UL + 1013904223UL;
    int ox = jitter ? (int)((r >> 8) % (2 * jitter + 1)) - jitter : 0;
    useFont(c, TITLE);
    c.setTextSize(2);
    c.setTextDatum(lgfx::v1::middle_center);
    c.setTextColor(th.acc2); c.drawString("SHIV", W / 2 + ox + 3, 56);
    c.setTextColor(th.acc);  c.drawString("SHIV", W / 2 - ox - 3, 56);
    c.setTextColor(th.fg);   c.drawString("SHIV", W / 2, 56);
    c.setTextSize(1);
    for (int i = 0; i < 4; i++) {           // glitch slices
      r = r * 1664525UL + 1013904223UL;
      if ((int)(r % 100) < jitter * 8) {
        int sy = 20 + (r >> 8) % 70, sh = 3 + (r >> 16) % 8, dx = (int)((r >> 20) % 30) - 15;
        c.setClipRect(0, sy, W, sh);
        c.fillRect(0, sy, W, sh, th.bg);
        useFont(c, TITLE); c.setTextSize(2);
        c.setTextColor((i & 1) ? th.acc : th.acc2);
        c.drawString("SHIV", W / 2 + dx, 56);
        c.setTextSize(1);
        c.clearClipRect();
      }
    }
    text(c, BODY, "POCKET DAEMON", W / 2, 112, lgfx::v1::middle_center, k > 0.5f ? th.dim : th.bg);
    char vb[16]; snprintf(vb, sizeof(vb), "v%s", ver ? ver : "?");
    text(c, BODY, vb, W / 2, 90, lgfx::v1::middle_center, th.acc);   // which build is on the stick - bright the whole logo phase
    decoHex(c, 2, 2, seed + t / 120, 8, th.panel);
    decoHex(c, 2, H - 9, seed * 3 + t / 90, 8, th.panel);
  } else if (t < 2700) {
    // system checks
    static const char* const L[] = {"CORE", "RADIO", "MOTION", "INFRARED", "DAEMON"};
    static const char* const V[] = {"OK", "OK", "OK", "ARMED", "AWAKE"};
    int n = (int)((t - 1300) / 330) + 1;
    if (n > 5) n = 5;
    for (int i = 0; i < n; i++) {
      int y = 14 + i * 26;
      text(c, BODY, L[i], 8, y, lgfx::v1::middle_left, th.fg);
      bool done = (i < n - 1) || (t - 1300) % 330 > 160;
      int lw = textW(c, BODY, L[i]);
      int vw = textW(c, BODY, V[i]);
      for (int x = 8 + lw + 8; x < W - 8 - vw - 8; x += 8) c.fillRect(x, y + 5, 3, 3, th.dim);
      if (done) text(c, BODY, V[i], W - 8, y, lgfx::v1::middle_right, i >= 3 ? th.acc2 : th.ok);
    }
  } else {
    // ready card - the version is the star, the last thing shown before the app opens so it can't be missed
    char vb[16]; snprintf(vb, sizeof(vb), "v%s", ver ? ver : "?");
    text(c, HEAD, "SHIV", W / 2, 34, lgfx::v1::middle_center, th.acc);
    text(c, BIG, vb, W / 2, 74, lgfx::v1::middle_center, th.fg);
    text(c, BODY, "READY", W / 2, 112, lgfx::v1::middle_center, th.acc2);
  }
}

// ---------------------------------------------------------------- setup mode (captive portal up)
// The k-th saved network that was tried and did not get in (-1 = there are fewer).
inline int joinFailed(const WifiJoin& j, int k) {
  for (int i = 0; i < WIFI_SLOTS; i++) if (j.ssid[i][0] && j.res[i] >= JR_PASS && k-- == 0) return i;
  return -1;
}

// "BAD PASSWORD #15": the radio's own reason number rides along when there is room for it.
inline void joinReason(Canvas& c, const WifiJoin& j, int slot, int x, int y, Col col) {
  char b[32];
  snprintf(b, sizeof(b), "%s #%u", joinText(j.res[slot]), (unsigned)j.code[slot]);
  if (!j.code[slot] || textW(c, BODY, b) > W - 2 * x) snprintf(b, sizeof(b), "%s", joinText(j.res[slot]));
  text(c, BODY, b, x, y, lgfx::v1::middle_left, col);
}

inline void setupMode(Canvas& c, const State& st, const Theme& th, const char* apName) {
  c.fillScreen(th.bg);
  c.fillRect(0, 0, W, 26, th.acc2);
  text(c, BODY, "WIFI SETUP", W / 2, 13, lgfx::v1::middle_center, th.bg);
  int nf = 0;
  while (joinFailed(st.join, nf) >= 0) nf++;
  int phase = (int)((st.now / 3000) % (uint32_t)(nf + 1));   // 0 = how to join, then each network that failed and why
  if (phase) {
    int f = joinFailed(st.join, phase - 1);
    textFit(c, BODY, st.join.ssid[f], 6, 40, W - 12, lgfx::v1::middle_left, th.fg);
    joinReason(c, st.join, f, 6, 63, th.warn);
  } else {
    text(c, BODY, "JOIN THIS WIFI:", 6, 40, lgfx::v1::middle_left, th.dim);
    textFit(c, BODY, apName, 6, 63, W - 12, lgfx::v1::middle_left, th.acc);
  }
  text(c, BODY, "GO TO 192.168.4.1", 6, 89, lgfx::v1::middle_left, th.fg);
  if (st.sys.wifiSet) footer(c, th, "USE OFFLINE", nullptr);
  else if ((st.now / 400) & 1) c.fillRect(W - 16, 110, 9, 16, th.acc);
}

// Power-up: the saved networks are tried one after another. One block per saved network:
// hollow = not in range, outline = waiting, filling = being tried, red = failed, green = joined.
inline void joining(Canvas& c, const State& st, const Theme& th) {
  const WifiJoin& j = st.join;
  c.fillRect(0, 0, W, 26, th.acc);
  text(c, BODY, "JOINING WIFI", W / 2, 13, lgfx::v1::middle_center, th.bg);
  int n = 0;
  for (int i = 0; i < WIFI_SLOTS; i++) if (j.ssid[i][0]) n++;
  bool in = j.cur >= 0 && j.res[j.cur] == JR_OK;
  char l2[24];
  if (j.scanning || j.cur < 0) {
    int tw = text(c, BODY, "SCANNING", 6, 40, lgfx::v1::middle_left, th.fg);
    for (int i = 0; i <= (int)((st.now / 300) % 3); i++) c.fillRect(6 + tw + 10 + i * 12, 43, 6, 6, th.fg);
    snprintf(l2, sizeof(l2), "%d SAVED", n);
    text(c, BODY, l2, 6, 63, lgfx::v1::middle_left, th.dim);
  } else {
    textFit(c, BODY, j.ssid[j.cur], 6, 40, W - 12, lgfx::v1::middle_left, th.acc);
    if (j.res[j.cur] >= JR_PASS) joinReason(c, j, j.cur, 6, 63, th.warn);
    else {
      if (in) snprintf(l2, sizeof(l2), "JOINED"); else snprintf(l2, sizeof(l2), "TRY %d OF %d", j.tryNo, j.tries);
      text(c, BODY, l2, 6, 63, lgfx::v1::middle_left, in ? th.ok : th.fg);
    }
  }
  if (n) {
    int gap = 4, bw = (W - 12 - (n - 1) * gap) / n, x = 6, y = 80, bh = 18;
    for (int i = 0; i < WIFI_SLOTS; i++) {
      if (!j.ssid[i][0]) continue;
      uint8_t r = j.res[i];
      if (r == JR_OK) c.fillRect(x, y, bw, bh, th.ok);
      else if (r >= JR_PASS) c.fillRect(x, y, bw, bh, th.bad);
      else if (r == JR_TRYING) {
        uint32_t el = ago(st.now, j.tryAt);
        if (el > JOIN_TRY_MS) el = JOIN_TRY_MS;
        c.drawRect(x, y, bw, bh, th.acc);
        c.fillRect(x + 2, y + 2, (int)((bw - 4) * el / JOIN_TRY_MS), bh - 4, th.acc);
      } else c.drawRect(x, y, bw, bh, r == JR_FAR ? th.panel : th.fg);
      x += bw + gap;
    }
  }
  footer(c, th, "SKIP", "SETUP");
}

// ---------------------------------------------------------------- ringing overlay
inline void ringing(Canvas& c, const State& st, const Theme& th, const char* big, const char* sub) {
  bool flash = (st.now / 250) & 1;
  c.fillScreen(flash ? th.acc2 : th.bg);
  Col fg = flash ? th.bg : th.acc2;
  text(c, XL, big, W / 2, 44, lgfx::v1::middle_center, fg);
  text(c, BODY, sub, W / 2, 90, lgfx::v1::middle_center, fg);
  text(c, BODY, "PRESS TO STOP", W / 2, 118, lgfx::v1::middle_center, fg);
}

}  // namespace draw
}  // namespace shiv
