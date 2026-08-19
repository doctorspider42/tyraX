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

// map_Kd token -> res-relative path, resolved against the .mtl's directory.
// A SUBDIRECTORY token ("Textures/wall.png") resolves normally: the page is
// written into the .mtl's OWN directory either way, so the rewritten
// reference stays a same-directory token and the PS2 host filesystem never
// sees a "..". This used to return "" for any token carrying a separator,
// which silently disqualified every asset pack that keeps its images in a
// subfolder - the shipped night-walk example atlased NOTHING with the
// feature switched on, and said so nowhere. What is still refused is a token
// that climbs out of the project's res/ tree, which no consumer could
// rewrite.
std::string texRel(const std::string& dirRel, const std::string& tok) {
    if (tok.empty()) return "";
    std::string t = tok;
    for (char& c : t)
        if (c == '\\') c = '/';
    const std::string rel =
        (fs::path(dirRel) / t).lexically_normal().generic_string();
    if (rel.rfind("res/", 0) != 0) return "";  // escaped the asset tree
    return rel;
}

struct Gather {
    // candidate texture -> the .mtl directory its consumers resolve against
    std::map<std::string, std::string> candidates;  // resRel -> dirRel
    // resRel -> WHY it cannot be packed (first reason wins). Kept so the
    // editor's Texture Atlas window can explain every absence instead of
    // leaving the author to infer it from a page that never appeared.
    std::map<std::string, std::string> ineligible;

    void candidate(const std::string& dirRel, const std::string& tok) {
        const std::string rel = texRel(dirRel, tok);
        if (rel.empty()) return;
        auto it = candidates.find(rel);
        if (it == candidates.end())
            candidates.emplace(rel, dirRel);
        else if (it->second != dirRel)
            ban(dirRel, tok,
                "referenced from two directories - one page cannot sit in "
                "both");
    }
    void ban(const std::string& dirRel, const std::string& tok,
             const std::string& why) {
        const std::string rel = texRel(dirRel, tok);
        if (!rel.empty()) ineligible.emplace(rel, why);
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
                    g.ban(dirOf(o.materialPath), mats[0].texture,
                          "an emitter, decal, mirror or portal binds it - "
                          "each samples through its own ST path");
            }
        }
        // terrain (base + paint layers) tiles its textures - never atlas
        auto banTerrainMtl = [&](const std::string& mtlRel) {
            if (mtlRel.empty()) return;
            std::vector<objparser::MtlMaterial> mats;
            if (objparser::loadMtl((root / mtlRel).string(), mats))
                for (const objparser::MtlMaterial& m : mats)
                    g.ban(dirOf(mtlRel), m.texture,
                          "the terrain tiles it (UVs run far past 0..1)");
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
            if (!m.refl.empty() && m.refl != "@sky")
                g.ban(dir, m.refl,
                      "a refl sphere map - its STs are computed at runtime");
            if (m.texture.empty()) continue;
            // a tiling factor means the texture is meant to repeat
            if (i == 0 && (m.scale[0] != 1.0f || m.scale[1] != 1.0f)) {
                g.ban(dir, m.texture,
                      "the material has a tiling factor (map_Kd -s)");
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
            if (!s.refl.empty() && s.refl != "@sky")
                g.ban(dir, s.refl,
                      "a refl sphere map - its STs are computed at runtime");
            if (s.texture.empty()) continue;
            bool inBounds = true;
            constexpr float eps = 1e-3f;
            for (size_t i = 0; i + 7 < s.verts.size() && inBounds; i += 8)
                inBounds = s.verts[i + 6] >= -eps && s.verts[i + 6] <= 1.0f + eps &&
                           s.verts[i + 7] >= -eps && s.verts[i + 7] <= 1.0f + eps;
            if (inBounds)
                g.candidate(dir, s.texture);
            else
                g.ban(dir, s.texture,
                      "a model samples it outside 0..1 - it is meant to "
                      "repeat");
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
                    g.ban(dirOf(assetRel), m.texture,
                          "a per-asset texture quality is pinned on it - "
                          "pages re-quantize as one image");
        } else if (fs::path(assetRel).extension() == ".obj") {
            objparser::Model m;
            if (objparser::load(asset.string(), m))
                for (const objparser::Submesh& s : m.submeshes)
                    g.ban(dirOf(assetRel), s.texture,
                          "a per-asset texture quality is pinned on it - "
                          "pages re-quantize as one image");
        }
    }

    // --- the author's own decisions (docs/texture-atlasing.md) --------------
    // keepOut is the honest form of the trick that has always existed (pin a
    // per-asset quality and the texture drops out); a group name replaces the
    // directory as the packing key, because "what shares a page" is a claim
    // about the SCENE - one page is one allocation and one palette - and the
    // folder layout only approximates it.
    for (const auto& [texRelPath, ctl] : p.atlasControl)
        if (ctl.keepOut)
            g.ineligible.emplace(texRelPath, "kept out by the project");

    // --- measure, filter, sort ------------------------------------------------
    struct Member {
        std::string resRel, dirRel, group;
        int w, h;
        int bucket = 0;  // palette bucket within the group
    };
    std::vector<Member> members;
    for (const auto& [resRel, dirRel] : g.candidates) {
        if (auto it = g.ineligible.find(resRel); it != g.ineligible.end()) {
            out.excluded.push_back({resRel, it->second});
            continue;
        }
        int w = 0, h = 0, comp = 0;
        if (!stbi_info((root / resRel).string().c_str(), &w, &h, &comp)) {
            out.excluded.push_back({resRel, "the image could not be read"});
            continue;
        }
        const int bw = nearestValidDim(w), bh = nearestValidDim(h);
        if (bw > 128 || bh > 128) {  // fills half a page alone
            out.excluded.push_back(
                {resRel, "bakes to " + std::to_string(bw) + "x" +
                             std::to_string(bh) +
                             " - too big to share a 256 page"});
            continue;
        }
        std::string group = dirRel;
        if (auto it = p.atlasControl.find(resRel);
            it != p.atlasControl.end() && !it->second.group.empty())
            group = "@" + it->second.group;  // "@" cannot collide with a dir
        members.push_back({resRel, dirRel, group, bw, bh});
    }
    // A texture that was BANNED without ever becoming a candidate (the whole
    // model tiles it, the terrain owns it, an emitter binds it) is exactly
    // what an author looking for a missing texture wants explained, so those
    // are reported too - the loop above only sees rejected candidates.
    for (const auto& [resRel, why] : g.ineligible)
        if (!g.candidates.count(resRel))
            out.excluded.push_back({resRel, why});
    // Sort the exclusions too - the window reads them, and a stable order is
    // what makes two runs of the plan comparable.
    std::sort(out.excluded.begin(), out.excluded.end(),
              [](const Excluded& a, const Excluded& b) {
                  return a.resRel < b.resRel;
              });
    if (members.size() < 2) return out;

    // --- palette buckets: keep clashing colours off one shared CLUT ---------
    // A page is quantized AS ONE IMAGE, so a vivid red sign packed beside a
    // blue crate spends the page's palette twice over and both come back
    // muddy. Members are bucketed by a coarse average hue/……value signature
    // and pages are cut per bucket - but only for a group with more than one
    // page's worth of content, because splitting a half-empty page costs more
    // VRAM than the palette ever buys back.
    {
        std::map<std::string, long long> areaOf;
        for (const Member& m : members)
            areaOf[m.group] += (long long)(m.w + 2 * 2) * (m.h + 2 * 2);
        for (Member& m : members) {
            if (areaOf[m.group] <= 256LL * 256LL) continue;  // one page anyway
            int w = 0, h = 0, comp = 0;
            unsigned char* px =
                stbi_load((root / m.resRel).string().c_str(), &w, &h, &comp, 4);
            if (!px) continue;
            double sr = 0, sg = 0, sb = 0;
            const long long n = (long long)w * h;
            for (long long i = 0; i < n; ++i) {
                sr += px[i * 4 + 0];
                sg += px[i * 4 + 1];
                sb += px[i * 4 + 2];
            }
            stbi_image_free(px);
            if (n <= 0) continue;
            const double r = sr / n, gg = sg / n, b = sb / n;
            const double mx = std::max(r, std::max(gg, b));
            const double mn = std::min(r, std::min(gg, b));
            // Grey (low saturation) is its own bucket - greys sit happily on
            // any palette and would otherwise scatter across every page.
            if (mx - mn < 24.0) {
                m.bucket = 0;
                continue;
            }
            double hue;  // 0..6, the standard sextant form
            if (mx == r)
                hue = (gg - b) / (mx - mn);
            else if (mx == gg)
                hue = 2.0 + (b - r) / (mx - mn);
            else
                hue = 4.0 + (r - gg) / (mx - mn);
            if (hue < 0) hue += 6.0;
            m.bucket = 1 + (int)(hue) % 6;  // six colour buckets + grey
        }
    }

    std::sort(members.begin(), members.end(), [](const Member& a, const Member& b) {
        if (a.group != b.group) return a.group < b.group;
        if (a.bucket != b.bucket) return a.bucket < b.bucket;
        if (a.h != b.h) return a.h > b.h;
        if (a.w != b.w) return a.w > b.w;
        return a.resRel < b.resRel;
    });

    // --- shelf-pack per directory, 2px gutter all around ---------------------
    constexpr int S = 256, G = 2;
    out.pageSize = S;
    out.fullColor = p.settings.textureQuant == "none";
    // The page FILE always lands in the .mtl's own directory (so the rewritten
    // map_Kd stays a same-directory token); the GROUP only decides who shares
    // it, and its pages are numbered per directory so two groups in one folder
    // cannot claim the same file name.
    std::string curGroup;
    int pageInDir = -1, shelfY = 0, shelfH = 0, cursorX = 0;
    std::map<std::string, int> pagesInDir;
    auto newPage = [&](const std::string& dirRel, const std::string& group) {
        pageInDir = pagesInDir[dirRel]++;
        shelfY = 0, shelfH = 0, cursorX = 0;
        out.pages.push_back(dirRel + "/tyra-atlas-" +
                            std::to_string(pageInDir) + ".png");
        out.pageGroup.push_back(group);
    };
    for (const Member& m : members) {
        if (m.group != curGroup) {
            curGroup = m.group;
            newPage(m.dirRel, m.group);
        }
        const int cw = m.w + 2 * G, ch = m.h + 2 * G;
        if (cursorX + cw > S) {  // next shelf
            shelfY += shelfH;
            cursorX = 0;
            shelfH = 0;
        }
        if (shelfY + ch > S) newPage(m.dirRel, m.group);
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

    // --- drop pages that ended up with ONE member ---------------------------
    // A page is a full 256x256 allocation whatever it holds, so a lone member
    // on one is strictly worse than shipping that texture by itself - it pays
    // a whole page, loses its own palette to the page's quantization, and
    // keeps nothing resident that it needed. It happens naturally: a group
    // with one eligible texture, or the last shelf of a bucket.
    {
        std::map<int, int> countPerPage;
        for (const Entry& e : out.entries) countPerPage[e.page]++;
        std::vector<Entry> kept;
        std::vector<std::string> keptPages, keptGroups;
        std::map<int, int> remap;
        for (int i = 0; i < (int)out.pages.size(); ++i) {
            if (countPerPage[i] < 2) continue;
            remap[i] = (int)keptPages.size();
            keptPages.push_back(out.pages[i]);
            keptGroups.push_back(i < (int)out.pageGroup.size()
                                     ? out.pageGroup[i]
                                     : std::string());
        }
        for (Entry& e : out.entries) {
            auto it = remap.find(e.page);
            if (it == remap.end()) {
                out.excluded.push_back(
                    {e.resRel,
                     "the only texture that qualified in its group - a page "
                     "to itself would cost more than it saves"});
                continue;
            }
            e.page = it->second;
            e.pageRel = keptPages[e.page];
            kept.push_back(std::move(e));
        }
        // Page FILE names are per directory and were numbered before this
        // cull, so renumber what survives or the .mtl rewrite would point at
        // a page nobody composites.
        std::map<std::string, int> nextInDir;
        for (size_t i = 0; i < keptPages.size(); ++i) {
            const std::string dir =
                fs::path(keptPages[i]).parent_path().generic_string();
            keptPages[i] = dir + "/tyra-atlas-" +
                           std::to_string(nextInDir[dir]++) + ".png";
        }
        for (Entry& e : kept) e.pageRel = keptPages[e.page];
        out.entries = std::move(kept);
        out.pages = std::move(keptPages);
        out.pageGroup = std::move(keptGroups);
        std::sort(out.excluded.begin(), out.excluded.end(),
                  [](const Excluded& a, const Excluded& b) {
                      return a.resRel < b.resRel;
                  });
    }
    return out;
}

// The GS footprint of one texture, in words - the host twin of the engine's
// RendererCoreVram::getSize (whole pages above one page; a palettized image
// also carries its CLUT). menulayout.cpp holds the same arithmetic for the
// menu bake; both mirror the engine, which is the authority.
static int gsWords(int w, int h, int bits) {
    if (w <= 0 || h <= 0) return 0;
    const int pageW = bits == 32 ? 64 : 128;
    const int pageH = bits == 32 ? 32 : bits == 8 ? 64 : 128;
    const int pagesW = (w + pageW - 1) / pageW;
    const int pagesH = (h + pageH - 1) / pageH;
    const int clut = bits == 4 ? 64 : bits == 8 ? 256 : 0;
    return pagesW * pagesH * 2048 + clut;
}

VramEstimate vram(const Plan& plan, const Project& p) {
    VramEstimate out;
    if (plan.empty()) return out;
    // A member ships at the project's own depth; a PAGE is quantized as one
    // image, so a palettized project's pages are 8-bit whatever the members
    // were. That asymmetry is the whole reason this is measured.
    const int memberBits = p.settings.textureQuant == "none"  ? 32
                           : p.settings.textureQuant == "8bit" ? 8
                                                               : 4;
    const int pageBits = plan.fullColor ? 32 : 8;
    long long memberWords = 0;
    for (const Entry& e : plan.entries)
        memberWords += gsWords(e.w, e.h, memberBits);
    const long long pageWords =
        (long long)plan.pages.size() * gsWords(plan.pageSize, plan.pageSize, pageBits);
    out.membersKb = (int)(memberWords * 4 / 1024);
    out.pagesKb = (int)(pageWords * 4 / 1024);
    out.savedKb = out.membersKb - out.pagesKb;
    return out;
}

std::string info(const Plan& plan) {
    if (plan.empty()) return "";
    return "Texture atlas: " + std::to_string(plan.entries.size()) +
           " textures in " + std::to_string(plan.pages.size()) + " page(s)";
}

}  // namespace texatlas
