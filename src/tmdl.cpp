#include "tmdl.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <utility>

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

// The version-4 strip twin. Same shape as appendMesh, but an EMPTY mesh is
// legal here (a part that did not strip smaller than its list writes 0 and
// the game renders the list), so it cannot share the function.
void appendStrip(std::string& out, const std::vector<float>& verts,
                 const std::vector<unsigned char>& ao) {
    const uint32_t count = (uint32_t)(verts.size() / 8);
    appendU32(out, count);
    if (count == 0) {
        appendU32(out, 0u);
        return;
    }
    appendBytes(out, verts.data(), (size_t)count * 8 * sizeof(float));
    const bool hasAo = ao.size() == count;
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
        // Version 4: the strip twin of the base mesh and of every tier, in the
        // same order, after the lists so a reader of an older layout that
        // stops here still sees a complete part.
        appendU32(out, part.stripRun);
        appendStrip(out, part.stripVerts, part.stripAo);
        for (const Lod& lod : part.lods)
            appendStrip(out, lod.stripVerts, lod.stripAo);
    }
    const uint32_t shadowCorners = (uint32_t)(m.shadowVerts.size() / 3 / 3 * 3);
    appendU32(out, shadowCorners);
    appendBytes(out, m.shadowVerts.data(),
                (size_t)shadowCorners * 3 * sizeof(float));
    return out;
}

namespace {

// A bounds-checked cursor. Every read goes through it, so a truncated or
// hand-corrupted file fails the whole parse instead of walking off the end -
// the reader's one job beyond the layout itself.
struct Cursor {
    const std::string& s;
    size_t at = 0;
    bool bad = false;

    bool take(void* dst, size_t n) {
        if (bad || at + n > s.size()) return !(bad = true);
        if (dst) std::memcpy(dst, s.data() + at, n);
        at += n;
        return true;
    }
    bool skip(size_t n) { return take(nullptr, n); }
    uint32_t u32() {
        uint32_t v = 0;
        take(&v, 4);
        return v;
    }
    float f32() {
        float v = 0.0f;
        take(&v, 4);
        return v;
    }
    // A fixed NUL-padded field back to a std::string.
    std::string fixed(size_t size) {
        char buf[128] = {};
        if (size > sizeof(buf) - 1 || !take(buf, size)) return std::string();
        return std::string(buf);
    }
    // vertexCount + verts + optional AO. The vertices themselves are skipped
    // - the caller wants the COUNT, which is what a package count is derived
    // from - and the count is reported back through `count`.
    // The multiply is done in 64 bits: a corrupt count times 32 overflows a
    // 32-bit size and could wrap back INSIDE the buffer, which is a bounds
    // check that passes on a file that is nothing of the sort.
    bool skipMesh(uint32_t* count = nullptr) {
        const uint32_t n = u32();
        if (count) *count = n;
        if (!skip((size_t)((uint64_t)n * 8u * sizeof(float)))) return false;
        const uint64_t ao = u32();
        return skip((size_t)ao);
    }
};

}  // namespace

bool readInfo(const std::string& bytes, Info& out) {
    Cursor c{bytes};
    char magic[4] = {};
    if (!c.take(magic, 4) || std::memcmp(magic, "TMDL", 4) != 0) return false;
    const uint32_t version = c.u32();
    // The loader accepts a RANGE (tmdl.hpp), and so does this: the fields it
    // reads have been in the layout since v1 except stripRun, which is
    // defaulted to 0 below for anything older - "no strip", the honest answer
    // for a file baked before strips existed.
    if (version < 1 || version > kVersion) return false;

    Info info;
    for (int i = 0; i < 3; ++i) info.min[i] = c.f32();
    for (int i = 0; i < 3; ++i) info.max[i] = c.f32();
    const uint32_t partCount = c.u32();
    if (c.bad) return false;
    // A part is at least its fixed fields, so a count that could not fit in
    // what is left is rejected before it reserves anything on its word.
    if ((uint64_t)partCount * 160u > bytes.size()) return false;

    info.parts.reserve(partCount);
    for (uint32_t p = 0; p < partCount && !c.bad; ++p) {
        PartInfo part;
        part.name = c.fixed(32);
        part.texture = c.fixed(64);
        part.reflTexture = c.fixed(64);
        c.skip(3 * sizeof(float));  // kd
        if (version >= 2) c.skip(3 * sizeof(float));  // ke
        c.skip(sizeof(float));  // reflStrength
        c.skip(sizeof(uint32_t));  // flags
        if (!c.skipMesh(&part.vertexCount)) break;
        const uint32_t lodCount = c.u32();
        if (c.bad || lodCount > 8) return false;
        for (uint32_t l = 0; l < lodCount; ++l)
            if (!c.skipMesh()) break;
        if (version >= 4) {
            part.stripRun = c.u32();
            // The strip twin of the base mesh and of every tier, in order.
            // Tier 0's count is the one a package count is derived from; a
            // zero here is the legal "this part did not strip smaller than
            // its list" and means the game renders it as a list.
            if (!c.skipMesh(&part.stripVertexCount)) break;
            for (uint32_t l = 0; l < lodCount; ++l)
                if (!c.skipMesh()) break;
        }
        if (c.bad) break;
        info.parts.push_back(std::move(part));
    }
    if (c.bad || info.parts.size() != partCount) return false;
    out = std::move(info);
    return true;
}

}  // namespace tmdl
