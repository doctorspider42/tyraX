/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022-2022, tyra - https://github.com/h4570/tyrav2
# Licensed under Apache License 2.0
# Sandro Sobczyński <sandro.sobczynski@gmail.com>
*/

#include "info/banner.hpp"
#include "info/version.hpp"
#include <iostream>
#include <cstring>
// Modified by TyraX: for RendererCore2D::SPRITE_SPACE_HEIGHT - the logo is
// centred in the sprite space, not in the framebuffer (see show()).
#include "renderer/core/2d/renderer_core_2d.hpp"
#include "./banner_data.cpp"

namespace Tyra {

Banner::Banner() {}

Banner::~Banner() {}

void Banner::show(Renderer* renderer) {
  auto* bannerData = ___createTyraSplashBanner();

  // Modified by TyraX: 256x128 RGBA logo (resources/tryaX.png), drawn
  // larger and centered (2:1, matching the texture).
  TextureBuilderData tbd;
  tbd.bpp = bpp32;
  tbd.gsComponents = TEXTURE_COMPONENTS_RGBA;
  tbd.width = 256;
  tbd.height = 128;
  tbd.clut = nullptr;
  tbd.data = reinterpret_cast<unsigned char*>(bannerData);

  Sprite sprite;
  // Stretch the texture across the sprite: the default MODE_REPEAT tiles the
  // logo when the sprite is larger than the 256x128 texture.
  sprite.mode = SpriteMode::MODE_STRETCH;
  sprite.size.x = 384;
  sprite.size.y = 192;
  sprite.position.x =
      (renderer->core.getSettings().getWidth() / 2) - (sprite.size.x / 2);
  // Modified by TyraX: the VERTICAL centre is (renderHeight + 448) / 4, which
  // is neither half of getHeight() nor half of the sprite space. Reported as
  // "the TYRAX logo is not in the middle of the screen in HD (a bit low)".
  //
  // RendererCore2D::render() shifts the 448-row authored space up by
  // (renderHeight - 448) / 2, so in a taller mode the sprite space does not
  // end where the framebuffer does: the picture a sprite can occupy runs from
  // sprite row 0 down to (renderHeight + 448) / 2, and its centre is half of
  // that. Both other candidates are measurably wrong in 1080i (448x540):
  // getHeight() / 2 puts the logo 23 rows LOW (the original, what was
  // reported), the sprite space's own 224 puts it 25 rows HIGH (the first
  // attempt at this fix). Measured in PCSX2 by using the debug HUD's known
  // 20-row line pitch as a ruler: 480p wants 224 and 1080i wants 247, and
  // (renderHeight + 448) / 4 is the one expression that gives both.
  //
  // X is unaffected - sprites keep the raw framebuffer width, so getWidth()
  // is the right divisor there.
  sprite.position.y = ((renderer->core.getSettings().getRenderHeightF() +
                        RendererCore2D::SPRITE_SPACE_HEIGHT) /
                       4.0F) -
                      (sprite.size.y / 2.0F);

  auto texture = Texture(&tbd);
  texture.addLink(sprite.id);
  renderer->core.texture.repository.add(&texture);

  // Modified by TyraX: hold the splash for ~2 real seconds instead of 2
  // frames, re-rendering the logo each frame. Re-drawing matters because
  // beginFrame clears the framebuffer; timing off the EE COP0 Count register
  // (294.912 MHz) rather than a frame count keeps it ~2s on both PAL and NTSC.
  // A rendering loop is vsync/frame-limited, so real time tracks wall time (a
  // no-draw busy-wait would race ahead of the display and finish early).
  unsigned int start;
  asm volatile("mfc0 %0, $9" : "=r"(start));
  for (;;) {
    renderer->beginFrame();
    renderer->renderer2D.render(&sprite);
    renderer->endFrame();
    unsigned int now;
    asm volatile("mfc0 %0, $9" : "=r"(now));
    if ((unsigned int)(now - start) >= 589824000u) break;  // ~2.0 s
  }

  renderer->core.texture.repository.removeById(texture.id);
  texture.core->data = nullptr;
  delete[] bannerData;

  std::cout << "\n";
  std::cout << "-----------------------------------------\n";
  std::cout << "        _____        ____   ___\n";
  std::cout << "          |     \\/   ____| |___|\n";
  std::cout << "          |     |   |   \\  |   |\n";
  std::cout << "-----------------------------------------\n";
  std::cout << "Copyright 2022\n";
  std::cout << "Repository: https://github.com/h4570/tyra\n";
  std::cout << "Licensed under Apache License 2.0\n";
  std::cout << "Version: ";
  std::cout << Version::toString().c_str();
  std::cout << "\n";
  std::cout << "-----------------------------------------\n";
  std::cout << "\n";
}

}  // Namespace Tyra