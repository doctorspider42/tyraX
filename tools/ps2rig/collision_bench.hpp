#pragma once
#include "file/file_utils.hpp"
#include <algorithm>
#include <stdio.h>
#ifndef TYRA_SOLVER_COMPARISON
#define TYRA_SOLVER_COMPARISON 0
#endif
namespace CB {
using U = unsigned;
using U64 = unsigned long long;
inline U ticks() {
  U t;
  asm volatile("mfc0 %0,$9" : "=r"(t)::"memory");
  return t;
}
inline int phase = 0, frame = 0;
inline bool ready = false, setup = true, finished = false;
inline bool baseline = true;
inline int solverArm = 0;
inline int added = 0, layout = 0, repeat = 0;
constexpr int warm = 150, park = 120, drive = 360, settle = 120,
              total = warm + park + drive + settle,
              phases = TYRA_SOLVER_COMPARISON ? 42 : 28;
inline U phys = 0, awake = 0, contacts = 0, drops = 0, pushDrops = 0,
         pushes = 0;
inline U generated = 0, pushCandidates = 0, pushTested = 0;
inline float carZ = 0, carSpeed = 0;
inline int carValid = 0;
struct Row {
  U frame, period, work, phys, awake, contacts, drops, pushDrops, pushes;
  float z, speed;
  U generated, pushCandidates, pushTested;
};
inline Row rows[1024];
inline int used = 0;
inline bool gas() {
  return ready && !finished && frame >= warm + park &&
         frame < warm + park + drive;
}
inline const char *state(int f) {
  return f < warm                  ? "warm"
         : f < warm + park         ? "park"
         : f < warm + park + drive ? "drive"
                                   : "settle";
}
inline void configure() {
#if TYRA_SOLVER_COMPARISON
  repeat = phase / 21;
  const int variant = (phase / 3) % 7;
  solverArm = repeat ? 2 - phase % 3 : phase % 3;
  baseline = solverArm == 0;
  added = variant == 0                   ? 0
          : variant == 1 || variant == 4 ? 32
          : variant == 2 || variant == 5 ? 64
                                         : 128;
  layout = variant >= 4;
#else
  int variant = (phase / 2) % 7;
  repeat = phase / 14;
  baseline = (phase % 2) == (repeat % 2);
  added = variant == 0                   ? 0
          : variant == 1 || variant == 4 ? 32
          : variant == 2 || variant == 5 ? 64
                                         : 128;
  layout = variant >= 4;
  solverArm = baseline ? 0 : 1;
#endif
}
inline void begin(U stalls) {
  static U prev = 0, lastStalls = 0;
  U now = ticks();
  if (prev && ready && !finished && frame >= warm && frame < total &&
      used < 1024) {
    U period = now - prev, wait = stalls - lastStalls;
    rows[used++] = {
        (U)frame,       period,    period > wait ? period - wait : 0,
        phys,           awake,     contacts,
        drops,          pushDrops, pushes,
        carZ,           carSpeed,  generated,
        pushCandidates, pushTested};
  }
  prev = now;
  lastStalls = stalls;
  phys = awake = contacts = drops = pushDrops = pushes = 0;
  generated = pushCandidates = pushTested = 0;
  if (ready && !finished)
    ++frame;
}
inline void flush() {
  char name[64];
  snprintf(name, sizeof(name), "collision-phase-%02d.csv", phase);
  FILE *f = fopen(Tyra::FileUtils::fromCwd(name).c_str(), "wb");
  if (f) {
    fprintf(f, "phase,baseline,added,layout,repeat,frame,state,period_ticks,"
               "work_ticks,physics_ticks,awake,contacts,dropped_contacts,push_"
               "overflow,pushes,car_z,car_speed,generated_contacts,push_"
               "candidates,push_tested,solver_arm\n");
    for (int i = 0; i < used; ++i) {
      auto &r = rows[i];
      fprintf(f,
              "%d,%d,%d,%d,%d,%u,%s,%u,%u,%u,%u,%u,%u,%u,%u,%.5f,%.5f,%u,%u,%u,"
              "%d\n",
              phase, baseline, added, layout, repeat, r.frame, state(r.frame),
              r.period, r.work, r.phys, r.awake, r.contacts, r.drops,
              r.pushDrops, r.pushes, r.z, r.speed, r.generated,
              r.pushCandidates, r.pushTested, solverArm);
    }
    fclose(f);
  }
  U sorted[1024], physSorted[1024];
  U64 sum = 0;
  U cap = 0, pdrop = 0, pushed = 0, aw = 0, con = 0, miss = 0;
  for (int i = 0; i < used; ++i) {
    auto &r = rows[i];
    sorted[i] = r.work;
    physSorted[i] = r.phys;
    sum += r.period;
    cap += r.drops;
    pdrop += r.pushDrops;
    pushed += r.pushes;
    aw = std::max(aw, r.awake);
    con = std::max(con, r.contacts);
    miss += r.period > 7372800u;
  }
  std::sort(sorted, sorted + used);
  std::sort(physSorted, physSorted + used);
  printf("CB phase=%d baseline=%d added=%d layout=%d repeat=%d n=%d "
         "work_p50_us=%u work_p95_us=%u phys_p95_us=%u fps_milli=%u missed=%u "
         "awake_max=%u contacts_max=%u dropped=%u push_overflow=%u pushes=%u "
         "z=%.3f speed=%.3f file=%d\n",
         phase, baseline, added, layout, repeat, used,
         used ? unsigned(sorted[used / 2] / 294.912) : 0,
         used ? unsigned(sorted[used * 95 / 100] / 294.912) : 0,
         used ? unsigned(physSorted[used * 95 / 100] / 294.912) : 0,
         sum ? unsigned(294912000000ull * used / sum) : 0, miss, aw, con, cap,
         pdrop, pushed, carZ, carSpeed, f != nullptr);
  fflush(stdout);
  used = 0;
}
struct PhysicsScope {
  U start;
  PhysicsScope() : start(ticks()) {}
  ~PhysicsScope() { phys += ticks() - start; }
};
} // namespace CB
