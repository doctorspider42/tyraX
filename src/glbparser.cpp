#include "glbparser.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <map>

#include <stb_image.h>        // implementation lives in app.cpp
#include <stb_image_write.h>  // implementation lives in menubake.cpp

#include "json.hpp"

namespace glbparser {
namespace {

// ---------------------------------------------------------------------------
// Small column-major 4x4 matrix / quaternion toolkit (glTF conventions).

struct M16 {
    float m[16];
};

M16 identity() {
    M16 r = {};
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
    return r;
}

M16 mul(const M16& a, const M16& b) {  // a * b, column-major
    M16 r;
    for (int c = 0; c < 4; ++c)
        for (int row = 0; row < 4; ++row) {
            float acc = 0.0f;
            for (int k = 0; k < 4; ++k) acc += a.m[k * 4 + row] * b.m[c * 4 + k];
            r.m[c * 4 + row] = acc;
        }
    return r;
}

// T * R * S from a glTF node (rotation is an x,y,z,w quaternion).
M16 fromTrs(const float* t, const float* q, const float* s) {
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    const float x2 = x + x, y2 = y + y, z2 = z + z;
    const float xx = x * x2, xy = x * y2, xz = x * z2;
    const float yy = y * y2, yz = y * z2, zz = z * z2;
    const float wx = w * x2, wy = w * y2, wz = w * z2;
    M16 r;
    r.m[0] = (1.0f - (yy + zz)) * s[0];
    r.m[1] = (xy + wz) * s[0];
    r.m[2] = (xz - wy) * s[0];
    r.m[3] = 0.0f;
    r.m[4] = (xy - wz) * s[1];
    r.m[5] = (1.0f - (xx + zz)) * s[1];
    r.m[6] = (yz + wx) * s[1];
    r.m[7] = 0.0f;
    r.m[8] = (xz + wy) * s[2];
    r.m[9] = (yz - wx) * s[2];
    r.m[10] = (1.0f - (xx + yy)) * s[2];
    r.m[11] = 0.0f;
    r.m[12] = t[0];
    r.m[13] = t[1];
    r.m[14] = t[2];
    r.m[15] = 1.0f;
    return r;
}

void slerp(const float* a, const float* bIn, float t, float* out) {
    float b[4] = {bIn[0], bIn[1], bIn[2], bIn[3]};
    float dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
    if (dot < 0.0f) {  // take the short arc
        dot = -dot;
        for (int i = 0; i < 4; ++i) b[i] = -b[i];
    }
    float wa, wb;
    if (dot > 0.9995f) {  // nearly parallel - lerp avoids a degenerate sin
        wa = 1.0f - t;
        wb = t;
    } else {
        const float theta = std::acos(std::min(dot, 1.0f));
        const float sinTheta = std::sin(theta);
        wa = std::sin((1.0f - t) * theta) / sinTheta;
        wb = std::sin(t * theta) / sinTheta;
    }
    for (int i = 0; i < 4; ++i) out[i] = wa * a[i] + wb * b[i];
    const float len = std::sqrt(out[0] * out[0] + out[1] * out[1] +
                                out[2] * out[2] + out[3] * out[3]);
    if (len > 1e-6f)
        for (int i = 0; i < 4; ++i) out[i] /= len;
    else
        out[3] = 1.0f;
}

// ---------------------------------------------------------------------------
// glTF document access

uint32_t le32(const unsigned char* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

struct Doc {
    json::Value root;
    const unsigned char* bin = nullptr;  // BIN chunk (all buffers must live here)
    size_t binSize = 0;

    const json::Value* array(const char* key) const {
        const json::Value* v = root.find(key);
        return v && v->type == json::Value::Type::Array ? v : nullptr;
    }
    const json::Value* at(const char* key, int index) const {
        const json::Value* arr = array(key);
        if (!arr || index < 0 || index >= (int)arr->arr.size()) return nullptr;
        return &arr->arr[index];
    }
};

int intOr(const json::Value* obj, const char* key, int fallback) {
    if (!obj) return fallback;
    const json::Value* v = obj->find(key);
    return v && v->type == json::Value::Type::Number ? (int)v->number : fallback;
}

// Raw [byte pointer, stride] view of an accessor. Sparse accessors and
// accessors outside the GLB BIN chunk are rejected (not in Blender's output).
struct AccessorView {
    const unsigned char* data = nullptr;
    size_t stride = 0;   // bytes between elements
    int count = 0;
    int componentType = 0;  // 5120..5126
    int comps = 0;          // SCALAR 1 .. MAT4 16
    bool normalized = false;
    bool ok() const { return data != nullptr; }
};

int componentSize(int componentType) {
    switch (componentType) {
        case 5120: case 5121: return 1;
        case 5122: case 5123: return 2;
        case 5125: case 5126: return 4;
        default: return 0;
    }
}

int typeComps(const std::string& t) {
    if (t == "SCALAR") return 1;
    if (t == "VEC2") return 2;
    if (t == "VEC3") return 3;
    if (t == "VEC4") return 4;
    if (t == "MAT4") return 16;
    return 0;
}

AccessorView accessor(const Doc& doc, int index) {
    AccessorView out;
    const json::Value* acc = doc.at("accessors", index);
    if (!acc) return out;
    if (acc->find("sparse")) return out;
    const json::Value* typeV = acc->find("type");
    out.componentType = intOr(acc, "componentType", 0);
    out.comps = typeV ? typeComps(typeV->stringOr("")) : 0;
    out.count = intOr(acc, "count", 0);
    const json::Value* normV = acc->find("normalized");
    out.normalized = normV && normV->boolOr(false);
    const int compSize = componentSize(out.componentType);
    if (!compSize || !out.comps || out.count <= 0) return out;

    const json::Value* bv = doc.at("bufferViews", intOr(acc, "bufferView", -1));
    if (!bv) return out;
    if (intOr(bv, "buffer", 0) != 0) return out;  // GLB BIN is buffer 0
    const size_t bvOffset = (size_t)intOr(bv, "byteOffset", 0);
    const size_t bvLength = (size_t)intOr(bv, "byteLength", 0);
    const size_t accOffset = (size_t)intOr(acc, "byteOffset", 0);
    size_t stride = (size_t)intOr(bv, "byteStride", 0);
    const size_t tight = (size_t)compSize * out.comps;
    if (stride == 0) stride = tight;

    if (!doc.bin) return out;
    const size_t need = accOffset + (size_t)(out.count - 1) * stride + tight;
    if (bvOffset + bvLength > doc.binSize || need > bvLength) return out;

    out.data = doc.bin + bvOffset + accOffset;
    out.stride = stride;
    return out;
}

float readComponent(const unsigned char* p, int componentType, bool normalized) {
    switch (componentType) {
        case 5126: {
            float f;
            std::memcpy(&f, p, 4);
            return f;
        }
        case 5121: return normalized ? *p / 255.0f : (float)*p;
        case 5120: {
            const int8_t v = (int8_t)*p;
            return normalized ? std::max(v / 127.0f, -1.0f) : (float)v;
        }
        case 5123: {
            uint16_t v;
            std::memcpy(&v, p, 2);
            return normalized ? v / 65535.0f : (float)v;
        }
        case 5122: {
            int16_t v;
            std::memcpy(&v, p, 2);
            return normalized ? std::max(v / 32767.0f, -1.0f) : (float)v;
        }
        case 5125: {
            uint32_t v;
            std::memcpy(&v, p, 4);
            return (float)v;
        }
        default: return 0.0f;
    }
}

// Reads a whole accessor as floats (component-normalization applied).
std::vector<float> readFloats(const Doc& doc, int index) {
    std::vector<float> out;
    const AccessorView v = accessor(doc, index);
    if (!v.ok()) return out;
    const int compSize = componentSize(v.componentType);
    out.reserve((size_t)v.count * v.comps);
    for (int i = 0; i < v.count; ++i) {
        const unsigned char* e = v.data + (size_t)i * v.stride;
        for (int c = 0; c < v.comps; ++c)
            out.push_back(readComponent(e + (size_t)c * compSize, v.componentType,
                                        v.normalized));
    }
    return out;
}

std::vector<uint32_t> readIndices(const Doc& doc, int index) {
    std::vector<uint32_t> out;
    const AccessorView v = accessor(doc, index);
    if (!v.ok() || v.comps != 1) return out;
    out.reserve(v.count);
    for (int i = 0; i < v.count; ++i) {
        const unsigned char* e = v.data + (size_t)i * v.stride;
        if (v.componentType == 5121) out.push_back(*e);
        else if (v.componentType == 5123) {
            uint16_t x;
            std::memcpy(&x, e, 2);
            out.push_back(x);
        } else if (v.componentType == 5125) {
            uint32_t x;
            std::memcpy(&x, e, 4);
            out.push_back(x);
        } else
            return {};
    }
    return out;
}

// ---------------------------------------------------------------------------
// Scene graph

struct Node {
    float t[3] = {0, 0, 0};
    float r[4] = {0, 0, 0, 1};
    float s[3] = {1, 1, 1};
    bool hasMatrix = false;
    M16 matrix = identity();
    int mesh = -1, skin = -1;
    std::vector<int> children;
};

// One animation channel with its keyframes decoded up front.
struct Channel {
    int node = -1;
    int path = 0;  // 0 translation, 1 rotation, 2 scale
    int interpolation = 0;  // 0 linear, 1 step, 2 cubicspline
    std::vector<float> times;
    std::vector<float> values;  // comps per key (cubic: 3 * comps per key)
};

struct ClipSrc {
    std::string name;
    std::vector<Channel> channels;
    float start = 0.0f, end = 0.0f;
};

// Sample one channel at time t into out[comps].
void sampleChannel(const Channel& ch, float t, float* out) {
    const int comps = ch.path == 1 ? 4 : 3;
    const size_t keyStride = ch.interpolation == 2 ? comps * 3 : comps;
    const size_t valueOff = ch.interpolation == 2 ? comps : 0;  // cubic: mid value
    const size_t n = ch.times.size();
    if (n == 0 || ch.values.size() < n * keyStride) return;

    size_t hi = 0;
    while (hi < n && ch.times[hi] < t) ++hi;
    if (hi == 0) {
        std::memcpy(out, &ch.values[valueOff], comps * sizeof(float));
        return;
    }
    if (hi >= n) {
        std::memcpy(out, &ch.values[(n - 1) * keyStride + valueOff],
                    comps * sizeof(float));
        return;
    }
    const size_t lo = hi - 1;
    const float t0 = ch.times[lo], t1 = ch.times[hi];
    float f = t1 > t0 ? (t - t0) / (t1 - t0) : 0.0f;
    if (ch.interpolation == 1) f = 0.0f;  // STEP holds the left key
    const float* a = &ch.values[lo * keyStride + valueOff];
    const float* b = &ch.values[hi * keyStride + valueOff];
    if (ch.path == 1)
        slerp(a, b, f, out);
    else
        for (int c = 0; c < comps; ++c) out[c] = a[c] + (b[c] - a[c]) * f;
}

std::string sanitizePngName(std::string s) {
    std::string out;
    for (char c : s) {
        if (isalnum((unsigned char)c) || c == '-' || c == '_')
            out += (char)tolower((unsigned char)c);
        else if (c == ' ')
            out += '_';
    }
    if (out.empty()) out = "tex";
    if (out.size() > 40) out.resize(40);
    return out;
}

void appendBytes(std::string& out, const void* p, size_t n) {
    out.append(reinterpret_cast<const char*>(p), n);
}

void appendU32(std::string& out, uint32_t v) { appendBytes(out, &v, 4); }
void appendF32(std::string& out, float v) { appendBytes(out, &v, 4); }

void appendFixedString(std::string& out, const std::string& s, size_t size) {
    char buf[128] = {};
    std::snprintf(buf, std::min(size, sizeof(buf)), "%s", s.c_str());
    out.append(buf, size);
}

void stbWriteToVector(void* context, void* data, int size) {
    auto* vec = static_cast<std::vector<unsigned char>*>(context);
    const auto* p = static_cast<unsigned char*>(data);
    vec->insert(vec->end(), p, p + size);
}

// ---------------------------------------------------------------------------
// Shared .glb parse - everything bake() (morph frames) and parseSkel()
// (skeletal serialization) both need, extracted once per file.

struct Skin {
    std::vector<int> joints;
    std::vector<float> ibm;  // joints * 16
};

// A PrimRef is one glTF primitive bound to the scene node that draws it,
// with its bind-pose attributes decoded up front.
struct PrimRef {
    int node = -1, skin = -1, part = -1;
    std::vector<float> pos, nrm, uv;   // bind pose, indexed
    std::vector<float> joints, weights;
    std::vector<uint32_t> indices;     // expanded triangle order
};

// Per-part (per glTF material) constants; geometry is expanded from prims.
struct PartMeta {
    std::string material;
    float baseColor[4] = {1, 1, 1, 1};
    int image = -1;
};

struct ParsedGlb {
    std::vector<Node> nodes;
    std::vector<int> parent;  // per node, -1 = root
    std::vector<int> order;   // parents-first traversal order
    std::vector<Skin> skins;
    std::vector<PrimRef> prims;
    std::vector<PartMeta> parts;  // prims reference these via PrimRef::part
    std::vector<ClipSrc> clips;
};

// Local matrices composed parents-first into global (scene-space) matrices.
void computeGlobals(const std::vector<Node>& pose, const std::vector<int>& order,
                    const std::vector<int>& parent, std::vector<M16>& globals) {
    globals.resize(pose.size());
    for (int i : order) {
        const Node& n = pose[i];
        const M16 local = n.hasMatrix ? n.matrix : fromTrs(n.t, n.r, n.s);
        globals[i] = parent[i] >= 0 ? mul(globals[parent[i]], local) : local;
    }
}

bool parseGlb(const std::string& path, ParsedGlb& P, std::vector<Image>& images,
              std::vector<std::string>& warnings, std::string& error) {
    // --- GLB container -----------------------------------------------------
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "cannot open file";
        return false;
    }
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)),
                                     std::istreambuf_iterator<char>());
    if (bytes.size() < 20 || le32(bytes.data()) != 0x46546C67u) {
        error = "not a .glb file (missing glTF magic)";
        return false;
    }
    if (le32(bytes.data() + 4) != 2) {
        error = "unsupported glTF version (need 2)";
        return false;
    }

    Doc doc;
    std::string jsonText;
    size_t off = 12;
    while (off + 8 <= bytes.size()) {
        const uint32_t chunkLen = le32(bytes.data() + off);
        const uint32_t chunkType = le32(bytes.data() + off + 4);
        off += 8;
        if (off + chunkLen > bytes.size()) break;
        if (chunkType == 0x4E4F534Au)  // "JSON"
            jsonText.assign(reinterpret_cast<const char*>(bytes.data() + off),
                            chunkLen);
        else if (chunkType == 0x004E4942u) {  // "BIN"
            doc.bin = bytes.data() + off;
            doc.binSize = chunkLen;
        }
        off += chunkLen + (chunkLen % 4 ? 4 - chunkLen % 4 : 0);
    }
    if (jsonText.empty() || !json::parse(jsonText, doc.root)) {
        error = "malformed .glb (bad JSON chunk)";
        return false;
    }

    // --- nodes ---------------------------------------------------------------
    std::vector<Node>& nodes = P.nodes;
    if (const json::Value* arr = doc.array("nodes")) {
        nodes.resize(arr->arr.size());
        for (size_t i = 0; i < arr->arr.size(); ++i) {
            const json::Value& n = arr->arr[i];
            Node& node = nodes[i];
            auto readVec = [&](const char* key, float* dst, int count) {
                const json::Value* v = n.find(key);
                if (!v || v->type != json::Value::Type::Array ||
                    (int)v->arr.size() != count)
                    return;
                for (int c = 0; c < count; ++c) dst[c] = (float)v->arr[c].number;
            };
            readVec("translation", node.t, 3);
            readVec("rotation", node.r, 4);
            readVec("scale", node.s, 3);
            if (const json::Value* m = n.find("matrix");
                m && m->type == json::Value::Type::Array && m->arr.size() == 16) {
                node.hasMatrix = true;
                for (int c = 0; c < 16; ++c) node.matrix.m[c] = (float)m->arr[c].number;
            }
            node.mesh = intOr(&n, "mesh", -1);
            node.skin = intOr(&n, "skin", -1);
            if (const json::Value* ch = n.find("children");
                ch && ch->type == json::Value::Type::Array)
                for (const json::Value& c : ch->arr)
                    node.children.push_back((int)c.number);
        }
    }
    if (nodes.empty()) {
        error = "no nodes in file";
        return false;
    }

    // Roots: nodes of the default scene, or every unparented node.
    P.parent.assign(nodes.size(), -1);
    std::vector<int>& parent = P.parent;
    for (size_t i = 0; i < nodes.size(); ++i)
        for (int c : nodes[i].children)
            if (c >= 0 && c < (int)nodes.size()) parent[c] = (int)i;
    std::vector<int> roots;
    const json::Value* sceneV = doc.at("scenes", intOr(&doc.root, "scene", 0));
    if (sceneV) {
        if (const json::Value* ns = sceneV->find("nodes");
            ns && ns->type == json::Value::Type::Array)
            for (const json::Value& v : ns->arr) roots.push_back((int)v.number);
    }
    if (roots.empty())
        for (size_t i = 0; i < nodes.size(); ++i)
            if (parent[i] < 0) roots.push_back((int)i);

    // Parents-first traversal order for global matrix computation.
    std::vector<int>& order = P.order;
    order.reserve(nodes.size());
    {
        std::vector<int> stack(roots.rbegin(), roots.rend());
        std::vector<bool> seen(nodes.size(), false);
        while (!stack.empty()) {
            const int i = stack.back();
            stack.pop_back();
            if (i < 0 || i >= (int)nodes.size() || seen[i]) continue;
            seen[i] = true;
            order.push_back(i);
            for (auto it = nodes[i].children.rbegin();
                 it != nodes[i].children.rend(); ++it)
                stack.push_back(*it);
        }
    }

    // --- skins -----------------------------------------------------------
    std::vector<Skin>& skins = P.skins;
    if (const json::Value* arr = doc.array("skins")) {
        skins.resize(arr->arr.size());
        for (size_t i = 0; i < arr->arr.size(); ++i) {
            const json::Value& s = arr->arr[i];
            if (const json::Value* js = s.find("joints");
                js && js->type == json::Value::Type::Array)
                for (const json::Value& j : js->arr)
                    skins[i].joints.push_back((int)j.number);
            skins[i].ibm = readFloats(doc, intOr(&s, "inverseBindMatrices", -1));
            if (skins[i].ibm.size() != skins[i].joints.size() * 16) {
                skins[i].ibm.assign(skins[i].joints.size() * 16, 0.0f);
                for (size_t j = 0; j < skins[i].joints.size(); ++j)
                    for (int d = 0; d < 4; ++d) skins[i].ibm[j * 16 + d * 5] = 1.0f;
            }
        }
    }

    // --- materials/images ---------------------------------------------------
    // Baked::images is filled lazily with the images actually referenced.
    std::map<int, int> imageSlot;  // glTF image index -> Baked::images index
    auto imageFor = [&](int materialIndex) -> int {
        const json::Value* mat = doc.at("materials", materialIndex);
        if (!mat) return -1;
        const json::Value* pbr = mat->find("pbrMetallicRoughness");
        const json::Value* texRef = pbr ? pbr->find("baseColorTexture") : nullptr;
        if (!texRef) return -1;
        const json::Value* tex = doc.at("textures", intOr(texRef, "index", -1));
        if (!tex) return -1;
        const int imgIndex = intOr(tex, "source", -1);
        if (auto it = imageSlot.find(imgIndex); it != imageSlot.end())
            return it->second;
        const json::Value* img = doc.at("images", imgIndex);
        if (!img) return -1;
        const json::Value* bv = doc.at("bufferViews", intOr(img, "bufferView", -1));
        if (!bv || intOr(bv, "buffer", 0) != 0 || !doc.bin) {
            warnings.push_back("texture image is not embedded - skipped");
            return -1;
        }
        const size_t o = (size_t)intOr(bv, "byteOffset", 0);
        const size_t len = (size_t)intOr(bv, "byteLength", 0);
        if (o + len > doc.binSize) return -1;

        Image baked;
        const std::string mime =
            img->find("mimeType") ? img->find("mimeType")->stringOr("") : "";
        if (mime == "image/png") {
            baked.png.assign(doc.bin + o, doc.bin + o + len);
        } else {
            // Non-PNG (usually JPEG): transcode, the PS2 loader is PNG-only.
            int w = 0, h = 0, comp = 0;
            unsigned char* pixels = stbi_load_from_memory(doc.bin + o, (int)len,
                                                          &w, &h, &comp, 4);
            if (!pixels) {
                warnings.push_back("undecodable embedded texture - skipped");
                return -1;
            }
            stbi_write_png_to_func(stbWriteToVector, &baked.png, w, h, 4, pixels,
                                   w * 4);
            stbi_image_free(pixels);
            warnings.push_back("embedded " + (mime.empty() ? "image" : mime) +
                                   " transcoded to PNG");
        }
        {
            int w = 0, h = 0, comp = 0;
            if (stbi_info_from_memory(baked.png.data(), (int)baked.png.size(), &w,
                                      &h, &comp)) {
                const bool pot = w > 0 && h > 0 && !(w & (w - 1)) && !(h & (h - 1));
                if (!pot)
                    warnings.push_back(
                        "texture " + std::to_string(w) + "x" + std::to_string(h) +
                        " is not power-of-two - the PS2 cannot load it");
            }
        }
        std::string base = img->find("name") ? img->find("name")->stringOr("") : "";
        baked.name = sanitizePngName(base.empty() ? "tex" + std::to_string(imgIndex)
                                                  : base);
        for (const Image& other : images)
            if (other.name == baked.name + ".png") {
                baked.name += "_" + std::to_string(imgIndex);
                break;
            }
        baked.name += ".png";
        images.push_back(std::move(baked));
        imageSlot[imgIndex] = (int)images.size() - 1;
        return imageSlot[imgIndex];
    };

    // --- mesh primitives, expanded to flat triangle lists -------------------
    std::vector<PrimRef>& prims = P.prims;
    std::map<int, int> partOfMaterial;  // glTF material index (-1 ok) -> part

    for (int oi : order) {
        const Node& node = nodes[oi];
        if (node.mesh < 0) continue;
        const json::Value* mesh = doc.at("meshes", node.mesh);
        if (!mesh) continue;
        const json::Value* primsV = mesh->find("primitives");
        if (!primsV || primsV->type != json::Value::Type::Array) continue;
        for (const json::Value& prim : primsV->arr) {
            const int mode = intOr(&prim, "mode", 4);
            if (mode != 4) {
                warnings.push_back("non-triangle primitive skipped");
                continue;
            }
            const json::Value* attrs = prim.find("attributes");
            if (!attrs) continue;
            PrimRef ref;
            ref.node = oi;
            ref.skin = node.skin;
            ref.pos = readFloats(doc, intOr(attrs, "POSITION", -1));
            if (ref.pos.empty()) continue;
            ref.nrm = readFloats(doc, intOr(attrs, "NORMAL", -1));
            ref.uv = readFloats(doc, intOr(attrs, "TEXCOORD_0", -1));
            if (ref.skin >= 0) {
                ref.joints = readFloats(doc, intOr(attrs, "JOINTS_0", -1));
                ref.weights = readFloats(doc, intOr(attrs, "WEIGHTS_0", -1));
                if (ref.joints.size() != ref.pos.size() / 3 * 4 ||
                    ref.weights.size() != ref.pos.size() / 3 * 4)
                    ref.skin = -1;  // malformed skinning data - bake rigid
            }
            const int indicesAcc = intOr(&prim, "indices", -1);
            if (indicesAcc >= 0) {
                ref.indices = readIndices(doc, indicesAcc);
            } else {
                ref.indices.resize(ref.pos.size() / 3);
                for (size_t i = 0; i < ref.indices.size(); ++i)
                    ref.indices[i] = (uint32_t)i;
            }
            ref.indices.resize(ref.indices.size() - ref.indices.size() % 3);
            const uint32_t vertCount = (uint32_t)(ref.pos.size() / 3);
            bool inRange = true;
            for (uint32_t idx : ref.indices) inRange &= idx < vertCount;
            if (!inRange || ref.indices.empty()) continue;

            const int materialIndex = intOr(&prim, "material", -1);
            auto it = partOfMaterial.find(materialIndex);
            if (it == partOfMaterial.end()) {
                PartMeta part;
                const json::Value* mat = doc.at("materials", materialIndex);
                part.material =
                    mat && mat->find("name") ? mat->find("name")->stringOr("") : "";
                if (const json::Value* pbr =
                        mat ? mat->find("pbrMetallicRoughness") : nullptr) {
                    if (const json::Value* bc = pbr->find("baseColorFactor");
                        bc && bc->type == json::Value::Type::Array &&
                        bc->arr.size() == 4)
                        for (int c = 0; c < 4; ++c)
                            part.baseColor[c] = (float)bc->arr[c].number;
                }
                part.image = imageFor(materialIndex);
                P.parts.push_back(std::move(part));
                it = partOfMaterial.emplace(materialIndex, (int)P.parts.size() - 1)
                         .first;
            }
            ref.part = it->second;
            if (P.parts[ref.part].image >= 0 && ref.uv.size() < vertCount * 2) {
                warnings.push_back(
                    "primitive without TEXCOORD_0 uses a textured material");
                ref.uv.assign(vertCount * 2, 0.0f);
            }
            prims.push_back(std::move(ref));
        }
    }
    if (prims.empty()) {
        error = "no triangles found";
        return false;
    }

    // --- animation clips ----------------------------------------------------
    std::vector<ClipSrc>& clipSrcs = P.clips;
    if (const json::Value* arr = doc.array("animations")) {
        for (size_t ai = 0; ai < arr->arr.size(); ++ai) {
            const json::Value& anim = arr->arr[ai];
            ClipSrc clip;
            clip.name =
                anim.find("name") ? anim.find("name")->stringOr("") : "";
            if (clip.name.empty()) clip.name = "clip" + std::to_string(ai);
            const json::Value* samplers = anim.find("samplers");
            const json::Value* channels = anim.find("channels");
            if (!samplers || !channels) continue;
            bool first = true;
            for (const json::Value& chV : channels->arr) {
                const json::Value* target = chV.find("target");
                if (!target) continue;
                const std::string pathStr =
                    target->find("path") ? target->find("path")->stringOr("") : "";
                Channel ch;
                if (pathStr == "translation") ch.path = 0;
                else if (pathStr == "rotation") ch.path = 1;
                else if (pathStr == "scale") ch.path = 2;
                else {
                    if (pathStr == "weights")
                        warnings.push_back(
                            "morph-target channel skipped (not supported)");
                    continue;
                }
                ch.node = intOr(target, "node", -1);
                if (ch.node < 0 || ch.node >= (int)nodes.size()) continue;
                const int si = intOr(&chV, "sampler", -1);
                if (si < 0 || si >= (int)samplers->arr.size()) continue;
                const json::Value& sampler = samplers->arr[si];
                const std::string interp =
                    sampler.find("interpolation")
                        ? sampler.find("interpolation")->stringOr("LINEAR")
                        : "LINEAR";
                ch.interpolation =
                    interp == "STEP" ? 1 : (interp == "CUBICSPLINE" ? 2 : 0);
                ch.times = readFloats(doc, intOr(&sampler, "input", -1));
                ch.values = readFloats(doc, intOr(&sampler, "output", -1));
                if (ch.times.empty() || ch.values.empty()) continue;
                const float lo = ch.times.front(), hi = ch.times.back();
                if (first || lo < clip.start) clip.start = lo;
                if (first || hi > clip.end) clip.end = hi;
                first = false;
                clip.channels.push_back(std::move(ch));
            }
            if (!clip.channels.empty()) clipSrcs.push_back(std::move(clip));
        }
    }

    return true;
}

}  // namespace

bool bake(const std::string& path, float fps, Baked& out, std::string& error) {
    out = Baked();
    out.fps = fps > 1.0f ? fps : 1.0f;

    ParsedGlb P;
    if (!parseGlb(path, P, out.images, out.warnings, error)) return false;

    out.parts.reserve(P.parts.size());
    for (const PartMeta& meta : P.parts) {
        Part part;
        part.material = meta.material;
        std::memcpy(part.baseColor, meta.baseColor, sizeof(part.baseColor));
        part.image = meta.image;
        out.parts.push_back(std::move(part));
    }
    std::vector<PrimRef>& prims = P.prims;
    std::vector<Skin>& skins = P.skins;
    std::vector<ClipSrc>& clipSrcs = P.clips;

    // --- bake ---------------------------------------------------------------
    std::vector<M16> globals(P.nodes.size());
    // Working TRS copies - channels overwrite these per sample.
    std::vector<Node> pose = P.nodes;

    auto appendFrame = [&](bool wantUv) {
        for (PrimRef& ref : prims) {
            Part& part = out.parts[ref.part];
            const size_t vertCount = ref.pos.size() / 3;

            // Per-vertex transforms: skinned = blended joint palette,
            // rigid = the node's global matrix for every vertex.
            std::vector<M16> palette;
            if (ref.skin >= 0 && ref.skin < (int)skins.size()) {
                const Skin& skin = skins[ref.skin];
                palette.resize(skin.joints.size());
                for (size_t j = 0; j < skin.joints.size(); ++j) {
                    M16 ibm;
                    std::memcpy(ibm.m, &skin.ibm[j * 16], sizeof(ibm.m));
                    const int jn = skin.joints[j];
                    palette[j] = jn >= 0 && jn < (int)P.nodes.size()
                                     ? mul(globals[jn], ibm)
                                     : identity();
                }
            }
            const M16 rigid = globals[ref.node];

            // Transform the indexed bind pose once...
            std::vector<float> xp(vertCount * 3), xn(vertCount * 3);
            const bool hasNrm = ref.nrm.size() >= vertCount * 3;
            for (size_t v = 0; v < vertCount; ++v) {
                M16 blended;
                const float* src = &ref.pos[v * 3];
                if (!palette.empty()) {
                    std::memset(blended.m, 0, sizeof(blended.m));
                    const float* w = &ref.weights[v * 4];
                    const float* j = &ref.joints[v * 4];
                    float wsum = w[0] + w[1] + w[2] + w[3];
                    if (wsum < 1e-5f) wsum = 1.0f;
                    for (int k = 0; k < 4; ++k) {
                        const int joint = (int)(j[k] + 0.5f);
                        if (w[k] <= 0.0f || joint < 0 ||
                            joint >= (int)palette.size())
                            continue;
                        const float wk = w[k] / wsum;
                        for (int c = 0; c < 16; ++c)
                            blended.m[c] += palette[joint].m[c] * wk;
                    }
                } else {
                    blended = rigid;
                }
                const float* m = blended.m;
                xp[v * 3 + 0] =
                    m[0] * src[0] + m[4] * src[1] + m[8] * src[2] + m[12];
                xp[v * 3 + 1] =
                    m[1] * src[0] + m[5] * src[1] + m[9] * src[2] + m[13];
                xp[v * 3 + 2] =
                    m[2] * src[0] + m[6] * src[1] + m[10] * src[2] + m[14];
                if (hasNrm) {
                    const float* nn = &ref.nrm[v * 3];
                    float ox = m[0] * nn[0] + m[4] * nn[1] + m[8] * nn[2];
                    float oy = m[1] * nn[0] + m[5] * nn[1] + m[9] * nn[2];
                    float oz = m[2] * nn[0] + m[6] * nn[1] + m[10] * nn[2];
                    const float len = std::sqrt(ox * ox + oy * oy + oz * oz);
                    if (len > 1e-6f) ox /= len, oy /= len, oz /= len;
                    xn[v * 3 + 0] = ox;
                    xn[v * 3 + 1] = oy;
                    xn[v * 3 + 2] = oz;
                }
            }

            // ...then expand through the index list into the part arrays.
            const size_t triCount = ref.indices.size() / 3;
            for (size_t tri = 0; tri < triCount; ++tri) {
                float faceN[3] = {0, 1, 0};
                if (!hasNrm) {  // flat normal from the transformed triangle
                    const float* a = &xp[ref.indices[tri * 3] * 3];
                    const float* b = &xp[ref.indices[tri * 3 + 1] * 3];
                    const float* c = &xp[ref.indices[tri * 3 + 2] * 3];
                    const float e1[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
                    const float e2[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
                    faceN[0] = e1[1] * e2[2] - e1[2] * e2[1];
                    faceN[1] = e1[2] * e2[0] - e1[0] * e2[2];
                    faceN[2] = e1[0] * e2[1] - e1[1] * e2[0];
                    const float len = std::sqrt(faceN[0] * faceN[0] +
                                                faceN[1] * faceN[1] +
                                                faceN[2] * faceN[2]);
                    if (len > 1e-6f)
                        for (float& f : faceN) f /= len;
                }
                for (int k = 0; k < 3; ++k) {
                    const uint32_t idx = ref.indices[tri * 3 + k];
                    part.positions.insert(part.positions.end(),
                                          &xp[idx * 3], &xp[idx * 3] + 3);
                    if (hasNrm)
                        part.normals.insert(part.normals.end(), &xn[idx * 3],
                                            &xn[idx * 3] + 3);
                    else
                        part.normals.insert(part.normals.end(), faceN, faceN + 3);
                    if (wantUv) {
                        const bool hasUv = ref.uv.size() >= (idx + 1) * 2;
                        part.uvs.push_back(hasUv ? ref.uv[idx * 2] : 0.0f);
                        part.uvs.push_back(hasUv ? ref.uv[idx * 2 + 1] : 0.0f);
                    }
                }
            }
        }
    };

    int framesBaked = 0;
    if (clipSrcs.empty()) {
        computeGlobals(pose, P.order, P.parent, globals);
        appendFrame(true);
        out.clips.push_back({"default", 0, 1});
        framesBaked = 1;
    } else {
        for (const ClipSrc& clip : clipSrcs) {
            const float duration = clip.end - clip.start;
            int samples =
                duration <= 0.0f
                    ? 1
                    : (int)std::lround(duration * out.fps) + 1;
            if (duration > 0.0f && samples < 2) samples = 2;
            if (samples > 1024) {
                out.warnings.push_back("clip \"" + clip.name +
                                       "\" truncated to 1024 baked frames");
                samples = 1024;
            }
            for (int f = 0; f < samples; ++f) {
                const float t =
                    samples > 1
                        ? clip.start + duration * ((float)f / (samples - 1))
                        : clip.start;
                pose = P.nodes;
                for (const Channel& ch : clip.channels) {
                    Node& n = pose[ch.node];
                    n.hasMatrix = false;  // animated nodes always compose TRS
                    if (ch.path == 0) sampleChannel(ch, t, n.t);
                    else if (ch.path == 1) sampleChannel(ch, t, n.r);
                    else sampleChannel(ch, t, n.s);
                }
                computeGlobals(pose, P.order, P.parent, globals);
                appendFrame(framesBaked + f == 0);
            }
            std::string name = clip.name;
            for (const Clip& other : out.clips)
                if (other.name == name) {
                    name += "_2";
                    break;
                }
            out.clips.push_back({name, framesBaked, samples});
            framesBaked += samples;
        }
    }
    out.frameCount = framesBaked;

    for (Part& part : out.parts)
        part.vertexCount = framesBaked > 0
                               ? (int)(part.positions.size() / 3 / framesBaked)
                               : 0;
    out.parts.erase(std::remove_if(out.parts.begin(), out.parts.end(),
                                   [](const Part& p) { return p.vertexCount == 0; }),
                    out.parts.end());
    if (out.parts.empty()) {
        error = "no triangles found";
        return false;
    }

    // Frame-0 AABB (used for box collision + viewport framing).
    bool first = true;
    for (const Part& part : out.parts)
        for (int v = 0; v < part.vertexCount; ++v) {
            const float* p = &part.positions[(size_t)v * 3];
            for (int c = 0; c < 3; ++c) {
                if (first || p[c] < out.min[c]) out.min[c] = p[c];
                if (first || p[c] > out.max[c]) out.max[c] = p[c];
            }
            first = false;
        }

    return true;
}

// Layout (little-endian; keep in sync with the engine's tanm_loader.cpp):
//   "TANM" u32(version=1)
//   u32 partCount, u32 frameCount, u32 clipCount, f32 fps
//   f32 min[3], f32 max[3]
//   clipCount * { char name[32]; u32 firstFrame; u32 frameCount; }
//   partCount * {
//     char name[32]; char texture[64]; f32 color[4]; u32 vertexCount;
//     if (texture[0]) f32 uv[vertexCount*2]
//     frameCount * { f32 pos[vertexCount*3]; f32 nrm[vertexCount*3]; }
//   }
std::string writeTanm(const Baked& baked,
                      const std::vector<std::string>& textureNames) {
    std::string out;
    out.reserve(1024 + (size_t)baked.totalVertexCount() * baked.frameCount * 24);
    out += "TANM";
    appendU32(out, 1);
    appendU32(out, (uint32_t)baked.parts.size());
    appendU32(out, (uint32_t)baked.frameCount);
    appendU32(out, (uint32_t)baked.clips.size());
    appendF32(out, baked.fps);
    for (int c = 0; c < 3; ++c) appendF32(out, baked.min[c]);
    for (int c = 0; c < 3; ++c) appendF32(out, baked.max[c]);
    for (const Clip& clip : baked.clips) {
        appendFixedString(out, clip.name, 32);
        appendU32(out, (uint32_t)clip.firstFrame);
        appendU32(out, (uint32_t)clip.frameCount);
    }
    for (const Part& part : baked.parts) {
        appendFixedString(out, part.material.empty() ? "mat" : part.material, 32);
        const std::string tex =
            part.image >= 0 && part.image < (int)textureNames.size()
                ? textureNames[part.image]
                : "";
        appendFixedString(out, tex, 64);
        for (int c = 0; c < 4; ++c) appendF32(out, part.baseColor[c]);
        appendU32(out, (uint32_t)part.vertexCount);
        if (!tex.empty())
            appendBytes(out, part.uvs.data(),
                        (size_t)part.vertexCount * 2 * sizeof(float));
        for (int f = 0; f < baked.frameCount; ++f) {
            const size_t stride = (size_t)part.vertexCount * 3;
            appendBytes(out, &part.positions[f * stride], stride * sizeof(float));
            appendBytes(out, &part.normals[f * stride], stride * sizeof(float));
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Stage 2: skeletal serialization

size_t Skel::ps2Bytes() const {
    // Model data as the engine keeps it in RAM (SkelModel: nodes ~120 B,
    // palette slots ~72 B, tracks) plus one instance's Vec4 output buffers.
    size_t bytes = nodes.size() * 120 + palette.size() * 72;
    for (const SkelClip& clip : clips)
        for (const SkelChannel& ch : clip.channels)
            bytes += ch.times.size() * (4 + (ch.path == 1 ? 8 : 12)) + 48;
    for (const SkelPart& part : parts) {
        const bool textured = part.image >= 0;
        bytes += (size_t)part.vertexCount * (12 + 12 + (textured ? 8 : 0) + 8);
        bytes += (size_t)part.vertexCount * (16 + 16 + (textured ? 16 : 0));
    }
    return bytes;
}

bool parseSkel(const std::string& path, Skel& out, std::string& error) {
    out = Skel();
    ParsedGlb P;
    if (!parseGlb(path, P, out.images, out.warnings, error)) return false;

    // --- node hierarchy with bind-pose locals --------------------------------
    out.nodes.resize(P.nodes.size());
    for (size_t i = 0; i < P.nodes.size(); ++i) {
        const Node& src = P.nodes[i];
        SkelNode& dst = out.nodes[i];
        dst.parent = P.parent[i];
        dst.hasMatrix = src.hasMatrix;
        std::memcpy(dst.matrix, src.matrix.m, sizeof(dst.matrix));
        std::memcpy(dst.t, src.t, sizeof(dst.t));
        std::memcpy(dst.r, src.r, sizeof(dst.r));
        std::memcpy(dst.s, src.s, sizeof(dst.s));
    }

    // --- matrix palette ------------------------------------------------------
    // Skins referenced by prims contribute their joint lists (with IBMs);
    // rigid mesh nodes get one identity-IBM slot each - both cases skin the
    // same way at runtime, so the engine has a single path.
    std::map<int, int> skinBase;   // glTF skin index -> first palette slot
    std::map<int, int> rigidSlot;  // node index -> palette slot
    for (const PrimRef& ref : P.prims) {
        if (ref.skin >= 0 && ref.skin < (int)P.skins.size()) {
            if (skinBase.count(ref.skin)) continue;
            skinBase[ref.skin] = (int)out.palette.size();
            const Skin& skin = P.skins[ref.skin];
            for (size_t j = 0; j < skin.joints.size(); ++j) {
                SkelJoint joint;
                const int jn = skin.joints[j];
                joint.node = jn >= 0 && jn < (int)P.nodes.size() ? jn : 0;
                std::memcpy(joint.ibm, &skin.ibm[j * 16], sizeof(joint.ibm));
                out.palette.push_back(joint);
            }
        } else if (!rigidSlot.count(ref.node)) {
            rigidSlot[ref.node] = (int)out.palette.size();
            SkelJoint joint;
            joint.node = ref.node;
            const M16 id = identity();
            std::memcpy(joint.ibm, id.m, sizeof(joint.ibm));
            out.palette.push_back(joint);
        }
    }
    if (out.palette.size() > 256) {
        error = "model needs " + std::to_string(out.palette.size()) +
                " matrix-palette slots (bones + rigid mesh nodes) - the "
                "runtime supports 256";
        return false;
    }

    // --- parts: bind-pose mesh expanded to flat triangle lists ---------------
    // Prim iteration and index expansion mirror bake()'s appendFrame exactly,
    // so the per-part vertex order (and UV stream) matches stage 1.
    out.parts.resize(P.parts.size());
    for (size_t pi = 0; pi < P.parts.size(); ++pi) {
        out.parts[pi].material = P.parts[pi].material;
        std::memcpy(out.parts[pi].baseColor, P.parts[pi].baseColor,
                    sizeof(out.parts[pi].baseColor));
        out.parts[pi].image = P.parts[pi].image;
    }
    for (const PrimRef& ref : P.prims) {
        SkelPart& part = out.parts[ref.part];
        const size_t vertCount = ref.pos.size() / 3;
        const bool hasNrm = ref.nrm.size() >= vertCount * 3;
        const bool skinned = ref.skin >= 0 && ref.skin < (int)P.skins.size();
        const int skinJoints =
            skinned ? (int)P.skins[ref.skin].joints.size() : 0;
        const int base = skinned ? skinBase[ref.skin] : rigidSlot[ref.node];

        const size_t triCount = ref.indices.size() / 3;
        for (size_t tri = 0; tri < triCount; ++tri) {
            float faceN[3] = {0, 1, 0};
            if (!hasNrm) {
                // Flat normal from the BIND-pose triangle. Skinning bends it
                // per vertex at runtime - only exact for rigid triangles, but
                // files without normals are rare (Blender always writes them).
                const float* a = &ref.pos[ref.indices[tri * 3] * 3];
                const float* b = &ref.pos[ref.indices[tri * 3 + 1] * 3];
                const float* c = &ref.pos[ref.indices[tri * 3 + 2] * 3];
                const float e1[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
                const float e2[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
                faceN[0] = e1[1] * e2[2] - e1[2] * e2[1];
                faceN[1] = e1[2] * e2[0] - e1[0] * e2[2];
                faceN[2] = e1[0] * e2[1] - e1[1] * e2[0];
                const float len = std::sqrt(faceN[0] * faceN[0] +
                                            faceN[1] * faceN[1] +
                                            faceN[2] * faceN[2]);
                if (len > 1e-6f)
                    for (float& f : faceN) f /= len;
            }
            for (int k = 0; k < 3; ++k) {
                const uint32_t idx = ref.indices[tri * 3 + k];
                part.positions.insert(part.positions.end(), &ref.pos[idx * 3],
                                      &ref.pos[idx * 3] + 3);
                if (hasNrm)
                    part.normals.insert(part.normals.end(), &ref.nrm[idx * 3],
                                        &ref.nrm[idx * 3] + 3);
                else
                    part.normals.insert(part.normals.end(), faceN, faceN + 3);
                const bool hasUv = ref.uv.size() >= (idx + 1) * 2;
                part.uvs.push_back(hasUv ? ref.uv[idx * 2] : 0.0f);
                part.uvs.push_back(hasUv ? ref.uv[idx * 2 + 1] : 0.0f);

                unsigned char joints[4] = {0, 0, 0, 0};
                unsigned char weights[4] = {0, 0, 0, 0};
                if (!skinned) {
                    joints[0] = (unsigned char)base;
                    weights[0] = 255;
                } else {
                    // Quantize the 4 influences so they sum to exactly 255
                    // (the runtime divides by the actual sum, but an exact
                    // sum keeps single-influence verts bit-stable).
                    const float* w = &ref.weights[idx * 4];
                    const float* j = &ref.joints[idx * 4];
                    float wsum = 0.0f;
                    for (int i = 0; i < 4; ++i)
                        if (w[i] > 0.0f) wsum += w[i];
                    if (wsum > 1e-5f) {
                        int total = 0, biggest = 0;
                        for (int i = 0; i < 4; ++i) {
                            const int joint = (int)(j[i] + 0.5f);
                            if (w[i] <= 0.0f || joint < 0 ||
                                joint >= skinJoints)
                                continue;  // same skips as bake()
                            joints[i] = (unsigned char)(base + joint);
                            weights[i] = (unsigned char)std::lround(
                                w[i] / wsum * 255.0f);
                            total += weights[i];
                            if (weights[i] > weights[biggest]) biggest = i;
                        }
                        weights[biggest] =
                            (unsigned char)(weights[biggest] + (255 - total));
                    }
                }
                part.joints.insert(part.joints.end(), joints, joints + 4);
                part.weights.insert(part.weights.end(), weights, weights + 4);
            }
        }
    }
    for (SkelPart& part : out.parts)
        part.vertexCount = (int)(part.positions.size() / 3);
    out.parts.erase(
        std::remove_if(out.parts.begin(), out.parts.end(),
                       [](const SkelPart& p) { return p.vertexCount == 0; }),
        out.parts.end());
    if (out.parts.empty()) {
        error = "no triangles found";
        return false;
    }

    // --- clips: keyframe tracks, times rebased to clip start ----------------
    for (const ClipSrc& src : P.clips) {
        SkelClip clip;
        clip.name = src.name;
        for (const SkelClip& other : out.clips)
            if (other.name == clip.name) {  // same de-dup rule as bake()
                clip.name += "_2";
                break;
            }
        clip.duration = src.end > src.start ? src.end - src.start : 0.0f;
        for (const Channel& ch : src.channels) {
            const int comps = ch.path == 1 ? 4 : 3;
            const size_t keyStride =
                ch.interpolation == 2 ? (size_t)comps * 3 : (size_t)comps;
            const size_t valueOff = ch.interpolation == 2 ? (size_t)comps : 0;
            const size_t n = ch.times.size();
            // sampleChannel() ignores short-value channels; mirror that.
            if (n == 0 || ch.values.size() < n * keyStride) continue;

            SkelChannel dst;
            dst.node = ch.node;
            dst.path = ch.path;
            // CUBICSPLINE degrades to linear through its keyframe values,
            // exactly like the stage-1 sampler.
            dst.step = ch.interpolation == 1 ? 1 : 0;
            dst.times.reserve(n);
            dst.values.reserve(n * comps);
            for (size_t k = 0; k < n; ++k) {
                dst.times.push_back(ch.times[k] - src.start);
                const float* v = &ch.values[k * keyStride + valueOff];
                dst.values.insert(dst.values.end(), v, v + comps);
            }
            // Constant tracks collapse to one key (Blender exports plenty).
            bool constant = true;
            for (size_t k = 1; k < n && constant; ++k)
                for (int c = 0; c < comps; ++c)
                    if (dst.values[k * comps + c] != dst.values[c]) {
                        constant = false;
                        break;
                    }
            if (constant && n > 1) {
                dst.times.resize(1);
                dst.times[0] = 0.0f;
                dst.values.resize(comps);
            }
            clip.channels.push_back(std::move(dst));
        }
        out.clips.push_back(std::move(clip));
    }
    if (out.clips.empty()) out.clips.push_back({"default", 0.0f, {}});

    // --- AABB: union over every clip, sampled along each duration -----------
    // The runtime culls whole instances with this box (skipping pose+skin for
    // offscreen ones), so it must cover every pose the model can strike, not
    // just clip 0 at t=0. Linear sampling can straddle an extreme between two
    // samples; the runtime pads the box 10% per axis on load.
    {
        bool first = true;
        auto foldPoseAabb = [&](const ClipSrc* clip, float t) {
            std::vector<Node> pose = P.nodes;
            if (clip) {
                for (const Channel& ch : clip->channels) {
                    Node& n = pose[ch.node];
                    n.hasMatrix = false;
                    if (ch.path == 0) sampleChannel(ch, t, n.t);
                    else if (ch.path == 1) sampleChannel(ch, t, n.r);
                    else sampleChannel(ch, t, n.s);
                }
            }
            std::vector<M16> globals;
            computeGlobals(pose, P.order, P.parent, globals);
            for (const PrimRef& ref : P.prims) {
                // per-vertex blended matrix, same math as bake()'s appendFrame
                std::vector<M16> palette;
                if (ref.skin >= 0 && ref.skin < (int)P.skins.size()) {
                    const Skin& skin = P.skins[ref.skin];
                    palette.resize(skin.joints.size());
                    for (size_t j = 0; j < skin.joints.size(); ++j) {
                        M16 ibm;
                        std::memcpy(ibm.m, &skin.ibm[j * 16], sizeof(ibm.m));
                        const int jn = skin.joints[j];
                        palette[j] = jn >= 0 && jn < (int)P.nodes.size()
                                         ? mul(globals[jn], ibm)
                                         : identity();
                    }
                }
                const M16 rigid = globals[ref.node];
                for (uint32_t idx : ref.indices) {
                    M16 blended;
                    const float* src = &ref.pos[idx * 3];
                    if (!palette.empty()) {
                        std::memset(blended.m, 0, sizeof(blended.m));
                        const float* w = &ref.weights[idx * 4];
                        const float* j = &ref.joints[idx * 4];
                        float wsum = w[0] + w[1] + w[2] + w[3];
                        if (wsum < 1e-5f) wsum = 1.0f;
                        for (int k = 0; k < 4; ++k) {
                            const int joint = (int)(j[k] + 0.5f);
                            if (w[k] <= 0.0f || joint < 0 ||
                                joint >= (int)palette.size())
                                continue;
                            const float wk = w[k] / wsum;
                            for (int c = 0; c < 16; ++c)
                                blended.m[c] += palette[joint].m[c] * wk;
                        }
                    } else {
                        blended = rigid;
                    }
                    const float* m = blended.m;
                    const float p[3] = {
                        m[0] * src[0] + m[4] * src[1] + m[8] * src[2] + m[12],
                        m[1] * src[0] + m[5] * src[1] + m[9] * src[2] + m[13],
                        m[2] * src[0] + m[6] * src[1] + m[10] * src[2] + m[14]};
                    for (int c = 0; c < 3; ++c) {
                        if (first || p[c] < out.min[c]) out.min[c] = p[c];
                        if (first || p[c] > out.max[c]) out.max[c] = p[c];
                    }
                    first = false;
                }
            }
        };
        if (P.clips.empty()) {
            foldPoseAabb(nullptr, 0.0f);
        } else {
            const int samples = 8;
            for (const ClipSrc& clip : P.clips) {
                const float duration =
                    clip.end > clip.start ? clip.end - clip.start : 0.0f;
                if (duration <= 0.0f) {
                    foldPoseAabb(&clip, clip.start);
                    continue;
                }
                for (int s = 0; s < samples; ++s)
                    foldPoseAabb(&clip, clip.start +
                                            duration * s / (samples - 1));
            }
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
// Bake-time mesh LOD: quadric-error half-edge collapse on the bind-pose
// triangle list. Chosen shape:
//  - HALF-edge collapse (a snaps onto b, b keeps its attributes): normals,
//    uvs and skin bindings are never blended, so skinning stays valid by
//    construction and no new attribute values appear at any LOD;
//  - vertices are welded by their full attribute tuple first, which makes uv
//    and hard-normal seams distinct vertices - collapses cannot cross a
//    seam. Seam borders and open mesh borders are locked (never moved), so
//    silhouettes shrink from the inside out;
//  - collapses run in sorted-cost rounds instead of a mutating heap: within
//    a round each vertex participates in at most one collapse, then
//    adjacency and quadrics rebuild. Slightly worse than a true greedy heap,
//    a fraction of the code.
// ---------------------------------------------------------------------------
namespace {

struct Quadric {
    double m[10] = {};  // symmetric 4x4: xx xy xz xw yy yz yw zz zw ww
    void addPlane(double a, double b, double c, double d) {
        m[0] += a * a; m[1] += a * b; m[2] += a * c; m[3] += a * d;
        m[4] += b * b; m[5] += b * c; m[6] += b * d;
        m[7] += c * c; m[8] += c * d; m[9] += d * d;
    }
    void add(const Quadric& q) {
        for (int i = 0; i < 10; ++i) m[i] += q.m[i];
    }
    double error(const float* v) const {
        const double x = v[0], y = v[1], z = v[2];
        return m[0] * x * x + 2 * m[1] * x * y + 2 * m[2] * x * z +
               2 * m[3] * x + m[4] * y * y + 2 * m[5] * y * z + 2 * m[6] * y +
               m[7] * z * z + 2 * m[8] * z + m[9];
    }
};

/** One welded vertex: the full attribute tuple of the flat-list corner. */
struct WeldedMesh {
    std::vector<float> pos, nrm, uv;             // per welded vertex
    std::vector<unsigned char> joints, weights;  // per welded vertex * 4
    std::vector<uint32_t> tris;                  // 3 indices per triangle
    bool hasUv = false;
};

WeldedMesh weldPart(const SkelPart& part) {
    WeldedMesh w;
    w.hasUv = !part.uvs.empty();
    std::map<std::string, uint32_t> lookup;  // attr bytes -> welded index
    std::string key;
    for (int v = 0; v < part.vertexCount; ++v) {
        key.assign(reinterpret_cast<const char*>(&part.positions[v * 3]),
                   3 * sizeof(float));
        key.append(reinterpret_cast<const char*>(&part.normals[v * 3]),
                   3 * sizeof(float));
        if (w.hasUv)
            key.append(reinterpret_cast<const char*>(&part.uvs[v * 2]),
                       2 * sizeof(float));
        key.append(reinterpret_cast<const char*>(&part.joints[v * 4]), 4);
        key.append(reinterpret_cast<const char*>(&part.weights[v * 4]), 4);
        auto it = lookup.find(key);
        uint32_t idx;
        if (it == lookup.end()) {
            idx = (uint32_t)(w.pos.size() / 3);
            lookup.emplace(key, idx);
            w.pos.insert(w.pos.end(), &part.positions[v * 3],
                         &part.positions[v * 3] + 3);
            w.nrm.insert(w.nrm.end(), &part.normals[v * 3],
                         &part.normals[v * 3] + 3);
            if (w.hasUv)
                w.uv.insert(w.uv.end(), &part.uvs[v * 2],
                            &part.uvs[v * 2] + 2);
            w.joints.insert(w.joints.end(), &part.joints[v * 4],
                            &part.joints[v * 4] + 4);
            w.weights.insert(w.weights.end(), &part.weights[v * 4],
                             &part.weights[v * 4] + 4);
        } else {
            idx = it->second;
        }
        w.tris.push_back(idx);
    }
    return w;
}

/** Decimates a welded mesh to <= targetVerts live vertices. Returns the
 * remap (vertex -> live vertex it ended up in) and rewrites w.tris. */
void decimate(WeldedMesh& w, size_t targetVerts) {
    const size_t vertCount = w.pos.size() / 3;
    std::vector<uint32_t> remap(vertCount);
    for (size_t i = 0; i < vertCount; ++i) remap[i] = (uint32_t)i;
    auto resolve = [&](uint32_t v) {
        while (remap[v] != v) v = remap[v];
        return v;
    };
    size_t alive = vertCount;

    // "position twin" seams: the same position with different attributes
    // (uv/normal seams). Locked - moving one copy but not its twin would
    // crack the surface open.
    std::vector<uint8_t> locked(vertCount, 0);
    {
        std::map<std::string, uint32_t> firstAt;
        std::string pkey;
        for (size_t i = 0; i < vertCount; ++i) {
            pkey.assign(reinterpret_cast<const char*>(&w.pos[i * 3]),
                        3 * sizeof(float));
            auto it = firstAt.find(pkey);
            if (it == firstAt.end()) {
                firstAt.emplace(pkey, (uint32_t)i);
            } else {
                locked[i] = locked[it->second] = 1;
            }
        }
    }

    for (int round = 0; round < 64 && alive > targetVerts; ++round) {
        // live triangles + per-vertex quadrics + edge -> use count
        std::vector<Quadric> quadrics(vertCount);
        std::map<std::pair<uint32_t, uint32_t>, int> edgeUses;
        for (size_t t = 0; t + 2 < w.tris.size() + 1 && t < w.tris.size();
             t += 3) {
            uint32_t a = resolve(w.tris[t]), b = resolve(w.tris[t + 1]),
                     c = resolve(w.tris[t + 2]);
            if (a == b || b == c || a == c) continue;  // degenerate
            const float* pa = &w.pos[a * 3];
            const float* pb = &w.pos[b * 3];
            const float* pc = &w.pos[c * 3];
            const double ux = pb[0] - pa[0], uy = pb[1] - pa[1],
                         uz = pb[2] - pa[2];
            const double vx = pc[0] - pa[0], vy = pc[1] - pa[1],
                         vz = pc[2] - pa[2];
            double nx = uy * vz - uz * vy, ny = uz * vx - ux * vz,
                   nz = ux * vy - uy * vx;
            const double len = std::sqrt(nx * nx + ny * ny + nz * nz);
            if (len < 1e-12) continue;
            nx /= len, ny /= len, nz /= len;
            const double d = -(nx * pa[0] + ny * pa[1] + nz * pa[2]);
            Quadric q;
            q.addPlane(nx, ny, nz, d);
            quadrics[a].add(q);
            quadrics[b].add(q);
            quadrics[c].add(q);
            const uint32_t e[3][2] = {{a, b}, {b, c}, {c, a}};
            for (auto& ed : e)
                edgeUses[{std::min(ed[0], ed[1]), std::max(ed[0], ed[1])}]++;
        }
        if (edgeUses.empty()) break;

        // open-border vertices (edge used by a single triangle) are locked
        // for this round so silhouette outlines and part borders hold still
        std::vector<uint8_t> border(vertCount, 0);
        for (const auto& eu : edgeUses)
            if (eu.second == 1)
                border[eu.first.first] = border[eu.first.second] = 1;

        struct Candidate {
            double cost;
            uint32_t from, to;
        };
        std::vector<Candidate> cands;
        cands.reserve(edgeUses.size());
        for (const auto& eu : edgeUses) {
            const uint32_t a = eu.first.first, b = eu.first.second;
            const bool aMovable = !locked[a] && !border[a];
            const bool bMovable = !locked[b] && !border[b];
            const double costAtoB = quadrics[a].error(&w.pos[b * 3]);
            const double costBtoA = quadrics[b].error(&w.pos[a * 3]);
            if (aMovable && (costAtoB <= costBtoA || !bMovable))
                cands.push_back({costAtoB, a, b});
            else if (bMovable)
                cands.push_back({costBtoA, b, a});
        }
        if (cands.empty()) break;
        std::sort(cands.begin(), cands.end(),
                  [](const Candidate& x, const Candidate& y) {
                      return x.cost < y.cost;
                  });

        // cap the round so quadrics never go too stale between rebuilds
        size_t budget = alive > targetVerts ? alive - targetVerts : 0;
        if (budget > alive / 4 + 1) budget = alive / 4 + 1;
        std::vector<uint8_t> touched(vertCount, 0);
        size_t done = 0;
        for (const Candidate& c : cands) {
            if (done >= budget) break;
            const uint32_t from = resolve(c.from), to = resolve(c.to);
            if (from == to || touched[from] || touched[to]) continue;
            remap[from] = to;
            touched[from] = touched[to] = 1;
            --alive;
            ++done;
        }
        if (done == 0) break;
    }

    // rewrite the index list through the remap, dropping degenerates
    std::vector<uint32_t> tris;
    tris.reserve(w.tris.size());
    for (size_t t = 0; t + 2 < w.tris.size(); t += 3) {
        const uint32_t a = resolve(w.tris[t]), b = resolve(w.tris[t + 1]),
                       c = resolve(w.tris[t + 2]);
        if (a == b || b == c || a == c) continue;
        tris.push_back(a);
        tris.push_back(b);
        tris.push_back(c);
    }
    w.tris.swap(tris);
}

/** Expands the (decimated) welded mesh back to the flat triangle list. */
SkelLod unweld(const WeldedMesh& w) {
    SkelLod lod;
    lod.vertexCount = (int)w.tris.size();
    lod.positions.reserve(w.tris.size() * 3);
    lod.normals.reserve(w.tris.size() * 3);
    if (w.hasUv) lod.uvs.reserve(w.tris.size() * 2);
    lod.joints.reserve(w.tris.size() * 4);
    lod.weights.reserve(w.tris.size() * 4);
    for (uint32_t idx : w.tris) {
        lod.positions.insert(lod.positions.end(), &w.pos[idx * 3],
                             &w.pos[idx * 3] + 3);
        lod.normals.insert(lod.normals.end(), &w.nrm[idx * 3],
                           &w.nrm[idx * 3] + 3);
        if (w.hasUv)
            lod.uvs.insert(lod.uvs.end(), &w.uv[idx * 2], &w.uv[idx * 2] + 2);
        lod.joints.insert(lod.joints.end(), &w.joints[idx * 4],
                          &w.joints[idx * 4] + 4);
        lod.weights.insert(lod.weights.end(), &w.weights[idx * 4],
                           &w.weights[idx * 4] + 4);
    }
    return lod;
}

}  // namespace

void generateSkelLods(Skel& skel) {
    // Below ~2x this floor a decimated variant saves nothing worth the RAM.
    constexpr int kMinVerts = 96;
    constexpr float kRatios[] = {0.5f, 0.25f};  // of the WELDED vertex count
    for (SkelPart& part : skel.parts) {
        part.lods.clear();
        if (part.vertexCount < kMinVerts * 2) continue;
        for (float ratio : kRatios) {
            WeldedMesh w = weldPart(part);
            const size_t target =
                (size_t)((w.pos.size() / 3) * ratio + 0.5f);
            decimate(w, target < 3 ? 3 : target);
            SkelLod lod = unweld(w);
            // a LOD that failed to shrink meaningfully is dead weight
            if (lod.vertexCount == 0 ||
                lod.vertexCount > part.vertexCount * (ratio + 0.15f))
                break;
            part.lods.push_back(std::move(lod));
        }
    }
}

// Layout (little-endian; keep in sync with the engine's tskl_loader.cpp):
//   "TSKL" u32(version=2)      (v1 files = the same layout without lodCount)
//   u32 nodeCount, u32 paletteCount, u32 partCount, u32 clipCount
//   f32 min[3], f32 max[3]           (pose AABB union over all clips, sampled)
//   nodeCount * { s32 parent; u32 flags(bit0 = hasMatrix);
//                 f32 t[3]; f32 r[4]; f32 s[3]; f32 matrix[16] }
//   paletteCount * { u32 node; f32 ibm[16] }
//   clipCount * {
//     char name[32]; f32 duration; u32 channelCount;
//     channelCount * { u32 node; u8 path; u8 step; u8 pad[2]; u32 keyCount;
//                      f32 times[keyCount];
//                      path==1 ? s16 quat[keyCount*4] (x/32767)
//                              : f32 v[keyCount*3] }
//   }
//   partCount * {
//     char name[32]; char texture[64]; f32 color[4]; u32 vertexCount;
//     if (texture[0]) f32 uv[vertexCount*2]
//     f32 pos[vertexCount*3]; f32 nrm[vertexCount*3];
//     u8 joints[vertexCount*4]; u8 weights[vertexCount*4]
//     v2 only: u32 lodCount; lodCount * {
//       u32 vertexCount; if (texture[0]) f32 uv[...];
//       f32 pos[...]; f32 nrm[...]; u8 joints[...]; u8 weights[...] }
//   }
std::string writeTskl(const Skel& skel,
                      const std::vector<std::string>& textureNames) {
    std::string out;
    out.reserve(4096 + (size_t)skel.totalVertexCount() * 40);
    out += "TSKL";
    appendU32(out, 2);
    appendU32(out, (uint32_t)skel.nodes.size());
    appendU32(out, (uint32_t)skel.palette.size());
    appendU32(out, (uint32_t)skel.parts.size());
    appendU32(out, (uint32_t)skel.clips.size());
    for (int c = 0; c < 3; ++c) appendF32(out, skel.min[c]);
    for (int c = 0; c < 3; ++c) appendF32(out, skel.max[c]);

    for (const SkelNode& node : skel.nodes) {
        appendU32(out, (uint32_t)node.parent);  // -1 round-trips through u32
        appendU32(out, node.hasMatrix ? 1u : 0u);
        for (int c = 0; c < 3; ++c) appendF32(out, node.t[c]);
        for (int c = 0; c < 4; ++c) appendF32(out, node.r[c]);
        for (int c = 0; c < 3; ++c) appendF32(out, node.s[c]);
        for (int c = 0; c < 16; ++c) appendF32(out, node.matrix[c]);
    }
    for (const SkelJoint& joint : skel.palette) {
        appendU32(out, (uint32_t)joint.node);
        for (int c = 0; c < 16; ++c) appendF32(out, joint.ibm[c]);
    }
    for (const SkelClip& clip : skel.clips) {
        appendFixedString(out, clip.name, 32);
        appendF32(out, clip.duration);
        appendU32(out, (uint32_t)clip.channels.size());
        for (const SkelChannel& ch : clip.channels) {
            appendU32(out, (uint32_t)ch.node);
            out.push_back((char)ch.path);
            out.push_back((char)ch.step);
            out.push_back(0);
            out.push_back(0);
            const uint32_t keyCount = (uint32_t)ch.times.size();
            appendU32(out, keyCount);
            appendBytes(out, ch.times.data(), keyCount * sizeof(float));
            if (ch.path == 1) {
                for (uint32_t k = 0; k < keyCount * 4; ++k) {
                    float v = ch.values[k];
                    if (v > 1.0f) v = 1.0f;
                    if (v < -1.0f) v = -1.0f;
                    const int16_t q = (int16_t)std::lround(v * 32767.0f);
                    appendBytes(out, &q, 2);
                }
            } else {
                appendBytes(out, ch.values.data(),
                            (size_t)keyCount * 3 * sizeof(float));
            }
        }
    }
    for (const SkelPart& part : skel.parts) {
        appendFixedString(out, part.material.empty() ? "mat" : part.material,
                          32);
        const std::string tex =
            part.image >= 0 && part.image < (int)textureNames.size()
                ? textureNames[part.image]
                : "";
        appendFixedString(out, tex, 64);
        for (int c = 0; c < 4; ++c) appendF32(out, part.baseColor[c]);
        appendU32(out, (uint32_t)part.vertexCount);
        if (!tex.empty())
            appendBytes(out, part.uvs.data(),
                        (size_t)part.vertexCount * 2 * sizeof(float));
        appendBytes(out, part.positions.data(),
                    (size_t)part.vertexCount * 3 * sizeof(float));
        appendBytes(out, part.normals.data(),
                    (size_t)part.vertexCount * 3 * sizeof(float));
        appendBytes(out, part.joints.data(), (size_t)part.vertexCount * 4);
        appendBytes(out, part.weights.data(), (size_t)part.vertexCount * 4);
        appendU32(out, (uint32_t)part.lods.size());
        for (const SkelLod& lod : part.lods) {
            appendU32(out, (uint32_t)lod.vertexCount);
            if (!tex.empty())
                appendBytes(out, lod.uvs.data(),
                            (size_t)lod.vertexCount * 2 * sizeof(float));
            appendBytes(out, lod.positions.data(),
                        (size_t)lod.vertexCount * 3 * sizeof(float));
            appendBytes(out, lod.normals.data(),
                        (size_t)lod.vertexCount * 3 * sizeof(float));
            appendBytes(out, lod.joints.data(), (size_t)lod.vertexCount * 4);
            appendBytes(out, lod.weights.data(), (size_t)lod.vertexCount * 4);
        }
    }
    return out;
}

}  // namespace glbparser
