#pragma once
// Microphone pump. The StickS3 mic and speaker share one I2S bus, so we release
// the speaker (hw::speakerPark) before begin() and give it back on stop(). We only
// listen in silent-safe contexts and always yield the moment a tone is queued, so
// pager/doorbell sounds still play. The math is pure and lives in micdsp (state.h);
// this file is the hardware glue only, and is never part of the PC test build.
// Feeds G.mic for the spectrum face and the daemon (it reacts to the room).

namespace shiv { namespace mic {

static const uint32_t SR = 16000;     // sample rate
static const int N = 256;             // 16 ms frame
static int16_t buf[2][N];             // 2-buffer ring: record into one while the other is read
static int cur = 0;
static bool primed = false;           // the ring has one completed buffer behind `cur`

// Which mic features want to listen right now. Every context here is silent by
// design or low-stakes; a queued tone (soundPending) always wins.
inline bool wanted(uint32_t now) {
  if (!G.set.micOn || !hw::screenOn() || hw::soundPending(now)) return false;
  bool spec = G.set.spectrumIdle && (G.screen == SC_SPECTRUM || G.screen == SC_CLOCK);  // spectrum face
  bool pet  = G.set.petHears && G.screen == SC_DAEMON;                          // the daemon hears the room
  return spec || pet;
}

inline void stop() {
  if (!G.mic.active) return;
  M5.Mic.end();
  hw::speakerPark(false);             // hand the shared bus back to the speaker
  primed = false;
  Lock l;
  G.mic.active = false; G.mic.level = 0;
  for (int b = 0; b < micdsp::BANDS; b++) G.mic.band[b] = 0;
}

inline void start() {
  if (G.mic.active) return;
  hw::speakerPark(true);              // release the speaker's claim on I2S
  if (!M5.Mic.begin()) { hw::speakerPark(false); return; }
  cur = 0; primed = false;
  Lock l; G.mic.active = true;
}

inline void process(const int16_t* b, uint32_t now) {
  uint32_t r = micdsp::rms(b, N);
  uint8_t bd[micdsp::BANDS];
  float norm;
  { Lock l; norm = G.mic.norm; }
  float peak = micdsp::bands(b, N, SR, bd, norm);
  Lock l;
  G.mic.floor = micdsp::updateFloor(G.mic.floor, r);
  G.mic.level = (uint8_t)micdsp::loud(r, G.mic.floor, G.set.micSens);
  for (int i = 0; i < micdsp::BANDS; i++) G.mic.band[i] = bd[i];
  G.mic.norm = peak > G.mic.norm ? peak : G.mic.norm * 0.95f + peak * 0.05f;
  if (G.mic.norm < 500.0f) G.mic.norm = 500.0f;
  int ev = G.mic.clap.feed(r, now, G.set.micSens);
  if (ev) { G.mic.clapEv = (uint8_t)ev; G.mic.clapAt = now; }
  G.mic.frameAt = now;
}

// Called every loop from SHIV.ino (top level, outside the app Lock).
inline void tick(uint32_t now) {
  if (!wanted(now)) { stop(); return; }
  if (!G.mic.active) { start(); return; }        // begin this tick, first frame next
  if (M5.Mic.record(buf[cur], N, SR)) {          // true when the previously enqueued buffer is full
    if (primed) process(buf[cur ^ 1], now);
    primed = true;
    cur ^= 1;
  }
}

// Pull the pending clap event (0 none / 1 single / 2 double), clearing it.
inline uint8_t takeClap() {
  Lock l;
  uint8_t e = G.mic.clapEv; G.mic.clapEv = 0; return e;
}

}} // namespace shiv::mic
