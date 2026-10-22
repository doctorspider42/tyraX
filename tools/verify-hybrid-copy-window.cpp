#include "tyrax2-hybrid-copy-window.hpp"
#include <cassert>
#include <cstdio>

using namespace Tyra;
static_assert(sizeof(HybridWindowBounds) == 24);
static_assert(sizeof(HybridWindowSample) == 32);

#ifdef __mips__
bool actualWindowPredicate(const HybridWindowBounds& b,
                           const HybridWindowSample& s) {
  return hybridCopyWindowReady(b, s);
}
#else
int main() {
  unsigned cases = 0, accepted = 0;
  // Hardware resets continue during arbitrarily delayed software callbacks.
  // Scale ticks down to exhaust physical edge/read/budget positions cheaply.
  for (unsigned field = 37; field <= 41; ++field)
    for (unsigned blank = 13; blank <= 17; ++blank)
      for (unsigned budget = 3; budget <= 11; ++budget)
        for (unsigned t = 0; t < field * 8; ++t)
          for (unsigned span = 2; span <= 5; ++span) {
            HybridWindowBounds b{7, 13, 42, 14, 29, budget};
            auto startAge = [field](unsigned time) { return time % field; };
            auto endAge = [field, blank](unsigned time) {
              return (time + field - blank) % field;
            };
            HybridWindowSample s{7, 0x9e, 0xae, startAge(t),
                                 endAge(t + 1), startAge(t + span), 0x9e, 0xae};
            bool ok = hybridCopyWindowReady(b, s);
            if (ok) {
              ++accepted;
              unsigned age = startAge(t + span);
              assert(age < blank && age + budget < blank);
              assert(t / field == (t + span) / field);
            }
            ++cases;
          }
  assert(accepted > 0);
  HybridWindowBounds b{7, 100, 1000, 600, 800, 20};
  HybridWindowSample s{7, 0x9e, 0xae, 3, 704, 4, 0x9e, 0xae};
  assert(hybridCopyWindowReady(b, s));
  unsigned guards = 0;
  auto rejectSample = [&](HybridWindowSample bad) {
    assert(!hybridCopyWindowReady(b, bad)); ++guards;
  };
  auto rejectBounds = [&](HybridWindowBounds bad) {
    assert(!hybridCopyWindowReady(bad, s)); ++guards;
  };
  for (unsigned bit = 0; bit < 32; ++bit) {
    if (bit == 10 || bit == 11) continue;
    auto bad = s; bad.startModeBefore ^= 1u << bit; rejectSample(bad);
    bad = s; bad.startModeAfter ^= 1u << bit; rejectSample(bad);
    bad = s; bad.endModeBefore ^= 1u << bit; rejectSample(bad);
    bad = s; bad.endModeAfter ^= 1u << bit; rejectSample(bad);
  }
  { auto bad = s; bad.epoch = 8; rejectSample(bad); }
  { auto bad = s; bad.startAgeBefore = 5; rejectSample(bad); }
  { auto bad = s; bad.endAge = 4; rejectSample(bad); }
  { auto bad = s; bad.endAge = 1000; rejectSample(bad); }
  { auto bad = s; bad.endAge = 603; rejectSample(bad); }
  { auto bad = s; bad.endAge = 805; rejectSample(bad); }
  { auto bad = s; bad.startAgeAfter = 80; rejectSample(bad); }
  { auto bad = s; bad.startAgeAfter = 100; rejectSample(bad); }
  { auto bad = b; bad.epoch = 0; rejectBounds(bad); }
  { auto bad = b; bad.minBlank = 0; rejectBounds(bad); }
  { auto bad = b; bad.maxField = 65536; rejectBounds(bad); }
  { auto bad = b; bad.maxField = 100; rejectBounds(bad); }
  { auto bad = b; bad.copyAndDispatchBudget = 0; rejectBounds(bad); }
  { auto bad = b; bad.copyAndDispatchBudget = 0xffffffff; rejectBounds(bad); }
  { auto bad = b; bad.minActiveDelta = 0; rejectBounds(bad); }
  { auto bad = b; bad.minActiveDelta = 801; rejectBounds(bad); }
  { auto bad = b; bad.maxActiveDelta = 1000; rejectBounds(bad); }
  for (unsigned flags = 0; flags < 4; ++flags) {
    auto valid = s;
    valid.startModeBefore |= flags << 10;
    valid.startModeAfter |= flags << 10;
    valid.endModeBefore |= flags << 10;
    valid.endModeAfter |= flags << 10;
    assert(hybridCopyWindowReady(b, valid));
  }
  assert(guards == 137);
  std::printf("HYBRIDWINDOW cases=%u accepted=%u guards=%u physical=UNQUALIFIED\n",
              cases, accepted, guards);
}
#endif
