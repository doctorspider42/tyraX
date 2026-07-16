/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Added by TyraX: lightweight OBJ+MTL loader for editor-built games.
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
  // refl: spherical environment map ("" = not reflective) + strength 0..1;
  // rounded = env normals radiate from the part centroid ("-rounded" flag)
  std::string reflTextureName;
  float reflStrength = 0.0F;
  bool reflRounded = false;
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

/** One material of a standalone .mtl library (loadMtl). */
struct LeanMtlMaterial {
  std::string name;
  std::string textureName;  // map_Kd, relative to the .mtl directory ("" = none)
  float kd[3] = {1.0F, 1.0F, 1.0F};
  // refl: spherical environment map ("" = not reflective) + strength 0..1;
  // rounded = env normals radiate from the part centroid ("-rounded" flag)
  std::string reflTextureName;
  float reflStrength = 0.0F;
  bool reflRounded = false;
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
 *   coordinate to image space - the exact semantics of the TyraX
 *   viewport parser (src/objparser.cpp there; keep both in sync), so a scene
 *   previews identically in the editor and on the console,
 * - reads files sequentially into memory (no fseek - unreliable on host fs).
 */
class LeanObjLoader {
 public:
  /**
   * @param relativePath path relative to the ELF cwd, e.g. "models/tree.obj"
   * @param overrideMtl optional path (relative to the cwd) to a material
   *        library that REPLACES the model's own mtllib/sibling libraries -
   *        usemtl names resolve against it and the returned textureName
   *        paths are then relative to that file's directory
   * @return parsed mesh, or nullptr when the file is missing/has no triangles
   */
  static std::unique_ptr<LeanObjMesh> load(const std::string& relativePath,
                                           const std::string& overrideMtl = "");

  /**
   * Parses a standalone .mtl library (newmtl/Kd/map_Kd/refl), file order.
   * @return materials, empty when the file is missing or defines none
   */
  static std::vector<LeanMtlMaterial> loadMtl(const std::string& relativePath);
};

}  // namespace Tyra
