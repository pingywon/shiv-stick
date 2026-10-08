// PC test for shiv::ago() - the elapsed-time helper behind dimming, sleep, timers and data ages.
// Build + run:  g++ -std=c++17 -o /tmp/test_ago tools/test_ago.cpp && /tmp/test_ago
#include "../SHIV/state.h"
#include <cstdio>
using shiv::ago;
static int bad = 0;
static void check(const char* what, uint32_t got, uint32_t want) {
  printf("  %s %-52s got %lu want %lu\n", got == want ? "ok  " : "FAIL", what, (unsigned long)got, (unsigned long)want);
  if (got != want) bad++;
}
int main() {
  check("stamp 3 ms NEWER than now (the screen-blank bug)", ago(100000, 100003), 0);
  check("old plain subtraction would have said", 100000u - 100003u, 4294967293u);
  check("idle seconds the dimmer now sees", ago(100000, 100003) / 1000, 0);
  check("normal gap", ago(105000, 100000), 5000);
  check("same instant", ago(7, 7), 0);
  check("across the 49-day millis() rollover", ago(5, 0xFFFFFFF0u), 21);
  check("20 s idle still dims (20000 ms)", ago(120000, 100000) / 1000, 20);
  printf("%s\n", bad ? "FAILED" : "all passed");
  return bad ? 1 : 0;
}
