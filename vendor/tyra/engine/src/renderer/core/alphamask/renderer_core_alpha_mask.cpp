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

// FBMSK IS ALWAYS SPECIFIED IN 32-BIT RGBA8 BIT POSITIONS - R 0..7, G 8..15,
// B 16..23, A 24..31 - whatever PSM the framebuffer is in; the GS maps those
// onto a 16-bit target's 5551 layout itself. So "write alpha, protect colour"
// is this one constant for every colour depth, and the engine's other passes
// say the same thing (RendererCorePostFx: kKeepAlpha = 0xFF000000, and its
// per-channel masks clear one BYTE, on work buffers that are PSMCT16 in a
// 16-bit project).
//
// This used to be `psm == 0 ? 0x00FFFFFF : 0x7FFF7FFF`, reasoning from the
// 16-bit PIXEL layout (two pixels per word, alpha at bit 15 of each half).
// That mask exposes bit 15 - which in RGBA8 terms is the TOP BIT OF GREEN -
// so every "alpha only" write here also halved green wherever the torch lit
// something, and a 16-bit project came back magenta: (208, 56, 144) measured
// on a warm cream lamp post. 32-bit projects were unaffected, which is why it
// took a colour-depth switch to surface.
static constexpr unsigned kAlphaOnlyFbmsk = 0x00FFFFFFu;

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
  // The band's format follows the COLOUR depth, because the z buffer it is
  // depth-tested against does (RendererCoreDepth / RendererCoreGS): a 16-bit
  // project runs a PSMZ16 z whose pages are 64x64, so a PSMCT32 band would
  // reintroduce exactly the page-geometry mismatch this class documents - one
  // buffer further along. PSMCT16 also halves the band, and counting survives
  // 5-bit channels: N = 32 stores as 4, so seven overlapping front faces fit
  // before saturation, and a +N/-N pair still cancels at both extremes of the
  // dither matrix because the GS clamps at zero.
  //
  // COUNTING RUNS AT BOTH DEPTHS AGAIN. It was refused at 16-bit for one
  // release because the resolve laid dashed green marks down two fixed screen
  // columns over whatever the torch had lit - and the cause was NOT the
  // masked write at a PSMCT16 destination, which is what that refusal
  // assumed. Two measurements on a console settled it. A probe drawing the
  // same flat sprite through four different FBMSK values into a PSMCT16 frame
  // reads IDENTICALLY on hardware and in PCSX2: 0x00FFFFFF is colour-neutral
  // and its alpha half reaches the mask bit per pixel, so the constant is
  // right and the destination format is innocent. And a paired sweep, ONE
  // knob apart, over the same eight vantages of examples/night-walk at 16-bit
  // colour: with countResolve() binding TBP0 to the SLID band base (what this
  // file did until 1.59.1) the marks are on 8 of 8 frames, 800-4800 pixels
  // each; with TBP0 at the band's OWN base they are on 0 of 8, and flipping
  // the knob back brings them straight back. The columns are the ones a
  // console screenshot from the field showed, to within two pixels.
  //
  // The reading fault was double compensation: the count pass writes pixel
  // (x, y) through a FRAME slid by bandY0 worth of page rows, so the texel
  // for that pixel lives at the band's OWN base with V = y - bandY0. Binding
  // the texture to the slid base AS WELL made every band but the first sample
  // bandY0 rows BELOW the band - at 16-bit, 256 KB below: the top of the
  // scene z buffer and the projected-shadow slots. WHY garbage texels there
  // end up tinting pixels the mask is only supposed to gate is not explained
  // by anything measured here - the marks also appear above the band
  // boundary - and that half is written down as open in docs/flashlight.md.
  // What is settled is that they follow this one register and nothing else.
  const bool halfDepth = settings->getFrameBufferPsm() == GS_PSM_16;
  countPsm = halfDepth ? GS_PSM_16 : GS_PSM_32;
  countPageRows = halfDepth ? 64 : 32;
  countW = static_cast<int>(settings->getWidth());
  const int rasterH = static_cast<int>(settings->getRenderHeightF());
  countH = rasterH < kCountBandRows ? rasterH : kCountBandRows;
  // The band height must be a whole number of page rows, or the last page row
  // of the band would be addressed past the allocation.
  countH = countH / countPageRows * countPageRows;
  if (countH <= 0) {
    TYRA_WARN("Shadow-volume count band too short for a page row; ",
              "mesh volumes fall back to convex sub-boxes.");
    return;
  }
  const int addr = gs->vram.allocateBuffer(countW, countH, countPsm);
  if (addr < 0) {
    TYRA_WARN("Shadow-volume count target refused (VRAM); ",
              "mesh volumes fall back to convex sub-boxes.");
    return;
  }
  // The slide is SUBTRACTED from the base, so the deepest band this raster
  // can ask for must still leave a non-negative address. Checked once here
  // rather than per frame, so countReady() answers the whole question.
  const int lastBand = (rasterH - 1) / countH * countH;
  if (slidBaseFor(addr, lastBand, countW, countPageRows) < 0) {
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
           countH, halfDepth ? " CT16, " : " CT32, ",
           (countW * countH * (halfDepth ? 2 : 4)) / 1024, " KB)");
}

// The page-row slide. A GS page is 2048 words whatever the format - what
// changes is its SHAPE: 64x32 pixels at 32 bits, 64x64 at 16 - and a page ROW
// of the raster is (frameWidth / 64) pages laid out linearly. So a band whose
// first row is bandY0 is addressed through the base minus (bandY0 / pageRows)
// row-strides, and pixel (x, bandY0) then lands on the target's own row 0.
// ZBP is never slid, which is what keeps the depth test reading the scene's
// own depth for the true (x, y).
int RendererCoreAlphaMask::slidBaseFor(int base, int bandY0, int frameWidth,
                                       int pageRows) {
  const int pagesPerRow = frameWidth / 64;
  const int wordsPerPageRow = pagesPerRow * 2048;
  return base - (bandY0 / pageRows) * wordsPerPageRow;
}

int RendererCoreAlphaMask::slidBase(int bandY0, int frameWidth) const {
  return slidBaseFor(countAddress, bandY0, frameWidth, countPageRows);
}

void RendererCoreAlphaMask::maskClear() {
  // The mask lives in the framebuffer's alpha and must start the frame at 0
  // ("everything lit"): a pixel left at last frame's repainted 0x80 reads as
  // shadow to every DATE-gated pass. Once per frame, before the first band.
  if (path1->isVU1Configured()) sync->align3D();

  const RendererCoreGS::RasterTarget t = gs->getRasterTarget();
  const int psm = settings->getFrameBufferPsm();
  const unsigned fbmsk = kAlphaOnlyFbmsk;
  const int w = static_cast<int>(settings->getWidth());
  const int h = static_cast<int>(settings->getRenderHeightF());

  packet2_reset(maskClearPacket, false);
  qword_t* q = maskClearPacket->next;
  // Rows: FBA, FRAME, TEST, ZBUF, RGBAQ, PRIM, XYZ2, XYZ2 = 8.
  PACK_GIFTAG(q, GIF_SET_TAG(8, 0, 0, 0, GIF_FLG_PACKED, 1), GIF_REG_AD);
  q++;
  // FBA = 0, and this line is the whole reason the mask works on a 16-bit
  // framebuffer. FBA ("alpha correction") makes the GS force the MSB of every
  // alpha it writes to 1 - a convenience for 1-bit-alpha targets, and death to
  // a mask that lives in that very bit: the clear below writes alpha 0 and the
  // GS stored 1, so DATE read SHADOW over the whole raster and every
  // DATE-gated torch pass was discarded. The projected pool simply did not
  // draw in a 16-bit project. WHO set it: ps2sdk's draw_setup_environment
  // programs FBA = 1 for a 16-bit frame PSM (RendererCoreGS::
  // initDrawingEnvironment zeroes it right after that call now); nothing else
  // in this engine touches the register, so re-asserting 0 here (once per
  // frame, the way Path3::clearScreen re-asserts the REPEAT wrap contract)
  // cannot be undone behind our back. begin() carries the same line.
  PACK_GIFTAG(q, GS_SET_FBA(0), GS_REG_FBA_1);
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
  // Rows: DTHE, FRAME, SCISSOR, TEST, ZBUF, RGBAQ, PRIM, XYZ2, XYZ2 = 9.
  // NLOOP counts every register row - a mismatch stalls the GIF forever.
  PACK_GIFTAG(q, GIF_SET_TAG(9, 0, 0, 0, GIF_FLG_PACKED, 1), GIF_REG_AD);
  q++;
  // DITHERING OFF FOR THE WHOLE BRACKET, and this line is what makes the
  // count usable on a console. The project's GS dither (DTHE, the 4x4 DIMX
  // offsets) is meant for the 16-bit picture, and the manual's "16-bit
  // destinations only" reading let it stay armed here - PCSX2 draws the
  // band exactly either way. The real GS does NOT: every +N front face and
  // every -N back face took its own matrix offset, and the pair no longer
  // cancelled to zero. The band came back with residues on alternate rows
  // and every fourth column (read back on a console through debugShowCount:
  // 4,0,4,0 down a column that should be all zero), the resolve turned
  // each residue into a mask bit, and the picture showed it three ways -
  // a one-pixel checkerboard carved out of the pool at 32-bit colour, and
  // at 16-bit the dashed green slivers along every silhouette edge and the
  // dark halo in the volume's shape (the alpha bit reaching the CRTC's
  // flicker filter). Turning the project's dither off cleared all of it
  // in one boot; this turns it off for the count and the resolve only.
  PACK_GIFTAG(q, GS_SET_DTHE(0), GS_REG_DTHE);
  q++;
  // FRAME -> the count band, at the RASTER's stride and SLID by the band's
  // page rows, so screen row bandY0 lands on the band's own row 0. PSMCT32
  // so the page geometry matches the scene z this pass tests against; the
  // stride must be the scene's because z addressing walks FRAME.FBW.
  PACK_GIFTAG(q,
              GS_SET_FRAME(slidBase(bandY0, t.frameWidth) >> 11,
                           t.frameWidth >> 6, countPsm, 0),
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
  // Diagnostic (debugShowCount): write every channel, test nothing - the
  // band's own texels land on screen where the mask would have gone.
  const unsigned fbmsk = debugShowCount ? 0u : kAlphaOnlyFbmsk;
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
  // RGBAQ, PRIM, UV, XYZ2, UV, XYZ2, CLAMP-restore, TEXA-restore,
  // DTHE-restore = 18.
  PACK_GIFTAG(q, GIF_SET_TAG(18, 0, 0, 0, GIF_FLG_PACKED, 1), GIF_REG_AD);
  q++;
  // The count target was a render target a microsecond ago - invalidate the
  // texture cache before sampling it.
  PACK_GIFTAG(q, GS_SET_TEXFLUSH(0), GS_REG_TEXFLUSH);
  q++;
  // Sampled at the band's OWN base with V = y - bandY0, so the texel for
  // screen pixel (x, y) is the count the slid FRAME wrote for it: the count
  // pass addressed pixel (x, y) as slid + pageRow(y), and slid + pageRow(y) ==
  // countAddress + pageRow(y - bandY0) because the slide IS bandY0 worth of
  // page rows. This used to bind TBP0 to the SLID base as well - slide and
  // V-offset both - which for every band but the first reads bandY0 rows
  // BELOW the band (at 32-bit, 512 KB below: the top of the scene z buffer
  // and the post-fx / shadow-map allocations; the 24-bit z's alpha byte is 0
  // and the slots' alpha is what their last render left, so the lower band's
  // resolve ORed garbage or nothing - no shadow ever reached the bottom 256
  // rows of the screen from this pass). Found by reading the address
  // arithmetic, not from a report: a shadow that is missing only below row
  // 256 looks like the FPP torch's own "the shadow hides behind its caster"
  // rule. TBP is in 64-word blocks, FBP in 2048-word pages, hence the
  // different shifts.
  // READ AS 24-BIT WHEN THE BAND IS 32-BIT, and the whole feature hangs off
  // this word. The mask bit is produced by TEXA's AEM expansion below - "an
  // all-zero texel is transparent, anything else is 0x80" - and the GS only
  // EXPANDS an alpha it has to invent: PSMCT16 (1 bit) and PSMCT24 (none).
  // A PSMCT32 texel carries its own alpha byte, TEXA is ignored, and the
  // count pass deliberately writes alpha 0 (the count lives in RGB, so the
  // target's A bit can never trip the resolve). Bound as PSMCT32 the resolve
  // therefore sampled alpha 0 for EVERY pixel, failed the alpha test on all
  // of them, and wrote no mask bit at all: at 32-bit colour the flashlight
  // cast no shadow whatsoever, while the 16-bit band - where AEM does apply -
  // worked, which is why this survived a console pass. PSMCT24 is the same
  // memory with the byte we do not want left out.
  const int resolvePsm = countPsm == GS_PSM_32 ? GS_PSM_24 : countPsm;
  PACK_GIFTAG(q,
              GS_SET_TEX0(countAddress >> 6,
                          t.frameWidth >> 6, resolvePsm, lg2up(countW),
                          lg2up(countH), 1 /* tcc */,
                          debugShowCount ? 0 /* modulate */ : 1 /* decal */,
                          0, 0, 0, 0, 0),
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
  if (debugShowCount)
    PACK_GIFTAG(q, GS_SET_TEST(0, 0, 0, 0, 0, 0, 1, ZTEST_METHOD_ALLPASS),
                GS_REG_TEST_1);
  else
    PACK_GIFTAG(q,
                GS_SET_TEST(DRAW_ENABLE, ATEST_METHOD_NOTEQUAL, 0x00,
                            ATEST_KEEP_ALL, 0, 0, 1, ZTEST_METHOD_ALLPASS),
                GS_REG_TEST_1);
  q++;
  PACK_GIFTAG(q,
              GS_SET_ZBUF(gs->zBuffer.address >> 11, gs->zBuffer.zsm, 1),
              GS_REG_ZBUF_1);
  q++;
  // Diagnostic: MODULATE by 0xFF/0x80 doubles the count texel's brightness
  // (a count of 32 reads as 64 grey - visible); the real pass is DECAL.
  PACK_GIFTAG(q,
              debugShowCount ? GS_SET_RGBAQ(0xFF, 0xFF, 0xFF, 0x80, 0x3F800000)
                             : GS_SET_RGBAQ(0, 0, 0, 0, 0x3F800000),
              GS_REG_RGBAQ);
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
  // The project's dither comes back for the picture (countBegin turned it
  // off; emitRasterRestore does not carry DTHE).
  PACK_GIFTAG(q, GS_SET_DTHE(settings->getDither() ? 1 : 0), GS_REG_DTHE);
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
  const unsigned fbmsk = kAlphaOnlyFbmsk;

  const int w = static_cast<int>(settings->getWidth());
  const int h = static_cast<int>(settings->getRenderHeightF());

  packet2_reset(beginPacket, false);
  qword_t* q = beginPacket->next;
  // NLOOP counts every register row - a mismatch stalls the GIF forever
  // (the shadow-map bracket's warning, and this packet froze a frame at
  // exactly the moment the first volume drew until the count was right).
  // Rows: FBA, FRAME, TEST, ZBUF, RGBAQ, PRIM, XYZ2, XYZ2, ZBUF = 9.
  PACK_GIFTAG(q, GIF_SET_TAG(9, 0, 0, 0, GIF_FLG_PACKED, 1), GIF_REG_AD);
  q++;
  // FBA = 0 - the same line maskClear() carries, for the same reason, and it
  // was MISSING here: ps2sdk's draw_setup_environment programs FBA = 1 for a
  // 16-bit frame PSM (RendererCoreGS::initDrawingEnvironment zeroes it at
  // boot now), and with the MSB of every written alpha forced to 1 the clear
  // below stores SHADOW over the whole raster. The counting path got its
  // re-assert first; the convex path - which is what a 16-bit project runs -
  // kept the bug, so the pool was back for exactly as long as the count
  // target was allowed at 16-bit and gone again the moment it was refused.
  PACK_GIFTAG(q, GS_SET_FBA(0), GS_REG_FBA_1);
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
  const unsigned fbmsk = kAlphaOnlyFbmsk;

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
  const unsigned fbmsk = kAlphaOnlyFbmsk;
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
