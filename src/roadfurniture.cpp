// Street furniture (docs/roads.md "Street furniture"): lamps, trees, bollards,
// stop / give-way signs and traffic lights generated from the roads at build.
// See roadfurniture.hpp for the contract; this file is placement, the built-in
// models, the .obj instancing and the --vehicle-check block.
#include "roadfurniture.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <sstream>
#include <utility>

#include "json.hpp"
#include "objparser.hpp"

#include <stb_image.h>

namespace roadfurn {
namespace {

// --- deterministic hashing (roaddetail's: counter-based, never a running RNG)

uint64_t mix64(uint64_t x) {
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

uint64_t hashString(const std::string& s) {
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : s) h = (h ^ c) * 1099511628211ull;
    return h;
}

float hash01(uint64_t key, uint64_t a, uint64_t b) {
    const uint64_t h = mix64(key ^ mix64(a * 0x100000001B3ull + mix64(b)));
    return (float)(h >> 40) / 16777216.0f;
}

constexpr float kPi = 3.14159265f;

// --- the built-in models -------------------------------------------------------
//
// Box-and-prism geometry in metres, +Y up, +Z the facing (toward the road for
// a lamp, toward the approaching driver for a sign or a signal), base at y 0.
// Winding is counter-clockwise seen from outside, so the face normal points
// out and the shade is right; nothing on the console culls back faces anyway.

using Tris = std::vector<ModelTri>;
struct V3 {
    float x, y, z;
};

void tri(Tris& out, V3 a, V3 b, V3 c, const float* col, bool lit = true) {
    ModelTri t;
    const V3 p[3] = {a, b, c};
    for (int i = 0; i < 3; ++i) {
        t.p[i][0] = p[i].x, t.p[i][1] = p[i].y, t.p[i][2] = p[i].z;
        t.c[i][0] = col[0], t.c[i][1] = col[1], t.c[i][2] = col[2];
    }
    t.lit = lit;
    out.push_back(t);
}
void quad(Tris& out, V3 a, V3 b, V3 c, V3 d, const float* col, bool lit = true) {
    tri(out, a, b, c, col, lit);
    tri(out, a, c, d, col, lit);
}
// An axis-aligned box, all six faces.
void box(Tris& out, float cx, float cy, float cz, float hx, float hy, float hz,
         const float* col) {
    const float x0 = cx - hx, x1 = cx + hx, y0 = cy - hy, y1 = cy + hy, z0 = cz - hz,
                z1 = cz + hz;
    quad(out, {x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}, col);  // +z
    quad(out, {x1, y0, z0}, {x0, y0, z0}, {x0, y1, z0}, {x1, y1, z0}, col);  // -z
    quad(out, {x1, y0, z1}, {x1, y0, z0}, {x1, y1, z0}, {x1, y1, z1}, col);  // +x
    quad(out, {x0, y0, z0}, {x0, y0, z1}, {x0, y1, z1}, {x0, y1, z0}, col);  // -x
    quad(out, {x0, y1, z1}, {x1, y1, z1}, {x1, y1, z0}, {x0, y1, z0}, col);  // +y
    quad(out, {x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1}, col);  // -y
}
// A vertical prism about (cx, cz): `sides` walls from y0 to y1, radius r, an
// optional top cap. No bottom: it stands on the ground.
void prism(Tris& out, float cx, float cz, float y0, float y1, float r, int sides,
           const float* col, bool cap) {
    for (int k = 0; k < sides; ++k) {
        const float a0 = 2.0f * kPi * (float)k / (float)sides + kPi / (float)sides;
        const float a1 = 2.0f * kPi * (float)(k + 1) / (float)sides + kPi / (float)sides;
        const V3 b0{cx + r * std::sin(a0), y0, cz + r * std::cos(a0)};
        const V3 b1{cx + r * std::sin(a1), y0, cz + r * std::cos(a1)};
        const V3 t0{b0.x, y1, b0.z}, t1{b1.x, y1, b1.z};
        quad(out, b0, t0, t1, b1, col);
        if (cap && k > 0 && k + 1 < sides) {
            const float a = kPi / (float)sides;
            tri(out, {cx + r * std::sin(a), y1, cz + r * std::cos(a)}, t1, t0, col);
        }
    }
}
// A regular polygon facing +Z at depth z (fan from its first corner).
void plate(Tris& out, float cx, float cy, float z, float r, int sides, float rot,
           const float* col, bool lit, bool back) {
    std::vector<V3> ring;
    for (int k = 0; k < sides; ++k) {
        const float a = rot + 2.0f * kPi * (float)k / (float)sides;
        ring.push_back({cx + r * std::sin(a), cy + r * std::cos(a), z});
    }
    for (int k = 1; k + 1 < sides; ++k) {
        if (back)
            tri(out, ring[0], ring[(size_t)k + 1], ring[(size_t)k], col, lit);
        else
            tri(out, ring[0], ring[(size_t)k], ring[(size_t)k + 1], col, lit);
    }
}

const float kPoleGrey[3] = {0.30f, 0.32f, 0.32f};
const float kDark[3] = {0.12f, 0.12f, 0.13f};
const float kLens[3] = {1.0f, 0.93f, 0.72f};
const float kTrunk[3] = {0.38f, 0.27f, 0.17f};
const float kLeafLow[3] = {0.20f, 0.36f, 0.15f};
const float kLeafHigh[3] = {0.32f, 0.52f, 0.22f};
const float kBollardGrey[3] = {0.22f, 0.23f, 0.25f};
const float kWhite[3] = {0.92f, 0.92f, 0.90f};
const float kRed[3] = {0.80f, 0.10f, 0.10f};
const float kBackGrey[3] = {0.55f, 0.56f, 0.56f};

Tris makeLamp() {
    Tris t;
    prism(t, 0, 0, 0.0f, 0.5f, 0.16f, 6, kPoleGrey, true);   // base
    prism(t, 0, 0, 0.5f, 5.45f, 0.08f, 6, kPoleGrey, false); // pole
    box(t, 0.0f, 5.40f, 0.70f, 0.05f, 0.05f, 0.70f, kPoleGrey);  // arm over the road
    box(t, 0.0f, 5.32f, 1.38f, 0.18f, 0.08f, 0.32f, kDark);      // head
    // The lens under the head: unshaded, so it reads as a light at night.
    const float y = 5.235f;
    tri(t, {-0.15f, y, 1.10f}, {0.15f, y, 1.66f}, {0.15f, y, 1.10f}, kLens, false);
    tri(t, {-0.15f, y, 1.10f}, {-0.15f, y, 1.66f}, {0.15f, y, 1.66f}, kLens, false);
    return t;
}

Tris makeTree() {
    Tris t;
    prism(t, 0, 0, 0.0f, 2.3f, 0.14f, 6, kTrunk, false);
    // The crown: a six-sided bicone with a shoulder ring, darker underneath.
    const int n = 6;
    const V3 bottom{0.0f, 1.7f, 0.0f}, top{0.0f, 5.6f, 0.0f};
    std::vector<V3> low, high;
    for (int k = 0; k < n; ++k) {
        const float a = 2.0f * kPi * (float)k / (float)n;
        const float b = a + kPi / (float)n;
        low.push_back({1.55f * std::sin(a), 3.3f, 1.55f * std::cos(a)});
        high.push_back({1.15f * std::sin(b), 4.6f, 1.15f * std::cos(b)});
    }
    float mid[3];
    for (int i = 0; i < 3; ++i) mid[i] = 0.5f * (kLeafLow[i] + kLeafHigh[i]);
    for (int k = 0; k < n; ++k) {
        const int k1 = (k + 1) % n;
        tri(t, bottom, low[(size_t)k1], low[(size_t)k], kLeafLow);
        tri(t, low[(size_t)k], low[(size_t)k1], high[(size_t)k], mid);
        tri(t, low[(size_t)k1], high[(size_t)k1], high[(size_t)k], mid);
        tri(t, high[(size_t)k], high[(size_t)k1], top, kLeafHigh);
    }
    return t;
}

Tris makeBollard() {
    Tris t;
    prism(t, 0, 0, 0.0f, 0.74f, 0.11f, 6, kBollardGrey, false);
    prism(t, 0, 0, 0.74f, 0.92f, 0.11f, 6, kWhite, true);  // reflective top
    return t;
}

Tris makeSign(int signKind) {
    Tris t;
    prism(t, 0, 0, 0.0f, 2.35f, 0.04f, 4, kPoleGrey, false);
    const float cy = 2.0f;
    if (signKind == kSignStop) {
        plate(t, 0, cy, 0.05f, 0.40f, 8, kPi / 8.0f, kWhite, true, false);
        plate(t, 0, cy, 0.055f, 0.35f, 8, kPi / 8.0f, kRed, true, false);
        plate(t, 0, cy, 0.045f, 0.40f, 8, kPi / 8.0f, kBackGrey, true, true);
    } else {
        // Give way: a triangle point down, red rim round a white field.
        plate(t, 0, cy, 0.05f, 0.50f, 3, kPi, kRed, true, false);
        plate(t, 0, cy, 0.055f, 0.32f, 3, kPi, kWhite, true, false);
        plate(t, 0, cy, 0.045f, 0.50f, 3, kPi, kBackGrey, true, true);
    }
    return t;
}

Tris makeSignal(bool live) {
    Tris t;
    prism(t, 0, 0, 0.0f, 2.95f, 0.07f, 4, kPoleGrey, false);
    box(t, 0.0f, 3.40f, 0.0f, 0.17f, 0.46f, 0.11f, kDark);
    // Live signals (docs/traffic.md "Traffic lights"): the console draws the
    // lit lens over these, so the baked three are the UNLIT glass.
    const float lit[3][3] = {{0.95f, 0.12f, 0.08f}, {0.95f, 0.62f, 0.10f}, {0.15f, 0.85f, 0.30f}};
    const float dark[3][3] = {{0.22f, 0.05f, 0.04f}, {0.22f, 0.15f, 0.04f}, {0.04f, 0.18f, 0.08f}};
    const float (*lamps)[3] = live ? dark : lit;
    for (int k = 0; k < 3; ++k) {
        const float y = kSignalLensY[k];
        quad(t, {-0.10f, y - 0.10f, 0.115f}, {0.10f, y - 0.10f, 0.115f},
             {0.10f, y + 0.10f, 0.115f}, {-0.10f, y + 0.10f, 0.115f}, lamps[k], false);
    }
    return t;
}

// Per kind: the footprint used for placement, the collision radius and the
// collision height, all at scale 1.
struct KindSize {
    float place, solid, height;
};
const KindSize kSize[kKindCount] = {
    {0.40f, 0.12f, 5.5f},  // lamp
    {1.20f, 0.20f, 2.3f},  // tree (crown clearance; the trunk is solid)
    {0.20f, 0.12f, 0.92f}, // bollard
    {0.30f, 0.06f, 2.4f},  // sign
    {0.35f, 0.10f, 3.9f},  // signal
};

// --- .obj models --------------------------------------------------------------

struct Texture {
    int w = 0, h = 0;
    std::vector<unsigned char> rgba;
    bool ok() const { return w > 0 && h > 0; }
    void sample(float u, float v, float* out) const {
        u -= std::floor(u);
        v -= std::floor(v);
        const int x = std::clamp((int)(u * (float)w), 0, w - 1);
        const int y = std::clamp((int)(v * (float)h), 0, h - 1);
        const unsigned char* p = &rgba[((size_t)y * (size_t)w + (size_t)x) * 4];
        out[0] = p[0] / 255.0f, out[1] = p[1] / 255.0f, out[2] = p[2] / 255.0f;
    }
};

// An .obj as model triangles: every vertex coloured by Kd x the texel at its
// UV (the texture is sampled into the colour, never uploaded).
bool loadObjModel(const std::string& full, Tris& out, std::string* err) {
    objparser::Model m;
    if (!objparser::load(full, m)) {
        if (err) *err = "cannot read " + full;
        return false;
    }
    const std::filesystem::path dir = std::filesystem::path(full).parent_path();
    std::map<std::string, Texture> texCache;
    for (const objparser::Submesh& s : m.submeshes) {
        const Texture* tex = nullptr;
        if (!s.texture.empty()) {
            auto it = texCache.find(s.texture);
            if (it == texCache.end()) {
                Texture t;
                const std::string path = (dir / s.texture).string();
                int w = 0, h = 0, comp = 0;
                if (unsigned char* px = stbi_load(path.c_str(), &w, &h, &comp, 4)) {
                    t.w = w, t.h = h;
                    t.rgba.assign(px, px + (size_t)w * (size_t)h * 4);
                    stbi_image_free(px);
                }
                it = texCache.emplace(s.texture, std::move(t)).first;
            }
            if (it->second.ok()) tex = &it->second;
        }
        const bool emissive = s.ke[0] + s.ke[1] + s.ke[2] > 0.5f;
        for (size_t k = 0; k + 23 < s.verts.size(); k += 24) {
            ModelTri t;
            for (int i = 0; i < 3; ++i) {
                const float* v = &s.verts[k + (size_t)i * 8];
                t.p[i][0] = v[0], t.p[i][1] = v[1], t.p[i][2] = v[2];
                float c[3] = {1.0f, 1.0f, 1.0f};
                if (tex) tex->sample(v[6], v[7], c);
                for (int a = 0; a < 3; ++a) t.c[i][a] = c[a] * s.kd[a];
            }
            t.lit = !emissive;
            out.push_back(t);
        }
    }
    return !out.empty();
}

// --- geometry helpers ---------------------------------------------------------

// A road's centre line resampled by arc length (roaddetail's CentreLine).
class CentreLine {
public:
    explicit CentreLine(const std::vector<float>& pts) {
        const int n = (int)(pts.size() / 2);
        if (n < 2) return;
        const int steps = (n - 1) * 48;
        for (int k = 0; k <= steps; ++k) {
            float x, z;
            roadgen::splineAt(pts, (float)k / (float)steps, &x, &z);
            if (!xs_.empty()) {
                const float d = std::hypot(x - xs_.back(), z - zs_.back());
                if (d < 1e-5f) continue;
                arc_.push_back(arc_.back() + d);
            } else {
                arc_.push_back(0.0f);
            }
            xs_.push_back(x);
            zs_.push_back(z);
            mnx_ = std::min(mnx_, x), mxx_ = std::max(mxx_, x);
            mnz_ = std::min(mnz_, z), mxz_ = std::max(mxz_, z);
        }
    }
    float length() const { return arc_.empty() ? 0.0f : arc_.back(); }
    void at(float s, float* x, float* z, float* tx, float* tz) const {
        *x = *z = 0.0f, *tx = 1.0f, *tz = 0.0f;
        if (xs_.size() < 2) return;
        const size_t i = std::min(
            (size_t)(std::upper_bound(arc_.begin(), arc_.end(), s) - arc_.begin()),
            arc_.size() - 1);
        const size_t a = i == 0 ? 0 : i - 1, b = a + 1;
        const float span = arc_[b] - arc_[a];
        const float k = span > 0.0f ? std::clamp((s - arc_[a]) / span, 0.0f, 1.0f) : 0.0f;
        *x = xs_[a] + (xs_[b] - xs_[a]) * k;
        *z = zs_[a] + (zs_[b] - zs_[a]) * k;
        *tx = (xs_[b] - xs_[a]) / std::max(span, 1e-6f);
        *tz = (zs_[b] - zs_[a]) / std::max(span, 1e-6f);
    }
    // Distance from (x, z) to the line, or `cap` when the bounding box alone
    // proves it is farther than that.
    float distance(float x, float z, float cap) const {
        if (x < mnx_ - cap || x > mxx_ + cap || z < mnz_ - cap || z > mxz_ + cap) return cap;
        float best = cap * cap;
        for (size_t i = 0; i + 1 < xs_.size(); ++i) {
            const float ax = xs_[i], az = zs_[i];
            const float dx = xs_[i + 1] - ax, dz = zs_[i + 1] - az;
            const float l2 = dx * dx + dz * dz;
            float t = l2 > 0.0f ? ((x - ax) * dx + (z - az) * dz) / l2 : 0.0f;
            t = std::clamp(t, 0.0f, 1.0f);
            const float ex = ax + dx * t - x, ez = az + dz * t - z;
            best = std::min(best, ex * ex + ez * ez);
        }
        return std::sqrt(best);
    }

private:
    std::vector<float> xs_, zs_, arc_;
    float mnx_ = 1e30f, mxx_ = -1e30f, mnz_ = 1e30f, mxz_ = -1e30f;
};

float segDist(float px, float pz, float ax, float az, float bx, float bz) {
    const float dx = bx - ax, dz = bz - az;
    const float l2 = dx * dx + dz * dz;
    float t = l2 > 0.0f ? ((px - ax) * dx + (pz - az) * dz) / l2 : 0.0f;
    t = std::clamp(t, 0.0f, 1.0f);
    return std::hypot(ax + dx * t - px, az + dz * t - pz);
}

// Signed distance to a closed polygon (x0,z0,x1,z1,...): negative inside.
float polygonDistance(const std::vector<float>& ring, float x, float z) {
    const size_t n = ring.size() / 2;
    if (n < 3) return 1e30f;
    bool inside = false;
    float best = 1e30f;
    for (size_t i = 0, j = n - 1; i < n; j = i++) {
        const float xi = ring[i * 2], zi = ring[i * 2 + 1];
        const float xj = ring[j * 2], zj = ring[j * 2 + 1];
        if (((zi > z) != (zj > z)) && x < (xj - xi) * (z - zi) / (zj - zi) + xi) inside = !inside;
        best = std::min(best, segDist(x, z, xi, zi, xj, zj));
    }
    return inside ? -best : best;
}

// XZ distance from a point to a triangle (0 inside).
float triDistance(const roadgen::Vertex* t, float x, float z) {
    const float d0 = (t[1].x - t[0].x) * (z - t[0].z) - (t[1].z - t[0].z) * (x - t[0].x);
    const float d1 = (t[2].x - t[1].x) * (z - t[1].z) - (t[2].z - t[1].z) * (x - t[1].x);
    const float d2 = (t[0].x - t[2].x) * (z - t[2].z) - (t[0].z - t[2].z) * (x - t[2].x);
    if ((d0 >= 0 && d1 >= 0 && d2 >= 0) || (d0 <= 0 && d1 <= 0 && d2 <= 0)) return 0.0f;
    return std::min({segDist(x, z, t[0].x, t[0].z, t[1].x, t[1].z),
                     segDist(x, z, t[1].x, t[1].z, t[2].x, t[2].z),
                     segDist(x, z, t[2].x, t[2].z, t[0].x, t[0].z)});
}

// Triangles bucketed on a coarse XZ grid for "is anything within d" queries.
class TriGrid {
public:
    void build(const std::vector<roadgen::Vertex>& tris) {
        tris_ = &tris;
        for (size_t t = 0; t + 2 < tris.size(); t += 3) {
            float mnx = 1e30f, mxx = -1e30f, mnz = 1e30f, mxz = -1e30f;
            for (int k = 0; k < 3; ++k) {
                mnx = std::min(mnx, tris[t + k].x), mxx = std::max(mxx, tris[t + k].x);
                mnz = std::min(mnz, tris[t + k].z), mxz = std::max(mxz, tris[t + k].z);
            }
            for (int cx = cell(mnx); cx <= cell(mxx); ++cx)
                for (int cz = cell(mnz); cz <= cell(mxz); ++cz)
                    cells_[{cx, cz}].push_back(t);
        }
    }
    bool near(float x, float z, float d) const {
        if (!tris_) return false;
        for (int cx = cell(x - d); cx <= cell(x + d); ++cx)
            for (int cz = cell(z - d); cz <= cell(z + d); ++cz) {
                auto it = cells_.find({cx, cz});
                if (it == cells_.end()) continue;
                for (size_t t : it->second)
                    if (triDistance(&(*tris_)[t], x, z) < d) return true;
            }
        return false;
    }

private:
    static int cell(float v) { return (int)std::floor(v / 4.0f); }
    const std::vector<roadgen::Vertex>* tris_ = nullptr;
    std::map<std::pair<int, int>, std::vector<size_t>> cells_;
};

// Placed footprints, for the overlap test. Two instances keep their SOLID
// parts (pole, trunk) kClearance apart; two TALL ones (anything but a
// bollard) also keep their heads apart - a crown, a lamp head, a sign plate -
// so a bollard may stand under a tree, a lamp may not stand in one.
class Placed {
public:
    bool clear(float x, float z, float solid, float place, bool tall) const {
        const float reach = std::max(solid, place) + maxReach_ + kClearance;
        for (int cx = cell(x - reach); cx <= cell(x + reach); ++cx)
            for (int cz = cell(z - reach); cz <= cell(z + reach); ++cz) {
                auto it = cells_.find({cx, cz});
                if (it == cells_.end()) continue;
                for (const P& p : it->second) {
                    float need = p.solid + solid + kClearance;
                    if (tall && p.tall) need = std::max(need, p.place + place);
                    if (std::hypot(p.x - x, p.z - z) < need) return false;
                }
            }
        return true;
    }
    void add(float x, float z, float solid, float place, bool tall) {
        cells_[{cell(x), cell(z)}].push_back({x, z, solid, place, tall});
        maxReach_ = std::max(maxReach_, std::max(solid, place));
    }

private:
    static int cell(float v) { return (int)std::floor(v / 4.0f); }
    struct P {
        float x, z, solid, place;
        bool tall;
    };
    std::map<std::pair<int, int>, std::vector<P>> cells_;
    float maxReach_ = 0.0f;
};

std::string resolveModel(const std::string& projectDir, const std::string& model) {
    if (model.empty() || projectDir.empty()) return "";
    std::string rel = model;
    if (rel.size() > 5 && rel.compare(rel.size() - 5, 5, ".tmdl") == 0)
        rel = rel.substr(0, rel.size() - 5) + ".obj";
    return (std::filesystem::path(projectDir) / rel).string();
}

}  // namespace

const char* kindName(int kind) {
    switch (kind) {
    case kLamp: return "lamps";
    case kTree: return "trees";
    case kBollard: return "bollards";
    case kSign: return "signs";
    case kSignal: return "signals";
    }
    return "?";
}

// --- settings ---------------------------------------------------------------------

bool isDefault(const Settings& s) { return s == Settings{}; }

namespace {
std::string fmt(float v) {
    char b[32];
    std::snprintf(b, sizeof(b), "%.6g", (double)v);
    return b;
}
void lineJson(std::string& out, const char* key, const Line& l, const Line& d) {
    if (l == d) return;
    std::string body;
    auto add = [&](const std::string& kv) { body += (body.empty() ? "" : ", ") + kv; };
    if (l.model != d.model) add("\"model\": \"" + json::escape(l.model) + "\"");
    if (l.spacing != d.spacing) add("\"spacing\": " + fmt(l.spacing));
    if (l.side != d.side) add("\"side\": " + std::to_string(l.side));
    if (l.offset != d.offset) add("\"offset\": " + fmt(l.offset));
    if (l.phase != d.phase) add("\"phase\": " + fmt(l.phase));
    if (l.scale != d.scale) add("\"scale\": " + fmt(l.scale));
    if (l.yaw != d.yaw) add("\"yaw\": " + fmt(l.yaw));
    out += (out.empty() ? "" : ", ") + std::string("\"") + key + "\": {" + body + "}";
}
void lineFrom(const json::Value* v, Line& l) {
    if (!v || v->type != json::Value::Type::Object) return;
    if (const auto* x = v->find("model")) l.model = x->stringOr("");
    if (const auto* x = v->find("spacing"))
        l.spacing = std::clamp((float)x->numberOr(0.0), 0.0f, 500.0f);
    if (const auto* x = v->find("side")) l.side = std::clamp((int)x->numberOr(0.0), 0, 3);
    if (const auto* x = v->find("offset"))
        l.offset = std::clamp((float)x->numberOr(l.offset), -2.0f, 20.0f);
    if (const auto* x = v->find("phase"))
        l.phase = std::clamp((float)x->numberOr(0.0), 0.0f, 500.0f);
    if (const auto* x = v->find("scale"))
        l.scale = std::clamp((float)x->numberOr(1.0), 0.05f, 50.0f);
    if (const auto* x = v->find("yaw")) l.yaw = (float)x->numberOr(0.0);
}
}  // namespace

std::string toJson(const Settings& s) {
    const Settings d;
    if (s == d) return "";
    std::string body;
    lineJson(body, "lamps", s.lamps, d.lamps);
    lineJson(body, "trees", s.trees, d.trees);
    lineJson(body, "bollards", s.bollards, d.bollards);
    auto add = [&](const std::string& kv) { body += (body.empty() ? "" : ", ") + kv; };
    if (s.seed != d.seed) add("\"seed\": " + std::to_string(s.seed));
    if (s.signs != d.signs) add("\"signs\": " + std::to_string(s.signs));
    if (s.signals) add("\"signals\": true");
    if (!s.signModel.empty()) add("\"signModel\": \"" + json::escape(s.signModel) + "\"");
    if (!s.signalModel.empty()) add("\"signalModel\": \"" + json::escape(s.signalModel) + "\"");
    return "{" + body + "}";
}

void fromJson(const json::Value& v, Settings& out) {
    out = Settings{};
    if (v.type != json::Value::Type::Object) return;
    lineFrom(v.find("lamps"), out.lamps);
    lineFrom(v.find("trees"), out.trees);
    lineFrom(v.find("bollards"), out.bollards);
    if (const auto* x = v.find("seed")) out.seed = (int)x->numberOr(0.0);
    if (const auto* x = v.find("signs")) out.signs = std::clamp((int)x->numberOr(0.0), 0, 2);
    if (const auto* x = v.find("signals")) out.signals = x->boolOr(false);
    if (const auto* x = v.find("signModel")) out.signModel = x->stringOr("");
    if (const auto* x = v.find("signalModel")) out.signalModel = x->stringOr("");
}

uint64_t signature(const Settings& s) {
    if (isDefault(s)) return 0;
    uint64_t h = 1469598103934665603ull;
    auto mixB = [&](const void* p, size_t n) {
        const unsigned char* b = (const unsigned char*)p;
        for (size_t i = 0; i < n; ++i) h = (h ^ b[i]) * 1099511628211ull;
    };
    auto mixS = [&](const std::string& str) { mixB(str.data(), str.size() + 1); };
    for (int k = 0; k < 3; ++k) {
        const Line& l = s.line(k);
        mixS(l.model);
        mixB(&l.spacing, sizeof(float)), mixB(&l.side, sizeof(int));
        mixB(&l.offset, sizeof(float)), mixB(&l.phase, sizeof(float));
        mixB(&l.scale, sizeof(float)), mixB(&l.yaw, sizeof(float));
    }
    mixB(&s.seed, sizeof(int)), mixB(&s.signs, sizeof(int));
    const int sg = s.signals ? 1 : 0;
    mixB(&sg, sizeof(int));
    mixS(s.signModel), mixS(s.signalModel);
    return h ? h : 1;
}

std::vector<std::string*> modelPaths(Settings& s) {
    return {&s.lamps.model, &s.trees.model, &s.bollards.model, &s.signModel, &s.signalModel};
}
std::vector<const std::string*> modelPaths(const Settings& s) {
    return {&s.lamps.model, &s.trees.model, &s.bollards.model, &s.signModel, &s.signalModel};
}

bool any(const std::vector<Settings>& settings) {
    for (const Settings& s : settings)
        if (s.lamps.spacing > 0.0f || s.trees.spacing > 0.0f || s.bollards.spacing > 0.0f ||
            s.signs != kSignNone || s.signals)
            return true;
    return false;
}

std::vector<ModelTri> builtinModel(int kind, int signKind, bool liveSignals) {
    switch (kind) {
    case kLamp: return makeLamp();
    case kTree: return makeTree();
    case kBollard: return makeBollard();
    case kSign: return makeSign(signKind);
    case kSignal: return makeSignal(liveSignals);
    }
    return {};
}

bool nodeSignalled(const roadgen::Crossing& c, const std::vector<roadgen::CrossingRoad>& roads,
                   const std::vector<Settings>& sets) {
    if (c.kind != roadgen::kCrossPatch || c.patchDuplicate || c.transition || c.arms != 4)
        return false;
    bool railway = false, wantSignals = false;
    for (int r : c.roads) {
        railway |= roads[(size_t)r].kind == 1;
        if ((size_t)r < sets.size()) wantSignals |= sets[(size_t)r].signals;
    }
    return wantSignals && !railway;
}

// --- placement ----------------------------------------------------------------------

Result build(const SceneInput& in) {
    Result res;
    if (!in.roads || !in.settings || !in.plan || in.roads->size() != in.settings->size() ||
        !any(*in.settings))
        return res;
    const std::vector<roadgen::CrossingRoad>& roads = *in.roads;
    const std::vector<Settings>& sets = *in.settings;
    const roadgen::CrossingPlan& plan = *in.plan;

    std::vector<std::unique_ptr<CentreLine>> lines;
    for (const roadgen::CrossingRoad& r : roads)
        lines.push_back(std::make_unique<CentreLine>(r.points));
    TriGrid paint;
    paint.build(in.paint);
    roadgen::Surface pave;
    if (!in.pavements.empty()) {
        roadgen::addPavementsToSurface(pave, in.pavements);
        pave.build();
    }
    struct Patch {
        const std::vector<float>* ring;
    };
    std::vector<Patch> patches;
    for (const roadgen::Crossing& c : plan.crossings)
        if (c.kind == roadgen::kCrossPatch && !c.patchDuplicate && c.shape.outline.size() >= 6)
            patches.push_back({&c.shape.outline});
    Placed placed;

    // Off every carriageway (own road: the footprint clear of the edge;
    // others and bridges: kClearance more), off every patch, off the paint.
    // `sc` is the PLACEMENT scale: a line's scale, never a tree's jitter, so a
    // new seed turns and sizes the trees without moving or dropping one.
    auto rejectReason = [&](int ownRoad, int kind, float x, float z, float sc,
                            bool nodeGap) -> int {
        const float solid = kSize[kind].solid * sc, place = kSize[kind].place * sc;
        for (size_t j = 0; j < roads.size(); ++j) {
            const roadgen::CrossingRoad& r = roads[j];
            float need = 0.5f * r.width + solid;
            if ((int)j != ownRoad) need += kClearance;
            if (r.elevation) need += 0.4f;  // a bridge's parapet stands outside its deck
            if (lines[j]->distance(x, z, need + 1.0f) < need) return 1;
        }
        for (const Patch& p : patches)
            if (polygonDistance(*p.ring, x, z) < solid + kClearance + (nodeGap ? kNodeGap : 0.0f))
                return 2;
        if (paint.near(x, z, solid + kClearance)) return 2;
        if (!placed.clear(x, z, solid, place, kind != kBollard)) return 3;
        return 0;
    };
    auto baseY = [&](float x, float z) {
        float y = in.ground ? in.ground(x, z) : 0.0f;
        if (!pave.empty()) {
            const float p = pave.at(x, z);
            if (p != roadgen::Surface::kNone && p > y) y = p;
        }
        return y;
    };
    auto count = [&](int why) {
        if (why == 1) ++res.rejectedRoad;
        else if (why == 2) ++res.rejectedNode;
        else if (why == 3) ++res.rejectedOverlap;
    };
    auto emit = [&](int road, int kind, int node, float x, float z, float fx, float fz,
                    float placeScale, float scale) {
        Instance inst;
        inst.road = road, inst.kind = kind, inst.node = node;
        inst.x = x, inst.z = z, inst.y = baseY(x, z);
        const float fl = std::hypot(fx, fz);
        inst.fx = fl > 0.0f ? fx / fl : 0.0f;
        inst.fz = fl > 0.0f ? fz / fl : 1.0f;
        inst.scale = scale;
        inst.radius = kSize[kind].solid * scale;
        inst.height = kSize[kind].height * scale;
        placed.add(x, z, kSize[kind].solid * placeScale, kSize[kind].place * placeScale,
                   kind != kBollard);
        res.instances.push_back(inst);
        ++res.perKind[kind];
    };

    // 1. Junction furniture first: a sign or a signal belongs at its stop line,
    // and the lines along the road then keep clear of it.
    for (size_t ci = 0; ci < plan.crossings.size(); ++ci) {
        const roadgen::Crossing& c = plan.crossings[ci];
        if (c.kind != roadgen::kCrossPatch || c.patchDuplicate || c.transition) continue;
        bool railway = false;
        for (int r : c.roads) railway |= roads[(size_t)r].kind == 1;
        if (railway) continue;  // no paint, no stop line (bakeMarkings' rule)
        // The signal rule, shared with the lane graph's phase cycle.
        const bool signalled = nodeSignalled(c, roads, sets);
        // roadgen::bakeMarkings' giving-way rule (who gets the stop line),
        // the one function both call - and the lane graph's priorities too.
        const std::vector<unsigned char> yields = roadgen::giveWayArms(c, roads);
        for (size_t ai = 0; ai < c.armList.size(); ++ai) {
            const roadgen::NodeArm& a = c.armList[ai];
            const roadgen::CrossingRoad& R = roads[(size_t)a.road];
            const Settings& S = sets[(size_t)a.road];
            int kind = -1, signKind = kSignGiveWay;
            if (signalled) {
                kind = kSignal;
            } else {
                const bool givesWay = yields[ai] != 0;
                if (givesWay && a.h >= 1.5f && R.markings > roadgen::kMarkNone &&
                    S.signs != kSignNone) {
                    kind = kSign;
                    signKind = S.signs;
                }
            }
            if (kind < 0) continue;
            // On the incoming lane's side (+lat, bakeMarkings' frame), just
            // behind the kerb, moved back along the arm until it stands
            // clear of the patch and the other roads.
            const float nx = -a.tz, nz = a.tx;
            const float lat = a.h + (R.kerb ? R.kerbWidth : 0.0f) + 0.45f;
            ++res.candidates;
            int why = 0;
            bool done = false;
            for (float al = 0.6f; al <= 8.6f && !done; al += 1.0f) {
                const float x = a.capX + a.tx * al + nx * lat;
                const float z = a.capZ + a.tz * al + nz * lat;
                why = rejectReason(a.road, kind, x, z, 1.0f, false);
                if (why == 0) {
                    emit(a.road, kind, (int)ci, x, z, a.tx, a.tz, 1.0f, 1.0f);
                    if (kind == kSign) res.instances.back().variant = signKind;
                    res.instances.back().arm = (int)ai;
                    done = true;
                }
            }
            if (!done) count(why);
        }
    }

    // 2. The lines along each road: lamps, then bollards, then trees.
    const int order[3] = {kLamp, kBollard, kTree};
    for (size_t ri = 0; ri < roads.size(); ++ri) {
        const roadgen::CrossingRoad& R = roads[ri];
        const Settings& S = sets[ri];
        if (R.elevation) continue;  // a bridge has parapets, not pavements
        const CentreLine& cl = *lines[ri];
        const float len = cl.length();
        const uint64_t key = mix64(hashString(R.id) ^ mix64((uint64_t)(uint32_t)S.seed + 0xF0u));
        for (int kind : order) {
            const Line& L = S.line(kind);
            if (!(L.spacing > 0.0f)) continue;
            const float spacing = std::max(L.spacing, 1.0f);
            const float d = 0.5f * R.width + L.offset;
            const float scale = L.scale;
            int station = 0;
            for (float s = L.phase + 0.5f * spacing; s <= len; s += spacing, ++station) {
                float cx, cz, tx, tz;
                cl.at(s, &cx, &cz, &tx, &tz);
                const float nx = -tz, nz = tx;  // the left (bakeMarkings' +lat)
                int sides[2] = {1, -1};
                int nSides = 2;
                if (L.side == kLeft) sides[0] = 1, nSides = 1;
                else if (L.side == kRight) sides[0] = -1, nSides = 1;
                else if (L.side == kAlternate) sides[0] = (station % 2) ? -1 : 1, nSides = 1;
                for (int k = 0; k < nSides; ++k) {
                    const float sd = (float)sides[k];
                    const float x = cx + nx * sd * d, z = cz + nz * sd * d;
                    float jitter = 1.0f;
                    float fx = -nx * sd, fz = -nz * sd;  // toward the road
                    if (kind == kTree) {
                        const float yaw = 2.0f * kPi * hash01(key, (uint64_t)station * 2 + (k ? 1 : 0), 1);
                        fx = std::sin(yaw), fz = std::cos(yaw);
                        jitter = 0.85f + 0.3f * hash01(key, (uint64_t)station * 2 + (k ? 1 : 0), 2);
                    }
                    if (L.yaw != 0.0f) {
                        const float a = L.yaw * kPi / 180.0f, c = std::cos(a), sn = std::sin(a);
                        const float rx = fx * c + fz * sn, rz = -fx * sn + fz * c;
                        fx = rx, fz = rz;
                    }
                    const float sc = scale * jitter;
                    ++res.candidates;
                    const int why = rejectReason((int)ri, kind, x, z, scale, kind != kLamp);
                    if (why != 0) {
                        count(why);
                        continue;
                    }
                    emit((int)ri, kind, -1, x, z, fx, fz, scale, sc);
                }
            }
        }
    }

    // 3. Meshes: each instance's model, posed and shaded, chunked by cell.
    std::map<std::string, Tris> modelCache;
    auto modelFor = [&](const Instance& inst) -> const Tris& {
        const Settings& S = sets[(size_t)inst.road];
        std::string path;
        const int signKind = inst.kind == kSign && inst.variant == kSignStop ? kSignStop : kSignGiveWay;
        if (inst.kind == kSign) path = S.signModel;
        else if (inst.kind == kSignal) path = S.signalModel;
        else path = S.line(inst.kind).model;
        const std::string key = path.empty()
                                    ? "builtin:" + std::to_string(inst.kind) + ":" + std::to_string(signKind)
                                    : path;
        auto it = modelCache.find(key);
        if (it != modelCache.end()) return it->second;
        Tris t;
        if (!path.empty()) {
            std::string err;
            if (!loadObjModel(resolveModel(in.projectDir, path), t, &err)) {
                res.warnings.push_back(std::string(kindName(inst.kind)) + ": " + err +
                                       " - using the built-in model");
                t.clear();
            }
        }
        if (t.empty()) t = builtinModel(inst.kind, signKind, in.liveSignals);
        return modelCache.emplace(key, std::move(t)).first->second;
    };
    // A fixed sun for the baked shade (the kerbs' and the bridges' rule).
    float L[3] = {0.35f, 0.85f, 0.40f};
    {
        const float l = std::sqrt(L[0] * L[0] + L[1] * L[1] + L[2] * L[2]);
        for (float& v : L) v /= l;
    }
    std::map<std::pair<int, int>, std::vector<size_t>> cells;
    for (size_t i = 0; i < res.instances.size(); ++i) {
        const Instance& inst = res.instances[i];
        cells[{(int)std::floor(inst.x / kCell), (int)std::floor(inst.z / kCell)}].push_back(i);
    }
    for (const auto& [cell, members] : cells) {
        (void)cell;
        int chunk = 0;
        for (size_t idx : members) {
            Instance& inst = res.instances[idx];
            const Tris& m = modelFor(inst);
            const int verts = (int)m.size() * 3;
            if (chunk > 0 && chunk + verts > kChunkBudget) {
                res.chunkSizes.push_back(chunk);
                chunk = 0;
            }
            const float s = inst.scale;
            inst.firstVertex = (int)res.tris.size();
            inst.vertexCount = verts;
            for (const ModelTri& t : m) {
                Vertex v[3];
                for (int k = 0; k < 3; ++k) {
                    const float lx = t.p[k][0] * s, ly = t.p[k][1] * s, lz = t.p[k][2] * s;
                    // local +Z -> the facing, local +X -> its right.
                    v[k].x = inst.x + lx * inst.fz + lz * inst.fx;
                    v[k].y = inst.y + ly;
                    v[k].z = inst.z - lx * inst.fx + lz * inst.fz;
                }
                const float ax = v[1].x - v[0].x, ay = v[1].y - v[0].y, az = v[1].z - v[0].z;
                const float bx = v[2].x - v[0].x, by = v[2].y - v[0].y, bz = v[2].z - v[0].z;
                float nx = ay * bz - az * by, ny = az * bx - ax * bz, nz = ax * by - ay * bx;
                const float nl = std::sqrt(nx * nx + ny * ny + nz * nz);
                float shade = 1.0f;
                if (t.lit) {
                    const float dot = nl > 0.0f ? (nx * L[0] + ny * L[1] + nz * L[2]) / nl : 0.0f;
                    shade = 0.45f + 0.55f * std::max(0.0f, dot);
                }
                for (int k = 0; k < 3; ++k) {
                    v[k].r = std::min(1.0f, t.c[k][0] * shade);
                    v[k].g = std::min(1.0f, t.c[k][1] * shade);
                    v[k].b = std::min(1.0f, t.c[k][2] * shade);
                    res.tris.push_back(v[k]);
                }
            }
            chunk += verts;
            Box b;
            const float r = inst.radius;
            b.mn[0] = inst.x - r, b.mn[1] = inst.y - 0.1f, b.mn[2] = inst.z - r;
            b.mx[0] = inst.x + r, b.mx[1] = inst.y + inst.height, b.mx[2] = inst.z + r;
            res.boxes.push_back(b);
        }
        if (chunk > 0) res.chunkSizes.push_back(chunk);
    }
    // Boxes follow the chunk order; the instances keep theirs. Put the
    // instances in the same (cell) order so instance i owns box i.
    {
        std::vector<Instance> ordered;
        for (const auto& [cell, members] : cells) {
            (void)cell;
            for (size_t idx : members) ordered.push_back(res.instances[idx]);
        }
        res.instances.swap(ordered);
    }
    return res;
}

SceneInput prepare(const std::vector<roadgen::CrossingRoad>& roads,
                   const std::vector<Settings>& settings, const roadgen::CrossingPlan& plan,
                   const roadgen::HeightFn& ground, const roadgen::TerrainGrid& grid,
                   const std::string& projectDir) {
    SceneInput in;
    in.roads = &roads;
    in.settings = &settings;
    in.plan = &plan;
    in.ground = ground;
    in.projectDir = projectDir;
    std::vector<roadgen::Vertex> roadTris;
    for (const roadgen::CrossingRoad& r : roads) {
        std::vector<roadgen::Vertex> mesh;
        roadgen::tessellate(r.points, r.width,
                            [&](float x, float z) { return ground(x, z) + roadgen::rankLift(r.rank); },
                            mesh, {}, r.sampleStep);
        roadTris.insert(roadTris.end(), mesh.begin(), mesh.end());
    }
    for (const roadgen::Crossing& c : plan.crossings) {
        if (c.kind != roadgen::kCrossPatch || c.patchDuplicate) continue;
        std::vector<roadgen::Vertex> mesh;
        roadgen::tessellateJunctionSurface(c.shape, roadTris, ground, c.lift, mesh, grid);
        in.patches.insert(in.patches.end(), mesh.begin(), mesh.end());
    }
    roadgen::Surface surf;
    surf.add(roadTris);
    surf.add(in.patches);
    surf.build();
    roadgen::bakeMarkings(plan, roads, surf, in.paint);
    bool kerbs = false;
    for (const roadgen::CrossingRoad& r : roads) kerbs |= r.kerb;
    if (kerbs) {
        const std::vector<roadgen::KerbPiece> pieces = roadgen::planKerbs(
            roads, plan, [&](float x, float z) { return surf.at(x, z); }, ground);
        in.pavements = roadgen::planPavements(roads, plan, pieces, ground);
    }
    return in;
}

// --- the console tables ---------------------------------------------------------------

void Tables::add(int scene, const Result& r) {
    int at = (int)(verts.size() / 3);
    for (int sz : r.chunkSizes) {
        rows.push_back({scene, at, sz});
        at += sz;
    }
    for (const Vertex& v : r.tris) {
        verts.insert(verts.end(), {v.x, v.y, v.z});
        auto byte = [](float c) { return (uint32_t)std::clamp((int)std::lround(c * 255.0f), 0, 255); };
        rgb.push_back(byte(v.r) << 16 | byte(v.g) << 8 | byte(v.b));
    }
    for (const Box& b : r.boxes)
        boxes.insert(boxes.end(), {(float)scene, b.mn[0], b.mn[1], b.mn[2], b.mx[0], b.mx[1], b.mx[2]});
    if (!r.instances.empty()) {
        std::ostringstream n;
        n << "// scene " << scene << ": " << r.instances.size() << " instances (";
        for (int k = 0; k < kKindCount; ++k) n << (k ? ", " : "") << r.perKind[k] << " " << kindName(k);
        n << "), " << r.tris.size() << " vertices in " << r.chunkSizes.size() << " chunks\n";
        notes += n.str();
    }
}

void Tables::addLight(int scene, const std::vector<float>& xyz, const std::vector<uint32_t>& uv,
                      const std::vector<int>& chunkSizes, const std::string& note) {
    int at = (int)(verts.size() / 3);
    for (int sz : chunkSizes) {
        Row r{scene, at, sz};
        r.light = 1;
        rows.push_back(r);
        at += sz;
    }
    verts.insert(verts.end(), xyz.begin(), xyz.end());
    rgb.insert(rgb.end(), uv.begin(), uv.end());
    notes += note;
}

std::string Tables::source(bool embedVerts) const {
    std::ostringstream out;
    auto lit = [](float v) {
        char b[48];
        std::snprintf(b, sizeof(b), "%.7g", (double)v);
        std::string s = b;
        if (s.find_first_of(".eEn") == std::string::npos) s += ".0";
        return s + "F";
    };
    out << "// Street furniture (docs/roads.md \"Street furniture\"): lamps, trees, bollards,\n"
           "// signs and traffic lights, host-baked triangle lists in vertex colour, one\n"
           "// row per cell chunk; x, y, z per vertex, 0xRRGGBB beside it. Uploaded\n"
           "// unchanged at scene load, with one collision box per instance.\n"
        << notes << "constexpr int ROAD_FURN_COUNT = " << rows.size() << ";\n"
        << "constexpr float ROAD_FURN_DRAW_DISTANCE = " << lit(kDrawDistance) << ";\n"
        << (this->lit ? "struct RoadFurnRt { int scene; int first; int count; int light; };\n"
                : "struct RoadFurnRt { int scene; int first; int count; };\n")
        << "constexpr int ROAD_FURN_BOX_COUNT = " << boxes.size() / 7 << ";\n";
    if (boxes.empty()) {
        out << "constexpr float ROAD_FURN_BOXES[1] = {};\n";
    } else {
        out << "constexpr float ROAD_FURN_BOXES[" << boxes.size() << "] = {\n";
        for (size_t k = 0; k + 6 < boxes.size(); k += 7) {
            out << "   ";
            for (size_t j = 0; j < 7; ++j) out << " " << lit(boxes[k + j]) << ",";
            out << "\n";
        }
        out << "};\n";
    }
    if (rows.empty()) {
        out << "constexpr RoadFurnRt ROAD_FURN[1] = {};\n"
            << "constexpr float ROAD_FURN_VERTS[1] = {};\n"
            << "constexpr unsigned int ROAD_FURN_RGB[1] = {};\n";
        return out.str();
    }
    out << "constexpr RoadFurnRt ROAD_FURN[" << rows.size() << "] = {\n";
    for (const Row& r : rows)
        out << "    {" << r.scene << ", " << r.first << ", " << r.count
            << (this->lit ? (r.light ? ", 1" : ", 0") : "") << "},\n";
    if (!embedVerts) {
        out << "};\n// ROAD_FURN_VERTS, ROAD_FURN_RGB: in bin/roadfile/roads.bin.\n";
        return out.str();
    }
    out << "};\nconstexpr float ROAD_FURN_VERTS[" << verts.size() << "] = {\n";
    for (size_t k = 0; k + 2 < verts.size(); k += 3)
        out << "    " << lit(verts[k]) << ", " << lit(verts[k + 1]) << ", " << lit(verts[k + 2]) << ",\n";
    out << "};\nconstexpr unsigned int ROAD_FURN_RGB[" << rgb.size() << "] = {";
    for (size_t k = 0; k < rgb.size(); ++k) {
        char b[16];
        std::snprintf(b, sizeof(b), "0x%06X", (unsigned)rgb[k]);
        out << (k % 12 == 0 ? "\n    " : " ") << b << ",";
    }
    out << "\n};\n";
    return out.str();
}

std::string uploadSource(bool lit) {
    std::string s = R"(  // STREET FURNITURE (docs/roads.md "Street furniture"): lamps, trees,
  // bollards, signs and traffic lights - host-baked triangle lists in vertex
  // colour, one ROAD_FURN row per cell chunk, uploaded unchanged. Owner -7:
  // renderProcChunks draws them (frustum reject, the chunk draw distance,
  // occlusion), the road height index does not read them, and their poles and
  // trunks are procColliders (the walker and every car stop at them).
  // Untextured: no VRAM; 128 is the full colour in an untextured bag.
  for (size_t i = procChunks.size(); i > 0; --i)
    if (procChunks[i - 1].owner == -7)
      procChunks.erase(procChunks.begin() + (i - 1));
  {
    int furnChunks = 0, furnVertices = 0;
    const float k = 128.0F / 255.0F;
    for (int fi = 0; fi < ROAD_FURN_COUNT; ++fi) {
      const RoadFurnRt& fr = ROAD_FURN[fi];
      if (fr.scene != scene || fr.count < 3) continue;
      any = true;
      procChunks.push_back(ProcChunk());
      ProcChunk& c = procChunks.back();
      c.owner = -7;
      c.drawDist = ROAD_FURN_DRAW_DISTANCE;
      c.stripRun = 0;
      for (int v = 0; v < fr.count; ++v) {
        const float* p = &ROAD_FURN_VERTS[(size_t)(fr.first + v) * 3];
        const unsigned int rgb = ROAD_FURN_RGB[fr.first + v];
        c.vertices.push_back(Tyra::Vec4(p[0], p[1], p[2], 1.0F));
        c.colors.push_back(Tyra::Color((float)((rgb >> 16) & 255U) * k,
                                       (float)((rgb >> 8) & 255U) * k,
                                       (float)(rgb & 255U) * k, 128.0F));
      }
      ++furnChunks;
      furnVertices += fr.count;
    }
    for (int i = (int)procColliders.size() - 1; i >= 0; --i)
      if (procColliders[(size_t)i].owner == -7)
        procColliders.erase(procColliders.begin() + i);
    int furnBoxes = 0;
    for (int bi = 0; bi < ROAD_FURN_BOX_COUNT; ++bi) {
      const float* b = &ROAD_FURN_BOXES[(size_t)bi * 7];
      if ((int)b[0] != scene) continue;
      StaticBox sb;
      for (int a = 0; a < 3; ++a) {
        sb.mn[a] = b[1 + a];
        sb.mx[a] = b[4 + a];
      }
      sb.owner = -7;
      sb.instance = -1;
      procColliders.push_back(sb);
      ++furnBoxes;
    }
    if (furnChunks > 0 || furnBoxes > 0)
      TYRA_LOG("ROADFURN scene ", scene, " chunks ", furnChunks, " vertices ", furnVertices,
               " boxes ", furnBoxes);
  }
)";
    if (!lit) return s;
    // Lit street lamps (docs/weather.md): a light row is a lamp POOL - an
    // additive decal through the corona sprite, its UV packed in the colour
    // word, drawn at night by renderRoadLamps (the colour is the night level,
    // one per frame, bound by procFinishChunks).
    const std::string from = "      c.stripRun = 0;\n"
                             "      for (int v = 0; v < fr.count; ++v) {\n"
                             "        const float* p = &ROAD_FURN_VERTS[(size_t)(fr.first + v) * 3];\n"
                             "        const unsigned int rgb = ROAD_FURN_RGB[fr.first + v];\n"
                             "        c.vertices.push_back(Tyra::Vec4(p[0], p[1], p[2], 1.0F));\n";
    const std::string to = "      c.stripRun = 0;\n"
                           "      c.lampLight = fr.light;\n"
                           "      if (fr.light) {\n"
                           "        c.drawDist = ROAD_LAMP_DRAW_DISTANCE;\n"
                           "        c.roadTex = beamCoronaTex;\n"
                           "        c.colors.shrink_to_fit();  // no per-vertex colour: one per frame\n"
                           "      }\n"
                           "      for (int v = 0; v < fr.count; ++v) {\n"
                           "        const float* p = &ROAD_FURN_VERTS[(size_t)(fr.first + v) * 3];\n"
                           "        const unsigned int rgb = ROAD_FURN_RGB[fr.first + v];\n"
                           "        c.vertices.push_back(Tyra::Vec4(p[0], p[1], p[2], 1.0F));\n"
                           "        if (fr.light) {\n"
                           "          c.sts.push_back(Tyra::Vec4((float)((rgb >> 12) & 4095U) * (1.0F / 4095.0F),\n"
                           "                                     (float)(rgb & 4095U) * (1.0F / 4095.0F), 1.0F, 0.0F));\n"
                           "          continue;\n"
                           "        }\n";
    const size_t at = s.find(from);
    if (at != std::string::npos) s.replace(at, from.size(), to);
    return s;
}

// --- --vehicle-check "road furniture" ------------------------------------------------

void check(void (*verdict)(bool, const char*)) {
    std::printf("-- road furniture --\n");
    const roadgen::HeightFn ground = [](float, float) { return 0.0f; };
    auto road = [](const char* id, std::vector<float> pts, float width, bool kerb) {
        roadgen::CrossingRoad r;
        r.id = id;
        r.points = std::move(pts);
        r.width = width;
        r.intersection = "res/materials/x.mtl";
        r.kerb = kerb;
        r.pavement = kerb ? 2.5f : 0.0f;
        r.markings = roadgen::kMarkCrossings;
        return r;
    };
    // A kerbed T with pavements and zebras, a four-way crossing of two through
    // roads, and a lone kerbless street for exact counts.
    const std::vector<roadgen::CrossingRoad> roads = {
        road("main", {-60, 0, -20, 0, 20, 0, 60, 0}, 10, true),
        road("stem", {0, -50, 0, -25, 0, 0}, 8, true),
        road("ew", {-60, 100, 0, 100, 60, 100}, 9, true),
        road("ns", {0, 50, 0, 100, 0, 150}, 9, true),
        road("lone", {-60, 220, 0, 220, 60, 220}, 8, false),
        road("spur", {40, 140, 40, 120, 40, 100}, 8, true),
    };
    std::vector<Settings> sets(roads.size());
    sets[0].lamps.spacing = 15;
    sets[0].trees.spacing = 12;
    sets[0].trees.side = kAlternate;
    sets[0].bollards.spacing = 5;
    sets[0].bollards.side = kRight;
    sets[0].signs = kSignGiveWay;
    sets[1].lamps.spacing = 15;
    sets[1].signs = kSignStop;
    sets[2].signals = true;
    sets[2].lamps.spacing = 20;
    sets[3].signs = kSignGiveWay;  // overruled by the signals at the X
    sets[4].lamps.spacing = 10;
    sets[5].signs = kSignGiveWay;

    struct Scene {
        roadgen::CrossingPlan plan;
        std::vector<roadgen::Vertex> roadTris, patches, paint;
        std::vector<roadgen::PavementMesh> pave;
        Result res;
    };
    auto bake = [&](const std::vector<Settings>& s, Scene& sc) {
        sc.plan = roadgen::planCrossings(roads, {});
        sc.roadTris.clear();
        for (const roadgen::CrossingRoad& rd : roads) {
            std::vector<roadgen::Vertex> mesh;
            roadgen::tessellate(rd.points, rd.width,
                                [&](float x, float z) { return ground(x, z) + roadgen::rankLift(rd.rank); },
                                mesh, {}, rd.sampleStep);
            sc.roadTris.insert(sc.roadTris.end(), mesh.begin(), mesh.end());
        }
        sc.patches.clear();
        for (const roadgen::Crossing& c : sc.plan.crossings) {
            if (c.kind != roadgen::kCrossPatch || c.patchDuplicate) continue;
            std::vector<roadgen::Vertex> mesh;
            roadgen::tessellateJunctionSurface(c.shape, sc.roadTris, ground, c.lift, mesh);
            sc.patches.insert(sc.patches.end(), mesh.begin(), mesh.end());
        }
        roadgen::Surface surf;
        surf.add(sc.roadTris);
        surf.add(sc.patches);
        surf.build();
        roadgen::bakeMarkings(sc.plan, roads, surf, sc.paint);
        const std::vector<roadgen::KerbPiece> kerbs = roadgen::planKerbs(
            roads, sc.plan, [&](float x, float z) { return surf.at(x, z); }, ground);
        sc.pave = roadgen::planPavements(roads, sc.plan, kerbs, ground);
        SceneInput in;
        in.roads = &roads;
        in.settings = &s;
        in.plan = &sc.plan;
        in.ground = ground;
        in.patches = sc.patches;
        in.paint = sc.paint;
        in.pavements = sc.pave;
        sc.res = build(in);
    };
    Scene a, b;
    bake(sets, a);
    bake(sets, b);
    const Result& r = a.res;
    std::printf("  %zu instances (%d lamps, %d trees, %d bollards, %d signs, %d signals), "
                "%zu vertices in %zu chunks, %zu boxes; %d candidates, dropped %d road / "
                "%d node / %d overlap\n",
                r.instances.size(), r.perKind[kLamp], r.perKind[kTree], r.perKind[kBollard],
                r.perKind[kSign], r.perKind[kSignal], r.tris.size(), r.chunkSizes.size(),
                r.boxes.size(), r.candidates, r.rejectedRoad, r.rejectedNode, r.rejectedOverlap);
    verdict(r.tris.size() == b.res.tris.size() &&
                std::memcmp(r.tris.data(), b.res.tris.data(), r.tris.size() * sizeof(Vertex)) == 0 &&
                r.instances.size() == b.res.instances.size(),
            "the same roads bake the same furniture, bit for bit");
    bool allKinds = true;
    for (int k = 0; k < kKindCount; ++k) allKinds &= r.perKind[k] > 0;
    verdict(allKinds, "every kind is placed (lamps, trees, bollards, signs, signals)");

    // Nothing on a road, a patch or paint: the solid footprint, sampled.
    roadgen::Surface onRoad, onPatch;
    onRoad.add(a.roadTris);
    onRoad.build();
    onPatch.add(a.patches);
    onPatch.build();
    TriGrid paintGrid;
    paintGrid.build(a.paint);
    int bad = 0;
    for (const Instance& in : r.instances) {
        for (int k = 0; k <= 8; ++k) {
            const float ang = 2.0f * kPi * (float)k / 8.0f;
            const float rr = k == 8 ? 0.0f : in.radius;
            const float x = in.x + rr * std::sin(ang), z = in.z + rr * std::cos(ang);
            if (onRoad.at(x, z) != roadgen::Surface::kNone ||
                onPatch.at(x, z) != roadgen::Surface::kNone) {
                ++bad;
                break;
            }
        }
        if (paintGrid.near(in.x, in.z, in.radius)) ++bad;
    }
    std::printf("  %d instance(s) touching a road, a patch or paint\n", bad);
    verdict(bad == 0, "nothing stands on a road, a junction patch or node paint");

    // Exact count and spacing on the lone street: stations every 10 from 5.
    {
        std::vector<float> left, right;
        for (const Instance& in : r.instances)
            if (in.road == 4 && in.kind == kLamp) (in.z > 220 ? left : right).push_back(in.x);
        std::sort(left.begin(), left.end());
        std::sort(right.begin(), right.end());
        CentreLine cl(roads[4].points);
        const int expect = (int)std::floor((cl.length() - 5.0f) / 10.0f) + 1;
        float worst = 0.0f;
        for (const std::vector<float>* v : {&left, &right})
            for (size_t i = 1; i < v->size(); ++i)
                worst = std::max(worst, std::fabs((*v)[i] - (*v)[i - 1] - 10.0f));
        std::printf("  lone street: %zu + %zu lamps (expected %d per side), worst spacing "
                    "error %.4f\n", left.size(), right.size(), expect, worst);
        verdict((int)left.size() == expect && (int)right.size() == expect && worst < 0.01f,
                "a line keeps its spacing and its count on a free street");
    }
    // On a crowded street the survivors still sit on the station grid.
    {
        float worst = 0.0f;
        int n = 0;
        for (const Instance& in : r.instances) {
            if (in.road != 0 || in.kind != kLamp) continue;
            const float s = in.x + 60.0f;  // straight along +x from -60
            const float k = (s - 7.5f) / 15.0f;
            worst = std::max(worst, std::fabs(k - std::round(k)) * 15.0f);
            ++n;
        }
        std::printf("  main street: %d lamps, worst off-station %.4f\n", n, worst);
        verdict(n > 0 && worst < 0.01f, "skipped stations leave the others on their spacing");
    }
    // Lamps face their road; they stand on the pavement where there is one.
    {
        int facing = 0, lamps = 0, onPave = 0, paved = 0;
        roadgen::Surface pv;
        roadgen::addPavementsToSurface(pv, a.pave);
        pv.build();
        for (const Instance& in : r.instances) {
            if (in.kind != kLamp) continue;
            ++lamps;
            const roadgen::CrossingRoad& rd = roads[(size_t)in.road];
            CentreLine cl(rd.points);
            float best = 1e30f, bx = 0, bz = 0;
            for (float s = 0; s <= cl.length(); s += 0.25f) {
                float x, z, tx, tz;
                cl.at(s, &x, &z, &tx, &tz);
                const float d = std::hypot(x - in.x, z - in.z);
                if (d < best) best = d, bx = x, bz = z;
            }
            const float dx = (bx - in.x) / best, dz = (bz - in.z) / best;
            if (in.fx * dx + in.fz * dz > 0.95f) ++facing;
            const float p = pv.at(in.x, in.z);
            if (p != roadgen::Surface::kNone) {
                ++paved;
                if (std::fabs(in.y - p) < 1e-4f && p > 0.1f) ++onPave;
            }
        }
        std::printf("  lamps: %d of %d face their road, %d of %d over a pavement stand on it\n",
                    facing, lamps, onPave, paved);
        verdict(facing == lamps && paved > 0 && onPave == paved,
                "lamps face the road and stand on the pavement top");
    }
    // Signs: at a painted stop line, facing the approach; signals at the X.
    {
        int signs = 0, atLine = 0, facing = 0, stops = 0, signalsAtX = 0, signsAtX = 0;
        int xNode = -1;
        for (size_t ci = 0; ci < a.plan.crossings.size(); ++ci)
            if (a.plan.crossings[ci].arms == 4) xNode = (int)ci;
        for (const Instance& in : r.instances) {
            if (in.kind == kSignal && in.node == xNode) ++signalsAtX;
            if (in.kind != kSign) continue;
            ++signs;
            if (in.node == xNode) ++signsAtX;
            if (in.variant == kSignStop) ++stops;
            float best = 1e30f;
            for (const roadgen::Vertex& v : a.paint)
                best = std::min(best, std::hypot(v.x - in.x, v.z - in.z));
            if (best < 3.0f) ++atLine;
            const roadgen::Crossing& c = a.plan.crossings[(size_t)in.node];
            const float ox = in.x - c.shape.x, oz = in.z - c.shape.z, ol = std::hypot(ox, oz);
            if ((in.fx * ox + in.fz * oz) / ol > 0.5f) ++facing;
        }
        std::printf("  signs: %d (%d stop), %d within 3 of paint, %d facing the approach; "
                    "the X has %d signals and %d signs\n",
                    signs, stops, atLine, facing, signalsAtX, signsAtX);
        verdict(signs > 0 && atLine == signs && facing == signs,
                "every sign stands at a painted stop line and faces the approaching driver");
        verdict(stops > 0 && stops < signs, "the sign kind follows the road (stop / give way)");
        verdict(xNode >= 0 && signalsAtX == 4 && signsAtX == 0,
                "a signalled four-way gets one light per arm and no signs");
    }
    // The tables add up.
    {
        size_t verts = 0, sum = 0;
        bool chunksOk = true;
        for (const Instance& in : r.instances)
            verts += builtinModel(in.kind, in.variant).size() * 3;
        for (int sz : r.chunkSizes) {
            sum += (size_t)sz;
            chunksOk &= sz > 0 && sz % 3 == 0 && sz <= kChunkBudget;
        }
        std::printf("  %zu model vertices expected, %zu baked, chunks sum %zu\n", verts,
                    r.tris.size(), sum);
        verdict(verts == r.tris.size() && sum == r.tris.size() && chunksOk &&
                    r.boxes.size() == r.instances.size(),
                "the instance count, the vertices, the chunks and the boxes all match");
        bool boxesOk = true;
        for (size_t i = 0; i < r.boxes.size(); ++i) {
            const Instance& in = r.instances[i];
            const Box& bx = r.boxes[i];
            boxesOk &= in.x >= bx.mn[0] && in.x <= bx.mx[0] && in.z >= bx.mn[2] &&
                       in.z <= bx.mx[2] && bx.mx[1] > in.y + 0.5f;
        }
        verdict(boxesOk, "every instance owns the collision box round its pole or trunk");
    }
    // The seed moves the trees' yaw and size, not where they stand.
    {
        std::vector<Settings> s2 = sets;
        s2[0].seed = 7;
        Scene c;
        bake(s2, c);
        int same = 0, turned = 0, trees = 0, after = 0;
        for (const Instance& q : c.res.instances) after += q.kind == kTree;
        for (const Instance& p : r.instances) {
            if (p.kind != kTree) continue;
            ++trees;
            for (const Instance& q : c.res.instances)
                if (q.kind == kTree && q.x == p.x && q.z == p.z) {
                    ++same;
                    if (std::fabs(p.fx - q.fx) > 1e-3f) ++turned;
                }
        }
        std::printf("  seed 7: %d of %d trees in place, %d turned\n", same, trees, turned);
        verdict(trees > 0 && same == trees && after == trees && turned > trees / 2,
                "a new seed turns the trees and keeps their stations");
    }
    // An .obj model: its triangles, coloured by Kd.
    {
        const std::filesystem::path dir =
            std::filesystem::temp_directory_path() / "tyrax-roadfurn-check";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        if (std::FILE* f = std::fopen((dir / "post.obj").string().c_str(), "wb")) {
            std::fputs("mtllib post.mtl\nv -0.1 0 0\nv 0.1 0 0\nv 0 3 0\nv 0 0 0.1\n"
                       "usemtl red\nf 1 2 3\nf 2 4 3\n", f);
            std::fclose(f);
        }
        if (std::FILE* f = std::fopen((dir / "post.mtl").string().c_str(), "wb")) {
            std::fputs("newmtl red\nKd 1 0 0\n", f);
            std::fclose(f);
        }
        std::vector<Settings> s3(roads.size());
        s3[4].lamps.spacing = 10;
        s3[4].lamps.model = "post.obj";
        const roadgen::CrossingPlan plan = roadgen::planCrossings(roads, {});
        SceneInput in;
        in.roads = &roads;
        in.settings = &s3;
        in.plan = &plan;
        in.ground = ground;
        in.projectDir = dir.string();
        const Result m = build(in);
        bool red = !m.tris.empty();
        for (const Vertex& v : m.tris) red &= v.r > 0.4f && v.g == 0.0f && v.b == 0.0f;
        std::printf("  .obj lamp: %zu instances, %zu vertices, %zu warning(s)\n",
                    m.instances.size(), m.tris.size(), m.warnings.size());
        verdict(!m.instances.empty() && m.tris.size() == m.instances.size() * 6 && red &&
                    m.warnings.empty(),
                "an .obj model is instanced with its own triangles and colours");
        std::filesystem::remove_all(dir, ec);
    }
    // The settings round-trip, and the defaults write nothing.
    {
        Settings s = sets[0];
        s.trees.model = "res/models/urban/tree-park-large.obj";
        s.trees.yaw = 90;
        s.signals = true;
        json::Value v;
        const std::string text = toJson(s);
        Settings back;
        const bool parsed = json::parse(text, v);
        if (parsed) fromJson(v, back);
        verdict(parsed && back == s && toJson(Settings{}).empty() && signature(Settings{}) == 0 &&
                    signature(s) != 0,
                "the settings round-trip and the defaults save nothing");
    }
}

}  // namespace roadfurn
