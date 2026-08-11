/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Added by TyraX: per-object skeletal playback; pose evaluation on
# the EE, vertex skinning on VU0 in macro mode (COP2 inline asm).
# Modified by TyraX: terrain-aware whole-body pose and knee guidance.
# The sampling/pose math mirrors the editor's src/glbparser.cpp (the
# viewport preview and the stage-1 baker) - keep the formulas in sync so
# what the editor shows is what the console computes. The vertex loop is
# the one part that does NOT keep bit-parity: VU0 rounding differs from
# the EE FPU by ~1 ulp, verified by screenshot parity against the EE loop.
*/

#include "renderer/3d/mesh/dynamic/skel_instance.hpp"

#include <float.h>
#include <math.h>
#include <string.h>

#include "loaders/3d/builder/mesh_builder_data.hpp"

namespace Tyra {

namespace {

struct F3 {
  float x, y, z;
};

F3 add(const F3& a, const F3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
F3 sub(const F3& a, const F3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
F3 mul(const F3& a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float dot(const F3& a, const F3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
F3 cross(const F3& a, const F3& b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
          a.x * b.y - a.y * b.x};
}
float length(const F3& v) { return sqrtf(dot(v, v)); }
F3 normalized(const F3& v) {
  const float l = length(v);
  return l > 1e-7F ? mul(v, 1.0F / l) : F3{0.0F, 0.0F, 0.0F};
}
F3 positionOf(const M4x4& m) { return {m.data[12], m.data[13], m.data[14]}; }

struct R3 {
  float m[9];  // row-major rotation used only by these scalar helpers
};

F3 rotate(const R3& r, const F3& v) {
  return {r.m[0] * v.x + r.m[1] * v.y + r.m[2] * v.z,
          r.m[3] * v.x + r.m[4] * v.y + r.m[5] * v.z,
          r.m[6] * v.x + r.m[7] * v.y + r.m[8] * v.z};
}

R3 axisAngle(const F3& axisIn, float angle) {
  const F3 a = normalized(axisIn);
  const float c = cosf(angle), s = sinf(angle), t = 1.0F - c;
  return {{t * a.x * a.x + c, t * a.x * a.y - s * a.z,
           t * a.x * a.z + s * a.y, t * a.x * a.y + s * a.z,
           t * a.y * a.y + c, t * a.y * a.z - s * a.x,
           t * a.x * a.z - s * a.y, t * a.y * a.z + s * a.x,
           t * a.z * a.z + c}};
}

R3 rotationBetween(const F3& fromIn, const F3& toIn) {
  const F3 from = normalized(fromIn), to = normalized(toIn);
  float d = dot(from, to);
  if (d > 1.0F) d = 1.0F;
  if (d < -1.0F) d = -1.0F;
  F3 axis = cross(from, to);
  if (length(axis) < 1e-6F) {
    if (d > 0.0F) return {{1, 0, 0, 0, 1, 0, 0, 0, 1}};
    axis = cross(from, fabsf(from.y) < 0.9F ? F3{0, 1, 0} : F3{1, 0, 0});
  }
  return axisAngle(axis, acosf(d));
}

R3 basisRotation(const M4x4& from, const M4x4& to) {
  F3 fx = normalized({from.data[0], from.data[1], from.data[2]});
  F3 fy = normalized({from.data[4], from.data[5], from.data[6]});
  F3 fz = normalized({from.data[8], from.data[9], from.data[10]});
  F3 tx = normalized({to.data[0], to.data[1], to.data[2]});
  F3 ty = normalized({to.data[4], to.data[5], to.data[6]});
  F3 tz = normalized({to.data[8], to.data[9], to.data[10]});
  // toBasis * transpose(fromBasis)
  return {{tx.x * fx.x + ty.x * fy.x + tz.x * fz.x,
           tx.x * fx.y + ty.x * fy.y + tz.x * fz.y,
           tx.x * fx.z + ty.x * fy.z + tz.x * fz.z,
           tx.y * fx.x + ty.y * fy.x + tz.y * fz.x,
           tx.y * fx.y + ty.y * fy.y + tz.y * fz.y,
           tx.y * fx.z + ty.y * fy.z + tz.y * fz.z,
           tx.z * fx.x + ty.z * fy.x + tz.z * fz.x,
           tx.z * fx.y + ty.z * fy.y + tz.z * fz.y,
           tx.z * fx.z + ty.z * fy.z + tz.z * fz.z}};
}

bool descendantOf(const std::vector<SkelNode>& nodes, s32 node, s32 root) {
  for (s32 i = node; i >= 0; i = nodes[i].parent)
    if (i == root) return true;
  return false;
}

void rotateSubtree(std::vector<M4x4>& globals,
                   const std::vector<SkelNode>& nodes, s32 root,
                   const F3& pivot, const R3& r) {
  for (size_t i = 0; i < globals.size(); ++i) {
    if (!descendantOf(nodes, (s32)i, root)) continue;
    M4x4& m = globals[i];
    for (int c = 0; c < 3; ++c) {
      const F3 v = {m.data[c * 4], m.data[c * 4 + 1], m.data[c * 4 + 2]};
      const F3 q = rotate(r, v);
      m.data[c * 4] = q.x;
      m.data[c * 4 + 1] = q.y;
      m.data[c * 4 + 2] = q.z;
    }
    const F3 q = add(pivot, rotate(r, sub(positionOf(m), pivot)));
    m.data[12] = q.x;
    m.data[13] = q.y;
    m.data[14] = q.z;
  }
}

/** r = a * b for column-major 4x4 (plain EE floats - the amounts here are
 * tiny next to the vertex loop, and this keeps bit-parity with the editor's
 * math instead of depending on M4x4's VU0 composition order). */
void mulM4(float* r, const float* a, const float* b) {
  for (int c = 0; c < 4; ++c)
    for (int row = 0; row < 4; ++row) {
      float acc = 0.0F;
      for (int k = 0; k < 4; ++k) acc += a[k * 4 + row] * b[c * 4 + k];
      r[c * 4 + row] = acc;
    }
}

/** T * R * S from translation / quaternion / scale (glTF conventions). */
void fromTrs(float* m, const float* t, const float* q, const float* s) {
  const float x = q[0], y = q[1], z = q[2], w = q[3];
  const float x2 = x + x, y2 = y + y, z2 = z + z;
  const float xx = x * x2, xy = x * y2, xz = x * z2;
  const float yy = y * y2, yz = y * z2, zz = z * z2;
  const float wx = w * x2, wy = w * y2, wz = w * z2;
  m[0] = (1.0F - (yy + zz)) * s[0];
  m[1] = (xy + wz) * s[0];
  m[2] = (xz - wy) * s[0];
  m[3] = 0.0F;
  m[4] = (xy - wz) * s[1];
  m[5] = (1.0F - (xx + zz)) * s[1];
  m[6] = (yz + wx) * s[1];
  m[7] = 0.0F;
  m[8] = (xz + wy) * s[2];
  m[9] = (yz - wx) * s[2];
  m[10] = (1.0F - (xx + yy)) * s[2];
  m[11] = 0.0F;
  m[12] = t[0];
  m[13] = t[1];
  m[14] = t[2];
  m[15] = 1.0F;
}

void slerp(const float* a, const float* bIn, float t, float* out) {
  float b[4] = {bIn[0], bIn[1], bIn[2], bIn[3]};
  float dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
  if (dot < 0.0F) {  // take the short arc
    dot = -dot;
    for (int i = 0; i < 4; ++i) b[i] = -b[i];
  }
  float wa, wb;
  if (dot > 0.9995F) {  // nearly parallel - lerp avoids a degenerate sin
    wa = 1.0F - t;
    wb = t;
  } else {
    const float theta = acosf(dot > 1.0F ? 1.0F : dot);
    const float sinTheta = sinf(theta);
    wa = sinf((1.0F - t) * theta) / sinTheta;
    wb = sinf(t * theta) / sinTheta;
  }
  for (int i = 0; i < 4; ++i) out[i] = wa * a[i] + wb * b[i];
  const float len = sqrtf(out[0] * out[0] + out[1] * out[1] +
                          out[2] * out[2] + out[3] * out[3]);
  if (len > 1e-6F)
    for (int i = 0; i < 4; ++i) out[i] /= len;
  else
    out[3] = 1.0F;
}

/** First key index with times[hi] >= t; the cursor amortizes the scan to
 * O(1) since playback time only moves forward between resets. */
u32 findKey(const std::vector<float>& times, float t, u32* cursor) {
  const u32 n = times.size();
  u32 hi = *cursor;
  if (hi > n) hi = n;
  while (hi > 0 && times[hi - 1] >= t) --hi;  // rewind (restart safety)
  while (hi < n && times[hi] < t) ++hi;
  *cursor = hi;
  return hi;
}

/** Samples one channel at time t into out[3|4] - same rules as the editor:
 * clamp outside the key range, STEP holds the left key, rotations slerp. */
void sampleChannel(const SkelChannel& ch, float t, u32* cursor, float* out) {
  const u32 n = ch.times.size();
  const u32 hi = findKey(ch.times, t, cursor);

  if (ch.path == 1) {
    float a[4], b[4];
    const u32 lo = hi == 0 ? 0 : hi - 1;
    const u32 hiC = hi >= n ? n - 1 : hi;
    for (int c = 0; c < 4; ++c) {
      a[c] = ch.quat[(size_t)lo * 4 + c] * (1.0F / 32767.0F);
      b[c] = ch.quat[(size_t)hiC * 4 + c] * (1.0F / 32767.0F);
    }
    if (hi == 0 || hi >= n || lo == hiC) {
      memcpy(out, hi == 0 ? a : b, 4 * sizeof(float));
      // still normalize the dequantized endpoint
      const float len = sqrtf(out[0] * out[0] + out[1] * out[1] +
                              out[2] * out[2] + out[3] * out[3]);
      if (len > 1e-6F)
        for (int i = 0; i < 4; ++i) out[i] /= len;
      return;
    }
    const float t0 = ch.times[lo], t1 = ch.times[hi];
    float f = t1 > t0 ? (t - t0) / (t1 - t0) : 0.0F;
    if (ch.step) f = 0.0F;
    slerp(a, b, f, out);
    return;
  }

  if (hi == 0) {
    memcpy(out, &ch.vec[0], 3 * sizeof(float));
    return;
  }
  if (hi >= n) {
    memcpy(out, &ch.vec[(size_t)(n - 1) * 3], 3 * sizeof(float));
    return;
  }
  const u32 lo = hi - 1;
  const float t0 = ch.times[lo], t1 = ch.times[hi];
  float f = t1 > t0 ? (t - t0) / (t1 - t0) : 0.0F;
  if (ch.step) f = 0.0F;
  const float* a = &ch.vec[(size_t)lo * 3];
  const float* b = &ch.vec[(size_t)hi * 3];
  for (int c = 0; c < 3; ++c) out[c] = a[c] + (b[c] - a[c]) * f;
}

}  // namespace

namespace {

/** Repacks one mesh variant's bind data for the VU0 loop (see the header
 * comment on PartLod): aligned qwords, normalized weights, joints sorted by
 * descending weight, influence counts for the dispatch. */
void repackBind(const float* positions, const float* normals, const u8* joints,
                const u8* weights, u32 count, SkelInstance::PartLod& pl) {
  pl.count = count;
  pl.bindPositions.resize(count);
  pl.bindNormals.resize(count);
  pl.skinWeights.resize(count);
  pl.sortedJoints.resize((size_t)count * 4);
  pl.influences.resize(count);
  for (u32 v = 0; v < count; v++) {
    pl.bindPositions[v].set(positions[(size_t)v * 3],
                            positions[(size_t)v * 3 + 1],
                            positions[(size_t)v * 3 + 2], 1.0F);
    pl.bindNormals[v].set(normals[(size_t)v * 3], normals[(size_t)v * 3 + 1],
                          normals[(size_t)v * 3 + 2], 0.0F);
    const u8* jj = &joints[(size_t)v * 4];
    const u8* ww = &weights[(size_t)v * 4];
    u8 idx[4] = {0, 1, 2, 3};  // slot order by descending weight
    for (int a = 1; a < 4; a++)
      for (int b = a; b > 0 && ww[idx[b]] > ww[idx[b - 1]]; b--) {
        const u8 t = idx[b];
        idx[b] = idx[b - 1];
        idx[b - 1] = t;
      }
    const u32 wsum = (u32)ww[0] + ww[1] + ww[2] + ww[3];
    const float inv = wsum > 0 ? 1.0F / (float)wsum : 0.0F;
    float wf[4];
    u8 n = 0;
    for (int k = 0; k < 4; k++) {
      wf[k] = (float)ww[idx[k]] * inv;
      pl.sortedJoints[(size_t)v * 4 + k] = jj[idx[k]];
      if (ww[idx[k]] > 0) n++;
    }
    pl.skinWeights[v].set(wf[0], wf[1], wf[2], wf[3]);
    pl.influences[v] = n;
  }
}

}  // namespace

SkelInstance::SkelInstance(const SkelModel* t_model) : model(t_model) {
  // Single-frame builder data in bind pose; the DynamicMesh takes ownership
  // of the arrays, we keep the pointers and overwrite them every update.
  MeshBuilderData data;
  data.loadNormals = true;
  data.loadLightmap = false;
  partLods.resize(model->parts.size());
  for (size_t pi = 0; pi < model->parts.size(); pi++) {
    const SkelPart& part = model->parts[pi];
    auto* material = new MeshBuilderMaterialData();
    data.materials.push_back(material);
    material->name = part.name;
    if (!part.texturePath.empty()) material->texturePath = part.texturePath;
    // the single color the VU1 programs modulate with (128 = 1.0)
    material->ambient.set(part.color[0] * 128.0F, part.color[1] * 128.0F,
                          part.color[2] * 128.0F, 128.0F);

    auto* frame = new MeshBuilderMaterialFrameData();
    material->frames.push_back(frame);
    frame->count = part.vertexCount;
    frame->vertices = new Vec4[part.vertexCount];
    frame->normals = new Vec4[part.vertexCount];
    for (u32 v = 0; v < part.vertexCount; v++) {
      frame->vertices[v].set(part.positions[(size_t)v * 3],
                             part.positions[(size_t)v * 3 + 1],
                             part.positions[(size_t)v * 3 + 2], 1.0F);
      frame->normals[v].set(part.normals[(size_t)v * 3],
                            part.normals[(size_t)v * 3 + 1],
                            part.normals[(size_t)v * 3 + 2], 1.0F);
    }
    if (!part.texturePath.empty()) {
      frame->textureCoords = new Vec4[part.vertexCount];
      for (u32 v = 0; v < part.vertexCount; v++)
        frame->textureCoords[v].set(part.uvs[(size_t)v * 2],
                                    part.uvs[(size_t)v * 2 + 1], 1.0F, 0.0F);
    }

    // level 0 = the full mesh, skinning straight into the frame arrays
    auto& chain = partLods[pi];
    chain.resize(1 + part.lods.size());
    repackBind(part.positions.data(), part.normals.data(), part.joints.data(),
               part.weights.data(), part.vertexCount, chain[0]);
    chain[0].outV = frame->vertices;
    chain[0].outN = frame->normals;
    chain[0].uvPtr = frame->textureCoords;  // nullptr when untextured

    // deeper levels: baked decimated variants with their own skin buffers
    for (size_t l = 0; l < part.lods.size(); l++) {
      const SkelLod& src = part.lods[l];
      PartLod& pl = chain[1 + l];
      repackBind(src.positions.data(), src.normals.data(), src.joints.data(),
                 src.weights.data(), src.vertexCount, pl);
      pl.ownVertices.resize(src.vertexCount);
      pl.ownNormals.resize(src.vertexCount);
      pl.outV = pl.ownVertices.data();
      pl.outN = pl.ownNormals.data();
      if (!part.texturePath.empty()) {
        pl.uvs.resize(src.vertexCount);
        for (u32 v = 0; v < src.vertexCount; v++)
          pl.uvs[v].set(src.uvs[(size_t)v * 2], src.uvs[(size_t)v * 2 + 1],
                        1.0F, 0.0F);
        pl.uvPtr = pl.uvs.data();
      }
    }
    if (chain.size() > maxLodLevels) maxLodLevels = (u8)chain.size();
  }
  mesh = std::make_unique<DynamicMesh>(&data);

  localsCur.resize(model->nodes.size() * 10);
  localsPrev.resize(model->nodes.size() * 10);
  animatedCur.resize(model->nodes.size());
  animatedPrev.resize(model->nodes.size());
  globals.resize(model->nodes.size());
  sampledGlobals.resize(model->nodes.size());
  palette.resize(model->palette.size());

  play(0, true, 0.0F);
}

SkelInstance::~SkelInstance() {}

void SkelInstance::setPoseAdjust(const SkelPoseAdjust& adjust) {
  poseAdjust = adjust;
  poseDirty = true;
}

void SkelInstance::clearPoseAdjust() {
  if (poseAdjust.active()) poseDirty = true;
  poseAdjust = SkelPoseAdjust();
}

const M4x4* SkelInstance::nodeGlobal(s32 node) const {
  if (!poseReady || node < 0 || node >= (s32)sampledGlobals.size())
    return nullptr;
  return &sampledGlobals[(size_t)node];
}

void SkelInstance::play(u32 clip, bool loop, float fadeSeconds) {
  if (clip >= model->clips.size()) clip = 0;
  if (model->clips.empty()) return;

  if (fadeSeconds > 0.0F && cur.clip >= 0) {
    prev = cur;  // keeps its own time/loop/cursors alive during the fade
    fadeDuration = fadeSeconds;
    fadeT = 0.0F;
  } else {
    prev.clip = -1;
    fadeT = 1.0F;
  }
  cur.clip = (s32)clip;
  cur.time = 0.0F;
  cur.loop = loop;
  cur.cursors.assign(model->clips[clip].channels.size(), 0);
  oneShotDone = false;
  poseDirty = true;
}

void SkelInstance::advanceLayer(Layer& layer, float dt) {
  if (layer.clip < 0) return;
  const SkelClip& clip = model->clips[layer.clip];
  if (clip.duration <= 0.0F) return;
  layer.time += dt;
  if (layer.time >= clip.duration) {
    if (layer.loop) {
      layer.time = fmodf(layer.time, clip.duration);
      for (u32& c : layer.cursors) c = 0;
      if (&layer == &cur) oneShotDone = false;  // fresh end if loop turns off
    } else {
      layer.time = clip.duration;
    }
  }
}

bool SkelInstance::advance(float dt) {
  bool finished = false;
  if (cur.clip >= 0 && dt > 0.0F) {
    const SkelClip& clip = model->clips[cur.clip];
    if (clip.duration > 0.0F) {
      // the animFinished contract: once for one-shots, every wrap for loops
      const float next = cur.time + dt;
      if (next >= clip.duration)
        finished = cur.loop ? true : !oneShotDone;
      if (!cur.loop && next >= clip.duration) oneShotDone = true;
      advanceLayer(cur, dt);
      poseDirty = true;
    }
    if (fadeT < 1.0F) {
      advanceLayer(prev, dt);  // the fading-out clip keeps moving
      fadeT += fadeDuration > 0.0F ? dt / fadeDuration : 1.0F;
      if (fadeT >= 1.0F) {
        fadeT = 1.0F;
        prev.clip = -1;
      }
      poseDirty = true;
    }
  }
  return finished;
}

bool SkelInstance::ensurePose(u8 lod) {
  if (lod >= maxLodLevels) lod = maxLodLevels - 1;
  if (!poseDirty && lod == lastSkinnedLod) return false;
  // a pure LOD switch reuses the current palette - only the skin reruns
  if (poseDirty) evalPose();
  skinParts(lod);
  poseDirty = false;
  lastSkinnedLod = lod;
  return true;
}

bool SkelInstance::update(float dt) {
  const bool finished = advance(dt);
  ensurePose(lastSkinnedLod);
  return finished;
}

SkelInstance::LodArrays SkelInstance::lodArrays(size_t part, u8 lod) {
  const auto& chain = partLods[part];
  if (lod >= chain.size()) lod = (u8)(chain.size() - 1);
  const PartLod& pl = chain[lod];
  return {pl.outV, pl.outN, pl.uvPtr, pl.count};
}

void SkelInstance::evalLocals(Layer& layer, std::vector<float>& locals,
                              std::vector<u8>& animated) {
  const size_t nodeCount = model->nodes.size();
  for (size_t i = 0; i < nodeCount; i++) {
    const SkelNode& node = model->nodes[i];
    float* l = &locals[i * 10];
    memcpy(l, node.t, 3 * sizeof(float));
    memcpy(l + 3, node.r, 4 * sizeof(float));
    memcpy(l + 7, node.s, 3 * sizeof(float));
    animated[i] = 0;
  }
  if (layer.clip < 0) return;
  const SkelClip& clip = model->clips[layer.clip];
  for (size_t c = 0; c < clip.channels.size(); c++) {
    const SkelChannel& ch = clip.channels[c];
    float* l = &locals[(size_t)ch.node * 10];
    float* dst = ch.path == 0 ? l : (ch.path == 1 ? l + 3 : l + 7);
    sampleChannel(ch, layer.time, &layer.cursors[c], dst);
    animated[ch.node] = 1;
  }
}

void SkelInstance::evalPose() {
  evalLocals(cur, localsCur, animatedCur);

  if (prev.clip >= 0 && fadeT < 1.0F) {
    // Crossfade: blend the two poses' local transforms (nlerp rotations),
    // then walk the hierarchy once. Nodes only one clip animates blend
    // against the other pose's bind values, which is what a full-pose
    // blend means.
    evalLocals(prev, localsPrev, animatedPrev);
    const float w = fadeT;  // 0 = all prev, 1 = all cur
    const size_t nodeCount = model->nodes.size();
    for (size_t i = 0; i < nodeCount; i++) {
      float* a = &localsCur[i * 10];         // blend target (in place)
      const float* b = &localsPrev[i * 10];  // fading out
      for (int c = 0; c < 3; ++c) {
        a[c] = b[c] + (a[c] - b[c]) * w;          // translation
        a[7 + c] = b[7 + c] + (a[7 + c] - b[7 + c]) * w;  // scale
      }
      float dot = 0.0F;
      for (int c = 0; c < 4; ++c) dot += a[3 + c] * b[3 + c];
      const float sign = dot < 0.0F ? -1.0F : 1.0F;  // short arc
      float len = 0.0F;
      for (int c = 0; c < 4; ++c) {
        a[3 + c] = sign * b[3 + c] + (a[3 + c] - sign * b[3 + c]) * w;
        len += a[3 + c] * a[3 + c];
      }
      len = sqrtf(len);
      if (len > 1e-6F)
        for (int c = 0; c < 4; ++c) a[3 + c] /= len;
      else
        a[6] = 1.0F;  // w component
      animatedCur[i] |= animatedPrev[i];
    }
  }

  // globals, parents-first; matrix nodes are never animated (glTF spec),
  // an animated flag overrides just in case a file breaks that rule
  for (u32 i : model->order) {
    const SkelNode& node = model->nodes[i];
    float local[16];
    const float* localPtr = local;
    if (node.hasMatrix && !animatedCur[i]) {
      localPtr = node.matrix.data;
    } else {
      const float* l = &localsCur[(size_t)i * 10];
      fromTrs(local, l, l + 3, l + 7);
    }
    if (node.parent >= 0)
      mulM4(globals[i].data, globals[node.parent].data, localPtr);
    else
      memcpy(globals[i].data, localPtr, 16 * sizeof(float));
  }
  // Contact policy reads the previous unadjusted clip sample. Feeding the
  // already locked pose back here makes a planted ankle report that it never
  // moved, so the lock cannot observe the swing phase and release.
  for (size_t i = 0; i < globals.size(); ++i)
    sampledGlobals[i] = globals[i];
  applyPoseAdjust();
  for (size_t j = 0; j < model->palette.size(); j++)
    mulM4(palette[j].data, globals[model->palette[j].node].data,
          model->palette[j].ibm.data);
  poseReady = true;
}

void SkelInstance::applyPoseAdjust() {
  if (!poseAdjust.active()) return;

  if (poseAdjust.pelvis >= 0 && poseAdjust.pelvis < (s32)globals.size() &&
      (poseAdjust.pelvisY != 0.0F || poseAdjust.pelvisOffset.x != 0.0F ||
       poseAdjust.pelvisOffset.y != 0.0F || poseAdjust.pelvisOffset.z != 0.0F)) {
    for (size_t i = 0; i < globals.size(); ++i)
      if (descendantOf(model->nodes, (s32)i, poseAdjust.pelvis)) {
        globals[i].data[12] += poseAdjust.pelvisOffset.x;
        globals[i].data[13] += poseAdjust.pelvisY + poseAdjust.pelvisOffset.y;
        globals[i].data[14] += poseAdjust.pelvisOffset.z;
      }
  }

  if (poseAdjust.body >= 0 && poseAdjust.body < (s32)globals.size()) {
    float pitch = poseAdjust.bodyPitch;
    float roll = poseAdjust.bodyRoll;
    const float maxTilt = 0.174532925F;  // generic safety cap: 10 degrees
    if (pitch > maxTilt) pitch = maxTilt;
    if (pitch < -maxTilt) pitch = -maxTilt;
    if (roll > maxTilt) roll = maxTilt;
    if (roll < -maxTilt) roll = -maxTilt;
    const F3 pivot = positionOf(globals[poseAdjust.body]);
    if (fabsf(pitch) > 1e-6F)
      rotateSubtree(globals, model->nodes, poseAdjust.body, pivot,
                    axisAngle({1.0F, 0.0F, 0.0F}, pitch));
    if (fabsf(roll) > 1e-6F)
      rotateSubtree(globals, model->nodes, poseAdjust.body, pivot,
                    axisAngle({0.0F, 0.0F, 1.0F}, roll));
  }

  for (u8 li = 0; li < poseAdjust.legCount && li < 2; ++li) {
    const SkelIkLeg& leg = poseAdjust.legs[li];
    if (leg.weight <= 0.0F || leg.hip < 0 || leg.knee < 0 || leg.ankle < 0 ||
        leg.hip >= (s32)globals.size() || leg.knee >= (s32)globals.size() ||
        leg.ankle >= (s32)globals.size())
      continue;
    if (!descendantOf(model->nodes, leg.knee, leg.hip) ||
        !descendantOf(model->nodes, leg.ankle, leg.knee))
      continue;

    const M4x4 ankleOrientation = globals[leg.ankle];
    const F3 hip = positionOf(globals[leg.hip]);
    const F3 knee = positionOf(globals[leg.knee]);
    const F3 ankle = positionOf(globals[leg.ankle]);
    const F3 rawTarget = {leg.target.x, leg.target.y, leg.target.z};
    float w = leg.weight > 1.0F ? 1.0F : leg.weight;
    const F3 target = add(ankle, mul(sub(rawTarget, ankle), w));
    const F3 upper = sub(knee, hip), lower = sub(ankle, knee);
    const float upperLen = length(upper), lowerLen = length(lower);
    if (upperLen < 1e-5F || lowerLen < 1e-5F) continue;

    F3 reach = sub(target, hip);
    float dist = length(reach);
    if (dist < 1e-5F) continue;
    const float minReach = fabsf(upperLen - lowerLen) + 1e-4F;
    const float maxReach = upperLen + lowerLen - 1e-4F;
    if (dist < minReach) dist = minReach;
    if (dist > maxReach) dist = maxReach;
    const F3 dir = normalized(reach);
    F3 bendNormal = cross(upper, lower);
    if (length(bendNormal) < 1e-5F)
      bendNormal = cross(dir, fabsf(dir.y) < 0.9F ? F3{0, 1, 0} : F3{1, 0, 0});
    bendNormal = normalized(bendNormal);
    F3 bend = normalized(cross(bendNormal, dir));
    const float x = (upperLen * upperLen - lowerLen * lowerLen + dist * dist) /
                    (2.0F * dist);
    float y2 = upperLen * upperLen - x * x;
    if (y2 < 0.0F) y2 = 0.0F;
    const float y = sqrtf(y2);
    const F3 oldSide = sub(knee, add(hip, mul(dir, dot(upper, dir))));
    if (dot(oldSide, bend) < 0.0F) bend = mul(bend, -1.0F);
    float hintWeight = leg.bendHintWeight;
    if (hintWeight < 0.0F) hintWeight = 0.0F;
    if (hintWeight > 1.0F) hintWeight = 1.0F;
    if (hintWeight > 0.0F) {
      const F3 hint = {leg.bendHint.x, leg.bendHint.y, leg.bendHint.z};
      const F3 hintDelta = sub(hint, hip);
      const F3 hintSide = sub(hintDelta, mul(dir, dot(hintDelta, dir)));
      if (length(hintSide) > 1e-5F) {
        const F3 wantedBend = normalized(hintSide);
        F3 blended = add(mul(bend, 1.0F - hintWeight),
                         mul(wantedBend, hintWeight));
        if (length(blended) > 1e-5F) bend = normalized(blended);
      }
    }
    const F3 wantedKnee = add(add(hip, mul(dir, x)), mul(bend, y));

    rotateSubtree(globals, model->nodes, leg.hip, hip,
                  rotationBetween(upper, sub(wantedKnee, hip)));
    const F3 knee2 = positionOf(globals[leg.knee]);
    const F3 ankle2 = positionOf(globals[leg.ankle]);
    rotateSubtree(globals, model->nodes, leg.knee, knee2,
                  rotationBetween(sub(ankle2, knee2), sub(target, knee2)));

    // Hip/knee rotations should move the foot, not inherit their twist. Keep
    // the clip's global ankle orientation and rotate its subtree back around
    // the solved contact point.
    const R3 keepFoot = basisRotation(globals[leg.ankle], ankleOrientation);
    rotateSubtree(globals, model->nodes, leg.ankle,
                  positionOf(globals[leg.ankle]), keepFoot);

    // Tilt the restored clip orientation from model-up toward the supporting
    // surface. This preserves the authored yaw and only adds pitch/roll. The
    // generated runtime clamps both the weight and the normal; clamp again at
    // this generic engine boundary because learned controllers can feed it too.
    float align = leg.alignWeight;
    if (align < 0.0F) align = 0.0F;
    if (align > 1.0F) align = 1.0F;
    F3 normal = normalized(
        {leg.surfaceNormal.x, leg.surfaceNormal.y, leg.surfaceNormal.z});
    if (align > 0.0F && length(normal) > 1e-6F) {
      const F3 up = {0.0F, 1.0F, 0.0F};
      float cosine = dot(up, normal);
      if (cosine > 1.0F) cosine = 1.0F;
      if (cosine < -1.0F) cosine = -1.0F;
      float angle = acosf(cosine);
      float maxAngle = leg.maxAlignRadians;
      if (maxAngle < 0.0F) maxAngle = 0.0F;
      if (angle > maxAngle) angle = maxAngle;
      const F3 axis = cross(up, normal);
      if (length(axis) > 1e-6F && angle > 1e-6F)
        rotateSubtree(globals, model->nodes, leg.ankle,
                      positionOf(globals[leg.ankle]),
                      axisAngle(axis, angle * align));
    }
  }
}

void SkelInstance::skinParts(u8 lod) {
  // The whole per-vertex job runs on VU0 in macro mode (the era-correct
  // split: animation on VU0, 3D on VU1, game code on EE): build the blended
  // palette matrix in $vf5-$vf8 - dispatching on the vertex's influence
  // count so 1- and 2-bone vertices (the vast majority) pay for exactly
  // that many matrix loads - then transform position and normal, normalize
  // the normal with vrsqrt, and fold the skinned AABB with vmini/vmax.
  //
  // VU0 register state deliberately spans the asm blocks below: $vf5-$vf8
  // carry the matrix from the blend block into the shared tail, and
  // $vf20/$vf21 hold the running AABB across the whole loop ($vf20.w
  // carries the epsilon added to squared normal lengths, so a degenerate
  // zero normal divides by sqrt(eps) and comes out ~0 instead of blowing
  // up - the EE loop's `len > 1e-6` guard). That is safe for the same
  // reason every M4x4/Vec4 helper is: GCC never emits COP2 code of its
  // own, and nothing else in the engine runs VU0 concurrently. Don't call
  // anything between these blocks.
  float bmin[4] alignas(16) = {FLT_MAX, FLT_MAX, FLT_MAX, 1e-12F};
  float bmax[4] alignas(16) = {-FLT_MAX, -FLT_MAX, -FLT_MAX, 0.0F};
  asm volatile(
      "lqc2         $vf20, 0x00(%[bmin])     \n\t"
      "lqc2         $vf21, 0x00(%[bmax])     \n\t"
      :
      : [bmin] "r"(bmin), [bmax] "r"(bmax));

  const M4x4* pal = palette.data();
  for (size_t pi = 0; pi < partLods.size(); pi++) {
    const auto& chain = partLods[pi];
    const PartLod& plod =
        chain[lod < chain.size() ? lod : (u8)(chain.size() - 1)];
    Vec4* outV = plod.outV;
    Vec4* outN = plod.outN;
    const Vec4* srcP = plod.bindPositions.data();
    const Vec4* srcN = plod.bindNormals.data();
    const Vec4* wq = plod.skinWeights.data();
    const u8* joints = plod.sortedJoints.data();
    const u8* infl = plod.influences.data();

    for (u32 v = 0; v < plod.count; v++) {
      const u8* j = &joints[(size_t)v * 4];
      const u8 n = infl[v];

      if (n == 2) {
        // two influences - the common case for smooth skinning
        const float* p0 = pal[j[0]].data;
        const float* p1 = pal[j[1]].data;
        asm volatile(
            "lqc2         $vf9, 0x00(%[wgt])       \n\t"
            "lqc2         $vf1, 0x00(%[p0])        \n\t"
            "lqc2         $vf2, 0x00(%[p1])        \n\t"
            "vmulax.xyzw  $ACC, $vf1, $vf9         \n\t"
            "vmaddy.xyzw  $vf5, $vf2, $vf9         \n\t"
            "lqc2         $vf1, 0x10(%[p0])        \n\t"
            "lqc2         $vf2, 0x10(%[p1])        \n\t"
            "vmulax.xyzw  $ACC, $vf1, $vf9         \n\t"
            "vmaddy.xyzw  $vf6, $vf2, $vf9         \n\t"
            "lqc2         $vf1, 0x20(%[p0])        \n\t"
            "lqc2         $vf2, 0x20(%[p1])        \n\t"
            "vmulax.xyzw  $ACC, $vf1, $vf9         \n\t"
            "vmaddy.xyzw  $vf7, $vf2, $vf9         \n\t"
            "lqc2         $vf1, 0x30(%[p0])        \n\t"
            "lqc2         $vf2, 0x30(%[p1])        \n\t"
            "vmulax.xyzw  $ACC, $vf1, $vf9         \n\t"
            "vmaddy.xyzw  $vf8, $vf2, $vf9         \n\t"
            :
            : [wgt] "r"(wq + v), [p0] "r"(p0), [p1] "r"(p1)
            : "memory");
      } else if (n == 1) {
        // single influence - weight is exactly 1, use the matrix as-is
        const float* p0 = pal[j[0]].data;
        asm volatile(
            "lqc2         $vf5, 0x00(%[p0])        \n\t"
            "lqc2         $vf6, 0x10(%[p0])        \n\t"
            "lqc2         $vf7, 0x20(%[p0])        \n\t"
            "lqc2         $vf8, 0x30(%[p0])        \n\t"
            :
            : [p0] "r"(p0)
            : "memory");
      } else if (n >= 3) {
        // 3 or 4 influences - blend all four slots (a 0-weight 4th slot
        // contributes exactly 0; the loader validated every index)
        const float* p0 = pal[j[0]].data;
        const float* p1 = pal[j[1]].data;
        const float* p2 = pal[j[2]].data;
        const float* p3 = pal[j[3]].data;
        asm volatile(
            "lqc2         $vf9, 0x00(%[wgt])       \n\t"
            "lqc2         $vf1, 0x00(%[p0])        \n\t"
            "lqc2         $vf2, 0x00(%[p1])        \n\t"
            "lqc2         $vf3, 0x00(%[p2])        \n\t"
            "lqc2         $vf4, 0x00(%[p3])        \n\t"
            "vmulax.xyzw  $ACC, $vf1, $vf9         \n\t"
            "vmadday.xyzw $ACC, $vf2, $vf9         \n\t"
            "vmaddaz.xyzw $ACC, $vf3, $vf9         \n\t"
            "vmaddw.xyzw  $vf5, $vf4, $vf9         \n\t"
            "lqc2         $vf1, 0x10(%[p0])        \n\t"
            "lqc2         $vf2, 0x10(%[p1])        \n\t"
            "lqc2         $vf3, 0x10(%[p2])        \n\t"
            "lqc2         $vf4, 0x10(%[p3])        \n\t"
            "vmulax.xyzw  $ACC, $vf1, $vf9         \n\t"
            "vmadday.xyzw $ACC, $vf2, $vf9         \n\t"
            "vmaddaz.xyzw $ACC, $vf3, $vf9         \n\t"
            "vmaddw.xyzw  $vf6, $vf4, $vf9         \n\t"
            "lqc2         $vf1, 0x20(%[p0])        \n\t"
            "lqc2         $vf2, 0x20(%[p1])        \n\t"
            "lqc2         $vf3, 0x20(%[p2])        \n\t"
            "lqc2         $vf4, 0x20(%[p3])        \n\t"
            "vmulax.xyzw  $ACC, $vf1, $vf9         \n\t"
            "vmadday.xyzw $ACC, $vf2, $vf9         \n\t"
            "vmaddaz.xyzw $ACC, $vf3, $vf9         \n\t"
            "vmaddw.xyzw  $vf7, $vf4, $vf9         \n\t"
            "lqc2         $vf1, 0x30(%[p0])        \n\t"
            "lqc2         $vf2, 0x30(%[p1])        \n\t"
            "lqc2         $vf3, 0x30(%[p2])        \n\t"
            "lqc2         $vf4, 0x30(%[p3])        \n\t"
            "vmulax.xyzw  $ACC, $vf1, $vf9         \n\t"
            "vmadday.xyzw $ACC, $vf2, $vf9         \n\t"
            "vmaddaz.xyzw $ACC, $vf3, $vf9         \n\t"
            "vmaddw.xyzw  $vf8, $vf4, $vf9         \n\t"
            :
            : [wgt] "r"(wq + v), [p0] "r"(p0), [p1] "r"(p1), [p2] "r"(p2),
              [p3] "r"(p3)
            : "memory");
      } else {
        // all-zero weights - zero matrix collapses the vertex to the
        // origin, the same degenerate result the stage-1 baker produced
        asm volatile(
            "vsub.xyzw    $vf5, $vf0, $vf0         \n\t"
            "vsub.xyzw    $vf6, $vf0, $vf0         \n\t"
            "vsub.xyzw    $vf7, $vf0, $vf0         \n\t"
            "vsub.xyzw    $vf8, $vf0, $vf0         \n\t" ::);
      }

      // shared tail: transform by $vf5-$vf8, normalize, fold the AABB
      asm volatile(
          // position (x, y, z, 1) -> $vf11, force w = 1
          "lqc2         $vf10, 0x00(%[pos])      \n\t"
          "vmulax.xyzw  $ACC, $vf5, $vf10        \n\t"
          "vmadday.xyzw $ACC, $vf6, $vf10        \n\t"
          "vmaddaz.xyzw $ACC, $vf7, $vf10        \n\t"
          "vmaddw.xyzw  $vf11, $vf8, $vf10       \n\t"
          "vmulw.w      $vf11, $vf0, $vf0        \n\t"
          // normal (nx, ny, nz, 0) -> $vf12 (w = 0 drops the translation)
          "lqc2         $vf12, 0x00(%[nrm])      \n\t"
          "vmulax.xyzw  $ACC, $vf5, $vf12        \n\t"
          "vmadday.xyzw $ACC, $vf6, $vf12        \n\t"
          "vmaddaz.xyzw $ACC, $vf7, $vf12        \n\t"
          "vmaddw.xyzw  $vf12, $vf8, $vf12       \n\t"
          // 1 / sqrt(len^2 + eps); AABB fold hides the vrsqrt latency
          "vmul.xyz     $vf13, $vf12, $vf12      \n\t"
          "vaddy.x      $vf13, $vf13, $vf13      \n\t"
          "vaddz.x      $vf13, $vf13, $vf13      \n\t"
          "vaddw.x      $vf13, $vf13, $vf20      \n\t"
          "vrsqrt       $Q, $vf0w, $vf13x        \n\t"
          "vmini.xyz    $vf20, $vf20, $vf11      \n\t"
          "vmax.xyz     $vf21, $vf21, $vf11      \n\t"
          "sqc2         $vf11, 0x00(%[outv])     \n\t"
          "vwaitq                                \n\t"
          "vmulq.xyz    $vf12, $vf12, $Q         \n\t"
          "vmulw.w      $vf12, $vf0, $vf0        \n\t"
          "sqc2         $vf12, 0x00(%[outn])     \n\t"
          :
          : [outv] "r"(outV + v), [outn] "r"(outN + v), [pos] "r"(srcP + v),
            [nrm] "r"(srcN + v)
          : "memory");
    }
  }

  asm volatile(
      "sqc2         $vf20, 0x00(%[bmin])     \n\t"
      "sqc2         $vf21, 0x00(%[bmax])     \n\t"
      :
      : [bmin] "r"(bmin), [bmax] "r"(bmax)
      : "memory");

  // Refresh the frame bboxes from the skinned result - the bbox cache is
  // keyed by frame data and would otherwise stay at the bind pose (matters
  // for anything that culls by mesh bbox).
  Vec4 corners[2];
  corners[0].set(bmin[0], bmin[1], bmin[2], 1.0F);
  corners[1].set(bmax[0], bmax[1], bmax[2], 1.0F);
  const BBox box(corners, 2);
  if (!mesh->frames.empty() && mesh->frames[0]->bbox)
    *mesh->frames[0]->bbox = box;
  for (auto* material : mesh->materials)
    if (!material->frames.empty() && material->frames[0]->bbox)
      *material->frames[0]->bbox = box;
}

}  // namespace Tyra
