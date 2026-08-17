/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Added by TyraX: destination-alpha shadow mask (flashlight shadow volumes).
*/

#include "renderer/core/alphamask/renderer_core_alpha_mask.hpp"

#include <dma.h>
#include <draw.h>
#include <gs_gp.h>
#include <gs_psm.h>
#include "debug/debug.hpp"

namespace Tyra {

// TW/TH round UP to the next power of two - draw_log2 is only exact on
// powers of two and the render height (448) is not one.
static int lg2up(int v) {
  int r = 0;
  while ((1 << r) < v) r++;
  return r;
}

RendererCoreAlphaMask::RendererCoreAlphaMask() {}

RendererCoreAlphaMask::~RendererCoreAlphaMask() {
  if (beginPacket) packet2_free(beginPacket);
  if (endPacket) packet2_free(endPacket);
  if (repaintPacket) packet2_free(repaintPacket);
  if (keepPacket) packet2_free(keepPacket);
  if (countBeginPacket) packet2_free(countBeginPacket);
  if (countResolvePacket) packet2_free(countResolvePacket);
}

void RendererCoreAlphaMask::init(RendererSettings* t_settings,
                                 RendererCoreGS* t_gs,
                                 RendererCoreSync* t_sync, Path1* t_path1) {
  settings = t_settings;
  gs = t_gs;
  sync = t_sync;
  path1 = t_path1;
  if (!beginPacket)
    beginPacket = packet2_create(16, P2_TYPE_NORMAL, P2_MODE_NORMAL, 0);
  if (!endPacket)
    endPacket = packet2_create(16, P2_TYPE_NORMAL, P2_MODE_NORMAL, 0);
  if (!repaintPacket)
    repaintPacket = packet2_create(24, P2_TYPE_NORMAL, P2_MODE_NORMAL, 0);
  if (!keepPacket)
    keepPacket = packet2_create(8, P2_TYPE_NORMAL, P2_MODE_NORMAL, 0);
  // A display-mode switch resets the whole VRAM map and re-runs init();
  // re-place the count target when the game had turned it on (the
  // shadow-map slots' rule).
  if (countAllocated) {
    countAllocated = false;
    allocateCount();
  }
}

void RendererCoreAlphaMask::allocateCount() {
  if (countAllocated) return;
  // 32-BIT, because this target is depth-tested against the SCENE's z buffer
  // and the GS requires a colour buffer and its z to share PAGE GEOMETRY
  // (32/24-bit pages are 64x32 pixels, 16-bit ones 64x64). A 16-bit target
  // here put a 32-pixel checkerboard of wrong depth comparisons on real
  // hardware while PCSX2 showed nothing at all - see the header.
  //
  // A full raster at 32 bits is 1 MB, which no project can spare, so this is
  // a BAND of kCountBandRows and countBegin slides FRAME.FBP by whole page
  // rows to move it over the rect. Permanent-region discipline like the
  // shadow-map slots: below every texture, so the heap can never rewind past
  // it. Refusal is graceful - the caller keeps the convex 1-bit path.
  // 32-BIT FRAMEBUFFERS ONLY, for now, and this is a MEASURED limit rather
  // than a precaution. On a PSMCT16 framebuffer the counting path makes a
  // torch-lit surface come back HUE-SHIFTED - measured (208, 56, 144) on a
  // warm cream lamp post, i.e. each channel wrapping - while the same 16-bit
  // project in silhouette mode is pixel-clean. It reproduces in PCSX2, so it
  // is not the page-geometry trap above, and the cause is not yet found;
  // refusing here keeps a 16-bit project on the convex sub-box path, which
  // is correct there, instead of shipping wrong colour. (COLCLAMP was the
  // obvious suspect and is NOT it - programming COLOR_CLAMP_ENABLE around
  // the frame changed nothing.)
  if (settings->getFrameBufferPsm() != GS_PSM_32) {
    TYRA_WARN("Shadow-volume counting needs a 32-bit framebuffer (this ",
              "project is 16-bit); mesh volumes fall back to convex ",
              "sub-boxes.");
    return;
  }
  countW = static_cast<int>(settings->getWidth());
  const int rasterH = static_cast<int>(settings->getRenderHeightF());
  countH = rasterH < kCountBandRows ? rasterH : kCountBandRows;
  // The band height must be a whole number of page rows, or the last page row
  // of the band would be addressed past the allocation.
  countH = countH / 32 * 32;
  if (countH <= 0) {
    TYRA_WARN("Shadow-volume count band too short for a page row; ",
              "mesh volumes fall back to convex sub-boxes.");
    return;
  }
  const int addr = gs->vram.allocateBuffer(countW, countH, GS_PSM_32);
  if (addr < 0) {
    TYRA_WARN("Shadow-volume count target refused (VRAM); ",
              "mesh volumes fall back to convex sub-boxes.");
    return;
  }
  // The slide is SUBTRACTED from the base, so the deepest band this raster
  // can ask for must still leave a non-negative address. Checked once here
  // rather than per frame, so countReady() answers the whole question.
  const int lastBand = (rasterH - 1) / countH * countH;
  if (slidBaseFor(addr, lastBand, countW) < 0) {
    // The permanent region is a bump allocator, so there is nothing to give
    // back - free() deliberately does not know these addresses. It costs the
    // band's words in a layout the engine's own init order cannot produce
    // (the target lands high above the display buffers), and refusing is
    // still better than sliding FRAME below address 0.
    TYRA_WARN("Shadow-volume count target sits too low in VRAM for its ",
              "page slide; mesh volumes fall back to convex sub-boxes.");
    return;
  }
  countAddress = addr;
  countAllocated = true;
  if (!countBeginPacket)
    countBeginPacket = packet2_create(16, P2_TYPE_NORMAL, P2_MODE_NORMAL, 0);
  if (!countResolvePacket)
    countResolvePacket = packet2_create(40, P2_TYPE_NORMAL, P2_MODE_NORMAL, 0);
  if (!maskClearPacket)
    maskClearPacket = packet2_create(16, P2_TYPE_NORMAL, P2_MODE_NORMAL, 0);
  TYRA_LOG("Shadow-volume count band at ", countAddress, " (", countW, "x",
           countH, " CT32, ", (countW * countH * 4) / 1024, " KB)");
}

// The page-row slide. A 32-bit page is 64x32 pixels = 2048 words, and a page
// ROW of the raster is (frameWidth / 64) pages, laid out linearly - so a band
// whose first row is bandY0 is addressed through the base minus
// (bandY0 / 32) row-strides, and pixel (x, bandY0) then lands on the target's
// own row 0. ZBP is never slid, which is what keeps the depth test reading
// the scene's own depth for the true (x, y).
int RendererCoreAlphaMask::slidBaseFor(int base, int bandY0, int frameWidth) {
  const int pagesPerRow = frameWidth / 64;
  const int wordsPerPageRow = pagesPerRow * 2048;
  return base - (bandY0 / 32) * wordsPerPageRow;
}

int RendererCoreAlphaMask::slidBase(int bandY0, int frameWidth) const {
  return slidBaseFor(countAddress, bandY0, frameWidth);
}

void RendererCoreAlphaMask::maskClear() {
  // The mask lives in the framebuffer's alpha and must start the frame at 0
  // ("everything lit"): a pixel left at last frame's repainted 0x80 reads as
  // shadow to every DATE-gated pass. Once per frame, before the first band.
  if (path1->isVU1Configured()) sync->align3D();

  const RendererCoreGS::RasterTarget t = gs->getRasterTarget();
  const int psm = settings->getFrameBufferPsm();
  const unsigned fbmsk = psm == 0 ? 0x00FFFFFFu : 0x7FFF7FFFu;
  const int w = static_cast<int>(settings->getWidth());
  const int h = static_cast<int>(settings->getRenderHeightF());

  packet2_reset(maskClearPacket, false);
  qword_t* q = maskClearPacket->next;
  // Rows: FRAME, TEST, ZBUF, RGBAQ, PRIM, XYZ2, XYZ2 = 7.
  PACK_GIFTAG(q, GIF_SET_TAG(7, 0, 0, 0, GIF_FLG_PACKED, 1), GIF_REG_AD);
  q++;
  PACK_GIFTAG(q,
              GS_SET_FRAME(t.frameAddress >> 11, t.frameWidth >> 6, psm,
                           fbmsk),
              GS_REG_FRAME_1);
  q++;
  PACK_GIFTAG(q, GS_SET_TEST(0, 0, 0, 0, 0, 0, 1, ZTEST_METHOD_ALLPASS),
              GS_REG_TEST_1);
  q++;
  PACK_GIFTAG(q, GS_SET_ZBUF(gs->zBuffer.address >> 11, gs->zBuffer.zsm, 1),
              GS_REG_ZBUF_1);
  q++;
  PACK_GIFTAG(q, GS_SET_RGBAQ(0, 0, 0, 0, 0x3F800000), GS_REG_RGBAQ);
  q++;
  PACK_GIFTAG(q, GS_SET_PRIM(6 /* sprite */, 0, 0, 0, 0, 0, 0, 0, 0),
              GS_REG_PRIM);
  q++;
  PACK_GIFTAG(q, GS_SET_XYZ(t.offsetX16, t.offsetY16, 0), GS_REG_XYZ2);
  q++;
  PACK_GIFTAG(q,
              GS_SET_XYZ(t.offsetX16 + (w << 4), t.offsetY16 + (h << 4), 0),
              GS_REG_XYZ2);
  q++;
  packet2_update(maskClearPacket, q);
  packet2_update(maskClearPacket,
                 gs->emitRasterRestore(maskClearPacket->next, false));
  packet2_update(maskClearPacket, draw_finish(maskClearPacket->next));
  dma_channel_wait(DMA_CHANNEL_GIF, 0);
  dma_channel_send_packet2(maskClearPacket, DMA_CHANNEL_GIF, true);
  draw_wait_finish();
}

void RendererCoreAlphaMask::countBegin(int x0, int y0, int x1, int y1,
                                       int bandY0) {
  TYRA_ASSERT(countAllocated, "countBegin() before allocateCount()!");
  // The FRAME redirect below is global GS state - drain first.
  if (path1->isVU1Configured()) sync->align3D();

  const RendererCoreGS::RasterTarget t = gs->getRasterTarget();
  const int w = static_cast<int>(settings->getWidth());
  const int h = static_cast<int>(settings->getRenderHeightF());
  bandY0 = bandY0 / countH * countH;  // bands step by the band height
  if (x0 < 0) x0 = 0;
  if (x1 > w) x1 = w;
  // The drawn region is the caller's rect INTERSECTED with this band.
  if (y0 < bandY0) y0 = bandY0;
  const int bandEnd = bandY0 + countH < h ? bandY0 + countH : h;
  if (y1 > bandEnd) y1 = bandEnd;
  if (x1 <= x0) x1 = x0 + 1;
  if (y1 <= y0) y1 = y0 + 1;

  packet2_reset(countBeginPacket, false);
  qword_t* q = countBeginPacket->next;
  // Rows: FRAME, SCISSOR, TEST, ZBUF, RGBAQ, PRIM, XYZ2, XYZ2 = 8. NLOOP
  // counts every register row - a mismatch stalls the GIF forever.
  PACK_GIFTAG(q, GIF_SET_TAG(8, 0, 0, 0, GIF_FLG_PACKED, 1), GIF_REG_AD);
  q++;
  // FRAME -> the count band, at the RASTER's stride and SLID by the band's
  // page rows, so screen row bandY0 lands on the band's own row 0. PSMCT32
  // so the page geometry matches the scene z this pass tests against; the
  // stride must be the scene's because z addressing walks FRAME.FBW.
  PACK_GIFTAG(q,
              GS_SET_FRAME(slidBase(bandY0, t.frameWidth) >> 11,
                           t.frameWidth >> 6, GS_PSM_32, 0),
              GS_REG_FRAME_1);
  q++;
  // XYOFFSET is left exactly as the raster set it, which is what keeps pixel
  // (x,y) reading the same z word the scene wrote; the SCISSOR narrows to the
  // rect so neither the clear nor the volume draws pay for pixels no volume
  // can reach.
  PACK_GIFTAG(q, GS_SET_SCISSOR(x0, x1 - 1, y0, y1 - 1), GS_REG_SCISSOR_1);
  q++;
  // Clear sprite: all channels to zero, no tests, and Z WRITES MASKED - the
  // z bound here is the SCENE's depth, which the volume passes are about to
  // test against.
  PACK_GIFTAG(q, GS_SET_TEST(0, 0, 0, 0, 0, 0, 1, ZTEST_METHOD_ALLPASS),
              GS_REG_TEST_1);
  q++;
  PACK_GIFTAG(q, GS_SET_ZBUF(gs->zBuffer.address >> 11, gs->zBuffer.zsm, 1),
              GS_REG_ZBUF_1);
  q++;
  PACK_GIFTAG(q, GS_SET_RGBAQ(0, 0, 0, 0, 0x3F800000), GS_REG_RGBAQ);
  q++;
  PACK_GIFTAG(q, GS_SET_PRIM(6 /* sprite */, 0, 0, 0, 0, 0, 0, 0, 0),
              GS_REG_PRIM);
  q++;
  PACK_GIFTAG(q,
              GS_SET_XYZ(t.offsetX16 + (x0 << 4), t.offsetY16 + (y0 << 4), 0),
              GS_REG_XYZ2);
  q++;
  PACK_GIFTAG(q,
              GS_SET_XYZ(t.offsetX16 + (x1 << 4), t.offsetY16 + (y1 << 4), 0),
              GS_REG_XYZ2);
  q++;
  packet2_update(countBeginPacket, q);
  packet2_update(countBeginPacket, draw_finish(countBeginPacket->next));
  dma_channel_wait(DMA_CHANNEL_GIF, 0);
  dma_channel_send_packet2(countBeginPacket, DMA_CHANNEL_GIF, true);
  draw_wait_finish();
}

void RendererCoreAlphaMask::countResolve(int x0, int y0, int x1, int y1,
                                         int bandY0) {
  TYRA_ASSERT(countAllocated, "countResolve() before allocateCount()!");
  // Drain the volume draws - the sprite below samples what they just wrote.
  if (path1->isVU1Configured()) sync->align3D();

  const RendererCoreGS::RasterTarget t = gs->getRasterTarget();
  const int psm = settings->getFrameBufferPsm();
  const unsigned fbmsk = psm == 0 ? 0x00FFFFFFu : 0x7FFF7FFFu;
  const int w = static_cast<int>(settings->getWidth());
  const int h = static_cast<int>(settings->getRenderHeightF());
  bandY0 = bandY0 / countH * countH;
  if (x0 < 0) x0 = 0;
  if (x1 > w) x1 = w;
  if (y0 < bandY0) y0 = bandY0;
  const int bandEnd = bandY0 + countH < h ? bandY0 + countH : h;
  if (y1 > bandEnd) y1 = bandEnd;
  if (x1 <= x0) x1 = x0 + 1;
  if (y1 <= y0) y1 = y0 + 1;

  packet2_reset(countResolvePacket, false);
  qword_t* q = countResolvePacket->next;
  // Rows: TEXFLUSH, TEX0, TEX1, TEXA, CLAMP, FRAME, SCISSOR, TEST, ZBUF,
  // RGBAQ, PRIM, UV, XYZ2, UV, XYZ2, CLAMP-restore, TEXA-restore = 17.
  PACK_GIFTAG(q, GIF_SET_TAG(17, 0, 0, 0, GIF_FLG_PACKED, 1), GIF_REG_AD);
  q++;
  // The count target was a render target a microsecond ago - invalidate the
  // texture cache before sampling it.
  PACK_GIFTAG(q, GS_SET_TEXFLUSH(0), GS_REG_TEXFLUSH);
  q++;
  // Sampled through the SAME page slide the count pass wrote through, in the
  // same 32-bit format, so the texel at (x, y - bandY0) is the count written
  // for screen pixel (x, y). TBP is in 64-word blocks, FBP in 2048-word
  // pages, hence the different shifts of one address.
  PACK_GIFTAG(q,
              GS_SET_TEX0(slidBase(bandY0, t.frameWidth) >> 6,
                          t.frameWidth >> 6, GS_PSM_32, lg2up(countW),
                          lg2up(countH), 1 /* tcc */, 1 /* decal */, 0, 0, 0,
                          0, 0),
              GS_REG_TEX0_1);
  q++;
  PACK_GIFTAG(q, GS_SET_TEX1(1, 0, 0, 0, 0, 0, 0), GS_REG_TEX1_1);
  q++;
  // The whole conversion: AEM = 1 expands an ALL-ZERO 16-bit texel to alpha
  // 0 and anything else to TA0 = 0x80. The count channel is the only thing
  // ever written into the target, so "count > 0" IS "texel != 0".
  PACK_GIFTAG(q, GS_SET_TEXA(0x80, 1, 0x80), GS_REG_TEXA);
  q++;
  PACK_GIFTAG(q, GS_SET_CLAMP(2, 2, 0, countW - 1, 0, countH - 1),
              GS_REG_CLAMP_1);  // region clamp: the band, in its own space
  q++;
  // The real framebuffer, alpha bits only, scissored to the same rect the
  // count bracket used - resolving pixels no volume can reach costs a
  // full-raster read-modify-write for nothing.
  PACK_GIFTAG(q,
              GS_SET_FRAME(t.frameAddress >> 11, t.frameWidth >> 6, psm,
                           fbmsk),
              GS_REG_FRAME_1);
  q++;
  PACK_GIFTAG(q, GS_SET_SCISSOR(x0, x1 - 1, y0, y1 - 1), GS_REG_SCISSOR_1);
  q++;
  // ATEST != 0 with AFAIL = write NOTHING is what makes this an OR: a
  // zero-count texel expands to fragment alpha 0, fails, and leaves the
  // mask bit an earlier caster set. Z writes masked, test all-pass.
  PACK_GIFTAG(q,
              GS_SET_TEST(DRAW_ENABLE, ATEST_METHOD_NOTEQUAL, 0x00,
                          ATEST_KEEP_ALL, 0, 0, 1, ZTEST_METHOD_ALLPASS),
              GS_REG_TEST_1);
  q++;
  PACK_GIFTAG(q,
              GS_SET_ZBUF(gs->zBuffer.address >> 11, gs->zBuffer.zsm, 1),
              GS_REG_ZBUF_1);
  q++;
  PACK_GIFTAG(q, GS_SET_RGBAQ(0, 0, 0, 0, 0x3F800000), GS_REG_RGBAQ);
  q++;
  PACK_GIFTAG(q,
              GS_SET_PRIM(6 /* sprite */, 0, 1 /* tme */, 0, 0, 0,
                          1 /* uv */, 0, 0),
              GS_REG_PRIM);
  q++;
  PACK_GIFTAG(q, GS_SET_UV(x0 << 4, (y0 - bandY0) << 4), GS_REG_UV);
  q++;
  PACK_GIFTAG(q,
              GS_SET_XYZ(t.offsetX16 + (x0 << 4), t.offsetY16 + (y0 << 4), 0),
              GS_REG_XYZ2);
  q++;
  PACK_GIFTAG(q, GS_SET_UV(x1 << 4, (y1 - bandY0) << 4), GS_REG_UV);
  q++;
  PACK_GIFTAG(q,
              GS_SET_XYZ(t.offsetX16 + (x1 << 4), t.offsetY16 + (y1 << 4), 0),
              GS_REG_XYZ2);
  q++;
  // 3D texture wrap is REPEAT by contract (Path3::clearScreen asserts it per
  // frame); put it back before any bag samples through this register.
  PACK_GIFTAG(q, GS_SET_CLAMP(0, 0, 0, 0, 0, 0), GS_REG_CLAMP_1);
  q++;
  // TEXA is GLOBAL: leave AEM = 1 armed and every pure-black texel of a
  // 16/24-bit texture later in the frame reads alpha 0 - under the cutout
  // ATEST that DELETES black pixels of ordinary textures. Restore the
  // environment default (the value BLSS and the frame profiler restore to).
  PACK_GIFTAG(q, GS_SET_TEXA(0x80, 0, 0x80), GS_REG_TEXA);
  q++;
  packet2_update(countResolvePacket, q);
  packet2_update(countResolvePacket,
                 gs->emitRasterRestore(countResolvePacket->next, false));
  packet2_update(countResolvePacket, draw_finish(countResolvePacket->next));
  dma_channel_wait(DMA_CHANNEL_GIF, 0);
  dma_channel_send_packet2(countResolvePacket, DMA_CHANNEL_GIF, true);
  draw_wait_finish();
}

void RendererCoreAlphaMask::begin() {
  // The redirect below is global GS state - anything still in flight must
  // land in the unmasked frame first.
  if (path1->isVU1Configured()) sync->align3D();

  const RendererCoreGS::RasterTarget t = gs->getRasterTarget();
  const int psm = settings->getFrameBufferPsm();
  // Expose ONLY the alpha bits. FBMSK is specified in the frame format's own
  // pixel layout: PSMCT32 (psm 0) has A in the top byte; the 16-bit formats
  // pack two pixels per word with A as bit 15 of each half (docs/gs-vram.md).
  const unsigned fbmsk = psm == 0 ? 0x00FFFFFFu : 0x7FFF7FFFu;

  const int w = static_cast<int>(settings->getWidth());
  const int h = static_cast<int>(settings->getRenderHeightF());

  packet2_reset(beginPacket, false);
  qword_t* q = beginPacket->next;
  // NLOOP counts every register row - a mismatch stalls the GIF forever
  // (the shadow-map bracket's warning, and this packet froze a frame at
  // exactly the moment the first volume drew until the count was right).
  // Rows: FRAME, TEST, ZBUF, RGBAQ, PRIM, XYZ2, XYZ2, ZBUF = 8.
  PACK_GIFTAG(q, GIF_SET_TAG(8, 0, 0, 0, GIF_FLG_PACKED, 1), GIF_REG_AD);
  q++;
  // Same frame, color channels masked.
  PACK_GIFTAG(q,
              GS_SET_FRAME(t.frameAddress >> 11, t.frameWidth >> 6, psm,
                           fbmsk),
              GS_REG_FRAME_1);
  q++;
  // The clear sprite: alpha 0 across the raster, no tests, and Z WRITES
  // MASKED - a full-screen sprite that stamped depth would erase the very
  // buffer the volume passes are about to test against.
  PACK_GIFTAG(q, GS_SET_TEST(0, 0, 0, 0, 0, 0, 1, ZTEST_METHOD_ALLPASS),
              GS_REG_TEST_1);
  q++;
  PACK_GIFTAG(q,
              GS_SET_ZBUF(gs->zBuffer.address >> 11, gs->zBuffer.zsm, 1),
              GS_REG_ZBUF_1);
  q++;
  PACK_GIFTAG(q, GS_SET_RGBAQ(0, 0, 0, 0, 0x3F800000), GS_REG_RGBAQ);
  q++;
  PACK_GIFTAG(q, GS_SET_PRIM(6 /* sprite */, 0, 0, 0, 0, 0, 0, 0, 0),
              GS_REG_PRIM);
  q++;
  PACK_GIFTAG(q, GS_SET_XYZ(t.offsetX16, t.offsetY16, 0), GS_REG_XYZ2);
  q++;
  PACK_GIFTAG(q,
              GS_SET_XYZ(t.offsetX16 + (w << 4), t.offsetY16 + (h << 4), 0),
              GS_REG_XYZ2);
  q++;
  // The scene z comes back before any volume bag runs; their in-band TEST
  // qwords re-arm the depth test themselves, exactly as they do everywhere.
  PACK_GIFTAG(q,
              GS_SET_ZBUF(gs->zBuffer.address >> 11, gs->zBuffer.zsm,
                          gs->zBuffer.mask),
              GS_REG_ZBUF_1);
  q++;
  packet2_update(beginPacket, q);
  packet2_update(beginPacket, draw_finish(beginPacket->next));
  dma_channel_wait(DMA_CHANNEL_GIF, 0);
  dma_channel_send_packet2(beginPacket, DMA_CHANNEL_GIF, true);
  draw_wait_finish();
}

void RendererCoreAlphaMask::beginKeep() {
  // Re-mask the color channels without touching the alpha already written -
  // the walk in the generated game brackets each caster's volume separately
  // so its own light can draw first, and brackets after the first must not
  // reset what earlier casters put in the mask.
  if (path1->isVU1Configured()) sync->align3D();

  const RendererCoreGS::RasterTarget t = gs->getRasterTarget();
  const int psm = settings->getFrameBufferPsm();
  const unsigned fbmsk = psm == 0 ? 0x00FFFFFFu : 0x7FFF7FFFu;

  packet2_reset(keepPacket, false);
  qword_t* q = keepPacket->next;
  PACK_GIFTAG(q, GIF_SET_TAG(1, 0, 0, 0, GIF_FLG_PACKED, 1), GIF_REG_AD);
  q++;
  PACK_GIFTAG(q,
              GS_SET_FRAME(t.frameAddress >> 11, t.frameWidth >> 6, psm,
                           fbmsk),
              GS_REG_FRAME_1);
  q++;
  packet2_update(keepPacket, q);
  packet2_update(keepPacket, draw_finish(keepPacket->next));
  dma_channel_wait(DMA_CHANNEL_GIF, 0);
  dma_channel_send_packet2(keepPacket, DMA_CHANNEL_GIF, true);
  draw_wait_finish();
}

void RendererCoreAlphaMask::repaintAlpha() {
  // The mask LIVES in the framebuffer's alpha, and on the SDTV interlaced
  // modes that channel is live display state: presentFrameBuffer programs
  // ps2sdk's flicker filter (graph_set_framebuffer_filtered), whose PMODE
  // blends the two line-offset read circuits by PER-PIXEL alpha. Leave the
  // mask in place and the CRTC shows it - the volume shapes appear as
  // soft translucent wedges over the picture (found by frame-stepping a
  // torch toggle in PCSX2). So once the last DATE-gated light pass has
  // consumed the mask, the alpha byte is repainted to the 0x80 the rest of
  // the scene writes, colors untouched. Costs one alpha-only raster fill,
  // paid only on frames that drew a mask.
  if (path1->isVU1Configured()) sync->align3D();

  const RendererCoreGS::RasterTarget t = gs->getRasterTarget();
  const int psm = settings->getFrameBufferPsm();
  const unsigned fbmsk = psm == 0 ? 0x00FFFFFFu : 0x7FFF7FFFu;
  const int w = static_cast<int>(settings->getWidth());
  const int h = static_cast<int>(settings->getRenderHeightF());

  packet2_reset(repaintPacket, false);
  qword_t* q = repaintPacket->next;
  // Rows: FRAME, TEST, ZBUF, RGBAQ, PRIM, XYZ2, XYZ2 = 7. The raster
  // restore below re-emits FRAME/tests/ZBUF with its own tag.
  PACK_GIFTAG(q, GIF_SET_TAG(7, 0, 0, 0, GIF_FLG_PACKED, 1), GIF_REG_AD);
  q++;
  PACK_GIFTAG(q,
              GS_SET_FRAME(t.frameAddress >> 11, t.frameWidth >> 6, psm,
                           fbmsk),
              GS_REG_FRAME_1);
  q++;
  PACK_GIFTAG(q, GS_SET_TEST(0, 0, 0, 0, 0, 0, 1, ZTEST_METHOD_ALLPASS),
              GS_REG_TEST_1);
  q++;
  PACK_GIFTAG(q,
              GS_SET_ZBUF(gs->zBuffer.address >> 11, gs->zBuffer.zsm, 1),
              GS_REG_ZBUF_1);
  q++;
  PACK_GIFTAG(q, GS_SET_RGBAQ(0, 0, 0, 0x80, 0x3F800000), GS_REG_RGBAQ);
  q++;
  PACK_GIFTAG(q, GS_SET_PRIM(6 /* sprite */, 0, 0, 0, 0, 0, 0, 0, 0),
              GS_REG_PRIM);
  q++;
  PACK_GIFTAG(q, GS_SET_XYZ(t.offsetX16, t.offsetY16, 0), GS_REG_XYZ2);
  q++;
  PACK_GIFTAG(q,
              GS_SET_XYZ(t.offsetX16 + (w << 4), t.offsetY16 + (h << 4), 0),
              GS_REG_XYZ2);
  q++;
  packet2_update(repaintPacket, q);
  packet2_update(repaintPacket,
                 gs->emitRasterRestore(repaintPacket->next, false));
  packet2_update(repaintPacket, draw_finish(repaintPacket->next));
  dma_channel_wait(DMA_CHANNEL_GIF, 0);
  dma_channel_send_packet2(repaintPacket, DMA_CHANNEL_GIF, true);
  draw_wait_finish();
}

void RendererCoreAlphaMask::end() {
  // Drain the volume draws, then put the whole raster environment back -
  // emitRasterRestore rewrites FRAME with FBMSK 0, the scissor, the offset,
  // the tests and the scene ZBUF (the shadow-map bracket's lesson).
  if (path1->isVU1Configured()) sync->align3D();

  packet2_reset(endPacket, false);
  qword_t* q = gs->emitRasterRestore(endPacket->base, false);
  packet2_update(endPacket, q);
  packet2_update(endPacket, draw_finish(endPacket->next));
  dma_channel_wait(DMA_CHANNEL_GIF, 0);
  dma_channel_send_packet2(endPacket, DMA_CHANNEL_GIF, true);
  draw_wait_finish();
}

}  // namespace Tyra
