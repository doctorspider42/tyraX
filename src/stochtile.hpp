#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Stochastic tiling ("texture bombing") for terrain textures (docs/terrain-painting.md).
// PS2 has no pixel shaders, so the randomization happens AT BUILD TIME, in
// pixels: from a source tile we bake ONE larger, still-perfectly-tileable
// "supertile" (up to 512x512) whose interior scatters randomly rotated /
// flipped / offset patches of the source, feathered and wrapped on the torus.
// The game tiles that supertile like any texture - same single pass, zero
// runtime cost - but its repetition period is `factor` times longer, so the
// tell-tale grid "checkerboard" leaves the visible range.
//
// The generator is the single source of truth (like pngquant): texbake writes
// it into .res-baked, and the editor viewport uploads the same pixels, so what
// you author is what ships. Deterministic (seeded from the source path).
namespace stochtile {

// One source-tile cell edge inside the supertile (a power of two in [64,256]);
// the supertile is `factor * cell == 512`. factorFor derives `factor` from a
// source PNG's dimensions WITHOUT decoding the pixels (codegen uses it for the
// runtime tiling: repeats-per-unit is divided by `factor`). Returns 1 when the
// file cannot be read (treat as "not stochastic").
int factorFor(const std::string& srcFullPath);

// Bakes the supertile for a source PNG. Returns RGBA (outW*outH*4, always
// 512x512 unless the source cannot be read -> empty). `factor` is set as in
// factorFor. `seedKey` (e.g. the res-relative source path) makes the pattern
// stable and distinct per texture.
std::vector<uint8_t> generate(const std::string& srcFullPath,
                              const std::string& seedKey, int& outW, int& outH,
                              int& factor);

// Bin-relative path of a source texture's baked supertile (e.g.
// "stoch/res_materials_grass_png.png"). Shared by texbake and codegen so their
// paths always agree; lives under .res-baked/stoch (never in the user's res/).
std::string bakedBinPath(const std::string& srcRel);

}  // namespace stochtile
