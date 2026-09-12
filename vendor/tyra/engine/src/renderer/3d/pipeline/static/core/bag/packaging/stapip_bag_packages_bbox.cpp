/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Sandro Sobczyński <sandro.sobczynski@gmail.com>
*/

#include <tamtypes.h>
#include <string>
#include <sstream>
#include <iomanip>
#include <vector>
#include "debug/debug.hpp"
#include "renderer/3d/pipeline/static/core/bag/packaging/stapip_bag_packages_bbox.hpp"

// Modified by TyraX: integer ceiling division avoids software double math on EE.
namespace Tyra {

StaPipBagPackagesBBox::StaPipBagPackagesBBox(const Vec4* t_vertices,
                                             u32* t_faces,
                                             const u32& t_facesCount,
                                             const u32& t_maxVertCount) {
  u32 splitPartSize = t_maxVertCount / 3;
  vertexCount = t_facesCount;
  partsCount = (vertexCount + splitPartSize - 1) / splitPartSize;

  bboxParts = new std::vector<CoreBBox>;
  for (u32 i = 0; i < partsCount; i++) {
    u32 partSize =
        i == partsCount - 1 ? t_facesCount - i * splitPartSize : splitPartSize;
    bboxParts->push_back(
        CoreBBox(t_vertices, t_faces + i * splitPartSize, partSize));
  }
  mainBBox = new RenderBBox(*bboxParts, 0, partsCount);
  rebuildCoarseBounds();
}

StaPipBagPackagesBBox::StaPipBagPackagesBBox(const Vec4* t_vertices,
                                             const u32& t_count,
                                             const u32& t_maxVertCount) {
  u32 splitPartSize = t_maxVertCount / 3;
  vertexCount = t_count;
  partsCount = (vertexCount + splitPartSize - 1) / splitPartSize;

  bboxParts = new std::vector<CoreBBox>;
  for (u32 i = 0; i < partsCount; i++) {
    u32 partSize =
        i == partsCount - 1 ? t_count - i * splitPartSize : splitPartSize;
    bboxParts->push_back(RenderBBox(t_vertices + i * splitPartSize, partSize));
  }

  mainBBox = new RenderBBox(*bboxParts, 0, partsCount);
  rebuildCoarseBounds();
}

StaPipBagPackagesBBox::~StaPipBagPackagesBBox() {
  delete bboxParts;
  delete mainBBox;
}

// Modified by TyraX: skinned meshes rewrite the same vertex buffer
// every frame - rebuild the boxes in place instead of a fresh cache entry.
void StaPipBagPackagesBBox::recalculate(const Vec4* t_vertices,
                                        const u32& t_maxVertCount) {
  u32 splitPartSize = t_maxVertCount / 3;
  for (u32 i = 0; i < partsCount; i++) {
    u32 partSize =
        i == partsCount - 1 ? vertexCount - i * splitPartSize : splitPartSize;
    (*bboxParts)[i] = CoreBBox(t_vertices + i * splitPartSize, partSize);
  }
  *mainBBox = RenderBBox(*bboxParts, 0, partsCount);
  rebuildCoarseBounds();
}

const RenderBBox& StaPipBagPackagesBBox::getChildBBox1By3(
    const u32& index) const {
  TYRA_ASSERT(index < partsCount,
              "Index out of range. Provided index: ", index);
  return static_cast<RenderBBox&>(bboxParts->at(index));
}

RenderBBox* StaPipBagPackagesBBox::getMainBBox() { return mainBBox; }

const u32& StaPipBagPackagesBBox::getPartsCount() const { return partsCount; }

const u32& StaPipBagPackagesBBox::getVertexCount() const { return vertexCount; }

RenderBBox StaPipBagPackagesBBox::createChildBBox(const u32& index,
                                                  const u16& partsSize) const {
  return RenderBBox(*bboxParts, index, index + partsSize);
}

// Modified by TyraX: min/max merge without building a RenderBBox - the
// packager classifies packages from these two corners alone.
void StaPipBagPackagesBBox::getMergedMinMax(const u32& index,
                                            const u16& partsSize, Vec4* outMin,
                                            Vec4* outMax) const {
  TYRA_ASSERT(index < partsCount && index + partsSize <= partsCount,
              "Merged bbox range out of parts. index: ", index,
              " partsSize: ", partsSize, " partsCount: ", partsCount);

  const CoreBBox* parts = bboxParts->data();
  Vec4::copy(outMin, parts[index].vertices[0].xyzw);
  Vec4::copy(outMax, parts[index].vertices[7].xyzw);

  for (u32 i = index + 1; i < index + partsSize; i++) {
    const Vec4& lo = parts[i].vertices[0];
    const Vec4& hi = parts[i].vertices[7];
    if (lo.x < outMin->x) outMin->x = lo.x;
    if (lo.y < outMin->y) outMin->y = lo.y;
    if (lo.z < outMin->z) outMin->z = lo.z;
    if (hi.x > outMax->x) outMax->x = hi.x;
    if (hi.y > outMax->y) outMax->y = hi.y;
    if (hi.z > outMax->z) outMax->z = hi.z;
  }
}

// Modified by TyraX: cache a coarse level; rebuilding follows bboxVersion.
// Eight full packages = 24 existing one-third bounds, including the tail.
void StaPipBagPackagesBBox::rebuildCoarseBounds() {
  const u32 groups = (partsCount + 23) / 24;
  coarseBounds.resize(groups * 2);
  for (u32 g = 0; g < groups; ++g) {
    const u32 start = g * 24;
    const u16 count = partsCount - start < 24 ? partsCount - start : 24;
    getMergedMinMax(start, count, &coarseBounds[g*2], &coarseBounds[g*2+1]);
  }
}

void StaPipBagPackagesBBox::print() const {
  auto text = getPrint(nullptr);
  printf("%s\n", text.c_str());
}

void StaPipBagPackagesBBox::print(const char* name) const {
  auto text = getPrint(name);
  printf("%s\n", text.c_str());
}

std::string StaPipBagPackagesBBox::getPrint(const char* name) const {
  std::stringstream res;
  if (name) {
    res << name << "(";
  } else {
    res << "StaPipBagPackagesBBox(";
  }
  res << std::fixed << std::setprecision(2);
  res << std::endl;
  res << "Vertices count: " << static_cast<int>(vertexCount) << std::endl;

  res << "Main CoreBBox: " << std::endl;
  res << mainBBox->getPrint() << std::endl;

  res << "Child BBoxes: " << std::endl;
  for (u32 i = 0; i < partsCount; i++) {
    res << i << ": " << bboxParts->at(i).getPrint();
    if (i != partsCount - 1) {
      res << std::endl;
    }
  }

  res << ")";
  return res.str();
}

}  // namespace Tyra
