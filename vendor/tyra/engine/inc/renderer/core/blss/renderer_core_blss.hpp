/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Added by TyraX: BLSS, the neural upscaler (docs/neural-upscaler.md).
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
 * BLSS - "Bieda-Level Super Sampling", the neural upscaler.
 *
 * The 3D scene rasterises into a half-resolution GS render target; a small MLP
 * (kFeatures -> kHidden -> kOutputs, 8 -> 12 -> 3 and 147 weights as shipped,
 * trained on the host and baked into the game)
 * decides per 32x32 output tile HOW that image should be blown up to the
 * display buffer and how much of the previous frame to reuse. The three outputs
 * ride into the GS as VERTEX ALPHA on a Gouraud-shaded triangle-strip grid, so
 * the rasteriser's own interpolation is the upsampling of the weight field.
 *
 * The exact arithmetic is a CONTRACT shared with the host trainer
 * (`src/blss.cpp` in the editor) and written down in
 * `docs/blss-reconstruction.md`. The network is fitted against the hardware
 * formula - truncating shifts, 8-bit clamps, the two-triangle interpolation of
 * the grid - so a drift between the two sides does not merely make the net
 * inaccurate, it makes it optimise the wrong objective. Every formula below
 * cites its section of that page. Change one side and you change both.
 *
 * Frame order in a generated game:
 *
 *   renderer.beginFrame(camera);            // clears the DISPLAY buffer
 *   core.blss.beginScene(clearColor);       // raster -> low-res target
 *   ... the whole 3D scene (bags feed addBag() through StaPipCore) ...
 *   core.blss.endScene();                   // raster -> display buffer
 *   core.blss.composite();                  // features + MLP + 1..5 passes
 *   ... HUD / 2D / post fx, all at FULL resolution ...
 *   renderer.endFrame();
 *
 * Costs, when enabled: one lowW x lowH PSMCT32 VRAM target (224 KB at
 * 512x448 / 2x2) and one EE packet - and it PAYS FOR ITSELF, because the z
 * buffer shrinks to the raster size at the same time (672 KB back at 2x2, see
 * RendererCoreGS::allocateVramBuffers). Nothing at all when it is off - VRAM
 * and the packet are allocated by configure().
 */
class RendererCoreBlss {
 public:
  /** Decision tile edge, in OUTPUT pixels (docs "Symbols": kTile = 32). */
  static constexpr int kTile = 32;
  /** 512 / 32 = 16 tile columns is the widest mode. */
  static constexpr int kMaxCols = 16;
  /** ceil(540 / 32) = 17 tile rows is the tallest mode (HiDef1080i). */
  static constexpr int kMaxRows = 17;
  static constexpr int kMaxTiles = kMaxCols * kMaxRows;
  static constexpr int kMaxCorners = (kMaxCols + 1) * (kMaxRows + 1);

  static constexpr int kFeatures = 8;
  static constexpr int kHidden = 12;
  static constexpr int kOutputs = 3;  // wA (point), wC (temporal), wD (sharpen)

  /** Depth normalisation reference in world units (docs section 4). */
  static constexpr float kDepthRef = 8.0F;

  /**
   * The temporal pass's ceiling as a GS alpha byte, twinned with
   * src/blss.hpp's kTemporalMax. 115 is ~0.9 retention of an exponential
   * accumulator (the history is the previous frame's own composite); at the
   * old 64 the time constant is about one frame and the picture bobs.
   */
  static constexpr float kTemporalMax = 115.0F;

  /**
   * THE INFERENCE DEADZONE, in GS alpha bytes - twinned with src/blss.hpp's
   * kDeadzoneAlpha, where the measured sweep lives.
   *
   * A logistic output cannot say zero: where the oracle asks for nothing the
   * trained net asks for 0.02, which is alpha 2 - invisible in the picture and
   * A WHOLE FULL-SCREEN PASS here, because emitGrid draws a cell as soon as
   * ONE of its four corner alpha bytes is non-zero. runNet() therefore snaps
   * any output whose alpha byte would be at most this to exactly 0.
   */
  static constexpr float kDeadzoneAlpha = 8.0F;

  RendererCoreBlss();
  ~RendererCoreBlss();

  /** Called by RendererCore - dependency wiring, plus a re-place of the VRAM
   * target after a display-mode switch (which resets the whole VRAM map). */
  void init(RendererSettings* settings, RendererCoreGS* gs,
            RendererCoreSync* sync, Path1* path1, RendererCore3D* core3D);

  /**
   * How configure() asks the renderer to lay the permanent GS VRAM region out
   * again (RendererCore wires this to its own rebuildPermanentBuffers).
   *
   * It exists because the z buffer's SIZE follows the raster scale - with BLSS
   * on the scene never rasterises at display resolution, so a 512x448 z
   * reserves 672 KB nobody addresses - and the z buffer is the third thing
   * RendererCoreGS allocates, long before a generated game's init() gets to
   * call configure(). A plain function pointer rather than an owning pointer
   * back to RendererCore: RendererCore holds this object BY VALUE, so the
   * include would be circular, and this keeps the dependency one-way.
   */
  using VramRebuildFn = void (*)(void* user);
  void setVramRebuild(VramRebuildFn fn, void* user) {
    vramRebuild = fn;
    vramRebuildUser = user;
  }

  /**
   * Build-time config from the generated game's init(). scaleX/scaleY are
   * 2,2 or 1,2; sharpen 0..1; debugView 0 = off, 1 = tint by winning kernel.
   *
   * Enables BLSS (1,1 is treated as "off"), sizes and allocates the low-res
   * target, sizes the composite packet and rebuilds the 3D projection at the
   * reduced raster scale. Call ONCE from the game's init, before the first
   * frame uploads any texture - the low-res target lives in the permanent
   * VRAM region under the texture heap's floor.
   */
  void configure(int scaleX, int scaleY, float sharpen, bool temporal,
                 int debugView);

  /** The trained weights (147 at the shipped kHidden = 12), emitted into the
   * game as BLSS_NET_* tables by the editor's --blss-emit - so kHidden here
   * must equal blss::kHidden there, or the tables and these arrays disagree
   * about their own length.
   * w1 is [kHidden][kFeatures] row-major, w2 is [kOutputs][kHidden]. Until
   * this is called every weight reads 0, which makes composite() degrade to
   * the plain bilinear pass 1. */
  void setNet(const float* w1, const float* b1, const float* w2,
              const float* b2);

  bool isEnabled() const { return enabled; }
  int getLowResW() const { return lowW; }
  int getLowResH() const { return lowH; }

  /**
   * Raster redirect + jitter + clear. Brackets ONLY the 3D scene.
   *
   * Drains PATH1, writes the window-centred XYOFFSET the VU1 pipeline expects
   * (2048 - lowW/2, 2048 - lowH/2) PLUS this frame's +-4/16 jitter, then
   * FRAME/SCISSOR/ZBUF for the low-res target and a clear sprite. No-op when
   * BLSS is off.
   *
   * It also PUBLISHES the redirect on the GS (redirectRasterTo) and unmasks
   * z writes, which is what makes the env-map / camera-feed / shadow-map
   * brackets inside the scene nest instead of cancelling it - see
   * RendererCoreGS::RasterTarget.
   */
  void beginScene(const Color& clearColor);

  /** Close the redirect: FRAME/SCISSOR/ZBUF/XYOFFSET back to the display
   * buffer, and z writes masked again (the z buffer only covers the low-res
   * raster, so nothing after this may write scene depth). */
  void endScene();

  /** The 1..5 Gouraud grid passes into the display buffer (docs section 6).
   * Runs the per-tile features and the MLP first - the bag feed of this frame
   * is what it reads. Must be called after endScene(), before any 2D. */
  void composite();

  /**
   * Fed by the engine's own pipeline, not by generated games: one submitted
   * static-pipeline bag as the EE sees it (docs section 2 - `BagProxy`).
   * Screen bbox in OUTPUT pixels, w range over the bbox, and the two material
   * scalars. Accumulates straight into the tile grid, so there is no bag list
   * and no cap. Inert unless called inside the beginScene/endScene bracket.
   */
  void addBag(float x0, float y0, float x1, float y1, float wNear, float wFar,
              float texDetail, float luma);

  /**
   * The form the static pipeline's hook actually calls (TyraX addition on top
   * of the documented API): a bag's WORLD bounding sphere - the very sphere
   * StaPipCore::render already computes for the dynamic-light pick. Projects
   * the centre with the view-projection, derives the screen bbox from the
   * sphere's screen radius and wNear/wFar = w -+ radius, turns `texelArea`
   * (the bound texture's width * height, 0 when untextured) into the section-2
   * minification proxy `clamp(sqrt(texelArea / screenArea) / 4, 0, 1)`, and
   * forwards to addBag().
   *
   * It lives here rather than in the pipeline so the 2048 VU1 raster scale and
   * the BLSS raster-scale factors stay in one place, and so a mid-frame
   * projection swap (pushEnvView / pushPortalView) cannot feed it a foreign
   * camera. Inert unless called inside the beginScene/endScene bracket.
   */
  void addBagSphere(const Vec4& worldCenter, const float& worldRadius,
                    const float& texelArea, const float& luma);

 private:
  /** A pinhole camera - the only form reprojection needs (docs section 3). */
  struct Pinhole {
    float pos[3] = {0.0F, 0.0F, 0.0F};
    float right[3] = {1.0F, 0.0F, 0.0F};
    float up[3] = {0.0F, 1.0F, 0.0F};
    float fwd[3] = {0.0F, 0.0F, 1.0F};
    float tanHalfFovX = 0.577F;
    float tanHalfFovY = 0.433F;
  };

  void allocate();
  void updateGeometry();
  void capturePinhole();
  /** docs section 2: the accumulators -> per-tile stats. */
  void finishTileStats();
  /** docs section 3: per grid corner, in history-buffer texels. */
  void buildReproj();
  /** docs section 4: tile stats + reprojection + histAge -> features. */
  void buildFeatures();
  /** docs section 5: the MLP, then the corner average of its outputs. */
  void runNet();
  void updateHistAge();

  qword_t* emitPassState(qword_t* q, int srcVram, int srcBufW, int texW,
                         int texH, bool linear, u64 alpha, bool textured);
  qword_t* emitGrid(qword_t* q, int pass);
  /** 0..255 alpha byte of a pass at a grid corner (docs section 6). */
  u8 cornerAlpha(int pass, int corner) const;
  /**
   * The three weight -> alpha-byte scales, in output order (wA, wC, wD):
   * 128, kTemporalMax, sharpen * 128. One definition, because cornerAlpha()
   * quantises with them and runNet()'s deadzone divides by them - three
   * hardcoded numbers would drift the moment `sharpen` or kTemporalMax moved,
   * and a drifting deadzone is a silent twin divergence. Twinned with
   * blss::alphaScales() in src/blss.hpp.
   */
  void alphaScales(float out[kOutputs]) const;

  RendererSettings* settings = nullptr;
  RendererCoreGS* gs = nullptr;
  RendererCoreSync* sync = nullptr;
  Path1* path1 = nullptr;
  RendererCore3D* core3D = nullptr;
  VramRebuildFn vramRebuild = nullptr;
  void* vramRebuildUser = nullptr;

  bool enabled = false;
  bool allocated = false;
  bool inScene = false;
  int scaleX = 1, scaleY = 1;
  float sharpen = 0.0F;
  bool temporal = true;
  int debugView = 0;

  int outW = 0, outH = 0;   // PHYSICAL display buffer size
  int lowW = 0, lowH = 0;   // the low-res render target
  int lowBufW = 0;          // 64-aligned FRAME/TEX buffer width of the target
  int lowVram = 0;
  int cols = 0, rows = 0;   // tile counts
  int cornerCols = 0, cornerRows = 0;

  int phase = 0;            // jitter phase, alternating every frame
  int jitter16X = 0;        // this frame's jitter in 1/16 px (+-4)
  int jitter16Y = 0;

  // Projection scale captured at beginScene: a world offset d at view depth w
  // spans d * projScale / w OUTPUT pixels. Captured so addBag() cannot be
  // fooled by a mid-frame pushEnvView/pushPortalView projection swap.
  float projScaleX = 0.0F, projScaleY = 0.0F;

  // --- the network -----------------------------------------------------
  float w1[kHidden][kFeatures] = {};
  float b1[kHidden] = {};
  float w2[kOutputs][kHidden] = {};
  float b2[kOutputs] = {};

  // --- per-tile accumulators (docs section 2), reset by beginScene ------
  float coverAcc[kMaxTiles];
  float depthAcc[kMaxTiles];
  float lumaAcc[kMaxTiles];
  float detAcc[kMaxTiles];
  float edgeAcc[kMaxTiles];
  float dMin[kMaxTiles];
  float dMax[kMaxTiles];

  // --- per-tile stats + features + outputs -----------------------------
  float tCover[kMaxTiles];
  float tDepth[kMaxTiles];  // depthMean, already 1/w
  float tLuma[kMaxTiles];
  float tDetail[kMaxTiles];
  float tEdge[kMaxTiles];
  float tDepthMin[kMaxTiles];
  float tDepthMax[kMaxTiles];
  float feat[kMaxTiles][kFeatures];
  float outW_A[kMaxTiles];  // wA per tile
  float outW_C[kMaxTiles];  // wC per tile
  float outW_D[kMaxTiles];  // wD per tile

  // --- per-corner fields (what the grid vertices carry) -----------------
  float cornerA[kMaxCorners];
  float cornerC[kMaxCorners];
  float cornerD[kMaxCorners];
  float cornerDu[kMaxCorners];
  float cornerDv[kMaxCorners];

  // --- recurrent state --------------------------------------------------
  u8 histAge[kMaxTiles];
  float prevDepth[kMaxTiles];
  float prevCover[kMaxTiles];
  Pinhole cur, prev;
  bool hasPrev = false;

  packet2_t* packet = nullptr;
  packet2_t* beginPacket = nullptr;
  packet2_t* endPacket = nullptr;
};

}  // namespace Tyra
