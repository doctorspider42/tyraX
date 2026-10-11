// The model import window and the model picker (docs/model-import.md) -
// App:: methods declared in app.hpp, own TU (the assetbrowser.cpp precedent).
//
// Everything that decides what an import PRODUCES lives in modelimport.cpp
// (host-only, also run by --import-model); this file is the window around it
// and the two places a model meets the scene: the picker a Model / Animated
// model entity's "Model" field opens, and assignModel/addModelEntity, which
// are the one answer to "what changes when an object gets a new model".
#include "app.hpp"
#include "app_internal.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>

#include <imgui.h>

namespace fs = std::filesystem;

namespace {

bool startsWith(const std::string& s, const std::string& p) {
    return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}

std::string lowerOf(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

}  // namespace

// --- import window ----------------------------------------------------------

void App::importModelAsset(const std::string& targetFolder) {
    if (modelImporting_) return;
    const std::string src = pickModelFile();
    if (src.empty()) return;
    importModelFile(src, targetFolder);
}

void App::importModelFile(const std::string& path, const std::string& targetFolder) {
    if (modelImporting_ || !hasProject_) return;
    if (modelImportThread_.joinable()) modelImportThread_.join();
    const bool reload = modelImportFile_ == path && modelImportPhase_ == ImportPhase::Idle &&
                        modelImportSrc_ != nullptr;
    if (!reload) {
        // a fresh file starts from the defaults; a texture-folder re-read
        // keeps whatever the window was already set to
        modelImportOpt_ = modelimport::Options{};
        modelImportOpt_.pivot = modelimport::Pivot::BottomCenter;
        modelImportUnit_ = 0;
        modelImportMeters_ = 1.0f;
        modelImportSel_ = -1;
        modelImportZoom_ = 1.0f;
        modelImportAddToScene_ = modelImportFor_ == ModelImportFor::NewStatic ||
                                 modelImportFor_ == ModelImportFor::NewAnimated;
        const std::string stem =
            modelimport::safeName(fs::path(path).stem().string());
        std::snprintf(modelImportName_, sizeof modelImportName_, "%s", stem.c_str());
    }
    modelImportFile_ = path;
    modelImportTarget_ = targetFolder;
    modelImportProjectDir_ = project_.dir;
    modelImportError_.clear();
    modelImportWriteError_.clear();
    modelImportSrc_.reset();
    modelImportPlan_.clear();
    modelImportDone_.store(false, std::memory_order_relaxed);
    modelImportPhase_ = ImportPhase::Loading;
    modelImporting_ = true;
    modelImportOpen_ = true;
    const std::vector<std::string> dirs = modelImportTexDirs_;
    modelImportThread_ = std::thread([this, path, dirs]() {
        auto src = std::make_unique<modelimport::Source>();
        std::string err;
        if (modelimport::load(path, dirs, *src, err))
            modelImportSrc_ = std::move(src);
        else
            modelImportError_ = err.empty() ? "unreadable model" : err;
        modelImportDone_.store(true, std::memory_order_release);
    });
}

void App::modelImportReplan() {
    if (!modelImportSrc_) return;
    modelImportOpt_.baseName = modelImportName_;
    const size_t before = modelImportPlan_.size();
    modelImportPlan_ = modelimport::plan(*modelImportSrc_, modelImportOpt_);
    if (modelImportPlan_.size() != before || modelImportOpt_.include.size() !=
                                                 modelImportPlan_.size())
        modelImportOpt_.include.assign(modelImportPlan_.size(), 1);
    if (modelImportSel_ >= (int)modelImportPlan_.size()) modelImportSel_ = -1;
    ++modelImportVersion_;
}

void App::modelImportTick() {
    if (!modelImporting_ || !modelImportDone_.load(std::memory_order_acquire))
        return;
    if (modelImportThread_.joinable()) modelImportThread_.join();
    modelImportDone_.store(false, std::memory_order_relaxed);

    if (modelImportPhase_ == ImportPhase::Loading) {
        modelImportPhase_ = ImportPhase::Ready;
        if (modelImportSrc_) {
            const modelimport::Source& s = *modelImportSrc_;
            // Both forms by default when the file can animate: the static
            // twin is a few hundred KB and means either entity can use it.
            modelImportOpt_.writeAnimated =
                s.canAnimate() && modelImportFor_ != ModelImportFor::NewStatic;
            modelImportOpt_.writeStatic = modelImportFor_ != ModelImportFor::NewAnimated ||
                                          !s.canAnimate();
            modelImportReplan();
        }
        return;
    }

    // Writing finished
    modelImportPhase_ = ImportPhase::Idle;
    modelImporting_ = false;
    if (!hasProject_ || project_.dir != modelImportProjectDir_) return;
    if (!modelImportWriteError_.empty()) {
        statusMessage_ = "Model import failed: " + modelImportWriteError_;
        modelImportFor_ = ModelImportFor::Nothing;
        return;
    }
    const modelimport::Result& res = modelImportResult_;
    // The size the window was told is the asset's recorded real-world size,
    // the same record Model size... edits (docs/world-scale.md).
    std::vector<std::string> outputs = res.staticModels;
    if (!res.animatedModel.empty()) outputs.push_back(res.animatedModel);
    for (const std::string& rel : outputs) {
        if (std::fabs(modelImportMeters_ - 1.0f) > 1e-6f)
            project_.modelUnitMeters[rel] = modelImportMeters_;
        else
            project_.modelUnitMeters.erase(rel);
    }
    assetsChanged();
    std::string msg = "Imported " + fs::path(modelImportFile_).filename().string() +
                      ": ";
    if (!res.staticModels.empty())
        msg += std::to_string(res.staticModels.size()) + " static model(s)";
    if (!res.animatedModel.empty())
        msg += std::string(res.staticModels.empty() ? "" : " + ") + "animated model";
    msg += ", " + std::to_string(res.textures) + " texture(s), " + res.material;
    statusMessage_ = msg;
    if (!outputs.empty()) assetSelection_.assign(1, outputs.front());
    modelImportPlace(res);
    commitChange();  // modelUnitMeters + any placed objects: one step
    modelImportSrc_.reset();
    modelImportPlan_.clear();
}

void App::modelImportPlace(const modelimport::Result& res) {
    const ModelImportFor why = modelImportFor_;
    modelImportFor_ = ModelImportFor::Nothing;
    // what was asked for, unless the file could only give the other kind
    // (an "Animated model" from a file with no skeleton places it static)
    bool animated = why == ModelImportFor::NewAnimated ||
                    (why == ModelImportFor::Retarget && modelPickAnimated_) ||
                    (why == ModelImportFor::Nothing && res.staticModels.empty());
    if (animated && res.animatedModel.empty()) animated = false;
    if (!animated && res.staticModels.empty()) animated = true;
    const std::string first =
        animated ? res.animatedModel
                 : (res.staticModels.empty() ? std::string() : res.staticModels.front());

    if (why == ModelImportFor::Retarget) {
        for (int i = 0; i < (int)project_.objects().size(); ++i)
            if (project_.objects()[i].id == modelPickObjectId_ && !first.empty()) {
                assignModel(project_.objects()[i], first, animated);
                selectOnly(i);
            }
        return;
    }
    if (why == ModelImportFor::Nothing && !modelImportAddToScene_) return;
    if (first.empty()) return;

    // A split import placed as the source laid it out: every model at the
    // spot its pivot came from, one editor group so the set moves together.
    if (!animated && res.staticModels.size() > 1 && modelImportLayout_) {
        const std::string group = "import-" + std::string(modelImportName_);
        selection_.clear();
        for (size_t i = 0; i < res.staticModels.size(); ++i) {
            const std::string& rel = res.staticModels[i];
            const float s = project_.modelInsertScale(rel);
            SceneObject o;
            o.type = PrimitiveType::Model;
            o.modelPath = rel;
            o.color[0] = o.color[1] = o.color[2] = 0.85f;
            for (int a = 0; a < 3; ++a) {
                o.position[a] = res.origins[i][(size_t)a] * s;
                o.scale[a] = s;
            }
            o.editorGroup = group;
            std::string base = fs::path(rel).stem().string(), name = base;
            for (int n = 2;; ++n) {
                bool taken = false;
                for (const SceneObject& other : project_.objects())
                    taken |= other.name == name;
                if (!taken) break;
                name = base + "-" + std::to_string(n);
            }
            o.name = name;
            project_.objects().push_back(std::move(o));
            selection_.push_back((int)project_.objects().size() - 1);
        }
        selectedObject_ = selection_.empty() ? -1 : selection_.back();
        return;
    }
    addModelObject(first, nullptr, /*commit=*/false);
}

void App::drawModelImportModal() {
    if (modelImportOpen_) {
        ImGui::OpenPopup("Import model");
        modelImportOpen_ = false;
    }
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(scaled(1040), scaled(660)), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Import model", nullptr, ImGuiWindowFlags_NoSavedSettings))
        return;

    const std::string fileName = fs::path(modelImportFile_).filename().string();
    auto spinner = [&](const char* what) {
        ImGui::Text("%c  %s", "|/-\\"[(int)(ImGui::GetTime() * 10.0) & 3], what);
        ImGui::TextDisabled("%s", fileName.c_str());
    };
    if (modelImportPhase_ == ImportPhase::Idle) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    if (modelImportPhase_ == ImportPhase::Loading) {
        spinner("Reading model...");
        ImGui::EndPopup();
        return;
    }
    if (modelImportPhase_ == ImportPhase::Writing) {
        spinner("Writing model, material and textures...");
        ImGui::EndPopup();
        return;
    }
    if (!modelImportSrc_) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "%s: %s", fileName.c_str(),
                           modelImportError_.c_str());
        if (ImGui::Button("Choose another file...")) {
            modelImportPhase_ = ImportPhase::Idle;
            modelImporting_ = false;
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            importModelAsset(modelImportTarget_);
            return;
        }
        ImGui::SameLine();
        if (ImGui::Button("Close")) {
            modelImportPhase_ = ImportPhase::Idle;
            modelImporting_ = false;
            modelImportFor_ = ModelImportFor::Nothing;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
        return;
    }

    const modelimport::Source& src = *modelImportSrc_;
    modelimport::Options& opt = modelImportOpt_;
    bool replan = false;
    const float footer = ImGui::GetFrameHeightWithSpacing() * 1.6f;

    // ---- preview (left) ----
    const float previewW = ImGui::GetContentRegionAvail().x * 0.56f;
    ImGui::BeginChild("##importpreview", ImVec2(previewW, -footer), false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    {
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const float h = std::max(avail.y - ImGui::GetFrameHeightWithSpacing() * 2.2f,
                                 scaled(120));
        Viewport::ImportPreviewDesc d;
        d.version = modelImportVersion_;
        for (const modelimport::Image& im : src.images)
            d.textures.push_back({im.preview.data(), im.pw, im.ph});
        for (size_t mi = 0; mi < modelImportPlan_.size(); ++mi) {
            const modelimport::Planned& pl = modelImportPlan_[mi];
            const bool inc = mi >= opt.include.size() || opt.include[mi];
            for (const modelimport::Piece& pc : pl.pieces) {
                Viewport::ImportPreviewMesh m;
                m.verts = &pc.verts;
                if (pc.material >= 0 && pc.material < (int)src.materials.size()) {
                    const modelimport::Material& mt = src.materials[(size_t)pc.material];
                    m.texture = mt.image;
                    for (int a = 0; a < 3; ++a) m.tint[a] = mt.kd[a];
                }
                if (!inc)
                    for (float& t : m.tint) t *= 0.25f;
                for (int a = 0; a < 3; ++a) m.offset[a] = pl.origin[(size_t)a];
                m.highlight = (int)mi == modelImportSel_;
                d.meshes.push_back(m);
            }
        }
        for (int a = 0; a < 3; ++a) d.center[a] = (src.mn[a] + src.mx[a]) * 0.5f;
        d.minY = src.mn[1];
        const float dx = src.mx[0] - src.mn[0], dy = src.mx[1] - src.mn[1],
                    dz = src.mx[2] - src.mn[2];
        d.radius = 0.5f * std::sqrt(dx * dx + dy * dy + dz * dz);
        d.angleDeg = modelImportAngle_;
        d.pitchDeg = modelImportPitch_;
        d.zoom = modelImportZoom_;
        d.wireframe = modelImportWire_;
        const int pw = std::max(1, (int)avail.x), ph = std::max(1, (int)h);
        const uint32_t tex = viewport_.renderImportPreview(pw, ph, d);
        const ImVec2 at = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("Import preview", ImVec2((float)pw, (float)ph));
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
            const ImVec2 md = ImGui::GetIO().MouseDelta;
            modelImportAngle_ += md.x * 0.4f;
            modelImportPitch_ = std::clamp(modelImportPitch_ + md.y * 0.3f, -80.0f, 85.0f);
        }
        if (ImGui::IsItemHovered() && ImGui::GetIO().MouseWheel != 0.0f)
            modelImportZoom_ = std::clamp(
                modelImportZoom_ * std::pow(1.15f, ImGui::GetIO().MouseWheel), 0.05f, 40.0f);
        ImGui::GetWindowDrawList()->AddImage((ImTextureID)(intptr_t)tex, at,
                                             ImVec2(at.x + pw, at.y + ph), ImVec2(0, 1),
                                             ImVec2(1, 0));
        ImGui::Checkbox("Wireframe", &modelImportWire_);
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset view")) {
            modelImportAngle_ = 40.0f;
            modelImportPitch_ = 18.0f;
            modelImportZoom_ = 1.0f;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("drag = orbit, wheel = zoom");
        ImGui::TextDisabled("%s  |  %s, %d tris, %zu object(s), %zu material(s), %zu "
                            "texture(s)",
                            fileName.c_str(), src.format.c_str(), src.tris,
                            src.objects.size(), src.materials.size(), src.images.size());
    }
    ImGui::EndChild();
    ImGui::SameLine();

    // ---- options (right) ----
    ImGui::BeginChild("##importoptions", ImVec2(0, -footer), false);
    ImGui::SetNextItemWidth(scaled(220));
    if (ImGui::InputText("Name", modelImportName_, sizeof modelImportName_))
        replan = true;

    ImGui::SeparatorText("Use as");
    ImGui::Checkbox("Static model", &opt.writeStatic);
    prefHelp("A .obj + .mtl + PNG textures - what a Model entity places.\n"
             "From a .glb/.fbx this is the bind pose.");
    ImGui::BeginDisabled(!src.canAnimate());
    if (src.canAnimate()) {
        const std::string label =
            "Animated model (" + std::to_string(src.clips.size()) + " clip(s)" +
            (src.skinned ? ", skinned)" : ")");
        ImGui::Checkbox(label.c_str(), &opt.writeAnimated);
    } else {
        bool off = false;
        ImGui::Checkbox("Animated model", &off);
    }
    ImGui::EndDisabled();
    prefHelp(src.canAnimate()
                 ? "Keeps the .glb/.fbx skeleton and clips - what an Animated model\n"
                   "entity places. Its materials go to the same .mtl."
                 : "This file has no skeleton and no clips.");
    if (!opt.writeStatic && !opt.writeAnimated)
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "Pick at least one.");

    ImGui::SeparatorText("Split");
    {
        ImGui::BeginDisabled(src.objects.size() <= 1 || !opt.writeStatic);
        const std::string top = "One per top-level object (" +
                                std::to_string(src.groups.size()) + ")";
        const std::string each = "One per mesh (" + std::to_string(src.objects.size()) + ")";
        const char* items[] = {"Keep as one model", top.c_str(), each.c_str()};
        int split = (int)opt.split;
        ImGui::SetNextItemWidth(scaled(260));
        if (ImGui::Combo("##split", &split, items, 3)) {
            opt.split = (modelimport::Split)split;
            replan = true;
        }
        ImGui::EndDisabled();
        prefHelp("A pack often ships many props in one file. Splitting writes\n"
                 "each as its own model into res/models/<name>/, sharing one\n"
                 ".mtl and the textures.");
    }
    if (modelImportPlan_.size() > 1) {
        int included = 0;
        for (char c : opt.include) included += c != 0;
        ImGui::Text("%d of %zu model(s)", included, modelImportPlan_.size());
        ImGui::SameLine();
        if (ImGui::SmallButton("All")) std::fill(opt.include.begin(), opt.include.end(), 1);
        ImGui::SameLine();
        if (ImGui::SmallButton("None")) std::fill(opt.include.begin(), opt.include.end(), 0);
        ImGui::BeginChild("##importlist", ImVec2(0, scaled(150)), true);
        for (size_t i = 0; i < modelImportPlan_.size(); ++i) {
            ImGui::PushID((int)i);
            bool inc = opt.include[i] != 0;
            if (ImGui::Checkbox("##inc", &inc)) opt.include[i] = inc;
            ImGui::SameLine();
            char row[160];
            std::snprintf(row, sizeof row, "%s  (%d tris)", modelImportPlan_[i].name.c_str(),
                          modelImportPlan_[i].tris);
            if (ImGui::Selectable(row, modelImportSel_ == (int)i))
                modelImportSel_ = modelImportSel_ == (int)i ? -1 : (int)i;
            ImGui::PopID();
        }
        ImGui::EndChild();
    }

    ImGui::SeparatorText("Pivot and size");
    {
        int pivot = (int)opt.pivot;
        const char* items[] = {"As authored", "Bottom centre", "Centre"};
        ImGui::SetNextItemWidth(scaled(180));
        if (ImGui::Combo("Pivot", &pivot, items, 3)) {
            opt.pivot = (modelimport::Pivot)pivot;
            replan = true;
        }
        prefHelp("Where the object's origin goes. Bottom centre stands a\n"
                 "model on whatever it is placed on.");
        const char* units[] = {"Meters", "Centimeters", "Inches", "Custom"};
        ImGui::SetNextItemWidth(scaled(180));
        if (ImGui::Combo("Source units", &modelImportUnit_, units, 4) && modelImportUnit_ < 3)
            modelImportMeters_ = kUnitPresetMeters[modelImportUnit_];
        prefHelp(".glb/.fbx already arrive in meters; an .obj carries no unit.");
        // the height of the selected model, else of the whole file
        const float srcH = modelImportSel_ >= 0
                               ? modelImportPlan_[(size_t)modelImportSel_].mx[1] -
                                     modelImportPlan_[(size_t)modelImportSel_].mn[1]
                               : src.mx[1] - src.mn[1];
        if (srcH > 1e-6f) {
            float realH = srcH * modelImportMeters_;
            ImGui::SetNextItemWidth(scaled(180));
            if (ImGui::DragFloat(modelImportSel_ >= 0 ? "Height of selected (m)"
                                                      : "Real height (m)",
                                 &realH, 0.01f, 0.001f, 10000.0f, "%.3f m") &&
                realH > 0.0001f) {
                modelImportMeters_ = realH / srcH;
                modelImportUnit_ = unitPresetIndex(modelImportMeters_);
            }
        }
        if (modelImportUnit_ == 3) {
            ImGui::SetNextItemWidth(scaled(180));
            if (ImGui::DragFloat("Meters per unit", &modelImportMeters_, 0.001f, 0.00001f,
                                 10000.0f, "%.5f", ImGuiSliderFlags_Logarithmic))
                modelImportUnit_ = unitPresetIndex(modelImportMeters_);
        }
        const float s = modelImportMeters_ * project_.settings.unitsPerMeter;
        ImGui::TextDisabled("Inserted at scale %.3f (%.3f x %.3f x %.3f units)", s,
                            (src.mx[0] - src.mn[0]) * s, (src.mx[1] - src.mn[1]) * s,
                            (src.mx[2] - src.mn[2]) * s);
    }

    ImGui::SeparatorText("Optimization");
    if (ImGui::Checkbox("Clean up", &opt.cleanup)) replan = true;
    prefHelp("Drops zero-area and duplicated triangles.");
    {
        int pct = (int)std::lround(opt.keep * 100.0f);
        ImGui::SetNextItemWidth(scaled(180));
        ImGui::SliderInt("Keep triangles", &pct, 5, 100, "%d%%");
        opt.keep = pct / 100.0f;
        if (ImGui::IsItemDeactivatedAfterEdit()) replan = true;
        prefHelp("Reduces the mesh (the same decimator as the build's LOD\n"
                 "tiers). 100% = untouched.");
        const int sizes[] = {128, 256, 512, 1024, 0};
        const char* sizeNames[] = {"128", "256", "512", "1024", "Original"};
        int cur = 4;
        for (int i = 0; i < 5; ++i)
            if (sizes[i] == opt.maxTexture) cur = i;
        ImGui::SetNextItemWidth(scaled(180));
        if (ImGui::Combo("Max texture size", &cur, sizeNames, 5)) opt.maxTexture = sizes[cur];
        prefHelp("Longest side written to res/. The PS2 tops out at 512, and\n"
                 "the build resizes anything bigger anyway.");
    }
    {
        int tris = 0, list = 0, strip = 0;
        for (size_t i = 0; i < modelImportPlan_.size(); ++i) {
            if (i < opt.include.size() && !opt.include[i]) continue;
            tris += modelImportPlan_[i].tris;
            list += modelImportPlan_[i].listVerts;
            strip += modelImportPlan_[i].stripVerts;
        }
        ImGui::Text("%d triangles (source %d)", tris, src.tris);
        ImGui::Text("VU1 vertices: %d", strip);
        if (strip < list) {
            ImGui::SameLine();
            ImGui::TextDisabled("(strips, %d%% of a list)", list ? strip * 100 / list : 100);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("What the build ships: the same triangle-strip pass\n"
                              "every static model goes through, run here so the\n"
                              "number is the bake's own.");
    }

    ImGui::SeparatorText("Materials");
    ImGui::Text("%zu material(s), %zu texture(s)", src.materials.size(), src.images.size());
    if (src.missingTextures() > 0) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f), "%d texture(s) not found",
                           src.missingTextures());
        if (ImGui::IsItemHovered()) {
            std::string list;
            int n = 0;
            for (const modelimport::Material& m : src.materials)
                if (!m.missingTexture.empty() && n++ < 24)
                    list += m.missingTexture + "  (" + m.name + ")\n";
            ImGui::SetTooltip("%s", list.c_str());
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Search folder...")) {
            const std::string dir = pickFolder();
            if (!dir.empty()) {
                modelImportTexDirs_.push_back(dir);
                modelImportPhase_ = ImportPhase::Idle;  // let importModelFile reload
                modelImporting_ = false;
                const std::string file = modelImportFile_, target = modelImportTarget_;
                importModelFile(file, target);
                ImGui::EndChild();
                ImGui::EndPopup();
                return;
            }
        }
    }
    for (size_t i = 0; i < src.warnings.size() && i < 4; ++i)
        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "%s", src.warnings[i].c_str());

    ImGui::EndChild();

    if (replan) modelImportReplan();

    // ---- footer ----
    ImGui::Separator();
    const std::string stem = modelimport::safeName(modelImportName_);
    std::string base = "res/models";
    if (startsWith(modelImportTarget_, "res/models/")) base = modelImportTarget_;
    const bool split = opt.writeStatic && modelImportPlan_.size() > 1;
    const std::string dest = split ? base + "/" + stem : base;
    // the scene options live in the footer so they are on screen however
    // long the options column grows
    if (modelImportFor_ == ModelImportFor::Nothing) {
        ImGui::Checkbox("Add to the scene", &modelImportAddToScene_);
        ImGui::SameLine();
    }
    if ((modelImportAddToScene_ || modelImportFor_ == ModelImportFor::NewStatic) && split) {
        ImGui::Checkbox("at the file's layout", &modelImportLayout_);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Every model where the file had it, as one editor group.\n"
                              "Off: only the first model is placed.");
        ImGui::SameLine();
    }
    ImGui::TextDisabled("Into %s/", dest.c_str());
    ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - scaled(250));
    const bool can = (opt.writeStatic || opt.writeAnimated) && !modelImportPlan_.empty();
    ImGui::BeginDisabled(!can);
    if (ImGui::Button("Import", ImVec2(scaled(120), 0))) {
        opt.baseName = stem;
        if (!src.canAnimate()) opt.writeAnimated = false;
        modelImportPhase_ = ImportPhase::Writing;
        modelImportWriteError_.clear();
        modelImportDone_.store(false, std::memory_order_relaxed);
        modelImportThread_ = std::thread([this, dest]() {
            modelimport::Result res;
            std::string err;
            if (!modelimport::write(*modelImportSrc_, modelImportPlan_, modelImportOpt_,
                                    modelImportProjectDir_, dest, res, err))
                modelImportWriteError_ = err.empty() ? "write failed" : err;
            modelImportResult_ = std::move(res);
            modelImportDone_.store(true, std::memory_order_release);
        });
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(scaled(120), 0))) {
        modelImportPhase_ = ImportPhase::Idle;
        modelImporting_ = false;
        modelImportFor_ = ModelImportFor::Nothing;
        modelImportSrc_.reset();
        modelImportPlan_.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// --- model picker -------------------------------------------------------------

void App::openModelPicker(bool animated, const std::string& objectId) {
    modelPickOpen_ = true;
    modelPickAnimated_ = animated;
    modelPickObjectId_ = objectId;
    modelPickFilter_[0] = 0;
    modelPickItems_.clear();
    std::vector<std::string> anim = listAnimatedModelFiles();
    if (animated) {
        for (const std::string& m : anim) modelPickItems_.push_back("res/models/" + m);
    } else {
        for (const std::string& m : listAssetFiles("models", ".obj"))
            modelPickItems_.push_back("res/models/" + m);
        // a .glb/.fbx without a static twin yet: picking it makes one
        std::error_code ec;
        for (const std::string& m : anim) {
            const std::string rel = "res/models/" + m;
            if (!fs::exists(project_.filePath(modelimport::staticTwinPath(rel)), ec))
                modelPickItems_.push_back(rel);
        }
        std::sort(modelPickItems_.begin(), modelPickItems_.end());
    }
}

bool App::assignModel(SceneObject& o, const std::string& relIn, bool animated) {
    std::string rel = relIn;
    if (!animated && isAnimatedModelPath(rel)) {
        std::string err;
        rel = modelimport::staticTwin(project_.dir, relIn, err);
        if (rel.empty()) {
            statusMessage_ = "Cannot make a static model of " + relIn + ": " + err;
            return false;
        }
        assetsChanged();
    }
    if (rel == o.modelPath) return false;
    const std::string oldSibling = o.modelPath.empty()
                                       ? std::string()
                                       : modelimport::siblingMaterialPath(o.modelPath);
    o.modelPath = rel;
    o.animClip.clear();  // clip names belong to the old file
    if (o.type == PrimitiveType::Player) {
        o.playerIdleClip.clear();
        o.playerWalkClip.clear();
        o.playerRunClip.clear();
        o.playerSprintClip.clear();
        o.playerJumpClip.clear();
        o.playerBackClip.clear();
        o.playerStrafeLeftClip.clear();
        o.playerStrafeRightClip.clear();
    }
    // The material that came with the old model goes with it; one the user
    // picked on purpose (a shared library) stays.
    if (o.materialPath.empty() || o.materialPath == oldSibling) {
        o.materialPath.clear();
        const std::string sib = modelimport::siblingMaterialPath(rel);
        std::error_code ec;
        if (isAnimatedModelPath(rel) && fs::exists(project_.filePath(sib), ec))
            o.materialPath = sib;
    }
    return true;
}

void App::addModelEntity(const std::string& relIn, bool animated) {
    std::string rel = relIn;
    if (!animated && isAnimatedModelPath(rel)) {
        std::string err;
        rel = modelimport::staticTwin(project_.dir, relIn, err);
        if (rel.empty()) {
            statusMessage_ = "Cannot make a static model of " + relIn + ": " + err;
            return;
        }
        assetsChanged();
    }
    addModelObject(rel);
}

void App::drawModelPicker() {
    if (modelPickOpen_) {
        ImGui::OpenPopup("Choose model");
        modelPickOpen_ = false;
    }
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(scaled(760), scaled(540)), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Choose model", nullptr, ImGuiWindowFlags_NoSavedSettings))
        return;
    if (!hasProject_) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    ImGui::TextUnformatted(modelPickAnimated_ ? "Animated model" : "Model");
    ImGui::SameLine();
    ImGui::TextDisabled(modelPickAnimated_ ? "(.glb / .fbx - skeleton and clips)"
                                           : "(static - a .glb/.fbx gets a static copy)");
    ImGui::SetNextItemWidth(scaled(260));
    ImGui::InputTextWithHint("##modelfilter", "Search", modelPickFilter_,
                             sizeof modelPickFilter_);
    ImGui::SameLine();
    if (ImGui::Button("Import new model...")) {
        const bool retarget = !modelPickObjectId_.empty();
        modelImportFor_ = retarget ? ModelImportFor::Retarget
                          : modelPickAnimated_ ? ModelImportFor::NewAnimated
                                               : ModelImportFor::NewStatic;
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        const std::string file = pickModelFile();
        if (file.empty())
            modelImportFor_ = ModelImportFor::Nothing;
        else
            importModelFile(file, {});
        return;
    }

    const float footer = ImGui::GetFrameHeightWithSpacing() * 1.4f;
    ImGui::BeginChild("##modelgrid", ImVec2(0, -footer), true);
    const std::string filter = lowerOf(modelPickFilter_);
    const float tile = scaled(116);
    const int cols = std::max(1, (int)(ImGui::GetContentRegionAvail().x / (tile + 8)));
    int col = 0, shown = 0;
    std::string chosen;
    assetThumbBudget_ = 4;
    for (const std::string& rel : modelPickItems_) {
        const std::string name = fs::path(rel).filename().string();
        if (!filter.empty() && lowerOf(rel).find(filter) == std::string::npos) continue;
        ImGui::PushID(rel.c_str());
        if (col > 0) ImGui::SameLine();
        ImGui::BeginGroup();
        const bool baked = viewport_.assetThumb(rel, false) != 0;
        const uint32_t tex = baked ? viewport_.assetThumb(rel, false)
                                   : viewport_.assetThumb(rel, assetThumbBudget_ > 0);
        if (!baked && tex) --assetThumbBudget_;
        // The label IS the id, so the picker is scriptable by file name;
        // drawn at the bottom, under the thumbnail.
        ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.5f, 1.0f));
        if (ImGui::Selectable(name.c_str(), false, 0, ImVec2(tile, tile))) chosen = rel;
        ImGui::PopStyleVar();
        const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        if (tex)
            ImGui::GetWindowDrawList()->AddImage(
                (ImTextureID)(intptr_t)tex, ImVec2(a.x + 4, a.y + 4),
                ImVec2(b.x - 4, b.y - 4 - ImGui::GetTextLineHeight()), ImVec2(0, 1),
                ImVec2(1, 0));
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s%s", rel.c_str(),
                              !modelPickAnimated_ && isAnimatedModelPath(rel)
                                  ? "\nA static copy (.obj) is made next to it."
                                  : "");
        ImGui::EndGroup();
        ImGui::PopID();
        col = (col + 1) % cols;
        ++shown;
    }
    if (shown == 0)
        ImGui::TextDisabled(modelPickItems_.empty() ? "No models yet - Import new model..."
                                                    : "Nothing matches.");
    ImGui::EndChild();
    if (ImGui::Button("Cancel", ImVec2(scaled(120), 0))) ImGui::CloseCurrentPopup();

    if (!chosen.empty()) {
        if (modelPickObjectId_.empty()) {
            addModelEntity(chosen, modelPickAnimated_);
        } else {
            for (SceneObject& o : project_.objects())
                if (o.id == modelPickObjectId_ && assignModel(o, chosen, modelPickAnimated_))
                    commitChange();
        }
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}
