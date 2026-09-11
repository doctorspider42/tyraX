#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// meshlod: bake-time mesh decimation, shared by the animated (.tskl) and
// static (.tmdl) model bakes. Quadric-error half-edge collapse over a welded
// triangle list. Chosen shape:
//  - HALF-edge collapse (a snaps onto b, b keeps its attributes): normals,
//    uvs and skin bindings are never blended, so skinning stays valid by
//    construction and no new attribute values appear at any LOD;
//  - vertices are welded by their full attribute tuple first, which makes uv
//    and hard-normal seams distinct vertices - collapses cannot cross a
//    seam. Seam borders and open mesh borders are locked (never moved), so
//    silhouettes shrink from the inside out;
//  - collapses run in sorted-cost rounds instead of a mutating heap: within
//    a round each vertex participates in at most one collapse, then
//    adjacency and quadrics rebuild. Slightly worse than a true greedy heap,
//    a fraction of the code.
namespace meshlod {

// A welded triangle list: one entry per unique attribute tuple, plus indices.
// The skin arrays are present only for skinned input (animated models).
struct Mesh {
    std::vector<float> pos, nrm, uv;             // per welded vertex
    std::vector<unsigned char> joints, weights;  // per welded vertex * 4
    std::vector<uint32_t> tris;                  // 3 indices per triangle
    bool hasUv = false;
    bool hasSkin = false;
    size_t vertexCount() const { return pos.size() / 3; }
};

// Welds a flat triangle list held as separate attribute arrays. `uvs`,
// `joints` and `weights` may be null; `count` is the number of corners.
//
// `keyNormals` decides whether the normal is part of the identity. Keep it on
// for authored normals (skinned models), where a hard edge must stay a seam.
// Turn it OFF for a mesh whose normals are DERIVED per face - the static .obj
// path computes flat face normals and ignores `vn` entirely, so every corner
// of a position carries a different normal, every position looks like a seam
// twin, the collapse locks all of them and nothing decimates at all. Weld
// such a mesh by position (and uv) and recompute the normals afterwards with
// recomputeFaceNormals().
Mesh weld(const float* positions, const float* normals, const float* uvs,
          const unsigned char* joints, const unsigned char* weights,
          size_t count, bool keyNormals = true);

// Welds a flat triangle list of interleaved 8-float corners (the layout the
// static model path uses everywhere: x y z nx ny nz u v).
Mesh weldInterleaved(const float* verts, size_t count, bool keyNormals = true);

// Overwrites every triangle's three corner normals with its face normal, in
// place, over an interleaved 8-float triangle list. Same formula (and same
// degenerate fallback) as objparser/LeanObjLoader, so a decimated tier is
// flat-shaded exactly the way the full mesh is.
void recomputeFaceNormals(std::vector<float>& verts);

// Collapses the mesh down to at most `targetVerts` live welded vertices,
// rewriting `tris` (degenerate triangles are dropped, so the triangle count
// is not predictable from the vertex target).
//
// `lockBorders` (the default, what the draw LODs use) pins every open-border
// vertex so part outlines hold still. Off, a border vertex may collapse
// along its border under a perpendicular-plane penalty (Garland's boundary
// quadric), so the outline is preserved in the least-squares sense instead
// of frozen - which is what a SHADOW proxy of a non-watertight model needs:
// a game-ready prop is mostly open borders, and with them all locked the
// collapse stalls far above any useful triangle budget.
void decimate(Mesh& m, size_t targetVerts, bool lockBorders = true);

// Expands back to interleaved 8-float corners.
std::vector<float> unweldInterleaved(const Mesh& m);

// The tier policy, shared by both model bakes so animated and static models
// decimate to the same shape:
//  - kRatios are fractions of the WELDED vertex count;
//  - a mesh below kMinCorners corners gets no tiers at all (a decimated
//    variant of something this small saves nothing worth the RAM);
//  - a tier that failed to shrink to roughly its ratio ends the chain, and
//    so do all coarser ones.
constexpr float kRatios[] = {0.5f, 0.25f};
constexpr size_t kMinCorners = 192;
constexpr float kShrinkSlack = 0.15f;

// Runs the policy over one interleaved 8-float triangle list and returns the
// tiers, coarsest last (empty when the mesh is too small or would not shrink).
std::vector<std::vector<float>> generateTiers(const std::vector<float>& verts);

// Shadow proxy (docs/flashlight.md "The shadow"): the flashlight's shadow
// volumes silhouette-extrude a model's REAL triangles only up to
// kShadowProxyMaxTris per model - past that the EE classification stops being
// cheap and the caster used to fall back to its bounding sub-boxes, i.e. a
// rectangle on the wall. This bakes a positions-only stand-in under the
// budget: every part welded together BY POSITION (uv and normal seams do not
// exist for a shadow), borders unlocked, collapsed until the triangle count
// fits. Returns xyz per corner (9 floats per triangle), or empty when the
// model already fits (use the real mesh) or could not be brought under the
// budget (the caller keeps the box fallback). The game's kShadowMeshMaxTris is
// spliced from this constant, so the two can never disagree.
constexpr size_t kShadowProxyMaxTris = 1200;
std::vector<float> generateShadowProxy(
    const std::vector<const std::vector<float>*>& parts,
    size_t maxTris = kShadowProxyMaxTris);

}  // namespace meshlod
