/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Added by tyra-editor: loader for .tskl skeletal-animation models.
*/

#pragma once

#include <memory>
#include <string>
#include <vector>
#include <tamtypes.h>

#include "math/m4x4.hpp"

namespace Tyra {

/** One node of the model's hierarchy with its bind-pose local transform. */
struct SkelNode {
  s32 parent = -1;  // -1 = root
  u8 hasMatrix = 0; // 1 = use `matrix` (such nodes are never animated)
  float t[3] = {0.0F, 0.0F, 0.0F};
  float r[4] = {0.0F, 0.0F, 0.0F, 1.0F};  // x, y, z, w quaternion
  float s[3] = {1.0F, 1.0F, 1.0F};
  M4x4 matrix;  // column-major, valid when hasMatrix
};

/** One matrix-palette slot: global(node) * ibm skins the verts bound to it. */
struct SkelJoint {
  u32 node = 0;
  M4x4 ibm;  // inverse bind matrix, column-major
};

/** One keyframe track: a node's translation / rotation / scale over time. */
struct SkelChannel {
  u32 node = 0;
  u8 path = 0;  // 0 translation, 1 rotation, 2 scale
  u8 step = 0;  // 1 = STEP interpolation (hold left key), 0 = linear
  std::vector<float> times;  // seconds from clip start, ascending
  std::vector<float> vec;    // path 0/2: keyCount * 3
  std::vector<s16> quat;     // path 1: keyCount * 4, component * 32767
};

struct SkelClip {
  std::string name;
  float duration = 0.0F;  // seconds; 0 = static pose
  std::vector<SkelChannel> channels;
};

/** A decimated variant of a part's mesh (same layout, fewer triangles).
 * Baked by the editor (v2 files, Preferences > Rendering > Mesh LOD). */
struct SkelLod {
  u32 vertexCount = 0;
  std::vector<float> positions;
  std::vector<float> normals;
  std::vector<float> uvs;
  std::vector<u8> joints;
  std::vector<u8> weights;
};

/** One draw batch in bind pose (flat triangle list) with palette bindings. */
struct SkelPart {
  std::string name;
  std::string texturePath;  // relative to the ELF cwd, "" = untextured
  float color[4] = {1.0F, 1.0F, 1.0F, 1.0F};
  u32 vertexCount = 0;
  std::vector<float> positions;  // vertexCount * 3
  std::vector<float> normals;    // vertexCount * 3
  std::vector<float> uvs;        // vertexCount * 2 (textured parts only)
  std::vector<u8> joints;        // vertexCount * 4 palette slots
  std::vector<u8> weights;       // vertexCount * 4; skinning divides by sum
  std::vector<SkelLod> lods;     // [0] ~50% verts, [1] ~25%; may be empty
};

/**
 * A skeletal model: shared, immutable data every SkelInstance samples from.
 * Written by the tyra-editor build (src/glbparser.cpp writeTskl) - keep the
 * binary layout in sync with it.
 */
struct SkelModel {
  std::vector<SkelNode> nodes;
  std::vector<u32> order;  // parents-first traversal (computed at load)
  std::vector<SkelJoint> palette;
  std::vector<SkelClip> clips;  // >= 1
  std::vector<SkelPart> parts;
  // Pose AABB, union over every clip (sampled by the baker; files written
  // before 2026-07 carry the clip-0 t=0 box). Used for box collision and,
  // padded, for whole-instance frustum culling.
  float min[3] = {0.0F, 0.0F, 0.0F};
  float max[3] = {0.0F, 0.0F, 0.0F};
};

/**
 * Loader for the tyra-editor .tskl format: node hierarchy, skin palette,
 * bind-pose mesh and keyframe tracks of a .glb model. Poses are evaluated
 * and skinned on the EE at runtime by SkelInstance (renderer/3d/mesh).
 *
 * Reads the whole file into memory first (fseek is unreliable over the PS2
 * host filesystem) and works on both host: and cdrom0: boot paths.
 */
class TsklLoader {
 public:
  /**
   * @param relativePath path relative to the ELF cwd, e.g. "models/hero.tskl"
   * @return parsed model, or nullptr when the file is missing/malformed
   */
  static std::unique_ptr<SkelModel> load(const std::string& relativePath);
};

}  // namespace Tyra
