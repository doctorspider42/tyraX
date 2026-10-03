#pragma once

#include <string>
#include <vector>

// Road streaming (docs/roads.md "Road streaming", format v102): with
// ProjectSettings::roadStreamRadius > 0 the generated game keeps only the road
// geometry within that radius of the view focus resident - strip chunks,
// junction rows, spills, soft edges, kerbs and rails, bridge structure,
// details, street furniture and the bridge/furniture collision boxes - and
// builds the rest on demand, a budget per frame, as the player moves.
//
// Host-only. Two halves:
//   - the CODEGEN, which assembles the streaming runtime out of the text the
//     non-streaming build already emits (buildRoads, the upload blocks,
//     procFinishChunks), so the two can never disagree about how a chunk is
//     made: emit() cuts those texts at fixed anchors and wraps them. A missing
//     anchor becomes an #error in the generated source (a build that fails
//     loudly), and --vehicle-check generates a streaming project to prove
//     every anchor still holds;
//   - the CORE (src/roadstream_core.inl): the item boxes, the coarse cell
//     grid, the per-chunk height index and the ring's decisions. The same
//     file is compiled into the editor (so --vehicle-check exercises the exact
//     code the console runs) and pasted verbatim into the generated class.
//
// Off (radius 0, the default) the generated sources are byte-identical to a
// build without this feature: nothing here is consulted.
namespace roadstream {

// The cell of the coarse item grid, and of the "is this place ready?" test
// the far cars use. Every road chunk is smaller than this (strip chunks stop
// at 16 V = 64 units of arc, baked rows at 32-48-unit cells).
inline constexpr float kCell = 64.0f;
// Load inside the radius, keep until radius + kHysteresis.
inline constexpr float kHysteresis = 40.0f;
// The per-frame build budget, in vertex units: a strip vertex is tessellated
// on the EE (terrain heights, the lateral merge) and counts kStripWeight, a
// baked vertex is a copy and counts 1, and every item costs kItemCost on top.
inline constexpr int kBudget = 600;
inline constexpr int kStripWeight = 4;
inline constexpr int kItemCost = 32;

// What the codegen hands over: the non-streaming road source, already
// assembled. `roads` is roadsImpl's text WITH every upload block spliced in;
// the upload blocks are passed separately too (empty when the project has
// none) so each can be cut on its own anchors; `finish` is the template text
// that defines procFinishChunks.
struct Sources {
    std::string roads;
    std::string kerbs;
    std::string details;
    std::string bridges;
    std::string furniture;
    std::string finish;
};

struct Params {
    float radius = 0.0f;
    bool vehicles = false;
};

struct Emitted {
    std::string members;  // class members (core + state + declarations)
    std::string impl;     // the TerrainGame:: functions
    std::string setup;    // the loadScene call
    std::vector<std::string> errors;  // anchors that did not match
};

Emitted emit(const Sources& src, const Params& prm);

// The template patches a streaming project needs in code shared with every
// other project (roadSurfaceAt's lookup, the per-frame update, the far-car
// freeze). Applied by fillTemplate to every template; each one matches in
// exactly one of them.
std::string patchTemplate(std::string s, const Params& prm);

// The core, as pasted into the generated class (CR stripped).
std::string coreSource();

// The default radius the Project Preferences suggest: the fog end or the
// terrain view distance, whichever is larger, plus one cell.
float suggestedRadius(float fogEnd, float terrainViewDistance);

// --vehicle-check "road streaming".
void check(void (*verdict)(bool, const char*));

}  // namespace roadstream
