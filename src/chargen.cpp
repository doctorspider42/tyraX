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
};

// One game body. Body 0 is MakeHuman's female1605 proxy, body 1 (if present)
// male1591: one topology cannot serve both - the female one's breast loops
// give a man a bust. Every chunk of body N is named with its prefix ("" or
// "m/"), images included ("img/m/skin/...").
struct Body {
    std::string prefix;
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
        for (const json::Value& v : bl.arr)
            k.bodies.push_back(Body{v.find("prefix") ? v.find("prefix")->stringOr("") : ""});
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
           skinTone == o.skinTone &&
           skinWarmth == o.skinWarmth && aging == o.aging && brows == o.brows &&
           browDensity == o.browDensity && lashes == o.lashes && hairColor == o.hairColor &&
           eyeColor == o.eyeColor && stubble == o.stubble && lipstick == o.lipstick &&
           lipColor == o.lipColor && eyeShadow == o.eyeShadow &&
           eyeShadowColor == o.eyeShadowColor && blush == o.blush &&
           textureSize == o.textureSize && outfit == o.outfit && hair == o.hair &&
           options == o.options &&
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
    // Body 0 is the female topology, body 1 the male one: past the middle of
    // the gender slider the male mesh takes over (the targets move both the
    // same way, so the shape is continuous; only the edge loops change).
    const int bi = (k.bodies.size() > 1 && p.gender >= 0.5f) ? 1 : 0;
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
            if (!g || !optionSlot(g->item.slot) || g->kind != "mesh") {
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
        for (size_t gi = 0; gi < worn.size(); ++gi) {
            if (isOpt[gi]) continue;
            const GarmentData* g = worn[gi];
            std::shared_ptr<std::vector<uint8_t>> kb;
            const uint8_t* paint = L("g/" + g->item.id + "/body", kb);
            if (!paint) continue;
            for (size_t i = 0; i < n; ++i) {
                const float pa = px01(paint, i, 3);
                if (pa <= 0.0f) continue;
                float c[3] = {px01(paint, i, 0), px01(paint, i, 1), px01(paint, i, 2)};
                if (g->item.dyeable) dye(c, 1.0f, g->luma, all[gi].color, g->cutout ? 0.55f : 1.0f);
                for (int q = 0; q < 3; ++q) cv.rgb[i * 3 + q] += (c[q] - cv.rgb[i * 3 + q]) * pa;
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
    auto emitItem = [&](size_t gi, glbparser::SkelPart& part, std::vector<uint8_t>& atlas, int A,
                        int cx, int cy, int cw, int ch) {
        const GarmentData* g = worn[gi];
        const GarmentBody& gb = g->body[bi];
        // Geometry, riding the body.
        const size_t gv = gb.bindTri.size();
        std::vector<float> gp(gv * 3), gn;
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
            for (int a = 0; a < 3; ++a)
                gp[v * 3 + a] = s[a] + (nn[a] * off[0] + u[a] * off[1] + w[a] * off[2]) * scale;
        }
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
                     cw, ch);
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
    for (size_t gi = 0; gi < worn.size(); ++gi) {
        if (!isOpt[gi] || worn[gi]->tri.empty()) continue;
        // Half the atlas size: an option is one item, not a grid of them.
        const int A = std::max(32, texSize / 2);
        std::vector<uint8_t> tex((size_t)A * A * 4, 0);
        glbparser::SkelPart part;
        emitItem(gi, part, tex, A, 0, 0, A, A);
        const std::string tag = std::string(isOpt[gi] == 2 ? "optd-" : "opt-") +
                                worn[gi]->item.slot + "-" + worn[gi]->item.id;
        part.material = std::string(worn[gi]->cutout ? "hair:" : "cloth:") + tag;
        glbparser::Image img;
        img.name = tag;
        img.png = encodePng(tex, A, A);
        part.image = (int)out.images.size();
        out.images.push_back(std::move(img));
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
      << ", \"dimorphism\": " << num(p.dimorphism) << ",\n";
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
    if (!p.options.empty()) {  // written only when used: older sidecars stay byte-identical
        o << "  \"options\": [";
        for (size_t i = 0; i < p.options.size(); ++i)
            o << (i ? ", " : "") << "\"" << json::escape(p.options[i]) << "\"";
        o << "],\n";
    }
    o << "  \"defaultClips\": " << (p.defaultClips ? "true" : "false") << ", \"clips\": [";
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
    f("dimorphism", d.dimorphism);
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
    if (const json::Value* o = v.find("options"))
        for (const json::Value& x : o->arr)
            if (!x.stringOr("").empty()) d.options.push_back(x.stringOr(""));
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
