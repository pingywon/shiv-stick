#pragma once
#include "ui.h"

namespace shiv {
namespace draw {
using namespace ui;

// Self-update overlay card: offer (confirm), install progress, done, or failure. One screen, two buttons.
inline void otaCard(Canvas& c, const State& st, const Theme& th) {
  const Ota& o = st.ota;
  uint8_t ph = otaui::phase(st, st.now);
  c.fillScreen(th.bg);
  char line[40];
  if (ph == otaui::INSTALLING) {
    header(c, st, th, "UPDATING");
    text(c, BODY, "DO NOT UNPLUG", W / 2, CONTENT_Y + 18, lgfx::v1::middle_center, th.warn);
    bar(c, 20, H / 2 + 2, W - 40, 10, o.prog / 100.0f, th.acc, th.panel, 6);
    snprintf(line, sizeof(line), "%u%%", (unsigned)o.prog);
    text(c, BODY, line, W / 2, H / 2 + 26, lgfx::v1::middle_center, th.fg);
    return;
  }
  if (ph == otaui::DONE) {
    header(c, st, th, "UPDATED");
    text(c, TITLE, o.ver, W / 2, H / 2 - 6, lgfx::v1::middle_center, th.acc2);
    text(c, BODY, "RESTARTING", W / 2, H / 2 + 24, lgfx::v1::middle_center, th.fg);
    return;
  }
  if (ph == otaui::FAILED) {
    header(c, st, th, "UPDATE FAILED");
    text(c, BODY, o.err, W / 2, H / 2 - 4, lgfx::v1::middle_center, th.warn);
    text(c, BODY, "WILL RETRY", W / 2, H / 2 + 20, lgfx::v1::middle_center, th.dim);
    return;
  }
  header(c, st, th, "UPDATE READY");
  snprintf(line, sizeof(line), "%s to %s", st.sys.version, o.ver);
  text(c, BODY, line, W / 2, CONTENT_Y + 20, lgfx::v1::middle_center, th.fg);
  text(c, BODY, "SIDE = later", W / 2, CONTENT_Y + 46, lgfx::v1::middle_center, th.dim);
  footer(c, th, "INSTALL", nullptr, nullptr);
}

}  // namespace draw
}  // namespace shiv
