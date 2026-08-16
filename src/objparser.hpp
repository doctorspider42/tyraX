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
    // Ke: emission (docs/emissive-materials.md). A brightness FLOOR the shaded
    // surface never drops below - the material stays lit in total darkness.
    // {0,0,0} = matte, lit like everything else.
    float ke[3] = {0.0f, 0.0f, 0.0f};
    // "# tyra-glow-light <range> <strength>": the material also BAKES light
    // into the geometry around it (docs/emissive-materials.md). range in world
    // units, 0 = lights nothing (the default).
    float glowRange = 0.0f;
    float glowLight = 1.0f;
    std::string refl;           // refl sphere map ("" = not reflective)
    float reflStrength = 0.0f;  // reflection strength 0..1
    bool reflRounded = false;   // -rounded: centroid-radial env normals
    std::vector<float> verts;
    // Resolved 0-based obj `v` index per emitted vertex (verts.size()/8
    // entries): corners sharing a position share the entry. The key the
    // baked-AO sidecar is indexed by (aobake::modelAO) - the PS2 loader
    // resolves the same indices while parsing faces.
    std::vector<int> posIdx;
};

struct Model {
    std::vector<Submesh> submeshes;  // first-use order of usemtl
    float min[3] = {0, 0, 0};        // AABB over all vertices
    float max[3] = {0, 0, 0};
    int positionCount = 0;           // obj `v` entries (posIdx range)
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
    float ke[3] = {0.0f, 0.0f, 0.0f};  // emission floor ({0,0,0} = matte)
    // The AUTHORED glow color from "# tyra-glow" - Ke with the white-hot core
    // already folded in, so it is the wrong thing to light a room with (an
    // overexposed emitter looks whiter, the light it casts does not change
    // hue). Falls back to Ke normalized by its brightest channel.
    float glowColor[3] = {1.0f, 1.0f, 1.0f};
    float glowRange = 0.0f;   // "# tyra-glow-light": baked light reach, 0 = none
    float glowLight = 1.0f;   // ...and its strength
    float scale[2] = {1.0f, 1.0f};  // map_Kd -s (u, v) UV multiplier; 1 = as-is
    std::string refl;           // refl sphere map ("" = not reflective)
    float reflStrength = 0.0f;  // reflection strength 0..1 (refl -mm gain)
    bool reflRounded = false;   // -rounded: centroid-radial env normals
};

// Loads a Wavefront .obj (+ its .mtl material libraries, resolved relative to
// the .obj) split into per-material submeshes. A sibling .mtl named like the
// .obj is picked up implicitly, even without a mtllib line. When overrideMtl
// is non-empty it is a path to a material library that REPLACES the model's
// own libraries (mtllib + sibling are skipped) - usemtl names resolve against
// it, and the submesh texture paths are then relative to that file. Faces are
// fan-triangulated, normals are DERIVED (`vn` is ignored): per face, then
// crease-smoothed across submeshes (meshlod::smoothNormals - a curved
// low-poly surface shades as a gradient, a hard edge stays hard). `vt`
// texture coordinates are used when present (v is flipped to image space,
// missing = 0,0). Faces before any usemtl (or with an unknown material) land
// in a default white submesh. Returns false when the file cannot be read or
// has no triangles.
//
// Keep the parsing semantics in sync with the PS2 runtime loader in
// vendor/tyra/engine/src/loaders/3d/obj_loader/lean_obj_loader.cpp - the
// editor viewport and the game must see the same geometry and shading. (The
// runtime .obj path stays flat-shaded: models ship as .tmdl, which bakes
// these smoothed normals in, so the game never re-derives them - see
// src/tmdl.hpp.)
bool load(const std::string& path, Model& out,
          const std::string& overrideMtl = "");

// Parses a standalone .mtl library (newmtl/Kd/Ke/map_Kd/refl), materials in
// file order. Returns false when the file cannot be read or defines no
// materials.
bool loadMtl(const std::string& path, std::vector<MtlMaterial>& out);

// Applies a material library as an OVERRIDE to an already-parsed model (a
// glbparser Baked or Skel - both expose .parts[i].{material,baseColor,image}
// and .images), mirroring load()'s overrideMtl rule for static .obj models:
// each part's material NAME is matched against the library; a hit replaces the
// part's baseColor (from Kd) and texture, a miss falls back to plain white and
// untextured (a full replace, not a merge - the built-in materials are dropped
// wholesale). Reflection (refl) has no skeletal-runtime slot and is ignored.
// The override's textures are read from the .mtl's directory and injected as
// PNG bytes into model.images (the same channel the file's own textures use),
// so both the .tskl bake (writes them next to the .tskl) and the viewport
// preview (decodes them to a GL texture) need no special case. Returns false
// (leaving the model untouched) when the library cannot be read or is empty.
template <class Model>
bool applyMaterialOverride(Model& model, const std::string& overrideMtlPath,
                          std::vector<std::string>* warnings = nullptr);

}  // namespace objparser
