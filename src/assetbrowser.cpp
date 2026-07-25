// Asset Browser (Tools > Asset Browser) - see docs/asset-browser.md.
//
// The project's res/ folder IS the asset database: nothing is imported into a
// registry, every picker in the editor lists what is on disk. So this window is
// a file manager over that tree - plus the two things a plain file manager
// cannot do, which are what make moving and deleting assets safe here:
//
//   * The REFERENCE CENSUS (rebuildAssetUsage): one pass over the model records
//     every place an asset path is stored, so the grid can badge unused files,
//     the inspector can list who uses one, and a move/rename can carry the
//     references along in the same commit (retargetAssetPath).
//   * The SIBLING INVARIANT: a Wavefront reference (mtllib, map_Kd, refl) is a
//     bare file name resolved next to the file that named it. The PS2 loads
//     assets from a flat ISO9660/host path and cannot walk "..", so a texture
//     must never end up in a different folder from the material that names it.
//     A move therefore takes the whole dependency group along (copying a
//     dependency that files left behind still need), and a move that would
//     break a reference is refused rather than half-applied. A RENAME inside
//     one folder is free of that problem, so there the references are rewritten
//     instead.
//
// Everything here is App:: methods declared in app.hpp; the file is separate
// only to keep app.cpp's panel section readable.

#include "app.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <imgui.h>
#include <imgui_internal.h>
#include <stb_image.h>  // implementation lives in app.cpp

#include "menubake.hpp"
#include "objparser.hpp"
#include "templates.hpp"  // saveMenuAssets - the built-in HUD sprites

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>  // ShellExecuteA - "Reveal in Explorer"

namespace fs = std::filesystem;

namespace {

std::string lowerOf(std::string s) {
    for (char& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

std::string extOf(const std::string& rel) {
    return lowerOf(fs::path(rel).extension().string());
}

std::string nameOf(const std::string& rel) {
    return fs::path(rel).filename().string();
}

std::string stemOf(const std::string& rel) {
    return fs::path(rel).stem().string();
}

// "res/models/props/tree.obj" -> "res/models/props" ("res" for a root file).
std::string folderOf(const std::string& rel) {
    const size_t slash = rel.rfind('/');
    return slash == std::string::npos ? std::string("res") : rel.substr(0, slash);
}

bool startsWith(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

// True when `rel` is inside `folder` (at any depth) or is the folder itself.
bool insideFolder(const std::string& rel, const std::string& folder) {
    return rel == folder || startsWith(rel, folder + "/");
}

// Human file size, aligned with how the Disc Layout window reads.
std::string sizeText(unsigned long long bytes) {
    char buf[48];
    if (bytes < 1024)
        std::snprintf(buf, sizeof(buf), "%llu B", bytes);
    else if (bytes < 1024 * 1024)
        std::snprintf(buf, sizeof(buf), "%.1f KB", (double)bytes / 1024.0);
    else
        std::snprintf(buf, sizeof(buf), "%.2f MB", (double)bytes / (1024.0 * 1024.0));
    return buf;
}

// Same rule as the import paths (sanitizeAssetName in app.cpp): asset names ride
// shell command lines, Makefiles and ISO9660 paths.
std::string sanitizeName(const std::string& name) {
    std::string out;
    for (char c : name) {
        const bool safe = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                          (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
        out.push_back(safe ? c : '_');
    }
    return out;
}

// A path component of the form "<texture>.layers" is a Material Editor paint
// sidecar directory: editor-only data that travels with its texture.
bool isLayersDir(const fs::path& rel) {
    for (const fs::path& part : rel) {
        const std::string s = part.string();
        if (s.size() > 7 && s.rfind(".layers") == s.size() - 7) return true;
    }
    return false;
}

// The type-filter chips, in toolbar order.
enum {
    kFilterAll = 0,
    kFilterModels,
    kFilterMaterials,
    kFilterTextures,
    kFilterAudio,
    kFilterFonts,
    kFilterOther,
    kFilterCount,
};
const char* const kFilterLabels[kFilterCount] = {"All",      "Models", "Materials",
                                                 "Textures", "Audio",  "Fonts",
                                                 "Other"};

// Per-kind tile accent color: one glance tells models from textures from audio.
ImU32 kindColor(int kind) {
    switch (kind) {
        case 0: return IM_COL32(96, 148, 224, 255);   // Model
        case 1: return IM_COL32(140, 122, 232, 255);  // AnimModel
        case 2: return IM_COL32(224, 152, 74, 255);   // Material
        case 3: return IM_COL32(84, 186, 130, 255);   // Texture
        case 4: return IM_COL32(212, 96, 152, 255);   // Music
        case 5: return IM_COL32(212, 132, 96, 255);   // Sound
        case 6: return IM_COL32(196, 196, 108, 255);  // Font
        default: return IM_COL32(130, 130, 138, 255);
    }
}

}  // namespace

std::string App::assetAbs(const std::string& rel) const {
    return (fs::path(project_.dir) / rel).string();
}

App::AssetKind App::assetKindOf(const std::string& rel) {
    const std::string ext = extOf(rel);
    if (ext == ".obj") return AssetKind::Model;
    if (ext == ".glb" || ext == ".fbx") return AssetKind::AnimModel;
    if (ext == ".mtl") return AssetKind::Material;
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg") return AssetKind::Texture;
    if (ext == ".wav")
        return startsWith(rel, "res/sfx/") ? AssetKind::Sound : AssetKind::Music;
    if (ext == ".ttf" || ext == ".otf") return AssetKind::Font;
    return AssetKind::Other;
}

const char* App::assetKindName(AssetKind k) {
    switch (k) {
        case AssetKind::Model: return "static model";
        case AssetKind::AnimModel: return "animated model";
        case AssetKind::Material: return "material";
        case AssetKind::Texture: return "texture";
        case AssetKind::Music: return "music track";
        case AssetKind::Sound: return "sound";
        case AssetKind::Font: return "font";
        default: return "file";
    }
}

// ---------------------------------------------------------------------------
// Scanning
// ---------------------------------------------------------------------------

void App::scanAssetTree() {
    assetItems_.clear();
    assetDirs_.clear();
    assetWfUsers_.clear();
    assetWfUsersReady_ = false;
    if (!hasProject_) return;

    AssetDir rootDir;
    rootDir.rel = "res";
    rootDir.name = "res";
    assetDirs_["res"] = rootDir;

    const fs::path root = fs::path(project_.dir) / "res";
    std::error_code ec;
    if (!fs::exists(root, ec)) return;

    // Files the build writes into res/. They are listed only with "Show
    // generated" and never moved, renamed or deleted from here: the next build
    // would just recreate them (or, for a menu panel, in the wrong place).
    std::set<std::string> generated;
    for (const GameMenu& m : project_.menus) {
        generated.insert("res/menus/" + menubake::panelFileName(m.name));
        generated.insert("res/menus/" + menubake::valueStripFileName(m.name));
    }
    for (const HudText& t : project_.hudTexts)
        generated.insert("res/hud/" + menubake::textFileName(t.name));
    for (const LoadingScreenDef& ls : project_.loadingScreens)
        for (const HudText& t : ls.texts)
            generated.insert("res/hud/" + menubake::textFileName(t.name));
    for (const GameFont& f : project_.fonts)
        generated.insert("res/fonts/" + menubake::atlasFileName(f.name));

    for (fs::recursive_directory_iterator it(root, ec), end; it != end;
         it.increment(ec)) {
        if (ec) break;
        const fs::path rel = fs::relative(it->path(), root, ec);
        if (ec) continue;
        const std::string relRes = "res/" + rel.generic_string();
        // Paint-layer sidecar folders are internal bookkeeping, not assets.
        if (isLayersDir(rel)) {
            if (it->is_directory()) it.disable_recursion_pending();
            continue;
        }
        if (it->is_directory()) {
            AssetDir d;
            d.rel = relRes;
            d.name = rel.filename().generic_string();
            d.parent = folderOf(relRes);
            assetDirs_[relRes] = d;
            continue;
        }
        if (!it->is_regular_file()) continue;

        AssetItem item;
        item.rel = relRes;
        item.name = rel.filename().string();
        item.kind = assetKindOf(relRes);
        std::error_code sec;
        item.bytes = (unsigned long long)fs::file_size(it->path(), sec);
        if (sec) item.bytes = 0;
        sec.clear();
        const auto tp = fs::last_write_time(it->path(), sec);
        item.mtime = sec ? 0 : (long long)tp.time_since_epoch().count();
        const std::string ext = extOf(relRes);
        // Baked binaries + sidecars: the .tmdl the game actually loads, the
        // animated-model replacement UVs, the AO sidecar.
        item.generated = generated.count(relRes) > 0 || ext == ".tmdl" ||
                         ext == ".uvs" || ext == ".aov" ||
                         startsWith(relRes, "res/menus/");
        assetItems_.push_back(std::move(item));
    }

    std::sort(assetItems_.begin(), assetItems_.end(),
              [](const AssetItem& a, const AssetItem& b) { return a.rel < b.rel; });

    // Folder children + file counts (deep counts fold up the parent chain).
    for (auto& [rel, dir] : assetDirs_) {
        if (dir.rel == "res") continue;
        auto parent = assetDirs_.find(dir.parent);
        if (parent != assetDirs_.end()) parent->second.children.push_back(dir.rel);
    }
    for (auto& [rel, dir] : assetDirs_)
        std::sort(dir.children.begin(), dir.children.end());
    for (const AssetItem& item : assetItems_) {
        if (item.generated && !assetShowGenerated_) continue;
        auto dir = assetDirs_.find(folderOf(item.rel));
        if (dir != assetDirs_.end()) ++dir->second.files;
        for (std::string walk = folderOf(item.rel); !walk.empty();) {
            auto d = assetDirs_.find(walk);
            if (d == assetDirs_.end()) break;
            ++d->second.filesDeep;
            if (walk == "res") break;
            walk = d->second.parent;
        }
    }

    if (!assetDirs_.count(assetFolder_)) assetFolder_ = "res";
    // A selection that no longer exists on disk would keep showing stale
    // details in the inspector.
    for (size_t i = assetSelection_.size(); i-- > 0;) {
        bool live = false;
        for (const AssetItem& item : assetItems_) live |= item.rel == assetSelection_[i];
        if (!live) assetSelection_.erase(assetSelection_.begin() + i);
    }
    assetScanTime_ = ImGui::GetTime();
}

void App::assetsChanged() {
    modelInfoCache_.clear();
    glbInfoCache_.clear();
    wavIssueCache_.clear();
    viewport_.invalidateAssets();  // model/material/texture/thumbnail caches
    assetUsageSerial_ = ~0ull;     // re-census on the next query
    scanAssetTree();
}

// ---------------------------------------------------------------------------
// Reference census
// ---------------------------------------------------------------------------

void App::rebuildAssetUsage() {
    assetUsage_.clear();
    assetUsageSerial_ = modelEditSerial_;
    if (!hasProject_) return;

    // Keep the readable "where" list bounded - a texture used by 300 objects
    // needs a count, not 300 lines.
    constexpr size_t kMaxLines = 12;
    auto note = [&](const std::string& path, int bucket, const std::string& line,
                    int scene = -1, int object = -1) {
        if (path.empty()) return;
        AssetUsage& u = assetUsage_[path];
        if (bucket == 0) ++u.objects;
        else if (bucket == 1) ++u.nodes;
        else ++u.other;
        if (u.lines.size() < kMaxLines) u.lines.push_back(line);
        if (scene >= 0 && object >= 0) u.objectRefs.emplace_back(scene, object);
    };

    for (int si = 0; si < (int)project_.scenes.size(); ++si) {
        const SceneData& scene = project_.scenes[si];
        const std::string sn = scene.name.empty() ? ("scene " + std::to_string(si + 1))
                                                  : scene.name;
        for (int oi = 0; oi < (int)scene.objects.size(); ++oi) {
            const SceneObject& o = scene.objects[oi];
            const std::string where = sn + " / " + o.name;
            if (!o.modelPath.empty()) note(o.modelPath, 0, where + " (model)", si, oi);
            if (!o.materialPath.empty())
                note(o.materialPath, 0, where + " (material)", si, oi);
            if (!o.soundPath.empty()) note(o.soundPath, 0, where + " (sound)", si, oi);
            for (const FlowNode& n : o.flowGraph.nodes) {
                const FlowNodeType* t = flowNodeType(n.type);
                if (!t || n.str.empty()) continue;
                if (t->strKind == FlowParamKind::MusicTrack ||
                    t->strKind == FlowParamKind::SoundTrack)
                    note(n.str, 1, where + " / " + t->title + " node");
            }
        }
        if (!scene.settings.terrainMaterial.empty())
            note(scene.settings.terrainMaterial, 2, sn + " terrain material");
        for (const TerrainLayer& l : scene.terrainLayers)
            if (!l.material.empty())
                note(l.material, 2, sn + " terrain layer \"" + l.name + "\"");
    }
    if (!project_.settings.terrainMaterial.empty())
        note(project_.settings.terrainMaterial, 2, "project terrain material");

    auto noteHud = [&](const HudImage& h, const std::string& where) {
        if (!h.imagePath.empty()) note(h.imagePath, 2, where);
    };
    for (const HudImage& h : project_.hud) noteHud(h, "HUD \"" + h.name + "\"");
    noteHud(project_.usePrompt, "USE prompt");
    for (const LoadingScreenDef& ls : project_.loadingScreens) {
        for (const HudImage& h : ls.images)
            noteHud(h, "loading screen \"" + ls.name + "\"");
        for (const LoadingBar& b : ls.bars)
            noteHud(b.segImage, "loading bar in \"" + ls.name + "\"");
    }
    for (const SplashScreen& s : project_.splashScreens)
        noteHud(s.image, "boot splash \"" + s.name + "\"");
    for (const GameMenu& m : project_.menus)
        for (const MenuImage& img : m.images)
            note(img.path, 2, "menu \"" + m.name + "\"");
    for (const GameFont& f : project_.fonts)
        if (!f.fontPath.empty()) note(f.fontPath, 2, "font \"" + f.name + "\"");

    // A LOD tier is a real reference: another model's chain points at that file.
    // What is deliberately NOT counted anywhere here is per-asset SETTINGS -
    // texture quality, a recorded real-world size, music build options,
    // animation clip edits, and membership of the auto-scanned music/sound
    // lists. Those are metadata attached to the file, not somebody using it, and
    // counting them would mean no imported asset ever reads as unused - which is
    // exactly the question the census exists to answer. They still follow the
    // file on a move (retargetAssetPath) and are cleaned up on a delete.
    for (const auto& [asset, tiers] : project_.modelLods)
        for (const std::string& t : tiers)
            note(t, 2, "LOD level of " + nameOf(asset));

    // The built-in sprites the generated game loads by fixed name (save menu,
    // USE prompt, loading screen, debug font). Nothing in the model points at
    // them, so without this they would read as unused - they are not, they are
    // just referenced from the generated sources. Deleting one is still
    // harmless: refreshGenerated writes it back on the next build.
    for (const templates::BuiltinAsset& a : templates::saveMenuAssets())
        note("res/hud/" + std::string(a.fileName), 2, "built-in engine sprite");
    for (const char* name : {"use.png", "pickup.png", "loading.png", "debugfont.png"})
        note("res/hud/" + std::string(name), 2, "built-in engine sprite");
}

const App::AssetUsage* App::assetUsageFor(const std::string& rel) {
    if (assetUsageSerial_ != modelEditSerial_) rebuildAssetUsage();
    auto it = assetUsage_.find(rel);
    return it == assetUsage_.end() ? nullptr : &it->second;
}

// ---------------------------------------------------------------------------
// Wavefront dependencies (the sibling invariant)
// ---------------------------------------------------------------------------

std::vector<std::string> App::assetWavefrontDeps(const std::string& rel) {
    std::vector<std::string> out;
    if (!hasProject_) return out;
    std::error_code ec;

    // A reference stored in `holder` (project-relative file) resolves next to
    // it; only results that stay inside res/ and exist are interesting.
    auto resolve = [&](const std::string& holder,
                       const std::string& ref) -> std::string {
        if (ref.empty() || ref == "@sky") return {};
        const fs::path abs =
            (fs::path(project_.dir) / folderOf(holder) / ref).lexically_normal();
        const std::string relPath =
            fs::relative(abs, fs::path(project_.dir), ec).generic_string();
        if (ec) {
            ec.clear();
            return {};
        }
        if (!startsWith(relPath, "res/")) return {};
        if (!fs::exists(abs, ec)) return {};
        return relPath;
    };
    auto push = [&](const std::string& p) {
        if (p.empty() || p == rel) return;
        for (const std::string& have : out)
            if (have == p) return;
        out.push_back(p);
    };

    const AssetKind kind = assetKindOf(rel);
    if (kind == AssetKind::Material) {
        std::vector<objparser::MtlMaterial> lib;
        if (objparser::loadMtl(assetAbs(rel), lib))
            for (const objparser::MtlMaterial& m : lib) {
                push(resolve(rel, m.texture));
                push(resolve(rel, m.refl));
            }
        return out;
    }
    if (kind != AssetKind::Model) return out;

    // Only the mtllib lines are read here, NOT the geometry: this runs for every
    // model in the project when the reverse map is built, and objparser::load
    // would triangulate megabytes of vertices to answer a question about two
    // text lines. The resolution rules are objparser's: the implicit
    // "<stem>.mtl" sibling counts even without a mtllib line.
    std::vector<std::string> libs;
    {
        std::ifstream f(assetAbs(rel));
        std::string line;
        while (std::getline(f, line)) {
            std::istringstream ss(line);
            std::string tag;
            ss >> tag;
            if (tag != "mtllib") continue;
            for (std::string name; ss >> name;) libs.push_back(name);
        }
    }
    libs.push_back(stemOf(rel) + ".mtl");
    for (const std::string& lib : libs) {
        const std::string libRel = resolve(rel, lib);
        if (libRel.empty()) continue;
        push(libRel);
        for (const std::string& dep : assetWavefrontDeps(libRel)) push(dep);
    }
    return out;
}

std::vector<std::string> App::assetWavefrontUsers(const std::string& rel) {
    if (!assetWfUsersReady_) {
        assetWfUsers_.clear();
        for (const AssetItem& item : assetItems_) {
            const bool isModel = item.kind == AssetKind::Model;
            if (!isModel && item.kind != AssetKind::Material) continue;
            for (const std::string& dep : assetWavefrontDeps(item.rel)) {
                // DIRECT references only: a model names its libraries, a
                // library names its textures. The dependency walk is transitive
                // (a move has to carry the whole group), but "who names this
                // file" must not claim the .obj names the texture - the reader
                // would go looking for the wrong line.
                if (isModel && extOf(dep) != ".mtl") continue;
                assetWfUsers_[dep].push_back(item.rel);
            }
        }
        assetWfUsersReady_ = true;
    }
    auto it = assetWfUsers_.find(rel);
    return it == assetWfUsers_.end() ? std::vector<std::string>() : it->second;
}

std::vector<std::string> App::assetSidecars(const std::string& rel) {
    std::vector<std::string> out;
    if (!hasProject_) return out;
    std::error_code ec;
    const std::string dir = folderOf(rel);
    const std::string stem = stemOf(rel);
    // Animated-model replacement UVs (uvunwrap) and the model AO sidecar.
    for (const char* ext : {".uvs", ".aov"}) {
        const std::string cand = dir + "/" + stem + ext;
        if (fs::exists(assetAbs(cand), ec)) out.push_back(cand);
    }
    // Material Editor paint layers of a texture live in "<file>.layers/".
    const std::string layers = rel + ".layers";
    if (fs::is_directory(assetAbs(layers), ec)) out.push_back(layers);
    return out;
}

bool App::rewriteWavefrontRef(const std::string& fileAbs,
                              const std::string& oldName,
                              const std::string& newName) {
    std::ifstream in(fileAbs);
    if (!in) return false;
    std::vector<std::string> lines;
    bool changed = false;
    std::string line;
    while (std::getline(in, line)) {
        // Reference statements name a file in their last token; rewrite only
        // that token so map options ("-s 2 2", "-mm 0 0.5") survive verbatim.
        std::istringstream ss(line);
        std::string tag;
        ss >> tag;
        if (tag == "mtllib" || tag == "map_Kd" || tag == "refl" || tag == "map_Ka" ||
            tag == "map_Ks" || tag == "map_Bump" || tag == "bump") {
            const size_t at = line.rfind(oldName);
            const bool trailing = at != std::string::npos &&
                                  at + oldName.size() >= line.find_last_not_of(" \t\r") + 1;
            if (trailing && at > 0 && (line[at - 1] == ' ' || line[at - 1] == '\t' ||
                                       line[at - 1] == '/' || line[at - 1] == '\\')) {
                line = line.substr(0, at) + newName +
                       line.substr(at + oldName.size());
                changed = true;
            }
        }
        lines.push_back(line);
    }
    in.close();
    if (!changed) return false;
    std::ofstream out(fileAbs, std::ios::trunc);
    if (!out) return false;
    for (const std::string& l : lines) out << l << "\n";
    return true;
}

// ---------------------------------------------------------------------------
// Reference retargeting
// ---------------------------------------------------------------------------

int App::retargetAssetPath(const std::string& from, const std::string& to) {
    if (from.empty() || from == to) return 0;
    int hits = 0;
    auto swap = [&](std::string& field) {
        if (field == from) {
            field = to;
            ++hits;
        }
    };
    for (SceneData& scene : project_.scenes) {
        for (SceneObject& o : scene.objects) {
            swap(o.modelPath);
            swap(o.materialPath);
            swap(o.soundPath);
            for (FlowNode& n : o.flowGraph.nodes) {
                const FlowNodeType* t = flowNodeType(n.type);
                if (!t) continue;
                if (t->strKind == FlowParamKind::MusicTrack ||
                    t->strKind == FlowParamKind::SoundTrack)
                    swap(n.str);
            }
        }
        swap(scene.settings.terrainMaterial);
        for (TerrainLayer& l : scene.terrainLayers) swap(l.material);
    }
    swap(project_.settings.terrainMaterial);

    for (HudImage& h : project_.hud) swap(h.imagePath);
    swap(project_.usePrompt.imagePath);
    for (LoadingScreenDef& ls : project_.loadingScreens) {
        for (HudImage& h : ls.images) swap(h.imagePath);
        for (LoadingBar& b : ls.bars) swap(b.segImage.imagePath);
    }
    for (SplashScreen& s : project_.splashScreens) swap(s.image.imagePath);
    for (GameMenu& m : project_.menus)
        for (MenuImage& img : m.images) swap(img.path);
    for (GameFont& f : project_.fonts) swap(f.fontPath);

    for (std::string& m : project_.music) swap(m);
    for (std::string& s : project_.sounds) swap(s);
    for (AnimClipEdit& e : project_.animClipEdits) swap(e.model);

    // Map keys have to be re-inserted rather than assigned.
    if (auto it = project_.textureQuality.find(from);
        it != project_.textureQuality.end()) {
        const std::string value = it->second;
        project_.textureQuality.erase(it);
        if (!to.empty()) project_.textureQuality[to] = value;
        ++hits;
    }
    if (auto it = project_.musicBuild.find(from); it != project_.musicBuild.end()) {
        const Project::MusicBuildOpt value = it->second;
        project_.musicBuild.erase(it);
        if (!to.empty()) project_.musicBuild[to] = value;
        ++hits;
    }
    if (auto it = project_.modelLods.find(from); it != project_.modelLods.end()) {
        const std::vector<std::string> value = it->second;
        project_.modelLods.erase(it);
        if (!to.empty()) project_.modelLods[to] = value;
        ++hits;
    }
    // The model's recorded real-world size (docs/world-scale.md) - a setting
    // keyed by the asset path, so it has to travel with the file or the next
    // object made from it comes in at scale 1.
    if (auto it = project_.modelUnitMeters.find(from);
        it != project_.modelUnitMeters.end()) {
        const float value = it->second;
        project_.modelUnitMeters.erase(it);
        if (!to.empty()) project_.modelUnitMeters[to] = value;
        ++hits;
    }
    for (auto& [asset, tiers] : project_.modelLods)
        for (std::string& t : tiers) swap(t);

    // Editor-side staging that names a file: the Material Editor's open .mtl and
    // paint target (a save would otherwise recreate the file at its old path)
    // and the pending real-world-size dialog (opened by an import, whose file
    // this move may be relocating). Not project data, but the same rule applies.
    swap(matEdPath_);
    swap(matEdPaintTexRel_);
    swap(modelSizePath_);
    swap(animEdModel_);

    return hits;
}

// ---------------------------------------------------------------------------
// File operations
// ---------------------------------------------------------------------------

namespace {

// Everything a move has to do with one file, resolved before anything touches
// the disk so a refusal leaves the project untouched.
struct MovePlan {
    std::string rel;      // source, project-relative
    std::string dest;     // destination, project-relative
    bool copy = false;    // a dependency somebody left behind still needs
};

}  // namespace

std::string App::moveAssets(const std::vector<std::string>& rels,
                            const std::string& destFolder) {
    if (rels.empty()) return {};
    std::error_code ec;
    if (!assetDirs_.count(destFolder) ||
        !fs::is_directory(assetAbs(destFolder), ec))
        return "The destination folder does not exist.";

    // The group = what was selected plus what those files need next to them.
    std::set<std::string> direct(rels.begin(), rels.end());
    std::set<std::string> group = direct;
    for (const std::string& rel : direct)
        for (const std::string& dep : assetWavefrontDeps(rel)) group.insert(dep);

    std::vector<MovePlan> plans;
    for (const std::string& rel : group) {
        if (folderOf(rel) == destFolder) continue;  // already there
        for (const AssetItem& item : assetItems_)
            if (item.rel == rel && item.generated)
                return nameOf(rel) + " is written by the build and cannot be moved.";
        if (fs::exists(assetAbs(destFolder + "/" + nameOf(rel)), ec))
            return "\"" + nameOf(rel) + "\" already exists in " + destFolder + ".";

        // Who still needs it where it is?
        std::vector<std::string> outside;
        for (const std::string& user : assetWavefrontUsers(rel))
            if (!group.count(user)) outside.push_back(user);
        if (!outside.empty() && direct.count(rel))
            return nameOf(rel) + " is referenced by " + nameOf(outside.front()) +
                   (outside.size() > 1
                        ? " and " + std::to_string(outside.size() - 1) + " more"
                        : "") +
                   ", which would stop finding it. Move them together, or "
                   "duplicate it first.";
        MovePlan plan;
        plan.rel = rel;
        plan.dest = destFolder + "/" + nameOf(rel);
        // A dependency others still need is COPIED instead: both folders end up
        // with a sibling copy, so every reference keeps resolving.
        plan.copy = !outside.empty();
        plans.push_back(std::move(plan));
    }
    if (plans.empty()) return {};

    int moved = 0, copied = 0, refs = 0;
    for (const MovePlan& plan : plans) {
        const fs::path src = assetAbs(plan.rel), dst = assetAbs(plan.dest);
        if (plan.copy) {
            fs::copy(src, dst, fs::copy_options::recursive, ec);
            if (ec) {
                ec.clear();
                continue;
            }
            ++copied;
            continue;
        }
        fs::rename(src, dst, ec);
        if (ec) {
            // Across volumes rename fails; copy+remove is the fallback.
            ec.clear();
            fs::copy(src, dst, fs::copy_options::recursive, ec);
            if (ec) {
                ec.clear();
                continue;
            }
            fs::remove_all(src, ec);
            ec.clear();
        }
        ++moved;
        refs += retargetAssetPath(plan.rel, plan.dest);
        // Sidecars follow their asset; the baked .tmdl is dropped instead - the
        // next build re-bakes it next to the model's new home.
        for (const std::string& side : assetSidecars(plan.rel)) {
            const std::string sideDest =
                destFolder + "/" + nameOf(side);
            fs::rename(assetAbs(side), assetAbs(sideDest), ec);
            ec.clear();
        }
        fs::remove(assetAbs(folderOf(plan.rel) + "/" + stemOf(plan.rel) + ".tmdl"),
                   ec);
        ec.clear();
        // A WAV that changed role (res/audio <-> res/sfx) leaves the list it no
        // longer belongs to; rescanAssets picks it up in the right one.
        const AssetKind was = assetKindOf(plan.rel), now = assetKindOf(plan.dest);
        if (was != now) {
            auto drop = [&](std::vector<std::string>& list) {
                for (size_t i = 0; i < list.size(); ++i)
                    if (list[i] == plan.dest) {
                        list.erase(list.begin() + i);
                        break;
                    }
            };
            if (was == AssetKind::Music) drop(project_.music);
            if (was == AssetKind::Sound) drop(project_.sounds);
        }
    }

    commitChange();
    assetsChanged();
    rescanAssets(false);  // audio lists follow the files
    std::string status = "Moved " + std::to_string(moved) + " file(s) to " + destFolder;
    if (copied) status += ", " + std::to_string(copied) + " shared file(s) copied along";
    if (refs) status += " (" + std::to_string(refs) + " reference(s) updated)";
    statusMessage_ = status;
    return {};
}

std::string App::moveAssetFolder(const std::string& folderRel,
                                 const std::string& destFolder) {
    if (folderRel == "res") return "The res folder itself cannot be moved.";
    if (!assetDirs_.count(folderRel)) return "That folder no longer exists.";
    if (insideFolder(destFolder, folderRel))
        return "A folder cannot be moved into itself.";
    if (folderOf(folderRel) == destFolder) return {};  // no-op
    std::error_code ec;
    const std::string dest = destFolder + "/" + nameOf(folderRel);
    if (fs::exists(assetAbs(dest), ec))
        return "\"" + nameOf(folderRel) + "\" already exists in " + destFolder + ".";

    // Nothing outside the folder may depend on a file inside it: the whole
    // subtree travels together, so only references crossing the boundary break.
    std::vector<std::string> inside;
    for (const AssetItem& item : assetItems_)
        if (insideFolder(item.rel, folderRel)) {
            if (item.generated)
                return "The folder contains build-generated files - move the "
                       "sources instead.";
            inside.push_back(item.rel);
        }
    for (const std::string& rel : inside)
        for (const std::string& user : assetWavefrontUsers(rel))
            if (!insideFolder(user, folderRel))
                return nameOf(rel) + " is referenced by " + nameOf(user) +
                       " outside this folder, which would stop finding it.";

    fs::rename(assetAbs(folderRel), assetAbs(dest), ec);
    if (ec) return "Move failed: " + ec.message();

    int refs = 0;
    for (const std::string& rel : inside)
        refs += retargetAssetPath(rel, dest + rel.substr(folderRel.size()));
    commitChange();
    assetsChanged();
    rescanAssets(false);
    if (assetFolder_ == folderRel || startsWith(assetFolder_, folderRel + "/"))
        assetFolder_ = dest + assetFolder_.substr(folderRel.size());
    statusMessage_ = "Moved " + nameOf(folderRel) + " into " + destFolder + " (" +
                     std::to_string(refs) + " reference(s) updated)";
    return {};
}

std::string App::renameAsset(const std::string& rel, const std::string& newNameIn) {
    std::error_code ec;
    const std::string dir = folderOf(rel);
    const std::string oldName = nameOf(rel);
    std::string newName = sanitizeName(newNameIn);
    if (newName.empty()) return "Give the file a name.";
    // Keep the extension: the type is what every picker filters on.
    const std::string ext = extOf(rel);
    if (extOf(newName) != ext) newName += ext;
    if (newName == oldName) return {};
    for (const AssetItem& item : assetItems_)
        if (item.rel == rel && item.generated)
            return "This file is written by the build and cannot be renamed.";
    const std::string dest = dir + "/" + newName;
    if (fs::exists(assetAbs(dest), ec))
        return "\"" + newName + "\" already exists in this folder.";

    // Users in another folder would reference this file through a path, which
    // the sibling invariant does not allow us to rewrite safely.
    for (const std::string& user : assetWavefrontUsers(rel))
        if (folderOf(user) != dir)
            return nameOf(user) + " references it from another folder - rename "
                                  "is only safe for siblings.";

    fs::rename(assetAbs(rel), assetAbs(dest), ec);
    if (ec) return "Rename failed: " + ec.message();
    int refs = retargetAssetPath(rel, dest);
    for (const std::string& side : assetSidecars(rel)) {
        // "<stem>.uvs" follows the stem; "<file>.layers" follows the full name.
        const std::string sideName = nameOf(side);
        const std::string moved =
            startsWith(sideName, oldName)
                ? dir + "/" + newName + sideName.substr(oldName.size())
                : dir + "/" + stemOf(dest) + extOf(side);
        fs::rename(assetAbs(side), assetAbs(moved), ec);
        ec.clear();
    }
    fs::remove(assetAbs(dir + "/" + stemOf(rel) + ".tmdl"), ec);
    ec.clear();

    // Siblings that name it keep working: within one folder the reference is a
    // bare file name, so rewriting it is exact.
    int files = 0;
    for (const std::string& user : assetWavefrontUsers(rel))
        if (rewriteWavefrontRef(assetAbs(user), oldName, newName)) ++files;

    // A model and the material library it exclusively owns are one asset to the
    // user ("tree.obj" + "tree.mtl", how every import writes them). Rename the
    // library along - and if the model relied on the IMPLICIT sibling rule,
    // write the mtllib line the new name needs.
    if (assetKindOf(rel) == AssetKind::Model) {
        const std::string ownMtl = dir + "/" + stemOf(rel) + ".mtl";
        if (fs::exists(assetAbs(ownMtl), ec)) {
            std::vector<std::string> others;
            for (const std::string& user : assetWavefrontUsers(ownMtl))
                if (user != rel) others.push_back(user);
            const AssetUsage* mtlUse = assetUsageFor(ownMtl);
            const bool shared = !others.empty() || (mtlUse && mtlUse->total() > 0);
            if (!shared) {
                const std::string newMtl = dir + "/" + stemOf(dest) + ".mtl";
                if (!fs::exists(assetAbs(newMtl), ec)) {
                    fs::rename(assetAbs(ownMtl), assetAbs(newMtl), ec);
                    if (!ec) {
                        refs += retargetAssetPath(ownMtl, newMtl);
                        rewriteWavefrontRef(assetAbs(dest), nameOf(ownMtl),
                                            nameOf(newMtl));
                    }
                    ec.clear();
                }
            } else {
                // The library stays where it is under its own name; make sure
                // the renamed .obj still names it explicitly.
                std::ifstream in(assetAbs(dest));
                bool hasMtllib = false;
                for (std::string line; std::getline(in, line);)
                    hasMtllib |= startsWith(line, "mtllib");
                in.close();
                if (!hasMtllib) {
                    std::ifstream body(assetAbs(dest), std::ios::binary);
                    std::string all((std::istreambuf_iterator<char>(body)),
                                    std::istreambuf_iterator<char>());
                    body.close();
                    std::ofstream out(assetAbs(dest),
                                      std::ios::binary | std::ios::trunc);
                    out << "mtllib " << nameOf(ownMtl) << "\n" << all;
                }
            }
        }
        ec.clear();
    }

    commitChange();
    assetsChanged();
    rescanAssets(false);
    assetSelection_.assign(1, dest);
    statusMessage_ = "Renamed to " + newName + " (" + std::to_string(refs) +
                     " reference(s), " + std::to_string(files) + " file(s) updated)";
    return {};
}

std::string App::renameAssetFolder(const std::string& folderRel,
                                   const std::string& newNameIn) {
    if (folderRel == "res") return "The res folder cannot be renamed.";
    if (!assetDirs_.count(folderRel)) return "That folder no longer exists.";
    const std::string newName = sanitizeName(newNameIn);
    if (newName.empty()) return "Give the folder a name.";
    if (newName == nameOf(folderRel)) return {};
    std::error_code ec;
    const std::string dest = folderOf(folderRel) + "/" + newName;
    if (fs::exists(assetAbs(dest), ec))
        return "\"" + newName + "\" already exists here.";

    std::vector<std::string> inside;
    for (const AssetItem& item : assetItems_)
        if (insideFolder(item.rel, folderRel)) inside.push_back(item.rel);
    fs::rename(assetAbs(folderRel), assetAbs(dest), ec);
    if (ec) return "Rename failed: " + ec.message();

    int refs = 0;
    for (const std::string& rel : inside)
        refs += retargetAssetPath(rel, dest + rel.substr(folderRel.size()));
    commitChange();
    assetsChanged();
    rescanAssets(false);
    if (insideFolder(assetFolder_, folderRel))
        assetFolder_ = dest + assetFolder_.substr(folderRel.size());
    statusMessage_ = "Renamed folder to " + newName + " (" + std::to_string(refs) +
                     " reference(s) updated)";
    return {};
}

std::string App::createAssetFolder(const std::string& parentRel,
                                   const std::string& nameIn) {
    const std::string name = sanitizeName(nameIn);
    if (name.empty()) return "Give the folder a name.";
    std::error_code ec;
    const std::string rel = parentRel + "/" + name;
    if (fs::exists(assetAbs(rel), ec)) return "\"" + name + "\" already exists here.";
    if (!fs::create_directories(assetAbs(rel), ec))
        return "Could not create the folder: " + ec.message();
    scanAssetTree();
    assetTreeOpen_.insert(parentRel);
    assetFolder_ = rel;
    statusMessage_ = "Created " + rel;
    return {};
}

std::string App::duplicateAsset(const std::string& rel) {
    std::error_code ec;
    const std::string dir = folderOf(rel);
    const std::string stem = stemOf(rel), ext = extOf(rel);
    std::string dest;
    for (int n = 1; n < 100; ++n) {
        dest = dir + "/" + stem + (n == 1 ? "-copy" : "-copy" + std::to_string(n)) +
               ext;
        if (!fs::exists(assetAbs(dest), ec)) break;
    }
    fs::copy_file(assetAbs(rel), assetAbs(dest), ec);
    if (ec) return "Copy failed: " + ec.message();
    // The copy stays in the same folder, so every bare-name reference inside it
    // (a .mtl's textures) still resolves - nothing to rewrite.
    assetsChanged();
    assetSelection_.assign(1, dest);
    statusMessage_ = "Duplicated as " + nameOf(dest);
    return {};
}

// Deletes one asset file plus its sidecars and clears what the project stored
// about it. Kinds the older per-asset dialog already knows are routed through
// performAssetDelete so the two paths cannot drift apart.
void App::deleteAssetFile(const std::string& rel) {
    std::error_code ec;
    const AssetKind kind = assetKindOf(rel);
    for (const std::string& side : assetSidecars(rel)) {
        fs::remove_all(assetAbs(side), ec);
        ec.clear();
    }
    fs::remove(assetAbs(folderOf(rel) + "/" + stemOf(rel) + ".tmdl"), ec);
    ec.clear();

    switch (kind) {
        case AssetKind::Model:
        case AssetKind::AnimModel:
            performAssetDelete({PendingAssetDelete::Model, rel, nameOf(rel), -1});
            return;
        case AssetKind::Material:
            performAssetDelete({PendingAssetDelete::Material, rel, nameOf(rel), -1});
            return;
        case AssetKind::Music:
            performAssetDelete({PendingAssetDelete::Music, rel, nameOf(rel), -1});
            return;
        case AssetKind::Sound:
            performAssetDelete({PendingAssetDelete::Sound, rel, nameOf(rel), -1});
            return;
        default:
            break;
    }
    fs::remove(assetAbs(rel), ec);
    project_.textureQuality.erase(rel);
    // A font whose TTF is gone falls back to the built-in Consolas chain, so
    // clearing the path keeps every text drawable (see menubake::resolveFontPath).
    for (GameFont& f : project_.fonts)
        if (f.fontPath == rel) f.fontPath.clear();
    commitChange();
}

// ---------------------------------------------------------------------------
// Activation (double-click) and drag & drop into the scene
// ---------------------------------------------------------------------------

bool App::activateAsset(const std::string& rel) {
    switch (assetKindOf(rel)) {
        case AssetKind::Model:
        case AssetKind::AnimModel:
            addModelObject(rel);
            statusMessage_ = "Added " + nameOf(rel) + " to the scene";
            return true;
        case AssetKind::Material:
            openMaterialEditor(rel);
            return true;
        case AssetKind::Font:
            ensureFontForPath(rel);
            showFontManager_ = true;
            return true;
        default:
            return false;
    }
}

// ---------------------------------------------------------------------------
// The window
// ---------------------------------------------------------------------------

void App::drawAssetBrowserWindow() {
    if (!showAssetBrowser_ || !hasProject_) return;
    ImGui::SetNextWindowSize(ImVec2(scaled(960), scaled(600)), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Asset Browser", &showAssetBrowser_)) {
        ImGui::End();
        return;
    }

    // res/ is the database, so the disk is the source of truth: a periodic
    // rescan picks up hand-dropped files, an import from another panel and
    // whatever the last build wrote, with no bookkeeping anywhere.
    if (assetScanTime_ < 0.0 || ImGui::GetTime() - assetScanTime_ > 1.5)
        scanAssetTree();
    // Each new thumbnail is a GL render + a texture copy; spread a folder full
    // of them over a few frames instead of stalling one.
    assetThumbBudget_ = 4;

    auto matchesFilter = [&](const AssetItem& item) {
        if (!assetSearch_.empty() &&
            lowerOf(item.name).find(lowerOf(assetSearch_)) == std::string::npos)
            return false;
        if (item.generated && !assetShowGenerated_) return false;
        switch (assetFilter_) {
            case kFilterModels:
                return item.kind == AssetKind::Model ||
                       item.kind == AssetKind::AnimModel;
            case kFilterMaterials: return item.kind == AssetKind::Material;
            case kFilterTextures: return item.kind == AssetKind::Texture;
            case kFilterAudio:
                return item.kind == AssetKind::Music || item.kind == AssetKind::Sound;
            case kFilterFonts: return item.kind == AssetKind::Font;
            case kFilterOther: return item.kind == AssetKind::Other;
            default: return true;
        }
    };
    auto inScope = [&](const AssetItem& item) {
        return assetRecursive_ ? insideFolder(item.rel, assetFolder_)
                               : folderOf(item.rel) == assetFolder_;
    };
    auto isSelectedAsset = [&](const std::string& rel) {
        for (const std::string& s : assetSelection_)
            if (s == rel) return true;
        return false;
    };

    // --- toolbar ----------------------------------------------------------
    // Imports land in the folder the browser is showing when that folder is
    // inside the root the type belongs to - otherwise in the type's own root,
    // where the importers put them.
    auto importInto = [&](const std::string& imported) {
        if (imported.empty()) return;
        if (folderOf(imported) != assetFolder_ &&
            startsWith(assetFolder_, folderOf(imported) + "/")) {
            scanAssetTree();
            assetOpError_ = moveAssets({imported}, assetFolder_);
        } else {
            assetsChanged();
        }
        assetSelection_.assign(1, imported);
    };
    if (ImGui::Button("Import...")) ImGui::OpenPopup("assetimport");
    if (ImGui::BeginPopup("assetimport")) {
        if (ImGui::MenuItem("Model (.obj / .glb / .fbx)...")) importInto(importModelAsset());
        if (ImGui::MenuItem("Material (.mtl)...")) importInto(importMaterialAsset());
        if (ImGui::MenuItem("Texture (.png)...")) importInto(importTextureAsset());
        ImGui::Separator();
        if (ImGui::MenuItem("Music track (.wav)...")) {
            importMusicTrack();
            assetsChanged();
        }
        if (ImGui::MenuItem("Sound effect (.wav)...")) {
            importSoundEffect();
            assetsChanged();
        }
        if (ImGui::MenuItem("HUD image (.png)...")) {
            importHudImage();
            assetsChanged();
        }
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("New folder")) {
        assetNewFolderParent_ = assetFolder_;
        assetNewFolderBuf_[0] = 0;
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(scaled(170));
    char search[128];
    std::snprintf(search, sizeof(search), "%s", assetSearch_.c_str());
    if (ImGui::InputTextWithHint("##assetsearch", "Search name...", search,
                                 sizeof(search)))
        assetSearch_ = search;
    ImGui::SameLine();
    if (ImGui::SmallButton("x##clearsearch")) assetSearch_.clear();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Clear the search");

    // Type chips carry the count in the current scope - "Textures (0)" says at a
    // glance that this folder has none, without switching filters.
    ImGui::SameLine();
    ImGui::TextUnformatted("|");
    int counts[kFilterCount] = {};
    for (const AssetItem& item : assetItems_) {
        if (!inScope(item)) continue;
        if (item.generated && !assetShowGenerated_) continue;
        ++counts[kFilterAll];
        switch (item.kind) {
            case AssetKind::Model:
            case AssetKind::AnimModel: ++counts[kFilterModels]; break;
            case AssetKind::Material: ++counts[kFilterMaterials]; break;
            case AssetKind::Texture: ++counts[kFilterTextures]; break;
            case AssetKind::Music:
            case AssetKind::Sound: ++counts[kFilterAudio]; break;
            case AssetKind::Font: ++counts[kFilterFonts]; break;
            default: ++counts[kFilterOther]; break;
        }
    }
    for (int f = 0; f < kFilterCount; ++f) {
        // Chips wrap instead of running off a narrowly docked window.
        ImGui::SameLine();
        if (ImGui::GetContentRegionAvail().x <
            ImGui::CalcTextSize(kFilterLabels[f]).x + scaled(48))
            ImGui::NewLine();
        const bool on = assetFilter_ == f;
        if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(
                                                           ImGuiCol_ButtonActive));
        char label[64];
        std::snprintf(label, sizeof(label), "%s (%d)##f%d", kFilterLabels[f],
                      counts[f], f);
        if (ImGui::SmallButton(label)) assetFilter_ = f;
        if (on) ImGui::PopStyleColor();
    }

    // --- second row: breadcrumb + view controls ----------------------------
    ImGui::AlignTextToFramePadding();
    {
        // Clickable path segments; the deepest one is the current folder.
        std::vector<std::string> parts;
        for (size_t i = 0; i <= assetFolder_.size(); ++i) {
            if (i == assetFolder_.size() || assetFolder_[i] == '/') {
                parts.push_back(assetFolder_.substr(0, i));
                continue;
            }
        }
        for (size_t i = 0; i < parts.size(); ++i) {
            if (i) {
                ImGui::SameLine(0.0f, scaled(4));
                ImGui::TextDisabled("/");
                ImGui::SameLine(0.0f, scaled(4));
            }
            const std::string label = nameOf(parts[i]) + "##bc" + std::to_string(i);
            if (i + 1 == parts.size()) {
                ImGui::TextUnformatted(nameOf(parts[i]).c_str());
            } else if (ImGui::SmallButton(label.c_str())) {
                assetFolder_ = parts[i];
                assetSelection_.clear();
            }
        }
    }
    {
        // Right-aligned: what the grid shows and how big. On a window too narrow
        // for both, they simply follow the breadcrumb instead of overlapping it.
        const float right = ImGui::GetWindowContentRegionMax().x;
        ImGui::SameLine();
        const float controlsX = right - scaled(470);
        if (controlsX > ImGui::GetCursorPosX()) ImGui::SameLine(controlsX);
        ImGui::Checkbox("Sub-folders", &assetRecursive_);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("List everything below this folder, not just its\n"
                              "own files (search across a whole tree).");
        ImGui::SameLine();
        if (ImGui::Checkbox("Generated", &assetShowGenerated_)) scanAssetTree();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Also show files the build writes into res/\n"
                              "(baked menu panels, text sprites, glyph atlases,\n"
                              ".tmdl meshes). They are read-only here.");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(scaled(96));
        const char* sorts[] = {"Name", "Type", "Size", "Newest"};
        ImGui::Combo("##assetsort", &assetSort_, sorts, 4);
        ImGui::SameLine();
        if (ImGui::SmallButton(assetGridView_ ? "List" : "Grid"))
            assetGridView_ = !assetGridView_;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(assetGridView_ ? "Switch to detail rows"
                                             : "Switch to the thumbnail grid");
        if (assetGridView_) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(scaled(90));
            ImGui::SliderFloat("##tilesize", &assetTileSize_, 48.0f, 160.0f, "%.0f px");
        }
    }

    if (!assetOpError_.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "%s", assetOpError_.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("OK##operr")) assetOpError_.clear();
    }
    ImGui::Separator();

    // --- folder tree ------------------------------------------------------
    const float inspectorH = scaled(150);
    ImGui::BeginChild("assettree", ImVec2(scaled(180), -inspectorH), true);
    {
        auto folderDropTarget = [&](const std::string& folder) {
            if (!ImGui::BeginDragDropTarget()) return;
            for (const char* type : {"TYRAX_ASSET_MODEL", "TYRAX_ASSET_FILE"})
                if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload(type)) {
                    const std::string rel((const char*)pl->Data);
                    // Dragging one of several selected files moves the whole
                    // selection - that is what the highlight promised.
                    std::vector<std::string> moving =
                        isSelectedAsset(rel) ? assetSelection_
                                             : std::vector<std::string>{rel};
                    assetOpError_ = moveAssets(moving, folder);
                }
            if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("TYRAX_ASSET_DIR")) {
                const std::string rel((const char*)pl->Data);
                assetOpError_ = moveAssetFolder(rel, folder);
            }
            ImGui::EndDragDropTarget();
        };
        auto folderContext = [&](const std::string& folder) {
            if (!ImGui::BeginPopupContextItem()) return;
            if (ImGui::MenuItem("New folder...")) {
                assetNewFolderParent_ = folder;
                assetNewFolderBuf_[0] = 0;
            }
            const bool isRoot = folder == "res";
            if (ImGui::MenuItem("Rename...", nullptr, false, !isRoot)) {
                assetRenameRel_ = folder;
                assetRenameIsFolder_ = true;
                assetRenameFocus_ = true;
                std::snprintf(assetRenameBuf_, sizeof(assetRenameBuf_), "%s",
                              nameOf(folder).c_str());
            }
            if (ImGui::MenuItem("Delete folder...", nullptr, false, !isRoot)) {
                assetDeleteBatch_.clear();
                for (const AssetItem& item : assetItems_)
                    if (insideFolder(item.rel, folder))
                        assetDeleteBatch_.push_back(item.rel);
                assetDeleteFolder_ = folder;
                assetDeleteBatchActive_ = true;
            }
            if (ImGui::MenuItem("Reveal in Explorer")) {
                const std::string arg = "\"" + assetAbs(folder) + "\"";
                ShellExecuteA(nullptr, "open", "explorer.exe", arg.c_str(), nullptr,
                              SW_SHOWNORMAL);
            }
            ImGui::EndPopup();
        };
        auto drawNode = [&](auto&& self, const std::string& rel) -> void {
            const AssetDir& dir = assetDirs_[rel];
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                                       ImGuiTreeNodeFlags_SpanAvailWidth;
            if (dir.children.empty()) flags |= ImGuiTreeNodeFlags_Leaf;
            if (assetFolder_ == rel) flags |= ImGuiTreeNodeFlags_Selected;
            if (assetTreeOpen_.count(rel) || rel == "res")
                ImGui::SetNextItemOpen(true, ImGuiCond_Once);
            const bool open = ImGui::TreeNodeEx(
                rel.c_str(), flags, "%s (%d)", dir.name.c_str(), dir.filesDeep);
            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
                assetFolder_ = rel;
                assetSelection_.clear();
            }
            // Folders are draggable too, so tidying up is one gesture.
            if (rel != "res" && ImGui::BeginDragDropSource()) {
                ImGui::SetDragDropPayload("TYRAX_ASSET_DIR", rel.c_str(),
                                          rel.size() + 1);
                ImGui::Text("%s (%d files)", dir.name.c_str(), dir.filesDeep);
                ImGui::EndDragDropSource();
            }
            folderDropTarget(rel);
            folderContext(rel);
            if (open) {
                for (const std::string& child : dir.children) self(self, child);
                ImGui::TreePop();
            }
            if (open) assetTreeOpen_.insert(rel);
            else assetTreeOpen_.erase(rel);
        };
        if (assetDirs_.count("res")) drawNode(drawNode, "res");
    }
    ImGui::EndChild();

    // --- file listing -----------------------------------------------------
    ImGui::SameLine();
    ImGui::BeginChild("assetgrid", ImVec2(0, -inspectorH), true);

    std::vector<const AssetItem*> shown;
    for (const AssetItem& item : assetItems_)
        if (inScope(item) && matchesFilter(item)) shown.push_back(&item);
    std::sort(shown.begin(), shown.end(),
              [&](const AssetItem* a, const AssetItem* b) {
                  switch (assetSort_) {
                      case 1:
                          if (a->kind != b->kind) return (int)a->kind < (int)b->kind;
                          break;
                      case 2:
                          if (a->bytes != b->bytes) return a->bytes > b->bytes;
                          break;
                      case 3:
                          if (a->mtime != b->mtime) return a->mtime > b->mtime;
                          break;
                      default: break;
                  }
                  return lowerOf(a->rel) < lowerOf(b->rel);
              });

    // Click/drag/context menu, shared by both views so the two never diverge.
    auto itemInteract = [&](const AssetItem& item, bool doubleClicked) {
        if (ImGui::IsItemClicked() || ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            const bool ctrl = ImGui::GetIO().KeyCtrl;
            const bool shift = ImGui::GetIO().KeyShift;
            if (shift && !assetAnchor_.empty()) {
                // Range over what is on screen, in the order it is shown.
                int from = -1, to = -1;
                for (int i = 0; i < (int)shown.size(); ++i) {
                    if (shown[i]->rel == assetAnchor_) from = i;
                    if (shown[i]->rel == item.rel) to = i;
                }
                if (from >= 0 && to >= 0) {
                    assetSelection_.clear();
                    for (int i = ImMin(from, to); i <= ImMax(from, to); ++i)
                        assetSelection_.push_back(shown[i]->rel);
                }
            } else if (!ctrl && !isSelectedAsset(item.rel))
                assetSelection_.assign(1, item.rel);
            else if (ctrl) {
                if (isSelectedAsset(item.rel)) {
                    for (size_t i = 0; i < assetSelection_.size(); ++i)
                        if (assetSelection_[i] == item.rel) {
                            assetSelection_.erase(assetSelection_.begin() + i);
                            break;
                        }
                } else {
                    assetSelection_.push_back(item.rel);
                }
            }
            if (!shift) assetAnchor_ = item.rel;  // shift extends from the anchor
        }
        if (doubleClicked && !item.generated) activateAsset(item.rel);
        if (ImGui::BeginDragDropSource()) {
            const bool model = item.kind == AssetKind::Model ||
                               item.kind == AssetKind::AnimModel;
            ImGui::SetDragDropPayload(model ? "TYRAX_ASSET_MODEL" : "TYRAX_ASSET_FILE",
                                      item.rel.c_str(), item.rel.size() + 1);
            const size_t n = isSelectedAsset(item.rel) ? assetSelection_.size() : 1;
            if (n > 1) ImGui::Text("%s + %d more", item.name.c_str(), (int)n - 1);
            else ImGui::TextUnformatted(item.name.c_str());
            if (model) ImGui::TextDisabled("drop on the viewport to place it");
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginPopupContextItem()) {
            const bool locked = item.generated;
            if (ImGui::MenuItem("Add to scene", nullptr, false,
                                !locked && (item.kind == AssetKind::Model ||
                                            item.kind == AssetKind::AnimModel)))
                activateAsset(item.rel);
            if (ImGui::MenuItem("Edit material", nullptr, false,
                                !locked && item.kind == AssetKind::Material))
                openMaterialEditor(item.rel);
            if (ImGui::MenuItem("Animation Editor", nullptr, false,
                                !locked && item.kind == AssetKind::AnimModel)) {
                animEdModel_ = item.rel;
                animEdClip_.clear();
                showAnimEditor_ = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Rename...", nullptr, false, !locked)) {
                assetRenameRel_ = item.rel;
                assetRenameIsFolder_ = false;
                assetRenameFocus_ = true;
                std::snprintf(assetRenameBuf_, sizeof(assetRenameBuf_), "%s",
                              item.name.c_str());
            }
            if (ImGui::MenuItem("Duplicate", nullptr, false, !locked))
                assetOpError_ = duplicateAsset(item.rel);
            if (ImGui::MenuItem("Delete...", nullptr, false, !locked)) {
                if (!isSelectedAsset(item.rel)) assetSelection_.assign(1, item.rel);
                requestAssetSelectionDelete();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Copy path"))
                ImGui::SetClipboardText(item.rel.c_str());
            if (ImGui::MenuItem("Reveal in Explorer")) {
                const std::string arg = "/select,\"" + assetAbs(item.rel) + "\"";
                ShellExecuteA(nullptr, "open", "explorer.exe", arg.c_str(), nullptr,
                              SW_SHOWNORMAL);
            }
            ImGui::EndPopup();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(item.rel.c_str());
            ImGui::TextDisabled("%s - %s", assetKindName(item.kind),
                                sizeText(item.bytes).c_str());
            const AssetUsage* use = assetUsageFor(item.rel);
            if (use) ImGui::TextDisabled("used %d time(s) in the project", use->total());
            if (item.generated) ImGui::TextDisabled("written by the build");
            ImGui::EndTooltip();
        }
    };

    if (shown.empty()) {
        ImGui::TextDisabled(
            assetItems_.empty()
                ? "res/ is empty - Import... or drop files into the project folder."
                : "Nothing here matches the filter.");
    } else if (assetGridView_) {
        const float tile = scaled(assetTileSize_);
        const float labelH = ImGui::GetTextLineHeightWithSpacing() * 2.0f;
        const ImVec2 cell(tile, tile + labelH);
        const float avail = ImGui::GetContentRegionAvail().x;
        const int perRow =
            (int)ImMax(1.0f, std::floor(avail / (cell.x + ImGui::GetStyle().ItemSpacing.x)));
        int col = 0;
        for (const AssetItem* itemPtr : shown) {
            const AssetItem& item = *itemPtr;
            ImGui::PushID(item.rel.c_str());
            const ImVec2 pos = ImGui::GetCursorScreenPos();
            const bool selected = isSelectedAsset(item.rel);
            ImGui::Selectable("##tile", selected,
                              ImGuiSelectableFlags_AllowDoubleClick, cell);
            const bool dbl = ImGui::IsItemHovered() &&
                             ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
            itemInteract(item, dbl);

            ImDrawList* dl = ImGui::GetWindowDrawList();
            const float pad = scaled(6);
            const ImVec2 imgA(pos.x + pad, pos.y + pad);
            const ImVec2 imgB(pos.x + tile - pad, pos.y + tile - pad);
            // Ask what is already baked first: that is what tells us whether
            // this tile is about to spend one of the frame's thumbnail slots.
            const bool baked = viewport_.assetThumb(item.rel, false) != 0;
            const uint32_t tex =
                baked ? viewport_.assetThumb(item.rel, false)
                      : viewport_.assetThumb(item.rel, assetThumbBudget_ > 0);
            if (!baked && tex) --assetThumbBudget_;
            if (tex) {
                // A rendered thumbnail comes out of a framebuffer (bottom-left
                // origin); an image file is its own thumbnail and is upright.
                const bool flip = item.kind != AssetKind::Texture;
                dl->AddImage((ImTextureID)(intptr_t)tex, imgA, imgB,
                             ImVec2(0, flip ? 1.0f : 0.0f),
                             ImVec2(1, flip ? 0.0f : 1.0f));
            } else {
                // No preview for this type (audio, fonts, anything else): a
                // colored plate with the extension reads as fast as an icon.
                const ImU32 col = kindColor((int)item.kind);
                dl->AddRectFilled(imgA, imgB, (col & 0x00FFFFFF) | 0x33000000,
                                  scaled(4));
                dl->AddRect(imgA, imgB, col, scaled(4));
                std::string ext = extOf(item.rel);
                if (!ext.empty()) ext = lowerOf(ext.substr(1));
                const ImVec2 ts = ImGui::CalcTextSize(ext.c_str());
                dl->AddText(ImVec2((imgA.x + imgB.x - ts.x) * 0.5f,
                                   (imgA.y + imgB.y - ts.y) * 0.5f),
                            col, ext.c_str());
            }
            // Badges: dim generated files, ring the ones nothing references.
            const AssetUsage* use = assetUsageFor(item.rel);
            const bool wfUsed = !assetWfUsers_.empty() &&
                                assetWfUsers_.count(item.rel) > 0;
            if (!item.generated && (!use || use->total() == 0) && !wfUsed)
                dl->AddCircle(ImVec2(imgB.x - scaled(5), imgA.y + scaled(5)),
                              scaled(4), IM_COL32(220, 180, 90, 220), 0,
                              scaled(1.5f));
            const ImU32 textCol = ImGui::GetColorU32(
                item.generated ? ImGuiCol_TextDisabled : ImGuiCol_Text);
            // Wrapped inside the tile and hard-clipped to it: a long file name
            // must not spill over the row below.
            const ImVec4 clip(pos.x + pad, pos.y + tile - scaled(4),
                              pos.x + tile - pad, pos.y + tile + labelH);
            dl->AddText(nullptr, 0.0f, ImVec2(pos.x + pad, pos.y + tile - scaled(2)),
                        textCol, item.name.c_str(), nullptr, tile - pad * 2.0f, &clip);
            ImGui::PopID();

            if (++col % perRow != 0) ImGui::SameLine();
        }
    } else if (ImGui::BeginTable("assetrows", 5,
                                 ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                                     ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0.45f);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthStretch, 0.16f);
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthStretch, 0.12f);
        ImGui::TableSetupColumn("Used", ImGuiTableColumnFlags_WidthStretch, 0.09f);
        ImGui::TableSetupColumn("Folder", ImGuiTableColumnFlags_WidthStretch, 0.18f);
        ImGui::TableHeadersRow();
        for (const AssetItem* itemPtr : shown) {
            const AssetItem& item = *itemPtr;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushID(item.rel.c_str());
            const bool selected = isSelectedAsset(item.rel);
            if (item.generated) ImGui::PushStyleColor(
                ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::Selectable(item.name.c_str(), selected,
                              ImGuiSelectableFlags_SpanAllColumns |
                                  ImGuiSelectableFlags_AllowDoubleClick);
            if (item.generated) ImGui::PopStyleColor();
            const bool dbl = ImGui::IsItemHovered() &&
                             ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
            itemInteract(item, dbl);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(assetKindName(item.kind));
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(sizeText(item.bytes).c_str());
            ImGui::TableNextColumn();
            const AssetUsage* use = assetUsageFor(item.rel);
            ImGui::TextUnformatted(std::to_string(use ? use->total() : 0).c_str());
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", folderOf(item.rel).c_str());
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    // Empty space in the listing is a drop target for the folder being shown,
    // and clears the selection on a click.
    if (ImGui::BeginDragDropTargetCustom(ImGui::GetCurrentWindow()->Rect(),
                                         ImGui::GetID("gridtarget"))) {
        for (const char* type : {"TYRAX_ASSET_MODEL", "TYRAX_ASSET_FILE"})
            if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload(type)) {
                const std::string rel((const char*)pl->Data);
                std::vector<std::string> moving = isSelectedAsset(rel)
                                                      ? assetSelection_
                                                      : std::vector<std::string>{rel};
                assetOpError_ = moveAssets(moving, assetFolder_);
            }
        if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("TYRAX_ASSET_DIR")) {
            const std::string rel((const char*)pl->Data);
            assetOpError_ = moveAssetFolder(rel, assetFolder_);
        }
        ImGui::EndDragDropTarget();
    }
    if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered() &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        assetSelection_.clear();
    ImGui::EndChild();

    // --- inspector --------------------------------------------------------
    ImGui::BeginChild("assetinspector", ImVec2(0, 0), true);
    if (assetSelection_.empty()) {
        ImGui::TextDisabled("Select an asset to see what it is and who uses it.");
        ImGui::TextDisabled(
            "Drag files onto a folder to move them (their references follow); drag a\n"
            "model onto the viewport to place it in the scene. The ring in a tile's\n"
            "corner means nothing in the project references that file.");
    } else if (assetSelection_.size() > 1) {
        unsigned long long total = 0;
        int used = 0;
        for (const std::string& rel : assetSelection_) {
            for (const AssetItem& item : assetItems_)
                if (item.rel == rel) total += item.bytes;
            const AssetUsage* u = assetUsageFor(rel);
            if (u && u->total() > 0) ++used;
        }
        ImGui::Text("%d files selected - %s, %d referenced by the project",
                    (int)assetSelection_.size(), sizeText(total).c_str(), used);
        if (ImGui::Button("Delete...")) requestAssetSelectionDelete();
        ImGui::SameLine();
        ImGui::TextDisabled("Drag them onto a folder in the tree to move the set.");
    } else {
        drawAssetInspector(assetSelection_.front());
    }
    ImGui::EndChild();

    drawAssetBrowserModals();
    ImGui::End();
}

// Details + actions for one selected asset. This is where the per-type controls
// live that used to sit inline in the Project panel's flat list.
void App::drawAssetInspector(const std::string& rel) {
    const AssetItem* item = nullptr;
    for (const AssetItem& i : assetItems_)
        if (i.rel == rel) item = &i;
    if (!item) {
        ImGui::TextDisabled("%s is gone from disk.", rel.c_str());
        return;
    }

    ImGui::Text("%s", item->name.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("(%s, %s)", assetKindName(item->kind),
                        sizeText(item->bytes).c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy path")) ImGui::SetClipboardText(rel.c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Reveal")) {
        const std::string arg = "/select,\"" + assetAbs(rel) + "\"";
        ShellExecuteA(nullptr, "open", "explorer.exe", arg.c_str(), nullptr,
                      SW_SHOWNORMAL);
    }
    ImGui::TextDisabled("%s", rel.c_str());
    if (item->generated) {
        ImGui::TextColored(ImVec4(0.85f, 0.75f, 0.4f, 1.0f),
                           "Written by the build - edit the source it is baked from.");
        return;
    }

    ImGui::BeginGroup();
    switch (item->kind) {
        case AssetKind::Model: {
            const ModelInfo& info = modelInfo(rel);
            if (info.ok) {
                ImGui::Text("%d triangles, %d material(s)", info.tris,
                            (int)info.materials.size());
                for (const ModelInfo::MaterialLine& line : info.materials)
                    if (line.missing)
                        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
                                           "%s - texture not found next to the .obj",
                                           line.text.c_str());
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
                                   "Unreadable .obj");
            }
            if (ImGui::SmallButton("Add to scene")) activateAsset(rel);
            ImGui::SameLine();
            drawAssetSizeButton(rel);
            ImGui::SameLine();
            drawAssetQualityCombo(rel);
            ImGui::SameLine();
            drawAssetLodButton(rel);
            break;
        }
        case AssetKind::AnimModel: {
            const GlbInfo& info = glbInfo(rel);
            if (info.ok) {
                ImGui::Text("%d clip(s), %d vertices, %d frames",
                            (int)info.clips.size(), info.vertexCount,
                            info.frameCount);
                std::string clips;
                for (const std::string& c : effectiveClips(rel))
                    clips += (clips.empty() ? "" : ", ") + c;
                if (!clips.empty()) ImGui::TextWrapped("%s", clips.c_str());
                for (const std::string& w : info.warnings)
                    ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "%s", w.c_str());
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "Unusable: %s",
                                   info.error.c_str());
            }
            if (ImGui::SmallButton("Add to scene")) activateAsset(rel);
            ImGui::SameLine();
            drawAssetSizeButton(rel);
            ImGui::SameLine();
            if (ImGui::SmallButton("Animation Editor...")) {
                animEdModel_ = rel;
                animEdClip_.clear();
                showAnimEditor_ = true;
            }
            break;
        }
        case AssetKind::Material: {
            const ModelInfo& info = materialInfo(rel);
            if (info.ok) {
                ImGui::Text("%d material(s)", (int)info.materials.size());
                for (const ModelInfo::MaterialLine& line : info.materials)
                    ImGui::TextColored(
                        line.missing ? ImVec4(1.0f, 0.45f, 0.35f, 1.0f)
                                     : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled),
                        "%s%s", line.text.c_str(),
                        line.missing ? " - texture MISSING" : "");
            } else {
                ImGui::TextDisabled("Empty or unreadable .mtl");
            }
            if (ImGui::SmallButton("Edit in Material Editor")) openMaterialEditor(rel);
            ImGui::SameLine();
            drawAssetQualityCombo(rel);
            break;
        }
        case AssetKind::Texture: {
            int w = 0, h = 0, comp = 0;
            if (stbi_info(assetAbs(rel).c_str(), &w, &h, &comp)) {
                ImGui::Text("%d x %d, %d channel(s)", w, h, comp);
                auto pow2 = [](int v) { return v > 0 && (v & (v - 1)) == 0; };
                if (!pow2(w) || !pow2(h))
                    ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f),
                                       "Not a power of two - the build resizes it "
                                       "for the PS2 (8..256 per axis).");
                if (w > 256 || h > 256)
                    ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f),
                                       "Larger than 256 - it is scaled down at "
                                       "build (GS VRAM is ~1.33 MB).");
            } else {
                ImGui::TextDisabled("Unreadable image");
            }
            break;
        }
        case AssetKind::Music:
        case AssetKind::Sound: {
            const bool sfx = item->kind == AssetKind::Sound;
            const std::string& issue = wavIssue(rel, sfx);
            if (issue.empty())
                ImGui::TextDisabled(sfx ? "Ready for the ADPCM bake."
                                        : "Streams from the disc as-is.");
            else
                ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "%s", issue.c_str());
            ImGui::TextDisabled(
                sfx ? "Volume, looping and conversion live in Project > Sounds."
                    : "Volume and the PS2 build options live in Project > Music.");
            break;
        }
        case AssetKind::Font:
            ImGui::TextDisabled(
                "TTF source. Static text bakes to sprites at build; a font a "
                "Display Text node uses also gets a glyph atlas.");
            if (ImGui::SmallButton("Font Manager...")) activateAsset(rel);
            break;
        default:
            ImGui::TextDisabled("No preview for this file type.");
            break;
    }
    ImGui::EndGroup();

    // Who uses it. This is the whole reason a move or a delete is safe here.
    ImGui::SameLine(0.0f, scaled(24));
    ImGui::BeginGroup();
    const AssetUsage* use = assetUsageFor(rel);
    const std::vector<std::string> wfUsers = assetWavefrontUsers(rel);
    const int total = (use ? use->total() : 0) + (int)wfUsers.size();
    if (total == 0) {
        ImGui::TextColored(ImVec4(0.85f, 0.72f, 0.36f, 1.0f),
                           "Nothing references this file.");
        ImGui::TextDisabled("Safe to delete - or it is waiting to be used.");
    } else {
        ImGui::Text("Referenced %d time(s):", total);
        if (use) {
            for (size_t i = 0; i < use->lines.size(); ++i) {
                ImGui::BulletText("%s", use->lines[i].c_str());
                if (i < use->objectRefs.size()) {
                    ImGui::SameLine();
                    ImGui::PushID((int)i);
                    if (ImGui::SmallButton("Select")) {
                        const auto [scene, object] = use->objectRefs[i];
                        if (scene != project_.activeScene) {
                            project_.activeScene = scene;
                            applyProjectToViewport();
                        }
                        selectOnly(object);
                    }
                    ImGui::PopID();
                }
            }
            if (use->total() > (int)use->lines.size())
                ImGui::TextDisabled("... and %d more",
                                    use->total() - (int)use->lines.size());
        }
        for (const std::string& user : wfUsers)
            ImGui::BulletText("%s names it (must stay siblings)",
                              nameOf(user).c_str());
    }
    ImGui::EndGroup();
}

void App::requestAssetSelectionDelete() {
    assetDeleteBatch_.clear();
    for (const std::string& rel : assetSelection_) {
        bool locked = false;
        for (const AssetItem& item : assetItems_)
            if (item.rel == rel) locked = item.generated;
        if (!locked) assetDeleteBatch_.push_back(rel);
    }
    assetDeleteFolder_.clear();
    assetDeleteBatchActive_ = !assetDeleteBatch_.empty();
}

void App::drawAssetBrowserModals() {
    // Rename (file or folder), in a small popup next to the list.
    if (!assetRenameRel_.empty() && !ImGui::IsPopupOpen("Rename asset"))
        ImGui::OpenPopup("Rename asset");
    if (ImGui::BeginPopupModal("Rename asset", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextDisabled("%s", assetRenameRel_.c_str());
        if (assetRenameFocus_) {
            ImGui::SetKeyboardFocusHere();
            assetRenameFocus_ = false;
        }
        const bool submit =
            ImGui::InputText("New name", assetRenameBuf_, sizeof(assetRenameBuf_),
                             ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::TextDisabled(
            assetRenameIsFolder_
                ? "Every reference to the files inside follows the folder."
                : "References in the project follow, and so do the materials in\n"
                  "this folder that name the file.");
        if (ImGui::Button("Rename", ImVec2(scaled(120), 0)) || submit) {
            assetOpError_ = assetRenameIsFolder_
                                ? renameAssetFolder(assetRenameRel_, assetRenameBuf_)
                                : renameAsset(assetRenameRel_, assetRenameBuf_);
            assetRenameRel_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(scaled(120), 0))) {
            assetRenameRel_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // New folder.
    if (!assetNewFolderParent_.empty() && !ImGui::IsPopupOpen("New asset folder"))
        ImGui::OpenPopup("New asset folder");
    if (ImGui::BeginPopupModal("New asset folder", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Inside %s", assetNewFolderParent_.c_str());
        ImGui::SetNextItemWidth(scaled(240));
        const bool submit = ImGui::InputText("Name", assetNewFolderBuf_,
                                             sizeof(assetNewFolderBuf_),
                                             ImGuiInputTextFlags_EnterReturnsTrue);
        if (ImGui::Button("Create", ImVec2(scaled(120), 0)) || submit) {
            assetOpError_ = createAssetFolder(assetNewFolderParent_, assetNewFolderBuf_);
            assetNewFolderParent_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(scaled(120), 0))) {
            assetNewFolderParent_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // Delete (one file, a selection, or a whole folder). One dialog for the set,
    // with the reference census spelled out per file - this is the only place a
    // res/ file is removed from inside the editor besides the older per-asset
    // buttons in the Project panel.
    if (assetDeleteBatchActive_ && !ImGui::IsPopupOpen("Delete assets?"))
        ImGui::OpenPopup("Delete assets?");
    if (ImGui::BeginPopupModal("Delete assets?", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        if (!assetDeleteFolder_.empty())
            ImGui::Text("Delete the folder \"%s\" and its %d file(s)?",
                        nameOf(assetDeleteFolder_).c_str(),
                        (int)assetDeleteBatch_.size());
        else
            ImGui::Text("Delete %d file(s)?", (int)assetDeleteBatch_.size());

        int referenced = 0;
        ImGui::BeginChild("delfiles",
                          ImVec2(scaled(420),
                                 ImMin(scaled(180),
                                       ImGui::GetTextLineHeightWithSpacing() *
                                           (assetDeleteBatch_.size() + 1.0f))),
                          true);
        for (const std::string& rel : assetDeleteBatch_) {
            const AssetUsage* use = assetUsageFor(rel);
            const int n = (use ? use->total() : 0) +
                          (int)assetWavefrontUsers(rel).size();
            if (n > 0) ++referenced;
            if (n > 0)
                ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "%s - %d reference(s)",
                                   nameOf(rel).c_str(), n);
            else
                ImGui::TextDisabled("%s", nameOf(rel).c_str());
        }
        ImGui::EndChild();
        if (referenced > 0)
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f),
                               "%d file(s) are still in use: objects keep their\n"
                               "path and show as missing, materials revert to plain\n"
                               "color, audio references are cleared.", referenced);
        ImGui::TextDisabled("The files are removed from res/. This cannot be undone.");
        ImGui::Separator();
        if (ImGui::Button("Delete", ImVec2(scaled(120), 0))) {
            const int n = (int)assetDeleteBatch_.size();
            for (const std::string& rel : assetDeleteBatch_) deleteAssetFile(rel);
            if (!assetDeleteFolder_.empty()) {
                std::error_code ec;
                fs::remove_all(assetAbs(assetDeleteFolder_), ec);
                if (assetFolder_ == assetDeleteFolder_ ||
                    startsWith(assetFolder_, assetDeleteFolder_ + "/"))
                    assetFolder_ = folderOf(assetDeleteFolder_);
            }
            assetSelection_.clear();
            assetDeleteBatch_.clear();
            assetDeleteFolder_.clear();
            assetDeleteBatchActive_ = false;
            assetsChanged();
            statusMessage_ = "Deleted " + std::to_string(n) + " asset(s)";
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(scaled(120), 0))) {
            assetDeleteBatch_.clear();
            assetDeleteFolder_.clear();
            assetDeleteBatchActive_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void App::dropAssetIntoScene(const std::string& rel, float u, float v) {
    const AssetKind kind = assetKindOf(rel);
    if (kind != AssetKind::Model && kind != AssetKind::AnimModel) return;
    float point[3] = {0.0f, 0.0f, 0.0f};
    const bool hit = viewport_.placementRaycast(u, v, project_.objects(),
                                               placementSkip(), point);
    addModelObject(rel, hit ? point : nullptr);
    statusMessage_ = hit ? "Dropped " + nameOf(rel) + " into the scene"
                         : "Added " + nameOf(rel) + " (cursor missed the ground)";
}
