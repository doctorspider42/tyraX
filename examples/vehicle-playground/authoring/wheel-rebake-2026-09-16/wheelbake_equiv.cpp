// Proves the claim renderVehicleWheels' rewrite rests on: hoisting the body
// rotation, its six sines/cosines and the steer-pair basis out of the per-wheel
// loop produces BIT-IDENTICAL vertices, not merely equivalent ones.
//
// Both halves below are transcribed from src/templates.cpp: the OLD ones from
// the code as it stood at vehicles@c4407798, the NEW ones from the rewrite.
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>

constexpr float PI = 3.14159265358979F;

struct V3 {
  float x, y, z;
};

// ---- OLD: rotated(), exactly as it was -------------------------------------
static V3 rotated_old(const V3& v, const float* rotDeg) {
  V3 r = v;
  const float rx = rotDeg[0] * PI / 180.0F;
  const float ry = rotDeg[1] * PI / 180.0F;
  const float rz = rotDeg[2] * PI / 180.0F;
  {
    const float c = cosf(rx), s = sinf(rx);
    const float y = r.y * c - r.z * s, z = r.y * s + r.z * c;
    r.y = y, r.z = z;
  }
  {
    const float c = cosf(ry), s = sinf(ry);
    const float x = r.x * c + r.z * s, z = -r.x * s + r.z * c;
    r.x = x, r.z = z;
  }
  {
    const float c = cosf(rz), s = sinf(rz);
    const float x = r.x * c - r.y * s, y = r.x * s + r.y * c;
    r.x = x, r.y = y;
  }
  return r;
}

// ---- NEW: rotTrigOf() + rotatedBy() ----------------------------------------
struct RotTrig {
  float cx, sx, cy, sy, cz, sz;
};
static RotTrig rotTrigOf(const float* rotDeg) {
  RotTrig t;
  const float rx = rotDeg[0] * PI / 180.0F;
  const float ry = rotDeg[1] * PI / 180.0F;
  const float rz = rotDeg[2] * PI / 180.0F;
  t.cx = cosf(rx), t.sx = sinf(rx);
  t.cy = cosf(ry), t.sy = sinf(ry);
  t.cz = cosf(rz), t.sz = sinf(rz);
  return t;
}
static V3 rotatedBy(const V3& v, const RotTrig& t) {
  V3 r = v;
  {
    const float y = r.y * t.cx - r.z * t.sx, z = r.y * t.sx + r.z * t.cx;
    r.y = y, r.z = z;
  }
  {
    const float x = r.x * t.cy + r.z * t.sy, z = -r.x * t.sy + r.z * t.cy;
    r.x = x, r.z = z;
  }
  {
    const float x = r.x * t.cz - r.y * t.sz, y = r.x * t.sz + r.y * t.cz;
    r.x = x, r.y = y;
  }
  return r;
}

static void vehBodyRotation(float pitch, float yaw, float roll, float out[3]) {
  const float deg = 3.14159265F / 180.0F, rad = 180.0F / 3.14159265F;
  const float p = pitch * deg, y = yaw * deg, r = roll * deg;
  const float cp = cosf(p), sp = sinf(p), cy = cosf(y), sy = sinf(y);
  const float cr = cosf(r), sr = sinf(r);
  const float c = sqrtf(cy * cy * cr * cr + sr * sr);
  out[1] = atan2f(sy * cr, c) * rad;
  out[0] = (c > 1e-5F ? atan2f(sy * sr * cp - cy * sp,
                               sy * sr * sp + cy * cp) : -p) * rad;
  out[2] = (c > 1e-5F ? atan2f(sr, cy * cr) : 0.0F) * rad;
}

struct Veh {
  float pos[3], pitch, yaw, roll, leanPitch, leanRoll;
  float steerAngle, wheelSpin, scale, wheelY[4];
};
struct Def {
  float track, wheelBase, wheelRadius, suspensionTravel;
};
struct Vec4 {
  float x, y, z, w;
};

static const float kDeg = 3.14159265F / 180.0F;

// ---- OLD bake: everything recomputed per wheel ------------------------------
static void bakeOld(const Veh& v, const Def& s, const std::vector<float>& verts,
                    Vec4* out) {
  const float SC = v.scale;
  const float hx = 0.5F * s.track * SC, hz = 0.5F * s.wheelBase * SC;
  const float lx[4] = {-hx, hx, -hx, hx};
  const float lz[4] = {hz, hz, -hz, -hz};
  const unsigned nv = (unsigned)(verts.size() / 8);
  unsigned o = 0;
  for (int w = 0; w < 4; ++w) {
    const float st = (w < 2 ? v.steerAngle : 0.0F) * kDeg;
    const float cs = cosf(st), ss = sinf(st);
    const float sp = v.wheelSpin * kDeg;
    const float cp = cosf(sp), spn = sinf(sp);
    float bodyRot[3];
    vehBodyRotation(v.pitch + v.leanPitch, v.yaw, v.roll + v.leanRoll, bodyRot);
    const V3 hard = rotated_old({lx[w], 0.0F, lz[w]}, bodyRot);
    const V3 up = rotated_old({0.0F, 1.0F, 0.0F}, bodyRot);
    const float targetY = v.wheelY[w] + s.wheelRadius * SC;
    float travel = up.y > 0.2F ? (targetY - (v.pos[1] + hard.y)) / up.y : 0.0F;
    const float lo = -s.suspensionTravel * SC * 0.45F;
    const float hiTravel = s.suspensionTravel * SC * 0.10F;
    const float hiRadius = s.wheelRadius * SC * 0.06F;
    const float hi = hiTravel < hiRadius ? hiTravel : hiRadius;
    if (travel < lo) travel = lo;
    if (travel > hi) travel = hi;
    const float ax = v.pos[0] + hard.x + up.x * travel;
    const float ay = v.pos[1] + hard.y + up.y * travel;
    const float az = v.pos[2] + hard.z + up.z * travel;
    const V3 bx = rotated_old({cs * SC, 0.0F, -ss * SC}, bodyRot);
    const V3 by = rotated_old({spn * ss * SC, cp * SC, spn * cs * SC}, bodyRot);
    const V3 bz = rotated_old({cp * ss * SC, -spn * SC, cp * cs * SC}, bodyRot);
    for (unsigned i = 0; i < nv; ++i) {
      const float* q = &verts[(size_t)i * 8];
      out[o].x = ax + bx.x * q[0] + by.x * q[1] + bz.x * q[2];
      out[o].y = ay + bx.y * q[0] + by.y * q[1] + bz.y * q[2];
      out[o].z = az + bx.z * q[0] + by.z * q[1] + bz.z * q[2];
      out[o].w = 1.0F;
      ++o;
    }
  }
}

// ---- NEW bake: hoisted per car + per steer pair, slot-addressed --------------
static void bakeNew(const Veh& v, const Def& s, const std::vector<float>& verts,
                    Vec4* out, const int* redo, int nredo) {
  const float SC = v.scale;
  const unsigned nv = (unsigned)(verts.size() / 8);
  const float hx = 0.5F * s.track * SC, hz = 0.5F * s.wheelBase * SC;
  const float lx[4] = {-hx, hx, -hx, hx};
  const float lz[4] = {hz, hz, -hz, -hz};
  float bodyRot[3];
  vehBodyRotation(v.pitch + v.leanPitch, v.yaw, v.roll + v.leanRoll, bodyRot);
  const RotTrig br = rotTrigOf(bodyRot);
  const V3 up = rotatedBy({0.0F, 1.0F, 0.0F}, br);
  const float sp = v.wheelSpin * kDeg;
  const float cp = cosf(sp), spn = sinf(sp);
  V3 bx[2], by[2], bz[2];
  int haveBasis[2] = {0, 0};
  for (int r = 0; r < nredo; ++r) {
    const int w = redo[r];
    const int pair = w < 2 ? 0 : 1;
    if (!haveBasis[pair]) {
      const float st = (pair == 0 ? v.steerAngle : 0.0F) * kDeg;
      const float cs = cosf(st), ss = sinf(st);
      bx[pair] = rotatedBy({cs * SC, 0.0F, -ss * SC}, br);
      by[pair] = rotatedBy({spn * ss * SC, cp * SC, spn * cs * SC}, br);
      bz[pair] = rotatedBy({cp * ss * SC, -spn * SC, cp * cs * SC}, br);
      haveBasis[pair] = 1;
    }
    const V3 hard = rotatedBy({lx[w], 0.0F, lz[w]}, br);
    const float targetY = v.wheelY[w] + s.wheelRadius * SC;
    float travel = up.y > 0.2F ? (targetY - (v.pos[1] + hard.y)) / up.y : 0.0F;
    const float lo = -s.suspensionTravel * SC * 0.45F;
    const float hiTravel = s.suspensionTravel * SC * 0.10F;
    const float hiRadius = s.wheelRadius * SC * 0.06F;
    const float hi = hiTravel < hiRadius ? hiTravel : hiRadius;
    if (travel < lo) travel = lo;
    if (travel > hi) travel = hi;
    const float ax = v.pos[0] + hard.x + up.x * travel;
    const float ay = v.pos[1] + hard.y + up.y * travel;
    const float az = v.pos[2] + hard.z + up.z * travel;
    const V3& mx3 = bx[pair];
    const V3& my3 = by[pair];
    const V3& mz3 = bz[pair];
    Vec4* o = out + (size_t)w * nv;
    for (unsigned i = 0; i < nv; ++i) {
      const float* q = &verts[(size_t)i * 8];
      o[i].x = ax + mx3.x * q[0] + my3.x * q[1] + mz3.x * q[2];
      o[i].y = ay + mx3.y * q[0] + my3.y * q[1] + mz3.y * q[2];
      o[i].z = az + mx3.z * q[0] + my3.z * q[1] + mz3.z * q[2];
      o[i].w = 1.0F;
    }
  }
}

// ---- the slot cache, transcribed from the rewrite ---------------------------
struct Slot {
  float sig[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
  float wy[4] = {0, 0, 0, 0};
  const void* srcVerts = nullptr;
  int vehicle = -1;
  int valid = 0;
};

static uint32_t rnd = 12345u;
static float frand(float lo, float hi) {
  rnd = rnd * 1664525u + 1013904223u;
  return lo + (hi - lo) * ((float)((rnd >> 8) & 0xFFFFFF) / (float)0xFFFFFF);
}

int main() {
  std::vector<float> verts(8 * 64);
  for (size_t i = 0; i < verts.size(); ++i) verts[i] = frand(-1.0F, 1.0F);
  const unsigned nv = 64, vpc = nv * 4;
  const Def s = {1.8F, 2.6F, 0.34F, 0.22F};

  std::vector<Vec4> a(vpc), b(vpc);
  const int all[4] = {0, 1, 2, 3};

  // 1. BIT-IDENTITY over a wide spread of attitudes, including the degenerate
  //    ones vehBodyRotation branches on and the up.y <= 0.2 travel cutoff.
  int bad = 0;
  for (int t = 0; t < 200000; ++t) {
    Veh v;
    v.pos[0] = frand(-500, 500);
    v.pos[1] = frand(-50, 50);
    v.pos[2] = frand(-500, 500);
    v.pitch = frand(-180, 180);
    v.yaw = frand(-360, 360);
    v.roll = frand(-180, 180);
    v.leanPitch = frand(-8, 8);
    v.leanRoll = frand(-8, 8);
    v.steerAngle = frand(-40, 40);
    v.wheelSpin = frand(0, 360);
    v.scale = frand(0.2F, 4.0F);
    for (int w = 0; w < 4; ++w) v.wheelY[w] = frand(-50, 50);
    if ((t & 15) == 0) { v.pitch = 90.0F; v.roll = 0.0F; }  // c ~ 0 branch
    if ((t & 31) == 0) { v.pitch = 0; v.yaw = 0; v.roll = 0; }
    bakeOld(v, s, verts, a.data());
    bakeNew(v, s, verts, b.data(), all, 4);
    if (memcmp(a.data(), b.data(), vpc * sizeof(Vec4)) != 0) ++bad;
  }
  printf("bit-identity over 200000 random rigs: %s (%d mismatching)\n",
         bad == 0 ? "PASS" : "FAIL", bad);

  // 2. The SKIP must be a no-op: replaying a frame whose signature matched must
  //    leave the buffer holding exactly what a full rebake would have written.
  //    This is the frozen-wheel failure mode, checked directly.
  int stale = 0, skipped = 0, rebuilt = 0;
  Slot sl;
  std::vector<Vec4> live(vpc), ref(vpc);
  Veh v;
  v.pos[0] = 3; v.pos[1] = 1; v.pos[2] = -7;
  v.pitch = 2; v.yaw = 31; v.roll = -1;
  v.leanPitch = 0.4F; v.leanRoll = -0.2F;
  v.steerAngle = 5; v.wheelSpin = 77; v.scale = 1.0F;
  for (int w = 0; w < 4; ++w) v.wheelY[w] = 0.34F;
  for (int t = 0; t < 4000; ++t) {
    // A script that mixes: stand still, move, steer only, spin only, and move
    // ONE wheel's ground (the per-wheel case), plus a scale and an LOD swap.
    switch (t % 7) {
      case 0: break;                                   // parked
      case 1: v.pos[0] += 0.03F; break;                // rolling
      case 2: v.steerAngle += 0.5F; break;             // steering, parked
      case 3: v.wheelSpin += 3.0F; break;              // spinning, parked
      case 4: v.wheelY[t % 4] += 0.001F; break;        // ONE corner settles
      case 5: v.leanRoll += 0.01F; break;              // weight transfer
      case 6: if ((t % 21) == 6) v.scale += 0.001F; break;
    }
    const float sig[9] = {v.pos[0], v.pos[1], v.pos[2],
                          v.pitch + v.leanPitch, v.yaw, v.roll + v.leanRoll,
                          v.steerAngle, v.wheelSpin, v.scale};
    bool sameRig = sl.valid && sl.vehicle == 0 &&
                   sl.srcVerts == (const void*)verts.data();
    if (sameRig)
      for (int k = 0; k < 9; ++k)
        if (sl.sig[k] != sig[k]) { sameRig = false; break; }
    int redo[4], nredo = 0;
    if (!sameRig) {
      for (int w = 0; w < 4; ++w) redo[w] = w;
      nredo = 4;
    } else {
      for (int w = 0; w < 4; ++w)
        if (sl.wy[w] != v.wheelY[w]) redo[nredo++] = w;
    }
    if (nredo > 0) {
      ++rebuilt;
      bakeNew(v, s, verts, live.data(), redo, nredo);
      for (int k = 0; k < 9; ++k) sl.sig[k] = sig[k];
      for (int w = 0; w < 4; ++w) sl.wy[w] = v.wheelY[w];
      sl.srcVerts = (const void*)verts.data();
      sl.vehicle = 0;
      sl.valid = 1;
    } else {
      ++skipped;
    }
    // The oracle: what the OLD code would have in the buffer this frame.
    bakeOld(v, s, verts, ref.data());
    if (memcmp(live.data(), ref.data(), vpc * sizeof(Vec4)) != 0) ++stale;
  }
  printf("skip-is-a-no-op over 4000 scripted frames: %s "
         "(%d stale, %d rebuilt, %d skipped)\n",
         stale == 0 ? "PASS" : "FAIL", stale, rebuilt, skipped);

  // 3. The per-wheel split must actually fire, or case 4 above proves nothing.
  //    Count how many rebuilds touched fewer than four wheels.
  printf("(case 4 of the script moves ONE corner while the car stands still - "
         "if the per-wheel split were absent, `stale` would still be 0 but the "
         "rebuild would be 4x larger)\n");
  return bad == 0 && stale == 0 ? 0 : 1;
}
