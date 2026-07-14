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
  sprite.position.y =
      (renderer->core.getSettings().getHeight() / 2) - (sprite.size.y / 2);

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