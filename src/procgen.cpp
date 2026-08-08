#include "prefab.hpp"
#include "procgen.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <mutex>
#include <unordered_map>

#include "objparser.hpp"
#include "primmesh.hpp"

namespace procgen {

namespace {

constexpr float kPi = 3.14159265358979f;
constexpr float kDeg = 180.0f / kPi;

// --- hashing / randomness --------------------------------------------------

// splitmix64 finalizer: cheap, no visible structure in the low bits, and
// stable across compilers - which matters because these numbers ARE the
// content (a different mix would reshuffle every existing project).
uint64_t mix64(uint64_t x) {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

uint64_t hashCombine(uint64_t h, uint64_t v) { return mix64(h ^ mix64(v)); }

uint64_t hashStr(uint64_t h, const std::string& s) {
    for (char c : s) h = hashCombine(h, (uint64_t)(unsigned char)c);
    return hashCombine(h, s.size());
}

uint64_t hashFloat(uint64_t h, float f) {
    // Quantize to the SIX SIGNIFICANT DIGITS the .tyra file keeps (project.cpp
    // writes every float as %.6g). Two reasons, both learned the hard way:
    //  - a slider that lands on the same value by a different float path must
    //    not invalidate the cache;
    //  - more importantly, a value that has been through a save/load cycle
    //    comes back as the nearest float to its printed form, so hashing raw
    //    bits made bakeHash differ before and after a save - every bake read
    //    as stale forever after reopening the project. A FIXED-STEP quantizer
    //    does not fix that either: whenever the printed value and the original
    //    straddle a step boundary the buckets differ, and over a thousand
    //    heightmap samples that is a certainty, not a risk.
    // Rounding to the file's own precision is stable by construction: the
    // reparsed value quantizes to the integer it was printed from.
    double v = (double)f;
    long long q = 0;
    int e = 0;
    if (v != 0.0 && std::isfinite(v)) {
        e = (int)std::floor(std::log10(std::fabs(v)));
        q = std::llround(v * std::pow(10.0, 5 - e));
        // Carry, so 9.999999 and its printed form 10.0 agree (the one boundary
        // the digit count cannot absorb).
        if (q >= 1000000 || q <= -1000000) {
            q /= 10;
            ++e;
        }
    }
    h = hashCombine(h, (uint64_t)q);
    return hashCombine(h, (uint64_t)(int64_t)e);
}

float unitFromHash(uint64_t h) {
    return (float)(h >> 40) * (1.0f / 16777216.0f);  // 24 bits -> [0,1)
}

// Halton sequence, the low-discrepancy source behind prefix stability: the
// first N points of it are already evenly spread, so density = a prefix.
float halton(int index, int base) {
    float f = 1.0f, r = 0.0f;
    int i = index + 1;  // index 0 would be the corner
    while (i > 0) {
        f /= (float)base;
        r += f * (float)(i % base);
        i /= base;
    }
    return r;
}

// --- noise -----------------------------------------------------------------

float gradDot(int ix, int iz, float dx, float dz, uint32_t seed) {
    const uint64_t h = mix64(((uint64_t)(uint32_t)ix << 32) ^ (uint32_t)iz ^
                             ((uint64_t)seed << 16));
    const float a = unitFromHash(h) * 2.0f * kPi;
    return std::cos(a) * dx + std::sin(a) * dz;
}

float fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }

// Perlin gradient noise, remapped to 0..1.
float perlin(float x, float z, uint32_t seed) {
    const int x0 = (int)std::floor(x), z0 = (int)std::floor(z);
    const float fx = x - (float)x0, fz = z - (float)z0;
    const float u = fade(fx), v = fade(fz);
    const float n00 = gradDot(x0, z0, fx, fz, seed);
    const float n10 = gradDot(x0 + 1, z0, fx - 1.0f, fz, seed);
    const float n01 = gradDot(x0, z0 + 1, fx, fz - 1.0f, seed);
    const float n11 = gradDot(x0 + 1, z0 + 1, fx - 1.0f, fz - 1.0f, seed);
    const float a = n00 + u * (n10 - n00);
    const float b = n01 + u * (n11 - n01);
    return std::clamp((a + v * (b - a)) * 0.7071f + 0.5f, 0.0f, 1.0f);
}

// Worley / cellular: distance to the nearest of one feature point per cell.
float worley(float x, float z, uint32_t seed) {
    const int cx = (int)std::floor(x), cz = (int)std::floor(z);
    float best = 4.0f;
    for (int dz = -1; dz <= 1; ++dz)
        for (int dx = -1; dx <= 1; ++dx) {
            const int gx = cx + dx, gz = cz + dz;
            const uint64_t h = mix64(((uint64_t)(uint32_t)gx << 32) ^
                                     (uint32_t)gz ^ ((uint64_t)seed << 20));
            const float px = (float)gx + unitFromHash(h);
            const float pz = (float)gz + unitFromHash(mix64(h));
            const float d = (px - x) * (px - x) + (pz - z) * (pz - z);
            if (d < best) best = d;
        }
    return std::clamp(std::sqrt(best), 0.0f, 1.0f);
}

float noiseAt(int kind, float x, float z, int octaves, uint32_t seed) {
    float sum = 0.0f, amp = 1.0f, norm = 0.0f, fx = x, fz = z;
    for (int o = 0; o < octaves; ++o) {
        float n;
        switch (kind) {
            case 1:  // ridged: veins where the noise crosses its midpoint
                n = 1.0f - std::fabs(perlin(fx, fz, seed + o * 977) * 2.0f - 1.0f);
                break;
            case 2: n = 1.0f - worley(fx, fz, seed + o * 977); break;
            case 3: {  // warped: push the domain through another noise field
                const float wx = perlin(fx * 0.5f, fz * 0.5f, seed + 31) - 0.5f;
                const float wz = perlin(fx * 0.5f + 5.2f, fz * 0.5f + 1.3f,
                                        seed + 71) -
                                 0.5f;
                n = perlin(fx + wx * 2.5f, fz + wz * 2.5f, seed + o * 977);
                break;
            }
            default: n = perlin(fx, fz, seed + o * 977); break;
        }
        sum += n * amp;
        norm += amp;
        amp *= 0.5f;
        fx *= 2.0f;
        fz *= 2.0f;
    }
    return norm > 0.0f ? sum / norm : 0.0f;
}

// Soft band response: 1 inside [lo,hi], ramping to 0 over `falloff` outside.
// The shared shape of every "range" parameter in the library, so a slope filter
// and a height mask feel the same.
float band(float v, float lo, float hi, float falloff) {
    if (lo > hi) std::swap(lo, hi);
    if (v >= lo && v <= hi) return 1.0f;
    if (falloff <= 0.0f) return 0.0f;
    const float d = v < lo ? lo - v : v - hi;
    return std::clamp(1.0f - d / falloff, 0.0f, 1.0f);
}

float remap01(float v, float lo, float hi) {
    if (hi - lo < 1e-6f) return v >= hi ? 1.0f : 0.0f;
    return std::clamp((v - lo) / (hi - lo), 0.0f, 1.0f);
}

// --- terrain ---------------------------------------------------------------

// The "there is no ground here" height. Twin of TERRAIN_VOID_Y in
// templates.cpp: deep, but deliberately finite so arithmetic on it stays sane.
constexpr float kTerrainVoidY = -1000000.0f;

// Bilinear terrain height - the exact mapping of the generated
// terrainHeightAtScene (templates.cpp) and of navmesh's copy. The game walks
// on this surface, so everything we place on it must sample it identically.
float terrainHeight(const SceneData& s, float x, float z) {
    // No terrain in this scene (docs/terrain.md) = no ground to stand on, and
    // the generated sampler says so with TERRAIN_VOID_Y. Answering 0 here - or
    // worse, the heightmap that terrain removal deliberately KEEPS so the
    // ground can come back - would put the editor's forest on a surface the
    // console does not have. This value is the twin of `TERRAIN_VOID_Y` in
    // templates.cpp; change one, change both.
    if (!s.terrain.enabled) return kTerrainVoidY;
    const bool hasData =
        s.hmW >= 2 && s.hmD >= 2 && (int)s.heights.size() == s.hmW * s.hmD;
    if (!hasData) return 0.0f;
    const float stepX = (float)s.terrain.width / (float)(s.hmW - 1);
    const float stepZ = (float)s.terrain.depth / (float)(s.hmD - 1);
    float gx = (x + (float)s.terrain.width * 0.5f) / stepX;
    float gz = (z + (float)s.terrain.depth * 0.5f) / stepZ;
    gx = std::clamp(gx, 0.0f, (float)s.hmW - 1.001f);
    gz = std::clamp(gz, 0.0f, (float)s.hmD - 1.001f);
    const int ix = (int)gx, iz = (int)gz;
    const float fx = gx - (float)ix, fz = gz - (float)iz;
    const float* hm = s.heights.data();
    const float t = hm[iz * s.hmW + ix] * (1.0f - fx) + hm[iz * s.hmW + ix + 1] * fx;
    const float b = hm[(iz + 1) * s.hmW + ix] * (1.0f - fx) +
                    hm[(iz + 1) * s.hmW + ix + 1] * fx;
    return t * (1.0f - fz) + b * fz;
}

void terrainNormal(const SceneData& s, float x, float z, float out[3]) {
    const float e = 0.5f;
    const float hx = terrainHeight(s, x + e, z) - terrainHeight(s, x - e, z);
    const float hz = terrainHeight(s, x, z + e) - terrainHeight(s, x, z - e);
    float n[3] = {-hx, 2.0f * e, -hz};
    const float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    const float inv = len > 1e-6f ? 1.0f / len : 0.0f;
    out[0] = n[0] * inv;
    out[1] = n[1] * inv;
    out[2] = n[2] * inv;
}

// Discrete Laplacian of the height field: > 0 on ridges/bumps, < 0 in
// hollows. Normalized into roughly -1..1 for the mask range parameters.
float terrainCurvature(const SceneData& s, float x, float z) {
    const float e = 2.0f;
    const float c = terrainHeight(s, x, z);
    const float sum = terrainHeight(s, x + e, z) + terrainHeight(s, x - e, z) +
                      terrainHeight(s, x, z + e) + terrainHeight(s, x, z - e);
    return std::clamp((4.0f * c - sum) * 0.5f, -1.0f, 1.0f);
}

// Painted terrain-layer weight 0..1 at a world position (SceneData::splat).
float terrainLayerWeight(const SceneData& s, int layer, float x, float z) {
    const int n = (int)s.terrainLayers.size();
    if (layer < 0 || layer >= n || s.splatW < 2 || s.splatD < 2) return 0.0f;
    if ((int)s.splat.size() != s.splatW * s.splatD * n) return 0.0f;
    const float stepX = (float)s.terrain.width / (float)(s.splatW - 1);
    const float stepZ = (float)s.terrain.depth / (float)(s.splatD - 1);
    float gx = (x + (float)s.terrain.width * 0.5f) / stepX;
    float gz = (z + (float)s.terrain.depth * 0.5f) / stepZ;
    gx = std::clamp(gx, 0.0f, (float)s.splatW - 1.001f);
    gz = std::clamp(gz, 0.0f, (float)s.splatD - 1.001f);
    const int ix = (int)gx, iz = (int)gz;
    const float fx = gx - (float)ix, fz = gz - (float)iz;
    auto at = [&](int px, int pz) {
        return (float)s.splat[((size_t)pz * s.splatW + px) * n + layer] / 255.0f;
    };
    const float t = at(ix, iz) * (1.0f - fx) + at(ix + 1, iz) * fx;
    const float b = at(ix, iz + 1) * (1.0f - fx) + at(ix + 1, iz + 1) * fx;
    return t * (1.0f - fz) + b * fz;
}

// How much of ONE terrain material the ground actually SHOWS at a world
// position: -1 = the base material, 0..N-1 = a painted layer. The layers are
// drawn in order, each alpha-over the last (viewport.cpp `terrainLayerMeshes_`,
// buildTerrainChunk in templates.cpp), so a layer is covered by whatever was
// painted ON TOP of it - which is exactly the difference between "I painted
// grass here" and "you can see grass here". Scattering wants the latter: with
// the raw weight, rock painted over grass still scatters trees.
float terrainMaterialCoverage(const SceneData& s, int layer, float x, float z) {
    const int n = (int)s.terrainLayers.size();
    if (layer >= n) return 0.0f;
    float cov = layer < 0 ? 1.0f : terrainLayerWeight(s, layer, x, z);
    for (int j = std::max(0, layer + 1); j < n; ++j)
        cov *= 1.0f - terrainLayerWeight(s, j, x, z);
    return std::clamp(cov, 0.0f, 1.0f);
}

// --- scene objects ---------------------------------------------------------

bool isSolidBlocker(const SceneObject& o) {
    switch (o.type) {
        case PrimitiveType::SpawnPoint:
        case PrimitiveType::Player:
        case PrimitiveType::Emitter:
        case PrimitiveType::SoundEmitter:
        case PrimitiveType::PointLight:
        case PrimitiveType::Empty:
        case PrimitiveType::Decal:
        case PrimitiveType::Camera:
        case PrimitiveType::Scatter:
            return false;
        default: return true;
    }
}

// --- point cloud -----------------------------------------------------------

struct Points {
    std::vector<Instance> pts;
    std::map<std::string, std::vector<float>> attrs;

    void reserveAttr(const char* name) {
        auto& a = attrs[name];
        a.resize(pts.size(), 0.0f);
    }
    float* attr(const std::string& name) {
        auto it = attrs.find(name);
        return it == attrs.end() ? nullptr : it->second.data();
    }
    const float* attr(const std::string& name) const {
        auto it = attrs.find(name);
        return it == attrs.end() ? nullptr : it->second.data();
    }
    // Keeps only the entries flagged in `keep`, attributes included - the one
    // operation every filter node needs, and the one place where forgetting an
    // attribute would silently desynchronize the cloud.
    void compact(const std::vector<char>& keep) {
        size_t w = 0;
        for (size_t i = 0; i < pts.size(); ++i)
            if (keep[i]) pts[w++] = pts[i];
        pts.resize(w);
        for (auto& kv : attrs) {
            std::vector<float>& a = kv.second;
            size_t aw = 0;
            for (size_t i = 0; i < a.size() && i < keep.size(); ++i)
                if (keep[i]) a[aw++] = a[i];
            a.resize(aw);
        }
    }
    void append(const Points& o) {
        // Union of attribute names: a channel one side lacks reads as 0 rather
        // than dropping (GRAF-02: an unknown attribute must not break a node).
        const size_t base = pts.size();
        pts.insert(pts.end(), o.pts.begin(), o.pts.end());
        for (auto& kv : attrs) kv.second.resize(pts.size(), 0.0f);
        for (const auto& kv : o.attrs) {
            std::vector<float>& dst = attrs[kv.first];
            dst.resize(pts.size(), 0.0f);
            for (size_t i = 0; i < kv.second.size(); ++i)
                dst[base + i] = kv.second[i];
        }
    }
};

struct Value {
    ProcType type = ProcType::Points;
    std::shared_ptr<const Points> points;
    std::shared_ptr<const Mask> mask;
    std::shared_ptr<const Curve> curve;
    uint64_t hash = 0;
};

}  // namespace

struct Cache::Entry {
    uint64_t hash = 0;
    Value value;
};

void Cache::clear() { nodes_.clear(); }

float rand01(uint32_t seed, int nodeId, uint64_t key, int channel) {
    uint64_t h = mix64(0x51ed270b7f3ac1ULL ^ seed);
    h = hashCombine(h, (uint64_t)(uint32_t)nodeId);
    h = hashCombine(h, key);
    h = hashCombine(h, (uint64_t)(uint32_t)channel);
    return unitFromHash(h);
}

float Mask::sample(float x, float z) const {
    if (w < 1 || h < 1 || v.empty()) return 1.0f;
    if (sizeX <= 0.0f || sizeZ <= 0.0f) return v[0];
    float gx = (x - originX) / sizeX * (float)(w - 1);
    float gz = (z - originZ) / sizeZ * (float)(h - 1);
    gx = std::clamp(gx, 0.0f, (float)w - 1.001f);
    gz = std::clamp(gz, 0.0f, (float)h - 1.001f);
    const int ix = (int)gx, iz = (int)gz;
    const float fx = gx - (float)ix, fz = gz - (float)iz;
    const int ix1 = std::min(ix + 1, w - 1), iz1 = std::min(iz + 1, h - 1);
    const float t = v[(size_t)iz * w + ix] * (1.0f - fx) + v[(size_t)iz * w + ix1] * fx;
    const float b =
        v[(size_t)iz1 * w + ix] * (1.0f - fx) + v[(size_t)iz1 * w + ix1] * fx;
    return t * (1.0f - fz) + b * fz;
}

void Curve::at(float u, float out[3]) const {
    const int n = count();
    out[0] = out[1] = out[2] = 0.0f;
    if (n == 0) return;
    if (n == 1) {
        out[0] = pts[0];
        out[1] = pts[1];
        out[2] = pts[2];
        return;
    }
    const int segs = closed ? n : n - 1;
    float t = std::clamp(u, 0.0f, 1.0f) * (float)segs;
    int seg = (int)t;
    if (seg >= segs) seg = segs - 1;
    t -= (float)seg;
    auto cp = [&](int i) {
        if (closed)
            i = ((i % n) + n) % n;
        else
            i = std::clamp(i, 0, n - 1);
        return &pts[(size_t)i * 3];
    };
    const float* p0 = cp(seg - 1);
    const float* p1 = cp(seg);
    const float* p2 = cp(seg + 1);
    const float* p3 = cp(seg + 2);
    const float t2 = t * t, t3 = t2 * t;
    for (int a = 0; a < 3; ++a)
        out[a] = 0.5f * ((2.0f * p1[a]) + (-p0[a] + p2[a]) * t +
                         (2.0f * p0[a] - 5.0f * p1[a] + 4.0f * p2[a] - p3[a]) * t2 +
                         (-p0[a] + 3.0f * p1[a] - 3.0f * p2[a] + p3[a]) * t3);
}

void Curve::tangent(float u, float out[3]) const {
    const float e = 0.004f;
    float a[3], b[3];
    at(std::max(0.0f, u - e), a);
    at(std::min(1.0f, u + e), b);
    float d[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    const float len = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    const float inv = len > 1e-6f ? 1.0f / len : 0.0f;
    out[0] = d[0] * inv;
    out[1] = d[1] * inv;
    out[2] = d[2] * inv;
}

float Curve::distanceXZ(float x, float z) const {
    const int n = count();
    if (n == 0) return 1e9f;
    const int steps = std::max(8, (closed ? n : n - 1) * 8);
    float best = 1e9f;
    float prev[3];
    at(0.0f, prev);
    for (int i = 1; i <= steps; ++i) {
        float cur[3];
        at((float)i / (float)steps, cur);
        const float dx = cur[0] - prev[0], dz = cur[2] - prev[2];
        const float len2 = dx * dx + dz * dz;
        float t = 0.0f;
        if (len2 > 1e-9f)
            t = std::clamp(((x - prev[0]) * dx + (z - prev[2]) * dz) / len2, 0.0f,
                           1.0f);
        const float px = prev[0] + dx * t, pz = prev[2] + dz * t;
        const float d = std::sqrt((x - px) * (x - px) + (z - pz) * (z - pz));
        if (d < best) best = d;
        prev[0] = cur[0];
        prev[1] = cur[1];
        prev[2] = cur[2];
    }
    return best;
}

// ---------------------------------------------------------------------------
// Asset meshes
// ---------------------------------------------------------------------------

std::shared_ptr<const AssetMesh> assetMesh(const Project& p,
                                           const std::string& relPath) {
    static std::mutex mu;
    static std::map<std::string, std::shared_ptr<const AssetMesh>> cache;
    if (relPath.empty()) return nullptr;
    const std::string key = p.dir + "|" + relPath;
    {
        std::lock_guard<std::mutex> lock(mu);
        auto it = cache.find(key);
        if (it != cache.end()) return it->second;
    }
    auto out = std::make_shared<AssetMesh>();
    objparser::Model m;
    // filePath(), never a hand-joined "\\": outside Windows a backslash is an
    // ordinary filename character, so the join named a file that does not
    // exist and EVERY asset-scattering volume baked zero chunks in silence
    // (bakeVolume skips an instance whose mesh will not load).
    if (!objparser::load(p.filePath(relPath), m)) {
        std::lock_guard<std::mutex> lock(mu);
        cache[key] = nullptr;
        return nullptr;
    }
    for (const objparser::Submesh& sm : m.submeshes) {
        if (sm.verts.empty()) continue;
        AssetMesh::Part part;
        part.material = sm.material;
        part.verts = sm.verts;
        out->parts.push_back(std::move(part));
    }
    out->mtlLibs = m.mtlLibs;
    for (int a = 0; a < 3; ++a) {
        out->min[a] = m.min[a];
        out->max[a] = m.max[a];
    }
    std::lock_guard<std::mutex> lock(mu);
    cache[key] = out;
    return out;
}

// ---------------------------------------------------------------------------
// Evaluator
// ---------------------------------------------------------------------------

namespace {

// The scatter volume as the evaluator sees it: an oriented box (yaw only - a
// tilted scatter region has no meaning for ground cover) plus its
// axis-aligned footprint, which is the rectangle every mask covers.
struct Volume {
    float cx = 0, cy = 0, cz = 0;
    float sx = 1, sy = 1, sz = 1;  // full box size
    float yaw = 0.0f;              // radians
    float minX = 0, minZ = 0, sizeX = 0, sizeZ = 0;  // AABB footprint

    void localToWorld(float lx, float lz, float& wx, float& wz) const {
        const float c = std::cos(yaw), s = std::sin(yaw);
        wx = cx + lx * c + lz * s;
        wz = cz - lx * s + lz * c;
    }
    bool containsXZ(float wx, float wz) const {
        const float c = std::cos(yaw), s = std::sin(yaw);
        const float dx = wx - cx, dz = wz - cz;
        const float lx = dx * c - dz * s;
        const float lz = dx * s + dz * c;
        return std::fabs(lx) <= sx * 0.5f && std::fabs(lz) <= sz * 0.5f;
    }
};

Volume volumeOf(const SceneObject& o) {
    Volume v;
    v.cx = o.position[0];
    v.cy = o.position[1];
    v.cz = o.position[2];
    v.sx = std::max(0.01f, std::fabs(o.scale[0]));
    v.sy = std::max(0.01f, std::fabs(o.scale[1]));
    v.sz = std::max(0.01f, std::fabs(o.scale[2]));
    v.yaw = o.rotation[1] / kDeg;
    const float c = std::fabs(std::cos(v.yaw)), s = std::fabs(std::sin(v.yaw));
    const float ex = 0.5f * (v.sx * c + v.sz * s);
    const float ez = 0.5f * (v.sx * s + v.sz * c);
    v.minX = v.cx - ex;
    v.minZ = v.cz - ez;
    v.sizeX = 2.0f * ex;
    v.sizeZ = 2.0f * ez;
    return v;
}

struct Ctx {
    const Project& p;
    const SceneData& s;
    const SceneObject& volObj;
    Volume vol;
    const ProcGraph& g;
    // The seed every random draw derives from. Normally g.seed; the seed
    // SIMULATOR (Options::seedOverride) substitutes another one so the editor
    // can show what a runtime volume would build on a different boot without
    // touching the authored graph. It is deliberately a Ctx field rather than
    // reads of g.seed scattered over the node functions - one place to state
    // "which world is this".
    unsigned int seed = 1;
    const Options& opt;
    Cache* cache;
    Result* res;
    std::vector<std::string> assets;   // the graph's asset table
    std::vector<std::string> prefabs;  // ...and its prefab table
    int depth = 0;

    bool canceled() const {
        return opt.cancel && opt.cancel->load(std::memory_order_relaxed);
    }
};

// Terrain footprint of an object, in world XZ, plus its Y span. Models use
// their real mesh AABB; everything else the unit box - the same reduction
// navmesh and aobake make.
struct ObjBox {
    float minX, maxX, minZ, maxZ, bottom, top;
};

ObjBox objectBox(const Project& p, const SceneObject& o) {
    float mn[3] = {-0.5f, -0.5f, -0.5f}, mx[3] = {0.5f, 0.5f, 0.5f};
    if (o.type == PrimitiveType::Model && !o.modelPath.empty() &&
        !isAnimatedModelPath(o.modelPath)) {
        if (auto am = assetMesh(p, o.modelPath)) {
            for (int a = 0; a < 3; ++a) {
                mn[a] = am->min[a];
                mx[a] = am->max[a];
            }
        }
    }
    ObjBox b;
    const float cx = o.position[0] + 0.5f * (mn[0] + mx[0]) * o.scale[0];
    const float cy = o.position[1] + 0.5f * (mn[1] + mx[1]) * o.scale[1];
    const float cz = o.position[2] + 0.5f * (mn[2] + mx[2]) * o.scale[2];
    const float ex = 0.5f * (mx[0] - mn[0]) * std::fabs(o.scale[0]);
    const float ey = 0.5f * (mx[1] - mn[1]) * std::fabs(o.scale[1]);
    const float ez = 0.5f * (mx[2] - mn[2]) * std::fabs(o.scale[2]);
    b.minX = cx - ex;
    b.maxX = cx + ex;
    b.minZ = cz - ez;
    b.maxZ = cz + ez;
    b.bottom = cy - ey;
    b.top = cy + ey;
    return b;
}

float distanceToBoxXZ(const ObjBox& b, float x, float z) {
    const float dx = std::max(std::max(b.minX - x, 0.0f), x - b.maxX);
    const float dz = std::max(std::max(b.minZ - z, 0.0f), z - b.maxZ);
    return std::sqrt(dx * dx + dz * dz);
}

// World-space triangles of one scene object (for surface scattering onto a
// placed object): its mesh or its unit primitive, transformed. 9 floats per
// triangle (three positions) plus a parallel normal per triangle.
struct TriSoup {
    std::vector<float> pos;   // 9 per triangle
    std::vector<float> nrm;   // 3 per triangle
    std::vector<float> cdf;   // cumulative area per triangle
    float area = 0.0f;
};

void rotateVec(const float in[3], const float rotDeg[3], float out[3]) {
    const float rx = rotDeg[0] / kDeg, ry = rotDeg[1] / kDeg, rz = rotDeg[2] / kDeg;
    float x = in[0], y = in[1], z = in[2];
    float t = y * std::cos(rx) - z * std::sin(rx);
    z = y * std::sin(rx) + z * std::cos(rx);
    y = t;
    t = x * std::cos(ry) + z * std::sin(ry);
    z = -x * std::sin(ry) + z * std::cos(ry);
    x = t;
    t = x * std::cos(rz) - y * std::sin(rz);
    y = x * std::sin(rz) + y * std::cos(rz);
    x = t;
    out[0] = x;
    out[1] = y;
    out[2] = z;
}

TriSoup objectTriangles(const Project& p, const SceneObject& o) {
    TriSoup soup;
    std::vector<float> src;  // 8 floats/vertex
    if (o.type == PrimitiveType::Model && !o.modelPath.empty() &&
        !isAnimatedModelPath(o.modelPath)) {
        if (auto am = assetMesh(p, o.modelPath))
            for (const AssetMesh::Part& part : am->parts)
                src.insert(src.end(), part.verts.begin(), part.verts.end());
    } else {
        switch (o.type) {
            case PrimitiveType::Sphere:
                src = primmesh::unitSphere(clampPrimDetail(o.type, o.primDetail));
                break;
            case PrimitiveType::Cylinder:
                src = primmesh::unitCylinder(clampPrimDetail(o.type, o.primDetail),
                                             o.primRings);
                break;
            case PrimitiveType::Cone:
                src = primmesh::unitCone(clampPrimDetail(o.type, o.primDetail));
                break;
            case PrimitiveType::Plane: src = primmesh::unitPlane(); break;
            case PrimitiveType::Box:
            case PrimitiveType::SavePoint:
                src = primmesh::unitBox(clampPrimDetail(o.type, o.primDetail));
                break;
            default: return soup;
        }
    }
    const size_t tris = src.size() / 24;
    soup.pos.reserve(tris * 9);
    soup.nrm.reserve(tris * 3);
    soup.cdf.reserve(tris);
    for (size_t t = 0; t < tris; ++t) {
        float w[9];
        for (int c = 0; c < 3; ++c) {
            const float* v = &src[(t * 3 + c) * 8];
            float scaled[3] = {v[0] * o.scale[0], v[1] * o.scale[1],
                               v[2] * o.scale[2]};
            float rot[3];
            rotateVec(scaled, o.rotation, rot);
            w[c * 3 + 0] = rot[0] + o.position[0];
            w[c * 3 + 1] = rot[1] + o.position[1];
            w[c * 3 + 2] = rot[2] + o.position[2];
        }
        const float e1[3] = {w[3] - w[0], w[4] - w[1], w[5] - w[2]};
        const float e2[3] = {w[6] - w[0], w[7] - w[1], w[8] - w[2]};
        float n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2],
                      e1[0] * e2[1] - e1[1] * e2[0]};
        const float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        const float triArea = 0.5f * len;
        const float inv = len > 1e-9f ? 1.0f / len : 0.0f;
        soup.pos.insert(soup.pos.end(), w, w + 9);
        soup.nrm.push_back(n[0] * inv);
        soup.nrm.push_back(n[1] * inv);
        soup.nrm.push_back(n[2] * inv);
        soup.area += triArea;
        soup.cdf.push_back(soup.area);
    }
    return soup;
}

// The scatter volume's own footprint area, used to turn "density per 100
// square units" into a count.
float footprintArea(const Volume& v) { return v.sx * v.sz; }

Value evalNode(Ctx& ctx, int nodeId);

// The value on an input pin (empty Value when unconnected).
Value input(Ctx& ctx, const ProcNode& n, int pin) {
    const ProcLink* l = procgraph::linkTo(ctx.g, n.id, pin);
    if (!l) return Value{};
    return evalNode(ctx, l->fromNode);
}

Mask makeMask(const Volume& v, int res) {
    Mask m;
    m.w = m.h = res;
    m.originX = v.minX;
    m.originZ = v.minZ;
    m.sizeX = v.sizeX;
    m.sizeZ = v.sizeZ;
    m.v.assign((size_t)res * res, 0.0f);
    return m;
}

// Mask resolution: fine enough that the smallest feature the node can produce
// still has a few texels, capped so a huge volume cannot allocate megabytes.
int maskRes(const Volume& v, float featureSize) {
    const float span = std::max(v.sizeX, v.sizeZ);
    const float want = featureSize > 0.1f ? span / (featureSize * 0.25f) : 128.0f;
    return std::clamp((int)std::lround(want), 16, 256);
}

// --- generators ------------------------------------------------------------

void writeSurfaceAttrs(Points& pts, size_t i, float nx, float ny, float nz) {
    pts.attrs[procattr::kNormalX][i] = nx;
    pts.attrs[procattr::kNormalY][i] = ny;
    pts.attrs[procattr::kNormalZ][i] = nz;
    pts.attrs[procattr::kSlope][i] = std::acos(std::clamp(ny, -1.0f, 1.0f)) * kDeg;
    pts.attrs[procattr::kHeight][i] = pts.pts[i].pos[1];
}

void ensureBaseAttrs(Points& pts) {
    for (const char* a : {procattr::kNormalX, procattr::kNormalY,
                          procattr::kNormalZ, procattr::kSlope,
                          procattr::kHeight, procattr::kRandom, procattr::kSize})
        pts.attrs[a].resize(pts.pts.size(), 0.0f);
}

Points genSurface(Ctx& ctx, const ProcNode& n, const Value& maskIn) {
    Points out;
    const float density = procgraph::num(n, "density");
    const int cap = std::max(1, procgraph::inum(n, "max"));
    const std::string& target = procgraph::str(n, "target");

    const SceneObject* tgt = nullptr;
    if (!target.empty()) {
        for (const SceneObject& o : ctx.s.objects)
            if (o.name == target) {
                tgt = &o;
                break;
            }
        if (!tgt)
            ctx.res->warnings.push_back("Scatter on Surface: no object named '" +
                                        target + "' - using the terrain");
    }

    TriSoup soup;
    float area;
    if (tgt) {
        soup = objectTriangles(ctx.p, *tgt);
        area = soup.area;
        if (soup.cdf.empty()) {
            ctx.res->warnings.push_back(
                "Scatter on Surface: '" + target + "' has no surface to sample");
            return out;
        }
    } else {
        area = footprintArea(ctx.vol);
    }

    int count = (int)std::lround((double)density * (double)area / 100.0);
    count = std::clamp(count, 0, cap);
    const int wanted = count;
    count = (int)std::lround((double)count * std::clamp(ctx.opt.fraction, 0.0f, 1.0f));
    if (wanted > 0 && count < 1) count = 1;
    ctx.res->candidates += count;

    const float halfW = (float)ctx.s.terrain.width * 0.5f;
    const float halfD = (float)ctx.s.terrain.depth * 0.5f;
    const Mask* mask = maskIn.mask.get();

    out.pts.reserve((size_t)count);
    for (int i = 0; i < count; ++i) {
        if ((i & 1023) == 0 && ctx.canceled()) break;
        const uint64_t key = mix64(((uint64_t)(uint32_t)n.id << 40) ^ (uint64_t)i);
        Instance inst;
        float nx = 0.0f, ny = 1.0f, nz = 0.0f;
        if (tgt) {
            // Area-proportional triangle pick, so a densely tessellated region
            // does not collect more points than a coarse one of equal area.
            const float u = halton(i, 2) * soup.area;
            size_t t = (size_t)(std::lower_bound(soup.cdf.begin(), soup.cdf.end(), u) -
                                soup.cdf.begin());
            if (t >= soup.nrm.size() / 3) t = soup.nrm.size() / 3 - 1;
            float b1 = halton(i, 3), b2 = halton(i, 5);
            if (b1 + b2 > 1.0f) {
                b1 = 1.0f - b1;
                b2 = 1.0f - b2;
            }
            const float* w = &soup.pos[t * 9];
            for (int a = 0; a < 3; ++a)
                inst.pos[a] = w[a] + (w[3 + a] - w[a]) * b1 + (w[6 + a] - w[a]) * b2;
            nx = soup.nrm[t * 3];
            ny = soup.nrm[t * 3 + 1];
            nz = soup.nrm[t * 3 + 2];
            if (!ctx.vol.containsXZ(inst.pos[0], inst.pos[2])) continue;
            if (inst.pos[1] < ctx.vol.cy - ctx.vol.sy * 0.5f ||
                inst.pos[1] > ctx.vol.cy + ctx.vol.sy * 0.5f)
                continue;
        } else {
            const float lx = (halton(i, 2) - 0.5f) * ctx.vol.sx;
            const float lz = (halton(i, 3) - 0.5f) * ctx.vol.sz;
            float wx, wz;
            ctx.vol.localToWorld(lx, lz, wx, wz);
            if (wx < -halfW || wx > halfW || wz < -halfD || wz > halfD)
                continue;  // off the map: there is no surface there
            inst.pos[0] = wx;
            inst.pos[2] = wz;
            inst.pos[1] = terrainHeight(ctx.s, wx, wz);
            float nrm[3];
            terrainNormal(ctx.s, wx, wz, nrm);
            nx = nrm[0];
            ny = nrm[1];
            nz = nrm[2];
        }
        // The mask acts on the DENSITY here (rejection at generation), which is
        // what makes "half the density" mean half the points in the right
        // places instead of a uniform carpet thinned later.
        float maskV = 1.0f;
        if (mask) {
            maskV = std::clamp(mask->sample(inst.pos[0], inst.pos[2]), 0.0f, 1.0f);
            if (rand01(ctx.seed, n.id, key, 0) >= maskV) continue;
        }
        // Off the surface it landed on - the terrain under the point, or the
        // sampled object's triangle - so the lift FOLLOWS the ground instead of
        // flattening it. Applied after the volume's Y clip on purpose: the clip
        // asks whether the surface is inside the region, not where the point
        // ends up hovering.
        inst.pos[1] += procgraph::num(n, "lift");
        inst.key = key;
        out.pts.push_back(inst);
        ensureBaseAttrs(out);
        const size_t idx = out.pts.size() - 1;
        writeSurfaceAttrs(out, idx, nx, ny, nz);
        out.attrs[procattr::kRandom][idx] = rand01(ctx.seed, n.id, key, 1);
        out.attrs[procattr::kSize][idx] = 1.0f;
        if (mask) {
            out.attrs[procattr::kMask].resize(out.pts.size(), 0.0f);
            out.attrs[procattr::kMask][idx] = maskV;
        }
    }
    return out;
}

Points genGrid(Ctx& ctx, const ProcNode& n, const Value& maskIn) {
    Points out;
    const float spacing = std::max(0.1f, procgraph::num(n, "spacing"));
    const float jitter = std::clamp(procgraph::num(n, "jitter"), 0.0f, 1.0f);
    const bool snap = procgraph::flag(n, "snap");
    const int nx = std::max(1, (int)std::floor(ctx.vol.sx / spacing));
    const int nz = std::max(1, (int)std::floor(ctx.vol.sz / spacing));
    if ((int64_t)nx * nz > 400000) {
        ctx.res->warnings.push_back(
            "Scatter on Grid: spacing too small for this volume - clamped");
        return out;
    }
    const float halfW = (float)ctx.s.terrain.width * 0.5f;
    const float halfD = (float)ctx.s.terrain.depth * 0.5f;
    const Mask* mask = maskIn.mask.get();
    const float frac = std::clamp(ctx.opt.fraction, 0.0f, 1.0f);
    // Levels stack the whole lattice upward: the 2D floor plan becomes a 3D
    // grid without a second node, and level 0 keeps the exact positions a
    // single-level grid produced (so raising Levels ADDS content instead of
    // moving what is there).
    const int levels = std::max(1, procgraph::inum(n, "levels"));
    const float levelStep = procgraph::num(n, "levelstep");
    const float lift = procgraph::num(n, "lift");
    ctx.res->candidates += nx * nz * levels;
    for (int iy = 0; iy < levels; ++iy) {
        if (ctx.canceled()) break;
        for (int iz = 0; iz < nz; ++iz) {
            if (ctx.canceled()) break;
            for (int ix = 0; ix < nx; ++ix) {
                const uint64_t key = mix64(((uint64_t)(uint32_t)n.id << 40) ^
                                           ((uint64_t)(uint32_t)iy << 32) ^
                                           ((uint64_t)(uint32_t)iz << 20) ^
                                           (uint64_t)(uint32_t)ix);
                // Progressive preview thins the lattice by a hashed coin flip,
                // not by cutting rows - a coarse pass then covers the whole area.
                if (frac < 1.0f && rand01(ctx.seed, n.id, key, 7) >= frac)
                    continue;
                const float jx =
                    (rand01(ctx.seed, n.id, key, 2) - 0.5f) * jitter * spacing;
                const float jz =
                    (rand01(ctx.seed, n.id, key, 3) - 0.5f) * jitter * spacing;
                const float lx =
                    -ctx.vol.sx * 0.5f + ((float)ix + 0.5f) * spacing + jx;
                const float lz =
                    -ctx.vol.sz * 0.5f + ((float)iz + 0.5f) * spacing + jz;
                float wx, wz;
                ctx.vol.localToWorld(lx, lz, wx, wz);
                if (wx < -halfW || wx > halfW || wz < -halfD || wz > halfD)
                    continue;
                float maskV = 1.0f;
                if (mask) {
                    maskV = std::clamp(mask->sample(wx, wz), 0.0f, 1.0f);
                    if (rand01(ctx.seed, n.id, key, 0) >= maskV) continue;
                }
                Instance inst;
                inst.pos[0] = wx;
                inst.pos[2] = wz;
                inst.pos[1] = snap ? terrainHeight(ctx.s, wx, wz) : ctx.vol.cy;
                inst.pos[1] += lift + (float)iy * levelStep;
                inst.key = key;
                out.pts.push_back(inst);
                ensureBaseAttrs(out);
                const size_t idx = out.pts.size() - 1;
                float nrm[3] = {0.0f, 1.0f, 0.0f};
                if (snap) terrainNormal(ctx.s, wx, wz, nrm);
                writeSurfaceAttrs(out, idx, nrm[0], nrm[1], nrm[2]);
                out.attrs[procattr::kRandom][idx] =
                    rand01(ctx.seed, n.id, key, 1);
                out.attrs[procattr::kSize][idx] = 1.0f;
                if (mask) {
                    out.attrs[procattr::kMask].resize(out.pts.size(), 0.0f);
                    out.attrs[procattr::kMask][idx] = maskV;
                }
            }
        }
    }
    return out;
}

// --- the block world -------------------------------------------------------
// One node, because the whole value of it is in what never leaves: a solid
// column field's interior is invisible, so the generator walks the field and
// emits only blocks with an exposed face, each carrying the mask of WHICH
// faces are exposed. The identical formula runs on the console for a runtime
// volume (procrt emits it), which is why the height function is spelled out
// here rather than reusing the Noise Mask node's parameter set - the two must
// stay a twin, and a twin of one small function is maintainable.

// Column height in BLOCKS at lattice cell (ix, iz). Deterministic in the graph
// seed; procrt::emitBlockHeight is the generated-C++ twin of this - change one
// and change the other, or the editor preview and the console disagree about
// where the ground is.
int blockColumnHeight(uint32_t seed, int nodeId, int ix, int iz, float blockSz,
                      float noiseScale, int octaves, float relief, int levels,
                      int floorLayers, const Mask* mask, float originX,
                      float originZ) {
    const float wx = originX + ((float)ix + 0.5f) * blockSz;
    const float wz = originZ + ((float)iz + 0.5f) * blockSz;
    float v;
    if (mask) {
        v = std::clamp(mask->sample(wx, wz), 0.0f, 1.0f);
    } else {
        v = std::clamp(noiseAt(0, wx / noiseScale, wz / noiseScale, octaves,
                               seed ^ (uint32_t)(nodeId * 2654435761u)),
                       0.0f, 1.0f);
    }
    const float span = (float)levels * std::clamp(relief, 0.0f, 1.0f);
    int h = floorLayers + (int)std::lround(v * span);
    return std::clamp(h, 0, levels);
}

Points genBlocks(Ctx& ctx, const ProcNode& n, const Value& maskIn) {
    Points out;
    const float blockSz = std::max(0.25f, procgraph::num(n, "block"));
    const int levels = std::clamp(procgraph::inum(n, "levels"), 1, 32);
    const int floorLayers = std::clamp(procgraph::inum(n, "floor"), 0, levels);
    const float noiseScale = std::max(2.0f, procgraph::num(n, "scale"));
    const int octaves = std::clamp(procgraph::inum(n, "octaves"), 1, 6);
    const float relief = std::clamp(procgraph::num(n, "relief"), 0.0f, 1.0f);
    const int emitDepth = std::clamp(procgraph::inum(n, "depth"), 1, 32);
    const float baseY = procgraph::num(n, "base");
    const Mask* mask = maskIn.mask.get();

    // The lattice is anchored on the volume's axis-aligned footprint corner
    // (not its centre), so moving the volume by a whole block leaves the world
    // exactly where it was and blocks never land half-way between cells.
    const int nx = std::max(1, (int)std::floor(ctx.vol.sizeX / blockSz));
    const int nz = std::max(1, (int)std::floor(ctx.vol.sizeZ / blockSz));
    if ((int64_t)nx * nz > 65536) {
        ctx.res->warnings.push_back(
            "Blocks Fill: " + std::to_string(nx) + "x" + std::to_string(nz) +
            " columns is past the 65536 the collision field can hold - raise "
            "Block size");
        return out;
    }
    const float ox = ctx.vol.minX, oz = ctx.vol.minZ;

    // Heights first: the visibility test needs the four neighbours, so the
    // field is built once instead of re-derived per face.
    std::vector<short> h((size_t)nx * nz, 0);
    for (int iz = 0; iz < nz; ++iz)
        for (int ix = 0; ix < nx; ++ix)
            h[(size_t)iz * nx + ix] =
                (short)blockColumnHeight(ctx.seed, n.id, ix, iz, blockSz,
                                         noiseScale, octaves, relief, levels,
                                         floorLayers, mask, ox, oz);

    auto heightAt = [&](int ix, int iz) -> int {
        // Off the lattice reads as "open", so the world's rim shows its side
        // faces instead of ending in a seam.
        if (ix < 0 || iz < 0 || ix >= nx || iz >= nz) return 0;
        return h[(size_t)iz * nx + ix];
    };

    const float frac = std::clamp(ctx.opt.fraction, 0.0f, 1.0f);
    for (int iz = 0; iz < nz; ++iz) {
        if (ctx.canceled()) break;
        for (int ix = 0; ix < nx; ++ix) {
            const int top = heightAt(ix, iz);
            if (top <= 0) continue;
            const int hxp = heightAt(ix + 1, iz), hxm = heightAt(ix - 1, iz);
            const int hzp = heightAt(ix, iz + 1), hzm = heightAt(ix, iz - 1);
            const int lowest = std::max(0, top - emitDepth);
            for (int iy = top - 1; iy >= lowest; --iy) {
                // Face mask: a face is drawn only where the neighbouring column
                // is not tall enough to cover this level.
                unsigned char faces = 0;
                if (iy >= hxp) faces |= 1;   // +X
                if (iy >= hxm) faces |= 2;   // -X
                if (iy == top - 1) faces |= 4;  // +Y (nothing above in a column)
                if (iy == 0) faces |= 8;     // -Y, only the world's underside
                if (iy >= hzp) faces |= 16;  // +Z
                if (iy >= hzm) faces |= 32;  // -Z
                ++ctx.res->candidates;
                if (faces == 0) continue;  // buried: never leaves this node
                const uint64_t key = mix64(((uint64_t)(uint32_t)n.id << 44) ^
                                           ((uint64_t)(uint32_t)iy << 34) ^
                                           ((uint64_t)(uint32_t)iz << 17) ^
                                           (uint64_t)(uint32_t)ix);
                if (frac < 1.0f && rand01(ctx.seed, n.id, key, 7) >= frac)
                    continue;
                Instance inst;
                inst.pos[0] = ox + ((float)ix + 0.5f) * blockSz;
                inst.pos[1] = baseY + ((float)iy + 0.5f) * blockSz;
                inst.pos[2] = oz + ((float)iz + 0.5f) * blockSz;
                inst.scale = blockSz;  // the asset is authored as a UNIT cube
                inst.faces = faces;
                inst.key = key;
                out.pts.push_back(inst);
                ensureBaseAttrs(out);
                const size_t idx = out.pts.size() - 1;
                writeSurfaceAttrs(out, idx, 0.0f, 1.0f, 0.0f);
                out.attrs[procattr::kRandom][idx] =
                    rand01(ctx.seed, n.id, key, 1);
                out.attrs[procattr::kSize][idx] = 1.0f;
                out.attrs[procattr::kHeight].resize(out.pts.size(), 0.0f);
                out.attrs[procattr::kHeight][idx] = inst.pos[1];
                out.attrs[procattr::kFaces].resize(out.pts.size(), 0.0f);
                out.attrs[procattr::kFaces][idx] = (float)faces;
                out.attrs[procattr::kDepth].resize(out.pts.size(), 0.0f);
                out.attrs[procattr::kDepth][idx] = (float)(top - 1 - iy);
            }
        }
    }
    return out;
}

Points genVolume(Ctx& ctx, const ProcNode& n) {
    Points out;
    int count = std::max(0, procgraph::inum(n, "count"));
    ctx.res->candidates += count;
    count = (int)std::lround((double)count * std::clamp(ctx.opt.fraction, 0.0f, 1.0f));
    out.pts.reserve((size_t)count);
    for (int i = 0; i < count; ++i) {
        if ((i & 1023) == 0 && ctx.canceled()) break;
        const uint64_t key = mix64(((uint64_t)(uint32_t)n.id << 40) ^ (uint64_t)i);
        const float lx = (halton(i, 2) - 0.5f) * ctx.vol.sx;
        const float lz = (halton(i, 3) - 0.5f) * ctx.vol.sz;
        const float ly = (halton(i, 5) - 0.5f) * ctx.vol.sy;
        float wx, wz;
        ctx.vol.localToWorld(lx, lz, wx, wz);
        Instance inst;
        inst.pos[0] = wx;
        inst.pos[1] = ctx.vol.cy + ly;
        inst.pos[2] = wz;
        inst.key = key;
        out.pts.push_back(inst);
        ensureBaseAttrs(out);
        const size_t idx = out.pts.size() - 1;
        writeSurfaceAttrs(out, idx, 0.0f, 1.0f, 0.0f);
        out.attrs[procattr::kRandom][idx] = rand01(ctx.seed, n.id, key, 1);
        out.attrs[procattr::kSize][idx] = 1.0f;
    }
    return out;
}

Points genCurvePoints(Ctx& ctx, const ProcNode& n, const Value& curveIn) {
    Points out;
    const Curve* c = curveIn.curve.get();
    if (!c || c->count() < 2) return out;
    // Arc length by dense sampling: the placement is by distance, not by
    // parameter, or instances would bunch up on tight bends.
    const int steps = std::max(32, c->count() * 24);
    std::vector<float> uAt(steps + 1), lenAt(steps + 1);
    float prev[3];
    c->at(0.0f, prev);
    lenAt[0] = 0.0f;
    uAt[0] = 0.0f;
    for (int i = 1; i <= steps; ++i) {
        float cur[3];
        const float u = (float)i / (float)steps;
        c->at(u, cur);
        const float d = std::sqrt((cur[0] - prev[0]) * (cur[0] - prev[0]) +
                                  (cur[1] - prev[1]) * (cur[1] - prev[1]) +
                                  (cur[2] - prev[2]) * (cur[2] - prev[2]));
        lenAt[i] = lenAt[i - 1] + d;
        uAt[i] = u;
        prev[0] = cur[0];
        prev[1] = cur[1];
        prev[2] = cur[2];
    }
    const float total = lenAt[steps];
    if (total < 1e-4f) return out;

    const int fixedCount = std::max(0, procgraph::inum(n, "count"));
    const float spacing = std::max(0.05f, procgraph::num(n, "spacing"));
    const float side = procgraph::num(n, "offset");
    const float jitter = std::clamp(procgraph::num(n, "jitter"), 0.0f, 1.0f);
    const bool align = procgraph::flag(n, "align");
    const bool snap = procgraph::flag(n, "snap");
    int count = fixedCount > 0 ? fixedCount : (int)std::floor(total / spacing) + 1;
    count = std::min(count, 20000);
    ctx.res->candidates += count;
    const float frac = std::clamp(ctx.opt.fraction, 0.0f, 1.0f);

    for (int i = 0; i < count; ++i) {
        const uint64_t key = mix64(((uint64_t)(uint32_t)n.id << 40) ^ (uint64_t)i);
        if (frac < 1.0f && rand01(ctx.seed, n.id, key, 7) >= frac) continue;
        float dist;
        if (fixedCount > 0)
            dist = total * ((float)i / (float)std::max(1, count - 1));
        else
            dist = (float)i * spacing;
        if (jitter > 0.0f)
            dist += (rand01(ctx.seed, n.id, key, 4) - 0.5f) * jitter * spacing;
        dist = std::clamp(dist, 0.0f, total);
        // Distance -> parameter, by walking the table.
        int lo = 0, hi = steps;
        while (lo + 1 < hi) {
            const int mid = (lo + hi) / 2;
            if (lenAt[mid] <= dist)
                lo = mid;
            else
                hi = mid;
        }
        const float segLen = lenAt[hi] - lenAt[lo];
        const float t = segLen > 1e-6f ? (dist - lenAt[lo]) / segLen : 0.0f;
        const float u = uAt[lo] + (uAt[hi] - uAt[lo]) * t;
        float pos[3], tan[3];
        c->at(u, pos);
        c->tangent(u, tan);
        if (side != 0.0f) {
            // Sideways in the XZ plane: the curve's right-hand normal.
            const float rx = tan[2], rz = -tan[0];
            const float len = std::sqrt(rx * rx + rz * rz);
            if (len > 1e-6f) {
                pos[0] += rx / len * side;
                pos[2] += rz / len * side;
            }
        }
        Instance inst;
        inst.pos[0] = pos[0];
        inst.pos[1] = snap ? terrainHeight(ctx.s, pos[0], pos[2]) : pos[1];
        inst.pos[2] = pos[2];
        if (align) inst.rot[1] = std::atan2(tan[0], tan[2]) * kDeg;
        inst.key = key;
        out.pts.push_back(inst);
        ensureBaseAttrs(out);
        const size_t idx = out.pts.size() - 1;
        float nrm[3] = {0.0f, 1.0f, 0.0f};
        if (snap) terrainNormal(ctx.s, inst.pos[0], inst.pos[2], nrm);
        writeSurfaceAttrs(out, idx, nrm[0], nrm[1], nrm[2]);
        out.attrs[procattr::kRandom][idx] = rand01(ctx.seed, n.id, key, 1);
        out.attrs[procattr::kSize][idx] = 1.0f;
        out.attrs[procattr::kCurveT].resize(out.pts.size(), 0.0f);
        out.attrs[procattr::kCurveT][idx] = u;
    }
    return out;
}

// --- the analytic side: one placed point, and exact repetition of it --------

Points genPoint(Ctx& ctx, const ProcNode& n) {
    Points out;
    const std::string& target = procgraph::str(n, "target");
    float ax = ctx.vol.cx, ay = ctx.vol.cy, az = ctx.vol.cz;
    if (!target.empty()) {
        const SceneObject* tgt = nullptr;
        for (const SceneObject& o : ctx.s.objects)
            if (o.name == target) {
                tgt = &o;
                break;
            }
        if (tgt) {
            ax = tgt->position[0];
            ay = tgt->position[1];
            az = tgt->position[2];
        } else {
            ctx.res->warnings.push_back("Single Point: no object named '" +
                                        target + "' - using the volume centre");
        }
    }
    const bool snap = procgraph::flag(n, "snap");
    Instance inst;
    inst.pos[0] = ax + procgraph::num(n, "x");
    inst.pos[2] = az + procgraph::num(n, "z");
    inst.pos[1] = (snap ? terrainHeight(ctx.s, inst.pos[0], inst.pos[2]) : ay) +
                  procgraph::num(n, "y");
    inst.key = mix64(((uint64_t)(uint32_t)n.id << 40) ^ 1ULL);
    out.pts.push_back(inst);
    ctx.res->candidates += 1;
    ensureBaseAttrs(out);
    float nrm[3] = {0.0f, 1.0f, 0.0f};
    if (snap) terrainNormal(ctx.s, inst.pos[0], inst.pos[2], nrm);
    writeSurfaceAttrs(out, 0, nrm[0], nrm[1], nrm[2]);
    out.attrs[procattr::kRandom][0] = rand01(ctx.seed, n.id, inst.key, 1);
    out.attrs[procattr::kSize][0] = 1.0f;
    return out;
}

// Copy `i` of source point `srcKey`: a fresh identity derived from BOTH, so a
// manual edit binds to "copy 7 of that point" and survives everything upstream
// that leaves the source point alone. The odd multiplier keeps copy indices
// from colliding with a generator's own ordinals.
uint64_t copyKey(int nodeId, uint64_t srcKey, int i) {
    return mix64(srcKey ^ ((uint64_t)(uint32_t)nodeId << 40) ^
                 ((uint64_t)(uint32_t)i * 0x9E3779B97F4A7C15ULL));
}

// Shared tail of both repeat nodes: appends one copy and its attributes.
void pushCopy(Points& out, const Points& in, size_t src, const Instance& inst) {
    out.pts.push_back(inst);
    for (const auto& kv : in.attrs) {
        std::vector<float>& a = out.attrs[kv.first];
        a.resize(out.pts.size(), 0.0f);
        a[out.pts.size() - 1] = src < kv.second.size() ? kv.second[src] : 0.0f;
    }
    for (auto& kv : out.attrs) kv.second.resize(out.pts.size(), 0.0f);
}

// A repeat node multiplies its input, so it is the one place in the graph where
// a typo (count 2000 on a 20 000-point cloud) turns into gigabytes. The cap is
// reported rather than silently applied - a preview that quietly stops at a
// round number is how people conclude the tool is broken.
constexpr size_t kMaxRepeatOut = 200000;

Points arrayCopies(Ctx& ctx, const ProcNode& n, const Points& in) {
    Points out;
    const int count = std::clamp(procgraph::inum(n, "count"), 1, 2000);
    const float dx = procgraph::num(n, "dx");
    const float dy = procgraph::num(n, "dy");
    const float dz = procgraph::num(n, "dz");
    const float yaw = procgraph::num(n, "yaw");
    const float scale = std::max(0.1f, procgraph::num(n, "scale"));
    const bool local = procgraph::flag(n, "local");
    const bool snap = procgraph::flag(n, "snap");
    ctx.res->candidates += (int)in.pts.size() * (count - 1);
    out.pts.reserve(in.pts.size() * (size_t)count);
    bool capped = false;
    for (size_t s = 0; s < in.pts.size(); ++s) {
        if ((s & 255) == 0 && ctx.canceled()) break;
        const Instance& src = in.pts[s];
        // Point space: the step rides the source point's own yaw, so a row of
        // posts follows the fence it was scattered along instead of world X.
        const float a = local ? src.rot[1] / kDeg : 0.0f;
        const float ca = std::cos(a), sa = std::sin(a);
        for (int i = 0; i < count; ++i) {
            if (out.pts.size() >= kMaxRepeatOut) {
                capped = true;
                break;
            }
            Instance inst = src;
            const float ox = dx * (float)i, oy = dy * (float)i, oz = dz * (float)i;
            inst.pos[0] = src.pos[0] + (local ? ox * ca + oz * sa : ox);
            inst.pos[2] = src.pos[2] + (local ? -ox * sa + oz * ca : oz);
            inst.pos[1] = (snap ? terrainHeight(ctx.s, inst.pos[0], inst.pos[2])
                                : src.pos[1]) +
                          oy;
            inst.rot[1] = src.rot[1] + yaw * (float)i;
            inst.scale = src.scale * std::pow(scale, (float)i);
            inst.key = i == 0 ? src.key : copyKey(n.id, src.key, i);
            pushCopy(out, in, s, inst);
        }
        if (capped) break;
    }
    if (capped)
        ctx.res->warnings.push_back(
            "Array: stopped at " + std::to_string(kMaxRepeatOut) +
            " points - lower the count or thin the input first");
    return out;
}

Points radialCopies(Ctx& ctx, const ProcNode& n, const Points& in) {
    Points out;
    const int count = std::clamp(procgraph::inum(n, "count"), 1, 2000);
    const float radius = std::max(0.0f, procgraph::num(n, "radius"));
    const int axis = std::clamp(procgraph::inum(n, "axis"), 0, 2);
    const float start = procgraph::num(n, "start");
    const float sweep = std::clamp(procgraph::num(n, "sweep"), 1.0f, 360.0f);
    const bool face = procgraph::flag(n, "face");
    const bool snap = procgraph::flag(n, "snap");
    // A full circle must not put the last copy on top of the first, an arc
    // must reach its end angle - the two need different denominators.
    const bool full = sweep >= 359.9f;
    const float stepDeg =
        count > 1 ? sweep / (float)(full ? count : count - 1) : 0.0f;
    ctx.res->candidates += (int)in.pts.size() * (count - 1);
    out.pts.reserve(in.pts.size() * (size_t)count);
    bool capped = false;
    for (size_t s = 0; s < in.pts.size(); ++s) {
        if ((s & 255) == 0 && ctx.canceled()) break;
        const Instance& src = in.pts[s];
        for (int i = 0; i < count; ++i) {
            if (out.pts.size() >= kMaxRepeatOut) {
                capped = true;
                break;
            }
            const float deg = start + stepDeg * (float)i;
            const float rad = deg / kDeg;
            const float c = std::cos(rad), sn = std::sin(rad);
            Instance inst = src;
            switch (axis) {
                case 1:  // around X: the ring stands in the YZ plane
                    inst.pos[1] = src.pos[1] + radius * c;
                    inst.pos[2] = src.pos[2] + radius * sn;
                    inst.rot[0] = src.rot[0] + (face ? deg : 0.0f);
                    break;
                case 2:  // around Z: the ring stands in the XY plane
                    inst.pos[0] = src.pos[0] + radius * c;
                    inst.pos[1] = src.pos[1] + radius * sn;
                    inst.rot[2] = src.rot[2] + (face ? deg : 0.0f);
                    break;
                default:  // around Y: the flat ring on the ground
                    inst.pos[0] = src.pos[0] + radius * sn;
                    inst.pos[2] = src.pos[2] + radius * c;
                    if (face) inst.rot[1] = src.rot[1] + deg;
                    break;
            }
            if (snap)
                inst.pos[1] = terrainHeight(ctx.s, inst.pos[0], inst.pos[2]);
            inst.key = copyKey(n.id, src.key, i);
            pushCopy(out, in, s, inst);
        }
        if (capped) break;
    }
    if (capped)
        ctx.res->warnings.push_back(
            "Radial Array: stopped at " + std::to_string(kMaxRepeatOut) +
            " points - lower the count or thin the input first");
    return out;
}

Mask genNoise(Ctx& ctx, const ProcNode& n) {
    const int kind = procgraph::inum(n, "kind");
    const float scale = std::max(1.0f, procgraph::num(n, "scale"));
    const int octaves = std::clamp(procgraph::inum(n, "octaves"), 1, 6);
    const float low = procgraph::num(n, "low");
    const float high = procgraph::num(n, "high");
    const bool inv = procgraph::flag(n, "invert");
    Mask m = makeMask(ctx.vol, maskRes(ctx.vol, scale));
    // The noise seed folds the graph seed with the node id: reseeding the graph
    // moves every field, editing one node moves only its own.
    const uint32_t seed =
        (uint32_t)(mix64(((uint64_t)ctx.seed << 32) ^ (uint64_t)(uint32_t)n.id) &
                   0xffffffffu);
    for (int z = 0; z < m.h; ++z) {
        for (int x = 0; x < m.w; ++x) {
            const float wx = m.originX + m.sizeX * ((float)x / (float)(m.w - 1));
            const float wz = m.originZ + m.sizeZ * ((float)z / (float)(m.h - 1));
            float v = noiseAt(kind, wx / scale, wz / scale, octaves, seed);
            v = remap01(v, low, high);
            m.v[(size_t)z * m.w + x] = inv ? 1.0f - v : v;
        }
    }
    return m;
}

Mask genTerrainMask(Ctx& ctx, const ProcNode& n) {
    const int source = procgraph::inum(n, "source");
    const int layer = procgraph::inum(n, "layer");
    const float lo = procgraph::num(n, "min");
    const float hi = procgraph::num(n, "max");
    const float falloff = procgraph::num(n, "falloff");
    const bool inv = procgraph::flag(n, "invert");
    Mask m = makeMask(ctx.vol, maskRes(ctx.vol, std::max(2.0f, ctx.vol.sizeX / 128.0f)));
    for (int z = 0; z < m.h; ++z) {
        for (int x = 0; x < m.w; ++x) {
            const float wx = m.originX + m.sizeX * ((float)x / (float)(m.w - 1));
            const float wz = m.originZ + m.sizeZ * ((float)z / (float)(m.h - 1));
            float v;
            switch (source) {
                case 1: {
                    float nrm[3];
                    terrainNormal(ctx.s, wx, wz, nrm);
                    v = std::acos(std::clamp(nrm[1], -1.0f, 1.0f)) * kDeg;
                    break;
                }
                case 2: v = terrainCurvature(ctx.s, wx, wz); break;
                case 3: v = terrainMaterialCoverage(ctx.s, layer, wx, wz); break;
                default: v = terrainHeight(ctx.s, wx, wz); break;
            }
            const float b = band(v, lo, hi, falloff);
            m.v[(size_t)z * m.w + x] = inv ? 1.0f - b : b;
        }
    }
    return m;
}

Mask combineMasks(const ProcNode& n, const Mask& a, const Mask* b) {
    Mask out = a;
    if (!b) return out;
    const int op = procgraph::inum(n, "op");
    const float blend = std::clamp(procgraph::num(n, "blend"), 0.0f, 1.0f);
    for (int z = 0; z < out.h; ++z)
        for (int x = 0; x < out.w; ++x) {
            const float wx = out.originX + out.sizeX * ((float)x / (float)(out.w - 1));
            const float wz = out.originZ + out.sizeZ * ((float)z / (float)(out.h - 1));
            const float va = a.v[(size_t)z * a.w + x];
            const float vb = b->sample(wx, wz);
            float r;
            switch (op) {
                case 1: r = va + vb; break;
                case 2: r = va - vb; break;
                case 3: r = std::min(va, vb); break;
                case 4: r = std::max(va, vb); break;
                case 5: r = va * (1.0f - blend) + vb * blend; break;
                default: r = va * vb; break;
            }
            out.v[(size_t)z * out.w + x] = std::clamp(r, 0.0f, 1.0f);
        }
    return out;
}

Mask remapMask(const ProcNode& n, const Mask& in) {
    Mask out = in;
    const float lo = procgraph::num(n, "low");
    const float hi = procgraph::num(n, "high");
    const float gamma = std::max(0.05f, procgraph::num(n, "gamma"));
    const bool inv = procgraph::flag(n, "invert");
    for (float& v : out.v) {
        float r = std::pow(remap01(v, lo, hi), gamma);
        v = std::clamp(inv ? 1.0f - r : r, 0.0f, 1.0f);
    }
    return out;
}

// --- filters ---------------------------------------------------------------

Points filterRange(Ctx& ctx, const ProcNode& n, const Points& in) {
    Points out = in;
    const std::string attrName = procgraph::str(n, "attr").empty()
                                     ? std::string(procattr::kSlope)
                                     : procgraph::str(n, "attr");
    const float* a = out.attr(attrName);
    if (!a) {
        ctx.res->warnings.push_back("Filter by Attribute: no attribute named '" +
                                    attrName + "' on these points");
        return out;
    }
    const float lo = procgraph::num(n, "min");
    const float hi = procgraph::num(n, "max");
    const float falloff = procgraph::num(n, "falloff");
    const bool inv = procgraph::flag(n, "invert");
    std::vector<char> keep(out.pts.size(), 0);
    for (size_t i = 0; i < out.pts.size(); ++i) {
        float p = band(a[i], lo, hi, falloff);
        if (inv) p = 1.0f - p;
        keep[i] = rand01(ctx.seed, n.id, out.pts[i].key, 10) < p ? 1 : 0;
    }
    out.compact(keep);
    return out;
}

Points filterMask(Ctx& ctx, const ProcNode& n, const Points& in, const Mask* m) {
    Points out = in;
    if (!m) return out;
    const float lo = procgraph::num(n, "low");
    const float hi = procgraph::num(n, "high");
    const float strength = std::clamp(procgraph::num(n, "strength"), 0.0f, 1.0f);
    const bool inv = procgraph::flag(n, "invert");
    out.attrs[procattr::kMask].resize(out.pts.size(), 0.0f);
    std::vector<char> keep(out.pts.size(), 0);
    for (size_t i = 0; i < out.pts.size(); ++i) {
        const float raw = m->sample(out.pts[i].pos[0], out.pts[i].pos[2]);
        float v = remap01(raw, lo, hi);
        if (inv) v = 1.0f - v;
        out.attrs[procattr::kMask][i] = v;
        const float p = 1.0f - strength * (1.0f - v);
        keep[i] = rand01(ctx.seed, n.id, out.pts[i].key, 11) < p ? 1 : 0;
    }
    out.compact(keep);
    return out;
}

Points filterDistance(Ctx& ctx, const ProcNode& n, const Points& in) {
    Points out = in;
    const float radius = std::max(0.01f, procgraph::num(n, "radius"));
    const bool bySize = procgraph::flag(n, "bysize");
    const float* size = out.attr(procattr::kSize);
    // Spatial hash at the radius scale: with a per-point radius the cell is
    // sized by the LARGEST possible one, or a big instance could miss a
    // neighbour two cells away.
    float maxR = radius;
    if (bySize && size)
        for (size_t i = 0; i < out.pts.size(); ++i)
            maxR = std::max(maxR, radius * std::max(0.01f, size[i]) *
                                      std::max(0.01f, out.pts[i].scale));
    const float cell = std::max(0.05f, maxR);
    std::unordered_map<uint64_t, std::vector<int>> grid;
    grid.reserve(out.pts.size() * 2);
    auto cellKey = [&](int cx, int cz) {
        return ((uint64_t)(uint32_t)cx << 32) ^ (uint32_t)cz;
    };
    std::vector<char> keep(out.pts.size(), 0);
    std::vector<float> radii(out.pts.size(), radius);
    for (size_t i = 0; i < out.pts.size(); ++i) {
        if ((i & 1023) == 0 && ctx.canceled()) break;
        float r = radius;
        if (bySize)
            r *= std::max(0.01f, (size ? size[i] : 1.0f)) *
                 std::max(0.01f, out.pts[i].scale);
        radii[i] = r;
        const float x = out.pts[i].pos[0], z = out.pts[i].pos[2];
        const int cx = (int)std::floor(x / cell), cz = (int)std::floor(z / cell);
        bool blocked = false;
        for (int dz = -1; dz <= 1 && !blocked; ++dz)
            for (int dx = -1; dx <= 1 && !blocked; ++dx) {
                auto it = grid.find(cellKey(cx + dx, cz + dz));
                if (it == grid.end()) continue;
                for (int j : it->second) {
                    const float ddx = out.pts[j].pos[0] - x;
                    const float ddz = out.pts[j].pos[2] - z;
                    // The bigger of the two radii wins: a large tree pushes
                    // saplings away, not the other way round.
                    const float need = std::max(r, radii[j]);
                    if (ddx * ddx + ddz * ddz < need * need) {
                        blocked = true;
                        break;
                    }
                }
            }
        if (blocked) continue;
        keep[i] = 1;
        grid[cellKey(cx, cz)].push_back((int)i);
    }
    out.compact(keep);
    return out;
}

Points filterAvoid(Ctx& ctx, const ProcNode& n, const Points& in, const Curve* c) {
    Points out = in;
    const float radius = std::max(0.0f, procgraph::num(n, "radius"));
    const int mode = procgraph::inum(n, "mode");
    const float falloff = std::max(0.0f, procgraph::num(n, "falloff"));
    const std::string& target = procgraph::str(n, "target");

    std::vector<ObjBox> boxes;
    if (!c) {
        for (const SceneObject& o : ctx.s.objects) {
            if (&o == &ctx.volObj) continue;
            if (!o.procSource.empty()) continue;  // our own baked chunks
            if (!target.empty()) {
                if (o.name != target) continue;
            } else if (!isSolidBlocker(o)) {
                continue;
            }
            boxes.push_back(objectBox(ctx.p, o));
        }
        if (boxes.empty()) {
            if (!target.empty())
                ctx.res->warnings.push_back("Keep Away From: no object named '" +
                                            target + "'");
            return out;
        }
    }
    out.attrs[procattr::kDist].resize(out.pts.size(), 0.0f);
    std::vector<char> keep(out.pts.size(), 0);
    for (size_t i = 0; i < out.pts.size(); ++i) {
        if ((i & 1023) == 0 && ctx.canceled()) break;
        const float x = out.pts[i].pos[0], z = out.pts[i].pos[2];
        float d = 1e9f;
        if (c)
            d = c->distanceXZ(x, z);
        else
            for (const ObjBox& b : boxes) d = std::min(d, distanceToBoxXZ(b, x, z));
        out.attrs[procattr::kDist][i] = d;
        // Outside: 0 inside the radius, 1 past radius+falloff.
        float p;
        if (falloff <= 0.0f)
            p = d >= radius ? 1.0f : 0.0f;
        else
            p = std::clamp((d - radius) / falloff, 0.0f, 1.0f);
        if (mode == 1) p = 1.0f - p;
        keep[i] = rand01(ctx.seed, n.id, out.pts[i].key, 12) < p ? 1 : 0;
    }
    out.compact(keep);
    return out;
}

Points pickAsset(Ctx& ctx, const ProcNode& n, const Points& in) {
    Points out = in;
    struct Row {
        int asset;
        float weight, smin, smax;
    };
    std::vector<Row> rows;
    float total = 0.0f;
    for (const ProcRow& r : n.rows) {
        if (r.s.empty()) continue;
        const float w = std::max(0.0f, r.v[0]);
        if (w <= 0.0f) continue;
        int idx = -1;
        for (size_t i = 0; i < ctx.assets.size(); ++i)
            if (ctx.assets[i] == r.s) idx = (int)i;
        if (idx < 0) continue;
        const float smin = r.v[1] > 0.0f ? r.v[1] : 1.0f;
        const float smax = r.v[2] > 0.0f ? r.v[2] : smin;
        rows.push_back({idx, w, smin, smax});
        total += w;
    }
    if (rows.empty() || total <= 0.0f) {
        ctx.res->warnings.push_back("Pick Asset: the pool is empty");
        return out;
    }
    out.attrs[procattr::kSize].resize(out.pts.size(), 1.0f);
    for (size_t i = 0; i < out.pts.size(); ++i) {
        // Weighted draw from the point's OWN stream: changing a weight moves
        // the boundary between species, it does not reshuffle the layout.
        float r = rand01(ctx.seed, n.id, out.pts[i].key, 20) * total;
        size_t pickIdx = 0;
        for (size_t k = 0; k < rows.size(); ++k) {
            if (r < rows[k].weight) {
                pickIdx = k;
                break;
            }
            r -= rows[k].weight;
            pickIdx = k;
        }
        const Row& row = rows[pickIdx];
        const float t = rand01(ctx.seed, n.id, out.pts[i].key, 21);
        const float size = row.smin + (row.smax - row.smin) * t;
        out.pts[i].asset = row.asset;
        out.pts[i].scale *= size;
        out.attrs[procattr::kSize][i] = size;
    }
    return out;
}

// The prefab twin of pickAsset. Deliberately a separate node rather than a
// second row kind on Pick Asset: the two produce different KINDS of output
// (merged geometry vs a group of objects with their own budget), and a pool
// mixing them would hide that from the budget readout.
Points pickPrefab(Ctx& ctx, const ProcNode& n, const Points& in) {
    Points out = in;
    struct Row {
        int prefab;
        float weight, smin, smax;
    };
    std::vector<Row> rows;
    float total = 0.0f;
    for (const ProcRow& r : n.rows) {
        if (r.s.empty()) continue;
        const float w = std::max(0.0f, r.v[0]);
        if (w <= 0.0f) continue;
        int idx = -1;
        for (size_t i = 0; i < ctx.prefabs.size(); ++i)
            if (ctx.prefabs[i] == r.s) idx = (int)i;
        if (idx < 0) continue;
        const float smin = r.v[1] > 0.0f ? r.v[1] : 1.0f;
        const float smax = r.v[2] > 0.0f ? r.v[2] : smin;
        rows.push_back({idx, w, smin, smax});
        total += w;
    }
    if (rows.empty() || total <= 0.0f) {
        ctx.res->warnings.push_back("Pick Prefab: the pool is empty");
        return out;
    }
    out.attrs[procattr::kSize].resize(out.pts.size(), 1.0f);
    for (size_t i = 0; i < out.pts.size(); ++i) {
        float r = rand01(ctx.seed, n.id, out.pts[i].key, 22) * total;
        size_t pickIdx = 0;
        for (size_t k = 0; k < rows.size(); ++k) {
            if (r < rows[k].weight) {
                pickIdx = k;
                break;
            }
            r -= rows[k].weight;
            pickIdx = k;
        }
        const Row& row = rows[pickIdx];
        const float t = rand01(ctx.seed, n.id, out.pts[i].key, 23);
        const float size = row.smin + (row.smax - row.smin) * t;
        out.pts[i].prefab = row.prefab;
        out.pts[i].asset = -1;  // a point is one or the other
        out.pts[i].scale *= size;
        out.attrs[procattr::kSize][i] = size;
    }
    return out;
}

Points varyTransform(Ctx& ctx, const ProcNode& n, const Points& in) {
    Points out = in;
    const float yawRange = procgraph::num(n, "yaw");
    const float tilt = procgraph::num(n, "tilt");
    const float smin = procgraph::num(n, "scalemin");
    const float smax = procgraph::num(n, "scalemax");
    const float jitter = procgraph::num(n, "jitter");
    const float align = std::clamp(procgraph::num(n, "align"), 0.0f, 1.0f);
    const float* nx = out.attr(procattr::kNormalX);
    const float* ny = out.attr(procattr::kNormalY);
    const float* nz = out.attr(procattr::kNormalZ);
    for (size_t i = 0; i < out.pts.size(); ++i) {
        Instance& p = out.pts[i];
        const uint64_t k = p.key;
        p.rot[1] += (rand01(ctx.seed, n.id, k, 30) - 0.5f) * yawRange;
        if (align > 0.0f && nx && ny && nz) {
            // Lay the instance over toward the surface normal. Rotating around
            // the horizontal axis perpendicular to the slope direction is what
            // "align to normal" means; `align` scales the angle, so 0 keeps
            // everything upright and 1 follows the ground exactly.
            const float lean =
                std::acos(std::clamp(ny[i], -1.0f, 1.0f)) * kDeg * align;
            const float dirYaw = std::atan2(nx[i], nz[i]) * kDeg;
            p.rot[0] += lean * std::cos((dirYaw - p.rot[1]) / kDeg);
            p.rot[2] += -lean * std::sin((dirYaw - p.rot[1]) / kDeg);
        }
        if (tilt > 0.0f) {
            p.rot[0] += (rand01(ctx.seed, n.id, k, 31) - 0.5f) * 2.0f * tilt;
            p.rot[2] += (rand01(ctx.seed, n.id, k, 32) - 0.5f) * 2.0f * tilt;
        }
        if (smax > 0.0f && (smin != 1.0f || smax != 1.0f)) {
            const float t = rand01(ctx.seed, n.id, k, 33);
            p.scale *= smin + (smax - smin) * t;
        }
        if (jitter > 0.0f) {
            p.pos[0] += (rand01(ctx.seed, n.id, k, 34) - 0.5f) * 2.0f * jitter;
            p.pos[2] += (rand01(ctx.seed, n.id, k, 35) - 0.5f) * 2.0f * jitter;
        }
    }
    return out;
}

Points setAttribute(const ProcNode& n, const Points& in, const Mask* m) {
    Points out = in;
    if (!m) return out;
    std::string name = procgraph::str(n, "attr");
    if (name.empty()) name = procattr::kMask;
    const float lo = procgraph::num(n, "min");
    const float hi = procgraph::num(n, "max");
    std::vector<float>& a = out.attrs[name];
    a.assign(out.pts.size(), 0.0f);
    for (size_t i = 0; i < out.pts.size(); ++i) {
        const float v = std::clamp(m->sample(out.pts[i].pos[0], out.pts[i].pos[2]),
                                   0.0f, 1.0f);
        a[i] = lo + (hi - lo) * v;
    }
    return out;
}

// --- node dispatch ---------------------------------------------------------

uint64_t nodeParamHash(const ProcNode& n) {
    uint64_t h = hashStr(0x9e3779b9ULL, n.type);
    for (const auto& kv : n.nums) h = hashFloat(hashStr(h, kv.first), kv.second);
    for (const auto& kv : n.strs) h = hashStr(hashStr(h, kv.first), kv.second);
    for (const ProcRow& r : n.rows) {
        h = hashStr(h, r.s);
        for (float v : r.v) h = hashFloat(h, v);
    }
    return hashCombine(h, n.bypass ? 1u : 0u);
}

Value evalNode(Ctx& ctx, int nodeId) {
    const ProcNode* np = procgraph::node(ctx.g, nodeId);
    if (!np) return Value{};
    const ProcNode& n = *np;
    const ProcNodeType* t = procNodeType(n.type);
    if (!t) return Value{};
    if (ctx.depth > 64) {  // belt and braces: linkError already rejects cycles
        ctx.res->warnings.push_back("graph is too deeply nested");
        return Value{};
    }

    // Inputs first: their hashes are part of this node's cache key, which is
    // what makes a change invalidate everything BELOW it and nothing above.
    ++ctx.depth;
    std::vector<Value> ins;
    ins.reserve(t->ins.size());
    for (size_t i = 0; i < t->ins.size(); ++i) ins.push_back(input(ctx, n, (int)i));
    --ctx.depth;

    uint64_t key = nodeParamHash(n);
    key = hashCombine(key, ctx.seed);
    key = hashCombine(key, ctx.opt.contextSerial);
    key = hashFloat(key, ctx.opt.fraction);
    for (const Value& v : ins) key = hashCombine(key, v.hash);

    if (ctx.cache) {
        auto it = ctx.cache->nodes_.find(nodeId);
        if (it != ctx.cache->nodes_.end() && it->second->hash == key) {
            ++ctx.res->nodesCached;
            return it->second->value;
        }
    }
    ++ctx.res->nodesEvaluated;

    Value out;
    out.hash = key;
    if (!t->outs.empty()) out.type = t->outs.front().type;

    // Bypass: hand the first input through untouched. The A/B switch for one
    // step of a chain, and the reason every node's first input is the "main"
    // one.
    if (n.bypass && !ins.empty()) {
        Value pass = ins[0];
        pass.hash = key;
        if (ctx.cache) {
            auto e = std::make_shared<Cache::Entry>();
            e->hash = key;
            e->value = pass;
            ctx.cache->nodes_[nodeId] = e;
        }
        return pass;
    }

    auto pointsIn = [&](size_t i) -> const Points* {
        return i < ins.size() ? ins[i].points.get() : nullptr;
    };
    auto maskIn = [&](size_t i) -> const Mask* {
        return i < ins.size() ? ins[i].mask.get() : nullptr;
    };

    if (n.type == "ScatterSurface") {
        out.points = std::make_shared<Points>(
            genSurface(ctx, n, ins.empty() ? Value{} : ins[0]));
    } else if (n.type == "ScatterGrid") {
        out.points = std::make_shared<Points>(
            genGrid(ctx, n, ins.empty() ? Value{} : ins[0]));
    } else if (n.type == "BlocksFill") {
        out.points = std::make_shared<Points>(
            genBlocks(ctx, n, ins.empty() ? Value{} : ins[0]));
    } else if (n.type == "ScatterVolume") {
        out.points = std::make_shared<Points>(genVolume(ctx, n));
    } else if (n.type == "ScatterCurve") {
        out.points = std::make_shared<Points>(
            genCurvePoints(ctx, n, ins.empty() ? Value{} : ins[0]));
    } else if (n.type == "Point") {
        out.points = std::make_shared<Points>(genPoint(ctx, n));
    } else if (n.type == "Array") {
        if (const Points* a = pointsIn(0))
            out.points = std::make_shared<Points>(arrayCopies(ctx, n, *a));
    } else if (n.type == "RadialArray") {
        if (const Points* a = pointsIn(0))
            out.points = std::make_shared<Points>(radialCopies(ctx, n, *a));
    } else if (n.type == "Curve") {
        auto c = std::make_shared<Curve>();
        c->closed = procgraph::flag(n, "closed");
        for (const ProcRow& r : n.rows) {
            c->pts.push_back(r.v[0]);
            c->pts.push_back(r.v[1]);
            c->pts.push_back(r.v[2]);
        }
        out.curve = c;
    } else if (n.type == "NoiseMask") {
        out.mask = std::make_shared<Mask>(genNoise(ctx, n));
    } else if (n.type == "TerrainMask") {
        out.mask = std::make_shared<Mask>(genTerrainMask(ctx, n));
    } else if (n.type == "MaskCombine") {
        if (const Mask* a = maskIn(0))
            out.mask = std::make_shared<Mask>(combineMasks(n, *a, maskIn(1)));
    } else if (n.type == "MaskRemap") {
        if (const Mask* a = maskIn(0))
            out.mask = std::make_shared<Mask>(remapMask(n, *a));
    } else if (n.type == "FilterRange") {
        if (const Points* a = pointsIn(0))
            out.points = std::make_shared<Points>(filterRange(ctx, n, *a));
    } else if (n.type == "FilterMask") {
        if (const Points* a = pointsIn(0))
            out.points = std::make_shared<Points>(filterMask(ctx, n, *a, maskIn(1)));
    } else if (n.type == "FilterDistance") {
        if (const Points* a = pointsIn(0))
            out.points = std::make_shared<Points>(filterDistance(ctx, n, *a));
    } else if (n.type == "FilterAvoid") {
        if (const Points* a = pointsIn(0))
            out.points = std::make_shared<Points>(
                filterAvoid(ctx, n, *a, ins.size() > 1 ? ins[1].curve.get() : nullptr));
    } else if (n.type == "Merge") {
        auto merged = std::make_shared<Points>();
        for (size_t i = 0; i < ins.size(); ++i)
            if (const Points* a = pointsIn(i)) merged->append(*a);
        out.points = merged;
    } else if (n.type == "Limit") {
        if (const Points* a = pointsIn(0)) {
            auto lim = std::make_shared<Points>(*a);
            const size_t max = (size_t)std::max(1, procgraph::inum(n, "max"));
            if (lim->pts.size() > max) {
                std::vector<char> keep(lim->pts.size(), 0);
                for (size_t i = 0; i < max; ++i) keep[i] = 1;
                lim->compact(keep);
            }
            out.points = lim;
        }
    } else if (n.type == "PickAsset") {
        if (const Points* a = pointsIn(0))
            out.points = std::make_shared<Points>(pickAsset(ctx, n, *a));
    } else if (n.type == "PickPrefab") {
        if (const Points* a = pointsIn(0))
            out.points = std::make_shared<Points>(pickPrefab(ctx, n, *a));
    } else if (n.type == "Vary") {
        if (const Points* a = pointsIn(0))
            out.points = std::make_shared<Points>(varyTransform(ctx, n, *a));
    } else if (n.type == "SetAttribute") {
        if (const Points* a = pointsIn(0))
            out.points = std::make_shared<Points>(setAttribute(n, *a, maskIn(1)));
    } else if (n.type == "Output") {
        if (!ins.empty()) {
            out.points = ins[0].points;
            out.type = ProcType::Points;
        }
    }

    if (ctx.cache) {
        auto e = std::make_shared<Cache::Entry>();
        e->hash = key;
        e->value = out;
        ctx.cache->nodes_[nodeId] = e;
    }
    return out;
}

// The graph's asset table, in a stable order (node id, then row order) so an
// Instance::asset index means the same thing across evaluations.
std::vector<std::string> collectAssets(const ProcGraph& g) {
    std::vector<std::string> out;
    std::vector<const ProcNode*> pickers;
    for (const ProcNode& n : g.nodes)
        if (n.type == "PickAsset") pickers.push_back(&n);
    std::sort(pickers.begin(), pickers.end(),
              [](const ProcNode* a, const ProcNode* b) { return a->id < b->id; });
    for (const ProcNode* n : pickers)
        for (const ProcRow& r : n->rows) {
            if (r.s.empty()) continue;
            if (std::find(out.begin(), out.end(), r.s) == out.end())
                out.push_back(r.s);
        }
    return out;
}

// The same, for Pick Prefab. A separate table because a prefab is not geometry
// to merge - see Instance::prefab.
std::vector<std::string> collectPrefabs(const ProcGraph& g) {
    std::vector<std::string> out;
    std::vector<const ProcNode*> pickers;
    for (const ProcNode& n : g.nodes)
        if (n.type == "PickPrefab") pickers.push_back(&n);
    std::sort(pickers.begin(), pickers.end(),
              [](const ProcNode* a, const ProcNode* b) { return a->id < b->id; });
    for (const ProcNode* n : pickers)
        for (const ProcRow& r : n->rows) {
            if (r.s.empty()) continue;
            if (std::find(out.begin(), out.end(), r.s) == out.end())
                out.push_back(r.s);
        }
    return out;
}

}  // namespace

Result evaluate(const Project& p, const SceneData& s, const SceneObject& volume,
                const Options& opt, Cache* cache) {
    const auto t0 = std::chrono::steady_clock::now();
    Result res;
    res.fraction = std::clamp(opt.fraction, 0.01f, 1.0f);
    const ProcGraph& g = volume.procGraph;
    res.assets = collectAssets(g);
    res.prefabs = collectPrefabs(g);
    if (g.nodes.empty()) return res;

    // A graph that samples the ground in a scene with no terrain places
    // everything at the void height - correct (it is what the console does),
    // and completely mystifying unless it is said out loud. Cheap to detect:
    // Scatter on Surface always samples, everything else only with Snap on.
    if (!s.terrain.enabled) {
        for (const ProcNode& n : g.nodes) {
            if (n.type != "ScatterSurface" && !procgraph::flag(n, "snap")) continue;
            res.warnings.push_back(
                "this scene has no terrain, so there is no surface to place on - "
                "turn Snap to surface off (or give the scene a terrain in Tools > "
                "Terrain)");
            break;
        }
    }

    Ctx ctx{p,
            s,
            volume,
            volumeOf(volume),
            g,
            opt.seedOverride ? opt.seedOverride : g.seed,
            opt,
            cache,
            &res,
            res.assets,
            res.prefabs,
            0};

    int target = opt.previewNode;
    if (target != 0 && !procgraph::node(g, target)) target = 0;
    if (target == 0) {
        const ProcNode* on = procgraph::outputNode(g);
        if (!on) {
            res.warnings.push_back("no Output node - nothing to show");
            return res;
        }
        target = on->id;
    }
    const Value v = evalNode(ctx, target);
    res.canceled = ctx.canceled();
    res.type = v.type;
    res.mask = v.mask;
    res.curve = v.curve;
    if (v.points) {
        res.instances = v.points->pts;
        // Manual overrides last, so they win over every procedural decision
        // (FILT-05). Keys are matched, never indices.
        if (!g.overrides.empty()) {
            std::unordered_map<uint64_t, const ProcOverride*> byKey;
            byKey.reserve(g.overrides.size() * 2);
            for (const ProcOverride& o : g.overrides) byKey[o.key] = &o;
            std::vector<Instance> kept;
            kept.reserve(res.instances.size());
            size_t matched = 0;
            for (Instance& inst : res.instances) {
                auto it = byKey.find(inst.key);
                if (it == byKey.end()) {
                    kept.push_back(inst);
                    continue;
                }
                ++matched;
                const ProcOverride& o = *it->second;
                if (o.removed) continue;
                for (int a = 0; a < 3; ++a) {
                    inst.pos[a] += o.offset[a];
                    inst.rot[a] += o.rotate[a];
                }
                inst.scale *= o.scale;
                if (o.asset >= 0 && o.asset < (int)res.assets.size())
                    inst.asset = o.asset;
                kept.push_back(inst);
            }
            res.instances.swap(kept);
            res.overridesApplied = (int)matched;
            res.overridesOrphaned = (int)(g.overrides.size() - matched);
        }
    }
    // A RUNTIME volume spends one prefab-instance record PER POINT, and the
    // generated game's pool is fixed. Over it the console builds the first
    // kMaxRuntimeInstances and drops the rest with "instance pool full" - the
    // preview meanwhile shows every one, so the world looks like it generated
    // wrong rather than like it ran out. Points are produced level by level, so
    // what survives is the bottom of a stack, which reads as "only one row".
    if (volume.procGraph.runtime) {
        int pf = 0;
        for (const Instance& i : res.instances)
            if (i.prefab >= 0) ++pf;
        if (pf > prefab::kMaxRuntimeInstances)
            res.warnings.push_back(
                "runtime prefab instances: " + std::to_string(pf) + " of " +
                std::to_string(prefab::kMaxRuntimeInstances) +
                " the console can hold - the rest will NOT be built (thin the "
                "points, or scatter a model with Pick Asset, which merges "
                "without an instance record)");
    }

    res.millis = std::chrono::duration<double, std::milli>(
                     std::chrono::steady_clock::now() - t0)
                     .count();
    return res;
}

uint64_t bakeHash(const Project& p, const SceneData& s, const SceneObject& volume) {
    uint64_t h = 0x243f6a8885a308d3ULL;
    const ProcGraph& g = volume.procGraph;
    h = hashCombine(h, g.seed);
    // Flipping a volume between baked and runtime changes what the build has
    // to produce, so it has to read as stale (the bake then clears its chunks).
    h = hashCombine(h, (uint64_t)(g.runtime ? 1u : 0u));
    h = hashCombine(h, (uint64_t)(g.runAtStart ? 2u : 0u));
    h = hashCombine(h, (uint64_t)(uint32_t)g.seedMode);
    for (const ProcNode& n : g.nodes) {
        h = hashCombine(h, (uint64_t)(uint32_t)n.id);
        h = hashCombine(h, nodeParamHash(n));
    }
    for (const ProcLink& l : g.links) {
        h = hashCombine(h, (uint64_t)(uint32_t)l.fromNode);
        h = hashCombine(h, (uint64_t)(uint32_t)l.fromPin);
        h = hashCombine(h, (uint64_t)(uint32_t)l.toNode);
        h = hashCombine(h, (uint64_t)(uint32_t)l.toPin);
    }
    for (const ProcOverride& o : g.overrides) {
        h = hashCombine(h, o.key);
        h = hashCombine(h, o.removed ? 1u : 0u);
        for (int a = 0; a < 3; ++a) {
            h = hashFloat(h, o.offset[a]);
            h = hashFloat(h, o.rotate[a]);
        }
        h = hashFloat(h, o.scale);
        h = hashCombine(h, (uint64_t)(uint32_t)o.asset);
    }
    // The volume transform and everything the graph can read off the scene.
    for (int a = 0; a < 3; ++a) {
        h = hashFloat(h, volume.position[a]);
        h = hashFloat(h, volume.rotation[a]);
        h = hashFloat(h, volume.scale[a]);
    }
    h = hashCombine(h, (uint64_t)s.terrain.width);
    h = hashCombine(h, (uint64_t)s.terrain.depth);
    for (float y : s.heights) h = hashFloat(h, y);
    h = hashCombine(h, s.splat.size());
    for (uint8_t b : s.splat) h = hashCombine(h, b);
    for (const SceneObject& o : s.objects) {
        if (&o == &volume) continue;
        if (o.type == PrimitiveType::Scatter) continue;
        if (!o.procSource.empty()) continue;  // our own baked output
        h = hashStr(h, o.name);
        h = hashCombine(h, (uint64_t)(int)o.type);
        for (int a = 0; a < 3; ++a) {
            h = hashFloat(h, o.position[a]);
            h = hashFloat(h, o.rotation[a]);
            h = hashFloat(h, o.scale[a]);
        }
        h = hashStr(h, o.modelPath);
    }
    // A Pick Prefab graph also depends on what those prefabs CONTAIN - editing
    // a prefab has to make every volume that scatters it stale.
    bool usesPrefabs = false;
    for (const ProcNode& n : g.nodes) usesPrefabs |= (n.type == "PickPrefab");
    if (usesPrefabs)
        for (const Prefab& pf : p.prefabs) {
            h = hashStr(h, pf.name);
            for (const SceneObject& o : pf.objects) {
                h = hashStr(h, o.name);
                h = hashCombine(h, (uint64_t)(int)o.type);
                for (int a = 0; a < 3; ++a) {
                    h = hashFloat(h, o.position[a]);
                    h = hashFloat(h, o.rotation[a]);
                    h = hashFloat(h, o.scale[a]);
                    h = hashFloat(h, o.color[a]);
                }
                h = hashStr(h, o.modelPath);
                h = hashStr(h, o.materialPath);
            }
        }
    return h;
}

}  // namespace procgen
