#include "gibake.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>

#include <stb_image.h>  // implementation lives in app.cpp

#include "objparser.hpp"
#include "primmesh.hpp"
#include "wire.hpp"   // fnv1a64/hashFile - the bake signature hashes CONTENT

namespace fs = std::filesystem;

namespace gibake {

namespace {

constexpr float kPi = 3.14159265358979f;
constexpr uint32_t kCacheMagic = 0x49475854u;  // "TXGI"
constexpr uint32_t kCacheVersion = 2;

// Rotation order X, then Y, then Z - the twin of templates.cpp rotated(),
// aobake's and the viewport's model matrix. Keep in sync.
void rotate3(const float v[3], const float rotDeg[3], float out[3]) {
    float x = v[0], y = v[1], z = v[2];
    const float rx = rotDeg[0] * kPi / 180.0f;
    const float ry = rotDeg[1] * kPi / 180.0f;
    const float rz = rotDeg[2] * kPi / 180.0f;
    {
        const float c = std::cos(rx), s = std::sin(rx);
        const float ny = y * c - z * s, nz = y * s + z * c;
        y = ny, z = nz;
    }
    {
        const float c = std::cos(ry), s = std::sin(ry);
        const float nx = x * c + z * s, nz = -x * s + z * c;
        x = nx, z = nz;
    }
    {
        const float c = std::cos(rz), s = std::sin(rz);
        const float nx = x * c - y * s, ny = x * s + y * c;
        x = nx, y = ny;
    }
    out[0] = x, out[1] = y, out[2] = z;
}

inline uint32_t hashU32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

inline void mix64(uint64_t& h, uint64_t v) {
    h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
}
inline void mixF(uint64_t& h, float f) {
    uint32_t b;
    std::memcpy(&b, &f, 4);
    mix64(h, b);
}
inline void mixS(uint64_t& h, const std::string& s) {
    for (char c : s) mix64(h, (uint64_t)(unsigned char)c);
    mix64(h, 0x5bull);
}

// Cosine-weighted hemisphere directions around +Z, golden-angle spiral. The
// per-call rotation comes from a hash of the caller's seed, so a texel's
// sample set is a property of that texel and nothing else - the bake is
// bit-identical at any core count, which is what makes A/B comparison
// possible at all (matbake's discipline, inherited).
inline void hemiDir(int i, int n, float rot, float out[3]) {
    const float u = (i + 0.5f) / n;
    const float r = std::sqrt(u);
    const float zc = std::sqrt(1.0f - u);
    const float phi = kPi * (3.0f - std::sqrt(5.0f)) * i + rot;
    out[0] = r * std::cos(phi);
    out[1] = r * std::sin(phi);
    out[2] = zc;
}

// Uniform sphere directions (probes see the whole sphere, not a hemisphere).
inline void sphereDir(int i, int n, float rot, float out[3]) {
    const float z = 1.0f - 2.0f * (i + 0.5f) / n;
    const float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
    const float phi = kPi * (3.0f - std::sqrt(5.0f)) * i + rot;
    out[0] = r * std::cos(phi);
    out[1] = r * std::sin(phi);
    out[2] = z;
}

void basisAround(const float n[3], float t[3], float b[3]) {
    if (std::fabs(n[1]) < 0.9f) {
        t[0] = n[2], t[1] = 0.0f, t[2] = -n[0];
    } else {
        t[0] = 1.0f - n[0] * n[0];
        t[1] = -n[0] * n[1];
        t[2] = -n[0] * n[2];
    }
    const float l = std::sqrt(t[0] * t[0] + t[1] * t[1] + t[2] * t[2]);
    const float inv = l > 1e-6f ? 1.0f / l : 0.0f;
    t[0] *= inv, t[1] *= inv, t[2] *= inv;
    b[0] = n[1] * t[2] - n[2] * t[1];
    b[1] = n[2] * t[0] - n[0] * t[2];
    b[2] = n[0] * t[1] - n[1] * t[0];
}

bool animatedModelPath(const std::string& p) {
    const size_t dot = p.find_last_of('.');
    if (dot == std::string::npos) return false;
    std::string ext = p.substr(dot);
    for (char& c : ext) c = (char)std::tolower((unsigned char)c);
    return ext == ".glb" || ext == ".gltf" || ext == ".fbx";
}

// Mean colour of a texture, cached per path. A bounce only needs the average
// (interreflection is low frequency) - this is what stops a red brick wall
// bouncing white light.
struct TexMeanCache {
    std::map<std::string, std::array<float, 3>> map;
};
const std::array<float, 3>& textureMean(const std::string& absPath,
                                        TexMeanCache& cache) {
    auto it = cache.map.find(absPath);
    if (it != cache.map.end()) return it->second;
    std::array<float, 3> mean{1.0f, 1.0f, 1.0f};
    int w = 0, h = 0, comp = 0;
    unsigned char* px = stbi_load(absPath.c_str(), &w, &h, &comp, 4);
    if (px && w > 0 && h > 0) {
        // Stride so a large source costs the same as a small one; alpha-0
        // texels (cutouts) contribute nothing - they are holes, not colour.
        const int stepX = std::max(1, w / 64), stepY = std::max(1, h / 64);
        double acc[3] = {0, 0, 0};
        long long n = 0;
        for (int y = 0; y < h; y += stepY)
            for (int x = 0; x < w; x += stepX) {
                const unsigned char* p = px + ((size_t)y * w + x) * 4;
                if (p[3] == 0) continue;
                acc[0] += p[0], acc[1] += p[1], acc[2] += p[2];
                ++n;
            }
        if (n > 0)
            for (int k = 0; k < 3; ++k)
                mean[k] = (float)(acc[k] / n / 255.0);
    }
    if (px) stbi_image_free(px);
    return cache.map.emplace(absPath, mean).first->second;
}

// The .mtl an object points at, resolved once per path.
struct MatInfo {
    float kd[3] = {1, 1, 1};
    float emission[3] = {0, 0, 0};  // radiance, 0 = not a light
    std::array<float, 3> texMean{1.0f, 1.0f, 1.0f};
    bool loaded = false;
};
using MatCache = std::map<std::string, MatInfo>;

const MatInfo& materialInfo(const std::string& projectDir,
                            const std::string& materialPath, MatCache& cache,
                            TexMeanCache& texCache) {
    auto it = cache.find(materialPath);
    if (it != cache.end()) return it->second;
    MatInfo mi;
    std::vector<objparser::MtlMaterial> mats;
    if (!materialPath.empty() &&
        objparser::loadMtl((fs::path(projectDir) / materialPath).string(),
                           mats) &&
        !mats.empty()) {
        const objparser::MtlMaterial& m = mats.front();
        for (int k = 0; k < 3; ++k) mi.kd[k] = m.kd[k];
        // A material that GLOWS is not automatically a light: "# tyra-glow-
        // light" (Material Editor > Glow > Lights up surroundings) is what
        // says it lights the room, and that authored meaning survives GI. Ke
        // alone still just makes the surface look self-lit.
        if (m.glowRange > 0.0f && m.glowLight > 0.0f)
            for (int k = 0; k < 3; ++k)
                mi.emission[k] = m.glowColor[k] * m.glowLight;
        if (!m.texture.empty()) {
            const fs::path abs =
                (fs::path(projectDir) / materialPath).parent_path() / m.texture;
            mi.texMean = textureMean(abs.lexically_normal().string(), texCache);
        }
        mi.loaded = true;
    }
    return cache.emplace(materialPath, mi).first->second;
}

// Bilinear height over the vertex grid - the host twin of the generated
// terrainHeightAtScene (aobake has the same helper; keep the sampling
// identical or probes float above the ground the game walks on).
float heightAtWorld(const std::vector<float>& heights, int w, int d,
                    float width, float depth, float x, float z) {
    if (w < 2 || d < 2 || (int)heights.size() < w * d) return 0.0f;
    float gx = (x + width * 0.5f) / width * (w - 1);
    float gz = (z + depth * 0.5f) / depth * (d - 1);
    if (gx < 0) gx = 0;
    if (gz < 0) gz = 0;
    if (gx > w - 1.001f) gx = w - 1.001f;
    if (gz > d - 1.001f) gz = d - 1.001f;
    const int ix = (int)gx, iz = (int)gz;
    const float fx = gx - ix, fz = gz - iz;
    auto h = [&](int a, int b) { return heights[(size_t)b * w + a]; };
    const float t = h(ix, iz) * (1 - fx) + h(ix + 1, iz) * fx;
    const float b = h(ix, iz + 1) * (1 - fx) + h(ix + 1, iz + 1) * fx;
    return t * (1 - fz) + b * fz;
}

// Splits [0, count) across the hardware threads and runs `body(i)`. Every
// element's result depends only on the inputs, so the partition never changes
// the answer - only the wall clock.
void parallelFor(int count, const std::atomic<bool>* cancel,
                 const std::function<void(int, int)>& bodyRange) {
    if (count <= 0) return;
    int threads = (int)std::thread::hardware_concurrency();
    if (threads < 1) threads = 1;
    if (threads > 16) threads = 16;
    if (count < threads * 8) threads = 1;
    if (threads == 1) {
        bodyRange(0, count);
        return;
    }
    std::vector<std::thread> pool;
    const int chunk = (count + threads - 1) / threads;
    for (int t = 0; t < threads; ++t) {
        const int lo = t * chunk;
        const int hi = std::min(count, lo + chunk);
        if (lo >= hi) break;
        pool.emplace_back([&, lo, hi] {
            if (cancel && cancel->load()) return;
            bodyRange(lo, hi);
        });
    }
    for (std::thread& th : pool) th.join();
}

}  // namespace

Settings settingsOf(const ProjectSettings& s) {
    Settings st;
    st.enabled = s.giEnabled;
    st.rays = s.giRays;
    st.bounces = s.giBounces;
    st.skyLight = s.giSkyLight;
    st.sunLight = s.giSunLight;
    st.ambientFloor = s.giAmbientFloor;
    st.probeSpacing = s.giProbeSpacing;
    st.probeHeight = s.giProbeHeight;
    st.probeLevels = s.giProbeLevels;
    st.probes = s.giProbes;
    if (st.rays < 8) st.rays = 8;
    if (st.rays > 1024) st.rays = 1024;
    if (st.bounces < 0) st.bounces = 0;
    if (st.bounces > 8) st.bounces = 8;
    if (st.probeSpacing < 0.5f) st.probeSpacing = 0.5f;
    if (st.probeHeight < 0.25f) st.probeHeight = 0.25f;
    if (st.probeLevels < 1) st.probeLevels = 1;
    if (st.probeLevels > 16) st.probeLevels = 16;
    return st;
}

// --- scene tessellation ------------------------------------------------------

namespace {

// Appends one transformed triangle soup (8 floats per vertex: pos, normal, uv)
// to the tree, with a flat albedo / emission per triangle.
void appendMesh(Scene& s, const std::vector<float>& verts, const float pos[3],
                const float rot[3], const float scale[3], const float albedo[3],
                const float emission[3]) {
    const size_t tris = verts.size() / 24;
    for (size_t t = 0; t < tris; ++t) {
        for (int c = 0; c < 3; ++c) {
            const float* v = &verts[(t * 3 + c) * 8];
            float lp[3] = {v[0] * scale[0], v[1] * scale[1], v[2] * scale[2]};
            float wp[3];
            rotate3(lp, rot, wp);
            wp[0] += pos[0], wp[1] += pos[1], wp[2] += pos[2];
            // Normals: a non-uniform scale needs the inverse transpose, which
            // for an axis-aligned scale is 1/scale. Cheap and exact.
            float ln[3] = {v[3] / (scale[0] != 0 ? scale[0] : 1.0f),
                           v[4] / (scale[1] != 0 ? scale[1] : 1.0f),
                           v[5] / (scale[2] != 0 ? scale[2] : 1.0f)};
            float wn[3];
            rotate3(ln, rot, wn);
            const float l =
                std::sqrt(wn[0] * wn[0] + wn[1] * wn[1] + wn[2] * wn[2]);
            const float inv = l > 1e-6f ? 1.0f / l : 0.0f;
            s.tree.tv.insert(s.tree.tv.end(), {wp[0], wp[1], wp[2]});
            s.tree.tn.insert(s.tree.tn.end(),
                             {wn[0] * inv, wn[1] * inv, wn[2] * inv});
        }
        s.albedo.insert(s.albedo.end(), {albedo[0], albedo[1], albedo[2]});
        s.emission.insert(s.emission.end(),
                          {emission[0], emission[1], emission[2]});
    }
}

std::vector<float> primitiveMesh(PrimitiveType type, int detail) {
    const int d = clampPrimDetail(type, detail);
    switch (type) {
        case PrimitiveType::Box:
        case PrimitiveType::SavePoint: return primmesh::unitBox(d);
        case PrimitiveType::Sphere: return primmesh::unitSphere(d);
        case PrimitiveType::Cylinder: return primmesh::unitCylinder(d);
        case PrimitiveType::Cone: return primmesh::unitCone(d);
        case PrimitiveType::Plane: return primmesh::unitPlane();
        default: return {};
    }
}

}  // namespace

Scene build(const Project& p, const SceneData& sc, const Settings& st) {
    Scene s;
    const ProjectSettings rs = project::resolvedSettings(p, sc);

    s.skyDome = rs.skyDome;
    for (int k = 0; k < 3; ++k) {
        // The sky IS the light: its authored colour is its radiance, scaled by
        // the project brightness and the GI sky multiplier. At the shipped
        // defaults a fully open horizontal surface receives ~0.53, which is
        // within a hair of the flat `ambient` (0.55) it replaces - so turning
        // GI on re-lights a scene without re-exposing it.
        s.skyHorizon[k] = rs.skyColor[k] * rs.brightness * st.skyLight;
        s.skyTop[k] = rs.skyTopColor[k] * rs.brightness * st.skyLight;
    }
    {
        float z = rs.zenithSize;
        z = z < 0.05f ? 0.05f : (z > 0.95f ? 0.95f : z);
        s.skyExp = (1.0f - z) / z;  // twin of SKY_ZENITH_EXPS in templates.cpp
    }
    {
        float d[3] = {rs.lightDir[0], rs.lightDir[1], rs.lightDir[2]};
        const float l = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        const float inv = l > 1e-6f ? 1.0f / l : 0.0f;
        for (int k = 0; k < 3; ++k) s.sunDir[k] = d[k] * inv;
        if (l <= 1e-6f) s.sunDir[0] = 0, s.sunDir[1] = 1, s.sunDir[2] = 0;
        for (int k = 0; k < 3; ++k)
            s.sunColor[k] =
                rs.brightness * rs.diffuse * rs.lightColor[k] * st.sunLight;
    }
    s.ambientFloor = st.ambientFloor;

    MatCache matCache;
    TexMeanCache texCache;

    // --- terrain ------------------------------------------------------------
    s.heights = sc.heights;
    s.hmW = sc.hmW;
    s.hmD = sc.hmD;
    s.hmWidth = (float)sc.terrain.width;
    s.hmDepth = (float)sc.terrain.depth;
    s.terrainMinX = -s.hmWidth * 0.5f;
    s.terrainMaxX = s.hmWidth * 0.5f;
    s.terrainMinZ = -s.hmDepth * 0.5f;
    s.terrainMaxZ = s.hmDepth * 0.5f;
    {
        float albedo[3] = {0.35f, 0.45f, 0.3f};  // the checker greens' average
        if (!rs.terrainMaterial.empty()) {
            const MatInfo& mi =
                materialInfo(p.dir, rs.terrainMaterial, matCache, texCache);
            if (mi.loaded)
                for (int k = 0; k < 3; ++k)
                    albedo[k] = mi.kd[k] * mi.texMean[k];
        }
        const float emission[3] = {0, 0, 0};
        // The ground carries most of a scene's bounce, so it is worth real
        // triangles - but a 256-detail heightmap would be 130k of them for a
        // signal that is nearly flat. Resampled onto a grid the bake can
        // afford; the heights themselves are still the bilinear ones the game
        // walks on.
        const int cells = std::min(96, std::max(2, std::max(sc.hmW, sc.hmD) - 1));
        std::vector<float> soup;
        soup.reserve((size_t)cells * cells * 48);
        auto hAt = [&](float x, float z) {
            return heightAtWorld(sc.heights, sc.hmW, sc.hmD, s.hmWidth,
                                 s.hmDepth, x, z);
        };
        for (int j = 0; j < cells; ++j) {
            for (int i = 0; i < cells; ++i) {
                const float x0 = s.terrainMinX + s.hmWidth * i / cells;
                const float x1 = s.terrainMinX + s.hmWidth * (i + 1) / cells;
                const float z0 = s.terrainMinZ + s.hmDepth * j / cells;
                const float z1 = s.terrainMinZ + s.hmDepth * (j + 1) / cells;
                const float c[4][3] = {{x0, hAt(x0, z0), z0},
                                       {x1, hAt(x1, z0), z0},
                                       {x1, hAt(x1, z1), z1},
                                       {x0, hAt(x0, z1), z1}};
                const int idx[6] = {0, 3, 2, 0, 2, 1};
                for (int k = 0; k < 6; ++k) {
                    const float* v = c[idx[k]];
                    soup.insert(soup.end(), {v[0], v[1], v[2], 0, 1, 0, 0, 0});
                }
            }
        }
        // Per-triangle geometric normals (the flat pushed above is a
        // placeholder; a sloped cell must shade as a slope).
        for (size_t t = 0; t * 24 + 23 < soup.size(); ++t) {
            float* a = &soup[t * 24];
            float e1[3], e2[3], n[3];
            for (int k = 0; k < 3; ++k) {
                e1[k] = a[8 + k] - a[k];
                e2[k] = a[16 + k] - a[k];
            }
            n[0] = e1[1] * e2[2] - e1[2] * e2[1];
            n[1] = e1[2] * e2[0] - e1[0] * e2[2];
            n[2] = e1[0] * e2[1] - e1[1] * e2[0];
            const float l = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
            float inv = l > 1e-9f ? 1.0f / l : 0.0f;
            if (n[1] < 0.0f) inv = -inv;  // the ground always faces up
            for (int c = 0; c < 3; ++c)
                for (int k = 0; k < 3; ++k) a[c * 8 + 3 + k] = n[k] * inv;
        }
        const float unit[3] = {1, 1, 1};
        const float zero3[3] = {0, 0, 0};
        appendMesh(s, soup, zero3, zero3, unit, albedo, emission);
    }

    // --- objects ------------------------------------------------------------
    for (const SceneObject& o : sc.objects) {
        // "Cast shadow" off already means "light passes through me"; honouring
        // it here keeps that switch meaning one thing.
        if (!o.castShadow) continue;
        const MatInfo& mi = materialInfo(p.dir, o.materialPath, matCache, texCache);
        float albedo[3], emission[3];
        for (int k = 0; k < 3; ++k) {
            albedo[k] = o.color[k] * mi.kd[k] * mi.texMean[k];
            if (albedo[k] < 0.0f) albedo[k] = 0.0f;
            // Energy cap: an authored Kd over 1 (material brightness) would
            // make each bounce hotter than the last and the solve would run
            // away. Real surfaces do not reflect more than they receive.
            if (albedo[k] > 0.92f) albedo[k] = 0.92f;
            emission[k] = mi.emission[k];
        }
        if (o.type == PrimitiveType::Model) {
            if (o.modelPath.empty() || animatedModelPath(o.modelPath)) continue;
            objparser::Model m;
            if (!objparser::load((fs::path(p.dir) / o.modelPath).string(), m,
                                 o.materialPath.empty()
                                     ? std::string()
                                     : (fs::path(p.dir) / o.materialPath).string()))
                continue;
            for (const objparser::Submesh& sm : m.submeshes) {
                float a[3], e[3] = {0, 0, 0};
                for (int k = 0; k < 3; ++k)
                    a[k] = std::min(0.92f, o.color[k] * sm.kd[k]);
                if (!sm.texture.empty()) {
                    const fs::path base =
                        o.materialPath.empty()
                            ? fs::path(p.dir) / fs::path(o.modelPath).parent_path()
                            : (fs::path(p.dir) / o.materialPath).parent_path();
                    const std::array<float, 3>& tm = textureMean(
                        (base / sm.texture).lexically_normal().string(), texCache);
                    for (int k = 0; k < 3; ++k) a[k] *= tm[k];
                }
                if (sm.glowRange > 0.0f && sm.glowLight > 0.0f)
                    for (int k = 0; k < 3; ++k) e[k] = sm.ke[k] * sm.glowLight;
                appendMesh(s, sm.verts, o.position, o.rotation, o.scale, a, e);
            }
            continue;
        }
        if (o.type == PrimitiveType::PointLight) {
            if (o.lightDynamic) continue;  // dynamic lights are never baked
            Scene::PointLight pl;
            for (int k = 0; k < 3; ++k) {
                pl.pos[k] = o.position[k];
                pl.color[k] = o.color[k];
            }
            pl.radius = o.lightRadius > 0.01f ? o.lightRadius : 0.01f;
            pl.bright = o.lightBright;
            s.lights.push_back(pl);
            continue;
        }
        const std::vector<float> mesh = primitiveMesh(o.type, o.primDetail);
        if (mesh.empty()) continue;  // markers, decals, mirrors, portals, areas
        appendMesh(s, mesh, o.position, o.rotation, o.scale, albedo, emission);
    }

    bvh::build(s.tree);
    s.radiosity.assign(s.emission.size(), 0.0f);
    std::memcpy(s.bmin, s.tree.bmin, sizeof s.bmin);
    std::memcpy(s.bmax, s.tree.bmax, sizeof s.bmax);
    return s;
}

// --- the integrator ----------------------------------------------------------

void skyRadiance(const Scene& s, const float dir[3], float out[3]) {
    if (!s.skyDome) {
        for (int k = 0; k < 3; ++k) out[k] = s.skyHorizon[k];
        return;
    }
    // Elevation fraction, 0 = horizon .. 1 = zenith, biased by the zenith-size
    // exponent exactly as the generated dome build does.
    float t = dir[1];
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    t = std::asin(t) / (kPi * 0.5f);
    const float b = std::pow(t, s.skyExp);
    for (int k = 0; k < 3; ++k)
        out[k] = s.skyHorizon[k] + (s.skyTop[k] - s.skyHorizon[k]) * b;
}

namespace {

// Sun + baked point lights at a surface point, each behind one shadow ray.
void directAt(const Scene& s, const float o[3], const float n[3],
              float out[3]) {
    out[0] = out[1] = out[2] = 0.0f;
    const float ndl = n[0] * s.sunDir[0] + n[1] * s.sunDir[1] + n[2] * s.sunDir[2];
    if (ndl > 0.0f &&
        (s.sunColor[0] > 0.0f || s.sunColor[1] > 0.0f || s.sunColor[2] > 0.0f)) {
        bvh::Hit h;
        if (!bvh::trace(s.tree, o, s.sunDir, 1e6f, true, h))
            for (int k = 0; k < 3; ++k) out[k] += s.sunColor[k] * ndl;
    }
    for (const Scene::PointLight& L : s.lights) {
        float d[3] = {L.pos[0] - o[0], L.pos[1] - o[1], L.pos[2] - o[2]};
        const float dist = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (dist >= L.radius || dist < 1e-5f) continue;
        const float inv = 1.0f / dist;
        d[0] *= inv, d[1] *= inv, d[2] *= inv;
        const float nl = n[0] * d[0] + n[1] * d[1] + n[2] * d[2];
        if (nl <= 0.0f) continue;
        // Same linear-squared pool as the per-vertex bake (pointLightAt in
        // templates.cpp) - GI changes what SEES the light, not its shape.
        float att = 1.0f - dist / L.radius;
        att *= att;
        bvh::Hit h;
        if (bvh::trace(s.tree, o, d, dist - 1e-3f, true, h)) continue;
        const float k = L.bright * att * nl;
        for (int c = 0; c < 3; ++c) out[c] += k * L.color[c];
    }
}

}  // namespace

void gather(const Scene& s, const float wp[3], const float n[3], uint32_t seed,
            int rays, float out[3]) {
    out[0] = out[1] = out[2] = 0.0f;
    if (rays < 1) rays = 1;
    // Bias off the surface: without it every ray's first hit is the texel's
    // own triangle and the whole bake comes out black.
    const float eps = 1e-3f * std::max(1.0f, std::fabs(wp[0]) + std::fabs(wp[1]) +
                                                 std::fabs(wp[2]));
    const float o[3] = {wp[0] + n[0] * eps, wp[1] + n[1] * eps,
                        wp[2] + n[2] * eps};
    directAt(s, o, n, out);

    float t[3], b[3];
    basisAround(n, t, b);
    const float rot = (hashU32(seed) & 0xffffu) * (2.0f * kPi / 65536.0f);
    float acc[3] = {0, 0, 0};
    for (int i = 0; i < rays; ++i) {
        float h[3];
        hemiDir(i, rays, rot, h);
        const float d[3] = {t[0] * h[0] + b[0] * h[1] + n[0] * h[2],
                            t[1] * h[0] + b[1] * h[1] + n[1] * h[2],
                            t[2] * h[0] + b[2] * h[1] + n[2] * h[2]};
        bvh::Hit hit;
        if (bvh::trace(s.tree, o, d, 1e6f, true, hit)) {
            // A back-facing hit is the inside of a solid: no light comes from
            // there. (Accepting back sides in the traversal is what keeps a
            // single-sided wall opaque from both sides.)
            if (hit.back) continue;
            const size_t ti = (size_t)hit.tri * 3;
            for (int k = 0; k < 3; ++k)
                acc[k] += s.emission[ti + k] + s.radiosity[ti + k];
        } else {
            float sky[3];
            skyRadiance(s, d, sky);
            for (int k = 0; k < 3; ++k) acc[k] += sky[k];
        }
    }
    // Cosine-weighted sampling already carries the N.L factor and the 1/pi, so
    // the estimator is the plain mean - the value is irradiance / pi, i.e.
    // exactly the multiplier a Lambertian albedo wants.
    const float invN = 1.0f / rays;
    for (int k = 0; k < 3; ++k) out[k] += acc[k] * invN + s.ambientFloor;
}

void solve(Scene& s, const Settings& st, const std::atomic<bool>* cancel,
           const ProgressFn& progress) {
    const int tris = s.tree.triCount();
    s.radiosity.assign((size_t)tris * 3, 0.0f);
    if (!tris || st.bounces <= 0) return;
    // The bounce field is low frequency - it does not need the texel pass's
    // ray budget, and halving it here is most of the bake's wall clock.
    const int rays = std::max(24, st.rays / 3);
    std::vector<float> next((size_t)tris * 3, 0.0f);
    std::vector<float> centroid((size_t)tris * 3);
    std::vector<float> normal((size_t)tris * 3);
    for (int t = 0; t < tris; ++t) {
        const float* v = &s.tree.tv[(size_t)t * 9];
        const float* nn = &s.tree.tn[(size_t)t * 9];
        for (int k = 0; k < 3; ++k) {
            centroid[(size_t)t * 3 + k] = (v[k] + v[3 + k] + v[6 + k]) / 3.0f;
            normal[(size_t)t * 3 + k] = (nn[k] + nn[3 + k] + nn[6 + k]) / 3.0f;
        }
        float* n = &normal[(size_t)t * 3];
        const float l = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (l > 1e-6f)
            n[0] /= l, n[1] /= l, n[2] /= l;
        else
            n[0] = 0, n[1] = 1, n[2] = 0;
    }
    for (int pass = 0; pass < st.bounces; ++pass) {
        if (cancel && cancel->load()) return;
        parallelFor(tris, cancel, [&](int lo, int hi) {
            for (int t = lo; t < hi; ++t) {
                if (cancel && cancel->load()) return;
                float in[3];
                // Seeded by (triangle, pass): a triangle's sample set is a
                // property of the triangle, never of the thread that ran it.
                gather(s, &centroid[(size_t)t * 3], &normal[(size_t)t * 3],
                       (uint32_t)t * 2654435761u + (uint32_t)pass, rays, in);
                for (int k = 0; k < 3; ++k)
                    next[(size_t)t * 3 + k] =
                        s.albedo[(size_t)t * 3 + k] * in[k];
            }
        });
        s.radiosity.swap(next);
        if (progress) progress((pass + 1.0f) / st.bounces);
    }
}

// --- probes ------------------------------------------------------------------

namespace {

// One probe: uniform sphere sampling into L1 spherical harmonics.
// live = false when the probe sits inside solid geometry (most rays hit a back
// face right next to the origin) - the runtime weighs such a probe at zero
// instead of dragging a wall's interior into the room next door.
void bakeOneProbe(const Scene& s, const float wp[3], int rays, uint32_t seed,
                  float L0[3], float L1[3][3], bool& live) {
    float acc0[3] = {0, 0, 0};
    float acc1[3][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
    const float rot = (hashU32(seed) & 0xffffu) * (2.0f * kPi / 65536.0f);
    int inside = 0;
    const float scale = std::max(1.0f, 0.02f * (s.bmax[0] - s.bmin[0]));
    for (int i = 0; i < rays; ++i) {
        float d[3];
        sphereDir(i, rays, rot, d);
        float rad[3];
        bvh::Hit hit;
        if (bvh::trace(s.tree, wp, d, 1e6f, true, hit)) {
            const size_t ti = (size_t)hit.tri * 3;
            if (hit.back) {
                rad[0] = rad[1] = rad[2] = 0.0f;
                if (hit.t < scale) ++inside;
            } else {
                for (int k = 0; k < 3; ++k)
                    rad[k] = s.emission[ti + k] + s.radiosity[ti + k];
            }
        } else {
            skyRadiance(s, d, rad);
        }
        for (int k = 0; k < 3; ++k) {
            acc0[k] += rad[k];
            acc1[0][k] += rad[k] * d[0];
            acc1[1][k] += rad[k] * d[1];
            acc1[2][k] += rad[k] * d[2];
        }
    }
    const float inv = 1.0f / rays;
    for (int k = 0; k < 3; ++k) {
        L0[k] = acc0[k] * inv + s.ambientFloor;
        for (int a = 0; a < 3; ++a) L1[a][k] = 3.0f * acc1[a][k] * inv;
    }
    live = inside < rays / 2;
}

}  // namespace

ProbeGrid bakeProbes(const Scene& s, const Settings& st,
                     const std::atomic<bool>* cancel,
                     const ProgressFn& progress) {
    ProbeGrid g;
    if (!st.probes || s.empty()) return g;
    // The grid spans the terrain, raised to cover anything built above it.
    float minX = s.terrainMinX, maxX = s.terrainMaxX;
    float minZ = s.terrainMinZ, maxZ = s.terrainMaxZ;
    if (maxX <= minX || maxZ <= minZ) {
        minX = s.bmin[0], maxX = s.bmax[0];
        minZ = s.bmin[2], maxZ = s.bmax[2];
    }
    const float spacing = st.probeSpacing;
    int nx = (int)std::ceil((maxX - minX) / spacing) + 1;
    int nz = (int)std::ceil((maxZ - minZ) / spacing) + 1;
    int ny = st.probeLevels;
    // A hard cap on the shipped table: 12 bytes + 1 per probe in EE RAM, and
    // the codegen'd array is compiled, not loaded. 32x4x32 = 4096 probes =
    // 52 KB, which is the shape the design settled on.
    const int kMaxProbes = 24000;
    while ((long long)nx * ny * nz > kMaxProbes) {
        if (nx >= nz && nx > 2)
            nx = (nx + 1) / 2;
        else if (nz > 2)
            nz = (nz + 1) / 2;
        else if (ny > 1)
            --ny;
        else
            break;
    }
    if (nx < 2) nx = 2;
    if (nz < 2) nz = 2;
    g.dim[0] = nx, g.dim[1] = ny, g.dim[2] = nz;
    g.step[0] = (maxX - minX) / std::max(1, nx - 1);
    g.step[1] = st.probeHeight;
    g.step[2] = (maxZ - minZ) / std::max(1, nz - 1);
    g.origin[0] = minX;
    g.origin[2] = minZ;
    // Level 0 sits half a step above the LOWEST ground in the grid, so a probe
    // never starts buried in a hill; the levels above stack from there.
    float lowest = 1e30f;
    for (int j = 0; j < nz; ++j)
        for (int i = 0; i < nx; ++i) {
            const float x = minX + g.step[0] * i;
            const float z = minZ + g.step[2] * j;
            lowest = std::min(lowest,
                              heightAtWorld(s.heights, s.hmW, s.hmD, s.hmWidth,
                                            s.hmDepth, x, z));
        }
    if (lowest > 1e29f) lowest = s.bmin[1];
    g.origin[1] = lowest + g.step[1] * 0.5f;

    const int total = nx * ny * nz;
    std::vector<float> coeff((size_t)total * 12, 0.0f);
    g.live.assign(total, 1);
    const int rays = std::max(32, st.rays);
    std::atomic<int> done{0};
    parallelFor(total, cancel, [&](int lo, int hi) {
        for (int idx = lo; idx < hi; ++idx) {
            if (cancel && cancel->load()) return;
            const int i = idx % nx;
            const int y = (idx / nx) % ny;
            const int j = idx / (nx * ny);
            const float wp[3] = {g.origin[0] + g.step[0] * i,
                                 g.origin[1] + g.step[1] * y,
                                 g.origin[2] + g.step[2] * j};
            float L0[3], L1[3][3];
            bool live = true;
            bakeOneProbe(s, wp, rays, (uint32_t)idx * 2246822519u, L0, L1, live);
            float* c = &coeff[(size_t)idx * 12];
            for (int k = 0; k < 3; ++k) c[k] = L0[k];
            for (int a = 0; a < 3; ++a)
                for (int k = 0; k < 3; ++k) c[3 + a * 3 + k] = L1[a][k];
            g.live[idx] = live ? 1 : 0;
            const int d = ++done;
            if (progress && (d & 255) == 0) progress((float)d / total);
        }
    });
    if (cancel && cancel->load()) return ProbeGrid();

    float peak = 0.0f;
    for (float v : coeff) peak = std::max(peak, std::fabs(v));
    g.scale = peak > 1e-4f ? peak : 1.0f;
    g.sh.resize(coeff.size());
    const float enc = 127.0f / g.scale;
    for (size_t i = 0; i < coeff.size(); ++i) {
        float v = coeff[i] * enc;
        if (v > 127.0f) v = 127.0f;
        if (v < -127.0f) v = -127.0f;
        g.sh[i] = (int8_t)std::lround(v);
    }
    if (progress) progress(1.0f);
    return g;
}

void sampleProbes(const ProbeGrid& g, const float wp[3], const float n[3],
                  float out[3]) {
    out[0] = out[1] = out[2] = 0.0f;
    if (g.empty()) return;
    float f[3];
    int i0[3];
    for (int k = 0; k < 3; ++k) {
        const float step = g.step[k] > 1e-6f ? g.step[k] : 1.0f;
        float t = (wp[k] - g.origin[k]) / step;
        if (t < 0.0f) t = 0.0f;
        if (t > g.dim[k] - 1.0f) t = (float)(g.dim[k] - 1);
        i0[k] = (int)t;
        if (i0[k] > g.dim[k] - 2) i0[k] = std::max(0, g.dim[k] - 2);
        f[k] = g.dim[k] > 1 ? t - i0[k] : 0.0f;
    }
    const float dec = g.scale / 127.0f;
    float acc[12] = {0};
    float wsum = 0.0f;
    for (int c = 0; c < 8; ++c) {
        const int dx = c & 1, dy = (c >> 1) & 1, dz = (c >> 2) & 1;
        const int ix = std::min(i0[0] + dx, g.dim[0] - 1);
        const int iy = std::min(i0[1] + dy, g.dim[1] - 1);
        const int iz = std::min(i0[2] + dz, g.dim[2] - 1);
        const int idx = ix + g.dim[0] * (iy + g.dim[1] * iz);
        if (!g.live[idx]) continue;  // dead probe: inside a solid, weighs zero
        const float w = (dx ? f[0] : 1.0f - f[0]) * (dy ? f[1] : 1.0f - f[1]) *
                        (dz ? f[2] : 1.0f - f[2]);
        if (w <= 0.0f) continue;
        wsum += w;
        const int8_t* sh = &g.sh[(size_t)idx * 12];
        for (int k = 0; k < 12; ++k) acc[k] += w * sh[k] * dec;
    }
    if (wsum <= 1e-5f) return;
    const float inv = 1.0f / wsum;
    for (int k = 0; k < 3; ++k) {
        // shade(n) = L0 + (2/3) * dot(L1, n) - the clamped-cosine convolution
        // of an L1 environment (see the ProbeGrid comment).
        float v = acc[k] * inv +
                  (2.0f / 3.0f) * (acc[3 + k] * inv * n[0] +
                                   acc[6 + k] * inv * n[1] +
                                   acc[9 + k] * inv * n[2]);
        if (v < 0.0f) v = 0.0f;
        if (v > 4.0f) v = 4.0f;
        out[k] = v;
    }
}

// --- signature + cache -------------------------------------------------------

uint64_t signature(const Project& p, const SceneData& sc, const Settings& st) {
    uint64_t h = 0xcbf29ce484222325ull;
    mix64(h, kCacheVersion);
    const ProjectSettings rs = project::resolvedSettings(p, sc);
    for (int k = 0; k < 3; ++k) {
        mixF(h, rs.skyColor[k]);
        mixF(h, rs.skyTopColor[k]);
        mixF(h, rs.lightDir[k]);
        mixF(h, rs.lightColor[k]);
    }
    mixF(h, rs.zenithSize);
    mix64(h, rs.skyDome ? 1 : 0);
    mixF(h, rs.ambient);
    mixF(h, rs.diffuse);
    mixF(h, rs.brightness);
    mix64(h, rs.aoEnabled ? 1 : 0);
    mixF(h, rs.aoStrength);
    mixF(h, rs.aoRadius);
    mixS(h, rs.terrainMaterial);
    mix64(h, st.rays);
    mix64(h, st.bounces);
    mixF(h, st.skyLight);
    mixF(h, st.sunLight);
    mixF(h, st.ambientFloor);
    mixF(h, st.probeSpacing);
    mixF(h, st.probeHeight);
    mix64(h, st.probeLevels);
    mix64(h, st.probes ? 1 : 0);
    mix64(h, sc.terrain.width);
    mix64(h, sc.terrain.depth);
    mix64(h, sc.hmW);
    mix64(h, sc.hmD);
    for (float v : sc.heights) mixF(h, v);

    // The files a bake reads: a repainted texture or an edited .mtl must make
    // the cache stale even though nothing in the model changed.
    //
    // CONTENT, not mtime. Two reasons, both real: a bake takes minutes and a
    // `touch` (or a checkout, or a copy) must not throw it away - and the
    // example projects ship their cache, which a fresh `git clone` would
    // otherwise invalidate the instant it landed on disk.
    std::map<std::string, int> seen;
    auto mixFile = [&](const std::string& rel) {
        if (rel.empty() || !seen.emplace(rel, 1).second) return;
        mixS(h, rel);
        uint64_t fh = 0, fsz = 0;
        if (wire::hashFile((fs::path(p.dir) / rel).string(), fh, fsz)) {
            mix64(h, fh);
            mix64(h, fsz);
        }
    };
    mixFile(rs.terrainMaterial);
    for (const SceneObject& o : sc.objects) {
        mix64(h, (uint64_t)o.type);
        mix64(h, o.castShadow ? 1 : 0);
        mix64(h, o.primDetail);
        mix64(h, o.physics ? 1 : 0);
        mix64(h, o.pickable ? 1 : 0);
        mix64(h, o.saveState ? 1 : 0);
        mix64(h, o.lightDynamic ? 1 : 0);
        mixF(h, o.lightBright);
        mixF(h, o.lightRadius);
        for (int k = 0; k < 3; ++k) {
            mixF(h, o.position[k]);
            mixF(h, o.rotation[k]);
            mixF(h, o.scale[k]);
            mixF(h, o.color[k]);
        }
        mixS(h, o.materialPath);
        mixS(h, o.modelPath);
        mixFile(o.materialPath);
        mixFile(o.modelPath);
    }
    return h;
}

std::string cachePath(const Project& p, int sceneIndex) {
    return (fs::path(p.dir) / ".res-baked" / "gi" /
            ("scene" + std::to_string(sceneIndex) + ".gi"))
        .string();
}

namespace {

template <class T>
void wr(std::ostream& f, const T& v) {
    f.write(reinterpret_cast<const char*>(&v), sizeof(T));
}
template <class T>
bool rd(std::istream& f, T& v) {
    f.read(reinterpret_cast<char*>(&v), sizeof(T));
    return (bool)f;
}
template <class V>
void wrVec(std::ostream& f, const V& v) {
    const uint32_t n = (uint32_t)v.size();
    wr(f, n);
    if (n) f.write(reinterpret_cast<const char*>(v.data()),
                   (std::streamsize)(n * sizeof(typename V::value_type)));
}
template <class V>
bool rdVec(std::istream& f, V& v) {
    uint32_t n = 0;
    if (!rd(f, n)) return false;
    if (n > 64u * 1024u * 1024u) return false;
    v.assign(n, typename V::value_type{});
    if (n) f.read(reinterpret_cast<char*>(v.data()),
                  (std::streamsize)(n * sizeof(typename V::value_type)));
    return (bool)f;
}

}  // namespace

bool write(const std::string& path, const Bake& b) {
    std::error_code ec;
    fs::create_directories(fs::path(path).parent_path(), ec);
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    wr(f, kCacheMagic);
    wr(f, kCacheVersion);
    wr(f, b.signature);
    // atlas. The `gi` flags travel WITH the pixels: they are what tells the
    // generated game to drop its own ambient + directional shade, and a cache
    // whose pixels say "all the light is here" while the flag says otherwise
    // renders the scene twice as bright as it was baked.
    wr(f, (uint8_t)(b.atlas.gi ? 1 : 0));
    wr(f, (uint8_t)(b.terrain.gi ? 1 : 0));
    wr(f, (int32_t)b.atlas.size);
    wrVec(f, b.atlas.alpha);
    wrVec(f, b.atlas.light);
    wrVec(f, b.atlas.firstRegion);
    wrVec(f, b.atlas.lit);
    wrVec(f, b.atlas.rects);
    // terrain map
    wr(f, (int32_t)b.terrain.size);
    wr(f, (uint8_t)(b.terrain.hasAlpha ? 1 : 0));
    wr(f, (uint8_t)(b.terrain.hasLight ? 1 : 0));
    wrVec(f, b.terrain.alpha);
    wrVec(f, b.terrain.light);
    // probes
    for (int k = 0; k < 3; ++k) wr(f, b.probes.origin[k]);
    for (int k = 0; k < 3; ++k) wr(f, b.probes.step[k]);
    for (int k = 0; k < 3; ++k) wr(f, (int32_t)b.probes.dim[k]);
    wr(f, b.probes.scale);
    wrVec(f, b.probes.sh);
    wrVec(f, b.probes.live);
    return (bool)f;
}

bool read(const std::string& path, Bake& b) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    uint32_t magic = 0, version = 0;
    if (!rd(f, magic) || !rd(f, version)) return false;
    if (magic != kCacheMagic || version != kCacheVersion) return false;
    if (!rd(f, b.signature)) return false;
    uint8_t gi8 = 0;
    if (!rd(f, gi8)) return false;
    b.atlas.gi = gi8 != 0;
    if (!rd(f, gi8)) return false;
    b.terrain.gi = gi8 != 0;
    int32_t v32 = 0;
    if (!rd(f, v32)) return false;
    b.atlas.size = v32;
    if (!rdVec(f, b.atlas.alpha) || !rdVec(f, b.atlas.light) ||
        !rdVec(f, b.atlas.firstRegion) || !rdVec(f, b.atlas.lit) ||
        !rdVec(f, b.atlas.rects))
        return false;
    if (!rd(f, v32)) return false;
    b.terrain.size = v32;
    uint8_t f8 = 0;
    if (!rd(f, f8)) return false;
    b.terrain.hasAlpha = f8 != 0;
    if (!rd(f, f8)) return false;
    b.terrain.hasLight = f8 != 0;
    if (!rdVec(f, b.terrain.alpha) || !rdVec(f, b.terrain.light)) return false;
    for (int k = 0; k < 3; ++k)
        if (!rd(f, b.probes.origin[k])) return false;
    for (int k = 0; k < 3; ++k)
        if (!rd(f, b.probes.step[k])) return false;
    for (int k = 0; k < 3; ++k) {
        if (!rd(f, v32)) return false;
        b.probes.dim[k] = v32;
    }
    if (!rd(f, b.probes.scale)) return false;
    if (!rdVec(f, b.probes.sh) || !rdVec(f, b.probes.live)) return false;
    if ((int)b.probes.live.size() != b.probes.count() ||
        (int)b.probes.sh.size() != b.probes.count() * 12) {
        b.probes = ProbeGrid();
    }
    b.valid = true;
    return true;
}

Bake load(const Project& p, int sceneIndex) {
    Bake b;
    if (sceneIndex < 0 || sceneIndex >= (int)p.scenes.size()) return b;
    const Settings st = settingsOf(p.settings);
    if (!st.enabled) return b;
    if (!read(cachePath(p, sceneIndex), b)) return Bake();
    if (b.signature != signature(p, p.scenes[sceneIndex], st)) return Bake();
    return b;
}

// --- the whole bake for one scene -------------------------------------------

Bake bakeScene(const Project& p, int sceneIndex,
               const std::atomic<bool>* cancel, const ProgressFn& progress) {
    Bake out;
    if (sceneIndex < 0 || sceneIndex >= (int)p.scenes.size()) return out;
    const SceneData& sc = p.scenes[sceneIndex];
    const Settings st = settingsOf(p.settings);
    if (!st.enabled) return out;
    out.signature = signature(p, sc, st);

    const aobake::ModelAabbFn aabbFn = [&](const SceneObject& o, float* mn,
                                           float* mx) {
        if (o.modelPath.empty()) return false;
        return aobake::objAabb((fs::path(p.dir) / o.modelPath).string(), mn, mx);
    };

    auto step = [&](float base, float span, float t) {
        if (progress) progress(base + span * t);
    };

    Scene s = build(p, sc, st);
    if (cancel && cancel->load()) return Bake();
    step(0.0f, 0.0f, 0.0f);
    solve(s, st, cancel, [&](float t) { step(0.05f, 0.35f, t); });
    if (cancel && cancel->load()) return Bake();

    // The light source the lightmap bakes: one final gather per texel against
    // the solved scene. The seed is the texel's own atlas coordinate, handed
    // in by aobake - so a texel's sample set never depends on which thread or
    // which region reached it first.
    aobake::LightFn giLight = [&](const float wp[3], const float n[3],
                                  uint32_t seed, float outRgb[3]) {
        gather(s, wp, n, seed, st.rays, outRgb);
    };
    out.atlas = aobake::bakeSceneLightAtlas(p, sc, aabbFn, &giLight);
    step(0.40f, 0.0f, 0.0f);
    if (cancel && cancel->load()) return Bake();
    // A TEXTURED terrain cannot take a lightmap for the same reason a textured
    // object cannot: the additive pass adds a flat term over the texture and
    // blows out its dark texels, and the base pass would have to go black to
    // avoid double-counting - which throws the texture away. So a textured
    // ground gets an occlusion-only map here and takes its light from the
    // probe grid per vertex, which its dense cell grid can carry. The light
    // channel is left EMPTY rather than filled with the legacy emissive bake:
    // the probes already contain those emitters, and two sources of the same
    // photons is the one mistake this whole design is arranged to avoid.
    const ProjectSettings rs = project::resolvedSettings(p, sc);
    bool terrainTextured = false;
    if (!rs.terrainMaterial.empty()) {
        std::vector<objparser::MtlMaterial> mats;
        if (objparser::loadMtl(
                (fs::path(p.dir) / rs.terrainMaterial).string(), mats) &&
            !mats.empty())
            terrainTextured = !mats.front().texture.empty();
    }
    out.terrain = aobake::terrainAOMap(
        sc.heights, sc.hmW, sc.hmD, (float)sc.terrain.width,
        (float)sc.terrain.depth, aobake::collectOccluders(sc.objects, aabbFn),
        terrainTextured ? std::vector<aobake::Emitter>()
                        : aobake::collectEmitters(p.dir, sc.objects, aabbFn),
        rs.aoRadius, rs.aoStrength, rs.aoEnabled,
        terrainTextured ? nullptr : &giLight);
    step(0.70f, 0.0f, 0.0f);
    if (cancel && cancel->load()) return Bake();
    out.probes = bakeProbes(s, st, cancel, [&](float t) { step(0.70f, 0.3f, t); });
    if (cancel && cancel->load()) return Bake();
    out.valid = true;
    if (progress) progress(1.0f);
    return out;
}

// --- the progressive baker ---------------------------------------------------

void Baker::start(const Project& p, std::vector<int> scenes) {
    cancel();
    if (scenes.empty())
        for (int i = 0; i < (int)p.scenes.size(); ++i) scenes.push_back(i);
    cancel_ = false;
    running_ = true;
    progress_ = 0.0f;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        status_ = "Preparing...";
    }
    worker_ = std::thread(&Baker::run, this, p, scenes);
}

void Baker::cancel() {
    cancel_ = true;
    if (worker_.joinable()) worker_.join();
    running_ = false;
}

std::string Baker::status() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return status_;
}

void Baker::run(Project p, std::vector<int> scenes) {
    const int n = (int)scenes.size();
    for (int i = 0; i < n && !cancel_.load(); ++i) {
        const int si = scenes[i];
        {
            std::lock_guard<std::mutex> lk(mutex_);
            status_ = "Baking " +
                      (si < (int)p.scenes.size() ? p.scenes[si].name
                                                 : std::to_string(si)) +
                      " (" + std::to_string(i + 1) + "/" + std::to_string(n) + ")";
        }
        const Bake b = bakeScene(p, si, &cancel_, [&](float t) {
            progress_ = (i + t) / n;
        });
        if (cancel_.load()) break;
        if (b.valid) write(cachePath(p, si), b);
        version_.fetch_add(1);
        progress_ = (float)(i + 1) / n;
    }
    {
        std::lock_guard<std::mutex> lk(mutex_);
        status_ = cancel_.load() ? "Cancelled" : "Done";
    }
    version_.fetch_add(1);
    running_ = false;
}

}  // namespace gibake
