#pragma once

#include <cstdint>
#include <vector>

// Flat binned-SAH bounding-volume hierarchy over a triangle soup. Host-only,
// no GL, no project.hpp - the smallest shared piece of the two raytracing
// bakers:
//
//  - matbake (docs/material-baking.md) traces ONE model's own surface detail
//    into its texture;
//  - gibake (docs/global-illumination.md) traces a WHOLE scene for baked
//    global illumination and the light probes.
//
// They used to be one builder living in matbake.cpp's anonymous namespace.
// Keeping a second copy for the scene bake would have meant two subtly
// different traversals answering the same question, so it moved here
// unchanged: same 8-bin SAH, same 4-triangle leaves, same Moller-Trumbore.
namespace bvh {

// A leaf holds count > 0 triangles starting at slot `left` of the reordered
// index list; an inner node holds count == 0 and its children at left /
// left + 1.
struct Node {
    float bmin[3], bmax[3];
    int32_t left = 0;
    int32_t count = 0;
};

struct Tree {
    std::vector<Node> nodes;
    std::vector<int32_t> order;  // triangle indices, leaf-contiguous
    // The triangle soup the tree indexes: 9 floats position + 9 floats
    // per-corner normal per triangle (hit-normal interpolation). Fill both
    // before calling build().
    std::vector<float> tv;
    std::vector<float> tn;
    float bmin[3] = {0, 0, 0}, bmax[3] = {0, 0, 0};
    int triCount() const { return (int)(tv.size() / 9); }
    bool empty() const { return nodes.empty(); }
};

struct Hit {
    float t = 0.0f, u = 0.0f, v = 0.0f;
    int tri = -1;
    bool back = false;  // struck the winding's back side
};

// tv/tn must be filled before the call.
void build(Tree& t);

// Moller-Trumbore. Unlike aobake's per-vertex variant this KEEPS grazing
// hits: ray origins are epsilon-offset off the surface, so the origin's own
// tangent faces never self-hit, and rejecting grazers would poke light leaks
// into shallow crevices.
bool rayTri(const float* o, const float* dir, const float* a, const float* b,
            const float* c, float tMax, Hit& h);

// Closest hit within tMax. acceptBack = false makes back sides transparent
// (traversal continues past them).
bool trace(const Tree& t, const float* o, const float* dir, float tMax,
           bool acceptBack, Hit& best);

}  // namespace bvh
