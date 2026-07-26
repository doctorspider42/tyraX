#include "chargen.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <map>

#include <stb_image.h>        // implementation lives in app.cpp
#include <stb_image_write.h>  // implementation lives in menubake.cpp

#include "fbxparser.hpp"  // animimport::parseSkel - .glb and .fbx alike
#include "gltfwrite.hpp"
#include "meshlod.hpp"
#include "mhdata.hpp"
#include "mocap.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

namespace chargen {

namespace {

// ---------------------------------------------------------------------------
// The rig
//
// MakeHuman's default skeleton is 163 bones - fingers, toes, eyes, jaw,
// breasts, twist bones. The PS2 pays for every one of them (matrix palette
// slots, per-vertex bindings, pose evaluation on the EE), so the generator
// keeps 23 and folds every other bone's weights into the nearest ancestor it
// did keep. The names are Mixamo's: nothing here needs them, but they are what
// free animation libraries and retarget tools match on, and renaming a rig
// after the fact is far more annoying than naming it right once.
struct BoneDef {
    const char* name;   // Mixamo name
    int parent;         // index into this table, -1 = root
    const char* joint;  // MakeHuman joint whose vertex cube gives the position
    const char* mh;     // MakeHuman bone this stands in for (weight collapse)
};

const BoneDef kBones[] = {
    {"mixamorig:Hips", -1, "spine05____head", "spine05"},
    {"mixamorig:Spine", 0, "spine04____head", "spine04"},
    {"mixamorig:Spine1", 1, "spine03____head", "spine03"},
    {"mixamorig:Spine2", 2, "spine02____head", "spine02"},
    {"mixamorig:Neck", 3, "neck01____head", "neck01"},
    {"mixamorig:Head", 4, "head____head", "head"},
    // A leaf marking the top of the skull: no weights, but retargeting and
    // camera framing both want to know where the head ends.
    {"mixamorig:HeadTop_End", 5, "head____tail", nullptr},
    {"mixamorig:LeftShoulder", 3, "clavicle.L____head", "clavicle.L"},
    {"mixamorig:LeftArm", 7, "upperarm01.L____head", "upperarm01.L"},
    {"mixamorig:LeftForeArm", 8, "lowerarm01.L____head", "lowerarm01.L"},
    {"mixamorig:LeftHand", 9, "wrist.L____head", "wrist.L"},
    {"mixamorig:RightShoulder", 3, "clavicle.R____head", "clavicle.R"},
    {"mixamorig:RightArm", 11, "upperarm01.R____head", "upperarm01.R"},
    {"mixamorig:RightForeArm", 12, "lowerarm01.R____head", "lowerarm01.R"},
    {"mixamorig:RightHand", 13, "wrist.R____head", "wrist.R"},
    {"mixamorig:LeftUpLeg", 0, "upperleg01.L____head", "upperleg01.L"},
    {"mixamorig:LeftLeg", 15, "lowerleg01.L____head", "lowerleg01.L"},
    {"mixamorig:LeftFoot", 16, "foot.L____head", "foot.L"},
    // The ball of the foot: MakeHuman puts it at the foot bone's tail, and the
    // five toe chains fold into it by the naming rule in resolveSlot().
    {"mixamorig:LeftToeBase", 17, "foot.L____tail", nullptr},
    {"mixamorig:RightUpLeg", 0, "upperleg01.R____head", "upperleg01.R"},
    {"mixamorig:RightLeg", 19, "lowerleg01.R____head", "lowerleg01.R"},
    {"mixamorig:RightFoot", 20, "foot.R____head", "foot.R"},
    {"mixamorig:RightToeBase", 21, "foot.R____tail", nullptr},
};
constexpr int kBoneCount = (int)(sizeof(kBones) / sizeof(kBones[0]));
constexpr int kLeftToeBase = 18;
constexpr int kRightToeBase = 22;
constexpr int kMaxInfluences = 4;  // the VU0 skinning path's fixed budget

// ---------------------------------------------------------------------------
// Data location and caching

std::string findDataDir() {
#ifdef _WIN32
    char exePath[MAX_PATH] = {};
    if (GetModuleFileNameA(nullptr, exePath, MAX_PATH) > 0) {
        std::error_code ec;
        // The editor builds into <repo>/build, next to <repo>/vendor - the
        // same relative hop templates.cpp uses to find the engine sources.
        std::filesystem::path candidate =
            std::filesystem::path(exePath).parent_path() / ".." / "vendor" / "mh-assets";
        if (std::filesystem::exists(candidate / "base.obj", ec)) {
            std::string s = std::filesystem::weakly_canonical(candidate, ec).string();
            for (char& c : s)
                if (c == '\\') c = '/';
            return s;
        }
    }
#endif
    // Harnesses and any other non-installed caller run from the repo root.
    std::error_code ec;
    if (std::filesystem::exists("vendor/mh-assets/base.obj", ec)) return "vendor/mh-assets";
    return "";
}

// One wearable's geometry, loaded once. Clothes and hair are `.mhclo` files -
// the same barycentric format as the body proxy, which is the entire reason a
// shirt fits a generated body without a cloth solver.
struct GarmentData {
    mhdata::Proxy proxy;
    mhdata::BaseMesh mesh;
    bool ok = false;
};

// Everything the generator reads from disk, loaded once. The base mesh alone
// is 1.7 MB of text; a slider drag must not re-read it.
struct DataSet {
    bool loaded = false;
    std::string error;
    mhdata::BaseMesh base;
    mhdata::Skeleton skel;
    mhdata::Weights weights;
    mhdata::Proxy proxy;
    mhdata::BaseMesh proxyMesh;
    std::map<std::string, mhdata::Target> targets;  // filled lazily, by stem

    // Per-base-vertex weights already collapsed onto our palette slots. The
    // reference weights are attached to the base MESH, not to a morph, so this
    // does not depend on Params at all - computing it per build would repeat
    // 57k lookups on every slider frame.
    std::vector<float> collapsedWeights;  // vertCount * kBoneCount

    // The skin texture, decoded and boxed down. Decoding a 2048-square PNG is
    // the single most expensive thing in a rebuild (~70 ms), and it changes
    // only when the skin or the size does.
    int skinIndex = -1, skinSize = 0;
    std::vector<unsigned char> skinPixels;  // skinSize * skinSize * 4
    std::vector<unsigned char> skinPng;

    // Wearables, loaded and baked on demand and kept for the same reason.
    std::map<std::string, GarmentData> garments;
    struct GarmentTex {
        std::vector<unsigned char> png;
    };
    std::map<std::string, GarmentTex> garmentTex;

    // Imported animation libraries, keyed by path. A Mixamo .glb is several
    // megabytes with a mesh nobody needs - parsing it per slider frame would
    // be absurd.
    std::map<std::string, glbparser::Skel> animSources;
};

DataSet& dataSet() {
    static DataSet ds;
    return ds;
}

bool ensureLoaded(std::string& error) {
    DataSet& ds = dataSet();
    if (ds.loaded) return true;
    if (!ds.error.empty()) {
        error = ds.error;
        return false;
    }

    const std::string dir = dataDir();
    if (dir.empty()) {
        ds.error =
            "MakeHuman CC0 data not found (vendor/mh-assets). Run setup.ps1 in the editor "
            "repository to fetch it.";
        error = ds.error;
        return false;
    }
    if (!mhdata::loadBaseMesh(dir + "/base.obj", ds.base, ds.error) ||
        !mhdata::loadSkeleton(dir + "/default.mhskel", ds.skel, ds.error) ||
        !mhdata::loadWeights(dir + "/default_weights.mhw", ds.weights, ds.error) ||
        !mhdata::loadProxy(dir + "/proxy741.proxy", ds.proxy, ds.error) ||
        !mhdata::loadBaseMesh(dir + "/proxy741.obj", ds.proxyMesh, ds.error)) {
        error = ds.error;
        return false;
    }
    if (ds.proxy.vertCount() != ds.proxyMesh.vertCount()) {
        // The bindings address the .obj's vertices positionally, so a mismatch
        // scrambles the mesh subtly instead of failing - refuse it loudly.
        ds.error = "proxy741: " + std::to_string(ds.proxy.vertCount()) + " bindings but " +
                   std::to_string(ds.proxyMesh.vertCount()) + " vertices in the mesh";
        error = ds.error;
        return false;
    }
    ds.loaded = true;
    return true;
}

const mhdata::Target* target(const std::string& stem) {
    DataSet& ds = dataSet();
    auto it = ds.targets.find(stem);
    if (it != ds.targets.end()) return &it->second;
    mhdata::Target t;
    std::string err;
    // A missing target is not fatal: the macro blend simply contributes
    // nothing for that combination, which is also how the header-only
    // "average" files behave.
    mhdata::loadTarget(dataDir() + "/targets/" + stem + ".target", t, err);
    return &ds.targets.emplace(stem, std::move(t)).first->second;
}

// ---------------------------------------------------------------------------
// Macro blending
//
// MakeHuman's macro sliders do not each own a target - the targets are the
// CORNERS of the space they span, and a setting is the product of one factor
// per axis. Gender has two levels, age four, muscle and weight three each, so
// the "universal" body is a blend of up to 2*2*2*2 = 16 files and the
// ethnic/gender/age face another 3*2*2 = 12.

struct Level {
    const char* name;
    float weight;
};

// Splits a 0..1 slider across the two neighbouring levels of a scale.
void levelPair(float v, const char* const* names, const float* stops, int count,
               std::vector<Level>& out) {
    v = std::clamp(v, 0.0f, 1.0f);
    for (int i = 0; i + 1 < count; ++i) {
        if (v > stops[i + 1] && i + 2 < count) continue;
        const float span = stops[i + 1] - stops[i];
        const float f = span > 0.0f ? std::clamp((v - stops[i]) / span, 0.0f, 1.0f) : 0.0f;
        if (1.0f - f > 0.0f) out.push_back({names[i], 1.0f - f});
        if (f > 0.0f) out.push_back({names[i + 1], f});
        return;
    }
    out.push_back({names[count - 1], 1.0f});
}

std::vector<Level> genderLevels(float v) {
    static const char* names[] = {"female", "male"};
    static const float stops[] = {0.0f, 1.0f};
    std::vector<Level> out;
    levelPair(v, names, stops, 2, out);
    return out;
}

std::vector<Level> ageLevels(float v) {
    // The stops are MakeHuman's own age mapping: the slider runs 1..90 years
    // and the four targets sit at 1, 10, 25 and 90.
    static const char* names[] = {"baby", "child", "young", "old"};
    static const float stops[] = {0.0f, 0.1875f, 0.5f, 1.0f};
    std::vector<Level> out;
    levelPair(v, names, stops, 4, out);
    return out;
}

std::vector<Level> tripleLevels(float v, const char* lo, const char* mid, const char* hi) {
    const char* names[] = {lo, mid, hi};
    static const float stops[] = {0.0f, 0.5f, 1.0f};
    std::vector<Level> out;
    levelPair(v, names, stops, 3, out);
    return out;
}

// The morphed base mesh for a parameter set.
std::vector<float> morphBase(const Params& p) {
    const DataSet& ds = dataSet();
    std::vector<float> pos = ds.base.pos;

    const std::vector<Level> gender = genderLevels(p.gender);
    const std::vector<Level> age = ageLevels(p.age);
    const std::vector<Level> muscle = tripleLevels(p.muscle, "minmuscle", "averagemuscle", "maxmuscle");
    const std::vector<Level> weight = tripleLevels(p.weight, "minweight", "averageweight", "maxweight");

    float eth[3] = {p.african, p.asian, p.caucasian};
    for (float& e : eth) e = std::max(e, 0.0f);
    const float ethSum = eth[0] + eth[1] + eth[2];
    if (ethSum > 0.0f)
        for (float& e : eth) e /= ethSum;
    else
        eth[2] = 1.0f;
    static const char* ethNames[3] = {"african", "asian", "caucasian"};

    for (int e = 0; e < 3; ++e) {
        if (eth[e] <= 0.0f) continue;
        for (const Level& g : gender)
            for (const Level& a : age) {
                const float w = eth[e] * g.weight * a.weight;
                if (w <= 0.0f) continue;
                mhdata::applyTarget(
                    *target(std::string(ethNames[e]) + "-" + g.name + "-" + a.name), w, pos);
            }
    }
    for (const Level& g : gender)
        for (const Level& a : age)
            for (const Level& m : muscle)
                for (const Level& wt : weight) {
                    const float w = g.weight * a.weight * m.weight * wt.weight;
                    if (w <= 0.0f) continue;
                    mhdata::applyTarget(*target(std::string("universal-") + g.name + "-" + a.name +
                                                "-" + m.name + "-" + wt.name),
                                        w, pos);
                }
    return pos;
}

// ---------------------------------------------------------------------------
// Skinning

// Which palette slot a MakeHuman bone's weights end up in: the bone itself if
// it was kept, otherwise its nearest kept ancestor. Anything that reaches the
// top unclaimed (the rig's own `root`, the two `pelvis` bones) belongs to the
// hips.
int resolveSlot(const std::string& mhBone, const mhdata::Skeleton& skel,
                const std::map<std::string, int>& claimed) {
    std::string name = mhBone;
    for (int guard = 0; guard < 64 && !name.empty(); ++guard) {
        auto it = claimed.find(name);
        if (it != claimed.end()) return it->second;
        // The toe chains: MakeHuman gives every toe its own three bones, all
        // parented straight to the foot, so the ancestor walk would put them
        // on the foot and leave the toe bone dead. Name-matching keeps the
        // ball of the foot animated.
        if (name.compare(0, 3, "toe") == 0 && name.size() >= 2) {
            if (name[name.size() - 1] == 'L') return kLeftToeBase;
            if (name[name.size() - 1] == 'R') return kRightToeBase;
        }
        const mhdata::Skeleton::Bone* b = skel.find(name);
        if (!b) break;
        name = b->parent;
    }
    return 0;  // hips
}

// The reference weights, collapsed onto our palette once and kept.
const std::vector<float>& collapsedWeights() {
    DataSet& ds = dataSet();
    if (!ds.collapsedWeights.empty()) return ds.collapsedWeights;

    std::map<std::string, int> claimed;
    for (int i = 0; i < kBoneCount; ++i)
        if (kBones[i].mh) claimed[kBones[i].mh] = i;
    // MakeHuman's rig hangs the legs off `root` through two `pelvis` bones and
    // the spine off `root` as well; neither is kept, and both belong to the
    // hips.
    claimed["root"] = 0;

    const int baseVerts = ds.base.vertCount();
    ds.collapsedWeights.assign((size_t)baseVerts * kBoneCount, 0.0f);
    for (const auto& [boneName, list] : ds.weights.bone) {
        const int slot = resolveSlot(boneName, ds.skel, claimed);
        for (const auto& [vert, w] : list) {
            if (vert < 0 || vert >= baseVerts) continue;
            ds.collapsedWeights[(size_t)vert * kBoneCount + slot] += w;
        }
    }
    return ds.collapsedWeights;
}

// The chosen skin, decoded and box-filtered to `size`. Returns nullptr when it
// cannot be decoded; the pixels stay owned by the DataSet.
const std::vector<unsigned char>* skinPixels(int index, int size, std::string& note) {
    DataSet& ds = dataSet();
    if (ds.skinIndex == index && ds.skinSize == size)
        return ds.skinPixels.empty() ? nullptr : &ds.skinPixels;
    ds.skinIndex = index;
    ds.skinSize = size;
    ds.skinPixels.clear();
    ds.skinPng.clear();  // re-encoded from the new pixels on the next build

    const std::string file = dataDir() + "/skins/" + skins()[index] + ".png";
    int w = 0, h = 0, comp = 0;
    unsigned char* px = stbi_load(file.c_str(), &w, &h, &comp, 4);
    if (!px) {
        note = "skin " + skins()[index] + " could not be decoded - untextured";
        return nullptr;
    }
    ds.skinPixels.assign((size_t)size * size * 4, 0);
    // Box filter over each output texel's source footprint. The upstream skins
    // are 2048 square, so this is an exact 8x8 average - point-sampling a skin
    // at that ratio is pure aliasing.
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            const int sx0 = (int)((int64_t)x * w / size);
            const int sx1 = std::max(sx0 + 1, (int)((int64_t)(x + 1) * w / size));
            const int sy0 = (int)((int64_t)y * h / size);
            const int sy1 = std::max(sy0 + 1, (int)((int64_t)(y + 1) * h / size));
            unsigned sum[3] = {0, 0, 0};
            unsigned n = 0;
            for (int sy = sy0; sy < sy1 && sy < h; ++sy)
                for (int sx = sx0; sx < sx1 && sx < w; ++sx) {
                    const unsigned char* s = px + ((size_t)sy * w + sx) * 4;
                    for (int k = 0; k < 3; ++k) sum[k] += s[k];
                    ++n;
                }
            unsigned char* d = &ds.skinPixels[((size_t)y * size + x) * 4];
            for (int k = 0; k < 3; ++k) d[k] = (unsigned char)(n ? sum[k] / n : 0);
            // Forced opaque: StaPip draws with the GS alpha test set to "pass
            // only when alpha != 0", so a transparent texel in a body skin
            // would punch a hole straight through the character.
            d[3] = 255;
        }
    stbi_image_free(px);
    return &ds.skinPixels;
}

struct Influence {
    int slot;
    float weight;
};

void addInfluence(std::vector<Influence>& v, int slot, float w) {
    for (Influence& i : v)
        if (i.slot == slot) {
            i.weight += w;
            return;
        }
    v.push_back({slot, w});
}

// Skin bindings for one proxy vertex: it rides three base vertices, so its
// skinning is those three vertices' skinning mixed by the same barycentric
// weights its position uses. Bodies and garments bind identically - a shirt
// follows the shoulder because it is bound to the shoulder's vertices.
void bindingsFor(const mhdata::Proxy& proxy, int i, const std::vector<float>& baseWeights,
                 int baseVerts, int boneCount, unsigned char* joints, unsigned char* weights) {
    std::vector<Influence> inf;
    for (int k = 0; k < 3; ++k) {
        const int v = proxy.ref[i * 3 + k];
        const float bw = proxy.weight[i * 3 + k];
        if (v < 0 || v >= baseVerts || bw == 0.0f) continue;
        const float* row = &baseWeights[(size_t)v * boneCount];
        for (int s = 0; s < boneCount; ++s)
            if (row[s] > 0.0f) addInfluence(inf, s, row[s] * bw);
    }
    std::sort(inf.begin(), inf.end(),
              [](const Influence& a, const Influence& b) { return a.weight > b.weight; });
    if ((int)inf.size() > kMaxInfluences) inf.resize(kMaxInfluences);

    for (int k = 0; k < kMaxInfluences; ++k) {
        joints[k] = 0;
        weights[k] = 0;
    }
    float sum = 0.0f;
    for (const Influence& in : inf) sum += in.weight;
    if (sum <= 0.0f) {
        // A vertex the reference weights never touched (a proxy reaches
        // slightly outside the body in places): bind it rigidly to the hips
        // rather than leaving it unskinned at the origin.
        weights[0] = 255;
        return;
    }
    // Quantize to bytes summing to exactly 255, handing the rounding error to
    // the strongest influence, where it is least visible.
    int total = 0;
    for (size_t k = 0; k < inf.size(); ++k) {
        joints[k] = (unsigned char)inf[k].slot;
        weights[k] = (unsigned char)std::clamp((int)std::lround(inf[k].weight / sum * 255.0f), 0, 255);
        total += weights[k];
    }
    weights[0] = (unsigned char)std::clamp((int)weights[0] + (255 - total), 0, 255);
}

GarmentData* garment(const std::string& sub, const std::string& stem, std::string& note) {
    DataSet& ds = dataSet();
    const std::string key = sub + "/" + stem;
    auto it = ds.garments.find(key);
    if (it != ds.garments.end()) return it->second.ok ? &it->second : nullptr;

    GarmentData g;
    std::string err;
    const std::string base = dataDir() + "/" + sub + "/" + stem;
    if (mhdata::loadProxy(base + ".mhclo", g.proxy, err) &&
        mhdata::loadBaseMesh(base + ".obj", g.mesh, err) &&
        g.proxy.vertCount() == g.mesh.vertCount()) {
        g.ok = true;
    } else {
        note = stem + ": " + (err.empty() ? "bindings and mesh disagree" : err);
    }
    GarmentData& stored = ds.garments.emplace(key, std::move(g)).first->second;
    return stored.ok ? &stored : nullptr;
}

// A wearable's diffuse map, decoded and boxed down. `cutout` keeps the alpha
// channel and forces it BINARY with the opaque colors dilated outward - hair
// is an alpha-cutout card, and the engine's palettized tRNS->CLUT path loses a
// soft gradient while bilinear filtering would ring dark fringes through it
// (the same rule treegen's leaf card follows).
const std::vector<unsigned char>* garmentTexture(const std::string& sub, const std::string& stem,
                                                 int size, bool cutout, std::string& note) {
    DataSet& ds = dataSet();
    const std::string key = sub + "/" + stem + "@" + std::to_string(size);
    auto it = ds.garmentTex.find(key);
    if (it != ds.garmentTex.end())
        return it->second.png.empty() ? nullptr : &it->second.png;

    std::vector<unsigned char> png;
    const std::string file = dataDir() + "/" + sub + "/" + stem + "_diffuse.png";
    int w = 0, h = 0, comp = 0;
    unsigned char* px = stbi_load(file.c_str(), &w, &h, &comp, 4);
    if (!px) {
        note = stem + ": texture could not be decoded";
        ds.garmentTex[key] = {};
        return nullptr;
    }
    std::vector<unsigned char> small((size_t)size * size * 4);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            const int sx0 = (int)((int64_t)x * w / size);
            const int sx1 = std::max(sx0 + 1, (int)((int64_t)(x + 1) * w / size));
            const int sy0 = (int)((int64_t)y * h / size);
            const int sy1 = std::max(sy0 + 1, (int)((int64_t)(y + 1) * h / size));
            unsigned sum[4] = {0, 0, 0, 0};
            unsigned n = 0, opaque = 0;
            for (int sy = sy0; sy < sy1 && sy < h; ++sy)
                for (int sx = sx0; sx < sx1 && sx < w; ++sx) {
                    const unsigned char* s = px + ((size_t)sy * w + sx) * 4;
                    // Average only the opaque source texels, or a cutout's
                    // edge fades into whatever the transparent pixels hold
                    // (usually black).
                    if (!cutout || s[3] >= 128) {
                        for (int k = 0; k < 3; ++k) sum[k] += s[k];
                        ++opaque;
                    }
                    sum[3] += s[3];
                    ++n;
                }
            unsigned char* d = &small[((size_t)y * size + x) * 4];
            for (int k = 0; k < 3; ++k) d[k] = (unsigned char)(opaque ? sum[k] / opaque : 0);
            d[3] = cutout ? (unsigned char)((n && sum[3] / n >= 110) ? 255 : 0) : 255;
        }
    stbi_image_free(px);

    if (cutout) {
        // Dilate opaque color into the transparent margin so bilinear sampling
        // never pulls black in from outside the cutout.
        for (int pass = 0; pass < 2; ++pass) {
            std::vector<unsigned char> src = small;
            for (int y = 0; y < size; ++y)
                for (int x = 0; x < size; ++x) {
                    unsigned char* d = &small[((size_t)y * size + x) * 4];
                    if (src[((size_t)y * size + x) * 4 + 3]) continue;
                    unsigned sum[3] = {0, 0, 0}, n = 0;
                    for (int dy = -1; dy <= 1; ++dy)
                        for (int dx = -1; dx <= 1; ++dx) {
                            const int nx = x + dx, ny = y + dy;
                            if (nx < 0 || ny < 0 || nx >= size || ny >= size) continue;
                            const unsigned char* s = &src[((size_t)ny * size + nx) * 4];
                            if (!s[3] && !(pass && s[0] + s[1] + s[2])) continue;
                            for (int k = 0; k < 3; ++k) sum[k] += s[k];
                            ++n;
                        }
                    if (n)
                        for (int k = 0; k < 3; ++k) d[k] = (unsigned char)(sum[k] / n);
                }
        }
    }

    stbi_write_png_to_func(
        [](void* ctx, void* data, int len) {
            std::vector<unsigned char>* v = (std::vector<unsigned char>*)ctx;
            v->insert(v->end(), (unsigned char*)data, (unsigned char*)data + len);
        },
        &png, size, size, 4, small.data(), size * 4);
    DataSet::GarmentTex& stored = ds.garmentTex[key];
    stored.png = std::move(png);
    return stored.png.empty() ? nullptr : &stored.png;
}

// Thins an alpha-cutout garment by dropping whole CARDS, largest kept first.
//
// Edge collapse is close to useless on hair: it is a pile of separate quads
// with a uv seam around every one, and meshlod locks seam and border vertices
// by construction - a 3678-triangle hairstyle asked for 550 triangles comes
// back at 2696. Removing whole strands is both what actually shrinks it and
// what a low-poly hairstyle is: fewer, bigger cards. Smallest cards go first,
// which is the wispy detail nobody sees at PS2 resolution.
void thinCards(std::vector<float>& corners, std::vector<unsigned char>& joints,
               std::vector<unsigned char>& weights, size_t targetTris) {
    const size_t triCount = corners.size() / 24;
    if (triCount <= targetTris) return;

    // Connected components over shared positions (exact bits - the cards come
    // from one .obj, so a shared corner is bit-identical).
    std::map<std::array<uint32_t, 3>, int> vertId;
    std::vector<int> parent;
    auto find = [&](int a) {
        while (parent[a] != a) a = parent[a] = parent[parent[a]];
        return a;
    };
    std::vector<int> triVert(triCount * 3, 0);
    for (size_t t = 0; t < triCount; ++t)
        for (int c = 0; c < 3; ++c) {
            std::array<uint32_t, 3> key{};
            std::memcpy(key.data(), &corners[(t * 3 + c) * 8], 12);
            auto it = vertId.find(key);
            if (it == vertId.end()) {
                it = vertId.emplace(key, (int)parent.size()).first;
                parent.push_back((int)parent.size());
            }
            triVert[t * 3 + c] = it->second;
        }
    for (size_t t = 0; t < triCount; ++t) {
        const int a = find(triVert[t * 3]), b = find(triVert[t * 3 + 1]),
                  c = find(triVert[t * 3 + 2]);
        parent[b] = a;
        parent[find(c)] = a;
    }

    struct Card {
        double area = 0.0;
        std::vector<size_t> tris;
    };
    std::map<int, Card> cards;
    for (size_t t = 0; t < triCount; ++t) {
        const float* p = &corners[t * 24];
        const float u[3] = {p[8] - p[0], p[9] - p[1], p[10] - p[2]};
        const float v[3] = {p[16] - p[0], p[17] - p[1], p[18] - p[2]};
        const float n[3] = {u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2],
                            u[0] * v[1] - u[1] * v[0]};
        Card& card = cards[find(triVert[t * 3])];
        card.area += 0.5 * std::sqrt((double)n[0] * n[0] + (double)n[1] * n[1] + (double)n[2] * n[2]);
        card.tris.push_back(t);
    }
    if (cards.size() < 2) return;  // one shell: nothing to thin, leave it be

    std::vector<const Card*> order;
    order.reserve(cards.size());
    for (const auto& [id, c] : cards) order.push_back(&c);
    std::sort(order.begin(), order.end(),
              [](const Card* a, const Card* b) { return a->area > b->area; });

    std::vector<char> keep(triCount, 0);
    size_t kept = 0;
    for (const Card* c : order) {
        if (kept && kept + c->tris.size() > targetTris) continue;
        for (size_t t : c->tris) keep[t] = 1;
        kept += c->tris.size();
    }

    std::vector<float> outCorners;
    std::vector<unsigned char> outJoints, outWeights;
    for (size_t t = 0; t < triCount; ++t) {
        if (!keep[t]) continue;
        outCorners.insert(outCorners.end(), corners.begin() + t * 24, corners.begin() + t * 24 + 24);
        outJoints.insert(outJoints.end(), joints.begin() + t * 12, joints.begin() + t * 12 + 12);
        outWeights.insert(outWeights.end(), weights.begin() + t * 12, weights.begin() + t * 12 + 12);
    }
    corners = std::move(outCorners);
    joints = std::move(outJoints);
    weights = std::move(outWeights);
}

// Welds, decimates and re-expands one skinned triangle list. meshlod's own
// unweld does not carry skin bindings, so this mirrors what generateSkelLods
// does for the .tskl LOD tiers.
void decimateSkinned(std::vector<float>& corners, std::vector<unsigned char>& joints,
                     std::vector<unsigned char>& weights, size_t targetVerts) {
    const size_t count = corners.size() / 8;
    if (count < 3 || targetVerts >= count) return;
    std::vector<float> pos(count * 3), nrm(count * 3), uv(count * 2);
    for (size_t i = 0; i < count; ++i) {
        std::memcpy(&pos[i * 3], &corners[i * 8], 12);
        std::memcpy(&nrm[i * 3], &corners[i * 8 + 3], 12);
        std::memcpy(&uv[i * 2], &corners[i * 8 + 6], 8);
    }
    meshlod::Mesh m = meshlod::weld(pos.data(), nrm.data(), uv.data(), joints.data(),
                                    weights.data(), count, true);
    meshlod::decimate(m, targetVerts < 3 ? 3 : targetVerts);

    corners.clear();
    joints.clear();
    weights.clear();
    for (uint32_t idx : m.tris) {
        corners.insert(corners.end(), {m.pos[idx * 3], m.pos[idx * 3 + 1], m.pos[idx * 3 + 2],
                                       m.nrm[idx * 3], m.nrm[idx * 3 + 1], m.nrm[idx * 3 + 2],
                                       m.hasUv ? m.uv[idx * 2] : 0.0f,
                                       m.hasUv ? m.uv[idx * 2 + 1] : 0.0f});
        for (int k = 0; k < 4; ++k) {
            joints.push_back(m.hasSkin ? m.joints[idx * 4 + k] : 0);
            weights.push_back(m.hasSkin ? m.weights[idx * 4 + k] : (k ? 0 : 255));
        }
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// Public surface

bool Params::operator==(const Params& o) const {
    return gender == o.gender && age == o.age && muscle == o.muscle && weight == o.weight &&
           african == o.african && asian == o.asian && caucasian == o.caucasian &&
           heightMeters == o.heightMeters && skin == o.skin && textureSize == o.textureSize &&
           clothes == o.clothes && shoes == o.shoes && hair == o.hair &&
           clothingDetail == o.clothingDetail && animations == o.animations && anim == o.anim &&
           animSource == o.animSource && retarget.fps == o.retarget.fps &&
           retarget.inPlace == o.retarget.inPlace &&
           retarget.ground.enabled == o.retarget.ground.enabled && name == o.name;
}

std::string dataDir() {
    static const std::string dir = findDataDir();
    return dir;
}

bool dataAvailable() { return !dataDir().empty(); }

const std::vector<std::string>& skins() {
    static std::vector<std::string> list = [] {
        std::vector<std::string> out;
        const std::string dir = dataDir();
        if (dir.empty()) return out;
        std::error_code ec;
        for (const auto& e : std::filesystem::directory_iterator(dir + "/skins", ec)) {
            if (!e.is_regular_file(ec)) continue;
            if (e.path().extension() != ".png") continue;
            out.push_back(e.path().stem().string());
        }
        std::sort(out.begin(), out.end());
        return out;
    }();
    return list;
}

namespace {

// Asset stems in one wardrobe directory, split by whether the name looks like
// footwear (upstream keeps shoes in `clothes/`, and a shoes-or-suit choice is
// not a choice anyone wants to make).
std::vector<std::string> scanWardrobe(const char* sub, bool wantShoes) {
    std::vector<std::string> out;
    const std::string dir = dataDir();
    if (dir.empty()) return out;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(dir + "/" + sub, ec)) {
        if (!e.is_regular_file(ec) || e.path().extension() != ".mhclo") continue;
        const std::string stem = e.path().stem().string();
        const bool shoes = stem.rfind("shoes", 0) == 0;
        if (shoes == wantShoes) out.push_back(stem);
    }
    std::sort(out.begin(), out.end());
    return out;
}

}  // namespace

const std::vector<std::string>& clothesList() {
    static const std::vector<std::string> list = scanWardrobe("clothes", false);
    return list;
}

const std::vector<std::string>& shoesList() {
    static const std::vector<std::string> list = scanWardrobe("clothes", true);
    return list;
}

const std::vector<std::string>& hairList() {
    static const std::vector<std::string> list = scanWardrobe("hair", false);
    return list;
}

const std::vector<std::string>& boneNames() {
    static const std::vector<std::string> names = [] {
        std::vector<std::string> out;
        for (const BoneDef& b : kBones) out.push_back(b.name);
        return out;
    }();
    return names;
}

bool build(const Params& p, glbparser::Skel& out, std::vector<std::string>& warnings,
           std::string& error) {
    if (!ensureLoaded(error)) return false;
    const DataSet& ds = dataSet();
    out = glbparser::Skel();

    // --- morph + fit --------------------------------------------------------
    const std::vector<float> basePos = morphBase(p);
    std::vector<float> vertPos = mhdata::fitProxy(ds.proxy, basePos);
    const int vertCount = ds.proxy.vertCount();

    // --- rig ----------------------------------------------------------------
    std::vector<std::array<float, 3>> boneHead(kBoneCount, {0.0f, 0.0f, 0.0f});
    for (int i = 0; i < kBoneCount; ++i) {
        float pos[3] = {0, 0, 0};
        if (!mhdata::jointPos(ds.skel, kBones[i].joint, basePos, pos)) {
            warnings.push_back(std::string("rig: joint ") + kBones[i].joint +
                               " missing - bone placed on its parent");
            const int parent = kBones[i].parent;
            if (parent >= 0) boneHead[i] = boneHead[parent];
            continue;
        }
        boneHead[i] = {pos[0], pos[1], pos[2]};
    }

    // --- world transform ----------------------------------------------------
    // MakeHuman works in decimetres with the origin at the hips; a game wants
    // metres with the origin between the feet. Both the mesh and the rig go
    // through the same scale+offset, or the skin slides off the skeleton.
    float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
    for (int i = 0; i < vertCount; ++i)
        for (int k = 0; k < 3; ++k) {
            lo[k] = std::min(lo[k], vertPos[i * 3 + k]);
            hi[k] = std::max(hi[k], vertPos[i * 3 + k]);
        }
    const float rawHeight = hi[1] - lo[1];
    const float scale = rawHeight > 1e-4f ? std::max(p.heightMeters, 0.05f) / rawHeight : 0.1f;
    const float offset[3] = {-(lo[0] + hi[0]) * 0.5f, -lo[1], -(lo[2] + hi[2]) * 0.5f};
    auto toWorld = [&](float* v) {
        for (int k = 0; k < 3; ++k) v[k] = (v[k] + offset[k]) * scale;
    };
    for (int i = 0; i < vertCount; ++i) toWorld(&vertPos[i * 3]);
    for (std::array<float, 3>& h : boneHead) toWorld(h.data());

    // --- nodes + palette ----------------------------------------------------
    // Every bone keeps identity rotation, so a bone's local space is world
    // space at bind and the inverse bind matrix is a pure translation. That
    // makes the rig trivial to reason about and to author against; a roll-
    // aware rig would only matter to an animation authored for MakeHuman's
    // own bone axes, which is not where clips come from here.
    out.nodes.resize(kBoneCount);
    out.palette.resize(kBoneCount);
    for (int i = 0; i < kBoneCount; ++i) {
        glbparser::SkelNode& n = out.nodes[i];
        n.name = kBones[i].name;
        n.parent = kBones[i].parent;
        const int parent = kBones[i].parent;
        for (int k = 0; k < 3; ++k)
            n.t[k] = boneHead[i][k] - (parent >= 0 ? boneHead[parent][k] : 0.0f);

        glbparser::SkelJoint& j = out.palette[i];
        j.node = i;
        for (int k = 0; k < 4; ++k) j.ibm[k * 5] = 1.0f;
        for (int k = 0; k < 3; ++k) j.ibm[12 + k] = -boneHead[i][k];
    }

    // --- weights ------------------------------------------------------------
    const int baseVerts = ds.base.vertCount();
    const std::vector<float>& baseWeights = collapsedWeights();

    std::vector<unsigned char> jointBytes((size_t)vertCount * kMaxInfluences, 0);
    std::vector<unsigned char> weightBytes((size_t)vertCount * kMaxInfluences, 0);
    for (int i = 0; i < vertCount; ++i)
        bindingsFor(ds.proxy, i, baseWeights, baseVerts, kBoneCount,
                    &jointBytes[(size_t)i * kMaxInfluences],
                    &weightBytes[(size_t)i * kMaxInfluences]);

    // --- wearables: what they cover comes off the body ------------------------
    // Loaded before the body's triangles are expanded, because each garment
    // lists the base-mesh vertices it hides. Without that the torso pokes
    // through the shirt in exactly the places a shirt is supposed to cover.
    struct Wear {
        const char* sub;
        std::string stem;
        bool cutout;   // alpha-tested (hair) rather than solid
        float budget;  // share of the triangle budget this slot deserves
    };
    std::vector<Wear> wear;
    auto pick = [&](const std::vector<std::string>& list, int index, const char* sub, bool cutout,
                    float budget) {
        if (index >= 0 && index < (int)list.size())
            wear.push_back({sub, list[index], cutout, budget});
    };
    // The slots do not deserve equal budgets: a suit is most of the
    // silhouette, shoes are two small blocks at the bottom of the screen, and
    // hair is somewhere between. Upstream they are all 3.5k-16k triangles.
    pick(clothesList(), p.clothes, "clothes", false, 1.0f);
    pick(shoesList(), p.shoes, "clothes", false, 0.22f);
    pick(hairList(), p.hair, "hair", true, 0.5f);

    std::vector<const GarmentData*> worn;
    std::vector<char> bodyHidden((size_t)vertCount, 0);
    for (const Wear& w : wear) {
        std::string note;
        const GarmentData* g = garment(w.sub, w.stem, note);
        if (!note.empty()) warnings.push_back(note);
        worn.push_back(g);
        if (!g || g->proxy.deleteVerts.empty()) continue;
        for (int i = 0; i < vertCount; ++i) {
            if (bodyHidden[i]) continue;
            // A body vertex counts as covered only when ALL three base
            // vertices it rides are hidden - a coarse proxy vertex straddling
            // the garment's edge must stay, or the body gains a hole beside
            // the seam.
            bool all = true;
            for (int k = 0; k < 3 && all; ++k) all = g->proxy.deletes(ds.proxy.ref[i * 3 + k]);
            if (all) bodyHidden[i] = 1;
        }
    }

    // --- smooth normals on the fitted mesh ----------------------------------
    std::vector<float> normals((size_t)vertCount * 3, 0.0f);
    auto accumulate = [&](int a, int b, int c) {
        const float* pa = &vertPos[a * 3];
        const float* pb = &vertPos[b * 3];
        const float* pc = &vertPos[c * 3];
        const float u[3] = {pb[0] - pa[0], pb[1] - pa[1], pb[2] - pa[2]};
        const float v[3] = {pc[0] - pa[0], pc[1] - pa[1], pc[2] - pa[2]};
        // Not normalized on purpose: the cross product's length is twice the
        // triangle's area, which is the weighting a smooth normal wants.
        const float n[3] = {u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2],
                            u[0] * v[1] - u[1] * v[0]};
        for (int idx : {a, b, c})
            for (int k = 0; k < 3; ++k) normals[idx * 3 + k] += n[k];
    };
    for (const mhdata::BaseMesh::Face& f : ds.proxyMesh.faces) {
        accumulate(f.v[0], f.v[1], f.v[2]);
        if (f.n == 4) accumulate(f.v[0], f.v[2], f.v[3]);
    }

    // Winding check: on a closed body, outward normals point away from the
    // centroid. If the sum says otherwise the .obj is wound the other way, and
    // flipping here costs nothing where guessing later would cost a rebuild.
    float centroid[3] = {0, 0, 0};
    for (int i = 0; i < vertCount; ++i)
        for (int k = 0; k < 3; ++k) centroid[k] += vertPos[i * 3 + k] / (float)vertCount;
    double facing = 0.0;
    for (int i = 0; i < vertCount; ++i)
        for (int k = 0; k < 3; ++k)
            facing += (double)(vertPos[i * 3 + k] - centroid[k]) * normals[i * 3 + k];
    const bool flip = facing < 0.0;
    for (float& n : normals)
        if (flip) n = -n;
    for (int i = 0; i < vertCount; ++i) {
        float* n = &normals[i * 3];
        const float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (len > 1e-8f)
            for (int k = 0; k < 3; ++k) n[k] /= len;
        else
            n[1] = 1.0f;
    }

    // --- expand to a flat triangle list -------------------------------------
    glbparser::SkelPart part;
    part.material = "skin";
    auto pushCorner = [&](int v, int t) {
        for (int k = 0; k < 3; ++k) part.positions.push_back(vertPos[v * 3 + k]);
        for (int k = 0; k < 3; ++k) part.normals.push_back(normals[v * 3 + k]);
        if (t >= 0 && (size_t)t * 2 + 1 < ds.proxyMesh.uv.size()) {
            part.uvs.push_back(ds.proxyMesh.uv[t * 2]);
            // OBJ has v pointing up, every consumer here works in image space.
            part.uvs.push_back(1.0f - ds.proxyMesh.uv[t * 2 + 1]);
        } else {
            part.uvs.push_back(0.0f);
            part.uvs.push_back(0.0f);
        }
        for (int k = 0; k < kMaxInfluences; ++k)
            part.joints.push_back(jointBytes[(size_t)v * kMaxInfluences + k]);
        for (int k = 0; k < kMaxInfluences; ++k)
            part.weights.push_back(weightBytes[(size_t)v * kMaxInfluences + k]);
        ++part.vertexCount;
    };
    auto pushTri = [&](const mhdata::BaseMesh::Face& f, int a, int b, int c) {
        if (flip) std::swap(b, c);
        pushCorner(f.v[a], f.t[a]);
        pushCorner(f.v[b], f.t[b]);
        pushCorner(f.v[c], f.t[c]);
    };
    for (const mhdata::BaseMesh::Face& f : ds.proxyMesh.faces) {
        // Any covered corner drops the face, which is what MakeHuman itself
        // does with a garment's deleted vertices.
        bool hidden = false;
        for (int c = 0; c < f.n && !hidden; ++c)
            hidden = f.v[c] >= 0 && f.v[c] < vertCount && bodyHidden[f.v[c]];
        if (hidden) continue;
        pushTri(f, 0, 1, 2);
        if (f.n == 4) pushTri(f, 0, 2, 3);
    }

    // --- skin texture -------------------------------------------------------
    if (p.skin >= 0 && p.skin < (int)skins().size()) {
        int size = std::clamp(p.textureSize, 32, 256);
        int pot = 32;
        while (pot * 2 <= size) pot *= 2;
        size = pot;

        std::string note;
        const std::vector<unsigned char>* px = skinPixels(p.skin, size, note);
        if (!note.empty()) warnings.push_back(note);
        if (px) {
            DataSet& ds2 = dataSet();
            if (ds2.skinPng.empty())
                stbi_write_png_to_func(
                    [](void* ctx, void* data, int len) {
                        std::vector<unsigned char>* v = (std::vector<unsigned char>*)ctx;
                        v->insert(v->end(), (unsigned char*)data, (unsigned char*)data + len);
                    },
                    &ds2.skinPng, size, size, 4, px->data(), size * 4);
            if (ds2.skinPng.empty())
                warnings.push_back("skin texture could not be encoded - untextured");
            else {
                glbparser::Image img;
                img.name = "skin";
                img.png = ds2.skinPng;
                out.images.push_back(std::move(img));
                part.image = 0;
            }
        }
    }

    out.parts.push_back(std::move(part));

    // --- wearable parts -------------------------------------------------------
    // Each garment is fitted exactly like the body proxy, skinned from the same
    // reference weights, then DECIMATED: the upstream meshes are 3.5k-16k
    // triangles, built for offline rendering. Measured floor for the suits is
    // about 1000 triangles - below that the collapse starts tearing holes in
    // them rather than simplifying.
    {
        const int budgets[3] = {500, 1100, 2200};
        const int budget = budgets[std::clamp(p.clothingDetail, 0, 2)];
        int texSize = std::clamp(p.textureSize, 32, 256);
        {
            int pot = 32;
            while (pot * 2 <= texSize) pot *= 2;
            texSize = pot;
        }
        for (size_t wi = 0; wi < wear.size(); ++wi) {
            const GarmentData* g = worn[wi];
            if (!g) continue;
            const int gv = g->proxy.vertCount();
            std::vector<float> gpos = mhdata::fitProxy(g->proxy, basePos);
            for (int i = 0; i < gv; ++i) toWorld(&gpos[i * 3]);

            std::vector<float> gnrm((size_t)gv * 3, 0.0f);
            auto accumG = [&](int a, int b, int c) {
                const float* pa = &gpos[a * 3];
                const float* pb = &gpos[b * 3];
                const float* pc = &gpos[c * 3];
                const float u[3] = {pb[0] - pa[0], pb[1] - pa[1], pb[2] - pa[2]};
                const float v[3] = {pc[0] - pa[0], pc[1] - pa[1], pc[2] - pa[2]};
                const float n[3] = {u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2],
                                    u[0] * v[1] - u[1] * v[0]};
                for (int idx : {a, b, c})
                    for (int k = 0; k < 3; ++k) gnrm[idx * 3 + k] += n[k];
            };
            for (const mhdata::BaseMesh::Face& f : g->mesh.faces) {
                accumG(f.v[0], f.v[1], f.v[2]);
                if (f.n == 4) accumG(f.v[0], f.v[2], f.v[3]);
            }
            for (int i = 0; i < gv; ++i) {
                float* n = &gnrm[i * 3];
                const float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
                if (len > 1e-8f)
                    for (int k = 0; k < 3; ++k) n[k] /= len;
                else
                    n[1] = 1.0f;
            }

            std::vector<unsigned char> gj((size_t)gv * kMaxInfluences, 0);
            std::vector<unsigned char> gw((size_t)gv * kMaxInfluences, 0);
            for (int i = 0; i < gv; ++i)
                bindingsFor(g->proxy, i, baseWeights, baseVerts, kBoneCount,
                            &gj[(size_t)i * kMaxInfluences], &gw[(size_t)i * kMaxInfluences]);

            std::vector<float> corners;
            std::vector<unsigned char> cj, cw;
            auto pushG = [&](const mhdata::BaseMesh::Face& f, int a, int b, int c) {
                // The body's winding check applies here too: garments follow
                // the same .obj convention, so reuse its verdict.
                int order[3] = {a, b, c};
                if (flip) std::swap(order[1], order[2]);
                for (int idx : order) {
                    const int v = f.v[idx], t = f.t[idx];
                    for (int k = 0; k < 3; ++k) corners.push_back(gpos[v * 3 + k]);
                    for (int k = 0; k < 3; ++k) corners.push_back(gnrm[v * 3 + k]);
                    const bool hasUv = t >= 0 && (size_t)t * 2 + 1 < g->mesh.uv.size();
                    corners.push_back(hasUv ? g->mesh.uv[t * 2] : 0.0f);
                    corners.push_back(hasUv ? 1.0f - g->mesh.uv[t * 2 + 1] : 0.0f);
                    for (int k = 0; k < kMaxInfluences; ++k)
                        cj.push_back(gj[(size_t)v * kMaxInfluences + k]);
                    for (int k = 0; k < kMaxInfluences; ++k)
                        cw.push_back(gw[(size_t)v * kMaxInfluences + k]);
                }
            };
            for (const mhdata::BaseMesh::Face& f : g->mesh.faces) {
                pushG(f, 0, 1, 2);
                if (f.n == 4) pushG(f, 0, 2, 3);
            }
            const size_t slotBudget = (size_t)std::max(64.0f, (float)budget * wear[wi].budget);
            if (wear[wi].cutout) thinCards(corners, cj, cw, slotBudget);
            decimateSkinned(corners, cj, cw, slotBudget);

            glbparser::SkelPart gp;
            // The material name carries the kind: "hair:" is alpha-tested,
            // "cloth:" is solid. Nothing downstream needs it (the PS2 gets its
            // cutout from the texture's own alpha), but the editor preview has
            // to know which part to draw last, and a prefix beats guessing.
            gp.material = (wear[wi].cutout ? "hair:" : "cloth:") + wear[wi].stem;
            gp.vertexCount = (int)corners.size() / 8;
            gp.positions.reserve((size_t)gp.vertexCount * 3);
            gp.normals.reserve((size_t)gp.vertexCount * 3);
            gp.uvs.reserve((size_t)gp.vertexCount * 2);
            for (int v = 0; v < gp.vertexCount; ++v) {
                for (int k = 0; k < 3; ++k) gp.positions.push_back(corners[(size_t)v * 8 + k]);
                for (int k = 0; k < 3; ++k) gp.normals.push_back(corners[(size_t)v * 8 + 3 + k]);
                for (int k = 0; k < 2; ++k) gp.uvs.push_back(corners[(size_t)v * 8 + 6 + k]);
            }
            gp.joints = std::move(cj);
            gp.weights = std::move(cw);

            std::string note;
            const std::vector<unsigned char>* png =
                garmentTexture(wear[wi].sub, wear[wi].stem, texSize, wear[wi].cutout, note);
            if (!note.empty()) warnings.push_back(note);
            if (png) {
                glbparser::Image img;
                img.name = wear[wi].stem;
                img.png = *png;
                gp.image = (int)out.images.size();
                out.images.push_back(std::move(img));
            }
            out.parts.push_back(std::move(gp));
        }
    }

    // --- bounds -------------------------------------------------------------
    // Over every part: hair reaches above the skull and shoes below the sole.
    for (int k = 0; k < 3; ++k) {
        out.min[k] = 1e30f;
        out.max[k] = -1e30f;
    }
    for (const glbparser::SkelPart& built : out.parts)
        for (size_t i = 0; i + 2 < built.positions.size(); i += 3)
            for (int k = 0; k < 3; ++k) {
                out.min[k] = std::min(out.min[k], built.positions[i + k]);
                out.max[k] = std::max(out.max[k], built.positions[i + k]);
            }

    // Last, because both paths scale their translations by the finished body's
    // height - which is only known once the bounds above exist.
    if (p.animations) charanim::addLocomotion(out, p.anim);
    if (!p.animSource.empty()) {
        // An imported library replaces the procedural clips, but only if it
        // actually retargets - a file that turns out not to be a Mixamo-named
        // rig leaves the character with the cycles it already had rather than
        // with nothing.
        DataSet& ds2 = dataSet();
        auto it = ds2.animSources.find(p.animSource);
        if (it == ds2.animSources.end()) {
            glbparser::Skel src;
            std::string err2;
            // A phone take and a Mixamo download are the same thing to the
            // retargeter: a named rig with a bind pose and clips.
            const bool loaded = mocap::isTakePath(p.animSource)
                                    ? mocap::load(p.animSource, src, err2)
                                    : animimport::parseSkel(p.animSource, src, err2);
            for (const std::string& w : src.warnings) warnings.push_back(w);
            if (!loaded) {
                warnings.push_back("animation source: " + err2);
                src = glbparser::Skel();
            }
            it = ds2.animSources.emplace(p.animSource, std::move(src)).first;
        }
        if (!it->second.clips.empty())
            charanim::retarget(it->second, out, p.retarget, warnings);
    }
    return true;
}

bool writeAsset(const std::string& projectDir, const std::string& name,
                const glbparser::Skel& skel, std::string* outRelPath, std::string* outError) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::path(projectDir) / "res" / "models" / "characters";
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) {
        if (outError) *outError = "Could not create " + dir.generic_string();
        return false;
    }
    std::string error;
    if (!gltfwrite::writeGlbFile((dir / (name + ".glb")).string(), skel,
                                 "TyraX Character Generator", error)) {
        if (outError) *outError = error;
        return false;
    }
    if (outRelPath) *outRelPath = "res/models/characters/" + name + ".glb";
    return true;
}

const std::vector<Preset>& presets() {
    static const std::vector<Preset> list = [] {
        std::vector<Preset> out;
        Params p;
        out.push_back({"Default", p});

        Params man;
        man.gender = 1.0f;
        man.age = 0.5f;
        man.muscle = 0.65f;
        man.weight = 0.5f;
        man.caucasian = 1.0f;
        man.african = man.asian = 0.0f;
        man.heightMeters = 1.80f;
        man.name = "man";
        out.push_back({"Young man", man});

        Params woman = man;
        woman.gender = 0.0f;
        woman.muscle = 0.45f;
        woman.heightMeters = 1.67f;
        woman.name = "woman";
        out.push_back({"Young woman", woman});

        Params heavy = man;
        heavy.muscle = 0.35f;
        heavy.weight = 0.95f;
        heavy.age = 0.7f;
        heavy.heightMeters = 1.74f;
        heavy.name = "heavy";
        out.push_back({"Heavy-set", heavy});

        Params kid = man;
        kid.age = 0.22f;
        kid.muscle = 0.4f;
        kid.heightMeters = 1.25f;
        kid.name = "child";
        out.push_back({"Child", kid});

        Params old = woman;
        old.age = 0.95f;
        old.muscle = 0.3f;
        old.heightMeters = 1.60f;
        old.name = "elder";
        out.push_back({"Elder", old});
        return out;
    }();
    return list;
}

}  // namespace chargen
