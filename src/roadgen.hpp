#pragma once

#include <functional>
#include <vector>

// Roads (docs/roads.md): the spline tessellator.
//
// Host-only - no GL, no ImGui, no project.hpp - the vehiclesim shape, and for
// the same reason: this is the single source of truth for TWO consumers that
// must never disagree. The editor viewport previews a road through this exact
// function, and the generated PS2 runtime tessellates the same points at BOOT
// with its raw-string twin in templates.cpp (buildRoads). CHANGE ONE AND
// CHANGE BOTH - a road that previews half a metre off its console self is a
// road nobody can author.
//
// The economics this encodes: a road OBJECT is only its points, width and one
// texture name. All geometry is derived - sampled every ~2 units along a
// Catmull-Rom through the points, two vertices per sample glued to the
// caller's height function, V running along the arc length so ONE small
// texture tiles the whole street. A kilometre of road is a few hundred floats
// in the .tyra and one texture in VRAM.
namespace roadgen {

struct Vertex {
    float x, y, z;  // world, y from the height function + lift
    float u, v;     // u 0..1 across the width, v = arc length / texLen
};

// Ground height under a world XZ (the terrain, on both consumers).
using HeightFn = std::function<float(float x, float z)>;

// How far apart the spline is sampled, world units. Coarser than the terrain
// cell would let the road cut corners through relief; finer buys nothing.
inline constexpr float kSampleStep = 2.0f;
// One texture repeat every this many units of road.
inline constexpr float kTexLen = 4.0f;
// How far the surface floats above the terrain - enough to never z-fight,
// low enough that a wheel on the road reads as ON it.
inline constexpr float kLift = 0.05f;

// Tessellates `pointsXZ` (x0,z0,x1,z1,... - at least 2 points) into a
// triangle list, three Vertex per triangle, two triangles per segment.
// Endpoints are clamped (the spline passes through the first and last
// point). Returns the total arc length; `out` is cleared first.
float tessellate(const std::vector<float>& pointsXZ, float width,
                 const HeightFn& height, std::vector<Vertex>& out);

// The spline position alone (for the align-terrain pass and the editor's
// point handles): world XZ at parameter t in [0, 1] over the whole polyline.
void splineAt(const std::vector<float>& pointsXZ, float t, float* x, float* z);

}  // namespace roadgen
