/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Added by TyraX: per-object skeletal playback; pose evaluation on
# the EE, vertex skinning on VU0 in macro mode.
*/

#pragma once

// Optional per-instance pose/skin timings; zero instructions in normal builds.
#ifndef TYRA_SKEL_PROFILE
#define TYRA_SKEL_PROFILE 0
#endif

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
 * vertex/normal arrays are overwritten in place every update. Render the
 * arrays (mesh->materials[i]->frames[0]) through StaPip like any static
 * geometry: one vertex upload, no VU1 program swap, EE clipping - and use
 * advance()/ensurePose() to skip the pose+skin work for culled instances
 * (~0.9 ms per 1092-vert instance on real hardware; PCSX2's fast EE hides
 * it). DynamicPipeline rendering still works (one frame = verticesFrom ==
 * verticesTo, interpolation 0) but submits every vertex twice.
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

  /** Modified by TyraX: puts the current clip at `seconds` (wrapped into a
   * looping clip). Instances given bit-identical times share one skinned
   * mesh (poseEquals) - how a crowd of pedestrians walks in a few phase
   * groups instead of skinning everyone. */
  void setTime(float seconds);

  /** Modified by TyraX: frees this instance's own skin outputs (every part,
   * every level). For an instance that has been drawing another's shared
   * pose for a while: a crowd member that skinned itself once (a crossfade,
   * up close) would otherwise hold its buffers - ~0.4 MB at full detail -
   * for good. The next ensurePose allocates and skins again. */
  void trimOutputs();

  /**
   * Advances playback by dt seconds (scale dt for playback speed), then
   * evaluates the pose and skins into the mesh when anything changed.
   * dt = 0 holds the pose (still skins once after construction/play()).
   *
   * @returns true the frame the clip reaches its last frame: once for
   * one-shot clips, on every wrap for looping ones (the animFinished
   * contract of TyraX games).
   */
  bool update(float dt);

  /** Playback bookkeeping only - the expensive pose evaluation and skinning
   * are deferred until ensurePose(). For instances culled away from the
   * camera: time (and the animFinished contract) keeps running, the mesh
   * keeps its last skinned pose. Same return contract as update(). */
  bool advance(float dt);

  /** Evaluates the pose and skins into the mesh if playback moved since the
   * last skin (or the requested LOD differs from the last one skinned).
   * Call before reading the arrays of a visible instance.
   * @param lod 0 = full mesh; higher levels use the decimated variants
   * baked into the .tskl, clamped per part to what the file carries.
   * @returns true when the arrays were rewritten (bbox caches keyed on
   * the buffers should be invalidated), false when everything was current. */
  bool ensurePose(u8 lod = 0);

  /** LOD levels available (including the full mesh: always >= 1). Parts can
   * carry different chain lengths; this is the longest one - lodArrays()
   * clamps per part. */
  u8 lodCount() const { return maxLodLevels; }

  /** The level the out arrays currently hold (last ensurePose target). A
   * renderer throttling pose refreshes must still call ensurePose when the
   * wanted level differs - other levels' buffers hold older skins (or the
   * bind pose before their first ever skin). */
  u8 currentLod() const { return lastSkinnedLod; }

  /** The renderable arrays of one part at one LOD level (clamped to the
   * part's chain). Valid after the matching ensurePose(lod). */
  struct LodArrays {
    Vec4* vertices;
    Vec4* normals;
    Vec4* textureCoords;  // nullptr on untextured parts (shared, read-only)
    u32 count;
  };
  LodArrays lodArrays(size_t part, u8 lod);

  // Implementation detail, public only so the .cpp's file-local repack
  // helper can fill it: one part at one LOD level - bind-pose data repacked
  // into 16-byte-aligned qwords for the VU0 skinning loop (positions w = 1,
  // normals w = 0 so the translation column drops out; weights
  // pre-normalized to sum 1; joints re-sorted per vertex by descending
  // weight with the nonzero count in `influences`, so skinParts dispatches
  // to a blend of exactly 0, 1, 2 or 4 matrices), plus the uvs.
  //
  // Modified by TyraX: this is the SAME for every instance of a model, so it
  // is built once per model and shared (BindCache in the .cpp). It used to be
  // per instance - ~57 bytes a vertex per LOD level, ~1.1 MB for a 4400-
  // triangle generated character - and a crowd of eighteen of them ran the
  // EE out of its 32 MB before the first frame.
  struct PartBind {
    std::vector<Vec4> bindPositions, bindNormals, skinWeights;
    std::vector<u8> sortedJoints, influences;
    std::vector<u32> skinSource;  // first identical corner, always <= this corner
    std::vector<Vec4> uvs;        // packed texture coords (static)
    u32 count = 0;
  };
  // Per instance, per part, per level: the skin output. Allocated the first
  // time this instance skins at this level (ensurePose) - an instance that
  // only ever follows another's pose (poseEquals) or only renders far away
  // never pays for the levels it does not draw.
  struct PartLod {
    const PartBind* bind = nullptr;
    std::vector<Vec4> ownVertices, ownNormals;
    Vec4* outV = nullptr;   // skin destination (empty until first skinned)
    Vec4* outN = nullptr;
    const Vec4* uvPtr = nullptr;  // the shared uvs, nullptr on untextured parts
    u32 count = 0;
  };

  /** True when both instances strike the identical pose this frame (same
   * model, same clip at the same time, no crossfade in flight) - their
   * skinned meshes are interchangeable, so a renderer can skin one and draw
   * it for every instance in the group (with per-instance matrix/color).
   * Instances autoplaying the same clip from scene load stay equal forever;
   * a scripted play()/speed change simply splits the group. */
  bool poseEquals(const SkelInstance& other) const {
    return model == other.model && cur.clip == other.cur.clip &&
           cur.time == other.cur.time && fadeT >= 1.0F && other.fadeT >= 1.0F &&
           overrideCount == 0 && other.overrideCount == 0 &&
           partSkipped == other.partSkipped;
  }

  /**
   * Modified by TyraX: a part that is not drawn (a creator option the
   * character is not wearing) is not skinned either - its arrays keep their
   * last skin. Turning a part back on re-skins on the next ensurePose().
   * Instances skipping different parts never share a pose.
   */
  void setPartSkipped(u32 part, bool skip);

  /**
   * Modified by TyraX: a procedural layer over the clip - a node's local
   * rotation becomes clip rotation * q (q in the node's own animated frame;
   * x, y, z, w). What a face is made of: blinking lids, eyes and a head
   * that follow the player, a jaw that talks, spring bones. Marks the pose
   * dirty; an instance carrying overrides never shares its pose.
   * `replace`: the local rotation is q alone - the clip's rotation of that
   * node is dropped (spring bones: a generated clip carries the panel's
   * leg-driven swing, which the spring recomputes up close).
   */
  void setRotationOverride(u32 node, const float q[4], bool replace = false);

  /** Drops every override (a far instance goes back to sharing its pose). */
  void clearRotationOverrides();

  u32 rotationOverrideCount() const { return overrideCount; }

  /** The node's global (model-space) matrix as of the last evaluated pose -
   * one frame old when read before ensurePose(). Column-major. */
  const M4x4& nodeGlobal(u32 node) const { return globals[node]; }

 private:
  struct Layer {
    s32 clip = -1;
    float time = 0.0F;
    bool loop = false;
    std::vector<u32> cursors;  // per-channel last key index (times ascend)
  };

#if TYRA_SKEL_PROFILE
  u32 profileCount = 0, profilePose = 0, profileSkin = 0;
#endif
  Layer cur, prev;                // prev is only alive during a crossfade
  float fadeDuration = 0.0F;
  float fadeT = 1.0F;             // 0 = all prev, 1 = all cur (fade done)
  bool poseDirty = true;          // initial pose not yet skinned
  bool oneShotDone = false;
  u8 lastSkinnedLod = 0;          // which level the out arrays hold
  u32 overrideCount = 0;          // nodes with a live rotation override
  std::vector<float> overrideRot;   // nodes * 4 (x, y, z, w)
  std::vector<u8> overrideOn;       // per node: 1 = clip * q, 2 = q alone (replace)
  std::vector<u8> partSkipped;      // per part (empty = none skipped)
  u8 maxLodLevels = 1;            // longest per-part chain incl. the base

  // scratch buffers, sized once in the constructor
  std::vector<float> localsCur, localsPrev;  // nodes * 10: t[3] r[4] s[3]
  std::vector<u8> animatedCur, animatedPrev; // node had a channel this pose
  std::vector<M4x4> globals;                 // per node
  std::vector<M4x4> palette;                 // per palette slot

  std::vector<std::vector<PartLod>> partLods;  // [part][lod]
  std::shared_ptr<const SkelBindCache> binds;  // the model's (SkelModel::bindCache)

  void advanceLayer(Layer& layer, float dt);
  void evalLocals(Layer& layer, std::vector<float>& locals,
                  std::vector<u8>& animated);
  void evalPose();
  void skinParts(u8 lod);
};

/** Modified by TyraX: a model's skinning bind data, shared by its instances
 * (see SkelInstance::PartBind). */
struct SkelBindCache {
  std::vector<std::vector<SkelInstance::PartBind>> parts;  // [part][lod]
};

}  // namespace Tyra
