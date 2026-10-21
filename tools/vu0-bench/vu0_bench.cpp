// VU0 math A/B bench (dev-vu0-simd). Runs once from TerrainGame::init():
// per operator, a bit-exact FNV hash of the results (xyz and w separately)
// and COP0 cycles per call, min of 5 runs. Same source builds against the old
// and the new engine, so the two bin/log.txt files diff line by line.
#include <cstdio>
#include <cstring>
#include "debug/debug.hpp"
#include "math/m4x4.hpp"
#include "math/vec4.hpp"

using namespace Tyra;

namespace {

constexpr int N = 256;
constexpr int REPS = 16;
constexpr int RUNS = 5;

Vec4 A[N], B[N], OUT[N];
float S[N], F[N];
M4x4 MATS[8];
M4x4 MOUT[8];

unsigned int rng = 0x1234567u;
float frand(float lo, float hi) {
  rng = rng * 1664525u + 1013904223u;
  return lo + (hi - lo) * ((rng >> 8) * (1.0F / 16777216.0F));
}

inline unsigned int ticks() {
  unsigned int t;
  asm volatile("mfc0 %0, $9" : "=r"(t));
  return t;
}

struct Hash {
  unsigned int xyz = 2166136261u, w = 2166136261u;
  void lane(unsigned int& h, float f) {
    unsigned int u;
    memcpy(&u, &f, 4);
    h = (h ^ u) * 16777619u;
  }
  void add(const Vec4& v) {
    lane(xyz, v.x);
    lane(xyz, v.y);
    lane(xyz, v.z);
    lane(w, v.w);
  }
  void add(float f) { lane(xyz, f); }
};

void report(const char* name, const Hash& h, unsigned int best) {
  char line[160];
  snprintf(line, sizeof line, "VU0BENCH %-12s xyz=%08x w=%08x cyc/op=%u.%02u",
           name, h.xyz, h.w, best / (N * REPS),
           (best % (N * REPS)) * 100 / (N * REPS));
  TYRA_LOG(line);
}

void reset() {
  rng = 0x1234567u;
  for (int i = 0; i < N; i++) {
    A[i].set(frand(-100, 100), frand(-100, 100), frand(-100, 100),
             (i & 1) ? 1.0F : 0.0F);
    B[i].set(frand(-100, 100), frand(-100, 100), frand(-100, 100),
             frand(-2, 2));
    S[i] = frand(0.1F, 4.0F);
    OUT[i].set(0, 0, 0, 0);
  }
  for (int m = 0; m < 8; m++)
    for (int k = 0; k < 16; k++) MATS[m].data[k] = frand(-2, 2);
}

// Runs `body(i)` over every element REPS times, RUNS times; hashes OUT after
// the last run (the body writes OUT[i] or accumulates into F[i]).
template <typename Body, typename Collect>
void bench(const char* name, Body body, Collect collect) {
  unsigned int best = 0xffffffffu;
  Hash h;
  for (int run = 0; run < RUNS; run++) {
    reset();
    const unsigned int t0 = ticks();
    for (int r = 0; r < REPS; r++)
      for (int i = 0; i < N; i++) body(i);
    const unsigned int dt = ticks() - t0;
    if (dt < best) best = dt;
    if (run == RUNS - 1) {
      for (int i = 0; i < N; i++) collect(h, i);
    }
  }
  report(name, h, best);
}

}  // namespace

void vu0BenchRun() {
  auto outV = [](Hash& h, int i) { h.add(OUT[i]); };
  auto outF = [](Hash& h, int i) { h.add(F[i]); };
  auto outA = [](Hash& h, int i) { h.add(A[i]); };

  bench("copy", [](int i) { Vec4 c(A[i]); OUT[i] = c; }, outV);
  bench("add", [](int i) { OUT[i] = A[i] + B[i]; }, outV);
  bench("sub", [](int i) { OUT[i] = A[i] - B[i]; }, outV);
  bench("mul", [](int i) { OUT[i] = A[i] * B[i]; }, outV);
  bench("mulf", [](int i) { OUT[i] = A[i] * S[i]; }, outV);
  bench("divf", [](int i) { OUT[i] = A[i] / S[i]; }, outV);
  bench("addeq", [](int i) { A[i] += B[i]; }, outA);
  bench("muleqf", [](int i) { A[i] *= 0.999F; }, outA);
  bench("cross", [](int i) { OUT[i] = A[i].cross(B[i]); }, outV);
  bench("dot", [](int i) { F[i] = A[i].dot3(B[i]); }, outF);
  bench("length", [](int i) { F[i] = A[i].length(); }, outF);
  bench("normalize", [](int i) { OUT[i] = A[i]; OUT[i].normalize(); }, outV);
  bench("distance", [](int i) { F[i] = A[i].distanceTo(B[i]); }, outF);
  bench("lerp", [](int i) { OUT[i] = Vec4::getByLerp(A[i], B[i], 0.25F); },
        outV);
  bench("m4x4*v", [](int i) { OUT[i] = MATS[i & 7] * A[i]; }, outV);
  bench("m4x4copy", [](int i) { MOUT[i & 7] = MATS[(i + 3) & 7]; },
        [](Hash& h, int i) {
          if (i < 8)
            for (int k = 0; k < 16; k++) h.add(MOUT[i].data[k]);
        });
  // a particle-style step: the shape game code actually writes
  bench("particle",
        [](int i) {
          Vec4 v = B[i] * 0.98F;
          v += Vec4(0.0F, -0.2F, 0.0F, 0.0F);
          A[i] += v * 0.02F;
          F[i] = A[i].distanceTo(Vec4::Identity);
        },
        outA);
  TYRA_LOG("VU0BENCH done");
}
