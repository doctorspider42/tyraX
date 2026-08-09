/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Sandro Sobczyński <sandro.sobczynski@gmail.com>
# Modified by TyraX: frame extrapolation (docs/frame-extrapolation.md) - the
# whole file is a TyraX addition.
*/

#pragma once

#include <tamtypes.h>
#include <packet2.h>
#include "math/vec4.hpp"
#include "renderer/renderer_settings.hpp"
#include "renderer/core/gs/renderer_core_gs.hpp"
#include "renderer/core/renderer_core_sync.hpp"
#include "renderer/core/paths/path1/path1.hpp"

namespace Tyra {

/**
 * A pinhole camera, as the warp needs to see it: an eye, an orthonormal basis
 * and the two half-angle tangents. Deliberately the same shape as the neural
 * upscaler's reprojection input (docs/blss-reconstruction.md) - it is the same
 * projection, run forwards.
 */
struct WarpCamera {
  Vec4 position = Vec4(0.0F, 0.0F, 0.0F, 1.0F);
  Vec4 forward = Vec4(0.0F, 0.0F, -1.0F, 0.0F);
  Vec4 right = Vec4(1.0F, 0.0F, 0.0F, 0.0F);
  Vec4 up = Vec4(0.0F, 1.0F, 0.0F, 0.0F);
  float tanHalfFovX = 1.0F;
  float tanHalfFovY = 1.0F;
};

/**
 * Frame extrapolation: re-present the last finished frame under a NEWER camera
 * by drawing it as a warped textured grid (docs/frame-extrapolation.md).
 *
 * This is extrapolation, not interpolation, and that is the whole reason it is
 * possible here. Interpolating between two frames the way a desktop frame
 * generator does needs the FUTURE frame, which costs a full field of latency on
 * a machine that has none to spare, and per-pixel optical flow, which the GS
 * has no programmable stage to compute. Extrapolation is causal: it uses the
 * newest input available, so it does not add latency - it removes some, because
 * the camera the player sees is fresher than the one the world was rendered
 * with.
 *
 * What it is honest about:
 * - **Rotation is EXACT.** For a camera that only turned, the mapping is a
 *   homography and `planeDistance` cancels out of the arithmetic entirely - no
 *   depth buffer, no approximation, at any scene depth.
 * - **Translation is approximated** by assuming the world sits on one plane at
 *   `planeDistance`. Objects nearer or further than that shear. Strafing shows
 *   it first; walking forward barely does, because the flow it produces is
 *   radial and small near the centre.
 * - **Dynamic objects freeze** for the warped frame - they are pixels in the
 *   source image and the warp only knows about the camera. A game that cares
 *   redraws them on top; the HUD is the same problem and the same answer.
 * - **Disocclusion at the frame edge stretches.** The source has no pixels for
 *   what just came into view, so the outermost cells clamp, which reads as a
 *   smear a few pixels wide. This is what VR reprojection does too. A guard
 *   band would fix it and costs a wider framebuffer plus a widened frustum -
 *   see the doc.
 */
class RendererCoreWarp {
 public:
  RendererCoreWarp();
  ~RendererCoreWarp();

  void init(RendererSettings* settings, RendererCoreGS* gs,
            RendererCoreSync* sync, Path1* path1);

  /**
   * The depth the warp assumes the world sits at, in world units. Only affects
   * camera TRANSLATION - a pure turn reprojects exactly whatever this is. Set
   * it to something like the distance to what the player is looking at; the
   * default suits a walking third/first person camera.
   */
  void setPlaneDistance(const float& d) { planeDistance = d < 0.01F ? 0.01F : d; }
  const float& getPlaneDistance() const { return planeDistance; }

  /**
   * Draw the last finished frame into the buffer currently being rendered to,
   * reprojected from the camera it was rendered with (`from`) to a newer one
   * (`to`). Covers every pixel, so it needs no clear.
   *
   * Does NOT present - the caller flips (RendererCore::presentWarpFrame).
   */
  void draw(const WarpCamera& from, const WarpCamera& to);

  /** Grid cell size in pixels. Smaller = a better fit to the true warp field
   * and more GS work; 32 is two triangles per 32x32 tile. */
  constexpr static int kTile = 32;

 private:
  // 512x512 (Pal576i) is the largest raster, so 16x16 cells and 17x17 corners.
  constexpr static int kMaxCornerCols = 17;
  constexpr static int kMaxCorners = kMaxCornerCols * kMaxCornerCols;
  // Worst case: every row one unbroken run. Per row 1 giftag + 1 PRIM +
  // 3 registers x 2 x (cols + 1) vertices = 104 qwords at 16 columns; 16 rows,
  // plus the state block and the restores. Sized for the worst case and not
  // the typical one - an undersized packet corrupts the GIF stream.
  constexpr static int kPacketQwords = 2048;

  RendererSettings* settings = nullptr;
  RendererCoreGS* gs = nullptr;
  RendererCoreSync* sync = nullptr;
  Path1* path1 = nullptr;
  packet2_t* packet = nullptr;

  float planeDistance = 12.0F;

  int outW = 0, outH = 0;
  int cols = 0, rows = 0, cornerCols = 0, cornerRows = 0;

  // Per-corner source UVs in 12.4 fixed point, rebuilt every draw.
  s16 uv[kMaxCorners][2];

  void updateGeometry();
  void buildUVs(const WarpCamera& from, const WarpCamera& to);
  qword_t* emitState(qword_t* q, int srcVram, int srcBufW);
  qword_t* emitGrid(qword_t* q);
};

}  // namespace Tyra
