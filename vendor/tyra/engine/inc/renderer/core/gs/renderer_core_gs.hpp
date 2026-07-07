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

  void flipBuffers();

  void enableZTests();

  /** The buffer currently being drawn to (tyra-editor fork, for post fx). */
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
  qword_t* setXYOffset(qword_t* q, const int& drawContext, const float& x,
                       const float& y);
};

}  // namespace Tyra
