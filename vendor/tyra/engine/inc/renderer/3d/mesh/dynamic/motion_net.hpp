/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Added by TyraX: a small MLP evaluated on VU0, correcting a walk cycle
# from what the ground ahead looks like.
*/

#pragma once

#include <memory>
#include <string>
#include <vector>

#include <tamtypes.h>

#include "math/m4x4.hpp"
#include "./skel_foot_ik.hpp"
#include "./skel_instance.hpp"

namespace Tyra {

/**
 * The feature layout the net is trained against, shared by three parties:
 * the editor's dataset generator, the Python trainer and this runtime.
 * Bump kFeatureVersion whenever the meaning or the order of a slot changes -
 * a .tnet whose version does not match is REFUSED at load, because a net
 * silently fed the wrong columns produces confident nonsense rather than an
 * error.
 */
struct MotionFeatures {
  enum {
    kFeatureVersion = 1,
    kProbeForward = 3,  // sample rows: behind, under, ahead
    kProbeLateral = 3,  // sample columns: left, centre, right
    kProbeCount = kProbeForward * kProbeLateral,
    kMaxLegs = FootIkRig::kMaxLegs,
    /** phase(2) + speed + turn + strafe + probes + per leg (offset, planted) */
    kCount = 5 + kProbeCount + 2 * kMaxLegs
  };
};

/** One fully connected layer. `inPadded` rounds the input up to a multiple of
 * four so the VU0 loop never reads a partial quadword; the pad columns are
 * zero, so they contribute nothing. */
struct MotionLayer {
  u32 inCount = 0;
  u32 inPadded = 0;
  u32 outCount = 0;
  u8 relu = 1;
  const float* weights = nullptr;  // outCount rows of inPadded, 16B aligned
  const float* biases = nullptr;   // outCount
};

/**
 * A trained pose corrector: the weights, plus everything needed to turn its
 * outputs back into a pose. Shared and immutable - one per model asset, like
 * SkelModel; every instance evaluates it with its own inputs.
 *
 * Written by the editor (src/motionnet.cpp writeTnet), read by
 * MotionNetLoader. Keep the two layouts in sync.
 */
struct MotionNetModel {
  u32 featureVersion = 0;
  std::vector<MotionLayer> layers;

  /** Nodes the net writes a rotation delta for, in output order. */
  std::vector<u16> joints;
  /** Radians per unit of raw output - the trainer normalizes its targets, so
   * this is what puts them back into pose space. */
  float outScale = 0.35F;
  /** How far the net may pull playback speed around 1.0 (0 = never). This is
   * the output that changes a STRIDE rather than a pose, and the one no
   * amount of IK can produce. */
  float phaseRateRange = 0.35F;

  // --- how the probe rows are placed, in world units, in the character's
  // own frame (+Z forward). Baked with the net so a retrained net can change
  // its own sampling pattern without a code change. ---
  float probeForward[MotionFeatures::kProbeForward] = {-0.45F, 0.35F, 1.1F};
  float probeLateral[MotionFeatures::kProbeLateral] = {-0.3F, 0.0F, 0.3F};
  /** Heights and offsets are divided by this before they reach the net. */
  float probeScale = 1.0F;
  /** Speed that reads as 1.0 on the speed input. */
  float refSpeed = 3.0F;

  /** Backing store for the aligned weight/bias blocks the layers point into.
   * One allocation for the whole net - PS2 main memory is not the constraint
   * (a 128-unit net is ~50 KB), a fragmented heap is. */
  std::vector<float> storage;
  size_t maxUnits = 0;  // widest layer, for the scratch buffers
};

/** Loader for the TyraX .tnet trained-pose-corrector format. Same rules as
 * TsklLoader: whole file read into memory first (fseek is unreliable over the
 * PS2 host filesystem), host: and cdrom0: both fine, nullptr on a bad file -
 * a missing net must never be fatal, the character simply walks without it. */
class MotionNetLoader {
 public:
  static std::unique_ptr<MotionNetModel> load(const std::string& relativePath);
};

/**
 * The learned pose corrector, as a SkelInstance pose hook.
 *
 * What it is for: foot IK fixes a foot AFTER the clip put it in the wrong
 * place. This runs before that and changes the stride itself - where the
 * swing foot is heading, how the pelvis carries the weight, how far the
 * spine leans, and how fast the clip should be playing - from what the
 * ground AHEAD looks like. That is the half of stair walking a solver
 * cannot reach, because a solver only knows about now.
 *
 * Cost: the whole thing is one matrix-vector product per layer, run on VU0
 * in macro mode next to the EE - the same unit that skins the mesh, and the
 * same reason it is affordable. A 128-unit three-layer net is ~50k MACs,
 * measured in the low hundreds of microseconds. Weights stream straight out
 * of main memory through lqc2, so the net's size is bounded by RAM and not
 * by VU0's 4 KB of data memory.
 *
 * What it deliberately does NOT do: touch the character's position. Its
 * output is pose and playback rate only, so collision, triggers and the
 * camera see exactly the world they saw before - a net that drifts is ugly,
 * never wrong.
 */
class MotionPoseCorrector : public SkelPoseHook {
 public:
  MotionPoseCorrector();

  /** Not owned; nullptr disables the corrector at zero cost. */
  void setModel(const MotionNetModel* m);
  const MotionNetModel* getModel() const { return model; }

  void setWorld(const M4x4& objectToWorld) { world = objectToWorld; }
  /** The same ground callback the IK uses - one query surface for both. */
  void setGround(FootIk::GroundFn fn, void* user) {
    groundFn = fn;
    groundUser = user;
  }
  /** The IK stage that runs after this one; its per-foot state is part of
   * the net's input (what the ground did to the feet LAST frame is the
   * cheapest possible memory of the terrain). Optional. */
  void setFootIk(const FootIk* ik) { footIk = ik; }

  void setWeight(float w) { weight = w < 0.0F ? 0.0F : (w > 1.0F ? 1.0F : w); }
  float getWeight() const { return weight; }

  /**
   * Per-frame inputs the hook cannot work out for itself.
   * @param phase 0..1 through the locomotion clip
   * @param speed planar world speed
   * @param turn yaw rate, radians/second
   * @param strafe lateral speed fraction, -1..1
   * @param facingYaw where the character is pointing, radians
   * @param rootWorld the character's world position (the probe origin)
   */
  void beginFrame(float dt, float phase, float speed, float turn, float strafe,
                  float facingYaw, const float rootWorld[3]);

  void modifyPose(SkelInstance& inst) override;

  /** Playback rate the net asked for this frame, 1.0 = the clip's own. The
   * game multiplies its animation step by this; it is the only output that
   * leaves the pose. */
  float phaseRate() const { return rate; }

  /** The raw feature vector, for the editor's dataset dump and the debug
   * overlay. Valid after beginFrame + the next modifyPose. */
  const float* features() const { return feat; }

 private:
  const MotionNetModel* model = nullptr;
  const FootIk* footIk = nullptr;
  FootIk::GroundFn groundFn = nullptr;
  void* groundUser = nullptr;
  M4x4 world;
  float weight = 1.0F;
  float rate = 1.0F;

  float phase = 0.0F, speed = 0.0F, turn = 0.0F, strafe = 0.0F;
  float yaw = 0.0F, dt = 0.0F;
  float root[3];

  float feat[MotionFeatures::kCount];
  std::vector<float> scratchA, scratchB;
  std::vector<float> smoothed;  // per-output low pass, kills net jitter

  void buildFeatures();
  const float* evaluate();
};

}  // namespace Tyra
