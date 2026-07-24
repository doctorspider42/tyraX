#include "texatlas.hpp"

#include <algorithm>
#include <filesystem>
#include <map>
#include <set>

#include <stb_image.h>

#include "objparser.hpp"

namespace fs = std::filesystem;

namespace texatlas {

namespace {

// texbake's baked-dimension rule (keep in sync with texbake.cpp
// nearestValidDim - the plan must predict the exact baked size).
int nearestValidDim(int v) {
    static const int valid[] = {8, 16, 32, 64, 128, 256, 512};
    int best = valid[0], bestDist = 1 << 30;
    for (int d : valid) {
        const int dd = v > d ? v - d : d - v;
        if (dd < bestDist) {
            bestDist = dd;
            best = d;
        }
    }
    return best;
}

// map_Kd token -> res-relative path, but only for same-directory tokens
// (anything with a path separator would break the same-dir page reference
// after the rewrite). dirRel = res-relative directory of the defining .mtl.
std::string sameDirTexture(const std::string& dirRel, const std::string& tok) {
    if (tok.empty()) return "";
    if (tok.find('/') != std::string::npos ||
        tok.find('\\') != std::string::npos)
        return "";
    return dirRel + "/" + tok;
}

struct Gather {
    // candidate texture -> the .mtl directory its consumers resolve against
    std::map<std::string, std::string> candidates;  // resRel -> dirRel
    std::set<std::string> ineligible;               // resRel (any reason)

    void candidate(const std::string& dirRel, const std::string& tok) {
        const std::string rel = sameDirTexture(dirRel, tok);
        if (rel.empty()) return;
        auto it = candidates.find(rel);
        if (it == candidates.end())
            candidates.emplace(rel, dirRel);
        else if (it->second != dirRel)
            ineligible.insert(rel);  // cross-directory sharing
    }
    void ban(const std::string& dirRel, const std::string& tok) {
        // a consumer we can't remap: ban the same-dir resolution; a token
        // with separators was never a candidate anyway
        const std::string rel = sameDirTexture(dirRel, tok);
        if (!rel.empty()) ineligible.insert(rel);
    }
};

}  // namespace

Plan plan(const Project& p) {
    Plan out;
    if (!p.settings.textureAtlas) return out;

    Gather g;
    const fs::path root(p.dir);
    auto dirOf = [](const std::string& rel) {
        return fs::path(rel).parent_path().generic_string();
    };

    // --- standalone .mtl consumers (primitives / model overrides) ----------
    // Objects decide how a material's texture is sampled; the same .mtl may
    // serve several object kinds, so scan objects, not files.
    std::set<std::string> modelKeys;    // "model|overrideMtl"
    std::set<std::string> plainMtls;    // primitive-safe consumers
    for (const SceneData& sc : p.scenes) {
        for (const SceneObject& o : sc.objects) {
            const bool prim = o.type == PrimitiveType::Box ||
                              o.type == PrimitiveType::Sphere ||
                              o.type == PrimitiveType::Cylinder ||
                              o.type == PrimitiveType::Cone ||
                              o.type == PrimitiveType::Plane;
            const bool banned = o.type == PrimitiveType::Emitter ||
                                o.type == PrimitiveType::Decal ||
                                o.type == PrimitiveType::Mirror ||
                                o.type == PrimitiveType::Portal;
            if (o.type == PrimitiveType::Model &&
                fs::path(o.modelPath).extension() == ".obj")
                modelKeys.insert(o.modelPath + "|" + o.materialPath);
            if (o.materialPath.empty()) continue;
            if (prim)
                plainMtls.insert(o.materialPath);
            else if (banned) {
                // first-entry texture is what these bind
                std::vector<objparser::MtlMaterial> mats;
                if (objparser::loadMtl((root / o.materialPath).string(), mats) &&
                    !mats.empty())
                    g.ban(dirOf(o.materialPath), mats[0].texture);
            }
        }
        // terrain (base + paint layers) tiles its textures - never atlas
        auto banTerrainMtl = [&](const std::string& mtlRel) {
            if (mtlRel.empty()) return;
            std::vector<objparser::MtlMaterial> mats;
            if (objparser::loadMtl((root / mtlRel).string(), mats))
                for (const objparser::MtlMaterial& m : mats)
                    g.ban(dirOf(mtlRel), m.texture);
        };
        banTerrainMtl(p.settings.terrainMaterial);
        for (const TerrainLayer& l : sc.terrainLayers) banTerrainMtl(l.material);
    }

    for (const std::string& mtlRel : plainMtls) {
        std::vector<objparser::MtlMaterial> mats;
        if (!objparser::loadMtl((root / mtlRel).string(), mats)) continue;
        const std::string dir = dirOf(mtlRel);
        for (size_t i = 0; i < mats.size(); ++i) {
            const objparser::MtlMaterial& m = mats[i];
            // refl sphere maps sample runtime-computed STs
            if (!m.refl.empty() && m.refl != "@sky") g.ban(dir, m.refl);
            if (m.texture.empty()) continue;
            // a tiling factor means the texture is meant to repeat
            if (i == 0 && (m.scale[0] != 1.0f || m.scale[1] != 1.0f)) {
                g.ban(dir, m.texture);
                continue;
            }
            // primitives only bind the FIRST entry; other entries matter
            // for models, which are scanned with real UVs below
            if (i == 0) g.candidate(dir, m.texture);
        }
    }

    // --- model consumers: real UV bounds per textured submesh ---------------
    for (const std::string& key : modelKeys) {
        const size_t bar = key.find('|');
        const std::string modelRel = key.substr(0, bar);
        const std::string overrideRel = key.substr(bar + 1);
        objparser::Model m;
        if (!objparser::load((root / modelRel).string(), m,
                             overrideRel.empty()
                                 ? ""
                                 : (root / overrideRel).string()))
            continue;
        // textures resolve against the override's dir when present, else the
        // model's own dir (the mtllib/sibling convention)
        const std::string dir =
            dirOf(overrideRel.empty() ? modelRel : overrideRel);
        for (const objparser::Submesh& s : m.submeshes) {
            if (!s.refl.empty() && s.refl != "@sky") g.ban(dir, s.refl);
            if (s.texture.empty()) continue;
            bool inBounds = true;
            constexpr float eps = 1e-3f;
            for (size_t i = 0; i + 7 < s.verts.size() && inBounds; i += 8)
                inBounds = s.verts[i + 6] >= -eps && s.verts[i + 6] <= 1.0f + eps &&
                           s.verts[i + 7] >= -eps && s.verts[i + 7] <= 1.0f + eps;
            if (inBounds)
                g.candidate(dir, s.texture);
            else
                g.ban(dir, s.texture);
        }
    }

    // --- per-asset quality overrides pin their textures ---------------------
    for (const auto& [assetRel, q] : p.textureQuality) {
        (void)q;
        std::vector<objparser::MtlMaterial> mats;
        const fs::path asset = root / assetRel;
        if (fs::path(assetRel).extension() == ".mtl") {
            if (objparser::loadMtl(asset.string(), mats))
                for (const objparser::MtlMaterial& m : mats)
                    g.ban(dirOf(assetRel), m.texture);
        } else if (fs::path(assetRel).extension() == ".obj") {
            objparser::Model m;
            if (objparser::load(asset.string(), m))
                for (const objparser::Submesh& s : m.submeshes)
                    g.ban(dirOf(assetRel), s.texture);
        }
    }

    // --- measure, filter, sort ------------------------------------------------
    struct Member {
        std::string resRel, dirRel;
        int w, h;
    };
    std::vector<Member> members;
    for (const auto& [resRel, dirRel] : g.candidates) {
        if (g.ineligible.count(resRel)) continue;
        int w = 0, h = 0, comp = 0;
        if (!stbi_info((root / resRel).string().c_str(), &w, &h, &comp))
            continue;
        const int bw = nearestValidDim(w), bh = nearestValidDim(h);
        if (bw > 128 || bh > 128) continue;  // fills half a page alone
        members.push_back({resRel, dirRel, bw, bh});
    }
    if (members.size() < 2) return out;
    std::sort(members.begin(), members.end(), [](const Member& a, const Member& b) {
        if (a.dirRel != b.dirRel) return a.dirRel < b.dirRel;
        if (a.h != b.h) return a.h > b.h;
        if (a.w != b.w) return a.w > b.w;
        return a.resRel < b.resRel;
    });

    // --- shelf-pack per directory, 2px gutter all around ---------------------
    constexpr int S = 256, G = 2;
    out.pageSize = S;
    out.fullColor = p.settings.textureQuant == "none";
    std::string curDir;
    int pageInDir = -1, shelfY = 0, shelfH = 0, cursorX = 0;
    auto newPage = [&](const std::string& dirRel) {
        ++pageInDir;
        shelfY = 0, shelfH = 0, cursorX = 0;
        out.pages.push_back(dirRel + "/tyra-atlas-" +
                            std::to_string(pageInDir) + ".png");
    };
    for (const Member& m : members) {
        if (m.dirRel != curDir) {
            curDir = m.dirRel;
            pageInDir = -1;
            newPage(curDir);
        }
        const int cw = m.w + 2 * G, ch = m.h + 2 * G;
        if (cursorX + cw > S) {  // next shelf
            shelfY += shelfH;
            cursorX = 0;
            shelfH = 0;
        }
        if (shelfY + ch > S) newPage(curDir);
        if (ch > shelfH) shelfH = ch;
        Entry e;
        e.resRel = m.resRel;
        e.page = (int)out.pages.size() - 1;
        e.pageRel = out.pages[e.page];
        e.x = cursorX + G;
        e.y = shelfY + G;
        e.w = m.w;
        e.h = m.h;
        e.u0 = (float)e.x / S;
        e.v0 = (float)e.y / S;
        e.du = (float)e.w / S;
        e.dv = (float)e.h / S;
        out.entries.push_back(std::move(e));
        cursorX += cw;
    }
    return out;
}

std::string info(const Plan& plan) {
    if (plan.empty()) return "";
    return "Texture atlas: " + std::to_string(plan.entries.size()) +
           " textures in " + std::to_string(plan.pages.size()) + " page(s)";
}

}  // namespace texatlas
