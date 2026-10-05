/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Added by TyraX: per-object skeletal playback; pose evaluation on
# the EE, vertex skinning on VU0 in macro mode (COP2 inline asm).
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
#include <stdio.h>
#include <unordered_map>
#include <string>
#include "debug/debug.hpp"

#if TYRA_SKEL_PROFILE
static unsigned skelTicks() { unsigned t; asm volatile("mfc0 %0, $9" : "=r"(t)); return t; }
#endif

#include "loaders/3d/builder/mesh_builder_data.hpp"

namespace Tyra {

namespace {

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
                const u8* weights, u32 count, SkelInstance::PartBind& pl) {
  // Modified by TyraX: packed by UNIQUE corner. A triangle list repeats each
  // vertex ~5 times, the skin loop computes it once (skinSource) and copies
  // the rest - so only the first corner's bind data is ever read. Storing it
  // per corner was ~4/5 dead weight in the EE's 32 MB.
  pl.count = count;
  pl.skinSource.resize(count);
  // Exact bind attributes only: UV seams can share skinning, hard normals
  // and different bone weights cannot. This temporary table dies at load.
  std::unordered_map<std::string, u32> firstCorner;
  firstCorner.reserve(count);
  for (u32 v = 0; v < count; v++) {
    const u8* jj = &joints[(size_t)v * 4];
    const u8* ww = &weights[(size_t)v * 4];
    char key[32];
    memcpy(key, positions + (size_t)v * 3, 12);
    memcpy(key + 12, normals + (size_t)v * 3, 12);
    memcpy(key + 24, jj, 4);
    memcpy(key + 28, ww, 4);
    const auto inserted = firstCorner.emplace(std::string(key, sizeof(key)), v);
    pl.skinSource[v] = inserted.first->second;
    if (!inserted.second) continue;  // a repeat: skinned once, copied
    Vec4 bp, bn, bw;
    bp.set(positions[(size_t)v * 3], positions[(size_t)v * 3 + 1],
           positions[(size_t)v * 3 + 2], 1.0F);
    bn.set(normals[(size_t)v * 3], normals[(size_t)v * 3 + 1],
           normals[(size_t)v * 3 + 2], 0.0F);
    pl.bindPositions.push_back(bp);
    pl.bindNormals.push_back(bn);
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
      pl.sortedJoints.push_back(jj[idx[k]]);
      if (ww[idx[k]] > 0) n++;
    }
    bw.set(wf[0], wf[1], wf[2], wf[3]);
    pl.skinWeights.push_back(bw);
    pl.influences.push_back(n);
  }
  pl.bindPositions.shrink_to_fit();
  pl.bindNormals.shrink_to_fit();
  pl.skinWeights.shrink_to_fit();
  pl.sortedJoints.shrink_to_fit();
  pl.influences.shrink_to_fit();
}

// One bind cache per model, built by its first instance and OWNED by the
// model (SkelModel::bindCache), so it lives and dies with it. Building it
// frees the model's raw part arrays: they are read here and nowhere else, and
// keeping both was a second copy of every character in the EE's 32 MB.
std::shared_ptr<const SkelBindCache> bindCacheFor(const SkelModel* model) {
  if (model->bindCache) return model->bindCache;
  auto cache = std::make_shared<SkelBindCache>();
  cache->parts.resize(model->parts.size());
  for (size_t pi = 0; pi < model->parts.size(); pi++) {
    const SkelPart& part = model->parts[pi];
    auto& chain = cache->parts[pi];
    chain.resize(1 + part.lods.size());
    auto uvs = [](const std::vector<float>& src, u32 count,
                  std::vector<Vec4>& out) {
      if (src.empty()) return;
      out.resize(count);
      for (u32 v = 0; v < count; v++)
        out[v].set(src[(size_t)v * 2], src[(size_t)v * 2 + 1], 1.0F, 0.0F);
    };
    repackBind(part.positions.data(), part.normals.data(), part.joints.data(),
               part.weights.data(), part.vertexCount, chain[0]);
    if (!part.texturePath.empty()) uvs(part.uvs, part.vertexCount, chain[0].uvs);
    for (size_t l = 0; l < part.lods.size(); l++) {
      const SkelLod& src = part.lods[l];
      repackBind(src.positions.data(), src.normals.data(), src.joints.data(),
                 src.weights.data(), src.vertexCount, chain[1 + l]);
      if (!part.texturePath.empty())
        uvs(src.uvs, src.vertexCount, chain[1 + l].uvs);
    }
  }
  model->bindCache = cache;
  auto* raw = const_cast<SkelModel*>(model);  // the arrays only, never the shape
  for (SkelPart& part : raw->parts) {
    std::vector<float>().swap(part.positions);
    std::vector<float>().swap(part.normals);
    std::vector<float>().swap(part.uvs);
    std::vector<u8>().swap(part.joints);
    std::vector<u8>().swap(part.weights);
    for (SkelLod& lod : part.lods) {
      std::vector<float>().swap(lod.positions);
      std::vector<float>().swap(lod.normals);
      std::vector<float>().swap(lod.uvs);
      std::vector<u8>().swap(lod.joints);
      std::vector<u8>().swap(lod.weights);
    }
  }
  return cache;
}

}  // namespace

SkelInstance::SkelInstance(const SkelModel* t_model) : model(t_model) {
  binds = bindCacheFor(model);
  // The DynamicMesh exists for its MATERIALS (ids for texture links, the
  // part colour); its frames are one-vertex placeholders. What a renderer
  // draws comes from lodArrays() - the shared uvs and this instance's own
  // skin output, allocated on first use (see PartLod).
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
    frame->count = 1;
    frame->vertices = new Vec4[1];
    frame->normals = new Vec4[1];
    frame->vertices[0].set(0.0F, 0.0F, 0.0F, 1.0F);
    frame->normals[0].set(0.0F, 1.0F, 0.0F, 0.0F);
    if (!part.texturePath.empty()) {
      frame->textureCoords = new Vec4[1];
      frame->textureCoords[0].set(0.0F, 0.0F, 1.0F, 0.0F);
    }

    auto& chain = partLods[pi];
    const auto& bchain = binds->parts[pi];
    chain.resize(bchain.size());
    for (size_t l = 0; l < bchain.size(); l++) {
      chain[l].bind = &bchain[l];
      chain[l].count = bchain[l].count;
      chain[l].uvPtr = bchain[l].uvs.empty() ? nullptr : bchain[l].uvs.data();
    }
    if (chain.size() > maxLodLevels) maxLodLevels = (u8)chain.size();
  }
  mesh = std::make_unique<DynamicMesh>(&data);

  localsCur.resize(model->nodes.size() * 10);
  localsPrev.resize(model->nodes.size() * 10);
  animatedCur.resize(model->nodes.size());
  animatedPrev.resize(model->nodes.size());
  globals.resize(model->nodes.size());
  palette.resize(model->palette.size());
  overrideRot.assign(model->nodes.size() * 4, 0.0F);
  overrideOn.assign(model->nodes.size(), 0);

  play(0, true, 0.0F);
}

SkelInstance::~SkelInstance() {}

void SkelInstance::setRotationOverride(u32 node, const float q[4], bool replace) {
  if (node >= overrideOn.size()) return;
  float* o = &overrideRot[(size_t)node * 4];
  const u8 mode = replace ? 2 : 1;
  if (overrideOn[node] == mode && o[0] == q[0] && o[1] == q[1] &&
      o[2] == q[2] && o[3] == q[3])
    return;  // unchanged: the held pose stays valid
  if (!overrideOn[node]) ++overrideCount;
  overrideOn[node] = mode;
  memcpy(o, q, 4 * sizeof(float));
  poseDirty = true;
}

void SkelInstance::clearRotationOverrides() {
  if (overrideCount == 0) return;
  for (u8& on : overrideOn) on = 0;
  overrideCount = 0;
  poseDirty = true;
}

void SkelInstance::setPartSkipped(u32 part, bool skip) {
  if (part >= partLods.size()) return;
  if (partSkipped.size() != partLods.size()) {
    if (!skip) return;  // nothing skipped yet: nothing to turn back on
    partSkipped.assign(partLods.size(), 0);
  }
  if ((partSkipped[part] != 0) == skip) return;
  partSkipped[part] = skip ? 1 : 0;
  if (!skip) poseDirty = true;  // its arrays hold an old skin
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

void SkelInstance::setTime(float seconds) {
  if (cur.clip < 0) return;
  const SkelClip& clip = model->clips[cur.clip];
  if (clip.duration > 0.0F) {
    seconds = fmodf(seconds, clip.duration);
    if (seconds < 0.0F) seconds += clip.duration;
  }
  if (seconds == cur.time) return;
  if (seconds < cur.time)
    for (u32& c : cur.cursors) c = 0;  // the key search runs forward only
  cur.time = seconds;
  poseDirty = true;
}

void SkelInstance::trimOutputs() {
  for (auto& chain : partLods)
    for (PartLod& pl : chain) {
      std::vector<Vec4>().swap(pl.ownVertices);
      std::vector<Vec4>().swap(pl.ownNormals);
      pl.outV = pl.outN = nullptr;
    }
  poseDirty = true;  // nothing held any more
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
#if TYRA_SKEL_PROFILE
  const u32 t0 = skelTicks();
#endif
  if (poseDirty) evalPose();
#if TYRA_SKEL_PROFILE
  const u32 t1 = skelTicks();
#endif
  skinParts(lod);
#if TYRA_SKEL_PROFILE
  const u32 t2 = skelTicks();
  profilePose += t1 - t0;
  profileSkin += t2 - t1;
  if (++profileCount == 100) {
    char msg[192];
    snprintf(msg, sizeof(msg), "SKELTIME instance=%p nodes=%u lod=%u pose=%.3f skin=%.3f ms",
             this, (unsigned)model->nodes.size(), lod,
             profilePose / 29491200.0, profileSkin / 29491200.0);
    TYRA_LOG(msg);
    profileCount = profilePose = profileSkin = 0;
  }
#endif
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
  auto& chain = partLods[part];
  if (lod >= chain.size()) lod = (u8)(chain.size() - 1);
  PartLod& pl = chain[lod];
  if (pl.outV == nullptr) {
    // asked for a level never skinned: hand out the bind pose rather than
    // nothing (a renderer must still call ensurePose first to see a pose)
    const PartBind& b = *pl.bind;
    pl.ownVertices.resize(pl.count);
    pl.ownNormals.resize(pl.count);
    u32 c = 0;  // unpack the unique-corner bind data (see repackBind)
    for (u32 v = 0; v < pl.count; v++) {
      const u32 s = b.skinSource[v];
      if (s == v) {
        pl.ownVertices[v] = b.bindPositions[c];
        pl.ownNormals[v] = b.bindNormals[c++];
      } else {
        pl.ownVertices[v] = pl.ownVertices[s];
        pl.ownNormals[v] = pl.ownNormals[s];
      }
    }
    pl.outV = pl.ownVertices.data();
    pl.outN = pl.ownNormals.data();
  }
  return {pl.outV, pl.outN, const_cast<Vec4*>(pl.uvPtr), pl.count};
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

  // the procedural layer (setRotationOverride): local = clip * override,
  // or the override alone in replace mode
  if (overrideCount > 0) {
    const size_t nodeCount = model->nodes.size();
    for (size_t i = 0; i < nodeCount; i++) {
      if (!overrideOn[i]) continue;
      float* a = &localsCur[i * 10 + 3];
      const float* b = &overrideRot[i * 4];
      if (overrideOn[i] == 2) {  // replace: the clip's own rotation is dropped
        memcpy(a, b, 4 * sizeof(float));
        animatedCur[i] = 1;
        continue;
      }
      const float x = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
      const float y = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
      const float z = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
      const float w = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
      a[0] = x, a[1] = y, a[2] = z, a[3] = w;
      animatedCur[i] = 1;
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
  for (size_t j = 0; j < model->palette.size(); j++)
    mulM4(palette[j].data, globals[model->palette[j].node].data,
          model->palette[j].ibm.data);
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
  // First skin at this level: its output buffers. HERE, before the AABB
  // lives in $vf20/$vf21 - an allocation inside the loop below is a call
  // the register state cannot survive (see above).
  for (size_t pi = 0; pi < partLods.size(); pi++) {
    // a skipped part (setPartSkipped) is not skinned, so it needs no output
    // either - a creator character carries a dozen hidden options
    if (pi < partSkipped.size() && partSkipped[pi]) continue;
    auto& chain = partLods[pi];
    PartLod& pl = chain[lod < chain.size() ? lod : (u8)(chain.size() - 1)];
    if (pl.outV != nullptr && pl.ownVertices.size() == pl.count) continue;
    pl.ownVertices.resize(pl.count);
    pl.ownNormals.resize(pl.count);
    pl.outV = pl.ownVertices.data();
    pl.outN = pl.ownNormals.data();
  }
  float bmin[4] alignas(16) = {FLT_MAX, FLT_MAX, FLT_MAX, 1e-12F};
  float bmax[4] alignas(16) = {-FLT_MAX, -FLT_MAX, -FLT_MAX, 0.0F};
  asm volatile(
      "lqc2         $vf20, 0x00(%[bmin])     \n\t"
      "lqc2         $vf21, 0x00(%[bmax])     \n\t"
      :
      : [bmin] "r"(bmin), [bmax] "r"(bmax));

  const M4x4* pal = palette.data();
  for (size_t pi = 0; pi < partLods.size(); pi++) {
    if (pi < partSkipped.size() && partSkipped[pi]) continue;  // not drawn
    const auto& chain = partLods[pi];
    const PartLod& plod =
        chain[lod < chain.size() ? lod : (u8)(chain.size() - 1)];
    const PartBind& bind = *plod.bind;
    Vec4* outV = plod.outV;
    Vec4* outN = plod.outN;
    const Vec4* srcP = bind.bindPositions.data();
    const Vec4* srcN = bind.bindNormals.data();
    const Vec4* wq = bind.skinWeights.data();
    const u8* joints = bind.sortedJoints.data();
    const u8* infl = bind.influences.data();

    u32 c = 0;  // the unique-corner index the bind arrays are packed by
    for (u32 v = 0; v < plod.count; v++) {
      const u32 source = bind.skinSource[v];
      if (source != v) {
        // No Vec4 helper here: VU0's running AABB must survive the copy.
        asm volatile(
            "lqc2 $vf1, 0(%[srcv]) \n\t"
            "lqc2 $vf2, 0(%[srcn]) \n\t"
            "sqc2 $vf1, 0(%[dstv]) \n\t"
            "sqc2 $vf2, 0(%[dstn]) \n\t"
            : : [srcv] "r"(outV + source), [srcn] "r"(outN + source),
                [dstv] "r"(outV + v), [dstn] "r"(outN + v) : "memory");
        continue;
      }
      const u32 k = c++;
      const u8* j = &joints[(size_t)k * 4];
      const u8 n = infl[k];

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
            : [wgt] "r"(wq + k), [p0] "r"(p0), [p1] "r"(p1)
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
            : [wgt] "r"(wq + k), [p0] "r"(p0), [p1] "r"(p1), [p2] "r"(p2),
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
          : [outv] "r"(outV + v), [outn] "r"(outN + v), [pos] "r"(srcP + k),
            [nrm] "r"(srcN + k)
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
