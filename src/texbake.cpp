#include "texbake.hpp"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <vector>

#include <stb_image.h>

#include "aobake.hpp"    // model AO sidecars (<model>.aov)
#include "gibake.hpp"    // the baked global-illumination cache
#include "menubake.hpp"  // atlasFileName - which res/fonts PNGs are atlases
#include "menulayout.hpp"  // per-menu texture list + its `quant`
#include "modelao.hpp"   // automatic per-asset model AO, multiplied into the PNG
#include "objparser.hpp"
#include "pngquant.hpp"
#include "shadowbake.hpp"  // baked shadow decals - the atlas pages
#include "skytex.hpp"      // the painted sky's crop (docs/sky-texture.md)
#include "stochtile.hpp"
#include "texatlas.hpp"  // shared texture atlas plan (docs/texture-atlasing.md)

namespace fs = std::filesystem;

namespace texbake {

namespace {

// quality ranks, highest wins when assets share a texture
int rankOf(const std::string& q) {
    if (q == "none") return 3;   // full color
    if (q == "8bit") return 2;
    if (q == "4bit") return 1;
    return 0;  // unknown / follow project
}

int colorsOf(const std::string& q) {
    if (q == "8bit") return 256;
    if (q == "4bit") return 16;
    return 0;  // full color - plain copy
}

std::string lowerExt(const fs::path& p) {
    std::string e = p.extension().string();
    for (char& c : e) c = (char)tolower((unsigned char)c);
    return e;
}

// The PS2 texture dimensions the engine accepts (a runtime assert otherwise).
int nearestValidDim(int v) {
    static const int valid[] = {8, 16, 32, 64, 128, 256, 512};
    int best = valid[0], bestDist = 1 << 30;
    for (int d : valid) {
        const int dd = v > d ? v - d : d - v;
        if (dd < bestDist) { bestDist = dd; best = d; }
    }
    return best;
}

// Resize a HUD PNG into a valid power-of-two (HudImage::texW/texH, 0 = nearest
// to the source) and optionally palette-quantize it, writing dst. On a quant
// failure it falls back to a full-color write - still a valid size, so the
// game never asserts. Returns false only if the source cannot be decoded or
// nothing could be written (then the caller copies verbatim).
// Rewrites a .mtl into the bake with atlased map_Kd references redirected
// to their page + a "# tyra-uvrect u0 v0 du dv" hint line right after (the
// engine's LeanObjLoader applies it per material - see texatlas.hpp).
// Returns false when the file references no atlas member (caller copies
// verbatim). dirRel = res-relative directory of the .mtl.
// A crowd palette variant: "<base>.v<k>.png" beside "<base>.png" (the
// Character Generator writes them, docs/character-generator.md "Crowds").
// Returns k (1..99), or 0 for any other file; `base` gets the base's path.
int paletteVariantOf(const fs::path& p, fs::path* base) {
    const std::string name = p.filename().string();
    if (name.size() < 8 || lowerExt(p) != ".png") return 0;
    const std::string stem = name.substr(0, name.size() - 4);  // "x.v3"
    const size_t dot = stem.rfind(".v");
    if (dot == std::string::npos || dot + 2 >= stem.size()) return 0;
    int k = 0;
    for (size_t i = dot + 2; i < stem.size(); ++i) {
        if (stem[i] < '0' || stem[i] > '9') return 0;
        k = k * 10 + (stem[i] - '0');
    }
    if (k < 1 || k > 99) return 0;
    if (base) *base = p.parent_path() / (stem.substr(0, dot) + ".png");
    return k;
}

bool rewriteMtlForAtlas(const fs::path& src, const fs::path& dst,
                        const std::string& dirRel,
                        const texatlas::Plan& plan) {
    std::ifstream in(src);
    if (!in) return false;
    std::ostringstream out;
    bool any = false;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::istringstream ss(line);
        std::string tag;
        ss >> tag;
        if (tag == "map_Kd") {
            std::vector<std::string> toks;
            for (std::string t; ss >> t;) toks.push_back(t);
            std::string tex = toks.empty() ? "" : toks.back();
            for (char& c : tex)
                if (c == '\\') c = '/';
            // Resolve the token against the .mtl's directory - a
            // subdirectory reference ("Textures/wall.png") is an ordinary
            // member now, and the page it is redirected to still sits in
            // this .mtl's own folder, so the line stays same-directory.
            if (!tex.empty()) {
                const std::string rel =
                    (fs::path(dirRel) / tex).lexically_normal().generic_string();
                if (const texatlas::Entry* en = plan.find(rel)) {
                    // eligibility rejected tiling/options, so the line can
                    // be regenerated plain
                    out << "map_Kd "
                        << fs::path(en->pageRel).filename().string() << "\n";
                    char buf[96];
                    std::snprintf(buf, sizeof(buf),
                                  "# tyra-uvrect %.6g %.6g %.6g %.6g", en->u0,
                                  en->v0, en->du, en->dv);
                    out << buf << "\n";
                    any = true;
                    continue;
                }
            }
        }
        out << line << "\n";
    }
    if (!any) return false;
    std::ofstream o(dst, std::ios::trunc);
    if (!o) return false;
    o << out.str();
    return true;
}

// A previously baked .mtl carrying atlas rewrites - must be re-copied
// verbatim when the plan no longer covers it (the timestamp gate alone
// would keep the stale rewrite).
bool bakedMtlWasRewritten(const fs::path& dst) {
    std::ifstream in(dst);
    if (!in) return false;
    std::string line;
    while (std::getline(in, line))
        if (line.rfind("# tyra-uvrect", 0) == 0) return true;
    return false;
}

bool bakeHudImage(const fs::path& src, const fs::path& dst, const HudImage& hi,
                  const std::string& quant,
                  const std::function<void(const std::string&)>& log) {
    int sw = 0, sh = 0, comp = 0;
    unsigned char* px = stbi_load(src.string().c_str(), &sw, &sh, &comp, 4);
    if (!px) {
        log("[editor] HUD bake: cannot decode " + src.filename().string());
        return false;
    }
    const int tw = hi.texW > 0 ? hi.texW : nearestValidDim(sw);
    const int th = hi.texH > 0 ? hi.texH : nearestValidDim(sh);
    const int cols = colorsOf(quant);  // "" already resolved to the project

    std::vector<unsigned char> buf;
    const unsigned char* pixels = px;
    if (tw != sw || th != sh) {
        buf = pngquant::resizeRGBA(px, sw, sh, tw, th);
        pixels = buf.data();
    }

    std::string err;
    bool ok = false;
    if (cols > 0) {
        ok = pngquant::quantizeRGBA(dst.string(), pixels, tw, th, cols, err);
        if (!ok) {
            log("[editor] HUD bake: " + src.filename().string() + ": " + err +
                " - written full color");
            ok = pngquant::writePngRGBA(dst.string(), pixels, tw, th, err);
        }
    } else {
        ok = pngquant::writePngRGBA(dst.string(), pixels, tw, th, err);
    }
    stbi_image_free(px);
    if (!ok) log("[editor] HUD bake: " + src.filename().string() + ": " + err);
    return ok;
}

}  // namespace

std::string bake(const Project& p,
                 const std::function<void(const std::string&)>& log) {
    const fs::path res = fs::path(p.dir) / "res";
    const fs::path baked = fs::path(p.dir) / ".res-baked";
    std::error_code ec;
    if (!fs::exists(res, ec)) return "";  // nothing to bake
    fs::create_directories(baked, ec);

    // --- resolve the quality of every referenced PNG -----------------------
    // rel res-paths ("res/models/x.png") -> best quality seen so far
    std::map<std::string, std::string> quality;
    auto assetQuality = [&](const std::string& assetRel) -> std::string {
        auto it = p.textureQuality.find(assetRel);
        // a character's other body ("<x>-alt.glb", the creator's Body row)
        // bakes like the body it stands in for
        if (it == p.textureQuality.end() && assetRel.size() > 8 &&
            assetRel.compare(assetRel.size() - 8, 8, "-alt.glb") == 0)
            it = p.textureQuality.find(assetRel.substr(0, assetRel.size() - 8) + ".glb");
        return it == p.textureQuality.end() ? "" : it->second;
    };
    auto claim = [&](const std::string& pngRel, const std::string& q) {
        if (q.empty()) return;  // asset follows the project default
        std::string& cur = quality[pngRel];
        if (rankOf(q) > rankOf(cur)) cur = q;
    };
    auto texturesOf = [&](const fs::path& dir, const std::string& texRel,
                          std::vector<std::string>* out) {
        // material texture path -> project-relative res path
        fs::path full = (dir / texRel).lexically_normal();
        out->push_back(fs::relative(full, fs::path(p.dir), ec).generic_string());
    };

    // models: their own material libraries claim their textures
    for (const auto& e : fs::recursive_directory_iterator(res / "models", ec)) {
        if (!e.is_regular_file() || lowerExt(e.path()) != ".obj") continue;
        const std::string assetRel =
            fs::relative(e.path(), fs::path(p.dir), ec).generic_string();
        objparser::Model model;
        if (!objparser::load(e.path().string(), model)) continue;
        const std::string q = assetQuality(assetRel);
        for (const objparser::Submesh& s : model.submeshes) {
            for (const std::string& tex : {s.texture, s.refl}) {
                if (tex.empty() || tex == "@sky") continue;  // dynamic env map
                std::vector<std::string> rel;
                texturesOf(e.path().parent_path(), tex, &rel);
                for (const std::string& r : rel) claim(r, q);
            }
        }
    }
    // animated models (.glb / .fbx): the animated-model bake extracts their
    // embedded images next to the .tskl as "<stem>_<image>.png" (also with a
    // material-override suffix), so an override of the MODEL claims every PNG
    // carrying its stem. This is how a generated character's skin atlas gets
    // the 8 bits a face needs (docs/character-generator.md) while the project
    // stays at 4.
    for (const auto& e : fs::recursive_directory_iterator(res / "models", ec)) {
        if (!e.is_regular_file()) continue;
        const std::string ext = lowerExt(e.path());
        if (ext != ".glb" && ext != ".fbx") continue;
        const std::string q =
            assetQuality(fs::relative(e.path(), fs::path(p.dir), ec).generic_string());
        if (q.empty()) continue;
        const std::string prefix = e.path().stem().string() + "_";
        for (const auto& s : fs::directory_iterator(e.path().parent_path(), ec)) {
            const std::string name = s.path().filename().string();
            if (s.is_regular_file() && lowerExt(s.path()) == ".png" &&
                name.compare(0, prefix.size(), prefix) == 0)
                claim(fs::relative(s.path(), fs::path(p.dir), ec).generic_string(), q);
        }
    }
    // standalone material libraries (res/materials + mtls next to models)
    for (const char* sub : {"materials", "models"}) {
        for (const auto& e : fs::recursive_directory_iterator(res / sub, ec)) {
            if (!e.is_regular_file() || lowerExt(e.path()) != ".mtl") continue;
            const std::string assetRel =
                fs::relative(e.path(), fs::path(p.dir), ec).generic_string();
            std::vector<objparser::MtlMaterial> materials;
            if (!objparser::loadMtl(e.path().string(), materials)) continue;
            const std::string q = assetQuality(assetRel);
            for (const objparser::MtlMaterial& m : materials) {
                for (const std::string& tex : {m.texture, m.refl}) {
                    if (tex.empty() || tex == "@sky") continue;  // dynamic env map
                    std::vector<std::string> rel;
                    texturesOf(e.path().parent_path(), tex, &rel);
                    for (const std::string& r : rel) claim(r, q);
                }
            }
        }
    }

    // HUD images referenced by the project, keyed by res path (last wins if a
    // file is used by several entries - a rare, contradictory case). A custom
    // USE prompt image is baked the same way (pow2 resize + quantization).
    std::map<std::string, const HudImage*> hudBake;
    for (const HudImage& h : p.hud) hudBake[h.imagePath] = &h;
    if (!p.usePrompt.imagePath.empty())
        hudBake[p.usePrompt.imagePath] = &p.usePrompt;
    // Loading-screen images and quantized-bar segment sprites bake the same
    // way (they live in res/hud/ alongside the HUD images).
    for (const LoadingScreenDef& ls : p.loadingScreens) {
        for (const HudImage& h : ls.images) hudBake[h.imagePath] = &h;
        for (const LoadingBar& b : ls.bars)
            if (!b.segImage.imagePath.empty())
                hudBake[b.segImage.imagePath] = &b.segImage;
    }
    for (const SplashScreen& s : p.splashScreens)
        if (!s.image.imagePath.empty()) hudBake[s.image.imagePath] = &s.image;
    // HUD bars: an optional fill image and frame image, baked like the rest.
    for (const HudBar& b : p.hudBars) {
        if (!b.fillImage.imagePath.empty())
            hudBake[b.fillImage.imagePath] = &b.fillImage;
        if (!b.frameImage.imagePath.empty())
            hudBake[b.frameImage.imagePath] = &b.frameImage;
    }

    // Font atlases (res/fonts/atlas-<name>.png, baked by refreshGenerated for
    // the fonts a Display Text node uses): quantized per Font Manager entry
    // rather than by the project default, because an atlas is white glyphs the
    // runtime tints - 16 colors usually costs nothing visually and saves ~8x
    // the VRAM, which matters on a ~1.33 MB texture budget.
    std::map<std::string, std::string> fontQuant;
    for (const GameFont& gf : p.fonts)
        fontQuant["res/fonts/" + menubake::atlasFileName(gf.name)] = gf.quant;

    // Credits pages (res/credits/pages/<roll>-<k>.png, baked by refreshGenerated):
    // like a font atlas, the roll carries its own depth (CreditsRoll::quant)
    // instead of following the project default. It has to: a page is a
    // half-screen 512x256 texture and a roll is a dozen of them, so full color
    // would spend the whole ~1.33 MB GS budget on credits (docs/credits.md).
    // The skip-hint sprite is left alone - it is one small transparent text.
    std::map<std::string, std::string> creditsQuant;
    for (const CreditsRoll& r : p.credits) {
        const menubake::CreditsLayout l = menubake::creditsLayout(r, p);
        for (int k = 0; k < l.pageCount; ++k)
            creditsQuant[std::string(menubake::kCreditsBakeDir) + "/" +
                         menubake::creditsPageFileName(r.name, k)] =
                r.quant;
    }

    // Menu textures (res/menus/*.png, baked by refreshGenerated): the panel and
    // everything that goes with it follow the stylesheet's `quant`, for the same
    // reason a credits page does. Menu art is flat colour and text, so 4-bit is
    // nearly lossless - and it is the difference between a full-screen 512x512
    // panel costing 93% of the GS texture heap and costing 12% (docs/gs-vram.md).
    // Default (unset) leaves them full colour, which is what every existing
    // project already ships.
    std::map<std::string, std::string> menuQuant;
    for (const GameMenu& m : p.menus) {
        const menulayout::Layout ml = menulayout::compute(m, p);
        const char* q = ml.panel.quant == 1   ? "4bit"
                        : ml.panel.quant == 2 ? "8bit"
                                              : "";
        if (!*q) continue;
        for (const menulayout::Texture& t : ml.textures)
            menuQuant["menus/" + t.file] = q;
        if (menubake::menuHasValueEntries(m))
            menuQuant["menus/" + menulayout::valueStripFileName(m.name)] = q;
    }

    // --- mirror res/ into .res-baked/ --------------------------------------
    // Editor-only assets never ship: paint brushes (res/brushes), the Material
    // Editor's paint-layer sidecars (`<texture>.layers/` dirs - the game loads
    // the flattened composite PNG next to them), and the source TTFs under
    // res/fonts. The PS2 never reads a TTF: static text is baked to sprites and
    // dynamic text to a glyph atlas, both at build. Only the atlas PNGs in that
    // folder ship.
    auto editorOnly = [](const fs::path& rel) {
        for (const fs::path& part : rel)
            if (part.string().size() > 7 &&
                part.string().rfind(".layers") == part.string().size() - 7)
                return true;
        // "<model>.uvs" replacement-UV sidecars (animated-model unwrap) are
        // folded into the baked .tskl - the file itself never ships
        if (lowerExt(rel) == ".uvs") return true;
        // "<track>.drone" patches (Drone Generator) describe how a WAV was
        // generated; the game only ever streams the WAV.
        if (lowerExt(rel) == ".drone") return true;
        const std::string top = rel.begin()->generic_string();
        // res/sky/ holds the painted skies' SOURCE panoramas; the game loads
        // only the crop baked into .res-baked/sky/ (docs/sky-texture.md).
        if (top == "sky") return true;
        if (top == "fonts") {
            const std::string ext = lowerExt(rel);
            return ext == ".ttf" || ext == ".otf";
        }
        return top == "brushes";
    };
    // A static .obj whose binary .tmdl was baked next to it never ships: the
    // game loads the .tmdl (docs/model-pipeline.md) and the ASCII source is
    // the bulk of a model's size on the disc. A per-object material override
    // bakes to "<stem>__ovr<hash>.tmdl", so any .tmdl derived from this stem
    // counts. The .mtl keeps shipping - it may also be a standalone material
    // asset that primitives load through MATERIAL_PATHS.
    // Custom LOD meshes are folded into their model's .tmdl as tiers, so the
    // tier .obj itself never ships either (res-relative paths, as stored).
    std::set<std::string> customLodFiles;
    for (const auto& [asset, tiers] : p.modelLods)
        for (const std::string& t : tiers) customLodFiles.insert(t);

    auto supersededByTmdl = [&](const fs::path& src) {
        if (lowerExt(src) != ".obj") return false;
        {
            const std::string rel =
                fs::relative(src, fs::path(p.dir), ec).generic_string();
            if (customLodFiles.count(rel)) return true;
        }
        const std::string stem = src.stem().string();
        std::error_code sec;
        for (const auto& s : fs::directory_iterator(src.parent_path(), sec)) {
            if (!s.is_regular_file() || lowerExt(s.path()) != ".tmdl") continue;
            const std::string cand = s.path().stem().string();
            if (cand == stem || cand.rfind(stem + "__ovr", 0) == 0) return true;
        }
        return false;
    };
    const std::string defaultQ = p.settings.textureQuant;  // none/8bit/4bit
    // Texture atlasing (docs/texture-atlasing.md): the shared deterministic
    // plan - members skip their individual bake (the composited pages are
    // written after the loop) and their .mtl consumers are rewritten.
    const texatlas::Plan atlasPlan = texatlas::plan(p);
    // Automatic model AO (docs/ambient-occlusion.md, "Model AO"): make sure
    // every eligible model asset has a fresh AO map, then multiply it into the
    // texture as it is mirrored. Baking here rather than relying on the editor
    // is what makes a headless `--build` correct with no editor running; the
    // cache means a build that changed nothing pays a hash, not a raytrace.
    const modelao::Params aoParams = modelao::paramsOf(p.settings);
    std::map<std::string, std::string> aoMaps;
    if (aoParams.enabled || !p.modelAoMode.empty()) {
        const modelao::Plan aoPlan = modelao::plan(p, aoParams);
        aoMaps = modelao::ensureAll(p, aoParams, aoPlan, log);
        for (const modelao::Skipped& s : aoPlan.skipped)
            if (s.reason == "shared texture" || s.reason == "pre-lit")
                log("[editor] model AO: " + s.textureRel + " skipped (" +
                    s.reason + ")");
    }
    int quantized = 0, copied = 0;
    for (const auto& e : fs::recursive_directory_iterator(res, ec)) {
        if (!e.is_regular_file()) continue;
        const fs::path rel = fs::relative(e.path(), res, ec);
        if (editorOnly(rel)) continue;
        const fs::path dst = baked / rel;
        fs::create_directories(dst.parent_path(), ec);

        const std::string relRes = ("res/" + rel.generic_string());
        const std::string top = rel.begin()->generic_string();

        // crowd palette variants ship as a .pal fitted below, never as a PNG
        if (paletteVariantOf(e.path(), nullptr) > 0) {
            fs::remove(dst, ec);
            continue;
        }
        // atlas members ship only inside their page
        if (atlasPlan.find(relRes)) {
            fs::remove(dst, ec);  // a pre-atlas bake may have mirrored it
            continue;
        }
        if (supersededByTmdl(e.path())) {
            fs::remove(dst, ec);  // an earlier bake may have mirrored the .obj
            continue;
        }
        if (lowerExt(e.path()) == ".mtl" &&
            (top == "models" || top == "materials" || top == "textures")) {
            const std::string dirRel =
                "res/" + rel.parent_path().generic_string();
            if (!atlasPlan.empty() &&
                rewriteMtlForAtlas(e.path(), dst, dirRel, atlasPlan)) {
                ++quantized;
                continue;
            }
            // plan no longer covers this file: purge a stale rewrite that
            // the timestamp gate below would otherwise keep
            std::error_code mec;
            if (fs::exists(dst, mec) && bakedMtlWasRewritten(dst))
                fs::remove(dst, mec);
        }

        // HUD sprites: resize to a PS2-valid size (+ optional quantize) so a
        // mis-sized import cannot assert in-game. Built-in HUD assets (use.png,
        // loading.png, save-*.png, ...) are not project entries - copied below.
        if (top == "hud" && lowerExt(e.path()) == ".png") {
            if (auto it = hudBake.find(relRes); it != hudBake.end()) {
                // "" = follow the project texture default, like materials.
                const std::string q = it->second->texQuant.empty()
                                          ? defaultQ
                                          : it->second->texQuant;
                if (bakeHudImage(e.path(), dst, *it->second, q, log)) {
                    ++quantized;
                    continue;
                }
                // fell through: decode/write failed - copy verbatim below
            }
        }

        bool quantizable =
            lowerExt(e.path()) == ".png" &&
            (top == "models" || top == "materials" || top == "textures");

        std::string q = defaultQ;
        if (auto it = quality.find(relRes); it != quality.end()) q = it->second;
        // A font atlas carries its own depth (GameFont::quant) and ignores the
        // project default.
        if (top == "fonts" && lowerExt(e.path()) == ".png") {
            if (auto it = fontQuant.find(relRes); it != fontQuant.end()) {
                quantizable = true;
                q = it->second;
            }
        }
        if (top == "credits" && lowerExt(e.path()) == ".png") {
            if (auto it = creditsQuant.find(relRes); it != creditsQuant.end()) {
                quantizable = true;
                q = it->second;
            }
        }
        if (top == "menus" && lowerExt(e.path()) == ".png") {
            if (auto it = menuQuant.find(relRes); it != menuQuant.end()) {
                quantizable = true;
                q = it->second;
            }
        }
        const int colors = quantizable ? colorsOf(q) : 0;

        // Model AO: multiply the asset's own occlusion into this texture's RGB
        // (alpha untouched - the GS cutout rule). It happens BEFORE the resize
        // and the quantization below, so a palette is computed from the pixels
        // the console will actually display. res/ is never written.
        if (auto aoIt = aoMaps.find(relRes); aoIt != aoMaps.end()) {
            int sw = 0, sh = 0, comp = 0;
            unsigned char* px =
                stbi_load(e.path().string().c_str(), &sw, &sh, &comp, 4);
            if (px) {
                std::vector<unsigned char> buf(px, px + (size_t)sw * sh * 4);
                stbi_image_free(px);
                if (modelao::applyMapFile(aoIt->second, buf.data(), sw, sh,
                                          aoParams.strength)) {
                    const int tw = nearestValidDim(sw), th = nearestValidDim(sh);
                    std::vector<unsigned char> resized;
                    const unsigned char* pixels = buf.data();
                    if (tw != sw || th != sh) {
                        resized = pngquant::resizeRGBA(buf.data(), sw, sh, tw, th);
                        pixels = resized.data();
                    }
                    std::string err;
                    const bool ok =
                        colors > 0
                            ? pngquant::quantizeRGBA(dst.string(), pixels, tw, th,
                                                     colors, err)
                            : pngquant::writePngRGBA(dst.string(), pixels, tw, th,
                                                     err);
                    if (ok) {
                        log("[editor] model AO: multiplied into " + relRes);
                        ++quantized;
                        continue;
                    }
                    log("[editor] model AO: " + relRes + ": " + err +
                        " - shipped without it");
                }
            }
        }

        // Scene textures must be PS2-valid (power-of-two, max 512 per axis -
        // the engine asserts otherwise). An oversized/odd import (a "1k"
        // download, say) is resized INTO THE BAKE like HUD sprites are; the
        // source in res/ keeps its full resolution for the editor viewport.
        if (quantizable) {
            int sw = 0, sh = 0, comp = 0;
            if (stbi_info(e.path().string().c_str(), &sw, &sh, &comp) &&
                (sw != nearestValidDim(sw) || sh != nearestValidDim(sh))) {
                const int tw = nearestValidDim(sw), th = nearestValidDim(sh);
                unsigned char* px =
                    stbi_load(e.path().string().c_str(), &sw, &sh, &comp, 4);
                if (px) {
                    std::vector<unsigned char> buf =
                        pngquant::resizeRGBA(px, sw, sh, tw, th);
                    stbi_image_free(px);
                    std::string err;
                    const bool ok =
                        colors > 0 ? pngquant::quantizeRGBA(dst.string(), buf.data(),
                                                            tw, th, colors, err)
                                   : pngquant::writePngRGBA(dst.string(), buf.data(),
                                                            tw, th, err);
                    if (ok) {
                        log("[editor] texture bake: " + relRes + ": " +
                            std::to_string(sw) + "x" + std::to_string(sh) +
                            " resized to PS2-valid " + std::to_string(tw) + "x" +
                            std::to_string(th));
                        ++quantized;
                        continue;
                    }
                    log("[editor] texture bake: " + relRes + ": " + err +
                        " - copied at original size (the game may reject it)");
                }
                // decode/write failed - fall through to the plain paths below
            }
        }

        if (colors > 0) {
            std::string err;
            if (pngquant::quantize(e.path().string(), dst.string(), colors, err)) {
                ++quantized;
            } else {
                log("[editor] texture bake: " + relRes + ": " + err +
                    " - copied unquantized");
                fs::copy_file(e.path(), dst, fs::copy_options::overwrite_existing,
                              ec);
            }
            continue;
        }
        // verbatim copy, skipped when up to date
        std::error_code tec;
        if (!fs::exists(dst, tec) ||
            fs::last_write_time(e.path(), tec) > fs::last_write_time(dst, tec)) {
            fs::copy_file(e.path(), dst, fs::copy_options::overwrite_existing, ec);
            ++copied;
        }
    }


    // Crowd palette variants (docs/character-generator.md, "Crowds"): for each
    // "<base>.v<k>.png" the base is quantized again - the SAME pixels the loop
    // above wrote (model AO multiplied in, resized to a PS2 size) through the
    // SAME deterministic quantizer, so the indices are the shipped ones - and
    // the variant's palette is the variant image averaged over each index.
    // The game loads it as <base>.v<k>.pal (256 x RGBA, PNG order) and draws
    // the base's texels through it: a recoloured person for 1 KB of VRAM.
    {
        auto prepare = [&](const fs::path& src, const std::string& aoKey, int& w,
                           int& h) {
            std::vector<unsigned char> buf;
            int comp = 0;
            unsigned char* px = stbi_load(src.string().c_str(), &w, &h, &comp, 4);
            if (!px) return buf;
            buf.assign(px, px + (size_t)w * h * 4);
            stbi_image_free(px);
            if (auto aoIt = aoMaps.find(aoKey); aoIt != aoMaps.end())
                modelao::applyMapFile(aoIt->second, buf.data(), w, h,
                                      aoParams.strength);
            const int tw = nearestValidDim(w), th = nearestValidDim(h);
            if (tw != w || th != h) {
                buf = pngquant::resizeRGBA(buf.data(), w, h, tw, th);
                w = tw, h = th;
            }
            return buf;
        };
        int fitted = 0;
        for (const auto& e : fs::recursive_directory_iterator(res, ec)) {
            fs::path basePath;
            const int k = e.is_regular_file() ? paletteVariantOf(e.path(), &basePath) : 0;
            if (k == 0) continue;
            const fs::path rel = fs::relative(basePath, res, ec);
            const std::string relRes = "res/" + rel.generic_string();
            std::string q = defaultQ;
            if (auto it = quality.find(relRes); it != quality.end()) q = it->second;
            const int colors = colorsOf(q);
            const fs::path palDst =
                baked / rel.parent_path() /
                (rel.stem().string() + ".v" + std::to_string(k) + ".pal");
            if (colors != 256 && colors != 16) {
                log("[editor] crowd palette: " + relRes +
                    " is not palettized (Texture depth) - variant " +
                    std::to_string(k) + " skipped");
                fs::remove(palDst, ec);
                continue;
            }
            int bw = 0, bh = 0, vw = 0, vh = 0;
            const std::vector<unsigned char> base = prepare(basePath, relRes, bw, bh);
            const std::vector<unsigned char> var = prepare(e.path(), relRes, vw, vh);
            if (base.empty() || var.empty() || bw != vw || bh != vh) {
                log("[editor] crowd palette: " + e.path().filename().string() +
                    " does not match its base - skipped");
                continue;
            }
            std::vector<unsigned char> idx, pal;
            std::string err;
            if (!pngquant::quantizeIndices(base.data(), bw, bh, colors, idx, pal, err)) {
                log("[editor] crowd palette: " + relRes + ": " + err);
                continue;
            }
            const size_t entries = pal.size() / 4;
            std::vector<double> sum(entries * 3, 0.0);
            std::vector<int> count(entries, 0);
            for (size_t i = 0; i < idx.size(); ++i) {
                const size_t c = idx[i];
                for (int ch = 0; ch < 3; ++ch) sum[c * 3 + ch] += var[i * 4 + ch];
                ++count[c];
            }
            std::vector<unsigned char> out(entries * 4);
            for (size_t c = 0; c < entries; ++c) {
                for (int ch = 0; ch < 3; ++ch)
                    out[c * 4 + ch] = count[c] ? (unsigned char)std::lround(
                                                     sum[c * 3 + ch] / count[c])
                                               : pal[c * 4 + ch];
                out[c * 4 + 3] = pal[c * 4 + 3];  // cut-outs keep the base's
            }
            fs::create_directories(palDst.parent_path(), ec);
            std::ofstream f(palDst, std::ios::binary | std::ios::trunc);
            f.write((const char*)out.data(), (std::streamsize)out.size());
            ++fitted;
        }
        if (fitted)
            log("[editor] crowd palettes: " + std::to_string(fitted) + " fitted");
    }

    // drop baked files whose source vanished (they would still reach bin/) -
    // and editor-only files a pre-exclusion bake may have mirrored
    std::vector<fs::path> stale;
    for (const auto& e : fs::recursive_directory_iterator(baked, ec)) {
        if (!e.is_regular_file()) continue;
        const fs::path rel = fs::relative(e.path(), baked, ec);
        // stoch/ holds generated supertiles with no res/ source - regenerated
        // wholesale below, so leave them out of the vanished-source sweep.
        // aomap/ + aoatlas/ (textured AO) are regenerated wholesale too, and
        // gi/ holds the global-illumination bake cache - written by an
        // explicit bake, never by a build, so a build must not sweep it away.
        // modelao/ is the model-AO cache: content-hashed maps with no res/
        // source, kept across builds precisely so a build that changed nothing
        // does not re-raytrace them.
        // vehicles/ is the vehicle import bake (docs/vehicles.md): a body and
        // wheel .tmdl plus a colour palette, produced from a .glb/.fbx by the
        // Vehicle Editor and having no res/ source of their own. Sweeping them
        // deletes the geometry the game loads, with nothing to say so.
        // shadow/ is the baked-shadow cache - an explicit bake like gi/, so a
        // build must not sweep it - and shadowatlas/ its pages, regenerated
        // wholesale from that cache below.
        const std::string top0 = rel.begin()->generic_string();
        if (top0 == "stoch" || top0 == "aomap" || top0 == "aoatlas" ||
            top0 == "gi" || top0 == "modelao" || top0 == "vehicles" ||
            top0 == "shadow" || top0 == "shadowatlas" || top0 == "sky" ||
            top0 == "gshadow")
            continue;
        // atlas pages have no res/ source; the atlas block below removes the
        // ones the current plan no longer produces
        if (rel.filename().string().rfind("tyra-atlas-", 0) == 0) continue;
        std::error_code sec;
        // crowd palettes: their source is "<base>.v<k>.png", not a .pal
        if (lowerExt(e.path()) == ".pal") {
            fs::path pngRel = rel;
            pngRel.replace_extension(".png");
            if (!fs::exists(res / pngRel, sec)) stale.push_back(e.path());
            continue;
        }
        // "<model>.aov" AO sidecars: model self-AO is disabled for now (the
        // per-vertex bake reads as triangulated shading on authored meshes -
        // see aobake::modelAO), so any previously baked sidecar is stale.
        if (lowerExt(e.path()) == ".aov") {
            stale.push_back(e.path());
            continue;
        }
        if (!fs::exists(res / rel, sec) || editorOnly(rel))
            stale.push_back(e.path());
    }
    for (const fs::path& s : stale) fs::remove(s, ec);

    // Model self-AO sidecars: DISABLED for now (owner call, 2026-07) - the
    // per-vertex bake reads as triangulated shading on authored low-poly
    // meshes; a proper fix needs a per-model lightmap unwrap. The full
    // pipeline stays in place for that future path: aobake::modelAO +
    // writeModelAoSidecar bake "<model>.aov" per .obj under res/models, and
    // the engine's LeanObjLoader quietly folds an existing sidecar into
    // per-vertex visibility bytes. To re-enable, restore the loop that was
    // here (git log this file) and drop the unconditional .aov sweep above.

    // Texture atlas pages (docs/texture-atlasing.md): composite the plan's
    // members into shared pages, regenerated wholesale each bake. Members
    // are blitted at their baked size with a 2-texel edge-dilated gutter
    // (bilinear filtering never reaches a neighbor), then the whole page
    // quantizes as ONE image - a shared 256-color CLUT per page (the
    // era-authentic trade), or full color when the project ships full color.
    {
        // pages the current plan no longer produces
        for (const auto& e : fs::recursive_directory_iterator(baked, ec)) {
            if (!e.is_regular_file()) continue;
            if (e.path().filename().string().rfind("tyra-atlas-", 0) != 0)
                continue;
            const std::string rel =
                "res/" + fs::relative(e.path(), baked, ec).generic_string();
            bool wanted = false;
            for (const std::string& pg : atlasPlan.pages) wanted |= pg == rel;
            if (!wanted) fs::remove(e.path(), ec);
        }
        const int S = atlasPlan.pageSize;
        for (size_t pi = 0; pi < atlasPlan.pages.size(); ++pi) {
            std::vector<unsigned char> page((size_t)S * S * 4, 0);
            for (size_t i = 3; i < page.size(); i += 4) page[i] = 255;
            for (const texatlas::Entry& en : atlasPlan.entries) {
                if (en.page != (int)pi) continue;
                int sw = 0, sh = 0, comp = 0;
                unsigned char* px = stbi_load(
                    (fs::path(p.dir) / en.resRel).string().c_str(), &sw, &sh,
                    &comp, 4);
                if (!px) {
                    log("[editor] texture atlas: cannot decode " + en.resRel);
                    continue;
                }
                // An atlas member is composited from its res/ source, so it
                // never passes the mirror loop's AO branch above - the multiply
                // has to happen here too or atlasing silently deletes the
                // model's self-occlusion.
                std::vector<unsigned char> aoBuf;
                if (auto aoIt = aoMaps.find(en.resRel); aoIt != aoMaps.end()) {
                    aoBuf.assign(px, px + (size_t)sw * sh * 4);
                    if (modelao::applyMapFile(aoIt->second, aoBuf.data(), sw, sh,
                                              aoParams.strength)) {
                        log("[editor] model AO: multiplied into " + en.resRel +
                            " (atlas page)");
                        stbi_image_free(px);
                        px = nullptr;
                    } else {
                        aoBuf.clear();
                    }
                }
                std::vector<unsigned char> buf;
                const unsigned char* pix = aoBuf.empty() ? px : aoBuf.data();
                if (sw != en.w || sh != en.h) {
                    buf = pngquant::resizeRGBA(pix, sw, sh, en.w, en.h);
                    pix = buf.data();
                }
                for (int y = 0; y < en.h; ++y)
                    std::memcpy(&page[(((size_t)en.y + y) * S + en.x) * 4],
                                pix + (size_t)y * en.w * 4, (size_t)en.w * 4);
                stbi_image_free(px);
                // gutter: replicate the member's edges 2 texels outward
                // (rows first, then columns over the expanded rows so the
                // corners fill too)
                constexpr int G = 2;
                for (int gy = 1; gy <= G; ++gy) {
                    std::memcpy(&page[(((size_t)en.y - gy) * S + en.x) * 4],
                                &page[((size_t)en.y * S + en.x) * 4],
                                (size_t)en.w * 4);
                    std::memcpy(
                        &page[(((size_t)en.y + en.h - 1 + gy) * S + en.x) * 4],
                        &page[(((size_t)en.y + en.h - 1) * S + en.x) * 4],
                        (size_t)en.w * 4);
                }
                for (int y = -G; y < en.h + G; ++y)
                    for (int gx = 1; gx <= G; ++gx) {
                        const size_t row = ((size_t)en.y + y) * S;
                        std::memcpy(&page[(row + en.x - gx) * 4],
                                    &page[(row + en.x) * 4], 4);
                        std::memcpy(&page[(row + en.x + en.w - 1 + gx) * 4],
                                    &page[(row + en.x + en.w - 1) * 4], 4);
                    }
            }
            const fs::path dst =
                baked / fs::path(atlasPlan.pages[pi].substr(4));
            fs::create_directories(dst.parent_path(), ec);
            std::string err;
            // A page is quantized AS ONE IMAGE, at the depth its group asked
            // for (docs/texture-atlasing.md): 4 bits is half the VRAM of 8 and
            // is what makes atlasing pay in a 4-bit project, at the price of
            // one 16-colour palette for everything on the page.
            const int bits = atlasPlan.bitsOf((int)pi);
            const bool ok =
                bits == 32
                    ? pngquant::writePngRGBA(dst.string(), page.data(), S, S,
                                             err)
                    : pngquant::quantizeRGBA(dst.string(), page.data(), S, S,
                                             bits == 4 ? 16 : 256, err);
            if (!ok)
                log("[editor] texture atlas: " + atlasPlan.pages[pi] + ": " +
                    err);
        }
        if (!atlasPlan.empty()) log("[editor] " + texatlas::info(atlasPlan));
    }

    // The scene lightmaps (docs/ambient-occlusion.md, emissive-materials.md):
    // the terrain map + the primitive atlas, one pair per scene that has
    // baked occlusion or baked emissive light (both images carry BOTH:
    // alpha = occlusion, RGB = light), regenerated wholesale like the
    // stochastic supertiles. Codegen emits the matching atlas rects from the
    // SAME deterministic bake (aobake::bakeSceneLightAtlas), so pixels and
    // UVs cannot drift.
    fs::remove_all(baked / "aomap", ec);
    fs::remove_all(baked / "aoatlas", ec);
    {
        const aobake::ModelAabbFn aabbFn = [&](const SceneObject& o, float* mn,
                                               float* mx) {
            if (o.modelPath.empty()) return false;
            return aobake::objAabb((fs::path(p.dir) / o.modelPath).string(), mn,
                                   mx);
        };
        // rgb (optional, size*size*3) is the baked emissive light the additive
        // atlas pass adds; alpha is the occlusion the alpha-over pass
        // multiplies. One image carries both - see aobake::SceneLightAtlas.
        auto writeAlphaPng = [&](const fs::path& dst, int size,
                                 const std::vector<uint8_t>& alpha,
                                 const std::vector<uint8_t>& rgb = {}) {
            std::vector<unsigned char> rgba((size_t)size * size * 4, 0);
            for (size_t i = 0; i < alpha.size(); ++i) rgba[i * 4 + 3] = alpha[i];
            for (size_t i = 0; i * 3 + 2 < rgb.size(); ++i)
                for (int c = 0; c < 3; ++c) rgba[i * 4 + c] = rgb[i * 3 + c];
            fs::create_directories(dst.parent_path(), ec);
            std::string err;
            // Full RGBA32 on purpose: the engine's palettized (tRNS -> CLUT)
            // path loses the smooth alpha gradient these maps are made of
            // (verified in PCSX2 - the quantized bake rendered as nothing).
            // aobake caps both images at 256x256 to keep the VRAM cost sane.
            if (!pngquant::writePngRGBA(dst.string(), rgba.data(), size, size,
                                        err)) {
                log("[editor] textured AO: " + dst.filename().string() + ": " +
                    err);
                return false;
            }
            return true;
        };
        int aoTexCount = 0;
        for (size_t si = 0; si < p.scenes.size(); ++si) {
            const SceneData& sc = p.scenes[si];
            const ProjectSettings srs = project::resolvedSettings(p, sc);
            // Baked global illumination, when the scene has a fresh one
            // (docs/global-illumination.md). Pixels and rects must come from
            // ONE bake or the UVs point at the wrong texels - codegen reads
            // the same cache, so a stale one falls both sides back together.
            const gibake::Bake gi = gibake::load(p, (int)si);
            // Neither image is gated on the AO preference: both carry baked
            // light as well, so a scene can have glowing lamps and no ambient
            // occlusion at all.
            {
                // A scene with the terrain removed ships no ground lightmap -
                // nothing draws the ground pass that would sample it
                // (docs/terrain.md). Codegen makes the same call.
                const aobake::AoImage map =
                    !sc.terrain.enabled ? aobake::AoImage()
                    : gi.valid          ? gi.terrain
                             : aobake::terrainAOMap(
                                   sc.heights, sc.hmW, sc.hmD,
                                   (float)sc.terrain.width,
                                   (float)sc.terrain.depth,
                                   aobake::collectOccluders(sc.objects, aabbFn),
                                   aobake::collectEmitters(p.dir, sc.objects,
                                                           aabbFn),
                                   srs.aoRadius, srs.aoStrength, srs.aoEnabled);
                if (map.size > 0 &&
                    writeAlphaPng(
                        baked / "aomap" / ("scene" + std::to_string(si) + ".png"),
                        map.size, map.alpha, map.light))
                    ++aoTexCount;
            }
            const aobake::SceneLightAtlas atlas =
                gi.valid ? gi.atlas
                         : aobake::bakeSceneLightAtlas(p, sc, aabbFn);
            if (atlas.size > 0 &&
                writeAlphaPng(
                    baked / "aoatlas" / ("scene" + std::to_string(si) + ".png"),
                    atlas.size, atlas.alpha, atlas.light))
                ++aoTexCount;
        }
        if (aoTexCount)
            log("[editor] Ambient occlusion: baked " + std::to_string(aoTexCount) +
                " AO texture(s)");
    }

    // The painted skies (docs/sky-texture.md): one crop per scene whose
    // resolved sky names a panorama, always 8-bit whatever the project's
    // texture default - a 16-colour sky bands into stripes. Regenerated
    // wholesale like the lightmaps; codegen asks skySceneBaked() the same
    // question, so a panorama that cannot be read drops both sides together.
    fs::remove_all(baked / "sky", ec);
    for (size_t si = 0; si < p.scenes.size(); ++si) {
        const ProjectSettings srs = project::resolvedSettings(p, p.scenes[si]);
        if (!srs.skyDome || srs.skyTexture.empty()) continue;
        std::string err;
        const std::vector<unsigned char> px =
            skytex::crop(p.filePath(srs.skyTexture), err);
        const fs::path dst = baked / "sky" / ("scene" + std::to_string(si) + ".png");
        fs::create_directories(dst.parent_path(), ec);
        if (px.empty() || !pngquant::quantizeRGBA(dst.string(), px.data(),
                                                  skytex::kWidth, skytex::kHeight,
                                                  256, err))
            log("[editor] sky texture: " + srs.skyTexture + ": " + err);
    }

    // Baked shadow decals (docs/shadows.md): the atlas pages, written from the
    // SAME cached bake codegen reads its meshes and UVs out of - one bake, so
    // the pixels and the texture coordinates cannot point at different cells.
    // Regenerated wholesale like the lightmaps above, so a caster switched off
    // leaves nothing behind.
    fs::remove_all(baked / "shadowatlas", ec);
    fs::remove_all(baked / "gshadow", ec);
    if (p.settings.bakedShadows) {
        int shadowPages = 0, groundMaps = 0;
        for (size_t si = 0; si < p.scenes.size(); ++si) {
            const shadowbake::Bake sb = shadowbake::load(p, (int)si);
            if (!sb.valid) continue;
            // Every shadow image is ONE colour - the bake's tint, what a fully
            // shadowed texel blends toward - at sixteen alpha levels: 4 bits a
            // texel. The ramp is written by hand, never through the colour
            // quantizer, which merges alpha levels (that, not the engine, is
            // why these used to be RGBA32: the engine's loader keeps tRNS alpha
            // per CLUT entry). A 256x256 atlas page is 32 KB of GS VRAM instead
            // of 256 KB. A 4x4 ordered dither spreads the step between two
            // levels over the texels instead of banding.
            static const int kBayer[16] = {0, 8, 2, 10, 12, 4, 14, 6,
                                           3, 11, 1, 9, 15, 7, 13, 5};
            unsigned char pal[64];
            for (int k = 0; k < 16; ++k) {
                pal[k * 4 + 0] = sb.tint[0];
                pal[k * 4 + 1] = sb.tint[1];
                pal[k * 4 + 2] = sb.tint[2];
                pal[k * 4 + 3] = (unsigned char)(k * 17);
            }
            const auto toRamp = [&](const std::vector<uint8_t>& alpha, int size) {
                std::vector<unsigned char> idx((size_t)size * size, 0);
                for (int y = 0; y < size; ++y)
                    for (int x = 0; x < size; ++x) {
                        const uint8_t a = alpha[(size_t)y * size + x];
                        if (!a) continue;  // fully lit stays exactly 0: the GS
                                           // alpha test drops it, no blend paid
                        const float v = a / 17.0f +
                                        (kBayer[(y & 3) * 4 + (x & 3)] + 0.5f) / 16.0f -
                                        0.5f;
                        int k = (int)(v + 0.5f);
                        idx[(size_t)y * size + x] =
                            (unsigned char)(k < 1 ? 1 : (k > 15 ? 15 : k));
                    }
                return idx;
            };
            for (size_t pi = 0; pi < sb.pages.size(); ++pi) {
                const int size = shadowbake::kPageSize;
                const std::vector<unsigned char> idx = toRamp(sb.pages[pi].alpha, size);
                const fs::path dst =
                    baked / "shadowatlas" /
                    ("scene" + std::to_string(si) + "-" + std::to_string(pi) + ".png");
                fs::create_directories(dst.parent_path(), ec);
                std::string err;
                if (pngquant::writeIndexed4(dst.string(), idx.data(), size, size, pal,
                                            err))
                    ++shadowPages;
                else
                    log("[editor] baked shadows: " + dst.filename().string() +
                        ": " + err);
            }
            // Ground shadow maps (docs/shadows.md, "Ground shadow maps"): the
            // same ramp, one map per terrain chunk - a 128^2 map is 8 KB.
            const int gres = sb.groundRes;
            for (const shadowbake::GroundMap& gm : sb.ground) {
                const std::vector<unsigned char> idx = toRamp(gm.alpha, gres);
                const fs::path dst = baked / "gshadow" /
                                     ("s" + std::to_string(si) + "_" +
                                      std::to_string(gm.cx) + "_" +
                                      std::to_string(gm.cz) + ".png");
                fs::create_directories(dst.parent_path(), ec);
                std::string err;
                if (pngquant::writeIndexed4(dst.string(), idx.data(), gres, gres, pal,
                                            err))
                    ++groundMaps;
                else
                    log("[editor] ground shadows: " + dst.filename().string() +
                        ": " + err);
            }
        }
        if (shadowPages)
            log("[editor] Baked shadows: " + std::to_string(shadowPages) +
                " atlas page(s)");
        if (groundMaps)
            log("[editor] Ground shadows: " + std::to_string(groundMaps) +
                " chunk map(s)");
    }

    // Stochastic-tiling supertiles (docs/terrain-painting.md): one
    // non-repeating supertile per stochastic terrain texture, generated into
    // .res-baked/stoch (never mirrored from res/) and quantized like its
    // source. Regenerated wholesale so un-toggled layers leave nothing behind.
    fs::remove_all(baked / "stoch", ec);
    {
        std::set<std::string> done;  // a texture shared by layers bakes once
        int stochCount = 0;
        auto genStoch = [&](const std::string& srcRel) {
            if (srcRel.empty() || !done.insert(srcRel).second) return;
            int w = 0, h = 0, factor = 1;
            std::vector<unsigned char> px = stochtile::generate(
                (res.parent_path() / srcRel).string(), srcRel, w, h, factor);
            if (px.empty()) {
                log("[editor] stochastic tiling: cannot read " + srcRel);
                return;
            }
            const fs::path dst = baked / stochtile::bakedBinPath(srcRel);
            fs::create_directories(dst.parent_path(), ec);
            std::string q = defaultQ;
            if (auto it = quality.find(srcRel); it != quality.end()) q = it->second;
            const int cols = colorsOf(q);
            std::string err;
            const bool ok =
                cols > 0
                    ? pngquant::quantizeRGBA(dst.string(), px.data(), w, h, cols, err)
                    : pngquant::writePngRGBA(dst.string(), px.data(), w, h, err);
            if (ok)
                ++stochCount;
            else
                log("[editor] stochastic tiling: " + srcRel + ": " + err);
        };
        for (const SceneData& sc : p.scenes) {
            if (!sc.terrain.enabled) continue;  // no ground, no ground textures
            if (sc.terrainBaseStochastic)
                genStoch(project::resolveTerrainMaterial(
                             p, project::resolvedSettings(p, sc).terrainMaterial)
                             .texture);
            for (const TerrainLayer& l : sc.terrainLayers)
                if (l.stochastic)
                    genStoch(project::resolveTerrainMaterial(p, l.material).texture);
        }
        if (stochCount)
            log("[editor] Stochastic tiling: baked " + std::to_string(stochCount) +
                " supertile(s)");
    }

    if (quantized || !stale.empty())
        log("[editor] Texture bake: " + std::to_string(quantized) +
            " quantized, " + std::to_string(copied) + " copied, " +
            std::to_string(stale.size()) + " stale removed (res -> .res-baked)");
    return "";
}

}  // namespace texbake
