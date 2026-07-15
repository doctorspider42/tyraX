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

  /** The buffer currently being drawn to (TyraX fork, for post fx). */
  framebuffer_t* getCurrentFrameBuffer() { return &frameBuffers[context]; }

 private:
  constexpr static float gsCenter = 4096.0F;
  constexpr static float screenCenter = gsCenter / 2.0F;

  RendererSettings* settings;
  framebuffer_t frameBuffers[2];
  packet2_t* flipPacket;
  packet2_t* zTestPacket;
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
