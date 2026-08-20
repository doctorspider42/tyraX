#include "vehbake.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>

#include "fbxparser.hpp"  // animimport::parseSkel - .glb and .fbx alike
#include "meshlod.hpp"

#include <stb_image_write.h>  // implementation lives in menubake.cpp

namespace vehbake {

namespace {

// --- a 4x4, column-major like everything else here --------------------------
struct M4 {
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

    static M4 mul(const M4& a, const M4& b) {
        M4 r;
        for (int c = 0; c < 4; ++c)
            for (int row = 0; row < 4; ++row) {
                float s = 0.0f;
                for (int k = 0; k < 4; ++k) s += a.m[k * 4 + row] * b.m[c * 4 + k];
                r.m[c * 4 + row] = s;
            }
        return r;
    }
    void point(const float in[3], float out[3]) const {
        for (int r = 0; r < 3; ++r)
            out[r] = m[r] * in[0] + m[4 + r] * in[1] + m[8 + r] * in[2] + m[12 + r];
    }
    void dir(const float in[3], float out[3]) const {
        for (int r = 0; r < 3; ++r)
            out[r] = m[r] * in[0] + m[4 + r] * in[1] + m[8 + r] * in[2];
    }
};

M4 localOf(const glbparser::SkelNode& n) {
    if (n.hasMatrix) {
        M4 r;
        std::memcpy(r.m, n.matrix, sizeof(r.m));
        return r;
    }
    // T * R * S from the quaternion, the same composition parseSkel's own
    // consumers use.
    const float x = n.r[0], y = n.r[1], z = n.r[2], w = n.r[3];
    M4 r;
    r.m[0] = (1 - 2 * (y * y + z * z)) * n.s[0];
    r.m[1] = (2 * (x * y + z * w)) * n.s[0];
    r.m[2] = (2 * (x * z - y * w)) * n.s[0];
    r.m[4] = (2 * (x * y - z * w)) * n.s[1];
    r.m[5] = (1 - 2 * (x * x + z * z)) * n.s[1];
    r.m[6] = (2 * (y * z + x * w)) * n.s[1];
    r.m[8] = (2 * (x * z + y * w)) * n.s[2];
    r.m[9] = (2 * (y * z - x * w)) * n.s[2];
    r.m[10] = (1 - 2 * (x * x + y * y)) * n.s[2];
    r.m[12] = n.t[0];
    r.m[13] = n.t[1];
    r.m[14] = n.t[2];
    return r;
}

// Global bind transform of every node. Resolves along the parent chain rather
// than in one forward pass, because glTF node order is arbitrary and a child
// may precede its parent - the same trap animmerge::poseGlobals paid for.
std::vector<M4> globals(const glbparser::Skel& sk) {
    std::vector<M4> out(sk.nodes.size());
    std::vector<bool> done(sk.nodes.size(), false);
    std::vector<int> chain;
    for (size_t i = 0; i < sk.nodes.size(); ++i) {
        chain.clear();
        int cur = (int)i;
        while (cur >= 0 && !done[cur]) {
            chain.push_back(cur);
            cur = sk.nodes[cur].parent;
        }
        for (int k = (int)chain.size() - 1; k >= 0; --k) {
            const int n = chain[k];
            const int p = sk.nodes[n].parent;
            out[n] = p >= 0 ? M4::mul(out[p], localOf(sk.nodes[n])) : localOf(sk.nodes[n]);
            done[n] = true;
        }
    }
    return out;
}

// Which node owns a part: the palette slot its first corner binds to. A part
// is one material, and in every vehicle seen so far a material belongs to one
// node - but a part spanning several nodes must not be silently split, so the
// dominant slot decides and the caller is told (see build's note).
int ownerNode(const glbparser::Skel& sk, const glbparser::SkelPart& p, bool* mixed) {
    if (p.vertexCount <= 0 || p.joints.empty()) {
        if (mixed) *mixed = false;
        return sk.palette.empty() ? 0 : sk.palette[0].node;
    }
    std::map<int, int> tally;
    for (int v = 0; v < p.vertexCount; ++v) {
        const int slot = p.joints[(size_t)v * 4];
        if (slot < (int)sk.palette.size()) ++tally[sk.palette[slot].node];
    }
    int best = tally.begin()->first, bestN = 0;
    for (const auto& kv : tally)
        if (kv.second > bestN) best = kv.first, bestN = kv.second;
    if (mixed) *mixed = tally.size() > 1;
    return best;
}

struct Corner {
    float p[3], n[3], uv[2];
};

}  // namespace

std::string binReflPath(const std::string& resRel) {
    if (resRel.rfind("res/", 0) == 0) return resRel.substr(4);
    return resRel;
}

std::vector<vehiclesim::MeshNode> meshNodes(const glbparser::Skel& sk) {
    const std::vector<M4> g = globals(sk);
    std::vector<vehiclesim::MeshNode> out(sk.nodes.size());
    for (size_t i = 0; i < sk.nodes.size(); ++i) {
        out[i].name = sk.nodes[i].name;
        for (int a = 0; a < 3; ++a) {
            out[i].mn[a] = 1e30f;
            out[i].mx[a] = -1e30f;
        }
    }
    for (const glbparser::SkelPart& p : sk.parts) {
        const int node = ownerNode(sk, p, nullptr);
        if (node < 0 || node >= (int)out.size()) continue;
        vehiclesim::MeshNode& mn = out[node];
        mn.vertexCount += p.vertexCount;
        mn.materials.push_back(p.material);
        for (int v = 0; v < p.vertexCount; ++v) {
            float w[3];
            g[node].point(&p.positions[(size_t)v * 3], w);
            for (int a = 0; a < 3; ++a) {
                mn.mn[a] = std::min(mn.mn[a], w[a]);
                mn.mx[a] = std::max(mn.mx[a], w[a]);
            }
        }
    }
    for (vehiclesim::MeshNode& mn : out)
        if (mn.vertexCount == 0)
            for (int a = 0; a < 3; ++a) mn.mn[a] = mn.mx[a] = 0.0f;
    return out;
}

bool inspect(const std::string& modelPath, vehiclesim::Detection& out,
             std::vector<vehiclesim::MeshNode>& nodes, std::string& error) {
    glbparser::Skel sk;
    if (!animimport::parseSkel(modelPath, sk, error)) return false;
    nodes = meshNodes(sk);
    out = vehiclesim::detectWheels(nodes);
    return true;
}

namespace {

// The canonical frame: forward +Z, up +Y, right +X. Built from the detection's
// axes so that everything downstream - the sim, the viewport preview and the
// generated runtime - is free of whatever frame the asset was authored in.
// This is the ONE place an exporter's opinion about axes is discarded.
M4 canonicalFromDetection(const vehiclesim::Detection& d) {
    float fwd[3] = {0, 0, 0}, up[3] = {0, 0, 0}, right[3] = {0, 0, 0};
    fwd[d.forwardAxis] = (float)d.forwardSign;
    up[d.upAxis] = 1.0f;
    right[0] = up[1] * fwd[2] - up[2] * fwd[1];
    right[1] = up[2] * fwd[0] - up[0] * fwd[2];
    right[2] = up[0] * fwd[1] - up[1] * fwd[0];
    // Rows, because this maps model space INTO the canonical basis: the new
    // x is the model vector dotted with right, y with up, z with forward.
    M4 r;
    r.m[0] = right[0], r.m[4] = right[1], r.m[8] = right[2];
    r.m[1] = up[0], r.m[5] = up[1], r.m[9] = up[2];
    r.m[2] = fwd[0], r.m[6] = fwd[1], r.m[10] = fwd[2];
    return r;
}

int triCount(const std::vector<float>& verts) { return (int)(verts.size() / 24); }

// Decimates an interleaved 8-float triangle list toward a triangle budget.
// keyNormals is FALSE and the normals are recomputed afterwards, per the trap
// meshlod's own header spells out: a static mesh derives a flat normal per
// face, so keying on the normal makes every position a seam twin, locks every
// collapse and decimates nothing at all.
void decimateTo(std::vector<float>& verts, int triBudget) {
    if (triBudget <= 0 || triCount(verts) <= triBudget) return;
    meshlod::Mesh m = meshlod::weldInterleaved(verts.data(), verts.size() / 8, false);
    // Budget is in triangles; the collapse takes a vertex target. A closed
    // manifold has roughly half as many vertices as triangles.
    const size_t target = std::max<size_t>(12, (size_t)triBudget / 2);
    if (m.vertexCount() <= target) return;
    meshlod::decimate(m, target);
    std::vector<float> out = meshlod::unweldInterleaved(m);
    if (out.size() >= 24) {
        meshlod::recomputeFaceNormals(out);
        verts.swap(out);
    }
}

// One merged, palette-textured part plus the palette itself.
struct Merge {
    // Source material colour -> its palette cell. Keyed on the quantised
    // colour, not the material NAME: this car has twelve materials and six
    // distinct colours, and merging by colour makes the palette that much
    // smaller for free.
    std::map<uint32_t, int> cell;
    std::vector<uint32_t> colours;

    int cellFor(const float kd[3]) {
        const uint32_t key = ((uint32_t)std::lround(std::min(1.0f, std::max(0.0f, kd[0])) * 255.0f) << 16) |
                             ((uint32_t)std::lround(std::min(1.0f, std::max(0.0f, kd[1])) * 255.0f) << 8) |
                             (uint32_t)std::lround(std::min(1.0f, std::max(0.0f, kd[2])) * 255.0f);
        auto it = cell.find(key);
        if (it != cell.end()) return it->second;
        const int idx = (int)colours.size();
        colours.push_back(key);
        cell[key] = idx;
        return idx;
    }
};

// Palette geometry: a ONE-DIMENSIONAL strip. Each colour owns a full-height
// COLUMN kCellPx wide, and every UV sits at v = 0.5.
//
// It started as a 2-D grid of blocks and that was a mistake worth recording,
// because the failure is invisible: a grid makes the colour depend on the V
// coordinate, and V's origin is a CONVENTION - top-left in the image file,
// bottom-left in GL, and the console's own flip is resolved somewhere else
// again. With the grid, an entire car rendered pure black against a palette
// that decoded perfectly and UVs that pointed at exactly the right cell
// centres, because the sampler read v = 0.125 from the other end and landed in
// the unwritten bottom of the image. Flipping V fixed some cells and not
// others, which is the sound of guessing.
//
// A strip cannot have that bug: there is no row to be off by, so no V
// convention - present, future, editor or GS - can select the wrong colour.
// The width is the only thing that grows, and 8 px per colour still leaves a
// 16-colour palette at 128x8, i.e. 4 KB.
constexpr int kCellPx = 8;
constexpr int kPaletteH = 8;  // POT, and the engine asserts POT sides

int paletteWidth(int cells) {
    int need = std::max(1, cells) * kCellPx;
    int pow2 = 8;
    while (pow2 < need && pow2 < 512) pow2 *= 2;
    return pow2;
}

void paletteUv(int cell, int width, float& u, float& v) {
    u = (cell * kCellPx + kCellPx * 0.5f) / (float)width;
    v = 0.5f;  // deliberately mid-strip: V carries no information at all
}

std::vector<unsigned char> paletteImage(const Merge& mg, int width) {
    std::vector<unsigned char> rgba((size_t)width * kPaletteH * 4, 0);
    for (size_t i = 0; i < mg.colours.size(); ++i) {
        const uint32_t c = mg.colours[i];
        for (int y = 0; y < kPaletteH; ++y)
            for (int x = 0; x < kCellPx; ++x) {
                const size_t px = (size_t)i * kCellPx + x;
                if ((int)px >= width) continue;
                const size_t o = ((size_t)y * width + px) * 4;
                if (o + 3 >= rgba.size()) continue;
                rgba[o + 0] = (unsigned char)((c >> 16) & 0xff);
                rgba[o + 1] = (unsigned char)((c >> 8) & 0xff);
                rgba[o + 2] = (unsigned char)(c & 0xff);
                // Opaque, and never 0: StaPip's alpha test discards alpha-0
                // texels, so a transparent palette cell renders as a hole.
                rgba[o + 3] = 255;
            }
    }
    return rgba;
}

std::vector<unsigned char> encodePng(const std::vector<unsigned char>& rgba, int width) {
    std::vector<unsigned char> out;
    stbi_write_png_to_func(
        [](void* ctx, void* data, int len) {
            auto* v = (std::vector<unsigned char>*)ctx;
            v->insert(v->end(), (unsigned char*)data, (unsigned char*)data + len);
        },
        &out, width, kPaletteH, 4, rgba.data(), width * 4);
    return out;
}

// Is this source material PAINT (shiny) or RUBBER/TRIM (matte)? Name first -
// an author who called something glass or rubber deserves to be obeyed - then
// luminance: near-black is bumper rubber, tyre sidewall, arch liner, none of
// which mirror the sky on a real car. The threshold is deliberately low
// (0.12 of full scale) so dark PAINT still shines; glass forces shiny by name
// because a deep-blue window would otherwise land under it.
bool shinyMaterial(const glbparser::SkelPart& p) {
    std::string n;
    for (char c : p.material) n += (char)(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    auto has = [&](const char* w) { return n.find(w) != std::string::npos; };
    if (has("glass") || has("window") || has("windshield") || has("szyb") ||
        has("chrome") || has("chrom") || has("mirror"))
        return true;
    if (has("rubber") || has("tyre") || has("tire") || has("guma") || has("trim"))
        return false;
    float lum = p.baseColor[0];
    if (p.baseColor[1] > lum) lum = p.baseColor[1];
    if (p.baseColor[2] > lum) lum = p.baseColor[2];
    return lum >= 0.12f;
}

// Collects a set of nodes' parts into a tmdl::Model in the canonical frame.
// `offset` is subtracted after the transform (the wheel bake puts its hub at
// the origin so the runtime can spin it).
//
// With `shineSplit` the untextured merge lands in TWO parts - "merged" (the
// paint and everything else that mirrors) and "merged-matte" (rubber and
// near-black trim, per shinyMaterial) - so the reflection pass can attach to
// one and not the other. tmdl reflection is PER PART, which is exactly what
// permits matte tyres at all; the price is one more submit, paid only when
// the definition actually asks for shine.
void collect(const glbparser::Skel& sk, const std::vector<M4>& g, const M4& canon,
             const std::vector<int>& nodes, const float offset[3], bool merge,
             const std::string& paletteTex, Merge& mg, tmdl::Model& out,
             int& srcParts, int& srcTris, bool shineSplit = false) {
    std::vector<float> mergedVerts;
    std::vector<float> matteVerts;
    // Textured materials keep their own part - real UVs cannot be rewritten.
    std::map<std::string, tmdl::Part> textured;

    for (size_t pi = 0; pi < sk.parts.size(); ++pi) {
        const glbparser::SkelPart& p = sk.parts[pi];
        if (p.vertexCount <= 0) continue;
        const int node = ownerNode(sk, p, nullptr);
        if (std::find(nodes.begin(), nodes.end(), node) == nodes.end()) continue;
        ++srcParts;
        srcTris += p.vertexCount / 3;

        const M4 xf = M4::mul(canon, g[node]);
        const bool isTextured = p.image >= 0;

        std::vector<float>* dst = nullptr;
        tmdl::Part* tp = nullptr;
        float u = 0.0f, v = 0.0f;
        if (isTextured || !merge) {
            const std::string key = isTextured ? ("img" + std::to_string(p.image)) : p.material;
            tmdl::Part& part = textured[key];
            if (part.name.empty()) {
                part.name = p.material;
                for (int a = 0; a < 3; ++a) part.kd[a] = p.baseColor[a];
            }
            tp = &part;
            dst = &part.verts;
            v = 0.0f;  // real UVs, nothing to patch later
        } else {
            // The palette CELL INDEX rides in the u slot with v = -1 as the
            // marker, and resolvePaletteUvs turns both into a real coordinate
            // once the palette's final size is known. The alternative is
            // walking every vertex of the car a second time to size the
            // palette before placing anything in it.
            dst = (shineSplit && !shinyMaterial(p)) ? &matteVerts : &mergedVerts;
            (void)tp;
            u = (float)mg.cellFor(p.baseColor);
            v = -1.0f;
        }

        for (int c = 0; c < p.vertexCount; ++c) {
            Corner k;
            xf.point(&p.positions[(size_t)c * 3], k.p);
            for (int a = 0; a < 3; ++a) k.p[a] -= offset[a];
            xf.dir(&p.normals[(size_t)c * 3], k.n);
            const float len = std::sqrt(k.n[0] * k.n[0] + k.n[1] * k.n[1] + k.n[2] * k.n[2]);
            if (len > 1e-8f)
                for (int a = 0; a < 3; ++a) k.n[a] /= len;
            if (v < 0.0f) {
                k.uv[0] = u;
                k.uv[1] = -1.0f;
            } else if (!p.uvs.empty()) {
                k.uv[0] = p.uvs[(size_t)c * 2];
                k.uv[1] = p.uvs[(size_t)c * 2 + 1];
            } else {
                k.uv[0] = k.uv[1] = 0.0f;
            }
            dst->insert(dst->end(), {k.p[0], k.p[1], k.p[2], k.n[0], k.n[1], k.n[2], k.uv[0],
                                     k.uv[1]});
        }
    }

    if (!mergedVerts.empty()) {
        tmdl::Part part;
        part.name = "merged";
        part.texture = paletteTex;
        part.kd[0] = part.kd[1] = part.kd[2] = 1.0f;  // the palette carries the colour
        part.verts.swap(mergedVerts);
        out.parts.push_back(std::move(part));
    }
    if (!matteVerts.empty()) {
        tmdl::Part part;
        part.name = "merged-matte";  // build() leaves this one un-mirrored
        part.texture = paletteTex;
        part.kd[0] = part.kd[1] = part.kd[2] = 1.0f;
        part.verts.swap(matteVerts);
        out.parts.push_back(std::move(part));
    }
    for (auto& kv : textured) out.parts.push_back(std::move(kv.second));
}

void resolvePaletteUvs(tmdl::Model& m, int width) {
    for (tmdl::Part& p : m.parts) {
        if (p.texture.empty()) continue;
        for (size_t v = 0; v + 7 < p.verts.size(); v += 8) {
            if (p.verts[v + 7] != -1.0f) continue;  // not a palette placeholder
            float u = 0.0f, vv = 0.0f;
            paletteUv((int)std::lround(p.verts[v + 6]), width, u, vv);
            p.verts[v + 6] = u;
            p.verts[v + 7] = vv;
        }
    }
}

void computeBounds(tmdl::Model& m) {
    float mn[3] = {1e30f, 1e30f, 1e30f}, mx[3] = {-1e30f, -1e30f, -1e30f};
    bool any = false;
    for (const tmdl::Part& p : m.parts)
        for (size_t v = 0; v + 7 < p.verts.size(); v += 8) {
            any = true;
            for (int a = 0; a < 3; ++a) {
                mn[a] = std::min(mn[a], p.verts[v + a]);
                mx[a] = std::max(mx[a], p.verts[v + a]);
            }
        }
    if (!any)
        for (int a = 0; a < 3; ++a) mn[a] = mx[a] = 0.0f;
    for (int a = 0; a < 3; ++a) m.min[a] = mn[a], m.max[a] = mx[a];
}

int modelTris(const tmdl::Model& m) {
    int n = 0;
    for (const tmdl::Part& p : m.parts) n += (int)(p.verts.size() / 24);
    return n;
}

}  // namespace

bool build(const std::string& modelPath, const Options& opt, Result& out,
           std::string& error) {
    glbparser::Skel sk;
    if (!animimport::parseSkel(modelPath, sk, error)) return false;

    const std::vector<vehiclesim::MeshNode> nodes = meshNodes(sk);
    out.detection = vehiclesim::detectWheels(nodes);
    out.notes = out.detection.notes;

    const std::vector<M4> g = globals(sk);
    const M4 canon = canonicalFromDetection(out.detection);

    // The palette is shared by the body and the wheel: one texture for the
    // whole vehicle, so the pair costs one VRAM allocation rather than two.
    Merge mg;
    const std::string paletteTex = opt.mergeUntextured ? opt.paletteTexture : "";

    // The body is re-origined to the AXLE CENTRE at HUB HEIGHT - the mean of
    // the wheel centres in the canonical frame. The sim places the wheel
    // anchors at +-wheelBase/2 and +-track/2 around the CHASSIS origin with
    // the hubs at the origin's height, so a body that keeps the model's own
    // origin puts every wheel wherever the exporter's pivot happened to be:
    // the reference car's origin sat 0.25 behind the axle midpoint, and all
    // four wheels rode visibly forward of their arches. With the origin at
    // hub height, rideHeight = wheelRadius puts the tyres exactly on the
    // ground and the arches line up vertically too.
    float bodyOrigin[3] = {0.0f, 0.0f, 0.0f};
    if (!out.detection.wheels.empty()) {
        for (const vehiclesim::Wheel& w : out.detection.wheels) {
            float h[3];
            canon.point(w.centre, h);
            for (int a = 0; a < 3; ++a) bodyOrigin[a] += h[a];
        }
        for (int a = 0; a < 3; ++a)
            bodyOrigin[a] /= (float)out.detection.wheels.size();
    }
    collect(sk, g, canon, out.detection.bodyNodes, bodyOrigin, opt.mergeUntextured,
            paletteTex, mg, out.body, out.srcParts, out.srcTris,
            /*shineSplit=*/opt.bodyShine > 0.001f);

    if (!out.detection.wheels.empty()) {
        // One wheel is baked, hub at the origin. Which one does not matter for
        // geometry - they are the same mesh - but it does for the OFFSET, so
        // the hub comes from this wheel's own centre in the canonical frame.
        const vehiclesim::Wheel& w = out.detection.wheels[0];
        float hub[3];
        canon.point(w.centre, hub);
        const std::vector<int> one{w.node};
        collect(sk, g, canon, one, hub, opt.mergeUntextured, paletteTex, mg, out.wheel,
                out.srcParts, out.srcTris);
    }

    if (!mg.colours.empty()) {
        out.paletteSize = paletteWidth((int)mg.colours.size());
        resolvePaletteUvs(out.body, out.paletteSize);
        resolvePaletteUvs(out.wheel, out.paletteSize);
        out.palettePng = encodePng(paletteImage(mg, out.paletteSize), out.paletteSize);
        char buf[180];
        std::snprintf(buf, sizeof(buf),
                      "Merged %d untextured materials into %zu palette colours "
                      "(%dx%d strip).",
                      out.srcParts, mg.colours.size(), out.paletteSize, kPaletteH);
        out.notes.push_back(buf);
    }

    // The paint's shine (docs/vehicles.md, "A shiny body"): the authored
    // sphere map, or the dynamic "@sky" when none is named. Applied to every
    // body part EXCEPT the matte merge (rubber and near-black trim - tmdl
    // reflection is per PART, which is what makes matte tyres possible at
    // all) and any textured part whose name says rubber. After the merge,
    // before the decimation - the fields ride the part, not the vertices.
    if (opt.bodyShine > 0.001f) {
        const std::string reflTex =
            opt.bodyReflMap.empty() ? std::string("@sky") : opt.bodyReflMap;
        for (tmdl::Part& p : out.body.parts) {
            if (p.name == "merged-matte") continue;
            std::string n2;
            for (char c : p.name)
                n2 += (char)(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
            if (n2.find("rubber") != std::string::npos ||
                n2.find("tyre") != std::string::npos ||
                n2.find("tire") != std::string::npos)
                continue;
            p.reflTexture = reflTex;
            p.reflStrength = opt.bodyShine > 1.0f ? 1.0f : opt.bodyShine;
        }
    }

    const int bodyBefore = modelTris(out.body), wheelBefore = modelTris(out.wheel);
    // The body budget covers the WHOLE body, split across its parts by their
    // share of the source - the shine split made this visible: a per-part
    // budget let a 2-part body carry 2002 triangles against an authored 1500.
    if (bodyBefore > 0)
        for (tmdl::Part& p : out.body.parts)
            decimateTo(p.verts,
                       (int)((long long)opt.bodyTriBudget * triCount(p.verts) /
                             bodyBefore));
    for (tmdl::Part& p : out.wheel.parts) decimateTo(p.verts, opt.wheelTriBudget);

    computeBounds(out.body);
    computeBounds(out.wheel);
    out.bodyParts = (int)out.body.parts.size();
    out.wheelParts = (int)out.wheel.parts.size();
    out.bodyTris = modelTris(out.body);
    out.wheelTris = modelTris(out.wheel);

    {
        char buf[220];
        std::snprintf(buf, sizeof(buf),
                      "Body %d -> %d tris in %d part(s); wheel %d -> %d tris in %d "
                      "part(s). Submits per vehicle: %d.",
                      bodyBefore, out.bodyTris, out.bodyParts, wheelBefore, out.wheelTris,
                      out.wheelParts, out.bodyParts + out.wheelParts);
        out.notes.push_back(buf);
    }

    // Seed the drive spec from what the model measured. Everything else keeps
    // the DriveSpec defaults, which are a mid-weight road car.
    out.spec.wheelBase = out.detection.wheelBase;
    out.spec.track = out.detection.track;
    out.spec.wheelRadius = out.detection.radius;
    // The bumper overhang, off the BAKED body: how far it reaches past the
    // axle line at either end (the body is re-origined to the axle centre,
    // forward is +Z). This is what the wall test adds to the wheelbase - the
    // axle rectangle alone let the bonnet clip a bumper's length into walls.
    {
        const float over = std::max(out.body.max[2], -out.body.min[2]) -
                           0.5f * out.detection.wheelBase;
        if (over > 0.0f) out.spec.bodyOverhang = over;
    }
    // Lamp clusters, off the model's own MATERIALS (docs/vehicles.md, "The
    // visual pack"): parts whose material name says lamp get their canonical
    // AABBs pooled into a rear (z < 0) and a front (z > 0) cluster, and the
    // runtime draws its glow AT those spots instead of guessing from the
    // wheelbase - the material says where the lamps are on THIS shape. No
    // lamp-named material = size 0 = the shape-blind fallback, so a model
    // authored before this existed changes nothing.
    {
        auto isLamp = [](const std::string& mat, bool* front) {
            std::string n;
            for (char c : mat)
                n += (char)(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
            auto has = [&](const char* w) {
                return n.find(w) != std::string::npos;
            };
            const bool lampish = has("lamp") || has("light") || has("brake") ||
                                 has("tail") || has("stop") || has("head") ||
                                 has("swiatl");
            if (!lampish) return false;
            *front = has("head") || has("front") || has("przod");
            return true;
        };
        float mnR[3] = {1e30f, 1e30f, 1e30f}, mxR[3] = {-1e30f, -1e30f, -1e30f};
        float mnF[3] = {1e30f, 1e30f, 1e30f}, mxF[3] = {-1e30f, -1e30f, -1e30f};
        bool anyR = false, anyF = false;
        const std::vector<M4> g2 = globals(sk);
        for (const glbparser::SkelPart& p : sk.parts) {
            bool front = false;
            if (!isLamp(p.material, &front)) continue;
            const int node = ownerNode(sk, p, nullptr);
            if (node < 0) continue;
            if (std::find(out.detection.bodyNodes.begin(),
                          out.detection.bodyNodes.end(),
                          node) == out.detection.bodyNodes.end())
                continue;
            const M4 xf = M4::mul(canon, g2[node]);
            for (int c = 0; c < p.vertexCount; ++c) {
                float w[3];
                xf.point(&p.positions[(size_t)c * 3], w);
                for (int a = 0; a < 3; ++a) w[a] -= bodyOrigin[a];
                // A "light" material can wrap the whole body on junk models;
                // classify by the VERTEX end when the name did not say.
                const bool isFront = front || w[2] > 0.0f;
                float* mn = isFront ? mnF : mnR;
                float* mx = isFront ? mxF : mxR;
                for (int a = 0; a < 3; ++a) {
                    if (w[a] < mn[a]) mn[a] = w[a];
                    if (w[a] > mx[a]) mx[a] = w[a];
                }
                (isFront ? anyF : anyR) = true;
            }
        }
        auto pack = [](const float* mn, const float* mx, float outv[4]) {
            outv[0] = 0.5f * (std::fabs(mn[0]) + std::fabs(mx[0]));  // |x| offset
            outv[1] = 0.5f * (mn[1] + mx[1]);
            outv[2] = 0.5f * (mn[2] + mx[2]);
            float sz = 0.5f * (mx[1] - mn[1]);
            const float sx = 0.25f * (mx[0] - mn[0]);
            if (sx > sz) sz = sx;
            outv[3] = sz > 0.04f ? sz : 0.04f;
        };
        if (anyR) pack(mnR, mxR, out.lampRear);
        if (anyF) pack(mnF, mxF, out.lampFront);
        if (anyR || anyF) {
            char buf[128];
            snprintf(buf, sizeof(buf),
                     "Lamp materials measured: rear %s, front %s.",
                     anyR ? "yes" : "no", anyF ? "yes" : "no");
            out.notes.push_back(buf);
        }
    }

    // The radius comes from the BAKED wheel, not from the detected one. A
    // quadric collapse pulls a round silhouette inward - measured at 0.380
    // against a detected 0.480 on the test car at a 200-triangle budget - and
    // the sim must ride on the wheel that is actually DRAWN, or the car floats
    // above the road and its wheels spin at the wrong rate. The detected value
    // is still what seeds the panel; this is what corrects it.
    if (!out.wheel.parts.empty()) {
        // Canonical frame: X is the axle, so the diameter is the larger of the
        // two remaining extents.
        const float dy = out.wheel.max[1] - out.wheel.min[1];
        const float dz = out.wheel.max[2] - out.wheel.min[2];
        const float baked = 0.5f * std::max(dy, dz);
        if (baked > 1e-4f) {
            const float shrink = out.detection.radius > 1e-4f
                                     ? 1.0f - baked / out.detection.radius
                                     : 0.0f;
            out.spec.wheelRadius = baked;
            if (shrink > 0.05f) {
                char buf[200];
                std::snprintf(buf, sizeof(buf),
                              "Decimation shrank the wheel %.0f%% (radius %.3f -> "
                              "%.3f); the sim uses the baked size. Raise the wheel "
                              "triangle budget to keep the rim round.",
                              shrink * 100.0f, out.detection.radius, baked);
                out.notes.push_back(buf);
            }
        }
    }
    if (out.spec.wheelRadius > 0.0f)
        out.spec.rideHeight = out.spec.wheelRadius;  // hub height off the ground
    return true;
}

// --- the build-path bake ----------------------------------------------------

BakedPaths pathsFor(const VehicleDef& v) {
    BakedPaths b;
    if (v.id.empty()) return b;
    const std::string stem = "vehicles/veh-" + v.id;
    b.body = stem + "-body.tmdl";
    b.wheel = stem + "-wheel.tmdl";
    b.palette = stem + "-palette.png";
    return b;
}

std::string bakeProject(const Project& p,
                        const std::function<void(const std::string&)>& log) {
    namespace fs = std::filesystem;
    std::string firstError;
    if (p.vehicles.empty()) return firstError;

    const fs::path dir = fs::path(p.dir) / ".res-baked" / "vehicles";
    std::error_code ec;
    fs::create_directories(dir, ec);

    // Content-compared: this runs on every build, and a fresh mtime on an asset
    // the compiler reads is a rebuild nobody asked for (the refreshGenerated
    // rule, which the binary bakes are held to as well).
    auto put = [&](const std::string& binRel, const std::string& bytes) {
        const fs::path out = fs::path(p.dir) / ".res-baked" / binRel;
        std::ifstream in(out, std::ios::binary);
        if (in) {
            const std::string old((std::istreambuf_iterator<char>(in)),
                                  std::istreambuf_iterator<char>());
            if (old == bytes) return;
        }
        in.close();
        std::ofstream(out, std::ios::binary)
            .write(bytes.data(), (std::streamsize)bytes.size());
    };

    for (const VehicleDef& v : p.vehicles) {
        if (v.modelPath.empty() || v.id.empty()) continue;
        const BakedPaths bp = pathsFor(v);
        Options opt;
        opt.bodyTriBudget = v.bodyTriBudget;
        opt.wheelTriBudget = v.wheelTriBudget;
        opt.mergeUntextured = v.mergeUntextured;
        opt.bodyShine = v.bodyShine;
        opt.bodyReflMap = binReflPath(v.bodyReflMap);
        opt.paletteTexture = bp.palette;
        Result r;
        std::string err;
        if (!build(p.filePath(v.modelPath), opt, r, err)) {
            const std::string line =
                "[vehicle] " + v.name + ": import failed - " + err;
            if (log) log(line);
            if (firstError.empty()) firstError = line;
            continue;
        }
        put(bp.body, tmdl::write(r.body));
        put(bp.wheel, tmdl::write(r.wheel));
        if (!r.palettePng.empty())
            put(bp.palette, std::string((const char*)r.palettePng.data(),
                                        r.palettePng.size()));
        if (log) {
            char buf[220];
            std::snprintf(buf, sizeof(buf),
                          "[vehicle] %s: body %d tris / %d part(s), wheel %d tris, "
                          "%d submit(s) per vehicle",
                          v.name.c_str(), r.bodyTris, r.bodyParts, r.wheelTris,
                          r.bodyParts + r.wheelParts);
            log(buf);
            // The MEASURED geometry, stated wherever a build log is read: the
            // numbers the definition should carry. The editor adopts them on
            // import, but only in the GUI tick - a project authored headless
            // keeps the struct defaults, and the reference example shipped
            // with track 1.40 against a 1.41-wide body (wheels riding fully
            // outside the arches) and a 0.32 radius against a 0.23 baked
            // wheel (the car floated) with nothing anywhere saying so.
            if (r.spec.track > 1e-4f || r.spec.wheelBase > 1e-4f) {
                std::snprintf(buf, sizeof(buf),
                              "[vehicle] %s: measured wheelBase %.3f track %.3f "
                              "radius %.3f rideHeight %.3f - the definition's "
                              "Driving tab should match",
                              v.name.c_str(), r.spec.wheelBase, r.spec.track,
                              r.spec.wheelRadius, r.spec.rideHeight);
                log(buf);
            }
        }
    }
    return firstError;
}

}  // namespace vehbake
