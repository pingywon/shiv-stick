// SHIV - the daemon's brain: hunger, boredom, temper, small talk, reactions to what the stick sees.
// Pure C++ (no Arduino), so tools/test_pet.cpp and the render harness run the real thing.
// He cannot die. An empty belly only makes him loud.
#pragma once
#include "state.h"

namespace shiv {
namespace pet {

static const char* const MOOD_NAMES[MD_N] = {
  "CHILL", "HAPPY", "SLEEPY", "DRAINED", "ALERT", "LONELY", "HUNGRY", "HANGRY", "BORED",
  "IRKED", "ANGRY", "SULKING", "DIZZY", "EATING", "HYPED"
};

// What the speaker should do about it (app.h turns these into notes).
enum Cue : uint8_t { CUE_NONE, CUE_POKE, CUE_GRUMBLE, CUE_GROWL, CUE_RAGE, CUE_NOM, CUE_REFUSE, CUE_PLAY, CUE_DIZZY, CUE_FORGIVE, CUE_EVENT };

static const uint32_t ACT_MS[] = {0, 1800, 2400, 2600, 300};   // by PetAct
static const uint32_t SULK_MS = 30000UL;
static const uint32_t COOL_MS = 700;          // one rage point cools off this often
static const uint32_t IDLE_MS = 6500;         // small talk changes this often
static const uint32_t AGE_STEP_S = 180;       // food -1 every 3rd step (9 min), fun -1 every 2nd (6 min)
static const uint32_t SETTLE_MS = 500;        // his screen must stay up this long before he reacts (SIDE double-click passes through)
static const uint32_t AWAY_S = 7200;          // display dark this long = "you left me"
static const uint32_t SAVE_EVERY_S = 1800;    // keeps the saved clock fields fresh enough that a reboot is not mistaken for an absence
static const uint32_t WIFI_GRACE_MS = 15000;  // boot, wake-ups and blips are not worth a remark
static const uint8_t RAGE_IRKED = 30, RAGE_ANGRY = 60, RAGE_QUIT = 90;

static const int SAY_N = 6;
static const char* const MOOD_SAY[MD_N][SAY_N] = {
  {"ALL QUIET.", "JUST VIBING.", "IDLE LOOP. NICE.", "NOTHING ON FIRE.", "NOMINAL. BORINGLY SO.", "I LIVE IN A STICK. IT'S FINE."},
  {"LIFE IS GOOD.", "HEH HEH.", "FULL OF VOLTS!", "BEST STICK EVER.", "YOU'RE OK. FOR A HUMAN.", "GOOD SIGNAL. GOOD DAY."},
  {"zzz..", "LATE. ISN'T IT.", "DIM THE LIGHTS.", "5 MORE MIN..", "GO TO BED.", "EVEN DAEMONS SLEEP."},
  {"FEED ME VOLTS.", "LOW POWER..", "NEED A CABLE.", "FADING..", "PLUG ME IN. PLEASE.", "SEEING SPOTS."},
  {"YOU GOT MAIL!", "PAGE WAITING!", "LOOK AT PAGER.", "PING! PING!", "UNREAD. UNREAD!", "SOMEONE WANTS YOU."},
  {"NO SIGNAL..", "WHERE IS HOME?", "OFF THE GRID.", "LOST IN NOISE.", "HELLO? ANYONE?", "JUST YOU AND ME."},
  {"I COULD EAT.", "SNACK TIME?", "HOLD FRONT TO FEED.", "TUMMY RUMBLE.", "GOT ANY BYTES?", "FEED ME. SOON."},
  {"FEED ME. NOW.", "STARVING HERE!", "CAN'T DIE. CAN COMPLAIN.", "HOLD. FRONT. BUTTON.", "SO. HUNGRY.", "I'D EAT A FLOPPY."},
  {"BORED.", "SO BORED.", "PRESS BOTH TO PLAY.", "ENTERTAIN ME.", "COUNTING PIXELS. AGAIN.", "SHAKE ME. I DARE YOU."},
  {"QUIT IT.", "I BITE.", "WATCH IT.", "LAST WARNING.", "HANDS OFF.", "NOT. FUNNY."},
  {"BACK OFF!", "I WILL DELETE YOU.", "RM -RF YOU.", "GRRRR!", "TOUCH ME AGAIN. I DARE YOU.", "SEEING RED!"},
  {"...", "NOT TALKING TO YOU.", "GO AWAY.", "STILL MAD.", "HMPH.", "BRIBES ACCEPTED."},
  {"WHOA. WHOAAA.", "ROOM IS SPINNING.", "GONNA HURL BITS.", "WHICH WAY IS UP?", "SEEING DOUBLE.", "WHEEE. UGH."},
  {"NOM NOM NOM.", "TASTY BYTES!", "CRUNCHY PACKETS.", "MMM. COOKIES.", "OM NOM.", "MORE. MORE!"},
  {"WHEEE!", "AGAIN! AGAIN!", "WATCH THIS!", "BEST DAY EVER!", "ZOOMIES!", "I CAN FLY!"},
};

static const char* const POKE_SAY[] = {
  "HEY!", "THAT TICKLES.", "WHAT?", "I'M AWAKE!", "BOOP.", "ROOT ME NOT.", "AGAIN? REALLY?", "01001000 01001001",
  "DO I KNOW YOU?", "PERSONAL SPACE.", "OK OK OK.", "HEH. DO IT AGAIN.", "POKE BACK!", "THAT'S MY EYE.",
};
static const int POKE_N = sizeof(POKE_SAY) / sizeof(POKE_SAY[0]);

// One-off lines. Kept in a table so the render harness can prove every one fits the bubble.
enum Line : uint8_t {
  L_RAGEQUIT, L_STUFFED, L_FORGIVE, L_CALM, L_SHAKE_MAD, L_POKE_SULK, L_NO_PLAY_MAD, L_NO_PLAY_FOOD, L_NO_PLAY_SULK,
  L_USB_IN, L_USB_OUT, L_WIFI_LOST, L_WIFI_BACK, L_PAGE, L_PI_OFF, L_PI_ON, L_JOB_DONE, L_JOB_HELD, L_JOB_FAIL, L_ORDER,
  L_BATT_LOW, L_PECKISH, L_STARVING, L_EMPTY, L_BORED, L_REMEMBERED, L_LOOK_WHO, L_STILL_UP, L_MORNING, L_AFTERNOON,
  L_EVENING, L_HI, L_N
};
static const char* const LINES[L_N] = {
  "THAT'S IT. WE'RE DONE.", "NO MORE. I'LL POP.", "...FINE. SNACKS FIX THINGS.", "FINE. I'M OVER IT. MOSTLY.",
  "STOP SHAKING ME!", "POKING WON'T HELP.", "NOT IN THE MOOD.", "TOO HUNGRY TO PLAY.", "NO. STILL MAD.",
  "OOH. FRESH VOLTS.", "HEY! MY CABLE!", "SIGNAL'S GONE!", "BACK ONLINE. MISSED IT.", "PAGE FOR YOU! GO LOOK.",
  "PI-HOLE IS OFF. ADS ARE LOOSE!", "SHIELDS UP. ADS BLOCKED.", "CLAUDE FINISHED A JOB.", "A JOB IS HELD. IT NEEDS YOU.",
  "A CLAUDE JOB FAILED. OOF.", "KA-CHING! NEW ORDER.", "BATTERY LOW. JUST SAYING.", "I COULD EAT.",
  "HOLD FRONT TO FEED ME.", "RUNNING ON EMPTY.", "BORED. PRESS BOTH.",
  "OH. YOU CAME BACK.", "LOOK WHO IT IS.", "STILL UP?", "MORNING.", "HEY YOU.", "EVENING.", "HI.",
};

// ---------------------------------------------------------------- talking
inline void sayFor(Daemon& d, uint32_t now, const char* t, uint16_t ms = 3500) {
  scopy(d.say, t);
  d.sayAt = now;
  d.sayMs = ms;
}

inline bool watching(const State& s) { return s.screen == SC_DAEMON && !s.launcher && s.sys.lit; }

// A reaction is spoken if somebody is looking at him, otherwise kept for the next visit.
inline void react(State& s, const char* t) {
  Daemon& d = s.dmn;
  if (watching(s) && d.greeted) sayFor(d, s.now, t);
  else { scopy(d.note, t); d.noteAt = s.now; }
}

inline uint8_t moodOf(const State& s) {
  const Daemon& d = s.dmn;
  int hr = s.lt.tm_hour;
  if (d.act == PA_EAT) return MD_EATING;
  if (d.act == PA_PLAY) return MD_HYPED;
  if (d.act == PA_DIZZY) return MD_DIZZY;
  if (d.sulking) return MD_SULKING;
  if (d.rage >= RAGE_ANGRY) return MD_ANGRY;
  if (d.rage >= RAGE_IRKED) return MD_IRKED;
  if (s.pager.unread) return MD_ALERT;
  if (d.food < 15) return MD_HANGRY;
  if (s.sys.battPct <= 15 && !s.sys.usb) return MD_DRAINED;
  if (d.food < 35) return MD_HUNGRY;
  if (!s.sys.wifi && s.sys.wifiSet) return MD_LONELY;
  if (s.sys.timeValid && (hr >= 23 || hr < 6)) return MD_SLEEPY;
  if (d.fun < 25) return MD_BORED;
  if (s.sys.usb || s.sys.battPct > 80 || (d.food > 60 && d.fun > 60)) return MD_HAPPY;
  return MD_CHILL;
}

// Small talk about what the stick knows. Returns false when that topic has nothing to say right now.
enum Chat : uint8_t { CH_TIME, CH_WX, CH_PIHOLE, CH_HOSTS, CH_JOBS, CH_SHOP, CH_BATT, CH_UPTIME, CH_POKES, CH_AGE, CH_SIGNAL, CH_FED, CH_N };
inline bool chatLine(const State& s, int kind, char* out, size_t n) {
  const Daemon& d = s.dmn;
  switch (kind) {
    case CH_TIME:
      if (!s.sys.timeValid) return false;
      snprintf(out, n, (s.lt.tm_hour >= 23 || s.lt.tm_hour < 6) ? "IT'S %02d:%02d. GO TO BED." : "IT'S %02d:%02d. DO SOMETHING.", s.lt.tm_hour, s.lt.tm_min);
      return true;
    case CH_WX: {
      if (!s.wx.at || !(s.wx.tempF > -200 && s.wx.tempF < 200)) return false;
      int t = (int)lroundf(s.wx.tempF);
      snprintf(out, n, t < 35 ? "%dF OUT. BRRR." : t > 88 ? "%dF OUT. I'D MELT." : "%dF OUT THERE. I'M STAYING IN.", t);
      return true;
    }
    case CH_PIHOLE:
      if (!s.pi.at) return false;
      if (s.pi.enabled) snprintf(out, n, "PI-HOLE ATE %lu ADS. YUM.", (unsigned long)s.pi.blocked);
      else snprintf(out, n, "PI-HOLE IS OFF. ADS ROAM FREE.");
      return true;
    case CH_HOSTS: {
      if (!s.hosts.at || !s.hosts.n) return false;
      int down = -1;
      for (int i = 0; i < s.hosts.n; i++) if (s.hosts.h[i].up == 0) { down = i; break; }
      if (down < 0) snprintf(out, n, "ALL %d HOSTS UP. BORING.", s.hosts.n);
      else snprintf(out, n, "%s IS DOWN. AGAIN?", s.hosts.h[down].name);
      return true;
    }
    case CH_JOBS:
      if (!s.jobs.at) return false;
      if (s.jobs.running > 0) snprintf(out, n, "CLAUDE IS BUSY. %d RUNNING.", s.jobs.running > 99 ? 99 : s.jobs.running);
      else if (s.jobs.held > 0) snprintf(out, n, "%d HELD. WAITING ON YOU.", s.jobs.held > 99 ? 99 : s.jobs.held);
      else snprintf(out, n, "NO CLAUDE JOBS. QUIET.");
      return true;
    case CH_SHOP:
      if (!s.shop.at) return false;
      if (s.shop.orders > 0) snprintf(out, n, "%d ORDERS TODAY.", s.shop.orders > 9999 ? 9999 : s.shop.orders);
      else snprintf(out, n, "NO ORDERS YET. PATIENCE.");
      return true;
    case CH_BATT:
      snprintf(out, n, s.sys.battPct < 40 ? "%d%% LEFT. JUST SAYING." : "%d%% CHARGE. COMFY.", s.sys.battPct);
      return true;
    case CH_UPTIME:
      snprintf(out, n, "UP %luH %02luM. NO CRASHES.", (unsigned long)(s.sys.uptimeS / 3600 > 9999 ? 9999 : s.sys.uptimeS / 3600), (unsigned long)(s.sys.uptimeS / 60 % 60));
      return true;
    case CH_POKES:
      if (!d.pokes) return false;
      snprintf(out, n, "POKED %lu TIMES.", (unsigned long)(d.pokes > 999999UL ? 999999UL : d.pokes));
      return true;
    case CH_AGE: {
      if (!s.sys.timeValid || !d.bornEpoch || s.epoch < d.bornEpoch) return false;
      unsigned long days = (s.epoch - d.bornEpoch) / 86400UL;
      if (days == 0) snprintf(out, n, "BORN TODAY. HI.");
      else snprintf(out, n, "I'M %lu DAY%s OLD.", days > 99999UL ? 99999UL : days, days == 1 ? "" : "S");
      return true;
    }
    case CH_SIGNAL:
      if (!s.sys.wifi || s.sys.rssi > -75) return false;
      snprintf(out, n, "SIGNAL'S THIN. %d DBM.", s.sys.rssi);
      return true;
    case CH_FED:
      if (!d.fed) return false;
      snprintf(out, n, "FED %lu TIMES. KEEP IT UP.", (unsigned long)(d.fed > 999999UL ? 999999UL : d.fed));
      return true;
    default: return false;
  }
}

inline void chatter(State& s, uint32_t& rng) {
  Daemon& d = s.dmn;
  char next[64] = "";
  bool chatty = d.mood == MD_CHILL || d.mood == MD_HAPPY || d.mood == MD_BORED || d.mood == MD_HUNGRY;
  if (chatty && (rnd(rng) & 1))
    for (int tries = 0; tries < 4 && !next[0]; tries++)
      if (!chatLine(s, rnd(rng) % CH_N, next, sizeof(next))) next[0] = 0;
  if (!next[0]) scopy(next, MOOD_SAY[d.mood % MD_N][rnd(rng) % SAY_N]);
  if (!strcmp(next, d.idle)) scopy(next, MOOD_SAY[d.mood % MD_N][rnd(rng) % SAY_N]);
  scopy(d.idle, next);
  d.idleAt = s.now;
}

// ---------------------------------------------------------------- temper
// Returns the cue for the temper he ends up in. Hitting the top makes him quit on you.
inline Cue bump(State& s, uint8_t gain, uint32_t& rng) {
  Daemon& d = s.dmn;
  if (!d.rage) d.coolAt = s.now;
  d.rage = (uint8_t)(d.rage + gain > 100 ? 100 : d.rage + gain);
  if (d.rage >= RAGE_QUIT) {
    d.sulking = true; d.sulkAt = s.now; d.act = PA_NONE;
    d.fun = d.fun > 10 ? d.fun - 10 : 0;
    sayFor(d, s.now, LINES[L_RAGEQUIT]);
    return CUE_RAGE;
  }
  if (d.rage >= RAGE_ANGRY) { sayFor(d, s.now, MOOD_SAY[MD_ANGRY][rnd(rng) % SAY_N]); return CUE_GROWL; }
  if (d.rage >= RAGE_IRKED) { sayFor(d, s.now, MOOD_SAY[MD_IRKED][rnd(rng) % SAY_N]); return CUE_GRUMBLE; }
  return CUE_NONE;
}

inline Cue poke(State& s, uint32_t& rng) {
  Daemon& d = s.dmn;
  d.pokes++;
  if (d.sulking) {                               // poking a sulk only makes it last longer
    uint32_t el = ago(s.now, d.sulkAt);
    d.sulkAt = s.now - (el > 5000 ? el - 5000 : 0);
    sayFor(d, s.now, (rnd(rng) % 3) ? MOOD_SAY[MD_SULKING][rnd(rng) % SAY_N] : LINES[L_POKE_SULK]);
    return CUE_REFUSE;
  }
  d.act = PA_FLINCH; d.actAt = s.now;
  bool shortFuse = d.food < 15 || d.mood == MD_SLEEPY;
  Cue c = bump(s, shortFuse ? 22 : 14, rng);
  if (c != CUE_NONE) return c;
  if (d.fun < 100) d.fun++;
  sayFor(d, s.now, POKE_SAY[rnd(rng) % POKE_N]);
  return CUE_POKE;
}

inline Cue feed(State& s, uint32_t& rng) {
  Daemon& d = s.dmn;
  if (d.act == PA_EAT) return CUE_NONE;
  bool stuffed = d.food >= 95;
  if (d.sulking) {                               // a snack is the only thing that ends a sulk early - even one he is too full for
    d.sulking = false; d.rage = 20; d.coolAt = s.now;
    if (!stuffed) { d.food = (uint8_t)(d.food + 30 > 100 ? 100 : d.food + 30); d.fed++; d.act = PA_EAT; d.actAt = s.now; }
    sayFor(d, s.now, LINES[L_FORGIVE]);
    return CUE_FORGIVE;
  }
  if (stuffed) {
    Cue c = bump(s, 8, rng);
    if (c == CUE_NONE) sayFor(d, s.now, LINES[L_STUFFED]);
    return c == CUE_RAGE ? c : CUE_REFUSE;
  }
  d.food = (uint8_t)(d.food + 30 > 100 ? 100 : d.food + 30);
  d.fed++;
  d.act = PA_EAT; d.actAt = s.now;
  d.rage = d.rage > 25 ? d.rage - 25 : 0;
  sayFor(d, s.now, MOOD_SAY[MD_EATING][rnd(rng) % SAY_N]);
  return CUE_NOM;
}

inline Cue play(State& s, uint32_t& rng) {
  Daemon& d = s.dmn;
  if (d.act == PA_PLAY) return CUE_NONE;
  if (d.sulking) { sayFor(d, s.now, LINES[L_NO_PLAY_SULK]); return CUE_REFUSE; }
  if (d.rage >= RAGE_ANGRY) { sayFor(d, s.now, LINES[L_NO_PLAY_MAD]); return CUE_REFUSE; }
  if (d.food < 10) { sayFor(d, s.now, LINES[L_NO_PLAY_FOOD]); return CUE_REFUSE; }
  d.fun = (uint8_t)(d.fun + 30 > 100 ? 100 : d.fun + 30);
  d.food = d.food > 4 ? d.food - 4 : 0;
  d.rage = d.rage > 15 ? d.rage - 15 : 0;
  d.played++;
  d.act = PA_PLAY; d.actAt = s.now;
  sayFor(d, s.now, MOOD_SAY[MD_HYPED][rnd(rng) % SAY_N]);
  return CUE_PLAY;
}

inline Cue shake(State& s, uint32_t& rng) {
  Daemon& d = s.dmn;
  if (d.sulking) return CUE_NONE;
  if (d.act == PA_DIZZY) {                       // shaken again before he recovered
    d.actAt = s.now;
    Cue c = bump(s, 25, rng);
    if (c != CUE_RAGE) sayFor(d, s.now, LINES[L_SHAKE_MAD]);
    return c == CUE_NONE ? CUE_GRUMBLE : c;
  }
  d.act = PA_DIZZY; d.actAt = s.now;
  if (d.fun < 96) d.fun += 4;
  sayFor(d, s.now, MOOD_SAY[MD_DIZZY][rnd(rng) % SAY_N]);
  return CUE_DIZZY;
}

// His screen was just opened.
inline Cue visit(State& s, uint32_t& rng) {
  Daemon& d = s.dmn;
  Cue c = CUE_NONE;
  bool calm = !d.sulking && d.rage < RAGE_IRKED;
  if (d.note[0] && ago(s.now, d.noteAt) < 1800000UL) { sayFor(d, s.now, d.note, 5000); c = CUE_EVENT; }
  else if (calm && d.visited && ago(s.now, d.visitAt) > 3600000UL) sayFor(d, s.now, LINES[(rnd(rng) & 1) ? L_REMEMBERED : L_LOOK_WHO], 5000);
  else if (calm && !d.visited) {
    int hr = s.lt.tm_hour;
    sayFor(d, s.now, LINES[!s.sys.timeValid ? L_HI : hr < 5 ? L_STILL_UP : hr < 12 ? L_MORNING : hr < 18 ? L_AFTERNOON : L_EVENING]);
  }
  d.note[0] = 0;
  d.visited = true;
  d.visitAt = s.now;
  return c;
}

// ---------------------------------------------------------------- noticing things
struct Snap {
  bool init = false;
  bool usb = false, wifi = false, piOn = true;
  bool wifiEver = false, wifiLostSaid = false;   // only a link that was up, and stayed down a while, is "lost"
  uint32_t wifiDownAt = 0;
  uint32_t piAt = 0, jobsAt = 0, shopAt = 0, hostsAt = 0;
  uint8_t unread = 0, food = 100, fun = 100;
  int8_t up[MAX_HOSTS] = {0};
  int done = 0, held = 0, failed = 0, orders = 0, batt = 100;
};
static Snap snap;

inline bool notice(State& s) {
  Snap& p = snap;
  const Daemon& d = s.dmn;
  const char* line = nullptr;
  char buf[64];
  if (p.init) {
    if (s.sys.usb != p.usb) line = LINES[s.sys.usb ? L_USB_IN : L_USB_OUT];
    if (s.sys.wifi != p.wifi && !s.sys.wifi) p.wifiDownAt = s.now;
    if (s.sys.wifi) {
      if (p.wifiLostSaid) line = LINES[L_WIFI_BACK];
      p.wifiLostSaid = false; p.wifiEver = true;
    } else if (s.sys.wifiSet && p.wifiEver && !p.wifiLostSaid && ago(s.now, p.wifiDownAt) >= WIFI_GRACE_MS) {
      line = LINES[L_WIFI_LOST]; p.wifiLostSaid = true;
    }
    if (!s.sys.usb && s.sys.battPct <= 20 && p.batt > 20) line = LINES[L_BATT_LOW];
    if (d.fun < 25 && p.fun >= 25) line = LINES[L_BORED];
    if (d.food < 35 && p.food >= 35) line = LINES[L_PECKISH];
    if (d.food < 15 && p.food >= 15) line = LINES[L_STARVING];
    if (d.food == 0 && p.food > 0) line = LINES[L_EMPTY];
    if (s.shop.at && p.shopAt && s.shop.orders > p.orders) line = LINES[L_ORDER];
    if (s.jobs.at && p.jobsAt) {
      if (s.jobs.done > p.done) line = LINES[L_JOB_DONE];
      if (s.jobs.held > p.held) line = LINES[L_JOB_HELD];
      if (s.jobs.failed > p.failed) line = LINES[L_JOB_FAIL];
    }
    if (s.hosts.at && p.hostsAt)
      for (int i = 0; i < s.hosts.n && i < MAX_HOSTS; i++) {
        if (s.hosts.h[i].up == 0 && p.up[i] == 1) { snprintf(buf, sizeof(buf), "%s WENT DARK!", s.hosts.h[i].name); line = buf; }
        else if (s.hosts.h[i].up == 1 && p.up[i] == 0) { snprintf(buf, sizeof(buf), "%s IS BACK.", s.hosts.h[i].name); line = buf; }
      }
    if (s.pi.at && p.piAt && s.pi.enabled != p.piOn) line = LINES[s.pi.enabled ? L_PI_ON : L_PI_OFF];
    if (s.pager.unread > p.unread) line = LINES[L_PAGE];
  }
  if (!p.init) { p.wifiEver = s.sys.wifi; p.wifiDownAt = s.now; }
  p.init = true;
  p.usb = s.sys.usb; p.wifi = s.sys.wifi; p.batt = s.sys.battPct; p.unread = s.pager.unread;
  p.food = d.food; p.fun = d.fun;
  p.piAt = s.pi.at; p.piOn = s.pi.enabled;
  p.jobsAt = s.jobs.at; p.done = s.jobs.done; p.held = s.jobs.held; p.failed = s.jobs.failed;
  p.shopAt = s.shop.at; p.orders = s.shop.orders;
  p.hostsAt = s.hosts.at;
  for (int i = 0; i < MAX_HOSTS; i++) p.up[i] = i < s.hosts.n ? s.hosts.h[i].up : -1;
  if (!line) return false;
  // his own needs already show as his mood, so they never replace a remark that is waiting to be heard
  bool minor = line == LINES[L_BORED] || line == LINES[L_PECKISH] || line == LINES[L_STARVING] || line == LINES[L_EMPTY];
  if (minor && (d.say[0] || (d.note[0] && !(watching(s) && d.greeted)))) return false;
  react(s, line);
  return true;
}

inline void age(Daemon& d, uint32_t steps) {
  if (steps > 600) steps = 600;
  for (uint32_t i = 0; i < steps; i++) {
    d.ageN = (uint8_t)((d.ageN + 1) % 6);
    if (d.ageN % 3 == 0 && d.food) d.food--;
    if (d.ageN % 2 == 0 && d.fun) d.fun--;
  }
}

// Every loop pass. `dirty` is set when something worth saving changed.
inline Cue tick(State& s, uint32_t& rng, bool& dirty) {
  Daemon& d = s.dmn;
  uint32_t now = s.now;
  Cue cue = CUE_NONE;

  // is somebody looking at him? Settled first, so anything noticed below is kept for the greeting instead of
  // being said to a screen that only just lit up
  if (!watching(s)) d.open = d.greeted = false;
  else if (!d.open) { d.open = true; d.greeted = false; d.openAt = now; }

  if (d.act && ago(now, d.actAt) >= ACT_MS[d.act]) d.act = PA_NONE;
  if (d.say[0] && ago(now, d.sayAt) >= d.sayMs) d.say[0] = 0;

  if (d.sulking) {
    if (ago(now, d.sulkAt) >= SULK_MS) { d.sulking = false; d.rage = 35; d.coolAt = now; sayFor(d, now, LINES[L_CALM]); }
  } else if (d.rage) {
    uint32_t n = ago(now, d.coolAt) / COOL_MS;
    if (n) { d.rage = n >= d.rage ? 0 : (uint8_t)(d.rage - n); d.coolAt += n * COOL_MS; }
  }

  // hunger + boredom run on the wall clock, so hours spent asleep or switched off count too
  if (s.sys.timeValid && s.epoch > 1600000000UL) {
    if (!d.bornEpoch || d.bornEpoch > s.epoch) { d.bornEpoch = s.epoch; dirty = true; }
    if (!d.statEpoch || d.statEpoch > s.epoch) d.statEpoch = s.epoch;
    uint32_t steps = (s.epoch - d.statEpoch) / AGE_STEP_S;
    if (steps) { age(d, steps); d.statEpoch += steps * AGE_STEP_S; }
    if (steps > 10) { snap.food = d.food; snap.fun = d.fun; }   // a long gap gets ONE remark (below), not a hunger one too

    // "you left me": measured from the last second the display was on, not from the aging clock
    if (!d.seenEpoch || d.seenEpoch > s.epoch) d.seenEpoch = s.epoch;
    if (s.sys.lit) {
      uint32_t gone = s.epoch - d.seenEpoch;
      if (gone >= AWAY_S) {
        char b[64];
        unsigned long h = gone / 3600;
        if (h >= 48) snprintf(b, sizeof(b), "%lu DAYS ALONE. RUDE.", h / 24 > 9999 ? 9999UL : h / 24);
        else snprintf(b, sizeof(b), "%lu HOURS ALONE. RUDE.", h);
        react(s, b);
        dirty = true;
      }
      d.seenEpoch = s.epoch;
    }
    if (!d.saveEpoch || d.saveEpoch > s.epoch) d.saveEpoch = s.epoch;
    if (s.epoch - d.saveEpoch >= SAVE_EVERY_S) { d.saveEpoch = s.epoch; dirty = true; }
    d.ageAt = now;
  } else if (ago(now, d.ageAt) >= AGE_STEP_S * 1000UL) { age(d, 1); d.ageAt = now; }

  // he only reacts to a visit once his screen has stayed up for a moment: cycling past him is not a visit
  if (d.open && !d.greeted) { if (ago(now, d.openAt) >= SETTLE_MS) { d.greeted = true; cue = visit(s, rng); } }
  else if (d.greeted) d.visitAt = now;

  uint8_t m = moodOf(s);
  bool moodChanged = m != d.mood;
  d.mood = m;
  if (notice(s) && cue == CUE_NONE) cue = CUE_EVENT;
  if (moodChanged || !d.idle[0] || ago(now, d.idleAt) >= IDLE_MS) chatter(s, rng);
  return cue;
}

// True while something on his screen is moving fast enough to want the high frame rate.
inline bool lively(const Daemon& d) { return d.act != PA_NONE || d.rage >= RAGE_ANGRY; }

}  // namespace pet
}  // namespace shiv
