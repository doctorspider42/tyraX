/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Added by TyraX: loader for .tmdl binary static models.
# Binary layout lives in the editor's src/tmdl.hpp (written by
# templates::bakeStaticModels) - keep both sides in sync.
*/

#include "loaders/3d/tmdl_loader/tmdl_loader.hpp"

#include <stdio.h>
#include <string.h>

#include "debug/debug.hpp"
#include "file/file_utils.hpp"

namespace Tyra {

namespace {

// Corruption guards - a bad count must not turn into a huge allocation.
constexpr u32 kMaxParts = 256;
constexpr u32 kMaxVertices = 1u << 21;
constexpr u32 kMaxLods = 4;

/** Whole file read sequentially into memory (host fs fseek is unreliable). */
bool readWholeFile(const std::string& fullPath, std::vector<u8>& out) {
  FILE* f = fopen(fullPath.c_str(), "rb");
  if (!f) return false;
  out.clear();
  u8 chunk[8192];
  size_t got;
  while ((got = fread(chunk, 1, sizeof(chunk), f)) > 0)
    out.insert(out.end(), chunk, chunk + got);
  fclose(f);
  return !out.empty();
}

/** Bounds-checked sequential reader over the in-memory file. */
struct Reader {
  const u8* p;
  size_t left;

  bool bytes(void* dst, size_t n) {
    if (n > left) return false;
    memcpy(dst, p, n);
    p += n;
    left -= n;
    return true;
  }
  bool u32le(u32* dst) { return bytes(dst, 4); }
  bool f32le(float* dst) { return bytes(dst, 4); }
  bool fixedString(std::string* dst, size_t size) {
    if (size > left) return false;
    size_t len = 0;
    while (len < size && p[len]) ++len;
    dst->assign(reinterpret_cast<const char*>(p), len);
    p += size;
    left -= size;
    return true;
  }
};

/** vertexCount + interleaved vertices + the optional AO table. */
bool readMesh(Reader& in, std::vector<float>* verts, std::vector<u8>* ao) {
  u32 count = 0;
  if (!in.u32le(&count)) return false;
  if (count == 0 || count % 3 != 0 || count > kMaxVertices) return false;
  verts->resize((size_t)count * 8);
  if (!in.bytes(verts->data(), (size_t)count * 8 * sizeof(float))) return false;
  u32 aoCount = 0;
  if (!in.u32le(&aoCount)) return false;
  if (aoCount != 0 && aoCount != count) return false;  // partial = malformed
  ao->resize(aoCount);
  return aoCount == 0 || in.bytes(ao->data(), aoCount);
}

}  // namespace

std::unique_ptr<LeanObjMesh> TmdlLoader::load(const std::string& relativePath) {
  std::vector<u8> file;
  if (!readWholeFile(FileUtils::fromCwd(relativePath), file)) {
    TYRA_WARN("TmdlLoader: cannot read ", relativePath.c_str());
    return nullptr;
  }

  Reader in{file.data(), file.size()};
  char magic[4];
  u32 version = 0, partCount = 0;
  auto mesh = std::make_unique<LeanObjMesh>();
  if (!in.bytes(magic, 4) || memcmp(magic, "TMDL", 4) != 0 ||
      !in.u32le(&version) || version < 1 || version > 2) {
    TYRA_WARN("TmdlLoader: bad header in ", relativePath.c_str());
    return nullptr;
  }
  for (int c = 0; c < 3; c++)
    if (!in.f32le(&mesh->min[c])) return nullptr;
  for (int c = 0; c < 3; c++)
    if (!in.f32le(&mesh->max[c])) return nullptr;
  if (!in.u32le(&partCount) || partCount == 0 || partCount > kMaxParts) {
    TYRA_WARN("TmdlLoader: empty model ", relativePath.c_str());
    return nullptr;
  }

  mesh->materials.resize(partCount);
  for (u32 i = 0; i < partCount; i++) {
    LeanObjMaterial& mat = mesh->materials[i];
    u32 flags = 0, lodCount = 0;
    if (!in.fixedString(&mat.name, 32) ||
        !in.fixedString(&mat.textureName, 64) ||
        !in.fixedString(&mat.reflTextureName, 64) ||
        !in.bytes(mat.kd, sizeof(mat.kd))) {
      TYRA_WARN("TmdlLoader: malformed part in ", relativePath.c_str());
      return nullptr;
    }
    // Ke (emissive materials) arrived with version 2; a v1 file has no such
    // field and its parts stay matte, which is exactly what they were.
    if (version >= 2 && !in.bytes(mat.ke, sizeof(mat.ke))) {
      TYRA_WARN("TmdlLoader: malformed part in ", relativePath.c_str());
      return nullptr;
    }
    if (!in.f32le(&mat.reflStrength) || !in.u32le(&flags) ||
        !readMesh(in, &mat.vertices, &mat.vertexAo) ||
        !in.u32le(&lodCount) || lodCount > kMaxLods) {
      TYRA_WARN("TmdlLoader: malformed part in ", relativePath.c_str());
      return nullptr;
    }
    mat.reflRounded = (flags & 1) != 0;
    // uvRect stays {0,0,1,1}: an atlas rect is already folded into the baked
    // UVs, so nothing downstream may multiply through it a second time.
    mat.lods.resize(lodCount);
    for (u32 l = 0; l < lodCount; l++) {
      if (!readMesh(in, &mat.lods[l].vertices, &mat.lods[l].vertexAo)) {
        TYRA_WARN("TmdlLoader: malformed lod in ", relativePath.c_str());
        return nullptr;
      }
    }
  }
  return mesh;
}

}  // namespace Tyra
