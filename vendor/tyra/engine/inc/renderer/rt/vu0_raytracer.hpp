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

namespace Tyra {

/** One sphere proxy in the reflected scene. Color channels are 0..255. */
struct Vu0RtSphere {
  Vec4 center;
  float radius = 1.0F;
  Color color = Color(200.0F, 200.0F, 200.0F, 128.0F);
};

/**
 * VU0 MICROMODE ray tracer - the "impossible" PoC. Traces a small
 * reflection image (sphere proxies + optional checkerboard ground plane +
 * sky gradient) entirely on VU0's microprogram pipeline; the EE only feeds
 * ray batches through VU0 data memory and packs the returned integer colors
 * into RGBA32 texels. See src/renderer/rt/vu0_rt_kernel.vclpp for the
 * kernel and the data-memory contract.
 *
 * Usage (per frame, synchronous):
 *   rt.init();                       // uploads the microprogram once
 *   rt.setEye(eyeReflectedAcrossMirrorPlane);
 *   rt.setSpheres(spheres, n); rt.setSky(...); rt.setFloor(...);
 *   rt.trace(texel00World, duWorld, dvWorld, pixels, 64);
 *
 * The eye must be the camera position REFLECTED across the mirror plane
 * (Householder) - then every texel's reflected ray is simply
 * normalize(texelWorldPos - eye), which is what the kernel computes.
 *
 * Caveats: trace() blocks the EE while each row's kernel runs (VU0 macro
 * COP2 code - the engine's Vec4/M4x4 math - shares VU0's register file, so
 * the tracer never runs concurrently with it). One trace of 64x64 with a
 * handful of spheres costs a low single-digit ms of VU0 time.
 */
class Vu0Raytracer {
 public:
  static constexpr int MaxSpheres = 8;
  // Per-kick limit: one VU0 data-memory batch holds 64 output qwords.
  static constexpr int MaxRowTexels = 64;
  // Image edge limit: rows wider than a batch trace in 64-texel chunks.
  static constexpr int MaxSize = 128;

  /** Upload the microprogram to VU0 micro memory. Idempotent. */
  void init();

  /** Camera position reflected across the mirror plane, world space. */
  void setEye(const Vec4& eyeMirrored);

  /** Normalized direction TOWARD the light (for sphere lambert). */
  void setLight(const Vec4& dirTowardLight);

  /** Sky gradient: zenith / horizon colors (0..255). */
  void setSky(const Color& top, const Color& bottom);

  /**
   * Optional checkerboard ground plane at world y = planeY. cellSize is the
   * checker cell edge in world units; beyond fadeDistance the checker fades
   * into the horizon color. Colors should be pre-lit (0..255).
   */
  void setFloor(bool enabled, float planeY, float cellSize, float fadeDistance,
                const Color& a, const Color& b);

  /** Sphere proxies (up to MaxSpheres; extra entries are dropped). */
  void setSpheres(const Vu0RtSphere* spheres, int count);

  /**
   * Trace a size x size reflection image. origin = world position of texel
   * (0,0)'s center on the mirror plane, du/dv = world step per texel along
   * the image row/column. out receives size*size RGBA32 pixels (row-major,
   * alpha 0x80). size must be <= MaxSize; rows wider than MaxRowTexels
   * trace in 64-texel chunks (cost scales with size^2 - 128 is a "for
   * screenshots" tier, ~4x the 64 default).
   */
  void trace(const Vec4& origin, const Vec4& du, const Vec4& dv, u32* out,
             int size);

 private:
  void kickRowAndWait();

  bool uploaded = false;
  float eye[4] = {0.0F, 0.0F, 0.0F, 0.0F};
  float light[4] = {0.302F, 0.905F, 0.302F, 0.0F};
  float skyTop[4] = {40.0F, 90.0F, 170.0F, 0.0F};
  float skyBot[4] = {180.0F, 210.0F, 235.0F, 0.0F};
  float floorQ[4] = {0.0F, 0.25F, 0.005F, 0.0F};  // y, 1/cell, 1/fade, on
  float floorA[4] = {150.0F, 150.0F, 150.0F, 0.0F};
  float floorB[4] = {40.0F, 40.0F, 40.0F, 0.0F};
  float sph[MaxSpheres][4] = {};
  float sphCol[MaxSpheres][4] = {};
  int sphereCount = 0;
};

}  // namespace Tyra
