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

// std::function, for onDestroy below. GCC 11 reached it transitively through
// renderer_core.hpp; GCC 15's libstdc++ does not, and the error it gives
// ("'function' in namespace 'std' does not name a template type") points here
// rather than at the missing include.
#include <functional>

#include "renderer/core/renderer_core.hpp"

namespace Tyra {

class Renderer3DPipeline {
 public:
  Renderer3DPipeline() { onDestroy = nullptr; }
  ~Renderer3DPipeline() {}

  virtual void setRenderer(RendererCore* core) = 0;
  virtual void onUse() = 0;
  virtual void onFrameEnd() = 0;
  virtual void onUseEnd() = 0;
  std::function<void(Renderer3DPipeline*)> onDestroy;
};

}  // namespace Tyra
