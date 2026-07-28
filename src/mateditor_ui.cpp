// -------------------------------------------------------------------------
// The Material Editor: .mtl load/save, the paint-layer stack and its undo,
// the raytraced map bake (docs/material-baking.md) and the UV validator.
//
// Split out of app.cpp so the editor builds in parallel: it was one 26k-line
// translation unit and therefore the whole build's critical path. These are
// still App:: members declared in app.hpp - the assetbrowser.cpp precedent.
// -------------------------------------------------------------------------
#include "app.hpp"
#include "app_internal.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

#include "aisupport.hpp"
#include "animedit.hpp"
#include "decalproj.hpp"
#include "devsession.hpp"
#include "editorcfg.hpp"
#include "gl_loader.h"
#include "fbxparser.hpp"
#include "glbparser.hpp"
#include "json.hpp"
#include "menubake.hpp"
#include "objparser.hpp"
#include "pngquant.hpp"
#include "uvunwrap.hpp"
#include "stochtile.hpp"
#include "templates.hpp"
#include "wavconvert.hpp"

#include <stb_image.h>
#include <stb_image_write.h>

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <ImGuizmo.h>
#include <imnodes.h>

#include "platform.hpp"

// --- Material Editor ---------------------------------------------------------
// Materials are the project's plain Wavefront .mtl asset files - the same
// files objects reference via materialPath and the PS2 runtime parses with
// LeanObjLoader. The editor reads/writes the subset the whole pipeline
// understands (newmtl / Kd / Ke / map_Kd / refl) plus "# tyra-brightness" and
// "# tyra-glow" hint lines so the color x strength splits survive a round trip
// (each pair multiplies into the written Kd / Ke; every parser ignores
// comments). Unrecognized lines of hand-imported files are preserved verbatim.
// Edits are saved straight to disk on commit - assets are not project data, so
// no undo (same as imports).

void App::matEdKe(const MatEdEntry& e, float out[3]) {
    for (int i = 0; i < 3; ++i) {
        const float v = e.glowColor[i] * e.glow + e.glowWhite;
        out[i] = v < 0.0f ? 0.0f : v > 1.99f ? 1.99f : v;
    }
}

bool App::loadMaterialFile(const std::string& relPath) {
    matEdMats_.clear();
    matEdSel_ = 0;
    matBakeResetParams();  // files without a hint get the defaults
    std::ifstream in(std::filesystem::path(project_.dir) / relPath);
    if (!in) return false;

    std::vector<char> gotHint;      // "# tyra-brightness" seen (Kd split)
    std::vector<char> gotKe;        // an "Ke" statement seen (emission)
    // "# tyra-glow" seen: 0 none, 1 strength only (legacy - split Ke),
    // 2 strength + authored color (+ optional white-hot; nothing to split)
    std::vector<char> gotGlowHint;
    // The raw "Ke" of each entry, kept OUT of the staged color so the hint
    // line and the statement cannot clobber each other whatever their order.
    std::vector<std::array<float, 3>> keRaw;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::istringstream ss(line);
        std::string tag;
        ss >> tag;
        if (tag == "#") {
            // "# tyra-bake" = the file's bake parameters (file scope, may sit
            // above the first newmtl - see saveMaterialFile for the format)
            std::istringstream hs(line);
            std::string hash, what;
            hs >> hash >> what;
            if (what == "tyra-bake") {
                int size = 256, rays = 64, ss2 = 2, bf = 1, pad = 4, seed = 1;
                float maxDist = 0.0f, cage = 0.0f;
                std::string high;
                hs >> size >> rays >> ss2 >> maxDist >> bf >> pad >> seed >>
                    cage >> high;
                matBakeSizeIdx_ = size >= 512 ? 3 : size >= 256 ? 2
                                  : size >= 128 ? 1 : 0;
                matBakeRays_ = std::max(4, std::min(rays, 1024));
                matBakeSSIdx_ = ss2 >= 4 ? 2 : ss2 >= 2 ? 1 : 0;
                matBakeMaxDist_ = maxDist < 0.0f ? 0.0f : maxDist;
                matBakeBackface_ = bf != 0;
                matBakePadding_ = std::max(0, std::min(pad, 16));
                matBakeSeed_ = seed;
                matBakeCage_ = cage < 0.0f ? 0.0f : cage;
                if (high == "-") high.clear();
                for (size_t i = 0;
                     (i = high.find("%20", i)) != std::string::npos;)
                    high.replace(i, 3, " "), ++i;
                matBakeHigh_ = high;
                continue;
            }
        }
        if (tag == "newmtl") {
            MatEdEntry e;
            ss >> e.name;
            matEdMats_.push_back(std::move(e));
            gotHint.push_back(0);
            gotKe.push_back(0);
            gotGlowHint.push_back(0);
            keRaw.push_back({0.0f, 0.0f, 0.0f});
            continue;
        }
        if (matEdMats_.empty()) continue;  // stray header lines
        MatEdEntry& e = matEdMats_.back();
        if (tag == "Kd") {
            ss >> e.color[0] >> e.color[1] >> e.color[2];
        } else if (tag == "Ke") {
            std::array<float, 3>& k = keRaw.back();
            ss >> k[0] >> k[1] >> k[2];
            gotKe.back() = 1;
        } else if (tag == "map_Kd") {
            std::vector<std::string> toks;  // "<options> filename"; filename last
            for (std::string t; ss >> t;) toks.push_back(t);
            if (!toks.empty()) {
                e.texture = toks.back();
                for (char& c : e.texture)
                    if (c == '\\') c = '/';
                // -s <u> [v] [w]: tiling (a UV multiplier); take the u factor.
                for (size_t i = 0; i + 1 < toks.size(); ++i)
                    if (toks[i] == "-s") {
                        std::istringstream(toks[i + 1]) >> e.tile;
                        break;
                    }
            }
        } else if (tag == "refl") {
            // Spherical environment map: refl -type sphere -mm 0 <strength>
            // <file>. Filename = last token; -mm's gain is the strength.
            std::vector<std::string> toks;
            for (std::string t; ss >> t;) toks.push_back(t);
            if (!toks.empty()) {
                e.refl = toks.back();
                for (char& c : e.refl)
                    if (c == '\\') c = '/';
                e.reflStrength = 0.0f;
                for (size_t i = 0; i + 2 < toks.size(); ++i)
                    if (toks[i] == "-mm") {
                        std::istringstream(toks[i + 2]) >> e.reflStrength;
                        break;
                    }
                for (size_t i = 0; i + 1 < toks.size(); ++i)
                    if (toks[i] == "-rounded") e.reflRounded = true;
                if (e.reflStrength <= 0.0f) e.reflStrength = 0.5f;
            }
        } else if (tag == "#") {
            std::string what;
            ss >> what;
            if (what == "tyra-brightness") {
                ss >> e.brightness;
                gotHint.back() = 1;
            } else if (what == "tyra-glow") {
                // "<strength> [r g b] [white]" - the authored controls behind
                // the resolved Ke. The 1-number form is the original layout
                // (color recovered by dividing Ke, no white-hot core).
                ss >> e.glow;
                float r, g, b;
                if (ss >> r >> g >> b) {
                    e.glowColor[0] = r, e.glowColor[1] = g, e.glowColor[2] = b;
                    gotGlowHint.back() = 2;  // color authored too - no split
                    ss >> e.glowWhite;       // absent = 0 (leaves the default)
                } else {
                    gotGlowHint.back() = 1;
                }
            } else if (what == "tyra-glow-light") {
                ss >> e.glowRange >> e.glowLight;
            }
            // other comments are dropped - saveMaterialFile rewrites its own
        } else if (!tag.empty()) {
            e.extra.push_back(line);
        }
    }
    if (matEdMats_.empty()) {  // readable but no materials - start a fresh one
        MatEdEntry e;
        e.name = std::filesystem::path(relPath).stem().string();
        matEdMats_.push_back(std::move(e));
        gotHint.push_back(1);
        gotKe.push_back(0);
        gotGlowHint.push_back(0);
        keRaw.push_back({0.0f, 0.0f, 0.0f});
    }

    // The file's Kd = color x brightness; split them back for the UI. Without
    // a hint the split is ambiguous - treat Kd as the color (brightness 1),
    // except components > 1 which can only come from brightness.
    for (size_t i = 0; i < matEdMats_.size(); ++i) {
        MatEdEntry& e = matEdMats_[i];
        float b = e.brightness;
        if (!gotHint[i]) {
            b = std::max(e.color[0], std::max(e.color[1], e.color[2]));
            if (b <= 1.0f) b = 1.0f;
        }
        b = b < 0.0f ? 0.0f : b > 2.0f ? 2.0f : b;
        for (float& c : e.color) {
            c = b > 0.01f ? c / b : 1.0f;
            c = c < 0.0f ? 0.0f : c > 1.0f ? 1.0f : c;
        }
        e.brightness = b;

        // Emission. The full hint (form 2) carries the authored controls, so
        // nothing has to be recovered from Ke. Otherwise the split is the same
        // ambiguity as Kd/brightness: the brightest Ke component is the
        // strength (so "Ke 1 1 1" reads as a full white glow), which renders
        // identically either way.
        if (!gotKe[i]) {
            e.glow = 0.0f;
            e.glowWhite = 0.0f;
            e.glowColor[0] = e.glowColor[1] = e.glowColor[2] = 1.0f;
            continue;
        }
        if (gotGlowHint[i] == 2) {
            e.glow = e.glow < 0.0f ? 0.0f : (e.glow > 2.0f ? 2.0f : e.glow);
            e.glowWhite = e.glowWhite < 0.0f ? 0.0f
                          : (e.glowWhite > 1.0f ? 1.0f : e.glowWhite);
            for (float& c : e.glowColor)
                c = c < 0.0f ? 0.0f : (c > 1.0f ? 1.0f : c);
            continue;
        }
        const std::array<float, 3>& k = keRaw[i];
        float g = gotGlowHint[i] == 1 ? e.glow
                                      : std::max(k[0], std::max(k[1], k[2]));
        g = g < 0.0f ? 0.0f : g > 2.0f ? 2.0f : g;
        for (int c = 0; c < 3; ++c) {
            float v = g > 0.01f ? k[c] / g : 1.0f;
            e.glowColor[c] = v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
        }
        e.glow = g;
        e.glowWhite = 0.0f;
    }
    matEdPrevMats_ = matEdMats_;  // undo baseline for the first edit
    return true;
}

void App::saveMaterialFile() {
    if (matEdPath_.empty()) return;
    const std::filesystem::path full = std::filesystem::path(project_.dir) / matEdPath_;
    std::error_code ec;
    std::filesystem::create_directories(full.parent_path(), ec);
    std::ofstream out(full, std::ios::trunc);
    if (!out) {
        statusMessage_ = "Cannot write " + matEdPath_;
        return;
    }
    char buf[128];
    // bake parameters (file scope) - written only when they left the
    // defaults, so untouched files stay clean. Spaces in the high-poly path
    // ride as %20 (the line is whitespace-tokenized on load).
    const bool bakeDefault =
        matBakeSizeIdx_ == 2 && matBakeRays_ == 64 && matBakeMaxDist_ == 0.0f &&
        matBakeSSIdx_ == 1 && matBakeBackface_ && matBakePadding_ == 4 &&
        matBakeSeed_ == 1 && matBakeHigh_.empty() && matBakeCage_ == 0.0f;
    if (!bakeDefault) {
        std::string high = matBakeHigh_.empty() ? "-" : matBakeHigh_;
        for (size_t i = 0; (i = high.find(' ', i)) != std::string::npos;)
            high.replace(i, 1, "%20"), i += 3;
        std::snprintf(buf, sizeof(buf), "# tyra-bake %d %d %d %.6g %d %d %d %.6g ",
                      64 << matBakeSizeIdx_, matBakeRays_, 1 << matBakeSSIdx_,
                      matBakeMaxDist_, matBakeBackface_ ? 1 : 0, matBakePadding_,
                      matBakeSeed_, matBakeCage_);
        out << buf << high << "\n\n";
    }
    for (const MatEdEntry& e : matEdMats_) {
        out << "newmtl " << e.name << "\n";
        std::snprintf(buf, sizeof(buf), "# tyra-brightness %.4g", e.brightness);
        out << buf << "\n";
        // Kd = color x brightness, capped at 1.99: the PS2 texture-modulate
        // color tops out at 255 where 128 = 1.0 (untextured draws clamp in
        // the generated pushVert)
        auto kd = [&](int i) {
            const float v = e.color[i] * e.brightness;
            return v < 0.0f ? 0.0f : v > 1.99f ? 1.99f : v;
        };
        std::snprintf(buf, sizeof(buf), "Kd %.4f %.4f %.4f", kd(0), kd(1), kd(2));
        out << buf << "\n";
        // Emission, written only when the material glows so untouched files
        // stay clean. The hint carries the authored controls, "Ke" the
        // resolved emission every renderer reads (matEdKe caps it at the 1.99
        // the PS2 color byte can carry, where 128 = 1.0 modulating a texture).
        if (e.glow > 0.0f || e.glowWhite > 0.0f) {
            std::snprintf(buf, sizeof(buf), "# tyra-glow %.4g %.4g %.4g %.4g %.4g",
                          e.glow, e.glowColor[0], e.glowColor[1], e.glowColor[2],
                          e.glowWhite);
            out << buf << "\n";
            if (e.glowRange > 0.0f) {
                std::snprintf(buf, sizeof(buf), "# tyra-glow-light %.4g %.4g",
                              e.glowRange, e.glowLight);
                out << buf << "\n";
            }
            float ke[3];
            matEdKe(e, ke);
            std::snprintf(buf, sizeof(buf), "Ke %.4f %.4f %.4f", ke[0], ke[1],
                          ke[2]);
            out << buf << "\n";
        }
        if (!e.texture.empty()) {
            // -s tiling (repeats per world unit) matters only for terrain; skip
            // it at the default 1 to keep files clean. Wavefront: "-s u v w".
            out << "map_Kd";
            if (e.tile != 1.0f) {
                std::snprintf(buf, sizeof(buf), " -s %.4g %.4g 1", e.tile, e.tile);
                out << buf;
            }
            out << " " << e.texture << "\n";
        }
        if (!e.refl.empty()) {
            // Spherical environment map; the standard -mm option's gain
            // operand carries the reflection strength (see the PS2 loader);
            // the TyraX -rounded flag rides before the filename so parsers
            // that take the last token as the file stay compatible.
            std::snprintf(buf, sizeof(buf), "refl -type sphere -mm 0 %.4g ",
                          e.reflStrength);
            out << buf;
            if (e.reflRounded) out << "-rounded ";
            out << e.refl << "\n";
        }
        for (const std::string& x : e.extra) out << x << "\n";
        out << "\n";
    }
    out.close();
    // every consumer caches the parsed file - drop them so the scene viewport
    // and the properties panel pick the change up next frame
    viewport_.invalidateAssets();
    modelInfoCache_.clear();
    statusMessage_ = "Saved " + matEdPath_;
}

void App::openMaterialEditor(const std::string& relPath,
                             const std::string& modelHint) {
    showMaterialEditor_ = true;
    // preview straight on the mesh the material is used by - static .obj or
    // animated .glb/.fbx (both take the assigned .mtl as an override)
    if (!modelHint.empty()) {
        matEdShape_ = 4;
        matEdModel_ = modelHint;
    }
    if (relPath.empty() || relPath == matEdPath_) return;  // keep staged edits
    if (loadMaterialFile(relPath)) {
        matEdPath_ = relPath;
        // different file - drop the paint session and its undo (the steps
        // reference the previous file's entries/textures)
        matEdPaint_ = false;
        matEdStroke_ = false;
        matEdPaintTexRel_.clear();
        matEdLayers_.clear();
        matEdUndo_.clear();
        // bake results belong to the previous file (params reloaded above)
        matBaker_.cancel();
        matBakeMaps_ = matbake::Maps{};
        matBakeStartedSig_ = 0;
        matBakeApplyWhenDone_ = false;
        // No hint (the file was clicked in the asset list): find the model
        // this material belongs to, static or animated. Try, in order:
        // a scene object assigned this material (the ground truth), a
        // sibling model with the same stem (a model's own library), and
        // the extraction convention res/materials/<model>.mtl ->
        // res/models/<model>.* (the "+ New material from this model" path).
        if (modelHint.empty()) {
            std::string found;
            for (const SceneData& sc : project_.scenes) {
                for (const SceneObject& o : sc.objects)
                    if (o.type == PrimitiveType::Model &&
                        o.materialPath == relPath && !o.modelPath.empty()) {
                        found = o.modelPath;
                        break;
                    }
                if (!found.empty()) break;
            }
            if (found.empty()) {
                std::error_code ec;
                static const char* exts[] = {".obj", ".glb", ".fbx"};
                const std::filesystem::path stem =
                    std::filesystem::path(relPath).parent_path() /
                    std::filesystem::path(relPath).stem();
                for (const char* ext : exts) {
                    std::filesystem::path cand = stem;
                    cand += ext;  // sibling in the .mtl's own directory
                    if (std::filesystem::exists(
                            std::filesystem::path(project_.dir) / cand, ec)) {
                        found = cand.generic_string();
                        break;
                    }
                    cand = std::filesystem::path("res/models") /
                           std::filesystem::path(relPath).stem();
                    cand += ext;  // the extracted-material convention
                    if (std::filesystem::exists(
                            std::filesystem::path(project_.dir) / cand, ec)) {
                        found = cand.generic_string();
                        break;
                    }
                }
            }
            if (!found.empty()) {
                matEdShape_ = 4;
                matEdModel_ = found;
            }
        }
    } else {
        matEdPath_.clear();
        statusMessage_ = "Cannot read " + relPath;
    }
}

// Creates res/materials/<model>.mtl seeded from a model's OWN built-in
// materials, so an animated .glb/.fbx (or a static .obj) becomes editable in
// the Material Editor without hand-authoring an override. Each part becomes a
// newmtl entry keyed by the part's material NAME (the key the override matches
// on), with its base color as Kd and its texture extracted next to the .mtl
// (embedded PNG bytes for .glb/.fbx, the referenced file for .obj). Assigns
// the new file to the object and opens the editor previewing on the model.
std::string App::createMaterialForModel(SceneObject& o) {
    namespace fs = std::filesystem;
    if (o.modelPath.empty()) return "";

    // (name, Kd, optional texture bytes) collected from the model's built-ins.
    struct Ent {
        std::string name;
        float kd[3] = {1.0f, 1.0f, 1.0f};
        std::vector<unsigned char> tex;
        std::string ext;  // ".png" for embedded; source ext for .obj
    };
    std::vector<Ent> ents;
    std::set<std::string> seen;
    bool skippedUnnamed = false;  // parts the override can't name-match

    if (isAnimatedModelPath(o.modelPath)) {
        glbparser::Baked baked;
        std::string err;
        if (!animimport::bake((fs::path(project_.dir) / o.modelPath).string(),
                              12.0f, baked, err)) {
            statusMessage_ = "Cannot read " + o.modelPath;
            return "";
        }
        for (const glbparser::Part& p : baked.parts) {
            // an empty material name can't be written as newmtl (nor matched by
            // the override), so it stays on the model's built-in look
            if (p.material.empty()) { skippedUnnamed = true; continue; }
            if (!seen.insert(p.material).second) continue;
            Ent e;
            e.name = p.material;
            e.kd[0] = p.baseColor[0], e.kd[1] = p.baseColor[1], e.kd[2] = p.baseColor[2];
            if (p.image >= 0 && p.image < (int)baked.images.size()) {
                e.tex = baked.images[p.image].png;
                e.ext = ".png";
            }
            ents.push_back(std::move(e));
        }
    } else {
        objparser::Model m;
        if (!objparser::load((fs::path(project_.dir) / o.modelPath).string(), m)) {
            statusMessage_ = "Cannot read " + o.modelPath;
            return "";
        }
        const fs::path modelDir = fs::path(o.modelPath).parent_path();
        for (const objparser::Submesh& s : m.submeshes) {
            // like the animated branch: an unnamed submesh can't be matched by
            // a name-keyed override, so skip it (report it instead)
            if (s.material.empty()) { skippedUnnamed = true; continue; }
            if (!seen.insert(s.material).second) continue;
            Ent e;
            e.name = s.material;
            e.kd[0] = s.kd[0], e.kd[1] = s.kd[1], e.kd[2] = s.kd[2];
            if (!s.texture.empty()) {
                std::ifstream f(
                    (fs::path(project_.dir) / modelDir / s.texture).string(),
                    std::ios::binary);
                if (f) {
                    e.tex.assign(std::istreambuf_iterator<char>(f),
                                 std::istreambuf_iterator<char>());
                    e.ext = fs::path(s.texture).extension().string();
                    if (e.ext.empty()) e.ext = ".png";
                }
            }
            ents.push_back(std::move(e));
        }
    }
    if (ents.empty()) {
        statusMessage_ =
            skippedUnnamed
                ? "Model's material(s) have no name - a material override "
                  "matches by name, so name them in your modelling tool first"
                : "Model has no materials to import";
        return "";
    }

    const fs::path matDirAbs = fs::path(project_.dir) / "res" / "materials";
    std::error_code ec;
    fs::create_directories(matDirAbs, ec);
    const std::string stem = fs::path(o.modelPath).stem().string();
    std::string base;
    for (int n = 1;; ++n) {
        base = stem + (n == 1 ? "" : "-" + std::to_string(n));
        if (!fs::exists(matDirAbs / (base + ".mtl"), ec)) break;
    }
    const std::string rel = "res/materials/" + base + ".mtl";

    // filesystem-safe token for a texture filename (the newmtl name is written
    // raw so the override still matches the model's material name)
    auto sane = [](std::string s) {
        for (char& c : s)
            if (!(std::isalnum((unsigned char)c) || c == '-' || c == '_')) c = '_';
        return s.empty() ? std::string("tex") : s;
    };

    std::string mtl = "# Generated by TyraX from " +
                      fs::path(o.modelPath).filename().string() +
                      " built-in materials. Edit in the Material Editor.\n";
    std::set<std::string> usedTex;
    for (const Ent& e : ents) {
        mtl += "\nnewmtl " + e.name + "\n";
        char kd[64];
        std::snprintf(kd, sizeof kd, "Kd %.4f %.4f %.4f\n", e.kd[0], e.kd[1],
                      e.kd[2]);
        mtl += kd;
        if (!e.tex.empty()) {
            std::string texName = base + "-" + sane(e.name) + e.ext;
            for (int n = 1; usedTex.count(texName); ++n)
                texName = base + "-" + sane(e.name) + std::to_string(n) + e.ext;
            usedTex.insert(texName);
            std::ofstream tf((matDirAbs / texName).string(), std::ios::binary);
            tf.write((const char*)e.tex.data(), (std::streamsize)e.tex.size());
            mtl += "map_Kd " + texName + "\n";
        }
    }
    std::ofstream mf((matDirAbs / (base + ".mtl")).string(), std::ios::binary);
    if (!mf) {
        statusMessage_ = "Cannot write " + rel;
        return "";
    }
    mf << mtl;
    mf.close();

    o.materialPath = rel;
    modelInfoCache_.clear();  // the summary caches this .mtl by path
    openMaterialEditor(rel, o.modelPath);
    statusMessage_ = "Created " + rel + " from " +
                     std::to_string((int)ents.size()) + " built-in material(s)";
    return rel;
}

// Duplicate the OPEN .mtl under a fresh "-copy" name. Referenced textures are
// copied along (once each) and the map_Kd lines rewritten, so repainting the
// duplicate never bleeds into the original's pixels.
void App::duplicateMaterialAsset() {
    if (matEdPath_.empty()) return;
    const std::filesystem::path rel(matEdPath_);
    const std::filesystem::path dirAbs =
        (std::filesystem::path(project_.dir) / rel).parent_path();
    std::error_code ec;
    const std::string stem = rel.stem().string();
    std::string newBase;
    for (int n = 1;; ++n) {
        newBase = stem + "-copy" + (n == 1 ? "" : std::to_string(n));
        if (!std::filesystem::exists(dirAbs / (newBase + ".mtl"), ec)) break;
    }

    std::map<std::string, std::string> texMap;  // old rel -> copied rel
    for (MatEdEntry& e : matEdMats_) {
        if (e.texture.empty()) continue;
        auto it = texMap.find(e.texture);
        if (it == texMap.end()) {
            const std::filesystem::path tex(e.texture);
            const std::string copied =
                (tex.parent_path() / (newBase + "-" + tex.filename().string()))
                    .generic_string();
            std::error_code cec;
            std::filesystem::copy_file(dirAbs / e.texture, dirAbs / copied,
                                       std::filesystem::copy_options::overwrite_existing,
                                       cec);
            if (!cec) {
                // paint-layer sidecar travels with its texture so the copy
                // keeps the editable stack, not just the flattened composite
                std::error_code lec;
                const std::filesystem::path srcLayers =
                    dirAbs / (e.texture + ".layers");
                if (std::filesystem::exists(srcLayers, lec))
                    std::filesystem::copy(
                        srcLayers, dirAbs / (copied + ".layers"),
                        std::filesystem::copy_options::recursive |
                            std::filesystem::copy_options::overwrite_existing,
                        lec);
            }
            // keep pointing at the shared original when the copy failed
            // (missing source) - the duplicate still renders
            it = texMap.emplace(e.texture, cec ? e.texture : copied).first;
        }
        e.texture = it->second;
    }

    matEdPath_ = (rel.parent_path() / (newBase + ".mtl")).generic_string();
    matEdSel_ = 0;
    matEdPaint_ = false;
    matEdStroke_ = false;
    matEdPaintTexRel_.clear();
    matEdLayers_.clear();
    matEdUndo_.clear();
    matEdPrevMats_ = matEdMats_;
    saveMaterialFile();
    statusMessage_ = "Duplicated to " + matEdPath_;
}

// --- Texture painting --------------------------------------------------------
// Strokes paint the selected entry's PNG through the preview mesh's UVs: the
// pick returns a surface UV, the brush splats texels around it in a CPU RGBA
// buffer, the shared GL texture is re-uploaded live (scene viewport included)
// and the PNG is written back on mouse release. The flat texture on disk IS
// the bake - the PS2 loads it like any hand-made texture.

// One stack for everything the window changes: a paint step snapshots the
// painted LAYER before the stroke, layer add/remove snapshot the structure,
// a property step snapshots the entries as they were at the previous save
// (matEdPrevMats_). Capped - textures are small (<=512^2 RGBA = 1 MB a step).
void App::matEdPushUndo(MatEdUndoStep::Kind kind, int layer,
                        const MatEdLayer* removed) {
    MatEdUndoStep s;
    s.kind = kind;
    switch (kind) {
        case MatEdUndoStep::Kind::Paint:
            if (matEdPaintW_ < 1 || layer < 0 || layer >= (int)matEdLayers_.size())
                return;
            s.texRel = matEdPaintTexRel_;
            s.layer = layer;
            s.pixels = matEdLayers_[layer].pixels;
            break;
        case MatEdUndoStep::Kind::LayerAdd:
            s.texRel = matEdPaintTexRel_;
            s.layer = layer;
            break;
        case MatEdUndoStep::Kind::LayerRemove:
            if (!removed) return;
            s.texRel = matEdPaintTexRel_;
            s.layer = layer;
            s.layerData = *removed;
            break;
        case MatEdUndoStep::Kind::Props:
            s.mats = matEdPrevMats_;
            s.sel = matEdSel_;
            matEdPrevMats_ = matEdMats_;
            break;
    }
    if (matEdUndo_.size() >= 16) matEdUndo_.erase(matEdUndo_.begin());
    matEdUndo_.push_back(std::move(s));
}

void App::matEdUndoLast() {
    if (matEdPath_.empty()) return;
    if (matEdUndo_.empty()) {
        statusMessage_ = "Material Editor: nothing to undo";
        return;
    }
    MatEdUndoStep s = std::move(matEdUndo_.back());
    matEdUndo_.pop_back();
    switch (s.kind) {
        case MatEdUndoStep::Kind::Props:
            matEdMats_ = std::move(s.mats);
            if (matEdMats_.empty()) matEdMats_.push_back(MatEdEntry{});
            matEdSel_ = s.sel < 0                         ? 0
                        : s.sel >= (int)matEdMats_.size() ? (int)matEdMats_.size() - 1
                                                          : s.sel;
            matEdPrevMats_ = matEdMats_;
            saveMaterialFile();
            statusMessage_ = "Undid material edit";
            return;
        default: break;
    }
    // paint/layer steps apply to the loaded target only - the layer stack of
    // another texture is not in memory (switch the entry back to undo there)
    if (s.texRel != matEdPaintTexRel_ || matEdPaintW_ < 1) {
        statusMessage_ = "Undo skipped - stroke belongs to " + s.texRel;
        return;
    }
    switch (s.kind) {
        case MatEdUndoStep::Kind::Paint:
            if (s.layer < 0 || s.layer >= (int)matEdLayers_.size() ||
                s.pixels.size() != matEdLayers_[s.layer].pixels.size()) {
                statusMessage_ = "Undo skipped - the layer is gone";
                return;
            }
            matEdLayers_[s.layer].pixels = std::move(s.pixels);
            matEdHaveLastUV_ = false;
            statusMessage_ = "Undid paint stroke";
            break;
        case MatEdUndoStep::Kind::LayerAdd:
            if (s.layer <= 0 || s.layer >= (int)matEdLayers_.size()) {
                statusMessage_ = "Undo skipped - the layer is gone";
                return;
            }
            matEdLayers_.erase(matEdLayers_.begin() + s.layer);
            if (matEdActiveLayer_ >= (int)matEdLayers_.size())
                matEdActiveLayer_ = (int)matEdLayers_.size() - 1;
            statusMessage_ = "Undid add layer";
            break;
        case MatEdUndoStep::Kind::LayerRemove: {
            int at = s.layer;
            if (at < 1) at = 1;
            if (at > (int)matEdLayers_.size()) at = (int)matEdLayers_.size();
            matEdLayers_.insert(matEdLayers_.begin() + at, std::move(s.layerData));
            matEdActiveLayer_ = at;
            statusMessage_ = "Undid remove layer";
            break;
        }
        default: return;
    }
    matEdComposite();
    matEdSavePaintTarget();
}

std::filesystem::path App::matEdLayersDirAbs() const {
    return std::filesystem::path(project_.dir) / (matEdPaintTexRel_ + ".layers");
}

bool App::matEdLoadPaintTarget(const std::string& texRel) {
    matEdPaintTexRel_ = texRel;  // remembered even on failure (no retry loop)
    matEdPaintPixels_.clear();
    matEdLayers_.clear();
    matEdActiveLayer_ = 0;
    matEdPaintW_ = matEdPaintH_ = 0;
    matEdHaveLastUV_ = false;
    const std::string full =
        (std::filesystem::path(project_.dir) / texRel).string();
    int w = 0, h = 0, comp = 0;
    unsigned char* pixels = stbi_load(full.c_str(), &w, &h, &comp, 4);
    if (!pixels) return false;
    matEdPaintPixels_.assign(pixels, pixels + (size_t)w * h * 4);
    stbi_image_free(pixels);
    matEdPaintW_ = w;
    matEdPaintH_ = h;

    // Layer sidecar: `<texture>.layers/layers.json` + one PNG per layer. Any
    // inconsistency (missing/mis-sized layer, bad json) falls back to a single
    // Background layer built from the composite - never fails the load.
    const std::filesystem::path dir = matEdLayersDirAbs();
    std::error_code ec;
    bool loadedStack = false;
    if (std::filesystem::exists(dir / "layers.json", ec)) {
        std::ifstream in(dir / "layers.json");
        std::stringstream ss;
        ss << in.rdbuf();
        json::Value root;
        if (json::parse(ss.str(), root)) {
            std::vector<MatEdLayer> stack;
            bool ok = true;
            if (const json::Value* arr = root.find("layers");
                arr && arr->type == json::Value::Type::Array) {
                for (const json::Value& l : arr->arr) {
                    MatEdLayer layer;
                    layer.name = l.find("name") ? l.find("name")->stringOr("Layer")
                                                : "Layer";
                    layer.blend =
                        l.find("blend") ? (int)l.find("blend")->numberOr(0) : 0;
                    layer.opacity = l.find("opacity")
                                        ? (float)l.find("opacity")->numberOr(1.0)
                                        : 1.0f;
                    layer.visible =
                        l.find("visible") ? l.find("visible")->boolOr(true) : true;
                    if (const json::Value* gv = l.find("gen");
                        gv && gv->type == json::Value::Type::Object) {
                        layer.genOn = true;
                        auto num = [&](const char* key, float def) {
                            const json::Value* v = gv->find(key);
                            return v ? (float)v->numberOr(def) : def;
                        };
                        int srcIdx = (int)num("source", 0.0f);
                        if (srcIdx < 0 ||
                            srcIdx > (int)matbake::MaskParams::Source::Bricks)
                            srcIdx = 0;
                        layer.gen.source = (matbake::MaskParams::Source)srcIdx;
                        layer.gen.rangeLo = num("lo", 0.35f);
                        layer.gen.rangeHi = num("hi", 0.75f);
                        layer.gen.invert = gv->find("invert") &&
                                           gv->find("invert")->boolOr(false);
                        layer.gen.scale = num("scale", 4.0f);
                        layer.gen.seed = (uint32_t)num("seed", 1.0f);
                        layer.gen.breakupAmount = num("breakup", 0.0f);
                        layer.gen.breakupScale = num("breakupScale", 6.0f);
                        layer.gen.mortar = num("mortar", 0.06f);
                        if (const json::Value* c = gv->find("color");
                            c && c->type == json::Value::Type::Array &&
                            c->arr.size() >= 3)
                            for (int k = 0; k < 3; ++k)
                                layer.genColor[k] =
                                    (float)c->arr[k].numberOr(0.2);
                    }
                    const std::string file =
                        l.find("file") ? l.find("file")->stringOr("") : "";
                    int lw = 0, lh = 0, lc = 0;
                    unsigned char* lp = stbi_load((dir / file).string().c_str(),
                                                  &lw, &lh, &lc, 4);
                    if (!lp || lw != w || lh != h) {
                        if (lp) stbi_image_free(lp);
                        ok = false;
                        break;
                    }
                    layer.pixels.assign(lp, lp + (size_t)w * h * 4);
                    stbi_image_free(lp);
                    stack.push_back(std::move(layer));
                }
            } else {
                ok = false;
            }
            if (ok && !stack.empty()) {
                matEdLayers_ = std::move(stack);
                if (const json::Value* a = root.find("active"))
                    matEdActiveLayer_ = (int)a->numberOr(0);
                if (matEdActiveLayer_ < 0 ||
                    matEdActiveLayer_ >= (int)matEdLayers_.size())
                    matEdActiveLayer_ = (int)matEdLayers_.size() - 1;
                loadedStack = true;
                // the sidecar is the truth - rebuild the composite from it
                // (the PNG may lag behind a crashed session)
                matEdComposite();
            } else {
                statusMessage_ =
                    "Layer sidecar unreadable - flattened to Background";
            }
        }
    }
    if (!loadedStack) {
        MatEdLayer bg;
        bg.name = "Background";
        bg.pixels = matEdPaintPixels_;
        matEdLayers_.push_back(std::move(bg));
        matEdActiveLayer_ = 0;
    }
    // smart masks are pixel caches of their params - refresh them against
    // the current bake right away (and ask for maps if none exist yet)
    if (matEdAnyGenLayer()) {
        if (!matBakeMaps_.empty())
            matEdRegenMasks();
        else
            matBakeRunOnce_ = true;
    }
    return true;
}

// Rebuilds the composite (what the PNG holds and every mesh samples) from
// the layer stack and uploads it into the shared GL texture. Blends run in
// 0..255 with the layer's per-pixel alpha x opacity as the mask; the
// composite alpha is a plain "over" so erased background shows through
// (decal cutouts paint the same way).
void App::matEdComposite() {
    const int w = matEdPaintW_, h = matEdPaintH_;
    if (w < 1 || h < 1 || matEdLayers_.empty()) return;
    const size_t count = (size_t)w * h;
    matEdPaintPixels_.assign(count * 4, 0);
    for (size_t li = 0; li < matEdLayers_.size(); ++li) {
        const MatEdLayer& L = matEdLayers_[li];
        if (!L.visible || L.pixels.size() != count * 4) continue;
        const float op = L.opacity < 0.0f ? 0.0f : L.opacity > 1.0f ? 1.0f : L.opacity;
        unsigned char* dst = matEdPaintPixels_.data();
        const unsigned char* src = L.pixels.data();
        for (size_t i = 0; i < count; ++i, dst += 4, src += 4) {
            const float sa = (src[3] / 255.0f) * op;
            if (sa <= 0.0f) continue;
            for (int c = 0; c < 3; ++c) {
                const int d = dst[c], s = src[c];
                int b;
                switch (L.blend) {
                    case 1: b = d * s / 255; break;               // multiply
                    case 2: b = d + s > 255 ? 255 : d + s; break; // add
                    case 3:                                       // overlay
                        b = d < 128 ? 2 * d * s / 255
                                    : 255 - 2 * (255 - d) * (255 - s) / 255;
                        break;
                    default: b = s; break;                        // normal
                }
                dst[c] = (unsigned char)(d + (b - d) * sa + 0.5f);
            }
            dst[3] = (unsigned char)(dst[3] + (255.0f - dst[3]) * sa + 0.5f);
        }
    }
    matEdUploadComposite();
}

// Composite -> the shared GL texture. The bake's "AO on material" preview
// multiplies the baked AO in HERE, and the "PS2 CLUT" display mode palette-
// quantizes HERE - both at upload time only. matEdPaintPixels_ (what the
// PNG saves) never contains a preview, so a stroke saved while previewing
// ships clean.
void App::matEdUploadComposite() {
    const int w = matEdPaintW_, h = matEdPaintH_;
    if (w < 1 || h < 1 || matEdPaintTexRel_.empty()) return;
    const unsigned char* src = matEdPaintPixels_.data();
    std::vector<unsigned char> tmp;
    if (matBakePreviewMode_ == 1 && !matBakeMaps_.empty()) {
        const matbake::Maps& m = matBakeMaps_;
        tmp = matEdPaintPixels_;
        auto at = [&](int xx, int yy) {
            xx = ((xx % m.w) + m.w) % m.w;
            yy = ((yy % m.h) + m.h) % m.h;
            return (float)m.ao[(size_t)yy * m.w + xx];
        };
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                // bilinear AO at the texel center (map and texture sizes
                // may differ; both wrap)
                const float fx = (x + 0.5f) / w * m.w - 0.5f;
                const float fy = (y + 0.5f) / h * m.h - 0.5f;
                const int x0 = (int)std::floor(fx), y0 = (int)std::floor(fy);
                const float tx = fx - x0, ty = fy - y0;
                const float ao =
                    (at(x0, y0) * (1 - tx) + at(x0 + 1, y0) * tx) * (1 - ty) +
                    (at(x0, y0 + 1) * (1 - tx) + at(x0 + 1, y0 + 1) * tx) * ty;
                unsigned char* px = &tmp[((size_t)y * w + x) * 4];
                for (int c = 0; c < 3; ++c)
                    px[c] = (unsigned char)(px[c] * ao * (1.0f / 255.0f) + 0.5f);
            }
        src = tmp.data();
    }
    if (matEdDisplayMode_ == 3) {
        const int cols = matEdPs2Colors();
        if (cols > 0) {
            // quantize whatever the mesh would show (AO preview included) -
            // the closest thing to the shipped .res-baked texture
            const std::vector<unsigned char> q = pngquant::quantizePreviewRGBA(
                src, w, h, cols, (pngquant::Dither)matEdPs2Dither_,
                &matEdPs2Palette_);
            if (!q.empty()) {
                viewport_.updateTexturePixels(matEdPaintTexRel_, w, h, q.data());
                return;
            }
        } else {
            matEdPs2Palette_.clear();
        }
    }
    viewport_.updateTexturePixels(matEdPaintTexRel_, w, h, src);
}

// Palette size of the "PS2 CLUT" preview (0 = full color): the explicit
// combo choice, else the shipped policy - the .mtl's per-asset override or
// the project default. (texbake lets the HIGHEST quality claimed by any
// asset sharing a texture win; this line resolves only this .mtl's claim,
// which matches unless another asset pins the same PNG higher.)
int App::matEdPs2Colors() const {
    switch (matEdPs2Mode_) {
        case 1: return 16;
        case 2: return 256;
        case 3: return 0;
        default: break;
    }
    std::string q;
    auto it = project_.textureQuality.find(matEdPath_);
    if (it != project_.textureQuality.end()) q = it->second;
    if (q.empty()) q = project_.settings.textureQuant;
    return q == "8bit" ? 256 : q == "none" ? 0 : 16;
}

// The live memory-budget line: what this texture costs on the PS2 after
// texbake, at the quality the CLUT preview resolves to.
std::string App::matEdBudgetLine(int tw, int th) const {
    const int cols = matEdPs2Colors();
    const long px = (long)tw * th;
    long bytes;
    const char* what;
    long palette = 0;
    if (cols == 16) {
        bytes = px / 2;
        palette = 16 * 4;
        what = "4-bit";
    } else if (cols == 256) {
        bytes = px;
        palette = 256 * 4;
        what = "8-bit";
    } else {
        bytes = px * 4;
        what = "32-bit";
    }
    char buf[128];
    if (palette)
        std::snprintf(buf, sizeof(buf), "%dx%d %s = %.1f KB + %ld B palette",
                      tw, th, what, bytes / 1024.0, palette);
    else
        std::snprintf(buf, sizeof(buf), "%dx%d %s = %.1f KB (no palette)", tw,
                      th, what, bytes / 1024.0);
    return buf;
}

void App::matEdSaveLayers() {
    if (matEdPaintTexRel_.empty() || matEdPaintW_ < 1) return;
    const std::filesystem::path dir = matEdLayersDirAbs();
    std::error_code ec;
    if (matEdLayers_.size() <= 1) {
        // a lone Background equals the composite - no sidecar needed
        std::filesystem::remove_all(dir, ec);
        return;
    }
    std::filesystem::create_directories(dir, ec);
    std::ostringstream manifest;
    manifest << "{\n  \"active\": " << matEdActiveLayer_ << ",\n  \"layers\": [\n";
    for (size_t i = 0; i < matEdLayers_.size(); ++i) {
        const MatEdLayer& L = matEdLayers_[i];
        const std::string file = "layer" + std::to_string(i) + ".png";
        stbi_write_png((dir / file).string().c_str(), matEdPaintW_, matEdPaintH_,
                       4, L.pixels.data(), matEdPaintW_ * 4);
        char op[16];
        std::snprintf(op, sizeof(op), "%.4g", L.opacity);
        std::string name = L.name;
        for (char& c : name)  // keep the hand-written json trivially valid
            if (c == '"' || c == '\\') c = '\'';
        manifest << "    {\"name\": \"" << name << "\", \"blend\": " << L.blend
                 << ", \"opacity\": " << op << ", \"visible\": "
                 << (L.visible ? "true" : "false") << ", \"file\": \"" << file
                 << "\"";
        if (L.genOn) {
            char gb[256];
            std::snprintf(
                gb, sizeof(gb),
                ", \"gen\": {\"source\": %d, \"lo\": %.4g, \"hi\": %.4g, "
                "\"invert\": %s, \"scale\": %.4g, \"seed\": %u, \"breakup\": "
                "%.4g, \"breakupScale\": %.4g, \"mortar\": %.4g, \"color\": "
                "[%.4g, %.4g, %.4g]}",
                (int)L.gen.source, L.gen.rangeLo, L.gen.rangeHi,
                L.gen.invert ? "true" : "false", L.gen.scale, L.gen.seed,
                L.gen.breakupAmount, L.gen.breakupScale, L.gen.mortar,
                L.genColor[0], L.genColor[1], L.genColor[2]);
            manifest << gb;
        }
        manifest << "}" << (i + 1 < matEdLayers_.size() ? "," : "") << "\n";
    }
    manifest << "  ]\n}\n";
    std::ofstream out(dir / "layers.json", std::ios::trunc);
    out << manifest.str();
    // drop stale layer files past the current count
    for (int i = (int)matEdLayers_.size();; ++i) {
        const std::filesystem::path stale = dir / ("layer" + std::to_string(i) + ".png");
        std::error_code sec;
        if (!std::filesystem::exists(stale, sec)) break;
        std::filesystem::remove(stale, sec);
    }
}

void App::matEdSavePaintTarget() {
    if (matEdPaintTexRel_.empty() || matEdPaintW_ < 1) return;
    const std::string full =
        (std::filesystem::path(project_.dir) / matEdPaintTexRel_).string();
    if (stbi_write_png(full.c_str(), matEdPaintW_, matEdPaintH_, 4,
                       matEdPaintPixels_.data(), matEdPaintW_ * 4)) {
        statusMessage_ = "Painted " + matEdPaintTexRel_;
        liveTexNotify(matEdPaintTexRel_);  // hot-reload the running game
    } else {
        statusMessage_ = "Cannot write " + matEdPaintTexRel_;
    }
    matEdSaveLayers();
}

// Texture hot reload (docs/live-link.md): after a paint/bake save, re-bake
// the texture the way the build shipped it - the palette format is detected
// from the existing bin/ PNG's IHDR, so the swap is format-identical - drop
// it next to the ELF (tmp + rename, the game never sees a half file) and
// bump bin/livetex.bin. The generated live_tex poller re-decodes the file
// and re-sends the pixels to the texture's existing GS VRAM address.
void App::liveTexNotify(const std::string& texResRel) {
    if (!hasProject_) return;
    if (project_.settings.buildProfile != "debug" ||
        !project_.settings.liveLink)
        return;  // mirrors the poller's existence in the build
    if (texResRel.rfind("res/", 0) != 0) return;
    if (matEdPaintW_ < 1 || matEdPaintPixels_.empty()) return;
    const std::string gameRel = texResRel.substr(4);
    if (gameRel.size() >= 96) return;  // record path field is 96 bytes
    namespace fs = std::filesystem;
    const fs::path binDir = fs::path(project_.dir) / "bin";
    const fs::path dst = binDir / gameRel;
    std::error_code ec;
    if (!fs::exists(dst, ec)) return;  // never shipped - nothing to reload

    // shipped format from the PNG header: color type 3 = paletted, bit
    // depth picks the palette size; anything else = full color
    int cols = 0;
    {
        std::ifstream in(dst, std::ios::binary);
        unsigned char hdr[26] = {};
        in.read(reinterpret_cast<char*>(hdr), sizeof(hdr));
        if (in.gcount() >= 26 && hdr[25] == 3)
            cols = hdr[24] == 4 ? 16 : 256;
    }
    const fs::path tmp = binDir / (gameRel + ".txtmp");
    fs::create_directories(tmp.parent_path(), ec);
    std::string err;
    const bool ok =
        cols > 0 ? pngquant::quantizeRGBA(tmp.string(),
                                          matEdPaintPixels_.data(),
                                          matEdPaintW_, matEdPaintH_, cols, err)
                 : pngquant::writePngRGBA(tmp.string(),
                                          matEdPaintPixels_.data(),
                                          matEdPaintW_, matEdPaintH_, err);
    if (!ok) {
        fs::remove(tmp, ec);
        return;
    }
    fs::rename(tmp, dst, ec);
    if (ec) {  // the game holds the file open right now - drop this update
        fs::remove(tmp, ec);
        return;
    }

    // announce: livetex.bin lists every repainted texture with a growing
    // generation; the poller applies the ones it hasn't seen. The list is
    // cumulative for the session so a game booted later catches up.
    ++liveTexGen_[gameRel];
    if (liveTexGen_.size() > 64) {  // record cap; keep the current one
        const uint32_t keep = liveTexGen_[gameRel];
        liveTexGen_.clear();
        liveTexGen_[gameRel] = keep;
    }
    const uint32_t seq = ++liveTexSeq_;
    std::vector<unsigned char> file;
    auto app32 = [&](uint32_t v) {
        const unsigned char* b = reinterpret_cast<const unsigned char*>(&v);
        file.insert(file.end(), b, b + 4);
    };
    app32(0x544C5854u);  // "TXLT"
    app32(1u);
    app32(seq);
    app32((uint32_t)liveTexGen_.size());
    for (const auto& [rel, gen] : liveTexGen_) {
        char rec[104] = {};
        std::snprintf(rec, 96, "%s", rel.c_str());
        std::memcpy(rec + 96, &gen, 4);
        file.insert(file.end(), rec, rec + 104);
    }
    app32(seq ^ 0x5A5A5A5Au);
    const fs::path mtmp = binDir / "livetex.tmp";
    const fs::path mdst = binDir / "livetex.bin";
    {
        std::ofstream out(mtmp, std::ios::binary | std::ios::trunc);
        if (!out) return;
        out.write(reinterpret_cast<const char*>(file.data()),
                  (std::streamsize)file.size());
        if (!out) return;
    }
    fs::rename(mtmp, mdst, ec);
    if (ec) fs::remove(mtmp, ec);
}

// One stamp at a surface UV (image space, v down), onto the ACTIVE layer
// (straight-alpha "over"; the eraser mode takes alpha away instead). The
// color brush is a soft round splat; a Brush is the brush IMAGE fitted into
// the brush diameter - its alpha is the stamp shape, its RGB the paint
// (GIMP-style dabs, not a tiled pattern). Coordinates wrap (GS textures
// repeat), so strokes cross seams cleanly. The caller recomposites once per
// frame, not per stamp.
void App::matEdStamp(float u, float v) {
    const int w = matEdPaintW_, h = matEdPaintH_;
    if (w < 1 || h < 1 || matEdActiveLayer_ < 0 ||
        matEdActiveLayer_ >= (int)matEdLayers_.size())
        return;
    MatEdLayer& L = matEdLayers_[matEdActiveLayer_];
    if (L.pixels.size() != (size_t)w * h * 4) return;
    u -= std::floor(u);
    v -= std::floor(v);
    const float cx = u * w, cy = v * h;
    const float size = matEdBrushSize_ < 1.0f ? 1.0f : matEdBrushSize_;
    const bool brush = matEdBrushMode_ == 1 && matEdPatternW_ > 0;
    const bool eraser = matEdBrushMode_ == 2;
    // Dab rotation (brush images only): the manual Angle, or a fresh random
    // one per dab. The ghost pass never rolls - a preview that spins every
    // frame reads as noise (and would advance the sequence).
    float rotC = 1.0f, rotS = 0.0f;
    if (brush) {
        float deg = matEdBrushAngle_;
        if (matEdBrushRandomRot_ && !matEdGhostPass_) {
            matEdRng_ = matEdRng_ * 1664525u + 1013904223u;
            deg = (float)(matEdRng_ >> 8) * (360.0f / 16777216.0f);
        }
        const float rad = deg * 3.14159265f / 180.0f;
        rotC = std::cos(rad);
        rotS = std::sin(rad);
    }
    // Per-dab opacity: the base Opacity, randomly reduced by up to Vary%
    // (ghost pass exempt - the preview shows the base strength).
    float dabOpacity = matEdBrushOpacity_;
    if (matEdBrushOpacityVary_ > 0.0f && !matEdGhostPass_) {
        matEdRng_ = matEdRng_ * 1664525u + 1013904223u;
        const float r01 = (float)(matEdRng_ >> 8) * (1.0f / 16777216.0f);
        dabOpacity *= 1.0f - matEdBrushOpacityVary_ * 0.01f * r01;
    }
    // a rotated square dab pokes past the inscribed circle - pad the loop
    const int r = (int)std::ceil(size * (brush ? 1.4143f : 1.0f));
    const int icx = (int)cx, icy = (int)cy;
    for (int dy = -r; dy <= r; ++dy)
        for (int dx = -r; dx <= r; ++dx) {
            const int px = icx + dx, py = icy + dy;
            const float ox = px + 0.5f - cx, oy = py + 0.5f - cy;
            float a;
            float src[3];
            if (brush) {
                // the whole brush image spans the stamp: rotate the offset
                // back into image space, then map to image UV
                const float rx = ox * rotC + oy * rotS;
                const float ry = -ox * rotS + oy * rotC;
                const float fx = rx / size * 0.5f + 0.5f;
                const float fy = ry / size * 0.5f + 0.5f;
                if (fx < 0.0f || fx >= 1.0f || fy < 0.0f || fy >= 1.0f) continue;
                const int qx = (int)(fx * matEdPatternW_);
                const int qy = (int)(fy * matEdPatternH_);
                const unsigned char* sp =
                    &matEdPatternPixels_[((size_t)qy * matEdPatternW_ + qx) * 4];
                // the image's own alpha IS the dab shape - no radial falloff
                a = dabOpacity * (sp[3] / 255.0f);
                if (a <= 0.0f) continue;
                src[0] = sp[0], src[1] = sp[1], src[2] = sp[2];
            } else {
                const float d2 = ox * ox + oy * oy;
                if (d2 > size * size) continue;
                const float t = std::sqrt(d2) / size;
                a = dabOpacity * (1.0f - t * t);  // soft falloff
                if (a <= 0.0f) continue;
                src[0] = matEdBrushColor_[0] * 255.0f;
                src[1] = matEdBrushColor_[1] * 255.0f;
                src[2] = matEdBrushColor_[2] * 255.0f;
            }
            const int sx = ((px % w) + w) % w;
            const int sy = ((py % h) + h) % h;
            unsigned char* dst = &L.pixels[((size_t)sy * w + sx) * 4];
            if (eraser) {
                dst[3] = (unsigned char)(dst[3] * (1.0f - a) + 0.5f);
                continue;
            }
            // straight-alpha "over" onto the layer: a transparent texel takes
            // the stroke color outright (no dark fringe from the RGB zeros)
            const float da = dst[3] / 255.0f;
            const float outA = a + da * (1.0f - a);
            if (outA <= 0.0f) continue;
            for (int c = 0; c < 3; ++c) {
                const float blended =
                    (src[c] * a + dst[c] * da * (1.0f - a)) / outA;
                dst[c] = (unsigned char)(blended + 0.5f);
            }
            dst[3] = (unsigned char)(outA * 255.0f + 0.5f);
        }
}

// Lays stamps along the stroke at the Spacing interval (a % of the brush
// diameter, GIMP semantics): the residual distance carries across mouse
// samples, so low spacing draws one continuous line and >=100% drops clearly
// separated dabs no matter how fast the mouse moves. A jump longer than a
// third of the texture is a UV-seam crossing - laying dabs through it would
// smear a line across unrelated texels, so the stroke restarts there instead.
void App::matEdPaintTo(float u, float v) {
    const float w = (float)matEdPaintW_, h = (float)matEdPaintH_;
    if (w < 1.0f || h < 1.0f) return;
    const float size = matEdBrushSize_ < 1.0f ? 1.0f : matEdBrushSize_;
    const float step =
        std::max(1.0f, matEdBrushSpacing_ * 0.01f * 2.0f * size);
    if (!matEdHaveLastUV_) {  // stroke start: a dab right under the click
        matEdStamp(u, v);
        matEdLastUV_[0] = u, matEdLastUV_[1] = v;
        matEdHaveLastUV_ = true;
        matEdStampResidual_ = 0.0f;
        return;
    }
    const float x0 = matEdLastUV_[0] * w, y0 = matEdLastUV_[1] * h;
    const float x1 = u * w, y1 = v * h;
    const float dx = x1 - x0, dy = y1 - y0;
    const float dist = std::sqrt(dx * dx + dy * dy);
    const float seamGuard = 0.33f * (w < h ? w : h);
    if (dist >= seamGuard) {  // seam crossing - restart, dab at the new spot
        matEdStamp(u, v);
        matEdLastUV_[0] = u, matEdLastUV_[1] = v;
        matEdStampResidual_ = 0.0f;
        return;
    }
    float done = 0.0f;
    float need = step - matEdStampResidual_;  // distance to the next dab
    while (need <= dist - done) {
        done += need;
        const float t = done / dist;
        matEdStamp((x0 + dx * t) / w, (y0 + dy * t) / h);
        need = step;
        matEdStampResidual_ = 0.0f;
    }
    matEdStampResidual_ += dist - done;
    matEdLastUV_[0] = u;
    matEdLastUV_[1] = v;
}

// --- Map baking (docs/material-baking.md) ------------------------------------
// The Material Editor front of matbake: parameters live per .mtl (the
// "# tyra-bake" hint), the bake runs progressively on matbake's worker
// thread, snapshots land on the preview mesh within a round (~ms), and the
// finished AO becomes a "Baked AO" multiply layer of the entry's texture.

namespace {
uint64_t bakeFnv(const std::string& s, uint64_t h = 1469598103934665603ull) {
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

// Bilinear (wrapping) resample of a single-channel map at the center of
// texel (x, y) of a w x h target.
float bakeSampleGray(const std::vector<uint8_t>& map, int mw, int mh, int x,
                     int y, int w, int h) {
    const float fx = (x + 0.5f) / w * mw - 0.5f;
    const float fy = (y + 0.5f) / h * mh - 0.5f;
    const int x0 = (int)std::floor(fx), y0 = (int)std::floor(fy);
    const float tx = fx - x0, ty = fy - y0;
    auto at = [&](int xx, int yy) {
        xx = ((xx % mw) + mw) % mw;
        yy = ((yy % mh) + mh) % mh;
        return (float)map[(size_t)yy * mw + xx];
    };
    return (at(x0, y0) * (1 - tx) + at(x0 + 1, y0) * tx) * (1 - ty) +
           (at(x0, y0 + 1) * (1 - tx) + at(x0 + 1, y0 + 1) * tx) * ty;
}
}  // namespace

void App::matBakeResetParams() {
    matBakeSizeIdx_ = 2;
    matBakeRays_ = 64;
    matBakeMaxDist_ = 0.0f;
    matBakeSSIdx_ = 1;
    matBakeBackface_ = true;
    matBakePadding_ = 4;
    matBakeSeed_ = 1;
    matBakeHigh_.clear();
    matBakeCage_ = 0.0f;
}

matbake::Params App::matBakeParams() const {
    matbake::Params p;
    p.size = 64 << matBakeSizeIdx_;
    p.samples = matBakeRays_;
    p.maxDist = matBakeMaxDist_;
    p.supersample = 1 << matBakeSSIdx_;
    p.backface = matBakeBackface_;
    p.padding = matBakePadding_;
    p.seed = (uint32_t)matBakeSeed_;
    p.cageOffset = matBakeCage_;
    return p;
}

// Triangle soup (matbake::MeshInput) from an animated model's BIND-POSE
// geometry (frame 0). Parts whose material name matches entryName are
// paintable (empty = all). The .mtl override only affects color/texture, not
// geometry or UVs, so the bake needs the raw mesh - no override applied here.
static matbake::MeshInput meshInputFromBaked(const glbparser::Baked& baked,
                                             const std::string& entryName) {
    matbake::MeshInput mi;
    for (const glbparser::Part& p : baked.parts) {
        const bool paint = entryName.empty() || p.material == entryName;
        const bool hasUv = p.uvs.size() >= (size_t)p.vertexCount * 2;
        for (int v = 0; v + 2 < p.vertexCount; v += 3) {
            for (int c = 0; c < 3; ++c) {
                const int idx = v + c;
                mi.verts.insert(
                    mi.verts.end(),
                    {p.positions[idx * 3], p.positions[idx * 3 + 1],
                     p.positions[idx * 3 + 2], p.normals[idx * 3],
                     p.normals[idx * 3 + 1], p.normals[idx * 3 + 2],
                     hasUv ? p.uvs[idx * 2] : 0.0f,
                     hasUv ? p.uvs[idx * 2 + 1] : 0.0f});
            }
            mi.paintTri.push_back(paint ? 1 : 0);
        }
    }
    return mi;
}

// (Re)build the cached matbake inputs. Keys carry the source file mtimes so
// an external re-export re-bakes; unchanged keys cost two stat calls.
bool App::matBakeBuildMeshes(const std::string& entryName) {
    auto mtimeOf = [&](const std::string& rel) {
        std::error_code ec;
        const auto t = std::filesystem::last_write_time(
            std::filesystem::path(project_.dir) / rel, ec);
        return ec ? std::string("?")
                  : std::to_string(t.time_since_epoch().count());
    };
    std::string key;
    if (matEdShape_ == 4) {
        if (matEdModel_.empty()) {
            matBakeMeshError_ = "No preview model selected";
            return false;
        }
        // the .uvs replacement-UV sidecar changes the baked mesh without
        // touching the model file - its mtime joins the key
        key = "m|" + matEdModel_ + "|" + matEdPath_ + "|" + entryName + "|" +
              mtimeOf(matEdModel_) + "|" + mtimeOf(matEdModel_ + ".uvs");
    } else {
        key = "p|" + std::to_string(matEdShape_);
    }
    if (key != matBakeMeshKey_) {
        matBakeMeshKey_ = key;
        matBakeMeshError_.clear();
        if (matEdShape_ == 4 && isAnimatedModelPath(matEdModel_)) {
            glbparser::Baked baked;
            std::string err;
            if (animimport::bake(
                    (std::filesystem::path(project_.dir) / matEdModel_).string(),
                    12.0f, baked, err))
                matBakeMeshLow_ = meshInputFromBaked(baked, entryName);
            else {
                matBakeMeshLow_ = matbake::MeshInput{};
                matBakeMeshError_ = "Cannot read " + matEdModel_;
            }
        } else if (matEdShape_ == 4) {
            objparser::Model m;
            if (objparser::load(
                    (std::filesystem::path(project_.dir) / matEdModel_).string(),
                    m,
                    (std::filesystem::path(project_.dir) / matEdPath_).string()))
                matBakeMeshLow_ = matbake::fromModel(m, entryName);
            else {
                matBakeMeshLow_ = matbake::MeshInput{};
                matBakeMeshError_ = "Cannot read " + matEdModel_;
            }
        } else {
            matBakeMeshLow_ = matbake::fromPrimitive(
                matEdShape_,
                matEdShape_ == 0 ? kDefaultBoxDetail : kDefaultPrimDetail);
        }
        matBakeMeshLow_.signature = bakeFnv(key);
    }
    const std::string hkey =
        matBakeHigh_.empty() ? ""
                             : "h|" + matBakeHigh_ + "|" + mtimeOf(matBakeHigh_);
    if (hkey != matBakeHighKey_) {
        matBakeHighKey_ = hkey;
        matBakeMeshHigh_ = matbake::MeshInput{};
        if (!matBakeHigh_.empty()) {
            objparser::Model m;
            if (objparser::load(
                    (std::filesystem::path(project_.dir) / matBakeHigh_).string(),
                    m))
                matBakeMeshHigh_ = matbake::fromModel(m, "");
            else
                matBakeMeshError_ = "Cannot read " + matBakeHigh_;
            matBakeMeshHigh_.signature = bakeFnv(hkey);
        }
    }
    if (matBakeMeshLow_.empty()) {
        if (matBakeMeshError_.empty()) matBakeMeshError_ = "No mesh to bake";
        return false;
    }
    return true;
}

// Once per frame while the editor shows a file: restart the bake when any
// input changed (the Baker caches its gbuffer+BVH, so sampling-only slider
// drags re-run nearly free), poll snapshots onto the preview, and finish a
// pending "Bake & add layer".
void App::matBakeTick(const std::string& entryName, const std::string& texRel) {
    // The paint target must always belong to the SELECTED entry: with an
    // untextured entry selected, a target left over from the previous one
    // would silently receive this entry's bake previews and layers (the
    // "legs' AO showed up on the jaw" bug).
    if (texRel.empty()) matEdUnloadPaintTarget();
    // A pending "Bake & add layer" is armed for one specific entry -
    // switching entries turns it into a cross-application, so cancel.
    if (matBakeApplyWhenDone_ && entryName != matBakeApplyEntry_) {
        matBakeApplyWhenDone_ = false;
        statusMessage_ = "Bake apply canceled - the entry changed";
    }
    if (matBakeRunOnce_ && !matBakeMaps_.empty() &&
        matBakeMaps_.samplesDone >= matBakeRays_ && !matBaker_.running())
        matBakeRunOnce_ = false;  // the smart masks got their maps
    const bool wantBake = matBakePreviewMode_ > 0 || matBakeApplyWhenDone_ ||
                          matBakeRunOnce_;
    if (wantBake) {
        // the AO-on-material merge rides the paint target's GL upload
        if (matBakePreviewMode_ == 1 && !texRel.empty() &&
            matEdPaintTexRel_ != texRel) {
            if (matEdLoadPaintTarget(texRel)) matEdComposite();
        }
        if (matBakeBuildMeshes(entryName)) {
            char pb[128];
            std::snprintf(pb, sizeof(pb), "|%d|%d|%.6g|%d|%d|%d|%d|%.6g",
                          matBakeSizeIdx_, matBakeRays_, matBakeMaxDist_,
                          matBakeSSIdx_, (int)matBakeBackface_, matBakePadding_,
                          matBakeSeed_, matBakeCage_);
            const uint64_t sig =
                bakeFnv(pb, bakeFnv(matBakeMeshKey_ + "|" + matBakeHighKey_));
            if (sig != matBakeStartedSig_) {
                matBakeStartedSig_ = sig;
                matBaker_.start(matBakeMeshLow_, matBakeMeshHigh_,
                                matBakeParams());
            }
        } else if (matBakeApplyWhenDone_) {
            matBakeApplyWhenDone_ = false;
            statusMessage_ = "Bake: " + matBakeMeshError_;
        }
    }
    if (matBaker_.version() != matBakeSeenVersion_) {
        matBakeSeenVersion_ = matBaker_.version();
        matBakeMaps_ = matBaker_.snapshot();
        if (matEdAnyGenLayer() && matEdPaintW_ > 0)
            matEdRegenMasks();  // smart masks track the refining bake live
        if (matBakePreviewMode_ == 1)
            matEdUploadComposite();
        else if (matBakePreviewMode_ == 2)
            matBakeUploadSolo();
    }
    if (matBakeApplyWhenDone_ && !matBaker_.running()) {
        // the final round may land between the poll above and here
        matBakeMaps_ = matBaker_.snapshot();
        const std::string err = matBaker_.error();
        if (!matBakeMaps_.empty() && matBakeMaps_.samplesDone >= matBakeRays_) {
            matBakeApplyWhenDone_ = false;
            matBakeApplyLayer();
        } else if (!err.empty()) {
            matBakeApplyWhenDone_ = false;
            statusMessage_ = "Bake failed: " + err;
        }
    }
}

// Raw-map view: the selected map replaces the material texture on the
// preview mesh via a pseudo-path texture (never touches disk).
void App::matBakeUploadSolo() {
    if (matBakeMaps_.empty()) return;
    const matbake::Maps& m = matBakeMaps_;
    const std::vector<uint8_t>* gray = matBakeMapView_ == 0   ? &m.ao
                                       : matBakeMapView_ == 1 ? &m.curvature
                                       : matBakeMapView_ == 2 ? &m.thickness
                                                              : nullptr;
    const std::vector<uint8_t>* rgb = matBakeMapView_ == 3   ? &m.bent
                                      : matBakeMapView_ == 4 ? &m.normal
                                      : matBakeMapView_ == 5 ? &m.position
                                                             : nullptr;
    std::vector<unsigned char> rgba((size_t)m.w * m.h * 4);
    for (size_t i = 0; i < (size_t)m.w * m.h; ++i) {
        unsigned char* px = &rgba[i * 4];
        if (gray)
            px[0] = px[1] = px[2] = (*gray)[i];
        else
            for (int c = 0; c < 3; ++c) px[c] = (*rgb)[i * 3 + c];
        px[3] = 255;
    }
    viewport_.updateTexturePixels("@matbake-view", m.w, m.h, rgba.data());
}

// The auto-hookup: the baked AO lands as a "Baked AO" multiply layer of the
// entry's texture (re-bakes overwrite it in place instead of stacking
// duplicates), and the .mtl saves so the bake parameters ship next to the
// result. One click, the model just looks better.
void App::matBakeApplyLayer() {
    if (matBakeMaps_.empty()) {
        statusMessage_ = "Nothing baked yet";
        return;
    }
    if (matEdPaintW_ < 1 || matEdPaintTexRel_.empty()) {
        statusMessage_ =
            "Bake: the entry needs a texture (Texture > New paintable texture)";
        return;
    }
    // never apply onto another entry's texture (a stale target)
    if (matEdSel_ >= 0 && matEdSel_ < (int)matEdMats_.size()) {
        const MatEdEntry& e = matEdMats_[matEdSel_];
        const std::string expect =
            e.texture.empty()
                ? ""
                : (std::filesystem::path(matEdPath_).parent_path() / e.texture)
                      .generic_string();
        if (expect != matEdPaintTexRel_) {
            statusMessage_ =
                "Bake apply skipped - the loaded texture belongs to another "
                "entry";
            return;
        }
    }
    const int w = matEdPaintW_, h = matEdPaintH_;
    const matbake::Maps& m = matBakeMaps_;
    std::vector<unsigned char> pixels((size_t)w * h * 4);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const float ao = bakeSampleGray(m.ao, m.w, m.h, x, y, w, h);
            unsigned char* px = &pixels[((size_t)y * w + x) * 4];
            px[0] = px[1] = px[2] = (unsigned char)(ao + 0.5f);
            px[3] = 255;
        }
    int at = -1;
    for (size_t i = 0; i < matEdLayers_.size(); ++i)
        if (matEdLayers_[i].name == "Baked AO") at = (int)i;
    if (at >= 0) {
        matEdPushUndo(MatEdUndoStep::Kind::Paint, at);
        matEdLayers_[at].pixels = std::move(pixels);
        matEdLayers_[at].blend = 1;
        matEdLayers_[at].visible = true;
    } else {
        MatEdLayer l;
        l.name = "Baked AO";
        l.blend = 1;  // multiply
        l.pixels = std::move(pixels);
        at = (int)matEdLayers_.size();
        matEdLayers_.push_back(std::move(l));
        matEdPushUndo(MatEdUndoStep::Kind::LayerAdd, at);
    }
    matEdComposite();
    matEdSavePaintTarget();
    saveMaterialFile();  // persists the # tyra-bake parameters
    statusMessage_ = "Baked AO applied as a multiply layer (" +
                     std::to_string(m.samplesDone) + " rays/texel)";
}

// Writes the whole map set as PNGs next to the .mtl - mask sources for
// hand-painted wear, exports for external tools. They are ordinary res/
// PNGs afterwards (assignable as textures; delete what the game won't use).
void App::matBakeSaveMaps(const std::filesystem::path& mtlDirAbs,
                          const std::string& entryName) {
    if (matBakeMaps_.empty()) {
        statusMessage_ = "Nothing baked yet";
        return;
    }
    const matbake::Maps& m = matBakeMaps_;
    const std::string base = sanitizeAssetName(
        std::filesystem::path(matEdPath_).stem().string() +
        (entryName.empty() ? "" : "-" + entryName));
    struct Out {
        const char* suffix;
        const std::vector<uint8_t>* gray;
        const std::vector<uint8_t>* rgb;
    };
    const Out outs[] = {
        {"-ao.png", &m.ao, nullptr},
        {"-curvature.png", &m.curvature, nullptr},
        {"-thickness.png", &m.thickness, nullptr},
        {"-bent.png", nullptr, &m.bent},
        {"-normal.png", nullptr, &m.normal},
        {"-position.png", nullptr, &m.position},
    };
    int written = 0;
    std::vector<unsigned char> rgb((size_t)m.w * m.h * 3);
    for (const Out& o : outs) {
        for (size_t i = 0; i < (size_t)m.w * m.h; ++i)
            for (int c = 0; c < 3; ++c)
                rgb[i * 3 + c] = o.gray ? (*o.gray)[i] : (*o.rgb)[i * 3 + c];
        written += stbi_write_png((mtlDirAbs / (base + o.suffix)).string().c_str(),
                                  m.w, m.h, 3, rgb.data(), m.w * 3)
                       ? 1
                       : 0;
    }
    saveMaterialFile();  // persists the # tyra-bake parameters
    statusMessage_ = "Saved " + std::to_string(written) +
                     " baked maps next to " + matEdPath_;
}

// The "Bake maps" block of the property column.
void App::matEdBakeSection(const std::string& entryName,
                           const std::string& texRel) {
    ImGui::SeparatorText("Bake maps");
    const char* prevModes[] = {"Off", "AO on material", "Map view"};
    ImGui::SetNextItemWidth(scaled(150.0f));
    if (ImGui::Combo("Preview", &matBakePreviewMode_, prevModes, 3)) {
        if (matBakePreviewMode_ != 1) matEdUploadComposite();
        if (matBakePreviewMode_ == 2)
            matBakeUploadSolo();
        else if (matBakePreviewMode_ == 0) {
            matBaker_.cancel();
            matBakeStartedSig_ = 0;
        }
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "Live progressive bake on the preview mesh. \"AO on material\"\n"
            "multiplies the baked occlusion over the textured material\n"
            "(preview only - nothing is saved); \"Map view\" shows the raw\n"
            "baked map. Parameter changes re-bake immediately.");
    if (matBakePreviewMode_ == 2) {
        ImGui::SameLine();
        const char* maps[] = {"AO",        "Curvature", "Thickness",
                              "Bent normal", "OS normal", "Position"};
        ImGui::SetNextItemWidth(scaled(110.0f));
        if (ImGui::Combo("##bake_map", &matBakeMapView_, maps, 6))
            matBakeUploadSolo();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "One bake produces them all: occlusion, geometric edge/cavity\n"
                "curvature, thickness (white = solid depth below the surface),\n"
                "average unoccluded direction, object-space normals and\n"
                "AABB-normalized positions.");
    }

    // high-poly: bake the detail of a dense mesh into this (low) one
    {
        const char* noneLabel = "<none - bake this mesh>";
        ImGui::SetNextItemWidth(scaled(240.0f));
        if (ImGui::BeginCombo("High-poly",
                              matBakeHigh_.empty()
                                  ? noneLabel
                                  : std::filesystem::path(matBakeHigh_)
                                        .filename()
                                        .string()
                                        .c_str())) {
            if (ImGui::Selectable(noneLabel, matBakeHigh_.empty()))
                matBakeHigh_.clear();
            for (const std::string& mo : listAssetFiles("models", ".obj")) {
                const std::string rel = "res/models/" + mo;
                if (ImGui::Selectable(mo.c_str(), rel == matBakeHigh_))
                    matBakeHigh_ = rel;
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "A dense version of the preview mesh: every texel is first\n"
                "projected onto it (cage projection along the smoothed\n"
                "normals), so the baked maps carry the high mesh's detail -\n"
                "the low-poly model looks like it has geometry it doesn't.");
        if (!matBakeHigh_.empty()) {
            ImGui::SetNextItemWidth(scaled(140.0f));
            ImGui::DragFloat("Cage", &matBakeCage_, 0.005f, 0.0f, 10.0f,
                             matBakeCage_ <= 0.0f ? "auto" : "%.3f");
            if (matBakeCage_ < 0.0f) matBakeCage_ = 0.0f;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "How far above the low surface the projection rays start\n"
                    "(world units; auto = 2%% of the model size). Raise it if\n"
                    "tall high-poly detail gets clipped, lower it if opposite\n"
                    "surfaces bleed into each other.");
        }
    }

    const char* sizes[] = {"64", "128", "256", "512"};
    ImGui::SetNextItemWidth(scaled(90.0f));
    ImGui::Combo("Resolution", &matBakeSizeIdx_, sizes, 4);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Baked map size. Match the entry's texture for the\n"
                          "layer bake; bigger only helps saved maps.");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(scaled(110.0f));
    ImGui::SliderInt("Rays", &matBakeRays_, 8, 512, "%d",
                     ImGuiSliderFlags_Logarithmic);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Hemisphere rays per texel at full quality. 64 is\n"
                          "clean for most props; the preview refines toward\n"
                          "this progressively.");

    ImGui::SetNextItemWidth(scaled(140.0f));
    ImGui::DragFloat("Max distance", &matBakeMaxDist_, 0.02f, 0.0f, 1000.0f,
                     matBakeMaxDist_ <= 0.0f ? "auto" : "%.2f");
    if (matBakeMaxDist_ < 0.0f) matBakeMaxDist_ = 0.0f;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "Occlusion reach in world units (auto = half the model size).\n"
            "THE artistic knob: small = tight contact shadows in crevices,\n"
            "large = broad soft shading. Interiors need a limit or they\n"
            "bake pitch black.");

    const char* ssLevels[] = {"1x", "2x", "4x"};
    ImGui::SetNextItemWidth(scaled(90.0f));
    ImGui::Combo("Anti-alias", &matBakeSSIdx_, ssLevels, 3);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Surface samples per texel axis (supersampling in\n"
                          "UV space) - smooths island edges.");
    ImGui::SameLine();
    ImGui::Checkbox("Backface hits", &matBakeBackface_);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Rays hitting triangle back sides count as\n"
                          "occluders. Keep on for thin/unwelded geometry;\n"
                          "turn off if closed meshes bake too dark inside\n"
                          "overlaps.");

    ImGui::SetNextItemWidth(scaled(110.0f));
    ImGui::SliderInt("Padding", &matBakePadding_, 0, 16, "%d px");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Dilate ring around the UV islands - bilinear\n"
                          "filtering and mipmaps need it or seams go dark.");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(scaled(80.0f));
    ImGui::InputInt("Seed", &matBakeSeed_);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Sampling seed. Same seed + same settings = the\n"
                          "same bake, bit for bit.");

    if (!matBakeMeshError_.empty())
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "%s",
                           matBakeMeshError_.c_str());
    const std::string bakeErr = matBaker_.error();
    if (!bakeErr.empty())
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "%s",
                           bakeErr.c_str());
    if (matBaker_.running()) {
        ImGui::ProgressBar(matBaker_.progress(), ImVec2(-FLT_MIN, 0));
    } else if (!matBakeMaps_.empty() && matBakePreviewMode_ > 0) {
        ImGui::TextDisabled("%d rays/texel baked", matBakeMaps_.samplesDone);
    }

    ImGui::BeginDisabled(texRel.empty());
    if (ImGui::Button("Bake & add AO layer")) {
        matBakeApplyWhenDone_ = true;
        matBakeApplyEntry_ = entryName;  // switching entries cancels it
        matBakeStartedSig_ = 0;  // force a fresh full-quality run
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            texRel.empty()
                ? "The entry needs a texture first\n"
                  "(Texture > New paintable texture...)."
                : "Bakes at full quality, then drops the AO onto the\n"
                  "entry's texture as a \"Baked AO\" multiply layer\n"
                  "(re-bakes update it in place). Undo with Ctrl+Z.");
    ImGui::SameLine();
    ImGui::BeginDisabled(matBakeMaps_.empty() || matBaker_.running());
    if (ImGui::Button("Save all maps")) {
        matBakeSaveMaps(
            (std::filesystem::path(project_.dir) / matEdPath_).parent_path(),
            entryName);
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "Writes ao/curvature/thickness/bent/normal/position PNGs next\n"
            "to the .mtl - mask sources for wear & dirt, or exports for\n"
            "external tools.");
}

// --- Smart masks (docs/material-baking.md) -----------------------------------
// A gen layer's pixels are its color filled through a matbake::generateMask
// alpha - "wear on edges", "dirt in cavities", world-space noise - driven by
// the baked map set, regenerated live as the bake refines.

void App::matEdRegenLayer(MatEdLayer& l) {
    if (!l.genOn || matEdPaintW_ < 1 || matBakeMaps_.empty()) return;
    const std::vector<uint8_t> mask = matbake::generateMask(
        matBakeMaps_, l.gen, matEdPaintW_, matEdPaintH_);
    if (mask.empty()) return;
    const size_t count = (size_t)matEdPaintW_ * matEdPaintH_;
    l.pixels.resize(count * 4);
    const unsigned char r = (unsigned char)(l.genColor[0] * 255.0f + 0.5f);
    const unsigned char g = (unsigned char)(l.genColor[1] * 255.0f + 0.5f);
    const unsigned char b = (unsigned char)(l.genColor[2] * 255.0f + 0.5f);
    for (size_t i = 0; i < count; ++i) {
        unsigned char* px = &l.pixels[i * 4];
        px[0] = r, px[1] = g, px[2] = b;
        px[3] = mask[i];
    }
}

bool App::matEdAnyGenLayer() const {
    for (const MatEdLayer& l : matEdLayers_)
        if (l.genOn) return true;
    return false;
}

void App::matEdRegenMasks() {
    bool any = false;
    for (MatEdLayer& l : matEdLayers_) {
        if (!l.genOn) continue;
        matEdRegenLayer(l);
        any = true;
    }
    if (any) matEdComposite();
}

// Generator controls of the active layer (shown under the layer list).
void App::matEdGenControls() {
    if (matEdActiveLayer_ < 0 || matEdActiveLayer_ >= (int)matEdLayers_.size())
        return;
    MatEdLayer& L = matEdLayers_[matEdActiveLayer_];
    if (!L.genOn) return;
    ImGui::SeparatorText("Smart mask");
    bool changed = false, commit = false;
    const char* sources[] = {"Edge wear",   "Cavity grime", "Occlusion dirt",
                             "Thin rims",   "Height (Y)",   "Facing up",
                             "Perlin noise", "Worley cells", "Bricks"};
    int src = (int)L.gen.source;
    ImGui::SetNextItemWidth(scaled(130.0f));
    if (ImGui::Combo("##gen_src", &src, sources, 9)) {
        L.gen.source = (matbake::MaskParams::Source)src;
        changed = commit = true;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "What drives the mask. Edge wear / cavity grime come from the\n"
            "baked curvature, dirt from AO, rims from thickness, height and\n"
            "facing-up from position/normals; the noises sample 3D noise at\n"
            "the baked surface position, so they continue across UV seams.");
    ImGui::SameLine();
    changed |= ImGui::ColorEdit3("##gen_col", L.genColor,
                                 ImGuiColorEditFlags_NoInputs);
    commit |= ImGui::IsItemDeactivatedAfterEdit();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Mask fill color.");

    ImGui::SetNextItemWidth(scaled(180.0f));
    changed |= ImGui::DragFloatRange2("Range", &L.gen.rangeLo, &L.gen.rangeHi,
                                      0.004f, 0.0f, 1.0f, "%.2f");
    commit |= ImGui::IsItemDeactivatedAfterEdit();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Remap window over the source signal (smoothstep):\n"
                          "narrow = a hard-edged mask, wide = a soft ramp.");
    ImGui::SameLine();
    if (ImGui::Checkbox("Invert", &L.gen.invert)) changed = commit = true;

    const bool noisy = src >= (int)matbake::MaskParams::Source::Perlin;
    if (noisy) {
        ImGui::SetNextItemWidth(scaled(110.0f));
        changed |= ImGui::DragFloat("Scale", &L.gen.scale, 0.05f, 0.5f, 64.0f,
                                    "%.1f");
        commit |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SameLine();
        ImGui::SetNextItemWidth(scaled(80.0f));
        int seed = (int)L.gen.seed;
        if (ImGui::DragInt("Seed", &seed, 0.2f, 0, 9999)) {
            L.gen.seed = (uint32_t)seed;
            changed = commit = true;
        }
        if (src == (int)matbake::MaskParams::Source::Bricks) {
            ImGui::SetNextItemWidth(scaled(110.0f));
            changed |= ImGui::DragFloat("Mortar", &L.gen.mortar, 0.002f, 0.01f,
                                        0.3f, "%.2f");
            commit |= ImGui::IsItemDeactivatedAfterEdit();
        }
    }
    ImGui::SetNextItemWidth(scaled(110.0f));
    changed |= ImGui::SliderFloat("Breakup", &L.gen.breakupAmount, 0.0f, 1.0f,
                                  "%.2f");
    commit |= ImGui::IsItemDeactivatedAfterEdit();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Multiplies the mask by world-space Perlin noise -\n"
                          "organic, uneven wear instead of a uniform coat.");
    if (L.gen.breakupAmount > 0.0f) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(scaled(90.0f));
        changed |= ImGui::DragFloat("##gen_bscale", &L.gen.breakupScale, 0.05f,
                                    0.5f, 64.0f, "x%.1f");
        commit |= ImGui::IsItemDeactivatedAfterEdit();
    }

    if (matBakeMaps_.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f),
                           "Needs baked maps to generate.");
        ImGui::SameLine();
        if (ImGui::SmallButton("Bake maps now")) matBakeRunOnce_ = true;
    }
    if (changed) {
        matEdRegenLayer(L);
        matEdComposite();
    }
    if (commit) matEdSavePaintTarget();
}

// --- material presets: reusable smart-mask stacks ------------------------------
// A preset is the gen-enabled layers' PARAMETERS (never pixels), stored as
// <project>/material-presets/<name>.matpreset - project dir, outside res/,
// so it never ships (the flow-nodes/ pattern). Applying regenerates the
// masks from the current material's own bake.

void App::matEdSavePreset(const std::string& name) {
    std::vector<const MatEdLayer*> gens;
    for (const MatEdLayer& l : matEdLayers_)
        if (l.genOn) gens.push_back(&l);
    if (gens.empty()) {
        matEdPresetError_ = "No smart-mask layers to save.";
        return;
    }
    const std::filesystem::path dir =
        std::filesystem::path(project_.dir) / "material-presets";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    std::ofstream out(dir / (name + ".matpreset"), std::ios::trunc);
    if (!out) {
        matEdPresetError_ = "Cannot write the preset file.";
        return;
    }
    out << "{\n  \"layers\": [\n";
    // The layer name is user-supplied and unbounded, so the line goes straight
    // to the stream - a fixed line buffer truncates mid-JSON on a long name and
    // writes a silently corrupt preset that matEdApplyPreset cannot read back.
    // Only the floats are formatted, each on its own, to keep "%.4g" output.
    auto g4 = [](float v) {
        char b[64];
        std::snprintf(b, sizeof(b), "%.4g", (double)v);
        return std::string(b);
    };
    for (size_t i = 0; i < gens.size(); ++i) {
        const MatEdLayer& l = *gens[i];
        std::string nm = l.name;
        for (char& c : nm)
            if (c == '"' || c == '\\') c = '\'';
        out << "    {\"name\": \"" << nm << "\", \"blend\": " << l.blend
            << ", \"opacity\": " << g4(l.opacity) << ", \"color\": ["
            << g4(l.genColor[0]) << ", " << g4(l.genColor[1]) << ", "
            << g4(l.genColor[2]) << "], \"source\": " << (int)l.gen.source
            << ", \"lo\": " << g4(l.gen.rangeLo)
            << ", \"hi\": " << g4(l.gen.rangeHi)
            << ", \"invert\": " << (l.gen.invert ? "true" : "false")
            << ", \"scale\": " << g4(l.gen.scale) << ", \"seed\": " << l.gen.seed
            << ", \"breakup\": " << g4(l.gen.breakupAmount)
            << ", \"breakupScale\": " << g4(l.gen.breakupScale)
            << ", \"mortar\": " << g4(l.gen.mortar) << "}"
            << (i + 1 < gens.size() ? "," : "") << "\n";
    }
    out << "  ]\n}\n";
    out.flush();
    if (!out) {
        matEdPresetError_ = "Cannot write the preset file.";
        return;
    }
    matEdPresetError_.clear();
    statusMessage_ = "Saved material preset " + name;
}

bool App::matEdApplyPreset(const std::string& fileName) {
    const std::filesystem::path full = std::filesystem::path(project_.dir) /
                                       "material-presets" / fileName;
    std::ifstream in(full);
    if (!in) return false;
    std::stringstream ss;
    ss << in.rdbuf();
    json::Value root;
    if (!json::parse(ss.str(), root)) return false;
    const json::Value* arr = root.find("layers");
    if (!arr || arr->type != json::Value::Type::Array) return false;
    int added = 0;
    for (const json::Value& e : arr->arr) {
        MatEdLayer l;
        l.genOn = true;
        l.name = e.find("name") ? e.find("name")->stringOr("Smart mask")
                                : "Smart mask";
        l.blend = e.find("blend") ? (int)e.find("blend")->numberOr(0) : 0;
        l.opacity =
            e.find("opacity") ? (float)e.find("opacity")->numberOr(1.0) : 1.0f;
        if (const json::Value* c = e.find("color");
            c && c->type == json::Value::Type::Array && c->arr.size() >= 3)
            for (int k = 0; k < 3; ++k)
                l.genColor[k] = (float)c->arr[k].numberOr(0.2);
        auto num = [&](const char* key, float def) {
            const json::Value* v = e.find(key);
            return v ? (float)v->numberOr(def) : def;
        };
        int srcIdx = (int)num("source", 0.0f);
        if (srcIdx < 0 || srcIdx > (int)matbake::MaskParams::Source::Bricks)
            srcIdx = 0;
        l.gen.source = (matbake::MaskParams::Source)srcIdx;
        l.gen.rangeLo = num("lo", 0.35f);
        l.gen.rangeHi = num("hi", 0.75f);
        l.gen.invert = e.find("invert") && e.find("invert")->boolOr(false);
        l.gen.scale = num("scale", 4.0f);
        l.gen.seed = (uint32_t)num("seed", 1.0f);
        l.gen.breakupAmount = num("breakup", 0.0f);
        l.gen.breakupScale = num("breakupScale", 6.0f);
        l.gen.mortar = num("mortar", 0.06f);
        l.pixels.assign((size_t)matEdPaintW_ * matEdPaintH_ * 4, 0);
        matEdRegenLayer(l);
        const int at = (int)matEdLayers_.size();
        matEdLayers_.push_back(std::move(l));
        matEdActiveLayer_ = at;
        matEdPushUndo(MatEdUndoStep::Kind::LayerAdd, at);
        ++added;
    }
    if (!added) return false;
    if (matBakeMaps_.empty()) matBakeRunOnce_ = true;  // masks fill once baked
    matEdComposite();
    matEdSavePaintTarget();
    statusMessage_ = "Applied preset: " + std::to_string(added) + " mask layer(s)";
    return true;
}

// UV validator: the "why does my texture look wrong" list. Runs matbake's
// validateUv over the preview mesh on demand; findings are clickable and
// highlight the offending triangle(s) red in the UV panel and on the mesh.
void App::matEdUvValidateSection(const std::string& entryName) {
    ImGui::SeparatorText("UV check");
    if (ImGui::Button("Validate UVs")) {
        matEdUvIssues_.clear();
        matEdUvIssueTris_.clear();
        matEdUvIssueSel_ = -1;
        if (matBakeBuildMeshes(entryName)) {
            matEdUvIssues_ =
                matbake::validateUv(matBakeMeshLow_, 64 << matBakeSizeIdx_);
            matEdUvIssuesKey_ = matBakeMeshKey_;
            if (!matEdUvIssues_.empty()) matEdUvView_ = true;
        } else {
            matEdUvIssuesKey_.clear();
        }
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "Checks the preview mesh's UVs: overlapping islands (painting\n"
            "one paints the other), UVs outside 0-1, mirrored (flipped) and\n"
            "degenerate triangles, extreme texel-density outliers. Click a\n"
            "finding to highlight it in the UV panel and on the mesh.");

    // --- automatic unwrap: .obj rewrites in place, animated models get a
    // "<model>.uvs" sidecar folded in by animimport (sources can't be
    // rewritten - FBX has no writer)
    const bool unwrapAnimated =
        matEdShape_ == 4 && isAnimatedModelPath(matEdModel_);
    const bool unwrappable = matEdShape_ == 4 && !matEdModel_.empty();
    ImGui::SameLine();
    ImGui::BeginDisabled(!unwrappable);
    if (ImGui::Button("Unwrap UVs...")) openUnwrapPopup_ = true;
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip(
            unwrappable
                ? "Automatic smart-project unwrap of the preview model:\n"
                  "faces cluster into charts by normal, each chart projects\n"
                  "flat, everything packs into 0..1 at uniform texel\n"
                  "density. A static .obj is rewritten in place; an\n"
                  "animated model gets a <model>.uvs sidecar applied at\n"
                  "every load/bake (each part unwraps into its own square)."
                : "Pick a model as the preview mesh first - primitives\n"
                  "have generated UVs.");
    if (openUnwrapPopup_) {
        ImGui::OpenPopup("Unwrap UVs");
        openUnwrapPopup_ = false;
    }
    if (ImGui::BeginPopupModal("Unwrap UVs", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextDisabled("%s", matEdModel_.c_str());
        ImGui::SetNextItemWidth(scaled(200.0f));
        ImGui::SliderFloat("Chart angle", &unwrapAngle_, 15.0f, 89.0f,
                           "%.0f deg");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Faces within this angle of a chart's seed grow into one\n"
                "island. Low = many small flat charts (hard surface),\n"
                "high = few big charts with more distortion (organic).");
        ImGui::SetNextItemWidth(scaled(200.0f));
        ImGui::SliderInt("Margin", &unwrapMargin_, 1, 8, "%d px");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Spacing between charts, in texels at the bake\n"
                              "resolution - keeps bilinear filtering from\n"
                              "bleeding across islands.");
        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f),
                           unwrapAnimated
                               ? "Writes a <model>.uvs sidecar that overrides\n"
                                 "the model's UVs everywhere (previews, bakes,\n"
                                 "the shipped .tskl). Delete the sidecar to\n"
                                 "get the original mapping back."
                               : "Replaces the model's UVs in the .obj file.\n"
                                 "An already-painted texture will no longer line\n"
                                 "up - unwrap BEFORE texturing (or revert with git).");
        if (ImGui::Button("Unwrap", ImVec2(scaled(120), 0))) {
            uvunwrap::Params up;
            up.angleDeg = unwrapAngle_;
            up.marginPx = unwrapMargin_;
            up.marginRefSize = 64 << matBakeSizeIdx_;
            uvunwrap::Stats st;
            std::string err;
            bool ok = false;
            if (unwrapAnimated) {
                // per part into its own 0..1 square (each part carries its
                // own texture); the sidecar must hold the ORIGINAL mapping's
                // replacement, so bake the model fresh without it
                const std::string abs =
                    (std::filesystem::path(project_.dir) / matEdModel_)
                        .string();
                std::error_code fec;
                std::filesystem::remove(abs + ".uvs", fec);
                glbparser::Baked baked;
                if (animimport::bake(abs, 12.0f, baked, err)) {
                    std::vector<std::vector<float>> partUvs(
                        baked.parts.size());
                    int charts = 0;
                    ok = !baked.parts.empty();
                    for (size_t i = 0; i < baked.parts.size() && ok; ++i) {
                        const glbparser::Part& part = baked.parts[i];
                        std::vector<float> corners(
                            part.positions.begin(),
                            part.positions.begin() +
                                (size_t)part.vertexCount * 3);
                        uvunwrap::Stats ps;
                        ok = uvunwrap::unwrapTriangles(corners, partUvs[i],
                                                       up, err, &ps);
                        charts += ps.charts;
                    }
                    if (ok)
                        ok = animimport::writeUvSidecar(abs, baked, partUvs,
                                                        err);
                    st.charts = charts;
                    st.faces = baked.totalVertexCount() / 3;
                    st.coverage = 0.0f;  // per-part squares - not comparable
                }
            } else {
                ok = uvunwrap::unwrapObjFile(
                    (std::filesystem::path(project_.dir) / matEdModel_)
                        .string(),
                    up, err, &st);
            }
            if (ok) {
                // every consumer caches the parsed model - drop them all
                viewport_.invalidateAssets();
                modelInfoCache_.clear();
                matBakeMeshKey_.clear();
                matEdStatsKey_.clear();
                matEdUvIssueTris_.clear();
                matEdUvIssueSel_ = -1;
                // validate the fresh mapping right away and show it
                matEdUvIssues_.clear();
                matEdUvIssuesKey_.clear();
                if (matBakeBuildMeshes(entryName)) {
                    matEdUvIssues_ = matbake::validateUv(
                        matBakeMeshLow_, 64 << matBakeSizeIdx_);
                    matEdUvIssuesKey_ = matBakeMeshKey_;
                }
                matEdUvView_ = true;
                char msg[160];
                if (unwrapAnimated)
                    std::snprintf(msg, sizeof(msg),
                                  "Unwrapped %s: %d charts (sidecar written)",
                                  matEdModel_.c_str(), st.charts);
                else
                    std::snprintf(msg, sizeof(msg),
                                  "Unwrapped %s: %d charts, %.0f%% coverage",
                                  matEdModel_.c_str(), st.charts,
                                  st.coverage * 100.0f);
                statusMessage_ = msg;
                ImGui::CloseCurrentPopup();
            } else {
                statusMessage_ = "Unwrap failed: " + err;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(scaled(120), 0)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (matEdUvIssuesKey_.empty()) return;
    if (matEdUvIssuesKey_ != matBakeMeshKey_) {
        // different mesh/entry since the run - results no longer apply
        matEdUvIssues_.clear();
        matEdUvIssueTris_.clear();
        matEdUvIssuesKey_.clear();
        matEdUvIssueSel_ = -1;
        return;
    }
    if (matEdUvIssues_.empty()) {
        ImGui::TextDisabled("No UV issues found.");
        return;
    }
    int counts[6] = {0, 0, 0, 0, 0, 0};
    for (const matbake::UvIssue& i : matEdUvIssues_) counts[(int)i.kind]++;
    ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f),
                       "%d finding(s): %d overlap, %d out-of-range, %d "
                       "flipped, %d degenerate, %d density",
                       (int)matEdUvIssues_.size(), counts[0], counts[1],
                       counts[2], counts[3], counts[4] + counts[5]);
    const float listH =
        std::min(scaled(150.0f), (matEdUvIssues_.size() + 1) *
                                     ImGui::GetTextLineHeightWithSpacing());
    ImGui::BeginChild("##uv_issues", ImVec2(-FLT_MIN, listH),
                      ImGuiChildFlags_Borders);
    char label[128];
    for (int n = 0; n < (int)matEdUvIssues_.size(); ++n) {
        const matbake::UvIssue& i = matEdUvIssues_[n];
        switch (i.kind) {
            case matbake::UvIssue::Kind::Overlap:
                std::snprintf(label, sizeof(label),
                              "Overlap: tri %d and tri %d share %d texel(s)",
                              i.tri, i.otherTri, (int)i.value);
                break;
            case matbake::UvIssue::Kind::OutOfRange:
                std::snprintf(label, sizeof(label),
                              "Out of 0-1: tri %d (wraps on the PS2)", i.tri);
                break;
            case matbake::UvIssue::Kind::Flipped:
                std::snprintf(label, sizeof(label),
                              "Flipped: tri %d is mirrored in UV", i.tri);
                break;
            case matbake::UvIssue::Kind::Degenerate:
                std::snprintf(label, sizeof(label),
                              "Degenerate: tri %d has no UV area", i.tri);
                break;
            case matbake::UvIssue::Kind::DensityLow:
                std::snprintf(label, sizeof(label),
                              "Low density: tri %d at %.2fx of average",
                              i.tri, i.value);
                break;
            default:
                std::snprintf(label, sizeof(label),
                              "High density: tri %d at %.1fx of average",
                              i.tri, i.value);
                break;
        }
        ImGui::PushID(n);
        if (ImGui::Selectable(label, matEdUvIssueSel_ == n)) {
            matEdUvIssueSel_ = n;
            matEdUvIssueTris_.clear();
            matEdUvIssueTris_.push_back(i.tri);
            if (i.otherTri >= 0) matEdUvIssueTris_.push_back(i.otherTri);
            matEdUvView_ = true;  // the highlight lives there
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
    if (!matEdUvIssueTris_.empty()) {
        if (ImGui::SmallButton("Clear highlight")) {
            matEdUvIssueTris_.clear();
            matEdUvIssueSel_ = -1;
        }
    }
}

void App::matEdUnloadPaintTarget() {
    if (matEdPaintTexRel_.empty() && matEdPaintW_ < 1) return;
    matEdPaintTexRel_.clear();
    matEdPaintPixels_.clear();
    matEdLayers_.clear();
    matEdActiveLayer_ = 0;
    matEdPaintW_ = matEdPaintH_ = 0;
    matEdStroke_ = false;
    matEdHaveLastUV_ = false;
    matEdGhostShown_ = false;
}

bool App::matEdEnsurePaintTexture() {
    if (matEdSel_ < 0 || matEdSel_ >= (int)matEdMats_.size()) return false;
    MatEdEntry& e = matEdMats_[matEdSel_];
    const std::filesystem::path mtlDirAbs =
        (std::filesystem::path(project_.dir) / matEdPath_).parent_path();
    if (e.texture.empty()) {
        // fresh 256^2 white canvas named after the entry
        const std::string base = sanitizeAssetName(e.name + "-tex");
        std::string fileName;
        std::error_code ec;
        for (int n = 1;; ++n) {
            fileName = base + (n == 1 ? "" : "-" + std::to_string(n)) + ".png";
            if (!std::filesystem::exists(mtlDirAbs / fileName, ec)) break;
        }
        std::vector<unsigned char> px((size_t)256 * 256 * 4, 255);
        if (!stbi_write_png((mtlDirAbs / fileName).string().c_str(), 256, 256,
                            4, px.data(), 256 * 4)) {
            statusMessage_ = "Cannot create " + fileName;
            return false;
        }
        matEdPushUndo(MatEdUndoStep::Kind::Props);  // texture assignment
        e.texture = fileName;
        saveMaterialFile();
        statusMessage_ = "Created " + fileName + " for entry " + e.name;
    }
    const std::string texRel =
        (std::filesystem::path(matEdPath_).parent_path() / e.texture)
            .generic_string();
    if (matEdPaintTexRel_ != texRel) {
        if (!matEdLoadPaintTarget(texRel)) {
            statusMessage_ = "Cannot read " + texRel;
            return false;
        }
        matEdComposite();
    }
    return matEdPaintW_ > 0;
}

// The 2D UV-layout panel: the paintable triangles' UV wireframe over the
// entry's live texture, zoom/pan, and the hover sync - a face hovered in 3D
// lights up here, a triangle hovered here is outlined on the mesh (and all
// its UV-overlap twins with it). Validator issues outline red.
void App::drawMatEdUvPanel(const std::string& entryName,
                           const std::string& texRel, const ImVec2& size) {
    ImGui::BeginChild("##mat_uv", size, ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_NoScrollbar |
                          ImGuiWindowFlags_NoScrollWithMouse);
    if (!matBakeBuildMeshes(entryName)) {
        ImGui::TextDisabled("%s", matBakeMeshError_.c_str());
        matEdUvHoverTri_ = -1;
        ImGui::EndChild();
        return;
    }
    const matbake::MeshInput& mesh = matBakeMeshLow_;
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x < 8.0f || avail.y < 8.0f) {
        ImGui::EndChild();
        return;
    }
    ImGui::InvisibleButton("##uv_in", avail,
                           ImGuiButtonFlags_MouseButtonLeft |
                               ImGuiButtonFlags_MouseButtonRight);
    const bool hovered = ImGui::IsItemHovered();

    // uv (0..1) -> screen: a centered square scaled by zoom, plus the pan
    const float base = (avail.x < avail.y ? avail.x : avail.y) * 0.92f;
    float s = base * matEdUvZoom_;
    auto origin = [&]() {
        return ImVec2(p0.x + (avail.x - s) * 0.5f + matEdUvPan_[0],
                      p0.y + (avail.y - s) * 0.5f + matEdUvPan_[1]);
    };
    if (hovered && io.MouseWheel != 0.0f) {
        // zoom around the cursor: the uv under it stays put
        const ImVec2 o = origin();
        const float cu = (io.MousePos.x - o.x) / s;
        const float cv = (io.MousePos.y - o.y) / s;
        matEdUvZoom_ *= std::pow(1.15f, io.MouseWheel);
        matEdUvZoom_ = matEdUvZoom_ < 0.25f ? 0.25f
                       : matEdUvZoom_ > 64.0f ? 64.0f
                                              : matEdUvZoom_;
        s = base * matEdUvZoom_;
        matEdUvPan_[0] = io.MousePos.x - p0.x - (avail.x - s) * 0.5f - cu * s;
        matEdUvPan_[1] = io.MousePos.y - p0.y - (avail.y - s) * 0.5f - cv * s;
    }
    if (ImGui::IsItemActive() &&
        (ImGui::IsMouseDown(0) || ImGui::IsMouseDown(1))) {
        matEdUvPan_[0] += io.MouseDelta.x;
        matEdUvPan_[1] += io.MouseDelta.y;
    }
    const ImVec2 o = origin();
    auto toScreen = [&](float u, float v) {
        return ImVec2(o.x + u * s, o.y + v * s);
    };

    dl->PushClipRect(p0, ImVec2(p0.x + avail.x, p0.y + avail.y), true);
    dl->AddRectFilled(toScreen(0, 0), toScreen(1, 1), IM_COL32(24, 25, 30, 255));
    if (!texRel.empty()) {
        if (const uint32_t tex = viewport_.sharedTexture(texRel))
            dl->AddImage((ImTextureID)(intptr_t)tex, toScreen(0, 0),
                         toScreen(1, 1), ImVec2(0, 0), ImVec2(1, 1),
                         IM_COL32(255, 255, 255, 150));
    }
    dl->AddRect(toScreen(0, 0), toScreen(1, 1), IM_COL32(126, 130, 146, 255));

    // hover uv of the panel cursor
    const float mu = (io.MousePos.x - o.x) / s;
    const float mv = (io.MousePos.y - o.y) / s;
    auto contains = [](float ax, float ay, float bx, float by, float cx,
                       float cy, float px, float py) {
        const float d1 = (px - bx) * (ay - by) - (ax - bx) * (py - by);
        const float d2 = (px - cx) * (by - cy) - (bx - cx) * (py - cy);
        const float d3 = (px - ax) * (cy - ay) - (cx - ax) * (py - ay);
        const bool neg = d1 < 0 || d2 < 0 || d3 < 0;
        const bool pos = d1 > 0 || d2 > 0 || d3 > 0;
        return !(neg && pos);
    };

    const int tris = mesh.triCount();
    int hoverTri = -1;
    const ImU32 wireCol = IM_COL32(255, 196, 96, 150);
    const ImU32 hiliteFill = IM_COL32(255, 196, 96, 90);
    const ImU32 hoverFill = IM_COL32(96, 190, 255, 90);
    // other entries' islands draw dimmed for context - a multi-part model
    // (spider body/legs/jaw...) shows its WHOLE layout, with the selected
    // entry highlighted (switch entries in the combo up top to edit them)
    if (!mesh.paintTri.empty()) {
        const ImU32 dimCol = IM_COL32(140, 144, 158, 60);
        for (int t = 0; t < tris; ++t) {
            if (mesh.paintTri[t]) continue;
            const float* a = &mesh.verts[(size_t)(t * 3 + 0) * 8];
            const float* b = &mesh.verts[(size_t)(t * 3 + 1) * 8];
            const float* c = &mesh.verts[(size_t)(t * 3 + 2) * 8];
            dl->AddTriangle(toScreen(a[6], a[7]), toScreen(b[6], b[7]),
                            toScreen(c[6], c[7]), dimCol);
        }
    }
    for (int t = 0; t < tris; ++t) {
        if (!mesh.paintTri.empty() && !mesh.paintTri[t]) continue;
        const float* a = &mesh.verts[(size_t)(t * 3 + 0) * 8];
        const float* b = &mesh.verts[(size_t)(t * 3 + 1) * 8];
        const float* c = &mesh.verts[(size_t)(t * 3 + 2) * 8];
        const ImVec2 pa = toScreen(a[6], a[7]);
        const ImVec2 pb = toScreen(b[6], b[7]);
        const ImVec2 pc = toScreen(c[6], c[7]);
        // the surface point hovered in 3D lights its UV home(s) up
        if (matEd3dHoverValid_ &&
            contains(a[6], a[7], b[6], b[7], c[6], c[7], matEd3dHoverUV_[0],
                     matEd3dHoverUV_[1]))
            dl->AddTriangleFilled(pa, pb, pc, hiliteFill);
        if (hovered && hoverTri < 0 &&
            contains(a[6], a[7], b[6], b[7], c[6], c[7], mu, mv)) {
            hoverTri = t;
            dl->AddTriangleFilled(pa, pb, pc, hoverFill);
        }
        dl->AddTriangle(pa, pb, pc, wireCol);
    }
    // validator issues on top, red
    for (int t : matEdUvIssueTris_) {
        if (t < 0 || t >= tris) continue;
        const float* a = &mesh.verts[(size_t)(t * 3 + 0) * 8];
        const float* b = &mesh.verts[(size_t)(t * 3 + 1) * 8];
        const float* c = &mesh.verts[(size_t)(t * 3 + 2) * 8];
        dl->AddTriangle(toScreen(a[6], a[7]), toScreen(b[6], b[7]),
                        toScreen(c[6], c[7]), IM_COL32(255, 80, 70, 230),
                        2.0f);
    }
    // the 3D cursor's exact texel
    if (matEd3dHoverValid_) {
        const ImVec2 m = toScreen(matEd3dHoverUV_[0], matEd3dHoverUV_[1]);
        dl->AddCircle(m, 4.0f, IM_COL32(255, 235, 170, 255), 0, 1.5f);
    }
    dl->PopClipRect();
    matEdUvHoverTri_ = hovered ? hoverTri : -1;
    ImGui::EndChild();
}

void App::drawMaterialEditorWindow() {
    if (!showMaterialEditor_ || !hasProject_) {
        matEdFocused_ = false;
        if (matBaker_.running()) matBaker_.cancel();
        matBakeApplyWhenDone_ = false;
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(scaled(1020), scaled(600)),
                             ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Material Editor", &showMaterialEditor_)) {
        matEdFocused_ = false;
        ImGui::End();
        return;
    }
    // Routes Ctrl+Z to this window's own undo stack (see handleShortcuts)
    matEdFocused_ = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

    // --- left: .mtl asset list ----------------------------------------------
    ImGui::BeginChild("##mat_list", ImVec2(scaled(190), 0), ImGuiChildFlags_Borders);
    if (ImGui::Button("+ New material...", ImVec2(-1, 0))) {
        openNewMaterialPopup_ = true;
        matEdNewError_.clear();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Creates a .mtl in res/materials - assign it to any\n"
                          "object in Properties > Material.");
    ImGui::Separator();
    for (const std::string& rel : listMaterialAssets()) {
        if (ImGui::Selectable(rel.substr(4).c_str(), rel == matEdPath_))
            openMaterialEditor(rel);
    }
    if (listMaterialAssets().empty())
        ImGui::TextDisabled("No materials yet.\nA material is a color +\n"
                            "optional texture shared\nby any number of objects.");
    ImGui::EndChild();

    // --- "New material" modal ------------------------------------------------
    if (openNewMaterialPopup_) {
        ImGui::OpenPopup("New material");
        openNewMaterialPopup_ = false;
    }
    if (ImGui::BeginPopupModal("New material", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::SetNextItemWidth(scaled(220.0f));
        ImGui::InputText("Name", matEdNewName_, sizeof(matEdNewName_));
        if (!matEdNewError_.empty())
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "%s",
                               matEdNewError_.c_str());
        if (ImGui::Button("Create", ImVec2(scaled(120), 0))) {
            const std::string base =
                sanitizeAssetName(std::string(matEdNewName_).empty() ? "material"
                                                                     : matEdNewName_);
            const std::string rel = "res/materials/" + base + ".mtl";
            std::error_code ec;
            if (std::filesystem::exists(std::filesystem::path(project_.dir) / rel, ec)) {
                matEdNewError_ = base + ".mtl already exists.";
            } else {
                matEdPath_ = rel;
                matEdMats_.clear();
                MatEdEntry e;
                e.name = base;
                e.color[0] = 0.8f, e.color[1] = 0.8f, e.color[2] = 0.8f;
                matEdMats_.push_back(std::move(e));
                matEdSel_ = 0;
                matEdUndo_.clear();
                matEdPrevMats_ = matEdMats_;
                matEdPaint_ = false;
                matEdPaintTexRel_.clear();
                saveMaterialFile();
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(scaled(120), 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::SameLine();

    if (matEdPath_.empty()) {
        ImGui::BeginChild("##mat_edit");
        ImGui::TextDisabled("Select a material on the left (or create one).");
        ImGui::TextDisabled("\nMaterials are .mtl files under res/ - primitives use the");
        ImGui::TextDisabled("file's first entry, models override their own libraries");
        ImGui::TextDisabled("(usemtl names resolve against it), emitters take the");
        ImGui::TextDisabled("first entry's texture for their particles.");
        ImGui::EndChild();
        ImGui::End();
        return;
    }

    if (matEdSel_ < 0 || matEdSel_ >= (int)matEdMats_.size()) matEdSel_ = 0;
    bool committed = false;

    // --- middle: the selected entry's properties ------------------------------
    // The split between the property column and the preview is a draggable
    // splitter (matEdSplit_ = the preview's share, persisted in editor.ini);
    // both sides keep a workable floor.
    const float totalW = ImGui::GetContentRegionAvail().x;
    if (matEdSplit_ < 0.25f) matEdSplit_ = 0.25f;
    if (matEdSplit_ > 0.75f) matEdSplit_ = 0.75f;
    float previewW = totalW * matEdSplit_;
    const float minPreview = scaled(240.0f);
    const float minProps = scaled(260.0f);
    if (previewW < minPreview) previewW = minPreview;
    if (totalW - previewW < minProps) previewW = totalW - minProps;
    if (previewW < scaled(80.0f)) previewW = scaled(80.0f);  // tiny window
    ImGui::BeginChild("##mat_edit",
                      ImVec2(totalW - previewW - scaled(9.0f), 0));
    ImGui::TextDisabled("%s", matEdPath_.c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Duplicate")) duplicateMaterialAsset();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Copy this .mtl (and its textures) under a new name -\n"
                          "safe to recolor or repaint without touching the original.");
    ImGui::SameLine();
    if (ImGui::SmallButton("Delete..."))
        requestAssetDelete(PendingAssetDelete::Material, matEdPath_,
                           matEdPath_.rfind("res/", 0) == 0 ? matEdPath_.substr(4)
                                                            : matEdPath_);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Remove this .mtl from the project (asks first).\n"
                          "Objects using it fall back to plain color, models\n"
                          "to their own libraries; textures stay on disk.");
    ImGui::SameLine();
    ImGui::BeginDisabled(matEdUndo_.empty());
    if (ImGui::SmallButton("Undo")) matEdUndoLast();
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Ctrl+Z while this window is focused: undoes the\n"
                          "last paint stroke or material edit (the editor's\n"
                          "own history - scene undo is untouched).");

    // entry list within the file (universal libraries hold several; the FIRST
    // one is what primitives and emitters use)
    if (matEdMats_.size() > 1) {
        ImGui::SetNextItemWidth(scaled(180.0f));
        if (ImGui::BeginCombo("Entry", matEdMats_[matEdSel_].name.c_str())) {
            for (int i = 0; i < (int)matEdMats_.size(); ++i) {
                ImGui::PushID(i);
                std::string label = matEdMats_[i].name;
                if (i == 0) label += "  [primitives use this]";
                if (matEdMats_[i].texture.empty()) label += "  (no texture)";
                if (ImGui::Selectable(label.c_str(), i == matEdSel_)) matEdSel_ = i;
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
    }
    if (ImGui::SmallButton("+ Add entry")) {
        MatEdEntry e;
        std::string base = "mat";
        for (int n = 1;; ++n) {
            e.name = base + "-" + std::to_string(n);
            bool taken = false;
            for (const auto& other : matEdMats_) taken |= (other.name == e.name);
            if (!taken) break;
        }
        matEdMats_.push_back(std::move(e));
        matEdSel_ = (int)matEdMats_.size() - 1;
        committed = true;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Another material in the same file. Only universal\n"
                          "model libraries need this - usemtl names resolve\n"
                          "against the file. Primitives always use the first.");
    if (matEdMats_.size() > 1) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove")) {
            matEdMats_.erase(matEdMats_.begin() + matEdSel_);
            if (matEdSel_ >= (int)matEdMats_.size())
                matEdSel_ = (int)matEdMats_.size() - 1;
            committed = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Up") && matEdSel_ > 0) {
            std::swap(matEdMats_[matEdSel_], matEdMats_[matEdSel_ - 1]);
            --matEdSel_;
            committed = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Down") && matEdSel_ + 1 < (int)matEdMats_.size()) {
            std::swap(matEdMats_[matEdSel_], matEdMats_[matEdSel_ + 1]);
            ++matEdSel_;
            committed = true;
        }
    }

    MatEdEntry& e = matEdMats_[matEdSel_];
    ImGui::Separator();

    char nameBuf[64];
    std::snprintf(nameBuf, sizeof(nameBuf), "%s", e.name.c_str());
    ImGui::SetNextItemWidth(scaled(180.0f));
    if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf))) {
        // .mtl names are whitespace-delimited tokens - keep them pipeline-safe
        e.name = sanitizeAssetName(nameBuf);
        if (e.name.empty()) e.name = "material";
    }
    committed |= ImGui::IsItemDeactivatedAfterEdit();

    ImGui::ColorEdit3("Color", e.color);
    committed |= ImGui::IsItemDeactivatedAfterEdit();

    ImGui::SliderFloat("Brightness", &e.brightness, 0.0f, 2.0f, "%.2f");
    committed |= ImGui::IsItemDeactivatedAfterEdit();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Multiplies the color. Above 1.0 a textured material\n"
                          "over-brightens its texture (PS2 modulation goes up\n"
                          "to 2x); plain colors clamp at full white.");

    // texture: PNGs living next to the .mtl (map_Kd resolves relative to it -
    // the game copies that rule)
    const std::filesystem::path mtlDirAbs =
        (std::filesystem::path(project_.dir) / matEdPath_).parent_path();
    {
        const char* noneLabel = "<none - plain color>";
        ImGui::SetNextItemWidth(scaled(240.0f));
        if (ImGui::BeginCombo("Texture",
                              e.texture.empty() ? noneLabel : e.texture.c_str())) {
            if (ImGui::Selectable(noneLabel, e.texture.empty()) && !e.texture.empty()) {
                e.texture.clear();
                committed = true;
            }
            std::error_code ec;
            for (const auto& f :
                 std::filesystem::recursive_directory_iterator(mtlDirAbs, ec)) {
                if (!f.is_regular_file()) continue;
                std::string ext = f.path().extension().string();
                for (char& c : ext) c = (char)tolower((unsigned char)c);
                if (ext != ".png") continue;
                const std::string rel =
                    std::filesystem::relative(f.path(), mtlDirAbs, ec).generic_string();
                if (ImGui::Selectable(rel.c_str(), rel == e.texture) &&
                    rel != e.texture) {
                    e.texture = rel;
                    committed = true;
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Import PNG...")) {
                const std::string src = pickPngFile();
                if (!src.empty()) {
                    const std::string fileName = sanitizeAssetName(
                        std::filesystem::path(src).filename().string());
                    std::error_code cec;
                    std::filesystem::copy_file(
                        src, mtlDirAbs / fileName,
                        std::filesystem::copy_options::overwrite_existing, cec);
                    if (cec) {
                        statusMessage_ = "Texture import failed: " + cec.message();
                    } else {
                        e.texture = fileName;
                        committed = true;
                    }
                }
            }
            if (ImGui::MenuItem("New paintable texture...")) {
                openNewTexturePopup_ = true;
                std::snprintf(matEdNewTexName_, sizeof(matEdNewTexName_), "%s-tex",
                              e.name.c_str());
                matEdNewTexError_.clear();
            }
            ImGui::EndCombo();
        }
    }

    // --- "New texture" modal: a blank pow2 PNG next to the .mtl, assigned as
    // this entry's map_Kd - the canvas for the paint tool.
    if (openNewTexturePopup_) {
        ImGui::OpenPopup("New texture");
        openNewTexturePopup_ = false;
    }
    if (ImGui::BeginPopupModal("New texture", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::SetNextItemWidth(scaled(220.0f));
        ImGui::InputText("Name", matEdNewTexName_, sizeof(matEdNewTexName_));
        const char* sizes[] = {"64 x 64", "128 x 128", "256 x 256", "512 x 512"};
        ImGui::SetNextItemWidth(scaled(220.0f));
        ImGui::Combo("Size", &matEdNewTexSize_, sizes, 4);
        ImGui::TextDisabled("Power-of-two, as the PS2 GS requires. Bigger eats\n"
                            "video memory - 256 is plenty for most props.");
        ImGui::ColorEdit3("Fill color", matEdNewTexColor_);
        if (!matEdNewTexError_.empty())
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "%s",
                               matEdNewTexError_.c_str());
        if (ImGui::Button("Create", ImVec2(scaled(120), 0))) {
            const std::string base = sanitizeAssetName(
                std::string(matEdNewTexName_).empty() ? "painted" : matEdNewTexName_);
            const std::string fileName = base + ".png";
            std::error_code cec;
            if (std::filesystem::exists(mtlDirAbs / fileName, cec)) {
                matEdNewTexError_ = fileName + " already exists.";
            } else {
                const int s = 64 << (matEdNewTexSize_ < 0   ? 0
                                     : matEdNewTexSize_ > 3 ? 3
                                                            : matEdNewTexSize_);
                std::vector<unsigned char> px((size_t)s * s * 4);
                for (size_t i = 0; i < px.size(); i += 4) {
                    px[i] = (unsigned char)(matEdNewTexColor_[0] * 255.0f + 0.5f);
                    px[i + 1] = (unsigned char)(matEdNewTexColor_[1] * 255.0f + 0.5f);
                    px[i + 2] = (unsigned char)(matEdNewTexColor_[2] * 255.0f + 0.5f);
                    px[i + 3] = 255;
                }
                if (stbi_write_png((mtlDirAbs / fileName).string().c_str(), s, s, 4,
                                   px.data(), s * 4)) {
                    e.texture = fileName;
                    committed = true;
                    matEdPaint_ = true;  // the whole point of a blank canvas
                    ImGui::CloseCurrentPopup();
                } else {
                    matEdNewTexError_ = "Cannot write " + fileName;
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(scaled(120), 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (!e.texture.empty()) {
        const std::filesystem::path texAbs = mtlDirAbs / e.texture;
        std::error_code ec;
        if (!std::filesystem::exists(texAbs, ec)) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f),
                               "Texture missing - renders as plain color.");
        } else {
            int tw = 0, th = 0, comp = 0;
            if (stbi_info(texAbs.string().c_str(), &tw, &th, &comp)) {
                const bool pow2 = tw > 0 && th > 0 && (tw & (tw - 1)) == 0 &&
                                  (th & (th - 1)) == 0;
                if (!pow2)
                    ImGui::TextColored(
                        ImVec4(1.0f, 0.7f, 0.3f, 1.0f),
                        "%dx%d - not power-of-two; the PS2 GS needs\n"
                        "pow2 texture sizes (e.g. 128x128, 256x256).", tw, th);
                else
                    ImGui::TextDisabled("%s", matEdBudgetLine(tw, th).c_str());
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(
                        "What this texture costs after the build's texture\n"
                        "bake, at the quality this material resolves to\n"
                        "(per-asset override, else the project preference).\n"
                        "The GS adds ~8 KB of allocation overhead per\n"
                        "resident texture on top.");
            }
        }

        // Tiling (map_Kd -s): how densely the texture repeats. Used by terrain
        // (which generates its own UVs); objects carry baked UVs and ignore it.
        ImGui::SetNextItemWidth(scaled(180.0f));
        ImGui::DragFloat("Tile repeat", &e.tile, 0.05f, 0.01f, 64.0f, "%.2f/unit");
        committed |= ImGui::IsItemDeactivatedAfterEdit();
        if (e.tile < 0.01f) e.tile = 0.01f;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Terrain only: texture repeats per world unit.\n"
                              "Higher = smaller, denser tiles. Objects use their\n"
                              "mesh UVs and ignore this.");
    }

    // --- Reflection (refl): spherical environment map, drawn as a second
    // additive pass on the PS2 - the NFS/GT-style "chrome/lacquer" look. The
    // sphere map is a small PNG of the surroundings; UVs come from the
    // camera-space normals, so the highlight slides over the surface as the
    // camera moves.
    ImGui::SeparatorText("Reflection");
    {
        const char* noneLabel = "<none - matte>";
        ImGui::SetNextItemWidth(scaled(240.0f));
        const char* dynLabel = "<dynamic - live sky>";
        if (ImGui::BeginCombo("Sphere map",
                              e.refl.empty()  ? noneLabel
                              : e.refl == "@sky" ? dynLabel
                                                 : e.refl.c_str())) {
            if (ImGui::Selectable(noneLabel, e.refl.empty()) && !e.refl.empty()) {
                e.refl.clear();
                committed = true;
            }
            // GT3-style dynamic env map: the game re-renders the scene's sky
            // dome into a small VRAM texture every frame - reflections track
            // the live sky (script retints included). Stored as "@sky".
            if (ImGui::Selectable(dynLabel, e.refl == "@sky") &&
                e.refl != "@sky") {
                e.refl = "@sky";
                committed = true;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("The game renders the scene's sky into the\n"
                                  "sphere map every frame - reflections follow\n"
                                  "the live sky, including script retints.");
            std::error_code ec;
            for (const auto& f :
                 std::filesystem::recursive_directory_iterator(mtlDirAbs, ec)) {
                if (!f.is_regular_file()) continue;
                std::string ext = f.path().extension().string();
                for (char& c : ext) c = (char)tolower((unsigned char)c);
                if (ext != ".png") continue;
                const std::string rel =
                    std::filesystem::relative(f.path(), mtlDirAbs, ec).generic_string();
                if (ImGui::Selectable(rel.c_str(), rel == e.refl) &&
                    rel != e.refl) {
                    e.refl = rel;
                    committed = true;
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Import PNG...")) {
                const std::string src = pickPngFile();
                if (!src.empty()) {
                    const std::string fileName = sanitizeAssetName(
                        std::filesystem::path(src).filename().string());
                    std::error_code cec;
                    std::filesystem::copy_file(
                        src, mtlDirAbs / fileName,
                        std::filesystem::copy_options::overwrite_existing, cec);
                    if (cec) {
                        statusMessage_ = "Sphere map import failed: " + cec.message();
                    } else {
                        e.refl = fileName;
                        committed = true;
                    }
                }
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("A small texture of the environment (sky gradient\n"
                              "with a bright horizon works great). Drawn additively\n"
                              "over the material using camera-space normals -\n"
                              "the PS2-era chrome/car-paint trick.");
        if (!e.refl.empty()) {
            std::error_code ec;
            if (e.refl != "@sky" &&
                !std::filesystem::exists(mtlDirAbs / e.refl, ec))
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f),
                                   "Sphere map missing - reflection is skipped.");
            ImGui::SetNextItemWidth(scaled(180.0f));
            ImGui::SliderFloat("Strength", &e.reflStrength, 0.05f, 1.0f, "%.2f");
            committed |= ImGui::IsItemDeactivatedAfterEdit();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("How much of the sphere map is added on top.\n"
                                  "1.0 = full chrome.");
            committed |= ImGui::Checkbox("Rounded normals", &e.reflRounded);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Reflection UVs from normals radiating out of the object's\n"
                    "center instead of the real face normals. A flat face shows\n"
                    "a gradient of the map (curved-lacquer look) instead of one\n"
                    "uniform sample - flat walls of boxes/monoliths reflect like\n"
                    "the spheres do. Curved shapes barely change.");
        }
    }

    // --- Glow (Ke): emission. Not a light - a brightness FLOOR baked into the
    // vertex colors, so the surface keeps its own color in a pitch-black scene
    // (docs/emissive-materials.md). Pair it with the bloom threshold in the UI
    // Editor screen stack to get the halo around it.
    ImGui::SeparatorText("Glow (emissive)");
    {
        ImGui::SetNextItemWidth(scaled(180.0f));
        const float before = e.glow;
        ImGui::SliderFloat("Glow", &e.glow, 0.0f, 2.0f,
                           e.glow <= 0.0f ? "off" : "%.2f");
        // Raising it the first time: start from the material's own color, which
        // is what "it glows in its own color" means to the eye.
        if (before <= 0.0f && e.glow > 0.0f)
            for (int i = 0; i < 3; ++i) e.glowColor[i] = e.color[i];
        committed |= ImGui::IsItemDeactivatedAfterEdit();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Self-illumination. The surface never renders darker than\n"
                "glow color x this - at 1.0 it shows its full color even in\n"
                "complete darkness, ignoring the sun, point lights and baked\n"
                "AO. Above 1.0 only bites on textured materials (the PS2\n"
                "color byte modulates the texture up to 2x).");
        if (e.glow > 0.0f || e.glowWhite > 0.0f) {
            ImGui::ColorEdit3("Glow color", e.glowColor);
            committed |= ImGui::IsItemDeactivatedAfterEdit();
            if (ImGui::Button("Match material color", ImVec2(scaled(180), 0))) {
                for (int i = 0; i < 3; ++i) e.glowColor[i] = e.color[i];
                committed = true;
            }

            // The one control that can make an ALREADY saturated surface read
            // brighter: an untextured emitter is at the framebuffer maximum in
            // its own hue at glow 1, so "more" can only mean "whiter".
            ImGui::SetNextItemWidth(scaled(180.0f));
            ImGui::SliderFloat("White-hot core", &e.glowWhite, 0.0f, 1.0f,
                               e.glowWhite <= 0.0f ? "off" : "%.2f");
            committed |= ImGui::IsItemDeactivatedAfterEdit();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Blows the surface out toward white, the way an\n"
                    "overexposed emitter looks on camera. This is what makes\n"
                    "a glow read as HOT: at full strength a colored surface\n"
                    "is already at the maximum the framebuffer can hold in\n"
                    "its own hue, so the only way up is desaturating. It also\n"
                    "pushes every channel over the bloom threshold, which\n"
                    "widens and brightens the halo.");

            // Step 2: the emitter bakes light into the geometry around it.
            bool lights = e.glowRange > 0.0f;
            if (ImGui::Checkbox("Lights up surroundings", &lights)) {
                e.glowRange = lights ? 4.0f : 0.0f;
                committed = true;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Bakes this material's light into the walls, floor and\n"
                    "props around it at scene load - the ambient-occlusion\n"
                    "treatment, in reverse. Free at runtime (it lands in the\n"
                    "same vertex colors), and the editor viewport previews it.\n"
                    "Static: the pool of light does not follow a moving\n"
                    "object, and animated models do not receive it.");
            if (e.glowRange > 0.0f) {
                ImGui::SetNextItemWidth(scaled(180.0f));
                ImGui::DragFloat("Light reach", &e.glowRange, 0.1f, 0.2f, 60.0f,
                                 "%.1f units");
                committed |= ImGui::IsItemDeactivatedAfterEdit();
                ImGui::SetNextItemWidth(scaled(180.0f));
                ImGui::SliderFloat("Light strength", &e.glowLight, 0.0f, 3.0f,
                                   "%.2f");
                committed |= ImGui::IsItemDeactivatedAfterEdit();
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(
                        "How bright the pool of light is at the emitter's\n"
                        "surface. The light color is the glow color (white-hot\n"
                        "core included), the falloff is quadratic to the reach.");
            }

            ImGui::TextDisabled(
                "Baked into the vertex colors at scene load - free on the\n"
                "console.");
            // The halo is the bloom's job, and a glow with bloom off is the
            // single most common "why doesn't it glow?" - say so where the
            // slider is, not only in the docs.
            if (project_.settings.bloom <= 0.0f)
                ImGui::TextColored(
                    ImVec4(1.0f, 0.7f, 0.3f, 1.0f),
                    "No halo: Bloom is 0. Raise Bloom (and its Threshold)\n"
                    "in UI Editor > the screen stack > Bloom.");
            else if (project_.settings.bloomThreshold <= 0.0f)
                ImGui::TextDisabled(
                    "Bloom has no Threshold - the halo spreads over the whole\n"
                    "frame. Raise it in UI Editor > Bloom to focus the glow.");
            else
                ImGui::TextDisabled(
                    "Halo: Bloom %.2f, threshold %.2f, spread %.2f\n"
                    "(UI Editor > the screen stack > Bloom).",
                    project_.settings.bloom, project_.settings.bloomThreshold,
                    project_.settings.bloomSpread);
        }
    }

    // --- Bake maps (docs/material-baking.md) ---------------------------------
    matEdBakeSection(e.name,
                     e.texture.empty()
                         ? ""
                         : (std::filesystem::path(matEdPath_).parent_path() /
                            e.texture)
                               .generic_string());

    // --- UV validator ---------------------------------------------------------
    matEdUvValidateSection(e.name);

    ImGui::Spacing();
    ImGui::TextDisabled("Saved to the file on every change. The object's own\n"
                        "Color multiplies on top per object.");
    ImGui::EndChild();

    // --- splitter: drag to trade property-column width for preview width -----
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::InvisibleButton("##mat_split", ImVec2(scaled(9.0f), -1.0f));
    if (ImGui::IsItemHovered() || ImGui::IsItemActive())
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    if (ImGui::IsItemActive() && totalW > minPreview + minProps) {
        previewW -= ImGui::GetIO().MouseDelta.x;
        if (previewW < minPreview) previewW = minPreview;
        if (previewW > totalW - minProps) previewW = totalW - minProps;
        matEdSplit_ = previewW / totalW;
        if (matEdSplit_ < 0.25f) matEdSplit_ = 0.25f;
        if (matEdSplit_ > 0.75f) matEdSplit_ = 0.75f;
    }
    if (ImGui::IsItemDeactivated()) saveGlobalConfig();  // persist the split
    {
        const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
        const float cx = (mn.x + mx.x) * 0.5f;
        ImGui::GetWindowDrawList()->AddLine(
            ImVec2(cx, mn.y + scaled(4.0f)), ImVec2(cx, mx.y - scaled(4.0f)),
            ImGui::GetColorU32(ImGui::IsItemActive()   ? ImGuiCol_SeparatorActive
                               : ImGui::IsItemHovered() ? ImGuiCol_SeparatorHovered
                                                        : ImGuiCol_Separator),
            scaled(2.0f));
    }
    ImGui::SameLine(0.0f, 0.0f);

    // --- right: live preview + paint tool ---------------------------------------
    ImGui::BeginChild("##mat_preview", ImVec2(previewW, 0));  // previewW already scaled
    {
        ImGuiIO& io = ImGui::GetIO();
        const MatEdEntry& sel = matEdMats_[matEdSel_];

        // Shape: the four unit primitives or any of the project's .obj models
        const char* shapes[] = {"Box", "Sphere", "Cylinder", "Cone"};
        const std::string shapeLabel =
            matEdShape_ == 4
                ? std::filesystem::path(matEdModel_).filename().string()
                : shapes[matEdShape_ < 0 || matEdShape_ > 3 ? 1 : matEdShape_];
        ImGui::SetNextItemWidth(scaled(110.0f));
        if (ImGui::BeginCombo("##mat_shape", shapeLabel.c_str())) {
            for (int i = 0; i < 4; ++i)
                if (ImGui::Selectable(shapes[i], matEdShape_ == i)) matEdShape_ = i;
            const std::vector<std::string> models = listAssetFiles("models", ".obj");
            const std::vector<std::string> anim = listAnimatedModelFiles();
            if (!models.empty() || !anim.empty()) ImGui::Separator();
            for (const std::string& m : models) {
                const std::string rel = "res/models/" + m;
                if (ImGui::Selectable(m.c_str(),
                                      matEdShape_ == 4 && matEdModel_ == rel)) {
                    matEdShape_ = 4;
                    matEdModel_ = rel;
                }
            }
            for (const std::string& m : anim) {
                const std::string rel = "res/models/" + m;
                if (ImGui::Selectable((m + " (animated)").c_str(),
                                      matEdShape_ == 4 && matEdModel_ == rel)) {
                    matEdShape_ = 4;
                    matEdModel_ = rel;
                }
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Preview mesh. Pick one of your models (static or\n"
                              "animated) to see the material (and paint) on the real\n"
                              "thing - this file acts as the model's material\n"
                              "override, entries matched to the model's material names.");
        ImGui::SameLine();
        ImGui::Checkbox("Spin", &matEdSpin_);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Turntable. Grabbing the preview unchecks it -\n"
                              "your framing stays put; re-tick to resume.");
        if (matEdSpin_ && !matEdPaint_) matEdAngle_ += io.DeltaTime * 24.0f;
        ImGui::SameLine();
        ImGui::SetNextItemWidth(scaled(100.0f));
        if (ImGui::Combo("##mat_display", &matEdDisplayMode_,
                         "Solid\0Wireframe\0UV checker\0PS2 CLUT\0"))
            matEdUploadComposite();  // apply / drop the CLUT quantization
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Preview shading: the material as-is, a dark wireframe\n"
                "overlay, a generated UV checker in place of every texture\n"
                "(stretch and texel density read at a glance), or the PS2\n"
                "CLUT look - the texture palette-quantized exactly like the\n"
                "shipped bake, with a dithering choice and the live memory\n"
                "budget.");
        ImGui::SameLine();
        ImGui::Checkbox("UV", &matEdUvView_);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "UV layout panel under the preview: the entry's faces over\n"
                "its texture. Wheel zooms, drag pans. Hover a face in 3D to\n"
                "light up its texture region; hover a triangle in the panel\n"
                "to outline it on the mesh.");

        // preview-mesh stats (import sanity: sizes, UV presence)
        if (matBakeBuildMeshes(sel.name)) {
            if (matEdStatsKey_ != matBakeMeshKey_) {
                matEdStatsKey_ = matBakeMeshKey_;
                const matbake::MeshInput& mm = matBakeMeshLow_;
                int painted = 0;
                for (char c : mm.paintTri) painted += c ? 1 : 0;
                if (mm.paintTri.empty()) painted = mm.triCount();
                bool hasUv = false;
                for (size_t i = 0; i + 7 < mm.verts.size() && !hasUv; i += 8)
                    hasUv = mm.verts[i + 6] != 0.0f || mm.verts[i + 7] != 0.0f;
                int verts = 0;
                for (int pi : mm.posIdx) verts = std::max(verts, pi + 1);
                char sb[128];
                if (verts > 0)
                    std::snprintf(sb, sizeof(sb),
                                  "%d tris (%d on this entry) - %d verts",
                                  mm.triCount(), painted, verts);
                else
                    std::snprintf(sb, sizeof(sb), "%d tris (%d on this entry)",
                                  mm.triCount(), painted);
                matEdStatsLine_ = sb;
                matEdStatsWarn_ = !hasUv || painted == 0;
                if (!hasUv)
                    matEdStatsLine_ += " - no UVs (paint/bake need them)";
                else if (painted == 0)
                    matEdStatsLine_ +=
                        " - no faces use this entry (check usemtl names)";
            }
            if (matEdStatsWarn_)
                ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "%s",
                                   matEdStatsLine_.c_str());
            else
                ImGui::TextDisabled("%s", matEdStatsLine_.c_str());
        }

        // Staged values of the selected entry (live during slider drags)
        float kd[3];
        for (int i = 0; i < 3; ++i) {
            kd[i] = sel.color[i] * sel.brightness;
            if (kd[i] > 1.99f) kd[i] = 1.99f;
        }
        const std::string texRel =
            sel.texture.empty()
                ? ""
                : (std::filesystem::path(matEdPath_).parent_path() / sel.texture)
                      .generic_string();
        const bool reflSky = sel.refl == "@sky";
        const std::string reflRel =
            (sel.refl.empty() || reflSky)
                ? ""
                : (std::filesystem::path(matEdPath_).parent_path() / sel.refl)
                      .generic_string();

        // progressive map bake: restart on changed inputs, poll snapshots,
        // finish a pending "Bake & add layer"
        matBakeTick(sel.name, texRel);

        // --- "PS2 CLUT" display mode -----------------------------------------
        if (matEdDisplayMode_ == 3) {
            if (!texRel.empty() && matEdPaintTexRel_ != texRel) {
                // the quantization rides the paint target's GL upload
                if (matEdLoadPaintTarget(texRel)) matEdComposite();
            }
            if (texRel.empty()) {
                ImGui::TextDisabled("PS2 CLUT: plain colors have no texture\n"
                                    "to quantize - assign one to see it.");
            } else {
                const char* palModes[] = {"Project policy", "16 colors",
                                          "256 colors", "Full color"};
                ImGui::SetNextItemWidth(scaled(120.0f));
                bool changed = ImGui::Combo("##ps2_pal", &matEdPs2Mode_,
                                            palModes, 4);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(
                        "Palette budget. \"Project policy\" resolves what\n"
                        "texbake will actually ship for this material\n"
                        "(its per-asset override, else the project's\n"
                        "texture quantization preference).");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(scaled(130.0f));
                changed |= ImGui::Combo("##ps2_dither", &matEdPs2Dither_,
                                        "Floyd-Steinberg\0Ordered (Bayer)\0"
                                        "No dithering\0");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(
                        "Floyd-Steinberg is what the shipped bake uses;\n"
                        "Ordered trades noise for a stable pattern; None\n"
                        "shows the raw banding.");
                if (changed) matEdUploadComposite();
                if (matEdPaintW_ > 0)
                    ImGui::TextDisabled(
                        "%s", matEdBudgetLine(matEdPaintW_, matEdPaintH_).c_str());
                // palette swatch strip (what survived the median cut)
                if (!matEdPs2Palette_.empty()) {
                    const int n = (int)(matEdPs2Palette_.size() / 4);
                    const float cell = scaled(n > 64 ? 7.0f : 12.0f);
                    const int perRow = n > 64 ? 32 : 16;
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    const ImVec2 at = ImGui::GetCursorScreenPos();
                    for (int i = 0; i < n; ++i) {
                        const unsigned char* p = &matEdPs2Palette_[(size_t)i * 4];
                        const ImVec2 a(at.x + (i % perRow) * cell,
                                       at.y + (i / perRow) * cell);
                        dl->AddRectFilled(
                            a, ImVec2(a.x + cell - 1.0f, a.y + cell - 1.0f),
                            IM_COL32(p[0], p[1], p[2], 255));
                    }
                    ImGui::Dummy(ImVec2(perRow * cell,
                                        ((n + perRow - 1) / perRow) * cell));
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("The %d-entry palette the median cut\n"
                                          "settled on.", n);
                }
            }
        }

        // --- paint tool ------------------------------------------------------
        if (ImGui::Checkbox("Paint", &matEdPaint_) && matEdPaint_)
            matEdPaintTexRel_.clear();  // force the target check below
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Paint on the preview mesh, straight into this\n"
                              "entry's texture (through the UVs). The PNG is\n"
                              "saved on every stroke - what you paint is what\n"
                              "the PS2 loads. The layer stack below is always\n"
                              "visible; Paint only arms the brush.");
        if (matEdPaint_) {
            ImGui::SameLine();
            ImGui::Checkbox("Live dab", &matEdGhostOn_);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Preview the stamp under the cursor before\n"
                                  "clicking - one uncommitted dab drawn on the\n"
                                  "preview each frame.");
        }
        // The layer stack lives on the entry's texture and is shown whenever
        // one is loaded - Paint only gates the brush (the "babranie").
        bool haveTarget = false;
        if (!texRel.empty()) {
            if (matEdPaintTexRel_ != texRel && !matEdLoadPaintTarget(texRel))
                statusMessage_ = "Cannot read " + texRel;
            haveTarget = matEdPaintW_ > 0;
        }
        bool canPaint = matEdPaint_ && haveTarget;
        if (matEdPaint_) {
            if (texRel.empty())
                ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f),
                                   "No texture on this entry.\n"
                                   "Texture > New paintable texture...");
            else if (!haveTarget)
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f),
                                   "Texture file unreadable.");
            if (canPaint && (matBakePreviewMode_ == 2 || matEdDisplayMode_ == 2)) {
                ImGui::TextDisabled("Painting paused - the preview is not\n"
                                    "showing the texture (map view/checker).");
                canPaint = false;
            }
        }
        if (canPaint) {

            ImGui::SetNextItemWidth(scaled(90.0f));
            ImGui::Combo("##brush_mode", &matEdBrushMode_,
                         "Color\0Brush\0Eraser\0");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Color: solid paint. Brush: paints with a\n"
                                  "project brush image (res/brushes), tiled\n"
                                  "across the texture. Eraser: takes paint\n"
                                  "off the active layer.");
            if (matEdBrushMode_ == 0) {
                ImGui::SameLine();
                ImGui::ColorEdit3("##brush_col", matEdBrushColor_,
                                  ImGuiColorEditFlags_NoInputs);
            } else if (matEdBrushMode_ == 1) {
                ImGui::SameLine();
                // Brushes are project-global assets: res/brushes/*.png
                const std::string brushLabel =
                    matEdBrush_.empty()
                        ? "<pick brush>"
                        : std::filesystem::path(matEdBrush_).filename().string();
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::BeginCombo("##brush_pick", brushLabel.c_str())) {
                    for (const std::string& b : listAssetFiles("brushes", ".png")) {
                        const std::string rel = "res/brushes/" + b;
                        if (ImGui::Selectable(b.c_str(), rel == matEdBrush_))
                            matEdBrush_ = rel;
                    }
                    if (listAssetFiles("brushes", ".png").empty())
                        ImGui::TextDisabled("No brushes yet - import one below.");
                    ImGui::Separator();
                    if (ImGui::MenuItem("Import brush from PNG...")) {
                        const std::string src = pickPngFile();
                        if (!src.empty()) {
                            const std::string fileName = sanitizeAssetName(
                                std::filesystem::path(src).filename().string());
                            const std::filesystem::path dirAbs =
                                std::filesystem::path(project_.dir) / "res" /
                                "brushes";
                            std::error_code cec;
                            std::filesystem::create_directories(dirAbs, cec);
                            std::filesystem::copy_file(
                                src, dirAbs / fileName,
                                std::filesystem::copy_options::overwrite_existing,
                                cec);
                            if (cec) {
                                statusMessage_ =
                                    "Brush import failed: " + cec.message();
                            } else {
                                matEdBrush_ = "res/brushes/" + fileName;
                                statusMessage_ = "Imported brush " + fileName;
                            }
                        }
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip(
                            "Copies the PNG into res/brushes - brushes are\n"
                            "shared by the whole project and never ship\n"
                            "with the game.");
                    ImGui::EndCombo();
                }
            }
            ImGui::SetNextItemWidth(scaled(110.0f));
            ImGui::SliderFloat("Size", &matEdBrushSize_, 1.0f, 128.0f, "%.0f px",
                               ImGuiSliderFlags_Logarithmic);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(scaled(110.0f));
            ImGui::SliderFloat("Opacity", &matEdBrushOpacity_, 0.05f, 1.0f,
                               "%.2f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("How strongly each dab covers what is\n"
                                  "underneath (per dab, not per stroke).");
            ImGui::SetNextItemWidth(scaled(110.0f));
            ImGui::SliderFloat("Spacing", &matEdBrushSpacing_, 5.0f, 300.0f,
                               "%.0f%%", ImGuiSliderFlags_Logarithmic);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Distance between dabs, as %% of the brush size\n"
                    "(GIMP-style). Low = one continuous line, 100%%\n"
                    "and up = clearly separated stamps.");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(scaled(110.0f));
            ImGui::SliderFloat("Vary", &matEdBrushOpacityVary_, 0.0f, 100.0f,
                               "%.0f%%");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Random per-dab opacity variation: each dab's\n"
                    "opacity is reduced by up to this much - organic,\n"
                    "hand-worn strokes instead of a uniform coat.");
            if (matEdBrushMode_ == 1) {
                // dab orientation: dial bricks in by hand, or scatter organic
                // splats with a fresh random rotation per dab
                ImGui::SetNextItemWidth(scaled(110.0f));
                ImGui::BeginDisabled(matEdBrushRandomRot_);
                ImGui::SliderFloat("Angle", &matEdBrushAngle_, 0.0f, 360.0f,
                                   "%.0f deg");
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Rotates every dab - orient bricks,\n"
                                      "planks, arrows...");
                ImGui::SameLine();
                ImGui::Checkbox("Random", &matEdBrushRandomRot_);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Fresh random rotation per dab - organic\n"
                                      "scatter (leaves, splats, rubble).");
                // (re)decode the brush image when the pick changes
                if (matEdBrush_ != matEdPatternLoaded_) {
                    matEdPatternLoaded_ = matEdBrush_;
                    matEdPatternPixels_.clear();
                    matEdPatternW_ = matEdPatternH_ = 0;
                    int w = 0, h = 0, comp = 0;
                    unsigned char* p = stbi_load(
                        (std::filesystem::path(project_.dir) / matEdBrush_)
                            .string()
                            .c_str(),
                        &w, &h, &comp, 4);
                    if (p) {
                        matEdPatternPixels_.assign(p, p + (size_t)w * h * 4);
                        matEdPatternW_ = w;
                        matEdPatternH_ = h;
                        stbi_image_free(p);
                    }
                }
                if (!matEdBrush_.empty() && matEdPatternW_ == 0)
                    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f),
                                       "Brush unreadable.");
            }
        }

        // --- layers: painted strokes land on the active (selected) layer;
        // the composite of the stack is the PNG that ships. Top-most first.
        // Shown whenever the entry's texture is loaded - stack edits, smart
        // masks and presets work with the brush disarmed.
        if (haveTarget) {
            ImGui::Spacing();
            ImGui::TextDisabled("Layers");
            ImGui::SameLine();
            if (ImGui::SmallButton("+##layer_add")) {
                MatEdLayer l;
                int n = 1;
                for (const MatEdLayer& other : matEdLayers_)
                    if (other.name.rfind("Layer ", 0) == 0) ++n;
                l.name = "Layer " + std::to_string(n);
                l.pixels.assign((size_t)matEdPaintW_ * matEdPaintH_ * 4, 0);
                const int at = matEdActiveLayer_ + 1;
                matEdLayers_.insert(matEdLayers_.begin() + at, std::move(l));
                matEdActiveLayer_ = at;
                matEdPushUndo(MatEdUndoStep::Kind::LayerAdd, at);
                matEdSaveLayers();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("New transparent layer above the active one.");
            ImGui::SameLine();
            ImGui::BeginDisabled(matEdActiveLayer_ == 0);
            if (ImGui::SmallButton("-##layer_del") && matEdActiveLayer_ > 0) {
                matEdPushUndo(MatEdUndoStep::Kind::LayerRemove, matEdActiveLayer_,
                              &matEdLayers_[matEdActiveLayer_]);
                matEdLayers_.erase(matEdLayers_.begin() + matEdActiveLayer_);
                if (matEdActiveLayer_ >= (int)matEdLayers_.size())
                    matEdActiveLayer_ = (int)matEdLayers_.size() - 1;
                matEdComposite();
                matEdSavePaintTarget();
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Delete the active layer (Background stays;\n"
                                  "undo with Ctrl+Z).");
            ImGui::SameLine();
            ImGui::BeginDisabled(matEdActiveLayer_ + 1 >= (int)matEdLayers_.size());
            if (ImGui::SmallButton("Up##layer_up")) {
                std::swap(matEdLayers_[matEdActiveLayer_],
                          matEdLayers_[matEdActiveLayer_ + 1]);
                ++matEdActiveLayer_;
                matEdComposite();
                matEdSavePaintTarget();
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(matEdActiveLayer_ <= 1);
            if (ImGui::SmallButton("Down##layer_dn")) {
                std::swap(matEdLayers_[matEdActiveLayer_],
                          matEdLayers_[matEdActiveLayer_ - 1]);
                --matEdActiveLayer_;
                matEdComposite();
                matEdSavePaintTarget();
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::SmallButton("+ Mask")) {
                MatEdLayer l;
                l.name = "Smart mask";
                l.genOn = true;
                l.pixels.assign((size_t)matEdPaintW_ * matEdPaintH_ * 4, 0);
                matEdRegenLayer(l);
                const int at = matEdActiveLayer_ + 1;
                matEdLayers_.insert(matEdLayers_.begin() + at, std::move(l));
                matEdActiveLayer_ = at;
                matEdPushUndo(MatEdUndoStep::Kind::LayerAdd, at);
                if (matBakeMaps_.empty()) matBakeRunOnce_ = true;
                matEdComposite();
                matEdSavePaintTarget();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Procedural mask layer driven by the baked maps: wear on\n"
                    "edges, grime in cavities, dirt in occlusion, height\n"
                    "streaks, world-space noise, bricks... Select it to tune\n"
                    "the generator below; it re-generates live as the bake\n"
                    "refines (hand strokes on it are overwritten).");
            ImGui::SameLine();
            if (ImGui::SmallButton("Presets")) ImGui::OpenPopup("##mat_presets");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Reusable smart-mask stacks (material-presets/ in the\n"
                    "project). Apply one to another asset and the masks\n"
                    "regenerate from THAT model's own bake.");
            if (ImGui::BeginPopup("##mat_presets")) {
                const std::filesystem::path pdir =
                    std::filesystem::path(project_.dir) / "material-presets";
                std::error_code ec;
                bool anyPreset = false;
                if (std::filesystem::exists(pdir, ec))
                    for (const auto& f :
                         std::filesystem::directory_iterator(pdir, ec)) {
                        if (!f.is_regular_file() ||
                            f.path().extension() != ".matpreset")
                            continue;
                        anyPreset = true;
                        const std::string stem = f.path().stem().string();
                        if (ImGui::MenuItem(stem.c_str())) {
                            if (!matEdApplyPreset(f.path().filename().string()))
                                statusMessage_ =
                                    "Cannot read preset " + stem;
                        }
                    }
                if (!anyPreset) ImGui::TextDisabled("No presets yet.");
                ImGui::Separator();
                ImGui::BeginDisabled(!matEdAnyGenLayer());
                if (ImGui::MenuItem("Save current masks...")) {
                    openSavePresetPopup_ = true;
                    matEdPresetError_.clear();
                }
                ImGui::EndDisabled();
                ImGui::EndPopup();
            }
            if (openSavePresetPopup_) {
                ImGui::OpenPopup("Save material preset");
                openSavePresetPopup_ = false;
            }
            if (ImGui::BeginPopupModal("Save material preset", nullptr,
                                       ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::SetNextItemWidth(scaled(220.0f));
                ImGui::InputText("Name", matEdPresetName_,
                                 sizeof(matEdPresetName_));
                if (!matEdPresetError_.empty())
                    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "%s",
                                       matEdPresetError_.c_str());
                if (ImGui::Button("Save", ImVec2(scaled(120), 0))) {
                    const std::string base = sanitizeAssetName(
                        std::string(matEdPresetName_).empty()
                            ? "preset"
                            : matEdPresetName_);
                    matEdSavePreset(base);
                    if (matEdPresetError_.empty()) ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(scaled(120), 0)))
                    ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }

            const char* blends[] = {"Normal", "Multiply", "Add", "Overlay"};
            for (int i = (int)matEdLayers_.size() - 1; i >= 0; --i) {
                MatEdLayer& L = matEdLayers_[i];
                ImGui::PushID(i);
                if (ImGui::Checkbox("##vis", &L.visible)) {
                    matEdComposite();
                    matEdSavePaintTarget();
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Show/hide layer");
                ImGui::SameLine();
                const std::string lname = L.genOn ? L.name + " *" : L.name;
                if (ImGui::Selectable(lname.c_str(), matEdActiveLayer_ == i,
                                      0, ImVec2(scaled(96.0f), 0)))
                    matEdActiveLayer_ = i;
                if (L.genOn && ImGui::IsItemHovered())
                    ImGui::SetTooltip("Smart mask layer (generated)");
                if (i > 0) {
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(scaled(86.0f));
                    int blend = L.blend;
                    if (ImGui::Combo("##blend", &blend, blends, 4)) {
                        L.blend = blend;
                        matEdComposite();
                        matEdSavePaintTarget();
                    }
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(scaled(64.0f));
                    if (ImGui::DragFloat("##opacity", &L.opacity, 0.01f, 0.0f,
                                         1.0f, "%.2f"))
                        matEdComposite();
                    if (ImGui::IsItemDeactivatedAfterEdit()) matEdSavePaintTarget();
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Layer opacity");
                }
                ImGui::PopID();
            }

            // generator controls of the active smart-mask layer
            matEdGenControls();
        } else if (texRel.empty() && !matEdMats_.empty()) {
            // no texture on this entry yet: one click bootstraps the whole
            // layers/masks/presets flow ("mud on the shirt" starts here)
            ImGui::Spacing();
            ImGui::TextDisabled("Layers");
            if (ImGui::Button("Create texture for this entry"))
                matEdEnsurePaintTexture();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Creates a blank 256x256 PNG named after the entry,\n"
                    "assigns it and opens the layer stack - masks, presets\n"
                    "and painting all need a texture to land on.");
        }

        // --- the preview image + mouse interaction ---------------------------
        Viewport::MatPreviewDesc desc;
        desc.kd[0] = kd[0], desc.kd[1] = kd[1], desc.kd[2] = kd[2];
        matEdKe(sel, desc.ke);
        desc.texRel = texRel;
        desc.reflRel = reflRel;
        desc.reflStrength = sel.refl.empty() ? 0.0f : sel.reflStrength;
        desc.reflSky = reflSky;
        desc.reflRounded = sel.reflRounded;
        desc.shape = matEdShape_;
        desc.modelRel = matEdModel_;
        desc.mtlRel = matEdPath_;
        desc.entryName = sel.name;
        desc.angleDeg = matEdAngle_;
        desc.pitchDeg = matEdPitch_;
        desc.zoom = matEdZoom_;
        desc.displayMode = matEdDisplayMode_;
        if (matBakePreviewMode_ == 2 && !matBakeMaps_.empty()) {
            // raw-map view: the baked map replaces the material's look
            desc.texRel = "@matbake-view";
            desc.kd[0] = desc.kd[1] = desc.kd[2] = 1.0f;
            desc.ke[0] = desc.ke[1] = desc.ke[2] = 0.0f;
            desc.reflRel.clear();
            desc.reflStrength = 0.0f;
            desc.reflSky = false;
        }

        const ImVec2 avail = ImGui::GetContentRegionAvail();
        // UV view splits the space: 3D on top, the layout panel below
        float uvH = 0.0f;
        if (matEdUvView_) {
            uvH = avail.y * 0.42f;
            const float floor = scaled(140.0f);
            if (uvH < floor) uvH = floor;
            if (uvH > avail.y - scaled(80.0f)) uvH = avail.y - scaled(80.0f);
            if (uvH < 0.0f) uvH = 0.0f;
        }
        const int pw = (int)avail.x < 1 ? 1 : (int)avail.x;
        const int ph = (int)(avail.y - uvH) < 1 ? 1 : (int)(avail.y - uvH);
        const uint32_t tex = viewport_.renderMaterialPreview(pw, ph, desc);
        if (tex) {
            const ImVec2 imgPos = ImGui::GetCursorScreenPos();
            ImGui::Image((ImTextureID)(intptr_t)tex, ImVec2((float)pw, (float)ph),
                         ImVec2(0, 1), ImVec2(1, 0));
            ImGui::SetCursorScreenPos(imgPos);
            ImGui::InvisibleButton("##mat_prev_in", ImVec2((float)pw, (float)ph),
                                   ImGuiButtonFlags_MouseButtonLeft |
                                       ImGuiButtonFlags_MouseButtonRight);
            const bool hovered = ImGui::IsItemHovered();
            const bool active = ImGui::IsItemActive();

            if (hovered && io.MouseWheel != 0.0f) {
                matEdZoom_ *= std::pow(1.15f, io.MouseWheel);
                matEdZoom_ = matEdZoom_ < 0.2f ? 0.2f
                             : matEdZoom_ > 12.0f ? 12.0f
                                                  : matEdZoom_;
            }
            // orbit: LMB when not painting (the brush owns LMB then), RMB
            // always
            const bool orbiting =
                active && (ImGui::IsMouseDown(1) ||
                           (!matEdPaint_ && ImGui::IsMouseDown(0)));
            if (orbiting) {
                matEdAngle_ += io.MouseDelta.x * 0.5f;
                matEdPitch_ += io.MouseDelta.y * 0.4f;
                matEdPitch_ = matEdPitch_ < -30.0f ? -30.0f
                              : matEdPitch_ > 85.0f ? 85.0f
                                                    : matEdPitch_;
                matEdSpin_ = false;  // taking the camera unchecks the
                                     // turntable - the framing stays put
            }

            // Hover pick: feeds the UV-panel sync AND the click-a-part
            // entry selection. The outlines project the bake mesh's
            // triangles through the preview camera (matBakeMeshLow_ is
            // (re)built by the panel / validator; stale indices are
            // bounds-guarded).
            matEd3dHoverValid_ = false;
            if (hovered && !matEdStroke_ && !orbiting &&
                (matEdUvView_ || (!matEdPaint_ && matEdShape_ == 4))) {
                const float u = (io.MousePos.x - imgPos.x) / (float)pw;
                const float v = (io.MousePos.y - imgPos.y) / (float)ph;
                float hu = 0.0f, hv = 0.0f;
                bool pntbl = false;
                std::string hoverEntry;
                const bool hit = viewport_.materialPreviewPick(
                    u, v, hu, hv, pntbl, &hoverEntry);
                if (hit && pntbl && matEdUvView_) {
                    matEd3dHoverValid_ = true;
                    matEd3dHoverUV_[0] = hu;
                    matEd3dHoverUV_[1] = hv;
                }
                // a multi-part model: click any part to jump to its entry
                // (models only - primitives always use the first entry;
                // painting keeps LMB for the brush)
                if (hit && !matEdPaint_ && matEdShape_ == 4 &&
                    !hoverEntry.empty()) {
                    int hitIdx = -1;
                    for (size_t i = 0; i < matEdMats_.size(); ++i)
                        if (matEdMats_[i].name == hoverEntry)
                            hitIdx = (int)i;
                    if (hitIdx >= 0 && hitIdx != matEdSel_) {
                        ImGui::SetTooltip("%s - click to edit this entry",
                                          hoverEntry.c_str());
                        // a clean click, not the tail of an orbit drag
                        if (ImGui::IsMouseReleased(0) &&
                            io.MouseDragMaxDistanceSqr[0] < 16.0f)
                            matEdSel_ = hitIdx;
                    } else if (hitIdx < 0) {
                        ImGui::SetTooltip(
                            "part '%s' has no entry in this file",
                            hoverEntry.c_str());
                    }
                }
            }
            if (matEdUvHoverTri_ >= 0 || !matEdUvIssueTris_.empty()) {
                auto outlineTri = [&](int t, ImU32 col, float th) {
                    const matbake::MeshInput& mesh = matBakeMeshLow_;
                    if (t < 0 || t >= mesh.triCount()) return;
                    ImVec2 p[3];
                    for (int k = 0; k < 3; ++k) {
                        const float* vtx =
                            &mesh.verts[(size_t)(t * 3 + k) * 8];
                        float su = 0.0f, sv = 0.0f;
                        if (!viewport_.materialPreviewProject(vtx, su, sv))
                            return;
                        p[k] = ImVec2(imgPos.x + su * pw, imgPos.y + sv * ph);
                    }
                    ImGui::GetWindowDrawList()->AddTriangle(p[0], p[1], p[2],
                                                            col, th);
                };
                for (int t : matEdUvIssueTris_)
                    outlineTri(t, IM_COL32(255, 80, 70, 255), 2.0f);
                if (matEdUvView_)
                    outlineTri(matEdUvHoverTri_, IM_COL32(96, 190, 255, 255),
                               2.0f);
            }

            if (matEdPaint_ && canPaint) {
                if (ImGui::IsItemActivated() && ImGui::IsMouseDown(0)) {
                    // snapshot the painted layer before the stroke
                    matEdPushUndo(MatEdUndoStep::Kind::Paint, matEdActiveLayer_);
                    matEdStroke_ = true;
                    matEdHaveLastUV_ = false;
                }
                if (matEdStroke_ && ImGui::IsMouseDown(0)) {
                    const float u = (io.MousePos.x - imgPos.x) / (float)pw;
                    const float v = (io.MousePos.y - imgPos.y) / (float)ph;
                    float hu = 0.0f, hv = 0.0f;
                    bool paintable = false;
                    if (viewport_.materialPreviewPick(u, v, hu, hv, paintable) &&
                        paintable) {
                        matEdPaintTo(hu, hv);
                        matEdComposite();  // layer stack -> texture + GL upload
                    } else {
                        matEdHaveLastUV_ = false;  // left the paintable surface
                    }
                }
                if (matEdStroke_ && !ImGui::IsMouseDown(0)) {
                    matEdStroke_ = false;
                    matEdSavePaintTarget();  // the painted PNG ships as-is
                }
                // Live dab ghost: composite one uncommitted stamp under the
                // cursor. The active layer is backed up and restored right
                // away, so everything else (undo snapshots, stroke starts,
                // saves - which all recomposite first) sees clean layers.
                bool ghostDrawn = false;
                if (matEdGhostOn_ && hovered && !matEdStroke_ &&
                    !ImGui::IsMouseDown(0) && matEdActiveLayer_ >= 0 &&
                    matEdActiveLayer_ < (int)matEdLayers_.size()) {
                    const float u = (io.MousePos.x - imgPos.x) / (float)pw;
                    const float v = (io.MousePos.y - imgPos.y) / (float)ph;
                    float hu = 0.0f, hv = 0.0f;
                    bool paintable = false;
                    if (viewport_.materialPreviewPick(u, v, hu, hv, paintable) &&
                        paintable) {
                        MatEdLayer& L = matEdLayers_[matEdActiveLayer_];
                        std::vector<unsigned char> backup = L.pixels;
                        matEdGhostPass_ = true;
                        matEdStamp(hu, hv);
                        matEdGhostPass_ = false;
                        matEdComposite();
                        L.pixels = std::move(backup);
                        matEdGhostShown_ = true;
                        ghostDrawn = true;
                    }
                }
                if (matEdGhostShown_ && !ghostDrawn) {
                    matEdComposite();  // wipe the ghost off the composite
                    matEdGhostShown_ = false;
                }
                if (hovered) {
                    // approximate brush footprint (cosmetic cursor)
                    const float r = matEdBrushSize_ / (float)matEdPaintW_ *
                                    (float)(pw < ph ? pw : ph) * 0.5f;
                    ImGui::GetWindowDrawList()->AddCircle(
                        io.MousePos, r < 3.0f ? 3.0f : r,
                        IM_COL32(255, 255, 255, 180), 0, 1.5f);
                }
            }
        }
        if (matEdUvView_ && uvH > 8.0f)
            drawMatEdUvPanel(sel.name, texRel, ImVec2(avail.x, uvH));
    }
    ImGui::EndChild();

    ImGui::End();

    if (committed) {
        matEdPushUndo(MatEdUndoStep::Kind::Props);  // pre-edit entries -> Ctrl+Z
        saveMaterialFile();
    }
}
