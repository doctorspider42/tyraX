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
// A PUDDLE (docs/weather.md "Puddles") is a decal too, but not an atlas kind:
// it has its own texture (kPuddlePng), its own chunks (their one shared
// colour is the wetness, set once a frame) and is placed only in a project
// with weather (SceneInput::puddles), last, so it never displaces a decal of
// the kinds above. Kept out of kKindCount so every per-kind table and line
// that existed before stays what it was.
inline constexpr int kPuddle = 6;

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
// Puddles are sparser than the other decals: a bigger cell keeps their
// chunk (= submit) count down.
inline constexpr float kPuddleCell = 64.0f;

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

// The puddles' texture: 64 x 64, four irregular soft-edged puddles in a 2 x 2
// grid, every pixel one of 16 RGBA entries (alpha 0..255 in 15 steps, grey
// 112 inside - 128 is "the vertex colour exactly" - rising to 176 along the
// shore, a glint at the wet edge), so a 4-bit bake is lossless. The shape is
// in the alpha, the colour is the shared puddle colour.
inline constexpr int kPuddleSize = 64;
inline constexpr const char* kPuddlePng = "res/materials/roads/road-puddles.png";
std::vector<unsigned char> generatePuddles();  // RGBA8, row 0 at the top
// Writes kPuddlePng when it does not exist yet (a repaint is kept). "" on
// success, else the error.
std::string ensurePuddles(const std::string& projectDir);

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
    // Place puddles too (the project has weather): low spots by the kerb and
    // in the wheel ruts, from the same density and seed.
    bool puddles = false;
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
    // Puddles (SceneInput::puddles): their own decals, triangles (UV into
    // kPuddlePng) and chunks of whole puddles by kPuddleCell cell - never
    // mixed with the atlas decals, because their colour bag is shared.
    std::vector<Decal> puddles;
    std::vector<roadgen::Vertex> puddleTris;
    std::vector<int> puddleChunkSizes;
    int puddleCandidates = 0;
    int puddlesOnCrest = 0;  // candidates dropped because the road sheds water there
};

// True when any road has details > 0 (the zero-cost gate).
bool any(const std::vector<roadgen::CrossingRoad>& roads);
Result build(const SceneInput& in);

}  // namespace roaddetail
