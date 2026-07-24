#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "objparser.hpp"
#include "project.hpp"

// Baked ambient occlusion (docs/ambient-occlusion.md). Host-only, no GL - the
// decalproj/navmesh pattern. Three independent bakes, all folded into the same
// vertex colors the directional light already bakes into:
//
//  - terrainAO(): heightmap self-occlusion (ravines, foot of hills), a u8 grid
//    per terrain vertex. Codegen ships it as TERRAIN_AO_TABLES; the viewport
//    multiplies the same grid into its terrain chunk colors - both consume
//    identical data, no twin formula to drift.
//  - collectOccluders(): solid scene objects reduced to analytic occluders
//    (oriented boxes / spheres). The RESPONSE to these is computed per vertex
//    on the EE at scene load (templates.cpp aoOccludersAt/aoGroundAt) and per
//    fragment in the viewport shader (viewport.cpp FS) - those two are twins;
//    this function is the single source of the occluder SHAPES.
//  - modelAO(): raycast self-occlusion for imported .obj models, one u8 per
//    obj position index. texbake writes it as a "<model>.obj.ao" sidecar into
//    the .res-baked mirror (read by the engine's LeanObjLoader); the viewport
//    bakes the same values into its model vertex colors.
namespace aobake {

// One analytic occluder approximating a solid scene object. axis[k] is the
// object's local axis k in world space (rows of the world->local rotation);
// half = local half-extents. sphere: half[0] is the radius, axes unused.
struct Occluder {
    float pos[3] = {0, 0, 0};
    float axis[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    float half[3] = {0.5f, 0.5f, 0.5f};
    bool sphere = false;
    int objIndex = -1;  // authored scene-object index (self-exclusion)
};

// modelAabb: fill the local-space AABB of a Model object's mesh; return false
// to skip it (missing file, animated .glb - those relight dynamically and are
// never occluders).
using ModelAabbFn =
    std::function<bool(const SceneObject&, float mn[3], float mx[3])>;

// Solid authored objects -> occluder list, in object order. Markers, lights,
// decals, mirrors, portals and animated models cast nothing.
std::vector<Occluder> collectOccluders(const std::vector<SceneObject>& objects,
                                       const ModelAabbFn& modelAabb);

// Host reference of the occluder response formula at a surface point
// (0..1 occlusion; range = aoRadius). The generated game (aoOccluderAt in
// templates.cpp, per vertex at load) and the viewport fragment shader
// (aoOcclusion) are twins of THIS function - change one, change all three.
// The textured AO mode rasterizes it into maps/atlases here on the host.
float occluderOcclusionAt(const Occluder& oc, const float wp[3],
                          const float n[3], float range);

// --- the AO textures (how the bake ships) -----------------------------------

// A square, power-of-two occlusion image; alpha = strength * occlusion
// (255 = darken fully). Written as a black RGBA PNG whose alpha the GS
// alpha-over blend turns into an exact per-pixel multiply (Cd * (1 - a)).
struct AoImage {
    int size = 0;  // 0 = nothing to bake
    std::vector<uint8_t> alpha;
};

// Terrain AO map covering the full terrain extent: per-texel heightmap
// self-occlusion (the same horizon scan as terrainAO, on bilinear heights)
// plus the occluder contact term.
AoImage terrainAOMap(const std::vector<float>& heights, int w, int d,
                     float width, float depth,
                     const std::vector<Occluder>& occs, float radiusWorld,
                     float strength);

// One atlas region: normalized UV rect (inset by half a texel against
// bilinear bleed). A primitive's base-texture UVs map into it 1:1.
struct AtlasRect {
    float u0 = 0, v0 = 0, du = 0, dv = 0;
};

// Per-scene AO lightmap atlas for the primitives (box/sphere/cylinder/cone/
// plane/save point - types whose generated UV layout is known analytically).
// Regions follow the generated builders' emission order exactly: box 6 faces
// (+X,-X,+Y,-Y,+Z,-Z), sphere 1, cylinder 3 (side, +Y cap, -Y cap), cone 2
// (side, base), plane 2 (top, bottom). Deterministic - codegen emits the
// rects and texbake writes the pixels from two independent calls.
struct SceneAoAtlas {
    int size = 0;                  // atlas dimension, 0 = no atlas
    std::vector<uint8_t> alpha;    // size*size occlusion alpha
    std::vector<int> firstRegion;  // per authored object, -1 = not in atlas
    std::vector<AtlasRect> rects;  // flat regions, builder order
};
SceneAoAtlas bakeSceneAoAtlas(const Project& p, const SceneData& sc,
                              const ModelAabbFn& modelAabb);

// Terrain self-occlusion: an 8-direction horizon scan over the heightmap
// (w x d vertex grid, row-major [z*w+x], cell size stepX/stepZ world units).
// Scans up to radiusWorld out. Returns one byte per grid vertex, 255 = open
// sky, 0 = fully occluded.
std::vector<uint8_t> terrainAO(const std::vector<float>& heights, int w, int d,
                               float stepX, float stepZ, float radiusWorld);

// Raycast self-occlusion for an imported .obj model: a deterministic
// cosine-weighted hemisphere (24 rays) around the averaged vertex normal,
// traced against the model's own triangles (XZ-grid accelerated). Returns one
// byte per obj position index (objparser::Submesh::posIdx), 255 = open.
// Model-local, placement-independent - baked once per asset.
// CURRENTLY UNUSED (owner call, 2026-07): per-vertex occlusion on authored
// low-poly meshes reads as triangulated shading, so texbake no longer writes
// the .aov sidecars and the game stages models AO-off. Kept - with the
// LeanObjLoader sidecar reader - for a future per-model lightmap-unwrap path.
std::vector<uint8_t> modelAO(const objparser::Model& m);

// Sidecar IO ("TXAO" + u32 LE count + bytes). The engine-side reader lives in
// lean_obj_loader.cpp - keep the format in sync.
bool writeModelAoSidecar(const std::string& path,
                         const std::vector<uint8_t>& ao);

// Fast local-space AABB of an .obj (scans `v` lines only) - the ModelAabbFn
// for codegen, where parsing every placed model fully would be waste.
bool objAabb(const std::string& path, float mn[3], float mx[3]);

}  // namespace aobake
