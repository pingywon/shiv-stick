// SHIV - background data task (core 0). All network I/O that could block lives here so the
// UI on core 1 stays smooth. Results are copied into G under gMux.
#pragma once
#include <HTTPClient.h>
#include <NetworkClient.h>
#include <NetworkClientSecure.h>
#include <Update.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <PubSubClient.h>
#include <memory>
#include <esp_random.h>
#include "config.h"
#include "net.h"
#include "certs.h"

namespace shiv {

extern SemaphoreHandle_t gMux;
struct Lock {
  Lock() { xSemaphoreTakeRecursive(gMux, portMAX_DELAY); }
  ~Lock() { xSemaphoreGiveRecursive(gMux); }
};

namespace fetch {

enum ActType : uint8_t { A_FAV, A_MACRO, A_REFRESH, A_BLE, A_HOSTS, A_PIHOLE, A_HA_LIST, A_HA_ENT };
struct Act { uint8_t type; uint8_t arg; };
static QueueHandle_t q = nullptr;
static volatile bool paused = false;
static volatile bool busy = false;

inline bool request(ActType t, uint8_t arg = 0) {
  if (!q) return false;
  Act a = {(uint8_t)t, arg};
  return xQueueSend(q, &a, 0) == pdTRUE;
}

// ---------------------------------------------------------------- HTTP helper
struct Hdr { const char* k; const char* v; };

inline int httpDo(const char* method, const String& url, const String& body, const Hdr* hdrs, int nh, String& out,
                  bool insecureTls = false, uint16_t timeoutMs = 7000) {
  out = "";
  if (!net::online()) return -100;
  bool https = url.startsWith("https://");
  NetworkClient plain;
  std::unique_ptr<NetworkClientSecure> sec;      // declared before http so it is destroyed AFTER it
  HTTPClient http;
  http.setReuse(false);
  http.setConnectTimeout(4000);
  http.setTimeout(timeoutMs);
  bool okb;
  if (https) {
    sec.reset(new NetworkClientSecure());
    if (insecureTls) sec->setInsecure(); else sec->setCACert(ROOT_CAS);
    okb = http.begin(*sec, url);
  } else okb = http.begin(plain, url);
  if (!okb) return -101;
  for (int i = 0; i < nh; i++) http.addHeader(hdrs[i].k, hdrs[i].v);
  int code = body.length() || strcmp(method, "GET") ? http.sendRequest(method, body) : http.GET();
  if (code > 0) out = http.getString();
  http.end();
  return code;
}

inline void errText(int code, char* out, size_t n) {
  if (code == -100) snprintf(out, n, "NO WIFI");
  else if (code == 401 || code == 403) snprintf(out, n, "BAD LOGIN");
  else if (code == 404) snprintf(out, n, "NOT FOUND");
  else if (code < 0) snprintf(out, n, "NO ANSWER");
  else snprintf(out, n, "HTTP %d", code);
}

// ---------------------------------------------------------------- weather (open-meteo, no key, plain HTTP)
inline void weather() {
  char url[320];
  snprintf(url, sizeof(url),
           "http://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f&current=temperature_2m,relative_humidity_2m,weather_code,is_day,wind_speed_10m"
           "&daily=temperature_2m_max,temperature_2m_min&temperature_unit=fahrenheit&wind_speed_unit=mph&timezone=auto&forecast_days=1",
           CFG.lat, CFG.lon);
  String body;
  int code = httpDo("GET", url, "", nullptr, 0, body);
  JsonDocument d(psAlloc());
  if (code == 200 && !deserializeJson(d, body) && d["current"]["temperature_2m"].is<float>()) {
    Lock l;
    Weather& w = G.wx;
    w.tempF = d["current"]["temperature_2m"]; w.code = d["current"]["weather_code"] | 0; w.isDay = (d["current"]["is_day"] | 1) == 1;
    w.humidity = d["current"]["relative_humidity_2m"] | 0; w.windMph = d["current"]["wind_speed_10m"] | 0.0f;
    w.hiF = d["daily"]["temperature_2m_max"][0] | w.tempF; w.loF = d["daily"]["temperature_2m_min"][0] | w.tempF;
    w.at = millis(); w.err[0] = 0;
  } else {
    Lock l;
    errText(code == 200 ? -1 : code, G.wx.err, sizeof(G.wx.err));
  }
}

// ---------------------------------------------------------------- pi-hole
static char piSid[72] = "";
static volatile bool piSetWant = true;
static volatile uint32_t piSetAt = 0, piForceAt = 0;   // on/off button bookkeeping (see piholeSet)

inline int pi6Auth() {
  String body, out;
  JsonDocument b;
  b["password"] = CFG.pi6Pass;
  serializeJson(b, body);
  Hdr h[] = {{"Content-Type", "application/json"}};
  int code = httpDo("POST", String(CFG.pi6Url) + "/api/auth", body, h, 1, out);
  JsonDocument d;
  if (code == 200 && !deserializeJson(d, out) && d["session"]["valid"] == true) {
    scopy(piSid, d["session"]["sid"] | "");
    return 200;
  }
  piSid[0] = 0;
  return code == 200 ? 401 : code;
}

inline void pihole() {
  uint8_t src = G.pi.source;
  uint32_t v6Timer = 0;
  uint32_t queries = 0, blocked = 0;
  float pct = 0;
  bool enabled = true;
  int code;
  String out;
  if (src == 5) {
    String url = String(CFG.pi5Url) + "/admin/api.php?summaryRaw&auth=" + CFG.pi5Token;
    code = httpDo("GET", url, "", nullptr, 0, out);
    JsonDocument d;
    if (code == 200) {
      if (!deserializeJson(d, out) && d["dns_queries_today"].is<uint32_t>()) {
        queries = d["dns_queries_today"]; blocked = d["ads_blocked_today"]; pct = d["ads_percentage_today"] | 0.0f;
        enabled = strcmp(d["status"] | "enabled", "enabled") == 0;
      } else code = CFG.pi5Token[0] ? -1 : 401;
    }
  } else {
    code = 0;
    for (int attempt = 0; attempt < 2; attempt++) {
      if (!piSid[0] && CFG.pi6Pass[0]) { code = pi6Auth(); if (code != 200) break; }
      Hdr h[] = {{"X-FTL-SID", piSid}};
      code = httpDo("GET", String(CFG.pi6Url) + "/api/stats/summary", "", h, piSid[0] ? 1 : 0, out);
      if (code == 401 && CFG.pi6Pass[0]) { piSid[0] = 0; continue; }
      break;
    }
    JsonDocument d;
    if (code == 200) {
      if (!deserializeJson(d, out) && d["queries"]["total"].is<uint32_t>()) {
        queries = d["queries"]["total"]; blocked = d["queries"]["blocked"]; pct = d["queries"]["percent_blocked"] | 0.0f;
        String o2;
        Hdr h[] = {{"X-FTL-SID", piSid}};
        if (httpDo("GET", String(CFG.pi6Url) + "/api/dns/blocking", "", h, piSid[0] ? 1 : 0, o2) == 200) {
          JsonDocument b;
          if (!deserializeJson(b, o2)) {
            enabled = strcmp(b["blocking"] | "enabled", "enabled") == 0;
            v6Timer = b["timer"].is<float>() ? (uint32_t)b["timer"].as<float>() : 0;
          }
        }
      } else code = -1;
    }
  }
  Lock l;
  Pihole& p = G.pi;
  if (p.source != src) return;          // source swapped mid-fetch
  if (code == 200) {
    p.queries = queries; p.blocked = blocked; p.pct = pct; p.enabled = enabled; p.err[0] = 0;
    bool settling = piSetAt && ago(millis(), piSetAt) < 3000 && src == G.set.piTarget;   // ignore a status read that raced our own on/off
    if (settling) p.enabled = piSetWant;
    else if (src == G.set.piTarget) {
      if (enabled) G.set.piOffUntil = 0;
      else if (src == 6 && v6Timer && G.sys.timeValid) G.set.piOffUntil = (uint32_t)time(nullptr) + v6Timer;
    }
    static uint32_t lastHist = 0;
    uint32_t now = millis();
    if (!p.histN || now - lastHist > 300000UL) {
      lastHist = now;
      if (p.histN < 24) p.hist[p.histN++] = (uint8_t)constrain((int)pct, 0, 100);
      else { memmove(p.hist, p.hist + 1, 23); p.hist[23] = (uint8_t)constrain((int)pct, 0, 100); }
    }
    p.at = now;
  } else {
    if (code == 401 || code == 403) snprintf(p.err, sizeof(p.err), src == 5 ? "BAD TOKEN" : "BAD PASSWORD");
    else errText(code, p.err, sizeof(p.err));
  }
}

// ---------------------------------------------------------------- pi-hole on/off (the button)
// Pi-hole's own disable timer does the "back on after N hours", so it still happens if the stick
// sleeps, leaves Wi-Fi or runs flat. mode: 0 = toggle, 1 = enable, 2 = disable.
static volatile bool piForce = false;
inline void piholeSet(uint8_t mode) {
  uint8_t target = G.set.piTarget == 6 ? 6 : 5;
  int hours = constrain((int)G.set.piOffHours, 1, 24);
  uint32_t secs = (uint32_t)hours * 3600UL;
  bool enabledNow = true, okb = false, wantEnable = (mode == 1);
  int code = -1;
  String out;
  if (target == 5) {
    String base = String(CFG.pi5Url) + "/admin/api.php?", auth = String("&auth=") + CFG.pi5Token;
    code = 200;
    if (mode == 0) {
      code = httpDo("GET", base + "status" + auth, "", nullptr, 0, out);
      JsonDocument d;
      if (code == 200 && !deserializeJson(d, out) && d["status"].is<const char*>()) enabledNow = strcmp(d["status"] | "", "enabled") == 0;
      else if (code == 200) code = 401;                    // v5 answers [] when the token is wrong
      wantEnable = !enabledNow;
    }
    if (code == 200) {
      code = httpDo("GET", base + (wantEnable ? String("enable") : String("disable=") + secs) + auth, "", nullptr, 0, out);
      JsonDocument d;
      if (code == 200 && !deserializeJson(d, out) && d["status"].is<const char*>())
        okb = (strcmp(d["status"] | "", "enabled") == 0) == wantEnable;
      else if (code == 200) code = 401;
    }
  } else {
    for (int attempt = 0; attempt < 2; attempt++) {
      if (!piSid[0] && CFG.pi6Pass[0]) { code = pi6Auth(); if (code != 200) break; }
      Hdr h[] = {{"X-FTL-SID", piSid}, {"Content-Type", "application/json"}};
      int nh = piSid[0] ? 2 : 0;
      code = 200;
      if (mode == 0) {
        code = httpDo("GET", String(CFG.pi6Url) + "/api/dns/blocking", "", h, piSid[0] ? 1 : 0, out);
        if (code == 401 && CFG.pi6Pass[0]) { piSid[0] = 0; continue; }
        JsonDocument d;
        if (code == 200 && !deserializeJson(d, out)) enabledNow = strcmp(d["blocking"] | "enabled", "enabled") == 0;
        wantEnable = !enabledNow;
      }
      if (code != 200) break;
      String body = wantEnable ? String("{\"blocking\":true,\"timer\":null}") : String("{\"blocking\":false,\"timer\":") + secs + "}";
      Hdr hj[] = {{"Content-Type", "application/json"}, {"X-FTL-SID", piSid}};
      code = httpDo("POST", String(CFG.pi6Url) + "/api/dns/blocking", body, hj, piSid[0] ? 2 : 1, out);
      (void)nh;
      if (code == 401 && CFG.pi6Pass[0]) { piSid[0] = 0; continue; }
      JsonDocument d;
      if (code == 200 && !deserializeJson(d, out)) okb = (strcmp(d["blocking"] | "", "enabled") == 0) == wantEnable;
      break;
    }
  }
  {
    Lock l;
    G.pi.toggling = false;
    char t[22];
    if (okb) {
      if (G.pi.source == target) G.pi.enabled = wantEnable;
      G.set.piOffUntil = (!wantEnable && G.sys.timeValid) ? (uint32_t)time(nullptr) + secs : 0;
      gConfigDirty = true;
      if (wantEnable) snprintf(t, sizeof(t), "PI-HOLE ON"); else snprintf(t, sizeof(t), "PI-HOLE OFF %dH", hours);
    } else if (code == 401 || code == 403) snprintf(t, sizeof(t), target == 5 ? "BAD v5 TOKEN" : "BAD v6 PASS");
    else if (code == -100) snprintf(t, sizeof(t), "NO WIFI");
    else snprintf(t, sizeof(t), "PI-HOLE: FAILED");
    setToast(G, t);
  }
  if (okb) piSetWant = wantEnable;
  piSetAt = okb ? millis() : 0;                    // Pi-hole's status lags a set by up to ~1 s (measured): re-read after 3 s
  piForceAt = millis() + 3000;
}

// ---------------------------------------------------------------- LAN hosts (TCP connect)
inline void hosts() {
  Host copy[MAX_HOSTS];
  int n;
  { Lock l; n = G.hosts.n; memcpy((void*)copy, (void*)G.hosts.h, sizeof(copy)); }
  for (int i = 0; i < n; i++) {
    NetworkClient c;
    uint32_t t0 = millis();
    bool up = net::online() && c.connect(copy[i].addr, copy[i].port, 900);
    uint32_t ms = millis() - t0;
    c.stop();
    Lock l;
    if (i < G.hosts.n && !strcmp(G.hosts.h[i].addr, copy[i].addr)) { G.hosts.h[i].up = up ? 1 : 0; G.hosts.h[i].ms = ms; }
  }
  Lock l;
  G.hosts.at = millis();
}

// ---------------------------------------------------------------- shopify (orders since local midnight)
static char shopTok[80] = "";
static uint32_t shopTokAt = 0;
static volatile bool shopRetry = false;

inline bool shopConfigured() { return CFG.shopDomain[0] && (CFG.shopToken[0] || (CFG.shopClientId[0] && CFG.shopClientSecret[0])); }

inline int shopToken() {
  if (CFG.shopToken[0]) { scopy(shopTok, CFG.shopToken); return 200; }
  if (shopTok[0] && millis() - shopTokAt < 20UL * 3600000UL) return 200;
  String body = String("grant_type=client_credentials&client_id=") + CFG.shopClientId + "&client_secret=" + CFG.shopClientSecret;
  Hdr h[] = {{"Content-Type", "application/x-www-form-urlencoded"}};
  String out;
  int code = httpDo("POST", String("https://") + CFG.shopDomain + "/admin/oauth/access_token", body, h, 1, out, false, 10000);
  JsonDocument d;
  if (code == 200 && !deserializeJson(d, out) && d["access_token"].is<const char*>()) {
    scopy(shopTok, d["access_token"].as<const char*>());
    shopTokAt = millis();
    return 200;
  }
  shopTok[0] = 0;
  return code == 200 ? 401 : code;
}

inline void shop() {
  if (!shopConfigured()) { Lock l; snprintf(G.shop.err, sizeof(G.shop.err), "NO TOKEN SET"); return; }
  if (!G.sys.timeValid) { Lock l; snprintf(G.shop.err, sizeof(G.shop.err), "NO CLOCK YET"); shopRetry = true; return; }
  int code = shopToken();
  String out;
  if (code == 200) {
    time_t now = time(nullptr);
    struct tm lt;
    localtime_r(&now, &lt);
    lt.tm_hour = 0; lt.tm_min = 0; lt.tm_sec = 0;
    lt.tm_isdst = -1;                              // let mktime decide: correct on DST-change days
    time_t mid = mktime(&lt);
    struct tm ut;
    gmtime_r(&mid, &ut);
    char iso[24];
    strftime(iso, sizeof(iso), "%Y-%m-%dT%H:%M:%SZ", &ut);
    String body = String("{\"query\":\"{ orders(first: 100, query: \\\"created_at:>=") + iso +
                  "\\\") { nodes { cancelledAt displayFulfillmentStatus currentTotalPriceSet { shopMoney { amount } } } } }\"}";
    Hdr h[] = {{"Content-Type", "application/json"}, {"X-Shopify-Access-Token", shopTok}};
    code = httpDo("POST", String("https://") + CFG.shopDomain + "/admin/api/" + CFG.shopApiVer + "/graphql.json", body, h, 2, out, false, 12000);
    if (code == 401) shopTok[0] = 0;
  }
  JsonDocument d(psAlloc());
  if (code == 200 && !deserializeJson(d, out) && d["data"]["orders"]["nodes"].is<JsonArrayConst>()) {
    int orders = 0, unf = 0;
    float rev = 0;
    JsonArrayConst nodes = d["data"]["orders"]["nodes"].as<JsonArrayConst>();
    for (JsonObjectConst o : nodes) {
      if (!o["cancelledAt"].isNull()) continue;
      orders++;
      rev += atof(o["currentTotalPriceSet"]["shopMoney"]["amount"] | "0");
      if (strcmp(o["displayFulfillmentStatus"] | "", "FULFILLED") != 0) unf++;
    }
    Lock l;
    G.shop.orders = orders; G.shop.revenue = rev; G.shop.unfulfilled = unf; G.shop.at = millis(); G.shop.err[0] = 0;
  } else {
    Lock l;
    if (code == 200) snprintf(G.shop.err, sizeof(G.shop.err), "BAD REPLY");
    else if (code == 401 || code == 403) snprintf(G.shop.err, sizeof(G.shop.err), "BAD TOKEN");
    else errText(code, G.shop.err, sizeof(G.shop.err));
  }
}

// ---------------------------------------------------------------- claude jobs (Task Inbox feed)
inline void jobs() {
  if (!CFG.jobsUrl[0]) { Lock l; snprintf(G.jobs.err, sizeof(G.jobs.err), "NO URL SET"); return; }
  String out;
  int code = httpDo("GET", CFG.jobsUrl, "", nullptr, 0, out);
  JsonDocument d(psAlloc());
  if (code == 200 && !deserializeJson(d, out)) {
    JsonArrayConst arr = d.is<JsonArrayConst>() ? d.as<JsonArrayConst>() : d["jobs"].as<JsonArrayConst>();
    Jobs j;
    const char* newest = "";
    for (JsonObjectConst o : arr) {
      const char* s = o["status"] | "";
      if (!strcmp(s, "running") || !strcmp(s, "cancelling")) j.running++;
      else if (!strcmp(s, "queued")) j.queued++;
      else if (!strcmp(s, "held")) j.held++;
      else if (!strcmp(s, "done")) j.done++;
      else if (!strcmp(s, "failed") || !strcmp(s, "interrupted")) j.failed++;
      const char* created = o["created"] | "";
      bool isRun = !strcmp(s, "running");
      if (isRun || (strcmp(created, newest) > 0 && strcmp(j.lastStatus, "running") != 0)) {
        if (!isRun) newest = created;
        scopy(j.lastTitle, o["title"] | (o["cmd"] | ""));
        scopy(j.lastStatus, s);
      }
    }
    j.at = millis();
    Lock l;
    G.jobs = j;
  } else {
    Lock l;
    errText(code == 200 ? -1 : code, G.jobs.err, sizeof(G.jobs.err));
  }
}

// ---------------------------------------------------------------- home assistant
inline const char* haDomainService(const char* entity, char* domain, size_t dn) {
  const char* dot = strchr(entity, '.');
  size_t len = dot ? (size_t)(dot - entity) : 0;
  if (!len || len >= dn) { domain[0] = 0; return nullptr; }
  memcpy(domain, entity, len); domain[len] = 0;
  if (!strcmp(domain, "scene") || !strcmp(domain, "script")) return "turn_on";
  if (!strcmp(domain, "button") || !strcmp(domain, "input_button")) return "press";
  if (!strcmp(domain, "automation")) return "trigger";
  if (!strcmp(domain, "cover")) return "toggle";
  if (!strcmp(domain, "media_player")) return "media_play_pause";
  if (!strcmp(domain, "lock")) return nullptr;          // never toggled from a pocket
  strncpy(domain, "homeassistant", dn - 1);
  domain[dn - 1] = 0;
  return "toggle";
}

inline bool haStateless(const char* entity) {
  return !strncmp(entity, "scene.", 6) || !strncmp(entity, "script.", 7) || !strncmp(entity, "button.", 7) ||
         !strncmp(entity, "input_button.", 13) || !strncmp(entity, "automation.", 11);
}

inline int8_t haOnOff(const char* s) {
  return (!strcmp(s, "on") || !strcmp(s, "open") || !strcmp(s, "playing") || !strcmp(s, "home")) ? 1
       : (!strcmp(s, "off") || !strcmp(s, "closed") || !strcmp(s, "paused") || !strcmp(s, "idle")) ? 0 : -1;
}

// The screen font is ASCII: fold UTF-8 into '?' (one per character, not per byte).
inline void haAscii(const char* src, char* dst, size_t n) {
  size_t o = 0;
  for (const unsigned char* p = (const unsigned char*)src; *p && o + 1 < n; p++) {
    if (*p >= 0x80 && *p < 0xC0) continue;
    dst[o++] = (*p < 32 || *p > 126) ? '?' : (char)*p;
  }
  dst[o] = 0;
}

inline int haGetState(const char* entity, int8_t& on) {
  String auth = String("Bearer ") + CFG.haToken, out;
  Hdr h[] = {{"Authorization", auth.c_str()}};
  int code = httpDo("GET", String(CFG.haUrl) + "/api/states/" + entity, "", h, 1, out, true, 4000);
  if (code != 200) return code;
  JsonDocument filter, d;
  filter["state"] = true;
  if (deserializeJson(d, out, DeserializationOption::Filter(filter))) return -1;
  on = haOnOff(d["state"] | "");
  return 200;
}

// One domain, A-Z, via Home Assistant's template API: a reply of a few hundred bytes instead of the
// whole state machine. First line = how many exist, then "entity_id|name|state" lines.
inline int haListTemplate(const char* domain, HaEnt* out, int maxN, int& n, bool& more) {
  String t = String("{{ states.") + domain + "|list|count }}\n{% for s in (states." + domain + "|sort(attribute='name'))[:" + maxN +
             "] %}{{ s.entity_id }}|{{ s.name }}|{{ s.state }}\n{% endfor %}";
  JsonDocument b;
  b["template"] = t;
  String body, reply;
  serializeJson(b, body);
  String auth = String("Bearer ") + CFG.haToken;
  Hdr h[] = {{"Authorization", auth.c_str()}, {"Content-Type", "application/json"}};
  int code = httpDo("POST", String(CFG.haUrl) + "/api/template", body, h, 2, reply, true, 9000);
  if (code != 200) return code;
  n = 0;
  int total = -1, pos = 0, len = reply.length();
  while (pos < len) {
    int nl = reply.indexOf('\n', pos);
    if (nl < 0) nl = len;
    String line = reply.substring(pos, nl);
    pos = nl + 1;
    line.trim();
    if (!line.length()) continue;
    if (total < 0) { total = line.toInt(); continue; }
    int p1 = line.indexOf('|'), p2 = line.lastIndexOf('|');
    if (p1 <= 0 || p2 <= p1 || n >= maxN) continue;
    HaEnt& e = out[n];
    e = HaEnt();
    scopy(e.entity, line.substring(0, p1).c_str());
    haAscii(line.substring(p1 + 1, p2).c_str(), e.name, sizeof(e.name));
    if (!e.name[0]) scopy(e.name, e.entity);
    e.on = haStateless(e.entity) ? -1 : haOnOff(line.substring(p2 + 1).c_str());
    n++;
  }
  if (total < 0) return -1;
  more = total > n;
  return 200;
}

// Fallback when the template API is refused (it can be admin-only): stream the full state list through
// a JSON filter and keep one domain. Needs PSRAM for big installs.
inline int haListStates(const char* domain, HaEnt* out, int maxN, int& n, bool& more) {
  if (!net::online()) return -100;
  NetworkClient plain;
  std::unique_ptr<NetworkClientSecure> sec;
  HTTPClient http;
  String url = String(CFG.haUrl) + "/api/states";
  bool okb;
  if (url.startsWith("https://")) { sec.reset(new NetworkClientSecure()); sec->setInsecure(); okb = http.begin(*sec, url); }
  else okb = http.begin(plain, url);
  if (!okb) return -101;
  http.useHTTP10(true);
  http.setTimeout(12000);
  http.addHeader("Authorization", String("Bearer ") + CFG.haToken);
  int code = http.GET();
  if (code != 200) { http.end(); return code; }
  JsonDocument filter, d(psAlloc());
  filter[0]["entity_id"] = true;
  filter[0]["state"] = true;
  filter[0]["attributes"]["friendly_name"] = true;
  // Home Assistant nests attributes deeper than ArduinoJson's default limit of 10 (verified on a real 694-entity install)
  DeserializationError e = deserializeJson(d, http.getStream(), DeserializationOption::Filter(filter), DeserializationOption::NestingLimit(24));
  http.end();
  if (e) return -2;
  String prefix = String(domain) + ".";
  n = 0;
  int total = 0;
  for (JsonObjectConst o : d.as<JsonArrayConst>()) {
    const char* id = o["entity_id"] | "";
    if (strncmp(id, prefix.c_str(), prefix.length()) != 0) continue;
    total++;
    HaEnt t;
    scopy(t.entity, id);
    haAscii(o["attributes"]["friendly_name"] | id, t.name, sizeof(t.name));
    t.on = haStateless(id) ? -1 : haOnOff(o["state"] | "");
    // keep the alphabetically-first maxN: insert in order, drop whatever falls off the end
    if (n >= maxN && strcasecmp(t.name, out[n - 1].name) >= 0) continue;
    int j = (n < maxN) ? n++ : n - 1;
    while (j > 0 && strcasecmp(out[j - 1].name, t.name) > 0) { out[j] = out[j - 1]; j--; }
    out[j] = t;
  }
  more = total > n;
  return 200;
}

static volatile bool haTplDenied = false;   // HA's template API is admin-only: remember a refusal (reset when the config changes)

// Load one category into the on-stick browser.
inline void haList(uint8_t kind) {
  if (kind >= HC_N) return;
  static HaEnt tmp[MAX_HA_ENT];
  int n = 0, code = 200;
  bool more = false;
  if (!CFG.haToken[0]) code = -200;
  else if (kind == HC_FAVS) {
    Fav copy[MAX_FAVS];
    { Lock l; n = G.home.n; memcpy((void*)copy, (void*)G.home.f, sizeof(copy)); }
    for (int i = 0; i < n; i++) {
      tmp[i] = HaEnt();
      scopy(tmp[i].entity, copy[i].entity);
      scopy(tmp[i].name, copy[i].label);
      if (haStateless(copy[i].entity)) continue;
      int c = haGetState(copy[i].entity, tmp[i].on);
      if (c != 200 && c != 404) { code = c; if (c == 401 || c < 0) break; }
    }
  } else {
    if (!haTplDenied) code = haListTemplate(HA_CAT_DOMAIN[kind], tmp, MAX_HA_ENT, n, more);
    if (haTplDenied || code == 401 || code == 403 || code == 404 || code == 405 || code == 400) {
      int n2 = 0;
      bool more2 = false;
      int c2 = haListStates(HA_CAT_DOMAIN[kind], tmp, MAX_HA_ENT, n2, more2);
      if (c2 == 200) haTplDenied = true;                 // token is fine, only the template API is closed to it
      code = c2; n = n2; more = more2;
    }
  }
  Lock l;
  HaBrowse& b = G.hab;
  if (b.kind != kind) return;                            // the owner moved on: a newer request owns the loading flag
  b.loading = false;
  if (code == 200) {
    char keep[48];
    scopy(keep, (b.sel < b.n) ? b.e[b.sel].entity : "");
    memcpy((void*)b.e, (void*)tmp, sizeof(HaEnt) * n);
    b.n = n; b.more = more; b.err[0] = 0; b.at = millis();
    b.sel = 0;
    for (int i = 0; i < n; i++) if (keep[0] && !strcmp(b.e[i].entity, keep)) b.sel = i;
    for (int i = 0; i < n; i++)                          // keep the favourites' dots in step too
      for (int k = 0; k < G.home.n; k++) if (!strcmp(G.home.f[k].entity, b.e[i].entity)) G.home.f[k].on = b.e[i].on;
  } else if (code == -200) snprintf(b.err, sizeof(b.err), "NO HA TOKEN");
  else if (code == 401 || code == 403) snprintf(b.err, sizeof(b.err), "BAD HA TOKEN");
  else if (code == -2) snprintf(b.err, sizeof(b.err), "LIST TOO BIG");
  else errText(code, b.err, sizeof(b.err));
}

// Periodic refresh while a list is open. With the template API it is one tiny request; without it,
// re-reading the whole state list (hundreds of KB) every few seconds would be silly, so only the
// three rows on screen are re-read.
inline void haRefresh() {
  uint8_t kind = G.hab.kind;
  if (kind == HC_FAVS || !haTplDenied) { haList(kind); return; }
  char ent[3][48];
  int k = 0;
  {
    Lock l;
    int n = G.hab.n, top = (int)G.hab.sel - 1;
    if (top > n - 3) top = n - 3;
    if (top < 0) top = 0;
    for (int i = top; i < n && k < 3; i++) if (!haStateless(G.hab.e[i].entity)) { scopy(ent[k], G.hab.e[i].entity); k++; }
  }
  for (int i = 0; i < k; i++) {
    int8_t on = -1;
    if (haGetState(ent[i], on) != 200) break;
    Lock l;
    for (int j = 0; j < G.hab.n; j++) if (!strcmp(G.hab.e[j].entity, ent[i])) G.hab.e[j].on = on;
  }
}

// Toggle / run one entity. Returns true when Home Assistant accepted it.
inline bool haFireEntity(const char* entity) {
  char domain[16];
  const char* svc = haDomainService(entity, domain, sizeof(domain));
  bool okFire = false;
  if (svc && CFG.haToken[0]) {
    String auth = String("Bearer ") + CFG.haToken;
    Hdr h[] = {{"Authorization", auth.c_str()}, {"Content-Type", "application/json"}};
    String body = String("{\"entity_id\":\"") + entity + "\"}", out;
    int code = httpDo("POST", String(CFG.haUrl) + "/api/services/" + domain + "/" + svc, body, h, 2, out, true, 6000);
    okFire = code >= 200 && code < 300;
  }
  {
    Lock l;
    G.home.firedAt = millis(); G.home.firedOk = okFire;
    setToast(G, okFire ? "SENT" : (svc ? "FAILED" : "NOT ALLOWED"));
  }
  if (okFire && !haStateless(entity)) {
    delay(650);
    int8_t on = -1;
    if (haGetState(entity, on) == 200) {
      Lock l;
      for (int i = 0; i < G.hab.n; i++) if (!strcmp(G.hab.e[i].entity, entity)) G.hab.e[i].on = on;
      for (int k = 0; k < G.home.n; k++) if (!strcmp(G.home.f[k].entity, entity)) G.home.f[k].on = on;
    }
  }
  return okFire;
}

// Any Home Assistant service with a JSON body. Returns the HTTP code (negative = no answer).
inline int haCall(const char* domain, const char* svc, const String& body) {
  if (!CFG.haToken[0]) return 401;
  String auth = String("Bearer ") + CFG.haToken, out;
  Hdr h[] = {{"Authorization", auth.c_str()}, {"Content-Type", "application/json"}};
  return httpDo("POST", String(CFG.haUrl) + "/api/services/" + domain + "/" + svc, body, h, 2, out, true, 6000);
}
inline void haFire(uint8_t idx) {                        // favourite by index (web panel / API)
  char ent[48];
  { Lock l; if (idx >= G.home.n) return; scopy(ent, G.home.f[idx].entity); }
  haFireEntity(ent);
}

inline void haFireBrowse(uint8_t idx) {                  // row of the on-stick browser
  char ent[48];
  { Lock l; if (idx >= G.hab.n) return; scopy(ent, G.hab.e[idx].entity); }
  haFireEntity(ent);
}

// Panel "TEST CONNECTION": proves address + token and says what it reached.
inline int haTest(String& msg) {
  if (!CFG.haToken[0]) { msg = "No token saved yet."; return 400; }
  String auth = String("Bearer ") + CFG.haToken, out;
  Hdr h[] = {{"Authorization", auth.c_str()}};
  int code = httpDo("GET", String(CFG.haUrl) + "/api/", "", h, 1, out, true, 6000);
  if (code == 401 || code == 403) { msg = "Home Assistant rejected the token (401). Make a new long-lived token and paste it again."; return code; }
  if (code == -100) { msg = "The stick is not on Wi-Fi right now."; return 503; }
  if (code < 0) { msg = String("No answer from ") + CFG.haUrl + " - check the address (include http:// and the port if it is not 80)."; return 502; }
  if (code != 200) { msg = String("Home Assistant answered HTTP ") + code + "."; return 502; }
  String cfgOut;
  JsonDocument filter, d;
  filter["location_name"] = true; filter["version"] = true;
  msg = "Connected to Home Assistant.";
  if (httpDo("GET", String(CFG.haUrl) + "/api/config", "", h, 1, cfgOut, true, 6000) == 200 &&
      !deserializeJson(d, cfgOut, DeserializationOption::Filter(filter)))
    msg = String("Connected to \"") + (d["location_name"] | "Home") + "\" - Home Assistant " + (d["version"] | "?") + ". Open HA CONTROL on the stick.";
  return 200;
}

// ---------------------------------------------------------------- macros
inline void macroFire(uint8_t idx) {
  Macro m;
  { Lock l; if (idx >= G.macros.n) return; m = G.macros.m[idx]; }
  Hdr h[] = {{"Content-Type", m.body[0] == '{' || m.body[0] == '[' ? "application/json" : "text/plain"}};
  String out;
  int code = httpDo(m.method[0] ? m.method : "GET", m.url, m.body, h, m.body[0] ? 1 : 0, out, true, 8000);
  Lock l;
  G.macros.firedAt = millis(); G.macros.lastCode = code; G.macros.firedOk = code >= 200 && code < 400;
  char t[22];
  if (G.macros.firedOk) snprintf(t, sizeof(t), "DONE (%d)", code); else if (code < 0) snprintf(t, sizeof(t), "NO ANSWER"); else snprintf(t, sizeof(t), "FAILED (%d)", code);
  setToast(G, t);
}

// ---------------------------------------------------------------- bluetooth LE sniff (listen-only)
inline void bleScan() {
  { Lock l; G.ble.scanning = true; }
  BLEDevice::init("");
  BLEScan* s = BLEDevice::getScan();
  s->setActiveScan(true);
  s->setInterval(100);
  s->setWindow(80);
  BLEScanResults* res = s->start(4, false);
  BleScan r;
  if (res) {
    int n = res->getCount();
    r.total = n;
    int idx[MAX_BLE];
    int k = 0;
    for (int i = 0; i < n && k < MAX_BLE; i++) idx[k++] = i;
    for (int i = 0; i < k; i++) {
      BLEAdvertisedDevice dv = res->getDevice(idx[i]);
      scopy(r.d[i].name, dv.haveName() ? dv.getName().c_str() : "");
      scopy(r.d[i].mac, dv.getAddress().toString().c_str());
      for (char* p = r.d[i].mac; *p; p++) *p = toupper(*p);
      r.d[i].rssi = dv.getRSSI();
    }
    r.count = k;
    // strongest first, named devices ahead of anonymous ones at equal strength
    for (int i = 0; i < r.count; i++)
      for (int j = i + 1; j < r.count; j++) {
        int si = r.d[i].rssi + (r.d[i].name[0] ? 6 : 0), sj = r.d[j].rssi + (r.d[j].name[0] ? 6 : 0);
        if (sj > si) { BleDev t = r.d[i]; r.d[i] = r.d[j]; r.d[j] = t; }
      }
  }
  s->clearResults();
  BLEDevice::deinit(false);
  r.at = millis();
  r.scanning = false;
  Lock l;
  r.sel = 0;
  G.ble = r;
}

// ---------------------------------------------------------------- task
struct Due { uint32_t last = 0; bool force = true; };
static Due dWx, dPi, dHosts, dShop, dJobs, dHa;

inline bool due(Due& d, uint32_t now, uint32_t everyMs) {
  if (d.force || now - d.last >= everyMs) { d.force = false; d.last = now; return true; }
  return false;
}

inline void forceFor(uint8_t screen) {
  switch (screen) {
    case SC_WEATHER: dWx.force = true; break;
    case SC_PIHOLE: dPi.force = true; break;
    case SC_HOSTS: dHosts.force = true; break;
    case SC_SHOP: dShop.force = true; break;
    case SC_JOBS: dJobs.force = true; break;
    case SC_HOME: dHa.force = true; break;
  }
}

// ---------------------------------------------------------------- self-update (OTA pull)
// otaCheck polls the manifest and raises an offer; otaInstall streams the .bin into flash
// after the owner presses FRONT. Both run on the net task. See struct Ota / otaui in state.h.
static Due dOta;
static uint8_t otaBuf[1460];          // file-scope: keep the net task's 16 KB stack clear

inline void otaFail(const char* why) {
  Lock l;
  G.ota.installing = false; G.ota.prog = 0;
  scopy(G.ota.err, why);
  G.ota.at = millis();
  G.ota.snoozeUntil = millis() + 30UL * 60000UL;   // keep the offer, but do not nag: retry in 30 min
}

inline void otaCheck(uint32_t now) {
  if (!G.set.otaOn || !CFG.otaUrl[0]) return;
  if (G.ota.installing || G.ota.want || G.ota.done) return;
  if (!due(dOta, now, 6UL * 3600000UL)) return;    // every 6 h, forced once on boot (Due.force defaults true)
  String body;
  if (httpDo("GET", CFG.otaUrl, "", nullptr, 0, body, false, 6000) != 200) return;
  JsonDocument d(psAlloc());
  if (deserializeJson(d, body)) return;
  const char* ver = d["version"] | "";
  const char* url = d["bin"] | "";
  uint32_t size = d["size"] | 0;
  if (!ver[0] || !url[0]) return;
  Lock l;
  if (!otautil::newer(ver, FW_VERSION)) { G.ota.avail = false; G.ota.ver[0] = 0; return; }
  if (G.ota.avail && !strcmp(G.ota.ver, ver)) return;   // already offering this exact version: keep any snooze
  scopy(G.ota.ver, ver); scopy(G.ota.url, url);
  G.ota.size = size; G.ota.avail = true; G.ota.err[0] = 0; G.ota.snoozeUntil = 0; G.ota.at = now;
  Serial.printf("[ota] offer %s -> %s (%u B)\n", FW_VERSION, ver, (unsigned)size);
}

inline void otaInstall() {
  String url; uint32_t declared;
  { Lock l; G.ota.want = false; G.ota.installing = true; G.ota.prog = 0; G.ota.err[0] = 0; G.ota.at = millis();
    url = G.ota.url; declared = G.ota.size; }
  if (!net::online()) { otaFail("NO WIFI"); return; }
  NetworkClient client;
  HTTPClient http;
  http.setReuse(false); http.setConnectTimeout(5000); http.setTimeout(20000);
  if (!http.begin(client, url)) { otaFail("BAD URL"); return; }
  int code = http.GET();
  if (code != 200) { char e[28]; errText(code, e, sizeof(e)); otaFail(e); http.end(); return; }
  int len = http.getSize();                               // -1 if the server does not send a length
  uint32_t total = declared ? declared : (len > 0 ? (uint32_t)len : 0);
  if (!Update.begin(len > 0 ? (size_t)len : UPDATE_SIZE_UNKNOWN)) { otaFail("NO SPACE"); http.end(); return; }
  NetworkClient* s = http.getStreamPtr();
  uint32_t got = 0, lastData = millis();
  while (http.connected() && (len < 0 || got < (uint32_t)len)) {
    size_t have = s->available();
    if (have) {
      int r = s->readBytes(otaBuf, have > sizeof(otaBuf) ? sizeof(otaBuf) : have);
      if (r > 0) {
        if (Update.write(otaBuf, r) != (size_t)r) { Update.abort(); otaFail("WRITE ERR"); http.end(); return; }
        got += r; lastData = millis();
        uint8_t p = total ? (uint8_t)(got >= total ? 100 : got * 100 / total) : 50;
        { Lock l; G.ota.prog = p; }
      }
    } else {
      if (ago(millis(), lastData) > 20000) { Update.abort(); otaFail("STALLED"); http.end(); return; }
      delay(10);
    }
  }
  http.end();
  if (got == 0) { Update.abort(); otaFail("EMPTY"); return; }
  if (!Update.end(true)) { otaFail("VERIFY"); return; }   // true = set the boot partition, check the hash
  { Lock l; G.ota.installing = false; G.ota.done = true; G.ota.prog = 100; G.ota.at = millis(); }
  Serial.println("[ota] flashed OK, rebooting");
}

// ---------------------------------------------------------------- MQTT ticker (house broker)
static NetworkClient mqttNet;
static PubSubClient mqttCli(mqttNet);
static uint32_t mqttNextTry = 0;
static bool mqttInit = false;
static char mqttSubHost[40] = "";       // host we last pointed at, to reconnect when it changes
static char mqttSubTopics[96] = "";      // topics we last subscribed, to re-sub on change

inline void mqttOnMsg(char* topic, byte* payload, unsigned int len) {
  if (!strncmp(topic, "shiv/", 5) || !strncmp(topic, "homeassistant/", 14)) return;   // our own chatter stays off the ticker
  Lock l;
  MqttState& m = G.mqtt;
  for (int i = MAX_MQTT - 1; i > 0; i--) m.m[i] = m.m[i - 1];
  MqttMsg& e = m.m[0];
  e = MqttMsg();
  scopy(e.topic, topic);
  size_t n = len < sizeof(e.text) - 1 ? len : sizeof(e.text) - 1;
  for (size_t i = 0; i < n; i++) {       // the ticker font is ASCII
    uint8_t ch = payload[i];
    e.text[i] = (ch < 32 || ch > 126) ? ' ' : (char)ch;
  }
  e.text[n] = 0;
  if (G.sys.timeValid) snprintf(e.when, sizeof(e.when), "%02d:%02d", G.lt.tm_hour, G.lt.tm_min);
  if (m.n < MAX_MQTT) m.n++;
}

inline void mqttSubscribeAll() {         // subscribe to each comma-separated topic in CFG.mqttTopics
  char buf[96]; scopy(buf, CFG.mqttTopics);
  char* p = buf;
  while (*p) {
    while (*p == ' ' || *p == ',') p++;
    char* s = p;
    while (*p && *p != ',') p++;
    char* end = p;
    while (end > s && end[-1] == ' ') end--;
    char save = *end; *end = 0;
    if (*s) mqttCli.subscribe(s);
    *end = save;
    if (*p) p++;
  }
}

inline void mqttPump(uint32_t now) {
  if (!CFG.mqttHost[0]) {                 // MQTT off
    if (mqttCli.connected()) mqttCli.disconnect();
    if (G.mqtt.connected) { Lock l; G.mqtt.connected = false; }
    return;
  }
  if (!mqttInit) {
    mqttNet.setConnectionTimeout(3000);            // a silent broker must not stall core 0
    mqttCli.setBufferSize(1600);                    // Home Assistant discovery payloads (the Routine select lists every program)
    mqttCli.setSocketTimeout(4);                    // seconds, bounds each socket read/write
    mqttCli.setKeepAlive(60);                       // survive a busy fetch cycle between loop() calls
    mqttCli.setCallback(mqttOnMsg);
    mqttInit = true;
  }
  if (strcmp(mqttSubHost, CFG.mqttHost)) { mqttCli.disconnect(); scopy(mqttSubHost, CFG.mqttHost); mqttSubTopics[0] = 0; }
  if (!mqttCli.connected()) {
    if ((int32_t)(now - mqttNextTry) < 0) return;
    mqttNextTry = now + 5000;             // don't hammer a down broker
    mqttCli.setServer(CFG.mqttHost, CFG.mqttPort);
    const char* user = CFG.mqttUser[0] ? CFG.mqttUser : nullptr;
    const char* pass = CFG.mqttPass[0] ? CFG.mqttPass : nullptr;
    char avail[48]; snprintf(avail, sizeof(avail), "shiv/%s/status", CFG.hostName);
    bool ok = mqttCli.connect(CFG.hostName, user, pass, avail, 0, true, "offline");
    if (ok) mqttCli.publish(avail, "online", true);
    Lock l;
    G.mqtt.connected = ok;
    if (ok) { mqttSubscribeAll(); scopy(mqttSubTopics, CFG.mqttTopics); G.mqtt.err[0] = 0; }
    else snprintf(G.mqtt.err, sizeof(G.mqtt.err), "NO BROKER (%d)", mqttCli.state());
    return;
  }
  if (strcmp(mqttSubTopics, CFG.mqttTopics)) { mqttSubscribeAll(); scopy(mqttSubTopics, CFG.mqttTopics); }
  mqttCli.loop();
}

inline void taskMain(void*) {
  for (;;) {
    G.sys.netHwm = uxTaskGetStackHighWaterMark(nullptr);
    busy = true;                                   // claim first, then look: closes the sleep/OTA race
    if (paused) { busy = false; vTaskDelay(pdMS_TO_TICKS(120)); continue; }   // sleep / update
    busy = false;
    Act a;
    if (xQueueReceive(q, &a, pdMS_TO_TICKS(300)) == pdTRUE) {
      busy = true;
      if (paused) { busy = false; xQueueSendToFront(q, &a, 0); continue; }
      switch (a.type) {
        case A_FAV: haFire(a.arg); break;
        case A_PIHOLE: piholeSet(a.arg); break;
        case A_HA_LIST: haList(a.arg); dHa.last = millis(); dHa.force = false; break;
        case A_HA_ENT: haFireBrowse(a.arg); break;
        case A_MACRO: macroFire(a.arg); break;
        case A_REFRESH: forceFor(a.arg); break;
        case A_BLE: bleScan(); break;
        case A_HOSTS: dHosts.force = true; break;
      }
      busy = false;
    }
    busy = true;
    if (G.ota.want) { otaInstall(); busy = false; continue; }   // owner accepted: download + flash (blocking), then loop
    if (paused || !net::online()) { busy = false; continue; }
    uint32_t now = millis();
    bool on = hw::screenOn();
    uint8_t sc = G.screen;
    uint32_t slow = on ? 1 : 4;       // screen off: everything relaxes
    if (screenEnabled(G, SC_WEATHER) && due(dWx, now, 900000UL * slow)) weather();
    if (screenEnabled(G, SC_PIHOLE) && due(dPi, now, (on && sc == SC_PIHOLE ? 20000UL : 300000UL) * slow)) pihole();
    if (screenEnabled(G, SC_HOSTS) && due(dHosts, now, (on && sc == SC_HOSTS ? 45000UL : 300000UL) * slow)) hosts();
    if (shopRetry && G.sys.timeValid) { shopRetry = false; dShop.force = true; }
    if (screenEnabled(G, SC_SHOP) && due(dShop, now, (on && sc == SC_SHOP ? 120000UL : 600000UL) * slow)) shop();
    if (screenEnabled(G, SC_JOBS) && due(dJobs, now, (on && sc == SC_JOBS ? 12000UL : 180000UL) * slow)) jobs();
    if (piForce) { piForce = false; dPi.force = true; }
    if (piForceAt && (int32_t)(millis() - piForceAt) >= 0) { piForceAt = 0; dPi.force = true; }
    if (on && sc == SC_HOME && G.hab.level == 2 && !G.hab.loading && due(dHa, now, 10000UL)) haRefresh();
    mqttPump(now);
    otaCheck(now);                     // self-update: poll the manifest every 6 h, raise an offer
    busy = false;
  }
}

inline void begin() {
  q = xQueueCreate(8, sizeof(Act));
  xTaskCreatePinnedToCore(taskMain, "shiv-net", 16384, nullptr, 1, nullptr, 0);
}

}  // namespace fetch
}  // namespace shiv
