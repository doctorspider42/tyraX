/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Added by TyraX: VU0 micromode ray tracer (raytraced mirror reflections).
*/

#pragma once

#include <tamtypes.h>
#include "math/vec4.hpp"
#include "renderer/models/color.hpp"
#include "renderer/core/texture/models/texture.hpp"

namespace Tyra {

/** One sphere proxy in the reflected scene. Color channels are 0..255. */
struct Vu0RtSphere {
  Vec4 center;
  float radius = 1.0F;
  Color color = Color(200.0F, 200.0F, 200.0F, 128.0F);
};

/**
 * One axis-aligned slab proxy (min/max corners, world space). The flat
 * counterpart of Vu0RtSphere: a floor or wall as a bounding sphere engulfs
 * the mirror plane (ray origins inside -> the entry distance goes negative
 * and the hit dies on the eps mask), so flat objects trace as AABBs
 * instead. Rotation is NOT represented - a PoC trade like the spheres.
 */
struct Vu0RtBox {
  Vec4 min;
  Vec4 max;
  Color color = Color(160.0F, 160.0F, 160.0F, 128.0F);
};

/** One world-space triangle of a mesh proxy group, with per-corner UVs. */
struct Vu0RtTriangle {
  Vec4 a, b, c;
  float ua = 0.0F, va = 0.0F;
  float ub = 0.0F, vb = 0.0F;
  float uc = 0.0F, vc = 0.0F;
};

/**
 * VU0 MICROMODE ray tracer - the "impossible" PoC. Traces a small
 * reflection image (sphere + AABB slab proxies, up to two TRIANGLE MESH
 * groups, sky-gradient misses) entirely on VU0's microprogram pipeline.
 * The EE feeds ray batches through VU0 data memory and packs the returned
 * texels; triangle hits come back as (record, u, v, shade) and the EE
 * samples the group's TEXTURE in RAM while packing - VU0 cannot sample
 * textures, so the kernel traces and the EE shades.
 *
 * Usage (per frame, synchronous):
 *   rt.init();                       // uploads the microprogram once
 *   rt.setEye(eyeReflectedAcrossMirrorPlane);
 *   rt.setSpheres(...); rt.setBoxes(...); rt.setSky(...);
 *   rt.setTriangles(0, tris, n, texture, fallbackKd);
 *   rt.trace(texel00World, duWorld, dvWorld, pixels, size);
 *
 * The eye must be the camera position REFLECTED across the mirror plane
 * (Householder) - then every texel's reflected ray is simply
 * normalize(texelWorldPos - eye), which is what the kernel computes.
 *
 * Triangle budget: MaxTriangles (30) TOTAL across both groups - the whole
 * set must fit VU0's 4KB data memory next to the ray batch. Each group
 * carries a bounding sphere the kernel tests per texel before entering
 * the triangle loop, so rays that miss the model pay ~20 ops, not the
 * full Moller-Trumbore sweep.
 *
 * Caveats: trace() blocks the EE while each row's kernel runs (VU0 macro
 * COP2 code - the engine's Vec4/M4x4 math - shares VU0's register file, so
 * the tracer never runs concurrently with it).
 */
class Vu0Raytracer {
 public:
  static constexpr int MaxSpheres = 8;
  static constexpr int MaxBoxes = 4;
  static constexpr int MaxGroups = 2;
  static constexpr int MaxTriangles = 36;  // total, across both groups
  // Per-kick limit: one VU0 data-memory batch holds 64 output qwords.
  static constexpr int MaxRowTexels = 64;
  // Image edge limit: rows wider than a batch trace in 64-texel chunks.
  // 512 is the GS texture ceiling - and a photo mode, not a frame rate
  // (cost scales with edge^2: 64x the 64x64 default, ~1 MB of GS VRAM).
  static constexpr int MaxSize = 512;

  /** Upload the microprogram to VU0 micro memory. Idempotent. */
  void init();

  /** Camera position reflected across the mirror plane, world space. */
  void setEye(const Vec4& eyeMirrored);

  /** Normalized direction TOWARD the light (for the lambert terms). */
  void setLight(const Vec4& dirTowardLight);

  /** Sky gradient: zenith / horizon colors (0..255). */
  void setSky(const Color& top, const Color& bottom);

  /** Sphere proxies (up to MaxSpheres; extra entries are dropped). */
  void setSpheres(const Vu0RtSphere* spheres, int count);

  /** AABB slab proxies for flat objects (up to MaxBoxes). */
  void setBoxes(const Vu0RtBox* boxes, int count);

  /**
   * One triangle mesh group (0 or 1): world-space triangles with
   * per-corner UVs, the texture the EE samples on hit (nullptr = flat
   * fallback color modulated by the shade instead), and the fallback.
   * Triangles beyond the remaining shared budget (MaxTriangles across
   * both groups) are dropped. count 0 clears the group. The group's
   * bounding sphere is computed here.
   */
  void setTriangles(int group, const Vu0RtTriangle* tris, int count,
                    const Texture* texture, const Color& fallback);

  /**
   * Trace a size x size reflection image. origin = world position of texel
   * (0,0)'s center on the mirror plane, du/dv = world step per texel along
   * the image row/column. out receives size*size RGBA32 pixels (row-major,
   * alpha 0x80). size must be <= MaxSize; rows wider than MaxRowTexels
   * trace in 64-texel chunks. Cost scales with size^2: 128 costs ~4x the
   * 64 default and still holds a frame rate in a light scene; 256/512 are
   * photo modes.
   */
  void trace(const Vec4& origin, const Vec4& du, const Vec4& dv, u32* out,
             int size);

 private:
  void kickRowAndWait();
  u32 resolveTexel(s32 addr, s32 u4096, s32 v4096, s32 shade);

  bool uploaded = false;
  float eye[4] = {0.0F, 0.0F, 0.0F, 0.0F};
  float light[4] = {0.302F, 0.905F, 0.302F, 0.0F};
  float skyTop[4] = {40.0F, 90.0F, 170.0F, 0.0F};
  float skyBot[4] = {180.0F, 210.0F, 235.0F, 0.0F};
  float sph[MaxSpheres][4] = {};
  float sphCol[MaxSpheres][4] = {};
  int sphereCount = 0;
  float boxMin[MaxBoxes][4] = {};
  float boxMax[MaxBoxes][4] = {};
  float boxCol[MaxBoxes][4] = {};
  int boxCount = 0;
  // Triangle records in kernel format (4 qwords: v0, N, A, B - the dual
  // basis for barycentrics), packed group 0 first; UV rows stay EE-side
  // (triUV: u0,d1u,d2u, v0,d1v,d2v - texture coordinates never enter
  // VU0); group bounds + the EE-side texture/fallback per group.
  float tri[MaxTriangles * 4][4] = {};
  float triUV[MaxTriangles][6] = {};
  float grpBound[MaxGroups][4] = {};
  int grpCount[MaxGroups] = {0, 0};
  const Texture* grpTex[MaxGroups] = {nullptr, nullptr};
  u32 grpFallback[MaxGroups] = {0x80A0A0A0, 0x80A0A0A0};
};

}  // namespace Tyra
