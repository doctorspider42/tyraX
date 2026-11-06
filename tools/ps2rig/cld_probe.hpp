#pragma once
// Private night-performance probe (Claude rig, not production).
// Cheap COP0 scopes accumulated per window and printed to the ps2link console.
#include <stdio.h>
#ifndef CLD_ARMS
#define CLD_ARMS 3
#endif
namespace Cld {
using U = unsigned int;
using U64 = unsigned long long;
inline U ticks() { U t; asm volatile("mfc0 %0,$9" : "=r"(t)::"memory"); return t; }
// EE performance counters: PCR0 event 6 = I-cache miss, PCR1 event 6 = D-cache miss (all modes).
#if CLD_NOPMU
inline U pc0() { return 0; }
inline U pc1() { return 0; }
#else
inline U pc0() { U v; asm volatile("mfpc %0,0" : "=r"(v)::"memory"); return v; }
inline U pc1() { U v; asm volatile("mfpc %0,1" : "=r"(v)::"memory"); return v; }
#endif
#ifndef CLD_EV0
#define CLD_EV0 6u
#endif
inline void pmuArm() {
#if CLD_NOPMU
  return;
#endif
  U z = 0;
  asm volatile("mtps %0,0\n\tsync.p" ::"r"(z));
  asm volatile("mtpc %0,0\n\tmtpc %0,1\n\tsync.p" ::"r"(z));
  const U ctl = (1u << 31) | (CLD_EV0 << 5) | (0xFu << 1) | (6u << 15) | (0xFu << 11);
  asm volatile("mtps %0,0\n\tsync.p" ::"r"(ctl));
}
constexpr int kMax = 96;
inline const char* names[kMax];
inline U64 acc[kMax];
inline U cnt[kMax];
inline U64 im[kMax], dm[kMax];
inline U coreCalls = 0;
inline U64 frameAcc[kMax], slowAcc[kMax], fastAcc[kMax];
inline U64 frameBags[kMax], slowBags[kMax], fastBags[kMax];
inline U slowN = 0, fastN = 0;
inline U64 copyChain = 0, copyMut = 0, borrowB = 0, snaps = 0, stampHits = 0, stampCaps = 0;
inline bool (*isPool)(const void*) = nullptr; inline U64 mutPoolB = 0, mutRefs = 0;
inline U bagStack[32]; inline int bagTop = 0;
inline U routes[6]; inline U routeStack[32][6]; inline U64 routeAcc[96][6];
inline U64 bags[kMax];
inline int used = 0;
#ifdef CLD_TRACK
#ifndef CLD_TRK_NAMES
#define CLD_TRK_NAMES "T.build", "Terrain", "Roads", "Objects", "Reflections_shared_probe", "G.update"
#endif
inline const char* trkNames[6] = {CLD_TRK_NAMES};
inline U trk[6][160];
inline U trkWin = 0;
inline void trackAdd(const char* n, U dt);
#endif
inline void scope(const char* n, U dt, U di = 0, U dd = 0) {
#ifdef CLD_TRACK
  trackAdd(n, dt);
#endif
  for (int i = 0; i < used; ++i)
    if (names[i] == n) { acc[i] += dt; ++cnt[i]; im[i] += di; dm[i] += dd; frameAcc[i] += dt; return; }
  if (used < kMax) { names[used] = n; acc[used] = dt; cnt[used] = 1; im[used] = di; dm[used] = dd; frameAcc[used] = dt; ++used; }
}
inline void bagPush() { if (bagTop < 32) { bagStack[bagTop] = coreCalls; for (int r = 0; r < 6; ++r) routeStack[bagTop][r] = routes[r]; } ++bagTop; }
inline void bagPop(const char* n) {
  --bagTop; if (bagTop < 0) { bagTop = 0; return; }
  if (bagTop >= 32) return;
  const U d = coreCalls - bagStack[bagTop];
  for (int i = 0; i < used; ++i) if (names[i] == n) { bags[i] += d; frameBags[i] += d; for (int r = 0; r < 6; ++r) routeAcc[i][r] += routes[r] - routeStack[bagTop][r]; return; }
}
struct M { U t, i, d; };
inline M mark() { return M{ticks(), pc0(), pc1()}; }
inline void span(const char* n, const M& a, const M& b) { scope(n, b.t - a.t, b.i - a.i, b.d - a.d); }
struct S {
  const char* n; U t, i0, d0;
  explicit S(const char* n_) : n(n_), t(ticks()), i0(pc0()), d0(pc1()) {}
  ~S() { scope(n, ticks() - t, pc0() - i0, pc1() - d0); }
};
// Loop/state machine. state: 0 warm, 1 park, 2 drive
inline U frame = 0, prevStart = 0, prevStall = 0, stateFrame = 0;
inline int state = 0, cycle = 0, arm = 0;
inline bool gas = false, resetRequested = false, probePass = false, streamBusy = false;
inline float carZ = 0.0F, carSpeed = 0.0F;
inline U winN = 0, winLate = 0, workMax = 0;
inline U64 winWork = 0, winPeriod = 0;
inline U hist[9];
inline U64 frameIm = 0, frameDm = 0;
inline U works[512];
#ifdef CLD_TRACK
inline const char* trkPtr[6];
inline void trackAdd(const char* n, U dt) {
  if (winN >= 160) return;
  for (int i = 0; i < 6; ++i) {
    if (trkPtr[i] == n) { trk[i][winN] += dt; return; }
    if (trkPtr[i] == nullptr) {
      const char* a = trkNames[i]; const char* b = n;
      while (*a && *a == *b) { ++a; ++b; }
      if (*a == 0 && *b == 0) { trkPtr[i] = n; trk[i][winN] += dt; return; }
    }
  }
}
#endif
constexpr U kParkFrames = 600, kDriveFrames = 520, kWarmFrames = 360, kBootFrames = 1500;
inline void resetWin() {
#ifdef CLD_TRACK
  for (int i = 0; i < 6; ++i) for (U k = 0; k < 160; ++k) trk[i][k] = 0;
#endif
  winN = winLate = workMax = 0; winWork = winPeriod = 0;
  for (U& h : hist) h = 0;
  for (int i = 0; i < used; ++i) { acc[i] = 0; cnt[i] = 0; im[i] = 0; dm[i] = 0; bags[i] = 0; slowAcc[i] = 0; fastAcc[i] = 0; slowBags[i] = 0; fastBags[i] = 0; for (int r = 0; r < 6; ++r) routeAcc[i][r] = 0; }
  slowN = fastN = 0;
  frameIm = frameDm = 0; copyChain = copyMut = borrowB = snaps = stampHits = stampCaps = 0; mutPoolB = mutRefs = 0;
}
inline U us(U64 t) { return U(t * 1000ULL / 294912ULL); }
// GPU-side sampler (Claude rig): while the EE waits for the GPU, classify what
// the hardware is doing. VPU_STAT VBS1 = VU1 running; GIF_STAT APATH = which
// path owns the GIF (1 = VU1 XGKICK), OPH = data going out to the GS.
inline bool gpuSampling = false;
inline U64 gs_n = 0, gs_vu = 0, gs_kick = 0, gs_gif = 0, gs_vif = 0, gs_idle = 0;
inline void gsample() {
  U vpu; asm volatile("cfc2 %0, $vi29" : "=r"(vpu));
  const U gif = *reinterpret_cast<volatile U*>(0x10003020);
  const U vif = *reinterpret_cast<volatile U*>(0x10003c00);
  const U d1 = *reinterpret_cast<volatile U*>(0x10009000);
  const U apath = (gif >> 10) & 3;
  ++gs_n;
  if ((vpu & 0x100) && apath == 1) ++gs_kick;
  else if (vpu & 0x100) ++gs_vu;
  else if (apath != 0 || ((gif >> 24) & 31)) ++gs_gif;
  else if ((vif & 3) || (d1 & 0x100)) ++gs_vif;
  else ++gs_idle;
}
inline void flush(const char* why) {
  if (!winN) return;
  // p95 by partial selection over the stored window
  U n = winN < 512 ? winN : 512, p95 = 0, p50 = 0;
#ifdef CLD_SERIES
  if (state == 2) {  // per-frame work, 0.1 ms units, in frame order
    printf("CLDW");
    for (U i = 0; i < n; ++i) printf(" %u", us(works[i]) / 100);
    printf("\n");
  }
#endif
#ifdef CLD_TRACK
  if (state == 2)
    for (int t = 0; t < 6; ++t) {
      printf("CLDT %s", trkNames[t]);
      for (U i = 0; i < n && i < 160; ++i) printf(" %u", us(trk[t][i]) / 100);
      printf("\n");
    }
#endif
  {
    U tmp[512];
    for (U i = 0; i < n; ++i) tmp[i] = works[i];
    for (U i = 1; i < n; ++i) { U v = tmp[i]; int j = int(i) - 1; while (j >= 0 && tmp[j] > v) { tmp[j + 1] = tmp[j]; --j; } tmp[j + 1] = v; }
    p95 = tmp[(n * 95) / 100 < n ? (n * 95) / 100 : n - 1];
    p50 = tmp[n / 2];
  }
  printf("CLD %s arm=%d cyc=%d st=%d f=%u n=%u work=%u p50=%u p95=%u max=%u late=%u period=%u z=%d h=%u,%u,%u,%u,%u,%u,%u,%u,%u\n",
         why, arm, cycle, state, frame, winN, us(winWork / winN), us(p50), us(p95), us(workMax), winLate,
         us(winPeriod / winN), int(carZ), hist[0], hist[1], hist[2], hist[3], hist[4], hist[5], hist[6], hist[7], hist[8]);
  for (int i = 0; i < used; ++i)
    if (cnt[i]) printf("CLDS %d %s %u %u im=%u dm=%u bags10=%u\n", state, names[i], us(acc[i] / winN), (cnt[i] * 10) / winN, U(im[i] / winN), U(dm[i] / winN), U(bags[i] * 10 / winN));
  printf("CLDM mutPoolB=%u mutRefs10=%u\n", U(mutPoolB / winN), U(mutRefs * 10 / winN));
  printf("CLDP frame im=%u dm=%u snaps10=%u chainB=%u mutB=%u borrowB=%u stamp10=%u cap10=%u\n", U(frameIm / winN), U(frameDm / winN),
         U(snaps * 10 / winN), U(copyChain / winN), U(copyMut / winN), U(borrowB / winN),
         U(stampHits * 10 / winN), U(stampCaps * 10 / winN));
  for (int i = 0; i < used; ++i)
    if (bags[i])
      printf("CLDR %s stamp10=%u replay10=%u direct10=%u partial10=%u strip10=%u culled10=%u\n", names[i],
             U(routeAcc[i][5] * 10 / winN), U(routeAcc[i][0] * 10 / winN), U(routeAcc[i][1] * 10 / winN),
             U(routeAcc[i][2] * 10 / winN), U(routeAcc[i][3] * 10 / winN), U(routeAcc[i][4] * 10 / winN));
  if (gs_n) printf("CLDG n=%u vu=%u kick=%u gif=%u vif=%u idle=%u (per mille)\n", U(gs_n / winN), U(gs_vu * 1000 / gs_n), U(gs_kick * 1000 / gs_n), U(gs_gif * 1000 / gs_n), U(gs_vif * 1000 / gs_n), U(gs_idle * 1000 / gs_n));
  gs_n = gs_vu = gs_kick = gs_gif = gs_vif = gs_idle = 0;
  printf("CLDK slowN=%u fastN=%u\n", slowN, fastN);
  for (int i = 0; i < used; ++i)
    if (cnt[i])
      printf("CLDK %s slow=%u fast=%u bags10 slow=%u fast=%u\n", names[i], slowN ? us(slowAcc[i] / slowN) : 0,
             fastN ? us(fastAcc[i] / fastN) : 0, slowN ? U(slowBags[i] * 10 / slowN) : 0,
             fastN ? U(fastBags[i] * 10 / fastN) : 0);
  fflush(stdout);
  resetWin();
}
inline void setState(int s) {
  if (state == 1 || state == 2) flush("end");
  state = s; stateFrame = 0; resetWin();
}
// Counter calibration: known-footprint loops, printed once.
alignas(64) inline unsigned char calBuf[128 * 1024];
__attribute__((noinline)) inline U calTouch(U bytes, U stride, U reps) {
  volatile U sink = 0; U s = 0;
  for (U r = 0; r < reps; ++r)
    for (U i = 0; i < bytes; i += stride) s += calBuf[i];
  sink = s; return sink;
}
inline void calOne(const char* what, U bytes, U stride, U reps) {
  calTouch(bytes, stride, 1);
  pmuArm();
  const U t0 = ticks(), i0 = pc0(), d0 = pc1();
  calTouch(bytes, stride, reps);
  const U t1 = ticks(), i1 = pc0(), d1 = pc1();
  printf("CLDCAL %s bytes=%u stride=%u reps=%u cyc=%u im=%u dm=%u\n", what, bytes, stride, reps,
         (t1 - t0) * 2, i1 - i0, d1 - d0);
}
__attribute__((noinline)) inline void calNops32k() {
  asm volatile(".rept 8192\n\tnop\n\t.endr" ::: "memory");
}
__attribute__((noinline)) inline void calNops4k() {
  asm volatile(".rept 1024\n\tnop\n\t.endr" ::: "memory");
}
__attribute__((noinline)) inline void calStore(U bytes) {
  volatile U* p = reinterpret_cast<volatile U*>(calBuf);
  for (U i = 0; i < bytes / 4; i += 16) p[i] = i;
}
inline void calibrate() {
  {
    calNops4k(); pmuArm();
    U t0 = ticks(), i0 = pc0();
    for (int r = 0; r < 8; ++r) calNops4k();
    U t1 = ticks(), i1 = pc0();
    printf("CLDCAL nops4k x8 cyc=%u im=%u\n", (t1 - t0) * 2, i1 - i0);
    pmuArm(); t0 = ticks(); i0 = pc0();
    for (int r = 0; r < 4; ++r) calNops32k();
    t1 = ticks(); i1 = pc0();
    printf("CLDCAL nops32k x4 cyc=%u im=%u\n", (t1 - t0) * 2, i1 - i0);
    calStore(131072); pmuArm(); const U d0 = pc1(); t0 = ticks();
    calStore(131072);
    t1 = ticks();
    printf("CLDCAL store128k_s64 cyc=%u dm=%u\n", (t1 - t0) * 2, pc1() - d0);
  }
  calOne("fit4k", 4096, 64, 64);        // fits D$: expect ~0 misses
  calOne("big128k", 131072, 64, 4);     // 2048 lines x4: expect ~8192 misses
  calOne("big128k_s16", 131072, 16, 4); // same lines, 4 loads each
  {
    pmuArm();
    volatile U* vif1Stat = reinterpret_cast<volatile U*>(0x10003C00);
    const U t0 = ticks(), d0 = pc1();
    U s = 0;
    for (int i = 0; i < 1000; ++i) s += *vif1Stat;
    const U t1 = ticks(), d1 = pc1();
    printf("CLDCAL hwreg1000 cyc=%u dm=%u s=%u\n", (t1 - t0) * 2, d1 - d0, s & 1);
  }
  fflush(stdout);
}
// Called at the top of Engine::realLoop with the renderer's stall total.
inline void begin(U stall) {
  const U now = ticks();
  const U fi = pc0(), fd = pc1();
  static bool armed = false;
  const U di = armed ? fi : 0, dd = armed ? fd : 0;
  pmuArm(); armed = true;  // zero both counters every frame: no 31-bit overflow
  if (prevStart && (state == 1 || state == 2)) { frameIm += di; frameDm += dd; }
  if (prevStart) {
    const U period = now - prevStart, st = stall - prevStall;
    const U work = period > st ? period - st : 0;
    if (state == 1 || state == 2) {
      if (winN < 512) works[winN] = work;
      ++winN; winWork += work; winPeriod += period;
      if (work > workMax) workMax = work;
      if (period > U(18.0 * 294912)) ++winLate;
      const U wus = us(work);
      const int b = wus < 10000 ? 0 : wus < 12000 ? 1 : wus < 14000 ? 2 : wus < 15000 ? 3 : wus < 16000 ? 4 : wus < 16700 ? 5 : wus < 18000 ? 6 : wus < 25000 ? 7 : 8;
      ++hist[b];
      const bool slowFrame = wus > 16700;
      for (int i = 0; i < used; ++i) { (slowFrame ? slowAcc : fastAcc)[i] += frameAcc[i]; (slowFrame ? slowBags : fastBags)[i] += frameBags[i]; }
      if (slowFrame) ++slowN; else ++fastN;
      if (winN >= (state == 2 ? 150U : 300U)) flush("win");
    }
  }
  prevStart = now; prevStall = stall;
  for (int i = 0; i < used; ++i) { frameAcc[i] = 0; frameBags[i] = 0; }
  ++frame; ++stateFrame; bagTop = 0;
  if (frame == 600 || frame == 1400) calibrate();
#if CLD_PGO_GEN
  if (frame == 3600) {
    printf("CLDPGO dumping\n"); fflush(stdout);
    extern void cldGcovDump();
    cldGcovDump();
    printf("CLDPGO dumped\n"); fflush(stdout);
  }
#endif
  if (frame == kBootFrames) { arm = cycle % CLD_ARMS; setState(1); }
  if (state == 1 && stateFrame >= kParkFrames) setState(2);
  else if (state == 2 && stateFrame >= kDriveFrames) { setState(0); resetRequested = true; ++cycle; }
  else if (state == 0 && frame > kBootFrames && stateFrame >= kWarmFrames) { arm = cycle % CLD_ARMS; setState(1); }
  gas = state == 2;
}
}  // namespace Cld
