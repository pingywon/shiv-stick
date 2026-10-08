// SHIV - hardware layer: sound, backlight/sleep state machine, battery, motion
#pragma once
#include <M5Unified.h>
#include <WiFi.h>
#include <esp_sleep.h>
#include <driver/gpio.h>
#include "config.h"

namespace shiv {
namespace hw {

static const gpio_num_t PIN_BTN_FRONT = GPIO_NUM_11;
static const gpio_num_t PIN_BTN_SIDE = GPIO_NUM_12;

// ---------------------------------------------------------------- sound (non-blocking note queue)
struct Note { uint16_t hz; uint16_t ms; };
static Note sq[24];
static uint8_t sqHead = 0, sqTail = 0;
static uint32_t sqNextAt = 0;
static bool speakerOn = true;

inline void note(uint16_t hz, uint16_t ms) {
  uint8_t n = (sqTail + 1) % 24;
  if (n == sqHead) return;
  sq[sqTail] = {hz, ms};
  sqTail = n;
}
inline bool audible() { return speakerOn && !G.set.mute && G.set.volume > 0; }
inline bool soundPending(uint32_t now) { return sqHead != sqTail || now < sqNextAt; }   // a tone is queued or still ringing (mic yields to it)
inline void blip()    { if (audible()) note(1900, 18); }
inline void blipHi()  { if (audible()) note(2600, 22); }
inline void blipLo()  { if (audible()) note(900, 30); }
inline void ok()      { if (audible()) { note(1400, 50); note(2100, 70); } }
inline void bad()     { if (audible()) { note(500, 90); note(320, 140); } }
inline void jingle()  { if (audible()) { note(660, 70); note(880, 70); note(1320, 70); note(1760, 120); } }
inline void pageTone(){ if (audible()) { note(1760, 90); note(0, 50); note(1760, 90); note(0, 50); note(2350, 160); } }
inline void doorbellTone(){ if (audible()) { note(1760, 150); note(0, 40); note(1320, 320); } }   // ding-dong: high then low
inline void alarmTone() { note(2000, 120); note(0, 80); note(2000, 120); note(0, 80); note(2000, 120); note(0, 400); }

inline void soundTick(uint32_t now) {
  if (sqHead == sqTail || now < sqNextAt) return;
  Note n = sq[sqHead];
  sqHead = (sqHead + 1) % 24;
  if (n.hz && speakerOn) M5.Speaker.tone(n.hz, n.ms);
  sqNextAt = now + n.ms + 6;
}
inline void applyVolume() { M5.Speaker.setVolume(G.set.mute ? 0 : G.set.volume); }

// The IR receiver picks up noise from the speaker amp, so it is parked while learning.
inline void speakerPark(bool park) {
  if (park && speakerOn) { M5.Speaker.end(); speakerOn = false; }
  else if (!park && !speakerOn) { M5.Speaker.begin(); applyVolume(); speakerOn = true; }
}

// ---------------------------------------------------------------- backlight / activity
enum Light : uint8_t { L_ON, L_DIM, L_OFF };
static Light light = L_ON;
static uint32_t lastActivity = 0;
static uint32_t lastButtonAt = 0;      // real button presses only (keep-awake rules use this, not lastActivity)
static uint32_t offSince = 0;

inline void setLight(Light l) {
  if (l == light) return;
  light = l;
  if (l == L_ON) { M5.Display.wakeup(); M5.Display.setBrightness(G.set.brightness); }
  else if (l == L_DIM) M5.Display.setBrightness(G.set.brightness > 60 ? 28 : 12);
  else { M5.Display.setBrightness(0); M5.Display.sleep(); offSince = millis(); }
}
inline void poke() { lastActivity = millis(); if (light != L_ON) setLight(L_ON); }
inline bool screenOn() { return light != L_OFF; }

// keepAwake: timers ringing, games, IR learn, portal mode
// dimS / offS come from the caller; offS 0 = never off, dim still applies.
inline void lightTick(uint32_t now, bool keepAwake, uint16_t dimS, uint16_t offS) {
  if (keepAwake || G.sys.usb) { if (light != L_ON) setLight(L_ON); if (keepAwake) lastActivity = now; return; }
  uint32_t idle = ago(now, lastActivity) / 1000;
  if (offS && idle >= offS) setLight(L_OFF);
  else if (idle >= dimS) setLight(L_DIM);
}

// Light sleep on battery after the screen has been off for a while. Wakes on either button,
// or on a timer when a countdown / pomodoro is due. Returns true if it slept.
inline bool maybeSleep(uint32_t now, uint32_t nextDueMs) {
  if (light != L_OFF || G.sys.usb || !G.set.sleepMin || G.sys.portal) return false;
  if (ago(now, offSince) < (uint32_t)G.set.sleepMin * 60000UL) return false;
  bool hadWifi = WiFi.getMode() != WIFI_OFF;
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  gpio_wakeup_enable(PIN_BTN_FRONT, GPIO_INTR_LOW_LEVEL);
  gpio_wakeup_enable(PIN_BTN_SIDE, GPIO_INTR_LOW_LEVEL);
  esp_sleep_enable_gpio_wakeup();
  uint64_t us = nextDueMs ? (uint64_t)nextDueMs * 1000ULL : 30ULL * 60ULL * 1000000ULL;
  esp_sleep_enable_timer_wakeup(us);
  esp_light_sleep_start();
  esp_sleep_wakeup_cause_t why = esp_sleep_get_wakeup_cause();
  gpio_wakeup_disable(PIN_BTN_FRONT);
  gpio_wakeup_disable(PIN_BTN_SIDE);
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  (void)hadWifi;
  if (why == ESP_SLEEP_WAKEUP_GPIO) poke(); else offSince = millis() - (uint32_t)G.set.sleepMin * 60000UL + 4000;
  return true;
}

// ---------------------------------------------------------------- battery
inline void batteryTick(uint32_t now) {
  static uint32_t last = 0;
  if (last && now - last < 5000) return;
  last = now;
  int lvl = M5.Power.getBatteryLevel();
  if (lvl >= 0 && lvl <= 100) G.sys.battPct = lvl;
  int mv = M5.Power.getBatteryVoltage();
  if (mv > 2500 && mv < 5000) G.sys.battMv = mv;
  int vbus = M5.Power.getVBUSVoltage();
  G.sys.vbusMv = vbus;
  G.sys.charging = M5.Power.isCharging() == m5::Power_Class::is_charging;
  // Ask the power chip (M5PM1 @ 0x6E, register 0x04) where the power is coming from; fall back to the
  // USB voltage if it will not say. Read over plain I2C so this builds on every M5Unified version.
  uint8_t reg = 0xFF;
  int src = M5.In_I2C.readRegister(0x6E, 0x04, &reg, 1, 100000) ? (reg & 0x07) : 3;   // 3 = chip did not answer
  if (src > 2) src = 3;
  G.sys.pwrSrc = (int8_t)src;
  if (src == 0 || src == 1) G.sys.usb = true;
  else if (src == 2) G.sys.usb = G.sys.charging;
  else G.sys.usb = vbus > 4200 || G.sys.charging;
}

// ---------------------------------------------------------------- motion
// Calibration vectors in the sensor frame: U = reading when standing upright (text upright, landscape),
// O = reading when lying flat, screen up. Defaults follow the StickC family; SYSTEM > CALIBRATE fixes any unit.
struct Motion {
  float a[3] = {0, 0, 1};       // filtered accel (g)
  float raw[3] = {0, 0, 1};
  float tiltX = 0, tiltY = 0;   // screen right / screen down, -1..1, already rotation-corrected
  float zeroRoll = 0, zeroPitch = 0;
  bool shake = false;
  uint32_t lastShake = 0;
  bool ok = false;
};
static Motion mo;

inline float dot3(const float* p, const float* q) { return p[0] * q[0] + p[1] * q[1] + p[2] * q[2]; }
inline void cross3(const float* p, const float* q, float* r) {
  r[0] = p[1] * q[2] - p[2] * q[1]; r[1] = p[2] * q[0] - p[0] * q[2]; r[2] = p[0] * q[1] - p[1] * q[0];
}
inline bool norm3(float* v) {
  float m = sqrtf(dot3(v, v));
  if (m < 0.3f) return false;
  v[0] /= m; v[1] /= m; v[2] /= m;
  return true;
}

static uint8_t rotation = 1;      // 1 = normal landscape, 3 = flipped

inline void applyRotation(uint8_t r) {
  if (r == rotation) return;
  rotation = r;
  M5.Display.setRotation(r);
}

inline void motionTick(uint32_t now) {
  static uint32_t last = 0;
  if (now - last < 20) return;
  last = now;
  if (!M5.Imu.update()) return;
  auto d = M5.Imu.getImuData();
  float v[3] = {d.accel.x, d.accel.y, d.accel.z};
  float prevMag = sqrtf(dot3(mo.raw, mo.raw));
  memcpy(mo.raw, v, sizeof(v));
  float mag = sqrtf(dot3(v, v));
  mo.ok = true;
  for (int i = 0; i < 3; i++) mo.a[i] += (v[i] - mo.a[i]) * 0.25f;

  float R[3];
  cross3(gCalU, gCalO, R);
  float up = dot3(mo.a, gCalU), out = dot3(mo.a, gCalO), right = -dot3(mo.a, R);
  float sgn = rotation == 3 ? -1.0f : 1.0f;
  mo.tiltX = constrain(right * sgn, -1.0f, 1.0f);
  mo.tiltY = constrain(up * sgn, -1.0f, 1.0f);
  G.lvl.roll = atan2f(right * sgn, out) * 57.2958f - mo.zeroRoll;
  G.lvl.pitch = atan2f(up * sgn, out) * 57.2958f - mo.zeroPitch;

  // auto flip with hysteresis, ignored while lying flat
  if (G.set.flip == 0) {
    if (fabsf(out) < 0.75f) {
      if (up > 0.45f) applyRotation(1);
      else if (up < -0.45f) applyRotation(3);
    }
  } else applyRotation(G.set.flip == 2 ? 3 : 1);

  mo.shake = false;
  if (fabsf(mag - prevMag) > 1.1f && now - mo.lastShake > 700) { mo.shake = true; mo.lastShake = now; }
}

// Two-pose calibration. step 1: lying flat, screen up. step 2: standing upright in NORMAL landscape.
inline bool calibCapture(int step) {
  float v[3] = {mo.a[0], mo.a[1], mo.a[2]};
  if (!mo.ok || !norm3(v)) return false;
  static float flat[3];
  if (step == 1) { memcpy(flat, v, sizeof(v)); return true; }
  if (fabsf(dot3(flat, v)) > 0.6f) return false;        // the two poses must differ
  float k = dot3(v, flat);                              // make U exactly perpendicular to O
  for (int i = 0; i < 3; i++) v[i] -= k * flat[i];
  if (!norm3(v)) return false;
  if (rotation == 3) for (int i = 0; i < 3; i++) v[i] = -v[i];
  memcpy(gCalO, flat, sizeof(flat));
  memcpy(gCalU, v, sizeof(v));
  mo.zeroRoll = mo.zeroPitch = 0;
  return true;
}

inline void levelZero() { mo.zeroRoll += G.lvl.roll; mo.zeroPitch += G.lvl.pitch; }

}  // namespace hw
}  // namespace shiv
