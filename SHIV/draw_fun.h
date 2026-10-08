// SHIV - level, oracle, games (logic + drawing), daemon
#pragma once
#include "ui.h"
#include "pet.h"

namespace shiv {

// ================================================================ game logic (pure)
namespace game {

inline void runnerReset(Runner& r) {
  r.x = W / 2; r.speed = 1.6f; r.score = 0; r.dead = false; r.started = true; r.frame = 0;
  for (auto& o : r.ob) o.live = false;
}

inline void runnerStep(Runner& r, const GameInput& in, uint32_t& rng) {
  if (r.dead) return;
  r.frame++;
  r.x += in.tiltX * 6.5f;
  if (r.x < 10) r.x = 10;
  if (r.x > W - 10) r.x = W - 10;
  r.speed = 1.6f + r.frame / 600.0f;
  if (r.speed > 5.0f) r.speed = 5.0f;
  int gap = (int)(34 - r.speed * 4);
  if (gap < 14) gap = 14;
  if (r.frame % gap == 0) {
    for (auto& o : r.ob) if (!o.live) {
      o.live = true; o.w = 28 + rnd(rng) % 44; o.x = rnd(rng) % (W - (int)o.w); o.y = -12;
      break;
    }
  }
  for (auto& o : r.ob) {
    if (!o.live) continue;
    o.y += r.speed;
    if (o.y > H) { o.live = false; r.score += 5; continue; }
    // ship hit-box: 14 wide, y 112..126
    if (o.y + 12 > 113 && o.y < 126 && r.x + 6 > o.x && r.x - 6 < o.x + o.w) r.dead = true;
  }
  if (r.frame % 6 == 0) r.score++;
  if (r.dead && r.score > r.best) r.best = r.score;
}

inline void snakeFood(Snake& s, uint32_t& rng) {
  for (int tries = 0; tries < 200; tries++) {
    int fx = rnd(rng) % Snake::GW, fy = rnd(rng) % Snake::GH;
    bool hit = false;
    for (int i = 0; i < s.len; i++) if (s.bx[i] == fx && s.by[i] == fy) { hit = true; break; }
    if (!hit) { s.fx = fx; s.fy = fy; return; }
  }
}

inline void snakeReset(Snake& s, uint32_t now, uint32_t& rng) {
  s.len = 3; s.dx = 1; s.dy = 0; s.score = 0; s.dead = false; s.started = true; s.lastStep = now;
  for (int i = 0; i < 3; i++) { s.bx[i] = 6 - i; s.by[i] = 4; }
  snakeFood(s, rng);
}

inline void snakeStep(Snake& s, const GameInput& in, uint32_t now, uint32_t& rng) {
  if (s.dead) return;
  float ax = fabsf(in.tiltX), ay = fabsf(in.tiltY);
  if (ax > 0.22f || ay > 0.22f) {
    int ndx = 0, ndy = 0;
    if (ax >= ay) ndx = in.tiltX > 0 ? 1 : -1; else ndy = in.tiltY > 0 ? 1 : -1;
    if (!(ndx == -s.dx && ndy == -s.dy)) { s.dx = ndx; s.dy = ndy; }
  }
  uint32_t period = 230 > s.len * 4 ? 230 - s.len * 4 : 90;
  if (period < 90) period = 90;
  if (ago(now, s.lastStep) < period) return;
  s.lastStep = now;
  int nx = (s.bx[0] + s.dx + Snake::GW) % Snake::GW;
  int ny = (s.by[0] + s.dy + Snake::GH) % Snake::GH;
  for (int i = 0; i < s.len - 1; i++) if (s.bx[i] == nx && s.by[i] == ny) {
    s.dead = true;
    if (s.score > s.best) s.best = s.score;
    return;
  }
  bool eat = (nx == s.fx && ny == s.fy);
  if (eat && s.len < 178) s.len++;
  for (int i = s.len - 1; i > 0; i--) { s.bx[i] = s.bx[i - 1]; s.by[i] = s.by[i - 1]; }
  s.bx[0] = nx; s.by[0] = ny;
  if (eat) { s.score += 10; snakeFood(s, rng); }
}

// returns 1 when GO fires (for a beep)
inline int reflexStep(Reflex& r, const GameInput& in, uint32_t now, uint32_t& rng) {
  switch (r.phase) {
    case 0: case 3: case 4:
      if (in.frontClick) { r.phase = 1; r.goAt = now + 1500 + rnd(rng) % 2800; }
      break;
    case 1:
      if (in.frontClick) { r.phase = 4; r.shownAt = now; }
      else if (now >= r.goAt) { r.phase = 2; r.shownAt = now; return 1; }
      break;
    case 2:
      if (in.frontClick || in.front) {
        r.lastMs = now - r.shownAt;
        if (!r.bestMs || r.lastMs < r.bestMs) r.bestMs = r.lastMs;
        r.phase = 3;
      } else if (now - r.shownAt > 3000) { r.phase = 0; }
      break;
  }
  return 0;
}

}  // namespace game

// ================================================================ drawing
namespace draw {

using namespace ui;

// ---------------------------------------------------------------- bubble level
inline void level(Canvas& c, const State& st, const Theme& th) {
  header(c, st, th, "LEVEL");
  const Level& l = st.lvl;
  bool flat = fabsf(l.roll) < 0.6f && fabsf(l.pitch) < 0.6f;
  int cx = 56, cy = 82, R = 46;
  Col ring = flat ? th.ok : th.acc;
  c.drawCircle(cx, cy, R, ring);
  c.drawCircle(cx, cy, R - 1, ring);
  c.drawCircle(cx, cy, 14, th.dim);
  c.drawFastHLine(cx - R, cy, 2 * R, th.panel);
  c.drawFastVLine(cx, cy - R, 2 * R, th.panel);
  float bx = l.roll / 20.0f, by = l.pitch / 20.0f;
  float m = sqrtf(bx * bx + by * by);
  if (m > 1) { bx /= m; by /= m; }
  c.fillCircle(cx + (int)(bx * (R - 12)), cy + (int)(by * (R - 12)), 11, flat ? th.ok : th.acc2);
  char b[12];
  float rr = l.roll > 90 ? 90 : l.roll < -90 ? -90 : l.roll, pp = l.pitch > 90 ? 90 : l.pitch < -90 ? -90 : l.pitch;
  text(c, BODY, "ROLL", W - 6, 40, lgfx::v1::middle_right, th.dim);
  snprintf(b, sizeof(b), "%+.1f", rr);
  text(c, BIG, b, W - 6, 64, lgfx::v1::middle_right, flat ? th.ok : th.fg);
  text(c, BODY, "PITCH", W - 6, 93, lgfx::v1::middle_right, th.dim);
  snprintf(b, sizeof(b), "%+.1f", pp);
  text(c, BIG, b, W - 6, 117, lgfx::v1::middle_right, flat ? th.ok : th.fg);
}

// ---------------------------------------------------------------- oracle
static const char* const ORACLE_MODES[4] = {"8-BALL", "DIE 6", "DIE 20", "COIN"};
static const char* const EIGHT[][2] = {
  {"YES.", "DO IT NOW"},        {"NO.", "NOT A CHANCE"},     {"MAYBE", "ASK THE DAEMON"},
  {"LIKELY", "SIGNAL STRONG"},  {"DOUBTFUL", "TOO MUCH NOISE"}, {"404", "NOT FOUND"},
  {"RETRY", "PACKET LOST"},     {"ACCESS", "GRANTED"},       {"ACCESS", "DENIED"},
  {"ABORT", "BAD IDEA, CHOOM"}, {"SEND IT", "NO ROLLBACK"},  {"LATER", "SLEEP ON IT"},
  {"OBVIOUSLY", "WHY EVEN ASK"}, {"NEGATIVE", "ODDS ARE BAD"}, {"ROOT SAYS", "YES"},
  {"ROOT SAYS", "NO"},
};
static const int EIGHT_N = sizeof(EIGHT) / sizeof(EIGHT[0]);

inline void oracleRoll(Oracle& o, uint32_t& rng, uint32_t now) {
  o.rolledAt = now;
  o.rolling = true;
  uint32_t r = rnd(rng);
  switch (o.mode) {
    case 0: { int k = r % EIGHT_N; o.value = k; scopy(o.line1, EIGHT[k][0]); scopy(o.line2, EIGHT[k][1]); break; }
    case 1: o.value = 1 + r % 6;  snprintf(o.line1, sizeof(o.line1), "%d", o.value); o.line2[0] = 0; break;
    case 2: o.value = 1 + r % 20; snprintf(o.line1, sizeof(o.line1), "%d", o.value);
            scopy(o.line2, o.value == 20 ? "CRITICAL HIT" : o.value == 1 ? "CRITICAL FAIL" : ""); break;
    default: o.value = r & 1; scopy(o.line1, o.value ? "HEADS" : "TAILS"); o.line2[0] = 0; break;
  }
}

inline void oracle(Canvas& c, const State& st, const Theme& th) {
  const Oracle& o = st.orc;
  header(c, st, th, ORACLE_MODES[o.mode % 4]);
  bool spin = o.rolling && ago(st.now, o.rolledAt) < 700;
  int cy = 72;
  if (spin) {
    uint32_t s = st.now / 60 + 7;
    for (int i = 0; i < 9; i++) {
      s = s * 1664525UL + 1013904223UL;
      c.fillRect(30 + i * 20, cy - 14 + (int)((s >> 10) % 10), 16, 8 + (int)((s >> 16) % 14), (i & 1) ? th.acc : th.acc2);
    }
  } else if (!o.rolledAt) {
    text(c, BIG, "SHAKE ME", W / 2, cy - 4, lgfx::v1::middle_center, th.fg);
  } else if (o.mode == 1 || o.mode == 2) {
    text(c, SEG, o.line1, W / 2, 31, lgfx::v1::top_center, th.acc);
    if (o.line2[0]) text(c, BODY, o.line2, W / 2, 97, lgfx::v1::middle_center, o.value == 20 ? th.ok : th.bad);
  } else {
    textFit(c, BIG, o.line1, W / 2, o.line2[0] ? 54 : 68, W - 8, lgfx::v1::middle_center, th.acc);
    if (o.line2[0]) textFit(c, BODY, o.line2, W / 2, 90, W - 8, lgfx::v1::middle_center, th.fg);
  }
  footer(c, th, "ROLL", "MODE");
}

// ---------------------------------------------------------------- games
static const char* const GAME_NAMES[3] = {"NEON RUN", "SNAKE", "REFLEX"};

inline void gameOver(Canvas& c, const Theme& th, uint32_t score, uint32_t best) {
  c.fillScreen(th.bg);
  brackets(c, 14, 8, W - 28, 96, th.acc2, 14);
  text(c, HEAD, "GAME OVER", W / 2, 32, lgfx::v1::middle_center, th.bad);
  char b[24];
  snprintf(b, sizeof(b), "SCORE %lu", (unsigned long)score);
  text(c, BODY, b, W / 2, 62, lgfx::v1::middle_center, th.fg);
  snprintf(b, sizeof(b), "BEST %lu", (unsigned long)best);
  text(c, BODY, b, W / 2, 84, lgfx::v1::middle_center, score >= best && score ? th.ok : th.dim);
  footer(c, th, "AGAIN", "QUIT");
}

inline void runner(Canvas& c, const State& st, const Theme& th) {
  const Runner& r = st.games.run;
  // star streaks
  for (int i = 0; i < 14; i++) {
    int x = (i * 53 + 17) % W;
    int y = (int)((i * 37 + r.frame * (1 + i % 3)) % H);
    c.drawFastVLine(x, y, 4 + i % 3 * 3, th.panel);
  }
  for (const auto& o : r.ob) if (o.live) {
    c.fillRect((int)o.x, (int)o.y, (int)o.w, 12, th.acc2);
    c.fillRect((int)o.x + 2, (int)o.y + 2, (int)o.w - 4, 3, th.bg);
  }
  int x = (int)r.x;
  c.fillTriangle(x, 108, x - 9, 128, x + 9, 128, th.acc);
  c.fillTriangle(x, 116, x - 4, 128, x + 4, 128, th.bg);
  if ((r.frame / 3) & 1) c.fillRect(x - 3, 129, 6, 5, th.warn);
  if (r.dead && r.started) { gameOver(c, th, r.score, r.best); return; }
  char b[16];
  snprintf(b, sizeof(b), "%lu", (unsigned long)r.score);
  text(c, BODY, b, 5, 12, lgfx::v1::middle_left, th.fg);
}

inline void snake(Canvas& c, const State& st, const Theme& th) {
  const Snake& s = st.games.snake;
  if (s.dead && s.started) { gameOver(c, th, s.score, s.best); return; }
  const int oy = 27, CELL = Snake::CELL;
  c.fillRect(0, 0, W, 26, th.panel);
  char b[16];
  snprintf(b, sizeof(b), "SNAKE %lu", (unsigned long)s.score);
  text(c, BODY, b, 6, 13, lgfx::v1::middle_left, th.acc);
  text(c, BODY, "TILT", W - 6, 13, lgfx::v1::middle_right, th.dim);
  c.fillRect(s.fx * CELL + 2, oy + s.fy * CELL + 2, CELL - 4, CELL - 4, th.acc2);
  for (int i = s.len - 1; i >= 0; i--) {
    Col col = i == 0 ? th.fg : mix(th.ok, th.panel, (float)i / (s.len + 6));
    c.fillRect(s.bx[i] * CELL + 1, oy + s.by[i] * CELL + 1, CELL - 2, CELL - 2, col);
  }
  if (s.dead && s.started) gameOver(c, th, s.score, s.best);
}

// SHIV-16: a full-screen neon spectrum that dances to the room (the mic feeds st.mic.band).
inline void spectrum(Canvas& c, const State& st, const Theme& th) {
  const char* quit = st.screen == SC_SPECTRUM ? nullptr : "QUIT";
  if (!st.set.micOn) {
    text(c, TITLE, "SPECTRUM", W / 2, 44, lgfx::v1::middle_center, th.acc);
    text(c, BODY, "TURN THE MIC ON", W / 2, 82, lgfx::v1::middle_center, th.dim);
    footer(c, th, nullptr, quit);
    return;
  }
  const int B = micdsp::BANDS;
  const int top = 22, bottom = H - 16, barH = bottom - top, gap = 4;
  const int bw = (W - gap * (B + 1)) / B;
  bool live = st.mic.active && ago(st.now, st.mic.frameAt) < 1500;
  for (int i = 0; i < B; i++) {
    int x = gap + i * (bw + gap);
    int h = live ? (int)((uint32_t)st.mic.band[i] * barH / 255) : 2;
    if (h < 2) h = 2;
    c.fillRect(x, top, bw, barH, th.panel);                 // dim column
    c.fillRect(x, bottom - h, bw, h, (i & 1) ? th.acc : th.acc2);
  }
  text(c, TITLE, "SPECTRUM", W / 2, 10, lgfx::v1::top_center, th.fg);
  if (!live) text(c, BODY, "listening...", W / 2, H / 2, lgfx::v1::middle_center, th.dim);
  footer(c, th, nullptr, quit);
}
inline void reflex(Canvas& c, const State& st, const Theme& th) {
  const Reflex& r = st.games.reflex;
  const char* quit = st.screen == SC_REFLEX ? nullptr : "QUIT";   // on its own page you leave with SIDE
  char b[24];
  switch (r.phase) {
    case 1:
      text(c, TITLE, "WAIT..", W / 2, 56, lgfx::v1::middle_center, th.dim);
      text(c, BODY, "DON'T PRESS YET", W / 2, 100, lgfx::v1::middle_center, th.dim);
      break;
    case 2:
      c.fillScreen(th.ok);
      text(c, XL, "NOW!", W / 2, H / 2, lgfx::v1::middle_center, th.bg);
      break;
    case 3:
      snprintf(b, sizeof(b), "%lu", (unsigned long)r.lastMs);
      text(c, SEG, b, W / 2 - 22, 8, lgfx::v1::top_center, r.lastMs <= r.bestMs ? th.ok : th.acc);
      text(c, BODY, "ms", W / 2 + 22 + textW(c, SEG, b) / 2 - 18, 44, lgfx::v1::middle_left, th.dim);
      snprintf(b, sizeof(b), "BEST %lu ms", (unsigned long)r.bestMs);
      text(c, BODY, b, W / 2, 84, lgfx::v1::middle_center, th.fg);
      footer(c, th, "AGAIN", quit);
      break;
    case 4:
      text(c, TITLE, "TOO SOON", W / 2, 50, lgfx::v1::middle_center, th.bad);
      footer(c, th, "AGAIN", quit);
      break;
    default:
      text(c, TITLE, "REFLEX", W / 2, 36, lgfx::v1::middle_center, th.acc);
      text(c, BODY, "PRESS ON GREEN", W / 2, 82, lgfx::v1::middle_center, th.fg);
      footer(c, th, "ARM", quit);
      break;
  }
}

inline void games(Canvas& c, const State& st, const Theme& th) {
  const Games& g = st.games;
  if (g.active == 0) { runner(c, st, th); return; }
  if (g.active == 1) { snake(c, st, th); return; }
  if (g.active == 2) { reflex(c, st, th); return; }
  bool focus = st.sys.focus && st.screen == SC_GAMES;
  header(c, st, th, "GAMES", focus);
  list(c, th, CONTENT_Y + 1, 27, 3, 3, g.sel, focus,
       [&](int i, char* l, size_t ln, char* r, size_t rn, Col& col) {
         snprintf(l, ln, "%s", GAME_NAMES[i]);
         unsigned long best = i == 0 ? g.run.best : i == 1 ? g.snake.best : g.reflex.bestMs;
         if (best) snprintf(r, rn, "%lu", best);
         (void)col;
       });
  if (focus) footer(c, th, "PLAY", "BACK");
  else footer(c, th, "ENTER", nullptr);
}

// ---------------------------------------------------------------- daemon
static const char* const DAEMON_BODY[11] = {
  "..X........X..",
  ".XX........XX.",
  ".XXXXXXXXXXXX.",
  "XXXXXXXXXXXXXX",
  "XXXXXXXXXXXXXX",
  "XXXXXXXXXXXXXX",
  "XXXXXXXXXXXXXX",
  "XXXXXXXXXXXXXX",
  "XXXXXXXXXXXXXX",
  "XX.XXX..XXX.XX",
  "X...X....X...X",
};

inline Col daemonColor(const Theme& th, int mood) {
  switch (mood) {
    case MD_DRAINED: case MD_HANGRY: case MD_IRKED: return th.warn;
    case MD_ANGRY: return th.bad;
    case MD_ALERT: return th.acc2;
    case MD_LONELY: case MD_SULKING: return th.dim;
    case MD_HYPED: return th.ok;
    default: return th.acc;
  }
}

inline void daemonHeart(Canvas& c, int x, int y, Col col) {
  c.fillRect(x, y, 3, 3, col); c.fillRect(x + 4, y, 3, 3, col);
  c.fillRect(x, y + 2, 7, 2, col); c.fillRect(x + 1, y + 4, 5, 1, col); c.fillRect(x + 2, y + 5, 3, 1, col); c.fillRect(x + 3, y + 6, 1, 1, col);
}

inline void daemonSprite(Canvas& c, const Theme& th, int x, int y, const Daemon& d, uint32_t now) {
  const int P = 4;
  int mood = d.mood;
  uint32_t actMs = ago(now, d.actAt);
  int bob = ((now / 450) & 1) ? 0 : 2;
  if (mood == MD_SLEEPY || mood == MD_SULKING) bob = 2;
  if (mood == MD_ANGRY) x += ((now / 45) & 1) ? 2 : -2;
  if (mood == MD_DIZZY) x += (int)lroundf(sinf(now / 120.0f) * 3);
  if (mood == MD_HYPED) bob = -(int)lroundf(fabsf(sinf(actMs / 190.0f)) * 8);
  if (d.act == PA_FLINCH) bob = 4;
  y += bob;
  Col body = daemonColor(th, mood);
  for (int r = 0; r < 11; r++)
    for (int k = 0; k < 14; k++)
      if (DAEMON_BODY[r][k] == 'X') c.fillRect(x + k * P, y + r * P, P, P, body);

  if (mood == MD_SULKING) {                      // back turned: no face, just a spine and a huff
    for (int i = 0; i < 3; i++) c.fillRect(x + 6 * P + 2, y + (3 + i * 2) * P, P, P, th.bg);
    int k = (now / 500) % 3;
    for (int i = 0; i <= k; i++) c.fillCircle(x + 14 * P + 3 + i * 4, y + 5 * P - i * 3, 1 + (i > 1), th.dim);
    return;
  }

  bool blink = (now % 3400) < 140;
  int ey = y + 4 * P;
  int ex[2] = {x + 3 * P, x + 9 * P};
  bool closed = mood == MD_SLEEPY || blink;
  bool smileEyes = mood == MD_HAPPY || mood == MD_HYPED || mood == MD_EATING;
  bool crossEyes = mood == MD_DRAINED || mood == MD_DIZZY;
  int lx = fabsf(d.lookX) > 0.15f ? (int)lroundf(d.lookX * 2) : ((int)((now / 1100) % 3) - 1) * 2;
  int ly = (int)lroundf(d.lookY * 2);
  lx = lx < -2 ? -2 : lx > 2 ? 2 : lx;
  ly = ly < -2 ? -2 : ly > 2 ? 2 : ly;
  for (int e = 0; e < 2; e++) {
    int x0 = ex[e];
    if (closed) {
      c.fillRect(x0, ey + P, 2 * P, P - 1, th.bg);
    } else if (crossEyes) {
      if (mood == MD_DIZZY && ((now / 140) & 1)) {
        c.fillRect(x0 + P - 1, ey, 2, 2 * P, th.bg); c.fillRect(x0, ey + P - 1, 2 * P, 2, th.bg);
      } else {
        c.drawLine(x0, ey, x0 + 2 * P - 1, ey + 2 * P - 1, th.bg); c.drawLine(x0 + 2 * P - 1, ey, x0, ey + 2 * P - 1, th.bg);
        c.drawLine(x0 + 1, ey, x0 + 2 * P, ey + 2 * P - 1, th.bg); c.drawLine(x0 + 2 * P, ey, x0 + 1, ey + 2 * P - 1, th.bg);
      }
    } else if (smileEyes) {
      c.fillRect(x0, ey, 2 * P, 2 * P, th.bg);
      c.fillRect(x0, ey + P, 2 * P, P, body);
      c.fillRect(x0 + P / 2, ey + P - 2, P, 2, th.bg);
    } else {
      int tall = mood == MD_ALERT ? P : 0;
      c.fillRect(x0, ey, 2 * P, 2 * P + tall, th.bg);
      int py = ey + P / 2 + 1 + ly;
      if (mood == MD_IRKED || mood == MD_BORED) {   // heavy lids; the irked one twitches
        int lid = 3 + ((mood == MD_IRKED && e == 1 && ((now / 180) & 1)) ? 1 : 0);
        if (py < ey + lid + 1) py = ey + lid + 1;
        c.fillRect(x0 + P / 2 + 1 + lx, py, P - 1, P - 1, th.fg);
        c.fillRect(x0, ey, 2 * P, lid, body);
      } else {
        c.fillRect(x0 + P / 2 + 1 + lx, py, P - 1, P - 1, mood == MD_ANGRY ? th.bad : th.fg);
        if (mood == MD_ANGRY || mood == MD_HANGRY) {  // brows slanting down to the middle
          if (e == 0) c.fillTriangle(x0, ey - 1, x0 + 2 * P, ey - 1, x0 + 2 * P, ey + P + 1, body);
          else c.fillTriangle(x0 - 1, ey - 1, x0 + 2 * P - 1, ey - 1, x0 - 1, ey + P + 1, body);
        }
      }
    }
  }

  int my = y + 8 * P;
  switch (mood) {
    case MD_HAPPY: case MD_HYPED:
      c.fillRect(x + 5 * P, my - P, 4 * P, P, th.bg); c.fillRect(x + 5 * P + 2, my - P, 4 * P - 4, 2, body); break;
    case MD_ALERT: c.fillRect(x + 6 * P, my - P - 2, 2 * P, P + 2, th.bg); break;
    case MD_EATING:
      if ((now / 160) & 1) c.fillRect(x + 5 * P, my - P - 2, 4 * P, P + 3, th.bg);
      else c.fillRect(x + 5 * P + 2, my - P + 1, 4 * P - 4, 2, th.bg);
      for (int i = 0; i < 3; i++) c.fillRect(x + 5 * P + 1 + i * 6, my + 2 + (int)((now / 40 + i * 7) % 12), 2, 2, th.fg);
      break;
    case MD_ANGRY: case MD_HANGRY:
      c.fillRect(x + 4 * P + 2, my - P - 1, 5 * P, P + 1, th.bg);
      for (int i = 0; i < 4; i++) c.fillRect(x + 4 * P + 4 + i * 5, my - P - 1, 2, 2, th.fg);
      break;
    case MD_DIZZY:
      for (int i = 0; i < 4; i++) c.fillRect(x + 5 * P + i * P, my - P + (i & 1) * 2, P, 2, th.bg);
      break;
    case MD_SLEEPY: break;
    default:
      c.fillRect(x + 5 * P + 2, my - P + 1, 4 * P - 4, 2, th.bg);
      if (mood == MD_IRKED || mood == MD_LONELY || mood == MD_DRAINED) {   // corners pulled down
        c.fillRect(x + 5 * P, my - P + 3, 2, 2, th.bg); c.fillRect(x + 9 * P - 2, my - P + 3, 2, 2, th.bg);
      }
      if (mood == MD_HUNGRY) c.fillRect(x + 9 * P - 3, my - P + 3, 2, 2 + (int)((now / 300) % 5), th.fg);   // drool
      break;
  }

  if (mood == MD_SLEEPY) {
    int k = (now / 600) % 3;
    for (int i = 0; i <= k; i++) c.drawRect(x + 50 + i * 5, y + 6 - i * 5, 4 + i, 4 + i, th.dim);
  } else if (mood == MD_LONELY) {
    c.fillRect(ex[0] + 2, ey + 2 * P + 1 + (int)((now / 120) % 10), 2, 3, th.acc);
  } else if (mood == MD_ANGRY) {                  // the cartoon vein
    int vx = x + 12 * P, vy = y + P - 2;
    c.fillRect(vx, vy + 2, 3, 2, th.bad); c.fillRect(vx + 5, vy + 2, 3, 2, th.bad);
    c.fillRect(vx + 3, vy - 1, 2, 3, th.bad); c.fillRect(vx + 3, vy + 4, 2, 3, th.bad);
  } else if (mood == MD_DIZZY) {
    for (int i = 0; i < 3; i++) {
      float a = now / 150.0f + i * 2.094f;
      c.fillRect(x + 7 * P - 1 + (int)lroundf(cosf(a) * 22), y - 5 + (int)lroundf(sinf(a) * 3), 3, 3, th.warn);
    }
  } else if (mood == MD_HYPED) {
    for (int i = 0; i < 4; i++) {
      uint32_t h = (now / 200 + i) * 2654435761UL;
      int sx = x + (int)(h % 56), sy = y - 6 + (int)((h >> 8) % 14);
      c.drawFastHLine(sx - 2, sy, 5, th.warn); c.drawFastVLine(sx, sy - 2, 5, th.warn);
    }
  }
  if (mood == MD_EATING || (mood == MD_HAPPY && d.fun > 80 && d.food > 60))
    daemonHeart(c, x + 14 * P + 1, y + 5 * P - (int)((now / 70) % 22), th.acc2);
}

static const int PET_BUBBLE_X = 72, PET_TEXT_W = W - 6 - (PET_BUBBLE_X + 6), PET_LINES = 3;

inline void daemonMeter(Canvas& c, const Theme& th, int x, int cy, int w, int v, bool heart) {
  if (heart) daemonHeart(c, x + 2, cy - 4, th.acc2);
  else { c.fillCircle(x + 5, cy, 5, th.warn); c.fillRect(x + 3, cy - 2, 2, 2, th.bg); c.fillRect(x + 6, cy + 1, 2, 2, th.bg); c.fillRect(x + 7, cy - 3, 2, 2, th.bg); }
  int bx = x + 14, bw = w - 14;
  c.drawRect(bx, cy - 5, bw, 10, th.dim);
  int fw = (bw - 4) * v / 100;
  if (fw > 0) c.fillRect(bx + 2, cy - 3, fw, 6, v < 15 ? th.bad : v < 35 ? th.warn : th.ok);
}

inline void daemon(Canvas& c, const State& st, const Theme& th) {
  header(c, st, th, "DAEMON");
  const Daemon& d = st.dmn;
  int mood = d.mood % MD_N;
  daemonSprite(c, th, 4, CONTENT_Y + 10, d, st.now);

  // speech bubble
  const char* say = d.say[0] ? d.say : d.idle[0] ? d.idle : pet::MOOD_SAY[mood][0];
  Col edge = mood == MD_ANGRY ? th.bad : (mood == MD_IRKED || mood == MD_HANGRY) ? th.warn : th.dim;
  int bx = PET_BUBBLE_X, by = CONTENT_Y + 1, bw = W - 3 - bx, bh = 61;
  c.drawRoundRect(bx, by, bw, bh, 5, edge);
  c.fillTriangle(bx, by + 38, bx, by + 48, bx - 7, by + 43, th.bg);
  c.drawLine(bx, by + 38, bx - 7, by + 43, edge); c.drawLine(bx - 7, by + 43, bx, by + 48, edge);
  char lines[PET_LINES + 1][40];
  int n = wrap(c, BODY, say, PET_TEXT_W, lines, PET_LINES + 1);
#ifdef SHIV_HOST
  if (n > PET_LINES) notes().push_back(std::string("pet line needs more than 3 rows: ") + say);
  { char w[64]; int k = 0;
    for (const char* p = say;; p++) {
      if (*p && *p != ' ') { if (k < 63) w[k++] = *p; continue; }
      w[k] = 0;
      if (k && textW(c, BODY, w) > PET_TEXT_W) notes().push_back(std::string("pet word wider than the bubble: ") + w);
      k = 0;
      if (!*p) break;
    } }
#endif
  if (n > PET_LINES) n = PET_LINES;
  for (int i = 0; i < n; i++)
    text(c, BODY, lines[i], bx + 6, by + bh / 2 + 2 + (2 * i - (n - 1)) * 19 / 2, lgfx::v1::middle_left, mood == MD_SULKING ? th.dim : th.fg);

  // mood + the two needs
  int cy = H - FOOTER_H - 10;
  Col mc = mood == MD_ANGRY ? th.bad : (mood == MD_IRKED || mood == MD_HANGRY || mood == MD_DRAINED || mood == MD_HUNGRY) ? th.warn
         : (mood == MD_HAPPY || mood == MD_HYPED || mood == MD_EATING) ? th.ok : th.acc2;
  int tw = text(c, BODY, pet::MOOD_NAMES[mood], 6, cy, lgfx::v1::middle_left, mc);
  int x0 = 6 + tw + 10, each = (W - 6 - x0 - 8) / 2;
  if (each >= 30) {
    daemonMeter(c, th, x0, cy, each, d.food, false);
    daemonMeter(c, th, x0 + each + 8, cy, each, d.fun, true);
  }
  footer(c, th, "POKE", "FEED");
}

}  // namespace draw
}  // namespace shiv
