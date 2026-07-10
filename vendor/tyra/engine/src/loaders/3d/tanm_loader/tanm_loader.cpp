/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Added by tyra-editor: loader for .tanm baked-animation models.
# Binary layout lives in the editor's src/glbparser.cpp (writeTanm) -
# keep both sides in sync.
*/

#include "loaders/3d/tanm_loader/tanm_loader.hpp"

#include <stdio.h>
#include <string.h>

#include "debug/debug.hpp"
#include "file/file_utils.hpp"
#include "math/vec4.hpp"

namespace Tyra {

namespace {

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

}  // namespace

std::unique_ptr<TanmModel> TanmLoader::load(const std::string& relativePath) {
  std::vector<u8> file;
  if (!readWholeFile(FileUtils::fromCwd(relativePath), file)) {
    TYRA_WARN("TanmLoader: cannot read ", relativePath.c_str());
    return nullptr;
  }

  Reader in{file.data(), file.size()};
  char magic[4];
  u32 version = 0, partCount = 0, frameCount = 0, clipCount = 0;
  auto model = std::make_unique<TanmModel>();
  if (!in.bytes(magic, 4) || memcmp(magic, "TANM", 4) != 0 ||
      !in.u32le(&version) || version != 1 || !in.u32le(&partCount) ||
      !in.u32le(&frameCount) || !in.u32le(&clipCount) ||
      !in.f32le(&model->fps)) {
    TYRA_WARN("TanmLoader: bad header in ", relativePath.c_str());
    return nullptr;
  }
  for (int c = 0; c < 3; c++)
    if (!in.f32le(&model->min[c])) return nullptr;
  for (int c = 0; c < 3; c++)
    if (!in.f32le(&model->max[c])) return nullptr;
  if (partCount == 0 || frameCount == 0 || clipCount == 0) {
    TYRA_WARN("TanmLoader: empty model ", relativePath.c_str());
    return nullptr;
  }

  for (u32 c = 0; c < clipCount; c++) {
    TanmClip clip;
    if (!in.fixedString(&clip.name, 32) || !in.u32le(&clip.firstFrame) ||
        !in.u32le(&clip.frameCount))
      return nullptr;
    if (clip.firstFrame + clip.frameCount > frameCount) return nullptr;
    model->clips.push_back(clip);
  }

  model->data = std::make_unique<MeshBuilderData>();
  model->data->loadNormals = true;
  model->data->loadLightmap = false;

  // The directory of the .tanm resolves relative texture names.
  std::string dir = relativePath;
  {
    const size_t slash = dir.find_last_of('/');
    dir = slash == std::string::npos ? "" : dir.substr(0, slash + 1);
  }

  for (u32 mi = 0; mi < partCount; mi++) {
    auto* material = new MeshBuilderMaterialData();
    model->data->materials.push_back(material);  // owned by builder data now

    std::string texture;
    float color[4];
    u32 vertexCount = 0;
    if (!in.fixedString(&material->name, 32) || !in.fixedString(&texture, 64) ||
        !in.bytes(color, sizeof(color)) || !in.u32le(&vertexCount) ||
        vertexCount == 0) {
      TYRA_WARN("TanmLoader: bad material in ", relativePath.c_str());
      return nullptr;
    }
    if (material->name.empty()) material->name = "mat";
    // The single color the VU1 programs modulate with (128 = 1.0).
    material->ambient.set(color[0] * 128.0F, color[1] * 128.0F,
                          color[2] * 128.0F, 128.0F);
    if (!texture.empty()) material->texturePath = texture;
    model->texturePaths.push_back(texture);

    // UVs are stored once; every frame gets its own copy because each
    // MeshMaterialFrame owns (and frees) its arrays.
    std::vector<float> uv;
    if (!texture.empty()) {
      uv.resize((size_t)vertexCount * 2);
      if (!in.bytes(uv.data(), uv.size() * sizeof(float))) {
        TYRA_WARN("TanmLoader: truncated uvs in ", relativePath.c_str());
        return nullptr;
      }
    }

    // Error paths below leak the Vec4 arrays already handed to frame data
    // (frame data never frees them - ownership normally moves to the mesh).
    // A malformed .tanm is a broken build asset; the game just skips it.
    std::vector<float> raw((size_t)vertexCount * 3);
    for (u32 f = 0; f < frameCount; f++) {
      auto* frame = new MeshBuilderMaterialFrameData();
      material->frames.push_back(frame);  // owned by material data now
      frame->count = vertexCount;

      frame->vertices = new Vec4[vertexCount];
      if (!in.bytes(raw.data(), raw.size() * sizeof(float))) {
        TYRA_WARN("TanmLoader: truncated frames in ", relativePath.c_str());
        return nullptr;
      }
      for (u32 v = 0; v < vertexCount; v++)
        frame->vertices[v].set(raw[v * 3], raw[v * 3 + 1], raw[v * 3 + 2],
                               1.0F);

      frame->normals = new Vec4[vertexCount];
      if (!in.bytes(raw.data(), raw.size() * sizeof(float))) {
        TYRA_WARN("TanmLoader: truncated normals in ", relativePath.c_str());
        return nullptr;
      }
      for (u32 v = 0; v < vertexCount; v++)
        frame->normals[v].set(raw[v * 3], raw[v * 3 + 1], raw[v * 3 + 2],
                              1.0F);

      if (!texture.empty()) {
        frame->textureCoords = new Vec4[vertexCount];
        for (u32 v = 0; v < vertexCount; v++)
          frame->textureCoords[v].set(uv[v * 2], uv[v * 2 + 1], 1.0F, 0.0F);
      }
    }
  }

  // Texture names resolve relative to the model file, like map_Kd does.
  for (auto& path : model->texturePaths)
    if (!path.empty() && path.find('/') == std::string::npos) path = dir + path;

  return model;
}

}  // namespace Tyra
