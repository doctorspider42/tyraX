/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Added by tyra-editor: lightweight OBJ+MTL loader for editor-built games.
*/

#pragma once

#include <memory>
#include <string>
#include <vector>
#include <tamtypes.h>

namespace Tyra {

/**
 * One draw batch of a model: all triangles that share a material.
 * Vertices are a flat triangle list, 8 floats each (x, y, z, nx, ny, nz, u, v).
 */
struct LeanObjMaterial {
  std::string name;         // usemtl name ("" = no material)
  std::string textureName;  // map_Kd, relative to the .obj directory ("" = none)
  float kd[3] = {1.0F, 1.0F, 1.0F};  // diffuse color 0..1
  std::vector<float> vertices;
};

struct LeanObjMesh {
  std::vector<LeanObjMaterial> materials;  // first-use order of usemtl
  float min[3] = {0, 0, 0};                // AABB over all vertices
  float max[3] = {0, 0, 0};
  u32 vertexCount() const {
    u32 n = 0;
    for (const auto& m : materials) n += m.vertices.size() / 8;
    return n;
  }
};

/**
 * Lightweight Wavefront .obj + .mtl loader.
 *
 * Unlike ObjLoader (tinyobj), this loader:
 * - works without any .mtl file (faces land in a default white material),
 * - resolves the .obj, its mtllib entries and map_Kd textures through
 *   FileUtils::fromCwd, so it is safe on both host: and cdrom0: (ISO9660
 *   upper-case + ";1" version suffix) boot paths,
 * - computes flat per-face normals (vn is ignored) and flips the V texture
 *   coordinate to image space - the exact semantics of the tyra-editor
 *   viewport parser (src/objparser.cpp there; keep both in sync), so a scene
 *   previews identically in the editor and on the console,
 * - reads files sequentially into memory (no fseek - unreliable on host fs).
 */
class LeanObjLoader {
 public:
  /**
   * @param relativePath path relative to the ELF cwd, e.g. "models/tree.obj"
   * @return parsed mesh, or nullptr when the file is missing/has no triangles
   */
  static std::unique_ptr<LeanObjMesh> load(const std::string& relativePath);
};

}  // namespace Tyra
