#pragma once

// Reflection scenery (docs/reflective-materials.md, "Static scenery in the
// probe"): with ProjectSettings::reflectionScenery on, every static object of
// a scene is drawn into the shared dynamic env map ("@sky") as ONE untextured
// box - its own oriented bounds, coloured with the average of its material.
// The main view, collision and picking keep the real object.
//
// The probe is a 128x128 target that is sphere-mapped afterwards, so a
// building's windows and seams resolve to a few blurred texels there; what a
// car's paint needs from the city is where the masses are and roughly what
// colour they are. Twelve triangles per object give exactly that, and the
// generated game merges them into a handful of bags (one per grid cell and
// layer) instead of one submit per object.
//
// Host-only, no GL, no ImGui: this decides WHICH objects and WHAT box; the
// generated game (templates.cpp, renderReflectionScenery) only shades and
// draws the table codegen emits from it.

#include <string>
#include <vector>

#include "project.hpp"

namespace reflscenery {

// Smallest half extent, on an object's LONGEST axis, that still earns a box.
// A bench or a bollard is a sub-texel dot in the probe; a lamp post is not.
constexpr float kMinHalfExtent = 0.6f;

struct Box {
    float center[3] = {0, 0, 0};
    // Local axis k in world space, scaled by the half extent along it - the
    // eight corners are center +- axis[0] +- axis[1] +- axis[2].
    float axis[3][3] = {{0.5f, 0, 0}, {0, 0.5f, 0}, {0, 0, 0.5f}};
    // Albedo 0..255: the object's colour x its material's Kd x the mean of its
    // texture, area-weighted over a model's parts. Lighting is applied in the
    // game with the same shadeOf() every static object is baked with.
    unsigned char rgb[3] = {153, 153, 153};
    int layer = -1;   // index into SceneData::layers, -1 = always resident
    int object = -1;  // authored object index in the scene
};

// Why an object does or does not get a box - the Properties readout says it.
enum class Verdict {
    Box,          // it gets one
    Explicit,     // "Show in reflections" is on: it renders through that path
    NotSolid,     // a marker, light, decal, mirror, portal, area, invisible wall...
    Moves,        // can move at runtime (physics, scripts, vehicles, belts...)
    Animated,     // an animated model
    Merged,       // a procedural chunk: one box would be a block over the lot
    TooSmall,     // under kMinHalfExtent on its longest axis
    NoModel,      // the model file cannot be read
};
const char* verdictText(Verdict v);

// The boxes of one scene, in object order. Deterministic.
std::vector<Box> collect(const Project& p, const SceneData& sc);

// One object's verdict in its scene (index = its position in sc.objects).
Verdict verdictFor(const Project& p, const SceneData& sc, int index);

// The ground stand-in (ProjectSettings::reflectionGroundProxy, "The ground in
// stand-in"): the probe draws a coarse height-following grid around its eye
// instead of the terrain and road chunks, coloured from this. `rgb` is a
// kGroundGrid x kGroundGrid albedo map over the terrain's extents: the base
// material blended with the painted layers by their splat weights, with every
// road painted over it in its surface's mean colour by how much of a texel it
// covers. The game lights it with shadeOf() and looks nothing else up.
constexpr int kGroundGrid = 64;
struct Ground {
    bool ok = false;  // false = no terrain: the probe draws no ground at all
    float minX = 0, minZ = 0, sizeX = 1, sizeZ = 1;
    std::vector<unsigned char> rgb;  // kGroundGrid^2 x 3, row-major in z
    unsigned char road[3] = {77, 77, 82};  // a road without a readable texture
};
Ground ground(const Project& p, const SceneData& sc);

}  // namespace reflscenery
