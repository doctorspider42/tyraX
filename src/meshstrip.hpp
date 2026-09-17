#pragma once

#include <cstddef>
#include <vector>

// meshstrip: turn a baked triangle LIST into triangle STRIPS, at build time.
//
// Why this exists, and what it is worth. The PS2 static pipeline submits a
// mesh as VU1 packages, and EVERYTHING the EE does per frame - bounding boxes,
// package creation, frustum classification, packet construction, the
// dma_channel_send_packet2 bracket - scales with the NUMBER of packages, which
// scales with the vertex count. On a measured Motor District garage frame that
// EE half is about 34 ms of a 40 ms render submission, against 5.8 ms of VIF1
// wait (docs/vu1-and-dma-cache-cost.md). An unindexed triangle list packages,
// transfers and transforms a shared vertex once per triangle that uses it; the
// district's own baked models hold only 27.4% unique full vertices. So the
// lever is fewer vertices, and a strip is the cheapest way to get them: the GS
// takes one vertex per triangle after the first two.
//
// The output is not just "a strip". It is chopped into independent RUNS of at
// most `run` vertices, because a VU1 package is a contiguous slice of the bag's
// vertex array and a strip spanning two packages would need its two-vertex
// overlap repeated at a boundary only the runtime knows. Making the runs the
// packages instead moves that whole problem to build time:
//
//   - every run is a self-contained strip, so a package boundary can never
//     splice two unrelated vertices into one triangle;
//   - every run length is a multiple of 3, because the VU1 vertex loops step
//     by three and a count that is not runs off into VU1 memory (padding
//     repeats the last vertex, which makes a degenerate triangle the GS
//     rasterises to nothing);
//   - separate strips inside one run are joined by repeating a vertex either
//     side of the seam. Winding parity is NOT preserved and does not need to
//     be: nothing in this engine backface-culls.
//
// The game pins StaPipBag::packageSize to `run` and sets StaPipBag::stripped,
// and the engine does the rest (see StaPipBag::stripped in the fork).
namespace meshstrip {

// Vertices per run. 75 is the SMALLEST package any static program class
// derives (textured, per-vertex colours - StaPipVU1Program::getMaxVertCount),
// so one baked number is legal for every pass an object can take; it is also a
// multiple of 3, the invariant StaPipCore's package-size clamp keeps. Baking a
// larger run and meeting a smaller derived size would make the clamp move the
// package boundaries off the run boundaries - which is the one way this can
// render wrong, so it is a constant rather than a parameter of the file.
//
// It was 72 until the engine's rounding step was relaxed from a multiple of 9
// to a multiple of 3 (docs/render-submission-attribution.md, "Round four"):
// the package count is vertices / kRun and almost every per-package term in
// StaPipCore::dispatch scales with it, so +3 vertices is -4% of the packages.
// Every consumer GUARDS rather than trusts - a run longer than the engine
// derives falls back to the triangle list (`stripRun <= minPackageSize()`) -
// so a .tmdl baked by an older editor stays correct, just slower.
inline constexpr unsigned kRun = 75;

// WHICH ATTRIBUTES DECIDE THAT TWO CORNERS ARE THE SAME VERTEX.
//
// A strip vertex is submitted once and read by up to three triangles, so two
// corners may share one only when every attribute the GS receives for them is
// identical. Which attributes those ARE is a property of the BAG, not of the
// mesh, and the difference is the whole reason a mesh strips or refuses to.
//
// kFull is the answer for anything the pipeline shades: position, normal and
// UV. A flat-shaded mesh gives every face its own normals, so almost no corner
// welds and build() honestly reports that the strip is bigger than the list -
// measured on the Motor District's vehicles, 2 242 unique corners out of 2 280
// and a 1.65x strip.
//
// kNoNormal is the answer for a bag the pipeline renders UNLIT - no lighting
// bag, a single flat colour - where the normal is never read and is therefore
// not an attribute the GS receives. The emitted vertex still carries the
// normal of the corner it was welded from, so the array stays a well-formed
// 8-float mesh; it is simply not the array to shade. The same three vehicle
// wheels that refuse kFull strip to 0.64-0.76x under it
// (docs/vehicles.md, "The wheel batch is a strip").
//
// Using kNoNormal for a bag that IS lit is a rendering bug, not a slower
// render: neighbouring faces would take one face's normal.
enum class Weld {
    kFull,
    kNoNormal,
};

// Stripify an interleaved 8-float-per-vertex triangle list.
//
// `ao` is either empty or one byte per input vertex, and is reordered with the
// geometry. Returns false - leaving the outputs untouched - when the input is
// unusable or when the strips came out no smaller than the list, which is the
// honest answer for a mesh with no shared vertices at all (every triangle is
// then its own 3-vertex strip plus 2 vertices of join, i.e. worse). The caller
// keeps the list in that case and the bag is not marked stripped.
bool build(const std::vector<float>& verts,
           const std::vector<unsigned char>& ao, unsigned run,
           std::vector<float>& outVerts, std::vector<unsigned char>& outAo,
           Weld weld = Weld::kFull);

// Diagnostics for the host harness (docs/model-pipeline.md). Set by every
// build() call; nothing in the editor or the game reads them.
extern int dbgStripCount;
extern int dbgStripMax;
extern float dbgStripAvg;
extern size_t dbgPadding;

}  // namespace meshstrip
