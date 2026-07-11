/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Added by tyra-editor: loader for .tskl skeletal-animation models.
# Binary layout lives in the editor's src/glbparser.cpp (writeTskl) -
# keep both sides in sync.
*/

#include "loaders/3d/tskl_loader/tskl_loader.hpp"

#include <stdio.h>
#include <string.h>

#include "debug/debug.hpp"
#include "file/file_utils.hpp"

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
  bool floats(std::vector<float>* dst, size_t count) {
    dst->resize(count);
    return count == 0 || bytes(dst->data(), count * sizeof(float));
  }
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

std::unique_ptr<SkelModel> TsklLoader::load(const std::string& relativePath) {
  std::vector<u8> file;
  if (!readWholeFile(FileUtils::fromCwd(relativePath), file)) {
    TYRA_WARN("TsklLoader: cannot read ", relativePath.c_str());
    return nullptr;
  }

  Reader in{file.data(), file.size()};
  char magic[4];
  u32 version = 0, nodeCount = 0, paletteCount = 0, partCount = 0,
      clipCount = 0;
  auto model = std::make_unique<SkelModel>();
  if (!in.bytes(magic, 4) || memcmp(magic, "TSKL", 4) != 0 ||
      !in.u32le(&version) || version != 1 || !in.u32le(&nodeCount) ||
      !in.u32le(&paletteCount) || !in.u32le(&partCount) ||
      !in.u32le(&clipCount)) {
    TYRA_WARN("TsklLoader: bad header in ", relativePath.c_str());
    return nullptr;
  }
  for (int c = 0; c < 3; c++)
    if (!in.f32le(&model->min[c])) return nullptr;
  for (int c = 0; c < 3; c++)
    if (!in.f32le(&model->max[c])) return nullptr;
  if (nodeCount == 0 || paletteCount == 0 || paletteCount > 256 ||
      partCount == 0 || clipCount == 0) {
    TYRA_WARN("TsklLoader: empty model ", relativePath.c_str());
    return nullptr;
  }

  model->nodes.resize(nodeCount);
  for (u32 i = 0; i < nodeCount; i++) {
    SkelNode& node = model->nodes[i];
    u32 parent = 0, flags = 0;
    if (!in.u32le(&parent) || !in.u32le(&flags) ||
        !in.bytes(node.t, sizeof(node.t)) ||
        !in.bytes(node.r, sizeof(node.r)) ||
        !in.bytes(node.s, sizeof(node.s)) ||
        !in.bytes(node.matrix.data, sizeof(float) * 16))
      return nullptr;
    node.parent = (s32)parent;
    node.hasMatrix = flags & 1 ? 1 : 0;
    if (node.parent >= (s32)nodeCount) return nullptr;
  }

  // Parents-first order for the pose walk: iterative DFS from the roots.
  // A malformed hierarchy (cycle / unreachable node) fails the load.
  {
    std::vector<std::vector<u32>> children(nodeCount);
    for (u32 i = 0; i < nodeCount; i++)
      if (model->nodes[i].parent >= 0)
        children[model->nodes[i].parent].push_back(i);
    std::vector<u32> stack;
    for (u32 i = nodeCount; i-- > 0;)
      if (model->nodes[i].parent < 0) stack.push_back(i);
    model->order.reserve(nodeCount);
    while (!stack.empty()) {
      const u32 i = stack.back();
      stack.pop_back();
      model->order.push_back(i);
      for (u32 k = children[i].size(); k-- > 0;)
        stack.push_back(children[i][k]);
    }
    if (model->order.size() != nodeCount) {
      TYRA_WARN("TsklLoader: broken hierarchy in ", relativePath.c_str());
      return nullptr;
    }
  }

  model->palette.resize(paletteCount);
  for (u32 i = 0; i < paletteCount; i++) {
    SkelJoint& joint = model->palette[i];
    if (!in.u32le(&joint.node) ||
        !in.bytes(joint.ibm.data, sizeof(float) * 16))
      return nullptr;
    if (joint.node >= nodeCount) return nullptr;
  }

  for (u32 c = 0; c < clipCount; c++) {
    SkelClip clip;
    u32 channelCount = 0;
    if (!in.fixedString(&clip.name, 32) || !in.f32le(&clip.duration) ||
        !in.u32le(&channelCount))
      return nullptr;
    for (u32 ch = 0; ch < channelCount; ch++) {
      SkelChannel channel;
      u8 head[4];
      u32 keyCount = 0;
      if (!in.u32le(&channel.node) || !in.bytes(head, 4) ||
          !in.u32le(&keyCount) || channel.node >= nodeCount || keyCount == 0)
        return nullptr;
      channel.path = head[0];
      channel.step = head[1];
      if (channel.path > 2) return nullptr;
      if (!in.floats(&channel.times, keyCount)) return nullptr;
      if (channel.path == 1) {
        channel.quat.resize((size_t)keyCount * 4);
        if (!in.bytes(channel.quat.data(), channel.quat.size() * 2))
          return nullptr;
      } else {
        if (!in.floats(&channel.vec, (size_t)keyCount * 3)) return nullptr;
      }
      clip.channels.push_back(std::move(channel));
    }
    model->clips.push_back(std::move(clip));
  }

  // The directory of the .tskl resolves relative texture names.
  std::string dir = relativePath;
  {
    const size_t slash = dir.find_last_of('/');
    dir = slash == std::string::npos ? "" : dir.substr(0, slash + 1);
  }

  for (u32 mi = 0; mi < partCount; mi++) {
    SkelPart part;
    if (!in.fixedString(&part.name, 32) ||
        !in.fixedString(&part.texturePath, 64) ||
        !in.bytes(part.color, sizeof(part.color)) ||
        !in.u32le(&part.vertexCount) || part.vertexCount == 0) {
      TYRA_WARN("TsklLoader: bad part in ", relativePath.c_str());
      return nullptr;
    }
    if (part.name.empty()) part.name = "mat";
    const size_t count = part.vertexCount;
    if (!part.texturePath.empty() && !in.floats(&part.uvs, count * 2))
      return nullptr;
    if (!in.floats(&part.positions, count * 3) ||
        !in.floats(&part.normals, count * 3))
      return nullptr;
    part.joints.resize(count * 4);
    part.weights.resize(count * 4);
    if (!in.bytes(part.joints.data(), count * 4) ||
        !in.bytes(part.weights.data(), count * 4)) {
      TYRA_WARN("TsklLoader: truncated part in ", relativePath.c_str());
      return nullptr;
    }
    for (u8 joint : part.joints)
      if (joint >= paletteCount) {
        TYRA_WARN("TsklLoader: joint out of range in ", relativePath.c_str());
        return nullptr;  // validated once here - the skinning loop trusts it
      }
    if (!part.texturePath.empty() &&
        part.texturePath.find('/') == std::string::npos)
      part.texturePath = dir + part.texturePath;
    model->parts.push_back(std::move(part));
  }

  // Merge parts that share texture and color: every part becomes a draw bag
  // per instance per frame (object-data DMA + packager run each), and models
  // commonly split one material into several tiny parts. Merging is safe -
  // vertices carry their own joints/weights and draw order among equal
  // materials is irrelevant under z-testing.
  for (size_t a = 0; a + 1 < model->parts.size(); a++) {
    SkelPart& dst = model->parts[a];
    for (size_t b = a + 1; b < model->parts.size();) {
      SkelPart& src = model->parts[b];
      const bool same =
          dst.texturePath == src.texturePath &&
          memcmp(dst.color, src.color, sizeof(dst.color)) == 0;
      if (!same) {
        ++b;
        continue;
      }
      dst.positions.insert(dst.positions.end(), src.positions.begin(),
                           src.positions.end());
      dst.normals.insert(dst.normals.end(), src.normals.begin(),
                         src.normals.end());
      dst.uvs.insert(dst.uvs.end(), src.uvs.begin(), src.uvs.end());
      dst.joints.insert(dst.joints.end(), src.joints.begin(),
                        src.joints.end());
      dst.weights.insert(dst.weights.end(), src.weights.begin(),
                         src.weights.end());
      dst.vertexCount += src.vertexCount;
      model->parts.erase(model->parts.begin() + b);
    }
  }

  return model;
}

}  // namespace Tyra
