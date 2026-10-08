// SHIV render harness: draws every screen with the REAL device drawing code + M5GFX fonts
// into a 240x135 buffer on the PC, saves images, and audits the text for legibility.
#include "../../SHIV/draw_sys.h"
#include <cstdio>
#include <algorithm>
#include <string>
#include <vector>
#include <functional>
#include <sys/stat.h>

using namespace shiv;

struct Shot { std::string name; std::function<void(State&)> setup; };
static std::vector<Shot> shots;
static void add(const std::string& n, std::function<void(State&)> f) { shots.push_back({n, f}); }

static void baseState(State& s) {
  s.now = 123456;
  s.epoch = 1789754427UL;
  s.lt.tm_hour = 13; s.lt.tm_min = 50; s.lt.tm_sec = 27; s.lt.tm_mday = 18; s.lt.tm_mon = 8; s.lt.tm_wday = 5; s.lt.tm_year = 126;
  s.sys.timeValid = true; s.sys.wifi = true; s.sys.battPct = 82; s.sys.battMv = 4012; s.sys.rssi = -61;
  scopy(s.sys.ip, "192.168.1.42"); s.sys.heapKb = 212; s.sys.uptimeS = 11565;
  s.wx.at = s.now - 240000; s.wx.tempF = 72; s.wx.hiF = 78; s.wx.loF = 61; s.wx.code = 2; s.wx.isDay = true;
  s.pi.at = s.now - 20000; s.pi.queries = 48210; s.pi.blocked = 18400; s.pi.pct = 38.2f; s.pi.histN = 24;
  for (int i = 0; i < 24; i++) s.pi.hist[i] = 25 + (i * 7) % 22;
  const char* hn[8] = {"NAS", "HA", "PIHOLE", "SERVER", "ROUTER", "PRINTER", "OFFICE-PI", "GARAGE"};
  s.hosts.n = 8; s.hosts.at = s.now;
  for (int i = 0; i < 8; i++) { scopy(s.hosts.h[i].name, hn[i]); s.hosts.h[i].up = (i == 5) ? 0 : 1; }
  s.shop.at = s.now - 100000; s.shop.orders = 7; s.shop.revenue = 1284.4f; s.shop.unfulfilled = 2;
  s.jobs.at = s.now - 5000; s.jobs.running = 1; s.jobs.held = 2; s.jobs.done = 14;
  scopy(s.jobs.lastTitle, "nightly backup finished");
  const char* fl[5] = {"DESK LAMP", "MOVIE SCENE", "ALL OFF", "PORCH LIGHT", "GOODNIGHT"};
  s.home.n = 5; s.home.at = s.now;
  for (int i = 0; i < 5; i++) { scopy(s.home.f[i].label, fl[i]); s.home.f[i].on = (i == 0) ? 1 : (i == 3) ? 0 : -1; }
  const char* ml[4] = {"REBOOT SERVER", "WAKE NAS", "PAUSE PIHOLE", "PING ROUTER"};
  s.macros.n = 4;
  for (int i = 0; i < 4; i++) scopy(s.macros.m[i].label, ml[i]);
  s.pager.n = 2; s.pager.unread = 1;
  scopy(s.pager.m[0].text, "Claude job finished: SHIV firmware compiled clean. Flash it from the Arduino IDE whenever you are back at the desk."); scopy(s.pager.m[0].when, "13:42"); s.pager.m[0].unread = true;
  scopy(s.pager.m[1].text, "Dinner at 7"); scopy(s.pager.m[1].when, "11:05");
  s.door.n = 3; s.door.unread = 1;
  scopy(s.door.e[0].cam, "FRONT DOOR"); scopy(s.door.e[0].what, "PERSON"); scopy(s.door.e[0].when, "11:42"); s.door.e[0].unread = true;
  scopy(s.door.e[1].cam, "DRIVEWAY"); scopy(s.door.e[1].what, "MOTION"); scopy(s.door.e[1].when, "11:38");
  scopy(s.door.e[2].cam, "BACK GATE"); scopy(s.door.e[2].what, "RING"); scopy(s.door.e[2].when, "10:05");
  s.mqtt.n = 3; s.mqtt.connected = true;
  scopy(s.mqtt.m[0].topic, "zigbee/back-door"); scopy(s.mqtt.m[0].text, "open"); scopy(s.mqtt.m[0].when, "11:40");
  scopy(s.mqtt.m[1].topic, "govee/office/lamp"); scopy(s.mqtt.m[1].text, "ON 60%"); scopy(s.mqtt.m[1].when, "11:31");
  scopy(s.mqtt.m[2].topic, "sensor/garage/temp"); scopy(s.mqtt.m[2].text, "64.2 F"); scopy(s.mqtt.m[2].when, "11:05");
  const char* ss[6] = {"HomeNet", "GuestNet-5G-Upstairs", "xfinitywifi", "", "DIRECT-roku-882", "NETGEAR42"};
  s.wifi.count = 6; s.wifi.at = s.now;
  for (int i = 0; i < 6; i++) { scopy(s.wifi.n[i].ssid, ss[i]); s.wifi.n[i].rssi = -48 - i * 9; s.wifi.n[i].ch = (i * 5) % 11 + 1; s.wifi.n[i].open = (i == 2); }
  uint8_t cl[14] = {0, 40, 8, 0, 3, 0, 55, 9, 0, 0, 2, 22, 5, 0};
  memcpy(s.wifi.chanLoad, cl, 14);
  const char* bn[5] = {"Galaxy Watch6", "", "Govee_H6159_3A1F", "JBL Flip 6", ""};
  s.ble.count = 5; s.ble.total = 23; s.ble.at = s.now;
  for (int i = 0; i < 5; i++) { scopy(s.ble.d[i].name, bn[i]); snprintf(s.ble.d[i].mac, 18, "A4:C1:38:%02X:%02X:%02X", i * 17, i * 31 + 5, 200 - i); s.ble.d[i].rssi = -50 - i * 8; }
  const char* in[3] = {"TV POWER", "SOUNDBAR VOL+", "IR-03"};
  s.ir.n = 3;
  for (int i = 0; i < 3; i++) { scopy(s.ir.c[i].name, in[i]); s.ir.c[i].len = 67; }
  s.lvl.roll = 7.4f; s.lvl.pitch = -3.1f;
  s.games.run.best = 412; s.games.snake.best = 180; s.games.reflex.bestMs = 231;
  s.dmn.pokes = 42;
}

static void defineShots() {
  for (int f = 0; f < 4; f++) add("clock_face" + std::to_string(f), [f](State& s) { s.screen = SC_CLOCK; s.set.clockFace = f; });
  add("clock_face0_wide", [](State& s) { s.screen = SC_CLOCK; s.lt.tm_hour = 20; s.lt.tm_min = 8; s.lt.tm_wday = 3; s.lt.tm_mday = 30; s.pager.unread = 3; s.sys.battPct = 9; });
  add("clock_notime", [](State& s) { s.screen = SC_CLOCK; s.sys.timeValid = false; });
  add("timer_fresh", [](State& s) { s.screen = SC_TIMER; });
  add("timer_running", [](State& s) { s.screen = SC_TIMER; s.cd.running = true; s.cd.remainMs = 187000; });
  add("timer_hour", [](State& s) { s.screen = SC_TIMER; s.cd.totalMs = s.cd.remainMs = 3600000; });
  add("timer_ringing", [](State& s) { s.screen = SC_TIMER; s.cd.ringing = true; s.now = 123500; });
  add("pomo_idle", [](State& s) { s.screen = SC_POMO; });
  add("pomo_focus", [](State& s) { s.screen = SC_POMO; s.pomo.phase = 1; s.pomo.running = true; s.pomo.remainMs = 1111000; s.pomo.round = 1; });
  add("pomo_break_ring", [](State& s) { s.screen = SC_POMO; s.pomo.phase = 2; s.pomo.ringing = true; });
  add("weather_partly", [](State& s) { s.screen = SC_WEATHER; });
  add("weather_storm_hot", [](State& s) { s.screen = SC_WEATHER; s.wx.code = 95; s.wx.tempF = 102; s.wx.hiF = 104; s.wx.loF = 88; });
  add("weather_snow_night", [](State& s) { s.screen = SC_WEATHER; s.wx.code = 86; s.wx.tempF = -5; s.wx.hiF = 12; s.wx.loF = -11; s.wx.isDay = false; });
  add("weather_clear_night", [](State& s) { s.screen = SC_WEATHER; s.wx.code = 0; s.wx.isDay = false; });
  add("weather_frzrain", [](State& s) { s.screen = SC_WEATHER; s.wx.code = 67; });
  add("weather_nodata", [](State& s) { s.screen = SC_WEATHER; s.wx.at = 0; s.sys.wifi = false; });
  add("pihole", [](State& s) { s.screen = SC_PIHOLE; });
  add("pihole_big_disabled", [](State& s) { s.screen = SC_PIHOLE; s.pi.queries = 1248210; s.pi.blocked = 918400; s.pi.pct = 73.6f; s.pi.enabled = true; s.pi.source = 5; });
  add("pihole_err", [](State& s) { s.screen = SC_PIHOLE; s.pi.at = 0; scopy(s.pi.err, "BAD PASSWORD"); });
  add("hosts", [](State& s) { s.screen = SC_HOSTS; });
  add("hosts_page2", [](State& s) { s.screen = SC_HOSTS; s.hosts.page = 1; });
  add("hosts_empty", [](State& s) { s.screen = SC_HOSTS; s.hosts.n = 0; });
  add("shop", [](State& s) { s.screen = SC_SHOP; });
  add("shop_big", [](State& s) { s.screen = SC_SHOP; s.shop.revenue = 128499; s.shop.orders = 312; s.shop.unfulfilled = 0; });
  add("shop_notoken", [](State& s) { s.screen = SC_SHOP; s.shop.at = 0; scopy(s.shop.err, "NO TOKEN SET"); });
  add("jobs", [](State& s) { s.screen = SC_JOBS; });
  add("jobs_failed", [](State& s) { s.screen = SC_JOBS; s.jobs.failed = 2; s.jobs.running = 0; });
  auto haFill = [](State& s, int n) {
    const char* nm[8] = {"Bedroom Lamp", "Desk Lamp", "Kitchen Under Cabinet Strip", "Living Room", "Office Ceiling", "Porch Light", "Stairs", "TV Backlight"};
    s.hab.n = n; for (int i = 0; i < n; i++) { scopy(s.hab.e[i].name, nm[i % 8]); snprintf(s.hab.e[i].entity, 48, "light.e%d", i); s.hab.e[i].on = (i % 3 == 0) ? 1 : 0; }
  };
  add("ha_cats", [](State& s) { s.screen = SC_HOME; });
  add("ha_cats_focus", [](State& s) { s.screen = SC_HOME; s.hab.level = 1; s.hab.cat = 2; });
  add("ha_cats_focus_last", [](State& s) { s.screen = SC_HOME; s.hab.level = 1; s.hab.cat = 4; });
  add("ha_cats_nofavs", [](State& s) { s.screen = SC_HOME; s.home.n = 0; s.hab.level = 1; s.hab.cat = 3; });
  add("ha_list", [haFill](State& s) { s.screen = SC_HOME; s.hab.level = 2; s.hab.kind = HC_LIGHTS; haFill(s, 8); s.hab.sel = 1; });
  add("ha_list_focus_longnames", [haFill](State& s) { s.screen = SC_HOME; s.hab.level = 2; s.hab.kind = HC_SWITCHES; haFill(s, 8); s.hab.sel = 2; });
  add("ha_list_marquee_mid", [haFill](State& s) { s.screen = SC_HOME; s.hab.level = 2; s.hab.kind = HC_LIGHTS; haFill(s, 8); s.hab.sel = 2; s.now = 28 * 60; });
  add("ha_list_marquee_end", [haFill](State& s) { s.screen = SC_HOME; s.hab.level = 2; s.hab.kind = HC_LIGHTS; haFill(s, 8); s.hab.sel = 2; s.now = 28 * 400; });
  add("ha_list_48", [haFill](State& s) { s.screen = SC_HOME; s.hab.level = 2; s.hab.kind = HC_LIGHTS; haFill(s, 96); s.hab.sel = 95; s.hab.more = true; });
  add("ha_list_scenes", [haFill](State& s) { s.screen = SC_HOME; s.hab.level = 2; s.hab.kind = HC_SCENES; haFill(s, 4); for (int i = 0; i < 4; i++) s.hab.e[i].on = -1; });
  add("ha_list_scripts", [haFill](State& s) { s.screen = SC_HOME; s.hab.level = 2; s.hab.kind = HC_SCRIPTS; haFill(s, 2); for (int i = 0; i < 2; i++) s.hab.e[i].on = -1; });
  add("ha_list_favs", [haFill](State& s) { s.screen = SC_HOME; s.hab.level = 2; s.hab.kind = HC_FAVS; haFill(s, 5); });
  add("ha_loading", [](State& s) { s.screen = SC_HOME; s.hab.level = 2; s.hab.kind = HC_SCENES; s.hab.loading = true; });
  add("ha_notoken", [](State& s) { s.screen = SC_HOME; s.hab.level = 2; s.hab.kind = HC_LIGHTS; scopy(s.hab.err, "NO HA TOKEN"); });
  add("ha_badtoken", [](State& s) { s.screen = SC_HOME; s.hab.level = 2; s.hab.kind = HC_LIGHTS; scopy(s.hab.err, "BAD HA TOKEN"); });
  add("ha_busy", [](State& s) { s.screen = SC_HOME; s.hab.level = 2; s.hab.kind = HC_LIGHTS; scopy(s.hab.err, "BUSY - TRY AGAIN"); });
  add("ha_empty_category", [](State& s) { s.screen = SC_HOME; s.hab.level = 2; s.hab.kind = HC_SCRIPTS; });
  add("pihole_off_timer", [](State& s) { s.screen = SC_PIHOLE; s.pi.source = 5; s.pi.enabled = false; s.set.piOffUntil = s.epoch + 9 * 3600 + 42 * 60; });
  add("pihole_off_minutes", [](State& s) { s.screen = SC_PIHOLE; s.pi.source = 5; s.pi.enabled = false; s.set.piOffUntil = s.epoch + 59 * 60 + 59; s.pi.queries = 1248210; s.pi.blocked = 918400; });
  add("pihole_off_notimer", [](State& s) { s.screen = SC_PIHOLE; s.pi.source = 5; s.pi.enabled = false; });
  add("pihole_off_other_source", [](State& s) { s.screen = SC_PIHOLE; s.pi.source = 6; s.pi.enabled = false; s.set.piOffUntil = s.epoch + 3600; });
  add("pihole_toggling", [](State& s) { s.screen = SC_PIHOLE; s.pi.source = 5; s.pi.toggling = true; });
  add("pihole_24h", [](State& s) { s.screen = SC_PIHOLE; s.pi.source = 5; s.set.piOffHours = 24; });
  add("toast_pihole_off", [](State& s) { s.screen = SC_CLOCK; setToast(s, "PI-HOLE OFF 24H"); });
  add("toast_badpass", [](State& s) { s.screen = SC_CLOCK; setToast(s, "BAD v6 PASS"); });
  add("toast_settoken", [](State& s) { s.screen = SC_CLOCK; setToast(s, "NO HA TOKEN"); });
  add("toast_busy", [](State& s) { s.screen = SC_CLOCK; setToast(s, "BUSY - TRY AGAIN"); });
  add("macros", [](State& s) { s.screen = SC_MACROS; });
  add("macros_focus", [](State& s) { s.screen = SC_MACROS; s.macros.focus = true; s.macros.sel = 2; });
  add("macros_hold", [](State& s) { s.screen = SC_MACROS; s.macros.focus = true; s.macros.sel = 2; s.macros.holdStart = s.now - 700; });
  add("pager_p1", [](State& s) { s.screen = SC_PAGER; });
  add("pager_p2", [](State& s) { s.screen = SC_PAGER; s.pager.scroll = 1; });
  add("pager_short", [](State& s) { s.screen = SC_PAGER; s.pager.sel = 1; s.pager.unread = 0; });
  add("pager_none", [](State& s) { s.screen = SC_PAGER; s.pager.n = 0; s.pager.unread = 0; });
  add("door_list", [](State& s) { s.screen = SC_DOORBELL; });
  add("door_sel2", [](State& s) { s.screen = SC_DOORBELL; s.door.sel = 2; });
  add("door_longname", [](State& s) { s.screen = SC_DOORBELL; scopy(s.door.e[0].cam, "SIDE ENTRANCE CAM"); scopy(s.door.e[0].what, "PACKAGE"); });
  add("door_none", [](State& s) { s.screen = SC_DOORBELL; s.door.n = 0; s.door.unread = 0; });
  add("mqtt_list", [](State& s) { s.screen = SC_MQTT; });
  add("mqtt_sel2", [](State& s) { s.screen = SC_MQTT; s.mqtt.sel = 2; });
  add("mqtt_listening", [](State& s) { s.screen = SC_MQTT; s.mqtt.n = 0; s.mqtt.connected = true; });
  add("mqtt_off", [](State& s) { s.screen = SC_MQTT; s.mqtt.n = 0; s.mqtt.connected = false; });
  add("mqtt_nobroker", [](State& s) { s.screen = SC_MQTT; s.mqtt.n = 0; s.mqtt.connected = false; scopy(s.mqtt.err, "NO BROKER (-2)"); });
  add("mqtt_offline", [](State& s) { s.screen = SC_MQTT; s.sys.wifi = false; });
  add("wifi_scanning", [](State& s) { s.screen = SC_WIFI; s.wifi.count = 0; s.wifi.scanning = true; });
  add("wifi_list", [](State& s) { s.screen = SC_WIFI; });
  add("wifi_focus", [](State& s) { s.screen = SC_WIFI; s.sys.focus = true; s.wifi.sel = 1; });
  add("wifi_detail", [](State& s) { s.screen = SC_WIFI; s.sys.focus = true; s.wifi.sel = 1; s.wifi.detail = true; });
  add("wifi_detail_open", [](State& s) { s.screen = SC_WIFI; s.sys.focus = true; s.wifi.sel = 2; s.wifi.detail = true; });
  add("channels", [](State& s) { s.screen = SC_CHAN; });
  add("ble_sniff", [](State& s) { s.screen = SC_BLE; s.ble.count = 0; s.ble.scanning = true; });
  add("ble_list", [](State& s) { s.screen = SC_BLE; });
  add("ble_focus", [](State& s) { s.screen = SC_BLE; s.sys.focus = true; s.ble.sel = 2; });
  add("ir_list", [](State& s) { s.screen = SC_IR; });
  add("ir_focus_learnrow", [](State& s) { s.screen = SC_IR; s.ir.focus = true; s.ir.sel = 3; });
  add("ir_listening", [](State& s) { s.screen = SC_IR; s.ir.focus = true; s.ir.mode = 1; s.ir.modeAt = s.now - 3000; });
  add("ir_learned", [](State& s) { s.screen = SC_IR; s.ir.mode = 2; s.ir.modeAt = s.now - 300; s.ir.lastLen = 67; });
  add("ir_nosignal", [](State& s) { s.screen = SC_IR; s.ir.mode = 4; s.ir.modeAt = s.now - 300; });
  add("tvoff_ready", [](State& s) { s.screen = SC_TVOFF; });
  add("tvoff_running", [](State& s) { s.screen = SC_TVOFF; s.tv.running = true; s.tv.idx = 5; s.tv.total = 11; scopy(s.tv.brand, "PHILIPS RC6"); });
  add("level_tilted", [](State& s) { s.screen = SC_LEVEL; });
  add("level_flat", [](State& s) { s.screen = SC_LEVEL; s.lvl.roll = 0.2f; s.lvl.pitch = -0.1f; });
  add("level_max", [](State& s) { s.screen = SC_LEVEL; s.lvl.roll = -178.4f; s.lvl.pitch = -89.9f; });
  add("oracle_idle", [](State& s) { s.screen = SC_ORACLE; });
  for (int k = 0; k < draw::EIGHT_N; k++)
    add("oracle_8ball_" + std::to_string(k), [k](State& s) { s.screen = SC_ORACLE; s.orc.rolledAt = 1; s.orc.mode = 0; scopy(s.orc.line1, draw::EIGHT[k][0]); scopy(s.orc.line2, draw::EIGHT[k][1]); });
  add("oracle_d20_crit", [](State& s) { s.screen = SC_ORACLE; s.orc.rolledAt = 1; s.orc.mode = 2; s.orc.value = 20; scopy(s.orc.line1, "20"); scopy(s.orc.line2, "CRITICAL HIT"); });
  add("oracle_d20_fail", [](State& s) { s.screen = SC_ORACLE; s.orc.rolledAt = 1; s.orc.mode = 2; s.orc.value = 1; scopy(s.orc.line1, "1"); scopy(s.orc.line2, "CRITICAL FAIL"); });
  add("oracle_coin", [](State& s) { s.screen = SC_ORACLE; s.orc.rolledAt = 1; s.orc.mode = 3; scopy(s.orc.line1, "HEADS"); s.orc.line2[0] = 0; });
  add("oracle_spin", [](State& s) { s.screen = SC_ORACLE; s.orc.rolling = true; s.orc.rolledAt = s.now - 200; });
  add("games_menu", [](State& s) { s.screen = SC_GAMES; });
  add("games_menu_focus", [](State& s) { s.screen = SC_GAMES; s.sys.focus = true; s.games.sel = 1; });
  add("game_runner", [](State& s) {
    s.screen = SC_GAMES; s.games.active = 0; uint32_t rng = 99; game::runnerReset(s.games.run); GameInput in; in.tiltX = 0.1f;
    for (int i = 0; i < 70 && !s.games.run.dead; i++) { in.tiltX = 0.05f; game::runnerStep(s.games.run, in, rng); }
  });
  add("game_runner_over", [](State& s) { s.screen = SC_GAMES; s.games.active = 0; s.games.run.started = true; s.games.run.dead = true; s.games.run.score = 1288; s.games.run.best = 1288; });
  add("game_snake", [](State& s) {
    s.screen = SC_GAMES; s.games.active = 1; uint32_t rng = 7; game::snakeReset(s.games.snake, 0, rng); GameInput in; uint32_t t = 0;
    for (int i = 0; i < 30; i++) { t += 300; in.tiltX = (i % 9 < 5) ? 0.5f : 0; in.tiltY = (i % 9 >= 5) ? 0.5f : 0; game::snakeStep(s.games.snake, in, t, rng); }
    s.games.snake.len = 12; for (int i = 3; i < 12; i++) { s.games.snake.bx[i] = s.games.snake.bx[2]; s.games.snake.by[i] = (s.games.snake.by[2] + i) % 9; }
  });
  add("game_snake_over", [](State& s) { s.screen = SC_GAMES; s.games.active = 1; s.games.snake.started = true; s.games.snake.dead = true; s.games.snake.score = 90; });
  for (int p = 0; p < 5; p++) add("game_reflex_" + std::to_string(p), [p](State& s) { s.screen = SC_GAMES; s.games.active = 2; s.games.reflex.phase = p; s.games.reflex.lastMs = 1204; s.games.reflex.bestMs = 231; });
  for (int p = 0; p < 5; p++) add("reflex_solo_" + std::to_string(p), [p](State& s) { s.screen = SC_REFLEX; s.games.active = 2; s.games.reflex.phase = p; s.games.reflex.lastMs = 1204; s.games.reflex.bestMs = 231; });
  // SHIV-16 spectrum face: mic live (bars), and mic off (the hint)
  add("spectrum_live", [](State& s) { s.screen = SC_SPECTRUM; s.set.micOn = true; s.mic.active = true; s.mic.frameAt = s.now; s.mic.level = 5;
    for (int i = 0; i < micdsp::BANDS; i++) s.mic.band[i] = (uint8_t)(40 + i * 26); });
  add("spectrum_off", [](State& s) { s.screen = SC_SPECTRUM; s.set.micOn = false; });
  // the daemon: every line he can say has to fit the bubble, in every mood's pose
  auto petPose = [](State& s, int m) {
    s.screen = SC_DAEMON; s.dmn.mood = m; s.dmn.actAt = s.now - 400;
    s.dmn.act = m == MD_EATING ? PA_EAT : m == MD_HYPED ? PA_PLAY : m == MD_DIZZY ? PA_DIZZY : PA_NONE;
    s.dmn.sulking = m == MD_SULKING; s.dmn.rage = m == MD_ANGRY ? 75 : m == MD_IRKED ? 40 : 0;
    s.dmn.food = (m == MD_HANGRY) ? 6 : (m == MD_HUNGRY) ? 28 : 72; s.dmn.fun = m == MD_BORED ? 12 : 85;
  };
  for (int m = 0; m < MD_N; m++)
    for (int k = 0; k < pet::SAY_N; k++)
      add("daemon_m" + std::to_string(m) + "_" + std::to_string(k), [m, k, petPose](State& s) { petPose(s, m); scopy(s.dmn.say, pet::MOOD_SAY[m][k]); });
  for (int k = 0; k < pet::POKE_N; k++) add("daemon_poke_" + std::to_string(k), [k, petPose](State& s) { petPose(s, MD_CHILL); s.dmn.act = PA_FLINCH; scopy(s.dmn.say, pet::POKE_SAY[k]); });
  for (int k = 0; k < pet::L_N; k++) add("daemon_line_" + std::to_string(k), [k, petPose](State& s) { petPose(s, k % 2 ? MD_CHILL : MD_HAPPY); scopy(s.dmn.say, pet::LINES[k]); });
  for (int k = 0; k < pet::CH_N; k++) {
    add("daemon_chat_" + std::to_string(k), [k, petPose](State& s) {
      petPose(s, MD_CHILL); s.dmn.fed = 7; s.dmn.bornEpoch = s.epoch - 3 * 86400UL; s.sys.rssi = -82;
      if (!pet::chatLine(s, k, s.dmn.say, sizeof(s.dmn.say))) scopy(s.dmn.say, "CHAT LINE MISSING");
    });
    add("daemon_chatmax_" + std::to_string(k), [k, petPose](State& s) {     // worst-case numbers
      petPose(s, MD_BORED); s.lt.tm_hour = 23; s.lt.tm_min = 58; s.wx.tempF = -18; s.pi.blocked = 9999999; s.hosts.h[2].up = 0;
      s.jobs.running = 0; s.jobs.held = 12; s.shop.orders = 9999; s.sys.battPct = 100; s.sys.uptimeS = 9999UL * 3600 + 3599;
      s.dmn.pokes = 999999; s.dmn.fed = 999999; s.dmn.bornEpoch = s.epoch - 99999UL * 86400UL; s.sys.rssi = -100;
      if (k == pet::CH_WX) s.wx.tempF = 104;
      if (k == pet::CH_PIHOLE) s.pi.enabled = false;
      if (!pet::chatLine(s, k, s.dmn.say, sizeof(s.dmn.say))) scopy(s.dmn.say, "CHAT LINE MISSING");
    });
  }
  add("daemon_host_dark", [petPose](State& s) { petPose(s, MD_CHILL); scopy(s.dmn.say, "PRINTER WENT DARK!"); });
  add("daemon_host_back", [petPose](State& s) { petPose(s, MD_HAPPY); scopy(s.dmn.say, "PRINTER IS BACK."); });
  add("daemon_away_hours", [petPose](State& s) { petPose(s, MD_HANGRY); s.dmn.food = 0; s.dmn.fun = 0; scopy(s.dmn.say, "47 HOURS ALONE. RUDE."); });
  add("daemon_away_days", [petPose](State& s) { petPose(s, MD_HANGRY); s.dmn.food = 100; s.dmn.fun = 100; scopy(s.dmn.say, "9999 DAYS ALONE. RUDE."); });
  add("daemon_idle_fallback", [petPose](State& s) { petPose(s, MD_HAPPY); });
  add("daemon_look", [petPose](State& s) { petPose(s, MD_CHILL); s.dmn.lookX = 1; s.dmn.lookY = -1; scopy(s.dmn.idle, "JUST VIBING."); });
  for (int k = 1; k <= 3; k++) add("calib_" + std::to_string(k), [k](State& s) { s.screen = SC_SYSTEM; s.sys.focus = true; s.sys.calib = k; });
  for (int r = 0; r < SR_COUNT; r++) add("system_row" + std::to_string(r), [r](State& s) { s.screen = SC_SYSTEM; s.sys.focus = true; s.sys.sel = r; if (r % 2) { s.set.flip = 2; s.set.mute = true; s.set.theme = 1; s.set.sleepMin = 0; } });
  add("system_offline", [](State& s) { s.screen = SC_SYSTEM; s.sys.wifi = false; });
  add("launcher_groups", [](State& s) { s.launcher = true; s.launcherGrp = -1; s.launcherSel = 0; s.pager.unread = 12; });
  add("launcher_groups_sel4", [](State& s) { s.launcher = true; s.launcherGrp = -1; s.launcherSel = 4; });
  add("launcher_sub_data_last", [](State& s) { s.launcher = true; s.launcherGrp = SG_DATA; s.launcherSel = 7; s.pager.unread = 12; s.door.unread = 1; });
  add("launcher_sub_control", [](State& s) { s.launcher = true; s.launcherGrp = SG_CONTROL; s.launcherSel = 0; });
  auto joinBase = [](State& s) {
    s.join.show = true;
    const char* nm[5] = {"CorpNet-Guest-2.4", "HomeNet", "Workshop", "Phone", "Cabin"};
    for (int i = 0; i < 5; i++) scopy(s.join.ssid[i], nm[i]);
  };
  add("join_scanning", [joinBase](State& s) { joinBase(s); s.join.scanning = true; });
  add("join_trying", [joinBase](State& s) {
    joinBase(s); s.join.cur = 1; s.join.tryNo = 2; s.join.tries = 3; s.join.tryAt = s.now - 9000;
    s.join.res[0] = JR_PASS; s.join.res[1] = JR_TRYING; s.join.res[2] = JR_NONE; s.join.res[3] = JR_FAR; s.join.res[4] = JR_FAR;
  });
  add("join_failed_hold", [joinBase](State& s) {
    joinBase(s); s.join.cur = 0; s.join.tryNo = 1; s.join.tries = 3; s.join.res[0] = JR_PASS; s.join.code[0] = 15;
    s.join.res[1] = JR_NONE; s.join.res[2] = JR_NONE; s.join.res[3] = JR_FAR; s.join.res[4] = JR_FAR;
  });
  add("join_failed_long", [joinBase](State& s) { joinBase(s); s.join.cur = 0; s.join.tryNo = 1; s.join.tries = 1; s.join.res[0] = JR_NEEDUSER; s.join.code[0] = 202; });
  add("join_joined", [joinBase](State& s) {
    joinBase(s); s.join.cur = 0; s.join.tryNo = 1; s.join.tries = 1; s.join.res[0] = JR_OK;
    for (int i = 1; i < 5; i++) s.join.res[i] = JR_FAR;
  });
  add("join_one_saved", [](State& s) { s.join.show = true; scopy(s.join.ssid[0], "PingyNet-5G-Upstairs-Extended"); s.join.cur = 0; s.join.tryNo = 1; s.join.tries = 1; s.join.res[0] = JR_TRYING; s.join.tryAt = s.now - 19000; });
  add("toast", [](State& s) { s.screen = SC_PIHOLE; setToast(s, "SWITCHED TO v5"); });
  add("header_stress", [](State& s) { s.screen = SC_POMO; s.sys.wifi = false; s.pager.unread = 2; s.now = 123500; s.lt.tm_hour = 20; s.lt.tm_min = 0; });
  add("header_stress_hosts", [](State& s) { s.screen = SC_HOSTS; s.sys.wifi = false; s.pager.unread = 2; s.now = 123500; });
  // self-update overlay card (all four phases)
  add("ota_offer", [](State& s) { scopy(s.sys.version, "2.1.0"); s.ota.avail = true; scopy(s.ota.ver, "2.2.0"); });
  add("ota_installing", [](State& s) { s.ota.installing = true; s.ota.prog = 62; scopy(s.ota.ver, "2.2.0"); });
  add("ota_done", [](State& s) { s.ota.done = true; scopy(s.ota.ver, "2.2.0"); });
  add("ota_failed", [](State& s) { s.ota.avail = true; scopy(s.ota.ver, "2.2.0"); scopy(s.ota.err, "STALLED"); s.ota.at = s.now; });
}

static void savePPM(Canvas& c, const std::string& path) {
  FILE* f = fopen(path.c_str(), "wb");
  fprintf(f, "P6\n%d %d\n255\n", W, H);
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) {
      uint16_t p = c.readPixelValue(x, y);
      uint16_t v = (uint16_t)((p >> 8) | (p << 8));
      fputc(((v >> 11) & 31) * 255 / 31, f); fputc(((v >> 5) & 63) * 255 / 63, f); fputc((v & 31) * 255 / 31, f);
    }
  fclose(f);
}

int main(int argc, char** argv) {
  std::string out = argc > 1 ? argv[1] : "build/shots";
  mkdir(out.c_str(), 0755);
  defineShots();
  Canvas c;
  c.setColorDepth(16);
  c.createSprite(W, H);
  int fails = 0, warns = 0;
  FILE* rep = fopen((out + "/audit.txt").c_str(), "w");
  for (int theme = 0; theme < THEME_N; theme++) {
    for (auto& sh : shots) {
      State* s = new State();
      baseState(*s);
      s->set.theme = theme;
      sh.setup(*s);
      if (sh.name.rfind("system_row", 0) != 0) s->set.theme = theme;
      ui::audit().clear();
      ui::notes().clear();
      draw::screen(c, *s);
      savePPM(c, out + "/t" + std::to_string(theme) + "_" + sh.name + ".ppm");
      if (theme == 0) {
        for (auto& nt : ui::notes()) { fprintf(rep, "FAIL %-24s %s\n", sh.name.c_str(), nt.c_str()); fails++; }
        auto& A = ui::audit();
        for (size_t i = 0; i < A.size(); i++) {
          auto& a = A[i];
          if (a.role == ui::DECO) { fprintf(rep, "FAIL %-24s below-floor text: \"%s\"\n", sh.name.c_str(), a.s); fails++; }
          if (ui::CAP_PX[a.role] < ui::FLOOR_PX && a.role != ui::DECO) { fprintf(rep, "FAIL %-24s role %d under floor: \"%s\"\n", sh.name.c_str(), a.role, a.s); fails++; }
          if (a.x0 < 0 || a.x1 > W) { fprintf(rep, "FAIL %-24s off-screen x[%d..%d]: \"%s\"\n", sh.name.c_str(), a.x0, a.x1, a.s); fails++; }
          if (a.y0 < 0 || a.y1 > H) { fprintf(rep, "FAIL %-24s off-screen y[%d..%d]: \"%s\"\n", sh.name.c_str(), a.y0, a.y1, a.s); fails++; }
          size_t L = strlen(a.s);
          if (L >= 2 && a.s[L - 1] == '.' && a.s[L - 2] == '.' && !(L >= 3 && a.s[L - 3] == '.')) {
            bool intentional = strstr(a.s, "zzz") || strstr(a.s, "WAIT") || strstr(a.s, "WAITING") || strstr(a.s, "HOLDING") || strstr(a.s, "MIN..") || strstr(a.s, "POWER..") || strstr(a.s, "FADING") || strstr(a.s, "SIGNAL..");
            if (!intentional) { fprintf(rep, "WARN %-24s truncated: \"%s\"\n", sh.name.c_str(), a.s); warns++; }
          }
          for (size_t j = i + 1; j < A.size(); j++) {
            auto& b = A[j];
            int ox = std::min(a.x1, b.x1) - std::max(a.x0, b.x0), oy = std::min(a.y1, b.y1) - std::max(a.y0, b.y0);
            if (ox > 1 && oy > 1 && sh.name.rfind("toast", 0) != 0) { fprintf(rep, "FAIL %-24s overlap: \"%s\" x \"%s\" (%dx%d px)\n", sh.name.c_str(), a.s, b.s, ox, oy); fails++; }
          }
        }
      }
      delete s;
    }
  }
  // boot + setup frames
  int bt[] = {100, 500, 900, 1250, 1500, 2100, 2700, 3150};
  for (int t : bt) { ui::audit().clear(); draw::boot(c, themeOf(0), t, 1234, "1.9.1"); savePPM(c, out + "/t0_boot_" + std::to_string(t) + ".ppm");
    for (auto& a : ui::audit()) if (a.x0 < 0 || a.x1 > W) { fprintf(rep, "FAIL boot_%d off-screen: \"%s\"\n", t, a.s); fails++; } }
  for (int k = 0; k < 2 + JR_N; k++) {
    State s; baseState(s); s.sys.wifiSet = k >= 1;
    if (k >= 2) { scopy(s.join.ssid[0], "CorpNet-Guest-2.4-Floor3"); s.join.res[0] = k - 2; s.join.code[0] = 204; s.now = 123456 - (123456 % 6000) + 3100; }   // the "why" phase
    ui::audit().clear(); ui::notes().clear();
    draw::setupMode(c, s, themeOf(0), "SHIV-SETUP-A1B2"); savePPM(c, out + (k >= 2 ? "/t0_setup_why" + std::to_string(k - 2) + ".ppm" : k ? "/t0_setup_offline.ppm" : "/t0_setup.ppm"));
    for (auto& nt : ui::notes()) { fprintf(rep, "FAIL setup %s\n", nt.c_str()); fails++; }
    auto& A = ui::audit();
    for (size_t i = 0; i < A.size(); i++) {
      if (A[i].x0 < 0 || A[i].x1 > W || A[i].y0 < 0 || A[i].y1 > H) { fprintf(rep, "FAIL setup off-screen: \"%s\"\n", A[i].s); fails++; }
      size_t L = strlen(A[i].s);
      if (L >= 2 && A[i].s[L - 1] == '.' && A[i].s[L - 2] == '.' && !strstr(A[i].s, "CorpNet")) { fprintf(rep, "FAIL setup truncated: \"%s\"\n", A[i].s); fails++; }
      for (size_t j = i + 1; j < A.size(); j++) {
        int ox = std::min(A[i].x1, A[j].x1) - std::max(A[i].x0, A[j].x0), oy = std::min(A[i].y1, A[j].y1) - std::max(A[i].y0, A[j].y0);
        if (ox > 1 && oy > 1) { fprintf(rep, "FAIL setup overlap: \"%s\" x \"%s\"\n", A[i].s, A[j].s); fails++; }
      }
    }
  }
  fprintf(rep, "\n%zu shots x %d themes, %d FAIL, %d WARN\n", shots.size(), THEME_N, fails, warns);
  fclose(rep);
  printf("%zu shots x %d themes, %d FAIL, %d WARN\n", shots.size(), THEME_N, fails, warns);
  return fails ? 1 : 0;
}
