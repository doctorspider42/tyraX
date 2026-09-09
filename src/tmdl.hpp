#pragma once

#include <string>
#include <vector>

// tmdl: the binary static-model format the generated game ships (".tmdl").
//
// Static .obj models used to be parsed as ASCII on the EE every time they
// loaded - a per-line std::istringstream, iostream float parsing, per-face
// normals, a std::map lookup per usemtl - which is a visible hitch inside a
// streaming layer's one-asset-per-frame job. Everything that parse computed
// is a pure function of build-time inputs, so the build resolves it once:
// triangulation, flat normals, the v flip, the material assignment (including
// a per-object .mtl override), atlas UV rects, and bin-relative texture
// paths. What is left at runtime is one sequential read plus a memcpy per
// part, because `Part::verts` is exactly the interleaved 8-float layout
// (position, normal, uv) the engine's LeanObjMaterial and the generated
// game's GameModelPart already hold.
//
// Written by templates::bakeStaticModels. KEEP THE LAYOUT IN SYNC with the
// runtime loader in
// vendor/tyra/engine/src/loaders/3d/tmdl_loader/tmdl_loader.cpp.
namespace tmdl {

// Bumped when the layout changes; the loader accepts a range of versions.
constexpr unsigned int kVersion = 3;  // 2 added Part::ke, 3 Model::shadowVerts

// One decimated variant of a part's mesh (same layout, fewer triangles),
// rendered instead of the full mesh beyond a distance. Baked by the build,
// see docs/model-pipeline.md.
struct Lod {
    std::vector<float> verts;       // interleaved 8 floats per vertex
    std::vector<unsigned char> ao;  // empty, or one byte per vertex
};

// One draw batch: all triangles of the model that share a material.
struct Part {
    std::string name;         // usemtl name (truncated to 31 chars in the file)
    std::string texture;      // map_Kd, bin-relative ("" = untextured)
    std::string reflTexture;  // refl sphere map, bin-relative;
                              // "@sky" = the engine's dynamic env map
    float kd[3] = {1.0f, 1.0f, 1.0f};
    // Ke: emission floor (docs/emissive-materials.md). Version 2 onward - a
    // v1 file reads back matte, which is what it was.
    float ke[3] = {0.0f, 0.0f, 0.0f};
    float reflStrength = 0.0f;
    bool reflRounded = false;       // -rounded: centroid-radial env normals
    std::vector<float> verts;       // flat triangle list, 8 floats per vertex
    std::vector<unsigned char> ao;  // empty, or one byte per vertex
    std::vector<Lod> lods;          // distance tiers, coarsest last (max 2)
};

struct Model {
    std::vector<Part> parts;   // first-use order of usemtl
    float min[3] = {0, 0, 0};  // AABB of the FULL mesh - collision, split band
    float max[3] = {0, 0, 0};  // and physics extents all read tier 0 only
    // Shadow proxy (version 3, meshlod::generateShadowProxy): positions only,
    // xyz per corner, a flat triangle list under the flashlight shadow
    // volumes' per-model triangle budget. Empty = the real mesh fits (or no
    // proxy was wanted); the game then casts from the real triangles, or from
    // its sub-boxes when they are over budget.
    std::vector<float> shadowVerts;
};

// Serializes the model. Packed little-endian host layout, no padding and no
// section table - the same conventions as .tskl (see glbparser::writeTskl),
// legitimate because both are derived artifacts re-baked with the engine
// they ship against:
//
//   "TMDL" u32 version
//   f32    min[3], max[3]
//   u32    partCount
//   partCount * {
//     char name[32]           // NUL-padded, truncating
//     char texture[64]
//     char reflTexture[64]
//     f32  kd[3]
//     f32  ke[3]              // version >= 2
//     f32  reflStrength
//     u32  flags              // bit0 = reflRounded
//     u32  vertexCount        // always a multiple of 3
//     f32  verts[vertexCount * 8]
//     u32  aoCount            // 0 = no baked AO, else == vertexCount
//     u8   ao[aoCount]
//     u32  lodCount
//     lodCount * { u32 vertexCount; f32 verts[vc*8]; u32 aoCount; u8 ao[] }
//   }
//   u32    shadowCornerCount    // version >= 3; a multiple of 3, 0 = none
//   f32    shadowXyz[shadowCornerCount * 3]
std::string write(const Model& m);

}  // namespace tmdl
