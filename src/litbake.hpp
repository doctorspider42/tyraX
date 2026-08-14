#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "gibake.hpp"
#include "project.hpp"

// Scene light baked INTO one placed object's texture (docs/prelit-models.md).
// Host-only, no GL - the aobake/matbake/decalproj pattern.
//
// WHY THIS EXISTS, in one paragraph. A textured surface can never take this
// engine's lightmap: the atlas pass is ADDITIVE, and a flat additive term over
// a texture blows out its dark texels. That is not an implementation choice
// that can be revisited - the GS blend unit computes Cv = (A - B) * C + D with
// A, B, D chosen from {Cs, Cd, 0} and C always an ALPHA (As, Ad or FIX), so
// "texture times lightmap" is not expressible in a second pass at all. Which
// leaves exactly one way to put per-pixel light on a textured model, and it is
// the way the survival-horror games of the era did it: bake the light INTO the
// albedo and ship a unique, pre-lit texture for that surface.
//
// So this module is the missing half of a machine that was already here. gibake
// computes the light (sky, sun behind a shadow ray, emissive materials, baked
// point lights, bounces, all over a triangle BVH of the whole scene); matbake
// showed how to rasterize a model's UV space. What was missing was the join:
// walk the object's UV islands, ask gibake what light arrives at each texel's
// WORLD position, multiply it into the albedo there, and write the result as
// that object's own material.
//
// The price is the one the era paid too: a pre-lit surface needs its own
// texture, so this trades GS VRAM for per-pixel light. It is a per-OBJECT
// operation for that reason - you spend it on the wall the player walks up to,
// not on everything.
namespace litbake {

struct Params {
    int size = 128;   // output texture width = height, pow2, 32..512
    int rays = 96;    // hemisphere rays per texel (gibake::gather)
    int padding = 4;  // dilate ring in texels: bilinear/mip seam guard
    // Multiplies the baked light before it hits the albedo. 1.0 = physical.
    // Below 1 keeps more of the texture's own value in the dark parts, which
    // is what you want when the scene's dynamic lights do most of the work.
    float strength = 1.0f;
    // Never darken past this, per channel. A surface the bake finds no light
    // for goes black, and a black texture is indistinguishable from a bug when
    // the player's torch is pointed straight at it.
    float floorLevel = 0.12f;
    uint32_t seed = 1;
};

// What the bake produced, ready to be written by the caller.
struct Result {
    std::vector<uint8_t> rgba;  // size*size*4, the pre-lit albedo
    int size = 0;
    int litTexels = 0;    // texels covered by a UV island
    float meanLight = 0;  // average gathered light, for the report line
};

// Bakes ONE placed object. `scene` must already be built and solved for this
// scene (gibake::build + gibake::solve) - it is passed in rather than built
// here so baking several objects pays for the BVH and the bounce solve once.
//
// Model objects only, for now: a primitive that is untextured already has the
// per-texel route through the scene lightmap atlas, which costs no extra
// texture at all and is the better answer where it applies.
bool bakeObject(const Project& p, const SceneData& sc, int objectIndex,
                const gibake::Scene& scene, const Params& prm, Result& out,
                std::string& err);

// Writes the result as `<name>-lit.png` + a one-entry `<name>-lit.mtl` under
// res/materials/, and points the object at it (materialPath) with prelit set.
// Returns "" on success or the reason it failed.
std::string applyToObject(Project& p, SceneData& sc, int objectIndex,
                          const Result& r);

}  // namespace litbake
