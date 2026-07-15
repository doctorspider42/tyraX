#include "texbake.hpp"

#include <filesystem>
#include <map>
#include <vector>

#include <stb_image.h>

#include "menubake.hpp"  // atlasFileName - which res/fonts PNGs are atlases
#include "objparser.hpp"
#include "pngquant.hpp"

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
            if (s.texture.empty()) continue;
            std::vector<std::string> rel;
            texturesOf(e.path().parent_path(), s.texture, &rel);
            for (const std::string& r : rel) claim(r, q);
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
                if (m.texture.empty()) continue;
                std::vector<std::string> rel;
                texturesOf(e.path().parent_path(), m.texture, &rel);
                for (const std::string& r : rel) claim(r, q);
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

    // Font atlases (res/fonts/atlas-<name>.png, baked by refreshGenerated for
    // the fonts a Display Text node uses): quantized per Font Manager entry
    // rather than by the project default, because an atlas is white glyphs the
    // runtime tints - 16 colors usually costs nothing visually and saves ~8x
    // the VRAM, which matters on a ~1.33 MB texture budget.
    std::map<std::string, std::string> fontQuant;
    for (const GameFont& gf : p.fonts)
        fontQuant["res/fonts/" + menubake::atlasFileName(gf.name)] = gf.quant;

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
        const std::string top = rel.begin()->generic_string();
        if (top == "fonts") {
            const std::string ext = lowerExt(rel);
            return ext == ".ttf" || ext == ".otf";
        }
        return top == "brushes";
    };
    const std::string defaultQ = p.settings.textureQuant;  // none/8bit/4bit
    int quantized = 0, copied = 0;
    for (const auto& e : fs::recursive_directory_iterator(res, ec)) {
        if (!e.is_regular_file()) continue;
        const fs::path rel = fs::relative(e.path(), res, ec);
        if (editorOnly(rel)) continue;
        const fs::path dst = baked / rel;
        fs::create_directories(dst.parent_path(), ec);

        const std::string relRes = ("res/" + rel.generic_string());
        const std::string top = rel.begin()->generic_string();

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
        const int colors = quantizable ? colorsOf(q) : 0;

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

    // drop baked files whose source vanished (they would still reach bin/) -
    // and editor-only files a pre-exclusion bake may have mirrored
    std::vector<fs::path> stale;
    for (const auto& e : fs::recursive_directory_iterator(baked, ec)) {
        if (!e.is_regular_file()) continue;
        const fs::path rel = fs::relative(e.path(), baked, ec);
        std::error_code sec;
        if (!fs::exists(res / rel, sec) || editorOnly(rel))
            stale.push_back(e.path());
    }
    for (const fs::path& s : stale) fs::remove(s, ec);

    if (quantized || !stale.empty())
        log("[editor] Texture bake: " + std::to_string(quantized) +
            " quantized, " + std::to_string(copied) + " copied, " +
            std::to_string(stale.size()) + " stale removed (res -> .res-baked)");
    return "";
}

}  // namespace texbake
