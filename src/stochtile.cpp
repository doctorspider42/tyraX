#include "stochtile.hpp"

#include <cmath>

#include <stb_image.h>

#include "pngquant.hpp"

namespace stochtile {
namespace {

// Largest power of two <= v.
int pow2Floor(int v) {
    int p = 1;
    while (p * 2 <= v) p *= 2;
    return p;
}

// The source-tile edge inside the supertile: a power of two clamped to
// [64, 256]. 64 gives an 8x8 arrangement (max variety), 256 a 2x2 (max detail
// kept). Capping at 256 guarantees at least a doubled repetition period.
int cellFor(int minDim) {
    int c = pow2Floor(minDim);
    if (c < 64) c = 64;
    if (c > 256) c = 256;
    return c;
}

uint32_t fnv1a(const std::string& s) {
    uint32_t h = 2166136261u;
    for (char c : s) {
        h ^= (uint8_t)c;
        h *= 16777619u;
    }
    return h ? h : 1u;
}

}  // namespace

int factorFor(const std::string& srcFullPath) {
    int sw = 0, sh = 0, comp = 0;
    if (!stbi_info(srcFullPath.c_str(), &sw, &sh, &comp) || sw < 1 || sh < 1)
        return 1;
    const int cell = cellFor(sw < sh ? sw : sh);
    return 512 / cell;  // 2, 4 or 8
}

std::string bakedBinPath(const std::string& srcRel) {
    std::string s = "stoch/";
    for (char c : srcRel) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9');
        s += ok ? c : '_';
    }
    return s + ".png";
}

std::vector<uint8_t> generate(const std::string& srcFullPath,
                              const std::string& seedKey, int& outW, int& outH,
                              int& factor) {
    outW = outH = 0;
    factor = 1;
    int sw = 0, sh = 0, comp = 0;
    unsigned char* px = stbi_load(srcFullPath.c_str(), &sw, &sh, &comp, 4);
    if (!px) return {};

    const int cell = cellFor(sw < sh ? sw : sh);
    // Resample the source to a clean square cell (bilinear); this is the tile
    // scattered inside the supertile.
    std::vector<uint8_t> src = pngquant::resizeRGBA(px, sw, sh, cell, cell);
    stbi_image_free(px);
    factor = 512 / cell;
    const int S = cell * factor;  // == 512
    outW = outH = S;

    std::vector<uint8_t> out((size_t)S * S * 4, 255);
    // Base = the source tiled factor x factor (so nothing is ever bare).
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x) {
            const uint8_t* s = &src[((size_t)(y % cell) * cell + (x % cell)) * 4];
            uint8_t* o = &out[((size_t)y * S + x) * 4];
            o[0] = s[0], o[1] = s[1], o[2] = s[2], o[3] = 255;
        }

    // Scatter feathered, randomly transformed stamps of the source. Each stamp
    // fades to zero at its own radius, and destination writes wrap on the torus
    // (mod S), so the supertile stays perfectly tileable while its interior no
    // longer repeats on the source grid.
    uint32_t rng = fnv1a(seedKey);
    auto rnd = [&]() {
        rng = rng * 1664525u + 1013904223u;
        return rng >> 8;
    };
    auto sampleSrc = [&](int sx, int sy, int c) {
        sx = ((sx % cell) + cell) % cell;
        sy = ((sy % cell) + cell) % cell;
        return (float)src[((size_t)sy * cell + sx) * 4 + c];
    };

    const int stamps = 2 * factor * factor;
    const float strength = 0.72f;
    const float kPi = 3.14159265f;
    for (int k = 0; k < stamps; ++k) {
        const int rot = rnd() % 4;
        const bool flip = (rnd() & 1) != 0;
        const int ox = (int)(rnd() % cell), oy = (int)(rnd() % cell);
        const int cx = (int)(rnd() % S), cy = (int)(rnd() % S);
        const int r = cell / 2 + (int)(rnd() % (cell / 2 + 1));  // [cell/2, cell)
        for (int dy = -r; dy <= r; ++dy) {
            for (int dx = -r; dx <= r; ++dx) {
                const float dist = std::sqrt((float)(dx * dx + dy * dy));
                if (dist >= r) continue;
                const float m =
                    (0.5f + 0.5f * std::cos(dist / r * kPi)) * strength;
                // Transform the local offset (flip then rotate) to sample a
                // rotated/mirrored window of the source.
                int lx = flip ? -dx : dx, ly = dy;
                int tx, ty;
                switch (rot) {
                    case 1: tx = -ly, ty = lx; break;
                    case 2: tx = -lx, ty = -ly; break;
                    case 3: tx = ly, ty = -lx; break;
                    default: tx = lx, ty = ly; break;
                }
                const int px_ = (((cx + dx) % S) + S) % S;
                const int py_ = (((cy + dy) % S) + S) % S;
                uint8_t* o = &out[((size_t)py_ * S + px_) * 4];
                for (int c = 0; c < 3; ++c)
                    o[c] = (uint8_t)(o[c] * (1.0f - m) +
                                     sampleSrc(tx + ox, ty + oy, c) * m);
            }
        }
    }
    return out;
}

}  // namespace stochtile
