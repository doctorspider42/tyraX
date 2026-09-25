#pragma once

#include <vector>

// Bake-time rigid-body shapes (docs/physics.md). Host-only, no GL, no
// project.hpp - the aobake/meshlod shape, so a harness can feed it a point
// cloud and check the hull, the planes and the mass properties it returns.
//
// A physics body collides as a small CONVEX HULL of its mesh, not as the mesh
// (a PS2 cannot test a rigid body's thousands of triangles against the world
// every frame) and not as its bounding box (a stool standing on four legs is
// not a crate). The hull is reduced to at most kMaxVerts SUPPORT POINTS - the
// extreme vertex of the mesh along a fixed set of directions - so the points
// a body can actually rest on (a stool's feet, a railing's post bottoms) are
// exactly the ones that survive. The mass properties are those of the SOLID
// hull at unit density: volume, centre of mass and the second-moment
// (covariance) matrix about that centre, from which the game derives the
// inertia tensor under any per-axis scale without re-baking.
namespace physhull {

constexpr int kMaxVerts = 24;
// A convex hull of V points has at most 2V - 4 triangular faces; coplanar
// faces merge into one plane, so this never truncates a hull of kMaxVerts.
constexpr int kMaxPlanes = 2 * kMaxVerts - 4;

struct Hull {
    bool ok = false;
    std::vector<float> verts;   // x y z per vertex, mesh-local units
    std::vector<float> planes;  // nx ny nz d per face: inside is n.x <= d
    float volume = 0.0f;
    float com[3] = {0.0f, 0.0f, 0.0f};
    // Second moment about com: integral of (x-com)(x-com)^T dV, stored as
    // xx yy zz xy xz yz.
    float cov[6] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
};

// Builds the hull of a point cloud (x y z per point). A flat or needle-thin
// cloud (a Plane primitive, a single quad) is thickened along its thin axes to
// 2% of its largest extent, so every result has a real volume. Deterministic:
// the same points give the same hull byte for byte.
Hull build(const std::vector<float>& points);

// Convenience: the positions of an 8-float-per-vertex triangle list (the
// objparser / primmesh layout).
std::vector<float> positionsOf(const std::vector<float>& verts8);

}  // namespace physhull
