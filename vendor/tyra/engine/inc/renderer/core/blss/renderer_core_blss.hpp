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

/**
 * THE PROXY BUDGET, and it is a TWIN SWITCH - one number in two files, moved in
 * the same commit or not at all, exactly like TYRA_BLSS_ACT_TABLE.
 *
 * 0 = a bag is described by up to kMaxProxiesPerBag boxes however small it is
 * on screen (what has shipped so far).
 * 1 = the cap becomes the number of GRID TILES the bag's whole box covers, so a
 * distant terrain chunk landing in four tiles is described by four boxes
 * instead of twenty. RendererCoreBlss::proxyBudget states the exact rule.
 *
 * It changes WHAT THE CONSOLE DESCRIBES, so `bagOf()` / `bagList()` in the
 * editor's src/blsscorpus.cpp must cut identically before this may go to 1 -
 * otherwise the network is trained against a frame description the machine no
 * longer produces, which is the exact failure docs/blss-reconstruction.md
 * exists to prevent and which this feature has already paid for once.
 */
#ifndef TYRA_BLSS_PROXY_BUDGET
#define TYRA_BLSS_PROXY_BUDGET 0
#endif

/**
 * EMITTER BAGS DESCRIBE THEMSELVES - the sixth rule of the twin contract, and
 * the same kind of switch as the two above: one number in two files, moved in
 * the same commit or not at all.
 *
 * 0 = a particle emitter contributes NO proxy at all (what has shipped so far).
 * A billboard bag runs `frustumCulling = None`, so StaPipCore has no package
 * bbox for it, falls back to addBagSphere(modelTranslation, radius 0) and
 * addBag() rejects the empty box at `x1 <= x0`. Nothing describes it.
 * 1 = the bag is described by ONE box: the AABB over the particle centres it is
 * about to submit, grown by the widest quad those centres expand into.
 * RendererCoreBlss::addBagBillboard states the exact rule.
 *
 * Why it matters more than its one line suggests: on `examples/upscaler-lab`
 * the emitters are 71.65 of 72.63 counted coverages (98.7 %) and on
 * `examples/showcase` 14.57 of 15.24 (95.6 %), so with this at 0 the network
 * chooses its kernels over fire, fog and rain ENTIRELY FROM THE GEOMETRY
 * BEHIND THEM. All six channels describe the opaque scene while the picture is
 * mostly particles.
 *
 * It changes WHAT THE CONSOLE DESCRIBES, therefore the network's labels,
 * therefore every published fold table and the shipped resources/blss-default.
 * net. `bagList()` in the editor's src/blsscorpus.cpp (`--emitter-proxy`) must
 * cut identically before this may go to 1, and flipping it is a decision that
 * comes with a refit.
 */
#ifndef TYRA_BLSS_EMITTER_PROXY
#define TYRA_BLSS_EMITTER_PROXY 0
#endif

namespace Tyra {

/**
 * BLSS - "Bieda-Level Super Sampling", the neural upscaler.
 *
 * The 3D scene rasterises into a half-resolution GS render target; a small MLP
 * (kFeatures -> kHidden -> kOutputs, 6 -> 12 -> 3 and 123 weights as shipped,
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
 *
 * PLAIN MODE (configure()'s `network` = false) keeps the raster redirect and
 * the VRAM arithmetic EXACTLY as above and deletes everything between them:
 * the bag feed, the tile accumulators, the reprojection, the feature grid, the
 * MLP and 472 of the grid's 476 vertices, leaving beginScene / endScene / one
 * full-screen textured quad. It is the mode to reach for whenever the trained
 * net asks for nothing, and it draws the identical picture when it does.
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

  /**
   * SIX, and it was eight. Two channels have been deleted, both because
   * `--cv --drop-feature` with a CONTROL said so; the tables are on
   * blss::kFeatures in src/blss.hpp and only the consequences are here.
   *
   * `histAge` (frames since a tile last changed) took the only per-frame STATE
   * this class kept with it - the counters and their prevDepth/prevCover - so
   * the feature grid is a pure function of one frame's bag proxies and the two
   * cameras.
   *
   * `luma` took the one quantity THIS SIDE COULD NOT PRODUCE. A bag hands BLSS
   * a single brightness scalar, and StaPipCore could only fill it for bags with
   * a single colour; every per-vertex-lit mesh a generated game submits read a
   * constant 0.5, while the corpus that trained the weights spread the channel
   * over 0..0.48. Held out it scored +0.43 dB without the channel against +0.41
   * with, against a control worth 0.06 - so it was not neutral, it was mildly
   * harmful, and it cost a whole kMaxTiles accumulator and twelve weights.
   */
  static constexpr int kFeatures = 6;
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

  /**
   * HOW MANY PROXIES ONE SUBMITTED BAG MAY BECOME, and it is the difference
   * between a feature grid that describes the frame and one that reports the
   * same number in every tile.
   *
   * A bag carries ONE screen bbox and ONE w range. A terrain chunk or a floor
   * mesh whose bbox is the whole frame therefore told every tile "fully
   * covered, at my NEAREST depth" - which is how a still `fpp` scene measured
   * `depth=1 grad=1 cover=1` in all 224 tiles and the sky was temporally
   * reconstructed. The corpus never had that problem because it chunks its
   * floors 8x8 and its walls x6 (`kFloorChunks` / `kWallChunks` in
   * src/blsscorpus.cpp) precisely so one bag describes one bounded piece of
   * the world.
   *
   * The engine gets the same granularity for nearly nothing: StaPipCore
   * already holds `StaPipBagPackagesBBox`, one axis-aligned box per
   * maxVertCount/3 vertices, computed and CACHED for frustum classification.
   * The hook walks those instead of the bag's bounding sphere. This cap merges
   * consecutive parts when a mesh has more of them, so the per-bag cost is
   * bounded: at most this many boxes, each 8 corners built from one
   * matrix-vector product plus three column deltas.
   */
  static constexpr int kMaxProxiesPerBag = 32;

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
   * 2,2 or 1,2; sharpen 0..1; debugView 0 = off, 1 = tint by winning kernel,
   * 2 = the feature/output SPREAD instrument (logFeatureSpread(), one line
   * group per second into the game's log, picture untouched).
   *
   * Enables BLSS (1,1 is treated as "off"), sizes and allocates the low-res
   * target, sizes the composite packet and rebuilds the 3D projection at the
   * reduced raster scale. Call ONCE from the game's init, before the first
   * frame uploads any texture - the low-res target lives in the permanent
   * VRAM region under the texture heap's floor.
   *
   * `jitter` (TyraX): the +-1/4-pixel raster jitter that alternates every
   * frame - the temporal-supersampling half of the feature, and the documented
   * cause of the period-2 "bob" (docs/neural-upscaler.md, "The oscillation").
   * Jittered sampling is SUPPOSED to make every frame a different image, and
   * the only thing entitled to fuse the phases back together is the temporal
   * accumulator; when the fill term culls the temporal pass there is nothing
   * left doing it and the alternation reaches the screen. false = a pure
   * spatial upscale, stable by construction. Defaulted so previously generated
   * games keep their historical behaviour.
   *
   * `network` (TyraX): PLAIN MODE - the reduced raster with NO reconstruction
   * at all (docs/neural-upscaler.md, "Plain mode"). false means the whole
   * decision machinery is not merely idle but ABSENT: no bag proxies are fed
   * (StaPipCore asks wantsProxies() and skips the branch outright), no tile
   * accumulators are cleared, no reprojection, no feature grid, no MLP, and the
   * composite is ONE full-screen quad instead of the Gouraud grid.
   *
   * It exists because a trained net very often chooses NOTHING - all three
   * outputs quantise below the deadzone, BLSSFILL reports 1.00 passes, and the
   * composite is already a single bilinear pass - while the frame still pays
   * the whole EE bill to reach that conclusion. Plain mode pays the raster
   * redirect and that one pass and no more, which is what moves the break-even
   * from 13.1 full-screen coverages to 2.6 at an ordinary PAL raster (the
   * numbers are in blssui::fill:: and docs/profiling.md). Read BLSSFILL under
   * debugView 2 before assuming a given project is in that case: on
   * examples/upscaler-lab's own net it is 1.58 passes, all of it temporal.
   *
   * The picture is the base pass and only the base pass, i.e. exactly what a
   * degenerate network already produces. `jitter` is FORCED OFF here and that
   * is not a policy choice made twice: the only thing that can fuse two jitter
   * phases is the temporal pass, and in plain mode there is none - jittered
   * sampling with nothing to fuse it is the period-2 bob and nothing else.
   *
   * `nativeScenes` (TyraX, BLSS per scene): SOME SCENE OF THIS GAME WILL
   * RENDER AT THE FULL RASTER, so the z buffer must cover the display even
   * though this configuration is reduced (RendererCoreGS::setZRasterScale).
   *
   * It is what makes setScene() below free. The z buffer follows the raster,
   * and re-sizing it means vram.reset() + evicting every resident texture -
   * safe exactly once, here, before a single asset is loaded, and ruinous
   * later. Pinning the layout at the WIDEST raster any scene uses decides it
   * once for the whole run: a scene change then flips a flag and a projection
   * and touches no VRAM at all.
   *
   * The price is the z-buffer saving, and only for a game that actually mixes:
   * such a game keeps the low-res colour target as pure overhead (224 KB at
   * 512x448, 2x2) instead of trading it for 672 KB of z. A game whose scenes
   * all agree passes false and is byte-for-byte what it always was.
   */
  void configure(int scaleX, int scaleY, float sharpen, bool temporal,
                 int debugView, bool jitter = true, bool network = true,
                 bool nativeScenes = false);

  /**
   * The PER-SCENE half of configure(), and the one thing in this class that is
   * safe to call at any point in a game's life (docs/neural-upscaler.md,
   * "Per scene").
   *
   * `upscale` false = this scene rasterises straight into the display buffer,
   * with beginScene/endScene/composite becoming no-ops; true = the reduced
   * raster and the reconstruction. `network` picks the MLP or plain mode for
   * this scene, exactly like configure()'s.
   *
   * It touches NO VRAM: no allocation, no eviction, no packet, no re-placement.
   * All it does is flip two flags, republish the projection's raster scale and
   * drop the temporal history (which belongs to the scene being left - after a
   * switch the previous display buffer holds a loading screen). Everything
   * expensive was decided by configure(), which is why that one has to be told
   * about the native scenes up front.
   *
   * Calling it with `upscale` true when configure() never allocated a low-res
   * target is a no-op rather than a fault: the scene keeps rendering natively.
   */
  void setScene(bool upscale, bool network);

  /** The trained weights (123 at the shipped kFeatures = 6, kHidden = 12),
   * emitted into the
   * game as BLSS_NET_* tables by the editor's --blss-emit - so kHidden here
   * must equal blss::kHidden there, or the tables and these arrays disagree
   * about their own length.
   * w1 is [kHidden][kFeatures] row-major, w2 is [kOutputs][kHidden]. Until
   * this is called every weight reads 0, which makes composite() degrade to
   * the plain bilinear pass 1. */
  void setNet(const float* w1, const float* b1, const float* w2,
              const float* b2);

  bool isEnabled() const { return enabled; }
  /** False in PLAIN MODE - see configure()'s `network`. */
  bool usesNetwork() const { return useNet; }
  /**
   * THE ONE QUESTION THE BAG FEED ASKS, and it must be asked instead of
   * isEnabled(). Every addBag* entry point below is already inert in plain
   * mode, but "inert" is not "free": StaPipCore computes a world bounding
   * sphere (two sqrtf) and a texel area per bag before it calls, and that work
   * exists ONLY to describe a frame to a network that is not there. The
   * measured `proxy` term is 2.34 ms of a 4.60 ms bill - by far the largest
   * single item - so the caller's gate has to be this, not enabled.
   */
  bool wantsProxies() const { return enabled && useNet; }
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
   * Screen bbox in OUTPUT pixels, w range over the bbox, and the one material
   * scalar left (the brightness went with the `luma` channel - see kFeatures).
   * Accumulates straight into the tile grid, so there is no bag list and no
   * cap. Inert unless called inside the beginScene/endScene bracket.
   */
  void addBag(float x0, float y0, float x1, float y1, float wNear, float wFar,
              float texDetail);

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
                    const float& texelArea);

  /**
   * THE PROXY THE PIPELINE SHOULD PREFER, and the exact twin of the corpus'
   * `bagOf()` (src/blsscorpus.cpp): an OBJECT-SPACE axis-aligned box, its
   * eight corners carried through `mvp` into clip space, near-clipped along
   * its twelve edges, and reduced to the screen bbox + w range of section 2.
   *
   * Why a box and not the sphere below. A bounding sphere around a floor mesh
   * has a radius of tens of world units, so `wNear = w - radius` collapses to
   * the near-plane clamp and EVERY tile the bag touches reads `depth = 1` and
   * `depthGrad = 1` - confidently wrong, and wrong in the same way in every
   * tile, which is what a constant network output looks like from the
   * outside. The box says where the geometry actually is; the twelve-edge
   * near clip is what keeps a box that straddles the eye from projecting to
   * garbage instead of to the screen border.
   *
   * The corners cost one `mvp * min` plus three scaled matrix columns: clip
   * space is affine in the box's parametric coordinates, so the other seven
   * corners are sums of those. Inert unless called inside the
   * beginScene/endScene bracket, and ignored under a foreign view.
   */
  void addBagBox(const M4x4& mvp, const Vec4& objMin, const Vec4& objMax,
                 const float& texelArea);

  /**
   * THE PROXY FOR A PARTICLE EMITTER - the sixth rule of the twin contract
   * (docs/blss-reconstruction.md section 2), gated by TYRA_BLSS_EMITTER_PROXY
   * at the CALL SITE in StaPipCore so a build with the switch off carries not
   * one extra instruction.
   *
   * A billboard bag hands VU1 one CENTRE per particle plus a 2x2 basis weight
   * qword `(m00, m01, m10, m11)` per particle, and VU1 expands each centre into
   *
   *     corner = C +- (R*m00 + U*m01) +- (R*m10 + U*m11)
   *
   * with the bag's own `right`/`up` carried through the SAME `mvp` as the
   * centres (stapip_billboard_{c,t}_vu1.vclpp). So in the space this function
   * projects from, the union of every quad is contained in the AABB over the
   * centres grown per axis by
   *
   *     e.axis = |R.axis| * (max|m00| + max|m10|)
   *            + |U.axis| * (max|m01| + max|m11|)
   *
   * with the four maxima taken over the bag's own particles. That bound is
   * TIGHT for the ordinary emitter (m01 = m10 = 0, so it is exactly the quad's
   * half-width along R and half-height along U) and conservative by at most
   * sqrt(2) for the one kind that rotates its quads (fog's per-puff swirl).
   * Both passes are over memory the EE has just written, and it is one box, so
   * the whole feed for an emitter is `count` qword pairs and one projection.
   *
   * ONE BOX PER BAG, NOT ONE PER VU1 PACKAGE, and that is a twin decision
   * rather than a saving. Geometry splits per package because both halves can
   * agree on which vertices land in which package - the vertex order is the
   * authored order and it is the same on both machines. A particle pool's
   * order is its SPAWN order, an artefact of a simulation the corpus does not
   * run (it has no dt - see docs/backlog.md), so any sub-bag split would put
   * different particles in each box on each side. The AABB of a SET does not
   * depend on the order of the set, which is exactly what makes one box per
   * bag statable as a rule both halves can meet.
   *
   * `centres` and `params` must both hold `count` entries (the vertex slot and
   * the texture bag's `coordinates` slot of the same bag). `texelArea` is the
   * emitter material's texel count, 0 for an untextured emitter, and is turned
   * into section 2's minification ratio by addBagBox() exactly as for geometry.
   * Inert unless called inside the beginScene/endScene bracket, and ignored
   * under a foreign view.
   */
  void addBagBillboard(const M4x4& mvp, const Vec4* centres, const u32& count,
                       const Vec4* params, const Vec4& right, const Vec4& up,
                       const float& texelArea);

  /**
   * HOW MANY BOXES THIS BAG IS WORTH DESCRIBING WITH - the twin rule, and the
   * only lever left on the proxy feed that is not a micro-optimisation.
   *
   * The rule, in the terms docs/blss-reconstruction.md section 2 uses:
   *
   *   Project the bag's WHOLE object-space AABB through `mvp` exactly as
   *   addBagBox() projects a package box - eight corners, the twelve-edge near
   *   clip, the straddle rule, the screen clamp - and count the tiles of its
   *   tile range, `(tx1 - tx0 + 1) * (ty1 - ty0 + 1)`, with the same clamps and
   *   the same -0.001F addBag() uses. The per-bag proxy cap is that count,
   *   clamped to `1 .. kMaxProxiesPerBag`; a whole box that describes nothing
   *   keeps the full cap. Consecutive parts then merge into `ceil(parts / cap)`
   *   groups exactly as they already do above the fixed cap.
   *
   * Why the tile count is the right budget: a proxy's ONLY effect is on the
   * tiles its screen bbox overlaps, and the grid resolves nothing finer than a
   * tile. Twenty boxes landing in four tiles are summed into those four tiles
   * either way; the extra sixteen buy a slightly tighter per-tile `wNear` and
   * cost sixteen projections plus sixteen tile updates. The fidelity loss is
   * bounded by the mechanism the fixed cap already documents - merging by
   * vertex range can only ENLARGE a box, never move it - so the worst case
   * degrades toward the whole-bag proxy rather than lying about where the
   * geometry is. And the budget is never 0, so no bag stops being described: a
   * rule that could empty a bag would hand its tiles `coverage = 0`, which the
   * network reads as "nothing here".
   *
   * Returns kMaxProxiesPerBag - i.e. changes nothing - when BLSS is off or the
   * call is outside the scene bracket.
   */
  int proxyBudget(const M4x4& mvp, const Vec4& objMin,
                  const Vec4& objMax) const;

 private:
  /**
   * The shared geometry of addBagBox() and proxyBudget(): an object-space AABB
   * through `mvp`, near-clipped, reduced to a CLAMPED screen bbox and a w range
   * written into `out[6]` as {x0, y0, x1, y1, wNear, wFar}. Returns false for a
   * box that describes nothing - wholly behind the eye, wholly off screen, or
   * straddling the eye while still filling the frame.
   *
   * One implementation on purpose: the budget must count the tiles of exactly
   * the box the accumulator would have seen, or the two halves of the rule
   * disagree about what a bag covers.
   */
  bool projectBox(const M4x4& mvp, const Vec4& objMin, const Vec4& objMax,
                  float* out) const;

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
  /** docs section 4: tile stats + reprojection -> features. */
  void buildFeatures();
  /** docs section 5: the MLP, then the corner average of its outputs. */
  void runNet();
  /**
   * THE INSTRUMENT, and it is not temporary this time (debugView >= 2).
   *
   * The corpus can measure its own feature distribution (`--blss-eval
   * --features`); the console never could, so for the whole life of this
   * feature the network was fitted to one distribution and run on another,
   * and nobody could see it. One frame in `logEvery` this prints the SPREAD -
   * min / mean / max over the covered tiles - of all eight input channels and
   * all three outputs, plus the fill the frame actually paid.
   *
   * Read it against the `--blss-eval --features` table, which prints the same
   * channels in the same order, and against `--blss-eval --probe` which takes
   * a BLSSFEAT line back and says where each channel falls in the corpus.
   * A channel pinned at one value, or an output whose min equals its max, is
   * the failure this exists to make visible: no per-tile decision is
   * happening at all.
   */
  void logFeatureSpread();

  qword_t* emitPassState(qword_t* q, int srcVram, int srcBufW, int texW,
                         int texH, bool linear, u64 alpha, bool textured);
  qword_t* emitGrid(qword_t* q, int pass);
  /**
   * PLAIN MODE's whole composite: the base pass as ONE CELL of the grid - four
   * vertices instead of emitGrid(q, 0)'s 476.
   *
   * It is the same picture, and the equality is a property of the packet
   * rather than a hope. Pass 0's UV is u(x) = (x << 4) / scaleX + jitter, a
   * LINEAR function of the output pixel; the grid samples it at every 32-pixel
   * corner and lets the rasteriser interpolate between them, and this samples
   * it at the four screen corners and lets the SAME rasteriser interpolate
   * between those - same PRIM flags, same vertex order, same diagonal, same
   * gradient (an exact 16/scaleX per pixel), same bilinear filter, same region
   * clamp, same opaque blend, same constant vertex colour.
   *
   * Checked rather than argued: on examples/upscaler-lab, parked camera, every
   * emitter hidden, a neural build whose network asks for nothing and a plain
   * build are BYTE-IDENTICAL over 811 426 compared pixels in all nine
   * cross-pairings of three captures each (docs/neural-upscaler.md, "Plain mode
   * draws the same picture").
   */
  qword_t* emitBaseQuad(qword_t* q);
  /** The texture/blend state both composites hand back - see the .cpp. */
  qword_t* emitCompositeRestore(qword_t* q);
  /** 0..255 alpha byte of a pass at a grid corner (docs section 6). */
  u8 cornerAlpha(int pass, int corner) const;

  /**
   * Would this pass draw a single cell? emitGrid already skips a cell whose
   * four corners are all alpha 0; this asks the same question of the whole
   * grid BEFORE the corner UVs are built and before the pass' state block is
   * emitted. Point and sharpen measure 0 % occupancy at the shipped deadzone.
   */
  bool passHasAlpha(int pass) const;
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
  // PLAIN MODE's flag. Defaults true so an embedder calling the six-argument
  // configure() - and every generated game built before plain mode existed -
  // keeps the reconstruction it always had.
  bool useNet = true;
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

  // TyraX: what configure() was ASKED for, kept apart from jitterOn (what is
  // in force). Plain mode forces the jitter off, and with the mode now a
  // per-scene answer the request has to survive a scene that turns the network
  // off - otherwise the first plain scene would silently disable the jitter for
  // every neural scene after it, and the net for those was fitted with it on.
  bool jitterWanted = true;
  bool jitterOn = true;     // TyraX: the kill switch - see configure()
  int phase = 0;            // jitter phase, alternating every frame
  int jitter16X = 0;        // this frame's jitter in 1/16 px (+-4, or 0)
  int jitter16Y = 0;

  // --- the feature-spread instrument (debugView >= 2) -------------------
  int proxies = 0;      // bag proxies accumulated this frame (reset per frame)
  /**
   * TILE UPDATES this frame - the sum over proxies of the tiles each one
   * touched, i.e. how many times addBag()'s inner body ran. `proxies` alone
   * cannot price the feed: a distant package box costs one tile and a near
   * terrain chunk costs sixty, and it is this number, not the proxy count,
   * that FrameProfile::tBlssAccum is proportional to. Reported by BLSSGRID so
   * "ms per tile update" is a division rather than an estimate.
   */
  int proxyTiles = 0;
  /**
   * BOXES PROJECTED this frame - how many times addBagBox() ran, accepted or
   * not. It is NOT `proxies`: a box wholly behind the eye, wholly off screen,
   * or dropped by the straddle rule pays the full eight-corner projection and
   * then returns, so the gap between these two columns is work the frame did
   * to describe nothing. Without it the projection half of the feed cannot be
   * priced at all - `proxies` counts only what survived.
   */
  int proxyCalls = 0;
  int logFrame = 0;     // frames since the last spread line
  // The widest proxy of the frame, by tiles touched - the one that decides
  // whether the feature grid describes anything.
  int worstTiles = 0;
  float worstX0 = 0, worstY0 = 0, worstX1 = 0, worstY1 = 0;
  float worstWNear = 0, worstWFar = 0;
  static constexpr int kLogEvery = 60;  // ~1 s at 60 Hz - readable, not a flood

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
  float detAcc[kMaxTiles];
  float edgeAcc[kMaxTiles];
  float dMin[kMaxTiles];
  float dMax[kMaxTiles];

  // --- per-tile stats + features + outputs -----------------------------
  //
  // FIVE ARRAYS THAT USED TO LIVE HERE ARE GONE. tCover / tDetail / tEdge were
  // written by finishTileStats and then COPIED, unchanged, into feat[][5] / [4]
  // / [3] by buildFeatures; tDepthMin / tDepthMax existed only to be subtracted
  // from each other one function later. finishTileStats writes the three
  // straight into `feat` now and reduces the depth pair to `tGrad` on the spot,
  // which removes a 224-tile pass and 4.4 KB of per-frame working set. Every
  // expression is unchanged and in the same order, so the network's inputs are
  // bit-identical - proved on hardware, not asserted.
  //
  // **It is a simplification, NOT a speed-up, and the measurement says so.**
  // The work does not disappear, it MOVES: `feat` 0.190 -> 0.141 ms and
  // `reproj` 0.275 -> 0.310, for a net +0.014 ms. The pass that was deleted was
  // very nearly free, because 224 floats is 896 bytes and the EE's data cache
  // had no trouble with it - the cache story this comment used to tell was a
  // guess, and it was wrong. See docs/profiling.md, "The last terms in the
  // composite", for the numbers and for why nothing else here is worth taking.
  float tDepth[kMaxTiles];  // depthMean, already 1/w; buildReproj reads it
  // (depthMax - depthMin) * kDepthRef, the tile's own near/far spread - the
  // floor of the depthGrad channel, ready to compare against its neighbours.
  float tGrad[kMaxTiles];
  // [0] motion, [1] depth, [2] depthGrad, [3] edgeDens, [4] texDetail,
  // [5] coverage. finishTileStats fills 1/3/4/5; buildFeatures fills 0 and 2.
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

  // --- the only state that crosses a frame ------------------------------
  // The PREVIOUS camera, for the reprojection of section 3, and nothing else:
  // the per-tile histAge counters that used to live here went with the channel
  // (see kFeatures above).
  Pinhole cur, prev;
  bool hasPrev = false;

  packet2_t* packet = nullptr;
  packet2_t* beginPacket = nullptr;
  packet2_t* endPacket = nullptr;
};

}  // namespace Tyra
