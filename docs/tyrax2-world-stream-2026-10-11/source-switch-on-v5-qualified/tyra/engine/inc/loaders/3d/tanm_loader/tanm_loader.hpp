/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Added by TyraX: loader for .tanm baked-animation models.
*/

#pragma once

#include <memory>
#include <string>
#include <vector>
#include <tamtypes.h>

#include "loaders/3d/builder/mesh_builder_data.hpp"

namespace Tyra {

/** One named animation clip: a frame range inside the baked frame list. */
struct TanmClip {
  std::string name;
  u32 firstFrame = 0;
  u32 frameCount = 1;
};

/**
 * A baked-animation model: builder data ready for DynamicMesh plus the
 * clip table. `texturePaths[i]` is material i's texture, relative to the
 * ELF cwd ("" = untextured) - the game adds it to the texture repository
 * and links it to the mesh material itself.
 */
struct TanmModel {
  std::unique_ptr<MeshBuilderData> data;
  std::vector<TanmClip> clips;
  std::vector<std::string> texturePaths;
  float fps = 12.0F;
  float min[3] = {0, 0, 0};  // frame-0 AABB (box collision in editor games)
  float max[3] = {0, 0, 0};
};

/**
 * Loader for the TyraX .tanm format: animation clips of a .glb model,
 * pre-sampled ("baked") by the editor into MD2-style morph frames for the
 * dynamic pipeline. Layout documented in the editor's src/glbparser.cpp
 * (writeTanm) - keep both sides in sync.
 *
 * Reads the whole file into memory first (fseek is unreliable over the PS2
 * host filesystem) and works on both host: and cdrom0: boot paths.
 */
class TanmLoader {
 public:
  /**
   * @param relativePath path relative to the ELF cwd, e.g. "models/robot.tanm"
   * @return parsed model, or nullptr when the file is missing/malformed
   */
  static std::unique_ptr<TanmModel> load(const std::string& relativePath);
};

}  // namespace Tyra
