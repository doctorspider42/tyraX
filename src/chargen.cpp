#include "chargen.hpp"

#include <algorithm>
#include <array>
#include <functional>
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
#include "objparser.hpp"  // custom hair from a .obj

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

// What a garment is on ONE body: the vertices a shell pushes out, or how a
// mesh item rides that body's triangles, and what it hides.
struct GarmentBody {
    std::vector<int32_t> cover;    // body triangles this item hides
    std::vector<int32_t> inflIdx;  // shell: body vertices pushed out ...
    std::vector<float> inflDist;   // ... by this much along their normal (metres),
    std::vector<float> inflDistM;  // measured on the average woman / man (blended by gender)
    std::vector<int32_t> bindTri;  // mesh: body triangle per vertex
    std::vector<float> bindBary;   // 3 per vertex
    std::vector<float> bindOff;    // 3 per vertex: along the normal and the tangents u, w (kit_wear tangent_frame)
    std::vector<uint8_t> joints, weights;  // 4 per vertex
};

struct GarmentData {
    Item item;
    std::string kind;              // "shell" (moves body vertices) or "mesh"
    std::vector<int32_t> tri;      // mesh: 3 per triangle (the same mesh on every body)
    std::vector<float> uv;         // 6 per triangle (Blender convention, v up)
    bool cutout = false;           // alpha-tested texture (hair)
    int layer = 0;                 // stacking order: higher is outer
    float luma = 0.5f;             // luminance of its key colour (item.color): the recolour pivot
    std::vector<GarmentBody> body; // one per Kit::bodies entry
    bool offMeters = false;        // bindOff in metres, not body units (an imported custom item)
};

// One game body. Body 0 is MakeHuman's female1605 proxy, body 1 (if present)
// male1591: one topology cannot serve both - the female one's breast loops
// give a man a bust. Every chunk of body N is named with its prefix ("" or
// "m/"), images included ("img/m/skin/...").
struct Body {
    std::string prefix;
    // What it is for (kit "bodies"): detail "crowd" / "standard" / "hero",
    // sex "f" / "m" / "" (either). Params::detail and gender pick the body.
    std::string detail = "standard", sex;
    int verts = 0, tris = 0;
    const float* pos = nullptr;      // verts * 3 (metres, MakeHuman orientation)
    const int32_t* tri = nullptr;    // tris * 3
    const float* uv = nullptr;       // tris * 6
    const uint8_t* part = nullptr;   // tris (0 body, 1 eyes)
    const uint8_t* joints = nullptr; // verts * 4
    const uint8_t* weights = nullptr;
    std::unordered_map<std::string, TargetData> targets;
};

struct Kit {
    bool ok = false;
    std::string error;
    std::unordered_map<std::string, Chunk> chunks;

    std::vector<Body> bodies;
    int bones = 0;
    std::vector<std::string> boneNames;
    const int32_t* parent = nullptr;
    const float* head = nullptr;     // bones * 3

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

bool parseBody(Kit& k, Body& b) {
    const std::string& P = b.prefix;
    const Chunk* pc = k.find(P + "mesh/pos");
    const Chunk* tc = k.find(P + "mesh/tri");
    if (!pc || !tc) return k.error = "character kit has no body mesh '" + P + "'", false;
    b.verts = (int)pc->count / 3;
    b.tris = (int)tc->count / 3;
    b.pos = (const float*)pc->data;
    b.tri = (const int32_t*)tc->data;
    b.uv = k.arr<float>(P + "mesh/uv", (size_t)b.tris * 6);
    b.part = k.arr<uint8_t>(P + "mesh/part", (size_t)b.tris);
    b.joints = k.arr<uint8_t>(P + "mesh/joints", (size_t)b.verts * 4);
    b.weights = k.arr<uint8_t>(P + "mesh/weights", (size_t)b.verts * 4);
    if (!b.uv || !b.part || !b.joints || !b.weights)
        return k.error = "character kit mesh is incomplete", false;
    for (int i = 0; i < b.tris * 3; ++i)
        if (b.tri[i] < 0 || b.tri[i] >= b.verts)
            return k.error = "character kit: triangle index out of range", false;
    for (int i = 0; i < b.verts * 4; ++i)
        if (b.joints[i] >= k.bones) return k.error = "character kit: bad joint index", false;

    // Targets: every "<prefix>t/<name>/idx" chunk.
    const std::string tp = P + "t/";
    for (const auto& [name, c] : k.chunks) {
        if (name.size() < tp.size() + 5 || name.compare(0, tp.size(), tp) != 0 ||
            name.compare(name.size() - 4, 4, "/idx") != 0)
            continue;
        const std::string key = name.substr(tp.size(), name.size() - tp.size() - 4);
        TargetData t;
        const int32_t* idx = (const int32_t*)c.data;
        t.idx.assign(idx, idx + c.count);
        const int16_t* d = k.arr<int16_t>(tp + key + "/d", (size_t)c.count * 3);
        const float* s = k.arr<float>(tp + key + "/s", 1);
        const float* j = k.arr<float>(tp + key + "/j", (size_t)k.bones * 3);
        if (!d || !s || !j) continue;
        bool valid = true;
        for (int32_t v : t.idx) valid &= v >= 0 && v < b.verts;
        if (!valid) continue;
        t.d.assign(d, d + c.count * 3);
        t.scale = *s;
        t.joints.assign(j, j + k.bones * 3);
        b.targets.emplace(key, std::move(t));
    }
    return true;
}

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

    json::Value names;
    if (!k.parseJson("rig/names", names) || names.type != json::Value::Type::Array)
        return k.error = "character kit has no rig", false;
    for (const json::Value& n : names.arr) k.boneNames.push_back(n.stringOr(""));
    k.bones = (int)k.boneNames.size();
    k.parent = k.arr<int32_t>("rig/parent", (size_t)k.bones);
    k.head = k.arr<float>("rig/head", (size_t)k.bones * 3);
    if (!k.parent || !k.head) return k.error = "character kit rig is incomplete", false;

    json::Value bl;
    if (k.parseJson("bodies", bl) && !bl.arr.empty())
        for (const json::Value& v : bl.arr) {
            Body b;
            b.prefix = v.find("prefix") ? v.find("prefix")->stringOr("") : "";
            if (const json::Value* x = v.find("detail")) b.detail = x->stringOr("standard");
            if (const json::Value* x = v.find("sex")) b.sex = x->stringOr("");
            else b.sex = k.bodies.empty() ? "f" : "m";  // a two-body kit: female1605, male1591
            k.bodies.push_back(b);
        }
    else
        k.bodies.push_back(Body{""});  // a single-body kit
    for (Body& b : k.bodies)
        if (!parseBody(k, b)) return false;

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
            auto ints = [&](const std::string& n, std::vector<int32_t>& out) {
                if (const Chunk* c = k.find(n); c && c->type == 1) {
                    const int32_t* a = (const int32_t*)c->data;
                    out.assign(a, a + c->count);
                }
            };
            auto floats = [&](const std::string& n, std::vector<float>& out) {
                if (const Chunk* c = k.find(n); c && c->type == 0) {
                    const float* a = (const float*)c->data;
                    out.assign(a, a + c->count);
                }
            };
            auto bytes = [&](const std::string& n, std::vector<uint8_t>& out) {
                if (const Chunk* c = k.find(n); c && c->type == 2)
                    out.assign(c->data, c->data + c->count);
            };
            ints("g/" + g.item.id + "/tri", g.tri);
            floats("g/" + g.item.id + "/uv", g.uv);
            bool valid = !g.item.id.empty() && g.uv.size() == g.tri.size() * 2;
            for (const Body& b : k.bodies) {
                const std::string pre = b.prefix + "g/" + g.item.id + "/";
                GarmentBody gb;
                ints(pre + "cover", gb.cover);
                ints(pre + "inflIdx", gb.inflIdx);
                floats(pre + "inflDist", gb.inflDist);
                floats(pre + "inflDistM", gb.inflDistM);
                if (gb.inflDistM.size() != gb.inflDist.size()) gb.inflDistM = gb.inflDist;
                ints(pre + "bindTri", gb.bindTri);
                floats(pre + "bindBary", gb.bindBary);
                floats(pre + "bindOff", gb.bindOff);
                bytes(pre + "joints", gb.joints);
                bytes(pre + "weights", gb.weights);
                // Validate everything that indexes something else - a
                // malformed kit must not become a heap read.
                valid &= gb.inflIdx.size() == gb.inflDist.size();
                for (int32_t t : gb.cover) valid &= t >= 0 && t < b.tris;
                for (int32_t vv : gb.inflIdx) valid &= vv >= 0 && vv < b.verts;
                const size_t gv = gb.bindTri.size();
                valid &= gb.bindBary.size() == gv * 3 && gb.bindOff.size() == gv * 3;
                for (int32_t t : gb.bindTri) valid &= t >= 0 && t < b.tris;
                for (int32_t i : g.tri) valid &= i >= 0 && (size_t)i < gv;
                valid &= gv == 0 || (gb.joints.size() == gv * 4 && gb.weights.size() == gv * 4);
                for (uint8_t j : gb.joints) valid &= j < k.bones;
                if (g.kind == "mesh") valid &= gv > 0;
                g.body.push_back(std::move(gb));
            }
            if (!valid) continue;
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
    // The breast macro: women only (the targets are), per age / muscle /
    // weight corner like the universal ones, relative to average cup and
    // firmness - which is the base and has no target.
    {
        const auto cup = levels(p.breastSize, {"mincup", "averagecup", "maxcup"}, {0.0f, 0.5f, 1.0f});
        const auto firm = levels(p.breastFirmness, {"minfirmness", "averagefirmness", "maxfirmness"},
                                 {0.0f, 0.5f, 1.0f});
        const auto bage = levels(p.age, {"child", "child", "young", "old"}, {0.0f, 0.1875f, 0.5f, 1.0f});
        float female = 0.0f;
        for (const Level& g : gender)
            if (std::string(g.name) == "female") female += g.weight;
        for (const Level& a : bage)
            for (const Level& m : muscle)
                for (const Level& wt : weight)
                    for (const Level& c : cup)
                        for (const Level& f : firm) {
                            if (std::string(c.name) == "averagecup" &&
                                std::string(f.name) == "averagefirmness")
                                continue;
                            const float x = female * a.weight * m.weight * wt.weight * c.weight * f.weight;
                            if (x > 0.0f)
                                w[std::string("breast/female-") + a.name + "-" + m.name + "-" +
                                  wt.name + "-" + c.name + "-" + f.name] += x;
                        }
    }
    // Dimorphism: a profile of detail sliders that moves WITH gender, so the
    // man gets a man's jaw and the woman a woman's mouth without anybody
    // touching the face tab. Signed: below gender 0.5 the profile inverts.
    std::map<std::string, float> shape = p.shape;
    {
        static const std::pair<const char*, float> kProfile[] = {
            {"jaw", 0.55f},       {"browRidge", 0.5f},  {"chinWidth", 0.35f},
            {"chinHeight", 0.2f}, {"neckWidth", 0.4f},  {"noseSize", 0.15f},
            {"eyeSize", -0.2f},   {"upperLip", -0.25f}, {"lowerLip", -0.2f},
            {"cheekVolume", -0.2f}, {"shoulders", 0.3f}, {"browHeight", -0.2f},
        };
        const float sex = (std::clamp(p.gender, 0.0f, 1.0f) - 0.5f) * 2.0f;
        // Children: barely any (0 at the baby end, full from young adult on).
        const float adult = std::clamp((p.age - 0.1875f) / (0.5f - 0.1875f), 0.0f, 1.0f);
        const float k = std::clamp(p.dimorphism, 0.0f, 1.5f) * sex * adult;
        if (k != 0.0f)
            for (const auto& [id, v] : kProfile) shape[id] += v * k;
    }
    for (const Kit::SliderDef& s : kit().sliders) {
        auto it = shape.find(s.s.id);
        if (it == shape.end() || it->second == 0.0f) continue;
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

// ---- custom hair (Params::customHair) ----------------------------------------
// A hairstyle the user modelled: a .glb/.obj authored on the generator's
// reference body (exportReferenceBodies) and turned here into a mesh item like
// the kit's own - every vertex bound to the nearest body triangle (point,
// barycentrics, an offset in that triangle's normal/tangent frame), skinned
// from those corners - so it rides every body slider. Built once per file
// version and topology; its texture joins the kit's image cache.
std::string g_assetRoot;

std::string resolveAsset(const std::string& p) {
    namespace fs = std::filesystem;
    if (p.empty()) return p;
    fs::path q(p);
    if (q.is_relative() && !g_assetRoot.empty()) q = fs::path(g_assetRoot) / q;
    return q.string();
}

std::mutex g_customMutex;
std::map<std::string, std::shared_ptr<GarmentData>> g_custom;
int g_customCount = 0;

// The body a character is built on: the kit body tagged with its detail
// level and its sex (gender from 0.5 is a man's), else the standard one.
int bodyFor(const Params& p) {
    const Kit& k = kit();
    const char* want = p.detail <= 0 ? "crowd" : p.detail >= 2 ? "hero" : "standard";
    const std::string sex = p.gender >= 0.5f ? "m" : "f";
    for (int pass = 0; pass < 2; ++pass)
        for (size_t i = 0; i < k.bodies.size(); ++i)
            if (k.bodies[i].detail == (pass ? "standard" : want) &&
                (k.bodies[i].sex.empty() || k.bodies[i].sex == sex))
                return (int)i;
    return 0;
}

// The reference body of kit body `bi` - the one custom hair is modelled on:
// the average person of its sex at 1.75 m, at its detail level.
Params referenceParams(int bi) {
    const Kit& k = kit();
    Params ref;
    const Body& b = k.bodies[(size_t)std::clamp(bi, 0, (int)k.bodies.size() - 1)];
    ref.gender = b.sex == "m" ? 1.0f : 0.0f;
    ref.detail = b.detail == "crowd" ? 0 : b.detail == "hero" ? 2 : 1;
    ref.defaultClips = false;
    ref.clips.clear();
    ref.textureSize = 128;
    ref.name = "reference-" + b.detail + (b.sex.empty() ? "" : b.sex == "m" ? "-male" : "-female");
    return ref;
}

// ...and clothes the same way (Params::customWear): `slot` says what it is.
std::shared_ptr<GarmentData> customItem(const std::string& meshPath, const std::string& texPath,
                                        const std::string& slot, int bi,
                                        std::vector<std::string>& warnings) {
    const std::string what = slot == "hair" ? "custom hair" : "custom " + slot;
    namespace fs = std::filesystem;
    std::error_code ec;
    const std::string mp = resolveAsset(meshPath), tp = resolveAsset(texPath);
    auto stamp = [&](const std::string& f) -> long long {
        if (f.empty()) return 0;
        const auto t = fs::last_write_time(f, ec);
        return ec ? -1 : (long long)t.time_since_epoch().count();
    };
    const std::string key = mp + "|" + tp + "|" + std::to_string(stamp(mp)) + "|" +
                            std::to_string(stamp(tp)) + "|" + std::to_string(bi) + "|" + slot;
    {
        std::lock_guard<std::mutex> lock(g_customMutex);
        auto it = g_custom.find(key);
        if (it != g_custom.end()) return it->second;
    }
    // --- the mesh: a flat corner list, uvs in image space ---
    std::vector<float> P, UV;
    std::vector<uint8_t> rgba;
    int tw = 0, th = 0;
    auto decode = [&](const unsigned char* data, int size) {
        int w = 0, h = 0, comp = 0;
        unsigned char* d = stbi_load_from_memory(data, size, &w, &h, &comp, 4);
        if (!d) return;
        rgba.assign(d, d + (size_t)w * h * 4);
        tw = w, th = h;
        stbi_image_free(d);
    };
    std::string ext = fs::path(mp).extension().string();
    for (char& c : ext) c = (char)std::tolower((unsigned char)c);
    std::string err;
    if (ext == ".glb" || ext == ".gltf") {
        glbparser::Baked bk;
        if (!glbparser::bake(mp, 12.0f, bk, err)) {
            warnings.push_back(what + ": " + err);
            return nullptr;
        }
        for (const glbparser::Part& part : bk.parts) {
            for (int v = 0; v < part.vertexCount; ++v) {
                for (int a = 0; a < 3; ++a) P.push_back(part.positions[(size_t)v * 3 + a]);
                for (int a = 0; a < 2; ++a)
                    UV.push_back(part.uvs.size() > (size_t)v * 2 + a ? part.uvs[(size_t)v * 2 + a] : 0.0f);
            }
            if (rgba.empty() && part.image >= 0 && part.image < (int)bk.images.size())
                decode(bk.images[(size_t)part.image].png.data(),
                       (int)bk.images[(size_t)part.image].png.size());
        }
    } else if (ext == ".obj") {
        objparser::Model m;
        if (!objparser::load(mp, m)) {
            warnings.push_back(what + ": cannot read " + mp);
            return nullptr;
        }
        std::string objTex;
        for (const objparser::Submesh& sm : m.submeshes) {
            for (size_t i = 0; i + 7 < sm.verts.size(); i += 8) {
                for (int a = 0; a < 3; ++a) P.push_back(sm.verts[i + a]);
                UV.push_back(sm.verts[i + 6]);
                UV.push_back(sm.verts[i + 7]);
            }
            if (objTex.empty() && !sm.texture.empty())
                objTex = (fs::path(mp).parent_path() / sm.texture).string();
        }
        if (tp.empty() && !objTex.empty()) {
            std::ifstream f(objTex, std::ios::binary);
            const std::string bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            decode((const unsigned char*)bytes.data(), (int)bytes.size());
        }
    } else {
        warnings.push_back(what + ": a .glb or .obj, please (" + mp + ")");
        return nullptr;
    }
    if (!tp.empty()) {
        std::ifstream f(tp, std::ios::binary);
        const std::string bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        if (bytes.empty()) warnings.push_back(what + ": cannot read the texture " + tp);
        else decode((const unsigned char*)bytes.data(), (int)bytes.size());
    }
    const size_t corners = P.size() / 3;
    if (corners < 3) {
        warnings.push_back(what + ": no triangles in " + mp);
        return nullptr;
    }
    if (rgba.empty()) {  // untextured: a plain mid brown (hair) or mid grey (cloth) to dye
        tw = th = 4;
        rgba.assign(4 * 4 * 4, 0);
        const bool hair = slot == "hair";
        for (size_t i = 0; i < 16; ++i)
            rgba[i * 4] = hair ? 110 : 150, rgba[i * 4 + 1] = hair ? 80 : 150,
            rgba[i * 4 + 2] = hair ? 55 : 150, rgba[i * 4 + 3] = 255;
    }

    // --- the reference body it was modelled on (same topology) ---
    Kit& k = kit();
    if (bi < 0 || bi >= (int)k.bodies.size()) return nullptr;
    const Body& b = k.bodies[(size_t)bi];
    glbparser::Skel rs;
    std::vector<std::string> rw;
    std::string re;
    if (!build(referenceParams(bi), rs, rw, re) || rs.parts.empty() ||
        rs.parts[0].vertexCount != b.tris * 3) {
        warnings.push_back(what + ": no reference body (" + re + ")");
        return nullptr;
    }
    const std::vector<float>& RP = rs.parts[0].positions;  // b.tris * 3 corners, kit order
    const std::vector<float>& RN = rs.parts[0].normals;
    std::vector<float> cen((size_t)b.tris * 3), rad((size_t)b.tris);
    for (int t = 0; t < b.tris; ++t) {
        for (int a = 0; a < 3; ++a)
            cen[(size_t)t * 3 + a] = (RP[(size_t)t * 9 + a] + RP[(size_t)t * 9 + 3 + a] + RP[(size_t)t * 9 + 6 + a]) / 3.0f;
        float r = 0.0f;
        for (int c = 0; c < 3; ++c) {
            float d = 0.0f;
            for (int a = 0; a < 3; ++a) {
                const float x = RP[(size_t)t * 9 + c * 3 + a] - cen[(size_t)t * 3 + a];
                d += x * x;
            }
            r = std::max(r, std::sqrt(d));
        }
        rad[(size_t)t] = r;
    }

    auto g = std::make_shared<GarmentData>();
    g->item.id = "custom" + std::to_string(++g_customCount);
    g->item.label = slot == "hair" ? "Custom hair" : "Custom " + slot;
    g->item.slot = slot;
    // hair keeps the colours the user painted; clothes can take a recolour
    // (Wear::color < 0 = as made), like the kit's
    g->item.dyeable = slot != "hair";
    g->kind = "mesh";
    g->offMeters = true;
    // stacking: shoes and trousers under tops, "over" (a vest, a jacket) on a
    // top, hats and glasses outermost
    g->layer = slot == "hair" ? 50 : slot == "feet" ? 10 : slot == "bottom" ? 20 : slot == "hands" ? 25
             : slot == "top" || slot == "full" ? 30 : slot == "over" ? 40 : slot == "face" ? 55 : 60;
    double avg[3] = {0, 0, 0};
    int opaque = 0;
    for (size_t i = 0; i < (size_t)tw * th; ++i) {
        if (rgba[i * 4 + 3] < 128) {
            g->cutout = true;
            continue;
        }
        for (int a = 0; a < 3; ++a) avg[a] += rgba[i * 4 + a] / 255.0;
        ++opaque;
    }
    if (opaque) g->item.color = Rgb{(float)(avg[0] / opaque), (float)(avg[1] / opaque), (float)(avg[2] / opaque)};
    if (opaque && g->item.dyeable) {
        // The recolour's key colour is the texture's MAIN colour - the most
        // common one (4 bits a channel), averaged within its bin. The plain
        // average of a red-and-white stripe is a pink that is nowhere in it,
        // and the dye mask (near the key colour) then caught nothing.
        std::map<int, std::array<double, 4>> bins;
        for (size_t i = 0; i < (size_t)tw * th; ++i) {
            if (rgba[i * 4 + 3] < 128) continue;
            const int key = (rgba[i * 4] >> 4) << 8 | (rgba[i * 4 + 1] >> 4) << 4 | (rgba[i * 4 + 2] >> 4);
            auto& bn = bins[key];
            for (int a = 0; a < 3; ++a) bn[(size_t)a] += rgba[i * 4 + a] / 255.0;
            bn[3] += 1.0;
        }
        const std::array<double, 4>* top = nullptr;
        for (const auto& kv : bins)
            if (!top || kv.second[3] > (*top)[3]) top = &kv.second;
        if (top && (*top)[3] > 0.0)
            g->item.color = Rgb{(float)((*top)[0] / (*top)[3]), (float)((*top)[1] / (*top)[3]),
                                (float)((*top)[2] / (*top)[3])};
    }
    g->luma = g->item.color.r * 0.3f + g->item.color.g * 0.59f + g->item.color.b * 0.11f;
    // One vertex per position: the file's corners are welded (to 0.01 mm), so
    // the corners of neighbouring triangles share ONE binding. Unwelded, each
    // corner bound to its own nearest body triangle, and on any body but the
    // reference one (or in a pose) coincident corners drifted apart - a
    // collar standing off the neck tore into strips. UVs stay per corner.
    std::vector<float> VP;
    {
        std::map<std::array<long long, 3>, int32_t> at;
        for (size_t c = 0; c < corners; ++c) {
            const std::array<long long, 3> key = {std::llround(P[c * 3] * 1e5), std::llround(P[c * 3 + 1] * 1e5),
                                                  std::llround(P[c * 3 + 2] * 1e5)};
            auto it = at.find(key);
            if (it == at.end()) {
                it = at.emplace(key, (int32_t)(VP.size() / 3)).first;
                VP.insert(VP.end(), &P[c * 3], &P[c * 3] + 3);
            }
            g->tri.push_back(it->second);
        }
    }
    const size_t verts = VP.size() / 3;
    for (size_t v = 0; v < corners; ++v) {  // Blender convention: v up
        g->uv.push_back(UV[v * 2]);
        g->uv.push_back(1.0f - UV[v * 2 + 1]);
    }
    g->body.resize(k.bodies.size());
    GarmentBody& gb = g->body[(size_t)bi];
    for (size_t v = 0; v < verts; ++v) {
        const float* p = &VP[v * 3];
        float best = 1e30f, bw[3] = {1, 0, 0};
        int bt = 0;
        for (int t = 0; t < b.tris; ++t) {
            if (b.part[t] != 0) continue;
            float dc = 0.0f;
            for (int a = 0; a < 3; ++a) {
                const float x = p[a] - cen[(size_t)t * 3 + a];
                dc += x * x;
            }
            const float lim = std::sqrt(best) + rad[(size_t)t];
            if (best < 1e29f && dc > lim * lim) continue;
            // closest point on triangle ABC (Ericson, Real-Time Collision Detection 5.1.5)
            const float* A = &RP[(size_t)t * 9];
            const float* B = A + 3;
            const float* Cc = A + 6;
            float ab[3], ac[3], ap[3];
            for (int a = 0; a < 3; ++a) ab[a] = B[a] - A[a], ac[a] = Cc[a] - A[a], ap[a] = p[a] - A[a];
            auto dot3 = [](const float* x, const float* y) { return x[0] * y[0] + x[1] * y[1] + x[2] * y[2]; };
            const float d1 = dot3(ab, ap), d2 = dot3(ac, ap);
            float w[3];
            if (d1 <= 0 && d2 <= 0) {
                w[0] = 1, w[1] = 0, w[2] = 0;
            } else {
                float bp[3], cp[3];
                for (int a = 0; a < 3; ++a) bp[a] = p[a] - B[a], cp[a] = p[a] - Cc[a];
                const float d3 = dot3(ab, bp), d4 = dot3(ac, bp), d5 = dot3(ab, cp), d6 = dot3(ac, cp);
                const float vc = d1 * d4 - d3 * d2, vb = d5 * d2 - d1 * d6, va = d3 * d6 - d5 * d4;
                if (d3 >= 0 && d4 <= d3) {
                    w[0] = 0, w[1] = 1, w[2] = 0;
                } else if (vc <= 0 && d1 >= 0 && d3 <= 0) {
                    const float s = d1 / (d1 - d3);
                    w[0] = 1 - s, w[1] = s, w[2] = 0;
                } else if (d6 >= 0 && d5 <= d6) {
                    w[0] = 0, w[1] = 0, w[2] = 1;
                } else if (vb <= 0 && d2 >= 0 && d6 <= 0) {
                    const float s = d2 / (d2 - d6);
                    w[0] = 1 - s, w[1] = 0, w[2] = s;
                } else if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) {
                    const float s = (d4 - d3) / ((d4 - d3) + (d5 - d6));
                    w[0] = 0, w[1] = 1 - s, w[2] = s;
                } else {
                    const float den = 1.0f / (va + vb + vc);
                    w[1] = vb * den, w[2] = vc * den, w[0] = 1 - w[1] - w[2];
                }
            }
            float d = 0.0f;
            for (int a = 0; a < 3; ++a) {
                const float x = p[a] - (A[a] * w[0] + B[a] * w[1] + Cc[a] * w[2]);
                d += x * x;
            }
            if (d < best) {
                best = d, bt = t;
                for (int c = 0; c < 3; ++c) bw[c] = w[c];
            }
        }
        gb.bindTri.push_back(bt);
        for (int c = 0; c < 3; ++c) gb.bindBary.push_back(bw[c]);
        // the offset in the triangle's frame: normal (interpolated), then
        // u/w - the bisector and difference of the unit edges (itemPositions)
        const float* A = &RP[(size_t)bt * 9];
        float foot[3], nn[3] = {0, 0, 0};
        for (int a = 0; a < 3; ++a) {
            foot[a] = A[a] * bw[0] + A[3 + a] * bw[1] + A[6 + a] * bw[2];
            for (int c = 0; c < 3; ++c) nn[a] += RN[(size_t)bt * 9 + c * 3 + a] * bw[c];
        }
        float l = std::sqrt(nn[0] * nn[0] + nn[1] * nn[1] + nn[2] * nn[2]);
        for (float& x : nn) x = l > 1e-9f ? x / l : 0.0f;
        float e[2][3];
        for (int kk = 0; kk < 2; ++kk) {
            float el = 0.0f;
            for (int a = 0; a < 3; ++a) {
                e[kk][a] = A[3 + kk * 3 + a] - A[a];
                el += e[kk][a] * e[kk][a];
            }
            el = std::sqrt(el);
            for (int a = 0; a < 3; ++a) e[kk][a] = el > 1e-9f ? e[kk][a] / el : 0.0f;
        }
        float u[3], w[3];
        for (int a = 0; a < 3; ++a) u[a] = e[0][a] + e[1][a], w[a] = e[0][a] - e[1][a];
        for (float* x : {u, w}) {
            const float dn = x[0] * nn[0] + x[1] * nn[1] + x[2] * nn[2];
            float xl = 0.0f;
            for (int a = 0; a < 3; ++a) {
                x[a] -= nn[a] * dn;
                xl += x[a] * x[a];
            }
            xl = std::sqrt(xl);
            for (int a = 0; a < 3; ++a) x[a] = xl > 1e-6f ? x[a] / xl : 0.0f;
        }
        float off[3];
        for (int a = 0; a < 3; ++a) off[a] = p[a] - foot[a];
        gb.bindOff.push_back(off[0] * nn[0] + off[1] * nn[1] + off[2] * nn[2]);
        gb.bindOff.push_back(off[0] * u[0] + off[1] * u[1] + off[2] * u[2]);
        gb.bindOff.push_back(off[0] * w[0] + off[1] * w[1] + off[2] * w[2]);
        // skinned like the skin under it
        std::map<int, float> acc;
        for (int c = 0; c < 3; ++c) {
            const int bv = b.tri[bt * 3 + c];
            for (int i = 0; i < 4; ++i) acc[b.joints[(size_t)bv * 4 + i]] += b.weights[(size_t)bv * 4 + i] * bw[c];
        }
        std::vector<std::pair<float, int>> ws;
        for (const auto& [j, x] : acc) if (x > 0.0f) ws.push_back({x, j});
        std::sort(ws.rbegin(), ws.rend());
        ws.resize(std::min<size_t>(ws.size(), 4));
        float sum = 0.0f;
        for (const auto& x : ws) sum += x.first;
        int q[4] = {0, 0, 0, 0}, total = 0;
        for (size_t i = 0; i < ws.size(); ++i) total += q[i] = (int)std::lround(ws[i].first / std::max(sum, 1e-9f) * 255.0f);
        if (!ws.empty()) q[0] += 255 - total;
        for (int i = 0; i < 4; ++i) {
            gb.joints.push_back(i < (int)ws.size() ? (uint8_t)ws[(size_t)i].second : 0);
            gb.weights.push_back((uint8_t)std::clamp(q[i], 0, 255));
        }
    }
    // its texture, where emitItem looks for a kit item's: "g/<id>"
    {
        auto px = std::make_shared<std::vector<uint8_t>>(rgba);
        px->push_back((uint8_t)(tw & 0xff));
        px->push_back((uint8_t)(tw >> 8));
        px->push_back((uint8_t)(th & 0xff));
        px->push_back((uint8_t)(th >> 8));
        std::lock_guard<std::mutex> lock(k.imageMutex);
        k.images["g/" + g->item.id] = px;
    }
    std::lock_guard<std::mutex> lock(g_customMutex);
    g_custom[key] = g;
    return g;
}

// Recolours one texel: its luminance against the garment's average becomes
// the shading, the dye the hue - so folds, seams and print survive a recolour
// while the colour changes completely. `mask` (0..1) limits it.
// `contrast` < 1 flattens the shading toward the dye: hair textures are
// strands over near-black gaps, and at full contrast a recoloured braid comes
// out in tiger stripes.
// How much of a recolour a texel takes: 1 near the garment's key colour (its
// main fabric), 0 away from it - the white shirt under a trouser suit, a tie,
// stitching, a print. Distance in chromaticity, plus luma only past a factor
// of two (folds and shading stay inside that). build_kit.py's key_colour.
float dyeMask(const float* c, const Rgb& key) {
    if (key.r < 0.0f) return 1.0f;
    const float s = c[0] + c[1] + c[2] + 1e-3f, ks = key.r + key.g + key.b + 1e-3f;
    const float dr = c[0] / s - key.r / ks, dg = c[1] / s - key.g / ks;
    float d = std::sqrt(dr * dr + dg * dg);
    const float l = 0.299f * c[0] + 0.587f * c[1] + 0.114f * c[2];
    const float kl = 0.299f * key.r + 0.587f * key.g + 0.114f * key.b;
    const float dl = std::fabs(std::log2(std::max(l, 1e-3f) / std::max(kl, 1e-3f)));
    d += 0.15f * std::max(dl - 1.0f, 0.0f);
    const float t = std::clamp((0.11f - d) / 0.06f, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

void dye(float* c, float mask, float luma, const Rgb& to, float contrast = 1.0f) {
    if (to.r < 0.0f || mask <= 0.0f) return;
    float l = (0.299f * c[0] + 0.587f * c[1] + 0.114f * c[2]) / luma;
    l = std::min(1.0f + (l - 1.0f) * contrast, 1.8f);
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
           heightMeters == o.heightMeters && dimorphism == o.dimorphism && shape == o.shape &&
           breastSize == o.breastSize && breastFirmness == o.breastFirmness &&
           detail == o.detail &&
           skinTone == o.skinTone &&
           skinWarmth == o.skinWarmth && aging == o.aging && brows == o.brows &&
           browDensity == o.browDensity && lashes == o.lashes && hairColor == o.hairColor &&
           eyeColor == o.eyeColor && stubble == o.stubble && lipstick == o.lipstick &&
           lipColor == o.lipColor && eyeShadow == o.eyeShadow &&
           eyeShadowColor == o.eyeShadowColor && blush == o.blush &&
           textureSize == o.textureSize && outfit == o.outfit && hair == o.hair &&
           options == o.options && bodyChoice == o.bodyChoice && customHair == o.customHair &&
           customHairTexture == o.customHairTexture && customWear == o.customWear &&
           clips == o.clips && defaultClips == o.defaultClips && animFps == o.animFps &&
           motionStyleAuto == o.motionStyleAuto && motionStyle == o.motionStyle &&
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

float motionStyleFor(const Params& p) {
    if (!p.motionStyleAuto) return std::clamp(p.motionStyle, -1.0f, 1.0f);
    const float adult = std::clamp((p.age - 0.19f) / (0.5f - 0.19f), 0.0f, 1.0f);
    return std::clamp((0.5f - p.gender) * 2.0f, -1.0f, 1.0f) * 0.8f * adult;
}

static std::string guessGarmentSlot(std::string n) {
    for (char& c : n) c = (char)std::tolower((unsigned char)c);
    auto has = [&](std::initializer_list<const char*> ws) {
        for (const char* w : ws)
            if (n.find(w) != std::string::npos) return true;
        return false;
    };
    if (has({"vest", "jacket", "coat", "cardigan", "parka", "hoodie", "cape", "poncho"})) return "over";
    if (has({"hat", "cap", "helmet", "beanie", "hood", "crown", "beret"})) return "head";
    if (has({"glasses", "shades", "goggles", "mask", "visor"})) return "face";
    if (has({"boot", "shoe", "sneaker", "sandal", "heel", "slipper"})) return "feet";
    if (has({"glove", "mitten", "gauntlet"})) return "hands";
    if (has({"dress", "gown", "robe", "jumpsuit", "overall", "suit"})) return "full";
    if (has({"pants", "trouser", "jeans", "skirt", "shorts", "legging", "kilt"})) return "bottom";
    return "top";
}

std::vector<CustomGarmentFile> listCustomGarments(const std::string& projectDir) {
    namespace fs = std::filesystem;
    std::vector<CustomGarmentFile> out;
    const fs::path root(projectDir), dir = root / "res" / "models" / "characters" / "custom";
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (!e.is_regular_file()) continue;
        std::string ext = e.path().extension().string();
        for (char& c : ext) c = (char)std::tolower((unsigned char)c);
        if (ext != ".glb" && ext != ".gltf" && ext != ".obj") continue;
        const std::string stem = e.path().stem().string();
        if (stem.rfind("reference-", 0) == 0) continue;  // the bodies to model on
        CustomGarmentFile f;
        f.mesh = fs::relative(e.path(), root, ec).generic_string();
        f.label = stem;
        const fs::path png = e.path().parent_path() / (stem + ".png");
        if (fs::exists(png, ec)) f.texture = fs::relative(png, root, ec).generic_string();
        f.slot = guessGarmentSlot(stem);
        std::ifstream side(e.path().parent_path() / (stem + ".wear.json"), std::ios::binary);
        if (side) {
            std::stringstream ss;
            ss << side.rdbuf();
            json::Value v;
            if (json::parse(ss.str(), v))
                if (const json::Value* s = v.find("slot")) f.slot = s->stringOr(f.slot);
        }
        out.push_back(std::move(f));
    }
    std::sort(out.begin(), out.end(),
              [](const CustomGarmentFile& a, const CustomGarmentFile& b) { return a.label < b.label; });
    return out;
}

void rememberCustomSlot(const std::string& projectDir, const std::string& meshRel,
                        const std::string& slot) {
    namespace fs = std::filesystem;
    const fs::path mesh = fs::path(projectDir) / meshRel;
    std::ofstream side(mesh.parent_path() / (mesh.stem().string() + ".wear.json"), std::ios::binary);
    side << "{ \"slot\": \"" << json::escape(slot) << "\" }\n";
}

Params altParams(const Params& p) {
    Params a = p;
    a.bodyChoice = false;
    a.name = p.name.empty() ? std::string() : p.name + "-alt";
    const bool toWoman = p.gender >= 0.5f;
    a.gender = toWoman ? 0.0f : 1.0f;
    // the same person, the other sex: the average gap in height, a little
    // less (or more) muscle, and what only one of them wore on the skin
    a.heightMeters = toWoman ? p.heightMeters * 0.93f : p.heightMeters / 0.93f;
    a.muscle = toWoman ? p.muscle * 0.75f : std::min(1.0f, p.muscle / 0.75f);
    a.breastSize = a.breastFirmness = 0.5f;
    a.shape.erase("bust");  // a man's chest slider is no woman's
    if (toWoman) {
        a.stubble = 0.0f;
    } else {
        a.lipstick = a.eyeShadow = a.blush = 0.0f;
    }
    a.motionStyleAuto = p.motionStyleAuto;  // auto moves with the new gender
    return a;
}

std::string altModelPath(const std::string& glbPath) {
    namespace fs = std::filesystem;
    const fs::path g(glbPath);
    return (g.parent_path() / (g.stem().string() + "-alt" + g.extension().string())).string();
}

bool writeBodyChoice(const Params& p, const std::string& glbPath, int variants, std::string& error) {
    namespace fs = std::filesystem;
    const std::string alt = altModelPath(glbPath);
    std::error_code ec;
    if (!p.bodyChoice) {  // no Body row: no other body, and no stale one either
        fs::remove(alt, ec);
        return true;
    }
    const Params a = altParams(p);
    glbparser::Skel skel;
    std::vector<std::string> warnings;
    if (!build(a, skel, warnings, error)) return false;
    if (!gltfwrite::writeGlbFile(alt, skel, "TyraX Character Generator", error)) return false;
    for (int k = 1; k <= variants && k < 100; ++k)
        if (!writeVariantTextures(paletteVariant(a, (unsigned)k), alt, k, error)) return false;
    return true;
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
    // Body 0 is the female topology, body 1 the male one: past the middle of
    // the gender slider the male mesh takes over (the targets move both the
    // same way, so the shape is continuous; only the edge loops change).
    const int bi = bodyFor(p);
    const Body& b = k.bodies[bi];
    auto L = [&](const std::string& name, std::shared_ptr<std::vector<uint8_t>>& keep) {
        return layer(b.prefix + name, keep);
    };

    // --- morph -----------------------------------------------------------------
    std::vector<float> pos(b.pos, b.pos + (size_t)b.verts * 3);
    std::vector<float> heads(k.head, k.head + (size_t)k.bones * 3);
    for (const auto& [name, w] : targetWeights(p)) {
        auto it = b.targets.find(name);
        if (it == b.targets.end()) {
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
        for (int j = 0; j < k.bones * 3; ++j) heads[j] += t.joints[j] * w;
    }

    std::vector<float> nrm;
    vertexNormals(pos, b.tri, b.tris, nrm);

    // --- outfit: shells push body vertices out, items hide what they cover ---------
    // (garment, how it is worn) pairs, inner layers first so an outer shell's
    // push wins and an outer texture paints over an inner one.
    std::vector<std::pair<const GarmentData*, Wear>> wearing;
    std::shared_ptr<GarmentData> customKeep;  // a custom hairstyle, alive for this build
    std::vector<std::pair<std::shared_ptr<GarmentData>, Rgb>> customClothes;  // and your own clothes
    {
        std::vector<Wear> asked = p.outfit;
        // A custom hairstyle takes the hair slot from the kit's.
        std::shared_ptr<GarmentData> custom;
        if (!p.customHair.empty()) custom = customItem(p.customHair, p.customHairTexture, "hair", bi, warnings);
        if (custom) customKeep = custom;
        if (!p.hair.empty() && !custom) asked.push_back(Wear{p.hair, p.hairColor, p.hairColor, 0});
        // Your own clothes take their slot from the kit's (a full one top
        // and bottom too, a top or a bottom a full one).
        for (const Params::CustomWear& cw : p.customWear) {
            if (cw.mesh.empty()) continue;
            std::shared_ptr<GarmentData> g = customItem(cw.mesh, cw.texture, cw.slot, bi, warnings);
            if (!g) continue;
            const std::string& s = cw.slot;
            asked.erase(std::remove_if(asked.begin(), asked.end(), [&](const Wear& w) {
                            const GarmentData* kg = garment(w.id);
                            if (!kg) return false;
                            const std::string& o = kg->item.slot;
                            return o == s || (s == "full" && (o == "top" || o == "bottom")) ||
                                   ((s == "top" || s == "bottom") && o == "full");
                        }),
                        asked.end());
            customClothes.push_back({g, cw.color});
        }
        for (const Wear& w : asked) {
            const GarmentData* g = garment(w.id);
            if (!g) {
                warnings.push_back("unknown wardrobe item '" + w.id + "' - skipped");
                continue;
            }
            wearing.push_back({g, w});
        }
        if (customKeep) wearing.push_back({customKeep.get(), Wear{customKeep->item.id, p.hairColor, p.hairColor, 0}});
        for (const auto& [g, color] : customClothes)
            wearing.push_back({g.get(), Wear{g->item.id, color, Rgb{1, 1, 1}, 0}});
        std::stable_sort(wearing.begin(), wearing.end(), [](const auto& x, const auto& y) {
            return x.first->layer < y.first->layer;
        });
    }
    std::vector<const GarmentData*> worn;
    std::vector<Wear> all;
    for (const auto& [g, w] : wearing) {
        worn.push_back(g);
        all.push_back(w);
    }
    // Creator options (Params::options): 1 = an extra choice, 2 = the worn
    // item of a slot that has options - it becomes the default choice. Neither
    // hides body triangles, pushes the body or paints the scalp: the game may
    // take it off.
    std::vector<char> isOpt(worn.size(), 0);
    {
        auto optionSlot = [](const std::string& slot) {
            return slot == "hair" || slot == "head" || slot == "face";
        };
        std::vector<std::string> slots;
        for (const std::string& id : p.options) {
            const GarmentData* g = garment(id);
            if (!g) {
                warnings.push_back("unknown creator option '" + id + "' - skipped");
                continue;
            }
            if (!optionSlot(g->item.slot) || g->kind != "mesh") {
                warnings.push_back("'" + id + "' cannot be a creator option (hair, hats and glasses can)");
                continue;
            }
            bool have = false;
            for (const GarmentData* w : worn) have |= w == g;
            if (!have) {
                worn.push_back(g);
                all.push_back(Wear{id, g->item.slot == "hair" ? p.hairColor : Rgb{-1, -1, -1},
                                   Rgb{1, 1, 1}, 0});
                isOpt.push_back(1);
            }
            slots.push_back(g->item.slot);
        }
        // Optional hats switch the hair too (its pressed twin, see hatCap),
        // so the worn hairstyle must be a part of its own as well.
        if (std::find(slots.begin(), slots.end(), "head") != slots.end()) slots.push_back("hair");
        for (size_t gi = 0; gi < worn.size(); ++gi)
            if (!isOpt[gi] && worn[gi]->kind == "mesh" && std::find(slots.begin(), slots.end(), worn[gi]->item.slot) != slots.end())
                isOpt[gi] = 2;
    }
    std::vector<char> hidden((size_t)b.tris, 0);
    std::vector<float> push((size_t)b.verts, 0.0f);
    for (size_t gi = 0; gi < worn.size(); ++gi) {
        if (isOpt[gi]) continue;
        const GarmentData* g = worn[gi];
        const GarmentBody& gb = g->body[bi];
        for (int32_t t : gb.cover) hidden[t] = 1;
        const float male = std::clamp(p.gender, 0.0f, 1.0f);
        for (size_t i = 0; i < gb.inflIdx.size(); ++i) {
            const float d = gb.inflDist[i] + (gb.inflDistM[i] - gb.inflDist[i]) * male;
            push[gb.inflIdx[i]] = std::max(push[gb.inflIdx[i]], d);
        }
    }
    // A shell hides nothing - it IS the body, moved. Only mesh items hide.
    for (int v = 0; v < b.verts; ++v)
        if (push[v] != 0.0f)
            for (int c = 0; c < 3; ++c) pos[(size_t)v * 3 + c] += nrm[(size_t)v * 3 + c] * push[v];
    if (!worn.empty()) vertexNormals(pos, b.tri, b.tris, nrm);

    // --- world transform: metres, feet on y = 0, scaled to the asked height -------
    float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
    for (int t = 0; t < b.tris; ++t) {
        if (b.part[t] != 0) continue;
        for (int c = 0; c < 3; ++c) {
            const float* v = &pos[(size_t)b.tri[t * 3 + c] * 3];
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
    for (int v = 0; v < b.verts; ++v) toWorld(&pos[(size_t)v * 3]);
    for (int j = 0; j < k.bones; ++j) toWorld(&heads[(size_t)j * 3]);

    // --- rig: identity bind rotations, so the inverse bind is a translation --------
    out.nodes.resize(k.bones);
    out.palette.resize(k.bones);
    for (int j = 0; j < k.bones; ++j) {
        glbparser::SkelNode& n = out.nodes[j];
        n.name = k.boneNames[j];
        n.parent = k.parent[j];
        for (int a = 0; a < 3; ++a)
            n.t[a] = heads[(size_t)j * 3 + a] - (n.parent >= 0 ? heads[(size_t)n.parent * 3 + a] : 0.0f);
        glbparser::SkelJoint& jt = out.palette[j];
        jt.node = j;
        for (int a = 0; a < 4; ++a) jt.ibm[a * 5] = 1.0f;
        for (int a = 0; a < 3; ++a) jt.ibm[12 + a] = -heads[(size_t)j * 3 + a];
    }

    // An item's vertices in the world, riding the morphed body: the bound
    // triangle's point and normal plus the kit's offset (normal, u, w) - and,
    // for hair under a hat, the offset pressed by `cap` (hatCap).
    constexpr float kNoCap = 1e30f;
    // Clothes kept out of the body. An item rides the triangle it was bound
    // to on the average body; where this body grows past that (a larger
    // bust, wider hips, a belly) the cloth between its bound points stayed
    // where it was and the skin came through. Every vertex of a garment is
    // checked against the MORPHED body - the nearest skin vertex's tangent
    // plane - and pushed out to a small gap; the pushes are then smoothed
    // over the garment's own edges so the cloth bulges instead of kinking.
    // Hair and glasses keep their authored fit (a fringe sits on the face).
    constexpr float kCell = 0.04f;
    std::unordered_map<long long, std::vector<int>> skinGrid;
    auto cellKey = [](int x, int y, int z) {
        return ((long long)(x + 4096) << 26) | ((long long)(y + 4096) << 13) | (long long)(z + 4096);
    };
    auto buildSkinGrid = [&]() {
        if (!skinGrid.empty()) return;
        std::vector<char> eye((size_t)b.verts, 0);
        for (int t = 0; t < b.tris; ++t)
            if (b.part[t] != 0)
                for (int c = 0; c < 3; ++c) eye[(size_t)b.tri[t * 3 + c]] = 1;
        for (int v = 0; v < b.verts; ++v) {
            if (eye[(size_t)v]) continue;
            const float* q = &pos[(size_t)v * 3];
            skinGrid[cellKey((int)std::floor(q[0] / kCell), (int)std::floor(q[1] / kCell),
                             (int)std::floor(q[2] / kCell))]
                .push_back(v);
        }
    };
    auto conformItem = [&](const GarmentData* g, std::vector<float>& gp) {
        const std::string& slot = g->item.slot;
        if (slot == "hair" || slot == "face") return;
        buildSkinGrid();
        const float gap = (slot == "feet" || slot == "hands") ? 0.003f : 0.005f;
        const size_t gv = gp.size() / 3;
        std::vector<float> req(gv * 3, 0.0f), dir(gv * 3, 0.0f), gn;
        vertexNormals(gp, g->tri.data(), (int)g->tri.size() / 3, gn);
        bool any = false;
        for (size_t v = 0; v < gv; ++v) {
            const float* q = &gp[v * 3];
            const int cx = (int)std::floor(q[0] / kCell), cy = (int)std::floor(q[1] / kCell),
                      cz = (int)std::floor(q[2] / kCell);
            int best = -1;
            float bd = 0.10f * 0.10f;  // a woman's bust under a man's garment is that deep
            for (int dx = -2; dx <= 2; ++dx)
                for (int dy = -2; dy <= 2; ++dy)
                    for (int dz = -2; dz <= 2; ++dz) {
                        auto it = skinGrid.find(cellKey(cx + dx, cy + dy, cz + dz));
                        if (it == skinGrid.end()) continue;
                        for (int sv : it->second) {
                            const float* s = &pos[(size_t)sv * 3];
                            const float d = (q[0] - s[0]) * (q[0] - s[0]) + (q[1] - s[1]) * (q[1] - s[1]) +
                                            (q[2] - s[2]) * (q[2] - s[2]);
                            if (d < bd) bd = d, best = sv;
                        }
                    }
            if (best < 0) continue;
            const float* s = &pos[(size_t)best * 3];
            const float* n = &nrm[(size_t)best * 3];
            const float d = (q[0] - s[0]) * n[0] + (q[1] - s[1]) * n[1] + (q[2] - s[2]) * n[2];
            if (d >= gap) continue;
            // Deeper than 4 cm it may be another surface's business (between
            // the legs the nearest skin is the OTHER leg) - unless cloth and
            // skin face the same way: then it is this skin, grown past the
            // garment (a woman's bust under a vest modelled on a man).
            if (d < -0.04f) {
                const float* cn = &gn[v * 3];
                if (d < -0.12f || cn[0] * n[0] + cn[1] * n[1] + cn[2] * n[2] < 0.6f) continue;
            }
            for (int a = 0; a < 3; ++a) {
                req[v * 3 + a] = n[a] * (gap - d);
                dir[v * 3 + a] = n[a];
            }
            any = true;
        }
        if (!any) return;
        // smooth over the garment's edges (twice), never below what a vertex needs
        std::vector<std::vector<int>> nb(gv);
        for (size_t t = 0; t + 2 < g->tri.size(); t += 3)
            for (int c = 0; c < 3; ++c) {
                const int a = g->tri[t + c], o = g->tri[t + (c + 1) % 3];
                if (a < 0 || o < 0 || (size_t)a >= gv || (size_t)o >= gv) continue;
                nb[(size_t)a].push_back(o);
                nb[(size_t)o].push_back(a);
            }
        std::vector<float> cur(req), nxt(gv * 3);
        for (int it = 0; it < 2; ++it) {
            for (size_t v = 0; v < gv; ++v) {
                float s[3] = {cur[v * 3], cur[v * 3 + 1], cur[v * 3 + 2]};
                for (int o : nb[v])
                    for (int a = 0; a < 3; ++a) s[a] += cur[(size_t)o * 3 + a];
                const float inv = 1.0f / (float)(nb[v].size() + 1);
                for (int a = 0; a < 3; ++a) nxt[v * 3 + a] = s[a] * inv;
                // at least the required push along its own direction
                const float need = std::sqrt(req[v * 3] * req[v * 3] + req[v * 3 + 1] * req[v * 3 + 1] +
                                             req[v * 3 + 2] * req[v * 3 + 2]);
                if (need > 0.0f) {
                    const float* dv = &dir[v * 3];
                    const float have = nxt[v * 3] * dv[0] + nxt[v * 3 + 1] * dv[1] + nxt[v * 3 + 2] * dv[2];
                    if (have < need)
                        for (int a = 0; a < 3; ++a) nxt[v * 3 + a] += dv[a] * (need - have);
                }
            }
            cur.swap(nxt);
        }
        for (size_t i = 0; i < gp.size(); ++i) gp[i] += cur[i];
    };
    auto itemPositions = [&](const GarmentData* g, const std::vector<float>* cap) {
        const GarmentBody& gb = g->body[bi];
        const float offScale = g->offMeters ? 1.0f : scale;  // a custom item's offsets are metres
        // Geometry, riding the body.
        const size_t gv = gb.bindTri.size();
        std::vector<float> gp(gv * 3);
        for (size_t v = 0; v < gv; ++v) {
            const int t = gb.bindTri[v];
            float s[3] = {0, 0, 0}, nn[3] = {0, 0, 0};
            for (int c = 0; c < 3; ++c) {
                const int bv = b.tri[t * 3 + c];
                const float w = gb.bindBary[v * 3 + c];
                for (int a = 0; a < 3; ++a) {
                    s[a] += pos[(size_t)bv * 3 + a] * w;
                    nn[a] += nrm[(size_t)bv * 3 + a] * w;
                }
            }
            const float l = std::sqrt(nn[0] * nn[0] + nn[1] * nn[1] + nn[2] * nn[2]);
            for (int a = 0; a < 3; ++a) nn[a] = l > 1e-9f ? nn[a] / l : 0.0f;
            // The offset is (normal, u, w) of the bound triangle - u and w
            // are kit_wear.py's tangent_frame: the bisector and difference
            // of the unit edges 0->1 and 0->2, projected off the normal. A
            // skirt's hem hangs far below the hip triangle it rides; every
            // other item has u = w = 0.
            const float* off = &gb.bindOff[v * 3];
            float u[3] = {0, 0, 0}, w[3] = {0, 0, 0};
            if (off[1] != 0.0f || off[2] != 0.0f) {
                float e[2][3];
                for (int k = 0; k < 2; ++k) {
                    const float* p0 = &pos[(size_t)b.tri[t * 3] * 3];
                    const float* pk = &pos[(size_t)b.tri[t * 3 + 1 + k] * 3];
                    float el = 0;
                    for (int a = 0; a < 3; ++a) {
                        e[k][a] = pk[a] - p0[a];
                        el += e[k][a] * e[k][a];
                    }
                    el = std::sqrt(el);
                    for (int a = 0; a < 3; ++a) e[k][a] = el > 1e-9f ? e[k][a] / el : 0.0f;
                }
                for (int a = 0; a < 3; ++a) {
                    u[a] = e[0][a] + e[1][a];
                    w[a] = e[0][a] - e[1][a];
                }
                for (float* x : {u, w}) {
                    const float dn = x[0] * nn[0] + x[1] * nn[1] + x[2] * nn[2];
                    float xl = 0;
                    for (int a = 0; a < 3; ++a) {
                        x[a] -= nn[a] * dn;
                        xl += x[a] * x[a];
                    }
                    xl = std::sqrt(xl);
                    for (int a = 0; a < 3; ++a) x[a] = xl > 1e-6f ? x[a] / xl : 0.0f;
                }
            }
            float lift = off[0];
            if (cap) {
                // the hats' lowest surface over this vertex, from the corners
                // they cover; partial cover feathers the press out at the edge
                float lim = 0.0f, covered = 0.0f;
                for (int c = 0; c < 3; ++c) {
                    const float m = (*cap)[(size_t)b.tri[t * 3 + c]];
                    if (m >= kNoCap) continue;
                    const float bw = gb.bindBary[v * 3 + c];
                    lim += m * bw;
                    covered += bw;
                }
                if (covered > 0.0f) {
                    // the hats' limit is in body units; a custom item's lift in metres
                    const float pressed = std::min(lift, 0.7f * lim / covered * (g->offMeters ? scale : 1.0f));
                    lift += (pressed - lift) * std::min(1.0f, covered * 1.5f);
                }
            }
            for (int a = 0; a < 3; ++a)
                gp[v * 3 + a] = s[a] + (nn[a] * lift + u[a] * off[1] + w[a] * off[2]) * offScale;
        }
        conformItem(g, gp);
        return gp;
    };

    // Legs under a skirt or a dress: the kit hides only the body triangles
    // inside the garment's stand-in, so a thigh that grazes the cloth in the
    // rest pose comes straight through it once the leg moves - the "slits" in
    // every skirt. Hide every pelvis/leg-skinned triangle that is INSIDE the
    // cloth: per 5 cm band and 22.5-degree sector around the garment's axis,
    // the cloth's radius there; a vertex 1 cm or more inside it is covered.
    // Where a sector has no cloth (a real slit, a cut-out) the leg stays.
    {
        std::vector<char> legBone((size_t)k.bones, 0);
        for (int j = 0; j < k.bones; ++j) {
            const std::string& nm = k.boneNames[(size_t)j];
            legBone[(size_t)j] = nm == "mixamorig:Hips" || nm.find("UpLeg") != std::string::npos ||
                                 (nm.find("Leg") != std::string::npos && nm.find("UpLeg") == std::string::npos);
        }
        auto legWeight = [&](int v) {
            float w = 0.0f, sum = 0.0f;
            for (int i = 0; i < 4; ++i) {
                const float x = b.weights[(size_t)v * 4 + i];
                sum += x;
                const int j = b.joints[(size_t)v * 4 + i];
                if (j >= 0 && j < k.bones && legBone[(size_t)j]) w += x;
            }
            return sum > 0.0f ? w / sum : 0.0f;
        };
        constexpr float kBand = 0.05f;
        constexpr int kSectors = 16;
        for (size_t gi = 0; gi < worn.size(); ++gi) {
            const GarmentData* g = worn[gi];
            if (isOpt[gi] || g->kind != "mesh" || g->tri.empty() ||
                (g->item.slot != "full" && g->item.slot != "bottom"))
                continue;
            const std::vector<float> gp = itemPositions(g, nullptr);
            const size_t gv = gp.size() / 3;
            if (gv < 16) continue;
            float ylo = 1e30f, yhi = -1e30f;
            for (size_t v = 0; v < gv; ++v) {
                ylo = std::min(ylo, gp[v * 3 + 1]);
                yhi = std::max(yhi, gp[v * 3 + 1]);
            }
            const int bands = std::max(1, (int)std::ceil((yhi - ylo) / kBand));
            std::vector<float> cx((size_t)bands, 0.0f), cz((size_t)bands, 0.0f), cn((size_t)bands, 0.0f);
            auto bandOf = [&](float y) { return std::clamp((int)((y - ylo) / kBand), 0, bands - 1); };
            for (size_t v = 0; v < gv; ++v) {
                const int bd = bandOf(gp[v * 3 + 1]);
                cx[bd] += gp[v * 3];
                cz[bd] += gp[v * 3 + 2];
                cn[bd] += 1.0f;
            }
            for (int bd = 0; bd < bands; ++bd)
                if (cn[bd] > 0.0f) cx[bd] /= cn[bd], cz[bd] /= cn[bd];
            std::vector<float> rmax((size_t)bands * kSectors, 0.0f);
            auto sectorOf = [&](float dx, float dz) {
                const float a = std::atan2(dz, dx) + 3.14159265f;
                return std::clamp((int)(a / (2.0f * 3.14159265f) * kSectors), 0, kSectors - 1);
            };
            for (size_t v = 0; v < gv; ++v) {
                const int bd = bandOf(gp[v * 3 + 1]);
                const float dx = gp[v * 3] - cx[bd], dz = gp[v * 3 + 2] - cz[bd];
                float& r = rmax[(size_t)bd * kSectors + sectorOf(dx, dz)];
                r = std::max(r, std::sqrt(dx * dx + dz * dz));
            }
            auto inside = [&](int v) {
                const float* p = &pos[(size_t)v * 3];
                if (p[1] < ylo + kBand || p[1] > yhi) return false;  // the hem's band shows legs
                if (legWeight(v) < 0.6f) return false;
                const int bd = bandOf(p[1]);
                if (cn[bd] <= 0.0f) return false;
                const float dx = p[0] - cx[bd], dz = p[2] - cz[bd];
                const int sc = sectorOf(dx, dz);
                // the cloth's reach here: this sector or a neighbour (the
                // mesh is sparse); three empty sectors in a row are a slit
                float rr = 0.0f;
                for (int d = -1; d <= 1; ++d)
                    rr = std::max(rr, rmax[(size_t)bd * kSectors + ((sc + d + kSectors) % kSectors)]);
                return rr > 0.0f && std::sqrt(dx * dx + dz * dz) < rr - 0.01f;
            };
            std::vector<signed char> in((size_t)b.verts, -1);
            for (int t = 0; t < b.tris; ++t) {
                if (hidden[t] || b.part[t] != 0) continue;
                bool all = true;
                for (int c = 0; c < 3 && all; ++c) {
                    const int v = b.tri[t * 3 + c];
                    if (in[(size_t)v] < 0) in[(size_t)v] = inside(v) ? 1 : 0;
                    all = in[(size_t)v] == 1;
                }
                if (all) hidden[t] = 1;
            }
        }
    }

    // Skin under cloth is not drawn. The kit hides only what each item's
    // stand-in covered on the average body; anything else under a top, a
    // dress, trousers or shoes was still there to poke through when a slider
    // or a pose moved it. A body vertex is covered when a ray out along its
    // normal meets the garment within 4 cm on an opaque texel (lace and
    // cut-outs keep their skin); a triangle with all three corners covered
    // is dropped - which also saves its triangles.
    {
        for (size_t gi = 0; gi < worn.size(); ++gi) {
            const GarmentData* g = worn[gi];
            const std::string& slot = g->item.slot;
            if (isOpt[gi] || g->kind != "mesh" || g->tri.empty() ||
                (slot != "top" && slot != "bottom" && slot != "full" && slot != "feet" && slot != "hands" && slot != "over"))
                continue;
            const std::vector<float> gp = itemPositions(g, nullptr);
            const size_t gt = g->tri.size() / 3;
            int tw = 0, th = 0;
            std::shared_ptr<std::vector<uint8_t>> tex;
            if (g->cutout) tex = image("g/" + g->item.id, &tw, &th);
            float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
            for (size_t i = 0; i < gp.size(); ++i) {
                lo[i % 3] = std::min(lo[i % 3], gp[i]);
                hi[i % 3] = std::max(hi[i % 3], gp[i]);
            }
            // per-triangle bounds for a cheap reject
            std::vector<float> tb(gt * 6);
            for (size_t t = 0; t < gt; ++t)
                for (int a = 0; a < 3; ++a) {
                    float mn = 1e30f, mx = -1e30f;
                    for (int c = 0; c < 3; ++c) {
                        const float x = gp[(size_t)g->tri[t * 3 + c] * 3 + a];
                        mn = std::min(mn, x), mx = std::max(mx, x);
                    }
                    tb[t * 6 + a] = mn, tb[t * 6 + 3 + a] = mx;
                }
            constexpr float kReach = 0.04f, kBehind = 0.03f;
            auto covered = [&](int v) {
                const float* o = &pos[(size_t)v * 3];
                const float* n = &nrm[(size_t)v * 3];
                for (int a = 0; a < 3; ++a)
                    if (o[a] < lo[a] - kReach || o[a] > hi[a] + kReach) return false;
                float sl[3], sh[3];  // the ray segment's bounds
                for (int a = 0; a < 3; ++a) {
                    const float b0 = o[a] - n[a] * kBehind, e = o[a] + n[a] * kReach;
                    sl[a] = std::min(b0, e), sh[a] = std::max(b0, e);
                }
                for (size_t t = 0; t < gt; ++t) {
                    bool off = false;
                    for (int a = 0; a < 3 && !off; ++a) off = tb[t * 6 + 3 + a] < sl[a] || tb[t * 6 + a] > sh[a];
                    if (off) continue;
                    // Moller-Trumbore
                    const float* A = &gp[(size_t)g->tri[t * 3] * 3];
                    const float* B = &gp[(size_t)g->tri[t * 3 + 1] * 3];
                    const float* C = &gp[(size_t)g->tri[t * 3 + 2] * 3];
                    float e1[3], e2[3], pv[3], tv[3], qv[3];
                    for (int a = 0; a < 3; ++a) e1[a] = B[a] - A[a], e2[a] = C[a] - A[a];
                    pv[0] = n[1] * e2[2] - n[2] * e2[1];
                    pv[1] = n[2] * e2[0] - n[0] * e2[2];
                    pv[2] = n[0] * e2[1] - n[1] * e2[0];
                    const float det = e1[0] * pv[0] + e1[1] * pv[1] + e1[2] * pv[2];
                    if (std::fabs(det) < 1e-12f) continue;
                    const float inv = 1.0f / det;
                    for (int a = 0; a < 3; ++a) tv[a] = o[a] - A[a];
                    const float uu = (tv[0] * pv[0] + tv[1] * pv[1] + tv[2] * pv[2]) * inv;
                    if (uu < 0.0f || uu > 1.0f) continue;
                    qv[0] = tv[1] * e1[2] - tv[2] * e1[1];
                    qv[1] = tv[2] * e1[0] - tv[0] * e1[2];
                    qv[2] = tv[0] * e1[1] - tv[1] * e1[0];
                    const float vv = (n[0] * qv[0] + n[1] * qv[1] + n[2] * qv[2]) * inv;
                    if (vv < 0.0f || uu + vv > 1.0f) continue;
                    const float dist = (e2[0] * qv[0] + e2[1] * qv[1] + e2[2] * qv[2]) * inv;
                    // behind the skin too: skin poking THROUGH the cloth (a
                    // bust tip between a coarse garment's vertices) is covered
                    if (dist < -kBehind || dist > kReach) continue;
                    if (tex && tw > 0 && th > 0) {  // a cut-out: only where the texel is solid
                        const float w0 = 1.0f - uu - vv;
                        const float su = g->uv[t * 6] * w0 + g->uv[t * 6 + 2] * uu + g->uv[t * 6 + 4] * vv;
                        const float sv = g->uv[t * 6 + 1] * w0 + g->uv[t * 6 + 3] * uu + g->uv[t * 6 + 5] * vv;
                        const int x = std::clamp((int)(su * tw), 0, tw - 1);
                        const int y = std::clamp((int)((1.0f - sv) * th), 0, th - 1);
                        if (px01(tex->data(), (size_t)y * tw + x, 3) < 0.5f) continue;
                    }
                    return true;
                }
                return false;
            };
            std::vector<signed char> cov((size_t)b.verts, -1);
            for (int t = 0; t < b.tris; ++t) {
                if (hidden[t] || b.part[t] != 0) continue;
                bool all = true;
                for (int c = 0; c < 3 && all; ++c) {
                    const int v = b.tri[t * 3 + c];
                    if (cov[(size_t)v] < 0) cov[(size_t)v] = covered(v) ? 1 : 0;
                    all = cov[(size_t)v] == 1;
                }
                if (all) hidden[t] = 1;
            }
        }
    }

    // --- the body part ---------------------------------------------------------------
    const int texSize = potSize(p.textureSize);
    glbparser::SkelPart body;
    body.material = "skin";
    for (int t = 0; t < b.tris; ++t) {
        if (hidden[t]) continue;
        for (int c = 0; c < 3; ++c) {
            const int v = b.tri[t * 3 + c];
            for (int a = 0; a < 3; ++a) body.positions.push_back(pos[(size_t)v * 3 + a]);
            for (int a = 0; a < 3; ++a) body.normals.push_back(nrm[(size_t)v * 3 + a]);
            body.uvs.push_back(b.uv[(size_t)t * 6 + c * 2]);
            body.uvs.push_back(1.0f - b.uv[(size_t)t * 6 + c * 2 + 1]);
            for (int i = 0; i < kMaxInfluences; ++i) body.joints.push_back(b.joints[(size_t)v * 4 + i]);
            for (int i = 0; i < kMaxInfluences; ++i) body.weights.push_back(b.weights[(size_t)v * 4 + i]);
            ++body.vertexCount;
        }
    }

    // Closed scalp coverage of a hair item, on the body's layer grid (0..1):
    // every texel its scalp layer touches, closed (dilate 5, erode 3) and
    // softened. Shared by the scalp paint and an option hair's cap.
    std::map<size_t, std::vector<float>> scalpCache;
    // The head's texels: body triangles skinned (>= half) to the Head bone or
    // anything under it, rasterized in UV space. The closing below is strong
    // enough to bridge the gaps of a messy cut, and this keeps it from
    // spilling into whatever island lies next to the head in the atlas.
    // A body triangle's texels on the layer grid (value 1).
    auto rasterTri = [&](int t, std::vector<uint8_t>& mask) {
        const int S = kLayer;
        float px[3], py[3];
        for (int c = 0; c < 3; ++c) {
            px[c] = b.uv[(size_t)t * 6 + c * 2] * S;
            py[c] = (1.0f - b.uv[(size_t)t * 6 + c * 2 + 1]) * S;
        }
        const int x0 = std::max(0, (int)std::floor(std::min({px[0], px[1], px[2]})) - 1);
        const int x1 = std::min(S - 1, (int)std::ceil(std::max({px[0], px[1], px[2]})) + 1);
        const int y0 = std::max(0, (int)std::floor(std::min({py[0], py[1], py[2]})) - 1);
        const int y1 = std::min(S - 1, (int)std::ceil(std::max({py[0], py[1], py[2]})) + 1);
        const float den = (py[1] - py[2]) * (px[0] - px[2]) + (px[2] - px[1]) * (py[0] - py[2]);
        if (std::fabs(den) < 1e-9f) return;
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                const float qx = x + 0.5f, qy = y + 0.5f;
                const float l0 = ((py[1] - py[2]) * (qx - px[2]) + (px[2] - px[1]) * (qy - py[2])) / den;
                const float l1 = ((py[2] - py[0]) * (qx - px[2]) + (px[0] - px[2]) * (qy - py[2])) / den;
                const float l2 = 1.0f - l0 - l1;
                const float e = -0.05f;  // a sliver of slack: no seams between triangles
                if (l0 >= e && l1 >= e && l2 >= e) mask[(size_t)y * S + x] = 1;
            }
    };
    std::vector<uint8_t> headMask, scalpMask;
    auto headTexels = [&]() -> const std::vector<uint8_t>& {
        if (!headMask.empty()) return headMask;
        const int S = kLayer;
        headMask.assign((size_t)S * S, 0);
        std::vector<char> under((size_t)k.bones, 0);
        for (int j = 0; j < k.bones; ++j)
            for (int a = j; a >= 0; a = k.parent[a])
                if (k.boneNames[(size_t)a] == "mixamorig:Head") under[(size_t)j] = 1;
        auto headWeight = [&](int v) {
            float w = 0.0f, sum = 0.0f;
            for (int i = 0; i < 4; ++i) {
                const float x = b.weights[(size_t)v * 4 + i];
                sum += x;
                const int j = b.joints[(size_t)v * 4 + i];
                if (j >= 0 && j < k.bones && under[(size_t)j]) w += x;
            }
            return sum > 0.0f ? w / sum : 0.0f;
        };
        // The scalp - where hair may colour the skin; the rest of the head
        // is face, and a fringe hanging over it only shades it. On the
        // head: facing up, facing back, or facing sideways above the brows.
        // Heights from the eyes (the eyeball vertices) to the crown.
        scalpMask.assign((size_t)S * S, 0);
        float eyeY = 0.0f, topY = -1e9f;
        int eyeN = 0;
        for (int t = 0; t < b.tris; ++t)
            for (int c = 0; c < 3; ++c) {
                const int v = b.tri[t * 3 + c];
                if (b.part[t] != 0) {
                    eyeY += pos[(size_t)v * 3 + 1];
                    ++eyeN;
                } else {
                    topY = std::max(topY, pos[(size_t)v * 3 + 1]);
                }
            }
        eyeY = eyeN ? eyeY / (float)eyeN : topY;
        const float browY = eyeY + (topY - eyeY) * 0.25f;
        for (int t = 0; t < b.tris; ++t) {
            if (b.part[t] != 0) continue;
            float hw = 0.0f, y = 0.0f, ny = 0.0f, nz = 0.0f;
            for (int c = 0; c < 3; ++c) {
                const int v = b.tri[t * 3 + c];
                hw += headWeight(v) / 3.0f;
                y += pos[(size_t)v * 3 + 1] / 3.0f;
                ny += nrm[(size_t)v * 3 + 1] / 3.0f;
                nz += nrm[(size_t)v * 3 + 2] / 3.0f;
            }
            if (hw < 0.5f) continue;
            rasterTri(t, headMask);
            if (ny > 0.55f || nz < -0.2f || (nz < 0.35f && y > browY)) rasterTri(t, scalpMask);
        }
        return headMask;
    };
    auto scalpTexels = [&]() -> const std::vector<uint8_t>& {
        headTexels();
        return scalpMask;
    };
    auto scalpCover = [&](size_t gi, const uint8_t* paint) -> const std::vector<float>& {
        auto it = scalpCache.find(gi);
        if (it != scalpCache.end()) return it->second;
        const int S = kLayer;
        std::vector<float> m((size_t)S * S), t((size_t)S * S);
        for (size_t i = 0; i < m.size(); ++i) m[i] = px01(paint, i, 3) > 0.04f ? 1.0f : 0.0f;
        {
            // ...and the skin the hair is actually bound to: a body triangle
            // with two corners under hair vertices. The kit's baked layer
            // misses patches of a messy cut's crown; the binding does not.
            // Only near what the layer covers (4 texels): it fills holes in
            // the scalp, it does not widen it - on the dense hero body a
            // fringe binds to enough of the temple to paint the face.
            std::vector<uint8_t> nearLayer((size_t)S * S, 0), rowMax((size_t)S * S, 0);
            for (int y = 0; y < S; ++y)
                for (int x = 0; x < S; ++x) {
                    uint8_t v = 0;
                    for (int d = -4; d <= 4 && !v; ++d) v = m[(size_t)y * S + std::clamp(x + d, 0, S - 1)] > 0.0f;
                    rowMax[(size_t)y * S + x] = v;
                }
            for (int y = 0; y < S; ++y)
                for (int x = 0; x < S; ++x) {
                    uint8_t v = 0;
                    for (int d = -4; d <= 4 && !v; ++d) v = rowMax[(size_t)std::clamp(y + d, 0, S - 1) * S + x];
                    nearLayer[(size_t)y * S + x] = v;
                }
            const GarmentBody& gb = worn[gi]->body[bi];
            std::vector<char> under((size_t)b.verts, 0);
            for (size_t v = 0; v < gb.bindTri.size(); ++v)
                for (int c = 0; c < 3; ++c)
                    if (gb.bindBary[v * 3 + c] >= 0.15f) under[(size_t)b.tri[gb.bindTri[v] * 3 + c]] = 1;
            std::vector<uint8_t> bound((size_t)S * S, 0);
            for (int t = 0; t < b.tris; ++t) {
                if (b.part[t] != 0) continue;
                int n = 0;
                float fwd = 0.0f;  // +Z is the face's way
                for (int c = 0; c < 3; ++c) {
                    n += under[(size_t)b.tri[t * 3 + c]];
                    fwd += nrm[(size_t)b.tri[t * 3 + c] * 3 + 2] / 3.0f;
                }
                // not the face: a fringe is bound to the forehead, and the
                // forehead must stay skin (the kit layer draws the hairline)
                if (n >= 2 && fwd < 0.35f) rasterTri(t, bound);
            }
            for (size_t i = 0; i < m.size(); ++i)
                if (bound[i] && nearLayer[i]) m[i] = 1.0f;
        }
        auto pass = [&](int r, bool grow) {  // separable max (grow) or min
            for (int y = 0; y < S; ++y)
                for (int x = 0; x < S; ++x) {
                    float v = grow ? 0.0f : 1.0f;
                    for (int d = -r; d <= r; ++d) {
                        const float s = m[(size_t)y * S + std::clamp(x + d, 0, S - 1)];
                        v = grow ? std::max(v, s) : std::min(v, s);
                    }
                    t[(size_t)y * S + x] = v;
                }
            for (int y = 0; y < S; ++y)
                for (int x = 0; x < S; ++x) {
                    float v = grow ? 0.0f : 1.0f;
                    for (int d = -r; d <= r; ++d) {
                        const float s = t[(size_t)std::clamp(y + d, 0, S - 1) * S + x];
                        v = grow ? std::max(v, s) : std::min(v, s);
                    }
                    m[(size_t)y * S + x] = v;
                }
        };
        // close: bridge gaps up to ~20 texels without growing the outline,
        // then keep it on the head
        pass(10, true);
        pass(10, false);
        // ...on the scalp only: a fringe's layer reaches the forehead, the
        // eyes and the cheeks, and painted there it was a hair-coloured
        // blotch on the face (the face gets the fringe's shadow instead)
        const std::vector<uint8_t>& head = headTexels();
        const std::vector<uint8_t>& scalp = scalpTexels();
        for (size_t i = 0; i < m.size(); ++i) m[i] *= scalp[i];
        for (int k = 0; k < 2; ++k) {  // soften the edge: a 3x3 box, twice
            for (int y = 0; y < S; ++y)
                for (int x = 0; x < S; ++x) {
                    float s = 0.0f;
                    for (int dy = -1; dy <= 1; ++dy)
                        for (int dx = -1; dx <= 1; ++dx)
                            s += m[(size_t)std::clamp(y + dy, 0, S - 1) * S + std::clamp(x + dx, 0, S - 1)];
                    t[(size_t)y * S + x] = s / 9.0f;
                }
            m.swap(t);
        }
        for (size_t i = 0; i < m.size(); ++i) m[i] *= head[i];
        return scalpCache.emplace(gi, std::move(m)).first->second;
    };

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
                    const uint8_t* img = L(std::string("skin/") + ethNames[e] + "-" + g.name +
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
            const uint8_t* cls = L("class", keepCls);
            for (size_t i = 0; i < n; ++i) {
                // Eyeballs keep their colour whatever the skin does.
                if (cls && cls[i * 4 + 1] > 128 && cls[i * 4] < 128 && cls[i * 4 + 2] < 128) continue;
                for (int c = 0; c < 3; ++c)
                    cv.rgb[i * 3 + c] = std::min(1.0f, cv.rgb[i * 3 + c] * m[c] + add);
            }
        }

        std::shared_ptr<std::vector<uint8_t>> keep;
        // Face paint, under the brows and lashes.
        tintOver(cv, L("mask/lips", keep), 0, p.lipstick * 0.85f, p.lipColor);
        tintOver(cv, L("mask/eyeshadow", keep), 0, p.eyeShadow * 0.8f, p.eyeShadowColor);
        tintOver(cv, L("mask/cheeks", keep), 0, p.blush * 0.45f, Rgb{0.85f, 0.35f, 0.35f});
        {
            // Strong and dark: at 256 and below stubble is a few texels, and a
            // faint one simply is not there on the console.
            const Rgb dark{p.hairColor.r * 0.4f, p.hairColor.g * 0.4f, p.hairColor.b * 0.4f};
            blendOver(cv, L("mask/stubble", keep), 0, p.stubble * 0.95f, dark);
        }
        if (p.brows >= 0 && p.brows < (int)k.brows.size()) {
            const Rgb c{p.hairColor.r * 0.75f, p.hairColor.g * 0.75f, p.hairColor.b * 0.75f};
            blendOver(cv, L("brow/" + k.brows[p.brows], keep), 0,
                      std::clamp(p.browDensity, 0.0f, 1.5f) * 0.92f, c);
        }
        if (p.lashes >= 0 && p.lashes < (int)k.lashes.size())
            blendOver(cv, L("lash/" + k.lashes[p.lashes], keep), 0, 0.85f,
                      Rgb{0.04f, 0.03f, 0.03f});

        // Ambient occlusion from the 13k-quad reference body.
        if (const uint8_t* ao = L("ao", keep))
            for (size_t i = 0; i < n; ++i) {
                const float o = 0.35f + 0.65f * px01(ao, i, 0);
                for (int c = 0; c < 3; ++c) cv.rgb[i * 3 + c] *= o;
            }

        // The eyes: the eyeball layer replaces whatever the skin put there, and
        // the iris (desaturated in the kit) takes the chosen colour.
        {
            std::shared_ptr<std::vector<uint8_t>> keepEye, keepIris;
            const uint8_t* eye = L("eye", keepEye);
            const uint8_t* iris = L("iris", keepIris);
            if (eye)
                for (size_t i = 0; i < n; ++i) {
                    const float ea = px01(eye, i, 3);
                    if (ea <= 0.0f) continue;
                    float c[3] = {px01(eye, i, 0), px01(eye, i, 1), px01(eye, i, 2)};
                    if (iris) {
                        const float m = px01(iris, i, 0);
                        const float col[3] = {p.eyeColor.r, p.eyeColor.g, p.eyeColor.b};
                        for (int q = 0; q < 3; ++q)
                            c[q] += (std::min(1.0f, c[q] * col[q] * 2.4f) - c[q]) * m;
                    }
                    for (int q = 0; q < 3; ++q)
                        cv.rgb[i * 3 + q] += (c[q] - cv.rgb[i * 3 + q]) * ea;
                }
        }

        // Paint a mesh item leaves on the body: hair colours the scalp under
        // itself, so the gaps between its strands show hair, not a bald head.
        // The kit's layer is the hair baked onto the scalp WITH its alpha, so
        // every gap between strands came through as a gap on the scalp too -
        // skin under holey hair. scalpCover closes it: anything the hair
        // touches, dilated and eroded back, then softened; the gaps take the
        // hair's own colour, darkened like roots.
        for (size_t gi = 0; gi < worn.size(); ++gi) {
            if (isOpt[gi]) continue;
            const GarmentData* g = worn[gi];
            std::shared_ptr<std::vector<uint8_t>> kb;
            const uint8_t* paint = L("g/" + g->item.id + "/body", kb);
            if (!paint) continue;
            const std::vector<float>& cover = scalpCover(gi, paint);
            float root[3] = {g->item.color.r * 0.6f, g->item.color.g * 0.6f, g->item.color.b * 0.6f};
            if (g->item.dyeable) dye(root, 1.0f, g->luma, all[gi].color, g->cutout ? 0.55f : 1.0f);
            {
                // Off the scalp the hair's layer is a shadow: where a fringe
                // or a long cut hangs over the face, the skin darkens - a
                // soft one, blurred so the strands do not print.
                const int S = kLayer;
                std::vector<float> sh(n), tmp(n);
                for (size_t i = 0; i < n; ++i) sh[i] = std::min(1.0f, px01(paint, i, 3)) * (1.0f - cover[i]);
                for (int pass = 0; pass < 2; ++pass) {
                    const int r = 4;
                    for (int y = 0; y < S; ++y)
                        for (int x = 0; x < S; ++x) {
                            float s = 0.0f;
                            for (int d = -r; d <= r; ++d) s += sh[(size_t)y * S + std::clamp(x + d, 0, S - 1)];
                            tmp[(size_t)y * S + x] = s / (2 * r + 1);
                        }
                    for (int y = 0; y < S; ++y)
                        for (int x = 0; x < S; ++x) {
                            float s = 0.0f;
                            for (int d = -r; d <= r; ++d) s += tmp[(size_t)std::clamp(y + d, 0, S - 1) * S + x];
                            sh[(size_t)y * S + x] = s / (2 * r + 1);
                        }
                }
                for (size_t i = 0; i < n; ++i) {
                    const float o = 1.0f - 0.45f * sh[i] * (1.0f - cover[i]);
                    for (int q = 0; q < 3; ++q) cv.rgb[i * 3 + q] *= o;
                }
            }
            for (size_t i = 0; i < n; ++i) {
                const float ca = cover[i];
                if (ca <= 0.0f) continue;
                const float pa = std::min(1.0f, px01(paint, i, 3));
                float c[3] = {px01(paint, i, 0), px01(paint, i, 1), px01(paint, i, 2)};
                if (g->item.dyeable) dye(c, 1.0f, g->luma, all[gi].color, g->cutout ? 0.55f : 1.0f);
                for (int q = 0; q < 3; ++q) {
                    const float want = root[q] + (c[q] - root[q]) * pa;
                    cv.rgb[i * 3 + q] += (want - cv.rgb[i * 3 + q]) * ca;
                }
            }
        }

        // Shell garments are painted straight into the body's atlas: the shell
        // IS the body surface, pushed out.
        for (size_t gi = 0; gi < worn.size(); ++gi) {
            const GarmentData* g = worn[gi];
            if (g->kind != "shell") continue;
            std::shared_ptr<std::vector<uint8_t>> kc;
            const uint8_t* col = L("g/" + g->item.id, kc);
            if (!col) continue;
            const Wear& w = all[gi];
            for (int y = 0; y < kLayer; ++y)
                for (int x = 0; x < kLayer; ++x) {
                    const size_t i = (size_t)y * kLayer + x;
                    const float ca = px01(col, i, 3);
                    if (ca <= 0.0f) continue;
                    float c[3] = {px01(col, i, 0), px01(col, i, 1), px01(col, i, 2)};
                    Rgb to = w.color;
                    if (w.pattern > 0) {
                        // A pattern needs a base colour even on an undyed garment.
                        if (to.r < 0.0f) to = g->item.color;
                        const float f = patternAt(w.pattern, x, y);
                        to = Rgb{to.r + (w.color2.r - to.r) * f, to.g + (w.color2.g - to.g) * f,
                                 to.b + (w.color2.b - to.b) * f};
                    }
                    if (g->item.dyeable) dye(c, dyeMask(c, g->item.color), g->luma, to);
                    for (int q = 0; q < 3; ++q) cv.rgb[i * 3 + q] += (c[q] - cv.rgb[i * 3 + q]) * ca;
                }
        }

        glbparser::Image img;
        img.name = "skin";
        img.png = encodePng(downsample(cv.rgb, texSize), texSize, texSize);
        body.image = (int)out.images.size();
        out.images.push_back(std::move(img));
    }
    out.parts.push_back(std::move(body));

    // --- mesh items: ONE part, ONE texture ----------------------------------------
    // Every worn mesh item (hair, shoes, a hat, glasses, a skirt) rides the
    // body's surface, and all of them share a single accessory atlas laid out
    // as a grid of cells - a dressed character is two GS allocations and two
    // submits, not five. The atlas is the body's size; a cell is a fraction.
    //
    // Creator options are the exception: each is its own part with its own
    // small texture named "opt-<slot>-<id>" ("optd-" for the one worn by
    // default), so the game can show one per slot and hide the rest
    // (docs/character-generator.md, "In-game character creator").
    //
    // Hat hair: a hat is a mesh item riding the skull on its own, and big hair
    // used to poke straight through it. hatCap() measures, per body vertex,
    // how far above the skin the hats' lowest surface sits there (the inside
    // of the crown); emitItem then presses hair bound to that region under
    // it, feathered over the hat's edge, while what hangs below the hat - a
    // fringe, a ponytail - keeps its shape. A worn hat fits the hair outright;
    // with hat OPTIONS every hairstyle gets a second, pressed part
    // ("opth-hair-<id>", sharing its texture) the game shows under a hat.
    auto hatCap = [&](bool options) {
        std::vector<float> cap((size_t)b.verts, kNoCap);
        bool any = false;
        for (size_t gi = 0; gi < worn.size(); ++gi) {
            const GarmentData* g = worn[gi];
            if (g->item.slot != "head" || g->kind != "mesh" || (isOpt[gi] != 0) != options) continue;
            const GarmentBody& gb = g->body[bi];
            for (size_t v = 0; v < gb.bindTri.size(); ++v) {
                const int t = gb.bindTri[v];
                const float off = gb.bindOff[v * 3];
                for (int c = 0; c < 3; ++c) {
                    if (gb.bindBary[v * 3 + c] < 0.15f) continue;  // the corners it really rides
                    float& m = cap[(size_t)b.tri[t * 3 + c]];
                    m = std::min(m, std::max(off, 0.0f));
                }
                any = true;
            }
        }
        if (!any) cap.clear();
        return cap;
    };
    const std::vector<float> capWorn = hatCap(false), capOptions = hatCap(true);
    std::vector<float> capUnder = capOptions;  // under an optional hat, a worn one too
    for (size_t i = 0; i < capUnder.size() && i < capWorn.size(); ++i)
        capUnder[i] = std::min(capUnder[i], capWorn[i]);

    auto emitItem = [&](size_t gi, glbparser::SkelPart& part, std::vector<uint8_t>& atlas, int A,
                        int cx, int cy, int cw, int ch, const std::vector<float>* cap) {
        const GarmentData* g = worn[gi];
        const GarmentBody& gb = g->body[bi];
        if (g->item.slot != "hair" || (cap && cap->empty())) cap = nullptr;
        std::vector<float> gp = itemPositions(g, cap), gn;
        vertexNormals(gp, g->tri.data(), (int)g->tri.size() / 3, gn);
        const size_t gt = g->tri.size() / 3;
        for (size_t t = 0; t < gt; ++t)
            for (int c = 0; c < 3; ++c) {
                const int v = g->tri[t * 3 + c];
                for (int a = 0; a < 3; ++a) part.positions.push_back(gp[(size_t)v * 3 + a]);
                for (int a = 0; a < 3; ++a) part.normals.push_back(gn[(size_t)v * 3 + a]);
                // Into this item's cell, half a texel in from each edge so
                // bilinear filtering never reads the neighbouring cell.
                const float u = g->uv[t * 6 + c * 2], vv = 1.0f - g->uv[t * 6 + c * 2 + 1];
                part.uvs.push_back((cx + 0.5f + u * (cw - 1.0f)) / (float)A);
                part.uvs.push_back((cy + 0.5f + vv * (ch - 1.0f)) / (float)A);
                for (int i = 0; i < kMaxInfluences; ++i) part.joints.push_back(gb.joints[(size_t)v * 4 + i]);
                for (int i = 0; i < kMaxInfluences; ++i) part.weights.push_back(gb.weights[(size_t)v * 4 + i]);
                ++part.vertexCount;
            }

        // Its texture into the cell: dyed, alpha binary for cutouts and
        // opaque for everything else (StaPip alpha-tests "pass when alpha
        // != 0", and the CLUT path loses soft gradients).
        int w = 0, h = 0;
        std::shared_ptr<std::vector<uint8_t>> col = image("g/" + g->item.id, &w, &h);
        if (!col || w <= 0 || h <= 0) return;
        const Wear& choice = all[gi];
        for (int y = 0; y < ch; ++y)
            for (int x = 0; x < cw; ++x) {
                // Box-filter the source footprint of this cell texel.
                const int x0 = x * w / cw, x1 = std::max(x0 + 1, (x + 1) * w / cw);
                const int y0 = y * h / ch, y1 = std::max(y0 + 1, (y + 1) * h / ch);
                float s[4] = {0, 0, 0, 0};
                for (int sy = y0; sy < y1; ++sy)
                    for (int sx = x0; sx < x1; ++sx) {
                        const size_t i = (size_t)sy * w + sx;
                        float c[3] = {px01(col->data(), i, 0), px01(col->data(), i, 1),
                                      px01(col->data(), i, 2)};
                        if (g->item.dyeable)
                            dye(c, g->cutout ? 1.0f : dyeMask(c, g->item.color), g->luma, choice.color,
                                g->cutout ? 0.55f : 1.0f);
                        for (int q = 0; q < 3; ++q) s[q] += c[q];
                        s[3] += px01(col->data(), i, 3);
                    }
                const float inv = 1.0f / (float)((x1 - x0) * (y1 - y0));
                uint8_t* o = &atlas[((size_t)(cy + y) * A + (cx + x)) * 4];
                for (int q = 0; q < 3; ++q)
                    o[q] = (uint8_t)std::clamp((int)std::lround(s[q] * inv * 255.0f), 0, 255);
                o[3] = g->cutout ? (s[3] * inv >= 0.5f ? 255 : 0) : 255;
            }
    };
    std::vector<size_t> meshItems;
    for (size_t gi = 0; gi < worn.size(); ++gi)
        if (worn[gi]->kind == "mesh" && !worn[gi]->tri.empty() && !isOpt[gi]) meshItems.push_back(gi);
    if (!meshItems.empty()) {
        const int count = (int)meshItems.size();
        const int cols = (int)std::ceil(std::sqrt((float)count));
        const int rows = (count + cols - 1) / cols;
        const int A = texSize;
        const int cw = A / cols, ch = A / rows;
        std::vector<uint8_t> atlas((size_t)A * A * 4, 0);
        glbparser::SkelPart part;
        bool anyCutout = false;
        for (int m = 0; m < count; ++m) {
            anyCutout |= worn[meshItems[m]]->cutout;
            emitItem(meshItems[m], part, atlas, A, (m % cols) * (A / cols), (m / cols) * (A / rows),
                     cw, ch, &capWorn);
        }
        // The material name carries the kind: "hair:" means alpha-tested (the
        // editor preview draws it last), which is harmless for the opaque
        // cells - their alpha is 255.
        part.material = anyCutout ? "hair:accessories" : "cloth:accessories";
        glbparser::Image img;
        img.name = "accessories";
        img.png = encodePng(atlas, A, A);
        part.image = (int)out.images.size();
        out.images.push_back(std::move(img));
        out.parts.push_back(std::move(part));
    }
    // An option hairstyle cannot paint the scalp (the player may take it
    // off), so it brings its own: the scalp triangles it covers (scalpCover),
    // 4 mm out, every corner on its texture's darkest solid texel - the shade
    // at the roots. Drawn and hidden with the hair.
    std::vector<std::array<float, 2>> capUV(worn.size(), {-1.0f, -1.0f});
    // The cap's triangles. A hero body is the standard one subdivided, with
    // the standard body's vertices FIRST - so its cap uses the standard
    // body's (4x coarser) triangles on those vertices: on the hero's own the
    // caps were bigger than the hairstyles (1900 triangles each) and the
    // example's creator hero ran the EE out of memory. Nobody sees a cap.
    const Body* capBody = &b;
    if (b.detail == "hero")
        for (const Body& o : k.bodies)
            if (o.detail == "standard" && o.sex == b.sex && o.verts <= b.verts) {
                bool same = true;  // its body vertices must coincide with ours
                for (int t = 0; t < o.tris && same; t += 37) {
                    if (o.part[t] != 0) continue;
                    const int v = o.tri[t * 3];
                    for (int ax = 0; ax < 3; ++ax)
                        if (std::fabs(o.pos[(size_t)v * 3 + ax] - b.pos[(size_t)v * 3 + ax]) > 0.02f)
                            same = false;
                }
                if (same) capBody = &o;
            }
    auto addScalpCap = [&](size_t gi, glbparser::SkelPart& part) {
        if (worn[gi]->item.slot != "hair" || capUV[gi][0] < 0.0f) return;
        std::shared_ptr<std::vector<uint8_t>> kb;
        const uint8_t* paint = L("g/" + worn[gi]->item.id + "/body", kb);
        if (!paint) return;
        const std::vector<float>& cover = scalpCover(gi, paint);
        const int S = kLayer;
        // covered vertices: any of the body's own triangles at the vertex
        // has its centre under the closed scalp cover
        std::vector<char> vcov((size_t)b.verts, 0);
        for (int t = 0; t < b.tris; ++t) {
            if (b.part[t] != 0) continue;  // the eyes are not scalp
            float cu = 0.0f, cvv = 0.0f;
            for (int c = 0; c < 3; ++c) {
                cu += b.uv[(size_t)t * 6 + c * 2] / 3.0f;
                cvv += b.uv[(size_t)t * 6 + c * 2 + 1] / 3.0f;
            }
            const int x = std::clamp((int)(cu * S), 0, S - 1);
            const int y = std::clamp((int)((1.0f - cvv) * S), 0, S - 1);
            if (cover[(size_t)y * S + x] < 0.5f) continue;
            for (int c = 0; c < 3; ++c) vcov[(size_t)b.tri[t * 3 + c]] = 1;
        }
        const Body& cb = *capBody;
        for (int t = 0; t < cb.tris; ++t) {
            if (cb.part[t] != 0) continue;
            bool all = true;
            for (int c = 0; c < 3 && all; ++c) {
                const int v = cb.tri[t * 3 + c];
                all = v < b.verts && vcov[(size_t)v];
            }
            if (!all) continue;
            for (int c = 0; c < 3; ++c) {
                const int v = cb.tri[t * 3 + c];
                for (int a = 0; a < 3; ++a)
                    part.positions.push_back(pos[(size_t)v * 3 + a] + nrm[(size_t)v * 3 + a] * 0.004f);
                for (int a = 0; a < 3; ++a) part.normals.push_back(nrm[(size_t)v * 3 + a]);
                part.uvs.push_back(capUV[gi][0]);
                part.uvs.push_back(capUV[gi][1]);
                for (int i = 0; i < kMaxInfluences; ++i) part.joints.push_back(b.joints[(size_t)v * 4 + i]);
                for (int i = 0; i < kMaxInfluences; ++i) part.weights.push_back(b.weights[(size_t)v * 4 + i]);
                ++part.vertexCount;
            }
        }
    };
    // the darkest texel whose 3x3 neighbourhood is all solid
    auto darkestSolid = [](const std::vector<uint8_t>& tex, int A) -> std::array<float, 2> {
        std::array<float, 2> best = {-1.0f, -1.0f};
        float lo = 1e9f;
        for (int y = 1; y + 1 < A; ++y)
            for (int x = 1; x + 1 < A; ++x) {
                bool solid = true;
                float l = 0.0f;
                for (int dy = -1; dy <= 1 && solid; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const uint8_t* p = &tex[((size_t)(y + dy) * A + (x + dx)) * 4];
                        if (p[3] < 255) {
                            solid = false;
                            break;
                        }
                        l += p[0] * 0.3f + p[1] * 0.59f + p[2] * 0.11f;
                    }
                if (solid && l < lo) {
                    lo = l;
                    best = {(x + 0.5f) / (float)A, (y + 0.5f) / (float)A};
                }
            }
        return best;
    };
    std::vector<int> optImage(worn.size(), -1);
    for (size_t gi = 0; gi < worn.size(); ++gi) {
        if (!isOpt[gi] || worn[gi]->tri.empty()) continue;
        // Half the atlas size: an option is one item, not a grid of them.
        const int A = std::max(32, texSize / 2);
        std::vector<uint8_t> tex((size_t)A * A * 4, 0);
        glbparser::SkelPart part;
        emitItem(gi, part, tex, A, 0, 0, A, A, &capWorn);
        capUV[gi] = darkestSolid(tex, A);
        addScalpCap(gi, part);
        const std::string tag = std::string(isOpt[gi] == 2 ? "optd-" : "opt-") +
                                worn[gi]->item.slot + "-" + worn[gi]->item.id;
        part.material = std::string(worn[gi]->cutout ? "hair:" : "cloth:") + tag;
        glbparser::Image img;
        img.name = tag;
        img.png = encodePng(tex, A, A);
        part.image = optImage[gi] = (int)out.images.size();
        out.images.push_back(std::move(img));
        out.parts.push_back(std::move(part));
    }
    // ...and each hairstyle once more, pressed under the optional hats. After
    // every option, so the game meets the plain part (and its index) first.
    if (!capOptions.empty())
        for (size_t gi = 0; gi < worn.size(); ++gi) {
            if (optImage[gi] < 0 || worn[gi]->item.slot != "hair") continue;
            const int A = 4;  // the texels are the plain part's: a throwaway target
            std::vector<uint8_t> scratch((size_t)A * A * 4, 0);
            glbparser::SkelPart part;
            emitItem(gi, part, scratch, A, 0, 0, A, A, &capUnder);
            // no scalp cap: the hat over it covers the scalp, and on a hero
            // body the caps were most of a creator character's memory
            part.material = std::string(worn[gi]->cutout ? "hair:" : "cloth:") + "opth-hair-" +
                            worn[gi]->item.id;
            part.image = optImage[gi];
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
    // Skirt panels swung by the legs, baked into every clip - the same drive
    // the game's updateSprings computes (front: the thigh furthest forward,
    // back: furthest back, each side: its own; 0.85 / 0.7 of the swing). The
    // editor's preview, an exported .glb and a far instance in the game get
    // a skirt that follows the knees; up close the game's spring replaces the
    // panel's rotation with its own (SkelInstance's replace override). Only
    // when something worn is weighted to the panels: the EE pays per channel.
    int skirtBones[4] = {-1, -1, -1, -1};
    {
        static const char* kNames[4] = {"mixamorig:SkirtFront", "mixamorig:SkirtBack",
                                        "mixamorig:SkirtLeft", "mixamorig:SkirtRight"};
        for (int s = 0; s < 4; ++s)
            for (int j = 0; j < k.bones; ++j)
                if (k.boneNames[(size_t)j] == kNames[s]) skirtBones[s] = j;
        bool weighted = false;
        for (size_t gi = 0; gi < worn.size() && skirtBones[0] >= 0; ++gi) {
            if (isOpt[gi]) continue;
            const GarmentBody& gb = worn[gi]->body[bi];
            for (size_t i = 0; i < gb.joints.size() && !weighted; ++i)
                for (int s = 0; s < 4; ++s)
                    if (gb.joints[i] == skirtBones[s] && gb.weights[i] > 0) weighted = true;
        }
        if (!weighted) skirtBones[0] = -1;
    }
    auto findBone = [&](const char* name) {
        for (int j = 0; j < k.bones; ++j)
            if (k.boneNames[(size_t)j] == name) return j;
        return -1;
    };
    const int hipsBone = findBone("mixamorig:Hips");
    const int legBones[4] = {findBone("mixamorig:LeftUpLeg"), findBone("mixamorig:LeftLeg"),
                             findBone("mixamorig:RightUpLeg"), findBone("mixamorig:RightLeg")};
    auto bakeSkirt = [&](glbparser::SkelClip& sc) {
        if (hipsBone < 0 || legBones[0] < 0 || legBones[1] < 0 || legBones[2] < 0 || legBones[3] < 0)
            return;
        const size_t keys = sc.channels.empty() ? 0 : sc.channels[0].times.size();
        if (keys == 0) return;
        // the clip's rotation of each node per key (identity where it has no channel)
        std::vector<const glbparser::SkelChannel*> rotOf((size_t)k.bones, nullptr);
        for (const glbparser::SkelChannel& ch : sc.channels)
            if (ch.path == 1 && ch.node >= 0 && ch.node < k.bones) rotOf[(size_t)ch.node] = &ch;
        auto qmul = [](const float* a, const float* b, float* o) {
            o[0] = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
            o[1] = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
            o[2] = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
            o[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
        };
        auto qrot = [&](const float* q, const float* v, float* o) {  // q v q*
            const float p[4] = {v[0], v[1], v[2], 0.0f}, qc[4] = {-q[0], -q[1], -q[2], q[3]};
            float t[4], r[4];
            qmul(q, p, t);
            qmul(t, qc, r);
            o[0] = r[0], o[1] = r[1], o[2] = r[2];
        };
        std::vector<float> drive[4];
        for (size_t i = 0; i < keys; ++i) {
            // global rotation + position of the chain root..node (translations
            // are the rest offsets; the hips' own translation does not matter
            // for a direction measured in the hips' frame)
            std::vector<float> gq((size_t)k.bones * 4), gp((size_t)k.bones * 3);
            std::vector<char> done((size_t)k.bones, 0);
            std::function<void(int)> solve = [&](int j) {
                if (done[(size_t)j]) return;
                float lq[4] = {0, 0, 0, 1};
                if (const glbparser::SkelChannel* ch = rotOf[(size_t)j])
                    for (int c = 0; c < 4; ++c) lq[c] = ch->values[i * 4 + c];
                const int p = out.nodes[(size_t)j].parent;
                if (p < 0) {
                    for (int c = 0; c < 4; ++c) gq[(size_t)j * 4 + c] = lq[c];
                    for (int c = 0; c < 3; ++c) gp[(size_t)j * 3 + c] = out.nodes[(size_t)j].t[c];
                } else {
                    solve(p);
                    qmul(&gq[(size_t)p * 4], lq, &gq[(size_t)j * 4]);
                    float off[3];
                    qrot(&gq[(size_t)p * 4], out.nodes[(size_t)j].t, off);
                    for (int c = 0; c < 3; ++c) gp[(size_t)j * 3 + c] = gp[(size_t)p * 3 + c] + off[c];
                }
                done[(size_t)j] = 1;
            };
            float swing[2];
            for (int leg = 0; leg < 2; ++leg) {
                solve(legBones[leg * 2]);
                solve(legBones[leg * 2 + 1]);
                solve(hipsBone);
                float dm[3], dl[3];
                for (int c = 0; c < 3; ++c)
                    dm[c] = gp[(size_t)legBones[leg * 2 + 1] * 3 + c] - gp[(size_t)legBones[leg * 2] * 3 + c];
                const float* hq = &gq[(size_t)hipsBone * 4];
                const float inv[4] = {-hq[0], -hq[1], -hq[2], hq[3]};
                qrot(inv, dm, dl);
                swing[leg] = std::atan2(dl[2], -dl[1]);
            }
            // the front a little AHEAD of the thigh: at 0.85 a short dress's
            // hem lagged and the striding thigh came through (twin of the
            // game's updateSprings)
            drive[0].push_back(std::max(std::max(swing[0], swing[1]), 0.0f) * 1.1f);
            drive[1].push_back(std::min(std::min(swing[0], swing[1]), 0.0f) * 1.0f);
            drive[2].push_back(swing[0] * 0.85f);
            drive[3].push_back(swing[1] * 0.85f);
        }
        for (int s = 0; s < 4; ++s) {
            if (skirtBones[s] < 0) continue;
            glbparser::SkelChannel ch;
            ch.node = skirtBones[s];
            ch.path = 1;
            ch.times = sc.channels[0].times;
            for (size_t i = 0; i < keys; ++i) {
                // the panel's tip swung forward by `drive` about the hips' X:
                // a rotation of -drive (updateSprings' `driven` vector)
                const float h = -drive[s][i] * 0.5f;
                const float q[4] = {std::sin(h), 0.0f, 0.0f, std::cos(h)};
                ch.values.insert(ch.values.end(), q, q + 4);
            }
            sc.channels.push_back(std::move(ch));
        }
    };
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
        // Quaternius' jog and sprint shrug the shoulders up to the ears for
        // the whole cycle - fine on his mannequin, odd on a person. Their
        // clavicles are pulled most of the way to where the idle holds them;
        // the arms keep their swing.
        const bool running = srcName.find("Jog") != std::string::npos ||
                             srcName.find("Sprint") != std::string::npos ||
                             srcName.find("Run") != std::string::npos;
        const Kit::Clip* idle = nullptr;
        if (running)
            for (const Kit::Clip& c : k.clips)
                if (c.info.name == "Idle_Loop") idle = &c;
        auto relaxShoulder = [&](int bone, float q[4]) {
            if (!idle || idle->frames <= 0) return;
            const std::string& nm = k.boneNames[(size_t)bone];
            // ...and the elbows, held out at 42 degrees from the body (19
            // in the idle), come in by 18: a turn about the forward axis in
            // the clavicle's frame, before the clip's own rotation.
            if (nm == "mixamorig:LeftArm" || nm == "mixamorig:RightArm") {
                const float a = (nm == "mixamorig:LeftArm" ? -18.0f : 18.0f) * 3.14159265f / 180.0f;
                const float r[4] = {0.0f, 0.0f, std::sin(a * 0.5f), std::cos(a * 0.5f)};
                const float o[4] = {q[0], q[1], q[2], q[3]};
                q[0] = r[3] * o[0] + r[0] * o[3] + r[1] * o[2] - r[2] * o[1];
                q[1] = r[3] * o[1] - r[0] * o[2] + r[1] * o[3] + r[2] * o[0];
                q[2] = r[3] * o[2] + r[0] * o[1] - r[1] * o[0] + r[2] * o[3];
                q[3] = r[3] * o[3] - r[0] * o[0] - r[1] * o[1] - r[2] * o[2];
                return;
            }
            if (nm != "mixamorig:LeftShoulder" && nm != "mixamorig:RightShoulder") return;
            const int16_t* r = idle->rot + (size_t)bone * 4;  // the idle's first frame
            float qi[4], dot = 0.0f;
            for (int c = 0; c < 4; ++c) {
                qi[c] = r[c] / 32767.0f;
                dot += qi[c] * q[c];
            }
            if (dot < 0)
                for (float& c : qi) c = -c;
            float l = 0.0f;
            for (int c = 0; c < 4; ++c) {
                q[c] += (qi[c] - q[c]) * 1.0f;
                l += q[c] * q[c];
            }
            l = std::sqrt(l);
            for (int c = 0; c < 4; ++c) q[c] = l > 1e-9f ? q[c] / l : (c == 3);
        };
        // Posture. Quaternius' clips pull the clavicles ~20 degrees back
        // (the shoulder joint 6 cm behind where the rig rests it) and let
        // the upper arms hang 14 degrees backwards: on a person the arms
        // hung behind the body, the hands 11 cm behind the hips. Every clip:
        // the clavicles come 15 degrees forward, and an upper arm that hangs
        // down tips 12 degrees forward - scaled by how much it hangs, so an
        // arm held out (aiming, punching) is left alone. Both turns are in
        // the parent's frame, before the clip's own rotation.
        auto qmul = [](const float r[4], float q[4]) {
            const float o[4] = {q[0], q[1], q[2], q[3]};
            q[0] = r[3] * o[0] + r[0] * o[3] + r[1] * o[2] - r[2] * o[1];
            q[1] = r[3] * o[1] - r[0] * o[2] + r[1] * o[3] + r[2] * o[0];
            q[2] = r[3] * o[2] + r[0] * o[1] - r[1] * o[0] + r[2] * o[3];
            q[3] = r[3] * o[3] - r[0] * o[0] - r[1] * o[1] - r[2] * o[2];
        };
        const float deg = 3.14159265f / 180.0f;
        // an upper arm's direction under its local rotation q (unit vector),
        // from its rest direction (the bind is identity: rest = head to child)
        auto armDir = [&](int bone, const float q[4], float out[3]) -> bool {
            int child = -1;
            for (int j = 0; j < k.bones; ++j)
                if (k.parent[j] == bone) child = j;
            if (child < 0) return false;
            float d[3];
            for (int c = 0; c < 3; ++c) d[c] = heads[(size_t)child * 3 + c] - heads[(size_t)bone * 3 + c];
            const float len = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            if (len < 1e-6f) return false;
            const float x = q[0], y = q[1], z = q[2], w = q[3];
            out[0] = ((1.0f - 2.0f * (y * y + z * z)) * d[0] + 2.0f * (x * y - w * z) * d[1] + 2.0f * (x * z + w * y) * d[2]) / len;
            out[1] = (2.0f * (x * y + w * z) * d[0] + (1.0f - 2.0f * (x * x + z * z)) * d[1] + 2.0f * (y * z - w * x) * d[2]) / len;
            out[2] = (2.0f * (x * z - w * y) * d[0] + 2.0f * (y * z + w * x) * d[1] + (1.0f - 2.0f * (x * x + y * y)) * d[2]) / len;
            return true;
        };
        auto hangOf = [](const float d[3]) { return std::clamp(-d[1] / 0.8f, 0.0f, 1.0f); };
        // Per arm: how far this clip hangs it back on average (weighted by
        // how much it hangs), measured against the TORSO - the line from the
        // hips to the neck - not gravity (a sprint leans the body forward and
        // its arms are where they should be) and not the chest bone's frame
        // (the idle tips the chest forward and the arms back).
        auto rotv = [](const float q[4], const float v[3], float o[3]) {
            const float x = q[0], y = q[1], z = q[2], w = q[3];
            o[0] = (1.0f - 2.0f * (y * y + z * z)) * v[0] + 2.0f * (x * y - w * z) * v[1] + 2.0f * (x * z + w * y) * v[2];
            o[1] = 2.0f * (x * y + w * z) * v[0] + (1.0f - 2.0f * (x * x + z * z)) * v[1] + 2.0f * (y * z - w * x) * v[2];
            o[2] = 2.0f * (x * z - w * y) * v[0] + 2.0f * (y * z + w * x) * v[1] + (1.0f - 2.0f * (x * x + y * y)) * v[2];
        };
        int boneHips = -1, boneNeck = -1, armL = -1, armR = -1;
        for (int j = 0; j < k.bones; ++j) {
            const std::string& nm = k.boneNames[(size_t)j];
            if (nm == "mixamorig:Hips") boneHips = j;
            if (nm == "mixamorig:Neck") boneNeck = j;
            if (nm == "mixamorig:LeftArm") armL = j;
            if (nm == "mixamorig:RightArm") armR = j;
        }
        std::map<int, float> armFix;
        if (boneHips >= 0 && boneNeck >= 0 && armL >= 0 && armR >= 0) {
            float sum[2] = {0, 0}, wsum[2] = {0, 0};
            std::vector<float> wq((size_t)k.bones * 4), wp((size_t)k.bones * 3);
            std::vector<char> done((size_t)k.bones);
            for (int i = 0; i < keys; ++i) {
                // forward kinematics, rotations only (the hips sit at 0),
                // with the clavicles' posture turn
                std::fill(done.begin(), done.end(), 0);
                std::function<void(int)> fk = [&](int j) {
                    if (done[(size_t)j]) return;
                    float q[4];
                    sampleRot(j, times[i], q);
                    relaxShoulder(j, q);
                    const std::string& nm = k.boneNames[(size_t)j];
                    if (nm == "mixamorig:LeftShoulder" || nm == "mixamorig:RightShoulder") {
                        const float cs = (nm == "mixamorig:LeftShoulder" ? -15.0f : 15.0f) * deg;
                        const float ry[4] = {0.0f, std::sin(cs * 0.5f), 0.0f, std::cos(cs * 0.5f)};
                        qmul(ry, q);
                    }
                    const int pa = k.parent[j];
                    if (pa >= 0) {
                        fk(pa);
                        float qq[4] = {q[0], q[1], q[2], q[3]};
                        qmul(&wq[(size_t)pa * 4], qq);  // world = parent's world * local
                        std::copy(qq, qq + 4, &wq[(size_t)j * 4]);
                        float off[3], r[3];
                        for (int c = 0; c < 3; ++c) off[c] = heads[(size_t)j * 3 + c] - heads[(size_t)pa * 3 + c];
                        rotv(&wq[(size_t)pa * 4], off, r);
                        for (int c = 0; c < 3; ++c) wp[(size_t)j * 3 + c] = wp[(size_t)pa * 3 + c] + r[c];
                    } else {
                        std::copy(q, q + 4, &wq[(size_t)j * 4]);
                        for (int c = 0; c < 3; ++c) wp[(size_t)j * 3 + c] = 0.0f;
                    }
                    done[(size_t)j] = 1;
                };
                fk(boneNeck);
                fk(armL);
                fk(armR);
                fk(boneHips);
                float up[3], lat[3], fw[3];
                for (int c = 0; c < 3; ++c) {
                    up[c] = wp[(size_t)boneNeck * 3 + c] - wp[(size_t)boneHips * 3 + c];
                    lat[c] = wp[(size_t)armL * 3 + c] - wp[(size_t)armR * 3 + c];
                }
                fw[0] = lat[1] * up[2] - lat[2] * up[1];
                fw[1] = lat[2] * up[0] - lat[0] * up[2];
                fw[2] = lat[0] * up[1] - lat[1] * up[0];
                const float ul = std::sqrt(up[0] * up[0] + up[1] * up[1] + up[2] * up[2]);
                const float fl = std::sqrt(fw[0] * fw[0] + fw[1] * fw[1] + fw[2] * fw[2]);
                if (ul < 1e-6f || fl < 1e-6f) continue;
                for (int s = 0; s < 2; ++s) {
                    const int arm = s == 0 ? armL : armR;
                    float local[4], d[3], dw[3];
                    sampleRot(arm, times[i], local);
                    relaxShoulder(arm, local);
                    if (!armDir(arm, local, d)) continue;
                    rotv(&wq[(size_t)k.parent[arm] * 4], d, dw);
                    const float du = (dw[0] * up[0] + dw[1] * up[1] + dw[2] * up[2]) / ul;
                    const float df = (dw[0] * fw[0] + dw[1] * fw[1] + dw[2] * fw[2]) / fl;
                    const float h = std::clamp(-du / 0.8f, 0.0f, 1.0f);
                    sum[s] += h * std::atan2(-df, -du);  // + = hanging back
                    wsum[s] += h;
                }
            }
            for (int s = 0; s < 2; ++s)
                armFix[s == 0 ? armL : armR] =
                    wsum[s] > 0.0f ? std::clamp(sum[s] / wsum[s], 0.0f, 25.0f * deg) : 0.0f;
        }
        auto posture = [&](int bone, float q[4]) {
            const std::string& nm = k.boneNames[(size_t)bone];
            const bool left = nm.rfind("mixamorig:Left", 0) == 0;
            if (nm == "mixamorig:LeftShoulder" || nm == "mixamorig:RightShoulder") {
                const float a = (left ? -15.0f : 15.0f) * deg;  // about up (+Y)
                const float r[4] = {0.0f, std::sin(a * 0.5f), 0.0f, std::cos(a * 0.5f)};
                qmul(r, q);
                return;
            }
            auto it = armFix.find(bone);
            if (it == armFix.end() || it->second <= 0.0f) return;
            float d[3];
            if (!armDir(bone, q, d)) return;
            const float a = -it->second * hangOf(d);  // about +X: a hanging arm swings toward +Z
            const float r[4] = {std::sin(a * 0.5f), 0.0f, 0.0f, std::cos(a * 0.5f)};
            qmul(r, q);
        };

        // Movement style (docs/character-generator.md, "Movement style"):
        // one motion library, read as a woman's or a man's walk by what
        // animators exaggerate. Two kinds of change:
        // - amplitude, on locomotion clips only (a loop has a mean pose to
        //   swing about; a punch does not): each key's rotation away from
        //   the clip's mean is scaled - the hips sway and roll more, the
        //   chest counter-turns less, the arms swing less (a man the other
        //   way), and the hips' sideways shift follows the sway;
        // - a pose offset on every clip, scaled by how much the limb hangs
        //   (so sitting, aiming and punching keep theirs): the thighs turn
        //   in (a narrower step, feet toward the line), the elbows come in.
        const float style = motionStyleFor(p);
        const float fem = std::max(style, 0.0f), mas = std::max(-style, 0.0f);
        const bool locomotion = srcName.find("Walk") != std::string::npos ||
                                srcName.find("Jog") != std::string::npos ||
                                srcName.find("Sprint") != std::string::npos ||
                                srcName.find("Run") != std::string::npos ||
                                srcName.find("Crouch_Fwd") != std::string::npos;
        auto ampOf = [&](const std::string& nm) -> float {
            if (!locomotion || style == 0.0f) return 1.0f;
            if (nm == "mixamorig:Hips") return 1.0f + 0.8f * fem - 0.2f * mas;
            if (nm == "mixamorig:Spine2" || nm == "mixamorig:Spine1")
                return 1.0f - 0.35f * fem + 0.35f * mas;
            if (nm == "mixamorig:LeftArm" || nm == "mixamorig:RightArm" ||
                nm == "mixamorig:LeftForeArm" || nm == "mixamorig:RightForeArm")
                return 1.0f - 0.3f * fem + 0.15f * mas;
            return 1.0f;
        };
        auto mul = [](const float a[4], const float b[4], float o[4]) {  // o = a * b
            o[0] = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
            o[1] = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
            o[2] = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
            o[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
        };
        // each scaled bone's mean rotation over the clip (as relaxShoulder
        // leaves it; the keys are dense, a normalized sum is the mean)
        std::map<int, std::array<float, 4>> meanRot;
        for (int b = 0; b < k.bones; ++b) {
            if (ampOf(k.boneNames[(size_t)b]) == 1.0f) continue;
            float s[4] = {0, 0, 0, 0}, first[4] = {0, 0, 0, 1};
            for (int i = 0; i < keys; ++i) {
                float q[4];
                sampleRot(b, times[i], q);
                relaxShoulder(b, q);
                if (i == 0) std::memcpy(first, q, sizeof(first));
                const float dot = q[0] * first[0] + q[1] * first[1] + q[2] * first[2] + q[3] * first[3];
                for (int c = 0; c < 4; ++c) s[c] += dot < 0 ? -q[c] : q[c];
            }
            const float l = std::sqrt(s[0] * s[0] + s[1] * s[1] + s[2] * s[2] + s[3] * s[3]);
            if (l < 1e-6f) continue;
            meanRot[b] = {s[0] / l, s[1] / l, s[2] / l, s[3] / l};
        }
        auto styleAmp = [&](int bone, float q[4]) {
            auto it = meanRot.find(bone);
            if (it == meanRot.end()) return;
            const float* m = it->second.data();
            const float mc[4] = {-m[0], -m[1], -m[2], m[3]};
            float d[4];
            mul(q, mc, d);  // q = d * mean
            if (d[3] < 0)
                for (float& c : d) c = -c;
            const float half = std::acos(std::clamp(d[3], -1.0f, 1.0f));
            const float sn = std::sin(half);
            if (sn < 1e-6f) return;
            const float h2 = half * ampOf(k.boneNames[(size_t)bone]);
            const float f = std::sin(h2) / sn;
            const float ds[4] = {d[0] * f, d[1] * f, d[2] * f, std::cos(h2)};
            mul(ds, m, q);
        };
        auto stylePose = [&](int bone, float q[4]) {
            if (style == 0.0f) return;
            const std::string& nm = k.boneNames[(size_t)bone];
            const bool left = nm.rfind("mixamorig:Left", 0) == 0;
            float a = 0.0f;  // about +Z (forward): + moves a hanging limb toward +X
            if (nm == "mixamorig:LeftUpLeg" || nm == "mixamorig:RightUpLeg")
                // in: toward the midline - a standing stance narrows more
                // than a step can (the walk's feet would cross)
                a = ((locomotion ? 2.5f : 5.0f) * fem - 1.5f * mas) * deg;
            else if (nm == "mixamorig:LeftArm" || nm == "mixamorig:RightArm")
                a = (5.0f * fem - 4.0f * mas) * deg;  // in: elbows to the body
            else
                return;
            if (left) a = -a;  // the left limb hangs at +X: inward is -X
            float d[3];
            if (!armDir(bone, q, d)) return;
            a *= hangOf(d);
            const float r[4] = {0.0f, 0.0f, std::sin(a * 0.5f), std::cos(a * 0.5f)};
            qmul(r, q);
        };
        // the hips' sideways shift, about its mean, follows the sway
        float hipsMeanX = 0.0f;
        const float hipsSway = locomotion ? 1.0f + 0.5f * fem - 0.2f * mas : 1.0f;
        if (hipsSway != 1.0f) {
            for (int i = 0; i < clip->frames; ++i) hipsMeanX += clip->hips[i * 3];
            hipsMeanX /= (float)std::max(1, clip->frames);
        }
        // Hands out of the body. The clips were captured on a slim mannequin;
        // on wide hips (and with the feminine style's elbows in) a walking
        // hand swung through the hip and thigh. Per key: the wrist and the
        // knuckles by forward kinematics of the FINAL rotations, in the
        // hips' frame, against this body's own width at that height and
        // depth (its rest vertices, arms left out); a point inside, or
        // closer than a hand's half-thickness, turns the upper arm out by
        // just enough - smoothed over neighbouring keys.
        std::map<int, std::vector<float>> armOut;
        {
            std::vector<char> armBone((size_t)k.bones, 0);
            for (int j = 0; j < k.bones; ++j)
                for (int a = j; a >= 0; a = k.parent[a])
                    if (k.boneNames[(size_t)a] == "mixamorig:LeftShoulder" ||
                        k.boneNames[(size_t)a] == "mixamorig:RightShoulder")
                        armBone[(size_t)j] = 1;
            const float binH = 0.02f;
            std::map<int, std::vector<std::array<float, 2>>> bins;  // y bin -> (z, x)
            for (int v = 0; v < b.verts; ++v) {
                float onArm = 0.0f;
                for (int i = 0; i < 4; ++i) {
                    const int j = b.joints[(size_t)v * 4 + i];
                    if (j >= 0 && j < k.bones && armBone[(size_t)j]) onArm += b.weights[(size_t)v * 4 + i];
                }
                if (onArm > 0.2f) continue;
                const float* q = &pos[(size_t)v * 3];
                bins[(int)std::floor(q[1] / binH)].push_back({q[2], q[0]});
            }
            // the body's extent toward one side (+1 / -1) at a height and depth
            auto extent = [&](float y, float z, float side) {
                float best = 0.0f;
                const int yb = (int)std::floor(y / binH);
                for (int dy = -1; dy <= 1; ++dy) {
                    auto it = bins.find(yb + dy);
                    if (it == bins.end()) continue;
                    for (const auto& zx : it->second)
                        if (std::fabs(zx[0] - z) < 0.06f) best = std::max(best, zx[1] * side);
                }
                return best;
            };
            auto finalLocal = [&](int j, float t, float q[4]) {
                sampleRot(j, t, q);
                relaxShoulder(j, q);
                styleAmp(j, q);
                posture(j, q);
                stylePose(j, q);
            };
            int root = -1;
            for (int j = 0; j < k.bones; ++j)
                if (k.parent[j] < 0) root = j;
            for (const char* side : {"Left", "Right"}) {
                const std::string pre = std::string("mixamorig:") + side;
                int arm = -1, hand = -1, knuckle = -1;
                for (int j = 0; j < k.bones; ++j) {
                    const std::string& nm = k.boneNames[(size_t)j];
                    if (nm == pre + "Arm") arm = j;
                    if (nm == pre + "Hand") hand = j;
                    if (nm == pre + "HandMiddle1") knuckle = j;
                }
                if (arm < 0 || hand < 0 || root < 0) continue;
                const float sgn = side[0] == 'L' ? 1.0f : -1.0f;  // the left side is +X
                std::vector<float> th((size_t)keys, 0.0f);
                for (int i = 0; i < keys; ++i) {
                    // FK in the hips' frame: the root's rotation left out
                    std::map<int, std::pair<std::array<float, 4>, std::array<float, 3>>> w;
                    std::function<void(int)> fk = [&](int j) {
                        if (w.count(j)) return;
                        const int pa = k.parent[j];
                        if (pa < 0) {
                            w[j] = {{0, 0, 0, 1}, {heads[(size_t)j * 3], heads[(size_t)j * 3 + 1], heads[(size_t)j * 3 + 2]}};
                            return;
                        }
                        fk(pa);
                        const auto& [pq, pp] = w[pa];
                        float q[4], wq[4], off[3], r[3];
                        finalLocal(j, times[i], q);
                        std::copy(q, q + 4, wq);
                        qmul(pq.data(), wq);
                        for (int c = 0; c < 3; ++c) off[c] = heads[(size_t)j * 3 + c] - heads[(size_t)pa * 3 + c];
                        rotv(pq.data(), off, r);
                        w[j] = {{wq[0], wq[1], wq[2], wq[3]}, {pp[0] + r[0], pp[1] + r[1], pp[2] + r[2]}};
                    };
                    fk(hand);
                    if (knuckle >= 0) fk(knuckle);
                    const auto& ap = w[arm].second;
                    float need = 0.0f;
                    for (int pt : {hand, knuckle}) {
                        if (pt < 0) continue;
                        const auto& pp = w[pt].second;
                        const float x = pp[0] * sgn;
                        if (x <= 0.0f) continue;  // across the midline: not a hip collision
                        const float clear = pt == hand ? 0.035f : 0.025f;
                        const float deficit = extent(pp[1], pp[2], sgn) + clear - x;
                        if (deficit <= 0.0f) continue;
                        const float L = std::max(0.15f, std::hypot(pp[0] - ap[0], pp[1] - ap[1]));
                        need = std::max(need, std::asin(std::min(1.0f, deficit / L)));
                    }
                    th[(size_t)i] = need;
                }
                // a key's neighbours share its correction (dilate one key, then blur)
                std::vector<float> d(th);
                for (int i = 0; i < keys; ++i)
                    d[(size_t)i] = std::max({th[(size_t)i], th[(size_t)std::max(0, i - 1)], th[(size_t)std::min(keys - 1, i + 1)]});
                for (int i = 0; i < keys; ++i)
                    th[(size_t)i] = 0.25f * d[(size_t)std::max(0, i - 1)] + 0.5f * d[(size_t)i] +
                                    0.25f * d[(size_t)std::min(keys - 1, i + 1)];
                bool any = false;
                for (float v : th) any |= v > 0.0f;
                if (any) armOut[arm] = std::move(th);
            }
        }
        for (int b = 0; b < k.bones; ++b) {
            glbparser::SkelChannel ch;
            ch.node = b;
            ch.path = 1;
            ch.times = times;
            bool moves = false;
            float prev[4] = {0, 0, 0, 1};
            // The rig curls the index finger on its own chain and the other
            // three on one (Middle). Curled differently, the skin where the
            // two chains' weights meet shears into ragged fingers; so both
            // take the average of the two (the hand closes as one, like a
            // PS2 hand would), and every finger - thumb too - curls 60% less:
            // the 1600-vertex hand cannot take a full fist.
            int partner = -1;
            const bool finger = k.boneNames[(size_t)b].find("Hand") != std::string::npos &&
                                k.boneNames[(size_t)b].find("Hand") + 4 < k.boneNames[(size_t)b].size();
            {
                const std::string& nm = k.boneNames[(size_t)b];
                const size_t at = nm.find("HandIndex");
                const size_t am = nm.find("HandMiddle");
                std::string other;
                if (at != std::string::npos) other = nm.substr(0, at) + "HandMiddle" + nm.substr(at + 9);
                if (am != std::string::npos) other = nm.substr(0, am) + "HandIndex" + nm.substr(am + 10);
                if (!other.empty())
                    for (int j = 0; j < k.bones; ++j)
                        if (k.boneNames[(size_t)j] == other) partner = j;
            }
            for (int i = 0; i < keys; ++i) {
                float q[4];
                sampleRot(b, times[i], q);
                relaxShoulder(b, q);
                styleAmp(b, q);
                posture(b, q);
                stylePose(b, q);
                if (auto ao = armOut.find(b); ao != armOut.end()) {
                    // out: toward +X for the left arm, -X for the right
                    const float a = ao->second[(size_t)i] *
                                    (k.boneNames[(size_t)b].rfind("mixamorig:Left", 0) == 0 ? 1.0f : -1.0f);
                    const float r[4] = {0.0f, 0.0f, std::sin(a * 0.5f), std::cos(a * 0.5f)};
                    qmul(r, q);
                }
                if (finger) {
                    float l = 0.0f;
                    if (partner >= 0) {
                        float qp[4], dot = 0.0f;
                        sampleRot(partner, times[i], qp);
                        for (int c = 0; c < 4; ++c) dot += q[c] * qp[c];
                        if (dot < 0)
                            for (float& c : qp) c = -c;
                        for (int c = 0; c < 4; ++c) q[c] = (q[c] + qp[c]) * 0.5f;
                    }
                    if (q[3] < 0)  // the rest pose's hemisphere, for the damping below
                        for (float& c : q) c = -c;
                    for (int c = 0; c < 4; ++c) {
                        q[c] += ((c == 3 ? 1.0f : 0.0f) - q[c]) * 0.6f;  // toward the rest pose
                        l += q[c] * q[c];
                    }
                    l = std::sqrt(l);
                    for (int c = 0; c < 4; ++c) q[c] = l > 1e-9f ? q[c] / l : (c == 3);
                }
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
                    float o = clip->hips[i0 * 3 + a] + (clip->hips[i1 * 3 + a] - clip->hips[i0 * 3 + a]) * u;
                    if (a == 0) o = hipsMeanX + (o - hipsMeanX) * hipsSway;
                    ch.values.push_back(out.nodes[0].t[a] + o * hipsScale);
                }
            }
            sc.channels.push_back(std::move(ch));
        }
        if (skirtBones[0] >= 0) bakeSkirt(sc);
        out.clips.push_back(std::move(sc));
    }
    return true;
}

std::vector<bool> partsShownAsBuilt(const std::vector<std::string>& materials) {
    bool hat = false, twins = false;
    for (const std::string& m : materials) {
        hat |= m.find(":optd-head-") != std::string::npos;
        twins |= m.find(":opth-") != std::string::npos;
    }
    const bool underHat = hat && twins;
    std::vector<bool> shown(materials.size(), true);
    for (size_t i = 0; i < materials.size(); ++i) {
        const std::string& m = materials[i];
        if (m.find(":opth-") != std::string::npos) {
            // the twin of the hairstyle worn as built
            shown[i] = false;
            if (underHat) {
                const std::string plain = ":optd-" + m.substr(m.find(":opth-") + 6);
                for (const std::string& o : materials)
                    if (o.find(plain) != std::string::npos && o.size() - o.find(plain) == plain.size())
                        shown[i] = true;
            }
        } else if (m.find(":opt-") != std::string::npos) {
            shown[i] = false;
        } else if (underHat && m.find(":optd-hair-") != std::string::npos) {
            shown[i] = false;
        }
    }
    return shown;
}

void setAssetRoot(const std::string& dir) { g_assetRoot = dir; }

bool exportReferenceBodies(const std::string& dir, std::string& error) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(dir, ec);
    for (int bi = 0; bi < (int)kit().bodies.size(); ++bi) {
        glbparser::Skel s;
        std::vector<std::string> w;
        const Params ref = referenceParams(bi);
        if (!build(ref, s, w, error)) return false;
        // the standard bodies keep their short names
        std::string name = ref.name;
        if (name == "reference-standard-female") name = "reference-female";
        if (name == "reference-standard-male") name = "reference-male";
        const fs::path out = fs::path(dir) / (name + ".glb");
        if (!gltfwrite::writeGlbFile(out.string(), s, "TyraX Character Generator reference body", error))
            return false;
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
      << ", \"dimorphism\": " << num(p.dimorphism) << ",\n";
    // written only when moved: older recipes stay byte-identical
    if (p.detail != 1) o << "  \"detail\": " << p.detail << ",\n";  // only when not standard
    if (p.breastSize != 0.5f || p.breastFirmness != 0.5f)
        o << "  \"breastSize\": " << num(p.breastSize) << ", \"breastFirmness\": "
          << num(p.breastFirmness) << ",\n";
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
    o << "],\n";
    if (!p.customHair.empty())
        o << "  \"customHair\": \"" << json::escape(p.customHair) << "\", \"customHairTexture\": \""
          << json::escape(p.customHairTexture) << "\",\n";
    if (!p.customWear.empty()) {  // only when there are any
        o << "  \"customWear\": [";
        for (size_t i = 0; i < p.customWear.size(); ++i) {
            const Params::CustomWear& cw = p.customWear[i];
            o << (i ? ", " : "") << "{\"mesh\": \"" << json::escape(cw.mesh) << "\", \"texture\": \""
              << json::escape(cw.texture) << "\", \"slot\": \"" << json::escape(cw.slot)
              << "\", \"color\": " << rgb(cw.color) << "}";
        }
        o << "],\n";
    }
    if (p.bodyChoice) o << "  \"bodyChoice\": true,\n";  // only when set
    if (!p.options.empty()) {  // written only when used: older sidecars stay byte-identical
        o << "  \"options\": [";
        for (size_t i = 0; i < p.options.size(); ++i)
            o << (i ? ", " : "") << "\"" << json::escape(p.options[i]) << "\"";
        o << "],\n";
    }
    o << "  \"defaultClips\": " << (p.defaultClips ? "true" : "false") << ", \"clips\": [";
    for (size_t i = 0; i < p.clips.size(); ++i)
        o << (i ? ", " : "") << "\"" << json::escape(p.clips[i]) << "\"";
    o << "],\n  \"animFps\": " << num(p.animFps) << ",\n";
    if (!p.motionStyleAuto) o << "  \"motionStyle\": " << num(p.motionStyle) << ",\n";  // only when set
    o << "  \"animSource\": \""
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
    f("dimorphism", d.dimorphism);
    f("breastSize", d.breastSize);
    i("detail", d.detail);
    d.detail = std::clamp(d.detail, 0, 2);
    f("breastFirmness", d.breastFirmness);
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
    if (const json::Value* x = v.find("customHair")) d.customHair = x->stringOr("");
    if (const json::Value* x = v.find("customHairTexture")) d.customHairTexture = x->stringOr("");
    if (const json::Value* cws = v.find("customWear"))
        for (const json::Value& e : cws->arr) {
            Params::CustomWear cw;
            if (const json::Value* x = e.find("mesh")) cw.mesh = x->stringOr("");
            if (const json::Value* x = e.find("texture")) cw.texture = x->stringOr("");
            if (const json::Value* x = e.find("slot")) cw.slot = x->stringOr("top");
            cw.color = rgbOf(e.find("color"), cw.color);
            if (!cw.mesh.empty()) d.customWear.push_back(cw);
        }
    if (const json::Value* x = v.find("bodyChoice")) d.bodyChoice = x->boolOr(false);
    if (const json::Value* o = v.find("options"))
        for (const json::Value& x : o->arr)
            if (!x.stringOr("").empty()) d.options.push_back(x.stringOr(""));
    if (const json::Value* x = v.find("defaultClips")) d.defaultClips = x->boolOr(true);
    if (const json::Value* c = v.find("clips"))
        for (const json::Value& s : c->arr) d.clips.push_back(s.stringOr(""));
    f("animFps", d.animFps);
    if (const json::Value* x = v.find("motionStyle")) {
        d.motionStyleAuto = false;
        d.motionStyle = std::clamp((float)x->numberOr(0.0), -1.0f, 1.0f);
    }
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

Params paletteVariant(const Params& base, unsigned seed) {
    uint32_t s = seed * 2246822519u + 0x165667b1u;
    auto rnd = [&] {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return (s & 0xffffff) / (float)0x1000000;
    };
    Params v = base;
    v.skinTone = std::clamp(base.skinTone + (rnd() - 0.5f) * 1.0f, -1.0f, 1.0f);
    v.skinWarmth = std::clamp(base.skinWarmth + (rnd() - 0.5f) * 0.8f, -1.0f, 1.0f);
    static const Rgb hairs[] = {{0.06f, 0.05f, 0.05f}, {0.18f, 0.11f, 0.07f},
                                {0.35f, 0.22f, 0.12f}, {0.62f, 0.45f, 0.25f},
                                {0.85f, 0.70f, 0.45f}, {0.55f, 0.22f, 0.10f}};
    if (base.age < 0.85f) v.hairColor = hairs[(int)(rnd() * 6) % 6];
    static const Rgb eyes[] = {{0.33f, 0.22f, 0.12f}, {0.18f, 0.12f, 0.07f},
                               {0.25f, 0.42f, 0.62f}, {0.30f, 0.45f, 0.30f}};
    v.eyeColor = eyes[(int)(rnd() * 4) % 4];
    // Every item gets a dye - a variant left "as made" would look like its
    // base. Earthy, believable street colours, not a rainbow.
    static const Rgb pal[] = {{0.85f, 0.85f, 0.82f}, {0.15f, 0.15f, 0.18f},
                              {0.25f, 0.32f, 0.55f}, {0.55f, 0.18f, 0.16f},
                              {0.30f, 0.42f, 0.28f}, {0.62f, 0.55f, 0.40f},
                              {0.40f, 0.40f, 0.42f}, {0.70f, 0.55f, 0.20f},
                              {0.45f, 0.30f, 0.50f}, {0.20f, 0.45f, 0.55f},
                              {0.48f, 0.33f, 0.22f}, {0.72f, 0.70f, 0.60f}};
    for (Wear& w : v.outfit) {
        w.color = pal[(int)(rnd() * 12) % 12];
        if (w.pattern > 0) w.color2 = pal[(int)(rnd() * 12) % 12];
    }
    if (base.lipstick > 0.0f) v.lipColor = Rgb{0.45f + rnd() * 0.3f, 0.08f + rnd() * 0.1f, 0.12f + rnd() * 0.1f};
    return v;
}

bool writeVariantTextures(const Params& variant, const std::string& glbPath, int k,
                          std::string& error) {
    namespace fs = std::filesystem;
    glbparser::Skel skel;
    std::vector<std::string> warnings;
    if (!build(variant, skel, warnings, error)) return false;
    const fs::path glb(glbPath);
    for (const glbparser::Image& img : skel.images) {
        std::string name = img.name;
        if (const size_t dot = name.rfind('.'); dot != std::string::npos) name.resize(dot);
        const fs::path out = glb.parent_path() /
                             (glb.stem().string() + "_" + name + ".v" + std::to_string(k) + ".png");
        std::ofstream f(out, std::ios::binary | std::ios::trunc);
        if (!f.write((const char*)img.png.data(), (std::streamsize)img.png.size())) {
            error = "Could not write " + out.generic_string();
            return false;
        }
    }
    return true;
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
    p.breastSize = around(0.5f, 0.35f);
    p.breastFirmness = around(0.55f, 0.3f);
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
