/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Sandro Sobczyński <sandro.sobczynski@gmail.com>
# Modified by TyraX: per-bag fogDisabled flag (GS hardware fog opt-out)
# and per-bag additive blend equation (additiveBlendFix, reflective materials)
*/

#pragma once

#include <draw.h>
#include "math/m4x4.hpp"
#include "math/vec4.hpp"
#include "../pipeline_shading_type.hpp"
#include "../pipeline_texture_mapping_type.hpp"
#include "../pipeline_transformation_type.hpp"
#include "../pipeline_z_test.hpp"
#include "./pipeline_info_bag_frustum_culling.hpp"

namespace Tyra {

class PipelineInfoBag {
 public:
  PipelineInfoBag() {
    shadingType = TyraShadingFlat;
    transformationType = TyraMVP;
    textureMappingType = TyraLinear;
    blendingEnabled = true;
    antiAliasingEnabled = false;
    model = nullptr;
    frustumCulling = PipelineInfoBagFrustumCulling_None;
    zTestType = PipelineZTest_Standard;
    fogDisabled = false;
    additiveBlendFix = 0;
    subtractiveBlendFix = 0;
    dynLightPick = true;
    dynLightSkipSlot = -1;
    spotLit = true;
    dateLit = false;
    blssProxy = true;
  }
  ~PipelineInfoBag() {}

  /** Mandatory. Model matrix */
  M4x4* model;

  /** Flat or gouraud */
  PipelineShadingType shadingType;

  /** Linear or nearest */
  PipelineTextureMappingType textureMappingType;

  /** Multiply by model matrix by view-projection or projection matrix */
  PipelineTransformationType transformationType;

  /** Blending texture with color */
  bool blendingEnabled;

  /** Anti-aliasing */
  bool antiAliasingEnabled;

  /** Type of z buffer testing. */
  PipelineZTest zTestType;

  /** Type of frustum culling */
  PipelineInfoBagFrustumCulling frustumCulling;

  /**
   * Opt this bag out of GS hardware distance fog even when
   * RendererCore::setFog is active (e.g. the sky dome, which sits past the
   * fog end distance and would otherwise be painted solid fog color).
   */
  bool fogDisabled;

  /**
   * 0 = the standard alpha-over blend equation (Cs-Cd)*As/128 + Cd.
   * 1..255 = draw this bag with the additive equation Cs*FIX/128 + Cd,
   * FIX = this value (128 = +1.0) - the spherical-environment-map pass of
   * reflective materials and the additive scene-lightmap pass of emissive
   * materials. The equation travels IN-BAND with the mesh's tags
   * (sendObjectData uploads the ALPHA A+D qword and every program emits it),
   * so there is no pipeline barrier and no per-bag cost beyond the extra
   * draw. Consumed by the static pipeline only (StaPipCore::render).
   */
  u8 additiveBlendFix;

  /**
   * Modified by TyraX: like additiveBlendFix, but SUBTRACTIVE -
   * Cv = Cd - Cs*FIX/128, i.e. the equation (0 - Cs)*FIX/128 + Cd with the
   * GS clamping at 0. Rides the same in-band ALPHA qword; wins over
   * additiveBlendFix when both are set. Exists for the flashlight shadow
   * volumes' COUNTING pass: front faces add +N into the dedicated count
   * target, back faces subtract it back, and the pixels the beam cannot
   * reach are exactly the ones left non-zero (docs/flashlight.md).
   */
  u8 subtractiveBlendFix;

  /**
   * Modified by TyraX: opt-out from the per-bag scene-dynamic-light pick
   * (RendererCore::pickDynLight). The color programs light each mesh with
   * ONE light, so a mesh split into several bags (terrain chunks) shows a
   * hard seam wherever neighboring bags pick different lights - such bags
   * set this false and keep only the global flashlight state, while the
   * game paints the scene lights' ground pools as smooth additive patches
   * instead. Also for bags a nearby light must never tint (the sky dome).
   */
  bool dynLightPick;

  /**
   * Modified by TyraX: one scene light this bag must NOT be lit by through
   * the per-vertex slot (an index into RendererCore's dynLights, -1 = none).
   * The generated game's spot-light shadow pass draws a receiver's light a
   * second time, projected per pixel with the volumes carved out of it, and
   * the same lamp reaching the receiver per vertex as well would light it
   * twice and darken only half of it. The torch has spotLit for this; a
   * scene lamp needs its slot named (docs/shadows.md). pickDynLight skips
   * it and picks the next best light - or the torch - for that bag.
   */
  int dynLightSkipSlot;

  /**
   * Modified by TyraX: opt-out from the camera SPOT light (the flashlight)
   * as well - the global one dynLightPick = false falls back to.
   *
   * The spot is a per-VERTEX term, so on a mesh whose vertices are metres
   * apart it is not a cone but a Gouraud diamond across whole cells. The
   * terrain is exactly that mesh (a cell is never finer than one world unit),
   * and the game already draws the beam's real shape there per pixel - the
   * projected pool under it (docs/flashlight.md). Both at once is the worst of
   * the two: a soft ellipse sitting inside a blocky wedge that moves in
   * cell-sized steps. So the terrain takes the light from the pool alone and
   * sets this false; everything small enough to be lit properly keeps it.
   *
   * Nothing else in the frame changes - the scene's own point lights never
   * reached such a bag anyway (that is what dynLightPick = false means).
   */
  bool spotLit;

  /**
   * Modified by TyraX: gate this bag on the destination-alpha shadow mask
   * (RendererCoreAlphaMask): TEST.DATE = 1, DATM = 0, so the GS draws its
   * pixels only where the framebuffer alpha's MSB is 0 - i.e. where the
   * flashlight's shadow volumes did NOT mark shadow. The mask gates LIGHT;
   * nothing ever paints darkness. Meaningless (and off) outside the frames
   * that build the mask.
   */
  bool dateLit;

  /**
   * Modified by TyraX: opt-out from the BLSS neural upscaler's per-tile
   * feature grid (docs/neural-upscaler.md). BLSS describes a frame to its
   * network as a list of screen-space bounding boxes with a depth range
   * (docs/blss-reconstruction.md section 2), and a bag that is a SHELL AROUND
   * THE CAMERA cannot be described that way: the sky dome is a sphere centred
   * on the eye, so every one of its package boxes wraps the near plane, its
   * screen box is the frame by construction and its wNear is the clip
   * constant rather than a measurement. Feeding one to addBag() pins
   * `coverage`, `depth` and `depthGrad` at 1 over every tile it touches - the
   * exact failure 6a4cbead measured and only half-fixed.
   *
   * addBagBox() already rejects a box that straddles the eye AND still fills
   * the frame, but that rule cannot catch a dome CAP: the top patch of the
   * dome straddles the eye and covers only the top band of the screen, which
   * is a perfectly well-formed box describing nothing. The bag is the only
   * thing that knows it is a shell, so the bag says so.
   *
   * Set false for the sky dome, the star field and the sun/moon discs; leave
   * true for everything the player can walk up to. It costs the network
   * nothing: those bags carry no reconstructible detail, and a tile they alone
   * cover reads coverage 0, which is what kMinCoverage already treats as "do
   * nothing here".
   */
  bool blssProxy;
};

}  // namespace Tyra
