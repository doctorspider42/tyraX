#pragma once

#include <string>
#include <vector>

namespace objparser {

// One draw batch of a model: all triangles that share a material. Vertices
// are a flat triangle list, 8 floats each (x, y, z, nx, ny, nz, u, v).
struct Submesh {
    std::string material;  // material name from usemtl ("" = no material)
    std::string texture;   // map_Kd path, relative to the .obj's directory ("" = none)
    float kd[3] = {1.0f, 1.0f, 1.0f};  // diffuse color, multiplies the object color
    std::vector<float> verts;
};

struct Model {
    std::vector<Submesh> submeshes;  // first-use order of usemtl
    float min[3] = {0, 0, 0};        // AABB over all vertices
    float max[3] = {0, 0, 0};
    std::vector<std::string> mtlLibs;  // mtllib file names as written in the .obj
    int vertexCount() const {
        int n = 0;
        for (const Submesh& s : submeshes) n += (int)(s.verts.size() / 8);
        return n;
    }
};

// Loads a Wavefront .obj (+ its .mtl material libraries, resolved relative to
// the .obj) split into per-material submeshes. A sibling .mtl named like the
// .obj is picked up implicitly, even without a mtllib line. Faces are fan-triangulated,
// normals are computed per face, `vt` texture coordinates are used when
// present (v is flipped to image space, missing = 0,0). Materials carry the
// Kd diffuse color and the map_Kd texture path; faces before any usemtl (or
// with an unknown material) land in a default white submesh. Returns false
// when the file cannot be read or contains no triangles.
//
// Keep the parsing semantics in sync with the PS2 runtime loader in
// vendor/tyra/engine/src/loaders/3d/obj_loader/lean_obj_loader.cpp - the
// editor viewport and the game must see the same geometry and shading.
bool load(const std::string& path, Model& out);

// Legacy flat loader: all submeshes concatenated (8 floats per vertex).
bool load(const std::string& path, std::vector<float>& outPosNormalUv);

}  // namespace objparser
