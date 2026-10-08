// SHIV - shared state. Plain C++ only (no Arduino types) so the same
// drawing code builds on the device and in the PC render harness.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>

namespace shiv {

static const int W = 240;
static const int H = 135;

enum ScreenId : uint8_t {
  SC_CLOCK, SC_TIMER, SC_POMO, SC_WEATHER, SC_PIHOLE, SC_HOSTS, SC_SHOP, SC_JOBS,
  SC_HOME, SC_MACROS, SC_PAGER, SC_WIFI, SC_CHAN, SC_BLE, SC_IR, SC_TVOFF,
  SC_LEVEL, SC_ORACLE, SC_GAMES, SC_DAEMON, SC_SYSTEM, SC_REFLEX, SC_DOORBELL, SC_MQTT, SC_SPECTRUM, SC_COUNT
};

static const char* const SCREEN_NAMES[SC_COUNT] = {
  "CLOCK", "TIMER", "POMODORO", "WEATHER", "PI-HOLE", "HOSTS", "SHOP", "JOBS",
  "HA CONTROL", "MACROS", "PAGER", "WIFI", "CHANNELS", "BLE", "IR REMOTE", "TV-OFF",
  "LEVEL", "ORACLE", "GAMES", "DAEMON", "SYSTEM", "REFLEX", "DOORBELL", "MQTT", "SPECTRUM"
};

// The order screens come up in (SIDE cycle + GO TO list). Saved settings store the ids above,
// so reorder HERE and leave the enum alone.
static const uint8_t SCREEN_ORDER[SC_COUNT] = {
  SC_CLOCK, SC_TVOFF, SC_REFLEX, SC_SPECTRUM, SC_TIMER, SC_POMO, SC_WEATHER, SC_PIHOLE, SC_HOSTS, SC_SHOP, SC_JOBS,
  SC_HOME, SC_MACROS, SC_PAGER, SC_DOORBELL, SC_MQTT,
  SC_WIFI, SC_CHAN, SC_BLE, SC_IR, SC_LEVEL, SC_ORACLE, SC_GAMES, SC_DAEMON, SC_SYSTEM
};

// ---- Microphone DSP (pure, PC-tested in tools/test_pet.cpp) ----
// The StickS3 mic shares the I2S bus with the speaker, so mic.h only samples when
// no tone is playing. These helpers turn a frame of int16 PCM into the features
// the mic screens need - loudness, a tracked noise floor, claps, an 8-band
// magnitude for the spectrum face. No hardware here, just math.
namespace micdsp {

const int BANDS = 8;
// Bar centre frequencies (Hz): log-spaced across speech and music.
static const float BAND_HZ[BANDS] = {120, 250, 500, 900, 1500, 2500, 4000, 6000};

// RMS of a frame (0..~23170 for full-scale int16 input).
inline uint32_t rms(const int16_t* buf, int n) {
  if (!buf || n <= 0) return 0;
  uint64_t sum = 0;
  for (int i = 0; i < n; i++) { int32_t v = buf[i]; sum += (uint64_t)(v * v); }
  return (uint32_t)sqrt((double)(sum / (uint32_t)n));
}

// Goertzel magnitude of one frequency bin over the frame.
inline float goertzel(const int16_t* buf, int n, uint32_t sampleRate, float hz) {
  if (!buf || n <= 0 || sampleRate == 0) return 0.0f;
  float w = 2.0f * 3.14159265f * hz / (float)sampleRate;
  float coeff = 2.0f * cosf(w);
  float s1 = 0, s2 = 0;
  for (int i = 0; i < n; i++) { float s0 = (float)buf[i] + coeff * s1 - s2; s2 = s1; s1 = s0; }
  float mag2 = s1 * s1 + s2 * s2 - coeff * s1 * s2;
  return mag2 > 0 ? sqrtf(mag2) : 0.0f;
}

// Fill 8 band levels 0..255, scaled against `norm` (a running max the caller keeps
// so a quiet room still shows movement). Returns the frame's peak raw magnitude so
// the caller can decay `norm` toward it.
inline float bands(const int16_t* buf, int n, uint32_t sampleRate, uint8_t out[BANDS], float norm) {
  float mag[BANDS]; float peak = 1.0f;
  for (int b = 0; b < BANDS; b++) { mag[b] = goertzel(buf, n, sampleRate, BAND_HZ[b]); if (mag[b] > peak) peak = mag[b]; }
  float scale = norm > peak ? norm : peak; if (scale < 1.0f) scale = 1.0f;
  for (int b = 0; b < BANDS; b++) { int v = (int)(mag[b] * 255.0f / scale); out[b] = (uint8_t)(v > 255 ? 255 : v < 0 ? 0 : v); }
  return peak;
}

// Loudness 0..8 from an RMS reading, its noise floor and sensitivity 1..9
// (higher sens = reacts to quieter sound).
inline int loud(uint32_t r, uint32_t floor, uint8_t sens) {
  if (r <= floor) return 0;
  uint8_t s = sens > 9 ? 9 : sens < 1 ? 1 : sens;
  uint32_t span = 3000 - (uint32_t)s * 280;        // ~2720 (sens 1) .. 480 (sens 9)
  if (span < 200) span = 200;
  uint32_t lv = ((r - floor) * 8) / span;
  return lv > 8 ? 8 : (int)lv;
}

// Rolling noise-floor tracker: drops quickly toward quiet frames, rises very slowly.
inline uint32_t updateFloor(uint32_t floor, uint32_t r) {
  if (r < floor) return floor - ((floor - r) >> 3);
  return floor + ((r - floor) >> 7);
}

// Clap / double-clap detector, fed one RMS frame at a time.
enum { EV_NONE = 0, EV_CLAP1 = 1, EV_CLAP2 = 2 };
struct Clap {
  uint32_t floor = 400;
  bool armed = false;          // a transient is up, waiting to fall back down
  uint32_t armedAt = 0;
  uint8_t count = 0;           // claps counted in the open window
  uint32_t windowUntil = 0;    // ms the double-clap window closes
  uint32_t lockUntil = 0;      // debounce after an accepted onset
  // Feed one frame (RMS + wall-clock ms + sensitivity 1..9). Returns EV_*.
  int feed(uint32_t r, uint32_t nowMs, uint8_t sens) {
    floor = updateFloor(floor, r);
    uint8_t s = sens > 7 ? 7 : sens < 1 ? 1 : sens;
    uint32_t thresh = floor * (uint32_t)(8 - s) + 600;   // higher sens = lower bar
    if (armed && nowMs - armedAt > 250) armed = false;   // a sustained sound is not a clap
    if (!armed && nowMs >= lockUntil && r > thresh && r > 1200) {
      armed = true; armedAt = nowMs; lockUntil = nowMs + 90;
    } else if (armed && r < floor * 2 + 400) {
      armed = false;
      if (count == 0 || nowMs > windowUntil) { count = 1; windowUntil = nowMs + 600; }
      else { count = 0; windowUntil = 0; return EV_CLAP2; }
    }
    if (count == 1 && windowUntil && nowMs > windowUntil) { count = 0; windowUntil = 0; return EV_CLAP1; }
    return EV_NONE;
  }
};

} // namespace micdsp

// Live microphone readings, filled by mic.h from the DSP above. Consumed by the
// spectrum face and the daemon (it reacts to voice, music and a loud room).
struct Mic {
  bool active = false;                        // M5.Mic is begun right now (speaker released)
  uint8_t level = 0;                          // 0..8 loudness
  uint8_t band[micdsp::BANDS] = {};           // 0..255 spectrum bars
  uint32_t floor = 400;                       // tracked noise floor (rms units)
  float norm = 800.0f;                        // running max for band scaling
  micdsp::Clap clap;                          // clap-detector state
  uint8_t clapEv = 0;                         // pending event: 0 none, 1 single, 2 double (input consumes it)
  uint32_t clapAt = 0;
  uint32_t frameAt = 0;                       // ms of the last processed frame (liveliness)
};

// GO TO groups. Indexed by ScreenId (enum order, not display order).
enum ScreenGroup : uint8_t { SG_HOME, SG_DATA, SG_CONTROL, SG_RADIO, SG_PLAY, SG_SYSTEM, SG_N };
static const char* const GROUP_NAMES[SG_N] = {"HOME", "DATA", "CONTROL", "RADIO", "PLAY", "SYSTEM"};
static const uint8_t SCREEN_GROUP[SC_COUNT] = {
  SG_HOME, SG_HOME, SG_HOME, SG_DATA, SG_DATA, SG_DATA, SG_DATA, SG_DATA,          // CLOCK TIMER POMO WEATHER PIHOLE HOSTS SHOP JOBS
  SG_CONTROL, SG_CONTROL, SG_DATA, SG_RADIO, SG_RADIO, SG_RADIO, SG_CONTROL, SG_CONTROL,   // HOME MACROS PAGER WIFI CHAN BLE IR TVOFF
  SG_PLAY, SG_PLAY, SG_PLAY, SG_PLAY, SG_SYSTEM, SG_PLAY, SG_DATA, SG_DATA,        // LEVEL ORACLE GAMES DAEMON SYSTEM REFLEX DOORBELL MQTT
  SG_PLAY                                                                          // SPECTRUM
};

// Milliseconds from stamp t to now. A stamp taken with a fresher millis() than "now" (same loop pass,
// or written by the net task) must read as 0 - a plain unsigned subtraction wraps to ~49 days.
inline uint32_t ago(uint32_t now, uint32_t t) {
  uint32_t d = now - t;
  return d > 0x7FFFFFFFu ? 0 : d;
}

inline uint32_t rnd(uint32_t& s) {
  s ^= s << 13; s ^= s >> 17; s ^= s << 5;
  return s;
}

template <size_t N> inline void scopy(char (&dst)[N], const char* src) {
  if (!src) src = "";
  strncpy(dst, src, N - 1);
  dst[N - 1] = 0;
}

// ---------------------------------------------------------------- live data
struct Weather {
  uint32_t at = 0;            // millis of last good fetch, 0 = never
  char err[20] = "";
  float tempF = 0, hiF = 0, loF = 0, windMph = 0;
  int humidity = 0;
  int code = 0;               // WMO weather code
  bool isDay = true;
};

struct Pihole {
  uint32_t at = 0;
  char err[20] = "";
  uint32_t queries = 0, blocked = 0;
  float pct = 0;
  bool enabled = true;
  uint8_t source = 6;         // 5 = old v5, 6 = new v6
  uint8_t hist[24] = {0};     // blocked-% history ring for the sparkline
  uint8_t histN = 0;
  bool toggling = false;      // an on/off request is in flight
};

static const int MAX_HOSTS = 8;
struct Host {
  char name[12] = "";
  char addr[40] = "";
  uint16_t port = 80;
  int8_t up = -1;             // -1 unknown, 0 down, 1 up
  uint16_t ms = 0;
};
struct Hosts {
  uint32_t at = 0;
  Host h[MAX_HOSTS];
  uint8_t n = 0;
  uint8_t page = 0;
};

struct Shop {
  uint32_t at = 0;
  char err[20] = "";
  int orders = 0;
  float revenue = 0;
  int unfulfilled = 0;
};

struct Jobs {
  uint32_t at = 0;
  char err[20] = "";
  int running = 0, held = 0, queued = 0, done = 0, failed = 0;
  char lastTitle[40] = "";
  char lastStatus[12] = "";
};

static const int MAX_FAVS = 6;
struct Fav {
  char label[14] = "";
  char entity[48] = "";
  int8_t on = -1;             // -1 unknown / stateless (scene, script)
};
struct Home {
  uint32_t at = 0;
  char err[20] = "";
  Fav f[MAX_FAVS];
  uint8_t n = 0, sel = 0;
  bool focus = false;
  uint32_t firedAt = 0;       // flash feedback
  bool firedOk = true;
};

// On-stick Home Assistant browser: categories -> entities of one domain.
enum HaCat : uint8_t { HC_FAVS, HC_LIGHTS, HC_SWITCHES, HC_SCENES, HC_SCRIPTS, HC_N };
static const char* const HA_CAT_NAMES[HC_N] = {"FAVORITES", "LIGHTS", "SWITCHES", "SCENES", "SCRIPTS"};
static const char* const HA_CAT_HEAD[HC_N] = {"HA FAVS", "HA LIGHTS", "HA SWITCH", "HA SCENES", "HA SCRIPTS"};
static const char* const HA_CAT_DOMAIN[HC_N] = {"", "light", "switch", "scene", "script"};
static const int MAX_HA_ENT = 96;
struct HaEnt {
  char entity[48] = "";
  char name[32] = "";
  int8_t on = -1;
};
struct HaBrowse {
  uint8_t level = 0;          // 0 category list, 1 category list focused, 2 entity list
  uint8_t cat = 0;            // index into the VISIBLE categories
  uint8_t kind = HC_LIGHTS;   // HaCat being shown at level 2
  uint8_t sel = 0, n = 0;
  bool more = false, loading = false;
  char err[20] = "";
  uint32_t at = 0;
  HaEnt e[MAX_HA_ENT];
};

static const int MAX_MACROS = 6;
struct Macro {
  char label[14] = "";
  char url[128] = "";
  char method[6] = "GET";
  char body[128] = "";
};
struct Macros {
  Macro m[MAX_MACROS];
  uint8_t n = 0, sel = 0;
  bool focus = false;
  uint32_t holdStart = 0;     // confirm-hold progress, 0 = not holding
  uint32_t firedAt = 0;
  bool firedOk = true;
  int lastCode = 0;
};

static const int MAX_PAGES = 8;
struct PageMsg {
  char text[161] = "";
  char from[12] = "";
  char when[6] = "";          // HH:MM
  bool unread = false;
};
struct Pager {
  PageMsg m[MAX_PAGES];       // m[0] newest
  uint8_t n = 0, sel = 0, scroll = 0;
  uint8_t unread = 0;
};

// DOORBELL: a small alert log fed by UniFi Protect's Alarm Manager webhook (POST /api/doorbell).
static const int MAX_DOOR = 6;
struct DoorEvent {
  char cam[24] = "";          // camera / source, e.g. "FRONT DOOR"
  char what[24] = "";         // event, e.g. "PERSON", "MOTION", "RING"
  char when[6] = "";          // HH:MM
  bool unread = false;
};
struct Doorbell {
  DoorEvent e[MAX_DOOR];      // e[0] newest
  uint8_t n = 0, sel = 0;
  uint8_t unread = 0;
};

// MQTT ticker: latest messages from the house broker's subscribed topics.
static const int MAX_MQTT = 8;
struct MqttMsg {
  char topic[40] = "";
  char text[64] = "";
  char when[6] = "";          // HH:MM
};
struct MqttState {
  MqttMsg m[MAX_MQTT];        // m[0] newest
  uint8_t n = 0, sel = 0;
  bool connected = false;
  char err[24] = "";          // last connect problem, shown when not connected
};


static const int MAX_NETS = 24;
struct Net {
  char ssid[33] = "";
  int8_t rssi = -100;
  uint8_t ch = 0;
  bool open = false;
  bool ent = false;           // logs in with a username (enterprise)
};
struct WifiScan {
  uint32_t at = 0;
  bool scanning = false;
  Net n[MAX_NETS];
  uint8_t count = 0, sel = 0;
  bool detail = false;
  uint8_t chanLoad[14] = {0}; // index 1..13, summed signal weight
  uint8_t chanCount[14] = {0};
};

static const int MAX_BLE = 24;
struct BleDev {
  char name[20] = "";
  char mac[18] = "";
  int8_t rssi = -100;
};
struct BleScan {
  uint32_t at = 0;
  bool scanning = false;
  BleDev d[MAX_BLE];
  uint8_t count = 0, sel = 0;
  uint16_t total = 0;
};

static const int MAX_IR = 12;
struct IrCode {
  char name[14] = "";
  uint16_t len = 0;           // number of raw durations stored on flash
};
struct Ir {
  IrCode c[MAX_IR];
  uint8_t n = 0, sel = 0;     // sel == n means the "+ LEARN" row
  bool focus = false;
  uint8_t mode = 0;           // 0 idle, 1 listening, 2 learned-ok, 3 sent, 4 learn-failed
  uint32_t modeAt = 0;
  uint16_t lastLen = 0;
};

struct TvOff {
  bool running = false;
  uint16_t idx = 0, total = 0;
  char brand[14] = "";
  uint32_t doneAt = 0;
};

// ---------------------------------------------------------------- tools
struct Countdown {
  uint8_t preset = 1;                 // index into presets
  uint32_t totalMs = 5 * 60000UL;
  uint32_t remainMs = 5 * 60000UL;
  bool running = false;
  bool ringing = false;
  uint32_t lastTick = 0;
};
static const uint16_t TIMER_PRESETS_MIN[] = {1, 5, 10, 15, 30, 60};
static const int TIMER_PRESET_N = 6;

struct Pomo {
  uint8_t phase = 0;                  // 0 idle, 1 focus, 2 break
  uint8_t round = 0;                  // completed focus rounds
  uint32_t remainMs = 25 * 60000UL;
  uint32_t totalMs = 25 * 60000UL;
  bool running = false;
  bool ringing = false;
  uint32_t lastTick = 0;
  uint8_t focusMin = 25, breakMin = 5;
};

struct Level {
  float ax = 0, ay = 0, az = 1;       // filtered g
  float pitch = 0, roll = 0;          // degrees
};

struct Oracle {
  uint8_t mode = 0;                   // 0 = 8-ball, 1 = d6, 2 = d20, 3 = coin
  int value = 0;
  char line1[18] = "SHAKE ME";
  char line2[18] = "";
  uint32_t rolledAt = 0;
  bool rolling = false;
};

// ---------------------------------------------------------------- games
struct GameInput {
  float tiltX = 0, tiltY = 0;         // -1..1, screen right / screen down positive
  bool front = false, frontClick = false, sideClick = false;
};

struct Runner {                       // NEON RUN - tilt dodger
  float x = 120;
  struct Ob { float x, y, w; bool live; } ob[10];
  float speed = 1.6f;
  uint32_t score = 0, best = 0;
  bool dead = true, started = false;
  uint32_t frame = 0;
};

struct Snake {
  static const int GW = 20, GH = 9, CELL = 12;
  int8_t bx[180], by[180];
  int len = 3;
  int8_t dx = 1, dy = 0;
  int8_t fx = 10, fy = 4;
  uint32_t score = 0, best = 0;
  bool dead = true, started = false;
  uint32_t lastStep = 0;
};

struct Reflex {
  uint8_t phase = 0;                  // 0 ready, 1 waiting, 2 GO, 3 result, 4 too-soon
  uint32_t goAt = 0, shownAt = 0;
  uint32_t lastMs = 0, bestMs = 0;
};

struct Games {
  uint8_t sel = 0;                    // menu selection
  int8_t active = -1;                 // -1 menu, 0 runner, 1 snake, 2 reflex
  Runner run;
  Snake snake;
  Reflex reflex;
};

enum Mood : uint8_t {
  MD_CHILL, MD_HAPPY, MD_SLEEPY, MD_DRAINED, MD_ALERT, MD_LONELY, MD_HUNGRY, MD_HANGRY, MD_BORED,
  MD_IRKED, MD_ANGRY, MD_SULKING, MD_DIZZY, MD_EATING, MD_HYPED, MD_N
};
enum PetAct : uint8_t { PA_NONE, PA_EAT, PA_PLAY, PA_DIZZY, PA_FLINCH };

struct Daemon {
  uint8_t mood = MD_CHILL;
  char say[64] = "";                  // a reaction; wins over the small talk while it is fresh
  uint32_t sayAt = 0;
  uint16_t sayMs = 3500;
  char idle[64] = "";                 // rotating small talk
  uint32_t idleAt = 0;
  char note[64] = "";                 // something that happened while another screen was up
  uint32_t noteAt = 0;
  uint32_t pokes = 0, fed = 0, played = 0;
  uint8_t food = 70, fun = 60;        // 0..100. Empty only makes him complain - he cannot die.
  uint8_t rage = 0;                   // 0..100, cools off by itself
  uint32_t coolAt = 0;
  bool sulking = false;               // rage hit the top: back turned, ignores pokes
  uint32_t sulkAt = 0;
  uint8_t act = PA_NONE;              // short animation in progress
  uint32_t actAt = 0;
  uint32_t statEpoch = 0;             // wall-clock second food/fun were last aged (saved, so time off counts)
  uint32_t ageAt = 0;                 // same, in millis, for when the clock is not set
  uint8_t ageN = 0;                   // aging steps taken (food drops every 3rd, fun every 2nd)
  uint32_t bornEpoch = 0;
  uint32_t seenEpoch = 0;             // wall-clock second the owner last had the display on (saved)
  uint32_t saveEpoch = 0;             // last time the clock-based fields were flagged for saving
  uint32_t visitAt = 0;               // last time his screen was being looked at
  bool visited = false;
  bool open = false, greeted = false; // his screen is up + lit; he has reacted to this visit
  uint32_t openAt = 0;
  float lookX = 0, lookY = 0;         // where the eyes point (tilt), -1..1
};

// ---------------------------------------------------------------- system
struct Sys {
  int battPct = 100;
  int battMv = 4100;
  bool usb = false, charging = false;
  int vbusMv = 0;
  int8_t pwrSrc = -1;                 // power chip: 0 USB in, 1 USB in+out, 2 battery, 3/-1 unknown
  bool wifi = false, portal = false;
  bool wifiSet = false;               // Wi-Fi credentials exist, so being offline is worth a comment
  bool lit = true;                    // display is on (somebody may be looking)
  int rssi = -60;
  char ip[16] = "0.0.0.0";
  char ssid[33] = "";
  char host[16] = "shiv";
  bool timeValid = false;
  uint32_t heapKb = 0, psramKb = 0;
  uint32_t heapIntKb = 0, heapMinKb = 0, heapBigKb = 0;   // internal RAM: free now, lowest ever, largest block
  uint32_t loopHwm = 0, netHwm = 0;   // bytes of stack never touched (UI task / net task)
  char resetWhy[12] = "";             // why the last boot happened (esp_reset_reason)
  uint32_t uptimeS = 0;
  char version[12] = "1.0.0";
  uint8_t sel = 0;
  bool focus = false;
  uint8_t calib = 0;                  // 0 off, 1 "lay flat", 2 "stand up", 3 done
};

// ---------------------------------------------------------------- joining Wi-Fi (boot-time progress + why it failed)
static const int WIFI_SLOTS = 5;                  // saved networks, newest first
static const uint32_t JOIN_TRY_MS = 20000;        // how long one network gets
static const uint32_t JOIN_HOLD_MS = 3000;        // a failure stays on the JOINING screen this long, with its reason
enum JoinRes : uint8_t { JR_NONE, JR_FAR, JR_TRYING, JR_OK, JR_PASS, JR_LOGIN, JR_NEEDUSER, JR_NOIP, JR_SECURITY, JR_REFUSED, JR_NOREPLY, JR_N };
struct WifiJoin {
  bool show = false;                  // the JOINING WIFI screen is up (first attempt after power-up only)
  bool scanning = false;
  int8_t cur = -1;                    // slot being tried
  uint8_t tryNo = 0, tries = 0;       // "TRY 2 OF 3"
  char ssid[WIFI_SLOTS][33] = {};
  uint8_t res[WIFI_SLOTS] = {0};      // JoinRes per saved slot
  uint8_t code[WIFI_SLOTS] = {0};     // the radio's own reason number, for the record
  uint32_t tryAt = 0, doneAt = 0;
};
// Short enough for the stick's screen; the hotspot page spells it out.
inline const char* joinText(uint8_t res) {
  static const char* const T[JR_N] = {"NOT TRIED", "NOT IN RANGE", "TRYING", "JOINED", "BAD PASSWORD", "LOGIN REFUSED",
                                      "NEEDS USERNAME", "NO ADDRESS", "BAD SECURITY", "REFUSED", "NO REPLY"};
  return T[res < JR_N ? res : 0];
}

static const int MAX_FAV_SCREENS = 12;
struct Settings {
  uint8_t theme = 0;                  // 0 neon, 1 phosphor, 2 amber
  uint8_t volume = 60;                // 0..255
  bool mute = false;
  uint8_t brightness = 160;
  uint8_t flip = 0;                   // 0 auto, 1 normal, 2 flipped
  uint16_t dimS = 20, offS = 45;
  uint16_t sleepMin = 5;              // light sleep after screen-off on battery, 0 = never
  uint8_t clockFace = 0;
  uint32_t screenMask = 0xFFFFFFFF;   // enabled screens
  uint8_t piTarget = 5;               // which Pi-hole the on/off button drives: 5 (old) or 6 (new)
  uint8_t piOffHours = 10;            // disable duration; Pi-hole itself re-enables after this
  uint32_t piOffUntil = 0;            // epoch seconds the target comes back on (0 = unknown / on)
  uint8_t cfgVer = 0;                 // config schema version
  uint8_t fav[MAX_FAV_SCREENS] = {};  // SIDE-click loop, screen ids; empty = every enabled screen
  uint8_t favN = 0;
  // Microphone features (mic shares I2S with the speaker, so it only samples when silent)
  bool micOn = false;                 // master: the mic may run (every feature below needs it)
  uint8_t micSens = 5;                // 1..9 loudness sensitivity (higher = quieter sounds count)
  bool spectrumIdle = false;          // the idle clock shows a live audio spectrum strip
  bool petHears = false;              // the daemon reacts to voice, music and a loud room
  bool otaOn = true;                  // self-update: check otaUrl for a newer build and offer it (install needs a FRONT press)
};

// Pure version compare for self-update. Parses up to three dotted integers; trailing
// text and missing parts count as 0. newer("2.2.0","2.1.0") == true, equal == false.
namespace otautil {
inline void parse3(const char* s, long v[3]) {
  v[0] = v[1] = v[2] = 0;
  if (!s) return;
  for (int i = 0; i < 3 && *s; i++) {
    while (*s == ' ' || *s == 'v' || *s == 'V') s++;
    long n = 0; bool any = false;
    while (*s >= '0' && *s <= '9') { n = n * 10 + (*s - '0'); s++; any = true; }
    v[i] = any ? n : 0;
    while (*s && *s != '.') s++;        // skip to the next dot (ignores -rc1 etc.)
    if (*s == '.') s++;
  }
}
inline bool newer(const char* cand, const char* cur) {
  long a[3], b[3];
  parse3(cand, a); parse3(cur, b);
  for (int i = 0; i < 3; i++) { if (a[i] != b[i]) return a[i] > b[i]; }
  return false;
}
}  // namespace otautil

// Self-update (OTA pull): the net task checks otaUrl for a newer build and raises an
// overlay card; the owner presses FRONT to install. State only -- no behaviour here.
struct Ota {
  bool avail = false;         // a newer build than FW_VERSION was found at the manifest
  bool want = false;          // FRONT pressed on the offer: the net task should install now
  bool installing = false;    // download + flash in progress (swallows input, DO NOT UNPLUG)
  bool done = false;          // flashed OK, reboot pending
  uint8_t prog = 0;           // 0..100 install progress
  uint32_t snoozeUntil = 0;   // millis the offer is hidden until (SIDE = later)
  uint32_t at = 0;            // millis of the last state change (for the reboot delay)
  char ver[12] = "";          // offered version, e.g. "2.2.0"
  char url[160] = "";         // firmware .bin URL from the manifest
  uint32_t size = 0;          // .bin bytes for the progress bar (0 = unknown)
  char err[28] = "";          // last failure reason ("" = none)
};

struct State {
  uint32_t now = 0;                   // millis
  struct tm lt = {};                  // local time
  uint32_t epoch = 0;
  ScreenId screen = SC_CLOCK;
  bool launcher = false;
  uint8_t launcherSel = 0;
  int8_t launcherGrp = -1;            // -1 = the group list, else the open group
  char toast[22] = "";
  uint32_t toastAt = 0;

  Settings set;
  Sys sys;
  Weather wx;
  Pihole pi;
  Hosts hosts;
  Shop shop;
  Jobs jobs;
  Home home;
  HaBrowse hab;
  Macros macros;
  Pager pager;
  Doorbell door;
  Mic mic;
  MqttState mqtt;
  Ota ota;                            // self-update offer / progress (see struct Ota)
  WifiScan wifi;
  BleScan ble;
  Ir ir;
  TvOff tv;
  Countdown cd;
  Pomo pomo;
  Level lvl;
  Oracle orc;
  Games games;
  Daemon dmn;
  WifiJoin join;
};

inline bool screenEnabled(const State& s, int id) {
  if (id == SC_CLOCK || id == SC_SYSTEM) return true;
  return (s.set.screenMask >> id) & 1;
}

// Self-update overlay phase: what, if anything, the update card shows right now.
// 0 none, 1 offer (confirm), 2 installing, 3 done (restarting), 4 failed.
namespace otaui {
enum Phase : uint8_t { NONE, OFFER, INSTALLING, DONE, FAILED };
inline uint8_t phase(const State& s, uint32_t now) {
  const Ota& o = s.ota;
  if (o.installing) return INSTALLING;
  if (o.done) return DONE;
  if (o.err[0] && (uint32_t)(now - o.at) < 4000) return FAILED;
  if (o.avail && o.ver[0] && (o.snoozeUntil == 0 || (int32_t)(now - o.snoozeUntil) >= 0)) return OFFER;
  return NONE;
}
}  // namespace otaui

// FAVORITES only appears in the HA browser when some exist.
inline int haCatCount(const State& s) { return s.home.n ? HC_N : HC_N - 1; }
inline uint8_t haCatAt(const State& s, int idx) { return (uint8_t)(s.home.n ? idx : idx + 1); }

// ---------------------------------------------------------------- saved Wi-Fi networks (pure, PC-tested)
namespace wifilist {
// Saved networks stay packed at the front: slot 0 is the newest, empty slots are at the end, no name twice.
inline void compact(char ssid[][33], char pass[][65], char user[][65], int n) {
  int k = 0;
  for (int i = 0; i < n; i++) {
    if (!ssid[i][0]) continue;
    bool dup = false;
    for (int j = 0; j < k; j++) if (!strcmp(ssid[j], ssid[i])) dup = true;
    if (dup) continue;
    if (k != i) { scopy(ssid[k], ssid[i]); scopy(pass[k], pass[i]); scopy(user[k], user[i]); }
    k++;
  }
  for (; k < n; k++) { ssid[k][0] = 0; pass[k][0] = 0; user[k][0] = 0; }
}
// The panel sends a password only when one was typed. The saved one (and the username) is looked up by network
// NAME, never by row: the list reorders itself, so "row 2" in an old page or backup may be a different network by now.
inline const char* savedOf(char ssid[][33], char field[][65], int n, const char* name) {
  for (int i = 0; i < n; i++) if (name[0] && !strcmp(ssid[i], name)) return field[i];
  return "";
}
// A network joined from the setup page goes in front; the others move down one and the oldest of a full list drops off.
inline void remember(char ssid[][33], char pass[][65], char user[][65], int n, const char* id, const char* pw, const char* usr) {
  char nid[33], npw[65], nus[65];
  scopy(nid, id); scopy(npw, pw); scopy(nus, usr);   // the arguments may point into the slots being shuffled
  compact(ssid, pass, user, n);
  int at = n - 1;
  for (int i = 0; i < n; i++) if (!ssid[i][0] || !strcmp(ssid[i], nid)) { at = i; break; }
  for (int i = at; i > 0; i--) { scopy(ssid[i], ssid[i - 1]); scopy(pass[i], pass[i - 1]); scopy(user[i], user[i - 1]); }
  scopy(ssid[0], nid); scopy(pass[0], npw); scopy(user[0], nus);
}
}  // namespace wifilist

// ---------------------------------------------------------------- SIDE button navigation (pure, PC-tested)
// SIDE double-click = go back. The first click has already moved one forward (no waiting to see
// whether a second click follows), so the second click moves two back. Any further quick clicks keep
// going back one at a time, so a fast burst is always "backwards" and slow clicks are always "forwards".
namespace nav {
static const uint32_t DOUBLE_MS = 300;
struct Side { uint32_t at = 0; uint8_t run = 0; };   // run: 0 idle, 1 one click taken, 2 in a backwards burst
inline int sideStep(Side& k, uint32_t now) {
  bool quick = k.run && ago(now, k.at) < DOUBLE_MS;
  int st = !quick ? 1 : k.run == 1 ? -2 : -1;
  k.run = quick ? 2 : 1;
  k.at = now;
  return st;
}
inline int wrapStep(int sel, int step, int n) { return n > 0 ? ((sel + step) % n + n) % n : 0; }
inline bool screenUsable(const State& s, int id) {
  return screenEnabled(s, id);
}
inline bool grpVisible(const State& s, int g) {
  for (int i = 0; i < SC_COUNT; i++) if (SCREEN_GROUP[i] == g && screenEnabled(s, i)) return true;
  return false;
}
inline int grpCount(const State& s) { int n = 0; for (int g = 0; g < SG_N; g++) if (grpVisible(s, g)) n++; return n; }
inline int grpAt(const State& s, int idx) { for (int g = 0; g < SG_N; g++) if (grpVisible(s, g)) { if (idx == 0) return g; idx--; } return SG_HOME; }
inline int grpIndex(const State& s, int g) { int idx = 0; for (int h = 0; h < SG_N; h++) if (grpVisible(s, h)) { if (h == g) return idx; idx++; } return 0; }
inline int grpScreens(const State& s, int g) {
  int n = 0;
  for (int i = 0; i < SC_COUNT; i++) { int sc = SCREEN_ORDER[i]; if (SCREEN_GROUP[sc] == g && screenUsable(s, sc)) n++; }
  return n;
}
inline uint8_t grpScreenAt(const State& s, int g, int idx) {
  for (int i = 0; i < SC_COUNT; i++) { int sc = SCREEN_ORDER[i]; if (SCREEN_GROUP[sc] == g && screenUsable(s, sc)) { if (idx == 0) return (uint8_t)sc; idx--; } }
  return SC_CLOCK;
}
inline int grpBadge(const State& s, int g) {
  int n = 0;
  if (SCREEN_GROUP[SC_PAGER] == g) n += s.pager.unread;
  if (SCREEN_GROUP[SC_DOORBELL] == g) n += s.door.unread;
  return n;
}
// The enabled screen `step` places from the current one in SCREEN_ORDER.
inline uint8_t stepOrder(const State& s, int step) {
  int pos = 0;
  for (int i = 0; i < SC_COUNT; i++) if (SCREEN_ORDER[i] == s.screen) pos = i;
  int dir = step < 0 ? -1 : 1;
  for (int todo = abs(step); todo > 0; todo--)
    for (int k = 0; k < SC_COUNT; k++) {
      pos = (pos + dir + SC_COUNT) % SC_COUNT;
      if (screenUsable(s, SCREEN_ORDER[pos])) break;
    }
  return SCREEN_ORDER[pos];
}
// The favorites loop, usable screens only, no duplicates. Returns how many.
inline int favList(const State& s, uint8_t* out) {
  int n = 0;
  for (int i = 0; i < s.set.favN && i < MAX_FAV_SCREENS; i++) {
    uint8_t id = s.set.fav[i];
    if (id >= SC_COUNT || !screenUsable(s, id)) continue;
    bool dup = false;
    for (int k = 0; k < n; k++) if (out[k] == id) dup = true;
    if (!dup) out[n++] = id;
  }
  return n;
}
// SIDE click: walk the favorites; with none set, walk every enabled screen.
inline uint8_t stepScreen(const State& s, int step) {
  uint8_t f[MAX_FAV_SCREENS];
  int n = favList(s, f);
  if (!n) return stepOrder(s, step);
  int pos = step > 0 ? -1 : n;       // from outside the loop: +1 lands on the first stop, back lands near the end
  for (int i = 0; i < n; i++) if (f[i] == s.screen) pos = i;
  return f[wrapStep(pos, step, n)];
}
}  // namespace nav

inline void setToast(State& s, const char* t) {
  scopy(s.toast, t);
  s.toastAt = s.now;
}

inline uint32_t petAgeDays(const State& s) { return (s.sys.timeValid && s.dmn.bornEpoch && s.epoch >= s.dmn.bornEpoch) ? (s.epoch - s.dmn.bornEpoch) / 86400UL : 0; }

}  // namespace shiv
