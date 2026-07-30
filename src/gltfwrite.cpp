#include "gltfwrite.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

namespace gltfwrite {

namespace {

// glTF component types / accessor kinds, spelled out where they are used.
constexpr int kFloat = 5126;
constexpr int kUByte = 5121;

std::string escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                // Control characters would make the JSON invalid; names come
                // from user input (the character's name), so clamp them.
                if ((unsigned char)c < 0x20)
                    out += ' ';
                else
                    out += c;
        }
    }
    return out;
}

// %.9g round-trips a float exactly and keeps short values short ("0.5", not
// "0.50000000"). Anything non-finite would produce invalid JSON.
std::string num(float f) {
    if (!(f == f) || f > 3.0e38f || f < -3.0e38f) return "0";
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%.9g", (double)f);
    return buf;
}

std::string vec(const float* v, int n) {
    std::string s = "[";
    for (int i = 0; i < n; ++i) {
        if (i) s += ",";
        s += num(v[i]);
    }
    return s + "]";
}

// Accumulates the BIN chunk and the bufferViews/accessors that address it.
struct Buffers {
    std::vector<unsigned char> bin;
    std::string views;     // JSON array body
    std::string accessors; // JSON array body
    int viewCount = 0;
    int accessorCount = 0;

    void pad4() {
        while (bin.size() % 4) bin.push_back(0);
    }

    int addView(const void* data, size_t bytes) {
        pad4();
        const size_t offset = bin.size();
        const unsigned char* p = (const unsigned char*)data;
        bin.insert(bin.end(), p, p + bytes);
        if (viewCount) views += ",";
        views += "{\"buffer\":0,\"byteOffset\":" + std::to_string(offset) +
                 ",\"byteLength\":" + std::to_string(bytes) + "}";
        return viewCount++;
    }

    // `extra` carries per-accessor JSON that the caller wants appended
    // (min/max on POSITION, "normalized" on WEIGHTS_0).
    int addAccessor(int view, int componentType, const char* type, int count,
                    const std::string& extra = "") {
        if (accessorCount) accessors += ",";
        accessors += "{\"bufferView\":" + std::to_string(view) +
                     ",\"componentType\":" + std::to_string(componentType) +
                     ",\"count\":" + std::to_string(count) + ",\"type\":\"" + type + "\"" +
                     extra + "}";
        return accessorCount++;
    }

    int addFloats(const std::vector<float>& v, const char* type, int comps,
                  const std::string& extra = "") {
        if (v.empty() || comps <= 0) return -1;
        const int view = addView(v.data(), v.size() * sizeof(float));
        return addAccessor(view, kFloat, type, (int)v.size() / comps, extra);
    }

    int addBytes(const std::vector<unsigned char>& v, const char* type, int comps,
                 const std::string& extra = "") {
        if (v.empty() || comps <= 0) return -1;
        const int view = addView(v.data(), v.size());
        return addAccessor(view, kUByte, type, (int)v.size() / comps, extra);
    }
};

// POSITION is the one accessor glTF *requires* min/max on - Blender and the
// validators reject a file without them, even though our own reader ignores
// them.
std::string boundsExtra(const std::vector<float>& pos) {
    if (pos.size() < 3) return "";
    float lo[3] = {pos[0], pos[1], pos[2]}, hi[3] = {pos[0], pos[1], pos[2]};
    for (size_t i = 3; i + 2 < pos.size(); i += 3)
        for (int k = 0; k < 3; ++k) {
            if (pos[i + k] < lo[k]) lo[k] = pos[i + k];
            if (pos[i + k] > hi[k]) hi[k] = pos[i + k];
        }
    return ",\"min\":" + vec(lo, 3) + ",\"max\":" + vec(hi, 3);
}

}  // namespace

std::string writeGlb(const glbparser::Skel& skel, const std::string& generator) {
    Buffers buf;
    const int nodeCount = (int)skel.nodes.size();

    // --- images / textures / materials --------------------------------------
    std::string imagesJson, texturesJson, materialsJson;
    for (size_t i = 0; i < skel.images.size(); ++i) {
        const int view = buf.addView(skel.images[i].png.data(), skel.images[i].png.size());
        if (i) {
            imagesJson += ",";
            texturesJson += ",";
        }
        std::string name = skel.images[i].name;
        // The reader derives the extracted PNG's filename from image.name, and
        // appends ".png" itself.
        if (name.size() > 4 && name.compare(name.size() - 4, 4, ".png") == 0)
            name.resize(name.size() - 4);
        imagesJson += "{\"bufferView\":" + std::to_string(view) +
                      ",\"mimeType\":\"image/png\",\"name\":\"" + escape(name) + "\"}";
        texturesJson += "{\"sampler\":0,\"source\":" + std::to_string(i) + "}";
    }

    for (size_t i = 0; i < skel.parts.size(); ++i) {
        const glbparser::SkelPart& part = skel.parts[i];
        if (i) materialsJson += ",";
        materialsJson += "{\"name\":\"" + escape(part.material.empty()
                                                     ? "material" + std::to_string(i)
                                                     : part.material) +
                         "\",\"pbrMetallicRoughness\":{\"baseColorFactor\":" +
                         vec(part.baseColor, 4) + ",\"metallicFactor\":0,\"roughnessFactor\":1";
        if (part.image >= 0 && part.image < (int)skel.images.size())
            materialsJson += ",\"baseColorTexture\":{\"index\":" + std::to_string(part.image) + "}";
        materialsJson += "}}";
    }

    // --- mesh primitives ----------------------------------------------------
    // One primitive per part, flat triangle lists (no index buffer): the
    // parts already come expanded that way, and re-indexing here would only
    // have to be undone at load.
    std::string primsJson;
    for (size_t i = 0; i < skel.parts.size(); ++i) {
        const glbparser::SkelPart& part = skel.parts[i];
        if (part.vertexCount <= 0) continue;

        const int pos = buf.addFloats(part.positions, "VEC3", 3, boundsExtra(part.positions));
        const int nrm = buf.addFloats(part.normals, "VEC3", 3);
        const int uv = buf.addFloats(part.uvs, "VEC2", 2);
        const int joints = buf.addBytes(part.joints, "VEC4", 4);
        // Normalized weights: 0..255 is read back as 0..1, which is exactly
        // the convention SkelPart::weights already stores.
        const int weights = buf.addBytes(part.weights, "VEC4", 4, ",\"normalized\":true");

        if (!primsJson.empty()) primsJson += ",";
        primsJson += "{\"attributes\":{\"POSITION\":" + std::to_string(pos);
        if (nrm >= 0) primsJson += ",\"NORMAL\":" + std::to_string(nrm);
        if (uv >= 0) primsJson += ",\"TEXCOORD_0\":" + std::to_string(uv);
        if (joints >= 0 && weights >= 0)
            primsJson += ",\"JOINTS_0\":" + std::to_string(joints) +
                         ",\"WEIGHTS_0\":" + std::to_string(weights);
        primsJson += "},\"material\":" + std::to_string(i) + ",\"mode\":4}";
    }

    // --- skin ---------------------------------------------------------------
    std::string skinsJson;
    const bool skinned = !skel.palette.empty();
    if (skinned) {
        std::vector<float> ibm;
        ibm.reserve(skel.palette.size() * 16);
        std::string joints = "[";
        for (size_t j = 0; j < skel.palette.size(); ++j) {
            if (j) joints += ",";
            joints += std::to_string(skel.palette[j].node);
            for (int k = 0; k < 16; ++k) ibm.push_back(skel.palette[j].ibm[k]);
        }
        joints += "]";
        const int acc = buf.addFloats(ibm, "MAT4", 16);
        skinsJson = "{\"joints\":" + joints +
                    ",\"inverseBindMatrices\":" + std::to_string(acc) + "}";
    }

    // --- nodes --------------------------------------------------------------
    // The skeleton is written verbatim; the mesh hangs off ONE extra node
    // appended at the end, so bone indices (which the palette and every
    // animation channel address) stay exactly what the caller built.
    std::vector<std::string> children(nodeCount);
    for (int i = 0; i < nodeCount; ++i) {
        const int p = skel.nodes[i].parent;
        if (p < 0 || p >= nodeCount || p == i) continue;
        if (!children[p].empty()) children[p] += ",";
        children[p] += std::to_string(i);
    }

    std::string nodesJson;
    for (int i = 0; i < nodeCount; ++i) {
        const glbparser::SkelNode& n = skel.nodes[i];
        if (i) nodesJson += ",";
        nodesJson += "{";
        if (!n.name.empty()) nodesJson += "\"name\":\"" + escape(n.name) + "\",";
        if (n.hasMatrix) {
            nodesJson += "\"matrix\":" + vec(n.matrix, 16);
        } else {
            nodesJson += "\"translation\":" + vec(n.t, 3) + ",\"rotation\":" + vec(n.r, 4) +
                         ",\"scale\":" + vec(n.s, 3);
        }
        if (!children[i].empty()) nodesJson += ",\"children\":[" + children[i] + "]";
        nodesJson += "}";
    }

    const int meshNode = nodeCount;
    if (!primsJson.empty()) {
        if (nodeCount) nodesJson += ",";
        nodesJson += "{\"name\":\"Mesh\",\"mesh\":0";
        if (skinned) nodesJson += ",\"skin\":0";
        nodesJson += "}";
    }

    std::string roots;
    for (int i = 0; i < nodeCount; ++i) {
        if (skel.nodes[i].parent >= 0 && skel.nodes[i].parent < nodeCount) continue;
        if (!roots.empty()) roots += ",";
        roots += std::to_string(i);
    }
    if (!primsJson.empty()) {
        if (!roots.empty()) roots += ",";
        roots += std::to_string(meshNode);
    }

    // --- animations ---------------------------------------------------------
    std::string animsJson;
    for (const glbparser::SkelClip& clip : skel.clips) {
        std::string samplers, channels;
        int samplerIndex = 0;
        for (const glbparser::SkelChannel& ch : clip.channels) {
            if (ch.times.empty() || ch.node < 0 || ch.node >= nodeCount) continue;
            const int comps = ch.path == 1 ? 4 : 3;
            if (ch.values.size() != ch.times.size() * (size_t)comps) continue;
            const int in = buf.addFloats(ch.times, "SCALAR", 1);
            const int out = buf.addFloats(ch.values, comps == 4 ? "VEC4" : "VEC3", comps);
            if (in < 0 || out < 0) continue;
            if (samplerIndex) {
                samplers += ",";
                channels += ",";
            }
            samplers += "{\"input\":" + std::to_string(in) + ",\"output\":" + std::to_string(out) +
                        ",\"interpolation\":\"" + (ch.step ? "STEP" : "LINEAR") + "\"}";
            channels += "{\"sampler\":" + std::to_string(samplerIndex) + ",\"target\":{\"node\":" +
                        std::to_string(ch.node) + ",\"path\":\"" +
                        (ch.path == 0 ? "translation" : (ch.path == 1 ? "rotation" : "scale")) +
                        "\"}}";
            ++samplerIndex;
        }
        // A clip with no usable channel would be an empty animation object,
        // which the spec forbids (samplers/channels must be non-empty).
        if (!samplerIndex) continue;
        if (!animsJson.empty()) animsJson += ",";
        animsJson += "{\"name\":\"" + escape(clip.name) + "\",\"samplers\":[" + samplers +
                     "],\"channels\":[" + channels + "]}";
    }

    // --- document -----------------------------------------------------------
    std::string json = "{\"asset\":{\"version\":\"2.0\",\"generator\":\"" + escape(generator) +
                       "\"},\"scene\":0,\"scenes\":[{\"nodes\":[" + roots + "]}]";
    json += ",\"nodes\":[" + nodesJson + "]";
    if (!primsJson.empty()) json += ",\"meshes\":[{\"primitives\":[" + primsJson + "]}]";
    if (!skinsJson.empty()) json += ",\"skins\":[" + skinsJson + "]";
    if (!materialsJson.empty()) json += ",\"materials\":[" + materialsJson + "]";
    if (!imagesJson.empty()) {
        json += ",\"images\":[" + imagesJson + "]";
        json += ",\"textures\":[" + texturesJson + "]";
        // 10497 = REPEAT on both axes; the PS2 side tiles the same way.
        json += ",\"samplers\":[{\"wrapS\":10497,\"wrapT\":10497}]";
    }
    if (!animsJson.empty()) json += ",\"animations\":[" + animsJson + "]";
    json += ",\"bufferViews\":[" + buf.views + "]";
    json += ",\"accessors\":[" + buf.accessors + "]";
    json += ",\"buffers\":[{\"byteLength\":" + std::to_string(buf.bin.size()) + "}]}";

    // --- GLB container ------------------------------------------------------
    // Both chunks are padded to 4 bytes: JSON with spaces, BIN with zeros.
    while (json.size() % 4) json += ' ';
    buf.pad4();

    std::string out;
    out.reserve(28 + json.size() + buf.bin.size());
    auto put32 = [&out](uint32_t v) {
        out += (char)(v & 0xFF);
        out += (char)((v >> 8) & 0xFF);
        out += (char)((v >> 16) & 0xFF);
        out += (char)((v >> 24) & 0xFF);
    };
    const uint32_t total = 12 + 8 + (uint32_t)json.size() + 8 + (uint32_t)buf.bin.size();
    put32(0x46546C67u);  // "glTF"
    put32(2);
    put32(total);
    put32((uint32_t)json.size());
    put32(0x4E4F534Au);  // "JSON"
    out += json;
    put32((uint32_t)buf.bin.size());
    put32(0x004E4942u);  // "BIN"
    out.append((const char*)buf.bin.data(), buf.bin.size());
    return out;
}

bool writeGlbFile(const std::string& path, const glbparser::Skel& skel,
                  const std::string& generator, std::string& error) {
    const std::string bytes = writeGlb(skel, generator);
    std::ofstream f(path, std::ios::binary);
    if (!f) {
        error = "Could not write " + path;
        return false;
    }
    f.write(bytes.data(), (std::streamsize)bytes.size());
    if (!f) {
        error = "Could not write " + path;
        return false;
    }
    return true;
}

}  // namespace gltfwrite
