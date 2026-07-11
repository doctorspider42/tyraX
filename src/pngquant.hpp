#pragma once

#include <string>
#include <vector>

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

// Same as quantize() but from an in-memory RGBA buffer (w*h*4 bytes, row-major,
// 8 bits/channel) instead of a file - lets callers resize before quantizing.
bool quantizeRGBA(const std::string& dstPath, const unsigned char* rgba, int w,
                  int h, int colors, std::string& error);

// Writes an RGBA buffer as a plain 32-bit PNG (full color, no palette).
bool writePngRGBA(const std::string& dstPath, const unsigned char* rgba, int w,
                  int h, std::string& error);

// Bilinear resample of an RGBA buffer to dw x dh (returns dw*dh*4 bytes).
std::vector<unsigned char> resizeRGBA(const unsigned char* rgba, int sw, int sh,
                                      int dw, int dh);

}  // namespace pngquant
