#include "tmdl.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>

namespace tmdl {

namespace {

void appendBytes(std::string& out, const void* p, size_t n) {
    out.append(reinterpret_cast<const char*>(p), n);
}

void appendU32(std::string& out, uint32_t v) { appendBytes(out, &v, 4); }
void appendF32(std::string& out, float v) { appendBytes(out, &v, 4); }

// Fixed-size NUL-padded field, truncating (the .tskl convention).
void appendFixedString(std::string& out, const std::string& s, size_t size) {
    char buf[128] = {};
    std::snprintf(buf, std::min(size, sizeof(buf)), "%s", s.c_str());
    out.append(buf, size);
}

// vertexCount + the interleaved vertices + the optional AO table. Shared by
// the base mesh and every LOD tier so the two can never drift.
void appendMesh(std::string& out, const std::vector<float>& verts,
                const std::vector<unsigned char>& ao) {
    const uint32_t count = (uint32_t)(verts.size() / 8);
    appendU32(out, count);
    appendBytes(out, verts.data(), (size_t)count * 8 * sizeof(float));
    // AO is written only when it covers every vertex - a partial table would
    // make the loader guess.
    const bool hasAo = ao.size() == count && count > 0;
    appendU32(out, hasAo ? count : 0u);
    if (hasAo) appendBytes(out, ao.data(), count);
}

}  // namespace

std::string write(const Model& m) {
    std::string out;
    out.reserve(4096);
    out += "TMDL";
    appendU32(out, kVersion);
    for (int i = 0; i < 3; ++i) appendF32(out, m.min[i]);
    for (int i = 0; i < 3; ++i) appendF32(out, m.max[i]);
    appendU32(out, (uint32_t)m.parts.size());
    for (const Part& part : m.parts) {
        appendFixedString(out, part.name, 32);
        appendFixedString(out, part.texture, 64);
        appendFixedString(out, part.reflTexture, 64);
        for (int i = 0; i < 3; ++i) appendF32(out, part.kd[i]);
        for (int i = 0; i < 3; ++i) appendF32(out, part.ke[i]);
        appendF32(out, part.reflStrength);
        appendU32(out, part.reflRounded ? 1u : 0u);
        appendMesh(out, part.verts, part.ao);
        appendU32(out, (uint32_t)part.lods.size());
        for (const Lod& lod : part.lods) appendMesh(out, lod.verts, lod.ao);
    }
    const uint32_t shadowCorners = (uint32_t)(m.shadowVerts.size() / 3 / 3 * 3);
    appendU32(out, shadowCorners);
    appendBytes(out, m.shadowVerts.data(),
                (size_t)shadowCorners * 3 * sizeof(float));
    return out;
}

}  // namespace tmdl
