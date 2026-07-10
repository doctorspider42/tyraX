/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Added by tyra-editor: per-object skeletal playback + EE skinning.
# The sampling/pose math mirrors the editor's src/glbparser.cpp (the
# viewport preview and the stage-1 baker) - keep the formulas in sync so
# what the editor shows is what the console computes.
*/

#include "renderer/3d/mesh/dynamic/skel_instance.hpp"

#include <math.h>
#include <string.h>

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

SkelInstance::SkelInstance(const SkelModel* t_model) : model(t_model) {
  // Single-frame builder data in bind pose; the DynamicMesh takes ownership
  // of the arrays, we keep the pointers and overwrite them every update.
  MeshBuilderData data;
  data.loadNormals = true;
  data.loadLightmap = false;
  for (const SkelPart& part : model->parts) {
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
    outVertices.push_back(frame->vertices);
    outNormals.push_back(frame->normals);
  }
  mesh = std::make_unique<DynamicMesh>(&data);

  localsCur.resize(model->nodes.size() * 10);
  localsPrev.resize(model->nodes.size() * 10);
  animatedCur.resize(model->nodes.size());
  animatedPrev.resize(model->nodes.size());
  globals.resize(model->nodes.size());
  palette.resize(model->palette.size());

  play(0, true, 0.0F);
}

SkelInstance::~SkelInstance() {}

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

bool SkelInstance::update(float dt) {
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
  if (poseDirty) {
    evalPose();
    skinParts();
    poseDirty = false;
  }
  return finished;
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
  for (size_t j = 0; j < model->palette.size(); j++)
    mulM4(palette[j].data, globals[model->palette[j].node].data,
          model->palette[j].ibm.data);
}

void SkelInstance::skinParts() {
  float minX = 0.0F, minY = 0.0F, minZ = 0.0F;
  float maxX = 0.0F, maxY = 0.0F, maxZ = 0.0F;
  bool first = true;

  for (size_t pi = 0; pi < model->parts.size(); pi++) {
    const SkelPart& part = model->parts[pi];
    Vec4* outV = outVertices[pi];
    Vec4* outN = outNormals[pi];
    const float* srcP = part.positions.data();
    const float* srcN = part.normals.data();
    const u8* joints = part.joints.data();
    const u8* weights = part.weights.data();

    for (u32 v = 0; v < part.vertexCount; v++) {
      // blended 3x4 palette matrix (bottom row is constant 0 0 0 1)
      float m[12] = {0.0F};
      const u8* j = &joints[(size_t)v * 4];
      const u8* w = &weights[(size_t)v * 4];
      const u32 wsum = (u32)w[0] + w[1] + w[2] + w[3];
      if (wsum > 0) {
        const float invSum = 1.0F / (float)wsum;
        for (int k = 0; k < 4; k++) {
          if (w[k] == 0) continue;
          const float wk = (float)w[k] * invSum;
          const float* p = palette[j[k]].data;
          m[0] += p[0] * wk;
          m[1] += p[1] * wk;
          m[2] += p[2] * wk;
          m[3] += p[4] * wk;
          m[4] += p[5] * wk;
          m[5] += p[6] * wk;
          m[6] += p[8] * wk;
          m[7] += p[9] * wk;
          m[8] += p[10] * wk;
          m[9] += p[12] * wk;
          m[10] += p[13] * wk;
          m[11] += p[14] * wk;
        }
      }
      // else: all-zero weights collapse the vertex to the origin, the same
      // degenerate result the stage-1 baker produced for such data

      const float px = srcP[(size_t)v * 3];
      const float py = srcP[(size_t)v * 3 + 1];
      const float pz = srcP[(size_t)v * 3 + 2];
      const float ox = m[0] * px + m[3] * py + m[6] * pz + m[9];
      const float oy = m[1] * px + m[4] * py + m[7] * pz + m[10];
      const float oz = m[2] * px + m[5] * py + m[8] * pz + m[11];
      outV[v].set(ox, oy, oz, 1.0F);

      const float nx = srcN[(size_t)v * 3];
      const float ny = srcN[(size_t)v * 3 + 1];
      const float nz = srcN[(size_t)v * 3 + 2];
      float tx = m[0] * nx + m[3] * ny + m[6] * nz;
      float ty = m[1] * nx + m[4] * ny + m[7] * nz;
      float tz = m[2] * nx + m[5] * ny + m[8] * nz;
      const float len = sqrtf(tx * tx + ty * ty + tz * tz);
      if (len > 1e-6F) {
        const float inv = 1.0F / len;
        tx *= inv;
        ty *= inv;
        tz *= inv;
      }
      outN[v].set(tx, ty, tz, 1.0F);

      if (first || ox < minX) minX = ox;
      if (first || oy < minY) minY = oy;
      if (first || oz < minZ) minZ = oz;
      if (first || ox > maxX) maxX = ox;
      if (first || oy > maxY) maxY = oy;
      if (first || oz > maxZ) maxZ = oz;
      first = false;
    }
  }

  // Refresh the frame bboxes from the skinned result - the bbox cache is
  // keyed by frame data and would otherwise stay at the bind pose (matters
  // for anything that culls by mesh bbox).
  Vec4 corners[2];
  corners[0].set(minX, minY, minZ, 1.0F);
  corners[1].set(maxX, maxY, maxZ, 1.0F);
  const BBox box(corners, 2);
  if (!mesh->frames.empty() && mesh->frames[0]->bbox)
    *mesh->frames[0]->bbox = box;
  for (auto* material : mesh->materials)
    if (!material->frames.empty() && material->frames[0]->bbox)
      *material->frames[0]->bbox = box;
}

}  // namespace Tyra
