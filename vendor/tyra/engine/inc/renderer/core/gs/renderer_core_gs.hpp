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

  void init(RendererSettings* settings);

  // Modified by TyraX: runtime display switching (RendererCore::
  // setDisplayOutput). reinit() resets the VRAM allocator and rebuilds the
  // frame/z buffers + video mode for the settings' current display mode -
  // every texture must have been evicted first. reprogramDisplay() only
  // rewrites the display window (widescreen toggle).
  void reinit();
  void reprogramDisplay();

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
   * Set the GS CLAMP_1 register - the context-0 texture wrap mode - via
   * PATH3 (TyraX fork). Same contract as setAlpha: drain in-flight PATH1
   * rendering first (RendererCoreSync::align3D), or the new mode applies to
   * triangles already queued, and put REPEAT back when the bag is done.
   * Path3::clearScreen asserts REPEAT at the top of every frame, so 3D can
   * rely on it; only a texture that explicitly asked for clamping (the
   * render targets - camera feeds, the raytraced mirror) needs this.
   */
  void setTextureWrap(const texwrap_t& wrap);

  /** The wrap mode 3D is guaranteed at the start of every frame. */
  static const texwrap_t& repeatWrap();

  /** The buffer currently being drawn to (TyraX fork, for post fx). */
  framebuffer_t* getCurrentFrameBuffer() { return &frameBuffers[context]; }

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
  // Modified by TyraX: preallocated CLAMP-register packet (setTextureWrap
  // brackets every clamped bag - two calls per such mesh per frame).
  packet2_t* wrapPacket;
  u8 context;
  u8 currentField;

  void allocateBuffers();
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
