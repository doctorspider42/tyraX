/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Added by TyraX: analytic two-bone foot IK over the skeletal pose.
*/

#pragma once

#include <tamtypes.h>

#include "math/m4x4.hpp"
#include "./skel_instance.hpp"

namespace Tyra {

/**
 * Which nodes of a SkelModel form the legs, plus the numbers that decide how
 * hard the ground is allowed to pull on them. Authored in the editor
 * (Animation Editor > Foot IK), baked into the generated game as a constant;
 * every id indexes SkelModel::nodes and -1 means "not bound".
 *
 * Lengths are MODEL units - the same space the .tskl's vertices live in -
 * because that is the only scale the rig itself knows. The solver converts
 * to and from world space through the instance's own matrix.
 */
struct FootIkRig {
  enum { kMaxLegs = 4 };  // two for a biped; a quadruped fits as well

  struct Leg {
    s16 hip = -1;    // upper joint (thigh) - the chain's root, never moves
    s16 knee = -1;   // middle joint
    s16 ankle = -1;  // the joint the sole hangs under
    s16 toe = -1;    // optional; it just rides along, but the editor uses it
                     // to measure soleOffset and to draw the rig
  };

  Leg legs[kMaxLegs];
  u8 legCount = 0;
  s16 pelvis = -1;  // lowered when a foot has to reach further down

  /** Ankle height above the sole, so a trace starts at the floor and not
   * inside the ankle. Measured by the editor from the bind pose. */
  float soleOffset = 0.08F;
  /** How far above the clip's own foot height a foot may be lifted (a step
   * up) and how far the pelvis may be lowered (a step down). Both cap the
   * effect rather than the trace - a target beyond them is clamped, which
   * reads as the leg giving up gracefully instead of tearing the mesh. */
  float maxLift = 0.45F;
  float maxDrop = 0.45F;
  /** 0 = the foot keeps the clip's own angle, 1 = it lies flat on the
   * surface it found. Clamped by maxRollDeg either way. */
  float normalBlend = 0.8F;
  float maxRollDeg = 35.0F;
  /**
   * How far the pelvis TIPS toward the lower foot, as a fraction of the angle
   * the two feet actually make. Lowering the hips alone is not enough: with
   * one leg reaching down a step and the other bent under the body, level
   * hips read as a character standing on stilts. This is the term that makes
   * the weight land on a leg. 0 disables it.
   */
  float pelvisTilt = 0.6F;
  float maxTiltDeg = 14.0F;
  /** Critically damped follow rate, 1/s. The ground under a walker is a step
   * function; without this the foot pops on every tread edge. */
  float smoothing = 14.0F;
  /** Trace window around the clip's sole position, world units. */
  float traceUp = 0.6F;
  float traceDown = 1.2F;

  bool valid() const {
    if (legCount == 0 || legCount > kMaxLegs) return false;
    for (u8 i = 0; i < legCount; ++i)
      if (legs[i].hip < 0 || legs[i].knee < 0 || legs[i].ankle < 0)
        return false;
    return true;
  }
};

/**
 * Two-bone foot IK as a SkelInstance pose hook.
 *
 * Per frame, per leg: trace the world for the surface under the clip's own
 * sole position, lower the pelvis by the deepest of those offsets so the legs
 * can reach, then solve hip/knee/ankle analytically (law of cosines, bend
 * plane taken from the pose so the knee keeps bending the way the animator
 * bent it) and roll the foot onto the surface normal.
 *
 * Cost is per JOINT, not per vertex: a biped is two traces and a few dozen
 * flops on the EE, next to ~0.9 ms of skinning for the same instance. What it
 * does cost is pose sharing - see SkelInstance::poseEquals.
 *
 * The solver never moves the character. Its output is what the mesh looks
 * like; where the body IS remains the game's collision answer, so a graph, a
 * trigger and a camera all keep seeing the same world whether IK is on, off
 * or half faded.
 */
class FootIk : public SkelPoseHook {
 public:
  /**
   * The world's answer to "what is under this point".
   * @param world the traced point (the clip's sole position), world space
   * @param up/down how far above/below it to look
   * @param outY world height of the surface found
   * @param outNormal its unit normal, world space, pointing up
   * @return false for "nothing to stand on" - the foot then keeps the clip's
   * own placement, which is the correct answer over a ledge or a pit.
   */
  typedef bool (*GroundFn)(void* user, const float world[3], float up,
                           float down, float* outY, float* outNormal);

  FootIk();

  FootIkRig rig;

  /** The instance's object-to-world matrix for this frame - the same one the
   * renderer submits. Must be set before the pose is evaluated. */
  void setWorld(const M4x4& objectToWorld);
  void setGround(GroundFn fn, void* user) {
    groundFn = fn;
    groundUser = user;
  }
  /** Global 0..1 blend. 0 disables the solver outright (and costs nothing),
   * which is how a game fades IK out for a ragdoll, a cutscene or distance. */
  void setWeight(float w) { weight = w < 0.0F ? 0.0F : (w > 1.0F ? 1.0F : w); }
  float getWeight() const { return weight; }

  /** Advances the per-foot springs. Call once per frame per instance, before
   * the pose is evaluated - never per render pass (a split screen draws the
   * same instant twice and would integrate it twice). */
  void beginFrame(float dt) { frameDt = dt; }

  void modifyPose(SkelInstance& inst) override;

  // --- what the solve found, for the pose corrector and for debug overlays ---
  bool legGrounded(u8 leg) const { return leg < FootIkRig::kMaxLegs && hit[leg]; }
  /** Smoothed vertical correction applied to that foot, world units. */
  float legOffset(u8 leg) const {
    return leg < FootIkRig::kMaxLegs ? offset[leg] : 0.0F;
  }
  float pelvisOffset() const { return pelvisOff; }
  /** Surface normal under that foot, world space (0,1,0 when it missed). */
  const float* legNormal(u8 leg) const {
    return normalW[leg < FootIkRig::kMaxLegs ? leg : 0];
  }

 private:
  GroundFn groundFn = nullptr;
  void* groundUser = nullptr;
  float weight = 1.0F;
  float frameDt = 0.0F;

  float world[16];
  float worldInv[16];
  bool haveWorld = false;

  // per-leg state that survives between frames (the springs)
  float offset[FootIkRig::kMaxLegs];
  float offsetVel[FootIkRig::kMaxLegs];
  float normalW[FootIkRig::kMaxLegs][3];
  float targetW[FootIkRig::kMaxLegs][3];  // absolute world ankle target
  bool hit[FootIkRig::kMaxLegs];
  float pelvisOff = 0.0F;

  void solveLeg(SkelInstance& inst, const FootIkRig::Leg& leg, u8 index,
                const float* modelUpUnit, const float* modelUpRaw);
};

}  // namespace Tyra
