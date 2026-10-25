// Host-side proof for the LED pattern engine (no Arduino dependencies).
// Build: g++ -std=c++17 -I../src test_led_pattern.cpp ../src/LedPattern.cpp -o test_led
#include <cstdio>
#include <cstdlib>
#include "LedPattern.h"

static int failures = 0;

static void expect(bool cond, const char* what) {
  if (!cond) {
    std::printf("FAIL: %s\n", what);
    failures++;
  }
}

int main() {
  // Boot: three flashes, then dark.
  {
    LedPattern p;
    p.boot(1000);
    expect(p.level(1000), "boot first flash on");
    expect(!p.level(1070), "boot gap after first flash");
    expect(p.level(1180), "boot second flash on");
    expect(!p.level(1250), "boot second gap");
    expect(p.level(1360), "boot third flash on");
    expect(!p.level(1440), "boot dark after three flashes");
  }

  // Scan yield encodes 1/2/3 pulses.
  {
    LedPattern p;
    p.activity(5000, 1);
    expect(p.level(5000), "1 record: first pulse");
    expect(!p.level(5030), "1 record: gap");
    expect(!p.level(5200), "1 record: only one pulse");
  }
  {
    LedPattern p;
    p.activity(5000, 7);
    int edges = 0;
    bool prev = false;
    for (uint32_t t = 5000; t < 5500; t += 5) {
      bool l = p.level(t);
      if (l && !prev) edges++;
      prev = l;
    }
    expect(edges == 2, "7 records -> two pulses");
  }
  {
    LedPattern p;
    p.activity(5000, 42);
    int edges = 0;
    bool prev = false;
    for (uint32_t t = 5000; t < 5600; t += 5) {
      bool l = p.level(t);
      if (l && !prev) edges++;
      prev = l;
    }
    expect(edges == 3, "42 records -> three pulses");
  }

  // Heartbeat keeps a quiet node visibly alive, but stays dark between beats.
  {
    LedPattern p;
    p.boot(0);
    for (uint32_t t = 0; t < 7000; t += 5) p.level(t);
    bool beat = false;
    for (uint32_t t = 7500; t < 7600; t += 5)
      if (p.level(t)) beat = true;
    expect(beat, "heartbeat flashes in quiet period");
  }

  // Fault beacon: two flashes per period, distinct from heartbeat.
  {
    LedPattern p;
    p.set_fault(1000, true);
    int edges = 0;
    bool prev = false;
    for (uint32_t t = 1000; t < 2000; t += 5) {
      bool l = p.level(t);
      if (l && !prev) edges++;
      prev = l;
    }
    expect(edges == 2, "fault shows two flashes per period");
    p.set_fault(3000, false);
    expect(!p.level(3050), "fault clears to dark");
  }

  if (failures == 0) std::printf("PASS: led pattern\n");
  return failures == 0 ? 0 : 1;
}
