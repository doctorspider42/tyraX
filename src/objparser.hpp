#pragma once

#include <string>
#include <vector>

namespace objparser {

// Loads a Wavefront .obj as a flat triangle list: 8 floats per vertex
// (x, y, z, nx, ny, nz, u, v). Faces are fan-triangulated, normals are
// computed per face, `vt` texture coordinates are used when present
// (v is flipped to image space, missing = 0,0). Returns false when the
// file cannot be read or contains no triangles.
bool load(const std::string& path, std::vector<float>& outPosNormalUv);

}  // namespace objparser
