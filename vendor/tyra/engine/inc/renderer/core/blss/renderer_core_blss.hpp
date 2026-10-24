/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Added by TyraX: BLSS, the reduced-raster upscaler (docs/upscaler.md).
*/

#pragma once

#include <packet2.h>
#include "renderer/renderer_settings.hpp"
#include "renderer/core/gs/renderer_core_gs.hpp"
#include "renderer/core/renderer_core_sync.hpp"
#include "renderer/core/paths/path1/path1.hpp"
#include "renderer/core/3d/renderer_core_3d.hpp"
#include "renderer/models/color.hpp"

namespace Tyra {

/**
 * BLSS - "Bieda-Level Super Sampling": draw part of the frame into a reduced
 * GS render target and blow it back up with one bilinear pass.
 *
 * It has two shapes, chosen per scene (docs/upscaler.md):
 *
 * WHOLE SCENE. The 3D scene rasterises into the low-res target and composite()
 * draws it into the display buffer as one textured quad. The z buffer shrinks
 * with the raster (RendererCoreGS::allocateVramBuffers), which more than pays
 * for the colour target at 2x2. Depth of field, portals and split view need
 * display-resolution depth and are refused by codegen.
 *
 *   renderer.beginFrame(camera);
 *   core.blss.beginScene(clearColor);   // raster -> low-res target
 *   ... the whole 3D scene ...
 *   core.blss.endScene();               // raster -> display buffer
 *   core.blss.composite();              // one bilinear pass
 *   ... HUD / 2D / post fx at FULL resolution ...
 *
 * PARTICLES ONLY. The opaque scene keeps the full raster and the full z
 * buffer; only the particle emitters - the layer whose overdraw dominates the
 * fill of a smoke/haze/rain scene - draw into the low-res target:
 *
 *   ... opaque 3D at full resolution ...
 *   core.blss.beginParticles();  // z and colour downsampled into low-res
 *                                // targets, raster redirected there
 *   ... every emitter bag ...
 *   core.blss.endParticles();    // composited back where particles drew
 *
 * beginParticles() runs two quarter-area GS copies: the scene DEPTH, read as a
 * PSMZ texture and written point-sampled into a low-res z target of the same
 * format, and the scene COLOUR, box-filtered into the low-res colour target
 * with alpha 0. The emitters then draw there, depth-tested against the copy and
 * with FBA = 1, so every pixel they touch carries alpha >= 0x80 and every pixel
 * they did not keeps 0. endParticles() turns that into an exact 0 / 0x80 mask
 * (a DATE pass at 32-bit; TEXA does it for free at 16-bit) and composites the
 * low-res target over the display buffer with Cs*As + Cd*(1-As): untouched
 * pixels keep their full-resolution value, and the edge of the particle layer
 * fades over one low-res texel instead of stepping.
 *
 * What it costs: one low-res colour target AND one low-res z target in VRAM
 * (448 KB at 512x448x32, 2x2), the full-size z stays; on the GS two
 * quarter-area copies plus one full-screen blend; nothing on the EE beyond the
 * packet. What it does not do: anything under a particle is the quarter-area
 * version of the scene, so geometry seen through dense smoke loses
 * resolution. That is the trade.
 */
class RendererCoreBlss {
 public:
  RendererCoreBlss();
  ~RendererCoreBlss();

  /** Called by RendererCore - dependency wiring, plus a re-place of the VRAM
   * targets after a display-mode switch (which resets the whole VRAM map). */
  void init(RendererSettings* settings, RendererCoreGS* gs,
            RendererCoreSync* sync, Path1* path1, RendererCore3D* core3D);

  /**
   * How configure() asks the renderer to lay the permanent GS VRAM region out
   * again (RendererCore wires this to its own rebuildPermanentBuffers). The z
   * buffer's size follows the raster scale and was allocated before a
   * generated game's init() gets to call configure().
   */
  using VramRebuildFn = void (*)(void* user);
  void setVramRebuild(VramRebuildFn fn, void* user) {
    vramRebuild = fn;
    vramRebuildUser = user;
  }

  /**
   * Build-time config from the generated game's init(), ONCE, before any
   * texture is loaded: it re-lays the permanent VRAM region and evicts.
   *
   * scaleX/scaleY are 2,2 or 1,2 (1,1 is "off" - nothing allocated).
   * `nativeScenes`: some scene renders at the full raster, so z must cover the
   * display for the whole run (RendererCoreGS::setZRasterScale) and setScene()
   * stays free. `particles`: some scene uses the particles-only shape, which
   * needs the full z AND reserves the low-res z target next to the colour one.
   * The first scene's shape is whatever setScene() says next; configure()
   * leaves the whole-scene shape on when `particles` is false.
   */
  void configure(int scaleX, int scaleY, bool nativeScenes = false,
                 bool particles = false);

  /**
   * The per-scene half: `upscale` false = native, true = reduced; `particles`
   * picks the particles-only shape for this scene. Touches no VRAM - it flips
   * flags and republishes the projection's raster scale. Asking for a shape
   * configure() did not allocate for renders natively instead.
   */
  void setScene(bool upscale, bool particles = false);

  /** True when the WHOLE scene is reduced (beginScene/endScene are live). */
  bool isEnabled() const { return enabled; }
  /** True when the particles-only shape is live for this scene. */
  bool isParticleMode() const { return particleOn; }
  /** Either shape - the frame runs synchronous GS brackets. */
  bool isActive() const { return enabled || particleOn; }
  int getLowResW() const { return lowW; }
  int getLowResH() const { return lowH; }

  /** Whole scene: redirect + clear. No-op unless isEnabled(). */
  void beginScene(const Color& clearColor);
  /** Whole scene: close the redirect. No-op unless isEnabled(). */
  void endScene();
  /** Whole scene: the one bilinear pass. No-op unless isEnabled(). */
  void composite();

  /**
   * Particles only: downsample depth and colour, redirect the raster to the
   * low-res target and shrink the VU1 screen scale to match it
   * (RendererCoreDepth::screenScaleX) - the projection stays native, so the
   * billboard cull keeps its native threshold. Must run after every opaque
   * draw of the frame's MAIN view and before the first emitter bag. No-op
   * unless isParticleMode().
   */
  void beginParticles();
  /** Particles only: drain, mask, composite, restore raster and screen scale. */
  void endParticles();

 private:
  void allocate();
  void updateGeometry();
  void sendPacket(packet2_t* p);

  qword_t* emitBlitState(qword_t* q, int dstVram, int dstBufW, int dstPsm,
                         u32 dstMask, int srcVram, int srcBufW, int srcPsm,
                         int texW, int texH, bool linear, bool srcAlpha,
                         u64 alpha);
  qword_t* emitQuad(qword_t* q, int dstW, int dstH, int u0, int v0, int u1,
                    int v1, bool blend, u8 alpha);
  qword_t* emitCompositeRestore(qword_t* q);

  RendererSettings* settings = nullptr;
  RendererCoreGS* gs = nullptr;
  RendererCoreSync* sync = nullptr;
  Path1* path1 = nullptr;
  RendererCore3D* core3D = nullptr;
  VramRebuildFn vramRebuild = nullptr;
  void* vramRebuildUser = nullptr;

  bool enabled = false;     // whole-scene shape live
  bool particleOn = false;  // particles-only shape live
  bool wantDepth = false;   // configure() reserved the low-res z target
  bool allocated = false;
  bool inScene = false;
  bool inParticles = false;
  int scaleX = 1, scaleY = 1;

  int outW = 0, outH = 0;  // PHYSICAL display buffer size
  int lowW = 0, lowH = 0;  // the low-res render target
  // The framebuffer's pixel format (PSMCT32, or PSMCT16 in a 16bpp project);
  // the low-res target is allocated in it, and the low-res z in the matching
  // PSMZ format so the two share page geometry.
  int fbPsm = 0;
  int zPsm = 0;
  int lowBufW = 0;  // 64-aligned FRAME/TEX buffer width of the targets
  int lowVram = 0;
  int lowZVram = -1;

  packet2_t* packet = nullptr;
};

}  // namespace Tyra
