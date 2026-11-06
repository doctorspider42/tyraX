#pragma once
// Private rig (Claude): one VRAM frame grab per cycle, near the end of the
// parked window, written to host:shot_c<cycle>_a<arm>.tga. A copy of the
// game's writeFrameCapture (src/templates.cpp) without the devkit plumbing.
#include <stdio.h>
#include <string.h>
#include <kernel.h>
#include <screenshot.h>
#include "debug/cld_probe.hpp"
#include "engine.hpp"
#include "file/file_utils.hpp"
namespace CldShot {
inline void grab(Tyra::Engine* engine, const char* name) {
  engine->renderer.core.synchronizeFrame();
  framebuffer_t* fb = engine->renderer.core.gs.getPreviousRealFrameBuffer();
  if (!fb || fb->width == 0 || fb->height == 0 || fb->width > 1024) return;
  const unsigned int w = fb->width, h = fb->height;
  FILE* f = fopen(Tyra::FileUtils::fromCwd(name).c_str(), "wb");
  if (!f) return;
  unsigned char header[18];
  memset(header, 0, sizeof(header));
  header[2] = 2;
  header[12] = (unsigned char)(w & 255U);
  header[13] = (unsigned char)((w >> 8) & 255U);
  header[14] = (unsigned char)(h & 255U);
  header[15] = (unsigned char)((h >> 8) & 255U);
  header[16] = 32;
  header[17] = 8;
  fwrite(header, 1, sizeof(header), f);
  static unsigned int lineIn[1024] __attribute__((aligned(16)));
  static unsigned int lineOut[1024];
  unsigned int refused = 0;
  engine->renderer.core.sync.align2D();
  for (unsigned int y = 0; y < h; ++y) {
    if (!ps2_screenshot(lineIn, fb->address / 64, 0, (h - 1U) - y, w, 1, fb->psm)) ++refused;
    FlushCache(0);
    if (fb->psm == 2) {
      const unsigned short* in = (const unsigned short*)lineIn;
      for (unsigned int x = 0; x < w; ++x) {
        const unsigned int r = (unsigned int)((in[x] & 31U) << 3);
        const unsigned int g = (unsigned int)(((in[x] >> 5) & 31U) << 3);
        const unsigned int b = (unsigned int)(((in[x] >> 10) & 31U) << 3);
        lineOut[x] = 0xFF000000U | (r << 16) | (g << 8) | b;
      }
    } else if (fb->psm == 1) {
      const unsigned char* in = (const unsigned char*)lineIn;
      for (unsigned int x = 0; x < w; ++x, in += 3)
        lineOut[x] = 0xFF000000U | ((unsigned int)in[0] << 16) | ((unsigned int)in[1] << 8) | (unsigned int)in[2];
    } else {
      const unsigned char* in = (const unsigned char*)lineIn;
      for (unsigned int x = 0; x < w; ++x, in += 4)
        lineOut[x] = 0xFF000000U | ((unsigned int)in[0] << 16) | ((unsigned int)in[1] << 8) | (unsigned int)in[2];
    }
    fwrite(lineOut, 1, w * 4U, f);
  }
  fclose(f);
  printf("CLDSHOT %s %ux%u psm=%u refused=%u\n", name, w, h, (unsigned)fb->psm, refused);
  fflush(stdout);
}
inline void maybe(Tyra::Engine* engine) {
  if (Cld::state == 1 && Cld::stateFrame == 590 && Cld::cycle < 12) {
    char name[48];
    snprintf(name, sizeof(name), "shot_c%d_a%d.tga", Cld::cycle, Cld::arm);
    grab(engine, name);
  }
}
}  // namespace CldShot
