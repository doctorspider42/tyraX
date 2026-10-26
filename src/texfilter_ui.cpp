// -------------------------------------------------------------------------
// Tools > Texture Filtering (docs/texture-filtering.md): every colour texture
// the project can ship, the filter each one is drawn with, and the overrides
// set in bulk. The Asset Browser edits one texture at a time; this window is
// for "make the whole dungeon pack pixel" and for answering "which textures
// did I override?".
//
// Its own translation unit for the reason every other *_ui.cpp is one
// (app.cpp is the build's critical path). These are App:: members.
// -------------------------------------------------------------------------
#include "app.hpp"
#include "app_internal.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace {

const char* filterLabel(const std::string& f) {
    return f == "pixel" ? "Pixel" : f == "closeup" ? "Pixel close-up" : "Smooth";
}

}  // namespace

// The textures a material can bind: PNGs under res/models, res/materials and
// res/textures - the folders texbake rewrites .mtl files in. Lighting data
// (res/baked, the GI cache) and HUD art live elsewhere and stay smooth.
void App::scanTextureFilterList() {
    texFilterList_.clear();
    namespace fs = std::filesystem;
    std::error_code ec;
    for (const char* top : {"res/models", "res/materials", "res/textures"}) {
        const fs::path root = fs::path(project_.dir) / top;
        if (!fs::exists(root, ec)) continue;
        for (const auto& e : fs::recursive_directory_iterator(root, ec)) {
            if (!e.is_regular_file()) continue;
            std::string ext = e.path().extension().string();
            for (char& c : ext) c = (char)std::tolower((unsigned char)c);
            if (ext != ".png") continue;
            texFilterList_.push_back(
                fs::relative(e.path(), fs::path(project_.dir), ec).generic_string());
        }
    }
    std::sort(texFilterList_.begin(), texFilterList_.end());
    // An override for a texture that is gone stays listed, so it can be
    // cleared instead of lingering unseen in the project.
    for (const auto& [tex, f] : project_.textureFilters)
        if (!std::binary_search(texFilterList_.begin(), texFilterList_.end(), tex))
            texFilterList_.push_back(tex);
    texFilterScanDirty_ = false;
}

void App::drawTextureFilterWindow() {
    if (!showTextureFilters_) return;
    ImGui::SetNextWindowSize(ImVec2(scaled(820), scaled(560)), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Texture Filtering", &showTextureFilters_)) {
        ImGui::End();
        return;
    }
    if (!hasProject_) {
        ImGui::TextDisabled("Open a project to see its textures.");
        ImGui::End();
        return;
    }
    if (texFilterScanDirty_) scanTextureFilterList();

    // --- header: the default and where it lives ----------------------------
    ImGui::Text("Project default: %s", filterLabel(project_.settings.textureFilter));
    ImGui::SameLine();
    if (ImGui::SmallButton("Change...")) {
        showProjectPrefs_ = true;
        prefsFocusTab_ = "Graphics/Look";
        prefsScrollToItem_ = "Texture filtering";
        pendingFocusWindow_ = "Project Preferences";
    }
    ImGui::SameLine();
    ImGui::TextDisabled("- %d override%s", (int)project_.textureFilters.size(),
                        project_.textureFilters.size() == 1 ? "" : "s");
    ImGui::SameLine(ImGui::GetContentRegionMax().x - scaled(70));
    if (ImGui::SmallButton("Rescan")) scanTextureFilterList();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Re-read res/models, res/materials and res/textures.");

    // --- filters -------------------------------------------------------------
    ImGui::SetNextItemWidth(scaled(220));
    ImGui::InputTextWithHint("##texfilterSearch", "Search textures", texFilterSearch_,
                             sizeof texFilterSearch_);
    ImGui::SameLine();
    const char* shows[] = {"All", "Overrides", "Not smooth"};
    ImGui::SetNextItemWidth(scaled(260));
    ui::SegmentedRow("##texfilterShow", shows, 3, &texFilterShow_);

    std::string needle = texFilterSearch_;
    for (char& c : needle) c = (char)std::tolower((unsigned char)c);
    std::vector<const std::string*> rows;
    for (const std::string& tex : texFilterList_) {
        const bool overridden = project_.textureFilters.count(tex) != 0;
        if (texFilterShow_ == 1 && !overridden) continue;
        if (texFilterShow_ == 2 && project_.textureFilterOf(tex) == "smooth") continue;
        if (!needle.empty()) {
            std::string low = tex;
            for (char& c : low) c = (char)std::tolower((unsigned char)c);
            if (low.find(needle) == std::string::npos) continue;
        }
        rows.push_back(&tex);
    }

    // --- bulk bar ------------------------------------------------------------
    // Selection survives filtering; the bar acts on everything selected.
    for (auto it = texFilterSelected_.begin(); it != texFilterSelected_.end();)
        it = std::binary_search(texFilterList_.begin(), texFilterList_.end(), *it) ||
                     project_.textureFilters.count(*it)
                 ? std::next(it)
                 : texFilterSelected_.erase(it);
    ImGui::Separator();
    if (texFilterSelected_.empty()) {
        ImGui::TextDisabled("Select textures to set their filter together.");
    } else {
        ImGui::Text("%d selected - set:", (int)texFilterSelected_.size());
        static const char* const kModes[] = {"", "smooth", "pixel", "closeup"};
        static const char* const kNames[] = {"Default", "Smooth", "Pixel", "Pixel close-up"};
        for (int m = 0; m < 4; ++m) {
            ImGui::SameLine();
            if (ImGui::Button((std::string(kNames[m]) + "##texfilterBulk").c_str())) {
                for (const std::string& tex : texFilterSelected_) {
                    if (m == 0)
                        project_.textureFilters.erase(tex);
                    else
                        project_.textureFilters[tex] = kModes[m];
                }
                commitChange();
            }
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear selection")) texFilterSelected_.clear();
    }

    // --- the list ------------------------------------------------------------
    if (rows.empty()) {
        ImGui::TextDisabled(texFilterList_.empty()
                                ? "No PNGs under res/models, res/materials or res/textures."
                                : "Nothing matches.");
        ImGui::End();
        return;
    }
    const ImGuiTableFlags tf = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                               ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_Resizable;
    if (ImGui::BeginTable("##texfilterTable", 4, tf)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("##sel", ImGuiTableColumnFlags_WidthFixed, scaled(28));
        ImGui::TableSetupColumn("Texture", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Folder", ImGuiTableColumnFlags_WidthStretch, 1.6f);
        ImGui::TableSetupColumn("Filtering", ImGuiTableColumnFlags_WidthFixed, scaled(190));
        ImGui::TableHeadersRow();
        const std::string def =
            std::string("Default (") + filterLabel(project_.settings.textureFilter) + ")";
        ImGuiListClipper clip;
        clip.Begin((int)rows.size());
        while (clip.Step()) {
            for (int r = clip.DisplayStart; r < clip.DisplayEnd; ++r) {
                const std::string& tex = *rows[(size_t)r];
                ImGui::PushID(tex.c_str());
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                bool sel = texFilterSelected_.count(tex) != 0;
                if (ImGui::Checkbox("##sel", &sel)) {
                    if (sel) texFilterSelected_.insert(tex);
                    else texFilterSelected_.erase(tex);
                }
                const std::filesystem::path p(tex);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(p.filename().string().c_str());
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", p.parent_path().generic_string().c_str());
                ImGui::TableNextColumn();
                auto it = project_.textureFilters.find(tex);
                int cur = it == project_.textureFilters.end() ? 0
                          : it->second == "smooth"            ? 1
                          : it->second == "pixel"             ? 2
                                                              : 3;
                const char* labels[] = {def.c_str(), "Smooth", "Pixel", "Pixel close-up"};
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::Combo("##filter", &cur, labels, 4)) {
                    if (cur == 0)
                        project_.textureFilters.erase(tex);
                    else
                        project_.textureFilters[tex] =
                            cur == 1 ? "smooth" : cur == 2 ? "pixel" : "closeup";
                    commitChange();
                }
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
    ImGui::End();
}
