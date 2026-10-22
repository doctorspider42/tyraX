#include <cstdio>
#include <kernel.h>
#include <timer.h>
#include <graph.h>

namespace {
using U = unsigned;
struct Edge { U tick, startAge, endAge, startMode, endMode; };
volatile Edge edges[2][128];
volatile U counts[2] = {};
inline U tick() { U t; asm volatile("mfc0 %0,$9" : "=r"(t)); return t; }
inline volatile U& reg(U a) { return *reinterpret_cast<volatile U*>(a); }
int capture(U which) {
  U n = counts[which];
  if (n < 128) {
    edges[which][n].tick = tick();
    edges[which][n].startAge = *T0_COUNT;
    edges[which][n].endAge = *T1_COUNT;
    edges[which][n].startMode = *T0_MODE;
    edges[which][n].endMode = *T1_MODE;
    counts[which] = n + 1;
  }
  return 0;
}
int startHandler(int) { return capture(0); }
int endHandler(int) { return capture(1); }
struct Range {
  U n = 0, lo = 0xffffffff, hi = 0;
  void add(U v) { ++n; if (v < lo) lo = v; if (v > hi) hi = v; }
};
}

int main() {
  std::printf("WINDOWBOOT version=1 mode=480p copy=OFF\n");
  std::fflush(stdout);
  // Same signal as current A40, not the unknown ps2link menu signal.
  graph_set_mode(GRAPH_MODE_NONINTERLACED, GRAPH_MODE_HDTV_480P,
                 GRAPH_MODE_FRAME, GRAPH_DISABLE);
  for (U i = 0; i < 8; ++i) graph_wait_vsync();
  U mask = reg(0x1000f010), pending = reg(0x1000f000);
  U m0 = *T0_MODE, m1 = *T1_MODE;
  U c0 = *T0_COUNT, c1 = *T1_COUNT, p0 = *T0_COMP, p1 = *T1_COMP;
  std::printf("WINDOWOWN m0=%08x m1=%08x mask=%08x pending=%08x m2=%08x m3=%08x\n",
              m0, m1, mask, pending, *T2_MODE, *T3_MODE);
  // Isolated program has no other user timer clients. Reject any configured
  // timer, pending timer interrupt or enabled timer interrupt before reservation.
  if (m0 || m1 || ((mask | pending) & ((1u << 9) | (1u << 10)))) {
    std::printf("WINDOWDONE reserved=0 reason=timer-state-not-idle\n");
    std::fflush(stdout); return 0;
  }
  int sh = AddIntcHandler(INTC_VBLANK_S, startHandler, 0);
  int eh = AddIntcHandler(INTC_VBLANK_E, endHandler, 0);
  if (sh < 0 || eh < 0) {
    if (sh >= 0) RemoveIntcHandler(INTC_VBLANK_S, sh);
    if (eh >= 0) RemoveIntcHandler(INTC_VBLANK_E, eh);
    std::printf("WINDOWDONE reserved=0 reason=handler-install\n");
    std::fflush(stdout); return 0;
  }
  int restore = DIntr();
  *T0_COUNT = 0; *T1_COUNT = 0;
  *T0_MODE = 0x9e; *T1_MODE = 0xae;
  asm volatile("sync.l" ::: "memory");
  EnableIntc(INTC_VBLANK_S); EnableIntc(INTC_VBLANK_E);
  if (restore) EIntr();
  Range active, blank;
  U errors = 0, crossings = 0;
  U begin = tick();
  while (counts[0] < 120 || counts[1] < 120) {
    if (tick() - begin > 294912000u * 4) { ++errors; break; }
    U before = *T0_COUNT, other = *T1_COUNT, after = *T0_COUNT;
    if ((*T0_MODE & ~0xc00u) != 0x9e || (*T1_MODE & ~0xc00u) != 0xae) {
      ++errors; break;
    }
    if (before > after) { ++crossings; continue; }
    if (counts[0] < 3 || counts[1] < 3) continue;
    if (other > after) active.add(other - after);
    if (after > other) blank.add(after - other);
  }
  restore = DIntr();
  *T0_MODE = 0; *T1_MODE = 0;
  *T0_COUNT = c0; *T1_COUNT = c1;
  *T0_COMP = p0; *T1_COMP = p1;
  if (!(mask & (1u << 2))) DisableIntc(INTC_VBLANK_S);
  if (!(mask & (1u << 3))) DisableIntc(INTC_VBLANK_E);
  RemoveIntcHandler(INTC_VBLANK_S, sh); RemoveIntcHandler(INTC_VBLANK_E, eh);
  asm volatile("sync.l" ::: "memory");
  if (restore) EIntr();
  FILE* f = std::fopen("host:window-preflight.log", "w");
  for (U which = 0; which < 2; ++which)
    for (U i = 0; i < counts[which]; ++i) {
      const volatile auto& e = edges[which][i];
      if (f) std::fprintf(f, "EDGE kind=%u index=%u tick=%u a=%u b=%u ma=%x mb=%x\n",
          which, i, e.tick, e.startAge, e.endAge, e.startMode, e.endMode);
      std::printf("EDGE kind=%u index=%u tick=%u a=%u b=%u ma=%x mb=%x\n",
          which, i, e.tick, e.startAge, e.endAge, e.startMode, e.endMode);
    }
  std::printf("WINDOWDONE reserved=1 starts=%u ends=%u errors=%u crossings=%u activeN=%u activeLo=%u activeHi=%u blankN=%u blankLo=%u blankHi=%u restored=%u copy=OFF\n",
      counts[0], counts[1], errors, crossings, active.n, active.lo, active.hi,
      blank.n, blank.lo, blank.hi, unsigned(*T0_MODE == m0 && *T1_MODE == m1 &&
      *T0_COUNT == c0 && *T1_COUNT == c1 && *T0_COMP == p0 && *T1_COMP == p1));
  if (f) std::fclose(f);
  std::fflush(stdout);
  SleepThread();
  return 0;
}
