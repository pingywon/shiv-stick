// PC test for the daemon's brain (SHIV/pet.h) and the screen order table.
// Build + run:  g++ -std=c++17 -o /tmp/test_pet tools/test_pet.cpp && /tmp/test_pet
#include "../SHIV/pet.h"
#include <cstdio>
#include <set>
#include <string>
#include <algorithm>
using namespace shiv;

static int bad = 0;
static void ok(const char* what, bool pass) {
  printf("  %s %s\n", pass ? "ok  " : "FAIL", what);
  if (!pass) bad++;
}

static uint32_t rng = 0xC0FFEE11;
static bool dirty = false;

static State fresh() {
  State s;
  pet::snap = pet::Snap();
  s.now = 100000; s.epoch = 1789754427UL; s.sys.timeValid = true; s.lt.tm_hour = 14;
  s.sys.wifi = true; s.sys.wifiSet = true; s.sys.battPct = 70;
  s.screen = SC_DAEMON;
  pet::tick(s, rng, dirty);
  return s;
}
static void run(State& s, uint32_t ms, uint32_t step = 100) {
  static uint32_t acc = 0;
  for (uint32_t t = 0; t < ms; t += step) { s.now += step; acc += step; s.epoch += acc / 1000; acc %= 1000; pet::tick(s, rng, dirty); }
}
// on his screen, greeted, nothing being said
static State settled() {
  State s = fresh();
  run(s, 1000);
  s.dmn.say[0] = 0;
  return s;
}

int main() {
  { // screen order
    std::set<int> seen(SCREEN_ORDER, SCREEN_ORDER + SC_COUNT);
    ok("SCREEN_ORDER lists every screen exactly once", (int)seen.size() == SC_COUNT);
    ok("CLOCK first, TV-OFF second, REFLEX third", SCREEN_ORDER[0] == SC_CLOCK && SCREEN_ORDER[1] == SC_TVOFF && SCREEN_ORDER[2] == SC_REFLEX);
    ok("SPECTRUM is the last screen id", SC_SPECTRUM == SC_COUNT - 1);
  }
  {  // favorites loop + GO TO groups
    State s; s.now = 100000;
    s.set.favN = 3; s.set.fav[0] = SC_CLOCK; s.set.fav[1] = SC_WEATHER; s.set.fav[2] = SC_SYSTEM; s.screen = SC_CLOCK;
    ok("favorites: steps to the next favorite", nav::stepScreen(s, 1) == SC_WEATHER);
    s.screen = SC_SYSTEM; ok("favorites: wraps", nav::stepScreen(s, 1) == SC_CLOCK);
    s.set.screenMask &= ~(1u << SC_SYSTEM); ok("favorites: a disabled stop is skipped", nav::stepScreen(s, 1) == SC_CLOCK); s.set.screenMask = 0xFFFFFFFF;
    s.set.favN = 0; s.screen = SC_CLOCK; ok("no favorites = the full cycle", nav::stepScreen(s, 1) == SC_TVOFF);
    ok("GO TO groups: HOME first, SYSTEM last", nav::grpAt(s, 0) == SG_HOME && nav::grpAt(s, nav::grpCount(s) - 1) == SG_SYSTEM);
    s.pager.unread = 2; s.door.unread = 1; ok("badges add up on DATA only", nav::grpBadge(s, SG_DATA) == 3 && nav::grpBadge(s, SG_HOME) == 0);
  }
  { // a poke now and then is fine
    State s = settled();
    for (int i = 0; i < 6; i++) { pet::poke(s, rng); run(s, 12000); }
    ok("6 pokes, 12 s apart: never irked", s.dmn.rage < pet::RAGE_IRKED && !s.dmn.sulking);
    ok("pokes counted", s.dmn.pokes == 6);
  }
  { // too many pokes
    State s = settled();
    int irkedAt = 0, angryAt = 0, quitAt = 0;
    for (int i = 1; i <= 12 && !quitAt; i++) {
      pet::Cue c = pet::poke(s, rng);
      run(s, 300);
      if (!irkedAt && s.dmn.mood == MD_IRKED) irkedAt = i;
      if (!angryAt && s.dmn.mood == MD_ANGRY) angryAt = i;
      if (c == pet::CUE_RAGE) quitAt = i;
    }
    printf("       rapid pokes: irked at #%d, angry at #%d, rage-quit at #%d\n", irkedAt, angryAt, quitAt);
    ok("gets irked, then angry, then quits on you", irkedAt && angryAt > irkedAt && quitAt > angryAt);
    ok("sulking, back turned", s.dmn.sulking && s.dmn.mood == MD_SULKING);
    ok("a poke during the sulk is refused", pet::poke(s, rng) == pet::CUE_REFUSE && s.dmn.sulking);
    ok("play during the sulk is refused", pet::play(s, rng) == pet::CUE_REFUSE);
    run(s, 20000);
    ok("still sulking 20 s later", s.dmn.sulking);
    pet::poke(s, rng);
    run(s, 12000);
    ok("the poke made the sulk last longer", s.dmn.sulking);
    run(s, 30000);
    ok("left alone, he gets over it", !s.dmn.sulking && s.dmn.rage <= 35);
    run(s, 40000);
    ok("and cools all the way down", s.dmn.rage == 0);
  }
  { // a snack ends a sulk
    State s = settled();
    while (!s.dmn.sulking) pet::poke(s, rng);
    s.dmn.food = 50;
    ok("feeding a sulk = forgiven", pet::feed(s, rng) == pet::CUE_FORGIVE && !s.dmn.sulking);
    run(s, 200);
    ok("eating animation", s.dmn.mood == MD_EATING);
    run(s, 3000);
    ok("animation ends", s.dmn.act == PA_NONE);
  }
  { // feeding + playing
    State s = settled();
    s.dmn.food = 96;
    ok("refuses food when stuffed", pet::feed(s, rng) == pet::CUE_REFUSE && s.dmn.food == 96);
    s.dmn.food = 40; s.dmn.fun = 30;
    ok("eats", pet::feed(s, rng) == pet::CUE_NOM && s.dmn.food == 70 && s.dmn.fed == 1);
    run(s, 3000);
    ok("plays", pet::play(s, rng) == pet::CUE_PLAY && s.dmn.fun == 60 && s.dmn.played == 1);
    run(s, 200);
    ok("hyped while playing", s.dmn.mood == MD_HYPED);
    s.dmn.food = 5; run(s, 3000);
    ok("too hungry to play", pet::play(s, rng) == pet::CUE_REFUSE);
  }
  { // shake
    State s = settled();
    ok("shake = dizzy", pet::shake(s, rng) == pet::CUE_DIZZY);
    run(s, 200);
    ok("dizzy mood", s.dmn.mood == MD_DIZZY);
    uint8_t r0 = s.dmn.rage;
    pet::shake(s, rng);
    ok("shaken again = angrier", s.dmn.rage > r0);
  }
  { // he cannot die
    State s = settled();
    s.epoch += 3 * 86400UL; s.now += 1000;
    pet::tick(s, rng, dirty);
    ok("3 days alone: empty belly, no fun", s.dmn.food == 0 && s.dmn.fun == 0);
    ok("still here, just hangry", s.dmn.mood == MD_HANGRY);
    ok("remarks on the gap", strstr(s.dmn.say, "3 DAYS ALONE") != nullptr);
    ok("still pokeable", pet::poke(s, rng) != pet::CUE_NONE);
    ok("one snack helps", pet::feed(s, rng) != pet::CUE_REFUSE && s.dmn.food == 30);
  }
  { // hunger pace
    State s = settled();
    s.dmn.food = 100; s.dmn.fun = 100;
    uint32_t e0 = s.epoch;
    for (int i = 0; i < 60; i++) { s.epoch = e0 + (i + 1) * 60; s.now += 60000; pet::tick(s, rng, dirty); }
    printf("       after 1 h awake: food %d, fun %d\n", s.dmn.food, s.dmn.fun);
    ok("one hour costs ~7 food, ~10 fun", s.dmn.food >= 92 && s.dmn.food <= 94 && s.dmn.fun >= 89 && s.dmn.fun <= 91);
  }
  { // no clock: still ages, slowly, off millis
    State s = settled();
    s.sys.timeValid = false; s.dmn.fun = 50;
    for (int i = 0; i < 13; i++) { s.now += 60000; pet::tick(s, rng, dirty); }
    ok("ages without a clock", s.dmn.fun < 50);
  }
  { // noticing things
    State s = settled();
    s.screen = SC_CLOCK;
    s.sys.usb = true; run(s, 100);
    ok("cable noticed while on another screen -> kept as a note", !strcmp(s.dmn.note, pet::LINES[pet::L_USB_IN]) && !s.dmn.say[0]);
    s.screen = SC_DAEMON; run(s, 300); s.screen = SC_GAMES; run(s, 300);
    ok("passing through his screen (double-click back) does not use the note up", !strcmp(s.dmn.note, pet::LINES[pet::L_USB_IN]) && !s.dmn.say[0]);
    s.screen = SC_DAEMON; run(s, 800);
    ok("note spoken once you actually stop on him", !strcmp(s.dmn.say, pet::LINES[pet::L_USB_IN]) && !s.dmn.note[0]);
    run(s, 6000);
    s.hosts.at = s.now; s.hosts.n = 2; scopy(s.hosts.h[0].name, "NAS"); s.hosts.h[0].up = 1; s.hosts.h[1].up = 1;
    run(s, 200);
    s.hosts.h[0].up = 0; run(s, 100);
    ok("host going down is called out by name", !strcmp(s.dmn.say, "NAS WENT DARK!"));
    State q = settled();
    q.jobs.at = q.now; q.jobs.done = 4; run(q, 200);
    ok("first data arriving is not an event", !q.dmn.say[0] || strcmp(q.dmn.say, pet::LINES[pet::L_JOB_DONE]));
    q.jobs.done = 5; run(q, 100);
    ok("a finished Claude job is", !strcmp(q.dmn.say, pet::LINES[pet::L_JOB_DONE]));
  }
  { // review finding 1: a stuffed, sulking daemon must still take the bribe
    State s = settled();
    while (!s.dmn.sulking) pet::poke(s, rng);
    s.dmn.food = 98; uint8_t fun0 = s.dmn.fun;
    ok("stuffed + sulking: feeding still ends the sulk", pet::feed(s, rng) == pet::CUE_FORGIVE && !s.dmn.sulking && s.dmn.fun == fun0 && s.dmn.food == 98);
    State t = settled();
    t.dmn.food = 98; t.dmn.rage = 0;
    ok("stuffed refusal says so", pet::feed(t, rng) == pet::CUE_REFUSE && !strcmp(t.dmn.say, pet::LINES[pet::L_STUFFED]));
  }
  { // review finding 3: display dark on his screen -> the remark waits for you
    State s = settled();
    s.sys.lit = false; run(s, 1000);
    s.jobs.at = s.now; s.jobs.failed = 0; run(s, 200);
    s.jobs.failed = 1; run(s, 5000);
    ok("event with the display off is kept", !strcmp(s.dmn.note, pet::LINES[pet::L_JOB_FAIL]));
    s.sys.lit = true; run(s, 800);
    ok("and spoken when the display comes back", !strcmp(s.dmn.say, pet::LINES[pet::L_JOB_FAIL]));
  }
  { // review finding 4: Wi-Fi remarks
    State s; pet::snap = pet::Snap();
    s.now = 5000; s.epoch = 1789754427UL; s.sys.timeValid = true; s.lt.tm_hour = 14; s.sys.wifiSet = true; s.sys.wifi = false; s.screen = SC_CLOCK;
    pet::tick(s, rng, dirty); run(s, 4000);
    s.sys.wifi = true; run(s, 2000);
    ok("connecting at boot is not 'BACK ONLINE'", !s.dmn.note[0] && !s.dmn.say[0]);
    s.sys.wifi = false; run(s, 5000); s.sys.wifi = true; run(s, 2000);
    ok("a 5 s blip (wake from sleep) is ignored", !s.dmn.note[0]);
    s.sys.wifi = false; run(s, 20000);
    ok("down for 20 s = SIGNAL'S GONE", !strcmp(s.dmn.note, pet::LINES[pet::L_WIFI_LOST]));
    s.sys.wifi = true; run(s, 500);
    ok("then BACK ONLINE", !strcmp(s.dmn.note, pet::LINES[pet::L_WIFI_BACK]));
  }
  { // review finding 5: a reboot is not an absence; a dark pocket is
    State s = settled();
    s.dmn.statEpoch = s.epoch - 10 * 3600; s.dmn.seenEpoch = s.epoch - 600;      // config saved 10 h ago, display seen 10 min ago
    s.dmn.food = 100; s.dmn.fun = 100; run(s, 500);
    ok("stale aging clock after a reboot: he is hungrier, but no 'ALONE' remark", s.dmn.food < 40 && !strstr(s.dmn.say, "ALONE") && !strstr(s.dmn.note, "ALONE"));
    s.dmn.say[0] = 0; s.dmn.note[0] = 0;
    s.sys.lit = false;
    for (int i = 0; i < 6; i++) { s.epoch += 1800; s.now += 1800000; pet::tick(s, rng, dirty); }   // timer wakes while pocketed
    s.sys.lit = true; run(s, 800);
    printf("       after the pocket: say \"%s\" note \"%s\"\n", s.dmn.say, s.dmn.note);
    ok("3 h dark in a pocket (with timer wakes) = '3 HOURS ALONE'", strstr(s.dmn.say, "3 HOURS ALONE") != nullptr);
    dirty = false; s.epoch += 1900; s.now += 1900000; pet::tick(s, rng, dirty);
    ok("clock fields are flagged for saving every half hour", dirty);
  }
  { // aging counter wraps cleanly
    Daemon d; d.food = 100; d.fun = 100;
    pet::age(d, 300);
    ok("300 steps = exactly 100 food", d.food == 0);
    Daemon e; e.food = 100; e.fun = 100;
    for (int i = 0; i < 258; i++) pet::age(e, 1);
    ok("258 single steps = 86 food, 129 fun (no drift at the wrap)", e.food == 14 && e.fun == 0);
  }
  { // SIDE button: click = forward, double-click = back, fast burst keeps going back
    nav::Side k;
    ok("first click goes forward", nav::sideStep(k, 1000) == 1);
    ok("second click 200 ms later = two back", nav::sideStep(k, 1200) == -2);
    ok("third quick click keeps going back", nav::sideStep(k, 1400) == -1);
    ok("after a pause, forward again", nav::sideStep(k, 2400) == 1);
    ok("slow clicks (400 ms) all go forward", nav::sideStep(k, 2800) == 1 && nav::sideStep(k, 3200) == 1);
    k.run = 0;
    ok("disarmed (another button in between) = forward", nav::sideStep(k, 3300) == 1);
    State s; s.screen = SC_CLOCK;
    ok("CLOCK -> next = TV-OFF", nav::stepScreen(s, 1) == SC_TVOFF);
    s.screen = SC_TVOFF;
    ok("double-click from CLOCK (now on TV-OFF, -2) = SYSTEM", nav::stepScreen(s, -2) == SC_SYSTEM);
    s.screen = SC_REFLEX;
    ok("REFLEX (3rd), -1 = TV-OFF", nav::stepScreen(s, -1) == SC_TVOFF);
    s.set.screenMask &= ~(1u << SC_TVOFF);
    ok("disabled screens are skipped both ways", nav::stepScreen(s, -1) == SC_CLOCK && (s.screen = SC_CLOCK, nav::stepScreen(s, 1)) == SC_REFLEX);
    s.set.screenMask = 0;
    ok("only CLOCK + SYSTEM enabled: still sane", nav::stepScreen(s, 1) == SC_SYSTEM && nav::stepScreen(s, -2) == SC_CLOCK);
    ok("wrapStep: 1-item and empty lists", nav::wrapStep(0, -2, 1) == 0 && nav::wrapStep(0, 1, 0) == 0 && nav::wrapStep(0, -2, 5) == 3);
  }
  { // saved Wi-Fi networks: newest first, nothing lost until all five are full
    char id[WIFI_SLOTS][33] = {}, pw[WIFI_SLOTS][65] = {}, us[WIFI_SLOTS][65] = {};
    const int N = WIFI_SLOTS;
    ok("five slots", N == 5);
    wifilist::remember(id, pw, us, N, "Home", "h", "");
    wifilist::remember(id, pw, us, N, "Work", "w", "jdoe");
    wifilist::remember(id, pw, us, N, "Phone", "p", "");
    ok("three joined: newest first, older ones kept", !strcmp(id[0], "Phone") && !strcmp(id[1], "Work") && !strcmp(id[2], "Home") && !id[3][0] && !strcmp(pw[2], "h"));
    ok("the username travels with its network", !strcmp(us[1], "jdoe") && !us[0][0] && !us[2][0]);
    wifilist::remember(id, pw, us, N, "Home", "h2", "");
    ok("re-joining a saved one moves it to the front with the new password, no duplicate", !strcmp(id[0], "Home") && !strcmp(pw[0], "h2") && !strcmp(id[1], "Phone") && !strcmp(id[2], "Work") && !strcmp(us[2], "jdoe") && !id[3][0]);
    wifilist::remember(id, pw, us, N, "Cafe", "c", "");
    wifilist::remember(id, pw, us, N, "Hotel", "", "");
    ok("five saved, none lost", !strcmp(id[0], "Hotel") && !strcmp(id[4], "Work") && !strcmp(us[4], "jdoe"));
    wifilist::remember(id, pw, us, N, "Bar", "b", "");
    ok("a sixth network drops only the oldest", !strcmp(id[0], "Bar") && !strcmp(id[1], "Hotel") && !pw[1][0] && !strcmp(id[4], "Phone") && !us[4][0]);
    wifilist::remember(id, pw, us, N, id[4], pw[4], us[4]);
    ok("remembering a name that lives in the list itself is safe", !strcmp(id[0], "Phone") && !strcmp(pw[0], "p") && !strcmp(id[4], "Home"));
    ok("saved password is found by network name", !strcmp(wifilist::savedOf(id, pw, N, "Cafe"), "c") && !wifilist::savedOf(id, pw, N, "Nope")[0] && !wifilist::savedOf(id, pw, N, "")[0]);
    id[1][0] = 0; scopy(id[2], "Phone");
    wifilist::compact(id, pw, us, N);
    ok("compact closes gaps and removes duplicates", !strcmp(id[0], "Phone") && !strcmp(id[1], "Cafe") && !strcmp(id[2], "Home") && !id[3][0] && !id[4][0] && !pw[3][0]);
    for (int r = 0; r < JR_N; r++) if (!joinText(r)[0]) ok("every join result has words", false);
  }
  { // small talk keeps changing and never overflows
    State s = settled();
    s.wx.at = 1; s.wx.tempF = 72; s.pi.at = 1; s.pi.blocked = 18400; s.dmn.food = 80; s.dmn.fun = 80;
    std::set<std::string> said;
    for (int i = 0; i < 60; i++) { run(s, 7000, 1000); said.insert(s.dmn.idle); }
    printf("       %zu different idle lines in 7 minutes\n", said.size());
    ok("says plenty of different things", said.size() >= 10);
    size_t longest = 0;
    for (int m = 0; m < MD_N; m++) for (int k = 0; k < pet::SAY_N; k++) longest = std::max(longest, strlen(pet::MOOD_SAY[m][k]));
    for (int k = 0; k < pet::L_N; k++) longest = std::max(longest, strlen(pet::LINES[k]));
    ok("every line fits the 64-byte buffer", longest < 64);
  }
  { // stamps fresher than "now" (written mid-pass) must not wrap
    State s = settled();
    s.dmn.act = PA_EAT; s.dmn.actAt = s.now + 3; s.dmn.rage = 40; s.dmn.coolAt = s.now + 3;
    pet::tick(s, rng, dirty);
    ok("a stamp 3 ms in the future does not end the animation or zero the rage", s.dmn.act == PA_EAT && s.dmn.rage == 40);
  }
  {  // microphone DSP (mic.h foundation)
    using namespace micdsp;
    int16_t quiet[256]; for (int i = 0; i < 256; i++) quiet[i] = (int16_t)((i % 7) - 3);   // near-silent dither
    ok("rms: near-silent frame is tiny", rms(quiet, 256) < 5);
    int16_t full[256]; for (int i = 0; i < 256; i++) full[i] = (i & 1) ? 20000 : -20000;   // full-swing square
    ok("rms: a loud frame reads high", rms(full, 256) > 15000);
    ok("rms: empty/null is 0", rms(nullptr, 0) == 0 && rms(quiet, 0) == 0);

    ok("loud: at or below the floor is 0", loud(400, 400, 5) == 0 && loud(300, 400, 5) == 0);
    ok("loud: rises with volume and caps at 8", loud(1000, 400, 5) < loud(3000, 400, 5) && loud(50000, 400, 5) == 8);
    ok("loud: more sensitivity reads louder for the same sound", loud(1500, 400, 8) > loud(1500, 400, 2));

    ok("floor: drops fast toward a quiet frame", updateFloor(4000, 400) < 4000 && updateFloor(4000, 400) <= 3600);
    ok("floor: rises only slowly toward a loud frame", updateFloor(400, 4000) > 400 && updateFloor(400, 4000) < 500);

    // Goertzel: a 500 Hz sine at 16 kHz should light band 2 (BAND_HZ[2] == 500) brightest.
    int16_t sine[256]; uint32_t sr = 16000;
    for (int i = 0; i < 256; i++) sine[i] = (int16_t)(15000.0 * sin(2.0 * 3.14159265 * 500.0 * i / sr));
    uint8_t bd[BANDS]; bands(sine, 256, sr, bd, 1.0f);
    int top = 0; for (int b = 1; b < BANDS; b++) if (bd[b] > bd[top]) top = b;
    ok("spectrum: a 500 Hz tone peaks in the 500 Hz band", top == 2 && bd[2] > 200);
    for (int i = 0; i < 256; i++) sine[i] = (int16_t)(15000.0 * sin(2.0 * 3.14159265 * 4000.0 * i / sr));
    bands(sine, 256, sr, bd, 1.0f);
    top = 0; for (int b = 1; b < BANDS; b++) if (bd[b] > bd[top]) top = b;
    ok("spectrum: a 4 kHz tone peaks in the 4 kHz band", top == 6);

    // Clap detector. Frames are ~16 ms apart; settle the floor on quiet first.
    auto settleFloor = [](Clap& c, uint32_t& t) { for (int i = 0; i < 40; i++) { c.feed(300, t, 5); t += 16; } };
    { // one clap -> EV_CLAP1 after the window closes
      Clap c; uint32_t t = 1000; settleFloor(c, t);
      int e1 = c.feed(9000, t, 5); t += 16;            // onset
      int e2 = c.feed(300, t, 5);  t += 16;            // falls back -> count 1
      ok("clap: a single clap reports nothing yet", e1 == EV_NONE && e2 == EV_NONE);
      int ev = EV_NONE; for (int i = 0; i < 50 && ev == EV_NONE; i++) { ev = c.feed(300, t, 5); t += 16; }
      ok("clap: one clap resolves to EV_CLAP1 after the window", ev == EV_CLAP1);
    }
    { // two claps inside the window -> EV_CLAP2
      Clap c; uint32_t t = 1000; settleFloor(c, t);
      c.feed(9000, t, 5); t += 16; c.feed(300, t, 5); t += 16;   // clap 1
      for (int i = 0; i < 6; i++) { c.feed(300, t, 5); t += 16; } // ~100 ms gap
      c.feed(9000, t, 5); t += 16;                                 // clap 2 onset
      int ev = c.feed(300, t, 5);                                  // clap 2 falls
      ok("clap: two quick claps report EV_CLAP2", ev == EV_CLAP2);
    }
    { // a sustained loud sound is not a clap
      Clap c; uint32_t t = 1000; settleFloor(c, t);
      int ev = EV_NONE; for (int i = 0; i < 40 && ev != EV_CLAP1 && ev != EV_CLAP2; i++) { ev = c.feed(9000, t, 5); t += 16; }
      ok("clap: a steady loud noise never counts as a clap", ev == EV_NONE);
    }
    { // a quiet room stays silent
      Clap c; uint32_t t = 1000; int ev = EV_NONE;
      for (int i = 0; i < 80 && ev == EV_NONE; i++) { ev = c.feed(300, t, 5); t += 16; }
      ok("clap: a quiet room fires nothing", ev == EV_NONE);
    }
  }
  // ---- self-update version compare (otautil::newer)
  {
    using otautil::newer;
    ok("ota: 2.2.0 is newer than 2.1.0", newer("2.2.0", "2.1.0"));
    ok("ota: 2.1.0 is newer than 2.0.1", newer("2.1.0", "2.0.1"));
    ok("ota: 2.0.1 is newer than 2.0.0", newer("2.0.1", "2.0.0"));
    ok("ota: 10.0.0 is newer than 9.9.9 (not string compare)", newer("10.0.0", "9.9.9"));
    ok("ota: equal is NOT newer", !newer("2.1.0", "2.1.0"));
    ok("ota: older is NOT newer", !newer("2.0.0", "2.1.0"));
    ok("ota: a leading v is ignored", newer("v2.2.0", "2.1.0"));
    ok("ota: trailing text after the patch is ignored", !newer("2.1.0-rc1", "2.1.0"));
    ok("ota: a missing patch counts as 0", newer("2.2", "2.1.9"));
    ok("ota: empty candidate is never newer", !newer("", "2.1.0"));
  }
  printf("%s\n", bad ? "FAILED" : "all passed");
  return bad ? 1 : 0;
}
