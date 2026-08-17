/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Added by the TyraX fork.
*/

#include <dma.h>
#include <draw.h>
#include <gif_tags.h>
#include <gs_gp.h>
#include <gs_psm.h>
#include <kernel.h>
#include <malloc.h>
#include <stdlib.h>
#include "renderer/core/postfx/renderer_core_postfx.hpp"
#include "debug/debug.hpp"
#include "renderer/core/gs/renderer_core_depth.hpp"

namespace Tyra {

static u8 lg2(int v) {
  u8 r = 0;
  while ((1 << r) < v) r++;
  return r;
}

RendererCorePostFx::RendererCorePostFx() {
  settings = nullptr;
  gs = nullptr;
  bloom = 0;
  bloomThreshold = 0;
  bloomSpread = 1;
  grain = 0;
  dof = 0;
  dofFocus = 0.0F;
  dofRange = 0.01F;
  rays = 0;
  raysSunX = raysSunY = 0.0F;
  raysVis = 0.0F;
  clearGrading();
  rng = 0xC0FFEE01u;
  curFbVram = 0;
  curFbBufW = 0;
  fbPsm = 0;  // GS_PSM_32 until init() reads the real colour depth
  // Sized for every pass at once: DoF (8 blits) + bloom (downsample + the
  // optional bright-pass quad + up to 4 soften rounds of 4 blits + the
  // add-back = 18 primitives at full spread) + god rays (downsample + a
  // bright-pass quad + 2 zoom rounds of 2 blits + the composite = 7 blits
  // and a quad) + grading (6 quads) + grain (2) + setup/teardown. A blit is
  // 12 qwords, a quad 7 - the worst case is ~500, so 768 keeps real
  // headroom. An UNDERSIZED packet here corrupts the GIF stream, so grow
  // this whenever a pass gains primitives (the god-rays pass pushed the old
  // 512 to the edge when it met main's spread-capable bloom).
  packet = packet2_create(768, P2_TYPE_NORMAL, P2_MODE_NORMAL, 0);
}

RendererCorePostFx::~RendererCorePostFx() {
  if (packet) packet2_free(packet);
}

void RendererCorePostFx::init(RendererSettings* t_settings,
                              RendererCoreGS* t_gs) {
  settings = t_settings;
  gs = t_gs;

  fbW = static_cast<int>(settings->getWidth());
  // Physical buffer height - half the logical one when field rendering
  // (InterlacedField); every pass blits 1:1 over the real framebuffer.
  fbH = static_cast<int>(settings->getRenderHeightF());
  // 1/8 res: an even softer glow, and VRAM stays cheap (~48KB total).
  // Texture VRAM is a scarce, thrash-prone resource - RendererCoreTexture
  // evicts everything whenever a texture no longer fits.
  lowW = fbW / 8;
  lowH = fbH / 8;
  lowBufW = -64 & (lowW + 63);  // FRAME/TEX buffer widths are 64-aligned

  // Modified by TyraX: the work buffers take the framebuffer's format, so
  // the downsample/soften chain never converts between depths (and at
  // PSMCT16 they cost half as much too). The noise texture stays PSMCT32:
  // it is a plain uploaded texture, read but never rendered into.
  fbPsm = settings->getFrameBufferPsm();
  lowVram[0] = gs->vram.allocateBuffer(lowBufW, lowH, fbPsm);
  lowVram[1] = gs->vram.allocateBuffer(lowBufW, lowH, fbPsm);
  noiseVram = gs->vram.allocateBuffer(noiseSize, noiseSize, GS_PSM_32);
  TYRA_ASSERT(lowVram[0] >= 0 && lowVram[1] >= 0 && noiseVram >= 0,
              "Out of VRAM for post fx buffers");

  uploadNoise();

  TYRA_LOG("PostFx initialized!");
}

void RendererCorePostFx::uploadNoise() {
  const int count = noiseSize * noiseSize;
  u32* pixels = static_cast<u32*>(memalign(128, count * 4));
  u32 s = 0x1234ABCDu;
  for (int i = 0; i < count; i++) {
    s = s * 1664525u + 1013904223u;
    u32 r = (s >> 24) & 0xFF;
    r = (r * r) >> 8;  // skew dark, with occasional bright specks (filmic)
    pixels[i] = r | (r << 8) | (r << 16) | (0x80u << 24);
  }
  FlushCache(0);

  packet2_t* transfer = packet2_create(64, P2_TYPE_NORMAL, P2_MODE_CHAIN, 0);
  packet2_update(transfer,
                 draw_texture_transfer(transfer->base, pixels, noiseSize,
                                       noiseSize, GS_PSM_32, noiseVram,
                                       noiseSize));
  packet2_update(transfer, draw_texture_flush(transfer->next));
  dma_channel_wait(DMA_CHANNEL_GIF, 0);
  dma_channel_send_packet2(transfer, DMA_CHANNEL_GIF, true);
  dma_channel_wait(DMA_CHANNEL_GIF, 0);
  packet2_free(transfer);
  free(pixels);
}

qword_t* RendererCorePostFx::blit(qword_t* q, int srcVram, int srcBufW,
                                  int texW, int texH, int u0, int v0, int u1,
                                  int v1, int dstVram, int dstBufW, int x0,
                                  int y0, int x1, int y1, bool linear,
                                  bool wrap, int abe, u64 alpha, u32 z) {
  PACK_GIFTAG(q, GIF_SET_TAG(11, 0, 0, 0, GIF_FLG_PACKED, 1), GIF_REG_AD);
  q++;
  // The previous blit's output is this blit's input - invalidate the cache.
  PACK_GIFTAG(q, GS_SET_TEXFLUSH(0), GS_REG_TEXFLUSH);
  q++;
  PACK_GIFTAG(q,
              GS_SET_TEX0(srcVram >> 6, srcBufW >> 6, psmFor(srcVram),
                          lg2(texW), lg2(texH), 0, 1 /* decal */, 0, 0, 0, 0,
                          0),
              GS_REG_TEX0_1);
  q++;
  const int f = linear ? 1 : 0;
  PACK_GIFTAG(q, GS_SET_TEX1(1, 0, f, f, 0, 0, 0), GS_REG_TEX1_1);
  q++;
  // Repeat for the tiled grain; region clamp elsewhere so the offset taps
  // never sample past the real texel extent (TW/TH round up to pow2).
  PACK_GIFTAG(q,
              wrap ? GS_SET_CLAMP(0, 0, 0, 0, 0, 0)
                   : GS_SET_CLAMP(2, 2, 0, texW - 1, 0, texH - 1),
              GS_REG_CLAMP_1);
  q++;
  PACK_GIFTAG(q, GS_SET_FRAME(dstVram >> 11, dstBufW >> 6, psmFor(dstVram), 0),
              GS_REG_FRAME_1);
  q++;
  PACK_GIFTAG(q, alpha, GS_REG_ALPHA_1);
  q++;
  PACK_GIFTAG(q, GS_SET_PRIM(6 /* sprite */, 0, 1, 0, abe, 0, 1 /* uv */, 0, 0),
              GS_REG_PRIM);
  q++;
  PACK_GIFTAG(q, GS_SET_UV(u0, v0), GS_REG_UV);
  q++;
  PACK_GIFTAG(q, GS_SET_XYZ((2048 + x0) << 4, (2048 + y0) << 4, z),
              GS_REG_XYZ2);
  q++;
  PACK_GIFTAG(q, GS_SET_UV(u1, v1), GS_REG_UV);
  q++;
  PACK_GIFTAG(q, GS_SET_XYZ((2048 + x1) << 4, (2048 + y1) << 4, z),
              GS_REG_XYZ2);
  q++;
  return q;
}

qword_t* RendererCorePostFx::flatQuad(qword_t* q, int dstVram, int dstBufW,
                                      u32 fbmsk, u8 r, u8 g, u8 b, u8 a,
                                      u64 alpha, int w, int h) {
  if (w < 0) w = fbW;
  if (h < 0) h = fbH;
  PACK_GIFTAG(q, GIF_SET_TAG(6, 0, 0, 0, GIF_FLG_PACKED, 1), GIF_REG_AD);
  q++;
  PACK_GIFTAG(q,
              GS_SET_FRAME(dstVram >> 11, dstBufW >> 6, psmFor(dstVram), fbmsk),
              GS_REG_FRAME_1);
  q++;
  PACK_GIFTAG(q, alpha, GS_REG_ALPHA_1);
  q++;
  PACK_GIFTAG(q, GS_SET_RGBAQ(r, g, b, a, 0x3F800000), GS_REG_RGBAQ);
  q++;
  PACK_GIFTAG(q, GS_SET_PRIM(6 /* sprite */, 0, 0, 0, 1 /* abe */, 0, 0, 0, 0),
              GS_REG_PRIM);
  q++;
  PACK_GIFTAG(q, GS_SET_XYZ(2048 << 4, 2048 << 4, 0xFFFFFFFFu), GS_REG_XYZ2);
  q++;
  PACK_GIFTAG(q, GS_SET_XYZ((2048 + w) << 4, (2048 + h) << 4, 0xFFFFFFFFu),
              GS_REG_XYZ2);
  q++;
  return q;
}

qword_t* RendererCorePostFx::sizedQuad(qword_t* q, int dstVram, int dstBufW,
                                       int w, int h, u8 r, u8 g, u8 b, u8 a,
                                       u64 alpha) {
  PACK_GIFTAG(q, GIF_SET_TAG(6, 0, 0, 0, GIF_FLG_PACKED, 1), GIF_REG_AD);
  q++;
  PACK_GIFTAG(q, GS_SET_FRAME(dstVram >> 11, dstBufW >> 6, psmFor(dstVram), 0),
              GS_REG_FRAME_1);
  q++;
  PACK_GIFTAG(q, alpha, GS_REG_ALPHA_1);
  q++;
  PACK_GIFTAG(q, GS_SET_RGBAQ(r, g, b, a, 0x3F800000), GS_REG_RGBAQ);
  q++;
  PACK_GIFTAG(q, GS_SET_PRIM(6 /* sprite */, 0, 0, 0, 1 /* abe */, 0, 0, 0, 0),
              GS_REG_PRIM);
  q++;
  PACK_GIFTAG(q, GS_SET_XYZ(2048 << 4, 2048 << 4, 0xFFFFFFFFu), GS_REG_XYZ2);
  q++;
  PACK_GIFTAG(q, GS_SET_XYZ((2048 + w) << 4, (2048 + h) << 4, 0xFFFFFFFFu),
              GS_REG_XYZ2);
  q++;
  return q;
}

void RendererCorePostFx::portalMaskBegin(int x0, int y0, int x1, int y1) {
  if (gs == nullptr) return;

  // Modified by TyraX: this bracket takes FRAME and puts the raster window
  // back through RendererCoreGS::getRasterTarget() / emitRasterRestore(), the
  // way the env map, the camera feed and the shadow map already do. It used to
  // read gs->getCurrentFrameBuffer() and restore a DISPLAY-sized SCISSOR and
  // XYOFFSET - the fourth copy of the same bug, and the only one the pass that
  // fixed the other three missed. It runs inside the generated renderScene(),
  // so a bracket wrapping that (BLSS) was cancelled by the first portal and
  // silently lost for the rest of the frame. One implementation of the restore
  // also means this one finally carries the InterlacedField per-field XYOFFSET
  // bias, which it never did (RendererCoreGS::getFieldYOffset16).
  //
  // Portals stay incompatible with BLSS for an independent reason - a portal
  // through-view wants real display-resolution depth, and codegen refuses the
  // combination outright now - so this does not make the pair work. It removes
  // the last copy of a restore that does the wrong thing.
  const RendererCoreGS::RasterTarget rt = gs->getRasterTarget();
  const int rasterW = rt.scissorX1 - rt.scissorX0 + 1;
  const int rasterH = rt.scissorY1 - rt.scissorY0 + 1;
  if (x0 < rt.scissorX0) x0 = rt.scissorX0;
  if (y0 < rt.scissorY0) y0 = rt.scissorY0;
  if (x1 > rt.scissorX1 + 1) x1 = rt.scissorX1 + 1;
  if (y1 > rt.scissorY1 + 1) y1 = rt.scissorY1 + 1;
  if (x1 <= x0 || y1 <= y0) return;

  const int fbVram = rt.frameAddress;
  const int fbBufW = rt.frameWidth;

  packet2_reset(packet, false);
  // Screen-origin offset for the z-clear sprite; the window-centered offset
  // the destination view's VU1 render expects is restored below.
  packet2_update(packet,
                 draw_primitive_xyoffset(packet->base, 0, 2048.0F, 2048.0F));
  qword_t* q = packet->next;
  // NLOOP = 7: SCISSOR, FRAME, TEST, RGBAQ, PRIM + two XYZ2 - a miscount
  // here stalls the GIF forever (the stray qword parses as a new giftag).
  PACK_GIFTAG(q, GIF_SET_TAG(7, 0, 0, 0, GIF_FLG_PACKED, 1), GIF_REG_AD);
  q++;
  // Bound the destination view's fill to the portal's screen bbox. The
  // scissor is expressed in window coordinates (unaffected by XYOFFSET).
  PACK_GIFTAG(q, GS_SET_SCISSOR(x0, x1 - 1, y0, y1 - 1), GS_REG_SCISSOR_1);
  q++;
  // z-only clear of the bbox: with several portal views per frame, an
  // earlier portal's z-cap must not reject this portal's destination
  // geometry where their bboxes overlap (first portal: no-op, z is still
  // at the frame clear's far).
  PACK_GIFTAG(q,
              GS_SET_FRAME(fbVram >> 11, fbBufW >> 6, fbPsm, 0xFFFFFFFFu),
              GS_REG_FRAME_1);
  q++;
  PACK_GIFTAG(q, GS_SET_TEST(0, 0, 0, 0, 0, 0, 1, ZTEST_METHOD_ALLPASS),
              GS_REG_TEST_1);
  q++;
  PACK_GIFTAG(q, GS_SET_RGBAQ(0x80, 0x80, 0x80, 0x80, 0x3F800000),
              GS_REG_RGBAQ);
  q++;
  PACK_GIFTAG(q, GS_SET_PRIM(6 /* sprite */, 0, 0, 0, 0, 0, 0, 0, 0),
              GS_REG_PRIM);
  q++;
  PACK_GIFTAG(q, GS_SET_XYZ(2048 << 4, 2048 << 4, 0), GS_REG_XYZ2);
  q++;
  PACK_GIFTAG(q, GS_SET_XYZ((2048 + rasterW) << 4, (2048 + rasterH) << 4, 0),
              GS_REG_XYZ2);
  q++;
  // Put the raster back for the destination render: FRAME with color writes on
  // again, the window-centered XYOFFSET the VU1 pipeline expects, ZBUF and the
  // drawing environment's tests (their own giftags - not in the NLOOP above).
  // No TEXFLUSH: this bracket wrote depth into the buffer the scene renders
  // into, nothing the scene is about to sample as a texture.
  q = gs->emitRasterRestore(q, false);
  // ...and then narrow the scissor again. emitRasterRestore widened it back to
  // the whole raster window, but the destination view is the one thing here
  // that must stay bounded to the portal's bbox - portalMaskEnd is what finally
  // lets it go.
  PACK_GIFTAG(q, GIF_SET_TAG(1, 0, 0, 0, GIF_FLG_PACKED, 1), GIF_REG_AD);
  q++;
  PACK_GIFTAG(q, GS_SET_SCISSOR(x0, x1 - 1, y0, y1 - 1), GS_REG_SCISSOR_1);
  q++;
  packet2_update(packet, q);
  packet2_update(packet, draw_finish(packet->next));
  dma_channel_wait(DMA_CHANNEL_GIF, 0);
  dma_channel_send_packet2(packet, DMA_CHANNEL_GIF, true);
  draw_wait_finish();
}

void RendererCorePostFx::portalMaskEnd(const float* xy, const u32* z,
                                       int count, u8 clearR, u8 clearG,
                                       u8 clearB) {
  if (gs == nullptr || count < 3) return;
  if (count > 12) count = 12;  // a frustum-clipped quad tops out at 9 verts

  // Modified by TyraX: the raster target, not the display buffer - see
  // portalMaskBegin for why this bracket had to stop restoring the display
  // buffer unconditionally.
  const RendererCoreGS::RasterTarget rt = gs->getRasterTarget();
  const int rasterW = rt.scissorX1 - rt.scissorX0 + 1;
  const int rasterH = rt.scissorY1 - rt.scissorY0 + 1;
  const int fbVram = rt.frameAddress;
  const int fbBufW = rt.frameWidth;

  packet2_reset(packet, false);
  // Screen-origin raster offset for the mask sprites/fan; restored to the
  // window-centered offset the VU1 pipeline expects at the end. The scissor
  // is still the bbox from portalMaskBegin - every op below is bounded.
  packet2_update(packet,
                 draw_primitive_xyoffset(packet->base, 0, 2048.0F, 2048.0F));

  qword_t* q = packet->next;
  // NLOOP = 13 fixed regs + one XYZ2 per fan vertex - an undercount stalls
  // the GIF forever (the stray qword parses as a garbage giftag).
  PACK_GIFTAG(q, GIF_SET_TAG(13 + count, 0, 0, 0, GIF_FLG_PACKED, 1),
              GIF_REG_AD);
  q++;
  // --- 1) z-only: re-far the whole bbox (kill the destination depths) ---
  PACK_GIFTAG(q,
              GS_SET_FRAME(fbVram >> 11, fbBufW >> 6, fbPsm, 0xFFFFFFFFu),
              GS_REG_FRAME_1);
  q++;
  PACK_GIFTAG(q, GS_SET_TEST(0, 0, 0, 0, 0, 0, 1, ZTEST_METHOD_ALLPASS),
              GS_REG_TEST_1);
  q++;
  PACK_GIFTAG(q, GS_SET_RGBAQ(0x80, 0x80, 0x80, 0x80, 0x3F800000),
              GS_REG_RGBAQ);
  q++;
  PACK_GIFTAG(q, GS_SET_PRIM(6 /* sprite */, 0, 0, 0, 0, 0, 0, 0, 0),
              GS_REG_PRIM);
  q++;
  PACK_GIFTAG(q, GS_SET_XYZ(2048 << 4, 2048 << 4, 0), GS_REG_XYZ2);
  q++;
  PACK_GIFTAG(q, GS_SET_XYZ((2048 + rasterW) << 4, (2048 + rasterH) << 4, 0),
              GS_REG_XYZ2);
  q++;
  // --- 2) z-only: cap the quad interior at the surface depth ------------
  PACK_GIFTAG(q, GS_SET_PRIM(5 /* triangle fan */, 0, 0, 0, 0, 0, 0, 0, 0),
              GS_REG_PRIM);
  q++;
  for (int i = 0; i < count; i++) {
    // 12.4 fixed point; the caller clips to the frustum so raster coords
    // stay far from the 4096 wrap, but clamp defensively anyway.
    int xf = static_cast<int>((xy[i * 2] + 2048.0F) * 16.0F + 0.5F);
    int yf = static_cast<int>((xy[i * 2 + 1] + 2048.0F) * 16.0F + 0.5F);
    xf = xf < 0 ? 0 : (xf > 65535 ? 65535 : xf);
    yf = yf < 0 ? 0 : (yf > 65535 ? 65535 : yf);
    PACK_GIFTAG(q, GS_SET_XYZ(xf, yf, z[i]), GS_REG_XYZ2);
    q++;
  }
  // --- 3) color: repaint the still-far ring with the clear color --------
  // GEQUAL at z=0 passes exactly where step 1 left z at far and step 2 did
  // NOT re-cap - i.e. the destination pixels that spilled outside the quad
  // opening (writing z=0 over z=0 is a no-op, so no ZBUF toggle needed).
  PACK_GIFTAG(q, GS_SET_FRAME(fbVram >> 11, fbBufW >> 6, fbPsm, 0),
              GS_REG_FRAME_1);
  q++;
  PACK_GIFTAG(q,
              GS_SET_TEST(0, 0, 0, 0, 0, 0, 1,
                          static_cast<int>(gs->zBuffer.method)),
              GS_REG_TEST_1);
  q++;
  PACK_GIFTAG(q, GS_SET_RGBAQ(clearR, clearG, clearB, 0x80, 0x3F800000),
              GS_REG_RGBAQ);
  q++;
  PACK_GIFTAG(q, GS_SET_PRIM(6 /* sprite */, 0, 0, 0, 0, 0, 0, 0, 0),
              GS_REG_PRIM);
  q++;
  PACK_GIFTAG(q, GS_SET_XYZ(2048 << 4, 2048 << 4, 0), GS_REG_XYZ2);
  q++;
  PACK_GIFTAG(q, GS_SET_XYZ((2048 + rasterW) << 4, (2048 + rasterH) << 4, 0),
              GS_REG_XYZ2);
  q++;
  // Restore whatever was redirected before this bracket - FRAME, the full
  // raster scissor (the bbox bound from portalMaskBegin ends here), the
  // window-centered XYOFFSET the VU1 pipeline expects, ZBUF and the drawing
  // environment's tests. Their own giftags, not in the NLOOP above.
  q = gs->emitRasterRestore(q, false);

  packet2_update(packet, q);
  packet2_update(packet, draw_finish(packet->next));
  dma_channel_wait(DMA_CHANNEL_GIF, 0);
  dma_channel_send_packet2(packet, DMA_CHANNEL_GIF, true);
  draw_wait_finish();
}

qword_t* RendererCorePostFx::gradingQuads(qword_t* q, int fbVram,
                                          int fbBufW) {
  // The alpha byte carries no scene data but the blits' TEXFLUSH/decal path
  // reads it back, so every grading sprite masks it out of the write.
  constexpr u32 kKeepAlpha = 0xFF000000u;

  // Per-channel gain: (Cd - 0) * FIX >> 7 + 0, each channel through its own
  // FBMSK-masked sprite. Equal gains collapse into a single sprite.
  if (gGain[0] == gGain[1] && gGain[1] == gGain[2]) {
    if (gGain[0] != 128)
      q = flatQuad(q, fbVram, fbBufW, kKeepAlpha, 0x80, 0x80, 0x80, 0x80,
                   GS_SET_ALPHA(1, 2, 2, 2, gGain[0]));
  } else {
    for (int c = 0; c < 3; c++) {
      if (gGain[c] == 128) continue;
      const u32 fbmsk = 0xFFFFFFFFu ^ (0xFFu << (8 * c));
      q = flatQuad(q, fbVram, fbBufW, fbmsk, 0x80, 0x80, 0x80, 0x80,
                   GS_SET_ALPHA(1, 2, 2, 2, gGain[c]));
    }
  }

  // Per-channel lift: positive components added (Cd + Cs), negative ones
  // subtracted (Cd - Cs) - unsigned GS math needs the two passes.
  u8 pos[3], neg[3];
  bool anyPos = false, anyNeg = false;
  for (int c = 0; c < 3; c++) {
    const int l = gLift[c];
    pos[c] = (u8)(l > 0 ? (l > 255 ? 255 : l) : 0);
    neg[c] = (u8)(l < 0 ? (l < -255 ? 255 : -l) : 0);
    anyPos |= pos[c] != 0;
    anyNeg |= neg[c] != 0;
  }
  if (anyPos)
    q = flatQuad(q, fbVram, fbBufW, kKeepAlpha, pos[0], pos[1], pos[2], 0x80,
                 GS_SET_ALPHA(0, 2, 2, 1, 128));
  if (anyNeg)
    q = flatQuad(q, fbVram, fbBufW, kKeepAlpha, neg[0], neg[1], neg[2], 0x80,
                 GS_SET_ALPHA(2, 0, 2, 1, 128));

  // Mix toward a constant color: the plain alpha blend
  // (Cs - Cd) * As >> 7 + Cd with As = the mix amount.
  if (gMixAmt > 0)
    q = flatQuad(q, fbVram, fbBufW, kKeepAlpha, gMix[0], gMix[1], gMix[2],
                 gMixAmt, GS_SET_ALPHA(0, 1, 0, 1, 0));
  return q;
}

void RendererCorePostFx::apply(int passes) {
  if (!isEnabled(passes) || gs == nullptr) return;

  auto* fb = gs->getCurrentFrameBuffer();
  const int fbVram = static_cast<int>(fb->address);
  const int fbBufW = static_cast<int>(fb->width);

  packet2_reset(packet, false);
  // Same raster window convention as RendererCore2D sprites.
  packet2_update(packet,
                 draw_primitive_xyoffset(packet->base, 0, 2048.0F, 2048.0F));

  qword_t* q = packet->next;

  // Mask z writes for the whole pass. Tyra never clears the z buffer (the
  // scene re-passes with GEQUAL every frame), so a stray z=max written by
  // these sprites would lock those pixels against all future drawing.
  //
  // Also pin RGBAQ and disable the alpha test for the pass. The blits below
  // send only UV+XYZ, so their vertex alpha is whatever RGBAQ the scene left
  // behind - and the drawing environment's alpha test rejects alpha==0
  // fragments. Fading particles end on exactly alpha 0, and on frames where
  // such a vertex was the last thing drawn, BOTH grain blits were discarded
  // whole ("film grain off for a few frames" whenever emitter fade cycles
  // aligned). Found via a same-packet untextured marker sprite that survived
  // the dropout frames while the grain didn't.
  const int zbp = static_cast<int>(gs->zBuffer.address) >> 11;
  const int zsm = static_cast<int>(gs->zBuffer.zsm);
  PACK_GIFTAG(q, GIF_SET_TAG(3, 0, 0, 0, GIF_FLG_PACKED, 1), GIF_REG_AD);
  q++;
  PACK_GIFTAG(q, GS_SET_ZBUF(zbp, zsm, 1), GS_REG_ZBUF_1);
  q++;
  PACK_GIFTAG(q, GS_SET_RGBAQ(0x80, 0x80, 0x80, 0x80, 0x3F800000),
              GS_REG_RGBAQ);
  q++;
  PACK_GIFTAG(q,
              GS_SET_TEST(0, 0, 0, 0, 0, 0, 1,
                          static_cast<int>(gs->zBuffer.method)),
              GS_REG_TEST_1);
  q++;

  // Depth of field first: it crossfades the frame toward its own blur, so
  // bloom / grading / grain later composite over the already-defocused image.
  if ((passes & PassDof) && dof > 0 && dofFocus > 0.0F) {
    const int w4 = lowW << 4, h4 = lowH << 4;
    // Blur chain identical to bloom's: 1/8-res bilinear downsample, then
    // soften low0 into low1 with offset taps.
    q = blit(q, fbVram, fbBufW, fbW, fbH, 0, 0, fbW << 4, fbH << 4, lowVram[0],
             lowBufW, 0, 0, lowW, lowH, true, false, 0, 0);
    q = blit(q, lowVram[0], lowBufW, lowW, lowH, 0, 0, w4, h4, lowVram[1],
             lowBufW, 0, 0, lowW, lowH, true, false, 0, 0);
    q = blit(q, lowVram[0], lowBufW, lowW, lowH, 16, 16, w4 + 16, h4 + 16,
             lowVram[1], lowBufW, 0, 0, lowW, lowH, true, false, 1,
             GS_SET_ALPHA(0, 1, 2, 1, 64));
    q = blit(q, lowVram[0], lowBufW, lowW, lowH, 16, 0, w4 + 16, h4,
             lowVram[1], lowBufW, 0, 0, lowW, lowH, true, false, 1,
             GS_SET_ALPHA(0, 1, 2, 1, 43));
    q = blit(q, lowVram[0], lowBufW, lowW, lowH, 0, 16, w4, h4 + 16,
             lowVram[1], lowBufW, 0, 0, lowW, lowH, true, false, 1,
             GS_SET_ALPHA(0, 1, 2, 1, 32));

    // World distance -> GS depth: the VU1 vertex path writes
    // z = (z_ndc + 1) * 0xFFFFFF/2 with z_ndc from the shared perspective
    // matrix (m4x4.cpp), which solves to
    // z(d) = 0xFFFFFF * near * (far - d) / (d * (far - near))
    // (near plane = 0xFFFFFF, far plane = 0). A sprite drawn at z(d) passes
    // the pass's GEQUAL z-test exactly where the scene is d or farther.
    const float zn = settings->getNear();
    const float zf = settings->getFar();
    // The range is RendererCoreDepth's, not a literal: a 16-bit-colour
    // project runs a PSMZ16 z and these sprites must land on the same scale
    // the vertex path used, or the whole DoF composite sits at wrong depths.
    const float zMax = static_cast<float>(RendererCoreDepth::maxZ);
    auto zAt = [&](float d) -> u32 {
      if (d <= zn) return RendererCoreDepth::maxZ;
      if (d >= zf) return 0u;
      const float z = zMax * zn * (zf - d) / (d * (zf - zn));
      return z <= 0.0F ? 0u : (z >= zMax ? RendererCoreDepth::maxZ : (u32)z);
    };

    // Three z-tested layers step the blur in between dofFocus and
    // dofFocus + dofRange. Blends accumulate multiplicatively, so each layer
    // blends the share of the *remaining* sharp image that lands the
    // cumulative blur on dof * i/3.
    int cum = 0;
    for (int i = 1; i <= 3; i++) {
      const int target = (int)dof * i / 3;
      const int fix = cum < 128 ? (128 * (target - cum)) / (128 - cum) : 0;
      cum = target;
      if (fix <= 0) continue;
      const u32 layerZ = zAt(dofFocus + dofRange * (float)(i - 1) / 3.0F);
      q = blit(q, lowVram[1], lowBufW, lowW, lowH, 0, 0, w4, h4, fbVram,
               fbBufW, 0, 0, fbW, fbH, true, false, 1,
               GS_SET_ALPHA(0, 1, 2, 1, fix), layerZ);
    }
  }

  // God rays before bloom: the shafts are scene light, so bloom can glow on
  // top of them and grading corrects the combined image.
  if ((passes & PassGodRays) && rays > 0 && raysVis > 0.01F) {
    const int w4 = lowW << 4, h4 = lowH << 4;
    // Downsample, then bright-pass: subtract a flat threshold and add the
    // result onto a copy of itself (x2) - max(0, x - t) * 2 keeps the hot
    // sky around the sun and highlights, cuts midtone geometry. 150 looks
    // right in practice: a plain daytime sky (~200) passes at ~40%, white
    // passes fully, lit terrain stays out (96 washed the whole frame).
    q = blit(q, fbVram, fbBufW, fbW, fbH, 0, 0, fbW << 4, fbH << 4, lowVram[0],
             lowBufW, 0, 0, lowW, lowH, true, false, 0, 0);
    q = sizedQuad(q, lowVram[0], lowBufW, lowW, lowH, 150, 150, 150, 0x80,
                  GS_SET_ALPHA(2, 0, 2, 1, 128));  // Cd - Cs
    q = blit(q, lowVram[0], lowBufW, lowW, lowH, 0, 0, w4, h4, lowVram[1],
             lowBufW, 0, 0, lowW, lowH, true, false, 0, 0);
    q = blit(q, lowVram[0], lowBufW, lowW, lowH, 0, 0, w4, h4, lowVram[1],
             lowBufW, 0, 0, lowW, lowH, true, false, 1,
             GS_SET_ALPHA(0, 2, 2, 1, 128));  // low1 = 2 * low0

    // Sun position in low-res texels, clamped so a far-off-screen sun still
    // gives a sane zoom center. The game passes LOGICAL screen pixels -
    // divide by the logical height (getHeight), not the physical buffer
    // height: under field rendering (InterlacedField) the buffer is half
    // the logical height and fbH tracks the buffer.
    float sunLx = raysSunX * ((float)lowW / (float)fbW);
    float sunLy = raysSunY * ((float)lowH / settings->getHeight());
    if (sunLx < -2.0F * lowW) sunLx = -2.0F * lowW;
    if (sunLx > 3.0F * lowW) sunLx = 3.0F * lowW;
    if (sunLy < -2.0F * lowH) sunLy = -2.0F * lowH;
    if (sunLy > 3.0F * lowH) sunLy = 3.0F * lowH;

    // Two zoom-toward-the-sun iterations, ping-ponging the work buffers:
    // dst = zoom(src) + src/2. Each zoom samples a window shrunk by s around
    // the sun, which stretches bright pixels radially away from it; the
    // compounding zoom extends the streaks exponentially.
    int cur = 1, other = 0;
    const float s = 0.72F;
    for (int it = 0; it < 2; it++) {
      const int u0 = (int)(sunLx * (1.0F - s) * 16.0F);
      const int v0 = (int)(sunLy * (1.0F - s) * 16.0F);
      const int u1 = u0 + (int)(lowW * s * 16.0F);
      const int v1 = v0 + (int)(lowH * s * 16.0F);
      q = blit(q, lowVram[cur], lowBufW, lowW, lowH, u0, v0, u1, v1,
               lowVram[other], lowBufW, 0, 0, lowW, lowH, true, false, 0, 0);
      q = blit(q, lowVram[cur], lowBufW, lowW, lowH, 0, 0, w4, h4,
               lowVram[other], lowBufW, 0, 0, lowW, lowH, true, false, 1,
               GS_SET_ALPHA(0, 2, 2, 1, 64));
      const int t = cur;
      cur = other;
      other = t;
    }

    // Composite the streak buffer back over the frame additively, scaled by
    // strength x the sun's visibility factor. Halved: the streak buffer is
    // pre-gained x2 by the bright-pass, and full-strength washed the frame.
    int fix = (int)((float)rays * raysVis * 0.5F);
    if (fix > 128) fix = 128;
    if (fix > 0)
      q = blit(q, lowVram[cur], lowBufW, lowW, lowH, 0, 0, w4, h4, fbVram,
               fbBufW, 0, 0, fbW, fbH, true, false, 1,
               GS_SET_ALPHA(0, 2, 2, 1, fix));
  }

  if ((passes & PassBloom) && bloom > 0) {
    const int w4 = lowW << 4, h4 = lowH << 4;
    // Downsample the frame to quarter res (bilinear averages 2x2).
    q = blit(q, fbVram, fbBufW, fbW, fbH, 0, 0, fbW << 4, fbH << 4, lowVram[0],
             lowBufW, 0, 0, lowW, lowH, true, false, 0, 0);
    // Bright pass: subtract a flat grey from the downsampled frame,
    // Cv = (0 - Cs) * 128 >> 7 + Cd = Cd - threshold, which the GS clamps at
    // zero. Everything below the threshold becomes black and stops
    // contributing to the blur, so the glow collapses onto the bright pixels -
    // emissive materials, sky, specular hits - instead of veiling the whole
    // image. One extra low-res sprite; the blur chain below is unchanged.
    if (bloomThreshold > 0)
      q = flatQuad(q, lowVram[0], lowBufW, 0xFF000000u, bloomThreshold,
                   bloomThreshold, bloomThreshold, 0x80,
                   GS_SET_ALPHA(2, 0, 2, 1, 128), lowW, lowH);
    // Soften: 4 taps of the source blended into the destination at half/one
    // texel offsets. Iterating with DOUBLED offsets each round grows the halo
    // geometrically (1 texel, then 2, then 4...) for 4 sprites a round, which
    // is what turns a tight fringe into a corona; the buffers ping-pong so
    // every round reads the previous one's result. Round 1 alone is the
    // original blur, so spread 1 is bit-identical to the old behavior.
    int src = 0, dst = 1;
    for (int it = 0; it < bloomSpread; it++) {
      const int o = 16 << it;  // 1/16 texel units: 1 texel, 2, 4, 8
      q = blit(q, lowVram[src], lowBufW, lowW, lowH, 0, 0, w4, h4, lowVram[dst],
               lowBufW, 0, 0, lowW, lowH, true, false, 0, 0);
      q = blit(q, lowVram[src], lowBufW, lowW, lowH, o, o, w4 + o, h4 + o,
               lowVram[dst], lowBufW, 0, 0, lowW, lowH, true, false, 1,
               GS_SET_ALPHA(0, 1, 2, 1, 64));
      q = blit(q, lowVram[src], lowBufW, lowW, lowH, o, 0, w4 + o, h4,
               lowVram[dst], lowBufW, 0, 0, lowW, lowH, true, false, 1,
               GS_SET_ALPHA(0, 1, 2, 1, 43));
      q = blit(q, lowVram[src], lowBufW, lowW, lowH, 0, o, w4, h4 + o,
               lowVram[dst], lowBufW, 0, 0, lowW, lowH, true, false, 1,
               GS_SET_ALPHA(0, 1, 2, 1, 32));
      src ^= 1, dst ^= 1;
    }
    // Add the blur back over the frame: Cd + Cs * bloom / 128. bloom may
    // exceed 128 (the editor allows up to 2x) for a hotter re-add.
    q = blit(q, lowVram[src], lowBufW, lowW, lowH, 0, 0, w4, h4, fbVram, fbBufW,
             0, 0, fbW, fbH, true, false, 1, GS_SET_ALPHA(0, 2, 2, 1, bloom));
  }

  // Grading between bloom (glows come from the ungraded scene) and grain
  // (film grain sits on top of the graded image).
  if ((passes & PassGrading) && hasGrading()) q = gradingQuads(q, fbVram, fbBufW);

  if ((passes & PassGrain) && grain > 0) {
    u8 g = grain >> 1;
    if (g == 0) g = 1;
    // Two noise passes with independent offsets: subtract one, add the
    // other. Unsigned math, but the expectation over both passes is zero.
    rng = rng * 1664525u + 1013904223u;
    int ou = (rng >> 8) & (noiseSize - 1);
    int ov = (rng >> 16) & (noiseSize - 1);
    q = blit(q, noiseVram, noiseSize, noiseSize, noiseSize, ou << 4, ov << 4,
             (ou + fbW) << 4, (ov + fbH) << 4, fbVram, fbBufW, 0, 0, fbW, fbH,
             false, true, 1, GS_SET_ALPHA(2, 0, 2, 1, g));
    rng = rng * 1664525u + 1013904223u;
    ou = (rng >> 8) & (noiseSize - 1);
    ov = (rng >> 16) & (noiseSize - 1);
    q = blit(q, noiseVram, noiseSize, noiseSize, noiseSize, ou << 4, ov << 4,
             (ou + fbW) << 4, (ov + fbH) << 4, fbVram, fbBufW, 0, 0, fbW, fbH,
             false, true, 1, GS_SET_ALPHA(0, 2, 2, 1, g));
  }

  // Restore the drawing state the rest of the frame machinery expects.
  PACK_GIFTAG(q, GIF_SET_TAG(5, 0, 0, 0, GIF_FLG_PACKED, 1), GIF_REG_AD);
  q++;
  PACK_GIFTAG(q, GS_SET_FRAME(fbVram >> 11, fbBufW >> 6, fbPsm, 0),
              GS_REG_FRAME_1);
  q++;
  PACK_GIFTAG(q, GS_SET_CLAMP(1, 1, 0, 0, 0, 0), GS_REG_CLAMP_1);
  q++;
  // Modified by TyraX (BLSS): the mask is gs->zBuffer.mask, NEVER a literal.
  // ps2sdk's draw_enable_tests() below writes TEST_1 and NOTHING ELSE
  // (disassembled: one A+D qword, GS_REG_TEST_1), so THIS qword is the last
  // word on ZBUF for the whole rest of the frame - and for the next one, up
  // to the next bracket. A hardcoded 0 means "z writes enabled at DISPLAY
  // resolution", which was right for every project until the upscaler shrank
  // the z buffer to the low-res raster: the GS strides z by FRAME.FBW, so the
  // very next full-screen draw (the following frame's clearScreen sprite,
  // which draw_disable_tests leaves at ZTE=1/ZTST=ALWAYS) stamped 512x448
  // words from ZBP through the post-fx buffers, the env map, the camera feed,
  // the low-res target and into the TEXTURE HEAP. Symptom: whichever textures
  // landed in that window drew nothing at all - a zeroed 4-bit CLUT has
  // alpha 0 and ATEST NOTEQUAL/AREF 0 discards every fragment - so
  // `examples/showcase` (post fx on, one texture: the ground) lost its whole
  // TERRAIN while everything untextured kept drawing. See the same lesson in
  // RendererCoreGS::allocateVramBuffers: an allocation is not an addressable
  // extent, and the mask belongs to whoever made the allocation.
  PACK_GIFTAG(q, GS_SET_ZBUF(zbp, zsm, gs->zBuffer.mask), GS_REG_ZBUF_1);
  q++;
  PACK_GIFTAG(q, GS_SET_TEX1(1, 0, 1, 1, 0, 0, 0), GS_REG_TEX1_1);
  q++;
  PACK_GIFTAG(q, GS_SET_ALPHA(0, 1, 0, 1, 0), GS_REG_ALPHA_1);
  q++;
  // Re-enable the alpha test exactly as the drawing environment configures it.
  q = draw_enable_tests(q, 0, &gs->zBuffer);

  packet2_update(packet, q);
  // The VU1 3D pipeline expects the window-centered raster offset - put it
  // back, exactly like RendererCore2D does after its sprites.
  packet2_update(packet,
                 draw_primitive_xyoffset(packet->next, 0,
                                         2048.0F - (fbW / 2.0F),
                                         2048.0F - (fbH / 2.0F)));
  packet2_update(packet, draw_finish(packet->next));
  dma_channel_wait(DMA_CHANNEL_GIF, 0);
  dma_channel_send_packet2(packet, DMA_CHANNEL_GIF, true);
  draw_wait_finish();
}

void RendererCorePostFx::applyCustom(CustomFxBuild build, void* user) {
  if (build == nullptr || gs == nullptr) return;

  auto* fb = gs->getCurrentFrameBuffer();
  curFbVram = static_cast<int>(fb->address);
  curFbBufW = static_cast<int>(fb->width);

  packet2_reset(packet, false);
  packet2_update(packet,
                 draw_primitive_xyoffset(packet->base, 0, 2048.0F, 2048.0F));

  qword_t* q = packet->next;

  // Identical frame-state setup to apply(): mask z writes for the whole pass,
  // pin RGBAQ and disable the alpha test so the author's blits (which send
  // only UV+XYZ) are never rejected. See apply() for the full rationale.
  const int zbp = static_cast<int>(gs->zBuffer.address) >> 11;
  const int zsm = static_cast<int>(gs->zBuffer.zsm);
  PACK_GIFTAG(q, GIF_SET_TAG(3, 0, 0, 0, GIF_FLG_PACKED, 1), GIF_REG_AD);
  q++;
  PACK_GIFTAG(q, GS_SET_ZBUF(zbp, zsm, 1), GS_REG_ZBUF_1);
  q++;
  PACK_GIFTAG(q, GS_SET_RGBAQ(0x80, 0x80, 0x80, 0x80, 0x3F800000),
              GS_REG_RGBAQ);
  q++;
  PACK_GIFTAG(q,
              GS_SET_TEST(0, 0, 0, 0, 0, 0, 1,
                          static_cast<int>(gs->zBuffer.method)),
              GS_REG_TEST_1);
  q++;

  // The user-authored effect appends its GS primitives here. It draws through
  // blit()/flatQuad() (or raw PACK_GIFTAG) and advances the cursor. The 768-
  // qword packet leaves ~745 qwords after this setup: roughly 60 blits or 105
  // flat quads - plenty for a screen effect, but not unbounded.
  q = build(*this, q, user);

  // Restore exactly what the rest of the frame machinery expects (mirrors
  // apply()).
  PACK_GIFTAG(q, GIF_SET_TAG(5, 0, 0, 0, GIF_FLG_PACKED, 1), GIF_REG_AD);
  q++;
  PACK_GIFTAG(q, GS_SET_FRAME(curFbVram >> 11, curFbBufW >> 6, fbPsm, 0),
              GS_REG_FRAME_1);
  q++;
  PACK_GIFTAG(q, GS_SET_CLAMP(1, 1, 0, 0, 0, 0), GS_REG_CLAMP_1);
  q++;
  // gs->zBuffer.mask, never a literal - see apply()'s copy of this block.
  PACK_GIFTAG(q, GS_SET_ZBUF(zbp, zsm, gs->zBuffer.mask), GS_REG_ZBUF_1);
  q++;
  PACK_GIFTAG(q, GS_SET_TEX1(1, 0, 1, 1, 0, 0, 0), GS_REG_TEX1_1);
  q++;
  PACK_GIFTAG(q, GS_SET_ALPHA(0, 1, 0, 1, 0), GS_REG_ALPHA_1);
  q++;
  q = draw_enable_tests(q, 0, &gs->zBuffer);

  packet2_update(packet, q);
  packet2_update(packet,
                 draw_primitive_xyoffset(packet->next, 0,
                                         2048.0F - (fbW / 2.0F),
                                         2048.0F - (fbH / 2.0F)));
  packet2_update(packet, draw_finish(packet->next));
  dma_channel_wait(DMA_CHANNEL_GIF, 0);
  dma_channel_send_packet2(packet, DMA_CHANNEL_GIF, true);
  draw_wait_finish();
}

}  // namespace Tyra
