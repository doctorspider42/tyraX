#pragma once

#include <string>

namespace modelproxy {

// Builds a one-material convex footprint prism from a static OBJ. The proxy
// preserves the source XZ silhouette and height without inheriting its material
// splits, UV seams or triangle density. The returned path is project-relative.
bool bakeHull(const std::string& projectDir, const std::string& modelRel,
              const std::string& materialRel, const std::string& outputStem,
              std::string* outObjRel, float* outExtent, int* outTriangles,
              std::string* error);

}  // namespace modelproxy
