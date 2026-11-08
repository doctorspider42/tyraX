/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Sandro Sobczyński <sandro.sobczynski@gmail.com>
*/

#include "renderer/3d/pipeline/static/core/bag/stapip_bag.hpp"
#include "renderer/core/paths/path1/vif1_queue.hpp"
#include <sstream>
#include <iomanip>

#include <cstring>
#include <new>

namespace Tyra {

StaPipBag::StaPipBag() {
  info = nullptr;
  color = nullptr;
  texture = nullptr;
  lighting = nullptr;
  billboard = nullptr;  // Modified by TyraX: VU1 billboard expansion
  bboxVersion = 0;
  contentVersion = nullptr;  // Modified by TyraX: content stamp, opt-in
  packageSize = 0;  // Modified by TyraX: 0 = derive from the program class
  stripped = false;  // Modified by TyraX: triangle list unless told otherwise
  refineStrips = false;  // Generated terrain opts in for all coplanar passes.
  immutableStreams = false;  // Modified by TyraX: see the header
  patch = nullptr;  // Modified by TyraX: see StaPipBagPatch
  immutableRegistered = false;
  stampSlot = -1;  // Modified by TyraX: see StaPipBag::stampSlot
  for (auto& s : immutableSpans) s = nullptr;
}

StaPipBag::~StaPipBag() { releaseImmutableStreams(); }

std::unique_ptr<StaPipPackedMesh> StaPipPackedMesh::build(
    const Vec4* vertices, const Color* colors, const Vec4* sts, u32 count) {
  if (!vertices || !colors || !count || count % 3u) return nullptr;
  for (u32 i = 0; i < count; ++i) {
    if (vertices[i].w != 1.0F || colors[i].a != 128.0F ||
        (sts && (sts[i].z != 1.0F || sts[i].w != 0.0F))) return nullptr;
  }
  std::unique_ptr<StaPipPackedMesh> mesh(new (std::nothrow) StaPipPackedMesh());
  if (!mesh) return nullptr;
  const u32 xyzQw = (count * 3u + 3u) / 4u;
  const u32 stQw = sts ? (count + 1u) / 2u : 0;
  mesh->stStart = xyzQw;
  mesh->colorStart = xyzQw + stQw;
  mesh->quadwords = xyzQw * 2u + stQw;
  mesh->sliceCount = (count + kSliceVertices - 1u) / kSliceVertices;
  // Zero the final alignment words. After UNPACK consumes NUM vertices they
  // are VIF NOPs, and DMA still transfers a whole quadword.
  mesh->data.reset(new (std::nothrow) Quadword[mesh->quadwords]());
  mesh->slices.reset(new (std::nothrow) Bounds[mesh->sliceCount]);
  if (!mesh->data || !mesh->slices) return nullptr;

  auto* xyz = reinterpret_cast<u8*>(mesh->data.get());
  auto* st = xyz + mesh->stStart * 16u;
  auto* rgb = xyz + mesh->colorStart * 16u;
  auto extend = [](Bounds& b, const Vec4& p) {
    if (p.x < b.min.x) b.min.x = p.x;
    if (p.y < b.min.y) b.min.y = p.y;
    if (p.z < b.min.z) b.min.z = p.z;
    if (p.x > b.max.x) b.max.x = p.x;
    if (p.y > b.max.y) b.max.y = p.y;
    if (p.z > b.max.z) b.max.z = p.z;
  };
  Bounds whole{vertices[0], vertices[0]};
  for (u32 i = 0; i < count; ++i) {
    std::memcpy(xyz + i * 12u, &vertices[i].x, 12);
    std::memcpy(rgb + i * 12u, colors[i].rgba, 12);
    if (sts) std::memcpy(st + i * 8u, &sts[i].x, 8);
    Bounds& slice = mesh->slices[i / kSliceVertices];
    if (i % kSliceVertices == 0) slice = {vertices[i], vertices[i]};
    extend(slice, vertices[i]);
    extend(whole, vertices[i]);
  }
  const Vec4 corners[] = {whole.min, whole.max};
  mesh->whole = CoreBBox(corners, 2);
  return mesh;
}

void StaPipPackedMesh::decode(u32 offset, u32 count, Vec4* xyz,
                              Vec4* rgb, Vec4* st) const {
  const auto* pos = reinterpret_cast<const u8*>(data.get()) + offset * 12u;
  const auto* col = reinterpret_cast<const u8*>(data.get() + colorStart) + offset * 12u;
  const auto* uv = reinterpret_cast<const u8*>(data.get() + stStart) + offset * 8u;
  for (u32 i = 0; i < count; ++i) {
    std::memcpy(&xyz[i].x, pos + i * 12u, 12);
    xyz[i].w = 1.0F;
    std::memcpy(&rgb[i].x, col + i * 12u, 12);
    rgb[i].w = 128.0F;
    if (st) {
      std::memcpy(&st[i].x, uv + i * 8u, 8);
      st[i].z = 1.0F;
      st[i].w = 0.0F;
    }
  }
}

void StaPipPackedMesh::registerForDraw() {
  if (submitted) return;
  submitted = true;
  Vif1Queue::registerImmutableSpan(data.get(), quadwords);
}

StaPipPackedMesh::~StaPipPackedMesh() {
  if (!submitted) return;
  // Covers copied or synchronous submission too, where registration may
  // have failed or the frame arena may be disabled.
  Vif1Queue::drain();
  Vif1Queue::unregisterImmutableSpan(data.get());
}

// Modified by TyraX: see StaPipBagPatch.
void StaPipBagPatch::stampsOf(const StaPipBag* bag, u32 out[4]) {
  const auto at = [](const u32* p) { return p != nullptr ? *p : 0u; };
  out[0] = at(bag->contentVersion);
  out[1] = bag->color != nullptr ? at(bag->color->contentVersion) : 0u;
  out[2] = bag->texture != nullptr ? at(bag->texture->contentVersion) : 0u;
  out[3] = bag->lighting != nullptr ? at(bag->lighting->contentVersion) : 0u;
}

// Modified by TyraX: one span per stream, each exactly count quadwords. A
// stream another bag already registered (coplanar passes share vertices) or a
// full table simply stays copied - registration is an optimisation, never a
// requirement.
void StaPipBag::registerImmutableStreams() {
  if (immutableRegistered || count == 0) return;
  immutableRegistered = true;
  const void* streams[4] = {
      vertices, texture ? texture->coordinates : nullptr,
      color ? static_cast<const void*>(color->many) : nullptr,
      lighting ? lighting->normals : nullptr};
  for (int i = 0; i < 4; ++i) {
    immutableSpans[i] = nullptr;
    if (streams[i] && Vif1Queue::registerImmutableSpan(streams[i], count))
      immutableSpans[i] = streams[i];
  }
}

void StaPipBag::releaseImmutableStreams() {
  if (!immutableRegistered) return;
  immutableRegistered = false;
  for (auto& s : immutableSpans) {
    if (s) Vif1Queue::unregisterImmutableSpan(s);
    s = nullptr;
  }
}

int StaPipBag::retireImmutableStreams(const void* out[4]) {
  int n = 0;
  if (!immutableRegistered) return 0;
  immutableRegistered = false;
  for (auto& s : immutableSpans) {
    if (s) {
      Vif1Queue::retireImmutableSpan(s);
      out[n++] = s;
    }
    s = nullptr;
  }
  return n;
}

StaPipBagPackagesBBox StaPipBag::calculateBbox(const u32& maxVertCount) {
  TYRA_ASSERT(vertices != nullptr, "Vertices are required to calculate bbox");
  TYRA_ASSERT(count > 0, "Count must be greater than 0 to calculate bbox");
  return StaPipBagPackagesBBox(vertices, count, maxVertCount);
}

void StaPipBag::print() const {
  auto text = getPrint(nullptr);
  printf("%s\n", text.c_str());
}

void StaPipBag::print(const char* name) const {
  auto text = getPrint(name);
  printf("%s\n", text.c_str());
}

std::string StaPipBag::getPrint(const char* name) const {
  std::stringstream res;
  if (name) {
    res << name << "(";
  } else {
    res << "StaPipBag(";
  }
  res << std::fixed << std::setprecision(4);
  res << std::endl;
  res << "Count: " << count << ", " << std::endl;
  res << "Vertices present: " << (vertices != nullptr ? "Yes" : "No") << ", "
      << std::endl;
  res << "Info present: " << (info != nullptr ? "Yes" : "No") << ", "
      << std::endl;
  res << "Color present: " << (color != nullptr ? "Yes" : "No") << ", "
      << std::endl;
  res << "Texture present: " << (texture != nullptr ? "Yes" : "No") << ", "
      << std::endl;
  res << "Lighting present: " << (lighting != nullptr ? "Yes" : "No") << ", "
      << std::endl;
  res << "Model matrix: " << info->model->getPrint() << ", " << std::endl;
  if (color->single) {
    res << "Color single: " << color->single->getPrint() << ", " << std::endl;
  } else {
    res << "Color many present: " << (color->many != nullptr ? "Yes" : "No")
        << ", " << std::endl;
  }
  if (texture) {
    res << "Texture coords present: "
        << (texture->coordinates != nullptr ? "Yes" : "No") << ", "
        << std::endl;
    res << "Texture: " << texture->texture->getPrint() << ", " << std::endl;
  }
  if (lighting) {
    res << "Lighting normals present: " << (lighting->normals ? "Yes" : "No")
        << ", " << std::endl;
    res << "Lighting matrix: " << lighting->lightMatrix->getPrint() << ", "
        << std::endl;
  }
  res << ")";

  return res.str();
}

}  // namespace Tyra
