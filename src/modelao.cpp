#include "modelao.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <set>

#include <stb_image.h>

#include "matbake.hpp"
#include "objparser.hpp"
#include "pngquant.hpp"
#include "wire.hpp"  // hashFile - the CONTENT hash the signature is built on

namespace fs = std::filesystem;

namespace modelao {
namespace {

// Bump when anything about the produced pixels changes (the mesh input, the
// bake parameters this module pins, the map encoding). Every cached map goes
// stale at once, which is the point.
constexpr uint64_t kCacheVersion = 1;

// The AO map's own resolution, derived from the texture it multiplies into and
// clamped: below 64 the occlusion under an eave is a single texel, above 256 a
// low-frequency signal is being stored at a cost nobody sees. The map never
// ships, so this is bake time and disk, not VRAM.
constexpr int kMinMapSize = 64;
constexpr int kMaxMapSize = 256;

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

std::string lowerExt(const fs::path& p) {
    std::string e = p.extension().string();
    for (char& c : e) c = (char)tolower((unsigned char)c);
    return e;
}

// A model's texture reference is a bare name resolved next to the file that
// named it (the .obj's directory here - objparser resolves mtllib and the
// implicit sibling .mtl from there). lexically_normal because the PS2 cannot
// walk "..", so the project-relative form must not contain one.
std::string textureRelOf(const Project& p, const fs::path& objFile,
                         const std::string& tex) {
    std::error_code ec;
    fs::path full = (objFile.parent_path() / tex).lexically_normal();
    if (!fs::exists(full, ec)) {
        const fs::path alt = (fs::path(p.dir) / tex).lexically_normal();
        if (fs::exists(alt, ec)) full = alt;
    }
    return fs::relative(full, fs::path(p.dir), ec).generic_string();
}

// The litbake outputs (docs/prelit-models.md). Their gather already carries
// occlusion; multiplying AO in on top would darken the same shadow twice.
bool isPreLitTexture(const std::string& rel) {
    const std::string name = fs::path(rel).filename().string();
    return name.size() > 8 && name.rfind("-lit.png") == name.size() - 8;
}

int mapSizeFor(int texW, int texH) {
    int want = std::max(texW, texH);
    if (want < kMinMapSize) want = kMinMapSize;
    if (want > kMaxMapSize) want = kMaxMapSize;
    int size = kMinMapSize;
    while (size < want && size < kMaxMapSize) size <<= 1;
    return size;
}

uint64_t pairKey(const Target& t) {
    uint64_t h = 0xcbf29ce484222325ull;
    mixS(h, t.modelRel);
    mixS(h, t.textureRel);
    return h;
}

std::string hex16(uint64_t v) {
    char buf[17];
    std::snprintf(buf, sizeof buf, "%016llx", (unsigned long long)v);
    return buf;
}

}  // namespace

Params paramsOf(const ProjectSettings& s) {
    Params p;
    p.enabled = s.modelAo;
    p.strength = s.modelAoStrength;
    p.rays = s.modelAoRays;
    p.dist = s.modelAoDist;
    return p;
}

bool resolveFor(const Project& p, const std::string& modelRel,
                const Params& prm) {
    auto it = p.modelAoMode.find(modelRel);
    if (it != p.modelAoMode.end()) {
        if (it->second == ForceOn) return true;
        if (it->second == ForceOff) return false;
    }
    return prm.enabled;
}

Plan plan(const Project& p, const Params& prm) {
    Plan out;
    std::error_code ec;
    const fs::path models = fs::path(p.dir) / "res" / "models";
    if (!fs::exists(models, ec)) return out;

    // Sorted, so a plan is the same list on every machine - the pixels are
    // deterministic and the ORDER a build logs them in should be too.
    std::vector<fs::path> objs;
    for (const auto& e : fs::recursive_directory_iterator(models, ec)) {
        if (!e.is_regular_file() || lowerExt(e.path()) != ".obj") continue;
        objs.push_back(e.path());
    }
    std::sort(objs.begin(), objs.end());

    // Pass 1: which model assets reference which textures. The sharing count
    // is taken over EVERY model asset, not only the enabled ones - two UV
    // layouts over one image are incompatible whether or not both bake.
    struct Asset {
        std::string modelRel;
        std::vector<std::string> textures;  // distinct, in first-use order
    };
    std::vector<Asset> assets;
    std::map<std::string, std::set<std::string>> users;  // texture -> models
    for (const fs::path& obj : objs) {
        objparser::Model m;
        if (!objparser::load(obj.string(), m)) continue;
        Asset a;
        a.modelRel = fs::relative(obj, fs::path(p.dir), ec).generic_string();
        for (const objparser::Submesh& sm : m.submeshes) {
            if (sm.texture.empty() || sm.texture == "@sky") continue;
            const std::string rel = textureRelOf(p, obj, sm.texture);
            if (rel.empty()) continue;
            if (std::find(a.textures.begin(), a.textures.end(), rel) ==
                a.textures.end())
                a.textures.push_back(rel);
            users[rel].insert(a.modelRel);
        }
        assets.push_back(std::move(a));
    }

    // Pass 2: eligibility, one row per (model, texture).
    for (const Asset& a : assets) {
        const bool on = resolveFor(p, a.modelRel, prm);
        if (a.textures.empty()) {
            out.skipped.push_back({a.modelRel, "", "no texture"});
            continue;
        }
        for (const std::string& rel : a.textures) {
            if (!on) {
                out.skipped.push_back({a.modelRel, rel, "off"});
                continue;
            }
            if (users[rel].size() > 1) {
                out.skipped.push_back({a.modelRel, rel, "shared texture"});
                continue;
            }
            if (isPreLitTexture(rel)) {
                out.skipped.push_back({a.modelRel, rel, "pre-lit"});
                continue;
            }
            Target t;
            t.modelRel = a.modelRel;
            t.textureRel = rel;
            int comp = 0;
            if (!stbi_info((fs::path(p.dir) / rel).string().c_str(), &t.texW,
                           &t.texH, &comp)) {
                out.skipped.push_back({a.modelRel, rel, "unreadable texture"});
                continue;
            }
            out.targets.push_back(t);
        }
    }
    return out;
}

std::string textureRel(const Project& p, const std::string& modelRel,
                       const std::string& texRef) {
    if (texRef.empty() || texRef == "@sky") return "";
    return textureRelOf(p, fs::path(p.dir) / modelRel, texRef);
}

Plan planFor(const Project& p, const std::string& modelRel, const Params& prm) {
    // The whole-project walk is what decides SHARING, and sharing is the rule
    // that cannot be answered from one model - so this narrows the result
    // rather than the work.
    Plan all = plan(p, prm);
    Plan out;
    for (const Target& t : all.targets)
        if (t.modelRel == modelRel) out.targets.push_back(t);
    for (const Skipped& s : all.skipped)
        if (s.modelRel == modelRel) out.skipped.push_back(s);
    return out;
}

uint64_t signature(const Project& p, const Target& t, const Params& prm) {
    uint64_t h = 0xcbf29ce484222325ull;
    mix64(h, kCacheVersion);
    mixS(h, t.modelRel);
    mixS(h, t.textureRel);
    mix64(h, (uint64_t)t.texW);
    mix64(h, (uint64_t)t.texH);
    mix64(h, (uint64_t)prm.rays);
    mixF(h, prm.dist);
    // strength is deliberately absent: it is an apply-time remap (see Params).

    auto mixFile = [&](const fs::path& file) {
        uint64_t fh = 0, fsz = 0;
        if (wire::hashFile(file.string(), fh, fsz)) {
            mix64(h, fh);
            mix64(h, fsz);
        }
    };
    const fs::path objFile = fs::path(p.dir) / t.modelRel;
    mixFile(objFile);
    // The .mtl decides which usemtl carries which texture, so a re-pointed
    // material changes what the paintable set is even with the mesh untouched.
    objparser::Model m;
    if (objparser::load(objFile.string(), m))
        for (const std::string& lib : m.mtlLibs)
            mixFile((objFile.parent_path() / lib).lexically_normal());
    return h;
}

std::string cacheDir(const Project& p) {
    return (fs::path(p.dir) / ".res-baked" / "modelao").string();
}

std::string cachePath(const Project& p, const Target& t, const Params& prm) {
    return (fs::path(cacheDir(p)) /
            (hex16(pairKey(t)) + "-" + hex16(signature(p, t, prm)) + ".png"))
        .string();
}

bool fresh(const Project& p, const Target& t, const Params& prm) {
    std::error_code ec;
    return fs::exists(cachePath(p, t, prm), ec);
}

std::string ensure(const Project& p, const Target& t, const Params& prm) {
    const std::string path = cachePath(p, t, prm);
    std::error_code ec;
    if (fs::exists(path, ec)) return "";

    const fs::path objFile = fs::path(p.dir) / t.modelRel;
    objparser::Model m;
    if (!objparser::load(objFile.string(), m) || m.submeshes.empty())
        return "cannot read " + t.modelRel;

    // The mesh matbake bakes: EVERY triangle occludes (that is the whole point
    // - a doorway is dark because the wall beside it is there), only the parts
    // drawn with this texture are painted into the map.
    matbake::MeshInput mesh;
    bool posIdxOk = true;
    for (const objparser::Submesh& sm : m.submeshes) {
        const size_t n = (sm.verts.size() / 24) * 3;  // whole triangles only
        if (!n) continue;
        const bool paint =
            !sm.texture.empty() && sm.texture != "@sky" &&
            textureRelOf(p, objFile, sm.texture) == t.textureRel;
        mesh.verts.insert(mesh.verts.end(), sm.verts.begin(),
                          sm.verts.begin() + n * 8);
        if (sm.posIdx.size() >= n)
            mesh.posIdx.insert(mesh.posIdx.end(), sm.posIdx.begin(),
                               sm.posIdx.begin() + n);
        else
            posIdxOk = false;
        mesh.paintTri.insert(mesh.paintTri.end(), n / 3, paint ? 1 : 0);
    }
    if (!posIdxOk || mesh.posIdx.size() != mesh.verts.size() / 8)
        mesh.posIdx.clear();  // weld by position instead
    if (mesh.triCount() == 0) return t.modelRel + " has no triangles";

    matbake::Params bp;
    bp.size = mapSizeFor(t.texW, t.texH);
    bp.samples = std::max(4, std::min(prm.rays, 1024));
    bp.supersample = 2;
    bp.backface = true;
    bp.padding = 4;
    bp.seed = 1;
    bp.maxDist = prm.dist;
    if (!(bp.maxDist > 0.0f)) {
        // Auto: a quarter of the model's own bounding-box diagonal. Half (the
        // matbake default) reaches across a whole prop and reads as shading
        // rather than as contact; a quarter darkens where surfaces MEET.
        const float dx = m.max[0] - m.min[0], dy = m.max[1] - m.min[1],
                    dz = m.max[2] - m.min[2];
        bp.maxDist = 0.25f * std::sqrt(dx * dx + dy * dy + dz * dz);
        if (!(bp.maxDist > 1e-5f)) bp.maxDist = 1.0f;
    }

    // matbake's Baker is progressive and asynchronous because the Material
    // Editor wants a picture within a frame. Here there is nobody to show it
    // to, so it is driven to completion and joined - no second raytracer.
    matbake::Baker baker;
    baker.start(mesh, matbake::MeshInput{}, bp);
    while (baker.running())
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    const std::string err = baker.error();
    const matbake::Maps maps = baker.snapshot();
    baker.cancel();  // joins the finished worker
    if (!err.empty()) return err;
    if (maps.empty() || (int)maps.ao.size() != maps.w * maps.h)
        return "the AO bake produced nothing for " + t.modelRel;

    // Grey into RGB, alpha opaque: the map is read back through an ordinary
    // RGBA decode and its alpha never means anything (see applyToRgba).
    std::vector<unsigned char> rgba((size_t)maps.w * maps.h * 4, 255);
    for (size_t i = 0, n = maps.ao.size(); i < n; ++i) {
        rgba[i * 4 + 0] = maps.ao[i];
        rgba[i * 4 + 1] = maps.ao[i];
        rgba[i * 4 + 2] = maps.ao[i];
    }
    fs::create_directories(cacheDir(p), ec);
    // Sweep this pair's older bakes: the signature is in the file name, so a
    // re-bake leaves the previous one behind unless it is removed here.
    const std::string prefix = hex16(pairKey(t)) + "-";
    for (const auto& e : fs::directory_iterator(cacheDir(p), ec)) {
        if (!e.is_regular_file()) continue;
        const std::string name = e.path().filename().string();
        if (name.rfind(prefix, 0) == 0) fs::remove(e.path(), ec);
    }
    std::string werr;
    if (!pngquant::writePngRGBA(path, rgba.data(), maps.w, maps.h, werr))
        return werr.empty() ? ("cannot write " + path) : werr;
    return "";
}

void applyToRgba(const unsigned char* ao, int aoW, int aoH,
                 unsigned char* rgba, int w, int h, float strength) {
    if (!ao || !rgba || aoW < 1 || aoH < 1 || w < 1 || h < 1) return;
    if (strength < 0.0f) strength = 0.0f;
    if (strength > 1.0f) strength = 1.0f;
    for (int y = 0; y < h; ++y) {
        // Bilinear: the map is usually coarser than the texture, and a nearest
        // lookup turns a smooth gradient into visible map texels.
        const float sy = ((y + 0.5f) / h) * aoH - 0.5f;
        int y0 = (int)std::floor(sy);
        const float fy = sy - y0;
        int y1 = y0 + 1;
        y0 = std::max(0, std::min(y0, aoH - 1));
        y1 = std::max(0, std::min(y1, aoH - 1));
        for (int x = 0; x < w; ++x) {
            const float sx = ((x + 0.5f) / w) * aoW - 0.5f;
            int x0 = (int)std::floor(sx);
            const float fx = sx - x0;
            int x1 = x0 + 1;
            x0 = std::max(0, std::min(x0, aoW - 1));
            x1 = std::max(0, std::min(x1, aoW - 1));
            const float a =
                ao[((size_t)y0 * aoW + x0) * 4] * (1 - fx) * (1 - fy) +
                ao[((size_t)y0 * aoW + x1) * 4] * fx * (1 - fy) +
                ao[((size_t)y1 * aoW + x0) * 4] * (1 - fx) * fy +
                ao[((size_t)y1 * aoW + x1) * 4] * fx * fy;
            // THE FORMULA. strength 0 = the texture untouched, 1 = the raw
            // occlusion. Alpha is left alone on purpose.
            const float mul = 1.0f - strength * (1.0f - a / 255.0f);
            unsigned char* px = &rgba[((size_t)y * w + x) * 4];
            for (int c = 0; c < 3; ++c) {
                float v = px[c] * mul;
                if (v < 0.0f) v = 0.0f;
                if (v > 255.0f) v = 255.0f;
                px[c] = (unsigned char)(v + 0.5f);
            }
        }
    }
}

bool applyMapFile(const std::string& mapPath, unsigned char* rgba, int w, int h,
                  float strength) {
    int aw = 0, ah = 0, comp = 0;
    unsigned char* ao = stbi_load(mapPath.c_str(), &aw, &ah, &comp, 4);
    if (!ao) return false;
    applyToRgba(ao, aw, ah, rgba, w, h, strength);
    stbi_image_free(ao);
    return true;
}

std::map<std::string, std::string> ensureAll(
    const Project& p, const Params& prm, const Plan& pl,
    const std::function<void(const std::string&)>& log) {
    std::map<std::string, std::string> out;
    for (const Target& t : pl.targets) {
        const bool had = fresh(p, t, prm);
        const std::string err = ensure(p, t, prm);
        if (!err.empty()) {
            if (log) log("[editor] model AO: " + t.modelRel + ": " + err);
            continue;
        }
        if (!had && log)
            log("[editor] model AO: baked " + t.textureRel + " (from " +
                t.modelRel + ")");
        out[t.textureRel] = cachePath(p, t, prm);
    }
    return out;
}

// --- Baker -------------------------------------------------------------------

void Baker::start(const Project& p, const Params& prm) {
    cancel();
    cancel_ = false;
    running_ = true;
    progress_ = 0.0f;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        status_ = "scanning model assets...";
    }
    // A COPY, like litbake::Baker: the editor keeps editing while this runs.
    worker_ = std::thread(&Baker::run, this, p, prm);
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

Report Baker::report() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return report_;
}

std::map<std::string, std::string> Baker::maps() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return maps_;
}

void Baker::run(Project p, Params prm) {
    const Plan pl = plan(p, prm);
    Report rep;
    std::map<std::string, std::string> table;
    const size_t total = pl.targets.size();
    size_t done = 0;
    for (size_t i = 0; i < total && !cancel_; ++i, ++done) {
        const Target& t = pl.targets[i];
        {
            std::lock_guard<std::mutex> lock(mutex_);
            status_ = "baking " + t.textureRel + " (" + std::to_string(i + 1) +
                      "/" + std::to_string(total) + ")";
        }
        const std::string err = ensure(p, t, prm);
        progress_ = (float)(i + 1) / (float)total;
        Row r;
        r.modelRel = t.modelRel;
        r.textureRel = t.textureRel;
        r.eligible = true;
        if (err.empty()) {
            r.baked = true;
            r.status = "baked";
            table[t.textureRel] = cachePath(p, t, prm);
            ++rep.baked;
        } else {
            r.status = err;
            ++rep.pending;
        }
        rep.rows.push_back(r);
    }
    // A cancelled run still reports what it knows about the rest.
    for (size_t i = done; i < total; ++i) {
        Row r;
        r.modelRel = pl.targets[i].modelRel;
        r.textureRel = pl.targets[i].textureRel;
        r.eligible = true;
        r.status = "not baked";
        rep.rows.push_back(r);
        ++rep.pending;
    }
    for (const Skipped& s : pl.skipped) {
        Row r;
        r.modelRel = s.modelRel;
        r.textureRel = s.textureRel;
        r.status = s.reason;
        rep.rows.push_back(r);
        ++rep.skipped;
    }
    std::sort(rep.rows.begin(), rep.rows.end(), [](const Row& a, const Row& b) {
        if (a.modelRel != b.modelRel) return a.modelRel < b.modelRel;
        return a.textureRel < b.textureRel;
    });
    {
        std::lock_guard<std::mutex> lock(mutex_);
        report_ = std::move(rep);
        maps_ = std::move(table);
        status_ = cancel_ ? "cancelled" : "up to date";
    }
    progress_ = 1.0f;
    version_.fetch_add(1);
    running_ = false;
}

}  // namespace modelao
