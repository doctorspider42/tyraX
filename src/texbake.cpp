#include "texbake.hpp"

#include <filesystem>
#include <map>
#include <vector>

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

    // --- mirror res/ into .res-baked/ --------------------------------------
    const std::string defaultQ = p.settings.textureQuant;  // none/8bit/4bit
    int quantized = 0, copied = 0;
    for (const auto& e : fs::recursive_directory_iterator(res, ec)) {
        if (!e.is_regular_file()) continue;
        const fs::path rel = fs::relative(e.path(), res, ec);
        const fs::path dst = baked / rel;
        fs::create_directories(dst.parent_path(), ec);

        const std::string relRes = ("res/" + rel.generic_string());
        const std::string top = rel.begin()->generic_string();
        const bool quantizable =
            lowerExt(e.path()) == ".png" &&
            (top == "models" || top == "materials" || top == "textures");

        std::string q = defaultQ;
        if (auto it = quality.find(relRes); it != quality.end()) q = it->second;
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

    // drop baked files whose source vanished (they would still reach bin/)
    std::vector<fs::path> stale;
    for (const auto& e : fs::recursive_directory_iterator(baked, ec)) {
        if (!e.is_regular_file()) continue;
        const fs::path rel = fs::relative(e.path(), baked, ec);
        std::error_code sec;
        if (!fs::exists(res / rel, sec)) stale.push_back(e.path());
    }
    for (const fs::path& s : stale) fs::remove(s, ec);

    if (quantized || !stale.empty())
        log("[editor] Texture bake: " + std::to_string(quantized) +
            " quantized, " + std::to_string(copied) + " copied, " +
            std::to_string(stale.size()) + " stale removed (res -> .res-baked)");
    return "";
}

}  // namespace texbake
