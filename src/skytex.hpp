#pragma once
// Painted sky (docs/sky-texture.md): the one crop of an equirectangular
// panorama that the dome samples. Host-only, no GL, no project.hpp - texbake
// ships its pixels, the viewport uploads the same pixels, and codegen emits
// kVMax so the generated dome maps its UVs onto exactly this crop.
#include <string>
#include <vector>

namespace skytex {

// The shipped texture. 256 texels round the whole horizon is ~1.4 degrees a
// texel - about what a 512-wide screen shows of a 70-degree view - and 8-bit
// CLUT keeps it at 32 KB of GS VRAM.
constexpr int kWidth = 256;
constexpr int kHeight = 128;

// How much of the panorama's height the crop keeps, from the top. The dome
// ends 0.06 rad below the horizon (buildSkyDome), i.e. at v = 0.519 of an
// equirectangular image; 0.5625 leaves a margin so bilinear filtering never
// reaches the texture's bottom edge. The rest of the panorama is never seen.
constexpr float kVMax = 0.5625f;

// Loads `absPath`, crops rows [0, kVMax) and resizes to kWidth x kHeight RGBA.
// Empty on failure, with the reason in `err`.
std::vector<unsigned char> crop(const std::string& absPath, std::string& err);

// Whether `absPath` is an image crop() can read - a header check, cheap enough
// for codegen to ask per scene, so a missing panorama drops the scene back to
// the gradient on BOTH sides instead of shipping a path nothing wrote.
bool usable(const std::string& absPath);

// The dome's texture coordinate for a direction at longitude `lonRad` (as
// buildSkyDome walks it: x = cos(lat) cos(lon), z = cos(lat) sin(lon)) and
// latitude `latRad`, turned by `yawDeg`. u may leave [0, 1] - the texture is
// sampled with REPEAT, which is what closes the seam. Twin of the generated
// buildSkyDome and of Viewport's sky mesh - change all three together.
inline void domeUv(float lonRad, float latRad, float yawDeg, float& u, float& v) {
    constexpr float kPi = 3.14159265358979f;
    u = lonRad / (2.0f * kPi) + yawDeg / 360.0f;
    v = (0.5f - latRad / kPi) / kVMax;
}

}  // namespace skytex
