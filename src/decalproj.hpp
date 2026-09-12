#pragma once

#include <functional>
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

// Which receivers a projection is allowed to land on. The defaults are what an
// authored decal has always done; shadowbake narrows both (docs/shadows.md):
// a baked shadow must skip the caster itself, anything outside the caster's
// streaming layer, and any surface whose GI lightmap already holds that same
// sun shadow - projecting onto one darkens it twice.
struct Receivers {
    // null = every solid object, i.e. today's behaviour. Called once per
    // candidate object, before its geometry is read.
    std::function<bool(const SceneObject&)> accept;
    bool terrain = true;
};

// Projects `decal` onto the receivers of scene `s` in project `p`: terrain plus
// every solid object whose bounding box overlaps the projector volume (the
// decal itself, other decals and markers/lights are skipped). Returns an empty
// mesh when nothing overlaps. `decal.type` is expected to be Decal with
// decalProject set; the caller checks that.
DecalMesh project(const Project& p, const SceneData& s, const SceneObject& decal,
                  const Receivers& rx = Receivers());

// The world-space triangles one object is made of, exactly as the projection
// reads them: primitives through primmesh, static `.obj` through objparser.
// Nine floats per triangle, appended. False = this object contributes none (an
// animated model, a marker, a light, a decal).
//
// Exposed so the shadow bake can occlude with the SAME tessellation the
// projection lands on - a second one in shadowbake would be a second answer to
// "what shape is this object", and the two would drift on the day a primitive
// gains a segment.
bool objectTriangles(const Project& p, const SceneObject& o,
                     std::vector<float>& out);

// The projector's world origin and orthonormal basis, as `project` itself
// derives them from the object's Euler rotation. Exposed for a caller that
// BUILDS a projector rather than reading an authored one (shadowbake): the
// texture it bakes has to be laid out in exactly the frame the projection will
// sample it in, and two answers to "where is this decal's +X" would be a
// mirrored or rotated image with nothing to point at.
//
// Local +Z is the decal's facing, +Y its image up, and u runs with local -X
// (the slide-projector convention `project` emits).
void projectorBasis(const SceneObject& decal, float origin[3], float axisX[3],
                    float axisY[3], float axisZ[3]);

}  // namespace decalproj
