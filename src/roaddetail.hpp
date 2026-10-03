#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "roadgen.hpp"

// Road details (docs/roads.md "Road details"): the small things that make a
// generated street read as a real one - manhole covers, storm-drain gullies at
// the kerb, asphalt repair patches, cracks and oil stains - placed along every
// road whose `details` density is above 0.
//
// Host-only (no GL, no ImGui, no project.hpp), the roadgen shape and for the
// same reason: the codegen bakes the decals into ROAD_DETAIL_VERTS for the
// console, the viewport draws the same triangles, and --vehicle-check proves
// their properties - three consumers of ONE function. The PS2 does no detail
// work beyond uploading them at scene load.
//
// Each decal is a small quad laid onto the DRAWN road surface (the road's own
// tessellated triangles, rank lift included), split where that surface bends,
// lifted kDetailLift above it and textured from one generated ATLAS
// (res/materials/roads/road-details.png). A decal is only kept where its whole
// footprint lies on its own road and nothing else - no junction patch, no node
// paint, no other road, no spill - is within kClearance of it, so it can never
// z-fight or hang off an edge. Placement is a pure function of the road's
// points, width, density, seed and stable id: an unchanged project bakes the
// same decals byte for byte.
namespace roaddetail {

enum Kind : int {
    kManholeRound = 0,
    kManholeSquare = 1,
    kGully = 2,      // storm-drain grate just inside the kerb (kerbed roads only)
    kPatch = 3,      // asphalt repair patch
    kCrack = 4,
    kStain = 5,      // oil stain
    kKindCount = 6
};
const char* kindName(int kind);

// How far a decal floats above the drawn road: one step above the node paint
// and the spills (roadgen::kSpillLift, 0.02), well under the 0.03 rank step.
inline constexpr float kDetailLift = 0.03f;
// Nothing that is not this road (patch, paint, spill, another road) may lie
// within this distance of a decal's footprint.
inline constexpr float kClearance = 0.3f;
// The subdivided decal follows the road surface to this (world units).
inline constexpr float kFlatness = 0.005f;
// Chunk grid (cull granularity) and the console's draw distance.
inline constexpr float kDetailCell = 32.0f;
inline constexpr float kDrawDistance = 50.0f;
inline constexpr int kChunkBudget = 1800;  // vertices per chunk, == roadgen's

// --- the atlas ---------------------------------------------------------------
//
// One 128x128 RGBA image, every pixel one of 16 fixed RGBA entries, so the
// texture bake's 4-bit quantization is lossless (palette() lists them). Alpha
// 0 outside the shapes: StaPip's alpha test drops those texels, the blended
// bag mixes the soft alpha of cracks and stains.
inline constexpr int kAtlasSize = 128;
inline constexpr const char* kAtlasStem = "road-details";
inline constexpr const char* kAtlasMtl = "res/materials/roads/road-details.mtl";
inline constexpr const char* kAtlasPng = "res/materials/roads/road-details.png";

struct Cell {
    int kind;
    int x, y, w, h;  // pixels in the atlas
};
const std::vector<Cell>& cells();
// The 16 RGBA entries the atlas is drawn from.
const std::vector<uint32_t>& palette();  // 0xRRGGBBAA
// RGBA8, kAtlasSize x kAtlasSize, row 0 at the top (V = 0). Deterministic.
std::vector<unsigned char> generateAtlas();
// Writes the atlas .png and a one-material .mtl into <projectDir>/
// res/materials/roads when they do NOT exist yet (an artist's repaint is
// kept; delete the files to regenerate). "" on success, else the error.
std::string ensureAtlas(const std::string& projectDir);

// --- placement ---------------------------------------------------------------

struct Decal {
    int road = -1;   // CrossingRoad index
    int kind = 0;
    int cell = 0;    // cells() index
    float x = 0, z = 0;     // centre
    float ax = 1, az = 0;   // unit axis of the cell's U (its width)
    float hu = 0.5f, hv = 0.5f;  // half extents along U and across (V)
};

// One scene's roads as the codegen sees them. `patches` and `paint` are the
// drawn junction patches and node markings (any order, triangle lists); the
// roads themselves are tessellated here, rank-lifted over `ground`.
struct SceneInput {
    const std::vector<roadgen::CrossingRoad>* roads = nullptr;
    const roadgen::CrossingPlan* plan = nullptr;
    roadgen::HeightFn ground;
    std::vector<roadgen::Vertex> patches;
    std::vector<roadgen::Vertex> paint;
};

struct Result {
    std::vector<Decal> decals;
    // Triangle LIST, XYZ + atlas UV, grouped into chunks of whole decals by
    // kDetailCell cell; chunkSizes sums to tris.size().
    std::vector<roadgen::Vertex> tris;
    std::vector<int> chunkSizes;
    int candidates = 0;  // placements tried
    int rejected = 0;    // ... that were not placed, of which:
    int rejectedOverlap = 0;    // on top of an earlier decal
    int rejectedOffRoad = 0;    // footprint not wholly on the road's surface
    int rejectedClearance = 0;  // a patch, paint, spill or other road too close
};

// True when any road has details > 0 (the zero-cost gate).
bool any(const std::vector<roadgen::CrossingRoad>& roads);
Result build(const SceneInput& in);

}  // namespace roaddetail
