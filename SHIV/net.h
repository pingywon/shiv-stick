// SHIV - Wi-Fi manager. Up to WIFI_SLOTS saved networks; a scan picks the ones in range, strongest first.
//   power-up: JOINING WIFI screen tries them one by one (G.join), then - nothing saved or nothing joined -
//             the setup hotspot (192.168.4.1); one press on either screen = carry on offline
//   lost later / woke from sleep: offline mode with quiet re-scans, never the hotspot
// Every attempt leaves a plain reason behind (G.join.res) for the stick's screen and the hotspot page.
#pragma once
#include <WiFi.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <time.h>
#include "config.h"
#include "hw.h"

namespace shiv {
namespace net {

enum Mode : uint8_t { IDLE, SCANNING, CONNECTING, HOLD, ONLINE, OFFLINE, PORTAL };   // HOLD = showing why the last one failed
static Mode mode = IDLE;
static uint8_t order[WIFI_SLOTS];       // saved slots worth trying, best first
static uint8_t orderN = 0, orderI = 0;
static bool slotEnt[WIFI_SLOTS];        // the scan says this one wants a username (enterprise login)
static uint32_t stateAt = 0, lastRetry = 0, portalLookAt = 0, portalIdleAt = 0;
static DNSServer dns;
static char apName[20] = "SHIV-SETUP";
static bool mdnsUp = false;
static bool everConnected = false;
static bool bootTry = true;             // still the first attempt since power-up: failing it opens the hotspot
static bool bootRescanned = false;      // one missed scan must not be enough to open the hotspot at home
static bool portalAuto = false;         // the hotspot opened by itself, so it may also close by itself
static bool portalLooking = false;
static bool portalDismissed = false;    // the owner pressed USE OFFLINE: the hotspot never reopens by itself this power-up
static volatile uint8_t authFails = 0;  // counted from Wi-Fi disconnect reasons
static volatile uint8_t lastReason = 0; // the radio's reason for the latest drop (0 = none yet)
static volatile bool assocOk = false;   // the access point let us in (so a failure after that is about the address)

static const uint32_t PORTAL_LOOK_MS = 30000, PORTAL_IDLE_MS = 300000UL;

inline bool haveCreds() {
  for (int i = 0; i < WIFI_SLOTS; i++) if (CFG.wifiSsid[i][0]) return true;
  return false;
}
inline bool online() { return mode == ONLINE; }
// True while this file owns the radio's single scan. The WIFI screen then shares it instead of restarting it.
inline bool scanBusy() { return mode == SCANNING || portalLooking; }

inline bool isEnterprise(wifi_auth_mode_t a) {
  return a == WIFI_AUTH_ENTERPRISE || a == WIFI_AUTH_WPA3_ENT_192 || a == WIFI_AUTH_WPA3_ENTERPRISE ||
         a == WIFI_AUTH_WPA2_WPA3_ENTERPRISE || a == WIFI_AUTH_WPA_ENTERPRISE;
}

// ---------------------------------------------------------------- what happened to each saved network
inline void joinReset() {
  WifiJoin& j = G.join;
  bool show = j.show;
  j = WifiJoin();
  j.show = show;
  for (int s = 0; s < WIFI_SLOTS; s++) scopy(j.ssid[s], CFG.wifiSsid[s]);
}

// The attempt on G.join.cur is over without a connection: say why.
inline void joinClose() {
  WifiJoin& j = G.join;
  if (j.cur < 0 || j.res[j.cur] != JR_TRYING) return;
  uint8_t r = lastReason, res;
  if (assocOk) res = JR_NOIP;
  else if (r == 15 || r == 202 || r == 204) res = JR_PASS;          // 4-way handshake timeout / auth fail / handshake timeout
  else if (r == 23) res = JR_LOGIN;                                 // 802.1X login refused
  else if (r == 201) res = JR_FAR;
  else if (r == 210 || r == 211) res = JR_SECURITY;                 // no access point with a security type we accept
  else if (r == 0) res = JR_NOREPLY;
  else res = JR_REFUSED;
  if ((res == JR_PASS || res == JR_LOGIN) && slotEnt[j.cur] && !CFG.wifiUser[j.cur][0]) res = JR_NEEDUSER;
  j.res[j.cur] = res;
  j.code[j.cur] = r;
  Serial.printf("[wifi] \"%s\" -> %s (radio reason %u, let in: %d)\n", j.ssid[j.cur], joinText(res), (unsigned)r, (int)assocOk);
}

inline void startPortal(bool automatic = false) {
  G.join.show = false;
  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP_STA);           // AP for the phone, STA so the page can list nearby networks
  uint8_t mac[6];
  WiFi.macAddress(mac);
  snprintf(apName, sizeof(apName), "SHIV-SETUP-%02X%02X", mac[4], mac[5]);
  WiFi.softAP(apName);
  delay(100);
  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(53, "*", WiFi.softAPIP());
  mode = PORTAL;
  portalAuto = automatic; portalLooking = false; bootTry = false;
  G.sys.portal = true; G.sys.wifi = false;
  stateAt = portalLookAt = portalIdleAt = millis();
}

// Close the hotspot and carry on without it (the next tick re-scans for saved networks).
inline void stopPortal(bool byOwner = false) {
  if (mode != PORTAL) return;
  if (byOwner) portalDismissed = true;
  dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  G.sys.portal = false;
  portalLooking = false;
  mode = OFFLINE;
  lastRetry = millis() - 600000UL;
}

// The owner pressed a button on the JOINING WIFI screen: it only gets out of the way. The tries go on
// behind the scenes, and if they all fail the setup hotspot still opens - SKIP must never cost the way in.
inline void dismissJoin() { G.join.show = false; }

// Hold FRONT on the JOINING WIFI screen: never mind the saved networks, open the hotspot now.
inline void setupNow() {
  joinClose();
  G.join.cur = -1; G.join.scanning = false;
  if (!G.wifi.scanning) WiFi.scanDelete();
  startPortal();
}

inline void beginScan(bool thorough = false) {
  // A station still auto-retrying its last network makes the radio refuse a scan, so drop that first.
  if (mode != PORTAL) { WiFi.mode(WIFI_STA); WiFi.disconnect(); }
  if (!G.wifi.scanning) { WiFi.scanDelete(); WiFi.scanNetworks(true, true, false, thorough ? 300 : 120); }   // the WIFI screen's own scan is as good as ours
}

inline void blindOrder() {
  orderN = 0; orderI = 0;
  for (int s = 0; s < WIFI_SLOTS; s++) if (CFG.wifiSsid[s][0]) order[orderN++] = s;
}

// -1 still running, otherwise the number of saved networks worth trying (order[] filled, strongest first).
// A failed scan falls back to every saved slot, blind - unless blindOnFail is off (the hotspot is up: leave it alone).
inline int pollScan(bool blindOnFail = true) {
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) return -1;
  orderN = 0; orderI = 0;
  if (n < 0) {
    if (blindOnFail) blindOrder();
    return orderN;
  }
  int best[WIFI_SLOTS];
  bool sawHidden = false;
  for (int s = 0; s < WIFI_SLOTS; s++) { best[s] = -1000; slotEnt[s] = false; }
  for (int i = 0; i < n; i++) {
    String id = WiFi.SSID(i);
    int rssi = WiFi.RSSI(i);
    if (!id.length()) sawHidden = true;
    for (int s = 0; s < WIFI_SLOTS; s++)
      if (CFG.wifiSsid[s][0] && id == CFG.wifiSsid[s]) {
        if (rssi > best[s]) best[s] = rssi;
        if (isEnterprise(WiFi.encryptionType(i))) slotEnt[s] = true;
      }
  }
  for (int s = 0; s < WIFI_SLOTS; s++) if (best[s] > -1000) order[orderN++] = s;
  for (int i = 0; i < orderN; i++)
    for (int j = i + 1; j < orderN; j++)
      if (best[order[j]] > best[order[i]]) { uint8_t t = order[i]; order[i] = order[j]; order[j] = t; }
  // a hidden network shows up without its name: any saved one we did not see could be it, so try those last
  if (sawHidden) for (int s = 0; s < WIFI_SLOTS; s++) if (CFG.wifiSsid[s][0] && best[s] == -1000) order[orderN++] = s;
  if (!G.wifi.scanning) WiFi.scanDelete();
  return orderN;
}

inline void lookForNetworks() {
  if (!haveCreds()) { mode = OFFLINE; return; }
  authFails = 0;                                   // "wrong password" means three refusals within one round
  joinReset();
  G.join.scanning = true;
  beginScan();
  mode = SCANNING;
  stateAt = millis();
}

// Nothing (more) to join. Only the very first attempt after power-up opens the hotspot.
inline void giveUp(uint32_t now) {
  joinClose();
  G.join.cur = -1; G.join.scanning = false;
  WiFi.disconnect(true);
  mode = OFFLINE; lastRetry = now;
  bool wrongPass = !everConnected && authFails >= 3;   // in range but refusing us: needs a new password, not patience
  if (!portalDismissed && (bootTry || wrongPass)) startPortal(bootTry && !wrongPass);
  G.join.show = false;
  bootTry = false;
}

inline void connectNext(uint32_t now) {
  WifiJoin& j = G.join;
  joinClose();
  j.scanning = false;
  if (!j.tries) {                                  // first call of this round: everything the scan did not offer is out of range
    j.tries = orderN;
    for (int s = 0; s < WIFI_SLOTS; s++) if (CFG.wifiSsid[s][0]) j.res[s] = JR_FAR;
    for (int i = 0; i < orderN; i++) j.res[order[i]] = JR_NONE;
  }
  while (orderI < orderN) {
    uint8_t s = order[orderI++];
    j.cur = s; j.tryNo = orderI; j.tryAt = now;
    if (slotEnt[s] && !CFG.wifiUser[s][0]) { j.res[s] = JR_NEEDUSER; continue; }   // a work / school login: a password alone can never get in
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();                             // or the core keeps retrying the previous network and the radio refuses this one
    WiFi.setHostname(CFG.hostName);
    WiFi.STA.setHostname(CFG.hostName);           // the line above only names interfaces not started yet: without this the router sees esp32s3-XXXXXX
    WiFi.setSleep(true);
    lastReason = 0; assocOk = false;
    wl_status_t began;
    if (CFG.wifiUser[s][0]) began = WiFi.begin(CFG.wifiSsid[s], WPA2_AUTH_PEAP, CFG.wifiUser[s], CFG.wifiUser[s], CFG.wifiPass[s]);
    else began = WiFi.begin(CFG.wifiSsid[s], CFG.wifiPass[s][0] ? CFG.wifiPass[s] : nullptr);
    if (began == WL_CONNECT_FAILED) { j.res[s] = JR_REFUSED; continue; }
    j.res[s] = JR_TRYING;
    mode = CONNECTING;
    stateAt = now;
    return;
  }
  giveUp(now);
}

inline void begin() {
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  // An office has many access points under one name: look at all of them and take the strongest, not the first one heard.
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
  WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t info) {
    uint8_t r = info.wifi_sta_disconnected.reason;
    if (r != WIFI_REASON_ASSOC_LEAVE) lastReason = r;               // ASSOC_LEAVE is our own WiFi.disconnect()
    if (r == WIFI_REASON_AUTH_FAIL || r == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT || r == WIFI_REASON_HANDSHAKE_TIMEOUT || r == WIFI_REASON_AUTH_EXPIRE) {
      if (authFails < 250) authFails = authFails + 1;
    }
  }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t) { assocOk = true; }, ARDUINO_EVENT_WIFI_STA_CONNECTED);
  if (!haveCreds()) startPortal();
  else { G.join.show = true; lookForNetworks(); }
}

inline void onConnected() {
  mode = ONLINE;
  everConnected = true;
  bootTry = false;
  authFails = 0;
  G.sys.wifi = true; G.sys.portal = false;
  scopy(G.sys.ip, WiFi.localIP().toString().c_str());
  scopy(G.sys.ssid, WiFi.SSID().c_str());
  WifiJoin& j = G.join;
  if (j.cur >= 0) { j.res[j.cur] = JR_OK; j.code[j.cur] = 0; }
  j.scanning = false;
  j.doneAt = millis();
  configTzTime(CFG.tz, "pool.ntp.org", "time.cloudflare.com", "time.nist.gov");
  if (mdnsUp) MDNS.end();
  mdnsUp = MDNS.begin(CFG.hostName);
  if (mdnsUp) MDNS.addService("http", "tcp", 80);
}

inline void tick(uint32_t now) {
  switch (mode) {
    case PORTAL: {
      dns.processNextRequest();
      if (!portalAuto || !haveCreds()) break;
      // opened by itself: keep an eye out for a saved network, and do not stay up forever
      bool guest = WiFi.softAPgetStationNum() > 0;
      if (guest) portalIdleAt = now;
      if (portalLooking) {
        int found = pollScan(false);
        if (found != -1) {
          portalLooking = false; portalLookAt = now;
          bool fresh = false;                      // only hop for a network that has not already failed this power-up
          for (int i = 0; i < found; i++) if (G.join.res[order[i]] == JR_FAR || G.join.res[order[i]] == JR_NONE) fresh = true;
          if (fresh && !guest && authFails < 3) { stopPortal(); G.join.tries = 0; connectNext(now); break; }
        }
      } else if (!guest && !G.wifi.scanning && ago(now, portalLookAt) > PORTAL_LOOK_MS) { beginScan(); portalLooking = true; }
      if (!guest && ago(now, portalIdleAt) > PORTAL_IDLE_MS) stopPortal();
      break;
    }
    case SCANNING: {
      if (WiFi.status() == WL_CONNECTED) { if (!G.wifi.scanning) WiFi.scanDelete(); onConnected(); break; }
      int found = pollScan();
      if (found == -1) {
        if (ago(now, stateAt) <= 15000) break;
        if (!G.wifi.scanning) WiFi.scanDelete();
        blindOrder();                              // the scan never finished: try the saved networks without it
      } else if (found == 0 && bootTry && !bootRescanned) {
        bootRescanned = true;                      // scans do miss an access point now and then: look once more, slowly
        beginScan(true);
        stateAt = now;
        break;
      }
      connectNext(now);
      break;
    }
    case CONNECTING:
      if (WiFi.status() == WL_CONNECTED) { onConnected(); break; }
      if (ago(now, stateAt) > JOIN_TRY_MS) {
        joinClose();
        WiFi.disconnect();
        if (G.join.show) { mode = HOLD; stateAt = now; } else connectNext(now);
      }
      break;
    case HOLD:                                     // the reason is on screen; then on to the next one
      if (!G.join.show || ago(now, stateAt) > JOIN_HOLD_MS) connectNext(now);
      break;
    case ONLINE:
      if (G.join.show && ago(now, G.join.doneAt) > 1500) G.join.show = false;    // "JOINED" stays up for a moment
      if (WiFi.status() != WL_CONNECTED) {
        if (ago(now, stateAt) > 8000) { G.sys.wifi = false; mode = OFFLINE; lastRetry = now; }
      } else {
        stateAt = now;
        G.sys.rssi = WiFi.RSSI();
        if (!G.sys.wifi) onConnected();
      }
      break;
    case OFFLINE: {
      G.sys.wifi = false;
      if (WiFi.status() == WL_CONNECTED) { onConnected(); break; }
      uint32_t every = hw::screenOn() ? 45000UL : 300000UL;
      if (haveCreds() && ago(now, lastRetry) > every) { lastRetry = now; lookForNetworks(); }
      break;
    }
    default: break;
  }
  G.sys.timeValid = time(nullptr) > 1700000000L;
}

// After a light sleep the radio is off: come back quickly.
inline void afterSleep() {
  if (mode == PORTAL) return;
  mode = OFFLINE;
  lastRetry = millis();
  G.sys.wifi = false;
  G.wifi.scanning = false;                         // the radio was switched off under any scan that was running
  WiFi.scanDelete();
  if (hw::screenOn()) lookForNetworks();           // a timer wake goes back to sleep in seconds: not worth powering the radio
}

}  // namespace net
}  // namespace shiv
