/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Added by TyraX: .tnet loader + MLP inference on VU0 (macro mode).
# The feature vector and the output decoding are mirrored by the editor's
# src/motionnet.cpp, which is also what WRITES these files and what the
# Python trainer is fed - all three must agree or the net reads garbage.
*/

#include "renderer/3d/mesh/dynamic/motion_net.hpp"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "debug/debug.hpp"
#include "file/file_utils.hpp"

namespace Tyra {

namespace {

bool readWholeFile(const std::string& fullPath, std::vector<u8>& out) {
  FILE* f = fopen(fullPath.c_str(), "rb");
  if (!f) return false;
  out.clear();
  u8 chunk[8192];
  size_t got;
  while ((got = fread(chunk, 1, sizeof(chunk), f)) > 0)
    out.insert(out.end(), chunk, chunk + got);
  fclose(f);
  return !out.empty();
}

struct Reader {
  const u8* p;
  size_t left;
  bool bytes(void* dst, size_t n) {
    if (n > left) return false;
    memcpy(dst, p, n);
    p += n;
    left -= n;
    return true;
  }
  bool u32le(u32* dst) { return bytes(dst, 4); }
  bool u16le(u16* dst) { return bytes(dst, 2); }
  bool u8v(u8* dst) { return bytes(dst, 1); }
  bool f32le(float* dst) { return bytes(dst, 4); }
};

float clampf(float v, float lo, float hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

/**
 * y = W x + b for one layer, on VU0 in macro mode.
 *
 * Four multiply-accumulates per instruction pair, with the row and the input
 * streamed straight out of main memory - the same arrangement the skinning
 * loop uses, and the reason a net is affordable at all here. `inPadded` is a
 * multiple of four by construction (the loader pads with zero columns), so
 * there is no tail to special-case.
 *
 * $vf16 carries the running dot product across the inner loop; nothing else
 * in the engine touches VU0 concurrently (see the note in skel_instance.cpp),
 * so no state has to be saved around it.
 */
void layerForward(const MotionLayer& L, const float* in, float* out) {
  const u32 chunks = L.inPadded >> 2;
  for (u32 o = 0; o < L.outCount; ++o) {
    const float* w = L.weights + (size_t)o * L.inPadded;
    asm volatile("vsub.xyzw $vf16, $vf0, $vf0 \n\t" ::);  // acc = 0
    for (u32 c = 0; c < chunks; ++c) {
      asm volatile(
          "lqc2      $vf1, 0x00(%[w])    \n\t"
          "lqc2      $vf2, 0x00(%[x])    \n\t"
          "vmul.xyzw $vf3, $vf1, $vf2    \n\t"
          "vadd.xyzw $vf16, $vf16, $vf3  \n\t"
          :
          : [w] "r"(w + (c << 2)), [x] "r"(in + (c << 2)));
    }
    float acc[4] alignas(16);
    asm volatile(
        // horizontal fold into .x, then store the quadword and read it back:
        // one qword store beats four mfc2 round trips through the EE
        "vaddy.x  $vf16, $vf16, $vf16  \n\t"
        "vaddz.x  $vf16, $vf16, $vf16  \n\t"
        "vaddw.x  $vf16, $vf16, $vf16  \n\t"
        "sqc2     $vf16, 0x00(%[acc])  \n\t"
        :
        : [acc] "r"(acc)
        : "memory");
    float y = acc[0] + L.biases[o];
    if (L.relu && y < 0.0F) y = 0.0F;
    out[o] = y;
  }
}

}  // namespace

std::unique_ptr<MotionNetModel> MotionNetLoader::load(
    const std::string& relativePath) {
  std::vector<u8> file;
  if (!readWholeFile(FileUtils::fromCwd(relativePath), file)) {
    TYRA_WARN("MotionNetLoader: cannot read ", relativePath.c_str());
    return nullptr;
  }

  Reader in{file.data(), file.size()};
  char magic[4];
  u32 version = 0, layerCount = 0, jointCount = 0;
  auto net = std::make_unique<MotionNetModel>();

  if (!in.bytes(magic, 4) || memcmp(magic, "TXNN", 4) != 0) {
    TYRA_WARN("MotionNetLoader: not a .tnet: ", relativePath.c_str());
    return nullptr;
  }
  if (!in.u32le(&version) || version != 1) {
    TYRA_WARN("MotionNetLoader: unsupported .tnet version");
    return nullptr;
  }
  if (!in.u32le(&net->featureVersion)) return nullptr;
  if (net->featureVersion != (u32)MotionFeatures::kFeatureVersion) {
    // Refused rather than reinterpreted: a net fed shifted columns produces
    // a confident, plausible, wrong pose - the worst possible failure mode.
    TYRA_WARN("MotionNetLoader: feature layout mismatch, retrain the net (",
              relativePath.c_str(), ")");
    return nullptr;
  }
  if (!in.f32le(&net->outScale) || !in.f32le(&net->phaseRateRange) ||
      !in.f32le(&net->probeScale) || !in.f32le(&net->refSpeed))
    return nullptr;
  for (int i = 0; i < MotionFeatures::kProbeForward; ++i)
    if (!in.f32le(&net->probeForward[i])) return nullptr;
  for (int i = 0; i < MotionFeatures::kProbeLateral; ++i)
    if (!in.f32le(&net->probeLateral[i])) return nullptr;

  if (!in.u32le(&jointCount) || jointCount > 64) return nullptr;
  net->joints.resize(jointCount);
  for (u32 i = 0; i < jointCount; ++i)
    if (!in.u16le(&net->joints[i])) return nullptr;

  if (!in.u32le(&layerCount) || layerCount == 0 || layerCount > 8) return nullptr;

  struct Shape {
    u32 inCount, inPadded, outCount;
    u8 relu;
  };
  std::vector<Shape> shapes(layerCount);
  size_t floats = 0;
  for (u32 i = 0; i < layerCount; ++i) {
    Shape& s = shapes[i];
    if (!in.u32le(&s.inCount) || !in.u32le(&s.outCount) || !in.u8v(&s.relu))
      return nullptr;
    if (s.inCount == 0 || s.outCount == 0 || s.inCount > 4096 ||
        s.outCount > 4096)
      return nullptr;
    s.inPadded = (s.inCount + 3u) & ~3u;
    floats += (size_t)s.outCount * s.inPadded;  // weights
    floats += (s.outCount + 3u) & ~3u;          // biases, kept qword-aligned
    if (s.outCount > net->maxUnits) net->maxUnits = s.outCount;
  }
  if (shapes[0].inCount != (u32)MotionFeatures::kCount) {
    TYRA_WARN("MotionNetLoader: input width is not the feature count");
    return nullptr;
  }
  if (shapes[layerCount - 1].outCount != jointCount * 3 + 1) {
    TYRA_WARN("MotionNetLoader: output width does not match the joint list");
    return nullptr;
  }

  // One allocation, +4 floats of slack so the block can be advanced onto a
  // 16-byte boundary: lqc2 requires it and std::vector guarantees only 4.
  net->storage.assign(floats + 4, 0.0F);
  uintptr_t base = reinterpret_cast<uintptr_t>(net->storage.data());
  float* cur = reinterpret_cast<float*>((base + 15u) & ~(uintptr_t)15u);

  net->layers.resize(layerCount);
  for (u32 i = 0; i < layerCount; ++i) {
    const Shape& s = shapes[i];
    MotionLayer& L = net->layers[i];
    L.inCount = s.inCount;
    L.inPadded = s.inPadded;
    L.outCount = s.outCount;
    L.relu = s.relu;
    L.weights = cur;
    // Row by row, because the file stores the real width and the runtime
    // wants the padded one - the pad columns stay zero from the assign().
    for (u32 o = 0; o < s.outCount; ++o) {
      if (!in.bytes(cur + (size_t)o * s.inPadded, s.inCount * sizeof(float)))
        return nullptr;
    }
    cur += (size_t)s.outCount * s.inPadded;
    L.biases = cur;
    if (!in.bytes(cur, s.outCount * sizeof(float))) return nullptr;
    cur += (s.outCount + 3u) & ~3u;
  }

  return net;
}

MotionPoseCorrector::MotionPoseCorrector() {
  memset(feat, 0, sizeof(feat));
  root[0] = root[1] = root[2] = 0.0F;
  world.identity();
}

void MotionPoseCorrector::setModel(const MotionNetModel* m) {
  model = m;
  if (!m) return;
  // Scratch has to be 16-byte aligned for the same lqc2 reason, and both
  // buffers carry the widest layer plus its quadword pad.
  const size_t widest = m->maxUnits > (size_t)MotionFeatures::kCount
                            ? m->maxUnits
                            : (size_t)MotionFeatures::kCount;
  const size_t units = (widest + 3u) & ~(size_t)3u;
  scratchA.assign(units + 4, 0.0F);  // +4 floats to realign onto a quadword
  scratchB.assign(units + 4, 0.0F);
  smoothed.assign(m->layers.back().outCount, 0.0F);
}

void MotionPoseCorrector::beginFrame(float t_dt, float t_phase, float t_speed,
                                     float t_turn, float t_strafe,
                                     float t_yaw, const float rootWorld[3]) {
  dt = t_dt;
  phase = t_phase;
  speed = t_speed;
  turn = t_turn;
  strafe = t_strafe;
  yaw = t_yaw;
  root[0] = rootWorld[0];
  root[1] = rootWorld[1];
  root[2] = rootWorld[2];
}

void MotionPoseCorrector::buildFeatures() {
  const MotionNetModel& m = *model;
  const float invScale = m.probeScale > 1e-4F ? 1.0F / m.probeScale : 1.0F;

  const float tau = 6.28318531F;
  feat[0] = sinf(phase * tau);
  feat[1] = cosf(phase * tau);
  feat[2] = m.refSpeed > 1e-4F ? clampf(speed / m.refSpeed, -2.0F, 2.0F) : 0.0F;
  feat[3] = clampf(turn * 0.3183F, -2.0F, 2.0F);  // rad/s over pi
  feat[4] = clampf(strafe, -1.0F, 1.0F);

  // Probe grid in the character's own frame: rows ahead/behind, columns
  // left/right. Sampling in the character's frame rather than the world's is
  // what makes a learned gait direction-independent - the net sees "a step
  // 0.35 ahead of me", never "a step at world +X".
  const float cy = cosf(yaw), sy = sinf(yaw);
  int f = 5;
  for (int r = 0; r < MotionFeatures::kProbeForward; ++r) {
    for (int c = 0; c < MotionFeatures::kProbeLateral; ++c) {
      const float fwd = m.probeForward[r], lat = m.probeLateral[c];
      float p[3];
      p[0] = root[0] + sy * fwd + cy * lat;
      p[1] = root[1];
      p[2] = root[2] + cy * fwd - sy * lat;
      float gy = root[1];
      float gn[3] = {0.0F, 1.0F, 0.0F};
      // A miss reads as "the floor is where I am" rather than as a hole: an
      // extreme value on a probe the net never saw in training is exactly
      // how a net is made to do something spectacular and wrong.
      if (!groundFn || !groundFn(groundUser, p, 2.0F, 3.0F, &gy, gn))
        gy = root[1];
      feat[f++] = clampf((gy - root[1]) * invScale, -2.0F, 2.0F);
    }
  }

  // What the ground did to the feet last frame: the cheapest memory of the
  // terrain there is, and the input that tells the net whether the previous
  // step actually landed.
  for (int i = 0; i < MotionFeatures::kMaxLegs; ++i) {
    const bool live = footIk != nullptr && i < (int)footIk->rig.legCount;
    feat[f++] = live ? clampf(footIk->legOffset((u8)i) * invScale, -2.0F, 2.0F)
                     : 0.0F;
    feat[f++] = live && footIk->legGrounded((u8)i) ? 1.0F : 0.0F;
  }
}

const float* MotionPoseCorrector::evaluate() {
  // Feed the (aligned, zero-padded) input buffer, then ping-pong the layers.
  float* a = scratchA.data();
  float* b = scratchB.data();
  uintptr_t ba = reinterpret_cast<uintptr_t>(a);
  uintptr_t bb = reinterpret_cast<uintptr_t>(b);
  a = reinterpret_cast<float*>((ba + 15u) & ~(uintptr_t)15u);
  b = reinterpret_cast<float*>((bb + 15u) & ~(uintptr_t)15u);

  memset(a, 0, model->layers[0].inPadded * sizeof(float));
  memcpy(a, feat, MotionFeatures::kCount * sizeof(float));

  for (size_t i = 0; i < model->layers.size(); ++i) {
    const MotionLayer& L = model->layers[i];
    memset(b, 0, ((L.outCount + 3u) & ~3u) * sizeof(float));
    layerForward(L, a, b);
    float* t = a;
    a = b;
    b = t;
  }
  return a;
}

void MotionPoseCorrector::modifyPose(SkelInstance& inst) {
  rate = 1.0F;
  if (!model || weight <= 0.0F || model->joints.empty()) return;

  buildFeatures();
  const float* out = evaluate();

  // Low pass on the OUTPUT, not the pose: a net evaluated per frame on a
  // stepped input (the probes cross a tread edge in one frame) is jittery by
  // construction, and smoothing here costs one multiply-add per output
  // instead of a second pose blend.
  const float k = dt > 0.0F ? clampf(dt * 18.0F, 0.0F, 1.0F) : 1.0F;
  const size_t n = smoothed.size();
  for (size_t i = 0; i < n; ++i) smoothed[i] += (out[i] - smoothed[i]) * k;

  // The last output is the stride's own rate - what makes a character take
  // shorter, quicker steps going up a flight instead of gliding up it.
  rate = 1.0F + clampf(smoothed[n - 1], -1.0F, 1.0F) * model->phaseRateRange *
                    weight;
  if (rate < 0.25F) rate = 0.25F;

  // Every other output is an exponential-map rotation delta applied to that
  // joint IN PLACE. Rotating a global about the joint's own origin is the
  // same edit as a local rotation delta, and it means the corrector needs no
  // access to the local transforms at all.
  const float scale = model->outScale * weight;
  for (size_t j = 0; j < model->joints.size(); ++j) {
    const u16 node = model->joints[j];
    if (node >= inst.nodeCount()) continue;
    float rv[3] = {smoothed[j * 3 + 0] * scale, smoothed[j * 3 + 1] * scale,
                   smoothed[j * 3 + 2] * scale};
    const float ang = sqrtf(rv[0] * rv[0] + rv[1] * rv[1] + rv[2] * rv[2]);
    if (ang < 1e-4F) continue;
    const float ax = rv[0] / ang, ay = rv[1] / ang, az = rv[2] / ang;
    const float c = cosf(ang), s = sinf(ang), t = 1.0F - c;
    const float R[9] = {t * ax * ax + c,      t * ax * ay + s * az,
                        t * ax * az - s * ay, t * ax * ay - s * az,
                        t * ay * ay + c,      t * ay * az + s * ax,
                        t * ax * az + s * ay, t * ay * az - s * ax,
                        t * az * az + c};
    M4x4 m = inst.poseGlobal(node);
    for (int col = 0; col < 3; ++col) {
      const float x = m.data[col * 4], y = m.data[col * 4 + 1],
                  z = m.data[col * 4 + 2];
      m.data[col * 4] = R[0] * x + R[3] * y + R[6] * z;
      m.data[col * 4 + 1] = R[1] * x + R[4] * y + R[7] * z;
      m.data[col * 4 + 2] = R[2] * x + R[5] * y + R[8] * z;
    }
    inst.setPoseGlobal(node, m);
  }
}

}  // namespace Tyra
