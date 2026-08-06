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

#include <draw_types.h>
#include <draw_buffers.h>
#include "./sprite_mode.hpp"
#include "math/vec2.hpp"
#include "renderer/models/color.hpp"

namespace Tyra {

class Sprite {
 public:
  Sprite();
  ~Sprite();

  u32 id;
  Vec2 position, size, offset;
  // Modified by TyraX: destination size, per axis. 0 = the stock behaviour
  // (size * scale). It exists because the framebuffer is not the same shape in
  // every scan mode - 512x448 interlaced, 448x448 in 480p, 448x540 in 1080i -
  // so UI authored in one logical space has to be drawn into a DIFFERENT sized
  // rect per mode, and the two axes do not scale by the same factor. `scale` is
  // a single float and MODE_REPEAT reads `size` as the source rect, so neither
  // could express that (docs/menu-styles.md "Resolutions").
  Vec2 drawSize;
  float scale;
  Color color;
  SpriteMode mode;
  bool flipHorizontal, flipVertical;
  // Modified by TyraX: additive blend (Cs*As + Cd) instead of alpha-over -
  // for light-like overlays (lens flares, glows) that must never darken.
  bool additive;

 private:
  void setDefaultColor();
};

}  // namespace Tyra
