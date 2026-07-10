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

}  // namespace

bool bake(const std::string& path, float fps, Baked& out, std::string& error) {
    out = Baked();
    out.fps = fps > 1.0f ? fps : 1.0f;

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
    std::vector<Node> nodes;
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
    std::vector<int> parent(nodes.size(), -1);
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
    std::vector<int> order;
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
    struct Skin {
        std::vector<int> joints;
        std::vector<float> ibm;  // joints * 16
    };
    std::vector<Skin> skins;
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
            out.warnings.push_back("texture image is not embedded - skipped");
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
                out.warnings.push_back("undecodable embedded texture - skipped");
                return -1;
            }
            stbi_write_png_to_func(stbWriteToVector, &baked.png, w, h, 4, pixels,
                                   w * 4);
            stbi_image_free(pixels);
            out.warnings.push_back("embedded " + (mime.empty() ? "image" : mime) +
                                   " transcoded to PNG");
        }
        {
            int w = 0, h = 0, comp = 0;
            if (stbi_info_from_memory(baked.png.data(), (int)baked.png.size(), &w,
                                      &h, &comp)) {
                const bool pot = w > 0 && h > 0 && !(w & (w - 1)) && !(h & (h - 1));
                if (!pot)
                    out.warnings.push_back(
                        "texture " + std::to_string(w) + "x" + std::to_string(h) +
                        " is not power-of-two - the PS2 cannot load it");
            }
        }
        std::string base = img->find("name") ? img->find("name")->stringOr("") : "";
        baked.name = sanitizePngName(base.empty() ? "tex" + std::to_string(imgIndex)
                                                  : base);
        for (const Image& other : out.images)
            if (other.name == baked.name + ".png") {
                baked.name += "_" + std::to_string(imgIndex);
                break;
            }
        baked.name += ".png";
        out.images.push_back(std::move(baked));
        imageSlot[imgIndex] = (int)out.images.size() - 1;
        return imageSlot[imgIndex];
    };

    // --- mesh primitives, expanded to flat triangle lists -------------------
    // A PrimRef is one glTF primitive bound to the scene node that draws it;
    // per baked frame its bind-pose data is re-transformed and appended to the
    // owning Part, so the per-part vertex layout repeats identically.
    struct PrimRef {
        int node = -1, skin = -1, part = -1;
        std::vector<float> pos, nrm, uv;   // bind pose, indexed
        std::vector<float> joints, weights;
        std::vector<uint32_t> indices;     // expanded triangle order
    };
    std::vector<PrimRef> prims;
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
                out.warnings.push_back("non-triangle primitive skipped");
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
                Part part;
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
                out.parts.push_back(std::move(part));
                it = partOfMaterial.emplace(materialIndex, (int)out.parts.size() - 1)
                         .first;
            }
            ref.part = it->second;
            if (out.parts[ref.part].image >= 0 && ref.uv.size() < vertCount * 2) {
                out.warnings.push_back(
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
    std::vector<ClipSrc> clipSrcs;
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
                        out.warnings.push_back(
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

    // --- bake ---------------------------------------------------------------
    std::vector<M16> globals(nodes.size());
    // Working TRS copies - channels overwrite these per sample.
    std::vector<Node> pose = nodes;

    auto computeGlobals = [&]() {
        for (int i : order) {
            const Node& n = pose[i];
            const M16 local =
                n.hasMatrix ? n.matrix : fromTrs(n.t, n.r, n.s);
            globals[i] =
                parent[i] >= 0 ? mul(globals[parent[i]], local) : local;
        }
    };

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
                    palette[j] = jn >= 0 && jn < (int)nodes.size()
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
        computeGlobals();
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
                pose = nodes;
                for (const Channel& ch : clip.channels) {
                    Node& n = pose[ch.node];
                    n.hasMatrix = false;  // animated nodes always compose TRS
                    if (ch.path == 0) sampleChannel(ch, t, n.t);
                    else if (ch.path == 1) sampleChannel(ch, t, n.r);
                    else sampleChannel(ch, t, n.s);
                }
                computeGlobals();
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

}  // namespace glbparser
