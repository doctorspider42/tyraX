#include "reflscenery.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <map>
#include <set>

#include <stb_image.h>  // implementation lives in app.cpp

#include "aobake.hpp"
#include "objparser.hpp"
#include "roadgen.hpp"
#include "scrollsim.hpp"

namespace fs = std::filesystem;

namespace reflscenery {

const char* verdictText(Verdict v) {
    switch (v) {
        case Verdict::Box: return "drawn as a box";
        case Verdict::Explicit: return "drawn through Show in reflections";
        case Verdict::NotSolid: return "no solid shape";
        case Verdict::Moves: return "can move at runtime";
        case Verdict::Animated: return "animated model";
        case Verdict::Merged: return "procedural chunk";
        case Verdict::TooSmall: return "too small";
        case Verdict::NoModel: return "model not readable";
    }
    return "";
}

namespace {

bool animatedPath(const std::string& p) {
    const size_t dot = p.find_last_of('.');
    if (dot == std::string::npos) return false;
    std::string ext = p.substr(dot);
    for (char& c : ext) c = (char)std::tolower((unsigned char)c);
    return ext == ".glb" || ext == ".gltf" || ext == ".fbx";
}

// A procedural bake merges every instance of one asset in a chunk into one
// model (procbake.cpp, "procgen-<vol>-<asset>-x<i>z<j>.obj"): its bounds are
// the chunk, not a building.
bool proceduralChunk(const std::string& modelPath) {
    return fs::path(modelPath).filename().string().rfind("procgen-", 0) == 0;
}

// Mean colour of a texture, cached per path; alpha-0 texels are holes, not
// colour (the gibake textureMean rule).
using TexCache = std::map<std::string, std::array<float, 3>>;
const std::array<float, 3>& textureMean(const std::string& abs, TexCache& cache) {
    auto it = cache.find(abs);
    if (it != cache.end()) return it->second;
    std::array<float, 3> mean{1.0f, 1.0f, 1.0f};
    int w = 0, h = 0, comp = 0;
    unsigned char* px = stbi_load(abs.c_str(), &w, &h, &comp, 4);
    if (px && w > 0 && h > 0) {
        const int stepX = std::max(1, w / 64), stepY = std::max(1, h / 64);
        double acc[3] = {0, 0, 0};
        long long n = 0;
        for (int y = 0; y < h; y += stepY)
            for (int x = 0; x < w; x += stepX) {
                const unsigned char* q = px + ((size_t)y * w + x) * 4;
                if (q[3] == 0) continue;
                acc[0] += q[0], acc[1] += q[1], acc[2] += q[2];
                ++n;
            }
        if (n > 0)
            for (int k = 0; k < 3; ++k) mean[k] = (float)(acc[k] / n / 255.0);
    }
    if (px) stbi_image_free(px);
    return cache.emplace(abs, mean).first->second;
}

struct Albedo {
    float rgb[3] = {1, 1, 1};
    bool ok = false;
};

// A model's albedo: Kd x texture mean per part, weighted by the part's
// triangle count, so a tower that is mostly concrete reads as concrete.
Albedo modelAlbedo(const Project& p, const SceneObject& o,
                   std::map<std::string, Albedo>& cache, TexCache& tex) {
    const std::string key = o.modelPath + "|" + o.materialPath;
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;
    Albedo a;
    objparser::Model m;
    const std::string mtl =
        o.materialPath.empty() ? std::string() : p.filePath(o.materialPath);
    if (objparser::load(p.filePath(o.modelPath), m, mtl)) {
        const fs::path base = o.materialPath.empty()
                                  ? fs::path(p.filePath(o.modelPath)).parent_path()
                                  : fs::path(mtl).parent_path();
        double acc[3] = {0, 0, 0}, wsum = 0;
        for (const objparser::Submesh& sm : m.submeshes) {
            const double w = (double)(sm.verts.size() / 8);
            if (w <= 0) continue;
            float c[3] = {sm.kd[0], sm.kd[1], sm.kd[2]};
            if (!sm.texture.empty()) {
                const std::array<float, 3>& t =
                    textureMean((base / sm.texture).lexically_normal().string(), tex);
                for (int k = 0; k < 3; ++k) c[k] *= t[k];
            }
            for (int k = 0; k < 3; ++k) acc[k] += c[k] * w;
            wsum += w;
        }
        if (wsum > 0)
            for (int k = 0; k < 3; ++k) a.rgb[k] = (float)(acc[k] / wsum);
        a.ok = true;
    }
    return cache.emplace(key, a).first->second;
}

// A primitive's albedo: its material's first entry (the one primitives take
// their surface from), Kd x texture mean.
Albedo materialAlbedo(const Project& p, const std::string& materialPath,
                      std::map<std::string, Albedo>& cache, TexCache& tex) {
    auto it = cache.find("mtl|" + materialPath);
    if (it != cache.end()) return it->second;
    Albedo a;
    a.ok = true;
    std::vector<objparser::MtlMaterial> mats;
    if (!materialPath.empty() &&
        objparser::loadMtl(p.filePath(materialPath), mats) && !mats.empty()) {
        const objparser::MtlMaterial& m = mats.front();
        for (int k = 0; k < 3; ++k) a.rgb[k] = m.kd[k];
        if (!m.texture.empty()) {
            const fs::path abs =
                fs::path(p.filePath(materialPath)).parent_path() / m.texture;
            const std::array<float, 3>& t =
                textureMean(abs.lexically_normal().string(), tex);
            for (int k = 0; k < 3; ++k) a.rgb[k] *= t[k];
        }
    }
    return cache.emplace("mtl|" + materialPath, a).first->second;
}

// Moves at runtime, ignoring the streaming layer: a layer only decides
// whether the object EXISTS, and the game draws a layer's boxes only while
// that layer is resident.
bool moves(const SceneObject& o, const std::set<std::string>& refs) {
    if (o.layer.empty()) return project::objectRuntimeMovable(o, refs);
    SceneObject c = o;
    c.layer.clear();
    return project::objectRuntimeMovable(c, refs);
}

struct Context {
    std::set<std::string> refs;
    std::vector<char> belt;
    aobake::ModelAabbFn aabb;
};

Context contextFor(const Project& p, const SceneData& sc) {
    Context c;
    c.refs = project::runtimeRefNames(p, sc.objects);
    c.belt = scrollsim::memberTemplateFlags(sc.objects);
    c.aabb = [&p](const SceneObject& o, float mn[3], float mx[3]) {
        if (o.modelPath.empty()) return false;
        return aobake::objAabb(p.filePath(o.modelPath), mn, mx);
    };
    return c;
}

Verdict judge(const Project& p, const SceneData& sc, int i, const Context& c,
              aobake::Occluder* shape) {
    const SceneObject& o = sc.objects[(size_t)i];
    if (o.reflected) return Verdict::Explicit;
    switch (o.type) {
        case PrimitiveType::Box:
        case PrimitiveType::Sphere:
        case PrimitiveType::Cylinder:
        case PrimitiveType::Cone:
        case PrimitiveType::Plane:
        case PrimitiveType::SavePoint:
        case PrimitiveType::Model: break;
        default: return Verdict::NotSolid;
    }
    if (o.collisionMode == 3) return Verdict::NotSolid;
    if (o.type == PrimitiveType::Model) {
        if (o.modelPath.empty()) return Verdict::NoModel;
        if (animatedPath(o.modelPath)) return Verdict::Animated;
        if (proceduralChunk(o.modelPath)) return Verdict::Merged;
    }
    if (c.belt[(size_t)i] || moves(o, c.refs)) return Verdict::Moves;
    aobake::Occluder oc;
    if (!aobake::objectShape(o, i, c.aabb, oc))
        return o.type == PrimitiveType::Model ? Verdict::NoModel
                                              : Verdict::NotSolid;
    const float longest = std::max(oc.half[0], std::max(oc.half[1], oc.half[2]));
    if (longest < kMinHalfExtent) return Verdict::TooSmall;
    if (shape) *shape = oc;
    return Verdict::Box;
}

int layerIndex(const SceneData& sc, const std::string& name) {
    if (name.empty()) return -1;
    for (size_t i = 0; i < sc.layers.size(); ++i)
        if (sc.layers[i].name == name) return (int)i;
    return -1;
}

// A material's Kd x texture mean, false when the .mtl cannot be read.
bool mtlAlbedo(const Project& p, const std::string& materialPath, float out[3],
               TexCache& tex) {
    std::vector<objparser::MtlMaterial> mats;
    if (materialPath.empty() || !objparser::loadMtl(p.filePath(materialPath), mats) ||
        mats.empty())
        return false;
    const objparser::MtlMaterial& m = mats.front();
    for (int k = 0; k < 3; ++k) out[k] = m.kd[k];
    if (!m.texture.empty()) {
        const fs::path abs = fs::path(p.filePath(materialPath)).parent_path() / m.texture;
        const std::array<float, 3>& t = textureMean(abs.lexically_normal().string(), tex);
        for (int k = 0; k < 3; ++k) out[k] *= t[k];
    }
    return true;
}

unsigned char toByte(float v) {
    v = std::min(std::max(v, 0.0f), 0.92f);
    return (unsigned char)std::lround(v * 255.0f);
}

}  // namespace

Ground ground(const Project& p, const SceneData& sc) {
    Ground g;
    if (!sc.terrain.enabled) return g;
    g.ok = true;
    g.sizeX = (float)sc.terrain.width;
    g.sizeZ = (float)sc.terrain.depth;
    g.minX = -g.sizeX * 0.5f;
    g.minZ = -g.sizeZ * 0.5f;
    TexCache tex;
    // The base: its material, or the untextured checker's average green
    // (gibake's figure for the same ground).
    float base[3] = {0.35f, 0.45f, 0.3f};
    const ProjectSettings rs = project::resolvedSettings(p, sc);
    if (!rs.terrainMaterial.empty()) mtlAlbedo(p, rs.terrainMaterial, base, tex);
    const int n = (int)sc.terrainLayers.size();
    std::vector<std::array<float, 3>> layer((size_t)n, {base[0], base[1], base[2]});
    for (int l = 0; l < n; ++l) {
        float c[3];
        if (mtlAlbedo(p, sc.terrainLayers[(size_t)l].material, c, tex))
            layer[(size_t)l] = {c[0], c[1], c[2]};
    }
    const bool splat = n > 0 && sc.splatW > 1 && sc.splatD > 1 &&
                       (int)sc.splat.size() >= sc.splatW * sc.splatD * n;
    g.rgb.assign((size_t)kGroundGrid * kGroundGrid * 3, 0);
    for (int z = 0; z < kGroundGrid; ++z)
        for (int x = 0; x < kGroundGrid; ++x) {
            float c[3] = {base[0], base[1], base[2]};
            if (splat) {
                // Nearest splat vertex to the cell centre; layers blend over
                // the base in order, each by its own weight (the draw order).
                const float u = (x + 0.5f) / kGroundGrid, v = (z + 0.5f) / kGroundGrid;
                const int sx = std::min(sc.splatW - 1, (int)(u * (sc.splatW - 1) + 0.5f));
                const int sz = std::min(sc.splatD - 1, (int)(v * (sc.splatD - 1) + 0.5f));
                const uint8_t* w = &sc.splat[((size_t)sz * sc.splatW + sx) * n];
                for (int l = 0; l < n; ++l) {
                    const float a = w[l] / 255.0f;
                    for (int k = 0; k < 3; ++k)
                        c[k] = c[k] * (1.0f - a) + layer[(size_t)l][k] * a;
                }
            }
            unsigned char* o = &g.rgb[((size_t)z * kGroundGrid + x) * 3];
            for (int k = 0; k < 3; ++k) o[k] = toByte(c[k]);
        }
    // The roads are painted INTO the map, each in its surface's mean colour
    // and by how much of a texel it covers (4x4 samples against its own
    // tessellated ribbon, full width - the faded edges are road to a 128-pixel
    // probe). The game then never asks roadSurfaceAt() while it rebuilds the
    // grid: that was 441 triangle searches every time the eye crossed a cell.
    const float cellX = g.sizeX / kGroundGrid, cellZ = g.sizeZ / kGroundGrid;
    for (const SceneObject& o : sc.objects) {
        if (o.type != PrimitiveType::Road || o.roadPoints.size() < 4) continue;
        float rc[3] = {g.road[0] / 255.0f, g.road[1] / 255.0f, g.road[2] / 255.0f};
        const std::string texRel = project::resolveRoadTexture(p, o.roadTexture);
        if (!texRel.empty()) {
            const std::array<float, 3>& t =
                textureMean(fs::path(p.filePath(texRel)).lexically_normal().string(), tex);
            for (int k = 0; k < 3; ++k) rc[k] = t[k];
        }
        std::vector<roadgen::Vertex> tris;
        roadgen::tessellate(o.roadPoints, o.roadWidth, [](float, float) { return 0.0f; },
                            tris, {}, o.roadSampleStep);
        if (tris.empty()) continue;
        float lo[2] = {tris[0].x, tris[0].z}, hi[2] = {tris[0].x, tris[0].z};
        for (const roadgen::Vertex& v : tris) {
            lo[0] = std::min(lo[0], v.x); hi[0] = std::max(hi[0], v.x);
            lo[1] = std::min(lo[1], v.z); hi[1] = std::max(hi[1], v.z);
        }
        roadgen::Surface surf;
        surf.add(tris);
        surf.build();
        const int x0 = std::max(0, (int)std::floor((lo[0] - g.minX) / cellX));
        const int x1 = std::min(kGroundGrid - 1, (int)std::floor((hi[0] - g.minX) / cellX));
        const int z0 = std::max(0, (int)std::floor((lo[1] - g.minZ) / cellZ));
        const int z1 = std::min(kGroundGrid - 1, (int)std::floor((hi[1] - g.minZ) / cellZ));
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x) {
                int hits = 0;
                for (int sz = 0; sz < 4; ++sz)
                    for (int sx = 0; sx < 4; ++sx)
                        if (surf.at(g.minX + (x + (sx + 0.5f) / 4.0f) * cellX,
                                    g.minZ + (z + (sz + 0.5f) / 4.0f) * cellZ) >
                            roadgen::Surface::kNone)
                            ++hits;
                if (hits == 0) continue;
                const float a = hits / 16.0f;
                unsigned char* px = &g.rgb[((size_t)z * kGroundGrid + x) * 3];
                for (int k = 0; k < 3; ++k)
                    px[k] = toByte((px[k] / 255.0f) * (1.0f - a) + rc[k] * a);
            }
    }
    return g;
}

Verdict verdictFor(const Project& p, const SceneData& sc, int index) {
    if (index < 0 || index >= (int)sc.objects.size()) return Verdict::NotSolid;
    const Context c = contextFor(p, sc);
    return judge(p, sc, index, c, nullptr);
}

std::vector<Box> collect(const Project& p, const SceneData& sc) {
    std::vector<Box> out;
    const Context c = contextFor(p, sc);
    std::map<std::string, Albedo> albedoCache;
    TexCache texCache;
    for (int i = 0; i < (int)sc.objects.size(); ++i) {
        aobake::Occluder oc;
        if (judge(p, sc, i, c, &oc) != Verdict::Box) continue;
        const SceneObject& o = sc.objects[(size_t)i];
        const Albedo a =
            o.type == PrimitiveType::Model
                ? modelAlbedo(p, o, albedoCache, texCache)
                : materialAlbedo(p, o.materialPath, albedoCache, texCache);
        Box b;
        for (int k = 0; k < 3; ++k) b.center[k] = oc.pos[k];
        // A sphere's shape carries only a radius; its box is axis-aligned.
        for (int ax = 0; ax < 3; ++ax)
            for (int k = 0; k < 3; ++k)
                b.axis[ax][k] = oc.sphere ? (ax == k ? oc.half[0] : 0.0f)
                                          : oc.axis[ax][k] * oc.half[ax];
        for (int k = 0; k < 3; ++k) {
            float v = o.color[k] * a.rgb[k];
            // The same cap gibake puts on an albedo: nothing reflects more
            // than it receives.
            v = std::min(std::max(v, 0.0f), 0.92f);
            b.rgb[k] = (unsigned char)std::lround(v * 255.0f);
        }
        b.layer = layerIndex(sc, o.layer);
        b.object = i;
        out.push_back(b);
    }
    return out;
}

}  // namespace reflscenery
