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
#include <math.h>
#include "debug/debug.hpp"
#include "renderer/3d/pipeline/static/core/bag/packaging/stapip_bag_packager.hpp"

namespace Tyra {

StaPipBagPackager::StaPipBagPackager() {}

StaPipBagPackager::~StaPipBagPackager() {}

void StaPipBagPackager::init(Renderer3DFrustumPlanes* t_frustumPlanes) {
  frustumPlanes = t_frustumPlanes;
}

/**
 * @brief Create render packages from provided render data
 *
 * @param size Max maxVertCount verts (VU1 buffer size)
 */
StaPipBagPackage* StaPipBagPackager::create(u16* o_size, StaPipBag* data,
                                            u16 size) {
  TYRA_ASSERT(size <= maxVertCount, "StaPipBagPackage can have max ",
              maxVertCount, " verts. Provided \"", size, "\"");

  *o_size = ceil(data->count / static_cast<float>(size));
  // Modified by tyra-editor: grow-only pool instead of new[] per submit.
  // Pool entries are reused, so pointers absent from this bag must be
  // reset - a stale sts/colors/normals from a previous bag would otherwise
  // leak into the fill path.
  if (bagPackagesPool.size() < *o_size) bagPackagesPool.resize(*o_size);
  StaPipBagPackage* result = bagPackagesPool.data();

  for (u16 i = 0; i < *o_size; i++) {
    result[i].bag = data;
    result[i].vertices = &data->vertices[i * size];

    result[i].sts =
        data->texture ? &data->texture->coordinates[i * size] : nullptr;

    result[i].colors =
        data->color->many
            ? reinterpret_cast<const Vec4*>(&data->color->many[i * size])
            : nullptr;

    result[i].normals =
        data->lighting ? &data->lighting->normals[i * size] : nullptr;

    result[i].indexOf1By3BBox = (i * size) / (maxVertCount / 3);

    if (i == *o_size - 1) {
      result[i].size = data->count - i * size;
    } else {
      result[i].size = size;
    }

    // Modified by tyra-editor: last 1/3 bbox the package overlaps.
    result[i].endIndexOf1By3BBox =
        (i * size + result[i].size - 1) / (maxVertCount / 3);

    result[i].isInFrustum = checkFrustum(result[i]);
  }

  return result;
}

/**
 * @brief Split render package to smaller packages
 *
 * @param size Max maxVertCount verts (VU1 buffer size)
 */
StaPipBagPackage* StaPipBagPackager::create(u16* o_count,
                                            const StaPipBagPackage& pkg,
                                            u16 size) {
  TYRA_ASSERT(size <= maxVertCount, "StaPipBagPackage can have max ",
              maxVertCount, " verts. Provided \"", size, "\"");

  *o_count = ceil(pkg.size / static_cast<float>(size));
  // Modified by tyra-editor: grow-only pool instead of new[] per split (see
  // the bag-level overload above; separate pool - the parent package array
  // is still alive during a split).
  if (splitPackagesPool.size() < *o_count) splitPackagesPool.resize(*o_count);
  StaPipBagPackage* result = splitPackagesPool.data();

  for (u16 i = 0; i < *o_count; i++) {
    result[i].bag = pkg.bag;
    result[i].vertices = &pkg.vertices[i * size];

    result[i].sts = pkg.bag->texture ? &pkg.sts[i * size] : nullptr;

    result[i].colors = pkg.bag->color->many ? &pkg.colors[i * size] : nullptr;

    result[i].normals = pkg.bag->lighting ? &pkg.normals[i * size] : nullptr;

    result[i].indexOf1By3BBox =
        pkg.indexOf1By3BBox + ((i * size) / (maxVertCount / 3));

    if (i == *o_count - 1) {
      result[i].size = pkg.size - i * size;
    } else {
      result[i].size = size;
    }

    // Modified by tyra-editor: last 1/3 bbox the subpackage overlaps (parent
    // packages always start on a 1/3 boundary).
    result[i].endIndexOf1By3BBox =
        pkg.indexOf1By3BBox +
        ((i * size + result[i].size - 1) / (maxVertCount / 3));

    result[i].isInFrustum = checkFrustum(result[i]);
  }

  return result;
}

CoreBBoxFrustum StaPipBagPackager::checkFrustum(const StaPipBagPackage& pkg) {
  if (!renderBBox) return CoreBBoxFrustum::OUTSIDE_FRUSTUM;

  if (pkg.size <= (maxVertCount / 3)) {  // Is subpackage
    // Modified by tyra-editor: a subpackage smaller than maxVertCount / 3
    // (VU1 clipping mode) can straddle a 1/3 bbox boundary - classify it
    // against the merged bbox of every part it overlaps.
    if (pkg.endIndexOf1By3BBox > pkg.indexOf1By3BBox) {
      auto bbox = renderBBox->createChildBBox(
          pkg.indexOf1By3BBox,
          pkg.endIndexOf1By3BBox - pkg.indexOf1By3BBox + 1);
      return bbox.clipFrustumCheck(frustumPlanes->getAll(),
                                   *pkg.bag->info->model);
    }
    auto& bbox = renderBBox->getChildBBox1By3(pkg.indexOf1By3BBox);
    return bbox.clipFrustumCheck(frustumPlanes->getAll(),
                                 *pkg.bag->info->model);
  } else {  // Is package
    const auto& indexOfPart = pkg.indexOf1By3BBox;
    auto partSize = ceil(pkg.size / static_cast<float>(maxVertCount / 3));
    auto bbox = renderBBox->createChildBBox(indexOfPart, partSize);
    return bbox.clipFrustumCheck(frustumPlanes->getAll(),
                                 *pkg.bag->info->model);
  }
}

void StaPipBagPackager::setMaxVertCount(const u32& count) {
  maxVertCount = count;
}

}  // namespace Tyra
