/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Sandro Sobczyński <sandro.sobczynski@gmail.com>
*/

#pragma once

#include <packet2.h>
#include "renderer/renderer_settings.hpp"
#include "./renderer_core_gs_vram.hpp"
#include "../renderer_core_sync.hpp"

namespace Tyra {

class RendererCoreGS {
 public:
  RendererCoreGS();
  ~RendererCoreGS();

  zbuffer_t zBuffer;
  RendererCoreGSVRam vram;

  /**
   * Modified by TyraX: WHERE THE FRAME IS CURRENTLY BEING RASTERISED - the
   * three registers every raster-redirect bracket in this engine moves and
   * then has to put back (FRAME, SCISSOR, XYOFFSET).
   *
   * It exists because "put back" used to mean "point at the display buffer",
   * which is only right when nothing else had redirected first. The env map,
   * the camera feed and the shadow map all run INSIDE the generated
   * renderScene(), which BLSS brackets - so the first of them inside a BLSS
   * bracket cancelled the redirect and the rest of the frame drew
   * full-resolution into the display buffer, with no assert and no visual
   * signature beyond "BLSS did nothing" (docs/neural-upscaler.md).
   *
   * Restoring through getRasterTarget() / emitRasterRestore() instead makes
   * those brackets NEST: they restore whatever was redirected before them.
   * Only RendererCoreBlss publishes a redirect today; one level is all the
   * engine can produce, so this is deliberately a single slot and not a stack.
   */
  struct RasterTarget {
    int frameAddress = 0;  ///< FRAME base, in GS words
    int frameWidth = 0;    ///< FRAME.FBW source width, in pixels
    int scissorX0 = 0, scissorX1 = 0;  ///< SCISSOR, inclusive, window coords
    int scissorY0 = 0, scissorY1 = 0;
    /// XYOFFSET in 1/16 px. Window-CENTRED (the VU1 pipeline's fixed 2048
    /// raster scale expects that), with BLSS' sub-pixel jitter and the
    /// InterlacedField per-field bias already folded in - a nested bracket
    /// that restores a jitter-free offset moves the rest of the scene by a
    /// quarter pixel against the half it already drew.
    int offsetX16 = 0, offsetY16 = 0;
  };

  void init(RendererSettings* settings);

  // Modified by TyraX: runtime display switching (RendererCore::
  // setDisplayOutput). reinit() resets the VRAM allocator and rebuilds the
  // frame/z buffers + video mode for the settings' current display mode -
  // every texture must have been evicted first. reprogramDisplay() only
  // rewrites the display window (widescreen toggle).
  void reinit();
  void reprogramDisplay();

  /**
   * Modified by TyraX (docs/frame-pacing.md): hand the finished frame to the
   * display and take the next drawing buffer.
   *
   * `throttle` is RendererCore's frame limiter. With TWO buffers it is the
   * stock behaviour and the caller has already waited for vsync. With THREE
   * it is this function that paces: it blocks until the queued frame has been
   * latched by the vblank handler, i.e. at most one frame may be in flight
   * ahead of the display. That is the whole point of the feature - a frame
   * that overruns its field by a hair is presented one field late instead of
   * costing a full extra field of EE stall, so 20.4 ms of work on PAL
   * presents at ~49 fps rather than collapsing to 25.
   */
  void flipBuffers(bool throttle);

  /**
   * Modified by TyraX: INTERRUPT CONTEXT - called from the INTC vblank
   * handler, never by game code. Latches the queued buffer into DISPFB and
   * releases the one it replaces.
   *
   * Everything it touches must stay interrupt-safe: `presentFrameBuffer` is
   * GS privileged-register stores and nothing else, and the queue is three
   * scalars ordered so the main thread never needs to disable interrupts
   * (see the comment on pendingBuffer).
   */
  void onVblank();

  /** Frame buffers actually allocated - 3 when triple buffering came up, 2
   * when it was off or the third buffer did not fit in GS VRAM. */
  unsigned int getFrameBufferCount() const { return bufferCount; }

  /**
   * Modified by TyraX (BLSS): lay the permanent frame/z buffers out again
   * WITHOUT touching the video mode - the frame buffers come back at the same
   * addresses, the z buffer at the size the current raster scale asks for, and
   * everything the caller allocated above them has to be re-placed (see
   * RendererCore::rebuildPermanentBuffers). Every texture must have been
   * evicted first, exactly like reinit(). Skipping programDisplay() is the
   * point: graph_set_mode resets the GS and blanks the output, and the display
   * geometry has not changed.
   */
  void reallocateBuffers();

  /** True when the z buffer on the GS was allocated for a different raster
   * scale than the settings now ask for (TyraX fork, BLSS) - i.e. the
   * permanent VRAM region has to be laid out again. */
  bool needsBufferRealloc() const;

  void enableZTests();

  /** Set the GS FOGCOL register (TyraX fork, hardware fog). */
  void setFogColor(const u8& r, const u8& g, const u8& b);

  /**
   * Set the GS ALPHA_1 register - the context-0 blend equation - via PATH3
   * (TyraX fork, reflective materials). The caller must drain in-flight
   * PATH1 rendering first (RendererCoreSync::align3D), or the new equation
   * applies to triangles already queued. Pass a GS_SET_ALPHA(...) value;
   * restore GS_SET_ALPHA(0, 1, 0, 1, 0) (the draw_setup_environment default)
   * when done.
   */
  void setAlpha(const u64& alpha);

  /**
   * The DISPLAY buffer currently being drawn to (TyraX fork, for post fx).
   *
   * This is the double-buffered display target and nothing else - it is NOT
   * "where the frame is being rasterised", which is getRasterTarget(). Post
   * fx, the 2D path and the BLSS composite all run after the 3D scene and
   * genuinely want this one; a raster bracket's end() wants the other.
   */
  framebuffer_t* getCurrentFrameBuffer() { return &frameBuffers[context]; }

  /** Where the frame is currently being rasterised (TyraX fork): the BLSS
   * low-res target while its bracket is open, the display buffer otherwise. */
  RasterTarget getRasterTarget() const;

  /** Open a raster redirect: everything that restores through
   * emitRasterRestore() now puts THIS back (TyraX fork, BLSS). */
  void redirectRasterTo(const RasterTarget& target);

  /** Close the redirect - the display buffer is the target again. */
  void endRasterRedirect();

  bool isRasterRedirected() const { return rasterRedirected; }

  /**
   * Append the register writes that restore the current raster target -
   * FRAME, SCISSOR, XYOFFSET (+ TEXFLUSH first when the bracket rendered
   * something the scene is about to sample) followed by the drawing
   * environment's tests, which is also what re-applies zBuffer.mask.
   *
   * ONE implementation for the env map, the camera feed, the shadow map, the
   * BLSS bracket and post fx, because the failure mode of having five is
   * silent: they were not even consistent on the InterlacedField per-field
   * XYOFFSET bias (see getFieldYOffset16), so a bracket in that mode used to
   * hand the rest of the frame a raster window half a scan line off.
   */
  qword_t* emitRasterRestore(qword_t* q, bool texFlush);

  /**
   * The most recently FINISHED frame buffer (TyraX fork, for BLSS): the
   * previous composited image at full display resolution. RendererCoreBlss
   * samples it as the temporal history, which is why BLSS allocates no
   * history buffer of its own.
   *
   * With two buffers that is simply the other one. With three
   * (docs/frame-pacing.md) the newest finished frame is the one QUEUED for
   * the next vblank when there is one - it is newer than what the TV is
   * scanning right now - and the displayed buffer otherwise. Reading
   * `context ^ 1` here would have handed BLSS the free buffer, i.e. the
   * frame before last or uninitialised VRAM.
   */
  framebuffer_t* getPreviousFrameBuffer() {
    if (bufferCount < 3) return &frameBuffers[context ^ 1];
    const s32 queued = pendingBuffer;
    return &frameBuffers[queued >= 0 ? queued : displayedBuffer];
  }

  /**
   * The per-field XYOFFSET y bias in 1/16 px that true field rendering
   * (DisplayMode::InterlacedField) needs - 8 on odd fields, 0 otherwise, and
   * 0 in every other display mode (TyraX fork; see setXYOffset/flipBuffers).
   * Any raster bracket that writes XYOFFSET itself instead of going through
   * setXYOffset must add it, or static geometry bobs by a scan line at half
   * the field rate.
   */
  int getFieldYOffset16() const;

 private:
  constexpr static float gsCenter = 4096.0F;
  constexpr static float screenCenter = gsCenter / 2.0F;

  // Modified by TyraX (docs/frame-pacing.md): what must survive the third
  // display buffer, in GS words (1 word = 4 bytes, 1 MB = 262 144 words).
  // The reserve is everything RendererCore still allocates permanently after
  // gs.init() - post fx, the env-map and camera-feed targets with their z
  // buffers, and the projected-shadow slots a game may claim later; the
  // texture floor is a floor and not a budget, so a project can still run out
  // the ordinary way and say so through VRAMSTAT.
  constexpr static float kWordsPerMB = 262144.0F;
  constexpr static int kThirdBufferReserveWords = 98304;      // 384 KB
  constexpr static int kThirdBufferMinTextureWords = 65536;   // 256 KB

  RendererSettings* settings;
  // Modified by TyraX: three slots, of which the last is only allocated when
  // triple buffering asked for it AND it fitted (docs/frame-pacing.md).
  static constexpr unsigned int kMaxFrameBuffers = 3;
  framebuffer_t frameBuffers[kMaxFrameBuffers];
  unsigned int bufferCount = 2;
  packet2_t* flipPacket;
  packet2_t* zTestPacket;
  // Modified by TyraX: preallocated ALPHA-register packet (setAlpha
  // runs per reflective mesh per frame - no per-call heap churn).
  packet2_t* alphaPacket;
  u8 context;
  u8 currentField;

  /**
   * Modified by TyraX (docs/frame-pacing.md): the display queue, shared with
   * the vblank interrupt handler.
   *
   * `displayedBuffer` is what the GS is scanning out, `pendingBuffer` is a
   * finished frame waiting for the next vblank (-1 = none). The third,
   * implicit slot is the one being drawn into (`context`), and the three are
   * always distinct while a frame is queued - which is exactly why three
   * buffers are needed to present without stalling.
   *
   * NO INTERRUPT MASKING is needed around this, and that is a property worth
   * keeping: the handler only ever acts when `pendingBuffer >= 0`, so while
   * the main thread has waited for it to reach -1 the handler is inert and
   * `displayedBuffer` cannot move under it. flipBuffers() therefore writes
   * `context` FIRST and `pendingBuffer` LAST - that store is what hands
   * ownership over, and it must not be reordered ahead of the rest.
   */
  volatile s32 pendingBuffer = -1;
  volatile s32 displayedBuffer = 1;
  /** INTC handler id from AddIntcHandler, -1 when not installed. */
  s32 vblankHandlerId = -1;
  // Modified by TyraX (BLSS): the raster redirect currently open, if any.
  RasterTarget redirect;
  bool rasterRedirected = false;
  // Modified by TyraX (BLSS): the raster scale the z buffer was sized for,
  // so a later configure() can tell whether the layout has to be redone.
  int zRasterScaleX = 1;
  int zRasterScaleY = 1;

  void allocateBuffers();
  void allocateVramBuffers();
  void initDrawingEnvironment();
  void initChannels();
  void updateCurrentField();
  // Modified by TyraX: DTV scan modes (480p/1080i) need a custom
  // display window and an unfiltered framebuffer scan-out.
  void programDisplay();
  void setDtvDisplay(int modeX, int modeY, int modeDW, int modeDH, int magH,
                     int magV, bool interlaced);
  void presentFrameBuffer(u8 index);
  qword_t* setXYOffset(qword_t* q, const int& drawContext, const float& x,
                       const float& y);
  // Modified by TyraX (docs/frame-pacing.md): the flip packet - point FRAME
  // at frameBuffers[target] and, in InterlacedField, re-bias XYOFFSET for the
  // field that frame will be scanned in. Shared by both flip paths.
  void emitDrawTargetSwitch(u8 target);
  // Install / tear down the INTC vblank handler that latches DISPFB. Only
  // used with three buffers; with two, RendererCore's graph_wait_vsync is the
  // whole story and no handler is installed.
  void installVblankHandler();
  void removeVblankHandler();
  /** Drop any queued frame and re-arm the queue on `context`. The VRAM
   * rebuild paths (reinit / reallocateBuffers) move every buffer address, so
   * a frame queued against the old layout must not reach DISPFB. */
  void resetDisplayQueue();
};

}  // namespace Tyra
