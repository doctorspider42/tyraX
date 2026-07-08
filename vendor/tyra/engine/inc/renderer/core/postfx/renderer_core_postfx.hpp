/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Added by the tyra-editor fork.
*/

#pragma once

#include <packet2.h>
#include <tamtypes.h>
#include "renderer/renderer_settings.hpp"
#include "renderer/core/gs/renderer_core_gs.hpp"

namespace Tyra {

/**
 * Full screen post effects. There are no pixel shaders on the PS2 - both
 * effects are GS framebuffer blits at the end of the frame:
 *
 * - Bloom: the framebuffer is downsampled (bilinear) into a quarter-res
 *   VRAM buffer, softened with a few offset blits, and drawn back over the
 *   frame with additive blending. The bilinear filter is the blur.
 * - Film grain: a small noise texture is drawn over the frame twice
 *   (subtractive, then additive) with different random offsets every frame,
 *   which yields zero-mean grain out of unsigned GS math.
 *
 * Total cost is 8 textured sprites per frame - GS fill only, no EE/VU work.
 */
class RendererCorePostFx {
 public:
  RendererCorePostFx();
  ~RendererCorePostFx();

  void init(RendererSettings* settings, RendererCoreGS* gs);

  /** Bloom strength: 0 = off, 128 = the blurred frame fully re-added. */
  void setBloom(const u8 strength) { bloom = strength; }

  /** Film grain strength: 0 = off, 128 = maximum. */
  void setGrain(const u8 strength) { grain = strength; }

  u8 getBloom() const { return bloom; }
  u8 getGrain() const { return grain; }

  /** True when any effect is active, i.e. apply() will draw something. */
  bool isEnabled() const { return bloom != 0 || grain != 0; }

  /** Called by RendererCore::endFrame() right before the buffer flip. */
  void apply();

 private:
  static constexpr int noiseSize = 64;  // texels, power of two

  RendererSettings* settings;
  RendererCoreGS* gs;
  packet2_t* packet;
  u8 bloom, grain;
  u32 rng;

  int fbW, fbH;      // frame size in pixels
  int lowW, lowH;    // quarter-res working size
  int lowBufW;       // quarter buffer width (aligned to 64)
  int lowVram[2];    // two quarter-res work buffers (word addresses)
  int noiseVram;     // noise texture (word address)

  void uploadNoise();

  /** One textured sprite: src rect (UV in 1/16 texel) -> dst rect (pixels).
   * texW/texH describe the source texture (for TW/TH and region clamping),
   * abe + alpha select the blend equation. */
  qword_t* blit(qword_t* q, int srcVram, int srcBufW, int texW, int texH,
                int u0, int v0, int u1, int v1, int dstVram, int dstBufW,
                int x0, int y0, int x1, int y1, bool linear, bool wrap,
                int abe, u64 alpha);
};

}  // namespace Tyra
