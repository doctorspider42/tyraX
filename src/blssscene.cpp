#include "blssscene.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include <memory>

#include "fbxparser.hpp"
#include "glbparser.hpp"
#include "objparser.hpp"
#include "primmesh.hpp"
#include "project.hpp"
#include "sequence.hpp"

// Contract and rationale: blssscene.hpp. Three parts, in order: the geometry
// walk (objects, models, terrain), the camera-path builder, and loadProject()
// driving them.
//
// THE RULE THIS FILE HAS TO KEEP is the one at the top of blsscorpus.cpp,
// restated for real content: the images it enables may be as good as the host
// can make them, but everything the NETWORK is told about a frame comes out of
// a BagProxy that the EE could have produced. So this file never invents a
// mesh the game does not draw, never merges two draws the game submits apart
// (a merged proxy is a lie about where the geometry is), and never emits a
// proxy for a bag the engine opts out of - see SceneMesh::proxy and its twin
// PipelineInfoBag::blssProxy.

namespace fs = std::filesystem;

namespace blss {

namespace {

constexpr float kPi = 3.14159265358979f;

inline float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// Rotation order X, then Y, then Z - the twin of templates.cpp rotated(),
// gibake's rotate3() and the viewport's model matrix. A different order here
// would put the project's own props somewhere the game does not draw them.
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

// Bilinear height over the vertex grid - the host twin of the generated
// terrainHeightAtScene (gibake and aobake carry the same helper; keep the
// sampling identical or a camera path floats above the ground the game walks
// on).
float heightAtWorld(const std::vector<float>& h, int w, int d, float width,
                    float depth, float x, float z) {
    if (w < 2 || d < 2 || static_cast<int>(h.size()) < w * d) return 0.0f;
    float gx = (x + width * 0.5f) / width * (w - 1);
    float gz = (z + depth * 0.5f) / depth * (d - 1);
    gx = clampf(gx, 0.0f, w - 1.001f);
    gz = clampf(gz, 0.0f, d - 1.001f);
    const int ix = static_cast<int>(gx), iz = static_cast<int>(gz);
    const float fx = gx - ix, fz = gz - iz;
    const auto at = [&](int a, int b) { return h[static_cast<size_t>(b) * w + a]; };
    const float t = at(ix, iz) * (1 - fx) + at(ix + 1, iz) * fx;
    const float b = at(ix, iz + 1) * (1 - fx) + at(ix + 1, iz + 1) * fx;
    return t * (1 - fz) + b * fz;
}

// --------------------------------------------------------------- lighting ---

// The scene's directional light, as the generated game bakes it into vertex
// colours (templates.cpp shadeOf). Twinned deliberately: the corpus'
// ground-truth image is the picture the console would draw, and a differently
// exposed one would teach the oracle contrasts that are not there.
struct Light {
    float dir[3] = {0, 1, 0};
    float ambient = 0.55f, diffuse = 0.45f;
    float color[3] = {1, 1, 1};
    float brightness = 1.0f;

    void shade(const float n[3], float out[3]) const {
        float d = n[0] * dir[0] + n[1] * dir[1] + n[2] * dir[2];
        if (d < 0.0f) d = 0.0f;
        const float base = diffuse * d;
        for (int k = 0; k < 3; ++k)
            out[k] = std::min(1.0f, brightness * (ambient + base * color[k]));
    }
};

Light lightOf(const ProjectSettings& rs) {
    Light l;
    float d[3] = {rs.lightDir[0], rs.lightDir[1], rs.lightDir[2]};
    const float len = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (len > 1e-6f)
        for (int k = 0; k < 3; ++k) l.dir[k] = d[k] / len;
    l.ambient = rs.ambient;
    l.diffuse = rs.diffuse;
    for (int k = 0; k < 3; ++k) l.color[k] = rs.lightColor[k];
    l.brightness = rs.brightness;
    return l;
}

// --------------------------------------------------------------- materials ---

// One resolved .mtl, cached per project-relative path. Reading a .mtl is cheap
// and a scene assigns the same handful of them over and over.
struct Mat {
    bool loaded = false;
    std::string texture;  // ABSOLUTE path to the map_Kd PNG, "" = flat
    float kd[3] = {1, 1, 1};
};
using MatCache = std::map<std::string, Mat>;

const Mat& materialOf(const Project& p, const std::string& rel, MatCache& cache) {
    auto it = cache.find(rel);
    if (it != cache.end()) return it->second;
    Mat m;
    if (!rel.empty()) {
        std::vector<objparser::MtlMaterial> mats;
        if (objparser::loadMtl(p.filePath(rel), mats) && !mats.empty()) {
            const objparser::MtlMaterial& f = mats.front();
            m.loaded = true;
            for (int k = 0; k < 3; ++k) m.kd[k] = f.kd[k];
            if (!f.texture.empty()) {
                // map_Kd is relative to the .mtl's own directory, and the
                // normalisation is load-bearing: texbake copies assets to the
                // normalised location and the PS2 cannot walk "..".
                const fs::path abs =
                    (fs::path(p.filePath(rel)).parent_path() / f.texture)
                        .lexically_normal();
                m.texture = abs.string();
            }
        }
    }
    return cache.emplace(rel, m).first->second;
}

// ---------------------------------------------------------------- geometry ---

// Appends one triangle soup (primmesh / objparser layout: 8 floats per vertex -
// pos, normal, uv) transformed into world space, with the directional light
// baked into the vertex colours. `uvScale` is the material's own "-s" tiling.
void appendSoup(SceneMesh& out, const std::vector<float>& verts,
                const float pos[3], const float rot[3], const float scale[3],
                const float tint[3], const Light& light, const float uvScale[2]) {
    const size_t n = verts.size() / 8;
    const int base = static_cast<int>(out.vert.size());
    out.vert.reserve(out.vert.size() + n);
    for (size_t i = 0; i < n; ++i) {
        const float* v = &verts[i * 8];
        float lp[3] = {v[0] * scale[0], v[1] * scale[1], v[2] * scale[2]};
        SceneVert sv;
        rotate3(lp, rot, sv.p);
        for (int k = 0; k < 3; ++k) sv.p[k] += pos[k];
        // The inverse transpose of an axis-aligned scale is 1/scale.
        float ln[3] = {v[3] / (scale[0] != 0 ? scale[0] : 1.0f),
                       v[4] / (scale[1] != 0 ? scale[1] : 1.0f),
                       v[5] / (scale[2] != 0 ? scale[2] : 1.0f)};
        float wn[3];
        rotate3(ln, rot, wn);
        const float l = std::sqrt(wn[0] * wn[0] + wn[1] * wn[1] + wn[2] * wn[2]);
        if (l > 1e-6f)
            for (int k = 0; k < 3; ++k) wn[k] /= l;
        float sh[3];
        light.shade(wn, sh);
        for (int k = 0; k < 3; ++k) sv.c[k] = clampf(tint[k] * sh[k], 0.0f, 1.0f);
        sv.u = v[6] * uvScale[0];
        sv.v = v[7] * uvScale[1];
        out.vert.push_back(sv);
    }
    out.idx.reserve(out.idx.size() + n);
    for (size_t i = 0; i < n; ++i) out.idx.push_back(base + static_cast<int>(i));
}

std::vector<float> primitiveSoup(PrimitiveType type, int detail) {
    const int d = clampPrimDetail(type, detail);
    switch (type) {
        case PrimitiveType::Box:
        case PrimitiveType::SavePoint: return primmesh::unitBox(d);
        case PrimitiveType::Sphere: return primmesh::unitSphere(d);
        case PrimitiveType::Cylinder: return primmesh::unitCylinder(d);
        case PrimitiveType::Cone: return primmesh::unitCone(d);
        case PrimitiveType::Plane: return primmesh::unitPlane();
        // A decal is a unit quad in XY facing +Z, textured through its
        // material's map_Kd with the alpha TEST - so it is a cutout, and one of
        // the few places a generated game has a silhouette the GS cannot
        // antialias. Worth drawing; the caller sets `cutout`.
        case PrimitiveType::Decal: return primmesh::unitPlane();
        default: return {};
    }
}

// Does this object put geometry on the screen? The complement of what the
// generated game skips: markers, spawn points, the player capsule, cameras,
// areas, sound emitters and lights draw nothing a bag proxy could describe.
// Mirrors and portals DO draw, but their content is a foreign view the engine
// keeps out of the BLSS grid entirely (RendererCore3D::isForeignViewActive), so
// they are left out here for the same reason.
bool drawsGeometry(const SceneObject& o) {
    switch (o.type) {
        case PrimitiveType::Box:
        case PrimitiveType::SavePoint:
        case PrimitiveType::Sphere:
        case PrimitiveType::Cylinder:
        case PrimitiveType::Cone:
        case PrimitiveType::Plane:
        case PrimitiveType::Decal: return true;
        case PrimitiveType::Model:
            // STATIC models only; the animated ones go through appendAnimObject
            // instead, because their geometry is a function of the frame.
            //
            // This used to return false for an animated model and say that
            // animated .glb "goes down the dynamic pipeline, which does not feed
            // BLSS at all". That was wrong about THIS engine.
            // `updateAndRenderAnimObjects` skins on the EE and submits the
            // skinned arrays through `stapip.core.render()`, so a spider is one
            // more static bag as far as StaPipCore is concerned, `blssProxy`
            // defaults to true, and the console's feature grid sees it. See
            // AnimMesh in blssscene.hpp.
            return !o.modelPath.empty() && !isAnimatedModelPath(o.modelPath);
        default: return false;
    }
}

// Does this object draw an ANIMATED model? Same list of exclusions as above -
// the type must be Model and the path must be one the editor bakes to .tskl.
bool drawsAnimated(const SceneObject& o) {
    return o.type == PrimitiveType::Model && !o.modelPath.empty() &&
           isAnimatedModelPath(o.modelPath);
}

// --- the objects -------------------------------------------------------------

void appendObject(std::vector<SceneMesh>& out, const Project& p,
                  const SceneObject& o, const Light& light, MatCache& cache) {
    const Mat& mat = materialOf(p, o.materialPath, cache);

    if (o.type == PrimitiveType::Model) {
        objparser::Model m;
        const std::string abs = p.filePath(o.modelPath);
        const std::string mtl =
            o.materialPath.empty() ? std::string() : p.filePath(o.materialPath);
        if (!objparser::load(abs, m, mtl)) return;
        // ONE SUBMESH IS ONE BAG. The generated game emits one StaPipBag per
        // material part of a model (templates.cpp GeoPart), so a two-material
        // model is two proxies on the console and must be two here.
        const fs::path base = o.materialPath.empty()
                                  ? fs::path(abs).parent_path()
                                  : fs::path(mtl).parent_path();
        for (const objparser::Submesh& sm : m.submeshes) {
            if (sm.verts.empty()) continue;
            SceneMesh mesh;
            float tint[3];
            for (int k = 0; k < 3; ++k) tint[k] = o.color[k] * sm.kd[k];
            if (!sm.texture.empty())
                mesh.texture =
                    (base / sm.texture).lexically_normal().string();
            for (int k = 0; k < 3; ++k) mesh.tint[k] = tint[k];
            mesh.viewDist = o.drawDistance;
            const float uv[2] = {1.0f, 1.0f};  // the .obj's own UVs, as authored
            appendSoup(mesh, sm.verts, o.position, o.rotation, o.scale, tint,
                       light, uv);
            out.push_back(std::move(mesh));
        }
        return;
    }

    const std::vector<float> soup = primitiveSoup(o.type, o.primDetail);
    if (soup.empty()) return;
    SceneMesh mesh;
    float tint[3];
    for (int k = 0; k < 3; ++k) tint[k] = o.color[k] * mat.kd[k];
    mesh.texture = mat.texture;
    for (int k = 0; k < 3; ++k) mesh.tint[k] = tint[k];
    mesh.cutout = o.type == PrimitiveType::Decal;
    mesh.viewDist = o.drawDistance;
    // PARITY, NOT PREFERENCE, and it cost a look at the generated code: the
    // game's own primitive builders (templates.cpp addBox/addSphere/...) emit
    // UVs that span 0..1 per FACE and ignore the material's `map_Kd -s`
    // tiling entirely. Only the TERRAIN honours it, through
    // resolveTerrainMaterial's `tile`. Folding the tiling in here would make
    // the corpus' picture a different picture from the console's.
    const float uv[2] = {1.0f, 1.0f};
    appendSoup(mesh, soup, o.position, o.rotation, o.scale, tint, light, uv);
    out.push_back(std::move(mesh));
}

// --- the animated models -----------------------------------------------------

// THE CORPUS' FRAME RATE, and it is not a free parameter. Two consecutive
// corpus frames ARE two consecutive console frames - the history is one frame
// deep, the jitter phase alternates every frame, and `motion` is the
// reprojection between them - so the pose has to advance by exactly one console
// frame between them or the corpus teaches the temporal channel a slower world
// than it will run in. PAL is 50 Hz, which is what every generated project this
// feature has been measured on runs at.
constexpr float kAnimFps = 50.0f;

// One .glb baked once and shared by every instance of it in the project. The
// bake is the expensive part (it CPU-skins every clip at kAnimFps), the poses
// are identical for two spiders of the same model, and only the transform
// differs - which is applied per instance below.
using BakeCache = std::map<std::string, std::shared_ptr<glbparser::Baked>>;

// The content-forward correction, around the model's OWN Y, applied between the
// scale and the authored rotation. Twinned with updateAndRenderAnimObjects'
// `preYaw` lambda; folding it into `rotation.y` instead would only agree when
// the X and Z rotations are zero, which is not something a scene promises.
void preYaw(float v[3], float deg) {
    if (deg == 0.0f) return;
    const float a = deg * kPi / 180.0f;
    const float c = std::cos(a), s = std::sin(a);
    const float x = v[0] * c + v[2] * s;
    const float z = -v[0] * s + v[2] * c;
    v[0] = x, v[2] = z;
}

void appendAnimObject(std::vector<AnimMesh>& out,
                      std::vector<std::vector<unsigned char>>& embedded,
                      const Project& p, const SceneObject& o, const Light& light,
                      BakeCache& cache) {
    const std::string abs = p.filePath(o.modelPath);
    auto it = cache.find(abs);
    if (it == cache.end()) {
        auto b = std::make_shared<glbparser::Baked>();
        std::string err;
        // animimport::bake, not glbparser::bake: an "animated model" here is
        // .glb OR .fbx (project.hpp isAnimatedModelPath), and this is the one
        // entry the editor's own import, preview and matbake all go through -
        // including the replacement-UV sidecar, which the game's .tskl carries
        // and a corpus without it would texture differently.
        if (!animimport::bake(abs, kAnimFps, *b, err)) b->parts.clear();
        it = cache.emplace(abs, std::move(b)).first;
    }
    const glbparser::Baked& baked = *it->second;
    if (baked.parts.empty() || baked.frameCount <= 0) return;

    // Which clip is playing. The generated game starts `animClip` (or the
    // file's first) at scene start; `animAutoplay` off holds the first pose.
    int first = 0, count = baked.frameCount;
    if (!baked.clips.empty()) {
        const glbparser::Clip* pick = &baked.clips.front();
        if (!o.animClip.empty())
            for (const glbparser::Clip& c : baked.clips)
                if (c.name == o.animClip) { pick = &c; break; }
        first = pick->firstFrame;
        count = pick->frameCount > 0 ? pick->frameCount : 1;
    }
    const float speed = o.animSpeed > 0.0f ? o.animSpeed : 0.0f;

    // Which baked frame console frame `f` shows. Truncating rather than
    // rounding is what `SkelInstance::advance` plus a keyframe lookup does to a
    // clip sampled at a fixed rate, and truncation is the choice that cannot
    // depend on how a rounding mode is spelled.
    const auto frameAt = [&](int f) {
        if (!o.animAutoplay || speed <= 0.0f) return first;
        const int adv = static_cast<int>(static_cast<float>(f) * speed);
        return first + (o.animLoop ? adv % count : std::min(adv, count - 1));
    };

    for (const glbparser::Part& part : baked.parts) {
        if (part.vertexCount <= 0) continue;
        // ONE PART IS ONE BAG, exactly as `setupAnimObject` makes one
        // StaPipBag per .glb material - so a two-material character is two
        // proxies here and two on the console.
        AnimMesh am;
        am.pose.resize(kAnimPoses);

        int tex = -1;
        if (part.image >= 0 && part.image < static_cast<int>(baked.images.size()) &&
            !baked.images[static_cast<size_t>(part.image)].png.empty()) {
            tex = static_cast<int>(embedded.size());
            embedded.push_back(baked.images[static_cast<size_t>(part.image)].png);
        }

        for (int f = 0; f < kAnimPoses; ++f) {
            SceneMesh& mesh = am.pose[static_cast<size_t>(f)];
            mesh.embeddedTex = tex;
            for (int k = 0; k < 3; ++k) mesh.tint[k] = part.baseColor[k];
            mesh.viewDist = o.drawDistance;
            const size_t base =
                static_cast<size_t>(frameAt(f)) * static_cast<size_t>(part.vertexCount) * 3;
            if (base + static_cast<size_t>(part.vertexCount) * 3 > part.positions.size())
                continue;
            mesh.vert.resize(static_cast<size_t>(part.vertexCount));
            mesh.idx.resize(static_cast<size_t>(part.vertexCount));
            float lo[3] = {0, 0, 0}, hi[3] = {0, 0, 0};
            for (int i = 0; i < part.vertexCount; ++i) {
                SceneVert& sv = mesh.vert[static_cast<size_t>(i)];
                float lp[3] = {part.positions[base + i * 3 + 0] * o.scale[0],
                               part.positions[base + i * 3 + 1] * o.scale[1],
                               part.positions[base + i * 3 + 2] * o.scale[2]};
                preYaw(lp, o.modelYawOffset);
                rotate3(lp, o.rotation, sv.p);
                for (int k = 0; k < 3; ++k) sv.p[k] += o.position[k];
                float ln[3] = {part.normals[base + i * 3 + 0] / (o.scale[0] != 0 ? o.scale[0] : 1.0f),
                               part.normals[base + i * 3 + 1] / (o.scale[1] != 0 ? o.scale[1] : 1.0f),
                               part.normals[base + i * 3 + 2] / (o.scale[2] != 0 ? o.scale[2] : 1.0f)};
                preYaw(ln, o.modelYawOffset);
                float wn[3];
                rotate3(ln, o.rotation, wn);
                const float l = std::sqrt(wn[0] * wn[0] + wn[1] * wn[1] + wn[2] * wn[2]);
                if (l > 1e-6f)
                    for (int k = 0; k < 3; ++k) wn[k] /= l;
                float sh[3];
                light.shade(wn, sh);
                // The albedo is the glTF baseColorFactor and NOT the object's
                // colour: the lit VU1 program folds `parts[m].color` into the
                // light colours and never reads the object tint for an animated
                // mesh (templates.cpp setupAnimObject).
                for (int k = 0; k < 3; ++k) sv.c[k] = clampf(part.baseColor[k] * sh[k], 0.0f, 1.0f);
                sv.u = part.uvs[static_cast<size_t>(i) * 2 + 0];
                sv.v = part.uvs[static_cast<size_t>(i) * 2 + 1];
                mesh.idx[static_cast<size_t>(i)] = i;
                for (int k = 0; k < 3; ++k) {
                    if (i == 0) lo[k] = hi[k] = sv.p[k];
                    lo[k] = std::min(lo[k], sv.p[k]);
                    hi[k] = std::max(hi[k], sv.p[k]);
                }
            }
            for (int k = 0; k < 3; ++k) mesh.centre[k] = 0.5f * (lo[k] + hi[k]);
        }
        if (!am.pose.empty() && !am.pose[0].vert.empty()) out.push_back(std::move(am));
    }
}

// --- the terrain -------------------------------------------------------------

// The generated game cuts the heightmap into TERRAIN_CHUNK_CELLS-square chunks
// and submits ONE BAG PER CHUNK (templates.cpp buildTerrainChunk). That is not
// a detail: a terrain submitted whole would be one proxy covering the frame,
// which is the exact shape 6a4cbead found flattening every channel. The
// constant is the generated game's; keep the two in step.
constexpr int kTerrainChunkCells = 16;

void appendTerrain(std::vector<SceneMesh>& out, const Project& p,
                   const SceneData& sc, const ProjectSettings& rs,
                   const Light& light) {
    if (!sc.terrain.enabled) return;
    const float width = static_cast<float>(sc.terrain.width);
    const float depth = static_cast<float>(sc.terrain.depth);
    if (width <= 0.0f || depth <= 0.0f) return;

    const int maxCells = rs.terrainDetail > 1 ? rs.terrainDetail : 1;
    const int cellsX = std::max(1, std::min(sc.terrain.width, maxCells));
    const int cellsZ = std::max(1, std::min(sc.terrain.depth, maxCells));
    const bool haveHeights =
        sc.hmW >= 2 && sc.hmD >= 2 &&
        static_cast<int>(sc.heights.size()) >= sc.hmW * sc.hmD;

    const project::TerrainMaterial tm =
        project::resolveTerrainMaterial(p, rs.terrainMaterial);
    std::string tex;
    if (tm.present && !tm.texture.empty()) tex = p.filePath(tm.texture);

    const float x0 = -width * 0.5f, z0 = -depth * 0.5f;
    const float stepX = width / cellsX, stepZ = depth / cellsZ;
    const auto hAt = [&](float x, float z) {
        return haveHeights ? heightAtWorld(sc.heights, sc.hmW, sc.hmD, width,
                                           depth, x, z)
                           : 0.0f;
    };

    const int chunksX = (cellsX + kTerrainChunkCells - 1) / kTerrainChunkCells;
    const int chunksZ = (cellsZ + kTerrainChunkCells - 1) / kTerrainChunkCells;
    for (int cz = 0; cz < chunksZ; ++cz)
        for (int cx = 0; cx < chunksX; ++cx) {
            const int gx0 = cx * kTerrainChunkCells;
            const int gz0 = cz * kTerrainChunkCells;
            const int gx1 = std::min(gx0 + kTerrainChunkCells, cellsX);
            const int gz1 = std::min(gz0 + kTerrainChunkCells, cellsZ);
            if (gx0 >= gx1 || gz0 >= gz1) continue;
            SceneMesh mesh;
            for (int k = 0; k < 3; ++k) mesh.tint[k] = tm.kd[k];
            mesh.texture = tex;
            // The terrain is streamed: chunks past the view distance are never
            // submitted, so they are neither drawn nor described.
            mesh.viewDist = rs.terrainViewDistance;
            std::vector<float> soup;
            soup.reserve(static_cast<size_t>(gx1 - gx0) * (gz1 - gz0) * 48);
            for (int j = gz0; j < gz1; ++j)
                for (int i = gx0; i < gx1; ++i) {
                    const float ax = x0 + stepX * i, bx = x0 + stepX * (i + 1);
                    const float az = z0 + stepZ * j, bz = z0 + stepZ * (j + 1);
                    const float c[4][3] = {{ax, hAt(ax, az), az},
                                           {bx, hAt(bx, az), az},
                                           {bx, hAt(bx, bz), bz},
                                           {ax, hAt(ax, bz), bz}};
                    // Winding so the geometric normal comes out +Y - the ground
                    // always faces up, exactly as gibake forces it to.
                    const int order[6] = {0, 3, 2, 0, 2, 1};
                    float n[3] = {0, 1, 0};
                    {
                        const float* a = c[0];
                        const float* b = c[3];
                        const float* d = c[2];
                        const float e1[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
                        const float e2[3] = {d[0] - a[0], d[1] - a[1], d[2] - a[2]};
                        float g[3] = {e1[1] * e2[2] - e1[2] * e2[1],
                                      e1[2] * e2[0] - e1[0] * e2[2],
                                      e1[0] * e2[1] - e1[1] * e2[0]};
                        const float l =
                            std::sqrt(g[0] * g[0] + g[1] * g[1] + g[2] * g[2]);
                        if (l > 1e-9f) {
                            const float inv = g[1] < 0.0f ? -1.0f / l : 1.0f / l;
                            for (int k = 0; k < 3; ++k) n[k] = g[k] * inv;
                        }
                    }
                    for (int k = 0; k < 6; ++k) {
                        const float* v = c[order[k]];
                        // World-space UVs at the material's tiling - what the
                        // generated terrain does, and the reason a ground
                        // texture minifies to nothing at the horizon.
                        soup.insert(soup.end(),
                                    {v[0], v[1], v[2], n[0], n[1], n[2],
                                     v[0] * tm.tile[0], v[2] * tm.tile[1]});
                    }
                }
            const float zero3[3] = {0, 0, 0};
            const float unit3[3] = {1, 1, 1};
            const float uv[2] = {1.0f, 1.0f};  // already folded in above
            appendSoup(mesh, soup, zero3, zero3, unit3, tm.kd, light, uv);
            if (!mesh.vert.empty()) out.push_back(std::move(mesh));
        }
}

// ----------------------------------------------------------------- cameras ---

void addKey(SceneShot& s, const float eye[3], const float look[3]) {
    for (int k = 0; k < 3; ++k) s.eye.push_back(eye[k]);
    for (int k = 0; k < 3; ++k) s.look.push_back(look[k]);
}

// Where the player starts, and which way it faces. The FPP template's rule is
// "the first Player, else the first SpawnPoint, else the origin", and an author
// who placed one has told you which way the interesting half of the scene is.
struct Start {
    bool found = false;
    float pos[3] = {0, 0, 0};
    float yaw = 0.0f;   // radians, measured from +Z toward +X
    float eyeH = 1.8f;
};

Start startOf(const SceneData& sc, const ProjectSettings& rs) {
    Start st;
    st.eyeH = rs.eyeHeight > 0.1f ? rs.eyeHeight : 1.8f;
    const SceneObject* pick = nullptr;
    for (const SceneObject& o : sc.objects)
        if (o.type == PrimitiveType::Player) { pick = &o; break; }
    if (!pick)
        for (const SceneObject& o : sc.objects)
            if (o.type == PrimitiveType::SpawnPoint) { pick = &o; break; }
    if (!pick) return st;
    st.found = true;
    for (int k = 0; k < 3; ++k) st.pos[k] = pick->position[k];
    st.yaw = pick->rotation[1] * kPi / 180.0f;
    if (pick->type == PrimitiveType::Player && pick->playerEyeHeight > 0.1f)
        st.eyeH = pick->playerEyeHeight;
    return st;
}

// The automatic coverage set. Everything here is derived from the scene's own
// bounds and its player start, so an empty-ish project still gets six honest
// camera moves and a dense one gets six that are actually inside it.
void autoShots(std::vector<SceneShot>& out, const std::string& sceneName,
               const ProjectScene& ps, const SceneData& sc,
               const ProjectSettings& rs, int budget) {
    if (budget <= 0) return;
    const Start st = startOf(sc, rs);
    const float width = std::max(4.0f, static_cast<float>(sc.terrain.width));
    const float depth = std::max(4.0f, static_cast<float>(sc.terrain.depth));
    const bool haveHeights =
        sc.hmW >= 2 && sc.hmD >= 2 &&
        static_cast<int>(sc.heights.size()) >= sc.hmW * sc.hmD;
    // With the terrain switched off there is no ground to stand on, and y = 0
    // may be nowhere near the geometry (docs/terrain.md - a scene can be a
    // platform in the void). The bottom of the scene's own box is the only
    // floor there is.
    const auto ground = [&](float x, float z) {
        return haveHeights ? heightAtWorld(sc.heights, sc.hmW, sc.hmD, width,
                                           depth, x, z)
                           : ps.bmin[1];
    };

    // WHERE THE SCENE IS, by triangle count rather than by bounding box. A
    // bbox centre is dragged halfway to the horizon by one prop at the edge of
    // a 200-unit terrain and by the terrain itself; a vertex centroid sits
    // where the geometry actually is, which for a game scene is where the
    // props are. Terrain chunks are geometry too, so a scene that is mostly
    // ground still centres on its own middle - which is correct.
    float cx = 0.0f, cz = 0.0f, radius = 0.25f * std::min(width, depth);
    {
        double ax = 0.0, az = 0.0;
        size_t n = 0;
        const auto add = [&](const SceneMesh& m) {
            for (const SceneVert& v : m.vert) {
                ax += v.p[0];
                az += v.p[2];
                ++n;
            }
        };
        for (const SceneMesh& m : ps.mesh) add(m);
        // Animated models vote too - a scene whose only interesting content is
        // two spiders on a plain floor would otherwise frame the floor. Pose 0
        // only, for the reason the bounds use pose 0.
        for (const AnimMesh& a : ps.anim)
            if (!a.pose.empty()) add(a.pose[0]);
        if (n) cx = (float)(ax / (double)n), cz = (float)(az / (double)n);
        cx = clampf(cx, -width * 0.4f, width * 0.4f);
        cz = clampf(cz, -depth * 0.4f, depth * 0.4f);
        const float sx = std::min(ps.bmax[0] - ps.bmin[0], width);
        const float sz = std::min(ps.bmax[2] - ps.bmin[2], depth);
        radius = clampf(0.30f * std::max(sx, sz), 3.0f,
                        0.45f * std::min(width, depth));
    }
    const float ex = st.found ? st.pos[0] : cx;
    const float ez = st.found ? st.pos[2] : cz;
    const float eyeY = ground(ex, ez) + st.eyeH;
    // WHERE THE SHOTS LOOK, and it is NOT the player's authored heading.
    //
    // A camera aimed at empty sky produces a frame with nothing to reconstruct:
    // bilinear IS the ground truth there, the oracle asks for no kernel, and the
    // fold contributes a PSNR of 99 dB and no gradient at all. Measured on the
    // example projects, several player starts face exactly that - `nav-ai`'s
    // walk scored a literal 99.000 before this - and a six-shot budget cannot
    // afford to spend a sixth of itself on a blank frame.
    //
    // So the automatic set frames the scene's own CONTENT: the area-weighted
    // centroid of its triangles, which is where a player would turn to within a
    // second anyway. The player's START POSITION is still where the walk begins,
    // because that is the one thing about the camera the project really does
    // decide - and an authored cutscene take keeps its framing exactly, which is
    // where "what the author wants you to look at" is preserved verbatim.
    const float toX = cx - ex, toZ = cz - ez;
    const float yaw0 = (toX * toX + toZ * toZ) > 1e-4f
                           ? std::atan2(toX, toZ)
                           : (st.found ? st.yaw : 0.0f);
    // HOW FAR A MOVE GOES, and it is calibrated against the bestiary rather
    // than against the scene, because a shot is spent over ~12 frames either
    // way and what the network actually sees is the PER-FRAME step. The
    // bestiary's dollies run 9-16 world units and its pan sweeps 0.44 rad over
    // a shot; a first draft of this file swept 0.9 rad and let the walk reach a
    // third of the terrain, and it showed up exactly where you would expect -
    // `motion` saturated at 1.0 in 66% of all tiles against the bestiary's 46%,
    // i.e. a corpus teaching "history is never usable".
    const float run = clampf(radius * 1.2f, 4.0f,
                             std::min(16.0f, 0.35f * std::min(width, depth)));
    constexpr float kPanSweep = 0.45f;  // radians over the whole shot

    const auto forward = [](float yaw, float pitch, float out[3]) {
        out[0] = std::sin(yaw) * std::cos(pitch);
        out[1] = std::sin(pitch);
        out[2] = std::cos(yaw) * std::cos(pitch);
    };

    // 1 - the walk. What the player sees for most of the running time, and the
    // shot the console's own debug view is read on.
    if (static_cast<int>(out.size()) < budget) {
        SceneShot s;
        s.name = sceneName + " walk";
        s.move = "dolly-forward";
        float f[3];
        forward(yaw0, -0.05f, f);
        for (int k = 0; k <= 8; ++k) {
            const float t = static_cast<float>(k) / 8.0f;
            const float x = ex + f[0] * run * t, z = ez + f[2] * run * t;
            const float eye[3] = {x, ground(x, z) + st.eyeH, z};
            const float look[3] = {eye[0] + f[0], eye[1] + f[1], eye[2] + f[2]};
            addKey(s, eye, look);
        }
        out.push_back(std::move(s));
    }
    // 2 - the pan. A yaw sweep from the same standpoint: the same content at
    // every reprojection offset the stick can produce.
    if (static_cast<int>(out.size()) < budget) {
        SceneShot s;
        s.name = sceneName + " pan";
        s.move = "pan";
        for (int k = 0; k <= 16; ++k) {
            const float t = static_cast<float>(k) / 16.0f;
            const float yaw = yaw0 + (t - 0.5f) * kPanSweep;
            float f[3];
            forward(yaw, -0.06f, f);
            const float eye[3] = {ex, eyeY, ez};
            const float look[3] = {ex + f[0], eyeY + f[1], ez + f[2]};
            addKey(s, eye, look);
        }
        out.push_back(std::move(s));
    }
    // 3 - the orbit. The only move that sweeps silhouettes across the whole
    // tile grid, and the one the procedural corpus scores best on.
    if (static_cast<int>(out.size()) < budget) {
        SceneShot s;
        s.name = sceneName + " orbit";
        s.move = "orbit";
        const float h = ground(cx, cz) + st.eyeH * 1.5f;
        for (int k = 0; k <= 16; ++k) {
            const float a = 0.4f + 1.3f * static_cast<float>(k) / 16.0f;
            const float x = cx + std::cos(a) * radius;
            const float z = cz + std::sin(a) * radius;
            const float eye[3] = {x, std::max(h, ground(x, z) + 1.0f), z};
            const float look[3] = {cx, ground(cx, cz) + st.eyeH * 0.6f, cz};
            addKey(s, eye, look);
        }
        out.push_back(std::move(s));
    }
    // 4 - the whip. Eased, so the angular velocity peaks mid-shot and the net
    // sees history that is fine, history that is useless, and both transitions.
    if (static_cast<int>(out.size()) < budget) {
        SceneShot s;
        s.name = sceneName + " whip";
        s.move = "whip";
        s.ease = 1.0f;
        for (int k = 0; k <= 24; ++k) {
            const float t = static_cast<float>(k) / 24.0f;
            const float yaw = yaw0 + (t - 0.5f) * 2.7f;
            float f[3];
            forward(yaw, -0.05f, f);
            const float eye[3] = {ex, eyeY, ez};
            const float look[3] = {ex + f[0], eyeY + f[1], ez + f[2]};
            addKey(s, eye, look);
        }
        out.push_back(std::move(s));
    }
    // 5 - the pitch. Coverage sweeps from 1 to nearly 0 over the shot, which is
    // what makes the empty-tile path a moving target rather than a corner.
    if (static_cast<int>(out.size()) < budget) {
        SceneShot s;
        s.name = sceneName + " pitch";
        s.move = "pitch-up";
        for (int k = 0; k <= 16; ++k) {
            const float t = static_cast<float>(k) / 16.0f;
            // Stops at +0.20 rad, not at the zenith: the shot exists to sweep
            // COVERAGE from 1 to nearly 0, and once the frame is entirely sky
            // every further frame is the same blank one.
            const float pitch = -0.45f + t * 0.65f;
            float f[3];
            forward(yaw0, pitch, f);
            const float eye[3] = {ex, eyeY, ez};
            const float look[3] = {ex + f[0], eyeY + f[1], ez + f[2]};
            addKey(s, eye, look);
        }
        out.push_back(std::move(s));
    }
    // 6 - the strafe. A sideways translation is the one move that produces real
    // parallax, so near geometry slides across far and the tile's single
    // representative depth reprojects the two of them to the same place.
    if (static_cast<int>(out.size()) < budget) {
        SceneShot s;
        s.name = sceneName + " strafe";
        s.move = "dolly-lateral";
        float f[3];
        forward(yaw0, -0.05f, f);
        const float rx = f[2], rz = -f[0];  // right = up x forward, y-up
        for (int k = 0; k <= 8; ++k) {
            const float t = static_cast<float>(k) / 8.0f - 0.5f;
            const float x = ex + rx * run * t, z = ez + rz * run * t;
            const float eye[3] = {x, ground(x, z) + st.eyeH, z};
            const float look[3] = {eye[0] + f[0], eye[1] + f[1], eye[2] + f[2]};
            addKey(s, eye, look);
        }
        out.push_back(std::move(s));
    }
}

// AUTHORED SHOTS, and they are the cheapest good idea in this file. A Cutscene
// Director sequence's camera track is ALREADY a polyline of (eye, look-at)
// keys, which is exactly SceneShot's form - so an author who has framed a shot
// has told the trainer which part of the scene matters, for the cost of a copy.
// Keys bound to a Camera entity resolve against THIS scene's objects; a
// sequence that names cameras from another scene simply contributes nothing.
void authoredShots(std::vector<SceneShot>& out, const Project& p,
                   const SceneData& sc, int budget) {
    for (const Sequence& seq : p.sequences) {
        if (static_cast<int>(out.size()) >= budget) return;
        if (!seq.cameraEnabled || seq.cameraKeys.size() < 2) continue;
        std::vector<SeqCameraKey> keys = seq.cameraKeys;
        std::sort(keys.begin(), keys.end(),
                  [](const SeqCameraKey& a, const SeqCameraKey& b) {
                      return a.time < b.time;
                  });
        SceneShot s;
        s.name = sc.name + " take " + seq.name;
        s.move = "take";
        for (const SeqCameraKey& k : keys) {
            float eye[3], look[3];
            if (k.camera.empty()) {
                for (int c = 0; c < 3; ++c) eye[c] = k.eye[c], look[c] = k.target[c];
            } else {
                const SceneObject* cam = nullptr;
                for (const SceneObject& o : sc.objects)
                    if (o.type == PrimitiveType::Camera && o.name == k.camera) {
                        cam = &o;
                        break;
                    }
                if (!cam) continue;
                float fwd[3];
                seqCameraForward(cam->rotation, fwd);
                for (int c = 0; c < 3; ++c) {
                    eye[c] = cam->position[c];
                    look[c] = eye[c] + fwd[c];
                }
                s.fovDeg = cam->cameraFov;
            }
            addKey(s, eye, look);
        }
        // One key is a still frame with no camera move at all; the automatic
        // set already carries a standpoint, so an unusable take is dropped
        // rather than padded into one.
        if (s.keys() >= 2) out.push_back(std::move(s));
    }
}

}  // namespace

// -------------------------------------------------------------------- entry ---

std::vector<ProjectScene> loadProject(const std::string& projectDir,
                                      std::string* err, bool verbose,
                                      bool animated, ProjectBlss* blssOut) {
    std::vector<ProjectScene> out;
    Project p;
    const std::string e = project::load(p, projectDir);
    if (!e.empty()) {
        if (err) *err = e;
        return out;
    }
    // Read BEFORE the scene walk and independently of whether it produces
    // anything: a project that loads has told us how it will be built, even if
    // it turns out to have nothing drawable and the caller falls back.
    if (blssOut) {
        blssOut->found = true;
        blssOut->jitter = p.settings.blssJitter;
    }
    if (verbose)
        std::printf("[blss] project '%s' (%s), %zu scene(s)\n", p.name.c_str(),
                    p.gameTemplate.c_str(), p.scenes.size());

    MatCache cache;
    BakeCache bakes;
    for (const SceneData& sc : p.scenes) {
        const ProjectSettings rs = project::resolvedSettings(p, sc);
        const Light light = lightOf(rs);
        ProjectScene ps;
        ps.name = sc.name.empty() ? "scene" : sc.name;
        appendTerrain(ps.mesh, p, sc, rs, light);
        for (const SceneObject& o : sc.objects) {
            if (drawsAnimated(o)) {
                // ALWAYS built, even under `--no-anim`. The bounds and the
                // triangle centroid below decide where the six camera moves
                // point, so dropping the spiders here as well would move the
                // cameras and turn the A/B into two different experiments -
                // exactly the confound that makes a before/after unreadable.
                // `--no-anim` clears the list AFTER the shots are built, so the
                // two runs shoot the same frames and differ only in whether
                // those frames contain the animated models.
                appendAnimObject(ps.anim, ps.embedded, p, o, light, bakes);
                continue;
            }
            if (!drawsGeometry(o)) continue;
            appendObject(ps.mesh, p, o, light, cache);
        }
        // Prune the empties BEFORE the bounds, and record each mesh's own
        // centre: the draw-distance test the console applies is per bag, from
        // the camera to the bag, and a corpus that drew a chunk the console
        // culls would be describing a frame the console never renders.
        std::vector<SceneMesh> kept;
        for (SceneMesh& m : ps.mesh) {
            if (m.vert.empty() || m.idx.size() < 3) continue;
            float lo[3], hi[3];
            for (int k = 0; k < 3; ++k) lo[k] = hi[k] = m.vert[0].p[k];
            for (const SceneVert& v : m.vert)
                for (int k = 0; k < 3; ++k) {
                    lo[k] = std::min(lo[k], v.p[k]);
                    hi[k] = std::max(hi[k], v.p[k]);
                }
            for (int k = 0; k < 3; ++k) m.centre[k] = 0.5f * (lo[k] + hi[k]);
            kept.push_back(std::move(m));
        }
        ps.mesh = std::move(kept);
        if (ps.mesh.empty() && ps.anim.empty()) continue;

        bool first = true;
        const auto grow = [&](const SceneMesh& m) {
            for (const SceneVert& v : m.vert)
                for (int k = 0; k < 3; ++k) {
                    if (first) ps.bmin[k] = ps.bmax[k] = v.p[k];
                    ps.bmin[k] = std::min(ps.bmin[k], v.p[k]);
                    ps.bmax[k] = std::max(ps.bmax[k], v.p[k]);
                }
            if (!m.vert.empty()) first = false;
        };
        for (const SceneMesh& m : ps.mesh) grow(m);
        // The animated meshes join the bounds through POSE 0 only. Every camera
        // move is derived from these bounds, so letting all 48 poses vote would
        // make the shot table depend on how far a walk cycle happens to swing an
        // arm - a scene whose framing moved when someone retimed a clip.
        for (const AnimMesh& a : ps.anim)
            if (!a.pose.empty()) grow(a.pose[0]);

        // Authored first, automatic to fill: a take is the author saying which
        // frame matters, and the automatic set is what covers the rest.
        authoredShots(ps.shot, p, sc, kShotsPerScene / 2);
        autoShots(ps.shot, ps.name, ps, sc, rs, kShotsPerScene);
        if (ps.shot.empty()) continue;

        // ...and only now does `--no-anim` take them away, so the shot table
        // above is identical either way.
        if (!animated) {
            ps.anim.clear();
            ps.embedded.clear();
            if (ps.mesh.empty()) continue;
        }

        if (verbose)
            std::printf(
                "[blss]   scene '%s': %zu mesh(es) + %zu animated part(s), %zu "
                "triangle(s), %zu shot(s)\n",
                ps.name.c_str(), ps.mesh.size(), ps.anim.size(), ps.triangles(),
                ps.shot.size());
        out.push_back(std::move(ps));
    }
    return out;
}

}  // namespace blss
