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

// Same again, but the palettized PNG comes back as BYTES instead of being
// written. The vehicle bake needs this: it content-compares what it is about
// to write against what is already on disk (a fresh mtime on an asset the
// compiler reads is a rebuild nobody asked for), so it cannot quantize a file
// it has already written. `out` is left untouched on failure.
bool quantizeRGBAToMemory(std::vector<unsigned char>& out,
                          const unsigned char* rgba, int w, int h, int colors,
                          std::string& error);

// Writes an RGBA buffer as a plain 32-bit PNG (full color, no palette).
bool writePngRGBA(const std::string& dstPath, const unsigned char* rgba, int w,
                  int h, std::string& error);

// The quantization quantizeRGBA writes, without the PNG: one index per pixel
// and the palette as r,g,b,a quadruplets. Bit-identical to what lands in the
// file - texbake fits crowd palette variants against exactly these indices
// (docs/character-generator.md, "Crowds").
bool quantizeIndices(const unsigned char* rgba, int w, int h, int colors,
                     std::vector<unsigned char>& indices,
                     std::vector<unsigned char>& paletteRgba, std::string& error);

// Dithering flavors for the in-memory preview below. The shipped bake
// (quantizeRGBA) always uses Floyd-Steinberg; the preview lets the eye
// compare before committing a texture to a palette budget.
enum class Dither {
    FloydSteinberg = 0,  // error diffusion - what texbake ships
    Ordered = 1,         // 4x4 Bayer - stable under motion, visible pattern
    None = 2,            // nearest color - hard banding
};

// In-memory preview of the CLUT look: the same median-cut palette the
// shipped bake computes, the chosen dithering, and the palettized result
// expanded back to RGBA (what the GS displays). outPalette, when given,
// receives the palette as RGBA entries (<= colors of them).
std::vector<unsigned char> quantizePreviewRGBA(
    const unsigned char* rgba, int w, int h, int colors, Dither dither,
    std::vector<unsigned char>* outPalette = nullptr);

// Bilinear resample of an RGBA buffer to dw x dh (returns dw*dh*4 bytes).
// A 4-bit indexed PNG from indices the caller already chose (0..15, one byte
// per pixel, w even) and the 16-entry RGBA palette to go with them. No
// quantization at all: for data whose palette is a fixed RAMP - the baked
// ground shadows (docs/shadows.md) store sixteen alpha levels of one colour,
// and a colour quantizer is exactly what would merge them. The engine's PNG
// loader keeps tRNS alpha per entry (`a >> 1` into the CLUT), so the levels
// reach the GS intact. False with `error` set on failure.
bool writeIndexed4(const std::string& dstPath, const unsigned char* indices, int w,
                   int h, const unsigned char paletteRgba[64], std::string& error);

std::vector<unsigned char> resizeRGBA(const unsigned char* rgba, int sw, int sh,
                                      int dw, int dh);

}  // namespace pngquant
