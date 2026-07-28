#include "treegen.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>

#include <stb_image_write.h>  // implementation lives in menubake.cpp

namespace treegen {

namespace {

constexpr float kPi = 3.14159265358979f;

struct V3 {
    float x = 0, y = 0, z = 0;
};
V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 operator*(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 cross(V3 a, V3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
float length(V3 a) { return std::sqrt(dot(a, a)); }
V3 normalize(V3 a) {
    const float l = length(a);
    return l > 1e-8f ? a * (1.0f / l) : V3{0, 1, 0};
}

// splitmix32 - the branch-seed mixer. Every branch derives its own stream
// from (parent seed, child index), so tweaking one tessellation slider does
// not reshuffle the whole tree: siblings and their subtrees keep their seeds.
uint32_t mix(uint32_t a, uint32_t b) {
    uint32_t z = a + 0x9E3779B9u * (b + 1);
    z = (z ^ (z >> 16)) * 0x85EBCA6Bu;
    z = (z ^ (z >> 13)) * 0xC2B2AE35u;
    return z ^ (z >> 16);
}

struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed ? seed : 1) {}
    uint32_t nextU() {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    }
    float next() { return (float)(nextU() >> 8) / 16777216.0f; }  // [0,1)
    float range(float a, float b) { return a + (b - a) * next(); }
    V3 unit() {
        // uniform direction from two draws
        const float z = range(-1.0f, 1.0f);
        const float a = range(0.0f, 2.0f * kPi);
        const float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
        return {r * std::cos(a), z, r * std::sin(a)};
    }
};

float lerpf(float a, float b, float t) { return a + (b - a) * t; }
float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }

// Below these a child branch is not worth its triangles. They are FRACTIONS OF
// HEIGHT on purpose: as absolute world sizes they made small trees a different
// shape rather than a smaller one - a 0.5-unit spruce lost whole whorls to the
// cutoff and came out a pole with a skirt, while the same parameters at 20
// units kept everything. A size control must not change what it is sizing.
constexpr float kMinLenFrac = 0.003f;   // ~0.02 units at the default height 7
constexpr float kMinRadFrac = 0.0006f;  // ~0.004 units there

struct Anchor {
    V3 pos, dir;
    // Length of branch this anchor speaks for. Foliage spreads over it, so
    // coverage follows the BRANCH rather than the tessellation: a low-poly
    // bough has few rings, and without this its needles clump at two or three
    // spots with bare tube between them.
    float span = 0;
};

struct Builder {
    const Params& p;
    Mesh& out;
    std::vector<Anchor> anchors;  // leaf spawn sites on the outer levels
    bool any = false;

    void grow(V3 v) {
        if (!any) {
            any = true;
            out.min[0] = out.max[0] = v.x;
            out.min[1] = out.max[1] = v.y;
            out.min[2] = out.max[2] = v.z;
            return;
        }
        out.min[0] = std::min(out.min[0], v.x);
        out.min[1] = std::min(out.min[1], v.y);
        out.min[2] = std::min(out.min[2], v.z);
        out.max[0] = std::max(out.max[0], v.x);
        out.max[1] = std::max(out.max[1], v.y);
        out.max[2] = std::max(out.max[2], v.z);
    }
    void vert(std::vector<float>& dst, V3 pos, V3 n, float u, float v) {
        grow(pos);
        dst.insert(dst.end(), {pos.x, pos.y, pos.z, n.x, n.y, n.z, u, v});
    }

    // One tube ring: seam-duplicated column ring (sides + 1 vertices worth of
    // parameters, generated on the fly by the segment emitters below).
    struct Ring {
        V3 center, dir, ax, ay;
        float radius = 0, v = 0;  // v: bark texture coordinate along the branch
    };

    V3 ringPoint(const Ring& r, int j, int sides) const {
        const float a = 2.0f * kPi * (float)j / (float)sides;
        return r.center + (r.ax * std::cos(a) + r.ay * std::sin(a)) * r.radius;
    }
    V3 ringNormal(const Ring& r, int j, int sides) const {
        const float a = 2.0f * kPi * (float)j / (float)sides;
        return r.ax * std::cos(a) + r.ay * std::sin(a);
    }

    void tubeSegment(const Ring& r0, const Ring& r1, int sides) {
        for (int j = 0; j < sides; ++j) {
            const float u0 = (float)j / (float)sides;
            const float u1 = (float)(j + 1) / (float)sides;
            const V3 a = ringPoint(r0, j, sides), na = ringNormal(r0, j, sides);
            const V3 b = ringPoint(r0, j + 1, sides), nb = ringNormal(r0, j + 1, sides);
            const V3 c = ringPoint(r1, j + 1, sides), nc = ringNormal(r1, j + 1, sides);
            const V3 d = ringPoint(r1, j, sides), nd = ringNormal(r1, j, sides);
            // outward winding (see the frame handedness: ax x ay = dir)
            vert(out.bark, a, na, u0, r0.v);
            vert(out.bark, b, nb, u1, r0.v);
            vert(out.bark, c, nc, u1, r1.v);
            vert(out.bark, a, na, u0, r0.v);
            vert(out.bark, c, nc, u1, r1.v);
            vert(out.bark, d, nd, u0, r1.v);
        }
    }
    void tipCap(const Ring& last, V3 tip, float tipV, int sides) {
        for (int j = 0; j < sides; ++j) {
            const float u0 = (float)j / (float)sides;
            const float u1 = (float)(j + 1) / (float)sides;
            const V3 a = ringPoint(last, j, sides), na = ringNormal(last, j, sides);
            const V3 b = ringPoint(last, j + 1, sides),
                     nb = ringNormal(last, j + 1, sides);
            vert(out.bark, a, na, u0, last.v);
            vert(out.bark, b, nb, u1, last.v);
            vert(out.bark, tip, last.dir, (u0 + u1) * 0.5f, tipV);
        }
    }

    void branch(V3 origin, V3 dir, float len, float baseRadius, int level,
                uint32_t seed) {
        const int maxLevel = std::max(1, p.levels) - 1;
        const float levelT = maxLevel > 0 ? (float)level / (float)maxLevel : 0.0f;
        const int sides =
            std::max(3, (int)std::lround(lerpf((float)p.sides, (float)p.sidesMin, levelT)));
        const int ringCount =
            std::max(1, (int)std::lround(lerpf((float)p.rings, (float)p.ringsMin, levelT)));

        Rng rng(mix(seed, 0xF00Du));
        const float stepLen = len / (float)ringCount;
        // v advances so bark texels stay roughly square around the base girth.
        // The guard is only against a degenerate radius - make it absolute and
        // the bark tiling stops tracking the tree's size on small trees.
        const float vStep = stepLen / std::max(1e-5f, 2.0f * kPi * baseRadius);

        // spine with parallel-transported frames
        std::vector<Ring> spine;
        spine.reserve(ringCount + 1);
        V3 ax = normalize(cross(dir, std::fabs(dir.y) < 0.95f ? V3{0, 1, 0}
                                                              : V3{1, 0, 0}));
        V3 ay = cross(dir, ax);  // ax x ay = dir (outward winding relies on it)
        V3 pos = origin;
        V3 d = dir;
        float v = 0.0f;
        for (int i = 0; i <= ringCount; ++i) {
            const float t = (float)i / (float)ringCount;
            float radius = baseRadius * lerpf(1.0f, p.taper, t);
            if (level == 0)  // root flare, strongest at the very base
                radius *= 1.0f + (p.flare - 1.0f) * std::pow(1.0f - t, 3.0f);
            spine.push_back({pos, d, ax, ay, radius, v});
            if (i == ringCount) break;
            // wander + phototropism, then re-transport the frame
            V3 nd = normalize(d + rng.unit() * (p.gnarliness * 0.55f) +
                              V3{0, p.sweep / (float)ringCount * 2.5f, 0});
            ax = normalize(ax - nd * dot(ax, nd));
            ay = cross(nd, ax);
            d = nd;
            pos = pos + d * stepLen;
            v += vStep;
        }

        for (int i = 0; i < ringCount; ++i) tubeSegment(spine[i], spine[i + 1], sides);
        const Ring& last = spine.back();
        const float tipLen = stepLen * 0.7f;
        tipCap(last, last.center + last.dir * tipLen, last.v + vStep * 0.7f, sides);

        // leaf anchors on the outer generations (and on a trunk-only tree).
        // A broadleaf clusters its foliage toward the branch tips; a conifer
        // needles the whole bough, so the anchor window starts much earlier.
        const bool leafy = level >= std::max(0, p.levels - std::max(1, p.leafLevels));
        // A conifer needles its bough from the trunk out; a broadleaf clusters
        // toward the tips.
        const float anchorFrom = p.crown == 1 ? 0.0f : 0.45f;
        if (leafy) {
            for (int i = 0; i <= ringCount; ++i) {
                const float t = (float)i / (float)ringCount;
                if (t >= anchorFrom)
                    anchors.push_back({spine[i].center, spine[i].dir, stepLen});
            }
            // half-way up the tip cone, not at its point: foliage spreads
            // along its span, and an anchor sitting at the very end throws
            // cards past the apex where they read as detached blobs
            anchors.push_back(
                {last.center + last.dir * (tipLen * 0.5f), last.dir, tipLen});
        } else if (p.crown == 1 && level == 0) {
            // The conifer's leader. Foliage is shared out per ANCHOR, so
            // hanging these off the trunk's rings starves the apex: the trunk
            // has a handful of rings over its whole length and the top of the
            // tree would get two of them against ~200 down in the whorls.
            // Sample the leader at its own fixed rate instead, starting below
            // the highest whorl (~0.93 of the trunk) so the two meet.
            const float from = 0.78f;
            const int kLeader = 9;
            for (int k = 0; k < kLeader; ++k) {
                const float t = from + (1.0f - from) * (float)k / (float)(kLeader - 1);
                const float f = t * (float)ringCount;
                const int i0 = std::min((int)f, ringCount - 1);
                const float fr = f - (float)i0;
                const V3 pos = spine[i0].center +
                               (spine[i0 + 1].center - spine[i0].center) * fr;
                anchors.push_back(
                    {pos, spine[i0].dir, (1.0f - from) * len / (float)kLeader});
            }
            // half-way up the tip cone, not at its point: foliage spreads
            // along its span, and an anchor sitting at the very end throws
            // cards past the apex where they read as detached blobs
            anchors.push_back(
                {last.center + last.dir * (tipLen * 0.5f), last.dir, tipLen});
        }

        // children
        if (level + 1 >= p.levels) return;
        if (p.crown == 1 && level == 0) {
            conicalWhorls(spine, ringCount, len, seed);
            return;
        }
        const int count = clampi(p.children[std::min(level, 2)], 0, 16);
        for (int i = 0; i < count; ++i) {
            Rng crng(mix(seed, 0xC0FFEEu + (uint32_t)i));
            float tt = p.spawnStart +
                       (0.92f - p.spawnStart) *
                           (((float)i + 0.25f + 0.5f * crng.next()) / (float)count);
            tt = clampf(tt, 0.05f, 0.92f);
            const int at = std::min((int)std::lround(tt * ringCount), ringCount);
            const Ring& s = spine[at];
            // golden-angle azimuth spread with jitter
            const float az = (float)i * 2.39996f + crng.range(-0.45f, 0.45f);
            const V3 radial = s.ax * std::cos(az) + s.ay * std::sin(az);
            const float ang =
                (p.branchAngle + crng.range(-p.angleJitter, p.angleJitter)) *
                kPi / 180.0f;
            const V3 cdir = normalize(s.dir * std::cos(ang) + radial * std::sin(ang));
            const float clen = len * p.lengthRatio * crng.range(0.85f, 1.15f) *
                               (1.0f - p.lengthTaper * tt);
            const float crad =
                std::min(s.radius * 0.85f, s.radius * p.radiusRatio);
            if (clen < p.height * kMinLenFrac || crad < p.height * kMinRadFrac)
                continue;
            branch(s.center + cdir * (s.radius * 0.25f), cdir, clen, crad,
                   level + 1, mix(seed, 0xB0B0u + (uint32_t)i));
        }
    }

    // Conifer trunk: rings ("whorls") of boughs instead of the spiral spread.
    // Two things make the silhouette, and neither is expressible as a ratio
    // per generation: length follows a PROFILE along the trunk (longest low,
    // vanishing at the apex - that is the cone), and the tilt sweeps from
    // drooping at the bottom to raised near the top, the way a real spruce
    // carries its branches.
    void conicalWhorls(const std::vector<Ring>& spine, int ringCount, float len,
                       uint32_t seed) {
        const int perWhorl = clampi(p.children[0], 1, 16);
        const int whorlCount = clampi(p.whorls, 1, 24);
        for (int w = 0; w < whorlCount; ++w) {
            const float tt =
                p.spawnStart + (0.97f - p.spawnStart) *
                                   (((float)w + 0.5f) / (float)whorlCount);
            const int at = std::min((int)std::lround(tt * ringCount), ringCount);
            const Ring& s = spine[at];
            // successive whorls are offset by the golden angle so the boughs
            // never stack into visible vertical columns
            const float whorlAz = (float)w * 2.39996f;
            for (int i = 0; i < perWhorl; ++i) {
                Rng crng(mix(seed, 0x5E1Fu + (uint32_t)(w * 32 + i)));
                const float az = whorlAz + 2.0f * kPi * (float)i / (float)perWhorl +
                                 crng.range(-0.16f, 0.16f);
                const V3 radial = s.ax * std::cos(az) + s.ay * std::sin(az);
                // > 90 deg = below horizontal (the drooping lower boughs)
                const float ang =
                    clampf(p.branchAngle * (1.18f - 0.62f * tt) +
                               crng.range(-p.angleJitter, p.angleJitter),
                           8.0f, 118.0f) *
                    kPi / 180.0f;
                const V3 cdir = normalize(s.dir * std::cos(ang) + radial * std::sin(ang));
                // the cone profile; a gentler exponent than linear keeps the
                // upper whorls from collapsing into stubs
                const float clen = len * p.lengthRatio *
                                   std::pow(std::max(0.0f, 1.0f - tt), 0.70f) *
                                   crng.range(0.85f, 1.15f);
                const float crad = std::min(s.radius * 0.8f, s.radius * p.radiusRatio);
                if (clen < p.height * kMinLenFrac || crad < p.height * kMinRadFrac)
                    continue;
                branch(s.center + cdir * (s.radius * 0.25f), cdir, clen, crad, 1,
                       mix(seed, 0xC0D1u + (uint32_t)(w * 32 + i)));
            }
        }
    }

    static int clampi(int v, int a, int b) { return v < a ? a : (v > b ? b : v); }

    void leaves() {
        if (p.leafCount <= 0 || anchors.empty()) return;
        // A needle sprig is a tuft growing OUT of the twig it sits on, so its
        // card wants to lie along the branch and spin around it. Broadleaf
        // cards stay free-floating - a cluster reads better scattered.
        const bool sprig = p.leafStyle == 1 || p.crown == 1;
        const float size = p.leafSize * p.height;
        for (int k = 0; k < p.leafCount; ++k) {
            Rng rng(mix(p.seed, 0x1EAFu + (uint32_t)k));
            const Anchor& a = anchors[(size_t)k % anchors.size()];
            const float w = size * rng.range(0.8f, 1.2f);
            const float h = w * p.leafAspect;
            V3 c, n, ua;
            if (sprig) {
                // frame around the twig: the card's width axis IS the branch
                // direction, its normal a random radial around it
                ua = normalize(a.dir);
                const V3 t1 = normalize(cross(ua, std::fabs(ua.y) < 0.95f
                                                      ? V3{0, 1, 0}
                                                      : V3{1, 0, 0}));
                const V3 t2 = cross(ua, t1);
                const float az = rng.range(0.0f, 2.0f * kPi);
                n = normalize(t1 * std::cos(az) + t2 * std::sin(az));
                // spread over the branch length this anchor owns, not over the
                // card size - that is what keeps a sparse bough evenly needled
                c = a.pos + ua * (rng.range(-0.5f, 0.5f) * std::max(a.span, w * 0.6f)) +
                    n * (w * rng.range(0.05f, 0.30f));
            } else {
                c = a.pos + rng.unit() * (size * 0.35f) +
                    a.dir * rng.range(0.0f, size * 0.4f);
                // face outward-ish with an upward bias so canopies read from above
                n = normalize(rng.unit() + V3{0, 0.35f, 0});
                ua = normalize(cross(n, rng.unit()));
            }
            const V3 ub = cross(n, ua);
            const V3 A = c - ua * (w * 0.5f) - ub * (h * 0.5f);
            const V3 B = c + ua * (w * 0.5f) - ub * (h * 0.5f);
            const V3 C = c + ua * (w * 0.5f) + ub * (h * 0.5f);
            const V3 D = c - ua * (w * 0.5f) + ub * (h * 0.5f);
            vert(out.leaves, A, n, 0, 1);
            vert(out.leaves, B, n, 1, 1);
            vert(out.leaves, C, n, 1, 0);
            vert(out.leaves, A, n, 0, 1);
            vert(out.leaves, C, n, 1, 0);
            vert(out.leaves, D, n, 0, 0);
        }
    }
};

// ---------------------------------------------------------------- textures

// Periodic (tileable) value noise on a wrapped lattice.
float vnoiseT(float x, float y, int px, int py, uint32_t seed) {
    const int ix = (int)std::floor(x), iy = (int)std::floor(y);
    const float fx = x - (float)ix, fy = y - (float)iy;
    auto lat = [&](int dx, int dy) {
        const int wx = ((ix + dx) % px + px) % px;
        const int wy = ((iy + dy) % py + py) % py;
        return (float)(mix(seed, (uint32_t)(wx * 73856093 ^ wy * 19349663)) >> 8) /
               16777216.0f;
    };
    const float sx = fx * fx * (3.0f - 2.0f * fx);
    const float sy = fy * fy * (3.0f - 2.0f * fy);
    const float a = lerpf(lat(0, 0), lat(1, 0), sx);
    const float b = lerpf(lat(0, 1), lat(1, 1), sx);
    return lerpf(a, b, sy);
}

float fbmT(float x, float y, int octaves, int px, int py, uint32_t seed) {
    float sum = 0, amp = 0.5f;
    for (int o = 0; o < octaves; ++o) {
        sum += amp * vnoiseT(x, y, px, py, seed + (uint32_t)o * 101);
        x *= 2, y *= 2, px *= 2, py *= 2;
        amp *= 0.5f;
    }
    return sum / (1.0f - std::pow(0.5f, (float)octaves));
}

void putPx(Image& img, int x, int y, const float rgb[3], unsigned char a) {
    unsigned char* p = &img.rgba[((size_t)y * img.w + x) * 4];
    p[0] = (unsigned char)clampf(rgb[0] * 255.0f + 0.5f, 0.0f, 255.0f);
    p[1] = (unsigned char)clampf(rgb[1] * 255.0f + 0.5f, 0.0f, 255.0f);
    p[2] = (unsigned char)clampf(rgb[2] * 255.0f + 0.5f, 0.0f, 255.0f);
    p[3] = a;
}

// Opaque colors bleed into the transparent margin (alpha stays 0) so bilinear
// sampling of the cutout edge never mixes toward black.
void dilate(Image& img, int passes) {
    const int w = img.w, h = img.h;
    for (int pass = 0; pass < passes; ++pass) {
        std::vector<unsigned char> src = img.rgba;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                unsigned char* dst = &img.rgba[((size_t)y * w + x) * 4];
                if (src[((size_t)y * w + x) * 4 + 3] != 0) continue;
                int r = 0, g = 0, b = 0, n = 0;
                const int nb[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
                for (const auto& d : nb) {
                    const int nx = x + d[0], ny = y + d[1];
                    if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                    const unsigned char* q = &src[((size_t)ny * w + nx) * 4];
                    if (q[3] == 0 && !(q[0] | q[1] | q[2])) continue;
                    // opaque neighbors or already-dilated margin pixels count
                    if (q[3] == 0 && pass == 0) continue;
                    r += q[0], g += q[1], b += q[2];
                    ++n;
                }
                if (n) {
                    dst[0] = (unsigned char)(r / n);
                    dst[1] = (unsigned char)(g / n);
                    dst[2] = (unsigned char)(b / n);
                }
            }
    }
}

}  // namespace

Mesh generate(const Params& params) {
    Params p = params;  // clamped working copy
    p.levels = Builder::clampi(params.levels, 1, 4);
    p.sides = Builder::clampi(params.sides, 3, 12);
    p.sidesMin = Builder::clampi(params.sidesMin, 3, p.sides);
    p.rings = Builder::clampi(params.rings, 1, 16);
    p.ringsMin = Builder::clampi(params.ringsMin, 1, p.rings);
    p.leafCount = Builder::clampi(params.leafCount, 0, 600);
    p.crown = Builder::clampi(params.crown, 0, 1);
    p.whorls = Builder::clampi(params.whorls, 1, 24);
    p.height = std::max(0.2f, params.height);
    Mesh mesh;
    Builder b{p, mesh};
    // radius (and the leaf card, in leaves()) derive from height, so dragging
    // Height scales the tree instead of thinning it
    b.branch({0, 0, 0}, {0, 1, 0}, p.height,
             std::max(0.01f, p.thickness) * p.height, 0, p.seed);
    b.leaves();
    return mesh;
}

Image bakeBarkTexture(const Params& p, int size) {
    Image img;
    img.w = img.h = size;
    img.rgba.assign((size_t)size * size * 4, 0);
    const uint32_t seed = mix(p.seed, 0xBA24u);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            const float nx = (float)x / (float)size, ny = (float)y / (float)size;
            float val = 0.5f;
            float crack = 1.0f;
            if (p.barkStyle == 1) {
                // birch: pale base, dark horizontal lenticel patches
                val = 0.8f + 0.2f * fbmT(nx * 4, ny * 4, 3, 4, 4, seed);
                const float len = fbmT(nx * 2, ny * 16, 2, 2, 16, seed + 7);
                if (len > 0.60f) val = -0.15f + 0.25f * fbmT(nx * 9, ny * 9, 2, 9, 9, seed + 9);
            } else if (p.barkStyle == 2) {
                // plates: quantized fbm islands split by dark cracks
                const float n = fbmT(nx * 6, ny * 6, 3, 6, 6, seed);
                const float cell = n * 4.0f;
                val = 0.35f + 0.65f * (std::floor(cell) / 4.0f) +
                      0.15f * fbmT(nx * 12, ny * 12, 2, 12, 12, seed + 3);
                const float e = cell - std::floor(cell);
                if (e < 0.10f || e > 0.90f) crack = 0.45f;
            } else {
                // rough bark: vertically stretched ridges with crevices
                const float ridge = fbmT(nx * 8, ny * 2.0f, 4, 8, 2, seed);
                const float fine = fbmT(nx * 20, ny * 5, 3, 20, 5, seed + 5);
                val = 0.30f + 0.55f * ridge + 0.30f * fine;
                if (fine < 0.34f) crack = 0.55f;
            }
            val = clampf(val, 0.0f, 1.0f);
            float rgb[3];
            for (int c = 0; c < 3; ++c)
                rgb[c] = lerpf(p.barkColor2[c], p.barkColor[c], val) * crack;
            putPx(img, x, y, rgb, 255);
        }
    return img;
}

Image bakeLeafTexture(const Params& p, int size) {
    Image img;
    img.w = img.h = size;
    img.rgba.assign((size_t)size * size * 4, 0);
    const uint32_t seed = mix(p.seed, 0x1EAFu);

    // one leaf blade in local coords: s along the axis [0,1], q across
    auto blade = [&](float s, float q, float halfWidth, float tone,
                     float rgb[3]) -> bool {
        if (s < 0.0f || s > 1.0f) return false;
        const float w = halfWidth * std::pow(std::sin(kPi * std::pow(s, 0.9f)),
                                             0.8f);
        if (std::fabs(q) > w || w < 1e-4f) return false;
        float t = clampf(tone + 0.35f * (1.0f - std::fabs(q) / w), 0.0f, 1.0f);
        if (std::fabs(q) < w * 0.10f && s > 0.08f) t *= 0.72f;  // midrib
        const float vein = (s * 7.0f - std::fabs(q) * 5.0f) -
                           std::floor(s * 7.0f - std::fabs(q) * 5.0f);
        if (vein < 0.10f && std::fabs(q) < w * 0.9f) t *= 0.86f;  // side veins
        for (int c = 0; c < 3; ++c) rgb[c] = lerpf(p.leafColor2[c], p.leafColor[c], t);
        return true;
    };

    if (p.leafStyle == 1) {
        // needle sprig: thin strokes radiating from the card center
        struct Needle {
            float ox, oy, dx, dy, len, tone;
        };
        std::vector<Needle> needles;
        Rng rng(seed);
        for (int i = 0; i < 46; ++i) {
            const float a = rng.range(0.0f, 2.0f * kPi);
            const float r0 = rng.range(0.0f, 0.10f);
            Needle n;
            n.ox = 0.5f + std::cos(a) * r0;
            n.oy = 0.5f + std::sin(a) * r0;
            n.dx = std::cos(a), n.dy = std::sin(a);
            n.len = rng.range(0.28f, 0.44f);
            n.tone = rng.next();
            needles.push_back(n);
        }
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x) {
                const float px = ((float)x + 0.5f) / (float)size;
                const float py = ((float)y + 0.5f) / (float)size;
                for (const Needle& n : needles) {
                    const float rx = px - n.ox, ry = py - n.oy;
                    const float t = clampf(rx * n.dx + ry * n.dy, 0.0f, n.len);
                    const float qx = rx - n.dx * t, qy = ry - n.dy * t;
                    const float d = std::sqrt(qx * qx + qy * qy);
                    if (d > 0.011f * (1.0f - 0.5f * t / n.len)) continue;
                    float rgb[3];
                    const float tone = clampf(0.25f + 0.6f * (t / n.len) + 0.3f * n.tone,
                                              0.0f, 1.0f);
                    for (int c = 0; c < 3; ++c)
                        rgb[c] = lerpf(p.leafColor2[c], p.leafColor[c], tone);
                    putPx(img, x, y, rgb, 255);
                    break;
                }
            }
    } else if (p.leafStyle == 2) {
        // one big blade filling the card, stem at the bottom
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x) {
                const float px = ((float)x + 0.5f) / (float)size;
                const float py = ((float)y + 0.5f) / (float)size;
                const float s = (0.93f - py) / 0.80f;  // axis bottom->top
                float rgb[3];
                if (blade(s, px - 0.5f, 0.36f, 0.35f, rgb)) {
                    putPx(img, x, y, rgb, 255);
                } else if (s < 0.02f && s > -0.14f && std::fabs(px - 0.5f) < 0.016f) {
                    const float stem[3] = {p.leafColor2[0] * 0.8f,
                                           p.leafColor2[1] * 0.7f,
                                           p.leafColor2[2] * 0.6f};
                    putPx(img, x, y, stem, 255);
                }
            }
    } else {
        // broadleaf cluster: a handful of rotated blades around the center
        struct Blade {
            float cx, cy, ca, sa, len, width, tone;
        };
        std::vector<Blade> blades;
        Rng rng(seed);
        for (int i = 0; i < 8; ++i) {
            const float ang = rng.range(0.0f, 2.0f * kPi);
            const float r = rng.range(0.05f, 0.20f);
            Blade b;
            b.cx = 0.5f + std::cos(ang) * r;
            b.cy = 0.5f + std::sin(ang) * r;
            // blades point away from the cluster center
            const float rot = ang + rng.range(-0.5f, 0.5f);
            b.ca = std::cos(rot), b.sa = std::sin(rot);
            b.len = rng.range(0.26f, 0.34f);
            b.width = b.len * rng.range(0.42f, 0.55f);
            b.tone = rng.range(0.15f, 0.55f);
            blades.push_back(b);
        }
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x) {
                const float px = ((float)x + 0.5f) / (float)size;
                const float py = ((float)y + 0.5f) / (float)size;
                for (const Blade& b : blades) {
                    const float rx = px - b.cx, ry = py - b.cy;
                    const float s = (rx * b.ca + ry * b.sa) / b.len;
                    const float q = (-rx * b.sa + ry * b.ca) / b.len;
                    float rgb[3];
                    if (blade(s, q, b.width / b.len * 0.5f, b.tone, rgb)) {
                        putPx(img, x, y, rgb, 255);
                        break;
                    }
                }
            }
    }
    dilate(img, 3);
    return img;
}

bool writeAssets(const std::string& projectDir, const std::string& name,
                 const Params& p, const Mesh& mesh, const Image& bark,
                 const Image& leaf, std::string* outObjRel,
                 std::string* outError) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::path(projectDir) / "res" / "models" / "trees";
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) {
        if (outError) *outError = "Could not create " + dir.generic_string();
        return false;
    }

    const std::string barkPng = name + "-bark.png";
    const std::string leafPng = name + "-leaf.png";
    const bool hasLeaves = !mesh.leaves.empty();

    if (!stbi_write_png((dir / barkPng).string().c_str(), bark.w, bark.h, 4,
                        bark.rgba.data(), bark.w * 4)) {
        if (outError) *outError = "Could not write " + barkPng;
        return false;
    }
    if (hasLeaves &&
        !stbi_write_png((dir / leafPng).string().c_str(), leaf.w, leaf.h, 4,
                        leaf.rgba.data(), leaf.w * 4)) {
        if (outError) *outError = "Could not write " + leafPng;
        return false;
    }

    {
        std::ofstream mtl(dir / (name + ".mtl"));
        if (!mtl) {
            if (outError) *outError = "Could not write " + name + ".mtl";
            return false;
        }
        mtl << "# Generated by TyraX Tree Generator (seed " << p.seed << ")\n";
        mtl << "newmtl bark\nKd 1.0 1.0 1.0\nmap_Kd " << barkPng << "\n";
        if (hasLeaves)
            mtl << "\nnewmtl leaf\nKd 1.0 1.0 1.0\nmap_Kd " << leafPng << "\n";
    }

    std::ofstream obj(dir / (name + ".obj"));
    if (!obj) {
        if (outError) *outError = "Could not write " + name + ".obj";
        return false;
    }
    obj << "# Generated by TyraX Tree Generator (seed " << p.seed << ", "
        << mesh.triangles() << " triangles)\n";
    obj << "mtllib " << name << ".mtl\n";

    // positions/uvs dedup by exact bits - rings share most corners, so this
    // shrinks the file about 4x without any welding heuristics
    std::map<std::array<uint32_t, 3>, int> vIndex;
    std::map<std::array<uint32_t, 2>, int> tIndex;
    std::vector<std::array<float, 3>> vList;
    std::vector<std::array<float, 2>> tList;
    auto keyOf3 = [](const float* f) {
        std::array<uint32_t, 3> k;
        std::memcpy(k.data(), f, 12);
        return k;
    };
    auto keyOf2 = [](const float* f) {
        std::array<uint32_t, 2> k;
        std::memcpy(k.data(), f, 8);
        return k;
    };
    auto faceCorner = [&](const float* vtx, std::string& line) {
        const auto vk = keyOf3(vtx);
        auto vi = vIndex.find(vk);
        if (vi == vIndex.end()) {
            vList.push_back({vtx[0], vtx[1], vtx[2]});
            vi = vIndex.emplace(vk, (int)vList.size()).first;
        }
        const float uv[2] = {vtx[6], 1.0f - vtx[7]};  // back to OBJ v-space
        const auto tk = keyOf2(uv);
        auto ti = tIndex.find(tk);
        if (ti == tIndex.end()) {
            tList.push_back({uv[0], uv[1]});
            ti = tIndex.emplace(tk, (int)tList.size()).first;
        }
        char buf[48];
        std::snprintf(buf, sizeof(buf), " %d/%d", vi->second, ti->second);
        line += buf;
    };
    auto facesOf = [&](const std::vector<float>& tris, std::string& faces) {
        for (size_t i = 0; i + 23 < tris.size(); i += 24) {
            std::string line = "f";
            faceCorner(&tris[i], line);
            faceCorner(&tris[i + 8], line);
            faceCorner(&tris[i + 16], line);
            faces += line + "\n";
        }
    };
    std::string barkFaces, leafFaces;
    facesOf(mesh.bark, barkFaces);
    if (hasLeaves) facesOf(mesh.leaves, leafFaces);

    char buf[96];
    for (const auto& v : vList) {
        std::snprintf(buf, sizeof(buf), "v %.5f %.5f %.5f\n", v[0], v[1], v[2]);
        obj << buf;
    }
    for (const auto& t : tList) {
        std::snprintf(buf, sizeof(buf), "vt %.5f %.5f\n", t[0], t[1]);
        obj << buf;
    }
    obj << "usemtl bark\n" << barkFaces;
    if (hasLeaves) obj << "usemtl leaf\n" << leafFaces;
    if (!obj.good()) {
        if (outError) *outError = "Write failed on " + name + ".obj";
        return false;
    }
    obj.close();

    if (outObjRel) *outObjRel = "res/models/trees/" + name + ".obj";
    return true;
}

const std::vector<Preset>& presets() {
    static const std::vector<Preset> list = [] {
        std::vector<Preset> v;
        {
            Preset pr{"Oak", {}};
            v.push_back(pr);  // the Params defaults ARE the oak
        }
        {
            Preset pr{"Birch", {}};
            Params& p = pr.params;
            p.height = 8.0f, p.thickness = 0.025f, p.flare = 1.2f;
            p.taper = 0.5f, p.gnarliness = 0.07f, p.sweep = 0.20f;
            p.levels = 3, p.children[0] = 2, p.children[1] = 3;
            p.branchAngle = 35.0f, p.lengthRatio = 0.55f, p.lengthTaper = 0.35f;
            p.sides = 5, p.leafCount = 110, p.leafSize = 0.062f;
            p.barkStyle = 1;
            const float c1[3] = {0.88f, 0.87f, 0.80f}, c2[3] = {0.16f, 0.15f, 0.13f};
            std::copy(c1, c1 + 3, p.barkColor);
            std::copy(c2, c2 + 3, p.barkColor2);
            const float l1[3] = {0.45f, 0.62f, 0.22f}, l2[3] = {0.22f, 0.40f, 0.12f};
            std::copy(l1, l1 + 3, p.leafColor);
            std::copy(l2, l2 + 3, p.leafColor2);
            v.push_back(pr);
        }
        {
            Preset pr{"Spruce", {}};
            Params& p = pr.params;
            // The conifer habit: an unbroken leader (hard taper = a spike),
            // ten whorls of four boughs, needles down the whole bough.
            p.height = 9.0f, p.thickness = 0.030f, p.flare = 1.35f;
            p.taper = 0.16f, p.gnarliness = 0.02f, p.sweep = 0.0f;
            p.crown = 1, p.whorls = 10;
            p.levels = 2, p.children[0] = 5;
            p.branchAngle = 72.0f, p.angleJitter = 7.0f;
            p.lengthRatio = 0.30f, p.radiusRatio = 0.35f;
            p.spawnStart = 0.12f;
            p.sides = 6, p.sidesMin = 3, p.rings = 7, p.ringsMin = 2;
            p.leafCount = 300, p.leafSize = 0.072f, p.leafAspect = 1.0f;
            p.leafStyle = 1, p.leafLevels = 1;
            p.barkStyle = 2;
            const float c1[3] = {0.38f, 0.26f, 0.18f}, c2[3] = {0.20f, 0.13f, 0.09f};
            std::copy(c1, c1 + 3, p.barkColor);
            std::copy(c2, c2 + 3, p.barkColor2);
            const float l1[3] = {0.20f, 0.38f, 0.20f}, l2[3] = {0.08f, 0.22f, 0.12f};
            std::copy(l1, l1 + 3, p.leafColor);
            std::copy(l2, l2 + 3, p.leafColor2);
            v.push_back(pr);
        }
        {
            Preset pr{"Poplar", {}};
            Params& p = pr.params;
            p.height = 8.5f, p.thickness = 0.028f, p.flare = 1.3f;
            p.taper = 0.45f, p.gnarliness = 0.06f, p.sweep = 0.30f;
            p.levels = 3, p.children[0] = 4, p.children[1] = 2;
            p.branchAngle = 25.0f, p.lengthRatio = 0.45f, p.lengthTaper = 0.45f;
            p.spawnStart = 0.15f, p.leafCount = 140, p.leafSize = 0.053f;
            v.push_back(pr);
        }
        {
            Preset pr{"Dead tree", {}};
            Params& p = pr.params;
            p.height = 6.0f, p.thickness = 0.043f, p.flare = 1.7f;
            p.gnarliness = 0.30f, p.sweep = 0.0f;
            p.levels = 4, p.children[0] = 3, p.children[1] = 2, p.children[2] = 2;
            p.branchAngle = 50.0f, p.angleJitter = 18.0f;
            p.leafCount = 0;
            const float c1[3] = {0.42f, 0.40f, 0.36f}, c2[3] = {0.18f, 0.17f, 0.15f};
            std::copy(c1, c1 + 3, p.barkColor);
            std::copy(c2, c2 + 3, p.barkColor2);
            v.push_back(pr);
        }
        {
            Preset pr{"Bush", {}};
            Params& p = pr.params;
            p.height = 1.6f, p.thickness = 0.062f, p.flare = 1.1f;
            p.levels = 3, p.children[0] = 4, p.children[1] = 3;
            p.branchAngle = 55.0f, p.spawnStart = 0.10f;
            p.lengthRatio = 0.70f, p.lengthTaper = 0.20f;
            p.sides = 4, p.rings = 3;
            p.leafCount = 150, p.leafSize = 0.24f;
            v.push_back(pr);
        }
        return v;
    }();
    return list;
}

}  // namespace treegen
