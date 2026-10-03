#pragma once

#include <algorithm>
#include <functional>
#include <string>
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

// Closed roads keep one repeated endpoint in the existing point array.
// Three distinct controls minimum; the repeated endpoint is not an edit handle.
inline bool isClosed(const std::vector<float>& points) {
    return points.size() >= 8 && points[0] == points[points.size() - 2] &&
           points[1] == points.back();
}
inline int controlCount(const std::vector<float>& points) {
    return (int)(points.size() / 2) - (isClosed(points) ? 1 : 0);
}
inline void moveControl(std::vector<float>& points, int i, float x, float z) {
    const bool closed = isClosed(points);
    points[(size_t)i * 2] = x;
    points[(size_t)i * 2 + 1] = z;
    if (closed && i == 0) {
        points[points.size() - 2] = x;
        points.back() = z;
    }
}
inline bool removeControl(std::vector<float>& points, int i) {
    const bool closed = isClosed(points);
    const int count = controlCount(points);
    if (i < 0 || i >= count || count <= (closed ? 3 : 2)) return false;
    points.erase(points.begin() + (size_t)i * 2,
                 points.begin() + (size_t)i * 2 + 2);
    if (closed) {
        points[points.size() - 2] = points[0];
        points.back() = points[1];
    }
    return true;
}

struct Vertex {
    float x, y, z;  // world, y projected onto the height function
    float u, v;     // u 0..1 across the width, v = arc length / texLen
};

// A junction footprint. `outline` (x0,z0,x1,z1,...) is the node polygon the
// planner builds (1.170.0, docs/roads.md "Road nodes"): every arm cut off
// square at its trim distance, neighbouring arms joined by a fillet arc. It
// is ordered counter-clockwise seen from above and star-shaped about (x, z),
// so the patch is a fan from the centre. `cornerXZ` is the legacy four-corner
// overlap findJunctions() still produces; the patch uses it only when the
// outline is empty. The PS2 never performs road detection.
struct Junction {
    float x = 0, z = 0;
    float cornerXZ[8] = {};
    std::vector<float> outline;
    // Per outline point: 1 when the segment from it to the next is an arm's
    // CAP (the road carries on there), 0 for a road edge or fillet.
    std::vector<unsigned char> outlineCap;
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

// Road RANK (1.143.0, docs/roads.md "Crossings"): 0 track, 1 local (the
// default, every road authored before ranks), 2 main. A higher rank sits
// this much higher, so where two roads of different rank cross, the higher
// one covers the lower without z-fighting and every "highest surface"
// query (the wheels, the grip) answers the higher road. Equal ranks keep
// the junction patches of the intersection material.
inline float rankLift(int rank) { return (float)(rank - 1) * 0.03f; }
// How far above the road under it a spill patch floats.
inline constexpr float kSpillLift = 0.02f;
inline constexpr float kSpillDefault = 1.5f;

// A SPILL (1.143.0): where a road crosses a higher-rank one, its surface
// carries on over the higher road's edge for `spill` units and fades out -
// mud trailed onto the asphalt. XZ + the low road's own UV + the fade alpha
// (1 at the higher road's edge, 0 `spill` units in). The consumer puts Y on
// it from the surface under it: the patch is a decal on the higher road.
struct SpillVertex {
    float x, z, u, v, a;
};
// Triangles of the LOW road that lie on the HIGH road within `spill` of its
// edge, with their fade. Empty when the two do not overlap. Deterministic:
// the codegen bakes this for the console and the editor draws the same.
// `lowEdgeFade` (1.144.0): the low road's soft edge carries onto the spill,
// so the mud on the asphalt has soft sides too (the spill is then built on
// the dense 0.5-unit lateral grid instead of the reduced one).
void tessellateSpill(const std::vector<float>& lowPts, float lowWidth,
                     float lowSampleStep, const std::vector<float>& highPts,
                     float highWidth, float spill, std::vector<SpillVertex>& out,
                     float lowEdgeFade = 0.0f);

// EDGE FADE (1.144.0, docs/roads.md "Soft edges"): the road's outer `fade`
// units on each side are drawn as blended bands whose alpha falls from 1 at
// the core to 0 at the authored edge, so a dirt track bleeds into the
// terrain. The fade snaps to the tessellator's lateral grid (width /
// ceil(width / 0.5)); the CORE is the rest, tessellated at coreWidth with
// uInset so its texture still spans the full width, and its outer vertices
// are exactly the bands' inner ones - no crack.
struct EdgeFade {
    float coreWidth = 0.0f;
    float uInset = 0.0f;
    int columns = 0;  // lateral cells per band; 0 = no fade
};
EdgeFade edgeFadeFor(float width, float fade);
// The two bands as triangles (XZ, full-width UV, alpha), wound like the road.
// Empty when edgeFadeFor gives no columns.
void tessellateEdges(const std::vector<float>& pointsXZ, float width,
                     float sampleStep, float fade, std::vector<SpillVertex>& out);

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
// `uInset` (1.144.0, edge fade): the road's U runs uInset..1-uInset across
// this width instead of 0..1 - the CORE of a road whose outer bands are the
// faded edge (tessellateEdges), so the texture still spans the full width.
float tessellate(const std::vector<float>& pointsXZ, float width,
                 const HeightFn& height, std::vector<Vertex>& out,
                 const std::vector<float>& lifts = {},
                 float sampleStep = kSampleStep, float uInset = 0.0f);

// Conforming junction patch: sample the actual road triangles, refine the
// shared fan grid, then bound clearance at every triangle intersection.
// XYZUV is baked on the host; the EE only uploads it at scene load.
// The terrain's render grid: node (i, k) at (x0 + i*dx, z0 + k*dz), each cell
// split along the diagonal terrainHeight() uses. Default = unknown.
struct TerrainGrid {
    float x0 = 0.0f, z0 = 0.0f, dx = 0.0f, dz = 0.0f;
};
TerrainGrid terrainGridOf(int columns, int rows, float width, float depth);
// A node patch (1.170.0) that a fan from the centre cannot fit is cut along
// `grid` instead, so every piece lies in one ground plane; pass the scene's
// grid (an unknown one falls back to a 2-unit grid).
void tessellateJunctionSurface(const Junction& junction,
    const std::vector<Vertex>& roads, const HeightFn& terrain, float lift,
    std::vector<Vertex>& out, const TerrainGrid& grid = {});
// Render-grid interpolation (the two terrain triangles, not bilinear height).
float terrainHeight(const std::vector<float>& heights, int columns, int rows,
                    float width, float depth, float x, float z);

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
inline constexpr float kChunkTexRange = 16.0f; // bounded V even at 2m spacing
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
                       float sampleStep = kSampleStep, float uInset = 0.0f);

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

// The DRAWN road surface under a world XZ, for host code that must stand on
// it: the highest road or junction triangle containing the point. The host
// twin of the generated TerrainGame::roadSurfaceAt (the same barycentric test
// and tolerance over the same triangles; a uniform grid instead of the
// runtime's prefix-offset one, which only changes how fast the answer comes).
// The editor's vehicle test drive is the consumer: without it the host car
// sat on the terrain 0.12 under every road, like the console one did
// (docs/vehicles.md, "Wheels on the road surface").
class Surface {
public:
    // A triangle LIST (three Vertex per triangle): tessellate() and
    // tessellateJunction() output. Call build() once after the last add().
    void add(const std::vector<Vertex>& triangles, float grip = 1.0f);
    // Triangles with a grip PER VERTEX (a spill patch: its fade blends the
    // low road's grip over the one under it). `grips` is one per vertex.
    void addBlended(const std::vector<Vertex>& triangles,
                    const std::vector<float>& grips);
    // A faded EDGE band: `covers` (per vertex) is how much of the road a
    // tyre is on - its fade alpha - so the grip there blends toward the
    // terrain's.
    void addEdge(const std::vector<Vertex>& triangles, float grip,
                 const std::vector<float>& covers);
    void build();
    bool empty() const { return tris_.empty(); }
    // kNone when no triangle covers (x, z). `grip`, when given, receives the
    // grip of the triangle that answered (1 when none did); `cover` how much
    // of the road is there (1, or a faded edge's alpha).
    float at(float x, float z, float* grip = nullptr, float* cover = nullptr) const;
    static constexpr float kNone = -1.0e30f;

private:
    std::vector<Vertex> tris_;
    std::vector<float> grip_;   // one per VERTEX, interpolated by at()
    std::vector<float> cover_;  // one per VERTEX: 1, or an edge band's fade
    std::vector<unsigned> cellStart_, cellItems_;
    int nx_ = 0, nz_ = 0;
    float minX_ = 0, minZ_ = 0, inv_ = 1;
};

// --- crossings: the one decision (1.145.0, docs/roads.md "Junction overrides")
//
// Which crossing gets a patch, which road runs through and who spills onto
// whom used to be decided three times - the codegen (the console's tables),
// the viewport and the vehicle test drive - by three copies of the same
// pairing loops. planCrossings() is now the ONLY place, and the three read
// its result. It works per SCENE: callers hand it that scene's roads.
//
// A crossing is identified by its PAIR of road ids plus its position, so a
// per-junction override survives small point edits: it matches the crossing
// of the same pair nearest to where it was stored, within the narrower road's
// width. One that matches nothing is ORPHANED - kept, and reported.

// One road as the planner sees it.
struct CrossingRoad {
    std::string id;            // the SceneObject's stable id
    std::vector<float> points; // x0,z0,x1,z1,...
    float width = 8.0f, sampleStep = 1.0f, grip = 1.0f;
    float spill = kSpillDefault, edgeFade = 0.0f;
    int rank = 1;
    std::string intersection;  // intersection material ("" = none)
    int markings = 1;          // RoadMarkings: what this road's node arms get painted
    // Where this road's texture paints its edge line, U across the width - so
    // a node's painted edge line meets it. The default is the Motor District's
    // texture (columns 5..8 of 128); a generated texture's recipe says its own
    // (project::crossingRoads reads it). edgeLine false = the texture has none.
    bool edgeLine = true;
    float edgeU0 = 5.0f / 128.0f, edgeU1 = 8.0f / 128.0f;
    // Kerbs (docs/roads.md "Kerbs"): only planKerbs reads these.
    bool kerb = false;
    float kerbHeight = 0.15f, kerbWidth = 0.25f;
    // Details (docs/roads.md "Road details"): only roaddetail reads these.
    float details = 0.0f;
    int detailSeed = 0;
};

// What a road's arms get painted at its nodes (1.171.0, docs/roads.md
// "Markings"). A stop line marks the road that GIVES WAY: one ending at a node
// another road runs through (a T), or at a crossing the lower-ranked /
// narrower road. Crossings are zebras across the arm just past the patch.
// The markings' paint colour, 0xRRGGBB in the untextured 0..255 range.
inline constexpr int kMarkingRgb = 0xE8E8E0;
enum RoadMarkings : int {
    kMarkNone = 0,
    kMarkStopLines = 1,
    kMarkCrossings = 2,  // stop lines + zebra crossings
};

// What an override says the crossing does. Auto = the rank rule.
enum JunctionWinner : int {
    kWinnerAuto = 0,   // the rank rule (and the intersection-material patch)
    kWinnerPatch = 1,  // a junction patch, whatever the ranks and materials
    kWinnerRoadA = 2,  // road A runs through, B is covered (and may spill)
    kWinnerRoadB = 3,
};
// Stored in the scene (SceneData::roadJunctions). Fields at their Auto value
// change nothing; a material alone forces a patch.
struct JunctionOverride {
    std::string roadA, roadB;  // object ids (A/B as stored; order is free)
    float x = 0.0f, z = 0.0f;  // where the crossing was when last edited
    int winner = kWinnerAuto;
    std::string material;      // patch material, "" = the roads' own
    float grip = 0.0f;         // 0 = auto (the lower road's / the winner's)
};
inline bool operator==(const JunctionOverride& a, const JunctionOverride& b) {
    return a.roadA == b.roadA && a.roadB == b.roadB && a.x == b.x && a.z == b.z &&
           a.winner == b.winner && a.material == b.material && a.grip == b.grip;
}
inline bool operator!=(const JunctionOverride& a, const JunctionOverride& b) {
    return !(a == b);
}

enum CrossingKind : int {
    kCrossOverlap = 0,  // equal ranks, no patch: the roads simply overlap
    kCrossPatch = 1,    // a fitted patch (tessellateJunctionSurface)
    kCrossThrough = 2,  // `winner` runs through, the other is covered
};

// One arm of a road node: where the node's outline cut it and which way it
// leaves - what anything painted on the node (markings) is laid out from.
struct NodeArm {
    int road = -1;
    bool ends = false;          // the road ends here (else it runs through)
    float h = 1.0f;             // half width
    float trim = 0.0f;          // the cap's distance from the node, along the road
    float capX = 0.0f, capZ = 0.0f;  // the cap's centre
    float tx = 1.0f, tz = 0.0f;      // the road's outward direction there
};

struct Crossing {
    int a = -1, b = -1;  // road indices into the planner's input, a < b
    // Every road meeting at this node, ascending (a and b are its first two).
    // A plain crossing has two; a three-road fork or a five-way plaza more.
    std::vector<int> roads;
    int arms = 0;        // how many road ends leave the node (X = 4, T/Y = 3)
    // A TRANSITION node (1.171.0): two roads joined in line with different
    // widths; its patch is the taper between them.
    bool transition = false;
    std::vector<NodeArm> armList;  // per arm, in angular order
    Junction shape;
    int override = -1;   // index into the overrides, or -1 (Auto)
    int kind = kCrossOverlap;
    int winner = -1;     // road index for kCrossThrough
    bool patchDuplicate = false;  // a patch another crossing already makes
    // The patch (kCrossPatch): material key, grip and the rank lift it sits at.
    std::string material;
    float grip = 1.0f;
    float lift = 0.0f;
    // kCrossThrough decided by an override: the winner is drawn over the
    // loser here as an OVERLAY decal. Its grip.
    bool overlay = false;
    float overlayGrip = 1.0f;
    bool has(int road) const {
        return std::find(roads.begin(), roads.end(), road) != roads.end();
    }
};

// ROAD NODES (1.170.0, docs/roads.md "Road nodes"): where roads meet - a
// centre-line crossing, an open end resting on another road (a T, or a fork
// at any angle) or two ends sharing a spot (a corner) - clustered into ONE
// node per place, however many roads meet there. Each node knows its arms
// (one per road end leaving it) and carries the filleted outline in
// `shape.outline`. Kind/material/overrides are planCrossings' business; this
// is the geometry alone, in a deterministic order.
std::vector<Crossing> findNodes(const std::vector<CrossingRoad>& roads);

// A spill or an overlay: a road's own triangles laid over another road.
struct CrossingDecal {
    int road = -1;   // whose surface (texture, UV, colour)
    int under = -1;  // the road it lies on
    bool overlay = false;
    std::vector<SpillVertex> verts;  // triangles; alpha 1 = this road's surface
    float grip = 1.0f;      // at alpha 1
    float baseGrip = 1.0f;  // at alpha 0 (the surface under it)
    // Above the highest road under it, BEYOND kSpillLift: kSpillLift for a
    // spill that lands on an overlay, 0 otherwise. The console adds it to its
    // roadSurfaceAt; hosts use hostLift (rank lift + kSpillLift + this).
    float extraLift = 0.0f;
    float hostLift = 0.0f;
};

struct CrossingPlan {
    std::vector<Crossing> crossings;
    std::vector<CrossingDecal> decals;  // overlays first, then spills
    std::vector<int> overrideCrossing;  // per override: crossing index or -1
    int orphans = 0;                    // overrides matching no crossing
};

// `withDecals` false skips the spill/overlay tessellation (markers and the
// Properties panel only need the crossings).
CrossingPlan planCrossings(const std::vector<CrossingRoad>& roads,
                           const std::vector<JunctionOverride>& overrides,
                           bool withDecals = true);

// The plan's node MARKINGS (1.171.0) as white-paint triangles (XYZ, UV 0),
// laid onto `surface` - the drawn roads plus the node patches - kSpillLift
// above it, split where the surface bends so the paint follows it. Untextured;
// the consumer gives them one colour. Deterministic, crossing order.
void bakeMarkings(const CrossingPlan& plan, const std::vector<CrossingRoad>& roads,
                  const Surface& surface, std::vector<Vertex>& out);

// The plan's patches and decals as drawn surface (the test drive, the check):
// `terrain` is the bare ground height.
void addCrossingsToSurface(Surface& s, const std::vector<CrossingRoad>& roads,
                           const CrossingPlan& plan, const HeightFn& terrain,
                           const TerrainGrid& grid = {});

// --- kerbs (docs/roads.md "Kerbs") -----------------------------------------
//
// A kerb is a two-face profile swept along a line: a vertical FACE at the
// line from just below the road surface up to the kerb height, and the flat
// TOP from the line outward by the kerb width. No bottom and no back face:
// nobody sees them, and nothing in this engine backface-culls. Host-baked,
// like the junction patches: the codegen emits the strips the console
// uploads unchanged, and the viewport draws the same triangles.
//
// The lines are each kerbed road's two edges, CUT wherever the kerb would lie
// on another road or inside a junction patch, plus the edge stretches of
// every patch outline between two arms whose roads both have kerbs (never
// across an arm's cap: the road continues there). Points follow the drawn
// surface; straight stretches merge within kKerbTolerance.
inline constexpr float kKerbTolerance = 0.02f;  // lateral and vertical merge error
inline constexpr float kKerbSink = 0.04f;       // face base below the surface
inline constexpr float kKerbCell = 32.0f;       // chunk grid (cull granularity)
inline constexpr float kKerbShadeTop = 0.78f;   // baked shading, 1 = white
inline constexpr float kKerbShadeFace = 0.56f;
// One kerb line. pts: x, y (the surface under the line), z, outward unit
// normal nx, nz - per point.
struct KerbPiece {
    int road = -1;  // CrossingRoad index that owns it (a patch's chain: an arm's road)
    int node = -1;  // crossing index for a patch chain, -1 for a road edge
    float height = 0.15f, width = 0.25f;
    std::vector<float> pts;
    int points() const { return (int)(pts.size() / 5); }
};
struct KerbVertex {
    float x, y, z, shade;
};
// `surface` answers the DRAWN road height under (x, z) (roads + patches, the
// rank lift included) or Surface::kNone; `ground` is the bare terrain.
std::vector<KerbPiece> planKerbs(const std::vector<CrossingRoad>& roads,
                                 const CrossingPlan& plan, const HeightFn& surface,
                                 const HeightFn& ground);
// Every piece as triangle STRIP runs of kStripRun (the road chunks' run
// contract), grouped into kKerbCell chunks of at most kChunkBudget vertices.
// `chunkSizes` receives one vertex count per chunk.
void kerbStrips(const std::vector<KerbPiece>& pieces, std::vector<KerbVertex>& out,
                std::vector<int>& chunkSizes);
// One piece as a triangle LIST (the viewport, the check).
void kerbTriangles(const KerbPiece& piece, std::vector<KerbVertex>& out);
// The kerb TOPS as drawn surface - what the console's road height index reads
// from the owner -4 chunks, so the test drive bumps over a kerb where the
// console car does. `s` must already hold the roads and patches (planKerbs
// reads it); it is built here, and the caller builds it again after.
void addKerbsToSurface(Surface& s, const std::vector<CrossingRoad>& roads,
                       const CrossingPlan& plan, const HeightFn& ground);

}  // namespace roadgen
