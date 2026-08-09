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

#include <dma.h>
#include <draw.h>
#include <gs_gp.h>
#include <gs_psm.h>
#include <math.h>
#include <packet2_utils.h>
#include "debug/debug.hpp"
#include "renderer/core/warp/renderer_core_warp.hpp"

namespace Tyra {

// ceil(log2(v)) - the GS TEX0 TW/TH fields are log2 of a power-of-two extent,
// so a 512x448 buffer is addressed as 512x512 and the region clamp below keeps
// the sampling inside the real rows. (Same helper the BLSS composite needs.)
static int lg2(int v) {
  int r = 0;
  while ((1 << r) < v) r++;
  return r;
}

static inline float dot3(const Vec4& a, const Vec4& b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

RendererCoreWarp::RendererCoreWarp() {}

RendererCoreWarp::~RendererCoreWarp() {
  if (packet) packet2_free(packet);
}

void RendererCoreWarp::init(RendererSettings* t_settings, RendererCoreGS* t_gs,
                            RendererCoreSync* t_sync, Path1* t_path1,
                            RendererCoreBlss* t_blss) {
  settings = t_settings;
  gs = t_gs;
  sync = t_sync;
  path1 = t_path1;
  blss = t_blss;
  if (packet == nullptr)
    packet = packet2_create(kPacketQwords, P2_TYPE_NORMAL, P2_MODE_NORMAL, 0);
  updateGeometry();
}

void RendererCoreWarp::updateGeometry() {
  // The PHYSICAL raster - half height in InterlacedField - because that is
  // what the buffers are and what the grid has to cover.
  outW = static_cast<int>(settings->getWidth());
  outH = static_cast<int>(settings->getRenderHeightF());
  cols = (outW + kTile - 1) / kTile;
  rows = (outH + kTile - 1) / kTile;
  cornerCols = cols + 1;
  cornerRows = rows + 1;
  if (cornerCols * cornerRows > kMaxCorners) {
    TYRA_SOFT_ERROR("Frame warp: raster ", outW, "x", outH,
                    " needs more grid corners than the fixed table holds - "
                    "the warp is off for this display mode.");
    cols = rows = cornerCols = cornerRows = 0;
  }
}

// The reprojection, per grid corner. This is the neural upscaler's buildReproj()
// run FORWARDS, and when BLSS is on it reads the SAME per-tile 1/w:
// BLSS asks "where was this pixel last frame" to fetch history, the warp asks
// "where does last frame's pixel belong now" to synthesise a whole picture.
//
// The property worth protecting: for a pure ROTATION (from.position ==
// to.position) the translation term vanishes whatever the depth is, so `rel` is
// the ray itself and the mapping is exact at every scene depth. Depth only ever
// matters for TRANSLATION - and getting it per tile instead of per frame is the
// difference between parallax and a lens zoom.
void RendererCoreWarp::buildUVs(const WarpCamera& from, const WarpCamera& to) {
  const float invW = 2.0F / static_cast<float>(outW);
  const float invH = 2.0F / static_cast<float>(outH);
  const int uvMax = 0x3FFF;

  // How far the eye moved. Translation enters the reprojection ONLY through
  // this, scaled per corner by that corner's 1/w - so a corner at infinity
  // (1/w = 0) sees pure rotation and one close to the camera sees the full
  // parallax. That is the whole difference between a warp that reprojects and
  // one that zooms.
  const float dx = to.position.x - from.position.x;
  const float dy = to.position.y - from.position.y;
  const float dz = to.position.z - from.position.z;

  // The depth source, in order: the neural upscaler's per-tile mean 1/w (the
  // same numbers its own reprojection uses), else the single-plane fallback,
  // else nothing at all - which is rotation only, and correct rather than
  // merely safe.
  const bool tileDepth = blss != nullptr && blss->hasTileDepth() &&
                         blss->getTileCols() == cols &&
                         blss->getTileRows() == rows;
  const float planeInvW = planeDistance > 0.0F ? 1.0F / planeDistance : 0.0F;

  for (int j = 0; j < cornerRows; j++) {
    const int py = j * kTile > outH ? outH : j * kTile;
    const float sY = (1.0F - static_cast<float>(py) * invH) * to.tanHalfFovY;
    for (int i = 0; i < cornerCols; i++) {
      const int px = i * kTile > outW ? outW : i * kTile;
      const float sX = (static_cast<float>(px) * invW - 1.0F) * to.tanHalfFovX;
      const int c = j * cornerCols + i;

      // The view ray of this corner under the NEW camera.
      const Vec4 dir(to.forward.x + to.right.x * sX + to.up.x * sY,
                     to.forward.y + to.right.y * sX + to.up.y * sY,
                     to.forward.z + to.right.z * sX + to.up.z * sY, 0.0F);

      // This corner's 1/w, averaged over the covered tiles that touch it -
      // indexed by the corner's position in the NEW frame while the depths
      // describe the OLD one. That is the usual reprojection approximation and
      // it holds while the camera delta is small, which one field's worth of
      // motion is; the alternative is an iterative search per corner.
      // the same "representative depth" rule buildReproj() uses, so the two
      // reprojections describe the world the same way. An uncovered tile
      // contributes 0 by construction (no coverage, no depth), which reads as
      // "infinitely far" and is exactly right for sky.
      float cInvW = planeInvW;
      if (tileDepth) {
        float sum = 0.0F;
        int n = 0;
        for (int ty = j - 1; ty <= j; ty++) {
          if (ty < 0 || ty >= rows) continue;
          for (int tx = i - 1; tx <= i; tx++) {
            if (tx < 0 || tx >= cols) continue;
            const float d = blss->getTileInvW(tx, ty);
            if (d > 0.0F) { sum += d; n++; }
          }
        }
        cInvW = n > 0 ? sum / static_cast<float>(n) : 0.0F;
      }

      // rel is the world offset from the OLD eye, divided by this corner's
      // depth. Dividing rather than multiplying is what lets 1/w = 0 mean
      // infinity with no special case: the projection below is a ratio, so
      // scaling rel by any positive constant leaves the UVs alone.
      const Vec4 rel(dir.x + dx * cInvW, dir.y + dy * cInvW,
                     dir.z + dz * cInvW, 0.0F);

      const float wPrev = dot3(rel, from.forward);
      float u, v;
      if (wPrev < 1e-3F) {
        // Behind the old eye: nothing in the source image can stand for it.
        // Fall back to the identity sample so the cell shows the un-warped
        // pixel rather than a wild one from the far edge.
        u = static_cast<float>(px);
        v = static_cast<float>(py);
      } else {
        const float sXp =
            dot3(rel, from.right) / (wPrev * from.tanHalfFovX);
        const float sYp = dot3(rel, from.up) / (wPrev * from.tanHalfFovY);
        u = (sXp * 0.5F + 0.5F) * static_cast<float>(outW);
        v = (0.5F - sYp * 0.5F) * static_cast<float>(outH);
      }

      // 12.4 fixed point, and the UV register's fields are 14 bits UNSIGNED -
      // a negative value wraps to ~1023 texels and makes the cell sample the
      // opposite edge. Clamping here IS the edge behaviour: the outermost
      // cells stretch their last row of texels over the disoccluded strip,
      // which is what a guard band would otherwise fill.
      int ui = static_cast<int>(floorf(u * 16.0F + 0.5F));
      int vi = static_cast<int>(floorf(v * 16.0F + 0.5F));
      if (ui < 0) ui = 0;
      if (vi < 0) vi = 0;
      if (ui > uvMax) ui = uvMax;
      if (vi > uvMax) vi = uvMax;
      uv[c][0] = static_cast<s16>(ui);
      uv[c][1] = static_cast<s16>(vi);
    }
  }
}

qword_t* RendererCoreWarp::emitState(qword_t* q, int srcVram, int srcBufW) {
  const auto* fb = gs->getCurrentFrameBuffer();
  const int fbVram = static_cast<int>(fb->address);
  const int fbBufW = static_cast<int>(fb->width);
  const int zbp = static_cast<int>(gs->zBuffer.address) >> 11;
  const int zsm = static_cast<int>(gs->zBuffer.zsm);

  // NLOOP 7: TEXFLUSH, TEX0, TEX1, CLAMP, FRAME, TEST, ZBUF. Count these - a
  // miscounted NLOOP stalls the GIF forever, in either direction.
  //
  // What is deliberately NOT here is as important as what is. This pass is an
  // OPAQUE DECAL copy (PRIM.ABE = 0), so it has nothing to blend and nothing to
  // clamp: writing TEXA or COLCLAMP would mean having to put them back, and
  // "put them back" means ASSUMING what the engine runs with. Nothing else in
  // the engine writes either register, so their value is the GS reset value -
  // an assumption that was wrong enough to hue-shift a whole scene with bloom
  // in it (docs/frame-extrapolation.md). Touch the fewest global registers
  // that the copy actually needs.
  PACK_GIFTAG(q, GIF_SET_TAG(7, 0, 0, 0, GIF_FLG_PACKED, 1), GIF_REG_AD);
  q++;
  // The source is the frame the GS finished presenting a moment ago, so the
  // texture cache must be invalidated before sampling it.
  PACK_GIFTAG(q, GS_SET_TEXFLUSH(0), GS_REG_TEXFLUSH);
  q++;
  // TFX = DECAL: the texel IS the output, the vertex colour is ignored. This
  // is a copy, not a blend - there is nothing underneath to blend with.
  PACK_GIFTAG(q,
              GS_SET_TEX0(srcVram >> 6, srcBufW >> 6, GS_PSM_32, lg2(outW),
                          lg2(outH), 0 /* TCC: RGB */, 1 /* TFX: DECAL */, 0, 0,
                          0, 0, 0),
              GS_REG_TEX0_1);
  q++;
  // Bilinear: the warp lands on fractional texels by construction, and point
  // sampling turns a sub-pixel camera drift into visible stair-stepping.
  PACK_GIFTAG(q, GS_SET_TEX1(1, 0, 1, 1, 0, 0, 0), GS_REG_TEX1_1);
  q++;
  // Region clamp to the REAL extent - TW/TH above rounded up to a power of
  // two, and 448 rows of a 512-addressed buffer must not sample the 64 rows
  // that belong to whatever was allocated next.
  PACK_GIFTAG(q, GS_SET_CLAMP(2, 2, 0, outW - 1, 0, outH - 1), GS_REG_CLAMP_1);
  q++;
  PACK_GIFTAG(q, GS_SET_FRAME(fbVram >> 11, fbBufW >> 6, GS_PSM_32, 0),
              GS_REG_FRAME_1);
  q++;
  // Alpha test off, z test all-pass: this is a full-screen copy that must not
  // be filtered by anything the previous frame left in the z buffer.
  PACK_GIFTAG(q, GS_SET_TEST(0, 0, 0, 0, 0, 0, 1, ZTEST_METHOD_ALLPASS),
              GS_REG_TEST_1);
  q++;
  // z writes masked - a full-screen pass stamping z would lock those pixels
  // against everything the game draws on top of the warped frame.
  PACK_GIFTAG(q, GS_SET_ZBUF(zbp, zsm, 1), GS_REG_ZBUF_1);
  q++;
  return q;
}

qword_t* RendererCoreWarp::emitGrid(qword_t* q) {
  // TRIANGLE_STRIP, IIP = 1, TME = 1, ABE = 0 (opaque copy), FST = 1 (the UVs
  // are 12.4 texels, no perspective divide - the warp field is already the
  // perspective).
  const u64 primVal = GS_SET_PRIM(4, 1, 1, 0, 0, 0, 1 /* FST */, 0, 0);

  for (int cy = 0; cy < rows; cy++) {
    const int nVerts = 2 * (cols + 1);
    // NLOOP = 1 (PRIM) + 3 registers per vertex (RGBAQ, UV, XYZ2).
    PACK_GIFTAG(q, GIF_SET_TAG(1 + 3 * nVerts, 0, 0, 0, GIF_FLG_PACKED, 1),
                GIF_REG_AD);
    q++;
    // Writing PRIM resets the GS vertex queue - that is what starts each row's
    // strip rather than continuing the previous row's.
    PACK_GIFTAG(q, primVal, GS_REG_PRIM);
    q++;

    // (i, j) (i, j+1) (i+1, j) (i+1, j+1) ... - each quad's diagonal runs
    // (i, j+1) -> (i+1, j), the same split BLSS' grid uses, so the two halves
    // of a cell interpolate the warp field the way the corners describe it.
    for (int i = 0; i <= cols; i++) {
      for (int d = 0; d < 2; d++) {
        const int j = cy + d;
        const int c = j * cornerCols + i;
        const int px = i * kTile > outW ? outW : i * kTile;
        const int py = j * kTile > outH ? outH : j * kTile;
        // DECAL ignores this, but the vertex format must stay 3 registers to
        // match the NLOOP above.
        PACK_GIFTAG(q, GS_SET_RGBAQ(0x80, 0x80, 0x80, 0x80, 0x3F800000),
                    GS_REG_RGBAQ);
        q++;
        PACK_GIFTAG(q, GS_SET_UV(uv[c][0], uv[c][1]), GS_REG_UV);
        q++;
        // Screen-origin XYOFFSET is set by draw(), so the window coordinate is
        // the vertex minus 2048.
        PACK_GIFTAG(q, GS_SET_XYZ((2048 + px) << 4, (2048 + py) << 4, 0),
                    GS_REG_XYZ2);
        q++;
      }
    }
  }
  return q;
}

void RendererCoreWarp::draw(const WarpCamera& from, const WarpCamera& to) {
  if (gs == nullptr || packet == nullptr) return;
  updateGeometry();  // a display-mode switch moves the raster under us
  if (cols == 0 || rows == 0) return;

  // Never race the tail of the 3D render that produced the source image.
  if (path1 != nullptr && path1->isVU1Configured()) sync->align3D();

  const auto* src = gs->getPreviousFrameBuffer();
  const int srcVram = static_cast<int>(src->address);
  const int srcBufW = static_cast<int>(src->width);

  buildUVs(from, to);



  packet2_reset(packet, false);
  qword_t* q = packet->base;

  // Screen-origin raster offset for the grid, like the post-fx and BLSS
  // passes; the window-centred one the VU1 pipeline expects goes back through
  // emitRasterRestore at the end, with the per-field bias folded in.
  PACK_GIFTAG(q, GIF_SET_TAG(1, 0, 0, 0, GIF_FLG_PACKED, 1), GIF_REG_AD);
  q++;
  PACK_GIFTAG(q,
              GS_SET_XYOFFSET(2048 * 16, 2048 * 16 + gs->getFieldYOffset16()),
              GS_REG_XYOFFSET_1);
  q++;

  q = emitState(q, srcVram, srcBufW);
  q = emitGrid(q);

  // Put back the one texture register this pass moves.
  // NLOOP 1: CLAMP. The region clamp is the only texture register this pass
  // moves away from what the engine uses; TEX1 was written to the engine-wide
  // bilinear value, and TEX0 is re-emitted per textured mesh by the pipeline
  // (packet2_utils_gs_add_texbuff_clut), CLUT fields included. COUNT what
  // follows this tag - an overcount makes the GS read the next giftag as a
  // register write and the GIF stream desyncs into a hang with no assert.
  PACK_GIFTAG(q, GIF_SET_TAG(1, 0, 0, 0, GIF_FLG_PACKED, 1), GIF_REG_AD);
  q++;
  PACK_GIFTAG(q, GS_SET_CLAMP(1, 1, 0, 0, 0, 0), GS_REG_CLAMP_1);
  q++;
  // FRAME / SCISSOR / XYOFFSET / the drawing environment's tests, from the ONE
  // shared restore - so this bracket nests inside a BLSS redirect the way the
  // env map, camera feed and shadow map do.
  q = gs->emitRasterRestore(q, true);

  packet2_update(packet, q);
  dma_channel_wait(DMA_CHANNEL_GIF, 0);
  dma_channel_send_packet2(packet, DMA_CHANNEL_GIF, true);
  dma_channel_wait(DMA_CHANNEL_GIF, 0);
}

}  // namespace Tyra
