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
// texture name. All geometry is derived - sampled at the road's authored
// 1..2-unit longitudinal spacing along a Catmull-Rom through the points and
// every ~0.5 unit across its width, every vertex glued to the caller's height
// function. V runs along the arc length so ONE small texture tiles the whole
// street. The authored data is still only a few hundred floats per kilometre
// in the .tyra and one texture in VRAM.
namespace roadgen {

struct Vertex {
    float x, y, z;  // world, y projected onto the height function
    float u, v;     // u 0..1 across the width, v = arc length / texLen
};

// A crossing of two sampled centre lines. `cornerXZ` is the convex overlap of
// the two road-width strips, ordered around the centre. Codegen stores these
// ten floats directly, so the PS2 never performs pairwise road detection.
struct Junction {
    float x = 0, z = 0;
    float cornerXZ[8] = {};
};

// Ground height under a world XZ (the terrain, on both consumers).
using HeightFn = std::function<float(float x, float z)>;

// Default distance between spline samples, in world units. Individual roads
// may opt into a coarser 1..2-unit spacing; the one-unit default remains the
// safe choice for sharp terrain folds.
#ifndef TYRA_ROAD_SAMPLE_STEP
#define TYRA_ROAD_SAMPLE_STEP 1.0f
#endif
inline constexpr float kSampleStep = TYRA_ROAD_SAMPLE_STEP;
inline constexpr float kArcSampleStep = 1.0f;
// A two-edge strip spans an entire road with one plane. On a terrain cell
// wider than the strip's lift that plane can pass below the heightfield in
// the middle, showing grass triangles through the asphalt. Subdivide across
// the road as well, so the generated surface follows the ground it projects
// onto rather than merely touching it at both shoulders.
inline constexpr float kCrossSampleStep = 0.5f;
// One texture repeat every this many units of road.
inline constexpr float kTexLen = 4.0f;
// How far the surface floats above the terrain - enough to never z-fight,
// low enough that a wheel on the road reads as ON it.
inline constexpr float kLift = 0.12f;

// The two budgets that decide how coarsely a station pair may be stitched
// laterally (roadgen.cpp, spanCuts). Both are world units and both are
// measured against the DENSE sampling. THEY ARE NOT THE SAME KIND OF NUMBER,
// which is the whole reason there are two of them:
//
//   - `kSpanFlatness` bounds how far a dense sample may sit off the merged
//     quad's plane. It is the SURFACE error, and - because neighbouring
//     station pairs cut the row they share independently - it is also the
//     size of the T-vertex seam a merge can open. At 1e-5, the float noise
//     floor at district coordinates, both are exactly zero: a row's samples
//     lie on a straight line in XZ, so coplanar implies collinear and the
//     shared segment has ONE representation. Raising it buys a great deal of
//     geometry and starts opening cracks; the sweep in docs/roads.md prices
//     both halves and this repository has not accepted that trade.
//
//   - `kSpanShear` bounds the quad's parallelogram defect. The two triangles
//     of a trapezoid interpolate ST with two different affine maps, so this is
//     a pure UV error - the surface and the seams are untouched by it, which
//     is why it is the budget that was relaxed. It is what a BEND trips, and
//     on the district's curved splines it is what was refusing every merge.
//
// 0.05 is the measured knee: the reduction saturates just past it, and the
// worst UV drift it causes anywhere in the Motor District is 0.36 of a texel
// on the 128-pixel road texture. docs/roads.md, "The lateral budget", carries
// the sweep, the error at each setting and the harness that produced them.
#ifndef TYRA_ROAD_SPAN_FLATNESS
#define TYRA_ROAD_SPAN_FLATNESS 0.00001f
#endif
#ifndef TYRA_ROAD_SPAN_SHEAR
#define TYRA_ROAD_SPAN_SHEAR 0.05f
#endif
inline constexpr float kSpanFlatness = TYRA_ROAD_SPAN_FLATNESS;
inline constexpr float kSpanShear = TYRA_ROAD_SPAN_SHEAR;

// Tessellates `pointsXZ` (x0,z0,x1,z1,... - at least 2 points) into a
// triangle list, three Vertex per triangle, two triangles per longitudinal /
// lateral cell.
// Horizontal pairs of rows collapse to one full-width quad after all interior
// heights have been checked; uneven terrain retains every lateral cell.
// Endpoints are clamped (the spline passes through the first and last
// point). Returns the total arc length; `out` is cleared first.
// `lifts` is retained only for source/format compatibility with the short-lived
// raised-road authoring pass. It is ignored: roads are terrain decals and every
// generated vertex is projected onto the height function.
float tessellate(const std::vector<float>& pointsXZ, float width,
                 const HeightFn& height, std::vector<Vertex>& out,
                 const std::vector<float>& lifts = {},
                 float sampleStep = kSampleStep);

// --- triangle strips (docs/model-pipeline.md, "Triangle strips") ------------
//
// A road is a ribbon over a regular grid, and a grid strips PROPERLY. One
// station pair of n lateral cells is 6n list vertices and 2(n + 1) strip ones:
// on the district's 13-unit streets (crossSteps 26) that is 156 against 54,
// a 0.346x count, where the flat-shaded baked models only reached 0.732x.
// Every EE term of render submission scales with the VU1 PACKAGE count, which
// scales with vertices, so this is the lever the .tmdl bake already pulled -
// aimed at the 93 150 road vertices that are the rest of the frame.
//
// meshstrip is deliberately NOT reused here, for three reasons in order of
// weight:
//   - THE TWIN RUNS ON THE EE. buildRoads tessellates the whole district at
//     SCENE LOAD on the PlayStation 2, and meshstrip is an exact-bytes weld
//     hash over every corner, an edge-adjacency multimap, and a six-
//     orientation greedy walk per seed. A grid's optimal strip is known in
//     closed form, so that search would buy nothing at a price the EE cannot
//     pay at all.
//   - meshstrip's weld key is the 32 bytes of an 8-float BAKED vertex. A road
//     vertex is position + UV (this Vertex), and its colour lives in a
//     parallel array on the runtime side - there is no such key to hash.
//   - the ordering subtlety meshstrip found the hard way (a strip's trailing
//     pair is ORDERED, so a seed has six orientations and picking from three
//     gives 201 strips of mean length 4 on a 200-cell row) is exactly what
//     the closed form cannot get wrong: the ribbon's rows ARE the strip.
//
// What IS reused is meshstrip's run CONTRACT, because StaPipCore slices a
// stripped bag the same way whatever produced it: runs of exactly kStripRun
// vertices, every one a self-contained strip, padded with repeats of the last
// vertex, separate strips inside a run joined by repeating a vertex either
// side of the seam, and every run length a multiple of 3.
inline constexpr int kStripRun = 75;  // == meshstrip::kRun, asserted in the .cpp

// Chunking, and the reason it belongs in this header now. TWIN NOTICE: the
// generated buildRoads carries these as literals. They used to matter only to
// the runtime, because the list emitter produced one flat vertex sequence
// that chunking merely CUT. A run may not straddle a chunk, so with strips
// the chunk boundaries move padding into the array and the host has to agree
// about where they fall or the twins no longer produce the same vertices.
inline constexpr int kChunkSpans = 36;    // at most this many station pairs
inline constexpr int kChunkBudget = 1800; // ... and this many vertices

// The same surface as tessellate(), emitted as triangle STRIP runs and cut
// into the same chunks the generated runtime builds, so the two can be
// compared vertex for vertex (examples/vehicle-playground/authoring/
// verify-road-twins.py). Every triangle of tessellate() is present, split
// along the SAME diagonal - a ribbon row pair walks N[0], P[0], N[s], P[s],
// ..., whose successive triples are that row's quads cut P[j]-N[j+s], which
// is the cut the list stitch makes. Winding parity alternates, as it does in
// any strip; nothing in this engine backface-culls.
//
// `chunkSizes`, when given, receives one vertex count per chunk (they sum to
// out.size()). Returns the total arc length; `out` is cleared first.
float tessellateStrips(const std::vector<float>& pointsXZ, float width,
                       const HeightFn& height, std::vector<Vertex>& out,
                       std::vector<int>* chunkSizes = nullptr,
                       float sampleStep = kSampleStep);

// Find centre-line crossings and turn each into four terrain-projected
// triangles. Near-parallel crossings are rejected because their strip overlap
// grows without bound; repeated hits within one road width are deduplicated.
void findJunctions(const std::vector<float>& aPoints, float aWidth,
                   const std::vector<float>& bPoints, float bWidth,
                   std::vector<Junction>& out);
void tessellateJunction(const Junction& junction, const HeightFn& height,
                        std::vector<Vertex>& out);

// The spline position alone (for the align-terrain pass and the editor's
// point handles): world XZ at parameter t in [0, 1] over the whole polyline.
void splineAt(const std::vector<float>& pointsXZ, float t, float* x, float* z);

}  // namespace roadgen
