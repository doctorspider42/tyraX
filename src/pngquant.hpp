#pragma once

#include <string>

// PNG palette quantization - the PS2's native "texture compression". The GS
// has no DXT-style hardware format; what era games did instead was palettized
// textures (PSMT8/PSMT4), which the engine's PNG loader consumes directly
// from indexed PNGs. This module turns any readable PNG into such a file.
namespace pngquant {

// Quantizes srcPath to `colors` (16 -> 4-bit indexed, 256 -> 8-bit indexed)
// and writes the result as a palettized PNG (PLTE + tRNS) to dstPath.
// Median-cut over RGBA with Floyd-Steinberg dithering; images that already
// fit the palette budget lose nothing. src == dst is allowed (whole-file
// read first). Returns false and fills `error` on failure - dstPath is then
// left untouched.
bool quantize(const std::string& srcPath, const std::string& dstPath,
              int colors, std::string& error);

}  // namespace pngquant
