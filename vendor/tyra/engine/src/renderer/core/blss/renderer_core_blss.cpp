/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Added by TyraX: BLSS, the reduced-raster upscaler (docs/upscaler.md).
# Modified by TyraX: GIF-channel sends pass path3Fence() first.
*/

#include "renderer/core/paths/path1/frame_submission.hpp"
#include <dma.h>
#include "renderer/core/paths/path3/path3_fence.hpp"
#include <draw.h>
#include <gif_tags.h>
#include <graph.h>
#include <gs_gp.h>
#include <gs_psm.h>
#include "renderer/core/blss/renderer_core_blss.hpp"
#include "debug/debug.hpp"
#include "debug/frame_profile.hpp"

namespace Tyra {

// The TEXA the DRAWING ENVIRONMENT runs with - NOT the GS reset value.
// ps2sdk's draw_setup_environment leaves TA0 0x80, AEM 0, TA1 0x80 (found by
// disassembling libdraw.a). A 24-bit texture draws with TCC = 0 and takes its
// alpha from TA0, and the environment's ATEST NOTEQUAL/AREF 0 discards alpha
// 0 - so TA0 = 0 left behind deletes every 24-bit textured primitive.
static const u64 kEnvTexa = GS_SET_TEXA(0x80, 0, 0x80);
// The TEXA the copies and the composite read 16-bit targets with: the 1-bit
// alpha expands to exactly 0 or 0x80. A PSMZ16 copy needs it too, or bit 15
// of every depth reads back as 1.
static const u64 kMaskTexa = GS_SET_TEXA(0x00, 0, 0x80);

// Blits run as 32-pixel-wide sprite strips: a full-screen sprite walks the
// texture page cache across the whole source row, a strip stays inside it.
static constexpr int kStrip = 32;
// 512 / 32 strips per pass, 4 registers each, three passes per packet plus
// the state blocks - 512 qwords leaves room for every display mode.
static constexpr int kPacketQwords = 512;

// ceil(log2(v)) - TEX0 TW/TH must COVER the real extent; the region CLAMP cuts
// it back down.
static u8 lg2(int v) {
  u8 r = 0;
  while ((1 << r) < v) r++;
  return r;
}

RendererCoreBlss::RendererCoreBlss() {}

RendererCoreBlss::~RendererCoreBlss() {
  if (packet) packet2_free(packet);
}

void RendererCoreBlss::init(RendererSettings* t_settings, RendererCoreGS* t_gs,
                            RendererCoreSync* t_sync, Path1* t_path1,
                            RendererCore3D* t_core3D) {
  settings = t_settings;
  gs = t_gs;
  sync = t_sync;
  path1 = t_path1;
  core3D = t_core3D;

  // Re-place the targets after a display-mode switch: setDisplayOutput resets
  // the whole VRAM map. `allocated`, not `enabled`: a switch can land while a
  // native scene is active, and the next reduced scene must not rasterise into
  // memory the texture heap now owns.
  if (allocated) {
    updateGeometry();
    allocated = false;
    allocate();
    if (core3D != nullptr) core3D->setFov(core3D->getFov());
  }
}

void RendererCoreBlss::updateGeometry() {
  outW = static_cast<int>(settings->getWidth());
  // PHYSICAL buffer height - half the logical one in InterlacedField.
  outH = static_cast<int>(settings->getRenderHeightF());
  lowW = outW / scaleX;
  lowH = outH / scaleY;
  lowBufW = -64 & (lowW + 63);  // FRAME/TEX buffer widths are 64-aligned
}

void RendererCoreBlss::allocate() {
  if (allocated || gs == nullptr || scaleX * scaleY <= 1) return;
  fbPsm = settings->getFrameBufferPsm();
  zPsm = gs->zBuffer.zsm == GS_ZBUF_16 ? GS_PSMZ_16 : GS_PSMZ_32;
  lowVram = gs->vram.allocateBuffer(lowBufW, lowH, fbPsm);
  TYRA_ASSERT(lowVram >= 0, "Out of VRAM for the BLSS low-res target");
  lowZVram = -1;
  if (wantDepth) {
    lowZVram = gs->vram.allocateBuffer(lowBufW, lowH, zPsm);
    TYRA_ASSERT(lowZVram >= 0, "Out of VRAM for the BLSS low-res z target");
  }
  allocated = true;
  if (wantDepth)
    TYRA_LOG("BLSS: ", lowW, "x", lowH, " low-res target at VRAM ", lowVram,
             ", low-res z at ", lowZVram);
  else
    TYRA_LOG("BLSS: ", lowW, "x", lowH, " low-res target at VRAM ", lowVram);
}

void RendererCoreBlss::configure(int t_scaleX, int t_scaleY,
                                 bool t_nativeScenes, bool t_particles) {
  TYRA_ASSERT(settings != nullptr,
              "BLSS configure() before the renderer was initialized!");
  if (settings == nullptr) return;
  scaleX = t_scaleX < 1 ? 1 : t_scaleX;
  scaleY = t_scaleY < 1 ? 1 : t_scaleY;
  const bool any = (scaleX * scaleY) > 1;
  wantDepth = any && t_particles;
  // The first scene's shape: the whole scene unless this game draws its
  // particles reduced, in which case loadScene()'s setScene() decides.
  enabled = any && !t_particles;
  particleOn = false;

  // The particles-only shape keeps the opaque scene at full raster, so the
  // z buffer must cover the display for the whole run - exactly what a game
  // with native scenes pins.
  if (gs != nullptr && any && (t_nativeScenes || t_particles))
    gs->setZRasterScale(1, 1);

  settings->setRasterScale(enabled ? scaleX : 1, enabled ? scaleY : 1);
  if (gs != nullptr)
    gs->setLowResTargetScale(any ? scaleX : 1, any ? scaleY : 1, wantDepth);
  if (core3D != nullptr) core3D->setFov(core3D->getFov());

  // The z buffer is sized for the raster and the colour reserve changed, so
  // the permanent VRAM region is laid out again - safe HERE, at the top of
  // init(), before a single texture is loaded. zBuffer.mask is NOT set here:
  // it belongs to whoever sizes the buffer (RendererCoreGS::
  // allocateVramBuffers derives it), and assigning it from outside once
  // stamped display-resolution depth across the texture heap.
  if (gs != nullptr && gs->needsBufferRealloc()) {
    if (vramRebuild != nullptr) {
      vramRebuild(vramRebuildUser);
      allocated = false;  // vram.reset() forgot our targets
    } else {
      TYRA_WARN("BLSS: no VRAM rebuild hook - the z buffer keeps its "
                "display-resolution size.");
    }
  }

  if (!any) return;
  updateGeometry();
  allocate();
  if (packet == nullptr)
    packet = packet2_create(kPacketQwords, P2_TYPE_NORMAL, P2_MODE_NORMAL, 0);
  TYRA_LOG("BLSS configured: scale ", scaleX, "x", scaleY,
           t_particles ? ", particles drawn reduced" : ", whole scene reduced");
}

void RendererCoreBlss::setScene(bool t_upscale, bool t_particles) {
  if (settings == nullptr) return;
  const bool any = t_upscale && allocated && (scaleX * scaleY) > 1;
  const bool wasEnabled = enabled;
  const bool wasParticles = particleOn;
  particleOn = any && t_particles && wantDepth;
  enabled = any && !t_particles;

  if (enabled != wasEnabled) {
    settings->setRasterScale(enabled ? scaleX : 1, enabled ? scaleY : 1);
    // The raster scale moved; the world-space frustum planes did not.
    if (core3D != nullptr) core3D->setFov(core3D->getFov());
    // endScene() leaves z masked, which is right while z is smaller than the
    // display and wrong the moment a native scene follows a reduced one.
    if (gs != nullptr) gs->zBuffer.mask = gs->getZMaskDefault();
  }
#if TYRA_FRAME_PROFILE
  // A bracket that no longer runs must not keep reporting its last cost.
  FrameProfile::tBlssBegin = FrameProfile::tBlssEnd = 0;
  FrameProfile::tBlssComposite = FrameProfile::tBlssCompositeEe = 0;
#endif
  if (enabled != wasEnabled || particleOn != wasParticles)
    TYRA_LOG("BLSS scene: ",
             enabled ? "whole scene reduced"
                     : (particleOn ? "particles reduced" : "native"));
}

void RendererCoreBlss::applyRasterScale(bool reduced) {
  settings->setRasterScale(reduced ? scaleX : 1, reduced ? scaleY : 1);
  if (core3D != nullptr) core3D->refreshRasterProjection();
}

void RendererCoreBlss::sendPacket(packet2_t* p) {
  packet2_update(p, frameDrawFinish(p->next));
  frameWaitGif(DMA_CHANNEL_GIF, 0);
  path3Fence();  // Modified by TyraX: path3_fence.hpp
  frameSendPacket(p, DMA_CHANNEL_GIF, true);
  frameWaitFinish();
}

// One textured (or, with srcVram < 0, untextured) blit pass' state. NLOOP
// counts every register row - a miscount stalls the GIF forever, and the
// symptom is a hang in draw_wait_finish with a clean log.
qword_t* RendererCoreBlss::emitBlitState(qword_t* q, int dstVram, int dstBufW,
                                         int dstPsm, u32 dstMask, int srcVram,
                                         int srcBufW, int srcPsm, int texW,
                                         int texH, bool linear, bool srcAlpha,
                                         u64 alpha) {
  const bool textured = srcVram >= 0;
  const int zbp = static_cast<int>(gs->zBuffer.address) >> 11;
  const int zsm = static_cast<int>(gs->zBuffer.zsm);
  PACK_GIFTAG(q, GIF_SET_TAG(textured ? 8 : 4, 0, 0, 0, GIF_FLG_PACKED, 1),
              GIF_REG_AD);
  q++;
  if (textured) {
    // Every pass reads what an earlier draw wrote.
    PACK_GIFTAG(q, GS_SET_TEXFLUSH(0), GS_REG_TEXFLUSH);
    q++;
    // TFX = DECAL: the fragment IS the texel (RGB always, alpha when TCC = 1;
    // with TCC = 0 it is the vertex alpha).
    PACK_GIFTAG(q,
                GS_SET_TEX0(srcVram >> 6, srcBufW >> 6, srcPsm, lg2(texW),
                            lg2(texH), srcAlpha ? 1 : 0, 1 /* DECAL */, 0, 0,
                            0, 0, 0),
                GS_REG_TEX0_1);
    q++;
    const int f = linear ? 1 : 0;
    PACK_GIFTAG(q, GS_SET_TEX1(1, 0, f, f, 0, 0, 0), GS_REG_TEX1_1);
    q++;
    // Region clamp to the REAL extent: TW/TH round up to a power of two.
    PACK_GIFTAG(q, GS_SET_CLAMP(2, 2, 0, texW - 1, 0, texH - 1),
                GS_REG_CLAMP_1);
    q++;
  }
  PACK_GIFTAG(q, GS_SET_FRAME(dstVram >> 11, dstBufW >> 6, dstPsm, dstMask),
              GS_REG_FRAME_1);
  q++;
  PACK_GIFTAG(q, alpha, GS_REG_ALPHA_1);
  q++;
  // Alpha test off, z all-pass: these passes are copies, not geometry.
  PACK_GIFTAG(q, GS_SET_TEST(0, 0, 0, 0, 0, 0, 1, ZTEST_METHOD_ALLPASS),
              GS_REG_TEST_1);
  q++;
  // z writes masked: nothing here may touch scene depth.
  PACK_GIFTAG(q, GS_SET_ZBUF(zbp, zsm, 1), GS_REG_ZBUF_1);
  q++;
  return q;
}

// A dstW x dstH screen-origin rectangle as sprite strips, UVs (1/16 texel)
// running linearly from (u0, v0) at the top-left to (u1, v1) at the
// bottom-right.
qword_t* RendererCoreBlss::emitQuad(qword_t* q, int dstW, int dstH, int u0,
                                    int v0, int u1, int v1, bool blend,
                                    u8 alpha) {
  const bool textured = u1 > u0;
  const int strips = (dstW + kStrip - 1) / kStrip;
  // NLOOP = PRIM + RGBAQ + (UV, XYZ2, UV, XYZ2) per strip.
  PACK_GIFTAG(q, GIF_SET_TAG(2 + 4 * strips, 0, 0, 0, GIF_FLG_PACKED, 1),
              GIF_REG_AD);
  q++;
  PACK_GIFTAG(q,
              GS_SET_PRIM(6 /* sprite */, 0, textured ? 1 : 0, 0,
                          blend ? 1 : 0, 0, 1 /* FST */, 0, 0),
              GS_REG_PRIM);
  q++;
  PACK_GIFTAG(q, GS_SET_RGBAQ(0x80, 0x80, 0x80, alpha, 0x3F800000),
              GS_REG_RGBAQ);
  q++;
  for (int s = 0; s < strips; s++) {
    const int xa = s * kStrip;
    const int xb = xa + kStrip > dstW ? dstW : xa + kStrip;
    const int ua = u0 + (u1 - u0) * xa / dstW;
    const int ub = u0 + (u1 - u0) * xb / dstW;
    PACK_GIFTAG(q, GS_SET_UV(ua, v0), GS_REG_UV);
    q++;
    PACK_GIFTAG(q, GS_SET_XYZ((2048 + xa) << 4, 2048 << 4, 0), GS_REG_XYZ2);
    q++;
    PACK_GIFTAG(q, GS_SET_UV(ub, v1), GS_REG_UV);
    q++;
    PACK_GIFTAG(q, GS_SET_XYZ((2048 + xb) << 4, (2048 + dstH) << 4, 0),
                GS_REG_XYZ2);
    q++;
  }
  return q;
}

// Put back the texture / blend / dither state the passes touched (the raster
// registers come from emitRasterRestore, which also restores the window-
// centred XYOFFSET the VU1 pipeline expects, with the per-field bias).
qword_t* RendererCoreBlss::emitCompositeRestore(qword_t* q) {
  const texwrap_t& w = gs->currentTextureWrap();
  PACK_GIFTAG(q, GIF_SET_TAG(6, 0, 0, 0, GIF_FLG_PACKED, 1), GIF_REG_AD);
  q++;
  // The wrap the GS's cache says is programmed, so the next bag's
  // setBagWrap comparison stays honest (gs-state: REPEAT is a contract).
  PACK_GIFTAG(q,
              GS_SET_CLAMP(w.horizontal, w.vertical, w.minu, w.maxu, w.minv,
                           w.maxv),
              GS_REG_CLAMP_1);
  q++;
  PACK_GIFTAG(q, GS_SET_TEX1(1, 0, 1, 1, 0, 0, 0), GS_REG_TEX1_1);
  q++;
  PACK_GIFTAG(q, GS_SET_ALPHA(0, 1, 0, 1, 0), GS_REG_ALPHA_1);
  q++;
  PACK_GIFTAG(q, kEnvTexa, GS_REG_TEXA);
  q++;
  PACK_GIFTAG(q, GS_SET_FBA(0), GS_REG_FBA_1);
  q++;
  PACK_GIFTAG(q, GS_SET_DTHE(settings->isDitherActive() ? 1 : 0),
              GS_REG_DTHE);
  q++;
  return gs->emitRasterRestore(q, false);
}

// ------------------------------------------------------------ whole scene ---
void RendererCoreBlss::beginScene(const Color& clearColor) {
  if (!enabled) return;
#if TYRA_FRAME_PROFILE
  const u32 fpT0 = FrameProfile::ticks();
#endif
  if (!allocated) allocate();
  // The raster redirect is global GS state - drain in-flight PATH1 3D work
  // first. Before any 3D pipeline is up there is nothing to drain (and the
  // FINISH handshake would spin forever).
  if (path1->isVU1Configured()) sync->align3D();
  inScene = true;

  // Window-CENTRED, because that is what the VU1 pipeline's fixed 2048 raster
  // scale expects. No per-field bias: the low-res target is an offscreen
  // texture, and the interleave belongs to composite(), which writes the
  // buffer the CRT scans.
  const int offX16 = static_cast<int>((2048.0F - lowW / 2.0F) * 16.0F);
  const int offY16 = static_cast<int>((2048.0F - lowH / 2.0F) * 16.0F);
  const int zbp = static_cast<int>(gs->zBuffer.address) >> 11;
  const int zsm = static_cast<int>(gs->zBuffer.zsm);

  // PUBLISH the redirect first: every nested bracket's end() (env map,
  // camera feed, shadow map, portal masks) restores through it, and
  // draw_enable_tests reads zBuffer.mask, unmasked for exactly this bracket.
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

  packet2_reset(packet, false);
  qword_t* q = packet->base;
  // NLOOP = 9: XYOFFSET, FRAME, SCISSOR, ZBUF, TEST, RGBAQ, PRIM and the
  // clear sprite's two XYZ2.
  PACK_GIFTAG(q, GIF_SET_TAG(9, 0, 0, 0, GIF_FLG_PACKED, 1), GIF_REG_AD);
  q++;
  // First, so the clear sprite is addressed in this offset's space.
  PACK_GIFTAG(q, GS_SET_XYOFFSET(offX16, offY16), GS_REG_XYOFFSET_1);
  q++;
  PACK_GIFTAG(q, GS_SET_FRAME(lowVram >> 11, lowBufW >> 6, fbPsm, 0),
              GS_REG_FRAME_1);
  q++;
  PACK_GIFTAG(q, GS_SET_SCISSOR(0, lowW - 1, 0, lowH - 1), GS_REG_SCISSOR_1);
  q++;
  // The MAIN z buffer: its row stride comes from FRAME.FBW, so the low-res
  // pass overwrites a prefix of it - free, because nothing after the
  // composite reads scene depth (DoF, portals and split view are refused).
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
  // With the all-pass test and an unmasked ZBUF this also resets the depths.
  PACK_GIFTAG(q, GS_SET_XYZ(offX16 - 16, offY16 - 16, 0), GS_REG_XYZ2);
  q++;
  PACK_GIFTAG(q,
              GS_SET_XYZ(offX16 + (lowW + 1) * 16, offY16 + (lowH + 1) * 16, 0),
              GS_REG_XYZ2);
  q++;
  q = draw_enable_tests(q, 0, &gs->zBuffer);
  packet2_update(packet, q);
  sendPacket(packet);
#if TYRA_FRAME_PROFILE
  FrameProfile::tBlssBegin = FrameProfile::ticks() - fpT0;
#endif
}

void RendererCoreBlss::endScene() {
  if (!enabled || !inScene) return;
#if TYRA_FRAME_PROFILE
  // Almost entirely the align3D below: the GS overhang of the reduced scene.
  const u32 fpT0 = FrameProfile::ticks();
#endif
  inScene = false;
  if (path1->isVU1Configured()) sync->align3D();
  gs->endRasterRedirect();
  // The DERIVED default, never a literal (RendererCoreGS owns the answer
  // because it owns the allocation).
  gs->zBuffer.mask = gs->getZMaskDefault();

  packet2_reset(packet, false);
  // TEXFLUSH: composite() is about to sample the target just rendered.
  qword_t* q = gs->emitRasterRestore(packet->base, true);
  packet2_update(packet, q);
  sendPacket(packet);
#if TYRA_FRAME_PROFILE
  FrameProfile::tBlssEnd = FrameProfile::ticks() - fpT0;
#endif
}

void RendererCoreBlss::composite() {
  if (!enabled || gs == nullptr || packet == nullptr) return;
#if TYRA_FRAME_PROFILE
  const u32 fpT0 = FrameProfile::ticks();
#endif
  if (path1->isVU1Configured()) sync->align3D();

  const auto* fb = gs->getCurrentFrameBuffer();
  packet2_reset(packet, false);
  qword_t* q = packet->base;
  // Screen-origin offset, with the per-field bias: this pass writes the
  // buffer the CRT scans.
  PACK_GIFTAG(q, GIF_SET_TAG(1, 0, 0, 0, GIF_FLG_PACKED, 1), GIF_REG_AD);
  q++;
  PACK_GIFTAG(q,
              GS_SET_XYOFFSET(2048 * 16, 2048 * 16 + gs->getFieldYOffset16()),
              GS_REG_XYOFFSET_1);
  q++;
  q = emitBlitState(q, static_cast<int>(fb->address),
                    static_cast<int>(fb->width), fbPsm, 0, lowVram, lowBufW,
                    fbPsm, lowW, lowH, true, false,
                    GS_SET_ALPHA(0, 1, 0, 1, 0));
  // u(x) = x / scale: the bilinear tap at the output pixel centre.
  q = emitQuad(q, outW, outH, 0, 0, lowW << 4, lowH << 4, false, 0x80);
  q = emitCompositeRestore(q);
  packet2_update(packet, q);
#if TYRA_FRAME_PROFILE
  FrameProfile::tBlssCompositeEe = FrameProfile::ticks() - fpT0;
#endif
  sendPacket(packet);
#if TYRA_FRAME_PROFILE
  FrameProfile::tBlssComposite = FrameProfile::ticks() - fpT0;
#endif
}

// --------------------------------------------------------- particles only ---
void RendererCoreBlss::beginParticles() {
  if (!particleOn || inParticles || gs == nullptr || packet == nullptr ||
      lowZVram < 0)
    return;
#if TYRA_FRAME_PROFILE
  const u32 fpT0 = FrameProfile::ticks();
#endif
  // The copies read the finished opaque scene, so every queued 3D draw must
  // have reached the GS first.
  if (path1->isVU1Configured()) sync->align3D();
  inParticles = true;

  const auto* fb = gs->getCurrentFrameBuffer();
  const int fbVram = static_cast<int>(fb->address);
  const int fbBufW = static_cast<int>(fb->width);
  const int zVram = static_cast<int>(gs->zBuffer.address);

  packet2_reset(packet, false);
  qword_t* q = packet->base;
  // Offscreen targets: screen origin, NO field bias. DTHE 0 because one of
  // the targets is a z buffer seen as a frame - dithering it adds noise to
  // depth. FBA 0 so the colour copy writes alpha 0.
  PACK_GIFTAG(q, GIF_SET_TAG(4, 0, 0, 0, GIF_FLG_PACKED, 1), GIF_REG_AD);
  q++;
  PACK_GIFTAG(q, GS_SET_XYOFFSET(2048 * 16, 2048 * 16), GS_REG_XYOFFSET_1);
  q++;
  PACK_GIFTAG(q, GS_SET_DTHE(0), GS_REG_DTHE);
  q++;
  PACK_GIFTAG(q, GS_SET_FBA(0), GS_REG_FBA_1);
  q++;
  PACK_GIFTAG(q, kMaskTexa, GS_REG_TEXA);
  q++;

  // 1) DEPTH: the full z read as a PSMZ texture and written POINT-sampled into
  // the low-res z, seen as a frame of the same format. Same format both ways,
  // so every bit survives; one sample of each 2x2 is the depth the particles
  // test against.
  q = emitBlitState(q, lowZVram, lowBufW, zPsm, 0, zVram, fbBufW, zPsm, outW,
                    outH, false, true, GS_SET_ALPHA(0, 1, 0, 1, 0));
  q = emitQuad(q, lowW, lowH, 0, 0, outW << 4, outH << 4, false, 0x80);

  // 2) COLOUR: box-filtered (the bilinear tap lands between four texels),
  // vertex alpha 0 - "no particle here yet".
  q = emitBlitState(q, lowVram, lowBufW, fbPsm, 0, fbVram, fbBufW, fbPsm, outW,
                    outH, true, false, GS_SET_ALPHA(0, 1, 0, 1, 0));
  q = emitQuad(q, lowW, lowH, 0, 0, outW << 4, outH << 4, false, 0x00);

  // 3) The raster goes to the low-res target, z tests against the low-res
  // copy and writes nothing, and FBA marks every pixel a particle touches.
  const int offX16 = static_cast<int>((2048.0F - lowW / 2.0F) * 16.0F);
  const int offY16 = static_cast<int>((2048.0F - lowH / 2.0F) * 16.0F);
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

  const texwrap_t& w = gs->currentTextureWrap();
  PACK_GIFTAG(q, GIF_SET_TAG(10, 0, 0, 0, GIF_FLG_PACKED, 1), GIF_REG_AD);
  q++;
  PACK_GIFTAG(q, GS_SET_TEXFLUSH(0), GS_REG_TEXFLUSH);
  q++;
  PACK_GIFTAG(q, GS_SET_XYOFFSET(offX16, offY16), GS_REG_XYOFFSET_1);
  q++;
  PACK_GIFTAG(q, GS_SET_FRAME(lowVram >> 11, lowBufW >> 6, fbPsm, 0),
              GS_REG_FRAME_1);
  q++;
  PACK_GIFTAG(q, GS_SET_SCISSOR(0, lowW - 1, 0, lowH - 1), GS_REG_SCISSOR_1);
  q++;
  PACK_GIFTAG(q,
              GS_SET_ZBUF(lowZVram >> 11, static_cast<int>(gs->zBuffer.zsm),
                          1),
              GS_REG_ZBUF_1);
  q++;
  // The 3D contract the copies broke: the cached wrap, the environment's
  // TEXA, the project's dither, the environment's TEX1.
  PACK_GIFTAG(q,
              GS_SET_CLAMP(w.horizontal, w.vertical, w.minu, w.maxu, w.minv,
                           w.maxv),
              GS_REG_CLAMP_1);
  q++;
  PACK_GIFTAG(q, kEnvTexa, GS_REG_TEXA);
  q++;
  PACK_GIFTAG(q, GS_SET_DTHE(settings->isDitherActive() ? 1 : 0),
              GS_REG_DTHE);
  q++;
  PACK_GIFTAG(q, GS_SET_TEX1(1, 0, 1, 1, 0, 0, 0), GS_REG_TEX1_1);
  q++;
  PACK_GIFTAG(q, GS_SET_FBA(1), GS_REG_FBA_1);
  q++;
  q = draw_enable_tests(q, 0, &gs->zBuffer);
  packet2_update(packet, q);
  sendPacket(packet);

  applyRasterScale(true);
#if TYRA_FRAME_PROFILE
  FrameProfile::tBlssBegin = FrameProfile::ticks() - fpT0;
#endif
}

void RendererCoreBlss::endParticles() {
  if (!inParticles) return;
#if TYRA_FRAME_PROFILE
  const u32 fpT0 = FrameProfile::ticks();
#endif
  inParticles = false;
  if (path1->isVU1Configured()) sync->align3D();
  applyRasterScale(false);
  gs->endRasterRedirect();

  const auto* fb = gs->getCurrentFrameBuffer();
  packet2_reset(packet, false);
  // The display raster first - FRAME, SCISSOR, ZBUF, TEST. The scissor
  // matters most: left at the low-res one it clips the full-screen composite
  // below to the top-left quarter. TEXFLUSH: the composite samples the
  // target the particles just drew.
  qword_t* q = gs->emitRasterRestore(packet->base, true);
  PACK_GIFTAG(q, GIF_SET_TAG(4, 0, 0, 0, GIF_FLG_PACKED, 1), GIF_REG_AD);
  q++;
  PACK_GIFTAG(q, GS_SET_FBA(0), GS_REG_FBA_1);
  q++;
  PACK_GIFTAG(q, GS_SET_DTHE(0), GS_REG_DTHE);
  q++;
  PACK_GIFTAG(q, kMaskTexa, GS_REG_TEXA);
  q++;
  PACK_GIFTAG(q, GS_SET_XYOFFSET(2048 * 16, 2048 * 16), GS_REG_XYOFFSET_1);
  q++;

  // 4) At 32 bits FBA left alpha >= 0x80 wherever a particle drew, and the
  // composite would read 0xFF as a factor of two. Rewrite the ALPHA BYTE
  // ALONE (FBMSK keeps RGB) to exactly 0x80 where its top bit is set (DATE,
  // DATM = 1). At 16 bits kMaskTexa already reads the A bit as 0 / 0x80.
  if (fbPsm != GS_PSM_16) {
    q = emitBlitState(q, lowVram, lowBufW, fbPsm, 0x00FFFFFF, -1, 0, 0, 0, 0,
                      false, false, GS_SET_ALPHA(0, 1, 0, 1, 0));
    // emitBlitState wrote the copy test; this pass needs the alpha one.
    PACK_GIFTAG(q, GIF_SET_TAG(1, 0, 0, 0, GIF_FLG_PACKED, 1), GIF_REG_AD);
    q++;
    PACK_GIFTAG(q, GS_SET_TEST(0, 0, 0, 0, 1, 1, 1, ZTEST_METHOD_ALLPASS),
                GS_REG_TEST_1);
    q++;
    q = emitQuad(q, lowW, lowH, 0, 0, 0, 0, false, 0x80);
  }

  // 5) Composite: Cs*As + Cd*(1-As) over the display buffer, bilinear, so
  // untouched pixels keep their full-resolution value and the layer's edge
  // fades over one low-res texel. FBMSK keeps the display buffer's alpha,
  // which the alpha-mask passes own.
  PACK_GIFTAG(q, GIF_SET_TAG(1, 0, 0, 0, GIF_FLG_PACKED, 1), GIF_REG_AD);
  q++;
  PACK_GIFTAG(q,
              GS_SET_XYOFFSET(2048 * 16, 2048 * 16 + gs->getFieldYOffset16()),
              GS_REG_XYOFFSET_1);
  q++;
  q = emitBlitState(q, static_cast<int>(fb->address),
                    static_cast<int>(fb->width), fbPsm, 0xFF000000, lowVram,
                    lowBufW, fbPsm, lowW, lowH, true, true,
                    GS_SET_ALPHA(0, 1, 0, 1, 0));
  q = emitQuad(q, outW, outH, 0, 0, lowW << 4, lowH << 4, true, 0x80);
  q = emitCompositeRestore(q);
  packet2_update(packet, q);
  sendPacket(packet);
#if TYRA_FRAME_PROFILE
  FrameProfile::tBlssComposite = FrameProfile::ticks() - fpT0;
#endif
}

}  // namespace Tyra
