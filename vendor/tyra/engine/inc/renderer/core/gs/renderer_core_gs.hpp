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

  void flipBuffers();

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
   * The OTHER double-buffered frame buffer (TyraX fork, for BLSS): the frame
   * presented last vsync, i.e. the previous composited image at full display
   * resolution. RendererCoreBlss samples it as the temporal history, which is
   * why BLSS allocates no history buffer of its own.
   */
  framebuffer_t* getPreviousFrameBuffer() { return &frameBuffers[context ^ 1]; }

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

  RendererSettings* settings;
  framebuffer_t frameBuffers[2];
  packet2_t* flipPacket;
  packet2_t* zTestPacket;
  // Modified by TyraX: preallocated ALPHA-register packet (setAlpha
  // runs per reflective mesh per frame - no per-call heap churn).
  packet2_t* alphaPacket;
  u8 context;
  u8 currentField;
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
};

}  // namespace Tyra
