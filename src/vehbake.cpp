#include <array>
#include "vehbake.hpp"

#include "particletex.hpp"

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
#include "meshstrip.hpp"
#include "particletex.hpp"
#include "pngquant.hpp"

#include <stb_image.h>
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

bool identityIbm(const float* m) {
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r) {
            const float want = c == r ? 1.0f : 0.0f;
            if (std::fabs(m[c * 4 + r] - want) > 1e-6f) return false;
        }
    return true;
}

// glbparser batches primitives by MATERIAL, so four rigid wheel nodes sharing
// one atlas arrive as one SkelPart. Their first palette slot still records the
// node of every corner. Distinguish that case from a genuinely skinned part:
// rigid corners have one full-weight identity-IBM slot, and every triangle is
// owned by one node. Returning false keeps the old dominant-owner behaviour
// for skins instead of mistaking their bones for separate vehicle meshes.
bool rigidOwners(const glbparser::Skel& sk, const glbparser::SkelPart& p) {
    if (p.vertexCount <= 0 || p.joints.size() != (size_t)p.vertexCount * 4 ||
        p.weights.size() != (size_t)p.vertexCount * 4)
        return false;
    for (int v = 0; v < p.vertexCount; ++v) {
        const size_t i = (size_t)v * 4;
        const int slot = p.joints[i];
        if (slot < 0 || slot >= (int)sk.palette.size() ||
            p.weights[i] != 255 || p.weights[i + 1] || p.weights[i + 2] ||
            p.weights[i + 3] || !identityIbm(sk.palette[(size_t)slot].ibm))
            return false;
    }
    for (int v = 0; v + 2 < p.vertexCount; v += 3) {
        const int a = sk.palette[p.joints[(size_t)v * 4]].node;
        const int b = sk.palette[p.joints[(size_t)(v + 1) * 4]].node;
        const int c = sk.palette[p.joints[(size_t)(v + 2) * 4]].node;
        if (a != b || a != c) return false;
    }
    return true;
}

int rigidCornerNode(const glbparser::Skel& sk, const glbparser::SkelPart& p,
                    int corner) {
    return sk.palette[p.joints[(size_t)corner * 4]].node;
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
        const bool splitRigid = rigidOwners(sk, p);
        const int fallback = ownerNode(sk, p, nullptr);
        std::vector<bool> materialAdded(out.size(), false);
        for (int v = 0; v < p.vertexCount; ++v) {
            const int node = splitRigid ? rigidCornerNode(sk, p, v) : fallback;
            if (node < 0 || node >= (int)out.size()) continue;
            vehiclesim::MeshNode& mn = out[(size_t)node];
            ++mn.vertexCount;
            if (!materialAdded[(size_t)node]) {
                mn.materials.push_back(p.material);
                materialAdded[(size_t)node] = true;
            }
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

// The vehicle source carries authored normals, but a collapse must discard
// them: its welded topology no longer matches their smoothing groups. Flat
// normals made a sensibly budgeted curved body read like folded cardboard.
// Rebuild a crease-aware normal instead: corners at the same position share
// light only while their faces are within 55 degrees, so fenders and tyres
// become round again without melting the bonnet, glass or panel edges.
void recomputeCreasedNormals(std::vector<float>& verts) {
    if (verts.size() < 24) return;
    meshlod::recomputeFaceNormals(verts);
    std::map<std::string, std::vector<size_t>> atPosition;
    for (size_t c = 0; c + 7 < verts.size(); c += 8) {
        std::string key(reinterpret_cast<const char*>(&verts[c]),
                        3 * sizeof(float));
        atPosition[key].push_back(c);
    }
    constexpr float kCosCrease = 0.57357644f;  // cos(55 degrees)
    for (const auto& [key, corners] : atPosition) {
        (void)key;
        for (size_t c : corners) {
            const float bx = verts[c + 3], by = verts[c + 4], bz = verts[c + 5];
            float nx = 0.0f, ny = 0.0f, nz = 0.0f;
            for (size_t n : corners) {
                const float dot = bx * verts[n + 3] + by * verts[n + 4] +
                                  bz * verts[n + 5];
                if (dot < kCosCrease) continue;
                nx += verts[n + 3];
                ny += verts[n + 4];
                nz += verts[n + 5];
            }
            const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
            if (len > 1e-8f) {
                verts[c + 3] = nx / len;
                verts[c + 4] = ny / len;
                verts[c + 5] = nz / len;
            }
        }
    }
}

// Decimates an interleaved 8-float triangle list toward a triangle budget.
// keyNormals is FALSE and the normals are recomputed afterwards, per the trap
// meshlod's own header spells out: a static mesh derives a flat normal per
// face, so keying on the normal makes every position a seam twin, locks every
// collapse and decimates nothing at all.
void decimateTo(std::vector<float>& verts, int triBudget) {
    if (triBudget <= 0 || triCount(verts) <= triBudget) return;
    meshlod::Mesh m = meshlod::weldInterleaved(verts.data(), verts.size() / 8, false);
    // Budget is in triangles; the collapse takes a vertex target. The old
    // fixed `/ 2` conversion only fits a closed manifold. A vehicle merged
    // from many material shells has a very different triangle/vertex ratio,
    // so a requested 2400-triangle body came out at 1256 and the slider felt
    // brutally non-linear. Measure this mesh's own ratio instead.
    const double trisPerVert =
        (double)(m.tris.size() / 3) / (double)std::max<size_t>(m.vertexCount(), 1);
    const size_t target = std::max<size_t>(
        12, (size_t)std::ceil((double)triBudget /
                              std::max(trisPerVert, 0.01)));
    if (m.vertexCount() <= target) return;
    meshlod::decimate(m, target);
    std::vector<float> out = meshlod::unweldInterleaved(m);
    if (out.size() >= 24) {
        recomputeCreasedNormals(out);
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

std::vector<unsigned char> encodePng(const std::vector<unsigned char>& rgba,
                                     int width, int height = kPaletteH) {
    std::vector<unsigned char> out;
    stbi_write_png_to_func(
        [](void* ctx, void* data, int len) {
            auto* v = (std::vector<unsigned char>*)ctx;
            v->insert(v->end(), (unsigned char*)data, (unsigned char*)data + len);
        },
        &out, width, height, 4, rgba.data(), width * 4);
    return out;
}

// Is this source material PAINT (shiny) or RUBBER/TRIM (matte)? Name first -
// an author who called something glass or rubber deserves to be obeyed - then
// luminance: near-black is bumper rubber, tyre sidewall, arch liner, none of
// which mirror the sky on a real car. The threshold is deliberately low
// (0.12 of full scale) so dark PAINT still shines; glass forces shiny by name
// because a deep-blue window would otherwise land under it.
// Is this a LAMP material, and which end? (docs/vehicles.md, "The visual
// pack"). Shared by the AABB measuring pass and the part split - one
// definition of "lamp" or the two drift.
bool lampMaterial(const std::string& mat, bool* front) {
    std::string n;
    for (char c : mat) n += (char)(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    auto has = [&](const char* w) { return n.find(w) != std::string::npos; };
    const bool lampish = has("lamp") || has("light") || has("brake") ||
                         has("tail") || has("stop") || has("head") ||
                         has("swiatl");
    if (!lampish) return false;
    *front = has("head") || has("front") || has("przod");
    return true;
}

// Is this a GLASS material? The same words shinyMaterial obeys - one
// vocabulary, so a window that shines is the window that turns translucent.
bool glassMaterial(const std::string& mat) {
    std::string n;
    for (char c : mat) n += (char)(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    return n.find("glass") != std::string::npos ||
           n.find("window") != std::string::npos ||
           n.find("windshield") != std::string::npos ||
           n.find("szyb") != std::string::npos;
}

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
//
// Lamp-named materials (lampMaterial) leave the merge too, whenever merging
// is on at all - shine or no shine - and land in ONE part, "lamps", rear
// corners first; `lampRearVertsOut` receives the rear range's corner count so
// the runtime can brighten the two ranges separately (docs/vehicles.md, "The
// visual pack").
void collect(const glbparser::Skel& sk, const std::vector<M4>& g, const M4& canon,
             const std::vector<int>& nodes, const float offset[3], bool merge,
             const std::string& paletteTex, const std::vector<std::string>& imagePaths,
             Merge& mg, tmdl::Model& out,
             int& srcParts, int& srcTris, bool shineSplit = false,
             int* lampRearVertsOut = nullptr, bool glassSplit = false,
             std::vector<int>* glassCellsOut = nullptr) {
    std::vector<float> mergedVerts;
    std::vector<float> glassVerts;
    std::vector<float> matteVerts;
    std::vector<float> lampRearVerts, lampFrontVerts;
    float lampRearKd[3] = {-1.0f, 0.0f, 0.0f};
    float lampFrontKd[3] = {-1.0f, 0.0f, 0.0f};
    std::string lampTexture;
    // Textured materials keep their own part - real UVs cannot be rewritten.
    std::map<std::string, tmdl::Part> textured;

    for (size_t pi = 0; pi < sk.parts.size(); ++pi) {
        const glbparser::SkelPart& p = sk.parts[pi];
        if (p.vertexCount <= 0) continue;
        const bool splitRigid = rigidOwners(sk, p);
        const int fallback = ownerNode(sk, p, nullptr);
        int selectedCorners = 0;
        for (int c = 0; c + 2 < p.vertexCount; c += 3) {
            const int node = splitRigid ? rigidCornerNode(sk, p, c) : fallback;
            if (std::find(nodes.begin(), nodes.end(), node) != nodes.end())
                selectedCorners += 3;
        }
        if (!selectedCorners) continue;
        ++srcParts;
        srcTris += selectedCorners / 3;

        const bool isTextured = p.image >= 0;

        std::vector<float>* dst = nullptr;
        tmdl::Part* tp = nullptr;
        float u = 0.0f, v = 0.0f;
        // Lamp identity beats the ordinary image merge. Atlas-authored cars
        // commonly put paint and both lamp materials in ONE image; merging by
        // image first would bury their rear/front corner ranges inside the
        // body part, then a body strip would reorder them. A lamp image may be
        // shared by front and rear, but one runtime part cannot bind two
        // different images, so a later lamp on another image stays an ordinary
        // textured part instead of sampling the wrong texture.
        bool lampFront = false;
        const bool lamp = merge && lampMaterial(p.material, &lampFront) &&
                          (!isTextured || lampTexture.empty() ||
                           lampTexture == imagePaths[(size_t)p.image]);
        // Translucent glass: an untextured glass material keeps its palette
        // cell (so its colour is still the palette's) but lands in a part of
        // its own, which the runtime can give an alpha and draw last.
        const bool glass = merge && glassSplit && !isTextured && !lamp &&
                           glassMaterial(p.material);
        if (glass) {
            dst = &glassVerts;
            u = (float)mg.cellFor(p.baseColor);
            v = -1.0f;
        } else if (lamp) {
            dst = lampFront ? &lampFrontVerts : &lampRearVerts;
            float* lkd = lampFront ? lampFrontKd : lampRearKd;
            if (lkd[0] < 0.0f)
                for (int a = 0; a < 3; ++a) lkd[a] = p.baseColor[a];
            if (isTextured && lampTexture.empty())
                lampTexture = imagePaths[(size_t)p.image];
            u = v = 0.0f;
        } else if (isTextured || !merge) {
            const std::string key = isTextured ? ("img" + std::to_string(p.image)) : p.material;
            tmdl::Part& part = textured[key];
            if (part.name.empty()) {
                part.name = p.material;
                if (isTextured) part.texture = imagePaths[(size_t)p.image];
                for (int a = 0; a < 3; ++a) part.kd[a] = p.baseColor[a];
            }
            tp = &part;
            dst = &part.verts;
            v = 0.0f;  // real UVs, nothing to patch later
        } else {
            // A merged glass material: remember its cell, so the piece
            // classifier can still tell its triangles apart after the merge.
            if (glassCellsOut && glassMaterial(p.material))
                glassCellsOut->push_back(mg.cellFor(p.baseColor));
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

        for (int tri = 0; tri + 2 < p.vertexCount; tri += 3) {
            const int node =
                splitRigid ? rigidCornerNode(sk, p, tri) : fallback;
            if (std::find(nodes.begin(), nodes.end(), node) == nodes.end())
                continue;
            const M4 xf = M4::mul(canon, g[(size_t)node]);
            for (int c = tri; c < tri + 3; ++c) {
                Corner k;
                xf.point(&p.positions[(size_t)c * 3], k.p);
                for (int a = 0; a < 3; ++a) k.p[a] -= offset[a];
                xf.dir(&p.normals[(size_t)c * 3], k.n);
                const float len = std::sqrt(k.n[0] * k.n[0] +
                                            k.n[1] * k.n[1] +
                                            k.n[2] * k.n[2]);
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
                dst->insert(dst->end(),
                            {k.p[0], k.p[1], k.p[2], k.n[0], k.n[1],
                             k.n[2], k.uv[0], k.uv[1]});
            }
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
    // The lamp part LAST, so the part INDEX the definition records survives
    // every rebake of the same model; rear corners first, then front, and the
    // split point goes out through lampRearVertsOut. Untextured, and ke = kd:
    // FULLBRIGHT - a lamp is a light source, the scene's shading must never
    // darken it (the era's fullbright trick). The part's kd is the rear
    // lamp's colour (the front's when a model marks only those); the runtime
    // overwrites both ranges every frame, so kd is what the FIRST frame and
    // any host reader that ignores the ranges see, nothing more.
    if (lampRearVertsOut) *lampRearVertsOut = (int)(lampRearVerts.size() / 8);
    if (!lampRearVerts.empty() || !lampFrontVerts.empty()) {
        tmdl::Part part;
        part.name = "lamps";
        part.texture = lampTexture;
        const float* lkd = lampRearVerts.empty() ? lampFrontKd : lampRearKd;
        for (int a = 0; a < 3; ++a) {
            part.kd[a] = lkd[0] < 0.0f ? 1.0f : lkd[a];
            part.ke[a] = part.kd[a];
        }
        part.verts.swap(lampRearVerts);
        part.verts.insert(part.verts.end(), lampFrontVerts.begin(),
                          lampFrontVerts.end());
        out.parts.push_back(std::move(part));
    }
    // Glass after the lamps: a fixed place the definition's glassPart can
    // record, and the part the runtime skips in the object pass.
    if (!glassVerts.empty()) {
        tmdl::Part part;
        part.name = "glass";
        part.texture = paletteTex;
        part.kd[0] = part.kd[1] = part.kd[2] = 1.0f;
        part.verts.swap(glassVerts);
        out.parts.push_back(std::move(part));
    }
    for (auto& kv : textured) out.parts.push_back(std::move(kv.second));
}

void resolvePaletteUvs(tmdl::Model& m, int width, const std::string& paletteTex) {
    for (tmdl::Part& p : m.parts) {
        if (p.texture.empty() || p.texture != paletteTex) continue;
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

// A 128px top-down occupancy bake. This is deliberately geometry-only: the
// vehicle's paint and windows must not punch holes in the shadow, and a mask
// made from the canonical body stays aligned with the runtime's yaw. Two box
// blur passes give the hard raster a small era-appropriate penumbra while the
// opaque centre keeps the Burnout-like silhouette readable.
std::vector<unsigned char> shadowImage(const tmdl::Model& body) {
    constexpr int S = 128;
    std::vector<unsigned char> mask((size_t)S * S, 0);
    const float dx = body.max[0] - body.min[0];
    const float dz = body.max[2] - body.min[2];
    if (!(dx > 1e-5f) || !(dz > 1e-5f)) return {};
    constexpr float pad = 4.0f;
    auto px = [&](float x) { return pad + (x - body.min[0]) / dx * (S - 1 - 2 * pad); };
    auto py = [&](float z) { return pad + (z - body.min[2]) / dz * (S - 1 - 2 * pad); };
    auto edge = [](float ax, float ay, float bx, float by, float x, float y) {
        return (x - ax) * (by - ay) - (y - ay) * (bx - ax);
    };
    for (const tmdl::Part& part : body.parts)
        for (size_t i = 0; i + 23 < part.verts.size(); i += 24) {
            const float x0 = px(part.verts[i]),      y0 = py(part.verts[i + 2]);
            const float x1 = px(part.verts[i + 8]),  y1 = py(part.verts[i + 10]);
            const float x2 = px(part.verts[i + 16]), y2 = py(part.verts[i + 18]);
            const float area = edge(x0, y0, x1, y1, x2, y2);
            if (std::fabs(area) < 1e-5f) continue;
            const int xa = std::max(0, (int)std::floor(std::min({x0, x1, x2})));
            const int xb = std::min(S - 1, (int)std::ceil(std::max({x0, x1, x2})));
            const int ya = std::max(0, (int)std::floor(std::min({y0, y1, y2})));
            const int yb = std::min(S - 1, (int)std::ceil(std::max({y0, y1, y2})));
            for (int y = ya; y <= yb; ++y)
                for (int x = xa; x <= xb; ++x) {
                    const float fx = x + 0.5f, fy = y + 0.5f;
                    const float a = edge(x0, y0, x1, y1, fx, fy);
                    const float b = edge(x1, y1, x2, y2, fx, fy);
                    const float c = edge(x2, y2, x0, y0, fx, fy);
                    if ((a >= 0 && b >= 0 && c >= 0) ||
                        (a <= 0 && b <= 0 && c <= 0))
                        mask[(size_t)y * S + x] = 255;
                }
        }
    std::vector<unsigned char> tmp(mask.size());
    for (int pass = 0; pass < 2; ++pass) {
        for (int y = 0; y < S; ++y)
            for (int x = 0; x < S; ++x) {
                int sum = 0, n = 0;
                for (int oy = -1; oy <= 1; ++oy)
                    for (int ox = -1; ox <= 1; ++ox) {
                        const int qx = x + ox, qy = y + oy;
                        if (qx < 0 || qx >= S || qy < 0 || qy >= S) continue;
                        sum += mask[(size_t)qy * S + qx];
                        ++n;
                    }
                tmp[(size_t)y * S + x] = (unsigned char)(sum / n);
            }
        mask.swap(tmp);
    }
    std::vector<unsigned char> rgba((size_t)S * S * 4, 255);
    for (size_t i = 0; i < mask.size(); ++i) {
        rgba[i * 4 + 0] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = 255;
        rgba[i * 4 + 3] = mask[i];
    }
    return encodePng(rgba, S, S);
}

int modelTris(const tmdl::Model& m) {
    int n = 0;
    for (const tmdl::Part& p : m.parts) n += (int)(p.verts.size() / 24);
    return n;
}

// Decoded RGBA of a PNG, for the pixel-equality test below; empty = unreadable.
std::vector<unsigned char> decodeRgba(const std::vector<unsigned char>& png, int& w,
                                      int& h) {
    int n = 0;
    unsigned char* px = stbi_load_from_memory(png.data(), (int)png.size(), &w, &h, &n, 4);
    if (!px) return {};
    std::vector<unsigned char> out(px, px + (size_t)w * h * 4);
    stbi_image_free(px);
    return out;
}

// THE AUTHORED FAR MODEL (docs/vehicles.md, "An authored far model"). A
// second file, authored in the SAME space as the full model, whose every
// triangle - wheels included, at their rest spots - becomes the far tier of
// the body part it samples. So it is collected with the FULL model's
// canonical frame and origin (never its own detection: a far model is free to
// merge its wheels into the body) and its images are matched to the body's by
// PIXELS - an exporter re-encodes a PNG, so bytes would not do. A textured
// material whose image the body does not use would need a second texture at
// distance, which is VRAM the far tier exists to save: it is dropped with a
// note. Untextured materials take palette cells in the SAME merge as the
// body's, so they must be collected before the palette is sized.
//
// The lamps part is never hidden (build() explains), so a far model must leave
// room for the full model's lamps where they are - recess the grille and tail
// panel the way the full body does.
//
// Returns false (and leaves `farVerts` empty) when nothing usable came out;
// on success `farVerts[k]` is body part k's far mesh, empty for the parts the
// far model does not reach.
bool collectFarModel(const std::string& path, const M4& canon, const float origin[3],
                     const tmdl::Model& body,
                     const std::vector<Result::Texture>& bodyTextures,
                     const std::string& paletteTex, Merge& mg,
                     std::vector<std::vector<float>>& farVerts,
                     std::vector<std::string>& notes) {
    farVerts.assign(body.parts.size(), {});
    glbparser::Skel fsk;
    std::string err;
    if (!animimport::parseSkel(path, fsk, err)) {
        notes.push_back("Far model: " + err + " - the decimated tiers stay.");
        return false;
    }
    const auto eligible = [](const tmdl::Part& p) {
        return p.name != "lamps" && p.name != "glass";
    };
    // Each far image -> the body texture path it is pixel-equal to ("" = none).
    std::vector<std::string> farImagePaths(fsk.images.size());
    for (size_t i = 0; i < fsk.images.size(); ++i) {
        int fw = 0, fh = 0;
        const std::vector<unsigned char> fpx = decodeRgba(fsk.images[i].png, fw, fh);
        if (fpx.empty()) continue;
        for (const Result::Texture& t : bodyTextures) {
            int bw = 0, bh = 0;
            if (decodeRgba(t.png, bw, bh) == fpx && bw == fw && bh == fh) {
                farImagePaths[i] = t.path;
                break;
            }
        }
    }
    std::vector<int> all;
    const std::vector<vehiclesim::MeshNode> nodes = meshNodes(fsk);
    for (int i = 0; i < (int)nodes.size(); ++i)
        if (nodes[(size_t)i].vertexCount > 0) all.push_back(i);
    tmdl::Model fm;
    int srcParts = 0, srcTris = 0;
    collect(fsk, globals(fsk), canon, all, origin, /*merge=*/true, paletteTex,
            farImagePaths, mg, fm, srcParts, srcTris);
    int dropped = 0;
    for (tmdl::Part& fp : fm.parts) {
        int target = -1;
        for (size_t k = 0; k < body.parts.size() && target < 0; ++k)
            if (eligible(body.parts[k]) && !fp.texture.empty() &&
                body.parts[k].texture == fp.texture)
                target = (int)k;
        // "lamps" comes out of collect with the lamp image (or none); an
        // untextured lamp colour is an ordinary palette colour here, because
        // nothing brightens a far tier's lamps - the glow sprites still do.
        if (target < 0 && fp.texture.empty() && fp.name == "lamps")
            for (size_t k = 0; k < body.parts.size() && target < 0; ++k)
                if (eligible(body.parts[k]) && body.parts[k].texture == paletteTex &&
                    !paletteTex.empty())
                    target = (int)k;
        if (target < 0) {
            dropped += triCount(fp.verts);
            continue;
        }
        if (fp.name == "lamps" && fp.texture.empty()) {
            // Its corners carry real (0,0) UVs, not palette placeholders:
            // give them the lamp colour's cell like any untextured material.
            float u = (float)mg.cellFor(fp.kd);
            for (size_t c = 0; c + 7 < fp.verts.size(); c += 8)
                fp.verts[c + 6] = u, fp.verts[c + 7] = -1.0f;
        }
        std::vector<float>& dst = farVerts[(size_t)target];
        dst.insert(dst.end(), fp.verts.begin(), fp.verts.end());
    }
    if (dropped > 0) {
        char buf[260];
        std::snprintf(buf, sizeof(buf),
                      "Far model: %d triangles sample an image the body does not "
                      "use (or none the body has a part for) - dropped. Author "
                      "them into the body's own texture or palette colours.",
                      dropped);
        notes.push_back(buf);
    }
    for (const std::vector<float>& v : farVerts)
        if (!v.empty()) return true;
    notes.push_back("Far model: nothing in it matched a body part - the decimated "
                    "tiers stay.");
    farVerts.clear();
    return false;
}

}  // namespace

bool build(const std::string& modelPath, const Options& opt, Result& out,
           std::string& error) {
    out = Result{};
    glbparser::Skel sk;
    if (!animimport::parseSkel(modelPath, sk, error)) return false;

    std::vector<std::string> imagePaths(sk.images.size());
    const std::string imageStem = opt.paletteTexture.empty()
                                     ? "vehicle"
                                     : std::filesystem::path(opt.paletteTexture).replace_extension().generic_string();
    for (const auto& part : sk.parts) {
        if (part.image < 0) continue;
        if ((size_t)part.image >= sk.images.size() || sk.images[(size_t)part.image].png.empty()) {
            error = "Vehicle material has no readable source texture: " + part.material;
            return false;
        }
        auto& path = imagePaths[(size_t)part.image];
        if (!path.empty()) continue;
        path = imageStem + "-image-" + std::to_string(part.image) + ".png";
        out.textures.push_back({path, sk.images[(size_t)part.image].png});
    }

    const std::vector<vehiclesim::MeshNode> nodes = meshNodes(sk);
    // A named fast-wheel node takes no part in the detection: with no
    // vertices it is neither body nor wheel (detectWheels only reasons about
    // geometry), and the indices stay the ones collect() is handed below.
    int fastNode = -1;
    if (!opt.fastWheel.empty() && opt.fastWheel != "@auto") {
        for (int i = 0; i < (int)nodes.size(); ++i)
            if (nodes[i].name == opt.fastWheel && nodes[i].vertexCount > 0) fastNode = i;
    }
    std::vector<vehiclesim::MeshNode> detNodes = nodes;
    if (fastNode >= 0) detNodes[(size_t)fastNode].vertexCount = 0;
    out.detection = vehiclesim::detectWheels(detNodes);
    out.notes = out.detection.notes;

    const std::vector<M4> g = globals(sk);
    const M4 canon = canonicalFromDetection(out.detection);

    // The palette is shared by the body and the wheel: one texture for the
    // whole vehicle, so the pair costs one VRAM allocation rather than two.
    Merge mg;
    std::vector<std::array<float, 2>> glassUvs;  // merged glass cells, resolved
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
    int lampRearVerts = 0;
    std::vector<int> glassCells;
    collect(sk, g, canon, out.detection.bodyNodes, bodyOrigin, opt.mergeUntextured,
            paletteTex, imagePaths, mg, out.body, out.srcParts, out.srcTris,
            /*shineSplit=*/opt.bodyShine > 0.001f, &lampRearVerts,
            opt.glassSplit, &glassCells);

    if (!out.detection.wheels.empty()) {
        // One wheel is baked, hub at the origin. Which one does not matter for
        // geometry - they are the same mesh - but it does for the OFFSET, so
        // the hub comes from this wheel's own centre in the canonical frame.
        const vehiclesim::Wheel& w = out.detection.wheels[0];
        float hub[3];
        canon.point(w.centre, hub);
        const std::vector<int> one{w.node};
        collect(sk, g, canon, one, hub, opt.mergeUntextured, paletteTex, imagePaths, mg, out.wheel,
                out.srcParts, out.srcTris);

        // THE FAST WHEEL, into the SAME merge `mg` so it samples the same
        // palette texture as the ordinary one: the wheel batch is one bag per
        // definition with one texture, and a car swaps all four wheels at once.
        // "@auto" is the ordinary wheel again, taken before any decimation so
        // its own budget decides its resolution.
        if (opt.fastWheel == "@auto") {
            out.fastWheel = out.wheel;
        } else if (fastNode >= 0) {
            const vehiclesim::MeshNode& fn = nodes[(size_t)fastNode];
            const float centre[3] = {fn.centre(0), fn.centre(1), fn.centre(2)};
            float fhub[3];
            canon.point(centre, fhub);
            int ignoredParts = 0, ignoredTris = 0;
            collect(sk, g, canon, std::vector<int>{fastNode}, fhub, opt.mergeUntextured,
                    paletteTex, imagePaths, mg, out.fastWheel, ignoredParts, ignoredTris);
        } else if (!opt.fastWheel.empty()) {
            out.notes.push_back("Fast wheel: no mesh node named \"" + opt.fastWheel +
                                "\" in the model - the car keeps one wheel model.");
        }
    }

    // The authored far model, before the palette is sized: its untextured
    // materials take cells in the same merge.
    std::vector<std::vector<float>> farVerts;
    const bool farAuthored =
        !opt.farModel.empty() &&
        collectFarModel(opt.farModel, canon, bodyOrigin, out.body, out.textures,
                        paletteTex, mg, farVerts, out.notes);

    if (!mg.colours.empty()) {
        out.paletteSize = paletteWidth((int)mg.colours.size());
        resolvePaletteUvs(out.body, out.paletteSize, paletteTex);
        resolvePaletteUvs(out.wheel, out.paletteSize, paletteTex);
        // Palette parts only: a real V of -1 is a valid atlas coordinate.
        for (size_t k = 0; k < farVerts.size(); ++k)
            for (size_t c = 0; c + 7 < farVerts[k].size(); c += 8) {
                std::vector<float>& fv = farVerts[k];
                if (out.body.parts[k].texture != paletteTex || fv[c + 7] != -1.0f)
                    continue;
                paletteUv((int)std::lround(fv[c + 6]), out.paletteSize, fv[c + 6],
                          fv[c + 7]);
            }
        if (opt.fastWheel != "@auto")  // "@auto" copied already-resolved UVs
            resolvePaletteUvs(out.fastWheel, out.paletteSize, paletteTex);
        out.palettePng = encodePng(paletteImage(mg, out.paletteSize), out.paletteSize);
        for (int cell : glassCells) {
            float gu = 0.0f, gv = 0.0f;
            paletteUv(cell, out.paletteSize, gu, gv);
            glassUvs.push_back({gu, gv});
        }
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
            if (p.name == "lamps") continue;  // lights, not paint
            // The env pass is drawn inline in the object loop, the translucent
            // glass at the frame's tail - a reflection on it would land under
            // a pane drawn later. Translucency wins.
            if (p.name == "glass") continue;
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

    // The mask needs the pre-decimation footprint; computeBounds is repeated
    // after the final tiers below because decimation remains free to move the
    // visible model's extrema.
    computeBounds(out.body);
    out.shadowPng = shadowImage(out.body);

    const int bodyBefore = modelTris(out.body), wheelBefore = modelTris(out.wheel);
    // The body budget covers the WHOLE body, split across its parts by their
    // share of the source - the shine split made this visible: a per-part
    // budget let a 2-part body carry 2002 triangles against an authored 1500.
    // The lamp part is NEVER decimated: a collapse reorders its corners and
    // the rear/front split is a corner index. It is a few dozen triangles.
    if (bodyBefore > 0)
        for (tmdl::Part& p : out.body.parts)
            if (p.name != "lamps" && p.name != "glass")
                decimateTo(p.verts,
                           (int)((long long)opt.bodyTriBudget * triCount(p.verts) /
                                 bodyBefore));
    for (tmdl::Part& p : out.wheel.parts) decimateTo(p.verts, opt.wheelTriBudget);
    for (tmdl::Part& p : out.fastWheel.parts) decimateTo(p.verts, opt.fastWheelTriBudget);
    // The wheel batch draws parts[0] of either model through ONE texture, so a
    // fast wheel that would sample a different image cannot share the bag.
    if (!out.fastWheel.parts.empty() && !out.wheel.parts.empty() &&
        out.fastWheel.parts[0].texture != out.wheel.parts[0].texture) {
        out.notes.push_back("Fast wheel: its material samples a different texture "
                            "than the wheel's, and the four wheels are one submit "
                            "with one texture - dropped. Give it the wheel's "
                            "material (or an untextured one).");
        out.fastWheel = tmdl::Model{};
    }

    // THE FAR TIERS (docs/vehicles.md, "Distant vehicles"): the body's paint
    // part gets the two ordinary distance tiers, and each of them carries
    // the four WHEELS at their rest anchors - decimated hard, hub-centred
    // like the wheel bake leaves them, at (+-track/2, 0, +-wheelBase/2) in
    // the body frame (whose origin IS the axle centre at hub height). At
    // the LOD distance the generic model machinery swaps the body to that
    // tier and the runtime stops submitting the wheel bag, so a distant car
    // is ONE submit instead of two to four - and it still has wheels, which
    // the old 70-unit "skip the wheels" rule did not give it. The matte
    // trim tiers itself; the lamps stay tier 0 (a corner range must not be
    // reordered). Palette UVs are already resolved on both models by now,
    // and the wheel shares the body's palette, so its corners can simply be
    // appended.
    //
    // An AUTHORED far model (opt.farModel) replaces all of this: one tier per
    // body part it reaches, wheels already inside, and every part it does
    // not reach but the lamps goes into farHideMask for the runtime to hide.
    if (farAuthored) {
        int total = 0, best = 0;
        for (size_t k = 0; k < out.body.parts.size(); ++k) {
            tmdl::Part& p = out.body.parts[k];
            p.lods.clear();
            if (farVerts[k].empty()) {
                // The LAMPS stay drawn at tier 0 (a few dozen triangles): they
                // are fullbright and the runtime lights them - brake lights on
                // a distant rival - which no lit paint can imitate (measured:
                // painted lamps read near-black on the console's shading).
                if (k < 31 && p.name != "lamps") out.farHideMask |= 1 << k;
                continue;
            }
            const int t = triCount(farVerts[k]);
            p.lods.push_back({std::move(farVerts[k]), {}});
            total += t;
            if (t > best) best = t, out.farPart = (int)k;
        }
        out.farTris.push_back(total);
        out.farAuthored = true;
    } else {
        // Palette cars and cars with a shared texture can both carry their
        // wheels in the body's distance tier. Never sample wheel UVs through
        // a different image (or tint) merely to save a draw.
        tmdl::Part* carrier = nullptr;
        for (auto& bp : out.body.parts) {
            if (bp.name == "lamps" || bp.name == "merged-matte" ||
                bp.name == "glass")
                continue;
            bool compatible = !out.wheel.parts.empty();
            for (const auto& wp : out.wheel.parts) {
                compatible = compatible && wp.texture == bp.texture;
                for (int a = 0; a < 3; ++a)
                    compatible = compatible && wp.kd[a] == bp.kd[a];
            }
            if (compatible) { carrier = &bp; break; }
        }
        std::vector<float> wheelFar;
        for (const tmdl::Part& wp : out.wheel.parts) {
            if (!carrier) continue;
            wheelFar.insert(wheelFar.end(), wp.verts.begin(), wp.verts.end());
        }
        std::vector<float> wheelTier[2];
        if (!wheelFar.empty()) {
            wheelTier[0] = wheelFar;
            decimateTo(wheelTier[0], std::max(24, opt.wheelTriBudget / 6));
            wheelTier[1] = wheelTier[0];
            decimateTo(wheelTier[1], std::max(16, opt.wheelTriBudget / 12));
        }
        const float hx = 0.5f * out.detection.track;
        const float hz = 0.5f * out.detection.wheelBase;
        const float ax[4] = {-hx, hx, -hx, hx};
        const float az[4] = {hz, hz, -hz, -hz};
        for (tmdl::Part& p : out.body.parts) {
            // The glass stays tier 0 like the lamps: the runtime finds it by
            // index and writes its alpha into ONE colour array.
            if (p.name == "lamps" || p.name == "glass") continue;
            std::vector<std::vector<float>> tiers = meshlod::generateTiers(p.verts);
            // A part too small for the policy still needs a tier when the
            // wheels have to ride on it - the paint part is the carrier.
            if (tiers.empty() && &p == carrier && !wheelFar.empty())
                tiers = {p.verts, p.verts};
            for (size_t t = 0; t < tiers.size() && t < 2; ++t) {
                std::vector<float> verts = std::move(tiers[t]);
                if (&p == carrier && !wheelTier[t].empty()) {
                    for (int w = 0; w < 4; ++w) {
                        const std::vector<float>& src = wheelTier[t];
                        for (size_t i = 0; i + 7 < src.size(); i += 8) {
                            verts.insert(verts.end(), {src[i] + ax[w], src[i + 1],
                                                       src[i + 2] + az[w], src[i + 3],
                                                       src[i + 4], src[i + 5],
                                                       src[i + 6], src[i + 7]});
                        }
                    }
                }
                p.lods.push_back({std::move(verts), {}});
            }
            if (&p == carrier && !p.lods.empty()) {
                for (const tmdl::Lod& l : p.lods)
                    out.farTris.push_back(triCount(l.verts));
                out.farPart = (int)(&p - out.body.parts.data());
            }
        }
    }

    // What a distant car submits: every body part the far tier does not hide
    // (the decimated tiers hide nothing - the lamps stay tier 0 and still
    // draw). 0 = the body carries no far tier at all.
    if (out.farPart >= 0)
        for (size_t k = 0; k < out.body.parts.size(); ++k)
            if (k >= 31 || !(out.farHideMask & (1 << k))) ++out.farSubmits;

    computeBounds(out.body);
    computeBounds(out.wheel);
    if (!out.fastWheel.parts.empty()) computeBounds(out.fastWheel);

    // LOOSE PIECES (docs/vehicles.md, "Loose panels and glass"): every body
    // triangle is sorted into the fixed shell or one of the panels/windows
    // vehiclesim::classifyTriangle names, and each part's list is REORDERED
    // so a piece is one contiguous run of triangles. The strip pass below then
    // strips each piece on its own and pads it to whole runs, so the runtime
    // can remove a piece by collapsing its range without touching a
    // neighbour's triangles. The lamps keep their order (corner ranges).
    //
    // The same reorder carries a second group: a SHINY textured part's MATTE
    // triangles - the ones sampling a near-black texel (cabin, engine bay
    // walls, black trim) - go LAST, and the runtime draws the part's
    // reflection pass over the prefix before them only (VEHICLE_ENV_LIMITS).
    // Without it the paint's sky reflection lit the dark cabin a pale grey
    // through every lost door and window; with it the env pass also has
    // fewer vertices to transform.
    std::vector<std::vector<std::pair<int, int>>> pieceTris(out.body.parts.size());
    const int kMatteGroup = vehiclesim::PieceKindCount;  // the bucket after the pieces
    for (size_t pi = 0; pi < out.body.parts.size(); ++pi) {
        tmdl::Part& p = out.body.parts[pi];
        if (p.name == "lamps" || p.name == "merged-matte" || !p.ao.empty()) continue;
        const bool allGlass = p.name == "glass";
        const bool palette = p.texture == paletteTex && !paletteTex.empty();
        // The texture a shiny textured part samples, for the matte test.
        std::vector<unsigned char> img;
        int iw = 0, ih = 0;
        if (!p.reflTexture.empty() && !palette && !p.texture.empty())
            for (const Result::Texture& t : out.textures)
                if (t.path == p.texture) img = decodeRgba(t.png, iw, ih);
        if (!opt.loosePieces && img.empty()) continue;
        auto lum = [&](float u, float v) {
            u -= std::floor(u);
            v -= std::floor(v);
            const int x = std::min(iw - 1, std::max(0, (int)(u * (float)iw)));
            const int y = std::min(ih - 1, std::max(0, (int)(v * (float)ih)));
            const unsigned char* c = &img[((size_t)y * iw + x) * 4];
            return 0.30f * c[0] + 0.59f * c[1] + 0.11f * c[2];
        };
        std::vector<std::vector<float>> bucket(vehiclesim::PieceKindCount + 1);
        const size_t nt = p.verts.size() / 24;
        for (size_t t = 0; t < nt; ++t) {
            const float* v = &p.verts[t * 24];
            bool glass = allGlass;
            if (!glass && palette && !glassUvs.empty()) {
                glass = true;
                for (int c = 0; c < 3 && glass; ++c) {
                    bool hit = false;
                    for (const auto& uv : glassUvs)
                        hit |= std::fabs(v[c * 8 + 6] - uv[0]) < 1e-5f &&
                               std::fabs(v[c * 8 + 7] - uv[1]) < 1e-5f;
                    glass = hit;
                }
            }
            int kind = opt.loosePieces
                           ? vehiclesim::classifyTriangle(v, v + 8, v + 16, glass,
                                                          out.body.min, out.body.max)
                           : 0;
            if (!img.empty() && !glass) {
                const float cu = (v[6] + v[14] + v[22]) / 3.0f, cv = (v[7] + v[15] + v[23]) / 3.0f;
                float l = lum(cu, cv);
                for (int c = 0; c < 3; ++c) l = std::max(l, lum(v[c * 8 + 6], v[c * 8 + 7]));
                // Matte trumps a piece: a door card stays when its skin goes,
                // which is also what keeps the hole dark.
                if (l < 42.0f) kind = kMatteGroup;
            }
            bucket[(size_t)kind].insert(bucket[(size_t)kind].end(), v, v + 24);
        }
        bool any = bucket[(size_t)kMatteGroup].size() / 24 >= 4;
        for (int k = 1; k < vehiclesim::PieceKindCount; ++k)
            // A piece of a couple of triangles is noise, not a panel.
            if (bucket[(size_t)k].size() / 24 >= 4) any = true;
            else if (!bucket[(size_t)k].empty()) {
                bucket[0].insert(bucket[0].end(), bucket[(size_t)k].begin(),
                                 bucket[(size_t)k].end());
                bucket[(size_t)k].clear();
            }
        if (!any) continue;
        p.verts.clear();
        for (int k = 0; k <= vehiclesim::PieceKindCount; ++k) {
            pieceTris[pi].push_back({(int)(p.verts.size() / 24),
                                     (int)(bucket[(size_t)k].size() / 24)});
            p.verts.insert(p.verts.end(), bucket[(size_t)k].begin(),
                           bucket[(size_t)k].end());
        }
    }
    // Strips one piece's triangles on their own; falls back to a
    // triangle-per-join encoding (A B C, then C D D E F per triangle) when
    // meshstrip refuses a small group, and never lets a triangle straddle a
    // run. Pads to whole runs unless `last`.
    auto stripGroup = [](const float* verts, int tris, bool last, std::vector<float>& out) {
        const size_t start = out.size();
        // Repeat the last vertex (a degenerate). Copied first: inserting a
        // vector's own range into it is undefined once it reallocates.
        auto dupLast = [&out]() {
            float t[8];
            std::copy(out.end() - 8, out.end(), t);
            out.insert(out.end(), t, t + 8);
        };
        std::vector<float> sub(verts, verts + (size_t)tris * 24), st;
        std::vector<unsigned char> noAo, stAo;
        if (tris > 0 && meshstrip::build(sub, noAo, meshstrip::kRun, st, stAo,
                                         meshstrip::Weld::kFull)) {
            out.insert(out.end(), st.begin(), st.end());
        } else {
            const unsigned kRun = meshstrip::kRun;
            unsigned inRun = 0;
            auto pad = [&]() {
                while (inRun > 0 && inRun < kRun) {
                    dupLast();
                    ++inRun;
                }
                inRun = 0;
            };
            for (int t = 0; t < tris; ++t) {
                const float* v = verts + (size_t)t * 24;
                const unsigned need = inRun == 0 ? 3 : 5;
                if (inRun + need > kRun) pad();
                if (inRun == 0) {
                    out.insert(out.end(), v, v + 24);
                    inRun = 3;
                } else {
                    dupLast();  // C again
                    out.insert(out.end(), v, v + 8);                  // D
                    out.insert(out.end(), v, v + 24);                 // D E F
                    inRun += 5;
                }
                if (inRun == kRun) inRun = 0;
            }
        }
        if (!last) {
            const size_t n = (out.size() - start) / 8;
            const size_t rem = n % meshstrip::kRun;
            if (rem && n)
                for (size_t k = rem; k < meshstrip::kRun; ++k)
                    dupLast();
        }
    };

    // VEHICLE TRIANGLE STRIPS (docs/vehicles.md, "Strip-ready bodies" and
    // "The wheel batch is a strip"; docs/model-pipeline.md, "Triangle strips").
    //
    // A body is lit, textured and may be drawn again by the reflection pass,
    // so its weld key MUST contain position, normal and UV. Old flat-shaded
    // vehicle exports usually fail that honest test: nearly every face owns a
    // different normal and the joined strips are larger than the list. A mesh
    // authored with shared smooth normals and an atlas can, however, remove
    // most of the repeated corners. meshstrip::build refuses the former and
    // keeps the latter automatically; no import checkbox or asset-specific
    // path is needed.
    //
    // The lamp part is deliberately left as a list. Its rear/front split is a
    // CORNER INDEX and renderVehicleGlow writes those two ranges in place;
    // strip order would destroy that contract. Distance tiers also remain
    // lists: applyGeoLod currently stages their list vertices lazily and tier
    // 0 is the expensive close representation this optimisation targets.
#ifndef TYRA_STRIP_VEHICLE_BODIES_BAKE
#define TYRA_STRIP_VEHICLE_BODIES_BAKE 1
#endif
#if TYRA_STRIP_VEHICLE_BODIES_BAKE
    size_t bodyListVerts = 0, bodyStripVerts = 0;
    int bodyStripParts = 0;
    for (tmdl::Part& p : out.body.parts) {
        p.stripVerts.clear();
        p.stripAo.clear();
        p.stripRun = 0;
        bodyListVerts += p.verts.size() / 8;
        const size_t partIdx = (size_t)(&p - out.body.parts.data());
        const std::vector<std::pair<int, int>>& groups = pieceTris[partIdx];
        bool grouped = false;
        if (!groups.empty()) {
            // Piece by piece: the body shell first, each loose piece padded
            // to whole runs. Kept only when it still beats the list.
            std::vector<float> st;
            std::vector<std::pair<int, int>> ranges;
            int lastNonEmpty = 0;
            for (size_t k = 0; k < groups.size(); ++k)
                if (groups[k].second > 0) lastNonEmpty = (int)k;
            for (size_t k = 0; k < groups.size(); ++k) {
                const int first = (int)(st.size() / 8);
                if (groups[k].second > 0)
                    stripGroup(&p.verts[(size_t)groups[k].first * 24], groups[k].second,
                               (int)k == lastNonEmpty, st);
                ranges.push_back({first, (int)(st.size() / 8) - first});
            }
            if (st.size() < p.verts.size()) {
                p.stripVerts.swap(st);
                grouped = true;
                if (groups.size() > (size_t)vehiclesim::PieceKindCount &&
                    groups[(size_t)vehiclesim::PieceKindCount].second > 0)
                    out.envLimits.push_back(
                        {(int)partIdx, ranges[(size_t)vehiclesim::PieceKindCount].first}),
                    out.envLimitsList.push_back(
                        {(int)partIdx, groups[(size_t)vehiclesim::PieceKindCount].first * 3});
                for (size_t k = 1; k < ranges.size() && k < (size_t)vehiclesim::PieceKindCount; ++k)
                    if (ranges[k].second > 0) {
                        out.pieces.push_back({(int)partIdx, (int)k, ranges[k].first,
                                              ranges[k].second});
                        out.pieceLists.push_back({groups[k].first * 3, groups[k].second * 3});
                    }
            } else {
                if (groups.size() > (size_t)vehiclesim::PieceKindCount &&
                    groups[(size_t)vehiclesim::PieceKindCount].second > 0)
                    out.envLimits.push_back(
                        {(int)partIdx, groups[(size_t)vehiclesim::PieceKindCount].first * 3}),
                    out.envLimitsList.push_back(
                        {(int)partIdx, groups[(size_t)vehiclesim::PieceKindCount].first * 3});
                for (size_t k = 1; k < groups.size() && k < (size_t)vehiclesim::PieceKindCount; ++k)
                    if (groups[k].second > 0) {
                        out.pieces.push_back({(int)partIdx, (int)k, groups[k].first * 3,
                                              groups[k].second * 3});
                        out.pieceLists.push_back({groups[k].first * 3, groups[k].second * 3});
                    }
            }
        }
        if (grouped) {
            p.stripRun = meshstrip::kRun;
            bodyStripVerts += p.stripVerts.size() / 8;
            ++bodyStripParts;
        } else if (p.name != "lamps" && groups.empty() &&
            meshstrip::build(p.verts, p.ao, meshstrip::kRun, p.stripVerts,
                             p.stripAo, meshstrip::Weld::kFull)) {
            p.stripRun = meshstrip::kRun;
            bodyStripVerts += p.stripVerts.size() / 8;
            ++bodyStripParts;
        } else {
            bodyStripVerts += p.verts.size() / 8;
        }
        // An AUTHORED far tier keeps its authored smooth normals and shared
        // atlas UVs, so it can strip on the full key like tier 0 does - a
        // decimated tier cannot (its face normals are recomputed flat, every
        // corner unique). The format carries a tier strip only beside a base
        // strip (one run length per part), and applyGeoLod binds it with the
        // bag's topology flag.
        if (out.farAuthored && p.stripRun != 0)
            for (tmdl::Lod& l : p.lods) {
                l.stripVerts.clear();
                l.stripAo.clear();
                if (!meshstrip::build(l.verts, l.ao, meshstrip::kRun, l.stripVerts,
                                      l.stripAo, meshstrip::Weld::kFull)) {
                    l.stripVerts.clear();
                    l.stripAo.clear();
                }
            }
    }
    if (bodyStripParts > 0) {
        char buf[220];
        size_t listPkgs = 0, stripPkgs = 0;
        for (const tmdl::Part& p : out.body.parts) {
            const size_t listN = p.verts.size() / 8;
            listPkgs += (listN + meshstrip::kRun - 1) / meshstrip::kRun;
            const size_t n =
                (p.stripRun ? p.stripVerts : p.verts).size() / 8;
            stripPkgs += (n + meshstrip::kRun - 1) / meshstrip::kRun;
        }
        std::snprintf(buf, sizeof(buf),
                      "Body strips: %zu -> %zu submitted vertices; packages "
                      "%zu -> %zu across %d eligible part(s).",
                      bodyListVerts, bodyStripVerts, listPkgs, stripPkgs,
                      bodyStripParts);
        out.notes.push_back(buf);
    }
#endif

    // The wheel uses a different, intentionally weaker weld key.
    //
    // Built HERE and nowhere else, because the wheel model never goes through
    // bakeStaticModels: it is an artifact of this bake, and until now it was
    // the one mesh in a district that reached the console as a pure triangle
    // list. The garage-day frame inventory measured what that costs - the
    // wheel batch ran 24.7 triangles a VU1 package against a strip's ~70,
    // which is 25 (75/3) with partial packages, i.e. exactly a list.
    //
    // The weld IGNORES NORMALS, and that is the whole reason this works. An
    // imported car is flat-shaded, so under the ordinary weld 878 of the CC96
    // wheel's 942 corners are unique and the strip comes out 1.6x the LIST -
    // meshstrip refuses it, correctly. But the bag that draws a wheel
    // (TerrainGame::renderVehicleWheels) has no lighting bag and one flat
    // colour, so the attributes the GS actually receives are position and UV;
    // on that key the same mesh strips to 0.764x. A body above uses kFull;
    // using this wheel-only key for it would silently corrupt its lighting.
    //
    // The tiers above are already built from `verts` and are untouched, which
    // matters: a far tier carries the wheels INTO the lit body part, so it
    // must keep the list's real normals.
    // The fast wheel goes through the SAME weld: it is drawn by the same
    // unlit, flat-coloured bag, and the batch only strips when both models do.
    for (tmdl::Model* wm : {&out.wheel, &out.fastWheel})
        for (tmdl::Part& p : wm->parts) {
            p.stripVerts.clear();
            p.stripAo.clear();
            p.stripRun = 0;
            if (meshstrip::build(p.verts, p.ao, meshstrip::kRun, p.stripVerts,
                                 p.stripAo, meshstrip::Weld::kNoNormal))
                p.stripRun = meshstrip::kRun;
        }
    for (const auto& el : out.envLimits) {
        char b[160];
        const tmdl::Part& lp = out.body.parts[(size_t)el.first];
        std::snprintf(b, sizeof(b), "Shine: part %d reflects its first %d of %zu vertices "
                      "(the dark cabin and trim after them stay matte).", el.first, el.second,
                      (lp.stripRun ? lp.stripVerts : lp.verts).size() / 8);
        out.notes.push_back(b);
    }
    if (!out.pieces.empty()) {
        std::string line = "Loose pieces:";
        for (const vehiclesim::Piece& pc : out.pieces) {
            char b[64];
            std::snprintf(b, sizeof(b), " %s (part %d, %d verts)",
                          vehiclesim::pieceName(pc.kind), pc.part, pc.count);
            line += b;
        }
        out.notes.push_back(line + ".");
    }
    for (size_t k = 0; k < out.body.parts.size(); ++k)
        if (out.body.parts[k].name == "lamps") {
            out.lampPart = (int)k;
            out.lampRearVerts = lampRearVerts;
        } else if (out.body.parts[k].name == "glass") {
            out.glassPart = (int)k;
        }
    out.bodyParts = (int)out.body.parts.size();
    out.wheelParts = (int)out.wheel.parts.size();
    out.bodyTris = modelTris(out.body);
    out.wheelTris = modelTris(out.wheel);
    out.fastWheelTris = modelTris(out.fastWheel);

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
        const auto isLamp = lampMaterial;
        float mnR[3] = {1e30f, 1e30f, 1e30f}, mxR[3] = {-1e30f, -1e30f, -1e30f};
        float mnF[3] = {1e30f, 1e30f, 1e30f}, mxF[3] = {-1e30f, -1e30f, -1e30f};
        bool anyR = false, anyF = false;
        const std::vector<M4> g2 = globals(sk);
        for (const glbparser::SkelPart& p : sk.parts) {
            bool front = false;
            if (!isLamp(p.material, &front)) continue;
            const bool splitRigid = rigidOwners(sk, p);
            const int fallback = ownerNode(sk, p, nullptr);
            for (int c = 0; c < p.vertexCount; ++c) {
                const int node =
                    splitRigid ? rigidCornerNode(sk, p, c) : fallback;
                if (node < 0 ||
                    std::find(out.detection.bodyNodes.begin(),
                              out.detection.bodyNodes.end(), node) ==
                        out.detection.bodyNodes.end())
                    continue;
                const M4 xf = M4::mul(canon, g2[(size_t)node]);
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


// --- built-in tyre-effect textures ------------------------------------------
// The skid ribbon's tread and the smoke puff a vehicle uses when its
// definition names no material (docs/vehicles.md, "Skid marks and smoke").
// Generated rather than shipped: no licence to track, and the look is a few
// lines of arithmetic that can be tuned here. Both are WHITE, so the runtime's
// vertex colour (rubber black, smoke grey) is the tint and the alpha is the
// shape.
namespace {

float fxHash(int x, int y, int seed) {
    // Unsigned throughout: `x * 374761393` in int overflows (UB), and GCC -O3
    // proved it for every caller's loop and compiled builtinSkidPng to a ud2.
    unsigned int h = (unsigned int)x * 374761393u + (unsigned int)y * 668265263u +
                     (unsigned int)seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return (float)((h ^ (h >> 16)) & 0xFFFF) / 65535.0f;
}

// Smooth value noise, tiling with period `per` so the puff has no seam.
float fxValueNoise(float x, float y, int per, int seed) {
    const int x0 = (int)std::floor(x), y0 = (int)std::floor(y);
    const float fx = x - x0, fy = y - y0;
    const float sx = fx * fx * (3.0f - 2.0f * fx), sy = fy * fy * (3.0f - 2.0f * fy);
    auto at = [&](int ix, int iy) {
        return fxHash(((ix % per) + per) % per, ((iy % per) + per) % per, seed);
    };
    const float a = at(x0, y0) + (at(x0 + 1, y0) - at(x0, y0)) * sx;
    const float c = at(x0, y0 + 1) + (at(x0 + 1, y0 + 1) - at(x0, y0 + 1)) * sx;
    return a + (c - a) * sy;
}

float fxSmooth(float e0, float e1, float x) {
    float t = (x - e0) / (e1 - e0);
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return t * t * (3.0f - 2.0f * t);
}

// 32 across the tyre x 64 along the mark; the runtime repeats it along the
// ribbon every 1.5 units of travel.
std::vector<unsigned char> skidTreadRGBA(int& w, int& h) {
    w = 32;
    h = 64;
    std::vector<unsigned char> rgba((size_t)w * h * 4);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const float u = (x + 0.5f) / w;  // across, 0..1
            const float v = (y + 0.5f) / h;  // along, 0..1 (repeats)
            // soft shoulders: the rubber feathers out at both edges
            float a = fxSmooth(0.0f, 0.16f, u) * fxSmooth(1.0f, 0.84f, u);
            // two longitudinal grooves
            const float g1 = std::fabs(u - 0.36f), g2 = std::fabs(u - 0.64f);
            a *= 1.0f - 0.55f * (1.0f - fxSmooth(0.0f, 0.045f, g1 < g2 ? g1 : g2));
            // slanted sipes across the shoulders, 8 per tile
            const float sipe = std::fmod(v * 8.0f + (u < 0.5f ? u : 1.0f - u) * 1.6f, 1.0f);
            const float shoulder = 1.0f - fxSmooth(0.22f, 0.34f, std::fabs(u - 0.5f) < 0.5f ? 0.5f - std::fabs(u - 0.5f) : 0.0f);
            a *= 1.0f - 0.45f * shoulder * (1.0f - fxSmooth(0.0f, 0.18f, sipe < 0.5f ? sipe : 1.0f - sipe));
            // rubber grain
            a *= 0.80f + 0.20f * fxHash(x, y, 7);
            const float lum = 0.86f + 0.14f * fxHash(x, y, 11);
            unsigned char* o = &rgba[((size_t)y * w + x) * 4];
            o[0] = o[1] = o[2] = (unsigned char)(lum * 255.0f + 0.5f);
            o[3] = (unsigned char)(a * 255.0f + 0.5f);
        }
    return rgba;
}

}  // namespace

const char* kSkidTexturePath = "vehicles/fx-skid.png";
const char* kSmokeTexturePath = "vehicles/fx-smoke.png";

std::vector<unsigned char> builtinSkidPng() {
    int w = 0, h = 0;
    const std::vector<unsigned char> rgba = skidTreadRGBA(w, h);
    return encodePng(rgba, w, h);
}

ParticleTexGen builtinSmokeRecipe() {
    // Picked against the old puff side by side over a dark and a light
    // ground (the particletex harness in tyra-testing): a soft billow, fine
    // wisps, lighter on top. The generator reaches zero alpha before the
    // quad's edge, so no bilinear tap reads the clamp border.
    ParticleTexGen g;
    g.kind = 1;
    g.size = 64;
    g.seed = 9;
    g.softness = 0.85f;
    g.detail = 0.4f;
    g.scale = 0.9f;
    return g;
}

std::vector<unsigned char> builtinSmokePng() {
    std::vector<unsigned char> rgba = particletex::generate(builtinSmokeRecipe());
    for (size_t i = 3; i < rgba.size(); i += 4)
        rgba[i] = (unsigned char)std::lround(rgba[i] * kBuiltinSmokeDensity);
    const int n = builtinSmokeRecipe().size;
    return encodePng(rgba, n, n);
}

SmokeLook smokeLookOf(const ParticleEffect& fx) {
    SmokeLook L{};
    // textured puffs MODULATE (128 = 1x); untextured ones are the vertex
    // colour itself
    const float scale = fx.materialPath.empty() ? 255.0f : 128.0f;
    for (int k = 0; k < 3; ++k) L.rgb[k] = fx.color[k] * scale;
    L.alpha = fx.opacity * 128.0f;
    L.size0 = fx.size * 0.6f;
    L.size1 = L.size0 * (fx.kind == 5 ? std::max(1.0f, fx.grow) : 2.4f);
    L.life = fx.life / 1.5f;
    L.rise = 1.0f;
    if (fx.kind == 5 && fx.gravity < 0.0f)
        L.rise = std::min(3.0f, std::max(0.3f, -fx.gravity));
    L.frames = fx.texGen.kind > 0 ? std::min(8, std::max(1, fx.texGen.frames)) : 1;
    L.fps = fx.texGen.fps;
    return L;
}

ParticleEffect tyreSmokeEffect() {
    // Custom motion (kind 5), so grow and gravity are the effect's own and
    // smokeLookOf maps them back onto the built-in numbers: size0 0.22,
    // size1 1.67, life 1, rise 1, peak alpha 84 x the built-in density.
    ParticleEffect fx;
    fx.name = "Tyre smoke";
    fx.kind = 5;
    fx.count = 24;
    fx.size = 0.22f / 0.6f;
    fx.grow = 1.67f / 0.22f;
    fx.life = 1.5f;
    fx.gravity = -1.0f;
    fx.speed = 1.0f, fx.spread = 30.0f, fx.weight = 0.5f;
    fx.opacity = 84.0f / 128.0f * kBuiltinSmokeDensity;
    fx.color[0] = fx.color[1] = fx.color[2] = 1.0f;
    fx.texGen = builtinSmokeRecipe();
    return fx;
}

// --- the build-path bake ----------------------------------------------------

BakedPaths pathsFor(const VehicleDef& v) {
    BakedPaths b;
    if (v.id.empty()) return b;
    const std::string stem = "vehicles/veh-" + v.id;
    b.body = stem + "-body.tmdl";
    b.wheel = stem + "-wheel.tmdl";
    b.palette = stem + "-palette.png";
    b.shadow = stem + "-shadow.png";
    if (!v.fastWheel.empty()) b.fastWheel = stem + "-wheelfast.tmdl";
    return b;
}

bool adoptMeasured(VehicleDef& v, const Result& r) {
    bool changed = false;
    for (int k = 0; k < 4; ++k) {
        if (v.lampRear[k] != r.lampRear[k]) changed = true;
        if (v.lampFront[k] != r.lampFront[k]) changed = true;
        v.lampRear[k] = r.lampRear[k];
        v.lampFront[k] = r.lampFront[k];
    }
    if (v.lampPart != r.lampPart || v.lampRearVerts != r.lampRearVerts)
        changed = true;
    v.lampPart = r.lampPart;
    v.lampRearVerts = r.lampRearVerts;
    if (v.glassPart != r.glassPart) changed = true;
    v.glassPart = r.glassPart;
    if (v.farPart != r.farPart || v.farHideMask != r.farHideMask) changed = true;
    v.farPart = r.farPart;
    v.farHideMask = r.farHideMask;
    if (v.pieces != r.pieces) changed = true;
    v.pieces = r.pieces;
    if (v.envLimits != r.envLimits) changed = true;
    v.envLimits = r.envLimits;
    return changed;
}

// Modified by TyraX: a vehicle's BODY TEXTURE is the one shipped image that
// used to ignore the project's texture depth. Everything under res/models,
// res/materials and res/textures goes through texbake's quantizer; these come
// out of the .glb's embedded PNG bytes and are written straight into
// .res-baked/vehicles/, a directory texbake deliberately does not sweep - so
// a project set to 4-bit still shipped a 32-bit car.
//
// It is not a rounding error on a PS2. On the Motor District at Pal576i the
// texture heap is 196 608 words, and the Tristar's 256x256 RGBA body texture
// is 65 536 of them - ONE THIRD of the heap for one car, against 1 088 words
// for each of the fourteen 4-bit building textures around it.
//
// IT IS A TRADE, NOT A FREE WIN, and it is gated on the project's own
// textureQuant for exactly that reason: a project that has not asked to
// palettize its models does not get its cars palettized either. Measured on a
// physical PS2 on the Motor District, which has 0.119 MB of heap free and
// evicts nothing parked, the same change costs +0.51 to +0.74 ms of work per
// pose - the VRAM it buys is VRAM that scene was not short of. Take it where
// the heap is tight; docs/vehicles.md, "The body texture obeys the project's
// depth - and what that costs", has the numbers and the open question about
// where the time actually goes.
//
// Refusal is graceful: anything the quantizer will not take (an unreadable
// image, an odd width at 4-bit) ships the original bytes exactly as before,
// and says why.
static std::string quantizedTexture(
    const std::string& png, const std::string& quant, const std::string& name,
    const std::function<void(const std::string&)>& log) {
    const int colors = quant == "8bit" ? 256 : quant == "4bit" ? 16 : 0;
    if (colors == 0) return png;

    int w = 0, h = 0, comp = 0;
    unsigned char* px =
        stbi_load_from_memory((const unsigned char*)png.data(), (int)png.size(),
                              &w, &h, &comp, 4);
    if (px == nullptr) {
        if (log) log("[vehicle] " + name + ": unreadable texture - shipped as is");
        return png;
    }
    std::vector<unsigned char> bytes;
    std::string err;
    const bool ok =
        pngquant::quantizeRGBAToMemory(bytes, px, w, h, colors, err);
    stbi_image_free(px);
    if (!ok) {
        if (log) log("[vehicle] " + name + ": " + err + " - shipped as is");
        return png;
    }
    // NO file-size guard here, and the first draft's was a real bug. What
    // costs GS VRAM is the PIXEL FORMAT, not the file: a 256x256 PSMT4 image
    // occupies 8 256 words against PSMCT32's 65 536 however either one
    // deflates. The Tristar's skin is flat colour that PNG compresses to 4 KB,
    // and Floyd-Steinberg dithering makes the palettized copy deflate WORSE -
    // so a "is the file smaller" test rejected precisely the texture that was
    // eating a third of the heap, and logged a sentence that sounded sensible
    // while doing it.
    if (log) {
        char buf[200];
        std::snprintf(buf, sizeof(buf),
                      "[vehicle] %s: %dx%d quantized to %s (%d -> %d bytes)",
                      name.c_str(), w, h, quant.c_str(), (int)png.size(),
                      (int)bytes.size());
        log(buf);
    }
    return std::string((const char*)bytes.data(), bytes.size());
}

std::string bakeProject(Project& p,
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

    for (VehicleDef& v : p.vehicles) {
        if (v.modelPath.empty() || v.id.empty()) continue;
        const BakedPaths bp = pathsFor(v);
        Options opt;
        opt.bodyTriBudget = v.bodyTriBudget;
        opt.wheelTriBudget = v.wheelTriBudget;
        opt.mergeUntextured = v.mergeUntextured;
        opt.bodyShine = v.bodyShine;
        opt.bodyReflMap = binReflPath(v.bodyReflMap);
        opt.paletteTexture = bp.palette;
        opt.fastWheel = v.fastWheel;
        opt.fastWheelTriBudget = v.fastWheelTriBudget;
        opt.glassSplit = v.glassOpacity < 1.0f;
        opt.loosePieces = v.drive.damage > 0.0f && v.drive.damageLoose > 0.0f;
        if (!v.farModel.empty()) opt.farModel = p.filePath(v.farModel);
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
        // Written even when empty: codegen names the path from the definition
        // alone (pathsFor), and a missing .tmdl is a load failure, whereas an
        // empty model is a wheel the game simply never swaps to.
        if (!bp.fastWheel.empty()) put(bp.fastWheel, tmdl::write(r.fastWheel));
        if (!r.palettePng.empty())
            put(bp.palette, std::string((const char*)r.palettePng.data(),
                                        r.palettePng.size()));
        if (!r.shadowPng.empty())
            put(bp.shadow, std::string((const char*)r.shadowPng.data(),
                                       r.shadowPng.size()));
        // The body texture follows the project's texture depth like every
        // other model texture does (see quantizedTexture above). The colour
        // PALETTE strip written just before this is deliberately NOT
        // quantized: it is a 64x8 ramp of the body colours the runtime
        // indexes into, and folding it to 16 entries would fold the colours
        // themselves.
        // A per-asset quality override of the MODEL (Project::textureQuality,
        // the Vehicle Editor's Texture depth) wins over the project default,
        // the rule every other model texture already follows in texbake - so
        // a 4-bit district can still ship a 256-colour hero car.
        std::string quant = p.settings.textureQuant;
        if (auto q = p.textureQuality.find(v.modelPath);
            q != p.textureQuality.end() && !q->second.empty())
            quant = q->second;
        for (const auto& texture : r.textures)
            put(texture.path,
                quantizedTexture(std::string((const char*)texture.png.data(),
                                             texture.png.size()),
                                 quant, v.name, log));
        adoptMeasured(v, r);
        if (log) {
            char buf[220];
            if (r.farPart >= 0) {
                char traffic[64] = "";
                if (v.trafficDistance > 0.0f)
                    std::snprintf(traffic, sizeof(traffic),
                                  " (%.0f for cars nobody drives)", v.trafficDistance);
                if (r.farAuthored)
                    std::snprintf(buf, sizeof(buf),
                                  "[vehicle] %s: far model %s %d tris (wheels in), "
                                  "%d submit(s) past %.0f units%s",
                                  v.name.c_str(),
                                  std::filesystem::path(v.farModel).filename().string().c_str(),
                                  r.farTris[0], r.farSubmits, v.farDistance, traffic);
                else
                    std::snprintf(buf, sizeof(buf),
                                  "[vehicle] %s: far tier %d tris (wheels in)%s%d, "
                                  "%d submit(s) past %.0f units%s",
                                  v.name.c_str(), r.farTris[0],
                                  r.farTris.size() > 1 ? ", then " : "",
                                  r.farTris.size() > 1 ? r.farTris[1] : r.farTris[0],
                                  r.farSubmits, v.farDistance, traffic);
                log(buf);
            } else {
                std::snprintf(buf, sizeof(buf),
                              "[vehicle] %s: no far tier carries the wheels (their "
                              "texture is not the body's), so the wheel bag draws at "
                              "every distance - a far model fixes that",
                              v.name.c_str());
                log(buf);
            }
            for (const std::string& n : r.notes)
                if (n.rfind("Far model:", 0) == 0) log("[vehicle] " + v.name + ": " + n);
            // Triangle strips per body part: submitted vertices of the list
            // against the strip (the far tier's too), the packing acceptance
            // number (docs/vehicles.md, "Strip-ready bodies").
            for (size_t k = 0; k < r.body.parts.size(); ++k) {
                const tmdl::Part& bp = r.body.parts[k];
                const size_t ln = bp.verts.size() / 8, sn = bp.stripVerts.size() / 8;
                std::string line = "[vehicle] " + v.name + ": part " + std::to_string(k) +
                                   " " + bp.name + " " + std::to_string(ln) + " list verts";
                std::snprintf(buf, sizeof(buf), " -> %zu strip (%.3fx)", sn,
                              ln ? (double)sn / (double)ln : 0.0);
                line += bp.stripRun ? std::string(buf) : std::string(" (no strip)");
                for (const tmdl::Lod& l : bp.lods) {
                    const size_t tl = l.verts.size() / 8, ts = l.stripVerts.size() / 8;
                    std::snprintf(buf, sizeof(buf), "; tier %zu list", tl);
                    line += buf;
                    if (ts) {
                        std::snprintf(buf, sizeof(buf), " -> %zu strip (%.3fx)", ts,
                                      (double)ts / (double)tl);
                        line += buf;
                    }
                }
                if (k < 31 && (r.farHideMask & (1 << k))) line += "; hidden far";
                log(line);
            }
            if (r.lampPart >= 0) {
                std::snprintf(buf, sizeof(buf),
                              "[vehicle] %s: lamp materials -> emissive part %d "
                              "(%d rear corners, %d front)",
                              v.name.c_str(), r.lampPart, r.lampRearVerts,
                              triCount(r.body.parts[(size_t)r.lampPart].verts) * 3 -
                                  r.lampRearVerts);
                log(buf);
            }
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
    // The built-in tyre-effect textures, once per project with vehicles -
    // whichever definition names no material draws with these. Quantized
    // like every shipped model texture (the project's texture depth).
    if (!p.vehicles.empty()) {
        const std::vector<unsigned char> skid = builtinSkidPng();
        const std::vector<unsigned char> smoke = builtinSmokePng();
        put(kSkidTexturePath,
            quantizedTexture(std::string((const char*)skid.data(), skid.size()),
                             p.settings.textureQuant, "tyre marks", log));
        // The puff is ALL alpha gradient: 16 palette entries band its edge
        // into hard rings (seen in PCSX2), so it never goes below 8-bit -
        // 64x64 at 8-bit is 4 KB plus the CLUT.
        const std::string smokeQuant =
            p.settings.textureQuant == "4bit" ? std::string("8bit") : p.settings.textureQuant;
        put(kSmokeTexturePath,
            quantizedTexture(std::string((const char*)smoke.data(), smoke.size()),
                             smokeQuant, "tyre smoke", log));
    }
    return firstError;
}

}  // namespace vehbake
