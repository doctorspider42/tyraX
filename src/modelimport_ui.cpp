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
#include <map>
#include <set>

#include <imgui.h>

#include "theme.hpp"
#include "toolkit.hpp"

namespace fs = std::filesystem;

namespace {

bool startsWith(const std::string& s, const std::string& p) {
    return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}

std::string lowerOf(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

// The planned model whose triangle the ray hits first (-1 = none). Each model's
// pieces are pivot-relative, so the ray is taken into its frame by its origin.
int pickPlannedModel(const std::vector<modelimport::Planned>& plan, const float ro[3],
                     const float rd[3]) {
    int best = -1;
    float bestT = 1e30f;
    for (size_t mi = 0; mi < plan.size(); ++mi) {
        const modelimport::Planned& pl = plan[mi];
        const float o[3] = {ro[0] - pl.origin[0], ro[1] - pl.origin[1],
                            ro[2] - pl.origin[2]};
        for (const modelimport::Piece& pc : pl.pieces)
            for (size_t t = 0; t + 23 < pc.verts.size(); t += 24) {
                // Moller-Trumbore, both faces
                const float* a = &pc.verts[t];
                const float* b = &pc.verts[t + 8];
                const float* c = &pc.verts[t + 16];
                const float e1[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
                const float e2[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
                const float p[3] = {rd[1] * e2[2] - rd[2] * e2[1],
                                    rd[2] * e2[0] - rd[0] * e2[2],
                                    rd[0] * e2[1] - rd[1] * e2[0]};
                const float det = e1[0] * p[0] + e1[1] * p[1] + e1[2] * p[2];
                if (std::fabs(det) < 1e-12f) continue;
                const float inv = 1.0f / det;
                const float s[3] = {o[0] - a[0], o[1] - a[1], o[2] - a[2]};
                const float u = (s[0] * p[0] + s[1] * p[1] + s[2] * p[2]) * inv;
                if (u < 0.0f || u > 1.0f) continue;
                const float q[3] = {s[1] * e1[2] - s[2] * e1[1],
                                    s[2] * e1[0] - s[0] * e1[2],
                                    s[0] * e1[1] - s[1] * e1[0]};
                const float v = (rd[0] * q[0] + rd[1] * q[1] + rd[2] * q[2]) * inv;
                if (v < 0.0f || u + v > 1.0f) continue;
                const float tt = (e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]) * inv;
                if (tt > 1e-6f && tt < bestT) {
                    bestT = tt;
                    best = (int)mi;
                }
            }
    }
    return best;
}

// A folder typed into the import window, made safe and project-relative:
// "terrain\\Rocks/" -> "res/models/terrain/Rocks"; "res/props" stays under
// res/. Empty, "." and ".." segments are dropped (the PS2 cannot walk "..").
std::string normalizeModelFolder(const std::string& in) {
    std::string path = in;
    for (char& c : path)
        if (c == '\\') c = '/';
    std::vector<std::string> parts;
    size_t start = 0;
    while (start <= path.size()) {
        const size_t slash = path.find('/', start);
        std::string seg = path.substr(start, slash == std::string::npos ? std::string::npos
                                                                        : slash - start);
        while (!seg.empty() && seg.front() == ' ') seg.erase(seg.begin());
        while (!seg.empty() && seg.back() == ' ') seg.pop_back();
        if (!seg.empty() && seg != "." && seg != "..") parts.push_back(modelimport::safeName(seg));
        if (slash == std::string::npos) break;
        start = slash + 1;
    }
    if (parts.empty() || parts[0] != "res") {
        if (!parts.empty() && parts[0] == "models") parts.erase(parts.begin());
        parts.insert(parts.begin(), {"res", "models"});
    }
    std::string out;
    for (const std::string& p : parts) out += (out.empty() ? "" : "/") + p;
    return out;
}

// Files the picker never offers: build products and derived models (a
// distant version, a procedural chunk), which a scene never places by hand.
bool pickableModel(const std::string& r, bool generated) {
    if (generated) return false;
    const std::string name = fs::path(r).filename().string();
    return r.find("/impostors/") == std::string::npos &&
           r.find("/proxies/") == std::string::npos && !startsWith(name, "procgen-") &&
           name.find("-impostor.") == std::string::npos;
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
        modelImportPanX_ = modelImportPanY_ = 0.0f;
        modelImportClip_ = 0;
        modelImportClipPlay_ = false;
        modelImportClipTime_ = 0.0f;
        modelImportAddToScene_ = modelImportFor_ == ModelImportFor::NewStatic ||
                                 modelImportFor_ == ModelImportFor::NewAnimated;
        const std::string stem =
            modelimport::safeName(fs::path(path).stem().string());
        std::snprintf(modelImportName_, sizeof modelImportName_, "%s", stem.c_str());
        // the Asset Browser folder that asked, else the last one used
        const std::string folder =
            startsWith(targetFolder, "res/") ? targetFolder : modelImportLastFolder_;
        std::snprintf(modelImportFolder_, sizeof modelImportFolder_, "%s", folder.c_str());
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
            // A skinned file is a character: by default it imports animated
            // ONLY - a frozen bind-pose .obj beside it is a prop nobody asked
            // for (it used to be written silently). Rigid animation (clips,
            // no skin) keeps both forms; a Model... pick asks for static.
            modelImportOpt_.writeAnimated =
                s.canAnimate() && modelImportFor_ != ModelImportFor::NewStatic;
            modelImportOpt_.writeStatic =
                modelImportFor_ == ModelImportFor::NewStatic || !s.canAnimate() ||
                (!s.skinned && modelImportFor_ != ModelImportFor::NewAnimated);
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
    modelImportLastFolder_ = normalizeModelFolder(modelImportFolder_);
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
    ImGui::SetNextWindowSize(ImVec2(scaled(1100), scaled(760)), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Import model", nullptr, ImGuiWindowFlags_NoSavedSettings))
        return;

    const theme::Semantics& sem = theme::semantics();
    const float fs = ImGui::GetFontSize();
    const std::string fileName = fs::path(modelImportFile_).filename().string();
    auto busy = [&](const char* what) {
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + avail.x * 0.2f,
                                   ImGui::GetCursorPosY() + avail.y * 0.42f));
        ImGui::BeginChild("##importbusy", ImVec2(avail.x * 0.6f, fs * 3.0f));
        ui::ProgressInline(-1.0f, what, fileName.c_str());
        ImGui::EndChild();
    };
    if (modelImportPhase_ == ImportPhase::Idle) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    if (modelImportPhase_ == ImportPhase::Loading) {
        busy("Reading model...");
        ImGui::EndPopup();
        return;
    }
    if (modelImportPhase_ == ImportPhase::Writing) {
        busy("Writing model, material and textures...");
        ImGui::EndPopup();
        return;
    }
    if (!modelImportSrc_) {
        ImGui::Dummy(ImVec2(0, ImGui::GetContentRegionAvail().y * 0.3f));
        ui::EmptyState(ui::Glyph::Box, ("Cannot read " + fileName).c_str(),
                       modelImportError_.c_str());
        const float bw = scaled(170), sp = ImGui::GetStyle().ItemSpacing.x;
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - bw * 2.0f - sp) * 0.5f);
        if (ImGui::Button("Close", ImVec2(bw, 0))) {
            modelImportPhase_ = ImportPhase::Idle;
            modelImporting_ = false;
            modelImportFor_ = ModelImportFor::Nothing;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ui::PrimaryButton("Choose another file...", bw)) {
            modelImportPhase_ = ImportPhase::Idle;
            modelImporting_ = false;
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            importModelAsset(modelImportTarget_);
            return;
        }
        ImGui::EndPopup();
        return;
    }

    const modelimport::Source& src = *modelImportSrc_;
    modelimport::Options& opt = modelImportOpt_;
    bool replan = false;

    // ---- header: the file is the title, its facts a badge ----
    {
        ImGui::AlignTextToFramePadding();
        {
            ui::FontScope b(ui::boldFont());
            ImGui::TextUnformatted(fileName.c_str());
        }
        ImGui::SameLine();
        char facts[160];
        std::snprintf(facts, sizeof facts, "%s - %d tris - %zu object%s - %zu texture%s",
                      src.format.c_str(), src.tris, src.objects.size(),
                      src.objects.size() == 1 ? "" : "s", src.images.size(),
                      src.images.size() == 1 ? "" : "s");
        ImGui::BeginGroup();
        ImGui::Dummy(ImVec2(0, (ImGui::GetFrameHeight() - ImGui::GetTextLineHeight()) * 0.5f -
                                   ImGui::GetStyle().ItemSpacing.y));
        {
            ui::FontScope m(ui::monoFont());
            ui::Badge(facts);
        }
        ImGui::EndGroup();
        const float cw = ImGui::CalcTextSize("Choose another file").x +
                         ImGui::GetStyle().FramePadding.x * 2.6f;
        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - cw);
        if (ui::GhostButton("Choose another file")) {
            modelImportPhase_ = ImportPhase::Idle;
            modelImporting_ = false;
            modelImportSrc_.reset();
            modelImportPlan_.clear();
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            importModelAsset(modelImportTarget_);
            return;
        }
    }
    ImGui::Separator();
    const float footer = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y * 3.0f + 1.0f;

    // ---- preview (left): the hero ----
    const float optionsW = scaled(380);
    const float previewW = std::max(scaled(240), ImGui::GetContentRegionAvail().x - optionsW -
                                                     ImGui::GetStyle().ItemSpacing.x);
    ImGui::BeginChild("##importpreview", ImVec2(previewW, -footer), false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    {
        const ImVec2 avail = ImGui::GetContentRegionAvail();
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
        d.panX = modelImportPanX_;
        d.panY = modelImportPanY_;
        d.wireframe = modelImportWire_;
        const int pw = std::max(1, (int)avail.x), ph = std::max(1, (int)avail.y);
        // Clip playback: the SOURCE file posed by the Animation Editor's
        // preview (bake keyed by the absolute path, so nothing is written and
        // the project's caches are not touched). The bake runs in the
        // background; until it lands the planned bind pose stays on screen.
        if (modelImportClip_ >= (int)src.clips.size()) modelImportClip_ = 0;
        std::string bakedClip;
        float clipDur = 0.0f;
        bool clipReady = false;
        if (!src.clips.empty()) {
            const std::vector<std::string> names = viewport_.animClipNames(modelImportFile_, "");
            if (!names.empty()) {
                clipReady = true;
                bakedClip = names[(size_t)std::min(modelImportClip_, (int)names.size() - 1)];
                clipDur = viewport_.animClipDuration(modelImportFile_, "", bakedClip);
            }
        }
        if (modelImportClipPlay_ && clipDur > 0.0f) {
            modelImportClipTime_ += ImGui::GetIO().DeltaTime;
            if (modelImportClipTime_ > clipDur)
                modelImportClipTime_ = std::fmod(modelImportClipTime_, clipDur);
        }
        const bool posed = clipReady && (modelImportClipPlay_ || modelImportClipTime_ > 0.0f);
        uint32_t tex = 0;
        if (posed) {
            Viewport::AnimPreviewDesc ad;
            ad.modelRel = modelImportFile_;
            ad.clip = bakedClip;
            ad.time = modelImportClipTime_;
            ad.angleDeg = modelImportAngle_;
            ad.pitchDeg = modelImportPitch_;
            ad.zoom = modelImportZoom_;
            ad.panX = modelImportPanX_;
            ad.panY = modelImportPanY_;
            ad.wireframe = modelImportWire_;
            tex = viewport_.renderAnimPreview(pw, ph, ad);
        } else {
            tex = viewport_.renderImportPreview(pw, ph, d);
        }
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const ImVec2 bt(at.x + pw, at.y + ph);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddImageRounded((ImTextureID)(intptr_t)tex, at, bt, ImVec2(0, 1), ImVec2(1, 0),
                            IM_COL32_WHITE, ImGui::GetStyle().FrameRounding + 3.0f);
        // right drag = orbit, middle drag = pan, wheel = zoom (the left button
        // is left alone: it is what closes popups and clicks the panel)
        ImGui::SetNextItemAllowOverlap();
        ImGui::InvisibleButton("Import preview", ImVec2((float)pw, (float)ph),
                               ImGuiButtonFlags_MouseButtonLeft |
                                   ImGuiButtonFlags_MouseButtonRight |
                                   ImGuiButtonFlags_MouseButtonMiddle);
        // Left click picks the planned model under the cursor (split imports):
        // the nearest triangle hit selects its row in the list, empty space
        // clears the selection.
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && modelImportPlan_.size() > 1 && !posed) {
            const ImVec2 mp = ImGui::GetIO().MousePos;
            float ro[3], rd[3];
            if (viewport_.importPreviewRay((mp.x - at.x) / (float)pw,
                                           (mp.y - at.y) / (float)ph, ro, rd)) {
                modelImportSel_ = pickPlannedModel(modelImportPlan_, ro, rd);
                modelImportScrollToSel_ = modelImportSel_ >= 0;
            }
        }
        if (ImGui::IsItemActive()) {
            const ImVec2 md = ImGui::GetIO().MouseDelta;
            if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
                modelImportAngle_ += md.x * 0.4f;
                modelImportPitch_ =
                    std::clamp(modelImportPitch_ + md.y * 0.3f, -80.0f, 85.0f);
            } else if (ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
                // one preview height of drag moves the target ~2.4 radii /
                // zoom - the visible span - so the model follows the cursor
                const float k = 2.0f / ((float)ph * modelImportZoom_);
                modelImportPanX_ -= md.x * k;
                modelImportPanY_ += md.y * k;
            }
        }
        if (ImGui::IsItemHovered() && ImGui::GetIO().MouseWheel != 0.0f)
            modelImportZoom_ = std::clamp(
                modelImportZoom_ * std::pow(1.15f, ImGui::GetIO().MouseWheel), 0.05f, 40.0f);

        // Height ruler ON the model: the number the Real height field edits,
        // drawn at the model's left edge from its lowest to its highest point.
        {
            // Bounds of what is DRAWN: the planned models at their origins
            // (the pivot moves geometry; the source bounds do not).
            float bmn[3] = {1e30f, 1e30f, 1e30f}, bmx[3] = {-1e30f, -1e30f, -1e30f};
            for (const modelimport::Planned& pl : modelImportPlan_)
                for (int a = 0; a < 3; ++a) {
                    bmn[a] = std::min(bmn[a], pl.mn[a] + pl.origin[(size_t)a]);
                    bmx[a] = std::max(bmx[a], pl.mx[a] + pl.origin[(size_t)a]);
                }
            // Stand the ruler beside the silhouette at the model's own depth:
            // the centre column pushed along the camera's left by the bounds'
            // extent in that direction (a box corner would sit far in front
            // of the model at this field of view).
            float ua = 0, va = 0, ub = 0, vb = 0;
            bool found = false;
            float r0[3], d0[3], r1[3], d1[3];
            if (!posed && !modelImportPlan_.empty() && bmx[1] > bmn[1] &&
                viewport_.importPreviewRay(0.5f, 0.5f, r0, d0) &&
                viewport_.importPreviewRay(0.6f, 0.5f, r1, d1)) {
                float right[3] = {d1[0] - d0[0], 0.0f, d1[2] - d0[2]};
                const float rl = std::sqrt(right[0] * right[0] + right[2] * right[2]);
                if (rl > 1e-6f) {
                    right[0] /= rl, right[2] /= rl;
                    const float cx = (bmn[0] + bmx[0]) * 0.5f, cz = (bmn[2] + bmx[2]) * 0.5f;
                    float ext = 0.0f;
                    for (int c = 0; c < 4; ++c) {
                        const float qx = (c & 1) ? bmx[0] : bmn[0], qz = (c & 2) ? bmx[2] : bmn[2];
                        ext = std::max(ext, -((qx - cx) * right[0] + (qz - cz) * right[2]));
                    }
                    ext *= 1.35f;
                    const float qa[3] = {cx - right[0] * ext, bmn[1], cz - right[2] * ext};
                    const float qb[3] = {qa[0], bmx[1], qa[2]};
                    found = viewport_.importPreviewProject(qa, ua, va) &&
                            viewport_.importPreviewProject(qb, ub, vb);
                }
            }
            if (dy > 1e-6f && found) {
                ImVec2 sa(at.x + ua * pw, at.y + va * ph), sb(at.x + ub * pw, at.y + vb * ph);
                // A wide model close to the camera pushes the column out of
                // the frame: slide it back in sideways (heights stay true).
                const float minX = at.x + fs * 3.0f;
                if (std::min(sa.x, sb.x) < minX) {
                    const float dxs = minX - std::min(sa.x, sb.x);
                    sa.x += dxs, sb.x += dxs;
                }
                const ImU32 col = ImGui::GetColorU32(sem.ok);
                dl->PushClipRect(at, bt, true);
                dl->AddLine(sa, sb, col, 1.5f);
                dl->AddLine(ImVec2(sa.x - 4, sa.y), ImVec2(sa.x + 4, sa.y), col, 1.5f);
                dl->AddLine(ImVec2(sb.x - 4, sb.y), ImVec2(sb.x + 4, sb.y), col, 1.5f);
                char hl[32];
                std::snprintf(hl, sizeof hl, "%.2f m", dy * modelImportMeters_);
                ui::FontScope m(ui::monoFont());
                const ImVec2 ts = ImGui::CalcTextSize(hl);
                const ImVec2 mid((sa.x + sb.x) * 0.5f, (sa.y + sb.y) * 0.5f);
                const ImVec2 la(mid.x - ts.x * 0.5f - fs * 0.4f, mid.y - ts.y * 0.5f - fs * 0.2f);
                const ImVec2 lb(mid.x + ts.x * 0.5f + fs * 0.4f, mid.y + ts.y * 0.5f + fs * 0.2f);
                dl->AddRectFilled(la, lb, ImGui::GetColorU32(ImVec4(sem.ground.x, sem.ground.y, sem.ground.z, 0.85f)), fs * 0.3f);
                dl->AddRect(la, lb, ImGui::GetColorU32(ImVec4(sem.ok.x, sem.ok.y, sem.ok.z, 0.5f)), fs * 0.3f);
                dl->AddText(ImVec2(la.x + fs * 0.4f, la.y + fs * 0.2f), col, hl);
                dl->PopClipRect();
            }
        }

        // Glass toolbar (top left): Shaded | Wireframe, Reset view.
        const float m = fs * 0.7f;
        {
            const char* modes[] = {"Shaded", "Wireframe"};
            int mode = modelImportWire_ ? 1 : 0;
            const float segW = ImGui::CalcTextSize("ShadedWireframe").x +
                               ImGui::GetStyle().FramePadding.x * 4.0f + 6.0f;
            const float resetW = ImGui::CalcTextSize("Reset view").x +
                                 ImGui::GetStyle().FramePadding.x * 2.6f;
            const ImVec2 pa(at.x + m, at.y + m);
            const ImVec2 pb(pa.x + segW + resetW + fs * 1.4f, pa.y + ImGui::GetFrameHeight() + fs * 0.6f);
            ui::OverlayPanel(dl, pa, pb);
            ImGui::SetCursorScreenPos(ImVec2(pa.x + fs * 0.3f, pa.y + fs * 0.3f));
            if (ui::Segmented("##importshade", modes, 2, &mode)) modelImportWire_ = mode == 1;
            ImGui::SameLine(0, fs * 0.4f);
            if (ui::GhostButton("Reset view")) {
                modelImportAngle_ = 40.0f;
                modelImportPitch_ = 18.0f;
                modelImportZoom_ = 1.0f;
                modelImportPanX_ = modelImportPanY_ = 0.0f;
            }
        }
        // The mouse hint as a quiet pill (top right).
        {
            const char* hint = modelImportPlan_.size() > 1
                                   ? "Click select   Right-drag orbit   Middle-drag pan   Wheel zoom"
                                   : "Right-drag orbit   Middle-drag pan   Wheel zoom";
            const ImVec2 ts = ImGui::CalcTextSize(hint);
            const ImVec2 pb(bt.x - m, at.y + m + ts.y + fs * 0.7f);
            const ImVec2 pa(pb.x - ts.x - fs * 1.2f, at.y + m);
            if (pa.x > at.x + fs * 18.0f) {
                ui::OverlayPanel(dl, pa, pb);
                dl->AddText(ImVec2(pa.x + fs * 0.6f, pa.y + fs * 0.35f),
                            ImGui::GetColorU32(sem.textDim), hint);
            }
        }
        // Clip bar (bottom) for animated sources: play / pause, the clip,
        // a scrub bar and what comes along. Paused at 0 = the planned bind
        // pose (what the options shape); playing or scrubbed = the source
        // file as it animates.
        if (!src.clips.empty() || src.skinned) {
            char line[96];
            std::snprintf(line, sizeof line, "%zu clip%s%s", src.clips.size(),
                          src.clips.size() == 1 ? "" : "s", src.skinned ? " - skinned" : "");
            const float h = ImGui::GetFrameHeight() + fs * 0.6f;
            const ImVec2 pa(at.x + m, bt.y - m - h), pb(bt.x - m, bt.y - m);
            ui::OverlayPanel(dl, pa, pb);
            ImGui::SetCursorScreenPos(ImVec2(pa.x + fs * 0.3f, pa.y + fs * 0.3f));
            const ImVec2 ts = ImGui::CalcTextSize(line);
            if (!src.clips.empty()) {
                if (modelImportClipPlay_) {
                    if (ui::ToolIconButton("Pause clip", ui::ToolIcon::Pause))
                        modelImportClipPlay_ = false;
                } else if (ui::ToolIconButton("Play clip", ui::ToolIcon::Play)) {
                    modelImportClipPlay_ = true;
                }
                ImGui::SameLine(0, fs * 0.4f);
                ImGui::SetNextItemWidth(std::min(scaled(230), (pb.x - pa.x) * 0.34f));
                if (ImGui::BeginCombo("##importclips", src.clips[(size_t)modelImportClip_].c_str())) {
                    for (int i = 0; i < (int)src.clips.size(); ++i)
                        if (ImGui::Selectable(src.clips[(size_t)i].c_str(), i == modelImportClip_)) {
                            modelImportClip_ = i;
                            modelImportClipTime_ = 0.0f;
                        }
                    ImGui::EndCombo();
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                    ImGui::SetTooltip("The clips this file brings. Trim and rename them in\n"
                                      "Tools > Animation Editor after the import.");
                ImGui::SameLine(0, fs * 0.6f);
                const float sw = pb.x - ImGui::GetCursorScreenPos().x - ts.x - fs * 1.4f;
                if (sw > fs * 4.0f) {
                    char fmt[48];
                    if (clipReady)
                        std::snprintf(fmt, sizeof fmt, "%%.2f / %.2f s", clipDur);
                    else
                        std::snprintf(fmt, sizeof fmt, "%s",
                                      modelImportClipPlay_ ? "loading clip..." : "bind pose");
                    ImGui::SetNextItemWidth(sw);
                    ui::FontScope mono(ui::monoFont());
                    ImGui::BeginDisabled(!clipReady);
                    // Scrubbing pauses: the bar is where the pose is.
                    if (ImGui::SliderFloat("##cliptime", &modelImportClipTime_, 0.0f,
                                           std::max(clipDur, 0.001f), fmt))
                        modelImportClipPlay_ = false;
                    ImGui::EndDisabled();
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                        ImGui::SetTooltip("Drag to scrub. Back to 0 shows the bind pose the\n"
                                          "options above shape (Keep tris, pivot, size).");
                }
            }
            dl->AddText(ImVec2(pb.x - ts.x - fs * 0.7f, pa.y + (h - ts.y) * 0.5f),
                        ImGui::GetColorU32(sem.textDim), line);
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();

    // ---- options (right): label-left rows, short enough not to scroll ----
    ImGui::BeginChild("##importoptions", ImVec2(0, -footer), false);
    const float capW = std::clamp(ImGui::GetContentRegionAvail().x * 0.3f, scaled(86), scaled(116));
    auto caption = [&](const char* t) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", t);
        ImGui::SameLine(capW);
    };
    auto sectionGap = [&]() {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
    };
    {
        ui::InspectorRows rows(scaled(86), scaled(116));
        if (ui::InputText("Name", modelImportName_, sizeof modelImportName_)) replan = true;
    }
    // The catalogue: type a path ("res/models/terrain/rocks" - missing folders
    // are created) or pick an existing folder from the arrow.
    caption("Folder");
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight());
    ImGui::InputText("##folder", modelImportFolder_, sizeof modelImportFolder_);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("Where the model goes. Type a new path to file it into a\n"
                          "catalogue - res/models/terrain/rocks - folders are created.\n"
                          "Move it later in the Asset Browser; references follow.");
    ImGui::SameLine(0, 0);
    if (ImGui::BeginCombo("##folders", nullptr, ImGuiComboFlags_NoPreview |
                                                    ImGuiComboFlags_PopupAlignLeft |
                                                    ImGuiComboFlags_HeightLarge)) {
        if (assetDirs_.empty()) scanAssetTree();
        for (const auto& [rel, dir] : assetDirs_) {
            if (rel == "res") continue;
            const int depth = (int)std::count(rel.begin(), rel.end(), '/') - 1;
            const std::string label =
                std::string((size_t)std::max(0, depth) * 2, ' ') + dir.name + "##" + rel;
            if (ImGui::Selectable(label.c_str(), rel == modelImportFolder_))
                std::snprintf(modelImportFolder_, sizeof modelImportFolder_, "%s",
                              rel.c_str());
        }
        ImGui::EndCombo();
    }

    // Use as: two switches whose descriptions say what each one WRITES.
    sectionGap();
    {
        const float y = ImGui::GetCursorPosY();
        ui::Caption("Use as");
        const char* rule = "pick at least one";
        const ImVec2 ts = ImGui::CalcTextSize(rule);
        const ImVec2 after = ImGui::GetCursorPos();
        ImGui::SetCursorPos(ImVec2(ImGui::GetWindowContentRegionMax().x - ts.x, y - fs * 0.15f));
        ImGui::PushStyleColor(ImGuiCol_Text, (!opt.writeStatic && !opt.writeAnimated) ? sem.warn
                                                                                       : sem.textDim);
        ImGui::TextUnformatted(rule);
        ImGui::PopStyleColor();
        ImGui::SetCursorPos(after);
    }
    {
        ui::InspectorRows rows(scaled(86), scaled(116));
        const std::string ext = src.format.empty() ? "file" : "." + src.format;
        if (src.canAnimate()) {
            const std::string label = "Animated model (Keeps the " + ext + " skeleton and its " +
                                      std::to_string(src.clips.size()) + " clip" +
                                      (src.clips.size() == 1 ? "" : "s") +
                                      " - for characters)";
            ui::Switch(label.c_str(), &opt.writeAnimated);
            const std::string sl = "Static model (Also writes a frozen .obj in the bind pose - "
                                   "for props)";
            ui::Switch(sl.c_str(), &opt.writeStatic);
        } else {
            ui::Switch("Static model (A .obj + .mtl + PNG textures - what a Model places)",
                       &opt.writeStatic);
            ImGui::BeginDisabled();
            bool off = false;
            ui::Switch("Animated model (This file has no skeleton and no clips)", &off);
            ImGui::EndDisabled();
        }
    }

    // Split (multi-object files only): the model list between Use as and Size.
    if (src.objects.size() > 1) {
        sectionGap();
        ui::Caption("Split");
        caption("Models");
        ImGui::BeginDisabled(!opt.writeStatic);
        const std::string top = "One per top-level object (" +
                                std::to_string(src.groups.size()) + ")";
        const std::string each = "One per mesh (" + std::to_string(src.objects.size()) + ")";
        const char* items[] = {"Keep as one model", top.c_str(), each.c_str()};
        int split = (int)opt.split;
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::Combo("##split", &split, items, 3)) {
            opt.split = (modelimport::Split)split;
            replan = true;
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("A pack often ships many props in one file. Splitting writes\n"
                              "each as its own model into res/models/<name>/, sharing one\n"
                              ".mtl and the textures.");
        if (modelImportPlan_.size() > 1) {
            int included = 0;
            for (char c : opt.include) included += c != 0;
            ImGui::TextDisabled("%d of %zu model(s)", included, modelImportPlan_.size());
            ImGui::SameLine();
            if (ImGui::SmallButton("All")) std::fill(opt.include.begin(), opt.include.end(), 1);
            ImGui::SameLine();
            if (ImGui::SmallButton("None")) std::fill(opt.include.begin(), opt.include.end(), 0);
            ImGui::BeginChild("##importlist", ImVec2(0, scaled(130)), true);
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
                if (modelImportScrollToSel_ && modelImportSel_ == (int)i) {
                    ImGui::SetScrollHereY(0.5f);
                    modelImportScrollToSel_ = false;
                }
                ImGui::PopID();
            }
            ImGui::EndChild();
        }
    }

    // Size & pivot: one decision (how big is it) in one row.
    sectionGap();
    ui::Caption("Size & pivot");
    {
        caption("Pivot");
        int pivot = (int)opt.pivot;
        const char* pivots[] = {"Authored", "Bottom", "Centre"};
        if (ui::Segmented("##importpivot", pivots, 3, &pivot, -1.0f)) {
            opt.pivot = (modelimport::Pivot)pivot;
            replan = true;
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("Where the object's origin goes. Bottom stands a model on\n"
                              "whatever it is placed on.");
        // the height of the selected model, else of the whole file
        const float srcH = modelImportSel_ >= 0
                               ? modelImportPlan_[(size_t)modelImportSel_].mx[1] -
                                     modelImportPlan_[(size_t)modelImportSel_].mn[1]
                               : src.mx[1] - src.mn[1];
        caption(modelImportSel_ >= 0 ? "Selected" : "Real height");
        const float unitW = scaled(122);
        const float sp = ImGui::GetStyle().ItemSpacing.x;
        if (srcH > 1e-6f) {
            float realH = srcH * modelImportMeters_;
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - unitW - sp);
            if (ImGui::DragFloat("##realheight", &realH, 0.01f, 0.001f, 10000.0f, "%.3f m") &&
                realH > 0.0001f) {
                modelImportMeters_ = realH / srcH;
                modelImportUnit_ = unitPresetIndex(modelImportMeters_);
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                ImGui::SetTooltip("The real height in meters - the green ruler on the model.");
            ImGui::SameLine();
        }
        const char* units[] = {"file: meters", "file: cm", "file: inches", "file: custom"};
        ImGui::SetNextItemWidth(srcH > 1e-6f ? unitW : -FLT_MIN);
        if (ImGui::Combo("##sourceunits", &modelImportUnit_, units, 4) && modelImportUnit_ < 3)
            modelImportMeters_ = kUnitPresetMeters[modelImportUnit_];
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("What one unit of the file measures. .glb/.fbx already arrive\n"
                              "in meters; an .obj carries no unit.");
        if (modelImportUnit_ == 3) {
            caption("Meters / unit");
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::DragFloat("##meterspu", &modelImportMeters_, 0.001f, 0.00001f, 10000.0f,
                                 "%.5f", ImGuiSliderFlags_Logarithmic))
                modelImportUnit_ = unitPresetIndex(modelImportMeters_);
        }
        const float s = modelImportMeters_ * project_.settings.unitsPerMeter;
        ImGui::SetCursorPosX(capW);
        ui::FontScope mono(ui::monoFont());
        ImGui::TextDisabled("%.2f x %.2f x %.2f m at scale %.3f",
                            (src.mx[0] - src.mn[0]) * modelImportMeters_,
                            (src.mx[1] - src.mn[1]) * modelImportMeters_,
                            (src.mx[2] - src.mn[2]) * modelImportMeters_, s);
    }

    // PS2 cost: the levers, with a verdict where the number needs one.
    sectionGap();
    ui::Caption("PS2 cost");
    {
        int tris = 0, list = 0, strip = 0;
        for (size_t i = 0; i < modelImportPlan_.size(); ++i) {
            if (i < opt.include.size() && !opt.include[i]) continue;
            tris += modelImportPlan_[i].tris;
            list += modelImportPlan_[i].listVerts;
            strip += modelImportPlan_[i].stripVerts;
        }
        caption("Keep tris");
        int pct = (int)std::lround(opt.keep * 100.0f);
        char fmt[64];
        std::snprintf(fmt, sizeof fmt, "%d / %d", tris, src.tris);
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::SliderInt("##keeptris", &pct, 5, 100, fmt);
        opt.keep = pct / 100.0f;
        if (ImGui::IsItemDeactivatedAfterEdit()) replan = true;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("Keeps %d%% of the triangles (the same decimator as the build's\n"
                              "LOD tiers). The count is what is kept / the source.", pct);
        caption("Textures");
        const int sizes[] = {128, 256, 512, 1024, 0};
        const char* sizeNames[] = {"128", "256", "512", "1024", "Original"};
        int cur = 4;
        for (int i = 0; i < 5; ++i)
            if (sizes[i] == opt.maxTexture) cur = i;
        if (ui::Segmented("##maxtexture", sizeNames, 5, &cur, -1.0f)) opt.maxTexture = sizes[cur];
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("Longest texture side written to res/. The PS2 tops out at 512,\n"
                              "and the build resizes anything bigger anyway.");
        {
            ui::InspectorRows rows(scaled(86), scaled(116));
            if (ui::Switch("Clean up (Drop zero-area and duplicate triangles)", &opt.cleanup))
                replan = true;
        }
        // The VU1 cost: 3 vertices per triangle means nothing stripped -
        // docs/model-pipeline.md's flat-shaded case.
        const float perTri = tris > 0 ? (float)strip / (float)tris : 0.0f;
        char vu[200];
        if (perTri > 2.5f) {
            std::snprintf(vu, sizeof vu,
                          "%d VU1 vertices - %.1f per tri: flat-shaded meshes rarely strip.",
                          strip, perTri);
            if (ui::NoteCard(1, vu))
                ImGui::SetTooltip("Every triangle sends its own three vertices: hard edges and\n"
                                  "split UVs break triangle strips. Smooth normals and fewer\n"
                                  "seams let the build strip it (docs/model-pipeline.md).");
        } else {
            ImGui::SetCursorPosX(capW);
            ui::FontScope mono(ui::monoFont());
            ImGui::TextDisabled("%d VU1 vertices - %.1f per tri%s", strip, perTri,
                                strip < list ? " (strips)" : "");
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                ImGui::SetTooltip("What the build ships: the same triangle-strip pass every\n"
                                  "static model goes through, run here so the number is the\n"
                                  "bake's own.");
        }
    }

    // Materials: one status line - ok, or what is missing and the fix.
    sectionGap();
    {
        const float y = ImGui::GetCursorPosY();
        ui::Caption("Materials", std::to_string(src.materials.size()).c_str());
        const ImVec2 after = ImGui::GetCursorPos();
        const int missing = src.missingTextures();
        char chip[64];
        if (missing > 0)
            std::snprintf(chip, sizeof chip, "%d texture%s missing", missing, missing == 1 ? "" : "s");
        else
            std::snprintf(chip, sizeof chip, "nothing missing");
        const float chipW = ImGui::CalcTextSize(chip).x * 0.82f + fs;
        ImGui::SetCursorPos(ImVec2(ImGui::GetWindowContentRegionMax().x - chipW, y));
        ui::Badge(chip, missing > 0 ? 1 : 0);
        if (missing > 0 && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
            std::string list;
            int n = 0;
            for (const modelimport::Material& mt : src.materials)
                if (!mt.missingTexture.empty() && n++ < 24)
                    list += mt.missingTexture + "  (" + mt.name + ")\n";
            ImGui::SetTooltip("%s", list.c_str());
        }
        ImGui::SetCursorPos(after);
        if (missing > 0) {
            ImGui::PushTextWrapPos(ImGui::GetWindowContentRegionMax().x);
            ImGui::TextDisabled("Missing textures render white. Point the importer at the "
                                "folder that has them:");
            ImGui::PopTextWrapPos();
            if (ImGui::Button("Search folder...")) {
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
        for (size_t i = 0; i < src.warnings.size() && i < 4; ++i) ui::NoteCard(1, src.warnings[i].c_str());
        ImGui::Dummy(ImVec2(1, 1));
    }
    ImGui::EndChild();

    if (replan) modelImportReplan();

    // ---- footer: scene switch, what it writes, Cancel + the one action ----
    ImGui::Separator();
    const std::string stem = modelimport::safeName(modelImportName_);
    const std::string base = normalizeModelFolder(modelImportFolder_);
    const bool split = opt.writeStatic && modelImportPlan_.size() > 1;
    const std::string dest = split ? base + "/" + stem : base;
    ImGui::AlignTextToFramePadding();
    if (modelImportFor_ == ModelImportFor::Nothing) {
        ui::Switch("Add to the scene", &modelImportAddToScene_);
        ImGui::SameLine(0, fs);
    }
    if ((modelImportAddToScene_ || modelImportFor_ == ModelImportFor::NewStatic) && split) {
        ui::Switch("at the file's layout", &modelImportLayout_);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("Every model where the file had it, as one editor group.\n"
                              "Off: only the first model is placed.");
        ImGui::SameLine(0, fs);
    }
    // What will be written - the import's consequence, stated before it runs.
    std::string writes;
    {
        int nStatic = 0;
        for (size_t i = 0; i < modelImportPlan_.size(); ++i)
            nStatic += (i >= opt.include.size() || opt.include[i]) ? 1 : 0;
        const bool anim = opt.writeAnimated && src.canAnimate();
        if (anim) writes = dest + "/" + stem + "." + src.format;
        if (opt.writeStatic) {
            if (!writes.empty()) writes += " + ";
            writes += split ? std::to_string(nStatic) + " .obj in " + dest + "/"
                            : (anim ? stem + ".obj" : dest + "/" + stem + ".obj");
        }
        if (!writes.empty()) {
            writes += " + " + stem + ".mtl";
            if (!src.images.empty())
                writes += " + " + std::to_string(src.images.size()) + " texture" +
                          (src.images.size() == 1 ? "" : "s");
            if (anim && !src.clips.empty())
                writes += " - " + std::to_string(src.clips.size()) + " clip" +
                          (src.clips.size() == 1 ? "" : "s");
            writes = "writes " + writes;
        } else {
            writes = "writes nothing - pick Animated or Static";
        }
    }
    const float cancelW = scaled(96), importW = scaled(150);
    const float sp = ImGui::GetStyle().ItemSpacing.x;
    const float right = ImGui::GetWindowContentRegionMax().x;
    {
        ui::FontScope mono(ui::monoFont());
        const float room = right - cancelW - importW - sp * 3.0f - ImGui::GetCursorPosX();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(std::max(1.0f, room), ImGui::GetFrameHeight()));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->PushClipRect(p, ImVec2(p.x + room, p.y + ImGui::GetFrameHeight()), true);
        dl->AddText(ImVec2(p.x, p.y + ImGui::GetStyle().FramePadding.y), ImGui::GetColorU32(sem.textDim),
                    writes.c_str());
        dl->PopClipRect();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip) &&
            ImGui::CalcTextSize(writes.c_str()).x > room)
            ImGui::SetTooltip("%s", writes.c_str());
    }
    ImGui::SameLine(right - cancelW - importW - sp);
    if (ImGui::Button("Cancel", ImVec2(cancelW, 0))) {
        modelImportPhase_ = ImportPhase::Idle;
        modelImporting_ = false;
        modelImportFor_ = ModelImportFor::Nothing;
        modelImportSrc_.reset();
        modelImportPlan_.clear();
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    ImGui::SameLine();
    const bool can = (opt.writeStatic || opt.writeAnimated) && !modelImportPlan_.empty();
    const std::string importText = "Import " + (stem.empty() ? std::string("model") : stem);
    if (ui::PrimaryButtonText("Import", importText.c_str(), importW, can)) {
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
    if (!can && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Pick Animated model or Static model first.");
    ImGui::EndPopup();
}

// --- model picker -------------------------------------------------------------

void App::openModelPicker(bool animated, const std::string& objectId) {
    modelPickOpen_ = true;
    modelPickAnimated_ = animated;
    modelPickObjectId_ = objectId;
    modelPickFilter_[0] = 0;
    modelPickItems_.clear();
    // Every model anywhere under res/ - the Asset Browser's own scan, so a
    // project from before the importer, with its models in res/aster/ or a
    // hand-made folder, offers them all; nothing on disk has to move.
    scanAssetTree();
    std::set<std::string> statics;
    for (const AssetItem& item : assetItems_)
        if (item.kind == AssetKind::Model) statics.insert(item.rel);
    for (const AssetItem& item : assetItems_) {
        if (!pickableModel(item.rel, item.generated)) continue;
        if (animated) {
            if (item.kind == AssetKind::AnimModel) modelPickItems_.push_back(item.rel);
        } else if (item.kind == AssetKind::Model) {
            modelPickItems_.push_back(item.rel);
        } else if (item.kind == AssetKind::AnimModel &&
                   !statics.count(modelimport::staticTwinPath(item.rel))) {
            // a .glb/.fbx without a static twin yet: picking it makes one
            modelPickItems_.push_back(item.rel);
        }
    }
    // assetItems_ is sorted by path, so modelPickItems_ is too
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

    // The catalogue: every folder holding a model (deep counts), indented by
    // depth. "All models" shows everything; the choice is remembered.
    std::map<std::string, int> folders;
    for (const std::string& rel : modelPickItems_)
        for (std::string f = fs::path(rel).parent_path().generic_string();
             !f.empty() && f != "res"; f = fs::path(f).parent_path().generic_string())
            ++folders[f];
    if (!modelPickFolder_.empty() && !folders.count(modelPickFolder_)) modelPickFolder_.clear();
    ImGui::BeginChild("##modelfolders", ImVec2(scaled(200), -footer), true);
    if (ImGui::Selectable(("All models (" + std::to_string(modelPickItems_.size()) + ")").c_str(),
                          modelPickFolder_.empty()))
        modelPickFolder_.clear();
    for (const auto& [f, count] : folders) {
        const int depth = (int)std::count(f.begin(), f.end(), '/') - 1;
        const std::string label = std::string((size_t)std::max(0, depth) * 2, ' ') +
                                  fs::path(f).filename().string() + " (" +
                                  std::to_string(count) + ")##" + f;
        if (ImGui::Selectable(label.c_str(), f == modelPickFolder_)) modelPickFolder_ = f;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", f.c_str());
    }
    ImGui::EndChild();
    ImGui::SameLine();

    ImGui::BeginChild("##modelgrid", ImVec2(0, -footer), true);
    const std::string filter = lowerOf(modelPickFilter_);
    const float tile = scaled(116);
    const int cols = std::max(1, (int)(ImGui::GetContentRegionAvail().x / (tile + 8)));
    int col = 0, shown = 0;
    std::string chosen;
    assetThumbBudget_ = 4;
    for (const std::string& rel : modelPickItems_) {
        const std::string name = fs::path(rel).filename().string();
        if (!modelPickFolder_.empty() && !startsWith(rel, modelPickFolder_ + "/")) continue;
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
