#pragma once

#include <string>
#include <vector>

namespace objparser {

// Loads a Wavefront .obj as a flat triangle list: 6 floats per vertex
// (x, y, z, nx, ny, nz). Faces are fan-triangulated, normals are computed
// per face (vt/vn in the file are ignored). Returns false when the file
// cannot be read or contains no triangles.
bool load(const std::string& path, std::vector<float>& outPosNormal);

}  // namespace objparser
