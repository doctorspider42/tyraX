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
std::vector<uint8_t> modelAO(const objparser::Model& m);

// Sidecar IO ("TXAO" + u32 LE count + bytes). The engine-side reader lives in
// lean_obj_loader.cpp - keep the format in sync.
bool writeModelAoSidecar(const std::string& path,
                         const std::vector<uint8_t>& ao);

// Fast local-space AABB of an .obj (scans `v` lines only) - the ModelAabbFn
// for codegen, where parsing every placed model fully would be waste.
bool objAabb(const std::string& path, float mn[3], float mx[3]);

}  // namespace aobake
