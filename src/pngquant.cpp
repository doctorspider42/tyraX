#include "pngquant.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>

#include <stb_image.h>

// stb_image_write's zlib compressor (used for the IDAT chunk) and PNG writer
// (full-color output). Public API, but only declared inside the header's
// implementation region; the implementation itself lives in menubake.cpp.
extern "C" unsigned char* stbi_zlib_compress(unsigned char* data, int data_len,
                                             int* out_len, int quality);
extern "C" int stbi_write_png(char const* filename, int w, int h, int comp,
                              const void* data, int stride_in_bytes);

namespace pngquant {

namespace {

struct Px {
    uint8_t r, g, b, a;
};

// --- median-cut quantizer ---------------------------------------------------

struct Bucket {
    std::vector<uint32_t> pixels;  // packed RGBA
};

uint32_t pack(const Px& p) {
    return (uint32_t)p.r | ((uint32_t)p.g << 8) | ((uint32_t)p.b << 16) |
           ((uint32_t)p.a << 24);
}
Px unpack(uint32_t v) {
    return {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
}
int channel(uint32_t v, int c) { return (int)((v >> (c * 8)) & 0xff); }

// Splits buckets on their widest channel until `colors` buckets exist, then
// averages each bucket into a palette entry. Works on the image's unique
// colors (weighted by count) so big flat areas don't drown gradients.
std::vector<Px> medianCut(const std::vector<uint32_t>& uniques,
                          const std::vector<int>& counts, int colors) {
    struct Cut {
        std::vector<int> idx;  // indices into uniques
    };
    std::vector<Cut> cuts(1);
    cuts[0].idx.resize(uniques.size());
    for (size_t i = 0; i < uniques.size(); ++i) cuts[0].idx[i] = (int)i;

    while ((int)cuts.size() < colors) {
        // widest bucket (by channel range) that can still be split
        int best = -1, bestRange = 0, bestChannel = 0;
        for (size_t b = 0; b < cuts.size(); ++b) {
            if (cuts[b].idx.size() < 2) continue;
            for (int c = 0; c < 4; ++c) {
                int lo = 255, hi = 0;
                for (int i : cuts[b].idx) {
                    const int v = channel(uniques[i], c);
                    lo = std::min(lo, v);
                    hi = std::max(hi, v);
                }
                if (hi - lo > bestRange) {
                    bestRange = hi - lo;
                    best = (int)b;
                    bestChannel = c;
                }
            }
        }
        if (best < 0) break;  // every bucket is a single color

        Cut& cut = cuts[best];
        const int c = bestChannel;
        std::sort(cut.idx.begin(), cut.idx.end(), [&](int a, int b2) {
            return channel(uniques[a], c) < channel(uniques[b2], c);
        });
        // median by pixel weight, not by unique-color count
        long total = 0;
        for (int i : cut.idx) total += counts[i];
        long acc = 0;
        size_t split = 1;
        for (size_t i = 0; i < cut.idx.size(); ++i) {
            acc += counts[cut.idx[i]];
            if (acc * 2 >= total) {
                split = std::max<size_t>(1, std::min(i + 1, cut.idx.size() - 1));
                break;
            }
        }
        Cut right;
        right.idx.assign(cut.idx.begin() + split, cut.idx.end());
        cut.idx.resize(split);
        cuts.push_back(std::move(right));
    }

    std::vector<Px> palette;
    for (const Cut& cut : cuts) {
        long r = 0, g = 0, b = 0, a = 0, n = 0;
        for (int i : cut.idx) {
            const Px p = unpack(uniques[i]);
            const long w = counts[i];
            r += p.r * w, g += p.g * w, b += p.b * w, a += p.a * w, n += w;
        }
        if (n == 0) continue;
        palette.push_back({(uint8_t)(r / n), (uint8_t)(g / n), (uint8_t)(b / n),
                           (uint8_t)(a / n)});
    }
    return palette;
}

int nearest(const std::vector<Px>& palette, int r, int g, int b, int a) {
    int best = 0;
    long bestDist = LONG_MAX;
    for (size_t i = 0; i < palette.size(); ++i) {
        const long dr = r - palette[i].r, dg = g - palette[i].g,
                   db = b - palette[i].b, da = a - palette[i].a;
        // alpha weighs double: blending a wrong alpha is uglier than a hue shift
        const long d = dr * dr + dg * dg + db * db + 2 * da * da;
        if (d < bestDist) {
            bestDist = d;
            best = (int)i;
        }
    }
    return best;
}

// --- indexed PNG writer -----------------------------------------------------

uint32_t crc32of(const uint8_t* data, size_t len, uint32_t crc = 0xffffffffu) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        init = true;
    }
    for (size_t i = 0; i < len; ++i) crc = table[(crc ^ data[i]) & 0xff] ^ (crc >> 8);
    return crc;
}

void putU32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back((uint8_t)(v >> 24));
    out.push_back((uint8_t)(v >> 16));
    out.push_back((uint8_t)(v >> 8));
    out.push_back((uint8_t)v);
}

void putChunk(std::vector<uint8_t>& out, const char* type, const uint8_t* data,
              size_t len) {
    putU32(out, (uint32_t)len);
    const size_t typeAt = out.size();
    out.insert(out.end(), type, type + 4);
    if (len) out.insert(out.end(), data, data + len);
    const uint32_t crc = crc32of(out.data() + typeAt, 4 + len) ^ 0xffffffffu;
    putU32(out, crc);
}

}  // namespace

bool quantize(const std::string& srcPath, const std::string& dstPath, int colors,
              std::string& error) {
    int w = 0, h = 0, comp = 0;
    unsigned char* pixels = stbi_load(srcPath.c_str(), &w, &h, &comp, 4);
    if (!pixels) {
        error = "cannot decode PNG";
        return false;
    }
    const bool ok = quantizeRGBA(dstPath, pixels, w, h, colors, error);
    stbi_image_free(pixels);
    return ok;
}

std::vector<unsigned char> resizeRGBA(const unsigned char* rgba, int sw, int sh,
                                      int dw, int dh) {
    std::vector<unsigned char> out((size_t)dw * dh * 4);
    if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return out;
    // Bilinear sample of source centers. HUD sprites are tiny, so a plain
    // bilinear tap is enough; downscaling by more than 2x is rare here.
    for (int y = 0; y < dh; ++y) {
        const float fy =
            dh == 1 ? 0.0f : ((y + 0.5f) * sh / dh - 0.5f);
        int y0 = (int)std::floor(fy);
        float wy = fy - y0;
        int y1 = y0 + 1;
        if (y0 < 0) { y0 = 0; wy = 0.0f; }
        if (y1 > sh - 1) y1 = sh - 1;
        if (y0 > sh - 1) y0 = sh - 1;
        for (int x = 0; x < dw; ++x) {
            const float fx =
                dw == 1 ? 0.0f : ((x + 0.5f) * sw / dw - 0.5f);
            int x0 = (int)std::floor(fx);
            float wx = fx - x0;
            int x1 = x0 + 1;
            if (x0 < 0) { x0 = 0; wx = 0.0f; }
            if (x1 > sw - 1) x1 = sw - 1;
            if (x0 > sw - 1) x0 = sw - 1;
            const unsigned char* p00 = rgba + ((size_t)y0 * sw + x0) * 4;
            const unsigned char* p10 = rgba + ((size_t)y0 * sw + x1) * 4;
            const unsigned char* p01 = rgba + ((size_t)y1 * sw + x0) * 4;
            const unsigned char* p11 = rgba + ((size_t)y1 * sw + x1) * 4;
            unsigned char* d = out.data() + ((size_t)y * dw + x) * 4;
            for (int c = 0; c < 4; ++c) {
                const float top = p00[c] * (1 - wx) + p10[c] * wx;
                const float bot = p01[c] * (1 - wx) + p11[c] * wx;
                const float v = top * (1 - wy) + bot * wy;
                d[c] = (unsigned char)(v < 0 ? 0 : v > 255 ? 255 : v + 0.5f);
            }
        }
    }
    return out;
}

bool writePngRGBA(const std::string& dstPath, const unsigned char* rgba, int w,
                  int h, std::string& error) {
    if (!stbi_write_png(dstPath.c_str(), w, h, 4, rgba, w * 4)) {
        error = "cannot write " + dstPath;
        return false;
    }
    return true;
}

std::vector<unsigned char> quantizePreviewRGBA(
    const unsigned char* pixels, int w, int h, int colors, Dither dither,
    std::vector<unsigned char>* outPalette) {
    std::vector<unsigned char> out;
    if (!pixels || w < 1 || h < 1) return out;
    if (colors != 16 && colors != 256) colors = 16;

    // unique colors, weighted - identical to the shipped quantizeRGBA path
    std::vector<uint32_t> uniques;
    std::vector<int> counts;
    {
        std::vector<uint32_t> sorted((size_t)w * h);
        std::memcpy(sorted.data(), pixels, (size_t)w * h * 4);
        std::sort(sorted.begin(), sorted.end());
        for (size_t i = 0; i < sorted.size();) {
            size_t j = i;
            while (j < sorted.size() && sorted[j] == sorted[i]) ++j;
            uniques.push_back(sorted[i]);
            counts.push_back((int)(j - i));
            i = j;
        }
    }
    std::vector<Px> palette;
    const bool lossless = (int)uniques.size() <= colors;
    if (lossless)
        for (uint32_t u : uniques) palette.push_back(unpack(u));
    else
        palette = medianCut(uniques, counts, colors);
    if (outPalette) {
        outPalette->clear();
        for (const Px& p : palette) {
            outPalette->push_back(p.r);
            outPalette->push_back(p.g);
            outPalette->push_back(p.b);
            outPalette->push_back(p.a);
        }
    }

    out.resize((size_t)w * h * 4);
    auto emit = [&](size_t i, int idx) {
        out[i * 4 + 0] = palette[idx].r;
        out[i * 4 + 1] = palette[idx].g;
        out[i * 4 + 2] = palette[idx].b;
        out[i * 4 + 3] = palette[idx].a;
    };
    if (lossless || dither == Dither::None) {
        for (size_t i = 0; i < (size_t)w * h; ++i) {
            const uint8_t* p = pixels + i * 4;
            emit(i, nearest(palette, p[0], p[1], p[2], p[3]));
        }
    } else if (dither == Dither::Ordered) {
        // 4x4 Bayer threshold; amplitude scaled to the palette coarseness
        static const int bayer[4][4] = {
            {0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
        const float amp = colors == 16 ? 40.0f : 18.0f;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const size_t i = (size_t)y * w + x;
                const uint8_t* p = pixels + i * 4;
                const float o = (bayer[y & 3][x & 3] / 16.0f - 0.5f) * amp;
                auto c255 = [](float v) {
                    return (int)(v < 0 ? 0 : v > 255 ? 255 : v);
                };
                emit(i, nearest(palette, c255(p[0] + o), c255(p[1] + o),
                                c255(p[2] + o), p[3]));
            }
    } else {
        // Floyd-Steinberg, the shipped-bake behavior (RGB error only -
        // dithered alpha shimmers on the GS)
        std::vector<float> err((size_t)w * h * 3, 0.0f);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const size_t i = (size_t)y * w + x;
                const uint8_t* p = pixels + i * 4;
                auto c255 = [](float v) {
                    return (int)(v < 0 ? 0 : v > 255 ? 255 : v);
                };
                const int r = c255(p[0] + err[i * 3 + 0]);
                const int g = c255(p[1] + err[i * 3 + 1]);
                const int b = c255(p[2] + err[i * 3 + 2]);
                const int idx = nearest(palette, r, g, b, p[3]);
                emit(i, idx);
                const float er = (float)(r - palette[idx].r);
                const float eg = (float)(g - palette[idx].g);
                const float eb = (float)(b - palette[idx].b);
                auto spread = [&](int dx, int dy, float k) {
                    if (x + dx < 0 || x + dx >= w || y + dy >= h) return;
                    const size_t j = (size_t)(y + dy) * w + (x + dx);
                    err[j * 3 + 0] += er * k;
                    err[j * 3 + 1] += eg * k;
                    err[j * 3 + 2] += eb * k;
                };
                spread(1, 0, 7.0f / 16.0f);
                spread(-1, 1, 3.0f / 16.0f);
                spread(0, 1, 5.0f / 16.0f);
                spread(1, 1, 1.0f / 16.0f);
            }
    }
    return out;
}

bool quantizeRGBA(const std::string& dstPath, const unsigned char* pixels, int w,
                  int h, int colors, std::string& error) {
    if (colors != 16 && colors != 256) {
        error = "palette size must be 16 or 256";
        return false;
    }
    const int bitDepth = colors == 16 ? 4 : 8;

    // the engine's 4bpp path packs two pixels per byte - odd widths would skew
    if (bitDepth == 4 && (w & 1)) {
        error = "4-bit textures need an even width";
        return false;
    }

    // unique colors, weighted
    std::vector<uint32_t> uniques;
    std::vector<int> counts;
    {
        std::vector<uint32_t> all((size_t)w * h);
        std::memcpy(all.data(), pixels, (size_t)w * h * 4);
        std::vector<uint32_t> sorted = all;
        std::sort(sorted.begin(), sorted.end());
        for (size_t i = 0; i < sorted.size();) {
            size_t j = i;
            while (j < sorted.size() && sorted[j] == sorted[i]) ++j;
            uniques.push_back(sorted[i]);
            counts.push_back((int)(j - i));
            i = j;
        }
    }

    std::vector<Px> palette;
    const bool lossless = (int)uniques.size() <= colors;
    if (lossless) {
        for (uint32_t u : uniques) palette.push_back(unpack(u));
    } else {
        palette = medianCut(uniques, counts, colors);
    }

    // map pixels; Floyd-Steinberg dithering only when actually lossy
    std::vector<uint8_t> indices((size_t)w * h);
    if (lossless) {
        for (size_t i = 0; i < (size_t)w * h; ++i) {
            uint32_t v;
            std::memcpy(&v, pixels + i * 4, 4);
            const Px p = unpack(v);
            indices[i] = (uint8_t)nearest(palette, p.r, p.g, p.b, p.a);
        }
    } else {
        // error accumulators (RGB only - dithered alpha shimmers on the GS)
        std::vector<float> err((size_t)w * h * 3, 0.0f);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const size_t i = (size_t)y * w + x;
                const uint8_t* p = pixels + i * 4;
                auto clamp255 = [](float v) {
                    return (int)(v < 0 ? 0 : v > 255 ? 255 : v);
                };
                const int r = clamp255(p[0] + err[i * 3 + 0]);
                const int g = clamp255(p[1] + err[i * 3 + 1]);
                const int b = clamp255(p[2] + err[i * 3 + 2]);
                const int idx = nearest(palette, r, g, b, p[3]);
                indices[i] = (uint8_t)idx;
                const float er = (float)(r - palette[idx].r);
                const float eg = (float)(g - palette[idx].g);
                const float eb = (float)(b - palette[idx].b);
                auto spread = [&](int dx, int dy, float k) {
                    if (x + dx < 0 || x + dx >= w || y + dy >= h) return;
                    const size_t j = (size_t)(y + dy) * w + (x + dx);
                    err[j * 3 + 0] += er * k;
                    err[j * 3 + 1] += eg * k;
                    err[j * 3 + 2] += eb * k;
                };
                spread(1, 0, 7.0f / 16.0f);
                spread(-1, 1, 3.0f / 16.0f);
                spread(0, 1, 5.0f / 16.0f);
                spread(1, 1, 1.0f / 16.0f);
            }
    }

    // scanlines: filter byte 0 + packed indices
    const int rowBytes = bitDepth == 4 ? w / 2 : w;
    std::vector<uint8_t> raw((size_t)(rowBytes + 1) * h);
    for (int y = 0; y < h; ++y) {
        uint8_t* row = raw.data() + (size_t)y * (rowBytes + 1);
        row[0] = 0;  // no filter
        if (bitDepth == 8) {
            std::memcpy(row + 1, indices.data() + (size_t)y * w, w);
        } else {
            for (int x = 0; x < w; x += 2)
                row[1 + x / 2] = (uint8_t)((indices[(size_t)y * w + x] << 4) |
                                           indices[(size_t)y * w + x + 1]);
        }
    }

    int deflatedLen = 0;
    unsigned char* deflated = stbi_zlib_compress(raw.data(), (int)raw.size(),
                                                 &deflatedLen, 8);
    if (!deflated) {
        error = "deflate failed";
        return false;
    }

    std::vector<uint8_t> out;
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    out.insert(out.end(), sig, sig + 8);
    uint8_t ihdr[13];
    ihdr[0] = (uint8_t)(w >> 24), ihdr[1] = (uint8_t)(w >> 16);
    ihdr[2] = (uint8_t)(w >> 8), ihdr[3] = (uint8_t)w;
    ihdr[4] = (uint8_t)(h >> 24), ihdr[5] = (uint8_t)(h >> 16);
    ihdr[6] = (uint8_t)(h >> 8), ihdr[7] = (uint8_t)h;
    ihdr[8] = (uint8_t)bitDepth;
    ihdr[9] = 3;  // palette color type
    ihdr[10] = ihdr[11] = ihdr[12] = 0;
    putChunk(out, "IHDR", ihdr, sizeof(ihdr));

    std::vector<uint8_t> plte, trns;
    for (const Px& p : palette) {
        plte.push_back(p.r);
        plte.push_back(p.g);
        plte.push_back(p.b);
        trns.push_back(p.a);
    }
    putChunk(out, "PLTE", plte.data(), plte.size());
    putChunk(out, "tRNS", trns.data(), trns.size());
    putChunk(out, "IDAT", deflated, (size_t)deflatedLen);
    free(deflated);  // stbi_zlib_compress allocates with malloc
    putChunk(out, "IEND", nullptr, 0);

    std::ofstream f(dstPath, std::ios::binary | std::ios::trunc);
    if (!f || !f.write((const char*)out.data(), (std::streamsize)out.size())) {
        error = "cannot write " + dstPath;
        return false;
    }
    return true;
}

}  // namespace pngquant
