/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Sandro Sobczyński <sandro.sobczynski@gmail.com>
# Modified by TyraX: drain PATH1 before the frame's first sprite
*/

#include "renderer/2d/renderer_2d.hpp"

namespace Tyra {

Renderer2D::Renderer2D() {}
Renderer2D::~Renderer2D() {}

void Renderer2D::init(RendererCore* t_rendererCore) { core = t_rendererCore; }

void Renderer2D::render(const Sprite* sprite) { render(*sprite); }

void Renderer2D::render(const Sprite& sprite) {
  auto* texture = core->texture.repository.getBySpriteId(sprite.id);

  TYRA_ASSERT(
      texture, "Texture for sprite with id: ", sprite.id,
      "Was not found in texture repository! Did you forget to add texture?");

  // Sprites go out over PATH3 with only a GIF-channel wait, racing whatever
  // VU1 is still pushing through PATH1. When the sprite wins, it stamps
  // z = max across its whole rect (transparent margins included) ahead of
  // the late scene triangles, which then z-fail inside it - on real
  // hardware particles flickered out in a rectangle around the HUD
  // crosshair. Drain PATH1 once per frame before the first sprite; gated on
  // VU1 being up, like endFrame's post fx barrier (a pure-2D frame would
  // spin forever on a FINISH that VU1 cannot deliver).
  if (!core->drained3DFor2D) {
    if (core->getPath1()->isVU1Configured()) core->sync.align3D();
    core->drained3DFor2D = true;
  }

  auto texBuffers = core->texture.useTexture(texture);
  core->texture.updateClutBuffer(texBuffers.clut);

  // Modified by TyraX (docs/frame-extrapolation.md): record where 2D landed.
  // The frame warp keeps this region unwarped, because the HUD is pixels in
  // the source image and carrying it along with the world is what makes it
  // double. Derived from what was actually drawn, so no project has to
  // describe its own HUD - and MODE_REPEAT aside, a sprite's drawn size is
  // its texture's unless drawSize overrides it.
  {
    float w = sprite.drawSize.x > 0.0F
                  ? sprite.drawSize.x
                  : (sprite.mode == MODE_REPEAT
                         ? sprite.size.x
                         : static_cast<float>(texture->getWidth())) *
                        sprite.scale;
    float h = sprite.drawSize.y > 0.0F
                  ? sprite.drawSize.y
                  : (sprite.mode == MODE_REPEAT
                         ? sprite.size.y
                         : static_cast<float>(texture->getHeight())) *
                        sprite.scale;
    core->note2dRect(static_cast<int>(sprite.position.x),
                     static_cast<int>(sprite.position.y),
                     static_cast<int>(sprite.position.x + w),
                     static_cast<int>(sprite.position.y + h));
  }

  core->renderer2D.render(sprite, texBuffers, texture);
}

}  // namespace Tyra
