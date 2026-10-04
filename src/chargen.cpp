#include "chargen.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <unordered_map>

#include <stb_image.h>        // implementation lives in app.cpp
#include <stb_image_write.h>  // implementation lives in menubake.cpp

#include "chargen_kit.hpp"
#include "fbxparser.hpp"  // animimport::parseSkel - .glb and .fbx alike
#include "gltfwrite.hpp"
#include "json.hpp"
#include "mocap.hpp"

namespace chargen {

namespace {

constexpr int kMaxInfluences = 4;  // the VU0 skinning path's fixed budget
constexpr int kLayer = 512;        // every atlas layer in the kit is this square

// ---------------------------------------------------------------------------
// The kit: one embedded binary (tools/chargen-kit/build_kit.py documents the
// layout). Parsed once; images are decoded lazily and kept.

struct Chunk {
    uint8_t type = 0;
    uint32_t count = 0;
    const uint8_t* data = nullptr;
};

struct TargetData {
    std::vector<int32_t> idx;
    std::vector<int16_t> d;   // idx.size() * 3, times `scale`
    float scale = 0.0f;
    std::vector<float> joints;  // bones * 3
};

struct GarmentData {
    Item item;
    std::string kind;              // "shell" (moves body vertices) or "mesh"
    std::vector<int32_t> cover;    // body triangles this item hides
    std::vector<int32_t> inflIdx;  // shell: body vertices pushed out ...
    std::vector<float> inflDist;   // ... by this much along their normal (metres),
    std::vector<float> inflDistM;  // measured on the average woman / man (blended by gender)
    // mesh: own triangles bound to the body surface
    std::vector<int32_t> bindTri;  // body triangle per vertex
    std::vector<float> bindBary;   // 3 per vertex
    std::vector<float> bindOff;    // offset along the interpolated body normal
    std::vector<int32_t> tri;      // 3 per triangle
    std::vector<float> uv;         // 6 per triangle (Blender convention, v up)
    std::vector<uint8_t> joints, weights;  // 4 per vertex
    bool cutout = false;           // alpha-tested texture (hair)
    int layer = 0;                 // stacking order of shells: higher is outer
    float luma = 0.5f;             // mean luminance of its texture (the recolour pivot)
};

struct Kit {
    bool ok = false;
    std::string error;
    std::unordered_map<std::string, Chunk> chunks;

    int verts = 0, tris = 0, bones = 0;
    const float* pos = nullptr;      // verts * 3 (metres, MakeHuman orientation)
    const int32_t* tri = nullptr;    // tris * 3
    const float* uv = nullptr;       // tris * 6
    const uint8_t* part = nullptr;   // tris (0 body, 1 eyes)
    const uint8_t* joints = nullptr; // verts * 4
    const uint8_t* weights = nullptr;
    std::vector<std::string> boneNames;
    const int32_t* parent = nullptr;
    const float* head = nullptr;     // bones * 3

    std::unordered_map<std::string, TargetData> targets;
    struct SliderDef {
        Slider s;
        std::vector<std::string> neg, pos;
    };
    std::vector<SliderDef> sliders;
    std::vector<std::string> brows, lashes;
    std::vector<GarmentData> garments;
    std::vector<Item> wardrobe, hair;

    struct Clip {
        ClipInfo info;
        const int16_t* rot = nullptr;  // frames * bones * 4
        const float* hips = nullptr;   // frames * 3
        int frames = 0;
    };
    std::vector<Clip> clips;
    float animFps = 30.0f, hipsHeight = 1.0f;

    std::mutex imageMutex;
    std::map<std::string, std::shared_ptr<std::vector<uint8_t>>> images;  // RGBA, decoded

    const Chunk* find(const std::string& name) const {
        auto it = chunks.find(name);
        return it == chunks.end() ? nullptr : &it->second;
    }
    template <class T>
    const T* arr(const std::string& name, size_t expect = 0) const {
        const Chunk* c = find(name);
        if (!c || (expect && c->count != expect)) return nullptr;
        return reinterpret_cast<const T*>(c->data);
    }
    bool parseJson(const std::string& name, json::Value& out) const {
        const Chunk* c = find(name);
        if (!c || c->type != 5) return false;
        return json::parse(std::string((const char*)c->data, c->count), out);
    }
};

bool parseKit(Kit& k) {
    const uint8_t* p = chargenkit::data();
    const size_t size = chargenkit::size();
    if (size < 12 || std::memcmp(p, "TXCK", 4) != 0) {
        k.error = "character kit missing from this build (resources/chargen-kit.bin)";
        return false;
    }
    uint32_t version = 0, count = 0;
    std::memcpy(&version, p + 4, 4);
    std::memcpy(&count, p + 8, 4);
    if (version != 1) {
        k.error = "character kit version " + std::to_string(version) + " is not supported";
        return false;
    }
    size_t at = 12;
    static const size_t kElem[] = {4, 4, 1, 2, 1, 1};
    for (uint32_t i = 0; i < count; ++i) {
        if (at + 2 > size) return k.error = "character kit truncated", false;
        uint16_t nameLen = 0;
        std::memcpy(&nameLen, p + at, 2);
        at += 2;
        if (at + nameLen + 6 > size) return k.error = "character kit truncated", false;
        std::string name((const char*)p + at, nameLen);
        at += nameLen;
        Chunk c;
        c.type = p[at];
        std::memcpy(&c.count, p + at + 2, 4);
        at += 6;
        if (c.type > 5) return k.error = "character kit: bad chunk type in " + name, false;
        const size_t bytes = (size_t)c.count * kElem[c.type];
        if (at + bytes > size) return k.error = "character kit truncated in " + name, false;
        c.data = p + at;
        at += bytes;
        at += (4 - (bytes % 4)) % 4;
        k.chunks[name] = c;
    }

    const Chunk* pc = k.find("mesh/pos");
    const Chunk* tc = k.find("mesh/tri");
    if (!pc || !tc) return k.error = "character kit has no body mesh", false;
    k.verts = (int)pc->count / 3;
    k.tris = (int)tc->count / 3;
    k.pos = (const float*)pc->data;
    k.tri = (const int32_t*)tc->data;
    k.uv = k.arr<float>("mesh/uv", (size_t)k.tris * 6);
    k.part = k.arr<uint8_t>("mesh/part", (size_t)k.tris);
    k.joints = k.arr<uint8_t>("mesh/joints", (size_t)k.verts * 4);
    k.weights = k.arr<uint8_t>("mesh/weights", (size_t)k.verts * 4);
    if (!k.uv || !k.part || !k.joints || !k.weights)
        return k.error = "character kit mesh is incomplete", false;
    for (int i = 0; i < k.tris * 3; ++i)
        if (k.tri[i] < 0 || k.tri[i] >= k.verts)
            return k.error = "character kit: triangle index out of range", false;

    json::Value names;
    if (!k.parseJson("rig/names", names) || names.type != json::Value::Type::Array)
        return k.error = "character kit has no rig", false;
    for (const json::Value& n : names.arr) k.boneNames.push_back(n.stringOr(""));
    k.bones = (int)k.boneNames.size();
    k.parent = k.arr<int32_t>("rig/parent", (size_t)k.bones);
    k.head = k.arr<float>("rig/head", (size_t)k.bones * 3);
    if (!k.parent || !k.head) return k.error = "character kit rig is incomplete", false;
    for (int i = 0; i < k.verts * 4; ++i)
        if (k.joints[i] >= k.bones) return k.error = "character kit: bad joint index", false;

    // Targets: every "t/<name>/idx" chunk.
    for (const auto& [name, c] : k.chunks) {
        if (name.size() < 7 || name.compare(0, 2, "t/") != 0 ||
            name.compare(name.size() - 4, 4, "/idx") != 0)
            continue;
        const std::string key = name.substr(2, name.size() - 6);
        TargetData t;
        const int32_t* idx = (const int32_t*)c.data;
        t.idx.assign(idx, idx + c.count);
        const int16_t* d = k.arr<int16_t>("t/" + key + "/d", (size_t)c.count * 3);
        const float* s = k.arr<float>("t/" + key + "/s", 1);
        const float* j = k.arr<float>("t/" + key + "/j", (size_t)k.bones * 3);
        if (!d || !s || !j) continue;
        bool valid = true;
        for (int32_t v : t.idx) valid &= v >= 0 && v < k.verts;
        if (!valid) continue;
        t.d.assign(d, d + c.count * 3);
        t.scale = *s;
        t.joints.assign(j, j + k.bones * 3);
        k.targets.emplace(key, std::move(t));
    }

    json::Value sl;
    if (k.parseJson("sliders", sl))
        for (const json::Value& v : sl.arr) {
            Kit::SliderDef d;
            if (const json::Value* x = v.find("id")) d.s.id = x->stringOr("");
            if (const json::Value* x = v.find("label")) d.s.label = x->stringOr(d.s.id);
            if (const json::Value* x = v.find("group")) d.s.group = x->stringOr("");
            if (const json::Value* x = v.find("neg"))
                for (const json::Value& t : x->arr) d.neg.push_back(t.stringOr(""));
            if (const json::Value* x = v.find("pos"))
                for (const json::Value& t : x->arr) d.pos.push_back(t.stringOr(""));
            k.sliders.push_back(std::move(d));
        }

    json::Value lists;
    if (k.parseJson("lists", lists)) {
        if (const json::Value* b = lists.find("brows"))
            for (const json::Value& v : b->arr) k.brows.push_back(v.stringOr(""));
        if (const json::Value* b = lists.find("lashes"))
            for (const json::Value& v : b->arr) k.lashes.push_back(v.stringOr(""));
    }

    json::Value wr;
    if (k.parseJson("wardrobe", wr))
        for (const json::Value& v : wr.arr) {
            GarmentData g;
            g.item.id = v.find("id") ? v.find("id")->stringOr("") : "";
            g.item.label = v.find("label") ? v.find("label")->stringOr(g.item.id) : g.item.id;
            g.item.slot = v.find("slot") ? v.find("slot")->stringOr("") : "";
            g.item.dyeable = v.find("dyeable") ? v.find("dyeable")->boolOr(true) : true;
            g.item.twoTone = v.find("twoTone") ? v.find("twoTone")->boolOr(false) : false;
            g.item.sex = v.find("sex") ? v.find("sex")->stringOr("") : "";
            g.kind = v.find("kind") ? v.find("kind")->stringOr("shell") : "shell";
            g.cutout = v.find("cutout") ? v.find("cutout")->boolOr(false) : false;
            g.layer = v.find("layer") ? (int)v.find("layer")->numberOr(0) : 0;
            g.luma = v.find("luma") ? std::max(0.02f, (float)v.find("luma")->numberOr(0.5)) : 0.5f;
            if (const json::Value* c = v.find("color"); c && c->arr.size() >= 3)
                g.item.color = Rgb{(float)c->arr[0].numberOr(0.5), (float)c->arr[1].numberOr(0.5),
                                   (float)c->arr[2].numberOr(0.5)};
            const std::string pre = "g/" + g.item.id + "/";
            auto ints = [&](const char* n, std::vector<int32_t>& out) {
                if (const Chunk* c = k.find(pre + n)) {
                    const int32_t* a = (const int32_t*)c->data;
                    out.assign(a, a + c->count);
                }
            };
            auto floats = [&](const char* n, std::vector<float>& out) {
                if (const Chunk* c = k.find(pre + n)) {
                    const float* a = (const float*)c->data;
                    out.assign(a, a + c->count);
                }
            };
            auto bytes = [&](const char* n, std::vector<uint8_t>& out) {
                if (const Chunk* c = k.find(pre + n)) out.assign(c->data, c->data + c->count);
            };
            ints("cover", g.cover);
            ints("inflIdx", g.inflIdx);
            floats("inflDist", g.inflDist);
            floats("inflDistM", g.inflDistM);
            if (g.inflDistM.size() != g.inflDist.size()) g.inflDistM = g.inflDist;
            ints("bindTri", g.bindTri);
            floats("bindBary", g.bindBary);
            floats("bindOff", g.bindOff);
            ints("tri", g.tri);
            floats("uv", g.uv);
            bytes("joints", g.joints);
            bytes("weights", g.weights);
            // Validate everything that indexes something else - a malformed
            // kit must not become a heap read.
            bool valid = g.inflIdx.size() == g.inflDist.size();
            for (int32_t t : g.cover) valid &= t >= 0 && t < k.tris;
            for (int32_t v : g.inflIdx) valid &= v >= 0 && v < k.verts;
            const size_t gv = g.bindTri.size();
            valid &= g.bindBary.size() == gv * 3 && g.bindOff.size() == gv;
            for (int32_t t : g.bindTri) valid &= t >= 0 && t < k.tris;
            for (int32_t i : g.tri) valid &= i >= 0 && (size_t)i < gv;
            valid &= g.uv.size() == g.tri.size() * 2;
            valid &= gv == 0 || (g.joints.size() == gv * 4 && g.weights.size() == gv * 4);
            for (uint8_t j : g.joints) valid &= j < k.bones;
            if (!valid || g.item.id.empty()) continue;
            (g.item.slot == "hair" ? k.hair : k.wardrobe).push_back(g.item);
            k.garments.push_back(std::move(g));
        }

    json::Value an;
    if (k.parseJson("anims", an)) {
        if (const json::Value* f = an.find("fps")) k.animFps = (float)f->numberOr(30.0);
        if (const json::Value* h = an.find("hipsHeight")) k.hipsHeight = (float)h->numberOr(1.0);
        if (const json::Value* cl = an.find("clips"))
            for (const json::Value& v : cl->arr) {
                Kit::Clip c;
                c.info.name = v.find("name") ? v.find("name")->stringOr("") : "";
                c.info.loop = v.find("loop") ? v.find("loop")->boolOr(false) : false;
                c.frames = v.find("frames") ? (int)v.find("frames")->numberOr(0) : 0;
                if (c.frames < 1 || c.info.name.empty()) continue;
                c.rot = k.arr<int16_t>("a/" + c.info.name + "/rot",
                                       (size_t)c.frames * k.bones * 4);
                c.hips = k.arr<float>("a/" + c.info.name + "/hips", (size_t)c.frames * 3);
                if (!c.rot || !c.hips) continue;
                c.info.seconds = (float)(c.frames - 1) / std::max(1.0f, k.animFps);
                k.clips.push_back(c);
            }
    }
    k.ok = true;
    return true;
}

Kit& kit() {
    static Kit k;
    static std::once_flag once;
    std::call_once(once, [] { parseKit(k); });
    return k;
}

// RGBA8 pixels of a kit image, decoded once. nullptr when absent.
std::shared_ptr<std::vector<uint8_t>> image(const std::string& name, int* w = nullptr,
                                            int* h = nullptr) {
    Kit& k = kit();
    std::lock_guard<std::mutex> lock(k.imageMutex);
    auto it = k.images.find(name);
    if (it == k.images.end()) {
        std::shared_ptr<std::vector<uint8_t>> px;
        const Chunk* c = k.find("img/" + name);
        if (c && c->type == 4) {
            int iw = 0, ih = 0, comp = 0;
            unsigned char* d =
                stbi_load_from_memory(c->data, (int)c->count, &iw, &ih, &comp, 4);
            if (d) {
                px = std::make_shared<std::vector<uint8_t>>(d, d + (size_t)iw * ih * 4);
                // Colour layers ship as JPEG (a third of the size) with their
                // coverage beside them as a greyscale PNG: "<name>/a".
                const Chunk* ac = k.find("img/" + name + "/a");
                if (ac && ac->type == 4) {
                    int aw = 0, ah = 0, acomp = 0;
                    unsigned char* a =
                        stbi_load_from_memory(ac->data, (int)ac->count, &aw, &ah, &acomp, 1);
                    if (a && aw == iw && ah == ih)
                        for (size_t i = 0; i < (size_t)iw * ih; ++i) (*px)[i * 4 + 3] = a[i];
                    if (a) stbi_image_free(a);
                }
                px->push_back((uint8_t)(iw & 0xff));  // dimensions ride at the end
                px->push_back((uint8_t)(iw >> 8));
                px->push_back((uint8_t)(ih & 0xff));
                px->push_back((uint8_t)(ih >> 8));
                stbi_image_free(d);
            }
        }
        it = k.images.emplace(name, px).first;
    }
    if (it->second && (w || h)) {
        const std::vector<uint8_t>& v = *it->second;
        const size_t n = v.size();
        if (w) *w = v[n - 4] | (v[n - 3] << 8);
        if (h) *h = v[n - 2] | (v[n - 1] << 8);
    }
    return it->second;
}

// A kit layer as a full kLayer x kLayer RGBA image, or nullptr.
const uint8_t* layer(const std::string& name, std::shared_ptr<std::vector<uint8_t>>& keep) {
    int w = 0, h = 0;
    keep = image(name, &w, &h);
    if (!keep || w != kLayer || h != kLayer) return nullptr;
    return keep->data();
}

// ---------------------------------------------------------------------------
// Macro blending. MakeHuman's macro targets are the CORNERS of the slider
// space; a setting is the product of one factor per axis.

struct Level {
    const char* name;
    float weight;
};

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

std::vector<Level> levels(float v, std::initializer_list<const char*> names,
                          std::initializer_list<float> stops) {
    std::vector<const char*> n(names);
    std::vector<float> s(stops);
    std::vector<Level> out;
    levelPair(v, n.data(), s.data(), (int)n.size(), out);
    return out;
}

void ethnicity(const Params& p, float eth[3]) {
    eth[0] = std::max(p.african, 0.0f);
    eth[1] = std::max(p.asian, 0.0f);
    eth[2] = std::max(p.caucasian, 0.0f);
    const float sum = eth[0] + eth[1] + eth[2];
    if (sum > 0.0f)
        for (int i = 0; i < 3; ++i) eth[i] /= sum;
    else
        eth[0] = eth[1] = 0.0f, eth[2] = 1.0f;
}

// target name -> weight, for the macros and every detail slider.
std::map<std::string, float> targetWeights(const Params& p) {
    std::map<std::string, float> w;
    const auto gender = levels(p.gender, {"female", "male"}, {0.0f, 1.0f});
    const auto age = levels(p.age, {"baby", "child", "young", "old"}, {0.0f, 0.1875f, 0.5f, 1.0f});
    const auto muscle =
        levels(p.muscle, {"minmuscle", "averagemuscle", "maxmuscle"}, {0.0f, 0.5f, 1.0f});
    const auto weight =
        levels(p.weight, {"minweight", "averageweight", "maxweight"}, {0.0f, 0.5f, 1.0f});
    float eth[3];
    ethnicity(p, eth);
    static const char* ethNames[3] = {"african", "asian", "caucasian"};
    for (int e = 0; e < 3; ++e)
        for (const Level& g : gender)
            for (const Level& a : age)
                if (eth[e] * g.weight * a.weight > 0.0f)
                    w[std::string(ethNames[e]) + "-" + g.name + "-" + a.name] +=
                        eth[e] * g.weight * a.weight;
    for (const Level& g : gender)
        for (const Level& a : age)
            for (const Level& m : muscle)
                for (const Level& wt : weight) {
                    const float x = g.weight * a.weight * m.weight * wt.weight;
                    if (x > 0.0f)
                        w[std::string("universal-") + g.name + "-" + a.name + "-" + m.name + "-" +
                          wt.name] += x;
                }
    for (const Kit::SliderDef& s : kit().sliders) {
        auto it = p.shape.find(s.s.id);
        if (it == p.shape.end() || it->second == 0.0f) continue;
        const float v = std::clamp(it->second, -1.0f, 1.0f);
        for (const std::string& t : v < 0 ? s.neg : s.pos) w[t] += std::fabs(v);
    }
    return w;
}

// ---------------------------------------------------------------------------
// Small vector helpers

struct V3 {
    float x = 0, y = 0, z = 0;
};
inline V3 sub(const float* a, const float* b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
inline V3 cross(V3 a, V3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float len(V3 a) { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); }

void vertexNormals(const std::vector<float>& pos, const int32_t* tri, int tris,
                   std::vector<float>& nrm) {
    nrm.assign(pos.size(), 0.0f);
    for (int t = 0; t < tris; ++t) {
        const int a = tri[t * 3], b = tri[t * 3 + 1], c = tri[t * 3 + 2];
        // Unnormalized on purpose: twice the area is the weighting wanted.
        const V3 n = cross(sub(&pos[b * 3], &pos[a * 3]), sub(&pos[c * 3], &pos[a * 3]));
        for (int v : {a, b, c}) {
            nrm[v * 3] += n.x;
            nrm[v * 3 + 1] += n.y;
            nrm[v * 3 + 2] += n.z;
        }
    }
    for (size_t i = 0; i < nrm.size(); i += 3) {
        const float l = std::sqrt(nrm[i] * nrm[i] + nrm[i + 1] * nrm[i + 1] + nrm[i + 2] * nrm[i + 2]);
        if (l > 1e-9f)
            for (int k = 0; k < 3; ++k) nrm[i + k] /= l;
        else
            nrm[i + 1] = 1.0f;
    }
}

// ---------------------------------------------------------------------------
// Texture composition: everything is composed at kLayer in linear-ish float
// and box-filtered down to the requested size at the end.

struct Canvas {
    int size = kLayer;
    std::vector<float> rgb;  // size * size * 3
    Canvas() : rgb((size_t)kLayer * kLayer * 3, 0.0f) {}
};

inline float px01(const uint8_t* img, size_t i, int c) { return img[i * 4 + c] * (1.0f / 255.0f); }

void blendOver(Canvas& cv, const uint8_t* mask, int maskChannel, float amount, Rgb color) {
    if (!mask || amount <= 0.0f) return;
    const size_t n = (size_t)kLayer * kLayer;
    for (size_t i = 0; i < n; ++i) {
        const float a = px01(mask, i, maskChannel) * amount;
        if (a <= 0.0f) continue;
        cv.rgb[i * 3] += (color.r - cv.rgb[i * 3]) * a;
        cv.rgb[i * 3 + 1] += (color.g - cv.rgb[i * 3 + 1]) * a;
        cv.rgb[i * 3 + 2] += (color.b - cv.rgb[i * 3 + 2]) * a;
    }
}

// Multiply-blend: tint the existing colour toward `color` (keeps the detail).
void tintOver(Canvas& cv, const uint8_t* mask, int maskChannel, float amount, Rgb color) {
    if (!mask || amount <= 0.0f) return;
    const size_t n = (size_t)kLayer * kLayer;
    for (size_t i = 0; i < n; ++i) {
        const float a = px01(mask, i, maskChannel) * amount;
        if (a <= 0.0f) continue;
        float* c = &cv.rgb[i * 3];
        const float l = 0.3f * c[0] + 0.59f * c[1] + 0.11f * c[2];
        const float t[3] = {color.r * l * 2.2f, color.g * l * 2.2f, color.b * l * 2.2f};
        for (int k = 0; k < 3; ++k) c[k] += (std::min(t[k], 1.0f) - c[k]) * a;
    }
}

std::vector<uint8_t> encodePng(const std::vector<uint8_t>& rgba, int w, int h) {
    std::vector<uint8_t> out;
    stbi_write_png_to_func(
        [](void* ctx, void* data, int len) {
            auto* v = (std::vector<uint8_t>*)ctx;
            v->insert(v->end(), (uint8_t*)data, (uint8_t*)data + len);
        },
        &out, w, h, 4, rgba.data(), w * 4);
    return out;
}

// Box-filters a kLayer canvas down to `size` (a power of two <= kLayer).
std::vector<uint8_t> downsample(const std::vector<float>& rgb, int size,
                                const std::vector<float>* alpha = nullptr) {
    const int f = kLayer / size;
    std::vector<uint8_t> out((size_t)size * size * 4);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            float s[4] = {0, 0, 0, 0};
            for (int dy = 0; dy < f; ++dy)
                for (int dx = 0; dx < f; ++dx) {
                    const size_t i = (size_t)(y * f + dy) * kLayer + (x * f + dx);
                    for (int c = 0; c < 3; ++c) s[c] += rgb[i * 3 + c];
                    s[3] += alpha ? (*alpha)[i] : 1.0f;
                }
            const float inv = 1.0f / (float)(f * f);
            uint8_t* o = &out[((size_t)y * size + x) * 4];
            for (int c = 0; c < 3; ++c)
                o[c] = (uint8_t)std::clamp((int)std::lround(s[c] * inv * 255.0f), 0, 255);
            o[3] = (uint8_t)std::clamp((int)std::lround(s[3] * inv * 255.0f), 0, 255);
        }
    return out;
}

int potSize(int requested) {
    int s = 64;
    while (s * 2 <= std::clamp(requested, 64, kLayer)) s *= 2;
    return s;
}

const GarmentData* garment(const std::string& id) {
    for (const GarmentData& g : kit().garments)
        if (g.item.id == id) return &g;
    return nullptr;
}

// Recolours one texel: its luminance against the garment's average becomes
// the shading, the dye the hue - so folds, seams and print survive a recolour
// while the colour changes completely. `mask` (0..1) limits it.
void dye(float* c, float mask, float luma, const Rgb& to) {
    if (to.r < 0.0f || mask <= 0.0f) return;
    const float l = (0.299f * c[0] + 0.587f * c[1] + 0.114f * c[2]) / luma;
    const float t[3] = {to.r, to.g, to.b};
    for (int k = 0; k < 3; ++k) c[k] += (std::min(1.0f, l * t[k]) - c[k]) * mask;
}

// Procedural pattern over a garment's primary dye region, in atlas space.
float patternAt(int pattern, int x, int y) {
    switch (pattern) {
        case 1: return ((y / 12) % 2) ? 1.0f : 0.0f;                    // stripes
        case 2: return (((x / 16) + (y / 16)) % 2) ? 1.0f : 0.0f;       // checks
        case 3: {                                                        // plaid
            const bool a = (x % 32) < 6, b = (y % 32) < 6;
            return a && b ? 1.0f : (a || b ? 0.55f : 0.0f);
        }
        case 4: return ((x + y) / 10 % 2) ? 1.0f : 0.0f;                // diagonal
        default: return 0.0f;
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// Public surface

bool Params::operator==(const Params& o) const {
    return gender == o.gender && age == o.age && muscle == o.muscle && weight == o.weight &&
           african == o.african && asian == o.asian && caucasian == o.caucasian &&
           heightMeters == o.heightMeters && shape == o.shape && skinTone == o.skinTone &&
           skinWarmth == o.skinWarmth && aging == o.aging && brows == o.brows &&
           browDensity == o.browDensity && lashes == o.lashes && hairColor == o.hairColor &&
           eyeColor == o.eyeColor && stubble == o.stubble && lipstick == o.lipstick &&
           lipColor == o.lipColor && eyeShadow == o.eyeShadow &&
           eyeShadowColor == o.eyeShadowColor && blush == o.blush &&
           textureSize == o.textureSize && outfit == o.outfit && hair == o.hair &&
           clips == o.clips && defaultClips == o.defaultClips && animFps == o.animFps &&
           animSource == o.animSource && retarget.fps == o.retarget.fps &&
           retarget.inPlace == o.retarget.inPlace &&
           retarget.ground.enabled == o.retarget.ground.enabled && name == o.name;
}

bool kitAvailable() { return kit().ok; }

const std::vector<Slider>& sliders() {
    static const std::vector<Slider> list = [] {
        std::vector<Slider> out;
        for (const Kit::SliderDef& s : kit().sliders) out.push_back(s.s);
        return out;
    }();
    return list;
}

const std::vector<std::string>& browList() { return kit().brows; }
const std::vector<std::string>& lashList() { return kit().lashes; }
const std::vector<Item>& wardrobe() { return kit().wardrobe; }
const std::vector<Item>& hairstyles() { return kit().hair; }
const std::vector<std::string>& patterns() {
    static const std::vector<std::string> p = {"None", "Stripes", "Checks", "Plaid",
                                               "Diagonal"};
    return p;
}

const std::vector<ClipInfo>& kitClips() {
    static const std::vector<ClipInfo> list = [] {
        std::vector<ClipInfo> out;
        for (const Kit::Clip& c : kit().clips) out.push_back(c.info);
        return out;
    }();
    return list;
}

const std::vector<std::pair<std::string, std::string>>& defaultClipSet() {
    // Source clip -> the name it is written under. idle first: a plain Model
    // object autoplays the model's FIRST clip, and a character should idle
    // rather than stand in the bind pose.
    static const std::vector<std::pair<std::string, std::string>> set = {
        {"Idle_Loop", "idle"},       {"Walk_Loop", "walk"},       {"Jog_Fwd_Loop", "run"},
        {"Sprint_Loop", "sprint"},   {"Jump_Loop", "jump"},       {"Jump_Start", "jump_start"},
        {"Jump_Land", "jump_land"},  {"Crouch_Idle_Loop", "crouch"},
        {"Crouch_Fwd_Loop", "crouch_walk"}, {"Interact", "interact"},
    };
    return set;
}

const std::vector<std::string>& boneNames() { return kit().boneNames; }

bool build(const Params& p, glbparser::Skel& out, std::vector<std::string>& warnings,
           std::string& error) {
    Kit& k = kit();
    out = glbparser::Skel();
    if (!k.ok) {
        error = k.error;
        return false;
    }

    // --- morph -----------------------------------------------------------------
    std::vector<float> pos(k.pos, k.pos + (size_t)k.verts * 3);
    std::vector<float> heads(k.head, k.head + (size_t)k.bones * 3);
    for (const auto& [name, w] : targetWeights(p)) {
        auto it = k.targets.find(name);
        if (it == k.targets.end()) {
            warnings.push_back("kit has no target " + name);
            continue;
        }
        const TargetData& t = it->second;
        const float s = t.scale * w;
        for (size_t i = 0; i < t.idx.size(); ++i) {
            float* v = &pos[(size_t)t.idx[i] * 3];
            v[0] += t.d[i * 3] * s;
            v[1] += t.d[i * 3 + 1] * s;
            v[2] += t.d[i * 3 + 2] * s;
        }
        for (int b = 0; b < k.bones * 3; ++b) heads[b] += t.joints[b] * w;
    }

    std::vector<float> nrm;
    vertexNormals(pos, k.tri, k.tris, nrm);

    // --- outfit: shells push body vertices out, items hide what they cover ---------
    // (garment, how it is worn) pairs, inner layers first so an outer shell's
    // push wins and an outer texture paints over an inner one.
    std::vector<std::pair<const GarmentData*, Wear>> wearing;
    {
        std::vector<Wear> asked = p.outfit;
        if (!p.hair.empty()) asked.push_back(Wear{p.hair, p.hairColor, p.hairColor, 0});
        for (const Wear& w : asked) {
            const GarmentData* g = garment(w.id);
            if (!g) {
                warnings.push_back("unknown wardrobe item '" + w.id + "' - skipped");
                continue;
            }
            wearing.push_back({g, w});
        }
        std::stable_sort(wearing.begin(), wearing.end(), [](const auto& a, const auto& b) {
            return a.first->layer < b.first->layer;
        });
    }
    std::vector<const GarmentData*> worn;
    std::vector<Wear> all;
    for (const auto& [g, w] : wearing) {
        worn.push_back(g);
        all.push_back(w);
    }
    std::vector<char> hidden((size_t)k.tris, 0);
    std::vector<float> push((size_t)k.verts, 0.0f);
    for (const GarmentData* g : worn) {
        for (int32_t t : g->cover) hidden[t] = 1;
        const float male = std::clamp(p.gender, 0.0f, 1.0f);
        for (size_t i = 0; i < g->inflIdx.size(); ++i) {
            const float d = g->inflDist[i] + (g->inflDistM[i] - g->inflDist[i]) * male;
            push[g->inflIdx[i]] = std::max(push[g->inflIdx[i]], d);
        }
    }
    // A shell hides nothing - it IS the body, moved. Only mesh items hide.
    for (int v = 0; v < k.verts; ++v)
        if (push[v] != 0.0f)
            for (int c = 0; c < 3; ++c) pos[(size_t)v * 3 + c] += nrm[(size_t)v * 3 + c] * push[v];
    if (!worn.empty()) vertexNormals(pos, k.tri, k.tris, nrm);

    // --- world transform: metres, feet on y = 0, scaled to the asked height -------
    float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
    for (int t = 0; t < k.tris; ++t) {
        if (k.part[t] != 0) continue;
        for (int c = 0; c < 3; ++c) {
            const float* v = &pos[(size_t)k.tri[t * 3 + c] * 3];
            for (int a = 0; a < 3; ++a) {
                lo[a] = std::min(lo[a], v[a]);
                hi[a] = std::max(hi[a], v[a]);
            }
        }
    }
    const float raw = hi[1] - lo[1];
    const float scale = raw > 1e-4f ? std::max(p.heightMeters, 0.05f) / raw : 1.0f;
    const float offset[3] = {-(lo[0] + hi[0]) * 0.5f, -lo[1], -(lo[2] + hi[2]) * 0.5f};
    auto toWorld = [&](float* v) {
        for (int a = 0; a < 3; ++a) v[a] = (v[a] + offset[a]) * scale;
    };
    for (int v = 0; v < k.verts; ++v) toWorld(&pos[(size_t)v * 3]);
    for (int b = 0; b < k.bones; ++b) toWorld(&heads[(size_t)b * 3]);

    // --- rig: identity bind rotations, so the inverse bind is a translation --------
    out.nodes.resize(k.bones);
    out.palette.resize(k.bones);
    for (int b = 0; b < k.bones; ++b) {
        glbparser::SkelNode& n = out.nodes[b];
        n.name = k.boneNames[b];
        n.parent = k.parent[b];
        for (int a = 0; a < 3; ++a)
            n.t[a] = heads[(size_t)b * 3 + a] - (n.parent >= 0 ? heads[(size_t)n.parent * 3 + a] : 0.0f);
        glbparser::SkelJoint& j = out.palette[b];
        j.node = b;
        for (int a = 0; a < 4; ++a) j.ibm[a * 5] = 1.0f;
        for (int a = 0; a < 3; ++a) j.ibm[12 + a] = -heads[(size_t)b * 3 + a];
    }

    // --- the body part ---------------------------------------------------------------
    const int texSize = potSize(p.textureSize);
    glbparser::SkelPart body;
    body.material = "skin";
    for (int t = 0; t < k.tris; ++t) {
        if (hidden[t]) continue;
        for (int c = 0; c < 3; ++c) {
            const int v = k.tri[t * 3 + c];
            for (int a = 0; a < 3; ++a) body.positions.push_back(pos[(size_t)v * 3 + a]);
            for (int a = 0; a < 3; ++a) body.normals.push_back(nrm[(size_t)v * 3 + a]);
            body.uvs.push_back(k.uv[(size_t)t * 6 + c * 2]);
            body.uvs.push_back(1.0f - k.uv[(size_t)t * 6 + c * 2 + 1]);
            for (int i = 0; i < kMaxInfluences; ++i) body.joints.push_back(k.joints[(size_t)v * 4 + i]);
            for (int i = 0; i < kMaxInfluences; ++i) body.weights.push_back(k.weights[(size_t)v * 4 + i]);
            ++body.vertexCount;
        }
    }

    // --- the atlas -------------------------------------------------------------------
    {
        Canvas cv;
        const size_t n = (size_t)kLayer * kLayer;
        float eth[3];
        ethnicity(p, eth);
        static const char* ethNames[3] = {"african", "asian", "caucasian"};
        // Skins exist for young (25), middle age (~45) and old (90); children
        // and babies use the young ones, as MakeHuman does.
        const float ageMid = 0.5f + (45.0f - 25.0f) / (90.0f - 25.0f) * 0.5f;
        const float a = std::clamp(p.age + p.aging * 0.5f, 0.0f, 1.0f);
        const auto ages = levels(a, {"young", "young", "middleage", "old"},
                                 {0.0f, 0.5f, ageMid, 1.0f});
        const auto gender = levels(p.gender, {"female", "male"}, {0.0f, 1.0f});
        float used = 0.0f;
        for (int e = 0; e < 3; ++e)
            for (const Level& g : gender)
                for (const Level& ag : ages) {
                    const float w = eth[e] * g.weight * ag.weight;
                    if (w <= 0.0f) continue;
                    std::shared_ptr<std::vector<uint8_t>> keep;
                    const uint8_t* img = layer(std::string("skin/") + ethNames[e] + "-" + g.name +
                                                   "-" + ag.name,
                                               keep);
                    if (!img) continue;
                    used += w;
                    for (size_t i = 0; i < n; ++i)
                        for (int c = 0; c < 3; ++c) cv.rgb[i * 3 + c] += px01(img, i, c) * w;
                }
        if (used > 0.0f)
            for (float& c : cv.rgb) c /= used;
        else
            warnings.push_back("kit has no skin layers - untextured skin");

        // Tone and warmth, as multipliers: the painted detail survives both.
        {
            const float t = std::clamp(p.skinTone, -1.0f, 1.0f);
            const float wm = std::clamp(p.skinWarmth, -1.0f, 1.0f);
            float m[3] = {1, 1, 1}, add = 0.0f;
            if (t > 0) {
                const float dark[3] = {0.52f, 0.42f, 0.36f};
                for (int c = 0; c < 3; ++c) m[c] = 1.0f + (dark[c] - 1.0f) * t;
            } else {
                const float pale[3] = {1.10f, 1.08f, 1.07f};
                for (int c = 0; c < 3; ++c) m[c] = 1.0f + (pale[c] - 1.0f) * -t;
                add = 0.05f * -t;
            }
            const float warm[3] = {1.04f, 1.0f, 0.86f}, rosy[3] = {1.06f, 0.95f, 0.97f};
            for (int c = 0; c < 3; ++c)
                m[c] *= wm > 0 ? 1.0f + (warm[c] - 1.0f) * wm : 1.0f + (rosy[c] - 1.0f) * -wm;
            std::shared_ptr<std::vector<uint8_t>> keepCls;
            const uint8_t* cls = layer("class", keepCls);
            for (size_t i = 0; i < n; ++i) {
                // Eyeballs keep their colour whatever the skin does.
                if (cls && cls[i * 4 + 1] > 128 && cls[i * 4] < 128 && cls[i * 4 + 2] < 128) continue;
                for (int c = 0; c < 3; ++c)
                    cv.rgb[i * 3 + c] = std::min(1.0f, cv.rgb[i * 3 + c] * m[c] + add);
            }
        }

        std::shared_ptr<std::vector<uint8_t>> keep;
        // Face paint, under the brows and lashes.
        tintOver(cv, layer("mask/lips", keep), 0, p.lipstick * 0.85f, p.lipColor);
        tintOver(cv, layer("mask/eyeshadow", keep), 0, p.eyeShadow * 0.8f, p.eyeShadowColor);
        tintOver(cv, layer("mask/cheeks", keep), 0, p.blush * 0.45f, Rgb{0.85f, 0.35f, 0.35f});
        {
            const Rgb dark{p.hairColor.r * 0.55f, p.hairColor.g * 0.55f, p.hairColor.b * 0.55f};
            blendOver(cv, layer("mask/stubble", keep), 0, p.stubble * 0.7f, dark);
        }
        if (p.brows >= 0 && p.brows < (int)k.brows.size()) {
            const Rgb c{p.hairColor.r * 0.75f, p.hairColor.g * 0.75f, p.hairColor.b * 0.75f};
            blendOver(cv, layer("brow/" + k.brows[p.brows], keep), 0,
                      std::clamp(p.browDensity, 0.0f, 1.5f) * 0.92f, c);
        }
        if (p.lashes >= 0 && p.lashes < (int)k.lashes.size())
            blendOver(cv, layer("lash/" + k.lashes[p.lashes], keep), 0, 0.85f,
                      Rgb{0.04f, 0.03f, 0.03f});

        // Ambient occlusion from the 13k-quad reference body.
        if (const uint8_t* ao = layer("ao", keep))
            for (size_t i = 0; i < n; ++i) {
                const float o = 0.35f + 0.65f * px01(ao, i, 0);
                for (int c = 0; c < 3; ++c) cv.rgb[i * 3 + c] *= o;
            }

        // The eyes: the eyeball layer replaces whatever the skin put there, and
        // the iris (desaturated in the kit) takes the chosen colour.
        {
            std::shared_ptr<std::vector<uint8_t>> keepEye, keepIris;
            const uint8_t* eye = layer("eye", keepEye);
            const uint8_t* iris = layer("iris", keepIris);
            if (eye)
                for (size_t i = 0; i < n; ++i) {
                    const float a = px01(eye, i, 3);
                    if (a <= 0.0f) continue;
                    float c[3] = {px01(eye, i, 0), px01(eye, i, 1), px01(eye, i, 2)};
                    if (iris) {
                        const float m = px01(iris, i, 0);
                        const float col[3] = {p.eyeColor.r, p.eyeColor.g, p.eyeColor.b};
                        for (int q = 0; q < 3; ++q)
                            c[q] += (std::min(1.0f, c[q] * col[q] * 2.4f) - c[q]) * m;
                    }
                    for (int q = 0; q < 3; ++q)
                        cv.rgb[i * 3 + q] += (c[q] - cv.rgb[i * 3 + q]) * a;
                }
        }

        // Paint a mesh item leaves on the body: hair colours the scalp under
        // itself, so the gaps between its strands show hair, not a bald head.
        for (size_t gi = 0; gi < worn.size(); ++gi) {
            const GarmentData* g = worn[gi];
            std::shared_ptr<std::vector<uint8_t>> kb;
            const uint8_t* paint = layer("g/" + g->item.id + "/body", kb);
            if (!paint) continue;
            for (size_t i = 0; i < n; ++i) {
                const float a = px01(paint, i, 3);
                if (a <= 0.0f) continue;
                float c[3] = {px01(paint, i, 0), px01(paint, i, 1), px01(paint, i, 2)};
                if (g->item.dyeable) dye(c, 1.0f, g->luma, all[gi].color);
                for (int q = 0; q < 3; ++q) cv.rgb[i * 3 + q] += (c[q] - cv.rgb[i * 3 + q]) * a;
            }
        }

        // Shell garments are painted straight into the body's atlas: the shell
        // IS the body surface, pushed out.
        for (size_t gi = 0; gi < worn.size(); ++gi) {
            const GarmentData* g = worn[gi];
            if (g->kind != "shell") continue;
            std::shared_ptr<std::vector<uint8_t>> kc;
            const uint8_t* col = layer("g/" + g->item.id, kc);
            if (!col) continue;
            const Wear& w = all[gi];
            for (int y = 0; y < kLayer; ++y)
                for (int x = 0; x < kLayer; ++x) {
                    const size_t i = (size_t)y * kLayer + x;
                    const float a = px01(col, i, 3);
                    if (a <= 0.0f) continue;
                    float c[3] = {px01(col, i, 0), px01(col, i, 1), px01(col, i, 2)};
                    Rgb to = w.color;
                    if (w.pattern > 0) {
                        // A pattern needs a base colour even on an undyed garment.
                        if (to.r < 0.0f) to = g->item.color;
                        const float f = patternAt(w.pattern, x, y);
                        to = Rgb{to.r + (w.color2.r - to.r) * f, to.g + (w.color2.g - to.g) * f,
                                 to.b + (w.color2.b - to.b) * f};
                    }
                    if (g->item.dyeable) dye(c, 1.0f, g->luma, to);
                    for (int q = 0; q < 3; ++q) cv.rgb[i * 3 + q] += (c[q] - cv.rgb[i * 3 + q]) * a;
                }
        }

        glbparser::Image img;
        img.name = "skin";
        img.png = encodePng(downsample(cv.rgb, texSize), texSize, texSize);
        body.image = (int)out.images.size();
        out.images.push_back(std::move(img));
    }
    out.parts.push_back(std::move(body));

    // --- mesh items: their own geometry, riding the body's surface ----------------
    for (size_t gi = 0; gi < worn.size(); ++gi) {
        const GarmentData* g = worn[gi];
        if (g->kind != "mesh" || g->tri.empty()) continue;
        const size_t gv = g->bindTri.size();
        std::vector<float> gp(gv * 3), gn;
        for (size_t v = 0; v < gv; ++v) {
            const int t = g->bindTri[v];
            float s[3] = {0, 0, 0}, nn[3] = {0, 0, 0};
            for (int c = 0; c < 3; ++c) {
                const int bv = k.tri[t * 3 + c];
                const float w = g->bindBary[v * 3 + c];
                for (int a = 0; a < 3; ++a) {
                    s[a] += pos[(size_t)bv * 3 + a] * w;
                    nn[a] += nrm[(size_t)bv * 3 + a] * w;
                }
            }
            const float l = std::sqrt(nn[0] * nn[0] + nn[1] * nn[1] + nn[2] * nn[2]);
            for (int a = 0; a < 3; ++a)
                gp[v * 3 + a] = s[a] + (l > 1e-9f ? nn[a] / l : 0.0f) * g->bindOff[v] * scale;
        }
        vertexNormals(gp, g->tri.data(), (int)g->tri.size() / 3, gn);
        glbparser::SkelPart part;
        part.material = (g->cutout ? "hair:" : "cloth:") + g->item.id;
        const size_t gt = g->tri.size() / 3;
        for (size_t t = 0; t < gt; ++t)
            for (int c = 0; c < 3; ++c) {
                const int v = g->tri[t * 3 + c];
                for (int a = 0; a < 3; ++a) part.positions.push_back(gp[(size_t)v * 3 + a]);
                for (int a = 0; a < 3; ++a) part.normals.push_back(gn[(size_t)v * 3 + a]);
                part.uvs.push_back(g->uv[t * 6 + c * 2]);
                part.uvs.push_back(1.0f - g->uv[t * 6 + c * 2 + 1]);
                for (int i = 0; i < kMaxInfluences; ++i) part.joints.push_back(g->joints[(size_t)v * 4 + i]);
                for (int i = 0; i < kMaxInfluences; ++i) part.weights.push_back(g->weights[(size_t)v * 4 + i]);
                ++part.vertexCount;
            }
        // Its texture: own image, dyed; alpha kept (binary) only for cutouts.
        int w = 0, h = 0;
        std::shared_ptr<std::vector<uint8_t>> col = image("g/" + g->item.id, &w, &h);
        if (col && w > 0 && h > 0) {
            const Wear& choice = all[gi];
            const int outW = std::max(16, std::min(w, texSize)), outH = std::max(16, std::min(h, texSize));
            const int fx = w / outW, fy = h / outH;
            std::vector<uint8_t> rgba((size_t)outW * outH * 4);
            for (int y = 0; y < outH; ++y)
                for (int x = 0; x < outW; ++x) {
                    float s[4] = {0, 0, 0, 0};
                    for (int dy = 0; dy < fy; ++dy)
                        for (int dx = 0; dx < fx; ++dx) {
                            const size_t i = (size_t)(y * fy + dy) * w + (x * fx + dx);
                            float c[3] = {px01(col->data(), i, 0), px01(col->data(), i, 1),
                                          px01(col->data(), i, 2)};
                            if (g->item.dyeable) dye(c, 1.0f, g->luma, choice.color);
                            for (int q = 0; q < 3; ++q) s[q] += c[q];
                            s[3] += px01(col->data(), i, 3);
                        }
                    const float inv = 1.0f / (float)(fx * fy);
                    uint8_t* o = &rgba[((size_t)y * outW + x) * 4];
                    for (int q = 0; q < 3; ++q)
                        o[q] = (uint8_t)std::clamp((int)std::lround(s[q] * inv * 255.0f), 0, 255);
                    // StaPip alpha-tests "pass when alpha != 0": a solid item
                    // must be fully opaque or it punches holes; a cutout is
                    // made BINARY (the CLUT path loses soft gradients).
                    o[3] = g->cutout ? (s[3] * inv >= 0.5f ? 255 : 0) : 255;
                }
            glbparser::Image img;
            img.name = g->item.id;
            img.png = encodePng(rgba, outW, outH);
            part.image = (int)out.images.size();
            out.images.push_back(std::move(img));
        }
        out.parts.push_back(std::move(part));
    }

    // --- bounds --------------------------------------------------------------------
    for (int a = 0; a < 3; ++a) {
        out.min[a] = 1e30f;
        out.max[a] = -1e30f;
    }
    for (const glbparser::SkelPart& sp : out.parts)
        for (size_t i = 0; i + 2 < sp.positions.size(); i += 3)
            for (int a = 0; a < 3; ++a) {
                out.min[a] = std::min(out.min[a], sp.positions[i + a]);
                out.max[a] = std::max(out.max[a], sp.positions[i + a]);
            }

    // --- animation -----------------------------------------------------------------
    if (!p.animSource.empty()) {
        glbparser::Skel src;
        std::string err2;
        const bool loaded = mocap::isTakePath(p.animSource)
                                ? mocap::load(p.animSource, src, err2)
                                : animimport::parseSkel(p.animSource, src, err2);
        for (const std::string& w : src.warnings) warnings.push_back(w);
        if (!loaded)
            warnings.push_back("animation source: " + err2);
        else if (charanim::retarget(src, out, p.retarget, warnings) > 0)
            return true;
        // A file that does not retarget leaves the kit clips in place.
    }
    std::vector<std::pair<std::string, std::string>> want;
    if (p.defaultClips)
        want = defaultClipSet();
    else
        for (const std::string& c : p.clips) {
            std::string as = c;
            for (const auto& [src, dst] : defaultClipSet())
                if (src == c) as = dst;
            want.push_back({c, as});
        }
    const float hipsScale = k.hipsHeight > 1e-4f ? heads[1] / k.hipsHeight : 1.0f;
    const float fps = std::clamp(p.animFps, 4.0f, 60.0f);
    for (const auto& [srcName, dstName] : want) {
        const Kit::Clip* clip = nullptr;
        for (const Kit::Clip& c : k.clips)
            if (c.info.name == srcName) clip = &c;
        if (!clip) {
            if (!k.clips.empty()) warnings.push_back("kit has no clip " + srcName);
            continue;
        }
        glbparser::SkelClip sc;
        sc.name = dstName;
        sc.duration = clip->info.seconds;
        const int keys = std::max(2, (int)std::ceil(sc.duration * fps) + 1);
        std::vector<float> times(keys);
        for (int i = 0; i < keys; ++i) times[i] = std::min(sc.duration, (float)i / fps);
        times.back() = sc.duration;
        auto sampleRot = [&](int bone, float t, float q[4]) {
            const float f = std::clamp(t * k.animFps, 0.0f, (float)(clip->frames - 1));
            const int i0 = (int)std::floor(f), i1 = std::min(i0 + 1, clip->frames - 1);
            const float u = f - (float)i0;
            const int16_t* a = clip->rot + ((size_t)i0 * k.bones + bone) * 4;
            const int16_t* b = clip->rot + ((size_t)i1 * k.bones + bone) * 4;
            float qa[4], qb[4], dot = 0.0f;
            for (int c = 0; c < 4; ++c) {
                qa[c] = a[c] / 32767.0f;
                qb[c] = b[c] / 32767.0f;
                dot += qa[c] * qb[c];
            }
            if (dot < 0)
                for (float& c : qb) c = -c;
            float l = 0.0f;
            for (int c = 0; c < 4; ++c) {
                q[c] = qa[c] + (qb[c] - qa[c]) * u;  // nlerp: keys are dense
                l += q[c] * q[c];
            }
            l = std::sqrt(l);
            for (int c = 0; c < 4; ++c) q[c] = l > 1e-9f ? q[c] / l : (c == 3);
        };
        for (int b = 0; b < k.bones; ++b) {
            glbparser::SkelChannel ch;
            ch.node = b;
            ch.path = 1;
            ch.times = times;
            bool moves = false;
            float prev[4] = {0, 0, 0, 1};
            for (int i = 0; i < keys; ++i) {
                float q[4];
                sampleRot(b, times[i], q);
                // consecutive keys on the same hemisphere: q and -q are the same
                // orientation, and interpolating between them goes the long way
                const float d = q[0] * prev[0] + q[1] * prev[1] + q[2] * prev[2] + q[3] * prev[3];
                if (d < 0)
                    for (float& c : q) c = -c;
                std::memcpy(prev, q, sizeof(prev));
                moves |= std::fabs(q[3]) < 0.99999f;
                ch.values.insert(ch.values.end(), q, q + 4);
            }
            // An all-identity channel is dropped: the EE pays per channel.
            if (moves) sc.channels.push_back(std::move(ch));
        }
        {
            glbparser::SkelChannel ch;
            ch.node = 0;
            ch.path = 0;
            ch.times = times;
            for (int i = 0; i < keys; ++i) {
                const float f = std::clamp(times[i] * k.animFps, 0.0f, (float)(clip->frames - 1));
                const int i0 = (int)std::floor(f), i1 = std::min(i0 + 1, clip->frames - 1);
                const float u = f - (float)i0;
                for (int a = 0; a < 3; ++a) {
                    const float o = clip->hips[i0 * 3 + a] + (clip->hips[i1 * 3 + a] - clip->hips[i0 * 3 + a]) * u;
                    ch.values.push_back(out.nodes[0].t[a] + o * hipsScale);
                }
            }
            sc.channels.push_back(std::move(ch));
        }
        out.clips.push_back(std::move(sc));
    }
    return true;
}

// ---------------------------------------------------------------------------
// JSON

namespace {

std::string num(float v) {
    char b[32];
    std::snprintf(b, sizeof(b), "%.6g", v);
    return b;
}
std::string rgb(const Rgb& c) { return "[" + num(c.r) + "," + num(c.g) + "," + num(c.b) + "]"; }
Rgb rgbOf(const json::Value* v, Rgb fallback) {
    if (!v || v->type != json::Value::Type::Array || v->arr.size() < 3) return fallback;
    return Rgb{(float)v->arr[0].numberOr(fallback.r), (float)v->arr[1].numberOr(fallback.g),
               (float)v->arr[2].numberOr(fallback.b)};
}

}  // namespace

std::string toJson(const Params& p) {
    std::ostringstream o;
    o << "{\n  \"version\": 2,\n  \"name\": \"" << json::escape(p.name) << "\",\n";
    o << "  \"gender\": " << num(p.gender) << ", \"age\": " << num(p.age)
      << ", \"muscle\": " << num(p.muscle) << ", \"weight\": " << num(p.weight) << ",\n";
    o << "  \"african\": " << num(p.african) << ", \"asian\": " << num(p.asian)
      << ", \"caucasian\": " << num(p.caucasian) << ", \"height\": " << num(p.heightMeters)
      << ",\n";
    o << "  \"shape\": {";
    bool first = true;
    for (const auto& [id, v] : p.shape) {
        if (v == 0.0f) continue;
        o << (first ? "" : ", ") << "\"" << json::escape(id) << "\": " << num(v);
        first = false;
    }
    o << "},\n";
    o << "  \"skinTone\": " << num(p.skinTone) << ", \"skinWarmth\": " << num(p.skinWarmth)
      << ", \"aging\": " << num(p.aging) << ",\n";
    o << "  \"brows\": " << p.brows << ", \"browDensity\": " << num(p.browDensity)
      << ", \"lashes\": " << p.lashes << ",\n";
    o << "  \"hairColor\": " << rgb(p.hairColor) << ", \"eyeColor\": " << rgb(p.eyeColor)
      << ", \"stubble\": " << num(p.stubble) << ",\n";
    o << "  \"lipstick\": " << num(p.lipstick) << ", \"lipColor\": " << rgb(p.lipColor)
      << ", \"eyeShadow\": " << num(p.eyeShadow) << ", \"eyeShadowColor\": "
      << rgb(p.eyeShadowColor) << ", \"blush\": " << num(p.blush) << ",\n";
    o << "  \"textureSize\": " << p.textureSize << ",\n";
    o << "  \"hair\": \"" << json::escape(p.hair) << "\",\n  \"outfit\": [";
    for (size_t i = 0; i < p.outfit.size(); ++i) {
        const Wear& w = p.outfit[i];
        o << (i ? ", " : "") << "{\"id\": \"" << json::escape(w.id) << "\", \"color\": "
          << rgb(w.color) << ", \"color2\": " << rgb(w.color2) << ", \"pattern\": " << w.pattern
          << "}";
    }
    o << "],\n  \"defaultClips\": " << (p.defaultClips ? "true" : "false") << ", \"clips\": [";
    for (size_t i = 0; i < p.clips.size(); ++i)
        o << (i ? ", " : "") << "\"" << json::escape(p.clips[i]) << "\"";
    o << "],\n  \"animFps\": " << num(p.animFps) << ",\n  \"animSource\": \""
      << json::escape(p.animSource) << "\"\n}\n";
    return o.str();
}

bool fromJson(const std::string& text, Params& p, std::string& error) {
    json::Value v;
    if (!json::parse(text, v) || v.type != json::Value::Type::Object) {
        error = "not a JSON object";
        return false;
    }
    Params d;
    auto f = [&](const char* key, float& out) {
        if (const json::Value* x = v.find(key)) out = (float)x->numberOr(out);
    };
    auto i = [&](const char* key, int& out) {
        if (const json::Value* x = v.find(key)) out = (int)x->numberOr(out);
    };
    if (const json::Value* x = v.find("name")) d.name = x->stringOr(d.name);
    f("gender", d.gender);
    f("age", d.age);
    f("muscle", d.muscle);
    f("weight", d.weight);
    f("african", d.african);
    f("asian", d.asian);
    f("caucasian", d.caucasian);
    f("height", d.heightMeters);
    if (const json::Value* s = v.find("shape"))
        for (const auto& [id, val] : s->obj) d.shape[id] = (float)val.numberOr(0.0);
    f("skinTone", d.skinTone);
    f("skinWarmth", d.skinWarmth);
    f("aging", d.aging);
    i("brows", d.brows);
    f("browDensity", d.browDensity);
    i("lashes", d.lashes);
    d.hairColor = rgbOf(v.find("hairColor"), d.hairColor);
    d.eyeColor = rgbOf(v.find("eyeColor"), d.eyeColor);
    f("stubble", d.stubble);
    f("lipstick", d.lipstick);
    d.lipColor = rgbOf(v.find("lipColor"), d.lipColor);
    f("eyeShadow", d.eyeShadow);
    d.eyeShadowColor = rgbOf(v.find("eyeShadowColor"), d.eyeShadowColor);
    f("blush", d.blush);
    i("textureSize", d.textureSize);
    if (const json::Value* x = v.find("hair")) d.hair = x->stringOr("");
    if (const json::Value* o = v.find("outfit"))
        for (const json::Value& w : o->arr) {
            Wear we;
            if (const json::Value* x = w.find("id")) we.id = x->stringOr("");
            we.color = rgbOf(w.find("color"), we.color);
            we.color2 = rgbOf(w.find("color2"), we.color2);
            if (const json::Value* x = w.find("pattern")) we.pattern = (int)x->numberOr(0);
            if (!we.id.empty()) d.outfit.push_back(we);
        }
    if (const json::Value* x = v.find("defaultClips")) d.defaultClips = x->boolOr(true);
    if (const json::Value* c = v.find("clips"))
        for (const json::Value& s : c->arr) d.clips.push_back(s.stringOr(""));
    f("animFps", d.animFps);
    if (const json::Value* x = v.find("animSource")) d.animSource = x->stringOr("");
    p = d;
    return true;
}

bool writeAsset(const std::string& projectDir, const std::string& name,
                const glbparser::Skel& skel, const Params& p, std::string* outRelPath,
                std::string* outError) {
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
    // The recipe beside the model: reopening it in the generator rebuilds the
    // same character, and a diff of two of these says what changed.
    Params named = p;
    named.name = name;
    std::ofstream side(dir / (name + ".chargen.json"), std::ios::binary);
    side << toJson(named);
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
        man.muscle = 0.62f;
        man.caucasian = 1.0f;
        man.african = man.asian = 0.0f;
        man.heightMeters = 1.80f;
        man.stubble = 0.35f;
        man.name = "man";
        out.push_back({"Young man", man});

        Params woman = man;
        woman.gender = 0.0f;
        woman.muscle = 0.45f;
        woman.heightMeters = 1.67f;
        woman.stubble = 0.0f;
        woman.lipstick = 0.35f;
        woman.name = "woman";
        out.push_back({"Young woman", woman});

        Params heavy = man;
        heavy.muscle = 0.35f;
        heavy.weight = 1.0f;
        heavy.age = 0.68f;
        heavy.shape["belly"] = 0.7f;
        heavy.shape["doubleChin"] = 0.6f;
        heavy.heightMeters = 1.74f;
        heavy.name = "heavy";
        out.push_back({"Heavy-set", heavy});

        Params brute = man;
        brute.muscle = 1.0f;
        brute.weight = 0.65f;
        brute.shape["vshape"] = 0.8f;
        brute.shape["jaw"] = 0.6f;
        brute.shape["neckWidth"] = 0.7f;
        brute.heightMeters = 1.90f;
        brute.name = "brute";
        out.push_back({"Bruiser", brute});

        Params kid = man;
        kid.age = 0.22f;
        kid.muscle = 0.4f;
        kid.heightMeters = 1.30f;
        kid.stubble = 0.0f;
        kid.name = "child";
        out.push_back({"Child", kid});

        Params old = woman;
        old.age = 0.95f;
        old.muscle = 0.3f;
        old.heightMeters = 1.60f;
        old.hairColor = Rgb{0.78f, 0.77f, 0.75f};
        old.lipstick = 0.0f;
        old.name = "elder";
        out.push_back({"Elder", old});
        return out;
    }();
    return list;
}

Params randomize(unsigned seed, const Params& keep) {
    uint32_t s = seed * 2654435761u + 0x9e3779b9u;
    auto rnd = [&] {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return (s & 0xffffff) / (float)0x1000000;
    };
    auto around = [&](float c, float spread) {  // roughly bell-shaped around c
        return std::clamp(c + (rnd() + rnd() + rnd() - 1.5f) * spread, 0.0f, 1.0f);
    };
    Params p = keep;
    p.gender = rnd() < 0.5f ? around(0.05f, 0.1f) : around(0.95f, 0.1f);
    p.age = around(0.55f, 0.35f);
    p.muscle = around(0.5f, 0.4f);
    p.weight = around(0.5f, 0.45f);
    p.african = rnd();
    p.asian = rnd();
    p.caucasian = rnd();
    // Usually one clear majority, sometimes a real mix.
    const int major = (int)(rnd() * 3) % 3;
    (major == 0 ? p.african : major == 1 ? p.asian : p.caucasian) += 1.5f;
    const bool male = p.gender > 0.5f;
    p.heightMeters = male ? 1.65f + rnd() * 0.3f : 1.52f + rnd() * 0.25f;
    if (p.age < 0.3f) p.heightMeters *= 0.65f + p.age;
    p.shape.clear();
    for (const Slider& sl : sliders())
        if (rnd() < 0.45f) p.shape[sl.id] = (rnd() + rnd() - 1.0f) * 0.7f;
    p.skinTone = (rnd() - 0.5f) * 0.6f;
    p.skinWarmth = (rnd() - 0.5f) * 0.8f;
    p.brows = browList().empty() ? -1 : (int)(rnd() * browList().size()) % (int)browList().size();
    p.browDensity = 0.7f + rnd() * 0.4f;
    static const Rgb hairs[] = {{0.06f, 0.05f, 0.05f}, {0.18f, 0.11f, 0.07f},
                                {0.35f, 0.22f, 0.12f}, {0.62f, 0.45f, 0.25f},
                                {0.85f, 0.70f, 0.45f}, {0.55f, 0.22f, 0.10f},
                                {0.75f, 0.74f, 0.72f}};
    p.hairColor = hairs[(int)(rnd() * 7) % 7];
    if (p.age > 0.85f) p.hairColor = hairs[6];
    static const Rgb eyes[] = {{0.33f, 0.22f, 0.12f}, {0.18f, 0.12f, 0.07f},
                               {0.25f, 0.42f, 0.62f}, {0.30f, 0.45f, 0.30f},
                               {0.45f, 0.45f, 0.42f}, {0.50f, 0.38f, 0.18f}};
    p.eyeColor = eyes[(int)(rnd() * 6) % 6];
    p.stubble = male && p.age > 0.3f ? rnd() * 0.8f : 0.0f;
    p.lipstick = !male && p.age > 0.3f && rnd() < 0.5f ? rnd() * 0.6f : 0.0f;
    p.eyeShadow = !male && rnd() < 0.3f ? rnd() * 0.5f : 0.0f;
    p.blush = rnd() < 0.3f ? rnd() * 0.5f : 0.0f;

    p.outfit.clear();
    const char* sex = male ? "m" : "f";
    auto fits = [&](const Item& it) { return it.sex.empty() || it.sex == sex; };
    auto pick = [&](const std::string& slot) -> const Item* {
        std::vector<const Item*> c;
        for (const Item& it : wardrobe())
            if (it.slot == slot && fits(it)) c.push_back(&it);
        return c.empty() ? nullptr : c[(size_t)(rnd() * c.size()) % c.size()];
    };
    auto color = [&] {
        if (rnd() < 0.5f) return Rgb{-1, -1, -1};  // as made
        static const Rgb pal[] = {{0.85f, 0.85f, 0.82f}, {0.15f, 0.15f, 0.18f},
                                  {0.25f, 0.32f, 0.55f}, {0.55f, 0.18f, 0.16f},
                                  {0.30f, 0.42f, 0.28f}, {0.62f, 0.55f, 0.40f},
                                  {0.40f, 0.40f, 0.42f}, {0.70f, 0.55f, 0.20f},
                                  {0.45f, 0.30f, 0.50f}, {0.20f, 0.45f, 0.55f}};
        return pal[(int)(rnd() * 10) % 10];
    };
    const bool full = rnd() < 0.25f;
    if (const Item* it = full ? pick("full") : nullptr) {
        p.outfit.push_back(Wear{it->id, color(), color(), 0});
    } else {
        if (const Item* it = pick("top")) p.outfit.push_back(Wear{it->id, color(), color(), rnd() < 0.2f ? 1 + (int)(rnd() * 4) % 4 : 0});
        if (const Item* it = pick("bottom")) p.outfit.push_back(Wear{it->id, color(), color(), 0});
    }
    if (const Item* it = pick("feet")) p.outfit.push_back(Wear{it->id, color(), color(), 0});
    if (rnd() < 0.15f)
        if (const Item* it = pick("head")) p.outfit.push_back(Wear{it->id, color(), color(), 0});
    if (rnd() < 0.15f)
        if (const Item* it = pick("face")) p.outfit.push_back(Wear{it->id, color(), color(), 0});
    p.hair.clear();
    if (rnd() < (male ? 0.85f : 0.97f)) {
        std::vector<const Item*> hs;
        for (const Item& it : hairstyles())
            if (fits(it)) hs.push_back(&it);
        if (!hs.empty()) p.hair = hs[(size_t)(rnd() * hs.size()) % hs.size()]->id;
    }
    return p;
}

}  // namespace chargen
