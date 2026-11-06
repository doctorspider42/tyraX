#pragma once

// Baked reflection panoramas (docs/reflective-materials.md, "The baked
// background"). With ProjectSettings::reflectionPanorama on, the shared
// dynamic env map ("@sky") stops re-rendering the static world on every
// capture. The editor bakes, per 16-unit cell near a road, what the probe would
// have drawn of the STATIC world from that cell - the terrain and road chunks
// within the reflection ground radius, and the "Reflection box proxy" objects
// that cannot move - as one 128x32 8-bit panorama (azimuth x elevation). The
// game draws it as ONE textured dome around the probe's eye, over the live sky,
// and keeps drawing everything that can change live on top: the sky, the
// "Show in reflections" objects that are not baked proxies (the district's
// night windows), static batches, cars.
//
// One palette per scene: entry 0 is transparent (the live sky shows through;
// its RGB is the sky colour, set by the game, so a bilinear edge fades toward
// the sky rather than black), entries 1..boxes are the baked proxy boxes -
// their colour is filled in BY THE GAME from the real object's vertex colours,
// exactly as renderReflectionProxy colours its box, so the panorama cannot
// disagree with what the live path drew - and the rest is the ground, median-
// cut over every cell of the scene.
//
// Host-only, no GL. texbake writes the result to bin/reflpano/s<scene>.bin;
// codegen emits only the switch.

#include <cstdint>
#include <string>
#include <vector>

#include "project.hpp"

namespace reflpano {

constexpr int kWidth = 128;           // azimuth, 2.8 degrees a texel
constexpr int kHeight = 32;           // elevation
constexpr float kElevTop = 64.0f;     // degrees: the probe's 110-degree square
constexpr float kElevBottom = -64.0f; // reaches ~63 in its corners, up and down
constexpr float kCellSize = 16.0f;    // world units
constexpr float kEyeHeight = 2.0f;    // above the terrain at the cell centre
constexpr float kRoadReach = 12.0f;   // a cell is baked when a road is this close
constexpr int kMaxBoxes = 96;         // palette entries reserved for proxy boxes

struct Bake {
    bool ok = false;
    float minX = 0, minZ = 0;  // the grid's corner (the terrain's)
    int cellsX = 0, cellsZ = 0;
    std::vector<int16_t> slot;            // cellsX*cellsZ, -1 = not baked
    std::vector<int32_t> boxObjects;      // palette entry 1+i = authored object
    std::vector<unsigned char> palette;   // 256 x RGBA (entries 1..boxes zero)
    std::vector<unsigned char> pixels;    // slots x kWidth x kHeight indices
    int slots() const { return (int)(pixels.size() / (kWidth * kHeight)); }
};

// Does this scene get a panorama at all (setting on, a terrain to stand on)?
bool wanted(const Project& p, const SceneData& sc);

// The authored objects a panorama stands in for: "Show in reflections" +
// "Reflection box proxy" objects that cannot move at runtime. The game skips
// exactly these in the probe while a panorama is drawn.
std::vector<int> bakedObjects(const Project& p, const SceneData& sc);

Bake bake(const Project& p, const SceneData& sc);

// The file the game loads: "RPAN" v1, little endian (see reflpano.cpp).
std::vector<unsigned char> serialize(const Bake& b);

}  // namespace reflpano
