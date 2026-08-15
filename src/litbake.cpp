#include "litbake.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "modelao.hpp"  // the model's own baked AO, folded into the albedo
#include "objparser.hpp"
#include "wire.hpp"  // hashFile - CONTENT hashes for the signature, never mtimes

#define STB_IMAGE_IMPLEMENTATION_ALREADY
#include "stb_image.h"
#include "stb_image_write.h"

namespace fs = std::filesystem;

namespace litbake {
namespace {

// The object's world transform, matching areaBasis in the generated game and
// the viewport exactly: the rotated unit axes are the columns of Rz*Ry*Rx.
// Getting this wrong does not crash - it bakes the light of a place the object
// is not, which reads as "the bake is broken" rather than "the matrix is".
struct Basis {
    float ax[3], ay[3], az[3];  // scaled axes (rotation * scale)
    float nx[3], ny[3], nz[3];  // the same, unscaled - for normals
    float o[3];                 // world position
};

Basis basisOf(const SceneObject& o) {
    Basis b;
    const float k = 3.14159265f / 180.0f;
    const float cx = std::cos(o.rotation[0] * k), sx = std::sin(o.rotation[0] * k);
    const float cy = std::cos(o.rotation[1] * k), sy = std::sin(o.rotation[1] * k);
    const float cz = std::cos(o.rotation[2] * k), sz = std::sin(o.rotation[2] * k);
    b.nx[0] = cy * cz;
    b.nx[1] = cy * sz;
    b.nx[2] = -sy;
    b.ny[0] = sx * sy * cz - cx * sz;
    b.ny[1] = sx * sy * sz + cx * cz;
    b.ny[2] = sx * cy;
    b.nz[0] = cx * sy * cz + sx * sz;
    b.nz[1] = cx * sy * sz - sx * cz;
    b.nz[2] = cx * cy;
    for (int i = 0; i < 3; ++i) {
        b.ax[i] = b.nx[i] * o.scale[0];
        b.ay[i] = b.ny[i] * o.scale[1];
        b.az[i] = b.nz[i] * o.scale[2];
        b.o[i] = o.position[i];
    }
    return b;
}

void toWorld(const Basis& b, const float l[3], float w[3]) {
    for (int i = 0; i < 3; ++i)
        w[i] = b.o[i] + b.ax[i] * l[0] + b.ay[i] * l[1] + b.az[i] * l[2];
}

void normalToWorld(const Basis& b, const float l[3], float w[3]) {
    for (int i = 0; i < 3; ++i)
        w[i] = b.nx[i] * l[0] + b.ny[i] * l[1] + b.nz[i] * l[2];
    const float len = std::sqrt(w[0] * w[0] + w[1] * w[1] + w[2] * w[2]);
    if (len > 1e-6f)
        for (int i = 0; i < 3; ++i) w[i] /= len;
}

// One decoded map_Kd, sampled with wrap - the same thing the game's texturing
// does, so a tiling wall bakes the tiling it will show.
struct SourceTex {
    std::vector<uint8_t> px;
    int w = 0, h = 0;
    bool ok() const { return w > 0; }
    void sample(float u, float v, float out[3]) const {
        int x = (int)std::floor(u * w), y = (int)std::floor((1.0f - v) * h);
        x %= w;
        y %= h;
        if (x < 0) x += w;
        if (y < 0) y += h;
        const uint8_t* p = &px[((size_t)y * w + x) * 4];
        out[0] = p[0] / 255.0f, out[1] = p[1] / 255.0f, out[2] = p[2] / 255.0f;
    }
};

// Bumped when a change here would make an existing prelitSig mean something
// else - it is what forces every object to read stale rather than silently
// keeping a texture baked by different code.
constexpr uint64_t kSigVersion = 1;

void mix64(uint64_t& h, uint64_t v) {
    h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
}
void mixS(uint64_t& h, const std::string& s) {
    for (unsigned char c : s) mix64(h, c);
}
void mixF(uint64_t& h, float f) {
    uint32_t bits = 0;
    std::memcpy(&bits, &f, sizeof bits);
    mix64(h, bits);
}

// The file name an object's pre-lit assets are written under. Its own function
// because three places need to agree on it: the writer, the panel's VRAM
// readout and anything that goes looking for the image afterwards.
std::string baseNameOf(const SceneObject& o) {
    std::string base = o.name.empty() ? ("object-" + o.id) : o.name;
    for (char& c : base)
        if (!std::isalnum((unsigned char)c) && c != '-' && c != '_') c = '-';
    return base;
}

bool loadTexture(const fs::path& path, SourceTex& t) {
    int n = 0;
    unsigned char* d = stbi_load(path.string().c_str(), &t.w, &t.h, &n, 4);
    if (!d) {
        t.w = t.h = 0;
        return false;
    }
    t.px.assign(d, d + (size_t)t.w * t.h * 4);
    stbi_image_free(d);
    return true;
}

}  // namespace

std::string outputPngRel(const SceneObject& o) {
    return "res/materials/" + baseNameOf(o) + "-lit.png";
}
std::string outputMtlRel(const SceneObject& o) {
    return "res/materials/" + baseNameOf(o) + "-lit.mtl";
}

// The scene-wide half of the signature: gibake's own, over a scene with every
// pre-lit override NORMALIZED BACK to the material it came from.
//
// That normalization is the whole trick, and without it the feature does not
// work at all. gibake::signature hashes each object's materialPath AND that
// file's content, so applying a bake - which repoints the object at its
// -lit.mtl - would change the scene signature and make the object it just
// baked read STALE on the very next frame, plus every other pre-lit object in
// the scene along with it. Hashing the scene AS AUTHORED is stable under
// pre-lighting; the price is that bounce light picked up off a pre-lit
// neighbour's new texture does not stale anything, which is a second-order
// term nobody would want a re-bake storm for.
uint64_t sceneSignature(const Project& p, const SceneData& sc) {
    SceneData norm = sc;
    for (SceneObject& o : norm.objects)
        if (o.prelit) o.materialPath = o.prelitSource;
    gibake::Settings st = gibake::settingsOf(p.settings);
    st.enabled = true;  // the gather works whether or not GI ships - as in run()
    return gibake::signature(p, norm, st);
}

// The per-object half, given the scene half. Split because the scene half
// hashes every file the GI bake reads and a panel listing N objects must pay
// for it once, not N times.
uint64_t signature(const Project& p, const SceneData& sc, int objectIndex,
                   const Params& prm, uint64_t sceneSig) {
    if (objectIndex < 0 || objectIndex >= (int)sc.objects.size()) return 0;
    const SceneObject& o = sc.objects[objectIndex];
    uint64_t h = 0xcbf29ce484222325ull;
    mix64(h, kSigVersion);
    mix64(h, sceneSig);
    for (int k = 0; k < 3; ++k) {
        mixF(h, o.position[k]);
        mixF(h, o.rotation[k]);
        mixF(h, o.scale[k]);
    }
    mixS(h, o.modelPath);
    mixS(h, o.prelitSource);
    mix64(h, (uint64_t)prm.size);
    mix64(h, (uint64_t)prm.rays);
    mix64(h, (uint64_t)prm.padding);
    mix64(h, (uint64_t)prm.seed);
    mixF(h, prm.strength);
    mixF(h, prm.floorLevel);

    auto mixFile = [&](const fs::path& file) {
        uint64_t fh = 0, fsz = 0;
        if (wire::hashFile(file.string(), fh, fsz)) {
            mix64(h, fh);
            mix64(h, fsz);
        }
    };
    // The albedo the bake reads comes from the model's OWN .mtl (bakeObject
    // deliberately ignores the override - that is what makes a re-bake
    // idempotent), so those files decide the output even though nothing else
    // hashes them. The source material joins them because Revert points back
    // at it and because it is what an un-baked instance of this object shows.
    const fs::path modelFile = fs::path(p.dir) / o.modelPath;
    mixFile(modelFile);
    objparser::Model m;
    if (!o.modelPath.empty() && objparser::load(modelFile.string(), m))
        for (const std::string& lib : m.mtlLibs)
            mixFile((modelFile.parent_path() / lib).lexically_normal());
    if (!o.prelitSource.empty()) mixFile(fs::path(p.dir) / o.prelitSource);

    // Model AO is multiplied into that albedo when it resolves on for the
    // asset, so its bake is an input to this one (docs/ambient-occlusion.md).
    // Asked only when it is on: planFor parses every .obj in the project, and
    // a project with the feature off must not pay for a question with one
    // answer.
    const modelao::Params aoPrm = modelao::paramsOf(p.settings);
    const bool ao = !o.modelPath.empty() && modelao::resolveFor(p, o.modelPath, aoPrm);
    mix64(h, ao ? 1 : 0);
    if (ao) {
        mixF(h, aoPrm.strength);  // an apply-time remap here, so it IS an input
        for (const modelao::Target& t :
             modelao::planFor(p, o.modelPath, aoPrm).targets)
            mix64(h, modelao::signature(p, t, aoPrm));
    }
    return h;
}

uint64_t signature(const Project& p, const SceneData& sc, int objectIndex,
                   const Params& prm) {
    if (objectIndex < 0 || objectIndex >= (int)sc.objects.size()) return 0;
    return signature(p, sc, objectIndex, prm, sceneSignature(p, sc));
}

bool fresh(const Project& p, const SceneData& sc, int objectIndex,
           const Params& prm) {
    if (objectIndex < 0 || objectIndex >= (int)sc.objects.size()) return false;
    const SceneObject& o = sc.objects[objectIndex];
    if (!o.prelit || !o.prelitSig) return false;
    return o.prelitSig == signature(p, sc, objectIndex, prm);
}

std::vector<char> freshFlags(const Project& p, const SceneData& sc,
                             const Params& prm) {
    std::vector<char> out(sc.objects.size(), 0);
    bool any = false;
    for (const SceneObject& o : sc.objects) any |= (o.prelit && o.prelitSig);
    if (!any) return out;  // nothing to compare against - hash nothing
    const uint64_t sceneSig = sceneSignature(p, sc);
    for (size_t i = 0; i < sc.objects.size(); ++i) {
        const SceneObject& o = sc.objects[i];
        if (!o.prelit || !o.prelitSig) continue;
        out[i] = o.prelitSig == signature(p, sc, (int)i, prm, sceneSig);
    }
    return out;
}

void revertObject(SceneObject& o) {
    o.materialPath = o.prelitSource;
    o.prelit = false;
    o.prelitWanted = false;
    o.prelitSig = 0;
    // prelitSource is deliberately NOT cleared: it costs nothing, and keeping
    // it means a re-bake after a Revert still knows the way back.
}

bool bakeObject(const Project& p, const SceneData& sc, int objectIndex,
                const gibake::Scene& scene, const Params& prm, Result& out,
                std::string& err) {
    if (objectIndex < 0 || objectIndex >= (int)sc.objects.size()) {
        err = "no such object";
        return false;
    }
    const SceneObject& o = sc.objects[objectIndex];
    if (o.type != PrimitiveType::Model || o.modelPath.empty()) {
        err =
            "pre-lighting bakes a MODEL's texture; an untextured primitive "
            "already takes the scene lightmap per texel, which costs no "
            "texture at all";
        return false;
    }
    if (scene.empty()) {
        err = "the scene has no geometry to gather light from (bake GI first)";
        return false;
    }

    objparser::Model m;
    const fs::path modelFile = fs::path(p.dir) / o.modelPath;
    if (!objparser::load(modelFile.string(), m) || m.submeshes.empty()) {
        err = "cannot read " + o.modelPath;
        return false;
    }
    // The object's own material override is deliberately NOT applied: the bake
    // is about to replace it with the pre-lit one, and reading the albedo from
    // the model's own .mtl is what makes a re-bake idempotent - otherwise the
    // second bake would multiply light into a texture that already has it.

    // ...but the model's automatic AO (docs/ambient-occlusion.md, "Model AO")
    // IS part of the albedo everywhere else - texbake multiplies it into the
    // shipped texture and the viewport into the uploaded one. Without this an
    // object would LOSE its self-occlusion the moment it went pre-lit, which
    // reads as the pre-lit bake having flattened it. Same helper, same
    // formula; the maps are ensured here so the headless verb works too.
    const modelao::Params aoPrm = modelao::paramsOf(p.settings);
    std::map<std::string, std::string> aoMaps;
    if (modelao::resolveFor(p, o.modelPath, aoPrm)) {
        const modelao::Plan aoPlan = modelao::planFor(p, o.modelPath, aoPrm);
        aoMaps = modelao::ensureAll(p, aoPrm, aoPlan, nullptr);
    }

    int size = prm.size;
    if (size < 32) size = 32;
    if (size > 512) size = 512;
    out.size = size;
    out.rgba.assign((size_t)size * size * 4, 0);
    std::vector<uint8_t> covered((size_t)size * size, 0);
    double lightSum = 0.0;
    int lit = 0;

    const Basis b = basisOf(o);

    // Every submesh into ONE canvas: a model's materials share its UV layout
    // and its islands do not overlap, so each texel belongs to exactly one of
    // them and takes that one's albedo. That is what lets a wall/roof/trim
    // model come back as a single pre-lit texture.
    for (const objparser::Submesh& sm : m.submeshes) {
        out.materials.push_back(sm.material);
        SourceTex tex;
        if (!sm.texture.empty()) {
            fs::path tp = modelFile.parent_path() / sm.texture;
            if (!fs::exists(tp)) tp = fs::path(p.dir) / sm.texture;
            loadTexture(tp, tex);
            if (tex.ok()) {
                const std::string rel =
                    modelao::textureRel(p, o.modelPath, sm.texture);
                if (auto it = aoMaps.find(rel); it != aoMaps.end())
                    modelao::applyMapFile(it->second, tex.px.data(), tex.w,
                                          tex.h, aoPrm.strength);
            }
        }
        const size_t triCount = sm.verts.size() / 24;  // 3 verts * 8 floats
        for (size_t t = 0; t < triCount; ++t) {
            const float* v0 = &sm.verts[t * 24];
            const float* v1 = v0 + 8;
            const float* v2 = v0 + 16;
            const float uv[3][2] = {{v0[6], v0[7]}, {v1[6], v1[7]}, {v2[6], v2[7]}};
            // UV-space bounding box of the triangle, in texels.
            float lo[2] = {uv[0][0], uv[0][1]}, hi[2] = {uv[0][0], uv[0][1]};
            for (int i = 1; i < 3; ++i)
                for (int c = 0; c < 2; ++c) {
                    lo[c] = std::min(lo[c], uv[i][c]);
                    hi[c] = std::max(hi[c], uv[i][c]);
                }
            int x0 = (int)std::floor(lo[0] * size) - 1;
            int x1 = (int)std::ceil(hi[0] * size) + 1;
            int y0 = (int)std::floor((1.0f - hi[1]) * size) - 1;
            int y1 = (int)std::ceil((1.0f - lo[1]) * size) + 1;
            x0 = std::max(x0, 0), y0 = std::max(y0, 0);
            x1 = std::min(x1, size - 1), y1 = std::min(y1, size - 1);
            const float d = (uv[1][0] - uv[0][0]) * (uv[2][1] - uv[0][1]) -
                            (uv[2][0] - uv[0][0]) * (uv[1][1] - uv[0][1]);
            if (std::fabs(d) < 1e-12f) continue;  // degenerate in UV space
            for (int y = y0; y <= y1; ++y) {
                for (int x = x0; x <= x1; ++x) {
                    const size_t idx = (size_t)y * size + x;
                    if (covered[idx]) continue;  // first island to claim it wins
                    const float pu = (x + 0.5f) / size;
                    const float pv = 1.0f - (y + 0.5f) / size;
                    // Barycentric, in UV space.
                    const float w1 = ((pu - uv[0][0]) * (uv[2][1] - uv[0][1]) -
                                      (uv[2][0] - uv[0][0]) * (pv - uv[0][1])) / d;
                    const float w2 = ((uv[1][0] - uv[0][0]) * (pv - uv[0][1]) -
                                      (pu - uv[0][0]) * (uv[1][1] - uv[0][1])) / d;
                    const float w0 = 1.0f - w1 - w2;
                    const float e = -0.002f;  // a hair outside still counts
                    if (w0 < e || w1 < e || w2 < e) continue;
                    float lp[3], ln[3];
                    for (int c = 0; c < 3; ++c) {
                        lp[c] = w0 * v0[c] + w1 * v1[c] + w2 * v2[c];
                        ln[c] = w0 * v0[3 + c] + w1 * v1[3 + c] + w2 * v2[3 + c];
                    }
                    float wp[3], wn[3];
                    toWorld(b, lp, wp);
                    normalToWorld(b, ln, wn);
                    // Lift off the surface along its own normal: the gather
                    // starts ON the geometry it is standing on, and a ray that
                    // begins exactly there hits it at t = 0.
                    for (int c = 0; c < 3; ++c) wp[c] += wn[c] * 0.01f;
                    float light[3];
                    gibake::gather(scene, wp, wn,
                                   prm.seed * 2654435761u + (uint32_t)idx,
                                   prm.rays, light);
                    float alb[3] = {sm.kd[0], sm.kd[1], sm.kd[2]};
                    if (tex.ok()) {
                        float tc[3];
                        tex.sample(w0 * uv[0][0] + w1 * uv[1][0] + w2 * uv[2][0],
                                   w0 * uv[0][1] + w1 * uv[1][1] + w2 * uv[2][1], tc);
                        for (int c = 0; c < 3; ++c) alb[c] *= tc[c];
                    }
                    uint8_t* px = &out.rgba[idx * 4];
                    for (int c = 0; c < 3; ++c) {
                        float l = light[c] * prm.strength;
                        if (l < prm.floorLevel) l = prm.floorLevel;
                        float val = alb[c] * l;
                        if (val > 1.0f) val = 1.0f;
                        px[c] = (uint8_t)(val * 255.0f + 0.5f);
                    }
                    px[3] = 255;
                    covered[idx] = 1;
                    lightSum += (light[0] + light[1] + light[2]) / 3.0;
                    ++lit;
                }
            }
        }
    }

    if (lit == 0) {
        err = "the model's UVs cover no texels (is it unwrapped?)";
        return false;
    }

    // Dilate into the seam ring, or bilinear sampling and the mip chain pull
    // background into every island edge.
    for (int pass = 0; pass < prm.padding; ++pass) {
        std::vector<uint8_t> grew = covered;
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x) {
                const size_t idx = (size_t)y * size + x;
                if (covered[idx]) continue;
                int acc[3] = {0, 0, 0}, n = 0;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int sx = x + dx, sy = y + dy;
                        if (sx < 0 || sy < 0 || sx >= size || sy >= size) continue;
                        const size_t si = (size_t)sy * size + sx;
                        if (!covered[si]) continue;
                        for (int c = 0; c < 3; ++c) acc[c] += out.rgba[si * 4 + c];
                        ++n;
                    }
                if (!n) continue;
                for (int c = 0; c < 3; ++c)
                    out.rgba[idx * 4 + c] = (uint8_t)(acc[c] / n);
                out.rgba[idx * 4 + 3] = 255;
                grew[idx] = 1;
            }
        covered.swap(grew);
    }

    out.litTexels = lit;
    out.meanLight = (float)(lightSum / lit);
    return true;
}

void Baker::start(const Project& p, int sceneIndex, int objectIndex,
                  const Params& prm) {
    start(p, sceneIndex, std::vector<int>{objectIndex}, prm);
    object_ = objectIndex;  // ...and this run BELONGS to that object
}

void Baker::start(const Project& p, int sceneIndex, std::vector<int> objects,
                  const Params& prm) {
    cancel();
    cancel_ = false;
    running_ = true;
    progress_ = 0.0f;
    scene_ = sceneIndex;
    object_ = -1;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        status_ = "building the scene...";
        error_.clear();
        done_.clear();
    }
    // The Project is COPIED into the worker: the editor keeps editing while
    // this runs, and a bake reading a model the user is mid-edit on would be
    // reading a moving target.
    worker_ = std::thread(&Baker::run, this, p, sceneIndex, std::move(objects),
                          prm);
}

void Baker::cancel() {
    cancel_ = true;
    if (worker_.joinable()) worker_.join();
    running_ = false;
}

std::string Baker::status() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return status_;
}

std::string Baker::error() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return error_;
}

bool Baker::take(std::vector<Done>& out) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (done_.empty()) return false;
    out = std::move(done_);
    done_.clear();
    return true;
}

void Baker::run(Project p, int sceneIndex, std::vector<int> objects,
                Params prm) {
    std::string err;
    if (sceneIndex >= 0 && sceneIndex < (int)p.scenes.size()) {
        SceneData& sc = p.scenes[sceneIndex];
        gibake::Settings st = gibake::settingsOf(p.settings);
        st.enabled = true;  // the gather works whether or not GI ships
        // ONE build and ONE solve for the whole batch - that is the entire
        // reason a batch exists. The gather per object is seconds; the bounce
        // solve is the rest, and it is a property of the SCENE.
        gibake::Scene scene = gibake::build(p, sc, st);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            status_ = "solving bounces...";
        }
        // The solve owns most of the wall clock, so it owns most of the bar.
        gibake::solve(scene, st, &cancel_,
                      [this](float f) { progress_ = f * 0.7f; });
        // Hashed once for the batch: it reads every file the GI bake reads.
        const uint64_t sceneSig = cancel_ ? 0 : sceneSignature(p, sc);
        const int n = (int)objects.size();
        for (int i = 0; i < n && !cancel_; ++i) {
            const int oi = objects[i];
            const std::string name =
                (oi >= 0 && oi < (int)sc.objects.size() &&
                 !sc.objects[oi].name.empty())
                    ? sc.objects[oi].name
                    : ("object " + std::to_string(oi));
            {
                std::lock_guard<std::mutex> lock(mutex_);
                status_ = std::to_string(i + 1) + "/" + std::to_string(n) +
                          ": " + name;
            }
            progress_ = 0.7f + 0.3f * ((float)i / (float)n);
            Result r;
            std::string e;
            if (!litbake::bakeObject(p, sc, oi, scene, prm, r, e)) {
                // One object's failure must not take the batch down - the
                // other N-1 bakes are done and paid for. The first reason is
                // what the panel shows.
                if (err.empty()) err = n > 1 ? (name + ": " + e) : e;
                continue;
            }
            Done d;
            d.objectIndex = oi;
            d.sig = signature(p, sc, oi, prm, sceneSig);
            d.result = std::move(r);
            std::lock_guard<std::mutex> lock(mutex_);
            done_.push_back(std::move(d));
        }
        if (!cancel_ && err.empty()) {
            std::lock_guard<std::mutex> lock(mutex_);
            status_ = "done";
        }
    } else {
        err = "no such scene";
    }
    if (!err.empty()) {
        std::lock_guard<std::mutex> lock(mutex_);
        error_ = err;
        status_ = err;
    }
    progress_ = 1.0f;
    running_ = false;
}

std::string applyToObject(Project& p, SceneData& sc, int objectIndex,
                          const Result& r, uint64_t sig) {
    if (objectIndex < 0 || objectIndex >= (int)sc.objects.size())
        return "no such object";
    SceneObject& o = sc.objects[objectIndex];
    const std::string base = baseNameOf(o);
    const std::string png = base + "-lit.png";
    const std::string mtl = base + "-lit.mtl";
    const fs::path dir = fs::path(p.dir) / "res" / "materials";
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (!stbi_write_png((dir / png).string().c_str(), r.size, r.size, 4,
                        r.rgba.data(), r.size * 4))
        return "cannot write " + png;
    std::ofstream f(dir / mtl);
    if (!f) return "cannot write " + mtl;
    // ONE ENTRY PER MATERIAL NAME THE MODEL USES, all pointing at the same
    // pre-lit image. An override binds by usemtl NAME: a library that does not
    // carry the model's own names overrides nothing, the parts silently lose
    // their textures, and under prelit's neutral vertex colours an untextured
    // part renders as a pure white block (found in PCSX2, not in review).
    // Kd 1 1 1 everywhere: the light - and the old per-entry Kd tint, which
    // the bake multiplied in - is IN the map now.
    f << "# generated by litbake (docs/prelit-models.md) - the scene's light is\n"
      << "# baked into this texture. Re-bake it if the lighting changes.\n";
    bool wroteAny = false;
    for (size_t mi = 0; mi < r.materials.size(); ++mi) {
        // A name may repeat across submeshes; write each once.
        bool dup = false;
        for (size_t k = 0; k < mi; ++k)
            if (r.materials[k] == r.materials[mi]) dup = true;
        if (dup) continue;
        f << "newmtl "
          << (r.materials[mi].empty() ? "default" : r.materials[mi]) << "\n"
          << "Kd 1 1 1\n"
          << "map_Kd " << png << "\n\n";
        wroteAny = true;
    }
    if (!wroteAny) f << "newmtl default\nKd 1 1 1\nmap_Kd " << png << "\n";
    f.close();
    // FIRST BAKE ONLY. A re-bake must not record the -lit material it is about
    // to assign as the thing to revert to - that would make Revert a no-op and
    // lose the object's real material for good.
    if (!o.prelit) o.prelitSource = o.materialPath;
    o.materialPath = "res/materials/" + mtl;
    o.prelit = true;
    o.prelitWanted = true;  // a successful bake IS the statement of intent
    o.prelitSig = sig;
    return "";
}

}  // namespace litbake
