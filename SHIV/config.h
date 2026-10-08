// SHIV - persistent configuration (LittleFS /config.json). Secrets live only here, on the
// device flash - never in the source code or the repo.
#pragma once
#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include "state.h"
#include "theme.h"

namespace shiv {

static const char* const FW_VERSION = "1.0.0";

struct Config {
  char wifiSsid[WIFI_SLOTS][33] = {};
  char wifiPass[WIFI_SLOTS][65] = {};
  char wifiUser[WIFI_SLOTS][65] = {};     // only for work / school networks that log in with a username
  char webPass[33] = "";              // web panel password (user: shiv). Empty = open until set.
  char apiKey[25] = "";               // for curl/pager API
  char hostName[16] = "shiv";
  char tz[48] = "EST5EDT,M3.2.0,M11.1.0";   // POSIX TZ string for your zone
  float lat = 40.0f, lon = -75.0f;    // your latitude / longitude for the weather screen
  char place[24] = "Your City";
  char pi5Url[64] = "http://<pi-hole-v5-ip>:81";   // Pi-hole v5 base URL (empty = off)
  char pi5Token[80] = "";
  char pi6Url[64] = "http://<pi-hole-v6-ip>";      // Pi-hole v6 base URL (empty = off)
  char pi6Pass[64] = "";
  char haUrl[64] = "http://<home-assistant-ip>:8123";   // Home Assistant base URL (empty = off)
  char haToken[220] = "";             // HA long-lived access token
  char shopDomain[64] = "";           // your-store.myshopify.com (empty = off)
  char shopToken[80] = "";            // static Admin API token, OR leave empty and use id+secret
  char shopClientId[48] = "";
  char shopClientSecret[80] = "";
  char shopApiVer[10] = "2025-10";
  char jobsUrl[96] = "";              // optional JSON feed for the JOBS ticker (empty = off)
  char otaUrl[96] = "http://<your-server>/shiv/latest.json";   // self-update manifest (empty = off)
  char mqttHost[40] = "";             // MQTT broker, e.g. 192.168.1.10. Empty = MQTT off.
  uint16_t mqttPort = 1883;
  char mqttUser[32] = "";
  char mqttPass[48] = "";             // secret
  char mqttTopics[96] = "#";          // comma-separated subscribe list
};

struct SpiRamAllocator : ArduinoJson::Allocator {
  void* allocate(size_t size) override {
    void* p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    return p ? p : malloc(size);
  }
  void deallocate(void* p) override { heap_caps_free(p); }
  void* reallocate(void* p, size_t n) override {
    void* q = heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM);
    return q ? q : realloc(p, n);
  }
};
inline SpiRamAllocator* psAlloc() { static SpiRamAllocator a; return &a; }

extern State G;
extern Config CFG;
extern volatile bool gConfigDirty;
extern float gCalU[3], gCalO[3];       // motion calibration (see hw.h)

template <size_t N> inline void jget(JsonVariantConst v, char (&dst)[N]) {
  if (v.is<const char*>()) scopy(dst, v.as<const char*>());
}

inline void configToJson(JsonDocument& d, bool withSecrets) {
  JsonArray w = d["wifi"].to<JsonArray>();
  for (int i = 0; i < WIFI_SLOTS; i++) {
    JsonObject o = w.add<JsonObject>();
    o["ssid"] = CFG.wifiSsid[i];
    if (CFG.wifiUser[i][0]) o["user"] = CFG.wifiUser[i];
    if (withSecrets) o["pass"] = CFG.wifiPass[i]; else o["passSet"] = CFG.wifiPass[i][0] != 0;
  }
  d["host"] = CFG.hostName; d["tz"] = CFG.tz; d["lat"] = CFG.lat; d["lon"] = CFG.lon; d["place"] = CFG.place;
  d["pi5Url"] = CFG.pi5Url; d["pi6Url"] = CFG.pi6Url; d["haUrl"] = CFG.haUrl;
  d["shopDomain"] = CFG.shopDomain; d["shopClientId"] = CFG.shopClientId; d["shopApiVer"] = CFG.shopApiVer; d["jobsUrl"] = CFG.jobsUrl; d["otaUrl"] = CFG.otaUrl;
  d["mqttHost"] = CFG.mqttHost; d["mqttPort"] = CFG.mqttPort; d["mqttUser"] = CFG.mqttUser; d["mqttTopics"] = CFG.mqttTopics;
  d["apiKey"] = CFG.apiKey;
  if (withSecrets) {
    d["webPass"] = CFG.webPass; d["pi5Token"] = CFG.pi5Token; d["pi6Pass"] = CFG.pi6Pass; d["haToken"] = CFG.haToken;
    d["shopToken"] = CFG.shopToken; d["shopClientSecret"] = CFG.shopClientSecret; d["mqttPass"] = CFG.mqttPass;
  } else {       // the web panel only ever learns whether a secret is set
    d["webPassSet"] = CFG.webPass[0] != 0; d["pi5TokenSet"] = CFG.pi5Token[0] != 0; d["pi6PassSet"] = CFG.pi6Pass[0] != 0;
    d["haTokenSet"] = CFG.haToken[0] != 0; d["shopTokenSet"] = CFG.shopToken[0] != 0; d["shopClientSecretSet"] = CFG.shopClientSecret[0] != 0;
    d["mqttPassSet"] = CFG.mqttPass[0] != 0;
  }
  const Settings& s = G.set;
  JsonObject so = d["set"].to<JsonObject>();
  so["theme"] = s.theme; so["volume"] = s.volume; so["mute"] = s.mute; so["brightness"] = s.brightness; so["flip"] = s.flip;
  JsonArray cal = d["cal"].to<JsonArray>();
  for (int i = 0; i < 3; i++) cal.add(gCalU[i]);
  for (int i = 0; i < 3; i++) cal.add(gCalO[i]);
  so["dimS"] = s.dimS; so["offS"] = s.offS; so["sleepMin"] = s.sleepMin; so["clockFace"] = s.clockFace; so["screenMask"] = s.screenMask;
  so["knownScreens"] = (int)SC_COUNT;   // screens this build knows about, so an upgrade can auto-enable new ones
  so["piTarget"] = s.piTarget; so["piOffHours"] = s.piOffHours; so["piOffUntil"] = s.piOffUntil; so["cfgVer"] = 1;
  so["micOn"] = s.micOn; so["micSens"] = s.micSens; so["spectrumIdle"] = s.spectrumIdle; so["petHears"] = s.petHears;
  so["otaOn"] = s.otaOn;
  JsonArray fo = so["favOrder"].to<JsonArray>();
  for (int i = 0; i < s.favN; i++) fo.add(s.fav[i]);
  so["piSource"] = G.pi.source; so["focusMin"] = G.pomo.focusMin; so["breakMin"] = G.pomo.breakMin;
  JsonArray hs = d["hosts"].to<JsonArray>();
  for (int i = 0; i < G.hosts.n; i++) { JsonObject o = hs.add<JsonObject>(); o["name"] = G.hosts.h[i].name; o["addr"] = G.hosts.h[i].addr; o["port"] = G.hosts.h[i].port; }
  JsonArray fs = d["favs"].to<JsonArray>();
  for (int i = 0; i < G.home.n; i++) { JsonObject o = fs.add<JsonObject>(); o["label"] = G.home.f[i].label; o["entity"] = G.home.f[i].entity; }
  JsonArray ms = d["macros"].to<JsonArray>();
  for (int i = 0; i < G.macros.n; i++) {
    JsonObject o = ms.add<JsonObject>();
    o["label"] = G.macros.m[i].label; o["url"] = G.macros.m[i].url; o["method"] = G.macros.m[i].method; o["body"] = G.macros.m[i].body;
  }
  { JsonArray tn = d["themes"].to<JsonArray>();
    for (int i = 0; i < THEME_N; i++) { const Theme& th = THEMES[i]; JsonObject o = tn.add<JsonObject>(); o["name"] = th.name;
      const Col cols[] = {th.bg, th.panel, th.fg, th.dim, th.acc, th.acc2, th.acc3, th.warn, th.bad, th.ok}; JsonArray ca = o["c"].to<JsonArray>(); for (Col cc : cols) ca.add((uint32_t)cc); } }
  JsonObject sc = d["scores"].to<JsonObject>();
  sc["run"] = G.games.run.best; sc["snake"] = G.games.snake.best; sc["reflex"] = G.games.reflex.bestMs; sc["pokes"] = G.dmn.pokes;
  JsonObject pt = d["pet"].to<JsonObject>();
  pt["food"] = G.dmn.food; pt["fun"] = G.dmn.fun; pt["fed"] = G.dmn.fed; pt["played"] = G.dmn.played;
  pt["at"] = G.dmn.statEpoch; pt["seen"] = G.dmn.seenEpoch; pt["n"] = G.dmn.ageN; pt["born"] = G.dmn.bornEpoch;
}

// Apply a (possibly partial) JSON object. Secret fields are only overwritten when present and non-empty,
// or when explicitly cleared with the value "-".
inline void secretSet(JsonVariantConst v, char* dst, size_t n) {
  if (!v.is<const char*>()) return;
  const char* s = v.as<const char*>();
  if (!s[0]) return;
  if (!strcmp(s, "-")) { dst[0] = 0; return; }
  strncpy(dst, s, n - 1); dst[n - 1] = 0;
}

inline void defaultFavs() {
  static const uint8_t F[] = {SC_CLOCK, SC_TVOFF, SC_PAGER, SC_WEATHER, SC_SYSTEM};
  G.set.favN = 0;
  for (uint8_t id : F) G.set.fav[G.set.favN++] = id;
}

inline void wifiCompact() { wifilist::compact(CFG.wifiSsid, CFG.wifiPass, CFG.wifiUser, WIFI_SLOTS); }
inline void wifiRemember(const char* ssid, const char* pass, const char* user) { wifilist::remember(CFG.wifiSsid, CFG.wifiPass, CFG.wifiUser, WIFI_SLOTS, ssid, pass, user); }

inline void configFromJson(JsonDocument& d, bool migrate = false) {
  if (d["wifi"].is<JsonArrayConst>()) {
    static char oldSsid[WIFI_SLOTS][33], oldPass[WIFI_SLOTS][65], oldUser[WIFI_SLOTS][65];
    memcpy(oldSsid, CFG.wifiSsid, sizeof(oldSsid)); memcpy(oldPass, CFG.wifiPass, sizeof(oldPass)); memcpy(oldUser, CFG.wifiUser, sizeof(oldUser));
    int i = 0;
    for (JsonObjectConst o : d["wifi"].as<JsonArrayConst>()) {
      if (i >= WIFI_SLOTS) break;
      const char* ns = o["ssid"] | (const char*)nullptr;
      if (ns && (ns[0] || i > 0)) scopy(CFG.wifiSsid[i], ns);   // slot 1 can only be replaced, never blanked
      const char* pw = o["pass"] | "";
      if (!strcmp(pw, "-")) CFG.wifiPass[i][0] = 0;
      else if (pw[0]) scopy(CFG.wifiPass[i], pw);
      else scopy(CFG.wifiPass[i], wifilist::savedOf(oldSsid, oldPass, WIFI_SLOTS, CFG.wifiSsid[i]));   // by name, not by row
      if (o["user"].is<const char*>()) scopy(CFG.wifiUser[i], o["user"].as<const char*>());
      else scopy(CFG.wifiUser[i], wifilist::savedOf(oldSsid, oldUser, WIFI_SLOTS, CFG.wifiSsid[i]));
      if (!CFG.wifiSsid[i][0]) CFG.wifiPass[i][0] = 0;
      i++;
    }
    wifiCompact();
  }
  jget(d["host"], CFG.hostName); jget(d["tz"], CFG.tz); jget(d["place"], CFG.place);
  if (d["lat"].is<float>()) CFG.lat = d["lat"]; if (d["lon"].is<float>()) CFG.lon = d["lon"];
  jget(d["pi5Url"], CFG.pi5Url); jget(d["pi6Url"], CFG.pi6Url); jget(d["haUrl"], CFG.haUrl);
  jget(d["shopDomain"], CFG.shopDomain); jget(d["shopClientId"], CFG.shopClientId); jget(d["shopApiVer"], CFG.shopApiVer); jget(d["jobsUrl"], CFG.jobsUrl); jget(d["otaUrl"], CFG.otaUrl);
  jget(d["mqttHost"], CFG.mqttHost); if (d["mqttPort"].is<int>()) CFG.mqttPort = (uint16_t)(int)d["mqttPort"];
  jget(d["mqttUser"], CFG.mqttUser); jget(d["mqttTopics"], CFG.mqttTopics);
  jget(d["apiKey"], CFG.apiKey);
  secretSet(d["webPass"], CFG.webPass, sizeof(CFG.webPass)); secretSet(d["pi5Token"], CFG.pi5Token, sizeof(CFG.pi5Token));
  secretSet(d["pi6Pass"], CFG.pi6Pass, sizeof(CFG.pi6Pass)); secretSet(d["haToken"], CFG.haToken, sizeof(CFG.haToken));
  secretSet(d["shopToken"], CFG.shopToken, sizeof(CFG.shopToken)); secretSet(d["shopClientSecret"], CFG.shopClientSecret, sizeof(CFG.shopClientSecret));
  secretSet(d["mqttPass"], CFG.mqttPass, sizeof(CFG.mqttPass));
  if (!CFG.hostName[0]) scopy(CFG.hostName, "shiv");

  JsonObjectConst so = d["set"];
  if (!so.isNull()) {
    Settings& s = G.set;
    s.theme = so["theme"] | s.theme; s.volume = so["volume"] | s.volume; s.mute = so["mute"] | s.mute;
    s.brightness = so["brightness"] | s.brightness; s.flip = so["flip"] | s.flip;
    s.dimS = so["dimS"] | s.dimS; s.offS = so["offS"] | s.offS; s.sleepMin = so["sleepMin"] | s.sleepMin;
    s.clockFace = so["clockFace"] | s.clockFace; s.screenMask = so["screenMask"] | s.screenMask;
    if (migrate) {   // on boot after an upgrade, turn ON any screen this build added since the config was saved
      int known = so["knownScreens"] | 22;   // configs older than this field pre-date DOORBELL (22 screens)
      if (known < SC_COUNT) {
        for (int id = known; id < SC_COUNT; id++) s.screenMask |= (1u << id);
        gConfigDirty = true;                 // re-save so knownScreens catches up and this runs only once
      }
    }
    s.piTarget = (so["piTarget"] | (int)s.piTarget) == 6 ? 6 : 5;
    s.piOffHours = (uint8_t)constrain((int)(so["piOffHours"] | (int)s.piOffHours), 1, 24);
    if (so["piOffUntil"].is<uint32_t>()) s.piOffUntil = so["piOffUntil"];
    if (s.theme >= THEME_N) s.theme = 0;
    if (s.brightness < 20) s.brightness = 20;
    if (s.dimS < 5) s.dimS = 5;
    if (s.offS <= s.dimS) s.offS = s.dimS + 10;
    G.pi.source = (so["piSource"] | (int)G.pi.source) == 5 ? 5 : 6;
    G.pomo.focusMin = so["focusMin"] | G.pomo.focusMin; G.pomo.breakMin = so["breakMin"] | G.pomo.breakMin;
    if (G.pomo.focusMin < 1) G.pomo.focusMin = 25;
    if (G.pomo.breakMin < 1) G.pomo.breakMin = 5;
    if (G.pomo.phase == 0) G.pomo.totalMs = G.pomo.remainMs = (uint32_t)G.pomo.focusMin * 60000UL;
    if (so["micOn"].is<bool>()) s.micOn = so["micOn"];
    if (so["micSens"].is<int>()) s.micSens = (uint8_t)constrain((int)so["micSens"], 1, 9);
    if (so["spectrumIdle"].is<bool>()) s.spectrumIdle = so["spectrumIdle"];
    if (so["petHears"].is<bool>()) s.petHears = so["petHears"];
    if (so["otaOn"].is<bool>()) s.otaOn = so["otaOn"];
    if (so["favOrder"].is<JsonArrayConst>()) {
      s.favN = 0;
      for (JsonVariantConst v : so["favOrder"].as<JsonArrayConst>()) {
        int id = v | -1;
        if (id < 0 || id >= SC_COUNT || s.favN >= MAX_FAV_SCREENS) continue;
        bool dup = false;
        for (int i = 0; i < s.favN; i++) if (s.fav[i] == id) dup = true;
        if (!dup) s.fav[s.favN++] = (uint8_t)id;
      }
    } else if (migrate) { defaultFavs(); gConfigDirty = true; }
  }
  if (d["hosts"].is<JsonArrayConst>()) {
    G.hosts.n = 0;
    for (JsonObjectConst o : d["hosts"].as<JsonArrayConst>()) {
      if (G.hosts.n >= MAX_HOSTS) break;
      Host& h = G.hosts.h[G.hosts.n];
      h = Host();
      jget(o["name"], h.name); jget(o["addr"], h.addr); h.port = o["port"] | 80;
      if (h.name[0] && h.addr[0]) G.hosts.n++;
    }
    G.hosts.page = 0;
  }
  if (d["favs"].is<JsonArrayConst>()) {
    G.home.n = 0; G.home.sel = 0; G.home.focus = false;
    G.hab.level = 0; G.hab.cat = 0; G.hab.n = 0;
    for (JsonObjectConst o : d["favs"].as<JsonArrayConst>()) {
      if (G.home.n >= MAX_FAVS) break;
      Fav& f = G.home.f[G.home.n];
      f = Fav();
      jget(o["label"], f.label); jget(o["entity"], f.entity);
      if (f.label[0] && f.entity[0]) G.home.n++;
    }
  }
  if (d["macros"].is<JsonArrayConst>()) {
    G.macros.n = 0; G.macros.sel = 0; G.macros.focus = false; G.macros.holdStart = 0;
    for (JsonObjectConst o : d["macros"].as<JsonArrayConst>()) {
      if (G.macros.n >= MAX_MACROS) break;
      Macro& m = G.macros.m[G.macros.n];
      m = Macro();
      jget(o["label"], m.label); jget(o["url"], m.url); jget(o["method"], m.method); jget(o["body"], m.body);
      if (!m.method[0]) scopy(m.method, "GET");
      if (m.label[0] && m.url[0]) G.macros.n++;
    }
  }
  if (d["cal"].is<JsonArrayConst>() && d["cal"].size() == 6) {
    for (int i = 0; i < 3; i++) { gCalU[i] = d["cal"][i]; gCalO[i] = d["cal"][i + 3]; }
  }
  JsonObjectConst sc = d["scores"];
  if (!sc.isNull()) {
    G.games.run.best = sc["run"] | 0u; G.games.snake.best = sc["snake"] | 0u; G.games.reflex.bestMs = sc["reflex"] | 0u; G.dmn.pokes = sc["pokes"] | 0u;
  }
  JsonObjectConst pt = d["pet"];
  if (!pt.isNull()) {
    Daemon& p = G.dmn;
    p.food = (uint8_t)constrain((int)(pt["food"] | (int)p.food), 0, 100); p.fun = (uint8_t)constrain((int)(pt["fun"] | (int)p.fun), 0, 100);
    p.fed = pt["fed"] | p.fed; p.played = pt["played"] | p.played;
    p.statEpoch = pt["at"] | p.statEpoch; p.seenEpoch = pt["seen"] | p.seenEpoch; p.bornEpoch = pt["born"] | p.bornEpoch;
    p.ageN = (uint8_t)((pt["n"] | (int)p.ageN) % 6);
  }
}

inline void defaultHosts() {
  // Example rows so the HOSTS screen isn't empty on first boot. Edit them on the web panel.
  static const char* const D[][3] = {
    {"ROUTER", "192.168.1.1", "80"}, {"NAS", "192.168.1.10", "445"}, {"PI-HOLE", "192.168.1.20", "80"},
  };
  G.hosts.n = 0;
  for (auto& r : D) { Host& h = G.hosts.h[G.hosts.n++]; scopy(h.name, r[0]); scopy(h.addr, r[1]); h.port = atoi(r[2]); }
}

inline bool configLoadFrom(const char* path) {
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  JsonDocument d(psAlloc());
  DeserializationError e = deserializeJson(d, f);
  f.close();
  if (e || !d["wifi"].is<JsonArrayConst>()) return false;
  configFromJson(d, true);   // boot load: allow the new-screen migration
  return true;
}

// config.tmp is the fallback: a reset between "remove" and "rename" in configSave leaves only that file.
inline bool configLoad() {
  if (configLoadFrom("/config.json") || configLoadFrom("/config.tmp")) return true;
  defaultHosts(); defaultFavs();
  return false;
}

inline bool configSave() {
  JsonDocument d(psAlloc());
  configToJson(d, true);
  File f = LittleFS.open("/config.tmp", "w");
  if (!f) return false;
  serializeJson(d, f);
  f.close();
  LittleFS.remove("/config.json");
  LittleFS.rename("/config.tmp", "/config.json");
  gConfigDirty = false;
  return true;
}

inline void markDirty() { gConfigDirty = true; }

}  // namespace shiv
