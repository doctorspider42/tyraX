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
#include <kernel.h>  // Modified by TyraX: INTC vblank handler (frame pacing)
#include <packet2_utils.h>
#include "debug/debug.hpp"
#include "info/info.hpp"  // Modified by TyraX: the presented-frame counter
#include "renderer/core/gs/renderer_core_gs.hpp"

namespace Tyra {

// Modified by TyraX (docs/frame-pacing.md): the vblank interrupt trampoline.
//
// One renderer exists per game, so a file-scope owner is enough and keeps the
// handler free of any lookup. It is set while the handler is installed and
// cleared before it is removed, so the ISR can never reach a dead object.
static RendererCoreGS* vblankOwner = nullptr;

extern "C" s32 tyraxVblankHandler(s32 cause) {
  (void)cause;
  if (vblankOwner != nullptr) vblankOwner->onVblank();
  return 0;
}

RendererCoreGS::RendererCoreGS() {
  context = 0;
  currentField = 0;
  alphaPacket = nullptr;
  wrapPacket = nullptr;
}

RendererCoreGS::~RendererCoreGS() {
  removeVblankHandler();  // Modified by TyraX: before the object dies
  if (flipPacket) {
    packet2_free(flipPacket);
  }
  if (zTestPacket) {
    packet2_free(zTestPacket);
  }
  if (alphaPacket) {
    packet2_free(alphaPacket);
  }
  if (wrapPacket) {
    packet2_free(wrapPacket);
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
  wrapPacket = packet2_create(4, P2_TYPE_NORMAL, P2_MODE_NORMAL, 0);
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
  // Modified by TyraX (BLSS per scene): the scale the z buffer is sized for is
  // normally the settings' active one, but a game whose scenes differ pins it
  // (setZRasterScale) so the layout is decided once and no scene change can
  // ask for a different one.
  zRasterScaleX = zPinScaleX > 0 ? zPinScaleX : settings->getRasterScaleX();
  zRasterScaleY = zPinScaleY > 0 ? zPinScaleY : settings->getRasterScaleY();
  const int zWidth = static_cast<int>(settings->getWidth() /
                                      static_cast<float>(zRasterScaleX));
  const int zHeight = static_cast<int>(settings->getRenderHeightF() /
                                       static_cast<float>(zRasterScaleY));
  zBuffer.address = vram.allocateBuffer(zWidth, zHeight, zBuffer.zsm);

  // Modified by TyraX (BLSS): the mask is DERIVED from the allocation here,
  // never assigned by a caller. A z buffer smaller than the display raster is
  // only safe while every pass that draws at DISPLAY resolution has its z
  // writes masked, because the GS addresses z at FRAME.FBW stride - a
  // 512-wide pass reaches 512*448 words past ZBP whatever this allocation
  // says, i.e. straight through the texture heap that starts just above it.
  // RendererCoreBlss::configure() used to set the flag itself, one line
  // BEFORE the rebuild it triggers - and the rebuild lands here, where the
  // flag was unconditionally cleared again. The result was z enabled at
  // display resolution over a 256x224 allocation: every frame stamped depth
  // across 458 752..688 128 words, and the first textures allocated (669 696
  // and, fatally, the 8x2 CLUT at 679 936) were inside it. Zeroing a CLUT
  // zeroes its alpha too, so ATEST NOTEQUAL/AREF 0 discarded every fragment
  // and 4-bit palettised geometry vanished completely while 24-bit textures -
  // no CLUT, alpha from TEXA - merely lost texels nobody looked at.
  zBuffer.mask =
      (zWidth < static_cast<int>(settings->getWidth()) ||
       zHeight < static_cast<int>(settings->getRenderHeightF()))
          ? 1
          : 0;
  // Modified by TyraX (BLSS per scene): remember what the derived answer WAS,
  // so the one place that has to put it back after a low-res bracket
  // (RendererCoreBlss::endScene) can ask for it instead of writing a literal.
  zMaskDefault = zBuffer.mask;

  // Modified by TyraX (docs/frame-pacing.md): the third display buffer, and
  // it is allocated LAST on purpose - it is the one allocation here that may
  // be refused, and everything the renderer cannot do without is placed by
  // then, so the fallback costs nothing.
  //
  // "Does it fit" is NOT the question, and asking it that way is a boot
  // crash: at 512x512x32 (Pal576i) three buffers plus z are 1 048 576 words,
  // i.e. EXACTLY the whole 4 MB, so the allocation succeeds and the post-fx
  // buffers right after it get -1 - measured, as `Out of VRAM for post fx
  // buffers` before the first frame. What has to survive is everything the
  // renderer still allocates AFTER this function (post fx ~12 288 words, the
  // env-map target + its z 32 768, the camera feed + its z 32 768, and the
  // projected-shadow slots a game may claim later, ~20 480) plus a texture
  // heap worth having.
  bufferCount = 2;
  if (settings->getFrameBufferCount() >= 3) {
    const int bufferWords = static_cast<int>(
        vram.getSizeInMB(static_cast<int>(frameBuffers[0].width),
                         static_cast<int>(frameBuffers[0].height),
                         frameBuffers[0].psm, GRAPH_ALIGN_PAGE) *
            kWordsPerMB +
        0.5F);
    // Modified by TyraX: the neural upscaler's low-res colour target is NOT in
    // getHeapWords() yet and is not in the reserve either. It is allocated
    // after this function (blss.configure -> this rebuild -> blss.allocate),
    // and the reserve names post fx, the env map, the camera feed and the
    // shadow slots and nothing else - so a BLSS project was being offered the
    // third buffer against space its own render target was about to take.
    // At 512x448 with the 1x2 raster that is 114 688 words, which is the
    // difference between a 576 KB texture heap and a 128 KB one.
    int blssWords = 0;
    if (settings->getRasterScaleX() != 1 || settings->getRasterScaleY() != 1) {
      const int lowW = static_cast<int>(settings->getWidth()) /
                       settings->getRasterScaleX();
      const int lowH = static_cast<int>(settings->getRenderHeightF()) /
                       settings->getRasterScaleY();
      const int lowBufW = -64 & (lowW + 63);  // as RendererCoreBlss sizes it
      blssWords = static_cast<int>(
          vram.getSizeInMB(lowBufW, lowH, GS_PSM_32, GRAPH_ALIGN_PAGE) *
              kWordsPerMB +
          0.5F);
    }
    const int left = vram.getHeapWords() - bufferWords - blssWords;
    if (left < kThirdBufferReserveWords + kThirdBufferMinTextureWords) {
      // Not a warning: a third buffer here boots into an assert or a scene
      // with no textures, so the honest answer is to not take it.
      // One complete sentence per argument: the log writes each on its own
      // line, so an interpolated number splits a phrase in half.
      TYRA_SOFT_ERROR(
          "Triple buffering: not enough GS VRAM at this display mode - "
          "staying double buffered.",
          "Words a third display buffer costs:", bufferWords,
          "Words it would leave:", left,
          "Words the rest of the renderer and the texture heap need:",
          kThirdBufferReserveWords + kThirdBufferMinTextureWords,
          "The interlaced-field display mode halves every buffer and has "
          "room. The buffer count is also in the GS buffers line above.");
    } else {
      frameBuffers[2] = frameBuffers[0];
      frameBuffers[2].address = vram.allocateBuffer(
          frameBuffers[2].width, frameBuffers[2].height, frameBuffers[2].psm);
      if (frameBuffers[2].address >= 0) bufferCount = 3;
    }
  }

  // The queue always starts with the frame being drawn into `context` and a
  // DIFFERENT buffer on screen - programDisplay() presents exactly that one,
  // and the first flip derives the free slot from the pair (3 - shown -
  // finished).
  //
  // Modified by TyraX: `context ^ 1` used to stand for "the other buffer", and
  // it only means that when there are TWO. This function runs a second time in
  // any game that re-lays the permanent region after boot - which today is
  // every neural-upscaler game, because configure() sizes the z buffer from the
  // raster and asks RendererCore::rebuildPermanentBuffers() for the layout - and
  // by then the ~2 s boot banner has flipped ~120 times, so `context` is
  // wherever the 3-buffer rotation left it. Land on 2 and `context ^ 1` is 3:
  // an index one past frameBuffers[]. flipBuffers() then computes
  // 3 - 3 - 2 = -1 and wraps it into a u8, so the rotation runs on 254/3 and
  // both draws and PRESENTS through garbage framebuffer_t's read past the
  // array. Symptom: one of the three presented frames is a display buffer
  // nothing ever drew - a fully black frame with no HUD either, at a third of
  // the field rate, i.e. violent flicker (docs/frame-pacing.md).
  //
  // The clamp above it is the same hole from the other side: a rebuild may come
  // back with FEWER buffers than the one before it (setDisplayOutput to a mode
  // with no room), which leaves `context` naming a buffer that no longer exists.
  if (context >= bufferCount) context = 0;
  displayedBuffer = (context + 1) % bufferCount;
  lastRealBuffer = displayedBuffer;
  pendingBuffer = -1;

  // The handler IS the third buffer's present path, so the two are decided
  // together and on every layout rebuild - a realloc that loses the buffer
  // must also lose the interrupt. Installing is idempotent.
  if (bufferCount >= 3) {
    installVblankHandler();
    if (vblankHandlerId < 0) bufferCount = 2;
  } else {
    removeVblankHandler();
  }

  // Modified by TyraX: the queue's arming is ON this line, and it is not
  // decoration. This function runs again whenever the permanent region is
  // re-laid, and what `context` happens to be when it does is what decided
  // whether a triple-buffered upscaler game presented black frames for a year.
  // Reading it back is the difference between "the rotation is fine" as an
  // argument and as a measurement.
  TYRA_LOG("GS buffers: frame ", static_cast<int>(frameBuffers[0].width), "x",
           static_cast<int>(frameBuffers[0].height), " x",
           static_cast<int>(bufferCount), ", z ", zWidth, "x", zHeight, " at ",
           static_cast<int>(zBuffer.address), ", drawing into ",
           static_cast<int>(context), ", showing ",
           static_cast<int>(displayedBuffer));
}

// Modified by TyraX (BLSS per scene) - see the header.
void RendererCoreGS::setZRasterScale(const int& sx, const int& sy) {
  zPinScaleX = sx < 0 ? 0 : sx;
  zPinScaleY = sy < 0 ? 0 : sy;
}

bool RendererCoreGS::needsBufferRealloc() const {
  // Against the PINNED scale when there is one: that is the whole point of
  // pinning, and comparing the active scale here would report a rebuild on
  // every scene that switches the upscaler on or off.
  const int wantX = zPinScaleX > 0 ? zPinScaleX : settings->getRasterScaleX();
  const int wantY = zPinScaleY > 0 ? zPinScaleY : settings->getRasterScaleY();
  return zRasterScaleX != wantX || zRasterScaleY != wantY;
}

// Modified by TyraX (BLSS): the same VRAM reset reinit() does, minus the video
// mode. See the header for why programDisplay() is deliberately not called.
void RendererCoreGS::reallocateBuffers() {
  resetDisplayQueue();  // Modified by TyraX: every address below moves
  vram.reset();
  allocateVramBuffers();
  // Modified by TyraX: and DISPFB has to move with them. Skipping
  // programDisplay() skips the only thing that ever wrote that register, so the
  // GS went on scanning the address the last flip latched - which for the THIRD
  // buffer is a different address after this call (the z buffer shrank, so
  // everything above it slid down: 602112 -> 458752 on a 448x448 progressive
  // raster at 2x2). That left the television scanning the texture heap until
  // the next flip, and it left `displayedBuffer` describing something that was
  // not on screen. This is register stores only - none of graph_set_mode's GS
  // reset, which is why programDisplay() is still not called here.
  presentFrameBuffer(static_cast<u8>(displayedBuffer));
  initDrawingEnvironment();
}

// Modified by TyraX (docs/frame-pacing.md): drop a queued frame before the
// buffer addresses it names stop being valid. allocateVramBuffers() re-arms
// the queue right after; this only has to make sure no vblank in between
// latches a DISPFB from the layout that is being torn down.
void RendererCoreGS::resetDisplayQueue() {
  pendingBuffer = -1;
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
  // Modified by TyraX: the buffer the queue considers on screen - whichever
  // index allocateVramBuffers() just armed it with, never a literal. Making
  // this and the arming agree is half the fix for the black-frame defect; the
  // other half is that reallocateBuffers(), which does NOT come through here,
  // has to present too.
  presentFrameBuffer(static_cast<u8>(displayedBuffer));
  graph_enable_output();
}

// Modified by TyraX: full display rebuild for a runtime scan-mode
// switch. The VRAM allocator is a bump allocator and the frame/z buffers
// are its first allocations, so resizing them means starting the layout
// over - the caller (RendererCore::setDisplayOutput) evicts all textures
// first and re-inits post fx right after.
void RendererCoreGS::reinit() {
  resetDisplayQueue();  // Modified by TyraX: every address below moves
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

// Modified by TyraX: per-bag texture wrap (see the header). Mirrors setAlpha
// exactly - the CLAMP register is global GS state and the caller owns the
// PATH1 drain around it.
const texwrap_t& RendererCoreGS::repeatWrap() {
  static const texwrap_t repeat = {WRAP_REPEAT, WRAP_REPEAT, 0, 0, 0, 0};
  return repeat;
}

void RendererCoreGS::setTextureWrap(const texwrap_t& wrap) {
  packet2_reset(wrapPacket, false);
  qword_t* q = wrapPacket->base;
  PACK_GIFTAG(q, GIF_SET_TAG(1, 1, 0, 0, GIF_FLG_PACKED, 1), GIF_REG_AD);
  q++;
  PACK_GIFTAG(q,
              GS_SET_CLAMP(wrap.horizontal, wrap.vertical, wrap.minu, wrap.maxu,
                           wrap.minv, wrap.maxv),
              GS_REG_CLAMP_1);
  q++;
  packet2_update(wrapPacket, q);
  dma_channel_wait(DMA_CHANNEL_GIF, 0);
  dma_channel_send_packet2(wrapPacket, DMA_CHANNEL_GIF, true);
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
  packet2_t* packet2 = packet2_create(24, P2_TYPE_NORMAL, P2_MODE_NORMAL, 0);
  packet2_update(packet2, draw_setup_environment(packet2->base, 0, frameBuffers,
                                                 &zBuffer));
  // Modified by TyraX: draw_setup_environment() ends with "Setup whole texture
  // clamping" - it programs GS_REG_CLAMP to CLAMP/CLAMP. Nothing in the 3D
  // pipelines ever writes that register again, so every 3D mesh inherited it,
  // and a mesh whose STs leave 0..1 (the terrain: world position x tile
  // factor) drew one tile at the world origin with the edge texels smeared
  // along both axes everywhere else. REPEAT is the contract here; Path3::
  // clearScreen re-asserts it every frame because the post-fx blits and 2D
  // texture uploads write the same register for their own purposes.
  {
    qword_t* q = packet2->next;
    PACK_GIFTAG(q, GIF_SET_TAG(1, 0, 0, 0, GIF_FLG_PACKED, 1), GIF_REG_AD);
    q++;
    PACK_GIFTAG(q, GS_SET_CLAMP(WRAP_REPEAT, WRAP_REPEAT, 0, 0, 0, 0),
                GS_REG_CLAMP_1);
    q++;
    packet2_update(packet2, q);
  }
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

// Modified by TyraX: the FRAME switch, shared by both flip paths.
void RendererCoreGS::emitDrawTargetSwitch(u8 target) {
  packet2_update(flipPacket,
                 draw_framebuffer(flipPacket->base, 0, &frameBuffers[target]));
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

void RendererCoreGS::flipBuffers(bool throttle, bool synthetic) {
  // Modified by TyraX: every flip is a frame the player is shown, whether a
  // game loop rendered it or the frame warp synthesised it - which is what
  // makes Info::getPresentedFps() a different number from Info::getFps() on
  // an extrapolating game (docs/profiling.md, "The three frame rate
  // counters"). Counted here rather than in RendererCore so both the real and
  // the synthetic present are covered by one line.
  Info::countPresentedFrame();

  // --- Two buffers: the stock path, unchanged. RendererCore::endFrame has
  // already waited for vsync, so presenting here lands in the vertical blank
  // and the EE owns the other buffer the moment this returns.
  if (bufferCount < 3) {
    presentFrameBuffer(context);  // Modified by TyraX (DTV modes)
    if (!synthetic) lastRealBuffer = context;  // Modified by TyraX
    context ^= 1;
    displayedBuffer = context ^ 1;
    emitDrawTargetSwitch(context);
    return;
  }

  // --- Three buffers (TyraX, docs/frame-pacing.md).
  //
  // Nothing here waits for a vsync in order to PRESENT - the interrupt
  // handler does that. What it waits for is a free buffer, i.e. for the
  // previously queued frame to have been latched, which is the pacing: at
  // most one frame in flight ahead of the display. When the frame limiter is
  // off the wait is skipped and a not-yet-shown frame is simply replaced, so
  // "unlimited" still means "render as fast as the EE can" for benchmarking.
  if (throttle) {
    while (pendingBuffer >= 0) graph_wait_vsync();
  }

  // With the queue drained, the handler is inert and displayedBuffer is
  // stable (see the header). The free slot is the third index: 0+1+2 = 3.
  const s32 shown = displayedBuffer;
  const u8 finished = context;
  const s32 free3 = 3 - shown - static_cast<s32>(finished);

  // Modified by TyraX: this arithmetic is only "the third one" while the two
  // inputs are distinct indices of the three, and when that stopped being true
  // it failed SILENTLY and spectacularly - 3 - 3 - 2 = -1 wrapped in the u8
  // cast, so the rotation ran on indices 254 and 3 and both drew and PRESENTED
  // through framebuffer_t's read past the end of the array. One of the three
  // presented frames was then VRAM nothing had ever drawn: a black frame with
  // no HUD either, a third of the time, i.e. violent flicker. The arming in
  // allocateVramBuffers() is what broke the invariant and is what is fixed;
  // this says so out loud instead of computing a garbage index, because a
  // display-buffer rotation that has come apart cannot be diagnosed from the
  // picture (docs/frame-pacing.md, "The black-frame defect").
  if (free3 < 0 || free3 >= static_cast<s32>(bufferCount) || shown == finished) {
    static bool rotationWarned = false;
    if (!rotationWarned) {
      rotationWarned = true;
      TYRA_SOFT_ERROR(
          "Triple buffering: the display rotation lost its third index and the "
          "frame would have been presented from outside the buffer array.",
          "Buffer on screen:", static_cast<int>(shown),
          "Buffer just finished:", static_cast<int>(finished),
          "Buffers allocated:", static_cast<int>(bufferCount));
    }
    displayedBuffer = (finished + 1) % bufferCount;
  }
  const u8 next = static_cast<u8>(
      (3 - displayedBuffer - static_cast<s32>(finished)) % bufferCount);

  // Point FRAME at the free buffer BEFORE queueing the finished one. The
  // draw_finish handshake inside is what makes the queue safe: the GIF is
  // in-order, so when FINISH comes back every triangle of the finished frame
  // has been rasterised and the buffer is genuinely complete. Queueing first
  // would let the handler put a half-drawn frame on screen.
  emitDrawTargetSwitch(next);

  if (!synthetic) lastRealBuffer = finished;  // Modified by TyraX
  context = next;
  pendingBuffer = finished;  // hands ownership to the interrupt handler
}

// Modified by TyraX (docs/frame-pacing.md): INTERRUPT CONTEXT.
void RendererCoreGS::onVblank() {
  const s32 queued = pendingBuffer;
  if (queued < 0) return;
  presentFrameBuffer(static_cast<u8>(queued));
  displayedBuffer = queued;
  pendingBuffer = -1;  // releases the buffer that just left the screen
}

void RendererCoreGS::installVblankHandler() {
  if (vblankHandlerId >= 0) return;
  vblankOwner = this;
  DIntr();
  vblankHandlerId = AddIntcHandler(INTC_VBLANK_S, tyraxVblankHandler, 0);
  if (vblankHandlerId >= 0) EnableIntc(INTC_VBLANK_S);
  EIntr();
  if (vblankHandlerId < 0) {
    vblankOwner = nullptr;
    TYRA_SOFT_ERROR(
        "Triple buffering: could not install the vblank handler - staying "
        "double buffered.");
  }
}

void RendererCoreGS::removeVblankHandler() {
  if (vblankHandlerId < 0) return;
  DIntr();
  DisableIntc(INTC_VBLANK_S);
  RemoveIntcHandler(INTC_VBLANK_S, vblankHandlerId);
  EIntr();
  vblankHandlerId = -1;
  vblankOwner = nullptr;
}

void RendererCoreGS::updateCurrentField() {
  if (*GS_REG_CSR & (1 << 13)) {
    currentField = GRAPH_FIELD_ODD;
    return;
  }

  currentField = GRAPH_FIELD_EVEN;
}

}  // namespace Tyra
