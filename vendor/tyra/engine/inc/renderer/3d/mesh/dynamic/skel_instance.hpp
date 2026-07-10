/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Added by tyra-editor: per-object skeletal playback; pose evaluation on
# the EE, vertex skinning on VU0 in macro mode.
*/

#pragma once

#include <memory>
#include <vector>

#include "loaders/3d/tskl_loader/tskl_loader.hpp"
#include "./dynamic_mesh.hpp"

namespace Tyra {

/**
 * One scene object's view of a SkelModel: playback state (with crossfade),
 * per-frame pose evaluation (EE) and matrix-palette skinning (VU0 macro
 * mode - runs on COP2 next to the EE, no microprogram involved).
 *
 * The skinned result lives in an owned single-frame DynamicMesh whose
 * vertex/normal arrays are overwritten in place every update - render it
 * through the ordinary DynamicPipeline; with one frame the pipeline submits
 * verticesFrom == verticesTo (interpolation 0), so no new VU1 code is
 * involved. Set the mesh's translation/rotation/scale like any other mesh.
 */
class SkelInstance {
 public:
  explicit SkelInstance(const SkelModel* t_model);
  ~SkelInstance();

  const SkelModel* model;

  /** Single-frame output mesh (owned). Materials carry the part colors in
   * `ambient` and fresh ids - (re)link textures against these ids. */
  std::unique_ptr<DynamicMesh> mesh;

  /**
   * (Re)starts a clip from its first frame.
   * @param fadeSeconds > 0 crossfades from whatever pose is currently
   * showing (linear blend of local transforms, nlerp on rotations).
   */
  void play(u32 clip, bool loop, float fadeSeconds = 0.0F);

  /** Live loop-flag change without restarting (a finished one-shot resumes
   * wrapping on the next update, like DynamicMeshAnimation did). */
  void setLoop(bool loop) { cur.loop = loop; }

  /**
   * Advances playback by dt seconds (scale dt for playback speed), then
   * evaluates the pose and skins into the mesh when anything changed.
   * dt = 0 holds the pose (still skins once after construction/play()).
   *
   * @returns true the frame the clip reaches its last frame: once for
   * one-shot clips, on every wrap for looping ones (the animFinished
   * contract of tyra-editor games).
   */
  bool update(float dt);

 private:
  struct Layer {
    s32 clip = -1;
    float time = 0.0F;
    bool loop = false;
    std::vector<u32> cursors;  // per-channel last key index (times ascend)
  };

  Layer cur, prev;                // prev is only alive during a crossfade
  float fadeDuration = 0.0F;
  float fadeT = 1.0F;             // 0 = all prev, 1 = all cur (fade done)
  bool poseDirty = true;          // initial pose not yet skinned
  bool oneShotDone = false;

  // scratch buffers, sized once in the constructor
  std::vector<float> localsCur, localsPrev;  // nodes * 10: t[3] r[4] s[3]
  std::vector<u8> animatedCur, animatedPrev; // node had a channel this pose
  std::vector<M4x4> globals;                 // per node
  std::vector<M4x4> palette;                 // per palette slot
  std::vector<Vec4*> outVertices, outNormals;  // per part, into mesh frame 0

  // bind-pose data repacked per part into 16-byte-aligned qwords for the
  // VU0 skinning loop: positions carry w = 1, normals w = 0 (so the
  // translation column drops out of the transform), and weights are
  // pre-normalized to sum 1 (all-zero rows stay zero - the vertex collapses
  // to the origin exactly like the EE loop used to produce). Joints and
  // weights are re-sorted per vertex by descending weight with the nonzero
  // count in `influences`, so skinParts dispatches to a blend of exactly
  // 0, 1, 2 or 4 matrices instead of always paying for 4.
  std::vector<std::vector<Vec4>> bindPositions, bindNormals, skinWeights;
  std::vector<std::vector<u8>> sortedJoints, influences;

  void advanceLayer(Layer& layer, float dt);
  void evalLocals(Layer& layer, std::vector<float>& locals,
                  std::vector<u8>& animated);
  void evalPose();
  void skinParts();
};

}  // namespace Tyra
