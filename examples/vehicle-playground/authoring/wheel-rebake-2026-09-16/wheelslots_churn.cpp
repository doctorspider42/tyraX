// The half the first harness does not reach: the PARTICIPANT LIST churning.
// Cars enter and leave the batch (frustum, draw distance, LOD tier, visibility)
// so slots are reassigned, the vertex vector is trimmed, and a stale slot that
// outlived the vertices it described would claim a match and freeze a wheel.
// This models the batch exactly as renderVehicleWheels does - slot assignment,
// the grow/trim rule, the signature, the sticky stamp - against an oracle that
// rebuilds everything from scratch every frame.
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>

// Two switches, so the two results this file is cited for are both one command
// away and neither needs the source edited:
//   --adversarial  replace the random participant list with the schedule that
//                  actually produces the trim hazard (three cars, then one,
//                  alternating, with nothing ever moving)
//   --no-trim      MUTANT: drop "slots.resize(slot)", i.e. let the slot table
//                  outlive the vertices it describes
// Random + shipped -> PASS.  Random + mutant -> PASS TOO, which is why the
// adversarial schedule exists.  Adversarial + shipped -> PASS.
// Adversarial + mutant -> one frozen car on every frame the slots regrow.
static bool g_adversarial = false;
static bool g_noTrim = false;

static uint32_t rnd = 987654321u;
static uint32_t nextRnd() { rnd = rnd * 1664525u + 1013904223u; return rnd; }
static float frand(float lo, float hi) {
  return lo + (hi - lo) * ((float)((nextRnd() >> 8) & 0xFFFFFF) / (float)0xFFFFFF);
}

// One "vertex" stands for a car's whole 4-wheel span; the arithmetic is not
// what is under test here, the bookkeeping is. A car's content is a pure
// function of its signature, so a frozen slot is detectable exactly.
struct Car {
  int id;
  float sig[9];
  float wy[4];
  const void* src;
};

struct Slot {
  float sig[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
  float wy[4] = {0, 0, 0, 0};
  const void* srcVerts = nullptr;
  int vehicle = -1;
  int valid = 0;
};

static const int kPerWheel = 5;          // "vertices" per wheel
static const int kPerCar = kPerWheel * 4;

static float cell(const Car& c, int w, int i) {
  // A value that depends on every input this wheel is a function of.
  float a = 0;
  for (int k = 0; k < 9; ++k) a += c.sig[k] * (float)(k + 1);
  return a + c.wy[w] * 100.0F + (float)w * 7.0F + (float)i * 0.25F +
         (float)(intptr_t)c.src * 1e-9F;
}

int main(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--adversarial") g_adversarial = true;
    else if (a == "--no-trim") g_noTrim = true;
    else { fprintf(stderr, "unknown option %s\n", argv[i]); return 2; }
  }
  std::vector<float> verts;              // the batch, slot-addressed
  std::vector<Slot> slots;
  size_t lastCount = 0;
  const void* lastData = nullptr;
  unsigned stamp = 0, gStamp = 0;

  std::vector<Car> pool(9);
  for (int i = 0; i < 9; ++i) {
    pool[i].id = i;
    for (int k = 0; k < 9; ++k) pool[i].sig[k] = frand(-10, 10);
    for (int w = 0; w < 4; ++w) pool[i].wy[w] = frand(-1, 1);
    pool[i].src = (const void*)(intptr_t)(0x1000 + i);
  }

  int stale = 0, badStamp = 0, frames = 0, skipped = 0, rebuilt = 0;
  unsigned prevStamp = 0;
  std::vector<float> prevBuf;

  const int kFrames = g_adversarial ? 2000 : 60000;
  for (int f = 0; f < kFrames; ++f) {
    // Who is visible this frame - an arbitrary, frequently changing subset in
    // ascending vehicle order, which is the order the real loop produces.
    std::vector<Car*> live;
    if (g_adversarial) {
      // The list oscillates so slots 1 and 2 are trimmed away and regrown,
      // while cars 1 and 2 never change at all - so a stale slot's signature
      // still matches and the skip it produces is silent.
      live.push_back(&pool[0]);
      if (f % 2 == 0) { live.push_back(&pool[1]); live.push_back(&pool[2]); }
    } else {
      for (int i = 0; i < 9; ++i)
        if ((nextRnd() & 3) != 0) live.push_back(&pool[i]);
      // Some of them moved; some swapped their source part (an LOD crossing).
      for (auto* c : live) {
        const uint32_t r = nextRnd();
        if ((r & 7) == 0) c->sig[0] += 0.01F;                 // rolled
        if ((r & 15) == 0) c->wy[(r >> 8) & 3] += 0.001F;     // one corner
        if ((r & 63) == 0)                                     // LOD swap
          c->src = (const void*)((intptr_t)c->src ^ 0x40);
      }
    }

    bool changed = false;
    int slot = 0;
    for (auto* c : live) {
      const size_t base = (size_t)slot * kPerCar;
      if (verts.size() < base + kPerCar) verts.resize(base + kPerCar);
      if ((int)slots.size() <= slot) slots.resize((size_t)slot + 1);
      Slot& sl = slots[(size_t)slot];
      bool sameRig = sl.valid && sl.vehicle == c->id && sl.srcVerts == c->src;
      if (sameRig)
        for (int k = 0; k < 9; ++k)
          if (sl.sig[k] != c->sig[k]) { sameRig = false; break; }
      int redo[4], nredo = 0;
      if (!sameRig) { for (int w = 0; w < 4; ++w) redo[w] = w; nredo = 4; }
      else for (int w = 0; w < 4; ++w)
             if (sl.wy[w] != c->wy[w]) redo[nredo++] = w;
      if (nredo > 0) {
        changed = true; ++rebuilt;
        for (int r = 0; r < nredo; ++r) {
          const int w = redo[r];
          for (int i = 0; i < kPerWheel; ++i)
            verts[base + (size_t)w * kPerWheel + i] = cell(*c, w, i);
        }
        for (int k = 0; k < 9; ++k) sl.sig[k] = c->sig[k];
        for (int w = 0; w < 4; ++w) sl.wy[w] = c->wy[w];
        sl.srcVerts = c->src; sl.vehicle = c->id; sl.valid = 1;
      } else { ++skipped; }
      ++slot;
    }
    if (slot == 0) continue;             // nothing drawn; buffer left alone
    const size_t total = (size_t)slot * kPerCar;
    if (verts.size() != total) { verts.resize(total); changed = true; }
    if (!g_noTrim && (int)slots.size() > slot) slots.resize((size_t)slot);

    if (verts.data() != lastData || verts.size() != lastCount || stamp == 0)
      changed = true;
    lastData = verts.data(); lastCount = verts.size();
    if (changed) stamp = ++gStamp;

    // ORACLE: what a from-scratch rebuild would hold this frame.
    std::vector<float> ref(total);
    for (int k = 0; k < slot; ++k)
      for (int w = 0; w < 4; ++w)
        for (int i = 0; i < kPerWheel; ++i)
          ref[(size_t)k * kPerCar + (size_t)w * kPerWheel + i] =
              cell(*live[(size_t)k], w, i);
    if (memcmp(verts.data(), ref.data(), total * sizeof(float)) != 0) ++stale;

    // THE STAMP CONTRACT: the stamp may only stay the same if the bytes,
    // the length and the address all stayed the same.
    if (stamp == prevStamp && !prevBuf.empty()) {
      if (prevBuf.size() != verts.size() ||
          memcmp(prevBuf.data(), verts.data(), total * sizeof(float)) != 0)
        ++badStamp;
    }
    prevStamp = stamp;
    prevBuf.assign(verts.begin(), verts.end());
    ++frames;
  }
  printf("%s schedule, %s logic, %d frames: %s\n",
         g_adversarial ? "adversarial" : "random",
         g_noTrim ? "MUTANT (no slot trim)" : "shipped",
         frames, (stale == 0 && badStamp == 0) ? "PASS" : "FAIL");
  printf("  stale buffers (a frozen wheel):      %d  (must be 0)\n", stale);
  printf("  stamps reused over changed bytes:    %d  (must be 0)\n", badStamp);
  printf("  car-slots rebuilt %d, skipped %d\n", rebuilt, skipped);
  return (stale == 0 && badStamp == 0) ? 0 : 1;
}
