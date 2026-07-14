#pragma once

#include <vector>

#include "project.hpp"

// Projected decals: wrap a decal's texture onto the receiver geometry (walls,
// models, terrain) instead of drawing a flat quad. This is PURE HOST-SIDE
// geometry - it runs in the editor viewport (live preview) and in codegen
// (templates.cpp bakes the result into the game). NONE of it runs on the PS2:
// the console receives a finished static triangle list and draws it through the
// normal VU1 static-pipeline path, so no projection or clipping ever burdens
// the EE per frame.
//
// The projector volume is the decal object's oriented unit cube (its transform:
// scale = footprint width/height + projection depth, rotation, position). Every
// receiver triangle inside the cube and facing the decal (+Z) contributes a
// clipped fragment with a projected UV; fragments are nudged toward the decal
// along the surface normal so they sit in front instead of z-fighting.
namespace decalproj {

// World-space triangle list, 5 floats per vertex: pos(3) + uv(2). The decal's
// tint/material/alpha are applied by the caller (they are object properties),
// so the mesh itself carries only geometry + texture coordinates.
struct DecalMesh {
    std::vector<float> verts;
    bool truncated = false;  // hit the per-decal triangle cap (see kMaxTris)
};

// Hard cap on emitted triangles per decal - keeps the runtime draw (and the EE
// frustum-classify/clip it rides through) negligible. Beyond it, projection
// stops and `truncated` is set.
constexpr int kMaxTris = 4096;

// Projects `decal` onto the receivers of scene `s` in project `p`: terrain plus
// every solid object whose bounding box overlaps the projector volume (the
// decal itself, other decals and markers/lights are skipped). Returns an empty
// mesh when nothing overlaps. `decal.type` is expected to be Decal with
// decalProject set; the caller checks that.
DecalMesh project(const Project& p, const SceneData& s, const SceneObject& decal);

}  // namespace decalproj
