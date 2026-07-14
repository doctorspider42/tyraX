#pragma once

#include <string>
#include <vector>

namespace objparser {

// One draw batch of a model: all triangles that share a material. Vertices
// are a flat triangle list, 8 floats each (x, y, z, nx, ny, nz, u, v).
struct Submesh {
    std::string material;  // material name from usemtl ("" = no material)
    std::string texture;   // map_Kd path ("" = none); relative to the .mtl
                           // that defined it (the .obj's directory when the
                           // library came from mtllib/the sibling .mtl)
    float kd[3] = {1.0f, 1.0f, 1.0f};  // diffuse color, multiplies the object color
    std::string refl;           // refl sphere map ("" = not reflective)
    float reflStrength = 0.0f;  // reflection strength 0..1
    std::vector<float> verts;
};

struct Model {
    std::vector<Submesh> submeshes;  // first-use order of usemtl
    float min[3] = {0, 0, 0};        // AABB over all vertices
    float max[3] = {0, 0, 0};
    std::vector<std::string> mtlLibs;  // mtl files used (as referenced/implicit)
    int vertexCount() const {
        int n = 0;
        for (const Submesh& s : submeshes) n += (int)(s.verts.size() / 8);
        return n;
    }
};

// One material of a standalone .mtl library.
struct MtlMaterial {
    std::string name;
    std::string texture;  // map_Kd, relative to the .mtl's directory ("" = none)
    float kd[3] = {1.0f, 1.0f, 1.0f};
    float scale[2] = {1.0f, 1.0f};  // map_Kd -s (u, v) UV multiplier; 1 = as-is
    std::string refl;           // refl sphere map ("" = not reflective)
    float reflStrength = 0.0f;  // reflection strength 0..1 (refl -mm gain)
};

// Loads a Wavefront .obj (+ its .mtl material libraries, resolved relative to
// the .obj) split into per-material submeshes. A sibling .mtl named like the
// .obj is picked up implicitly, even without a mtllib line. When overrideMtl
// is non-empty it is a path to a material library that REPLACES the model's
// own libraries (mtllib + sibling are skipped) - usemtl names resolve against
// it, and the submesh texture paths are then relative to that file. Faces are
// fan-triangulated, normals are computed per face, `vt` texture coordinates
// are used when present (v is flipped to image space, missing = 0,0). Faces
// before any usemtl (or with an unknown material) land in a default white
// submesh. Returns false when the file cannot be read or has no triangles.
//
// Keep the parsing semantics in sync with the PS2 runtime loader in
// vendor/tyra/engine/src/loaders/3d/obj_loader/lean_obj_loader.cpp - the
// editor viewport and the game must see the same geometry and shading.
bool load(const std::string& path, Model& out,
          const std::string& overrideMtl = "");

// Parses a standalone .mtl library (newmtl/Kd/map_Kd/refl), materials in file
// order. Returns false when the file cannot be read or defines no materials.
bool loadMtl(const std::string& path, std::vector<MtlMaterial>& out);

// Legacy flat loader: all submeshes concatenated (8 floats per vertex).
bool load(const std::string& path, std::vector<float>& outPosNormalUv);

}  // namespace objparser
