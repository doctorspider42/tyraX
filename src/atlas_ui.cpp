// -------------------------------------------------------------------------
// Tools > Texture Atlas (docs/texture-atlasing.md): what the build packed,
// what it refused and why, what it costs in GS VRAM - and the two decisions
// the author owns, per texture: keep it out of every page, or put it in a
// named group instead of letting the folder layout decide.
//
// It exists because the feature used to be one checkbox and one line in the
// boot log. Nothing said which textures shared a page - so nothing warned
// that a page had merged two rooms' props into one allocation that must then
// be resident whenever either room is - and nothing said when a texture was
// silently disqualified. On the shipped night-walk example the answer to
// "what did it pack?" was "nothing at all", and it took reading the packer to
// find out.
//
// Its own translation unit for the reason every other *_ui.cpp is one
// (app.cpp is the build's critical path). These are App:: members.
// -------------------------------------------------------------------------
#include "app.hpp"
#include "app_internal.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <imgui.h>

#include "objparser.hpp"
#include "texatlas.hpp"

namespace fs = std::filesystem;

namespace {

// Every texture an object's material reaches, res-relative. Used to answer
// "which scenes does this page serve?" - the question a page cannot answer
// for itself, and the one that decides whether a page is a good idea: a page
// is ONE allocation, so members from two different scenes (or two streaming
// layers) hold each other resident.
void collectObjectTextures(const Project& p, const SceneObject& o,
                           std::set<std::string>& out) {
    const fs::path root(p.dir);
    auto add = [&](const std::string& definingRel, const std::string& tok) {
        if (tok.empty()) return;
        std::string t = tok;
        for (char& c : t)
            if (c == '\\') c = '/';
        const std::string rel = (fs::path(definingRel).parent_path() / t)
                                    .lexically_normal()
                                    .generic_string();
        if (rel.rfind("res/", 0) == 0) out.insert(rel);
    };
    if (!o.materialPath.empty()) {
        std::vector<objparser::MtlMaterial> mats;
        if (objparser::loadMtl((root / o.materialPath).string(), mats))
            for (const objparser::MtlMaterial& m : mats)
                add(o.materialPath, m.texture);
    }
    if (!o.modelPath.empty() && fs::path(o.modelPath).extension() == ".obj") {
        objparser::Model m;
        if (objparser::load((root / o.modelPath).string(), m,
                            o.materialPath.empty()
                                ? ""
                                : (root / o.materialPath).string()))
            for (const objparser::Submesh& s : m.submeshes)
                add(o.materialPath.empty() ? o.modelPath : o.materialPath,
                    s.texture);
    }
}

// The (?) marker every other tool window uses; each *_ui.cpp keeps its own
// copy because it is three lines and app.hpp is not the place for it.
void helpMarker(const char* tip) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
}

}  // namespace

void App::drawTextureAtlasWindow() {
    if (!showTextureAtlas_) return;
    ImGui::SetNextWindowSize(ImVec2(scaled(820), scaled(620)),
                             ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Texture Atlas", &showTextureAtlas_)) {
        ImGui::End();
        return;
    }

    if (!project_.settings.textureAtlas) {
        ImGui::TextWrapped(
            "Texture atlasing is off for this project. It packs small model "
            "textures into shared 256x256 pages, so the GS keeps one "
            "allocation per page instead of one per texture and draw batches "
            "switch textures less.");
        ImGui::Spacing();
        // Straight to the tab the switch is on - this button exists to answer
        // "where do I turn it on", and Preferences opens on whichever tab it
        // was last left on otherwise.
        if (ImGui::Button("Open Project Preferences")) {
            showProjectPrefs_ = true;
            prefsFocusTab_ = "Rendering";
            // ...and in FRONT of this window, which is where the click came
            // from and would otherwise cover what it just opened.
            pendingFocusWindow_ = "Project Preferences";
        }
        ImGui::End();
        return;
    }

    // The plan is a pure function of the project + the files on disk, and it
    // reads every candidate image, so it is computed on demand rather than
    // per frame. Anything that can change it - a rebuild, a control below -
    // sets atlasPlanDirty_.
    if (atlasPlanDirty_) {
        atlasPlan_ = texatlas::plan(project_);
        atlasVram_ = texatlas::vram(atlasPlan_, project_);
        atlasPlanDirty_ = false;
    }
    const texatlas::Plan& plan = atlasPlan_;

    ImGui::TextUnformatted(plan.empty()
                               ? "Nothing qualified for a page."
                               : texatlas::info(plan).c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Recompute")) atlasPlanDirty_ = true;

    if (!plan.empty()) {
        const int saved = atlasVram_.savedKb;
        ImGui::TextColored(
            saved >= 0 ? ImVec4(0.5f, 0.9f, 0.5f, 1) : ImVec4(0.95f, 0.7f, 0.3f, 1),
            "GS VRAM: %d KB unpacked -> %d KB as pages (%s %d KB)",
            atlasVram_.membersKb, atlasVram_.pagesKb,
            saved >= 0 ? "saves" : "COSTS", saved >= 0 ? saved : -saved);
        if (saved < 0)
            helpMarker(
                "A page is a full 256x256 allocation whatever it holds, so it "
                "only pays once enough textures share it: about eight 64x64 "
                "members at 4 bits per pixel, about sixteen at 8. Below that "
                "atlasing still buys fewer allocations and fewer texture "
                "switches per frame - it just does not buy bytes yet.");
    }
    ImGui::Separator();

    // Which scene each texture is used by - computed with the plan, since it
    // walks the same object tables.
    std::map<std::string, std::set<std::string>> scenesOfTexture;
    for (const SceneData& sc : project_.scenes)
        for (const SceneObject& o : sc.objects) {
            std::set<std::string> texs;
            collectObjectTextures(project_, o, texs);
            for (const std::string& t : texs) scenesOfTexture[t].insert(sc.name);
        }

    // One row of per-texture controls, shared by the page members and the
    // excluded list. Writing here marks the project dirty and re-plans.
    auto controls = [&](const std::string& resRel, bool offerKeepOut) {
        Project::AtlasControl ctl;
        if (auto it = project_.atlasControl.find(resRel);
            it != project_.atlasControl.end())
            ctl = it->second;
        bool changed = false;
        if (offerKeepOut) {
            bool keep = ctl.keepOut;
            ImGui::PushID((resRel + "#k").c_str());
            if (ImGui::Checkbox("keep out", &keep)) {
                ctl.keepOut = keep;
                changed = true;
            }
            ImGui::PopID();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Never pack this texture into a page: it keeps its own "
                    "palette and its own allocation.");
            ImGui::SameLine();
        }
        char buf[64] = "";
        std::snprintf(buf, sizeof(buf), "%s", ctl.group.c_str());
        ImGui::PushID((resRel + "#g").c_str());
        ImGui::SetNextItemWidth(scaled(130));
        if (ImGui::InputTextWithHint("##group", "group (folder)", buf,
                                     sizeof(buf))) {
            ctl.group = buf;
            changed = true;
        }
        ImGui::PopID();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Pack this texture with everything carrying the SAME group "
                "name, instead of with its .mtl's folder. A page is one "
                "allocation and one shared palette, so group by what is on "
                "screen together - a page shared by two rooms keeps both "
                "resident.");
        ImGui::SameLine();
        // The depth REQUEST. A page is quantized as one image, so this asks
        // for the whole group and the highest request wins; the page header
        // shows what the group settled on.
        const char* bitNames[] = {"page: project", "page: 4-bit", "page: 8-bit",
                                  "page: full"};
        int bitIdx = ctl.pageBits == 4    ? 1
                     : ctl.pageBits == 8  ? 2
                     : ctl.pageBits == 32 ? 3
                                          : 0;
        ImGui::PushID((resRel + "#b").c_str());
        ImGui::SetNextItemWidth(scaled(120));
        if (ImGui::Combo("##bits", &bitIdx, bitNames, 4)) {
            ctl.pageBits = bitIdx == 1 ? 4 : bitIdx == 2 ? 8 : bitIdx == 3 ? 32 : 0;
            changed = true;
        }
        ImGui::PopID();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "How deep the PAGE this texture lands on is quantized. It "
                "follows the project's texture quality by default; a 4-bit "
                "page is half the VRAM of an 8-bit one and shares one "
                "16-colour palette between everything on it. The group takes "
                "the highest request any of its members makes.");
        if (changed) {
            if (ctl.keepOut || !ctl.group.empty() || ctl.pageBits != 0)
                project_.atlasControl[resRel] = ctl;
            else
                project_.atlasControl.erase(resRel);
            commitChange();
            atlasPlanDirty_ = true;
        }
    };

    if (ImGui::BeginTabBar("##atlastabs")) {
        if (ImGui::BeginTabItem("Pages")) {
            for (size_t pi = 0; pi < plan.pages.size(); ++pi) {
                const std::string& grp = plan.groupOf((int)pi);
                std::string title = fs::path(plan.pages[pi]).filename().string() +
                                    "  -  " + plan.pages[pi] + "   " +
                                    std::to_string(plan.bitsOf((int)pi)) +
                                    "-bit";
                if (!grp.empty() && grp[0] == '@')
                    title += "   [group " + grp.substr(1) + "]";
                ImGui::PushID((int)pi);
                if (ImGui::CollapsingHeader(title.c_str(),
                                            ImGuiTreeNodeFlags_DefaultOpen)) {
                    // The baked page, when a build has already composited it.
                    const std::string bakedRel =
                        ".res-baked/" + plan.pages[pi].substr(4);
                    if (const HudTexture* t = hudTexture(bakedRel)) {
                        const float s = scaled(180);
                        ImGui::Image((ImTextureID)(intptr_t)t->tex,
                                     ImVec2(s, s));
                        ImGui::SameLine();
                    } else {
                        ImGui::TextDisabled("(build the project to see the page)");
                    }
                    ImGui::BeginGroup();
                    // Cross-scene warning: the "different parish" problem.
                    std::set<std::string> pageScenes;
                    for (const texatlas::Entry& e : plan.entries)
                        if (e.page == (int)pi)
                            for (const std::string& s : scenesOfTexture[e.resRel])
                                pageScenes.insert(s);
                    if (pageScenes.size() > 1) {
                        std::string names;
                        for (const std::string& s : pageScenes)
                            names += (names.empty() ? "" : ", ") + s;
                        ImGui::TextColored(ImVec4(0.95f, 0.7f, 0.3f, 1),
                                           "Used by %d scenes: %s",
                                           (int)pageScenes.size(),
                                           names.c_str());
                        helpMarker(
                            "This page is one VRAM allocation, so every scene "
                            "listed here keeps all of its members resident. "
                            "Give the members of each scene their own group "
                            "name if that matters.");
                    }
                    if (ImGui::BeginTable("##members", 3,
                                          ImGuiTableFlags_SizingFixedFit |
                                              ImGuiTableFlags_RowBg)) {
                        for (const texatlas::Entry& e : plan.entries) {
                            if (e.page != (int)pi) continue;
                            ImGui::TableNextRow();
                            ImGui::TableNextColumn();
                            ImGui::TextUnformatted(e.resRel.c_str());
                            ImGui::TableNextColumn();
                            ImGui::Text("%dx%d at %d,%d", e.w, e.h, e.x, e.y);
                            ImGui::TableNextColumn();
                            controls(e.resRel, true);
                        }
                        ImGui::EndTable();
                    }
                    ImGui::EndGroup();
                }
                ImGui::PopID();
            }
            if (plan.pages.empty())
                ImGui::TextDisabled(
                    "No pages. The Not atlased tab says why for every "
                    "texture the packer looked at.");
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Not atlased")) {
            ImGui::TextWrapped(
                "Every texture the packer considered and refused, with the "
                "reason. A texture that appears nowhere here and nowhere on a "
                "page is not referenced by anything the atlas looks at "
                "(models and primitives).");
            ImGui::Spacing();
            if (ImGui::BeginTable("##excl", 3,
                                  ImGuiTableFlags_SizingStretchProp |
                                      ImGuiTableFlags_RowBg |
                                      ImGuiTableFlags_BordersInnerV)) {
                for (const texatlas::Excluded& e : plan.excluded) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(e.resRel.c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextDisabled("%s", e.reason.c_str());
                    ImGui::TableNextColumn();
                    controls(e.resRel, true);
                }
                ImGui::EndTable();
            }
            if (plan.excluded.empty())
                ImGui::TextDisabled("Nothing was refused.");
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}
