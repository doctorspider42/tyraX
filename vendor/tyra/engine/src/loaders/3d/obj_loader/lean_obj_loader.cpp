/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Added by TyraX: lightweight OBJ+MTL loader for editor-built games.
# Parsing semantics mirror the editor's src/objparser.cpp - keep in sync.
*/

#include "loaders/3d/obj_loader/lean_obj_loader.hpp"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <map>
#include <sstream>

#include "debug/debug.hpp"
#include "file/file_utils.hpp"

namespace Tyra {

namespace {

/** Whole file read sequentially into memory (host fs fseek is unreliable). */
bool readWholeFile(const std::string& fullPath, std::string& out) {
  FILE* f = fopen(fullPath.c_str(), "rb");
  if (!f) return false;
  out.clear();
  char chunk[4096];
  size_t got;
  while ((got = fread(chunk, 1, sizeof(chunk), f)) > 0) out.append(chunk, got);
  fclose(f);
  return !out.empty();
}

struct MtlEntry {
  float kd[3] = {1.0F, 1.0F, 1.0F};
  std::string texture;
  std::string reflTexture;
  float reflStrength = 0.0F;
};

/** newmtl/Kd/map_Kd/refl from one .mtl buffer (map_Kd/refl: last token of the
 * line; refl's -mm gain option carries the reflection strength).
 * order (optional) records material names in file order. */
void parseMtl(const std::string& text, std::map<std::string, MtlEntry>& out,
              std::vector<std::string>* order = nullptr) {
  std::istringstream file(text);
  std::string line, current;
  while (std::getline(file, line)) {
    std::istringstream ss(line);
    std::string tag;
    ss >> tag;
    if (tag == "newmtl") {
      ss >> current;
      if (out.emplace(current, MtlEntry{}).second && order)
        order->push_back(current);
    } else if (tag == "Kd" && !current.empty()) {
      MtlEntry& m = out[current];
      ss >> m.kd[0] >> m.kd[1] >> m.kd[2];
    } else if (tag == "map_Kd" && !current.empty()) {
      std::string tok, last;
      while (ss >> tok) last = tok;
      for (auto& c : last)
        if (c == '\\') c = '/';
      out[current].texture = last;
    } else if (tag == "refl" && !current.empty()) {
      // Spherical environment map (TyraX reflective materials):
      //   refl -type sphere -mm 0 <strength> <file>
      // Filename = last token; -mm's gain operand is the reflection strength.
      MtlEntry& m = out[current];
      std::string tok, last;
      while (ss >> tok) {
        if (tok == "-mm") {
          float base = 0.0F;
          ss >> base >> m.reflStrength;
        } else {
          last = tok;
        }
      }
      for (auto& c : last)
        if (c == '\\') c = '/';
      m.reflTexture = last;
      if (m.reflStrength <= 0.0F && !m.reflTexture.empty())
        m.reflStrength = 0.5F;  // refl without -mm: sensible default
    }
  }
}

}  // namespace

std::vector<LeanMtlMaterial> LeanObjLoader::loadMtl(
    const std::string& relativePath) {
  std::vector<LeanMtlMaterial> result;
  std::string text;
  if (!readWholeFile(FileUtils::fromCwd(relativePath), text)) {
    TYRA_WARN("LeanObjLoader: cannot read mtl ", relativePath.c_str());
    return result;
  }
  std::map<std::string, MtlEntry> materials;
  std::vector<std::string> order;
  parseMtl(text, materials, &order);
  for (const auto& name : order) {
    LeanMtlMaterial m;
    m.name = name;
    m.textureName = materials[name].texture;
    m.kd[0] = materials[name].kd[0];
    m.kd[1] = materials[name].kd[1];
    m.kd[2] = materials[name].kd[2];
    m.reflTextureName = materials[name].reflTexture;
    m.reflStrength = materials[name].reflStrength;
    result.push_back(m);
  }
  return result;
}

std::unique_ptr<LeanObjMesh> LeanObjLoader::load(
    const std::string& relativePath, const std::string& overrideMtl) {
  std::string text;
  if (!readWholeFile(FileUtils::fromCwd(relativePath), text)) {
    TYRA_WARN("LeanObjLoader: cannot read ", relativePath.c_str());
    return nullptr;
  }

  // mtllib names resolve relative to the .obj's directory
  std::string dir;
  const size_t slash = relativePath.find_last_of('/');
  if (slash != std::string::npos) dir = relativePath.substr(0, slash + 1);

  auto result = std::make_unique<LeanObjMesh>();

  std::vector<float> positions;  // x,y,z triples
  std::vector<float> texcoords;  // u,v pairs
  std::map<std::string, MtlEntry> materials;

  if (!overrideMtl.empty()) {
    // A material override replaces the model's own libraries entirely -
    // usemtl names resolve against it (universal .mtl shared by many models).
    std::string mtlText;
    if (readWholeFile(FileUtils::fromCwd(overrideMtl), mtlText))
      parseMtl(mtlText, materials);
    else
      TYRA_WARN("LeanObjLoader: missing override mtl ", overrideMtl.c_str());
  } else {
    // Implicit material library: a sibling .mtl named like the .obj is loaded
    // even without a mtllib line (mirrors the editor parser - many exporters
    // rely on that convention). Explicit mtllib files parse later and win on
    // name clashes.
    std::string stem = relativePath;
    const size_t dot = stem.find_last_of('.');
    if (dot != std::string::npos) stem = stem.substr(0, dot);
    std::string mtlText;
    if (readWholeFile(FileUtils::fromCwd(stem + ".mtl"), mtlText))
      parseMtl(mtlText, materials);
  }

  std::map<std::string, int> materialIndex;
  int current = -1;
  auto materialFor = [&](const std::string& name) {
    auto it = materialIndex.find(name);
    if (it != materialIndex.end()) return it->second;
    LeanObjMaterial m;
    m.name = name;
    auto mtl = materials.find(name);
    if (mtl != materials.end()) {
      m.kd[0] = mtl->second.kd[0];
      m.kd[1] = mtl->second.kd[1];
      m.kd[2] = mtl->second.kd[2];
      m.textureName = mtl->second.texture;
      m.reflTextureName = mtl->second.reflTexture;
      m.reflStrength = mtl->second.reflStrength;
    }
    result->materials.push_back(m);
    materialIndex[name] = (int)result->materials.size() - 1;
    return (int)result->materials.size() - 1;
  };

  auto vertexAt = [&](int objIndex, float* xyz) {
    // obj indices are 1-based; negative = relative to the end
    const int count = (int)positions.size() / 3;
    int i = objIndex > 0 ? objIndex - 1 : count + objIndex;
    if (i < 0 || i >= count) return false;
    xyz[0] = positions[i * 3];
    xyz[1] = positions[i * 3 + 1];
    xyz[2] = positions[i * 3 + 2];
    return true;
  };
  auto uvAt = [&](int objIndex, float* uv) {
    const int count = (int)texcoords.size() / 2;
    int i = objIndex > 0 ? objIndex - 1 : count + objIndex;
    if (objIndex == 0 || i < 0 || i >= count) {
      uv[0] = uv[1] = 0.0F;
      return;
    }
    uv[0] = texcoords[i * 2];
    uv[1] = 1.0F - texcoords[i * 2 + 1];  // flip to image space
  };

  bool anyVertex = false;
  auto grow = [&](const float* p) {
    if (!anyVertex) {
      anyVertex = true;
      for (int i = 0; i < 3; ++i) result->min[i] = result->max[i] = p[i];
      return;
    }
    for (int i = 0; i < 3; ++i) {
      if (p[i] < result->min[i]) result->min[i] = p[i];
      if (p[i] > result->max[i]) result->max[i] = p[i];
    }
  };

  std::istringstream file(text);
  std::string line;
  std::vector<int> vIdx, tIdx;
  while (std::getline(file, line)) {
    if (line.size() < 2) continue;
    std::istringstream ss(line);
    std::string tag;
    ss >> tag;

    if (tag == "v") {
      float x = 0, y = 0, z = 0;
      ss >> x >> y >> z;
      positions.push_back(x);
      positions.push_back(y);
      positions.push_back(z);
    } else if (tag == "vt") {
      float u = 0, v = 0;
      ss >> u >> v;
      texcoords.push_back(u);
      texcoords.push_back(v);
    } else if (tag == "mtllib" && overrideMtl.empty()) {
      std::string name;
      while (ss >> name) {
        std::string mtlText;
        if (readWholeFile(FileUtils::fromCwd(dir + name), mtlText))
          parseMtl(mtlText, materials);
        else
          TYRA_WARN("LeanObjLoader: missing mtllib ", (dir + name).c_str());
      }
    } else if (tag == "usemtl") {
      std::string name;
      ss >> name;
      current = materialFor(name);
    } else if (tag == "f") {
      // face vertex tokens: "7", "7/1", "7//2", "7/1/2", negatives
      vIdx.clear();
      tIdx.clear();
      std::string part;
      while (ss >> part) {
        vIdx.push_back(atoi(part.c_str()));
        const size_t sl = part.find('/');
        int t = 0;
        if (sl != std::string::npos && sl + 1 < part.size() &&
            part[sl + 1] != '/')
          t = atoi(part.c_str() + sl + 1);
        tIdx.push_back(t);
      }

      if (current < 0) current = materialFor("");
      std::vector<float>& out = result->materials[current].vertices;

      for (size_t k = 2; k < vIdx.size(); ++k) {
        float a[3], b[3], c[3];
        if (!vertexAt(vIdx[0], a) || !vertexAt(vIdx[k - 1], b) ||
            !vertexAt(vIdx[k], c))
          continue;
        float uva[2], uvb[2], uvc[2];
        uvAt(tIdx[0], uva);
        uvAt(tIdx[k - 1], uvb);
        uvAt(tIdx[k], uvc);

        // flat face normal
        const float ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2];
        const float vx = c[0] - a[0], vy = c[1] - a[1], vz = c[2] - a[2];
        float nx = uy * vz - uz * vy;
        float ny = uz * vx - ux * vz;
        float nz = ux * vy - uy * vx;
        const float len = sqrtf(nx * nx + ny * ny + nz * nz);
        if (len > 1e-8F) {
          nx /= len;
          ny /= len;
          nz /= len;
        } else {
          nx = 0.0F;
          ny = 1.0F;
          nz = 0.0F;
        }

        const float* pts[3] = {a, b, c};
        const float* uvs[3] = {uva, uvb, uvc};
        for (int i = 0; i < 3; ++i) {
          grow(pts[i]);
          out.push_back(pts[i][0]);
          out.push_back(pts[i][1]);
          out.push_back(pts[i][2]);
          out.push_back(nx);
          out.push_back(ny);
          out.push_back(nz);
          out.push_back(uvs[i][0]);
          out.push_back(uvs[i][1]);
        }
      }
    }
  }

  // drop materials that got no faces (usemtl with nothing after it)
  for (size_t i = result->materials.size(); i-- > 0;)
    if (result->materials[i].vertices.empty())
      result->materials.erase(result->materials.begin() + i);

  if (result->materials.empty()) {
    TYRA_WARN("LeanObjLoader: no triangles in ", relativePath.c_str());
    return nullptr;
  }
  return result;
}

}  // namespace Tyra
