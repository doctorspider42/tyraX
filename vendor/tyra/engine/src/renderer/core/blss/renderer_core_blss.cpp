/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Added by TyraX: BLSS, the neural upscaler (docs/neural-upscaler.md).
#
# EVERY formula here is the twin of one in docs/blss-reconstruction.md and of
# one in the editor's src/blss.cpp. The network is FITTED against this
# arithmetic - truncating shifts, 8-bit clamps, the two-triangle interpolation
# of the Gouraud grid - so a divergence does not make the net inaccurate, it
# makes it optimise the wrong objective. Section numbers below cite that page.
*/

#include <dma.h>
#include <draw.h>
#include <gif_tags.h>
#include <graph.h>
#include <gs_gp.h>
#include <gs_psm.h>
#include <math.h>
#include <stdio.h>
#include "renderer/core/blss/renderer_core_blss.hpp"
#include "math/math.hpp"
#include "debug/debug.hpp"

namespace Tyra {

// ceil(log2(v)) - the GS TEX0 TW/TH fields are log2 of a power-of-two extent
// that must COVER the real one; the region CLAMP below cuts it back down.
static u8 lg2(int v) {
  u8 r = 0;
  while ((1 << r) < v) r++;
  return r;
}

static inline float clamp01(float v) {
  return v < 0.0F ? 0.0F : (v > 1.0F ? 1.0F : v);
}

// The composite packet, in qwords. Worst case over ALL passes of the FULL
// grid, and it must be the worst case: an undersized packet corrupts the GIF
// stream, which shows up as a hang in draw_wait_finish rather than as a
// picture bug.
//
//   - a pass' A+D state block is 1 tag + 10 registers            = 11 qwords
//   - a run of cells is 1 tag + PRIM + 3 registers per vertex; a row of C
//     cells emitted as one strip is 2 * (C + 1) vertices, so
//     1 + 1 + 3 * 34                                             = 104 qwords
//     BUT sparsity can split a row into up to ceil(C/2) runs of one cell
//     each (draw, skip, draw, skip...), and 8 runs of 4 vertices is
//     8 * (1 + 1 + 12)                                           = 112 qwords
//     which is the real per-row ceiling.
//   - 17 rows (the tallest mode, HiDef1080i's 540 lines) x 112    = 1904
//   - 6 passes (5 reconstruction + the debug tint) x (1904 + 11) = 11490
//   - plus the two XYOFFSET writes, the restore block and draw_finish.
//
// 12288 leaves ~800 qwords of headroom. Grow it whenever a pass is added.
static constexpr int kPacketQwords = 12288;

RendererCoreBlss::RendererCoreBlss() {
  for (int i = 0; i < kMaxTiles; i++) {
    coverAcc[i] = depthAcc[i] = lumaAcc[i] = detAcc[i] = edgeAcc[i] = 0.0F;
    dMin[i] = 1e30F;
    dMax[i] = 0.0F;
    tCover[i] = tDepth[i] = tLuma[i] = tDetail[i] = tEdge[i] = 0.0F;
    tDepthMin[i] = tDepthMax[i] = 0.0F;
    outW_A[i] = outW_C[i] = outW_D[i] = 0.0F;
    for (int f = 0; f < kFeatures; f++) feat[i][f] = 0.0F;
  }
  for (int i = 0; i < kMaxCorners; i++) {
    cornerA[i] = cornerC[i] = cornerD[i] = 0.0F;
    cornerDu[i] = cornerDv[i] = 0.0F;
  }
}

RendererCoreBlss::~RendererCoreBlss() {
  if (packet) packet2_free(packet);
  if (beginPacket) packet2_free(beginPacket);
  if (endPacket) packet2_free(endPacket);
}

void RendererCoreBlss::init(RendererSettings* t_settings, RendererCoreGS* t_gs,
                            RendererCoreSync* t_sync, Path1* t_path1,
                            RendererCore3D* t_core3D) {
  settings = t_settings;
  gs = t_gs;
  sync = t_sync;
  path1 = t_path1;
  core3D = t_core3D;

  // Re-place the low-res target after a display-mode switch: setDisplayOutput
  // resets the whole VRAM map, so an already-configured BLSS has to take its
  // page-aligned slot again, in the same relative order as post fx / env map /
  // camera feed (below every texture, where eviction cannot reach it).
  if (enabled) {
    updateGeometry();
    allocated = false;
    allocate();
    if (core3D != nullptr) core3D->setFov(core3D->getFov());
  }
}

void RendererCoreBlss::updateGeometry() {
  outW = static_cast<int>(settings->getWidth());
  // PHYSICAL buffer height - half the logical one in InterlacedField. The
  // whole of BLSS lives in physical-buffer space; only the game-facing layout
  // uses getHeight().
  outH = static_cast<int>(settings->getRenderHeightF());
  lowW = outW / scaleX;
  lowH = outH / scaleY;
  lowBufW = -64 & (lowW + 63);  // FRAME/TEX buffer widths are 64-aligned

  cols = (outW + kTile - 1) / kTile;
  rows = (outH + kTile - 1) / kTile;
  if (cols > kMaxCols) cols = kMaxCols;
  if (rows > kMaxRows) rows = kMaxRows;
  cornerCols = cols + 1;
  cornerRows = rows + 1;
}

void RendererCoreBlss::allocate() {
  if (!enabled || allocated || gs == nullptr) return;
  // One colour target only. There is NO history buffer and NO low-res z: the
  // history is the other display framebuffer (section 6) and the low-res pass
  // points ZBUF at the main z buffer (section 7) - FRAME and ZBUF bases are
  // independent registers.
  lowVram = gs->vram.allocateBuffer(lowBufW, lowH, GS_PSM_32);
  TYRA_ASSERT(lowVram >= 0, "Out of VRAM for the BLSS low-res target");
  allocated = true;
  TYRA_LOG("BLSS: ", lowW, "x", lowH, " low-res target at VRAM ", lowVram);
}

void RendererCoreBlss::configure(int t_scaleX, int t_scaleY, float t_sharpen,
                                 bool t_temporal, int t_debugView) {
  TYRA_ASSERT(settings != nullptr,
              "BLSS configure() before the renderer was initialized!");
  if (settings == nullptr) return;
  scaleX = t_scaleX < 1 ? 1 : t_scaleX;
  scaleY = t_scaleY < 1 ? 1 : t_scaleY;
  sharpen = clamp01(t_sharpen);
  temporal = t_temporal;
  debugView = t_debugView;
  // 1x1 is "off" - nothing is allocated and the projection keeps its full
  // raster scale, so a project without BLSS costs zero words and zero cycles.
  enabled = (scaleX * scaleY) > 1;

  settings->setRasterScale(enabled ? scaleX : 1, enabled ? scaleY : 1);
  // The projection's raster scale changed; the world-space frustum planes did
  // not (they come from fov + aspectRatio) - see RendererCore3D::setProjection.
  if (core3D != nullptr) core3D->setFov(core3D->getFov());

  // The z buffer is sized for the RASTER, and the raster scale only becomes
  // known here - RendererCoreGS allocated it third, before this object existed
  // in the init order. So the permanent VRAM region is laid out again, exactly
  // the way a display-mode switch does it: textures evicted, frame/z buffers
  // rebuilt (the frame buffers land at the same addresses, z at the smaller
  // one), then every permanent buffer above them re-placed in the same
  // relative order. Only when the size actually changes - a project with BLSS
  // off never reaches this, and calling configure() twice with the same scale
  // is free.
  //
  // Safe HERE and nowhere later: generated games call configure() at the top
  // of init(), before buildScene() loads a single asset, so the eviction the
  // rebuild performs has nothing to evict and docs/gs-vram.md's "permanent
  // buffers before any texture" invariant holds.
  if (gs != nullptr) {
    // Mask scene-depth writes for everything OUTSIDE the low-res bracket, and
    // do it before the rebuild - reallocateBuffers() re-sends the drawing
    // environment, which is what puts the new mask on the GS. Every
    // draw_enable_tests / draw_setup_environment in the engine reads this one
    // field, so one assignment covers the frame clear, post fx, the 2D path
    // and the env-map / shadow-map restores.
    gs->zBuffer.mask = enabled ? 1 : 0;
    if (gs->needsBufferRealloc()) {
      if (vramRebuild != nullptr) {
        vramRebuild(vramRebuildUser);
        allocated = false;  // vram.reset() forgot our target
      } else {
        // No hook (an embedder driving RendererCoreBlss directly). An
        // oversized z buffer is merely wasteful, never wrong, so carry on.
        TYRA_WARN("BLSS: no VRAM rebuild hook - the z buffer keeps its "
                  "display-resolution size.");
      }
    }
  }

  if (!enabled) return;

  updateGeometry();
  allocate();

  if (packet == nullptr)
    packet = packet2_create(kPacketQwords, P2_TYPE_NORMAL, P2_MODE_NORMAL, 0);
  if (beginPacket == nullptr) {
    beginPacket = packet2_create(32, P2_TYPE_NORMAL, P2_MODE_NORMAL, 0);
    endPacket = packet2_create(32, P2_TYPE_NORMAL, P2_MODE_NORMAL, 0);
  }

  hasPrev = false;
  phase = 0;

  TYRA_LOG("BLSS configured: scale ", scaleX, "x", scaleY, ", sharpen ",
           static_cast<int>(sharpen * 100.0F), "%, temporal ",
           temporal ? 1 : 0, ", debug ", debugView);
}

void RendererCoreBlss::setNet(const float* t_w1, const float* t_b1,
                              const float* t_w2, const float* t_b2) {
  if (t_w1 == nullptr || t_b1 == nullptr || t_w2 == nullptr ||
      t_b2 == nullptr)
    return;
  for (int k = 0; k < kHidden; k++) {
    for (int i = 0; i < kFeatures; i++) w1[k][i] = t_w1[k * kFeatures + i];
    b1[k] = t_b1[k];
  }
  for (int m = 0; m < kOutputs; m++) {
    for (int k = 0; k < kHidden; k++) w2[m][k] = t_w2[m * kHidden + k];
    b2[m] = t_b2[m];
  }
}

// --------------------------------------------------------------- section 3 ---
// The camera as the reprojection needs it, derived from the view matrix rather
// than from stored CameraInfo3D pointers (which are transient). M4x4 is
// column-major storage of a column-vector matrix, and M4x4::setCamera builds
// rows { right, up, backward } with translation -(R * pos), so:
void RendererCoreBlss::capturePinhole() {
  const M4x4& v = core3D->getView();

  const float rx = v.data[0], ry = v.data[4], rz = v.data[8];
  const float ux = v.data[1], uy = v.data[5], uz = v.data[9];
  const float bx = v.data[2], by = v.data[6], bz = v.data[10];
  const float tx = v.data[12], ty = v.data[13], tz = v.data[14];

  cur.right[0] = rx;
  cur.right[1] = ry;
  cur.right[2] = rz;
  cur.up[0] = ux;
  cur.up[1] = uy;
  cur.up[2] = uz;
  // The view matrix' third row looks BACKWARD (position - target).
  cur.fwd[0] = -bx;
  cur.fwd[1] = -by;
  cur.fwd[2] = -bz;
  // pos = -R^T * t
  cur.pos[0] = -(rx * tx + ux * ty + bx * tz);
  cur.pos[1] = -(ry * tx + uy * ty + by * tz);
  cur.pos[2] = -(rz * tx + uz * ty + bz * tz);

  // The raster scale cancels out of the half-angle tangents (the projection's
  // x/y scale and the raster half-extent both carry it), so these are exactly
  // the values Renderer3DFrustumPlanes uses - which is the whole point of the
  // "frustum planes stay untouched" invariant.
  const float tang = tanf(core3D->getFov() * Math::HALF_ANG2RAD);
  cur.tanHalfFovY = tang;
  cur.tanHalfFovX = tang * settings->getAspectRatio();

  // Output-pixel scale of the projection: a world offset d at view depth w
  // spans d * projScale / w output pixels. 2048 is the VU1 raster scale
  // (stapip_vu1_program.cpp), data[0] / -data[5] the projection's x / y terms,
  // and the raster-scale factors lift low-res pixels to output pixels.
  const M4x4& p = core3D->getProjection();
  projScaleX = 2048.0F * p.data[0] * static_cast<float>(scaleX);
  projScaleY = 2048.0F * (-p.data[5]) * static_cast<float>(scaleY);
}

// --------------------------------------------------------------- section 2 ---
void RendererCoreBlss::addBag(float x0, float y0, float x1, float y1,
                              float wNear, float wFar, float texDetail,
                              float luma) {
  if (!enabled || !inScene) return;
  if (x1 <= x0 || y1 <= y0) return;

  // Only the tile RANGE is clamped to the screen, never the bbox itself: a
  // clipped bbox would plant a spurious bbox EDGE on the screen border.
  int tx0 = static_cast<int>(floorf(x0 / static_cast<float>(kTile)));
  int tx1 = static_cast<int>(floorf((x1 - 0.001F) / static_cast<float>(kTile)));
  int ty0 = static_cast<int>(floorf(y0 / static_cast<float>(kTile)));
  int ty1 = static_cast<int>(floorf((y1 - 0.001F) / static_cast<float>(kTile)));
  if (tx0 < 0) tx0 = 0;
  if (ty0 < 0) ty0 = 0;
  if (tx1 > cols - 1) tx1 = cols - 1;
  if (ty1 > rows - 1) ty1 = rows - 1;
  if (tx1 < tx0 || ty1 < ty0) return;

  proxies++;  // the instrument's "how finely was this frame described" column
  // ...and WHICH proxy is flattening the grid. One bag whose box covers the
  // frame pins depth, depthGrad and coverage in every tile it touches, and no
  // aggregate can say which bag that was - so keep the widest one and print
  // it. `wNear == the near plane` in that line means the box straddles the
  // eye, which is the shape that cannot carry a per-tile depth at all.
  const int span = (tx1 - tx0 + 1) * (ty1 - ty0 + 1);
  if (span > worstTiles) {
    worstTiles = span;
    worstX0 = x0;
    worstY0 = y0;
    worstX1 = x1;
    worstY1 = y1;
    worstWNear = wNear;
    worstWFar = wFar;
  }

  const float invNear = 1.0F / (wNear > 1e-4F ? wNear : 1e-4F);
  const float invFar = 1.0F / (wFar > 1e-4F ? wFar : 1e-4F);
  const float invTile2 = 1.0F / static_cast<float>(kTile * kTile);
  const float det = clamp01(texDetail);
  const float lum = clamp01(luma);

  for (int ty = ty0; ty <= ty1; ty++) {
    const float tileY0 = static_cast<float>(ty * kTile);
    const float tileY1 = tileY0 + static_cast<float>(kTile);
    float oy = (y1 < tileY1 ? y1 : tileY1) - (y0 > tileY0 ? y0 : tileY0);
    if (oy < 0.0F) oy = 0.0F;
    const bool topEdgeHere = y0 >= tileY0 && y0 < tileY1;
    const bool botEdgeHere = y1 >= tileY0 && y1 < tileY1;

    for (int tx = tx0; tx <= tx1; tx++) {
      const float tileX0 = static_cast<float>(tx * kTile);
      const float tileX1 = tileX0 + static_cast<float>(kTile);
      float ox = (x1 < tileX1 ? x1 : tileX1) - (x0 > tileX0 ? x0 : tileX0);
      if (ox < 0.0F) ox = 0.0F;

      const int idx = ty * cols + tx;
      const float a = ox * oy * invTile2;
      coverAcc[idx] += a;
      depthAcc[idx] += a * invNear;
      lumaAcc[idx] += a * lum;
      detAcc[idx] += a * det;
      if (invFar < dMin[idx]) dMin[idx] = invFar;
      if (invNear > dMax[idx]) dMax[idx] = invNear;

      // The four bbox EDGES: a horizontal edge inside this tile's y span
      // contributes its overlapX, a vertical one its overlapY.
      float e = 0.0F;
      if (topEdgeHere) e += ox;
      if (botEdgeHere) e += ox;
      if (x0 >= tileX0 && x0 < tileX1) e += oy;
      if (x1 >= tileX0 && x1 < tileX1) e += oy;
      edgeAcc[idx] += e;
    }
  }
}

void RendererCoreBlss::addBagSphere(const Vec4& worldCenter,
                                    const float& worldRadius,
                                    const float& texelArea,
                                    const float& luma) {
  if (!enabled || !inScene) return;
  // A reflection probe / camera feed / shadow caster re-submits the SAME bags
  // through a foreign camera, inside this bracket (that is what the nesting
  // fix made actually happen). Their screen bboxes would be computed with that
  // camera's view-projection and land in tiles they have nothing to do with,
  // so the feature grid must not see them.
  if (core3D->isForeignViewActive()) return;

  const Vec4 clip = core3D->getViewProj() * worldCenter;
  // Behind (or on) the near plane: no usable screen footprint.
  if (clip.w <= 1e-3F) return;
  const float invW = 1.0F / clip.w;

  // 2048 * ndc is the raster offset from the window centre in LOW-RES pixels;
  // the raster-scale factors lift it to output pixels.
  const float cx = static_cast<float>(outW) * 0.5F +
                   2048.0F * clip.x * invW * static_cast<float>(scaleX);
  const float cy = static_cast<float>(outH) * 0.5F +
                   2048.0F * clip.y * invW * static_cast<float>(scaleY);
  const float rx = worldRadius * projScaleX * invW;
  const float ry = worldRadius * projScaleY * invW;

  // Whole footprint off-screen: nothing to accumulate (this is also what
  // keeps a shadow-map / env "light camera" re-submission from poisoning the
  // grid should a game ever nest one inside the BLSS bracket).
  if (cx + rx < 0.0F || cx - rx > static_cast<float>(outW)) return;
  if (cy + ry < 0.0F || cy - ry > static_cast<float>(outH)) return;

  float wNear = clip.w - worldRadius;
  const float wFar = clip.w + worldRadius;
  if (wNear < 1e-4F) wNear = 1e-4F;

  // texDetail: the minification ratio the engine can compute for free from
  // what it already holds - texels per screen pixel, which is the direct
  // predictor of texture aliasing. NOT an editor bake (that is a follow-up);
  // the corpus computes the same ratio from its own materials.
  float texDetail = 0.0F;
  const float screenArea = 4.0F * rx * ry;
  if (texelArea > 0.0F && screenArea > 1e-3F) {
    texDetail = sqrtf(texelArea / screenArea) * 0.25F;
    if (texDetail > 1.0F) texDetail = 1.0F;
  }

  addBag(cx - rx, cy - ry, cx + rx, cy + ry, wNear, wFar, texDetail, luma);
}

// --------------------------------------------------------------- section 2 ---
// The twin of the corpus' bagOf() (src/blsscorpus.cpp): an object-space AABB
// through `mvp`, near-clipped along its twelve edges, reduced to a screen bbox
// and a w range. See the header for WHY this replaces the bounding sphere.
void RendererCoreBlss::addBagBox(const M4x4& mvp, const Vec4& objMin,
                                 const Vec4& objMax, const float& texelArea,
                                 const float& luma) {
  if (!enabled || !inScene) return;
  if (core3D->isForeignViewActive()) return;

  // Clip space is AFFINE in the box's parametric coordinates, so one
  // matrix-vector product and three scaled columns give all eight corners.
  // M4x4 is column-major storage of a column-vector matrix: data[0..3] is
  // column 0, i.e. what x multiplies.
  const float ex = objMax.x - objMin.x;
  const float ey = objMax.y - objMin.y;
  const float ez = objMax.z - objMin.z;
  const Vec4 base = mvp * Vec4(objMin.x, objMin.y, objMin.z, 1.0F);
  const float dxx = mvp.data[0] * ex, dxy = mvp.data[1] * ex;
  const float dxw = mvp.data[3] * ex;
  const float dyx = mvp.data[4] * ey, dyy = mvp.data[5] * ey;
  const float dyw = mvp.data[7] * ey;
  const float dzx = mvp.data[8] * ez, dzy = mvp.data[9] * ez;
  const float dzw = mvp.data[11] * ez;

  // Only x, y and w matter - z is the depth the GS would write and BLSS never
  // reads it (the network's `depth` is 1/w, section 4).
  struct P {
    float x, y, w;
  };
  P corner[8];
  for (int c = 0; c < 8; c++) {
    const float fx = (c & 1) ? 1.0F : 0.0F;
    const float fy = (c & 2) ? 1.0F : 0.0F;
    const float fz = (c & 4) ? 1.0F : 0.0F;
    corner[c].x = base.x + dxx * fx + dyx * fy + dzx * fz;
    corner[c].y = base.y + dxy * fx + dyy * fy + dzy * fz;
    corner[c].w = base.w + dxw * fx + dyw * fy + dzw * fz;
  }

  // The near plane is the PROJECT's, not a constant here: it is the plane the
  // engine's own clipper works to, so a box that reaches past it is described
  // by the part the frame will actually draw.
  const float near = settings->getNear();

  float x0 = 1e30F, y0 = 1e30F, x1 = -1e30F, y1 = -1e30F;
  float wn = 1e30F, wf = -1e30F;
  bool any = false;
  const float halfW = static_cast<float>(outW) * 0.5F;
  const float halfH = static_cast<float>(outH) * 0.5F;
  const float sx = 2048.0F * static_cast<float>(scaleX);
  const float sy = 2048.0F * static_cast<float>(scaleY);
  // 2048 * ndc is the raster offset from the window centre in LOW-RES pixels;
  // the raster-scale factors lift it to output pixels - the same mapping
  // addBagSphere uses, so the two proxies land in the same space.
  auto take = [&](const float& cx, const float& cy, const float& cw) {
    const float invW = 1.0F / cw;
    const float px = halfW + sx * cx * invW;
    const float py = halfH + sy * cy * invW;
    if (px < x0) x0 = px;
    if (px > x1) x1 = px;
    if (py < y0) y0 = py;
    if (py > y1) y1 = py;
    if (cw < wn) wn = cw;
    if (cw > wf) wf = cw;
    any = true;
  };

  // The twelve edges of eight corners. An edge that crosses the near plane
  // contributes its INTERSECTION, which is what keeps a floor the camera
  // stands on from projecting its behind-the-eye corners to nonsense.
  for (int c = 0; c < 8; c++) {
    for (int bit = 0; bit < 3; bit++) {
      if (c & (1 << bit)) continue;
      const P& a = corner[c];
      const P& b = corner[c | (1 << bit)];
      const bool ina = a.w >= near, inb = b.w >= near;
      if (!ina && !inb) continue;
      if (ina) take(a.x, a.y, a.w);
      if (inb) take(b.x, b.y, b.w);
      if (ina != inb) {
        const float d = b.w - a.w;
        const float t = (d > 1e-9F || d < -1e-9F) ? (near - a.w) / d : 0.0F;
        take(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, near);
      }
    }
  }
  if (!any) return;  // wholly behind the eye
  const float fOutW = static_cast<float>(outW);
  const float fOutH = static_cast<float>(outH);
  if (x1 <= 0.0F || y1 <= 0.0F || x0 >= fOutW || y0 >= fOutH) return;

  // A BOX THAT STRADDLES THE EYE AND STILL FILLS THE FRAME DESCRIBES NOTHING,
  // and both twins drop it (blss::accumulate's producer, bagOf(), has the same
  // rule). Its screen bbox is the whole frame BY CONSTRUCTION and its wNear is
  // the clip constant, not a measurement - so every tile it touches is handed
  // "fully covered, at the nearest representable depth", which is exactly the
  // `depth=1 grad=1 cover=1` in all 196 tiles that a generated game's SKY DOME
  // produced: a 90-unit dome centred on the camera, whose every package box
  // wraps the eye. The condition is threshold-free on purpose - it fires only
  // for a box that fills the frame in BOTH axes after clipping AND has no
  // usable near depth, which is the pathological shape and nothing else.
  const bool eyeInside = wn <= near * 1.0001F;
  const bool fillsFrame = x0 <= 0.0F && y0 <= 0.0F && x1 >= fOutW && y1 >= fOutH;
  if (eyeInside && fillsFrame) return;

  if (x0 < 0.0F) x0 = 0.0F;
  if (y0 < 0.0F) y0 = 0.0F;
  if (x1 > fOutW) x1 = fOutW;
  if (y1 > fOutH) y1 = fOutH;
  if (wn < near) wn = near;
  if (wf < wn) wf = wn;

  // Section 2's minification ratio, off the CLAMPED bbox - the corpus does the
  // same, so a proxy running off the side of the screen reports the detail of
  // what is on screen.
  float texDetail = 0.0F;
  if (texelArea > 0.0F) {
    float area = (x1 - x0) * (y1 - y0);
    if (area < 1.0F) area = 1.0F;
    texDetail = sqrtf(texelArea / area) * 0.25F;
    if (texDetail > 1.0F) texDetail = 1.0F;
  }

  addBag(x0, y0, x1, y1, wn, wf, texDetail, luma);
}

// --------------------------------------------------------------- section 2 ---
void RendererCoreBlss::finishTileStats() {
  const int n = cols * rows;
  for (int i = 0; i < n; i++) {
    const float acc = coverAcc[i];
    tCover[i] = acc > 1.0F ? 1.0F : acc;
    const float inv = 1.0F / (acc > 1e-6F ? acc : 1e-6F);
    tDepth[i] = depthAcc[i] * inv;
    tLuma[i] = lumaAcc[i] * inv;
    tDetail[i] = detAcc[i] * inv;
    if (acc > 0.0F) {
      tDepthMin[i] = dMin[i] > 1e29F ? 0.0F : dMin[i];
      tDepthMax[i] = dMax[i];
    } else {
      tDepthMin[i] = 0.0F;
      tDepthMax[i] = 0.0F;
    }
    const float e = edgeAcc[i] / static_cast<float>(2 * kTile);
    tEdge[i] = e > 1.0F ? 1.0F : e;
  }
}

// --------------------------------------------------------------- section 3 ---
void RendererCoreBlss::buildReproj() {
  const float fOutW = static_cast<float>(outW);
  const float fOutH = static_cast<float>(outH);

  for (int j = 0; j < cornerRows; j++) {
    const int py = j * kTile > outH ? outH : j * kTile;
    for (int i = 0; i < cornerCols; i++) {
      const int px = i * kTile > outW ? outW : i * kTile;
      const int c = j * cornerCols + i;
      cornerDu[c] = 0.0F;
      cornerDv[c] = 0.0F;
      if (!hasPrev) continue;

      // Representative 1/w: the mean over the adjacent COVERED tiles. No
      // coverage -> no reprojection (sky does not reproject).
      float sum = 0.0F;
      int count = 0;
      for (int dy = -1; dy <= 0; dy++) {
        const int ty = j + dy;
        if (ty < 0 || ty >= rows) continue;
        for (int dx = -1; dx <= 0; dx++) {
          const int tx = i + dx;
          if (tx < 0 || tx >= cols) continue;
          const int t = ty * cols + tx;
          if (tCover[t] <= 0.0F) continue;
          sum += tDepth[t];
          count++;
        }
      }
      if (count == 0) continue;
      const float invWrep = sum / static_cast<float>(count);
      if (invWrep <= 1e-6F) continue;

      const float w = 1.0F / invWrep;
      const float sX =
          (2.0F * static_cast<float>(px) / fOutW - 1.0F) * cur.tanHalfFovX;
      const float sY =
          (1.0F - 2.0F * static_cast<float>(py) / fOutH) * cur.tanHalfFovY;
      const float dirX = cur.fwd[0] + cur.right[0] * sX + cur.up[0] * sY;
      const float dirY = cur.fwd[1] + cur.right[1] * sX + cur.up[1] * sY;
      const float dirZ = cur.fwd[2] + cur.right[2] * sX + cur.up[2] * sY;
      const float relX = cur.pos[0] + dirX * w - prev.pos[0];
      const float relY = cur.pos[1] + dirY * w - prev.pos[1];
      const float relZ = cur.pos[2] + dirZ * w - prev.pos[2];

      const float wPrev =
          relX * prev.fwd[0] + relY * prev.fwd[1] + relZ * prev.fwd[2];
      if (wPrev < 1e-3F) continue;  // behind the previous camera

      const float sXp =
          (relX * prev.right[0] + relY * prev.right[1] + relZ * prev.right[2]) /
          (wPrev * prev.tanHalfFovX);
      const float sYp =
          (relX * prev.up[0] + relY * prev.up[1] + relZ * prev.up[2]) /
          (wPrev * prev.tanHalfFovY);

      // The history IS the other display framebuffer, so histW/histH ==
      // outW/outH and one history texel is one output pixel.
      float du = (sXp * 0.5F + 0.5F) * fOutW - static_cast<float>(px);
      float dv = (0.5F - sYp * 0.5F) * fOutH - static_cast<float>(py);
      // Keep the 12.4 UV field addressable (see emitGrid) - an offset this
      // large is off-screen history anyway, which the region clamp folds onto
      // the border.
      if (du < -fOutW) du = -fOutW;
      if (du > fOutW) du = fOutW;
      if (dv < -fOutH) dv = -fOutH;
      if (dv > fOutH) dv = fOutH;
      cornerDu[c] = du;
      cornerDv[c] = dv;
    }
  }
}

// --------------------------------------------------------------- section 4 ---
void RendererCoreBlss::buildFeatures() {
  const int n = cols * rows;

  // Normalised depth first: the 4-neighbour gradient below differences the
  // NORMALISED values, so it needs the whole field before it can run.
  for (int i = 0; i < n; i++) {
    feat[i][1] = clamp01(tDepth[i] * kDepthRef);
  }

  for (int cy = 0; cy < rows; cy++) {
    for (int cx = 0; cx < cols; cx++) {
      const int i = cy * cols + cx;

      // motion: the reprojection at the tile CENTRE, in tile edges. The
      // centre of a quad is the mean of its four corners.
      const int c00 = cy * cornerCols + cx;
      const int c10 = c00 + 1;
      const int c01 = c00 + cornerCols;
      const int c11 = c01 + 1;
      const float du = (cornerDu[c00] + cornerDu[c10] + cornerDu[c01] +
                        cornerDu[c11]) *
                       0.25F;
      const float dv = (cornerDv[c00] + cornerDv[c10] + cornerDv[c01] +
                        cornerDv[c11]) *
                       0.25F;
      feat[i][0] = clamp01(sqrtf(du * du + dv * dv) /
                           static_cast<float>(kTile));

      // depthGrad: the larger of the 4-neighbour difference and the tile's own
      // near/far spread - a disocclusion / silhouette proxy.
      const float d = feat[i][1];
      float grad = (tDepthMax[i] - tDepthMin[i]) * kDepthRef;
      if (cx > 0) {
        const float t = fabsf(feat[i - 1][1] - d);
        if (t > grad) grad = t;
      }
      if (cx < cols - 1) {
        const float t = fabsf(feat[i + 1][1] - d);
        if (t > grad) grad = t;
      }
      if (cy > 0) {
        const float t = fabsf(feat[i - cols][1] - d);
        if (t > grad) grad = t;
      }
      if (cy < rows - 1) {
        const float t = fabsf(feat[i + cols][1] - d);
        if (t > grad) grad = t;
      }
      feat[i][2] = clamp01(grad);

      feat[i][3] = tEdge[i];
      feat[i][4] = tDetail[i];
      feat[i][5] = tCover[i];
      feat[i][6] = tLuma[i];
    }
  }
}

// --------------------------------------------------------------- section 5 ---
void RendererCoreBlss::alphaScales(float out[kOutputs]) const {
  const float k = clamp01(sharpen);
  out[0] = 128.0F;        // wA: aA = wA * 128
  out[1] = kTemporalMax;  // wC: aC = wC * kTemporalMax
  out[2] = k * 128.0F;    // wD: aD = wD * sharpen * 128
}

void RendererCoreBlss::runNet() {
  const int n = cols * rows;
  // THE DEADZONE, as a threshold on each OUTPUT rather than on its byte: the
  // three scales differ and one of them is a project setting, so the weight
  // below which a tile is "off" is kDeadzoneAlpha/128 for point,
  // kDeadzoneAlpha/kTemporalMax for temporal and kDeadzoneAlpha/(sharpen*128)
  // for sharpen. Derived here, once, from the same scales cornerAlpha()
  // quantises with (kDeadzoneAlpha). A scale of 0 - sharpen 0 - makes the
  // threshold infinite, which is right: those passes are not drawn at all.
  float scale[kOutputs];
  alphaScales(scale);
  float dead[kOutputs];
  for (int m = 0; m < kOutputs; m++)
    dead[m] = scale[m] > 0.0F ? kDeadzoneAlpha / scale[m] : 1e30F;
  for (int i = 0; i < n; i++) {
    // An empty tile is not a decision the network is entitled to make. The
    // oracle's importance weighting gives "tiles where every kernel is
    // identical" no vote during training, so the net's output there is
    // unsupervised noise - and it came out asking for full temporal
    // reconstruction of the SKY, which costs nothing in PSNR (a flat colour
    // blends with itself) and ghosts as soon as the camera turns.
    // kMinCoverage, twinned with src/blss.hpp.
    if (feat[i][5] < 0.02F) {
      outW_A[i] = 0.0F;
      outW_C[i] = 0.0F;
      outW_D[i] = 0.0F;
      continue;
    }
    float h[kHidden];
    for (int k = 0; k < kHidden; k++) {
      float s = b1[k];
      for (int f = 0; f < kFeatures; f++) s += w1[k][f] * feat[i][f];
      h[k] = tanhf(s);
    }
    float o[kOutputs];
    for (int m = 0; m < kOutputs; m++) {
      float s = b2[m];
      for (int k = 0; k < kHidden; k++) s += w2[m][k] * h[k];
      o[m] = 1.0F / (1.0F + expf(-s));
      // A logistic cannot emit 0, and "barely on" costs exactly as much fill
      // as "fully on" - kDeadzoneAlpha. Three compares per tile against
      // thresholds derived above; twinned with netField() on the host, which
      // snaps at the same point, per TILE, BEFORE the corner averaging below.
      if (o[m] <= dead[m]) o[m] = 0.0F;
    }
    outW_A[i] = o[0];
    outW_C[i] = o[1];
    outW_D[i] = o[2];
  }

  // Per-tile values -> grid corners: a corner is the MEAN of the up-to-four
  // tiles touching it (edge and corner tiles average fewer). The rasteriser's
  // Gouraud interpolation of these is the upsampling of the weight field.
  for (int j = 0; j < cornerRows; j++) {
    for (int i = 0; i < cornerCols; i++) {
      const int c = j * cornerCols + i;
      float sa = 0.0F, sc = 0.0F, sd = 0.0F;
      int count = 0;
      for (int dy = -1; dy <= 0; dy++) {
        const int ty = j + dy;
        if (ty < 0 || ty >= rows) continue;
        for (int dx = -1; dx <= 0; dx++) {
          const int tx = i + dx;
          if (tx < 0 || tx >= cols) continue;
          const int t = ty * cols + tx;
          sa += outW_A[t];
          sc += outW_C[t];
          sd += outW_D[t];
          count++;
        }
      }
      if (count == 0) {
        cornerA[c] = cornerC[c] = cornerD[c] = 0.0F;
        continue;
      }
      const float inv = 1.0F / static_cast<float>(count);
      cornerA[c] = sa * inv;
      cornerC[c] = sc * inv;
      cornerD[c] = sd * inv;
    }
  }
}

// ---------------------------------------------------- the instrument (dv 2) ---
// PERMANENT, and it is permanent on purpose. A previous round added exactly
// this measurement, read it once and DELETED it - which is how the network
// spent its whole life fitted to the corpus' distribution and evaluated on the
// console's without anybody being able to see the difference. Same channel
// names and the same order as blss::kFeatureNames, so a line here sits next to
// a row of `--blss-eval --features`, and `--blss-eval --probe "<line>"` reads
// one straight back.
void RendererCoreBlss::logFeatureSpread() {
#ifndef NDEBUG
  // EXACTLY blss::kFeatureNames, spelled the same and in the same order: the
  // host's --probe matches these keys, and a line whose keys do not match the
  // corpus' is a line nobody can place in the corpus.
  static const char* const kFeatureNames[kFeatures] = {
      "motion", "depth", "depthGrad", "edgeDens", "texDetail", "coverage",
      "luma"};
  static const char* const kOutputNames[kOutputs] = {"point", "temporal",
                                                     "sharpen"};
  const int n = cols * rows;
  if (n <= 0) return;

  float fMin[kFeatures], fMax[kFeatures], fSum[kFeatures];
  for (int f = 0; f < kFeatures; f++) {
    fMin[f] = 1e30F;
    fMax[f] = -1e30F;
    fSum[f] = 0.0F;
  }
  float oMin[kOutputs], oMax[kOutputs], oSum[kOutputs];
  for (int m = 0; m < kOutputs; m++) {
    oMin[m] = 1e30F;
    oMax[m] = -1e30F;
    oSum[m] = 0.0F;
  }
  int covered = 0;
  for (int i = 0; i < n; i++) {
    if (tCover[i] >= 0.02F) covered++;
    for (int f = 0; f < kFeatures; f++) {
      const float v = feat[i][f];
      if (v < fMin[f]) fMin[f] = v;
      if (v > fMax[f]) fMax[f] = v;
      fSum[f] += v;
    }
    const float o[kOutputs] = {outW_A[i], outW_C[i], outW_D[i]};
    for (int m = 0; m < kOutputs; m++) {
      if (o[m] < oMin[m]) oMin[m] = o[m];
      if (o[m] > oMax[m]) oMax[m] = o[m];
      oSum[m] += o[m];
    }
  }
  const float inv = 1.0F / static_cast<float>(n);

  // Occupancy through the ENGINE's own skip rule - a cell is drawn when ANY of
  // its four corner alpha bytes is non-zero (emitGrid), so one tile asking for
  // a kernel lights the nine cells touching its corners. That bleed is real
  // fill and this is the number blss::occupancy() reports on the host.
  int cells = 0, drawn[3] = {0, 0, 0};
  for (int cy = 0; cy < rows; cy++)
    for (int cx = 0; cx < cols; cx++) {
      cells++;
      const int c00 = cy * cornerCols + cx;
      const int idx[4] = {c00, c00 + 1, c00 + cornerCols, c00 + cornerCols + 1};
      for (int p = 0; p < 3; p++) {
        // cornerAlpha's passes: 1 = point, 2 = temporal, 3 = sharpen.
        bool on = false;
        for (int k = 0; k < 4; k++)
          if (cornerAlpha(p + 1, idx[k]) != 0) on = true;
        if (on) drawn[p]++;
      }
    }
  const float invC = cells > 0 ? 1.0F / static_cast<float>(cells) : 0.0F;
  const float pt = static_cast<float>(drawn[0]) * invC;
  const float tp = static_cast<float>(drawn[1]) * invC;
  const float sh = sharpen > 0.0F ? static_cast<float>(drawn[2]) * invC : 0.0F;

  char line[512];
  snprintf(line, sizeof(line),
           "BLSSGRID %dx%d tiles, %d covered, %d proxy(ies), scale %dx%d",
           cols, rows, covered, proxies, scaleX, scaleY);
  TYRA_LOG(line);
  snprintf(line, sizeof(line),
           "BLSSWORST %d of %d tile(s), bbox %.0f,%.0f..%.0f,%.0f, w %.2f..%.2f"
           " (near plane %.2f)",
           worstTiles, cols * rows, static_cast<double>(worstX0),
           static_cast<double>(worstY0), static_cast<double>(worstX1),
           static_cast<double>(worstY1), static_cast<double>(worstWNear),
           static_cast<double>(worstWFar),
           static_cast<double>(settings->getNear()));
  TYRA_LOG(line);

  // min/mean/max per channel. A channel whose three numbers are equal is a
  // channel the 147 weights cannot use - that is the whole point of printing
  // the SPREAD rather than one sampled tile.
  int at = 0;
  at = snprintf(line, sizeof(line), "BLSSFEAT");
  for (int f = 0; f < kFeatures; f++)
    at += snprintf(line + at, sizeof(line) - at, " %s=%.3f/%.3f/%.3f",
                   kFeatureNames[f], static_cast<double>(fMin[f]),
                   static_cast<double>(fSum[f] * inv),
                   static_cast<double>(fMax[f]));
  TYRA_LOG(line);

  at = snprintf(line, sizeof(line), "BLSSOUT ");
  for (int m = 0; m < kOutputs; m++)
    at += snprintf(line + at, sizeof(line) - at, " %s=%.3f/%.3f/%.3f",
                   kOutputNames[m], static_cast<double>(oMin[m]),
                   static_cast<double>(oSum[m] * inv),
                   static_cast<double>(oMax[m]));
  TYRA_LOG(line);

  snprintf(line, sizeof(line),
           "BLSSFILL point=%.1f%% temporal=%.1f%% sharpen=%.1f%% passes=%.2f",
           static_cast<double>(pt * 100.0F), static_cast<double>(tp * 100.0F),
           static_cast<double>(sh * 100.0F),
           static_cast<double>(1.0F + pt + tp + 2.0F * sh));
  TYRA_LOG(line);
#endif
}

// --------------------------------------------------------------- section 1 ---
void RendererCoreBlss::beginScene(const Color& clearColor) {
  if (!enabled) return;
  if (!allocated) allocate();

  // The raster redirect below is global GS state - drain in-flight PATH1 3D
  // work first. Before any 3D pipeline is up there is nothing to drain (and
  // the FINISH handshake would spin forever).
  if (path1->isVU1Configured()) sync->align3D();

  // Two phases, alternating every frame, +-0.25 low-res pixels = +-4 raw
  // XYOFFSET units, which is why the host can reproduce them bit-exactly.
  jitter16X = phase == 0 ? -4 : 4;
  jitter16Y = phase == 0 ? -4 : 4;
  phase ^= 1;

  const int n = cols * rows;
  for (int i = 0; i < n; i++) {
    coverAcc[i] = depthAcc[i] = lumaAcc[i] = detAcc[i] = edgeAcc[i] = 0.0F;
    dMin[i] = 1e30F;
    dMax[i] = 0.0F;
  }
  proxies = 0;
  worstTiles = 0;

  capturePinhole();
  inScene = true;

  // XYOFFSET is written RAW (not through draw_primitive_xyoffset) so the
  // jitter is exactly +-4/16 of a pixel, and window-CENTRED because that is
  // what the VU1 pipeline's fixed 2048 raster scale expects. The GS subtracts
  // the offset from the vertex coordinate, so a vertex at 2048 + s lands at
  // lowW/2 + s.
  const int offX16 =
      static_cast<int>((2048.0F - lowW / 2.0F) * 16.0F) + jitter16X;
  const int offY16 = static_cast<int>((2048.0F - lowH / 2.0F) * 16.0F) +
                     jitter16Y + gs->getFieldYOffset16();

  const int zbp = static_cast<int>(gs->zBuffer.address) >> 11;
  const int zsm = static_cast<int>(gs->zBuffer.zsm);

  // PUBLISH the redirect before the packet is built. Two things read it:
  // every nested bracket's end() (the env map, the camera feed, the shadow
  // map - all of which run inside the generated renderScene() and used to
  // restore the DISPLAY buffer here, silently cancelling BLSS), and
  // draw_enable_tests through zBuffer.mask, which is unmasked for exactly the
  // duration of this bracket because the z buffer only covers the low-res
  // raster. The offsets carry the jitter, so a nested restore puts back THIS
  // frame's sub-pixel offset and not a jitter-free one.
  RendererCoreGS::RasterTarget target;
  target.frameAddress = lowVram;
  target.frameWidth = lowBufW;
  target.scissorX0 = 0;
  target.scissorX1 = lowW - 1;
  target.scissorY0 = 0;
  target.scissorY1 = lowH - 1;
  target.offsetX16 = offX16;
  target.offsetY16 = offY16;
  gs->redirectRasterTo(target);
  gs->zBuffer.mask = 0;

  packet2_reset(beginPacket, false);
  qword_t* q = beginPacket->base;
  // NLOOP = 9: XYOFFSET, FRAME, SCISSOR, ZBUF, TEST, RGBAQ, PRIM and the clear
  // sprite's two XYZ2. A miscount here stalls the GIF forever - the stray
  // qword parses as a new giftag with a garbage NLOOP - and the symptom is a
  // hang in draw_wait_finish with a clean log.
  PACK_GIFTAG(q, GIF_SET_TAG(9, 0, 0, 0, GIF_FLG_PACKED, 1), GIF_REG_AD);
  q++;
  // First, so the clear sprite below is addressed in this offset's space (the
  // env map paid for getting this order wrong: the sprite rasterised against
  // the previous window and never painted).
  PACK_GIFTAG(q, GS_SET_XYOFFSET(offX16, offY16), GS_REG_XYOFFSET_1);
  q++;
  PACK_GIFTAG(q, GS_SET_FRAME(lowVram >> 11, lowBufW >> 6, GS_PSM_32, 0),
              GS_REG_FRAME_1);
  q++;
  PACK_GIFTAG(q, GS_SET_SCISSOR(0, lowW - 1, 0, lowH - 1), GS_REG_SCISSOR_1);
  q++;
  // No low-res z buffer: point ZBUF at the MAIN z buffer (section 7). Its row
  // stride comes from FRAME.FBW, so the low-res pass reinterprets and
  // overwrites a prefix of it - which is free, because the frame clear already
  // happened and nothing after the composite reads scene depth (depth of
  // field, portals and split view are documented as incompatible with BLSS).
  PACK_GIFTAG(q, GS_SET_ZBUF(zbp, zsm, 0), GS_REG_ZBUF_1);
  q++;
  PACK_GIFTAG(q, GS_SET_TEST(0, 0, 0, 0, 0, 0, 1, ZTEST_METHOD_ALLPASS),
              GS_REG_TEST_1);
  q++;
  PACK_GIFTAG(q,
              GS_SET_RGBAQ(static_cast<u8>(clearColor.r),
                           static_cast<u8>(clearColor.g),
                           static_cast<u8>(clearColor.b), 0x80, 0x3F800000),
              GS_REG_RGBAQ);
  q++;
  PACK_GIFTAG(q, GS_SET_PRIM(6 /* sprite */, 0, 0, 0, 0, 0, 0, 0, 0),
              GS_REG_PRIM);
  q++;
  // One pixel of slop each way so the jittered window still covers the whole
  // scissor; the scissor clips the overhang. With the all-pass test and an
  // unmasked ZBUF this also resets the depths the scene will use.
  PACK_GIFTAG(q, GS_SET_XYZ(offX16 - 16, offY16 - 16, 0), GS_REG_XYZ2);
  q++;
  PACK_GIFTAG(q,
              GS_SET_XYZ(offX16 + (lowW + 1) * 16, offY16 + (lowH + 1) * 16, 0),
              GS_REG_XYZ2);
  q++;
  // Put the drawing environment's real z test back - unlike the env map, the
  // scene inside this bracket must occlude itself.
  q = draw_enable_tests(q, 0, &gs->zBuffer);
  packet2_update(beginPacket, q);
  packet2_update(beginPacket, draw_finish(beginPacket->next));
  dma_channel_wait(DMA_CHANNEL_GIF, 0);
  dma_channel_send_packet2(beginPacket, DMA_CHANNEL_GIF, true);
  draw_wait_finish();
}

void RendererCoreBlss::endScene() {
  if (!enabled || !inScene) return;
  inScene = false;

  // Drain the low-res scene itself before the raster moves back out.
  if (path1->isVU1Configured()) sync->align3D();

  // Close the redirect FIRST, so the restore below asks the GS for the display
  // buffer, and mask scene-depth writes again: the z buffer only covers the
  // low-res raster now, and everything that follows (composite, post fx, the
  // HUD) draws full-screen. Both are read by emitRasterRestore.
  gs->endRasterRedirect();
  gs->zBuffer.mask = 1;

  packet2_reset(endPacket, false);
  // TEXFLUSH: the low-res target was just rendered and composite() is about to
  // sample it as a texture.
  qword_t* q = gs->emitRasterRestore(endPacket->base, true);
  packet2_update(endPacket, q);
  packet2_update(endPacket, draw_finish(endPacket->next));
  dma_channel_wait(DMA_CHANNEL_GIF, 0);
  dma_channel_send_packet2(endPacket, DMA_CHANNEL_GIF, true);
  draw_wait_finish();
}

// --------------------------------------------------------------- section 6 ---
u8 RendererCoreBlss::cornerAlpha(int pass, int corner) const {
  // trunc(), not round: the host uses the same truncation, and the oracle was
  // fitted against it. The scales come from alphaScales() rather than from
  // three literals here, so runNet()'s deadzone divides by exactly what this
  // multiplies by (the temporal one is kTemporalMax - an accumulator's
  // retention, NOT a two-frame average; see src/blss.hpp).
  float s[kOutputs];
  alphaScales(s);
  switch (pass) {
    case 1:  // point / nearest:      aA = wA * 128
      return static_cast<u8>(clamp01(cornerA[corner]) * s[0]);
    case 2:  // temporal:             aC = wC * kTemporalMax
      return static_cast<u8>(clamp01(cornerC[corner]) * s[1]);
    case 3:  // sharpen, additive:    aD = wD * sharpen * 128
    case 4:  // sharpen, subtractive: same byte
      return static_cast<u8>(clamp01(cornerD[corner]) * s[2]);
    default:
      return 0x80;  // pass 0 is opaque; the debug tint blends with FIX
  }
}

qword_t* RendererCoreBlss::emitPassState(qword_t* q, int srcVram, int srcBufW,
                                         int texW, int texH, bool linear,
                                         u64 alpha, bool textured) {
  const auto* fb = gs->getCurrentFrameBuffer();
  const int fbVram = static_cast<int>(fb->address);
  const int fbBufW = static_cast<int>(fb->width);
  const int zbp = static_cast<int>(gs->zBuffer.address) >> 11;
  const int zsm = static_cast<int>(gs->zBuffer.zsm);

  // NLOOP: 10 textured (TEXFLUSH, TEX0, TEX1, CLAMP, TEXA, COLCLAMP, FRAME,
  // ALPHA, TEST, ZBUF) or 5 untextured (the last five). PRIM is written per run
  // in emitGrid, because re-writing it is what restarts the triangle strip.
  const int nloop = textured ? 10 : 5;
  PACK_GIFTAG(q, GIF_SET_TAG(nloop, 0, 0, 0, GIF_FLG_PACKED, 1), GIF_REG_AD);
  q++;
  if (textured) {
    // Every pass reads what the previous one wrote (pass 3 reads the other
    // display buffer, 4/5 re-read the low-res target) - invalidate the cache.
    PACK_GIFTAG(q, GS_SET_TEXFLUSH(0), GS_REG_TEXFLUSH);
    q++;
    // TCC = 0 (RGB only, alpha from TEXA) + TFX = MODULATE + vertex RGB pinned
    // to 128 is what makes the GS blend factor the VERTEX alpha:
    //   RGB = Ct * 128 >> 7 = Ct      (exact, no loss)
    //   A   = TA0 * Av >> 7 = Av      (with TEXA below)
    PACK_GIFTAG(q,
                GS_SET_TEX0(srcVram >> 6, srcBufW >> 6, GS_PSM_32, lg2(texW),
                            lg2(texH), 0 /* TCC: RGB */,
                            0 /* TFX: MODULATE */, 0, 0, 0, 0, 0),
                GS_REG_TEX0_1);
    q++;
    const int f = linear ? 1 : 0;
    PACK_GIFTAG(q, GS_SET_TEX1(1, 0, f, f, 0, 0, 0), GS_REG_TEX1_1);
    q++;
    // Region clamp to the REAL extent: TW/TH round up to a power of two, and
    // the +half-texel taps of pass 5 must not sample past the last texel.
    PACK_GIFTAG(q, GS_SET_CLAMP(2, 2, 0, texW - 1, 0, texH - 1),
                GS_REG_CLAMP_1);
    q++;
    // The first TEXA write in the engine. Without it the passes inherit the GS
    // reset value (0), TCC = 0 would hand every texel alpha 0, and the vertex
    // alpha trick above would multiply everything by zero.
    PACK_GIFTAG(q, GS_SET_TEXA(0x80, 0, 0x80), GS_REG_TEXA);
    q++;
  }
  // COLCLAMP is never written anywhere else either, so it also holds the GS
  // reset value - which is MASK, i.e. blend results WRAP. Section 6 assumes
  // 0..255 clamping and the host clamps, so a saturated pixel would diverge
  // spectacularly. Set it per pass and restore the engine-wide value on exit.
  PACK_GIFTAG(q, GS_SET_COLCLAMP(COLOR_CLAMP_ENABLE), GS_REG_COLCLAMP);
  q++;
  PACK_GIFTAG(q, GS_SET_FRAME(fbVram >> 11, fbBufW >> 6, GS_PSM_32, 0),
              GS_REG_FRAME_1);
  q++;
  PACK_GIFTAG(q, alpha, GS_REG_ALPHA_1);
  q++;
  // Alpha test OFF and z test ALL-PASS. The weights ARE vertex alpha, so the
  // drawing environment's NOTEQUAL-0 alpha test would DISCARD every
  // zero-weight corner's fragments instead of blending them at zero (the post
  // fx path pays for the same lesson with its blits).
  PACK_GIFTAG(q, GS_SET_TEST(0, 0, 0, 0, 0, 0, 1, ZTEST_METHOD_ALLPASS),
              GS_REG_TEST_1);
  q++;
  // z writes masked: Tyra never clears z separately, so a stray z from a
  // full-screen pass would lock those pixels against all later drawing.
  PACK_GIFTAG(q, GS_SET_ZBUF(zbp, zsm, 1), GS_REG_ZBUF_1);
  q++;
  return q;
}

qword_t* RendererCoreBlss::emitGrid(qword_t* q, int pass) {
  const bool debugPass = pass == 5;
  const bool abe = pass != 0;
  // TRIANGLE_STRIP (4), IIP = 1 (Gouraud - the whole point; flat shading would
  // take one corner's alpha for the entire primitive), FST = 1 (12.4 UVs, no
  // perspective divide).
  const u64 primVal =
      debugPass
          ? GS_SET_PRIM(4, 1, 0, 0, 1, 0, 0, 0, 0)
          : GS_SET_PRIM(4, 1, 1, 0, abe ? 1 : 0, 0, 1 /* FST */, 0, 0);

  // Per-corner alpha byte + UV, once per pass.
  u8 av[kMaxCorners];
  s16 uv[kMaxCorners][2];
  const int uMax = 0x3FFF;
  for (int j = 0; j < cornerRows; j++) {
    const int py = j * kTile > outH ? outH : j * kTile;
    for (int i = 0; i < cornerCols; i++) {
      const int px = i * kTile > outW ? outW : i * kTile;
      const int c = j * cornerCols + i;
      av[c] = cornerAlpha(pass, c);

      int u, v;
      if (pass == 2) {
        // History: the previous full-resolution frame, sampled at the
        // reprojected pixel. Same shape as the low-res mapping with sx = 1 and
        // the reprojection offset playing the jitter's role.
        u = static_cast<int>(
            floorf((static_cast<float>(px) + 0.5F + cornerDu[c]) * 16.0F +
                   0.5F));
        v = static_cast<int>(
            floorf((static_cast<float>(py) + 0.5F + cornerDv[c]) * 16.0F +
                   0.5F));
      } else {
        // u(x) = (x + 0.5) / sx + jx, expressed as a constant +jx*16 on the
        // 12.4 UVs. The +0.5 is the GS' own texel-centre convention, so a
        // vertex at output pixel boundary px maps to px/sx + jx exactly and
        // the rasteriser evaluates (x + 0.5)/sx + jx at the pixel centre.
        u = (px << 4) / scaleX + jitter16X;
        v = (py << 4) / scaleY + jitter16Y;
        if (pass == 4) {
          u += 8;  // + half a texel: the 2x2 box of the unsharp mask
          v += 8;
        }
      }
      // The UV register's fields are 14 bits UNSIGNED - a negative value would
      // wrap to ~1023 texels and make the whole first cell sample the far
      // edge. Clamping to 0 is what the region clamp would do to the sample
      // anyway; it costs a quarter texel of gradient in the outermost cell.
      if (u < 0) u = 0;
      if (v < 0) v = 0;
      if (u > uMax) u = uMax;
      if (v > uMax) v = uMax;
      uv[c][0] = static_cast<s16>(u);
      uv[c][1] = static_cast<s16>(v);
    }
  }

  // Sparsity is the performance knob, not an optimisation: a cell whose alpha
  // rounds to 0 everywhere is a RUN BREAK, not a transparent draw. Pass 0 is
  // the opaque base and the debug tint covers everything, so both skip nothing.
  const bool sparse = !debugPass && pass != 0;

  for (int cy = 0; cy < rows; cy++) {
    int cx = 0;
    while (cx < cols) {
      if (sparse) {
        const int c00 = cy * cornerCols + cx;
        if (av[c00] == 0 && av[c00 + 1] == 0 && av[c00 + cornerCols] == 0 &&
            av[c00 + cornerCols + 1] == 0) {
          cx++;
          continue;
        }
      }
      int runEnd = cx;
      while (runEnd < cols) {
        if (sparse) {
          const int c00 = cy * cornerCols + runEnd;
          if (av[c00] == 0 && av[c00 + 1] == 0 && av[c00 + cornerCols] == 0 &&
              av[c00 + cornerCols + 1] == 0)
            break;
        }
        runEnd++;
      }

      const int nVerts = 2 * (runEnd - cx + 1);
      // NLOOP = 1 (PRIM) + 3 registers per vertex (RGBAQ, UV, XYZ2). Count
      // these: an undercount stalls the GIF forever.
      PACK_GIFTAG(q, GIF_SET_TAG(1 + 3 * nVerts, 0, 0, 0, GIF_FLG_PACKED, 1),
                  GIF_REG_AD);
      q++;
      // Re-written per run: writing PRIM resets the GS' vertex queue, which is
      // exactly how a strip is broken and restarted.
      PACK_GIFTAG(q, primVal, GS_REG_PRIM);
      q++;

      // THE VERTEX ORDER IS LOAD-BEARING (section 5): the weight field is
      // interpolated piecewise-linearly over TWO TRIANGLES, and the host's
      // triLerp models exactly this. Emitting
      //   (i, j) (i, j+1) (i+1, j) (i+1, j+1) (i+2, j) ...
      // puts every quad's diagonal on (i, j+1) -> (i+1, j). Any other order
      // and the two implementations disagree in the middle of every tile -
      // precisely where the oracle's labels were fitted.
      for (int i = cx; i <= runEnd; i++) {
        for (int d = 0; d < 2; d++) {
          const int j = cy + d;
          const int c = j * cornerCols + i;
          const int px = i * kTile > outW ? outW : i * kTile;
          const int py = j * kTile > outH ? outH : j * kTile;

          if (debugPass) {
            // Tint by the winning kernel: red = point, green = temporal,
            // blue = sharpen. Untextured, mixed in at a fixed 50%.
            const u8 r = static_cast<u8>(clamp01(cornerA[c]) * 255.0F);
            const u8 g = static_cast<u8>(clamp01(cornerC[c]) * 255.0F);
            const u8 b = static_cast<u8>(clamp01(cornerD[c]) * 255.0F);
            PACK_GIFTAG(q, GS_SET_RGBAQ(r, g, b, 0x80, 0x3F800000),
                        GS_REG_RGBAQ);
          } else {
            // RGB pinned to 128 so MODULATE is the identity on the texel;
            // the alpha byte is the network's weight for this corner.
            PACK_GIFTAG(q, GS_SET_RGBAQ(0x80, 0x80, 0x80, av[c], 0x3F800000),
                        GS_REG_RGBAQ);
          }
          q++;
          PACK_GIFTAG(q, GS_SET_UV(uv[c][0], uv[c][1]), GS_REG_UV);
          q++;
          // Screen-origin XYOFFSET for these passes (set by composite), so the
          // window coordinate is the vertex minus 2048.
          PACK_GIFTAG(q, GS_SET_XYZ((2048 + px) << 4, (2048 + py) << 4, 0),
                      GS_REG_XYZ2);
          q++;
        }
      }
      cx = runEnd;
    }
  }
  return q;
}

void RendererCoreBlss::composite() {
  if (!enabled || gs == nullptr || packet == nullptr) return;

  // Defensive: endScene() already drained, but the composite must never race
  // the tail of the low-res render it is about to sample.
  if (path1->isVU1Configured()) sync->align3D();

  finishTileStats();
  buildReproj();
  buildFeatures();
  runNet();

  // debugView 2: the feature/output spread of this frame, once a second. It
  // runs HERE, on the same tile stats and outputs the packet below is about to
  // quantise, so what it prints is what the GS draws.
  if (debugView >= 2 && ++logFrame >= kLogEvery) {
    logFrame = 0;
    logFeatureSpread();
  }

  const auto* hist = gs->getPreviousFrameBuffer();
  const int histVram = static_cast<int>(hist->address);
  const int histBufW = static_cast<int>(hist->width);

  packet2_reset(packet, false);
  qword_t* q = packet->base;

  // Screen-origin raster offset for the grid, exactly like the post fx path;
  // the window-centred one the VU1 pipeline expects goes back at the end.
  PACK_GIFTAG(q, GIF_SET_TAG(1, 0, 0, 0, GIF_FLG_PACKED, 1), GIF_REG_AD);
  q++;
  PACK_GIFTAG(q, GS_SET_XYOFFSET(2048 * 16, 2048 * 16 + gs->getFieldYOffset16()),
              GS_REG_XYOFFSET_1);
  q++;

  // Pass 1 - the bilinear base, opaque.
  q = emitPassState(q, lowVram, lowBufW, lowW, lowH, true,
                    GS_SET_ALPHA(0, 1, 0, 1, 0), true);
  q = emitGrid(q, 0);

  // Pass 2 - nearest, lerped in by wA: (Cs - Cd) * As >> 7 + Cd.
  q = emitPassState(q, lowVram, lowBufW, lowW, lowH, false,
                    GS_SET_ALPHA(0, 1, 0, 1, 0), true);
  q = emitGrid(q, 1);

  // Pass 3 - the reprojected previous frame, lerped in by wC/2. The history is
  // the OTHER display framebuffer: already full resolution, one frame old, and
  // itself composited, so a static camera keeps accumulating samples.
  if (temporal && hasPrev) {
    q = emitPassState(q, histVram, histBufW, outW, outH, true,
                      GS_SET_ALPHA(0, 1, 0, 1, 0), true);
    q = emitGrid(q, 2);
  }

  // Passes 4 and 5 - the unsharp mask B + k*(B - box(B)), split into the two
  // blend equations the GS actually has. C = 0 (As), NOT 2 (FIX): the sharpen
  // amount is per tile, so it has to ride in on the vertex alpha like every
  // other weight.
  if (sharpen > 0.0F) {
    q = emitPassState(q, lowVram, lowBufW, lowW, lowH, true,
                      GS_SET_ALPHA(0, 2, 0, 1, 0), true);  // Cd + Cs * As
    q = emitGrid(q, 3);
    q = emitPassState(q, lowVram, lowBufW, lowW, lowH, true,
                      GS_SET_ALPHA(2, 0, 0, 1, 0), true);  // Cd - Cs * As
    q = emitGrid(q, 4);
  }

  if (debugView == 1) {
    // (Cs - Cd) * 64 >> 7 + Cd - a flat 50% mix toward the tint.
    q = emitPassState(q, 0, 0, 0, 0, false, GS_SET_ALPHA(0, 1, 2, 1, 64),
                      false);
    q = emitGrid(q, 5);
  }

  // Restore the TEXTURE / BLEND state only these passes touch (the raster
  // registers come from emitRasterRestore below, which is also what puts the
  // window-centred XYOFFSET the VU1 3D pipeline expects back - with the
  // per-field bias, in one place).
  PACK_GIFTAG(q, GIF_SET_TAG(5, 0, 0, 0, GIF_FLG_PACKED, 1), GIF_REG_AD);
  q++;
  PACK_GIFTAG(q, GS_SET_CLAMP(1, 1, 0, 0, 0, 0), GS_REG_CLAMP_1);
  q++;
  PACK_GIFTAG(q, GS_SET_TEX1(1, 0, 1, 1, 0, 0, 0), GS_REG_TEX1_1);
  q++;
  PACK_GIFTAG(q, GS_SET_ALPHA(0, 1, 0, 1, 0), GS_REG_ALPHA_1);
  q++;
  // TEXA and COLCLAMP go back to the GS reset values, which is what the whole
  // engine has always run with (neither register is written anywhere else), so
  // nothing outside these passes changes behaviour. Making those defaults
  // engine-wide would be a separate, deliberate decision.
  PACK_GIFTAG(q, GS_SET_TEXA(0, 0, 0), GS_REG_TEXA);
  q++;
  PACK_GIFTAG(q, GS_SET_COLCLAMP(COLOR_CLAMP_MASK), GS_REG_COLCLAMP);
  q++;
  q = gs->emitRasterRestore(q, false);

  packet2_update(packet, q);
  packet2_update(packet, draw_finish(packet->next));
  dma_channel_wait(DMA_CHANNEL_GIF, 0);
  dma_channel_send_packet2(packet, DMA_CHANNEL_GIF, true);
  draw_wait_finish();

  prev = cur;
  hasPrev = true;
}

}  // namespace Tyra
