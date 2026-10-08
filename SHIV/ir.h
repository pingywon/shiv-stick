// SHIV - infrared learn / replay on the ESP32-S3 RMT peripheral (no external IR library),
// plus a small TV power-code sweep. Codes are stored raw (mark/space microseconds) on LittleFS.
#pragma once
#include <Arduino.h>
#include <LittleFS.h>
#include "esp32-hal-rmt.h"
#include "config.h"
#include "hw.h"

namespace shiv {
namespace ir {

static const int PIN_TX = 46;
static const int PIN_RX = 42;
static const int MAX_SYM = 192;            // RX memory: 4 blocks x 48 symbols
static const int MAX_DUR = MAX_SYM * 2;

static rmt_data_t rxBuf[MAX_SYM];
static size_t rxCount = 0;
static bool rxActive = false, txReady = false;
static uint16_t lastRaw[MAX_DUR];
static uint16_t lastLen = 0;

// The IR parts sit on the external 5 V rail. It is only powered while IR is in use: left on, it
// drains the battery (also through light sleep).
static bool railOn = false;
inline void rail(bool on) {
  if (on == railOn) return;
  M5.Power.setExtOutput(on);
  railOn = on;
  if (on) delay(60);
}

inline void begin() {
  txReady = rmtInit(PIN_TX, RMT_TX_MODE, RMT_MEM_NUM_BLOCKS_1, 1000000);
  if (txReady) { rmtSetCarrier(PIN_TX, true, false, 38000, 0.33f); rmtSetEOT(PIN_TX, 0); }
}

// ---------------------------------------------------------------- storage
inline void fileName(int slot, char* out, size_t n) { snprintf(out, n, "/ir%02d.bin", slot); }

inline void saveIndex() {
  JsonDocument d;
  JsonArray a = d.to<JsonArray>();
  for (int i = 0; i < G.ir.n; i++) { JsonObject o = a.add<JsonObject>(); o["name"] = G.ir.c[i].name; o["len"] = G.ir.c[i].len; }
  File f = LittleFS.open("/ir.json", "w");
  if (f) { serializeJson(d, f); f.close(); }
}

inline void loadIndex() {
  G.ir.n = 0;
  File f = LittleFS.open("/ir.json", "r");
  if (!f) return;
  JsonDocument d;
  if (!deserializeJson(d, f)) {
    for (JsonObjectConst o : d.as<JsonArrayConst>()) {
      if (G.ir.n >= MAX_IR) break;
      scopy(G.ir.c[G.ir.n].name, o["name"] | "IR");
      G.ir.c[G.ir.n].len = o["len"] | 0;
      G.ir.n++;
    }
  }
  f.close();
}

inline bool saveNew(const uint16_t* raw, uint16_t len) {
  if (G.ir.n >= MAX_IR || len < 6) return false;
  int slot = G.ir.n;
  char fn[16];
  fileName(slot, fn, sizeof(fn));
  File f = LittleFS.open(fn, "w");
  if (!f) return false;
  f.write((const uint8_t*)raw, len * 2);
  f.close();
  int num = slot + 1;
  bool clash;
  do {
    clash = false;
    snprintf(G.ir.c[slot].name, sizeof(G.ir.c[slot].name), "IR-%02d", num);
    for (int i = 0; i < slot; i++) if (!strcmp(G.ir.c[i].name, G.ir.c[slot].name)) { clash = true; num++; break; }
  } while (clash);
  G.ir.c[slot].len = len;
  G.ir.n++;
  saveIndex();
  return true;
}

inline void remove(int idx) {
  if (idx < 0 || idx >= G.ir.n) return;
  char a[16], b[16];
  fileName(idx, a, sizeof(a));
  LittleFS.remove(a);
  for (int i = idx + 1; i < G.ir.n; i++) {     // keep files and index contiguous
    fileName(i, a, sizeof(a)); fileName(i - 1, b, sizeof(b));
    LittleFS.rename(a, b);
    G.ir.c[i - 1] = G.ir.c[i];
  }
  G.ir.n--;
  if (G.ir.sel > G.ir.n) G.ir.sel = G.ir.n;
  saveIndex();
}

// ---------------------------------------------------------------- transmit
inline bool sendRaw(const uint16_t* raw, uint16_t len) {
  if (!txReady || len < 2) return false;
  static rmt_data_t tx[MAX_SYM + 1];
  int ns = 0;
  for (int i = 0; i < len && ns < MAX_SYM; i += 2) {
    tx[ns].level0 = 1; tx[ns].duration0 = raw[i] > 32767 ? 32767 : raw[i];
    uint16_t sp = (i + 1 < len) ? raw[i + 1] : 0;
    tx[ns].level1 = 0; tx[ns].duration1 = sp > 32767 ? 32767 : (sp ? sp : 1);
    ns++;
  }
  return rmtWrite(PIN_TX, tx, ns, 1500);
}

inline bool sendSaved(int idx) {
  if (idx < 0 || idx >= G.ir.n) return false;
  struct RailGuard { bool was; RailGuard() : was(railOn) { rail(true); } ~RailGuard() { if (!was) rail(false); } } guard;
  char fn[16];
  fileName(idx, fn, sizeof(fn));
  File f = LittleFS.open(fn, "r");
  if (!f) return false;
  static uint16_t raw[MAX_DUR];
  int len = f.read((uint8_t*)raw, sizeof(raw)) / 2;
  f.close();
  return sendRaw(raw, len);
}

// ---------------------------------------------------------------- learn
inline bool learnStart() {
  hw::speakerPark(true);                   // the amp injects noise into the receiver
  rail(true);
  if (!rmtInit(PIN_RX, RMT_RX_MODE, RMT_MEM_NUM_BLOCKS_4, 1000000)) { hw::speakerPark(false); rail(false); return false; }
  rmtSetRxMinThreshold(PIN_RX, 3);         // hardware glitch filter tops out near 3 us on the S3 - larger values make rmt_receive fail silently
  rmtSetRxMaxThreshold(PIN_RX, 15000);     // 15 ms of silence ends a frame
  rxCount = MAX_SYM;
  rxActive = rmtReadAsync(PIN_RX, rxBuf, &rxCount);
  if (!rxActive) { rmtDeinit(PIN_RX); hw::speakerPark(false); rail(false); }
  return rxActive;
}

inline void learnStop() {
  if (rxActive) { rmtDeinit(PIN_RX); rxActive = false; }
  rail(false);
  hw::speakerPark(false);
}

// 0 = still waiting, 1 = captured into lastRaw/lastLen, -1 = junk (re-armed)
inline int learnPoll() {
  if (!rxActive || !rmtReceiveCompleted(PIN_RX)) return 0;
  uint16_t n = 0;
  for (size_t i = 0; i < rxCount && n + 1 < MAX_DUR; i++) {
    if (!rxBuf[i].duration0) break;
    lastRaw[n++] = rxBuf[i].duration0;
    if (!rxBuf[i].duration1) break;
    lastRaw[n++] = rxBuf[i].duration1;
  }
  bool clean = n >= 16;
  for (uint16_t i = 0; clean && i + 1 < n; i++) if (lastRaw[i] < 150) clean = false;
  if (clean) { lastLen = n; return 1; }
  rxCount = MAX_SYM;
  rmtReadAsync(PIN_RX, rxBuf, &rxCount);   // too short to be a remote: keep listening
  return -1;
}

// ---------------------------------------------------------------- TV power sweep
// Pulse-distance codes are written the classic way (MSB first). kind: 0 NEC-timing, 1 Samsung, 2 Sony12,
// 3 Panasonic48, 4 JVC16, 5 RC5, 6 RC6.
struct TvCode { const char* brand; uint8_t kind; uint64_t data; };
static const TvCode TV_CODES[] = {
  {"SAMSUNG", 1, 0xE0E040BFULL},   {"LG / VIZIO", 0, 0x20DF10EFULL}, {"SONY", 2, 0xA90ULL},
  {"TCL / ROKU", 0, 0x57E3E817ULL}, {"PANASONIC", 3, 0x40040100BCBDULL}, {"TOSHIBA", 0, 0x02FD48B7ULL},
  {"HISENSE", 0, 0x00FDB04FULL},   {"SANYO", 0, 0x1CE348B7ULL},     {"PHILIPS", 5, 0x0CULL},
  {"PHILIPS RC6", 6, 0x0CULL},     {"JVC", 4, 0xC5E8ULL},
};
static const int TV_N = sizeof(TV_CODES) / sizeof(TV_CODES[0]);
static bool rcToggle = false;               // Philips sets ignore a repeated toggle value as a held key

inline uint16_t pulseDistance(uint16_t* o, uint16_t hm, uint16_t hs, uint16_t bm, uint16_t one, uint16_t zero, uint64_t data, int bits) {
  uint16_t n = 0;
  if (hm) { o[n++] = hm; o[n++] = hs; }
  for (int i = bits - 1; i >= 0; i--) { o[n++] = bm; o[n++] = ((data >> i) & 1) ? one : zero; }
  o[n++] = bm; o[n++] = 20000;
  return n;
}

inline void manchester(uint16_t* o, uint16_t& n, bool level, uint16_t t) {   // append a half-bit, merging equal levels
  bool lastMark = (n % 2) == 1;                                              // odd count => last entry is a mark
  if (n && lastMark == level) o[n - 1] += t; else if (n == 0 && !level) return; else o[n++] = t;
}

inline uint16_t buildTv(const TvCode& c, uint16_t* o) {
  uint16_t n = 0;
  switch (c.kind) {
    case 0: return pulseDistance(o, 9000, 4500, 560, 1690, 560, c.data, 32);
    case 1: return pulseDistance(o, 4500, 4500, 560, 1690, 560, c.data, 32);
    case 3: return pulseDistance(o, 3456, 1728, 432, 1296, 432, c.data, 48);
    case 4: return pulseDistance(o, 8400, 4200, 525, 1575, 525, c.data, 16);
    case 2:                                   // Sony SIRC-12: pulse WIDTH coded, sent 3 times
      for (int rep = 0; rep < 3; rep++) {
        o[n++] = 2400; o[n++] = 600;
        for (int i = 11; i >= 0; i--) { o[n++] = ((c.data >> i) & 1) ? 1200 : 600; o[n++] = (i == 0) ? 25000 : 600; }
      }
      return n;
    case 5: {                                 // RC5: 14 bits, 1 = space->mark, address 0
      uint16_t word = (uint16_t)((1 << 13) | (1 << 12) | ((rcToggle ? 1 : 0) << 11) | (0 << 6) | (c.data & 0x3F));
      for (int i = 13; i >= 0; i--) {
        bool b = (word >> i) & 1;
        manchester(o, n, !b, 889); manchester(o, n, b, 889);
      }
      if (n % 2) o[n++] = 20000;
      return n;
    }
    case 6: {                                 // RC6 mode 0: leader, start 1, mode 000, toggle (double width), 8 addr, 8 cmd
      o[n++] = 2666; o[n++] = 889;
      auto mbit = [&](bool b, uint16_t t) { manchester(o, n, b, t); manchester(o, n, !b, t); };
      mbit(true, 444);
      for (int i = 0; i < 3; i++) mbit(false, 444);
      mbit(rcToggle, 889);
      uint16_t word = (uint16_t)(c.data & 0xFF);
      for (int i = 15; i >= 0; i--) mbit((word >> i) & 1, 444);
      if (n % 2) o[n++] = 20000;
      return n;
    }
  }
  return 0;
}

// Send the next code of a running sweep. Returns false when the sweep is complete.
inline bool tvStep() {
  TvOff& t = G.tv;
  if (t.idx == 0) { rail(true); rcToggle = !rcToggle; }
  if (t.idx >= TV_N) { t.running = false; t.doneAt = millis(); rail(false); return false; }
  static uint16_t raw[MAX_DUR];
  const TvCode& c = TV_CODES[t.idx];
  scopy(t.brand, c.brand);
  uint16_t len = buildTv(c, raw);
  if (len) sendRaw(raw, len);              // once only: a repeated full frame can toggle the TV back on
  t.idx++;
  return true;
}

}  // namespace ir
}  // namespace shiv
