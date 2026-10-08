// =====================================================================================
//  SHIV - cyberpunk pocket gadget for the M5Stack StickS3
//
//  Arduino IDE settings (Tools menu):
//    Board ............ M5StickS3            (M5Stack board package 3.2.5 or newer)
//    Partition Scheme . 8M with spiffs (3MB APP/1.5MB SPIFFS)     <-- REQUIRED
//    PSRAM ............ OPI PSRAM
//    USB CDC On Boot .. Enabled              (only matters for the serial monitor)
//  Libraries (Library Manager): M5Unified, M5GFX, ArduinoJson (v7)
//
//  Buttons:  SIDE click = next screen     SIDE hold = "GO TO" list
//            FRONT click = action shown bottom-left    FRONT hold = action bottom-right
//  First boot: join the Wi-Fi hotspot "SHIV-SETUP-xxxx" from a phone and follow the page.
// =====================================================================================
#include <M5Unified.h>
#include <LittleFS.h>
#include "web.h"

using namespace shiv;

State shiv::G;
Config shiv::CFG;
volatile bool shiv::gConfigDirty = false;
float shiv::gCalU[3] = {1, 0, 0};
float shiv::gCalO[3] = {0, 0, 1};
SemaphoreHandle_t shiv::gMux = nullptr;

static M5Canvas canvas(&M5.Display);
static uint32_t lastFrame = 0, bootAt = 0, dirtySince = 0;
static bool booting = true;

static void drawFrame(uint32_t now) {
  {
    Lock l;
    G.now = now;
    if (booting) draw::boot(canvas, themeOf(G.set.theme), now - bootAt, 0x5111, G.sys.version);
    else if (G.sys.portal) draw::setupMode(canvas, G, themeOf(G.set.theme), net::apName);
    else draw::screen(canvas, G);
  }
  canvas.pushSprite(0, 0);
}

void setup() {
  auto cfg = M5.config();
  cfg.serial_baudrate = 115200;
  cfg.internal_imu = true;
  cfg.internal_spk = true;
  cfg.internal_mic = false;
  M5.begin(cfg);
  gMux = xSemaphoreCreateRecursiveMutex();

  LittleFS.begin(true);
  configLoad();
  if (!CFG.apiKey[0]) {
    snprintf(CFG.apiKey, sizeof(CFG.apiKey), "%08lx%08lx", (unsigned long)esp_random(), (unsigned long)esp_random());
    configSave();
  }
  scopy(G.sys.version, FW_VERSION);
  {
    static const char* const WHY[] = {"UNKNOWN", "POWER ON", "EXTERNAL", "SOFTWARE", "CRASH", "INT WDT", "TASK WDT", "WDT", "SLEEP", "BROWNOUT", "SDIO"};
    int r = (int)esp_reset_reason();
    scopy(G.sys.resetWhy, r >= 0 && r < (int)(sizeof(WHY) / sizeof(WHY[0])) ? WHY[r] : "OTHER");
  }
  scopy(G.sys.host, CFG.hostName);

  M5.Display.setRotation(1);
  M5.Display.setBrightness(G.set.brightness);
  canvas.setPsram(psramFound());
  canvas.setColorDepth(16);
  canvas.createSprite(W, H);
  M5.BtnA.setHoldThresh(550);
  M5.BtnB.setHoldThresh(550);
  hw::applyVolume();
  hw::lastActivity = hw::lastButtonAt = millis();

  ir::begin();
  ir::loadIndex();
  net::begin();
  fetch::begin();
  web::begin(&canvas);

  bootAt = millis();
  hw::jingle();
}

void loop() {
  M5.update();
  uint32_t now = millis();

  if (booting) {
    hw::soundTick(now);
    net::tick(now);
    if (now - bootAt >= (uint32_t)draw::BOOT_MS || M5.BtnA.wasPressed() || M5.BtnB.wasPressed()) {
      booting = false;
      hw::poke();
      app::swallow = true;
    } else if (now - lastFrame >= 33) { lastFrame = now; drawFrame(now); }
    delay(2);
    return;
  }

  net::tick(now);
  web::tick();
  hw::soundTick(now);
  hw::batteryTick(now);
  if (hw::screenOn()) hw::motionTick(now);
  mic::tick(now);

  {
    Lock l;
    app::input(now, canvas);
    app::tick(now);
  }
  { uint16_t dimS, offS; { Lock l; app::lightTimes(dimS, offS); } hw::lightTick(now, app::keepAwake(), dimS, offS); }

  bool fast = app::inGame() || (G.screen == SC_HOME && G.hab.level == 2) || G.screen == SC_LEVEL || G.screen == SC_ORACLE || G.launcher || G.macros.holdStart || (G.screen == SC_DAEMON && pet::lively(G.dmn)) || G.screen == SC_REFLEX || G.screen == SC_SPECTRUM;
  if (hw::screenOn() && now - lastFrame >= (fast ? 33u : 80u)) { lastFrame = now; drawFrame(now); }

  if (gConfigDirty) {
    if (!dirtySince) dirtySince = now;
    if (now - dirtySince > 2500) { Lock l; configSave(); dirtySince = 0; }
  } else dirtySince = 0;

  // pocket idle: light sleep with the radio off; any button or a due timer wakes it
  if (!hw::screenOn() && !G.sys.usb && G.set.sleepMin && !G.sys.portal &&
      ago(now, hw::offSince) >= (uint32_t)G.set.sleepMin * 60000UL) {
    fetch::paused = true;
    if (!fetch::busy) {
      if (gConfigDirty) { Lock l; configSave(); }
      if (hw::maybeSleep(now, app::nextTimerDueMs())) {
        net::afterSleep();
        if (hw::screenOn()) { app::swallow = true; hw::lastButtonAt = millis(); }   // the press that woke it does nothing else
      }
      fetch::paused = false;
    }
  } else if (fetch::paused && !Update.isRunning() && !web::rebootAt) fetch::paused = false;

  delay(hw::screenOn() ? 2 : 25);
}
