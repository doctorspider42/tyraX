/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Sandro Sobczyński <sandro.sobczynski@gmail.com>
*/

#include <dma.h>
#include <tamtypes.h>
#include <draw.h>
#include <graph.h>
#include <gs_gp.h>
#include <gs_privileged.h>
#include <gs_psm.h>
#include <packet2_utils.h>
#include "debug/debug.hpp"
#include "renderer/core/gs/renderer_core_gs.hpp"

namespace Tyra {

RendererCoreGS::RendererCoreGS() {
  context = 0;
  currentField = 0;
  alphaPacket = nullptr;
}

RendererCoreGS::~RendererCoreGS() {
  if (flipPacket) {
    packet2_free(flipPacket);
  }
  if (zTestPacket) {
    packet2_free(zTestPacket);
  }
  if (alphaPacket) {
    packet2_free(alphaPacket);
  }
}

void RendererCoreGS::init(RendererSettings* t_settings) {
  settings = t_settings;

  initChannels();
  // Modified by TyraX: 8 qwords - the InterlacedField mode appends a
  // per-field XYOFFSET write to the flip packet.
  flipPacket = packet2_create(8, P2_TYPE_UNCACHED_ACCL, P2_MODE_NORMAL, 0);
  zTestPacket = packet2_create(8, P2_TYPE_NORMAL, P2_MODE_NORMAL, 0);
  alphaPacket = packet2_create(4, P2_TYPE_NORMAL, P2_MODE_NORMAL, 0);
  allocateBuffers();
  initDrawingEnvironment();

  TYRA_LOG("Renderer core initialized!");
}

void RendererCoreGS::initChannels() {
  dma_channel_initialize(DMA_CHANNEL_GIF, nullptr, 0);
}

void RendererCoreGS::allocateBuffers() {
  allocateVramBuffers();

  // Resolve Auto to the console's actual region so games can read the real
  // refresh rate back from RendererSettings (TyraX fork).
  if (settings->getVideoMode() == VideoMode::Auto) {
    settings->setVideoMode(graph_get_region() == GRAPH_MODE_PAL
                               ? VideoMode::PAL
                               : VideoMode::NTSC);
  }

  programDisplay();

  TYRA_LOG("Framebuffers, zBuffer set and allocated!");
}

// Modified by TyraX: split out of allocateBuffers so the permanent region can
// be laid out again WITHOUT re-programming the display (reallocateBuffers).
void RendererCoreGS::allocateVramBuffers() {
  // Modified by TyraX: physical buffer height - half the logical height in
  // the InterlacedField mode (true field rendering).
  frameBuffers[0].width = static_cast<unsigned int>(settings->getWidth());
  frameBuffers[0].height = settings->getRenderHeightUI();
  frameBuffers[0].mask = 0;
  frameBuffers[0].psm = GS_PSM_32;
  frameBuffers[0].address = vram.allocateBuffer(
      frameBuffers[0].width, frameBuffers[0].height, frameBuffers[0].psm);

  frameBuffers[1].width = frameBuffers[0].width;
  frameBuffers[1].height = frameBuffers[0].height;
  frameBuffers[1].mask = frameBuffers[0].mask;
  frameBuffers[1].psm = frameBuffers[0].psm;
  frameBuffers[1].address = vram.allocateBuffer(
      frameBuffers[1].width, frameBuffers[1].height, frameBuffers[1].psm);

  zBuffer.enable = DRAW_ENABLE;
  zBuffer.mask = 0;
  zBuffer.method = ZTEST_METHOD_GREATER_EQUAL;
  zBuffer.zsm = GS_ZBUF_32;
  // Modified by TyraX (BLSS, docs/neural-upscaler.md): the z buffer covers the
  // RASTER, not the display buffer. With the raster scale on, nothing ever
  // renders 3D at display resolution - the whole scene is bracketed into the
  // low-res target and the composite that blows it back up masks z writes - so
  // a 512x448 z reserves 172 032 words nobody addresses. At 2x2 that is
  // 57 344 words instead of 229 376: 672 KB back, three times what the low-res
  // colour target costs, which is what makes the feature VRAM-positive.
  //
  // The invariant it rides on (and that RendererCoreBlss::beginScene/endScene
  // maintain): zBuffer.mask is 0 only INSIDE the low-res bracket. Every
  // draw_enable_tests / draw_setup_environment in the engine reads that field,
  // so the 2D/HUD/post-fx half of the frame - which draws full-screen sprites
  // at z = 0xFFFFFFFF and would otherwise stamp 448 rows at a 512 stride -
  // cannot reach past the smaller allocation.
  zRasterScaleX = settings->getRasterScaleX();
  zRasterScaleY = settings->getRasterScaleY();
  const int zWidth = static_cast<int>(settings->getRasterWidthUI());
  const int zHeight = static_cast<int>(settings->getRasterHeightUI());
  zBuffer.address = vram.allocateBuffer(zWidth, zHeight, zBuffer.zsm);

  TYRA_LOG("GS buffers: frame ", static_cast<int>(frameBuffers[0].width), "x",
           static_cast<int>(frameBuffers[0].height), " x2, z ", zWidth, "x",
           zHeight, " at ", static_cast<int>(zBuffer.address));
}

bool RendererCoreGS::needsBufferRealloc() const {
  return zRasterScaleX != settings->getRasterScaleX() ||
         zRasterScaleY != settings->getRasterScaleY();
}

// Modified by TyraX (BLSS): the same VRAM reset reinit() does, minus the video
// mode. See the header for why programDisplay() is deliberately not called.
void RendererCoreGS::reallocateBuffers() {
  vram.reset();
  allocateVramBuffers();
  initDrawingEnvironment();
}

// Modified by TyraX: video mode + display window + scan-out, split
// from allocateBuffers so runtime display switching can rerun it.
void RendererCoreGS::programDisplay() {
  // DTV scan modes next to the stock interlaced one.
  switch (settings->getDisplayMode()) {
    case DisplayMode::Progressive480p:
      // 448x448 buffer scanned out at 3x horizontally into the 1440-VCK
      // 480p raster: a 1344x448 window inside 1440x480 - exactly 4:3,
      // centered, with a thin overscan border. Widescreen changes nothing
      // here: the TV stretches the same signal to 16:9 (the projection
      // aspect compensates - see RendererSettings::updateGeometry).
      graph_set_mode(GRAPH_MODE_NONINTERLACED, GRAPH_MODE_HDTV_480P,
                     GRAPH_MODE_FRAME, GRAPH_DISABLE);
      setDtvDisplay(232, 35, 1440, 480, 3, 1, false);
      break;
    case DisplayMode::HiDef1080i:
      // 448x540 buffer in interlaced FIELD mode with 2x vertical
      // magnification: at MAGV 2x each field steps through EVERY buffer
      // line (raster lines 2n/2n+1 both map to line n), so the two fields
      // draw the same 540 lines one raster line apart - a stable
      // line-doubled picture, no field jitter, no flicker filter needed.
      // (The obvious alternative - the gsKit/OPL interlaced FRAME recipe -
      // hard-crashes PCSX2 v2.3.205 seconds after SetGsCrt; avoid it.)
      // The 1080i raster is natively 16:9: 4:3 games get a 3x-MAGH
      // 1344x1080 pillarboxed window, widescreen ones the widest integer
      // fit, 4x MAGH = 1792 of 1920 VCK.
      graph_set_mode(GRAPH_MODE_INTERLACED, GRAPH_MODE_HDTV_1080I,
                     GRAPH_MODE_FIELD, GRAPH_DISABLE);
      setDtvDisplay(236, 38, 1920, 1080, settings->getWidescreen() ? 4 : 3, 2,
                    true);
      break;
    case DisplayMode::InterlacedField: {
      // True field rendering: half-height (512x224) buffers scanned with
      // SMODE2.FFMD = FRAME, so each field reads EVERY buffer line - a
      // fresh field image can be presented at 50/60 Hz for half the fill
      // and VRAM of the stock mode. The DISPLAY window is IDENTICAL to the
      // stock interlaced one (ps2sdk's graph_set_screen values: the full
      // 448 frame-line window, 5x MAGH for the 512-wide buffer) - FFMD
      // only changes how the buffer feeds it, and ps2sdk's own
      // graph_set_screen mis-programs DY/DH for the FRAME case, so the
      // registers are written directly via setDtvDisplay. No flicker
      // filter: there is no full frame to blend, each field is its own
      // picture (the standard retail-game recipe, e.g. OPL/gsKit).
      const bool pal = settings->getVideoMode() == VideoMode::PAL;
      graph_set_mode(GRAPH_MODE_INTERLACED,
                     pal ? GRAPH_MODE_PAL : GRAPH_MODE_NTSC, GRAPH_MODE_FRAME,
                     GRAPH_DISABLE);
      setDtvDisplay(pal ? 680 : 652, pal ? 72 : 50, 2560, 448, 5, 1, true);
      break;
    }
    default: {
      // Same call sequence as ps2sdk's graph_initialize, with the mode
      // explicit (region-resolved Auto, or forced PAL/NTSC from
      // EngineOptions). The 512x448 framebuffer is kept for both signals;
      // PAL just outputs at 50 Hz. Widescreen is again the TV's stretch.
      // Pal576i shares this path with the buffer sized 512x512 by
      // RendererSettings and the signal pinned to PAL: 512 lines is
      // ps2sdk's own full PAL frame (graph_set_screen centers it in the
      // 576i raster), so the full-height "true PAL" of European retail
      // releases needs nothing beyond the taller buffer.
      const int mode =
          (settings->getVideoMode() == VideoMode::PAL ||
           settings->getDisplayMode() == DisplayMode::Pal576i)
              ? GRAPH_MODE_PAL
              : GRAPH_MODE_NTSC;
      graph_set_mode(GRAPH_MODE_INTERLACED, mode, GRAPH_MODE_FIELD,
                     GRAPH_ENABLE);
      graph_set_screen(0, 0, frameBuffers[1].width, frameBuffers[1].height);
      break;
    }
  }
  graph_set_bgcolor(0, 0, 0);
  presentFrameBuffer(context ^ 1);
  graph_enable_output();
}

// Modified by TyraX: full display rebuild for a runtime scan-mode
// switch. The VRAM allocator is a bump allocator and the frame/z buffers
// are its first allocations, so resizing them means starting the layout
// over - the caller (RendererCore::setDisplayOutput) evicts all textures
// first and re-inits post fx right after.
void RendererCoreGS::reinit() {
  vram.reset();
  allocateBuffers();
  initDrawingEnvironment();
}

// Widescreen-only change: rewrite JUST the display window registers. Going
// through programDisplay() here would call graph_set_mode, whose GS reset
// (CSR bit 9) wipes the drawing environment (FRAME/ZBUF/SCISSOR/XYOFFSET)
// that only the full reinit() path re-creates - the game keeps running but
// the GS stops drawing (frozen picture, EE logs still flowing). The video
// mode itself is unchanged, so DISPLAY1/2 is all that may differ (and for
// the SDTV modes not even that - the TV does the 16:9 stretch).
void RendererCoreGS::reprogramDisplay() {
  switch (settings->getDisplayMode()) {
    case DisplayMode::Progressive480p:
      setDtvDisplay(232, 35, 1440, 480, 3, 1, false);
      break;
    case DisplayMode::HiDef1080i:
      setDtvDisplay(236, 38, 1920, 1080, settings->getWidescreen() ? 4 : 3, 2,
                    true);
      break;
    case DisplayMode::InterlacedField:
      // SDTV widescreen is the TV's stretch - rewrite the same window.
      setDtvDisplay(settings->getVideoMode() == VideoMode::PAL ? 680 : 652,
                    settings->getVideoMode() == VideoMode::PAL ? 72 : 50,
                    2560, 448, 5, 1, true);
      break;
    default:
      graph_set_screen(0, 0, frameBuffers[0].width, frameBuffers[0].height);
      break;
  }
}

// Modified by TyraX: display window for the DTV modes. ps2sdk's
// graph_set_screen always programs the mode's full VCK width into DW, which
// only scans 1:1 when the framebuffer width divides it exactly - and no
// 64-aligned buffer width divides the 1440/1920-VCK DTV rasters, so the GS
// would scan garbage past the buffer's right edge. Program DISPLAY1/2
// directly instead: window sized to framebuffer * magnification, centered
// in the mode's raster (the gsKit recipe, proven by OPL and friends).
void RendererCoreGS::setDtvDisplay(int modeX, int modeY, int modeDW,
                                   int modeDH, int magH, int magV,
                                   bool interlaced) {
  const int width = static_cast<int>(settings->getWidth());
  const int height = static_cast<int>(settings->getHeight());
  const int dw = width * magH;
  const int dh = height * magV;
  int dx = modeX + (modeDW - dw) / 2;
  int dy = modeY + (modeDH - dh) / 2;
  // Keep the odd/even field start alignment in interlaced modes.
  if (interlaced) dy &= ~1;
  const int magVReg = magV - 1;
  const u64 display =
      GS_SET_DISPLAY(dx, dy, magH - 1, magVReg, dw - 1, dh - 1);
  *GS_REG_DISPLAY1 = display;
  *GS_REG_DISPLAY2 = display;
}

// Modified by TyraX: scan-out selection per display mode. The stock
// interlaced mode (and Pal576i, the same scan with a taller buffer) keeps
// ps2sdk's flicker filter (both read circuits, the
// second offset by one line). The DTV modes and InterlacedField run with
// the filter off, where graph_enable_output displays read circuit 2 alone -
// it must scan from line 0, not the +1 line the _filtered variant programs
// (and in field rendering there is no full frame to blend anyway).
void RendererCoreGS::presentFrameBuffer(u8 index) {
  auto& fb = frameBuffers[index];
  if (settings->getDisplayMode() == DisplayMode::Interlaced ||
      settings->getDisplayMode() == DisplayMode::Pal576i) {
    graph_set_framebuffer_filtered(fb.address, fb.width, fb.psm, 0, 0);
  } else {
    graph_set_framebuffer(0, fb.address, fb.width, fb.psm, 0, 0);
    graph_set_framebuffer(1, fb.address, fb.width, fb.psm, 0, 0);
  }
}

// Modified by TyraX: GS hardware fog color register.
#ifndef GS_REG_FOGCOL
#define GS_REG_FOGCOL 0x3D
#endif
#ifndef GS_SET_FOGCOL
#define GS_SET_FOGCOL(R, G, B) \
  ((u64)(R) | ((u64)(G) << 8) | ((u64)(B) << 16))
#endif

void RendererCoreGS::setFogColor(const u8& r, const u8& g, const u8& b) {
  packet2_t* packet2 = packet2_create(4, P2_TYPE_NORMAL, P2_MODE_NORMAL, 0);
  qword_t* q = packet2->base;
  PACK_GIFTAG(q, GIF_SET_TAG(1, 1, 0, 0, GIF_FLG_PACKED, 1), GIF_REG_AD);
  q++;
  PACK_GIFTAG(q, GS_SET_FOGCOL(r, g, b), GS_REG_FOGCOL);
  q++;
  packet2_update(packet2, q);
  dma_channel_wait(DMA_CHANNEL_GIF, 0);
  dma_channel_send_packet2(packet2, DMA_CHANNEL_GIF, true);
  dma_channel_wait(DMA_CHANNEL_GIF, 0);
  packet2_free(packet2);
}

void RendererCoreGS::setAlpha(const u64& alpha) {
  packet2_reset(alphaPacket, false);
  qword_t* q = alphaPacket->base;
  PACK_GIFTAG(q, GIF_SET_TAG(1, 1, 0, 0, GIF_FLG_PACKED, 1), GIF_REG_AD);
  q++;
  PACK_GIFTAG(q, alpha, GS_REG_ALPHA_1);
  q++;
  packet2_update(alphaPacket, q);
  dma_channel_wait(DMA_CHANNEL_GIF, 0);
  dma_channel_send_packet2(alphaPacket, DMA_CHANNEL_GIF, true);
  dma_channel_wait(DMA_CHANNEL_GIF, 0);
}

void RendererCoreGS::enableZTests() {
  packet2_reset(zTestPacket, false);
  packet2_update(zTestPacket,
                 draw_enable_tests(zTestPacket->base, 0, &zBuffer));
  packet2_update(zTestPacket, draw_finish(zTestPacket->next));
  dma_channel_wait(DMA_CHANNEL_GIF, 0);
  dma_channel_send_packet2(zTestPacket, DMA_CHANNEL_GIF, true);
}

void RendererCoreGS::initDrawingEnvironment() {
  packet2_t* packet2 = packet2_create(20, P2_TYPE_NORMAL, P2_MODE_NORMAL, 0);
  packet2_update(packet2, draw_setup_environment(packet2->base, 0, frameBuffers,
                                                 &zBuffer));
  packet2_update(packet2, draw_primitive_xyoffset(
                              packet2->next, 0,
                              screenCenter - (settings->getWidth() / 2.0F),
                              screenCenter -
                                  (settings->getRenderHeightF() / 2.0F)));
  packet2_update(packet2, draw_finish(packet2->next));
  dma_channel_send_packet2(packet2, DMA_CHANNEL_GIF, true);
  dma_channel_wait(DMA_CHANNEL_GIF, 0);
  packet2_free(packet2);
  TYRA_LOG("Drawing environment initialized!");
}

// Modified by TyraX: the same per-field bias setXYOffset applies, exposed so
// the raster brackets that write XYOFFSET raw (BLSS's low-res redirect and its
// composite passes need exact 1/16 units for the sub-pixel jitter) do not
// silently drop it.
int RendererCoreGS::getFieldYOffset16() const {
  if (!settings->isFieldRendering()) return 0;
  return currentField == GRAPH_FIELD_ODD ? 8 : 0;
}

// Modified by TyraX: the nesting raster target - see the header for why this
// exists at all.
RendererCoreGS::RasterTarget RendererCoreGS::getRasterTarget() const {
  if (rasterRedirected) return redirect;

  RasterTarget t;
  const int w = static_cast<int>(settings->getWidth());
  // The PHYSICAL buffer height (half the logical one in InterlacedField).
  const int h = static_cast<int>(settings->getRenderHeightF());
  t.frameAddress = static_cast<int>(frameBuffers[context].address);
  t.frameWidth = static_cast<int>(frameBuffers[context].width);
  t.scissorX0 = 0;
  t.scissorX1 = w - 1;
  t.scissorY0 = 0;
  t.scissorY1 = h - 1;
  t.offsetX16 = static_cast<int>((screenCenter - w / 2.0F) * 16.0F);
  t.offsetY16 =
      static_cast<int>((screenCenter - h / 2.0F) * 16.0F) + getFieldYOffset16();
  return t;
}

void RendererCoreGS::redirectRasterTo(const RasterTarget& target) {
  redirect = target;
  rasterRedirected = true;
}

void RendererCoreGS::endRasterRedirect() { rasterRedirected = false; }

qword_t* RendererCoreGS::emitRasterRestore(qword_t* q, bool texFlush) {
  const RasterTarget t = getRasterTarget();

  // NLOOP counts every register row below - an undercount stalls the GIF
  // forever (the stray qword parses as a new giftag with a garbage NLOOP).
  const int nloop = texFlush ? 4 : 3;
  PACK_GIFTAG(q, GIF_SET_TAG(nloop, 0, 0, 0, GIF_FLG_PACKED, 1), GIF_REG_AD);
  q++;
  if (texFlush) {
    // The bracket just rendered into VRAM the scene is about to sample as a
    // texture - drop any stale texels of it from the GS texture cache.
    PACK_GIFTAG(q, GS_SET_TEXFLUSH(0), GS_REG_TEXFLUSH);
    q++;
  }
  PACK_GIFTAG(q,
              GS_SET_FRAME(t.frameAddress >> 11, t.frameWidth >> 6, GS_PSM_32,
                           0),
              GS_REG_FRAME_1);
  q++;
  PACK_GIFTAG(q,
              GS_SET_SCISSOR(t.scissorX0, t.scissorX1, t.scissorY0,
                             t.scissorY1),
              GS_REG_SCISSOR_1);
  q++;
  // Raw, not draw_primitive_xyoffset: the offset carries BLSS' sub-pixel
  // jitter and the per-field bias in exact 1/16 units.
  PACK_GIFTAG(q, GS_SET_XYOFFSET(t.offsetX16, t.offsetY16), GS_REG_XYOFFSET_1);
  q++;
  // The drawing environment's alpha/depth test.
  q = draw_enable_tests(q, 0, &zBuffer);
  // ZBUF written EXPLICITLY and LAST, not left to draw_enable_tests. Every
  // bracket that gets here pointed ZBUF at its OWN depth buffer (the env map's
  // 128x128, the shadow map's 64x64), so "restore" has to include putting the
  // scene z back - and this must not depend on what a ps2sdk helper happens to
  // pack. It cost a debugging session: the shadow-map bracket came back with
  // ZBUF still on the 64x64 silhouette buffer, so every projected-shadow
  // receiver patch failed GEQUAL against the caster's own light-space depth
  // and no shadow was drawn at all. The mask is bracket-scoped (0 only inside
  // the BLSS low-res bracket - see allocateVramBuffers).
  PACK_GIFTAG(q, GIF_SET_TAG(1, 0, 0, 0, GIF_FLG_PACKED, 1), GIF_REG_AD);
  q++;
  PACK_GIFTAG(q,
              GS_SET_ZBUF(static_cast<int>(zBuffer.address) >> 11,
                          static_cast<int>(zBuffer.zsm), zBuffer.mask),
              GS_REG_ZBUF_1);
  q++;
  return q;
}

qword_t* RendererCoreGS::setXYOffset(qword_t* q, const int& drawContext,
                                     const float& x, const float& y) {
  PACK_GIFTAG(q, GIF_SET_TAG(1, 0, 0, 0, GIF_FLG_PACKED, 1), GIF_REG_AD);
  q++;

  int yOffset = currentField == GRAPH_FIELD_ODD ? 8 : 0;

  PACK_GIFTAG(q,
              GS_SET_XYOFFSET(static_cast<int>(x * 16.0F),
                              static_cast<int>((y * 16.0F + yOffset))),
              GS_REG_XYOFFSET + drawContext);
  q++;

  return q;
}

void RendererCoreGS::flipBuffers() {
  presentFrameBuffer(context);  // Modified by TyraX (DTV modes)

  context ^= 1;

  packet2_update(flipPacket,
                 draw_framebuffer(flipPacket->base, 0, &frameBuffers[context]));
  // Modified by TyraX: true field rendering (InterlacedField). The frame we
  // start rendering now is presented at the next vsync, i.e. scanned during
  // the OPPOSITE field of the one running right now (CSR.FIELD, which shows
  // the frame just presented above). The odd field's raster lines sit half a
  // frame line below the even field's, so the image meant for it must be
  // sampled half a buffer line lower - setXYOffset() shifts the drawing
  // window up by 8 (0.5 px in 12.4 fixed point) on odd fields. Without this
  // static geometry bobs by a full scan line at half the field rate.
  if (settings->isFieldRendering()) {
    updateCurrentField();
    currentField =
        currentField == GRAPH_FIELD_ODD ? GRAPH_FIELD_EVEN : GRAPH_FIELD_ODD;
    packet2_update(
        flipPacket,
        setXYOffset(flipPacket->next, 0,
                    screenCenter - (settings->getWidth() / 2.0F),
                    screenCenter - (settings->getRenderHeightF() / 2.0F)));
  }

  packet2_update(flipPacket, draw_finish(flipPacket->next));
  dma_channel_wait(DMA_CHANNEL_GIF, 0);
  dma_channel_send_packet2(flipPacket, DMA_CHANNEL_GIF, true);
  draw_wait_finish();
}

void RendererCoreGS::updateCurrentField() {
  if (*GS_REG_CSR & (1 << 13)) {
    currentField = GRAPH_FIELD_ODD;
    return;
  }

  currentField = GRAPH_FIELD_EVEN;
}

}  // namespace Tyra
