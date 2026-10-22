/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Added by TyraX: loader for .tmdl binary static models.
*/

#pragma once

#include <memory>
#include <string>

#include "loaders/3d/obj_loader/lean_obj_loader.hpp"

namespace Tyra {

/**
 * Loader for the TyraX .tmdl format: a static model with everything the
 * ASCII .obj path used to work out on the EE at load time already resolved -
 * triangulation, flat normals, the V flip, material assignment (including a
 * per-object .mtl override), texture-atlas UV rects, bin-relative texture
 * paths, and optional distance LOD tiers. What is left here is a sequential
 * read plus a memcpy per part.
 *
 * Returns the same LeanObjMesh the .obj loader does, so the game builds its
 * geometry through one code path. Two differences the caller must know:
 * - textureName / reflTextureName are already **cwd-relative** ("models/x.png"),
 *   not relative to the model's directory - do not prepend a directory;
 * - materials may carry `lods`.
 *
 * Reads the whole file into memory first (fseek is unreliable over the PS2
 * host filesystem) and works on both host: and cdrom0: boot paths.
 *
 * Binary layout lives in the editor's src/tmdl.hpp (written by
 * templates::bakeStaticModels) - keep both sides in sync.
 */
class TmdlLoader {
 public:
  /**
   * @param relativePath path relative to the ELF cwd, e.g. "models/tree.tmdl"
   * @return parsed mesh, or nullptr when the file is missing/malformed
   */
  static std::unique_ptr<LeanObjMesh> load(const std::string& relativePath);
};

}  // namespace Tyra
