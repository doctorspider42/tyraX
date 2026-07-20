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
  grain = 0;
  dof = 0;
  dofFocus = 0.0F;
  dofRange = 0.01F;
  clearGrading();
  rng = 0xC0FFEE01u;
  curFbVram = 0;
  curFbBufW = 0;
  // Sized for every pass at once: DoF (8 blits) + bloom (6) + grading (6
  // quads) + grain (2) + setup/teardown still leave headroom.
  packet = packet2_create(352, P2_TYPE_NORMAL, P2_MODE_NORMAL, 0);
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

  lowVram[0] = gs->vram.allocateBuffer(lowBufW, lowH, GS_PSM_32);
  lowVram[1] = gs->vram.allocateBuffer(lowBufW, lowH, GS_PSM_32);
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
              GS_SET_TEX0(srcVram >> 6, srcBufW >> 6, GS_PSM_32, lg2(texW),
                          lg2(texH), 0, 1 /* decal */, 0, 0, 0, 0, 0),
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
  PACK_GIFTAG(q, GS_SET_FRAME(dstVram >> 11, dstBufW >> 6, GS_PSM_32, 0),
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
                                      u64 alpha) {
  PACK_GIFTAG(q, GIF_SET_TAG(6, 0, 0, 0, GIF_FLG_PACKED, 1), GIF_REG_AD);
  q++;
  PACK_GIFTAG(q, GS_SET_FRAME(dstVram >> 11, dstBufW >> 6, GS_PSM_32, fbmsk),
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
  PACK_GIFTAG(q,
              GS_SET_XYZ((2048 + fbW) << 4, (2048 + fbH) << 4, 0xFFFFFFFFu),
              GS_REG_XYZ2);
  q++;
  return q;
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
    auto zAt = [&](float d) -> u32 {
      if (d <= zn) return 0xFFFFFFu;
      if (d >= zf) return 0u;
      const float z = 16777215.0F * zn * (zf - d) / (d * (zf - zn));
      return z <= 0.0F ? 0u : (z >= 16777215.0F ? 0xFFFFFFu : (u32)z);
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

  if ((passes & PassBloom) && bloom > 0) {
    const int w4 = lowW << 4, h4 = lowH << 4;
    // Downsample the frame to quarter res (bilinear averages 2x2).
    q = blit(q, fbVram, fbBufW, fbW, fbH, 0, 0, fbW << 4, fbH << 4, lowVram[0],
             lowBufW, 0, 0, lowW, lowH, true, false, 0, 0);
    // Soften: 4 taps of low0 blended into low1 at half/one texel offsets.
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
    // Add the blur back over the frame: Cd + Cs * bloom / 128.
    q = blit(q, lowVram[1], lowBufW, lowW, lowH, 0, 0, w4, h4, fbVram, fbBufW,
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
  PACK_GIFTAG(q, GS_SET_FRAME(fbVram >> 11, fbBufW >> 6, GS_PSM_32, 0),
              GS_REG_FRAME_1);
  q++;
  PACK_GIFTAG(q, GS_SET_CLAMP(1, 1, 0, 0, 0, 0), GS_REG_CLAMP_1);
  q++;
  PACK_GIFTAG(q, GS_SET_ZBUF(zbp, zsm, 0), GS_REG_ZBUF_1);
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
  // blit()/flatQuad() (or raw PACK_GIFTAG) and advances the cursor. The 352-
  // qword packet leaves ~330 qwords after this setup: roughly 27 blits or 47
  // flat quads - plenty for a screen effect, but not unbounded.
  q = build(*this, q, user);

  // Restore exactly what the rest of the frame machinery expects (mirrors
  // apply()).
  PACK_GIFTAG(q, GIF_SET_TAG(5, 0, 0, 0, GIF_FLG_PACKED, 1), GIF_REG_AD);
  q++;
  PACK_GIFTAG(q, GS_SET_FRAME(curFbVram >> 11, curFbBufW >> 6, GS_PSM_32, 0),
              GS_REG_FRAME_1);
  q++;
  PACK_GIFTAG(q, GS_SET_CLAMP(1, 1, 0, 0, 0, 0), GS_REG_CLAMP_1);
  q++;
  PACK_GIFTAG(q, GS_SET_ZBUF(zbp, zsm, 0), GS_REG_ZBUF_1);
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
