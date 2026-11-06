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
  immutableRegistered = false;
  stampSlot = -1;  // Modified by TyraX: see StaPipBag::stampSlot
  for (auto& s : immutableSpans) s = nullptr;
}

StaPipBag::~StaPipBag() { releaseImmutableStreams(); }

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
