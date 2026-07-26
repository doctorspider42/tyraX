/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Added by the TyraX fork.
*/

#pragma once

#include <packet2.h>
#include <tamtypes.h>
#include "renderer/renderer_settings.hpp"
#include "renderer/core/gs/renderer_core_gs.hpp"

namespace Tyra {

/**
 * Full screen post effects. There are no pixel shaders on the PS2 - all
 * effects are GS framebuffer blits at the end of the frame:
 *
 * - Bloom: the framebuffer is downsampled (bilinear) into a quarter-res
 *   VRAM buffer, softened with a few offset blits, and drawn back over the
 *   frame with additive blending. The bilinear filter is the blur.
 * - Color grading: flat full-screen sprites driven by the GS blender
 *   ((A - B) * C >> 7 + D). Per-channel gain multiplies the frame through
 *   FBMSK-masked Cd * FIX sprites, per-channel lift adds/subtracts a flat
 *   color, and a final alpha-blend sprite mixes the frame toward a constant
 *   color (tint / desaturation approximation).
 * - Film grain: a small noise texture is drawn over the frame twice
 *   (subtractive, then additive) with different random offsets every frame,
 *   which yields zero-mean grain out of unsigned GS math.
 * - Depth of field: the frame is downsampled/softened like bloom, then the
 *   blur is alpha-blended back through full-screen sprites drawn at GS depths
 *   derived from the focus distance, with the ordinary z-test (GEQUAL, z
 *   writes masked) gating them per pixel - only pixels whose scene depth is
 *   beyond each layer's distance take that layer's share of the blur, so the
 *   image sharp/blurred split follows real geometry at zero EE cost.
 *
 * Total cost is a handful of sprites per frame - GS fill only, no EE/VU work.
 */
class RendererCorePostFx {
 public:
  RendererCorePostFx();
  ~RendererCorePostFx();

  void init(RendererSettings* settings, RendererCoreGS* gs);

  /** Bloom strength: 0 = off, 128 = the blurred frame fully re-added. */
  void setBloom(const u8 strength) { bloom = strength; }

  /**
   * Bloom bright-pass threshold: 0 = off (the whole frame glows, the classic
   * soft-focus look), 1..255 = only what is BRIGHTER than this contributes.
   * Implemented as a flat subtract over the downsampled frame - the GS clamps
   * at zero, so darker pixels drop out entirely and bright ones keep the
   * excess. That is what turns a global soft glow into a halo around emissive
   * materials (TyraX "Ke" materials) and specular hits.
   */
  void setBloomThreshold(const u8 level) { bloomThreshold = level; }
  u8 getBloomThreshold() const { return bloomThreshold; }

  /**
   * How far the glow reaches: extra soften iterations over the quarter-res
   * buffer, 1 (the original single 4-tap pass) to 4. Each iteration doubles
   * the tap offsets, so the halo roughly doubles in radius while staying
   * 4 blits - the cheapest way to turn a tight fringe into a real corona.
   * Costs 4 extra GS sprites per extra iteration, no EE work and no VRAM.
   */
  void setBloomSpread(const u8 iterations) {
    bloomSpread = iterations < 1 ? 1 : (iterations > 4 ? 4 : iterations);
  }
  u8 getBloomSpread() const { return bloomSpread; }

  /** Film grain strength: 0 = off, 128 = maximum. */
  void setGrain(const u8 strength) { grain = strength; }

  /**
   * Depth of field: the image blurs progressively from focusDist to
   * focusDist + range (world units from the camera; the projection near/far
   * from RendererSettings convert them to GS depths). strength: 0 = off,
   * 128 = the far image fully replaced by the blur.
   */
  void setDepthOfField(const float focusDist, const float range,
                       const u8 strength) {
    dofFocus = focusDist;
    dofRange = range > 0.01F ? range : 0.01F;
    dof = strength;
  }

  u8 getBloom() const { return bloom; }
  u8 getGrain() const { return grain; }
  u8 getDepthOfField() const { return dof; }

  /**
   * Color grading, applied between bloom and grain. Per channel:
   * out = mix(clamp(in * gain[c] / 128 + lift[c]), mixColor[c], mixAmt / 128)
   * gain: 128 = 1x (range 0..~2x). lift: -255..255 added. mixAmt: 0 = off,
   * 128 = the frame fully replaced by mixColor. Every step clamps to 0..255
   * exactly like the GS blender does.
   */
  void setGrading(const unsigned char gain[3], const short lift[3],
                  const unsigned char mixColor[3], unsigned char mixAmt) {
    for (int i = 0; i < 3; i++) {
      gGain[i] = gain[i];
      gLift[i] = lift[i];
      gMix[i] = mixColor[i];
    }
    gMixAmt = mixAmt > 128 ? 128 : mixAmt;
  }

  /** Back to the untouched frame (neutral grading). */
  void clearGrading() {
    for (int i = 0; i < 3; i++) {
      gGain[i] = 128;
      gLift[i] = 0;
      gMix[i] = 128;
    }
    gMixAmt = 0;
  }

  /**
   * Which effects a given apply() runs. Bloom, color grading and film grain
   * can be composited at different points in the frame (TyraX: the UI
   * Editor screen stack) - e.g. bloom under the HUD, grain over everything.
   * Grading pairs with bloom (it colour-corrects the same scene image).
   */
  enum Pass {
    PassBloom = 1,
    PassGrading = 2,
    PassGrain = 4,
    PassDof = 8,
    PassAll = 15
  };

  /** True when any of the selected effects is active (apply draws something). */
  bool isEnabled(int passes = PassAll) const {
    return (((passes & PassBloom) && bloom != 0) ||
            ((passes & PassGrading) && hasGrading()) ||
            ((passes & PassGrain) && grain != 0) ||
            ((passes & PassDof) && dof != 0 && dofFocus > 0.0F));
  }

  /**
   * Composite the selected effects over the current framebuffer. Called by
   * RendererCore::endFrame() with PassAll (whatever is left), or mid-frame by
   * RendererCore::applyPostFx() with a subset so 2D drawn afterwards stays on
   * top of it.
   */
  void apply(int passes = PassAll);

  // --- Custom full-screen passes (TyraX fork) ------------------------
  // User-authored screen effects (TyraX "custom screen effects") are
  // written the same low-level way as bloom/grain: raw GS blits over the
  // framebuffer. applyCustom() runs one such pass, wrapping it in the exact
  // same GS state setup/teardown + DMA kick apply() uses, so the author only
  // appends primitives and cannot corrupt the rest of the frame. The build
  // callback receives this object (for blit()/flatQuad() + the accessors
  // below), the packet cursor to advance, and an opaque user pointer (the
  // effect's params); it returns the advanced cursor.
  typedef qword_t* (*CustomFxBuild)(RendererCorePostFx& fx, qword_t* q,
                                    void* user);
  void applyCustom(CustomFxBuild build, void* user);

  // Framebuffer being composited (valid inside a build callback). Word address
  // and 64-aligned buffer width, matching FRAME register conventions.
  int currentFbVram() const { return curFbVram; }
  int currentFbBufW() const { return curFbBufW; }
  int screenW() const { return fbW; }
  int screenH() const { return fbH; }

  // The shared animated-noise texture (uploaded once, filmic dark-skewed).
  int noiseTexVram() const { return noiseVram; }
  int noiseTexSize() const { return noiseSize; }

  // Two quarter-res scratch buffers (the bloom working buffers). Transient -
  // a custom pass may use them freely within its own build callback.
  int lowBuf0() const { return lowVram[0]; }
  int lowBuf1() const { return lowVram[1]; }
  int lowResW() const { return lowW; }
  int lowResH() const { return lowH; }
  int lowResBufW() const { return lowBufW; }

  // Advance and return the shared PRNG (for per-frame animated offsets, like
  // the film grain's noise scroll). 64 = noiseSize, so `& (noiseTexSize()-1)`
  // gives a wrapped texel offset.
  u32 nextRand() {
    rng = rng * 1664525u + 1013904223u;
    return rng;
  }

  // One textured sprite: src rect (UV in 1/16 texel) -> dst rect (pixels).
  // texW/texH describe the source texture (for TW/TH and region clamping),
  // abe + alpha select the blend equation. Public so custom passes can blit.
  // z: sprite depth for the pass's z-test (GEQUAL, writes masked); the
  // default passes everywhere - depth of field uses real thresholds.
  qword_t* blit(qword_t* q, int srcVram, int srcBufW, int texW, int texH,
                int u0, int v0, int u1, int v1, int dstVram, int dstBufW,
                int x0, int y0, int x1, int y1, bool linear, bool wrap,
                int abe, u64 alpha, u32 z = 0xFFFFFFFFu);

  // One untextured full-screen sprite: flat RGBAQ color, blended over the
  // frame by `alpha`; `fbmsk` bits protect framebuffer bits from the write
  // (per-channel gain masks everything but its channel). Public for custom
  // passes (flat tint / fade / lift / mix). w/h default to the framebuffer
  // size; pass the low-res extent to cover a quarter-res scratch buffer
  // instead (the bloom bright-pass does).
  qword_t* flatQuad(qword_t* q, int dstVram, int dstBufW, u32 fbmsk, u8 r,
                    u8 g, u8 b, u8 a, u64 alpha, int w = -1, int h = -1);

  // TyraX portals: the in-place through-view mask. The destination scene
  // renders FULL-RES into the real framebuffer right after the frame clear,
  // scissored to the portal quad's screen bbox; the GS has no stencil, so
  // the "shaped opening" is carved with reversed-z tricks afterwards:
  //   portalMaskBegin - scissor the frame to the bbox and z-clear it (an
  //     earlier portal's z-cap must not reject this view's geometry where
  //     bboxes overlap; call before submitting the destination view).
  //   portalMaskEnd   - re-far the bbox z (the destination depths must not
  //     confuse the main scene), cap the quad interior at the surface depth
  //     (z-only triangle fan, ALWAYS - walls in front still win GEQUAL over
  //     the view, the wall behind loses, and DoF/particles see a solid
  //     surface), repaint the still-far ring around the opening with the
  //     clear color (GEQUAL at z=0 hits exactly the pixels the reset left
  //     at far - the spilled destination pixels outside the quad), then
  //     restore scissor/tests.
  // xy = screen pixels (pairs), z = 24-bit GS depths of the quad plane.
  // Call through RendererCore::portalViewBegin/End, which drain PATH1.
  void portalMaskBegin(int x0, int y0, int x1, int y1);
  void portalMaskEnd(const float* xy, const u32* z, int count, u8 clearR,
                     u8 clearG, u8 clearB);

 private:
  static constexpr int noiseSize = 64;  // texels, power of two

  RendererSettings* settings;
  RendererCoreGS* gs;
  packet2_t* packet;
  u8 bloom, grain;
  u8 bloomThreshold;  // bright-pass cut, 0 = the whole frame blooms
  u8 bloomSpread;     // soften iterations, 1 = the original tight blur
  u8 dof;         // depth-of-field strength, 0 = off
  float dofFocus; // sharp up to this camera distance (world units)
  float dofRange; // full blur reached at dofFocus + dofRange
  u8 gGain[3];   // per-channel multiplier, 128 = 1x
  s16 gLift[3];  // per-channel offset, -255..255
  u8 gMix[3];    // mix target color
  u8 gMixAmt;    // 0..128 mix amount
  u32 rng;

  bool hasGrading() const {
    return gGain[0] != 128 || gGain[1] != 128 || gGain[2] != 128 ||
           gLift[0] != 0 || gLift[1] != 0 || gLift[2] != 0 || gMixAmt != 0;
  }

  int fbW, fbH;      // frame size in pixels
  int lowW, lowH;    // quarter-res working size
  int lowBufW;       // quarter buffer width (aligned to 64)
  int lowVram[2];    // two quarter-res work buffers (word addresses)
  int noiseVram;     // noise texture (word address)
  int curFbVram;     // framebuffer of the pass in flight (custom-pass accessors)
  int curFbBufW;

  void uploadNoise();

  /** The color grading sprites (gain -> lift -> mix), see setGrading(). */
  qword_t* gradingQuads(qword_t* q, int fbVram, int fbBufW);
};

}  // namespace Tyra
