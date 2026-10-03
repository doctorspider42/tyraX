#include "roadtex.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <stb_image_write.h>  // implementation lives in menubake.cpp

namespace roadtex {
namespace {

// The road tessellator's V repeat (roadgen::kTexLen) and the junction patch's
// world-space repeat (docs/roads.md "Intersection material"). Copied rather
// than included so this module stays free of the road geometry code.
constexpr float kStripLen = 4.0f;
constexpr float kJunctionExtent = 32.0f;
// The pavement strip's mapping: one repeat per 2 units across AND along.
constexpr float kPavementExtent = 2.0f;

uint32_t hash3(uint32_t a, uint32_t b, uint32_t c) {
    uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA77u ^ (c + 0x165667B1u) * 0xC2B2AE3Du;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    h *= 0x297A2D39u;
    h ^= h >> 15;
    return h;
}

float hash01(uint32_t a, uint32_t b, uint32_t c) {
    return (float)(hash3(a, b, c) & 0xFFFFFFu) / 16777215.0f;
}

int wrap(int i, int n) { return ((i % n) + n) % n; }
float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
float smooth(float t) { return t * t * (3.0f - 2.0f * t); }
float smoothstep(float e0, float e1, float x) {
    if (e1 <= e0) return x < e0 ? 0.0f : 1.0f;
    return smooth(clamp01((x - e0) / (e1 - e0)));
}
float mix(float a, float b, float t) { return a + (b - a) * t; }

// The texture's world extent and a seed: every noise lookup is in UV but
// sized in world units, so a feature looks the same size on a strip (W x 4)
// and on a junction patch (32 x 32).
struct Field {
    int n;           // texels per side
    float extU, extV;  // world units the texture spans
    uint32_t seed;

    // Lattice cells per tile for a feature of `size` units: an INTEGER, so the
    // lattice wraps exactly at the tile edge (the image tiles), and never
    // finer than one texel (it would only alias).
    int cells(float ext, float size) const {
        int c = (int)std::lround(ext / size);
        return std::clamp(c, 1, n);
    }

    float lattice(uint32_t s, int x, int y, int cx, int cy) const {
        return hash01(seed ^ s, (uint32_t)wrap(x, cx), (uint32_t)wrap(y, cy));
    }

    // Periodic value noise, 0..1.
    float noise(uint32_t s, float u, float v, int cx, int cy) const {
        const float x = u * cx, y = v * cy;
        const int ix = (int)std::floor(x), iy = (int)std::floor(y);
        const float fx = smooth(x - ix), fy = smooth(y - iy);
        const float a = lattice(s, ix, iy, cx, cy), b = lattice(s, ix + 1, iy, cx, cy);
        const float c = lattice(s, ix, iy + 1, cx, cy), d = lattice(s, ix + 1, iy + 1, cx, cy);
        const float top = a + (b - a) * fx, bot = c + (d - c) * fx;
        return top + (bot - top) * fy;
    }

    // fBm: each octave doubles the cell count, so it stays periodic.
    float fbm(uint32_t s, float u, float v, float size, int octaves) const {
        int cx = cells(extU, size), cy = cells(extV, size);
        float sum = 0.0f, amp = 0.5f, norm = 0.0f;
        for (int o = 0; o < octaves; ++o) {
            sum += amp * noise(s + (uint32_t)o * 131u, u, v, cx, cy);
            norm += amp;
            cx = std::min(cx * 2, n), cy = std::min(cy * 2, n);
            amp *= 0.5f;
        }
        return sum / norm;
    }

    // Jittered pebbles on a periodic cell grid: returns coverage 0..1 of the
    // nearest pebble and its id (for colour), dome shading in *dome.
    float pebbles(uint32_t s, float u, float v, float size, float density, float* id,
                  float* dome) const {
        const int cx = cells(extU, size), cy = cells(extV, size);
        const float x = u * cx, y = v * cy;
        const int ix = (int)std::floor(x), iy = (int)std::floor(y);
        float best = 0.0f;
        *id = 0.0f, *dome = 0.0f;
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                const int gx = wrap(ix + dx, cx), gy = wrap(iy + dy, cy);
                if (hash01(seed ^ s, (uint32_t)gx, (uint32_t)gy + 7919u) > density) continue;
                const float px = (float)(ix + dx) + 0.2f + 0.6f * hash01(seed ^ s, (uint32_t)gx, (uint32_t)gy + 31u);
                const float py = (float)(iy + dy) + 0.2f + 0.6f * hash01(seed ^ s, (uint32_t)gx, (uint32_t)gy + 57u);
                const float r = 0.32f + 0.22f * hash01(seed ^ s, (uint32_t)gx, (uint32_t)gy + 91u);
                const float d = std::sqrt((x - px) * (x - px) + (y - py) * (y - py)) / r;
                const float cov = 1.0f - smoothstep(0.8f, 1.0f, d);
                if (cov > best) {
                    best = cov;
                    *id = hash01(seed ^ s, (uint32_t)gx, (uint32_t)gy + 113u);
                    // lit from the top-left: brighter on that side of the dome
                    *dome = clamp01(1.0f - d) * 0.6f + clamp01(((px - x) + (py - y)) / r) * 0.4f;
                }
            }
        return best;
    }
};

// Fraction of [a0, a1] covered by [b0, b1].
float overlap(float a0, float a1, float b0, float b1) {
    const float lo = std::max(a0, b0), hi = std::min(a1, b1);
    return hi > lo ? (hi - lo) / (a1 - a0) : 0.0f;
}

struct Rgb {
    float r, g, b;
};

Rgb lerp(Rgb a, Rgb b, float t) { return {mix(a.r, b.r, t), mix(a.g, b.g, t), mix(a.b, b.b, t)}; }
Rgb scale(Rgb a, float k) { return {a.r * k, a.g * k, a.b * k}; }

// One painted stripe across U (world units), solid or dashed along V.
struct Line {
    float c, halfW;  // centre and half width, world units across the road
    int dashes;      // dashes per 4-unit repeat; 0 = solid
    float dashFrac;  // fraction of each period that is painted
    Rgb colour;
};

// `texel` = world units per texel across: no line is drawn thinner than about
// one texel, or a wide road at 64 px would fade its lines to grey.
struct LineBuilder {
    float texel;
    std::vector<Line>* out;

    float half(const LinePaint& l) const { return std::max(0.5f * l.width, 0.55f * texel); }
    // Centre-to-centre half spacing of a pair: a gap of one line width, and
    // never under two texels so at least one of them is clear road.
    float pairOff(const LinePaint& l) const { return std::max(2.0f * half(l), half(l) + 1.05f * texel); }
    // How far the pattern reaches from its centre.
    float reach(const LinePaint& l) const {
        const bool pair = l.style == kLineDouble || l.style == kLineSolidDashed ||
                          l.style == kLineDashedSolid;
        return pair ? pairOff(l) + half(l) : half(l);
    }

    void stripe(const LinePaint& l, float c, bool dashed) const {
        Line ln{c, half(l), 0, 1.0f, {l.colour[0], l.colour[1], l.colour[2]}};
        if (dashed) {
            float d = 0.0f, g = 0.0f;
            ln.dashes = quantizeDash(l, &d, &g);
            ln.dashFrac = d / (d + g);
        }
        out->push_back(ln);
    }

    // `mirror` swaps the sides of the mixed kinds (the right-hand edge line).
    void add(const LinePaint& l, float b, bool mirror) const {
        const float off = pairOff(l);
        switch (l.style) {
            case kLineDashed: stripe(l, b, true); break;
            case kLineSolid: stripe(l, b, false); break;
            case kLineDouble:
                stripe(l, b - off, false);
                stripe(l, b + off, false);
                break;
            case kLineSolidDashed:
            case kLineDashedSolid: {
                const bool solidLeft = (l.style == kLineSolidDashed) != mirror;
                stripe(l, b - off, !solidLeft);
                stripe(l, b + off, solidLeft);
                break;
            }
            default: break;
        }
    }
};

std::vector<Line> markingLines(const RoadTexParams& p, float W, float texel) {
    std::vector<Line> out;
    if (p.isotropic()) return out;
    const LineBuilder lb{texel, &out};
    const float laneInset = std::min(0.35f, W * 0.08f);
    if (p.edge.style != kLineNone) {
        const float inset = std::min(std::max(laneInset, lb.reach(p.edge) + 0.05f), W * 0.2f);
        lb.add(p.edge, inset, false);
        lb.add(p.edge, W - inset, true);
    }
    const int lanes = std::clamp(p.lanes, 0, 6);
    if (lanes < 2) return out;
    const float laneW = (W - 2.0f * laneInset) / (float)lanes;
    const int centreAt = lanes / 2;
    for (int k = 1; k < lanes; ++k)
        lb.add(k == centreAt ? p.centre : p.divider, laneInset + laneW * (float)k, false);
    return out;
}

bool writeIfChanged(const std::filesystem::path& path, const std::string& bytes) {
    {
        std::ifstream in(path, std::ios::binary);
        if (in) {
            std::ostringstream cur;
            cur << in.rdbuf();
            if (cur.str() == bytes) return true;
        }
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(bytes.data(), (std::streamsize)bytes.size());
    return (bool)out;
}

const char* const kSurfaceKeys[] = {"asphalt", "cobble", "gravel", "dirt", "slabs", "pavers"};
const char* const kStyleKeys[] = {"none", "dashed", "solid", "double", "solid-dashed",
                                  "dashed-solid"};

int lookup(const char* const* keys, int n, const std::string& v) {
    for (int i = 0; i < n; ++i)
        if (v == keys[i]) return i;
    char* end = nullptr;
    const long k = std::strtol(v.c_str(), &end, 10);
    if (end && *end == '\0' && !v.empty() && k >= 0 && k < n) return (int)k;
    return -1;
}

bool parseBool(const std::string& v, bool* out) {
    if (v == "1" || v == "true" || v == "on" || v == "yes") return *out = true, true;
    if (v == "0" || v == "false" || v == "off" || v == "no") return *out = false, true;
    return false;
}

bool parseFloat(const std::string& v, float* out) {
    char* end = nullptr;
    const float f = std::strtof(v.c_str(), &end);
    if (v.empty() || !end || *end != '\0' || !std::isfinite(f)) return false;
    *out = f;
    return true;
}

bool parseInt(const std::string& v, int* out) {
    char* end = nullptr;
    const long k = std::strtol(v.c_str(), &end, 10);
    if (v.empty() || !end || *end != '\0') return false;
    *out = (int)k;
    return true;
}

}  // namespace

bool LinePaint::operator==(const LinePaint& o) const {
    return style == o.style && colour[0] == o.colour[0] && colour[1] == o.colour[1] &&
           colour[2] == o.colour[2] && width == o.width && dash == o.dash && gap == o.gap;
}

RoadTexParams::RoadTexParams() {
    centre.style = kLineDashed;  // 2 on / 2 off
    divider.style = kLineDashed;
    divider.dash = 1.5f, divider.gap = 2.5f;
    edge.style = kLineSolid;
    edge.width = 0.15f;
    edge.dash = 1.0f, edge.gap = 1.0f;
}

bool RoadTexParams::operator==(const RoadTexParams& o) const {
    return surface == o.surface && lanes == o.lanes && centre == o.centre &&
           divider == o.divider && edge == o.edge && wear == o.wear && grime == o.grime &&
           cracks == o.cracks &&
           tint[0] == o.tint[0] && tint[1] == o.tint[1] && tint[2] == o.tint[2] &&
           seed == o.seed && size == o.size && width == o.width &&
           raggedEdges == o.raggedEdges && intersection == o.intersection &&
           pavement == o.pavement && slabSize == o.slabSize && jointWidth == o.jointWidth;
}

int quantizeDash(const LinePaint& l, float* dashOut, float* gapOut) {
    const float d = std::max(0.05f, l.dash), g = std::max(0.05f, l.gap);
    const int k = std::clamp((int)std::lround(kStripLen / (d + g)), 1, 32);
    const float period = kStripLen / (float)k;
    *dashOut = period * d / (d + g);
    *gapOut = period - *dashOut;
    return k;
}

float designWidth(const RoadTexParams& p) {
    if (p.width > 0.5f) return p.width;
    const int lanes = std::clamp(p.lanes, 0, 6);
    return lanes > 0 ? (float)lanes * 3.0f + 1.5f : 6.0f;
}

void tileExtent(const RoadTexParams& p, float* across, float* along) {
    if (p.pavement) {
        *across = *along = kPavementExtent;
    } else if (p.intersection) {
        *across = *along = kJunctionExtent;
    } else {
        *across = designWidth(p);
        *along = kStripLen;
    }
}

void slabGrid(const RoadTexParams& p, int* cols, int* rows) {
    float W = 0.0f, L = 0.0f;
    tileExtent(p, &W, &L);
    const int n = p.size == 64 || p.size == 256 ? p.size : 128;
    const float s = std::clamp(p.slabSize, 0.1f, 2.0f);
    if (std::clamp(p.surface, 0, kSurfaceCount - 1) == kPavers) {
        // bricks half the slab size long, a quarter wide; rows EVEN so the
        // running bond's half-brick offset wraps at the tile edge
        *cols = std::clamp((int)std::lround(W / (0.5f * s)), 1, n / 4);
        *rows = std::clamp((int)std::lround(L / (0.25f * s)), 2, n / 2);
        *rows += *rows & 1;
    } else {
        *cols = std::clamp((int)std::lround(W / s), 1, n / 4);
        *rows = std::clamp((int)std::lround(L / s), 1, n / 4);
    }
}

bool edgeLineSpan(const RoadTexParams& p, float* u0, float* u1) {
    if (p.isotropic() || p.edge.style == kLineNone) return false;
    const int n = p.size == 64 || p.size == 256 ? p.size : 128;
    const float W = designWidth(p);
    const std::vector<Line> lines = markingLines(p, W, W / (float)n);
    // The edge lines are laid first, left then right; the left one's stripes
    // are the ones left of the middle among them.
    float lo = 1e30f, hi = -1e30f;
    for (const Line& l : lines) {
        if (l.c > 0.25f * W) break;
        lo = std::min(lo, l.c - l.halfW);
        hi = std::max(hi, l.c + l.halfW);
    }
    if (!(hi > lo) || !(W > 0.0f)) return false;
    *u0 = lo / W;
    *u1 = hi / W;
    return true;
}

std::vector<unsigned char> generate(const RoadTexParams& p) {
    const int n = p.size == 64 || p.size == 256 ? p.size : 128;
    std::vector<unsigned char> px((size_t)n * n * 4, 255);
    const bool junction = p.isotropic();  // no direction: no tracks, no edges
    float W = 0.0f, L = 0.0f;
    tileExtent(p, &W, &L);
    const Field f{n, W, L, (uint32_t)p.seed * 2654435761u + 17u};
    const float wear = clamp01(p.wear);
    const float grime = clamp01(p.grime), cracks = clamp01(p.cracks);
    const int surface = std::clamp(p.surface, 0, kSurfaceCount - 1);
    int slabCols = 1, slabRows = 1;
    slabGrid(p, &slabCols, &slabRows);
    // half the joint, never under half a texel so it cannot vanish
    const float jointHalf = std::max(0.5f * std::max(0.0f, p.jointWidth), 0.5f * W / (float)n);
    const Rgb tint{std::max(0.0f, p.tint[0]), std::max(0.0f, p.tint[1]),
                   std::max(0.0f, p.tint[2])};
    const std::vector<Line> lines = markingLines(p, W, W / (float)n);
    const int lanes = std::clamp(p.lanes, 0, 6);
    const bool ragged = p.raggedEdges && surface == kDirt && !junction;

    // Wheel paths (the polished / rutted tracks): two per lane, constant along
    // V so they tile trivially. A junction patch has no direction, so none.
    const float inset = std::min(0.35f, W * 0.08f);
    const int trackLanes =
        lanes > 0 ? lanes : (surface == kGravel || surface == kDirt ? 1 : 0);
    const float laneW = trackLanes > 0 ? (W - 2.0f * inset) / (float)trackLanes : 0.0f;
    auto wheelTrack = [&](float wx) {
        if (junction || trackLanes == 0) return 0.0f;
        float t = 0.0f;
        for (int k = 0; k < trackLanes; ++k) {
            const float c = inset + laneW * ((float)k + 0.5f);
            for (float s : {-0.27f, 0.27f}) {
                const float d = (wx - (c + s * laneW)) / 0.38f;
                t = std::max(t, std::exp(-d * d));
            }
        }
        return t;
    };
    // The rubber / oil strip down the middle of each lane (between the wheel
    // paths) - paved surfaces only, strips only.
    const bool paved = surface == kAsphalt || surface == kCobble || surface == kSlabs ||
                       surface == kPavers;
    auto laneCentre = [&](float wx) {
        if (junction || trackLanes == 0 || !paved) return 0.0f;
        float t = 0.0f;
        for (int k = 0; k < trackLanes; ++k) {
            const float d = (wx - (inset + laneW * ((float)k + 0.5f))) / 0.42f;
            t = std::max(t, std::exp(-d * d));
        }
        return t;
    };
    const float texelU = W / (float)n;
    // Tar-sealed seams (asphalt strips): one along the road and, with enough
    // cracks, one across it, placed by the seed. Both wobble with periodic
    // noise, so the texture still tiles. Not on a junction patch: a straight
    // seam repeating every 32 units would read as a grid.
    const float seamU = W * (0.2f + 0.6f * hash01(f.seed ^ 0x5EA1u, 1u, 2u));
    const float seamV = 0.15f + 0.7f * hash01(f.seed ^ 0x5EA1u, 3u, 4u);
    const bool seamAcross = hash01(f.seed ^ 0x5EA1u, 5u, 6u) < cracks * 1.6f;
    const float texelV = L / (float)n;
    const float seamHalf = 0.035f;

    const float du = 1.0f / (float)n;

    for (int y = 0; y < n; ++y) {
        const float v = ((float)y + 0.5f) / (float)n;
        for (int x = 0; x < n; ++x) {
            const float u = ((float)x + 0.5f) / (float)n;
            const float wx = u * W;
            const float grain = f.fbm(11, u, v, 0.08f, 2);
            const float mid = f.fbm(23, u, v, 0.8f, 3);
            const float big = f.fbm(37, u, v, 5.0f, 3);
            const float track = wheelTrack(wx) * wear;
            const float speck = hash01(f.seed, (uint32_t)x, (uint32_t)y);
            Rgb c{};

            if (surface == kAsphalt) {
                const Rgb base{0.23f, 0.23f, 0.24f};
                float k = 1.0f + (grain - 0.5f) * 0.28f + (mid - 0.5f) * 0.16f;
                if (speck > 0.94f) k += 0.10f;          // light aggregate
                else if (speck < 0.05f) k -= 0.08f;     // voids
                // weathering: bleached areas and darker oil stains
                k += wear * 0.18f * smoothstep(0.55f, 0.35f, big);
                k -= wear * 0.22f * smoothstep(0.62f, 0.78f, big);
                k -= track * 0.10f;
                // cracks: thin ridges of a mid-scale field
                const float cr = 1.0f - std::fabs(2.0f * f.fbm(41, u, v, 1.6f, 3) - 1.0f);
                k -= cracks * 0.45f * smoothstep(0.93f, 0.985f, cr);
                // a second, finer crack net where the wear is (crazing)
                const float cr2 = 1.0f - std::fabs(2.0f * f.fbm(47, u, v, 0.6f, 2) - 1.0f);
                k -= cracks * 0.25f * smoothstep(0.96f, 0.99f, cr2) * smoothstep(0.45f, 0.7f, big);
                // large tone blotches (old patching, sun-bleached areas)
                k += wear * 0.20f * (f.fbm(97, u, v, 3.0f, 2) - 0.5f);
                // repairs: rectangles of newer, darker and smoother tar on a
                // coarse periodic grid (inside their cells: no seam crossing)
                {
                    const int rcx = f.cells(W, 3.0f), rcy = f.cells(L, 4.0f);
                    const float gx = u * (float)rcx, gy = v * (float)rcy;
                    const int ix = (int)std::floor(gx), iy = (int)std::floor(gy);
                    const uint32_t hx = (uint32_t)wrap(ix, rcx), hy = (uint32_t)wrap(iy, rcy);
                    if (hash01(f.seed ^ 0xFA7Cu, hx, hy) < 0.45f * wear) {
                        const float x0 = 0.08f + 0.35f * hash01(f.seed ^ 0xFA7Du, hx, hy);
                        const float y0 = 0.08f + 0.35f * hash01(f.seed ^ 0xFA7Eu, hx, hy);
                        const float x1 = std::min(0.92f, x0 + 0.25f + 0.4f * hash01(f.seed ^ 0xFA7Fu, hx, hy));
                        const float y1 = std::min(0.92f, y0 + 0.25f + 0.4f * hash01(f.seed ^ 0xFA80u, hx, hy));
                        const float lx = gx - (float)ix, ly = gy - (float)iy;
                        const float ex = std::min(lx - x0, x1 - lx) * W / (float)rcx;
                        const float ey = std::min(ly - y0, y1 - ly) * L / (float)rcy;
                        const float in = smoothstep(-0.5f * texelU, 0.5f * texelU, std::min(ex, ey));
                        // fresh tar: darker, and its own finer grain
                        k = mix(k, 0.80f + (grain - 0.5f) * 0.12f, in);
                    }
                }
                // tar-sealed seams: thin, near-black, slightly glossy lines
                if (cracks > 0.0f && !junction) {
                    const float wob = (f.fbm(107, u, v, 1.5f, 2) - 0.5f) * 0.5f;
                    const float hu = std::max(seamHalf, 0.5f * texelU);
                    float seam = 1.0f - smoothstep(hu - 0.5f * texelU, hu + 0.5f * texelU,
                                                   std::fabs(wx - seamU - wob));
                    if (seamAcross) {
                        const float hv = std::max(seamHalf, 0.5f * texelV);
                        float dv = v - seamV - 0.3f * wob / L;
                        dv -= std::floor(dv + 0.5f);  // periodic distance
                        seam = std::max(seam, 1.0f - smoothstep(hv - 0.5f * texelV, hv + 0.5f * texelV,
                                                                std::fabs(dv) * L));
                    }
                    k = mix(k, 0.62f, seam * std::min(1.0f, cracks * 1.6f));
                }
                c = scale(base, k);
            } else if (surface == kCobble) {
                // Setts: rows along V, each row offset half a stone. Integer
                // rows (even, so the offset pattern wraps) and columns per tile.
                int rows = f.cells(L, 0.28f);
                rows += rows & 1;
                const int cols = f.cells(W, 0.45f);
                const float ry = v * (float)rows;
                const int row = (int)std::floor(ry);
                const float cx = u * (float)cols + ((row & 1) ? 0.5f : 0.0f);
                const int col = wrap((int)std::floor(cx), cols);
                const float fx = cx - std::floor(cx), fy = ry - (float)row;
                const float ex = std::min(fx, 1.0f - fx) * W / (float)cols;
                const float ey = std::min(fy, 1.0f - fy) * L / (float)rows;
                const float e = std::min(ex, ey);  // distance to the joint, units
                const float stoneId = hash01(f.seed ^ 0xC0BBu, (uint32_t)col, (uint32_t)row);
                const Rgb stone = lerp(Rgb{0.40f, 0.38f, 0.36f}, Rgb{0.50f, 0.46f, 0.41f},
                                       hash01(f.seed ^ 0xC0BCu, (uint32_t)col, (uint32_t)row));
                float k = 0.82f + 0.36f * stoneId + (grain - 0.5f) * 0.22f;
                k *= 0.78f + 0.22f * smoothstep(0.02f, 0.08f, e);       // bevel
                k += 0.06f * (0.5f - fy) * smoothstep(0.02f, 0.06f, e);  // top-lit
                k += track * 0.12f;  // polished where the wheels run
                const float joint = 1.0f - smoothstep(0.018f, 0.034f, e);
                const Rgb mortar = scale(Rgb{0.20f, 0.19f, 0.17f}, 1.0f - wear * 0.25f);
                c = lerp(scale(stone, k), mortar, joint);
                c = scale(c, 1.0f - wear * 0.15f * smoothstep(0.6f, 0.75f, big));
            } else if (surface == kGravel) {
                const Rgb base{0.47f, 0.44f, 0.39f};
                float k = 0.92f + (grain - 0.5f) * 0.30f + (mid - 0.5f) * 0.20f;
                k -= track * 0.16f;  // compacted, darker ruts
                c = scale(base, k);
                float id = 0.0f, dome = 0.0f;
                const float cov = f.pebbles(53, u, v, 0.11f, 0.85f - track * 0.5f, &id, &dome);
                const Rgb peb = lerp(Rgb{0.36f, 0.34f, 0.31f}, Rgb{0.66f, 0.62f, 0.55f}, id);
                c = lerp(c, scale(peb, 0.75f + 0.45f * dome), cov);
                c = scale(c, 1.0f - wear * 0.18f * smoothstep(0.6f, 0.78f, big));
            } else if (surface == kSlabs || surface == kPavers) {
                // A grid of slabs (or a running bond of pavers): whole slabs
                // per tile on both axes, so the pattern wraps in U and V.
                const bool pavers = surface == kPavers;
                const float ry = v * (float)slabRows;
                const int row = (int)std::floor(ry);
                const float cx = u * (float)slabCols + (pavers && (row & 1) ? 0.5f : 0.0f);
                const int col = wrap((int)std::floor(cx), slabCols);
                const float fx = cx - std::floor(cx), fy = ry - (float)row;
                const float ex = std::min(fx, 1.0f - fx) * W / (float)slabCols;
                const float ey = std::min(fy, 1.0f - fy) * L / (float)slabRows;
                const float e = std::min(ex, ey);  // distance to the joint, units
                const uint32_t sid = pavers ? 0xBA7Eu : 0x51ABu;
                const float t0 = hash01(f.seed ^ sid, (uint32_t)col, (uint32_t)row);
                const float t1 = hash01(f.seed ^ (sid + 1u), (uint32_t)col, (uint32_t)row);
                const Rgb slab = pavers ? lerp(Rgb{0.46f, 0.25f, 0.18f}, Rgb{0.58f, 0.36f, 0.25f}, t1)
                                        : lerp(Rgb{0.58f, 0.57f, 0.54f}, Rgb{0.66f, 0.64f, 0.60f}, t1);
                float k = 0.90f + 0.18f * t0 + (grain - 0.5f) * 0.16f + (mid - 0.5f) * 0.06f;
                k *= 0.90f + 0.10f * smoothstep(jointHalf, jointHalf + 0.04f, e);  // worn arris
                // wear: grime that ignores the slab edges, and the odd
                // stained / sunken slab darker than its neighbours
                k -= wear * 0.20f * smoothstep(0.55f, 0.75f, big);
                const float stained = hash01(f.seed ^ (sid + 2u), (uint32_t)col, (uint32_t)row);
                k -= wear * 0.18f * smoothstep(0.85f, 0.95f, stained);
                const float cr = 1.0f - std::fabs(2.0f * f.fbm(43, u, v, 0.7f, 3) - 1.0f);
                // hairline cracks, only on some slabs (a crack stops at a joint)
                const float cracked = hash01(f.seed ^ (sid + 3u), (uint32_t)col, (uint32_t)row);
                if (!pavers && cracked < 0.35f * cracks)
                    k -= cracks * 0.3f * smoothstep(0.95f, 0.99f, cr);
                const float texel = W / (float)n;
                const float joint = 1.0f - smoothstep(jointHalf - 0.5f * texel, jointHalf + 0.5f * texel, e);
                const Rgb jointCol = scale(pavers ? Rgb{0.33f, 0.31f, 0.27f} : Rgb{0.30f, 0.29f, 0.27f},
                                           1.0f - wear * 0.30f);
                c = lerp(scale(slab, k), jointCol, joint);
            } else {  // dirt / mud
                const Rgb base{0.40f, 0.30f, 0.20f};
                float k = 0.92f + (grain - 0.5f) * 0.26f + (mid - 0.5f) * 0.30f +
                          (big - 0.5f) * 0.20f;
                k -= track * 0.22f;
                c = scale(base, k);
                // wet mud: darker and greyer where a large field peaks
                const float mud = smoothstep(0.60f - wear * 0.12f, 0.70f - wear * 0.12f,
                                             f.fbm(61, u, v, 2.5f, 3)) *
                                  (0.35f + 0.65f * wear);
                c = lerp(c, Rgb{0.19f, 0.15f, 0.11f}, mud * 0.85f);
                float id = 0.0f, dome = 0.0f;
                const float cov = f.pebbles(67, u, v, 0.18f, 0.12f, &id, &dome);
                const Rgb peb = lerp(Rgb{0.38f, 0.34f, 0.29f}, Rgb{0.58f, 0.53f, 0.46f}, id);
                c = lerp(c, scale(peb, 0.75f + 0.4f * dome), cov * (1.0f - mud));
            }
            c = {c.r * tint.r, c.g * tint.g, c.b * tint.b};

            // Grime. Each term is multiplied by `grime`, so at 0 it is exact.
            if (grime > 0.0f) {
                // the rubber strip down each lane, mottled
                c = scale(c, 1.0f - grime * 0.16f * laneCentre(wx) * (0.6f + 0.8f * mid));
                if (!junction) {
                    const float de = std::min(wx, W - wx);  // units to the nearer edge
                    // dust swept toward the sides, then a dark gutter at the edge
                    const float dust = smoothstep(0.15f, 0.5f, de) * (1.0f - smoothstep(0.8f, 2.2f, de));
                    const Rgb dustCol{0.50f * tint.r, 0.47f * tint.g, 0.42f * tint.b};
                    c = lerp(c, dustCol, grime * 0.22f * dust * (0.5f + mid));
                    const float gutter = 1.0f - smoothstep(0.0f, 0.45f, de);
                    c = scale(c, 1.0f - grime * 0.40f * gutter * (0.7f + 0.6f * big));
                }
            }

            // Paint: exact box-filtered coverage of the texel by each line,
            // chipped by wear.
            if (!lines.empty()) {
                const float u0 = (float)x * du * W, u1 = (float)(x + 1) * du * W;
                const float v0 = (float)y * du, v1 = (float)(y + 1) * du;
                const float chip = f.fbm(71, u, v, 0.18f, 3);
                float keep = 1.0f - std::min(1.0f, wear * 1.4f) *
                                        smoothstep(0.72f - 0.3f * wear, 0.84f - 0.3f * wear, chip);
                float raggedW = 1.0f;
                if (wear > 0.0f) {
                    // patchy fading (low-frequency), and lengths broken off
                    keep *= 1.0f - std::min(0.6f, wear * 0.9f) *
                                       smoothstep(0.45f, 0.75f, f.fbm(101, u, v, 1.2f, 3));
                    keep *= 1.0f - std::min(1.0f, wear * 1.5f) *
                                       smoothstep(0.80f - 0.22f * wear, 0.85f - 0.22f * wear,
                                                  f.fbm(103, u, v, 2.5f, 2));
                    // the surface's pits show through
                    keep *= 1.0f - std::min(0.7f, wear * 1.4f) * smoothstep(0.40f, 0.28f, grain);
                    // ragged paint edge: the width breathes along the line
                    raggedW = 1.0f + wear * 0.5f * (f.fbm(109, u, v, 0.15f, 2) - 0.5f);
                }
                for (const Line& ln : lines) {
                    float cov = 0.0f;
                    if (wear > 0.0f) {
                        // soft falloff instead of a hard stripe
                        const float hw = ln.halfW * raggedW;
                        const float soft = texelU * (0.5f + 1.2f * wear);
                        cov = 1.0f - smoothstep(hw - soft, hw + soft, std::fabs(wx - ln.c));
                    } else {
                        cov = overlap(u0, u1, ln.c - ln.halfW, ln.c + ln.halfW);
                    }
                    if (cov <= 0.0f) continue;
                    if (ln.dashes > 0) {
                        // dashes centred in their periods: none crosses the V seam
                        float along = 0.0f;
                        const float h = 0.5f * ln.dashFrac / (float)ln.dashes;
                        for (int j = 0; j < ln.dashes; ++j) {
                            const float m = ((float)j + 0.5f) / (float)ln.dashes;
                            along += overlap(v0, v1, m - h, m + h);
                        }
                        cov *= along;
                    }
                    if (cov <= 0.0f) continue;
                    c = lerp(c, scale(ln.colour, 0.92f + 0.16f * grain), cov * keep);
                }
            }

            unsigned char* o = &px[((size_t)y * n + x) * 4];
            o[0] = (unsigned char)std::lround(clamp01(c.r) * 255.0f);
            o[1] = (unsigned char)std::lround(clamp01(c.g) * 255.0f);
            o[2] = (unsigned char)std::lround(clamp01(c.b) * 255.0f);
            if (ragged) {
                // up to ~0.3 units of notched edge, varying along V (periodic)
                const int cv = f.cells(L, 0.35f);
                const float maxDepth = std::min(0.3f, W * 0.06f);
                const float dl = maxDepth * (0.15f + 0.85f * f.noise(83, 0.0f, v, 1, cv));
                const float dr = maxDepth * (0.15f + 0.85f * f.noise(89, 0.0f, v, 1, cv));
                const float edgeNoise = (grain - 0.5f) * 0.12f;
                if (wx < dl + edgeNoise || W - wx < dr + edgeNoise) o[3] = 0;
            }
        }
    }
    return px;
}

std::string fileStem(const std::string& name) {
    std::string s;
    for (char ch : name) {
        const unsigned char u = (unsigned char)ch;
        if (std::isalnum(u)) s += (char)std::tolower(u);
        else if (!s.empty() && s.back() != '-') s += '-';
    }
    while (!s.empty() && s.back() == '-') s.pop_back();
    return s.empty() ? "road" : s;
}

std::string toText(const RoadTexParams& p) {
    std::string out =
        "# Road Texture Generator recipe (docs/road-textures.md) - editor-only\n";
    char buf[256];
    std::snprintf(buf, sizeof buf,
                  "surface=%s\nlanes=%d\nwear=%.6g\ntint=%.6g,%.6g,%.6g\nseed=%d\nsize=%d\n"
                  "width=%.6g\nragged=%d\nintersection=%d\n",
                  kSurfaceKeys[std::clamp(p.surface, 0, kSurfaceCount - 1)], p.lanes, p.wear,
                  p.tint[0], p.tint[1], p.tint[2], p.seed, p.size, p.width,
                  p.raggedEdges ? 1 : 0, p.intersection ? 1 : 0);
    out += buf;
    std::snprintf(buf, sizeof buf, "grime=%.6g\ncracks=%.6g\npavement=%d\nslab=%.6g\njoint=%.6g\n",
                  p.grime, p.cracks, p.pavement ? 1 : 0, p.slabSize, p.jointWidth);
    out += buf;
    const struct {
        const char* key;
        const LinePaint* l;
    } lines[] = {{"centre", &p.centre}, {"divider", &p.divider}, {"edge", &p.edge}};
    for (const auto& ln : lines) {
        const LinePaint& l = *ln.l;
        std::snprintf(buf, sizeof buf,
                      "%s=%s\n%s.colour=%.6g,%.6g,%.6g\n%s.width=%.6g\n%s.dash=%.6g\n"
                      "%s.gap=%.6g\n",
                      ln.key, kStyleKeys[std::clamp(l.style, 0, kLineStyleCount - 1)], ln.key,
                      l.colour[0], l.colour[1], l.colour[2], ln.key, l.width, ln.key, l.dash,
                      ln.key, l.gap);
        out += buf;
    }
    return out;
}

namespace {

// "r,g,b" with each 0..max.
bool parseTriple(const std::string& value, float lo, float hi, float* out) {
    std::string parts[3];
    int i = 0;
    for (char ch : value) {
        if (ch == ',') {
            if (++i > 2) return false;
        } else {
            parts[i] += ch;
        }
    }
    if (i != 2) return false;
    float t[3];
    for (int k = 0; k < 3; ++k)
        if (!parseFloat(parts[k], &t[k]) || t[k] < lo || t[k] > hi) return false;
    for (int k = 0; k < 3; ++k) out[k] = t[k];
    return true;
}

bool applyLineKey(LinePaint& l, const std::string& field, const std::string& value) {
    if (field.empty()) {
        const int k = lookup(kStyleKeys, kLineStyleCount, value);
        if (k < 0) return false;
        l.style = k;
        return true;
    }
    if (field == "colour" || field == "color") {
        if (value == "white") {
            for (int k = 0; k < 3; ++k) l.colour[k] = kWhite[k];
            return true;
        }
        if (value == "yellow") {
            for (int k = 0; k < 3; ++k) l.colour[k] = kYellow[k];
            return true;
        }
        return parseTriple(value, 0.0f, 1.0f, l.colour);
    }
    float f = 0.0f;
    if (!parseFloat(value, &f)) return false;
    if (field == "width") {
        if (f < 0.02f || f > 1.0f) return false;
        l.width = f;
    } else if (field == "dash") {
        if (f < 0.05f || f > 4.0f) return false;
        l.dash = f;
    } else if (field == "gap") {
        if (f < 0.05f || f > 4.0f) return false;
        l.gap = f;
    } else {
        return false;
    }
    return true;
}

}  // namespace

bool applyKey(RoadTexParams& p, const std::string& key, const std::string& value,
              std::string* err) {
    auto bad = [&]() {
        if (err) *err = "bad value for " + key + ": '" + value + "'";
        return false;
    };
    const size_t dot = key.find('.');
    const std::string head = key.substr(0, dot);
    const std::string field = dot == std::string::npos ? "" : key.substr(dot + 1);
    LinePaint* line = head == "centre" || head == "center" ? &p.centre
                      : head == "divider"                  ? &p.divider
                      : head == "edge"                     ? &p.edge
                                                           : nullptr;
    if (line) {
        if (!applyLineKey(*line, field, value)) return bad();
        return true;
    }
    if (key == "surface") {
        const int k = lookup(kSurfaceKeys, kSurfaceCount, value);
        if (k < 0) return bad();
        p.surface = k;
    } else if (key == "lanes") {
        int k = 0;
        if (!parseInt(value, &k) || k < 0 || k > 6) return bad();
        p.lanes = k;
    } else if (key == "edges") {  // shorthand: edge=solid / edge=none
        bool on = false;
        if (!parseBool(value, &on)) return bad();
        p.edge.style = on ? kLineSolid : kLineNone;
    } else if (key == "wear") {
        if (!parseFloat(value, &p.wear)) return bad();
        p.wear = clamp01(p.wear);
    } else if (key == "grime") {
        if (!parseFloat(value, &p.grime)) return bad();
        p.grime = clamp01(p.grime);
    } else if (key == "cracks") {
        if (!parseFloat(value, &p.cracks)) return bad();
        p.cracks = clamp01(p.cracks);
    } else if (key == "tint") {
        if (!parseTriple(value, 0.0f, 2.0f, p.tint)) return bad();
    } else if (key == "seed") {
        if (!parseInt(value, &p.seed)) return bad();
    } else if (key == "size") {
        int k = 0;
        if (!parseInt(value, &k) || (k != 64 && k != 128 && k != 256)) return bad();
        p.size = k;
    } else if (key == "width") {
        if (!parseFloat(value, &p.width) || p.width < 0.0f || p.width > 64.0f) return bad();
    } else if (key == "ragged") {
        if (!parseBool(value, &p.raggedEdges)) return bad();
    } else if (key == "intersection") {
        if (!parseBool(value, &p.intersection)) return bad();
    } else if (key == "pavement") {
        if (!parseBool(value, &p.pavement)) return bad();
    } else if (key == "slab") {
        if (!parseFloat(value, &p.slabSize) || p.slabSize < 0.1f || p.slabSize > 2.0f) return bad();
    } else if (key == "joint") {
        if (!parseFloat(value, &p.jointWidth) || p.jointWidth < 0.0f || p.jointWidth > 0.2f)
            return bad();
    } else {
        if (err) *err = "unknown key: " + key;
        return false;
    }
    return true;
}

RoadTexParams fromText(const std::string& text) {
    RoadTexParams p;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        applyKey(p, line.substr(0, eq), line.substr(eq + 1), nullptr);
    }
    return p;
}

std::string writeAssets(const std::string& projectDir, const std::string& name,
                        const RoadTexParams& p, std::string* err) {
    namespace fs = std::filesystem;
    const std::string stem = fileStem(name);
    const fs::path dir = fs::path(projectDir) / kDir;
    std::error_code ec;
    fs::create_directories(dir, ec);
    const std::vector<unsigned char> px = generate(p);
    const int n = (int)std::lround(std::sqrt((double)(px.size() / 4)));
    std::string png;
    stbi_write_png_to_func(
        [](void* ctx, void* data, int size) {
            static_cast<std::string*>(ctx)->append(static_cast<const char*>(data), (size_t)size);
        },
        &png, n, n, 4, px.data(), n * 4);
    const std::string mtl = "# Generated by TyraX (Tools > Road Texture Generator)\n"
                            "newmtl " + stem + "\nKd 1 1 1\nmap_Kd " + stem + ".png\n";
    const struct {
        const char* ext;
        const std::string* bytes;
    } files[] = {{".png", &png}, {".mtl", &mtl}};
    for (const auto& file : files) {
        const fs::path path = dir / (stem + file.ext);
        if (file.bytes->empty() || !writeIfChanged(path, *file.bytes)) {
            if (err) *err = "cannot write " + path.string();
            return "";
        }
    }
    const fs::path recipe = dir / (stem + ".roadtex");
    if (!writeIfChanged(recipe, toText(p))) {
        if (err) *err = "cannot write " + recipe.string();
        return "";
    }
    return std::string(kDir) + "/" + stem + ".mtl";
}

bool readRecipe(const std::string& projectDir, const std::string& name, RoadTexParams* out) {
    namespace fs = std::filesystem;
    std::ifstream in(fs::path(projectDir) / kDir / (fileStem(name) + ".roadtex"),
                     std::ios::binary);
    if (!in) return false;
    std::ostringstream s;
    s << in.rdbuf();
    *out = fromText(s.str());
    return true;
}

std::vector<Preset> presets() {
    std::vector<Preset> out;
    RoadTexParams two;  // the defaults: asphalt, 2 lanes, dashed white centre
    out.push_back({"road-2lane", two});
    RoadTexParams four;
    four.lanes = 4;
    four.centre.style = kLineDouble;
    for (int k = 0; k < 3; ++k) four.centre.colour[k] = kYellow[k];
    four.centre.width = 0.1f;
    four.seed = 2;
    out.push_back({"road-4lane", four});
    RoadTexParams dirt;
    dirt.surface = kDirt;
    dirt.lanes = 0;
    dirt.edge.style = kLineNone;
    dirt.wear = 0.6f;
    dirt.width = 5.0f;
    dirt.seed = 3;
    dirt.raggedEdges = true;
    out.push_back({"road-dirt", dirt});
    RoadTexParams cobble;
    cobble.surface = kCobble;
    cobble.lanes = 0;
    cobble.edge.style = kLineNone;
    cobble.width = 6.0f;
    cobble.seed = 4;
    out.push_back({"road-cobble", cobble});
    RoadTexParams junction;
    junction.intersection = true;
    junction.seed = 5;
    out.push_back({"road-junction", junction});
    RoadTexParams slabs;
    slabs.surface = kSlabs;
    slabs.pavement = true;
    slabs.lanes = 0;
    slabs.edge.style = kLineNone;
    slabs.centre.style = kLineNone;
    slabs.divider.style = kLineNone;
    slabs.wear = 0.3f;
    slabs.seed = 6;
    out.push_back({"pavement-slabs", slabs});
    return out;
}

std::string seedProject(const std::string& projectDir) {
    for (const Preset& pr : presets()) {
        std::string err;
        if (writeAssets(projectDir, pr.name, pr.params, &err).empty()) return err;
    }
    return "";
}

}  // namespace roadtex
