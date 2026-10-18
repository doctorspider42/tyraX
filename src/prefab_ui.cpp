// -------------------------------------------------------------------------
// Tools > Prefabs (docs/prefabs.md): reusable groups of scene objects - their
// flow graphs included - captured once and stamped into the world by hand, by
// a procedural graph's Pick Prefab node, or by the Spawn Prefab flow node while
// the game runs.
//
// A docked side panel: a header (count, From selection, docs), a thumbnail
// grid of the library, and the selected prefab's detail - name, notes, fact
// chips, ONE primary action (Place in scene), members, and the bake as a
// footer link. Bake to model and Delete live in the "..." menu.
//
// Its own translation unit for the reason every other *_ui.cpp is one (app.cpp
// used to be a 26k-line TU and therefore the whole build's critical path).
// These are still App:: members declared in app.hpp.
// -------------------------------------------------------------------------
#include "app.hpp"
#include "app_internal.hpp"

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "platform.hpp"
#include "prefab.hpp"
#include "theme.hpp"
#include "toolkit.hpp"
#include "uiscript.hpp"
#include "update.hpp"

#include <imgui.h>
#include <imgui_internal.h>

namespace {

// What one instance of a prefab costs on the console, in the two currencies
// that actually run out: draw submits and spawn-pool slots. Triangles are a
// third, but they are the one the rest of the editor already reports.
struct PrefabCost {
    int merged = 0;   // members folded into the instance's shared bag
    int spawned = 0;  // members that need an object of their own
    int markers = 0;  // spawned but never drawn
    int materials = 0;  // distinct materials among the merged members = bags
};

PrefabCost prefabCost(const Prefab& pf) {
    PrefabCost c;
    std::vector<std::string> mats;
    for (const SceneObject& o : pf.objects) {
        if (prefab::memberMerges(o)) {
            ++c.merged;
            const std::string key =
                o.type == PrimitiveType::Model ? o.modelPath : o.materialPath;
            if (std::find(mats.begin(), mats.end(), key) == mats.end())
                mats.push_back(key);
        } else if (prefab::memberIsMarker(o)) {
            ++c.markers;
            ++c.spawned;
        } else {
            ++c.spawned;
        }
    }
    c.materials = (int)mats.size();
    return c;
}

// The thumbnail's cache key: the prefab's id plus what the picture shows, so
// Replace (or a colour edit through a re-capture) renders a new tile.
std::string thumbKey(const Prefab& pf) {
    std::string s = pf.id;
    char buf[160];
    for (const SceneObject& o : pf.objects) {
        std::snprintf(buf, sizeof(buf), "|%d %.3f %.3f %.3f %.2f %.2f %.2f %.3f %.3f %.3f %.3f %.3f %.3f %d",
                      (int)o.type, o.position[0], o.position[1], o.position[2], o.rotation[0],
                      o.rotation[1], o.rotation[2], o.scale[0], o.scale[1], o.scale[2], o.color[0],
                      o.color[1], o.color[2], o.primDetail);
        s += buf;
        s += o.modelPath;
        s += o.materialPath;
    }
    return pf.id + "#" + std::to_string(std::hash<std::string>{}(s));
}

bool containsNoCase(const std::string& hay, const char* needle) {
    if (!needle || !*needle) return true;
    const size_t n = std::strlen(needle);
    if (n > hay.size()) return false;
    for (size_t i = 0; i + n <= hay.size(); ++i) {
        size_t k = 0;
        while (k < n && std::tolower((unsigned char)hay[i + k]) ==
                            std::tolower((unsigned char)needle[k]))
            ++k;
        if (k == n) return true;
    }
    return false;
}

void openDocPage(const char* page) {
    platform::openUrl(std::string("https://github.com/") + update::kRepo + "/blob/main/docs/" + page +
                      ".md");
}

}  // namespace

void App::drawPrefabsWindow() {
    if (!showPrefabs_) return;
    ImGui::SetNextWindowSize(ImVec2(scaled(420), scaled(820)), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Prefabs", &showPrefabs_)) {
        ImGui::End();
        return;
    }
    if (!hasProject_) {
        ImGui::TextDisabled("Open a project first.");
        ImGui::End();
        return;
    }
    const theme::Semantics& sem = theme::semantics();

    // Every verb below is a model EDIT with a discrete trigger, not a save
    // command - prefabs live in the .tyra and nothing here writes a file
    // except the bake delete, which is the same shape as the Asset Browser's
    // delete and also commits. So they mark (commitChange + a status line)
    // instead of writing, like the rest of the editor; see app.hpp. The
    // section comparison is the belt-and-braces half every project-wide panel
    // carries: a verb added here that forgets to commit still cannot lose the
    // edit.
    const std::string beforeSection =
        project::sectionJson(project_, project::Section::Prefabs);
    auto commitIfEdited = [&] {
        if (project::sectionJson(project_, project::Section::Prefabs) != beforeSection)
            commitChange();
    };

    // --- header: count, From selection, docs --------------------------------
    const int selCount = (int)selection_.size();
    auto capture = [&] {
        std::vector<int> sel = selection_;
        std::sort(sel.begin(), sel.end());
        // A single object names the prefab after itself; a group gets a
        // numbered "group" rather than "prefab" (which says nothing).
        const std::string base =
            (sel.size() == 1 && sel[0] < (int)project_.objects().size())
                ? project_.objects()[sel[0]].name
                : "group";
        Prefab pf = prefab::capture(project_.active(), sel,
                                    prefab::uniqueName(project_, base));
        if (pf.objects.empty()) {
            statusMessage_ = "Nothing to capture";
        } else {
            project_.prefabs.push_back(std::move(pf));
            prefabSelected_ = (int)project_.prefabs.size() - 1;
            prefabNotesEditing_ = false;
            prefabMembersAll_ = false;
            commitChange();
            statusMessage_ = "Prefab created";
        }
    };
    const char* captureWhy =
        "Select the objects in the viewport or the Outliner first. The prefab's\n"
        "origin is the selection's footprint centre at its lowest point, so\n"
        "clicking the ground places it the way you expect.";
    {
        const float rowH = ImGui::GetFrameHeight();
        const float y0 = ImGui::GetCursorPosY();
        {
            ui::FontScope bold(ui::boldFont());
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Prefabs");
        }
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%d", (int)project_.prefabs.size());
        const std::string capLabel =
            selCount > 1 ? "+ From selection (" + std::to_string(selCount) + ")###From selection"
                         : std::string("+ From selection###From selection");
        const float capW = ImGui::CalcTextSize(capLabel.c_str(), nullptr, true).x +
                           ImGui::GetStyle().FramePadding.x * 2.6f;
        const float right = ImGui::GetContentRegionMax().x;
        ImGui::SameLine(right - capW - rowH - ImGui::GetStyle().ItemSpacing.x);
        ImGui::SetCursorPosY(y0);
        if (ui::SecondaryButton(capLabel.c_str(), capW, selCount > 0)) capture();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("%s", selCount == 0 ? captureWhy
                                                  : "Captures the selected objects as a new prefab.\n"
                                                    "Members keep their materials, physics, scripts\n"
                                                    "and flow graphs.");
        ImGui::SameLine();
        ImGui::SetCursorPosY(y0);
        if (ui::HelpButton("Prefab docs", rowH, "Open docs/prefabs.md")) openDocPage("prefabs");
    }

    if (project_.prefabs.empty()) {
        if (ui::EmptyState(ui::Glyph::Box, "No prefabs yet",
                           selCount > 0
                               ? "Capture the selected objects as a reusable prefab."
                               : "Build something out of ordinary objects - a hut, a lamp post "
                                 "with its light and script - select it all, then From selection.",
                           selCount > 0 ? "Create prefab" : nullptr))
            capture();
        ImGui::End();
        commitIfEdited();
        return;
    }
    if (prefabSelected_ >= (int)project_.prefabs.size()) prefabSelected_ = 0;
    if (prefabSelected_ < 0) prefabSelected_ = 0;

    // --- search -------------------------------------------------------------
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##prefabsearch", "Search names and notes", prefabSearch_,
                             sizeof(prefabSearch_));

    // --- tile grid ----------------------------------------------------------
    std::vector<int> shown;
    for (int i = 0; i < (int)project_.prefabs.size(); ++i) {
        const Prefab& p = project_.prefabs[i];
        if (containsNoCase(p.name, prefabSearch_) || containsNoCase(p.notes, prefabSearch_))
            shown.push_back(i);
    }
    {
        const ImGuiStyle& st = ImGui::GetStyle();
        const float availW = ImGui::GetContentRegionAvail().x;
        const float gap = st.ItemSpacing.x;
        const int cols = availW > scaled(560) ? 4 : 3;
        const float tile = ImMax(scaled(56), (availW - st.ScrollbarSize - gap * (cols - 1) - 8.0f) / cols);
        const float tileH = tile + ImGui::GetFontSize() + 6.0f + st.ItemSpacing.y;
        const int rows = ImMax(1, ((int)shown.size() + cols - 1) / cols);
        const float maxH = ImMax(tileH + 8.0f, ImGui::GetContentRegionAvail().y * 0.36f);
        const float gridH = ImMin(rows * tileH + 10.0f, maxH);
        ImGui::BeginChild("prefab-grid", ImVec2(0, gridH), ImGuiChildFlags_None);
        ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + 3.0f, ImGui::GetCursorPosY() + 4.0f));
        if (shown.empty()) ImGui::TextDisabled("No prefab matches \"%s\".", prefabSearch_);
        int budget = 2;  // new renders per frame: a library of fifty opens smoothly
        for (size_t k = 0; k < shown.size(); ++k) {
            const int i = shown[k];
            const Prefab& p = project_.prefabs[i];
            const std::string key = thumbKey(p);
            uint32_t tex = viewport_.prefabThumb(key, p.objects, false);
            if (!tex && budget > 0) {
                tex = viewport_.prefabThumb(key, p.objects, true);
                --budget;
            }
            if (k > 0 && (int)(k % cols) != 0) ImGui::SameLine(0.0f, gap);
            char label[160], note[16];
            std::snprintf(label, sizeof(label), "%s##pf%d", p.name.c_str(), i);
            std::snprintf(note, sizeof(note), "%d", (int)p.objects.size());
            if (ui::ThumbTileEx(label, (ImTextureID)(intptr_t)tex, true, ui::Glyph::Box,
                                prefabSelected_ == i, tile, p.name.c_str(), note) &&
                prefabSelected_ != i) {
                prefabSelected_ = i;
                prefabNotesEditing_ = false;  // the buffer belonged to the old one
                prefabMembersAll_ = false;
            }
            uiscript::markLastItemChecked(prefabSelected_ == i);
            if (!p.notes.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos(scaled(320));
                ImGui::TextUnformatted(p.notes.c_str());
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
        }
        ImGui::EndChild();
    }
    ImGui::Separator();

    Prefab& pf = project_.prefabs[prefabSelected_];
    const PrefabCost cost = prefabCost(pf);

    // Baked or not - read from DISK, so the answer survives a restart (the
    // fresh report only lives for the session that clicked the button).
    // Cached: the check is a file read, and the key covers a rename (the stem
    // comes from the name, so a renamed prefab's bake is a different file).
    {
        const std::string key = pf.id + "|" + pf.name;
        if (key != prefabBakeDiskKey_) {
            prefabBakeDiskKey_ = key;
            prefabBakeDiskPath_ = prefab::bakeOnDisk(project_, pf);
        }
    }
    const bool fresh = !prefabBakeReport_.modelPath.empty() && prefabBakeFor_ == pf.id;
    const int footLines = 1 + (fresh && !prefabBakeDiskPath_.empty()
                                   ? (int)(prefabBakeReport_.skipped.size() + prefabBakeReport_.warnings.size())
                                   : 0);
    const float footerH = ImGui::GetFrameHeightWithSpacing() * (float)ImMin(footLines, 4) +
                          ImGui::GetStyle().ItemSpacing.y * 2.0f;

    bool openDeletePrefab = false;
    bool openDeleteBake = false;
    auto doBake = [&] {
        const prefab::BakeReport r = prefab::bakeToModel(project_, pf);
        if (!r.error.empty()) {
            statusMessage_ = "Bake failed: " + r.error;
        } else {
            statusMessage_ = "Baked " + std::to_string(r.members) + " member(s) into " +
                             r.modelPath + " - " + std::to_string(r.triangles) +
                             " triangles, " + std::to_string(r.materials) + " material(s)";
            if (!r.skipped.empty())
                statusMessage_ += " | skipped " + std::to_string(r.skipped.size()) + ": " +
                                  r.skipped.front() + (r.skipped.size() > 1 ? ", ..." : "");
            for (const std::string& w : r.warnings) statusMessage_ += " | " + w;
            prefabBakeReport_ = r;
            prefabBakeFor_ = pf.id;
            prefabBakeDiskKey_.clear();  // the on-disk readout just changed
        }
    };
    const char* bakeTip =
        "Flattens the mergeable members into one res/models/<name>.obj (plus a\n"
        "generated .mtl carrying their colours), so the prefab can be scattered\n"
        "with Pick Asset instead of Pick Prefab: a prefab instance takes a record\n"
        "from the runtime pool, a model costs none. One way - the result is dumb\n"
        "geometry; the prefab stays as the source.";

    // --- detail -------------------------------------------------------------
    ImGui::BeginChild("prefab-detail", ImVec2(0, -footerH), ImGuiChildFlags_None);
    {
        // Name: a large borderless field (Enter commits, renames retarget every
        // reference so a renamed prefab never silently stops spawning), then
        // the "..." menu at the row's end.
        const float moreS = ImGui::GetFrameHeight();
        char nameBuf[96];
        std::snprintf(nameBuf, sizeof(nameBuf), "%s", pf.name.c_str());
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, ImGui::GetStyle().FramePadding.y));
        ImGui::PushFont(ui::boldFont(), ImGui::GetFontSize() * 1.25f);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - moreS - ImGui::GetStyle().ItemSpacing.x);
        const bool renamed = ImGui::InputText("##prefabname", nameBuf, sizeof(nameBuf),
                                              ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::PopFont();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip) && !ImGui::IsItemActive())
            ImGui::SetTooltip("Click to rename; Enter applies. Flow nodes and Pick Prefab\nrows that name it follow.");
        if (renamed && pf.name != nameBuf) {
            if (!prefab::rename(project_, pf.name, nameBuf))
                statusMessage_ = "That prefab name is taken";
            else {
                commitChange();
                statusMessage_ = "Prefab renamed";
            }
        }
        ImGui::SameLine();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (ImGui::GetItemRectSize().y - moreS) * 0.5f);
        if (ui::MoreButton("Prefab actions", moreS)) ImGui::OpenPopup("##prefabmore");
        if (ImGui::BeginPopup("##prefabmore")) {
            if (ImGui::MenuItem("Bake to model")) doBake();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", bakeTip);
            if (!prefabBakeDiskPath_.empty() && ImGui::MenuItem("Delete bake...")) openDeleteBake = true;
            ImGui::Separator();
            ImGui::PushStyleColor(ImGuiCol_Text, sem.danger);
            if (ImGui::MenuItem("Delete prefab...")) openDeletePrefab = true;
            ImGui::PopStyleColor();
            ImGui::EndPopup();
        }
    }

    // Notes: read as WRAPPED prose, edited in a multiline box on click.
    //
    // The obvious widget is the wrong one twice over. A single-line InputText
    // shows the first ~50 characters and hides the rest behind a caret nobody
    // thinks to move - the field that documents a prefab was the one field
    // you could not read. And ImGui's multiline InputText does not word-wrap
    // either: a long note stays one line and scrolls sideways. So the resting
    // state is a plain wrapped paragraph and the editor only appears while it
    // is being typed in.
    if (prefabNotesEditing_) {
        if (prefabNotesFocus_) {
            ImGui::SetKeyboardFocusHere();
            prefabNotesFocus_ = false;
        }
        // Multiline has no Enter to commit on, so the buffer is a MEMBER: the
        // refresh-from-the-model-every-frame pattern the single-line fields use
        // works only because EnterReturnsTrue copies back on the Enter frame.
        ImGui::InputTextMultiline("##pfnotes", prefabNotesBuf_, sizeof(prefabNotesBuf_),
                                  ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 4.0f),
                                  ImGuiInputTextFlags_AllowTabInput);
        if (ImGui::IsItemDeactivated()) {
            prefabNotesEditing_ = false;
            if (pf.notes != prefabNotesBuf_) {
                pf.notes = prefabNotesBuf_;
                commitChange();
                statusMessage_ = "Prefab notes";
            }
        }
        ImGui::TextDisabled("Enter starts a new line; click away to save.");
    } else {
        ImGui::PushTextWrapPos(0.0f);
        if (pf.notes.empty())
            ImGui::TextDisabled("Add a note - what this prefab is for, how to place it.");
        else
            ImGui::TextUnformatted(pf.notes.c_str());
        ImGui::PopTextWrapPos();
        if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
        if (ImGui::IsItemClicked()) {
            std::snprintf(prefabNotesBuf_, sizeof(prefabNotesBuf_), "%s", pf.notes.c_str());
            prefabNotesEditing_ = true;
            prefabNotesFocus_ = true;
        }
    }
    ImGui::Dummy(ImVec2(0, scaled(2)));

    // --- facts as chips ---------------------------------------------------------
    {
        float mn[3], mx[3];
        prefab::bounds(pf, mn, mx);
        std::vector<std::string> where;
        for (const SceneData& s : project_.scenes)
            for (const std::string& n : prefab::referencedBy(project_, s))
                if (n == pf.name) where.push_back(s.name.empty() ? "(scene)" : s.name);
        char buf[160];
        std::snprintf(buf, sizeof(buf), "%d member%s", (int)pf.objects.size(),
                      pf.objects.size() == 1 ? "" : "s");
        ui::BadgeFlow(buf, 4, true);
        std::snprintf(buf, sizeof(buf), "%.3g x %.3g x %.3g u", mx[0] - mn[0], mx[1] - mn[1], mx[2] - mn[2]);
        ui::BadgeFlow(buf, 4, false, "Size of the prefab's bounds, in world units.");
        const char* costTip =
            "When the game spawns this prefab, static members (plain primitives and static\n"
            ".obj models) are merged into one geometry bag per material - a PS2 draw submit\n"
            "costs ~1 ms of EE time whatever it contains, so that merge is the whole reason a\n"
            "prefab is affordable. Members with a flow graph, scripts, physics, a streaming\n"
            "layer, a light or an emitter take a slot in the 32-clone spawn pool and a submit\n"
            "of their own.";
        if (cost.merged > 0) {
            std::snprintf(buf, sizeof(buf), "%d draw call%s (merged)", cost.materials,
                          cost.materials == 1 ? "" : "s");
            ui::BadgeFlow(buf, 0, false, costTip);
        }
        if (cost.spawned > 0) {
            std::snprintf(buf, sizeof(buf), "%d spawned object%s", cost.spawned,
                          cost.spawned == 1 ? "" : "s");
            ui::BadgeFlow(buf, cost.spawned > 8 ? 1 : 4, false,
                          cost.spawned > 8 ? "More than 8 spawned members per instance - the runtime\n"
                                             "clone pool holds 32 in total."
                                           : costTip);
        }
        if (where.empty()) {
            ui::BadgeFlow("placed copies only", 4, false,
                          "No Spawn Prefab node or Pick Prefab row names this prefab.");
        } else {
            std::string line;
            for (size_t i = 0; i < where.size(); ++i) line += (i ? ", " : "") + where[i];
            std::snprintf(buf, sizeof(buf), "spawned in: %s", line.c_str());
            ui::BadgeFlow(buf, 3, false, "Scenes whose graphs spawn this prefab at runtime\n(Spawn Prefab / Pick Prefab).");
        }
    }
    ImGui::Dummy(ImVec2(0, scaled(4)));

    // --- the one action, and Replace ---------------------------------------------
    {
        const float gap = ImGui::GetStyle().ItemSpacing.x;
        const float repW = ImGui::CalcTextSize("Replace").x + ImGui::GetStyle().FramePadding.x * 4.0f;
        const float placeW = ImGui::GetContentRegionAvail().x - repW - gap;
        if (ui::PrimaryButton("Place in scene", placeW)) {
            // Reuses the deferred-paste placement: the copies follow the cursor
            // and land on whatever is under it, exactly like Ctrl+V.
            pasteStaged_ = prefab::instantiate(pf, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, "");
            // Unique names within the scene, so two instances stay tellable apart.
            for (SceneObject& o : pasteStaged_) {
                std::string want = o.name;
                for (int n = 2;; ++n) {
                    bool taken = false;
                    for (const SceneObject& other : project_.objects()) taken |= (other.name == want);
                    if (!taken) break;
                    want = o.name + " " + std::to_string(n);
                }
                o.name = want;
            }
            if (pasteStaged_.empty()) {
                statusMessage_ = "That prefab is empty";
            } else {
                pastePending_ = true;
                pasteMoved_ = false;
                sculptMode_ = paintMode_ = false;
                clearSelection();
                statusMessage_ = "Click to place \"" + pf.name + "\" (Esc cancels)";
            }
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("The copies follow the cursor in the viewport like a paste:\nclick to drop them, Esc cancels.");
        ImGui::SameLine(0.0f, gap);
        if (ui::SecondaryButton("Replace", repW, selCount > 0)) {
            std::vector<int> sel = selection_;
            std::sort(sel.begin(), sel.end());
            Prefab np = prefab::capture(project_.active(), sel, pf.name);
            np.id = pf.id;
            np.notes = pf.notes;
            pf = std::move(np);
            prefabMembersAll_ = false;
            commitChange();
            statusMessage_ = "Prefab replaced";
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_ForTooltip)) {
            if (selCount == 0)
                ImGui::SetTooltip("Overwrites %s's %d member(s) with a new selection.\nSelect the objects to capture first.",
                                  pf.name.c_str(), (int)pf.objects.size());
            else
                ImGui::SetTooltip("Overwrites %s's %d member(s) with the %d selected object(s).\n"
                                  "Copies already placed do not change.",
                                  pf.name.c_str(), (int)pf.objects.size(), selCount);
        }
    }
    ImGui::Dummy(ImVec2(0, scaled(2)));
    {
        const std::string info = "Placed copies are independent: editing " + pf.name +
                                 " does not change copies already in the scene.";
        ui::Callout(3, info.c_str());
    }
    ImGui::Dummy(ImVec2(0, scaled(6)));

    // --- members: the first few, the rest on request --------------------------
    {
        char count[16];
        std::snprintf(count, sizeof(count), "%d", (int)pf.objects.size());
        const ImVec2 capPos = ImGui::GetCursorScreenPos();
        ui::Caption("Members", count);
        const char* frame = "local to the floor centre";
        const float fw = ImGui::CalcTextSize(frame).x;
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x - fw, capPos.y),
            ImGui::GetColorU32(sem.textDim), frame);
        const int total = (int)pf.objects.size();
        const int limit = prefabMembersAll_ ? total : ImMin(total, 4);
        const float right = ImGui::GetContentRegionAvail().x;
        for (int i = 0; i < limit; ++i) {
            const SceneObject& o = pf.objects[i];
            ImGui::PushID(i);
            const bool merges = prefab::memberMerges(o);
            const bool marker = !merges && prefab::memberIsMarker(o);
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float h = ImGui::GetFrameHeight();
            ImGui::InvisibleButton("##member", ImVec2(right, h));
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const float fs = ImGui::GetFontSize();
            const ImU32 gcol = ImGui::GetColorU32(merges ? sem.textDim : marker ? sem.textDim : sem.warn);
            ui::DrawGlyph(dl, ui::Glyph::Box, ImVec2(p.x + fs * 0.6f, p.y + h * 0.5f), fs * 0.8f, gcol);
            char pos[64];
            std::snprintf(pos, sizeof(pos), "%.2f, %.2f, %.2f", o.position[0], o.position[1], o.position[2]);
            float posW;
            {
                ui::FontScope mono(ui::monoFont());
                posW = ImGui::CalcTextSize(pos).x;
                dl->AddText(ImVec2(p.x + right - posW, p.y + (h - fs) * 0.5f), ImGui::GetColorU32(sem.textDim), pos);
            }
            dl->PushClipRect(p, ImVec2(p.x + right - posW - fs, p.y + h), true);
            dl->AddText(ImVec2(p.x + fs * 1.6f, p.y + (h - fs) * 0.5f), ImGui::GetColorU32(sem.text),
                        o.name.c_str());
            dl->PopClipRect();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                ImGui::SetTooltip("%s - %s", primitiveTypeName(o.type),
                                  merges ? "merged into the instance's draw call"
                                  : marker ? "marker (spawned, never drawn)"
                                           : "own object: a spawn-pool slot and a submit");
            ImGui::PopID();
        }
        if (total > 4) {
            char more[48];
            if (prefabMembersAll_) std::snprintf(more, sizeof(more), "Show fewer###pfmembers");
            else std::snprintf(more, sizeof(more), "Show %d more###pfmembers", total - 4);
            ImGui::PushStyleColor(ImGuiCol_Text, sem.textDim);
            if (ImGui::TextLink(more)) prefabMembersAll_ = !prefabMembersAll_;
            ImGui::PopStyleColor();
        }
    }
    ImGui::EndChild();

    // --- footer: the bake ---------------------------------------------------------
    ImGui::Separator();
    if (prefabBakeDiskPath_.empty()) {
        ImGui::TextDisabled("Not baked -");
        ImGui::SameLine();
        const std::string link = "Bake to res/models/" + pf.name + ".obj###Bake to model";
        ImGui::PushStyleColor(ImGuiCol_Text, sem.accent);
        if (ImGui::TextLink(link.c_str())) doBake();
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", bakeTip);
    } else {
        ui::StatusLine(0, ("Baked: " + prefabBakeDiskPath_).c_str());
        if (fresh) {
            ImGui::SameLine();
            ImGui::TextDisabled("%d tris, %d mat", prefabBakeReport_.triangles, prefabBakeReport_.materials);
        }
        ImGui::SameLine();
        // The way BACK from a bake: it is file generation, not a scene edit,
        // so Ctrl+Z cannot cover it - deleting the output is the undo.
        ImGui::PushStyleColor(ImGuiCol_Text, sem.textDim);
        if (ImGui::TextLink("Delete bake...")) openDeleteBake = true;
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("Deletes the generated .obj/.mtl - the undo for a bake, which\n"
                              "Ctrl+Z cannot cover. The prefab stays; re-bake to recreate it.");
        if (fresh) {
            int shownLines = 0;
            for (const std::string& s : prefabBakeReport_.skipped)
                if (shownLines++ < 3) ui::StatusLine(1, ("not baked: " + s).c_str());
            for (const std::string& w : prefabBakeReport_.warnings)
                if (shownLines++ < 3) ui::StatusLine(1, w.c_str());
        }
    }

    // --- modals (opened from the menu / links above, drawn at window scope) ---
    if (openDeletePrefab) ImGui::OpenPopup("Delete Prefab?");
    if (openDeleteBake) ImGui::OpenPopup("Delete Bake?");
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    bool erase = false;
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    // Deletion confirms first: prefabs live OUTSIDE the undo history (the
    // snapshot holds scenes only, like sequences and menus), so Ctrl+Z cannot
    // bring one back - a confirm modal is the only guard there is.
    if (ImGui::BeginPopupModal("Delete Prefab?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Delete prefab \"%s\"?", pf.name.c_str());
        ImGui::TextDisabled("This cannot be undone - prefabs are outside Ctrl+Z.\n"
                            "Copies already placed in scenes stay where they are.");
        // The readout that matters before deleting: graphs that spawn this
        // prefab by name would keep the name and spawn nothing.
        std::vector<std::string> where;
        for (const SceneData& s : project_.scenes)
            for (const std::string& n : prefab::referencedBy(project_, s))
                if (n == pf.name) where.push_back(s.name.empty() ? "(scene)" : s.name);
        if (!where.empty()) {
            std::string line;
            for (size_t i = 0; i < where.size(); ++i) line += (i ? ", " : "") + where[i];
            ImGui::TextColored(sem.warn,
                               "Still spawned by graphs in: %s\n"
                               "Those Spawn Prefab / Pick Prefab entries will stop spawning.",
                               line.c_str());
        }
        ImGui::Separator();
        if (ui::DangerButton("Delete", scaled(120))) {
            erase = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ui::SecondaryButton("Cancel", scaled(120))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Delete Bake?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Delete the baked model of \"%s\"?", pf.name.c_str());
        ImGui::TextUnformatted(prefabBakeDiskPath_.c_str());
        // Who still draws from that file - a Pick Asset row is the likely
        // one, since scattering through it is the whole reason to bake.
        int objUsers = 0, rowUsers = 0;
        for (const SceneData& s : project_.scenes)
            for (const SceneObject& o : s.objects) {
                if (o.type == PrimitiveType::Model && o.modelPath == prefabBakeDiskPath_) ++objUsers;
                for (const ProcNode& n : o.procGraph.nodes) {
                    if (n.type != "PickAsset") continue;
                    for (const ProcRow& r : n.rows)
                        if (r.s == prefabBakeDiskPath_) ++rowUsers;
                }
            }
        if (objUsers > 0 || rowUsers > 0)
            ImGui::TextColored(sem.warn,
                               "Used by %d object(s) and %d Pick Asset row(s) - they\n"
                               "will show as missing until repointed.",
                               objUsers, rowUsers);
        ImGui::TextDisabled("The prefab itself stays; re-bake to recreate the model.");
        ImGui::Separator();
        if (ui::DangerButton("Delete", scaled(120))) {
            const std::string err = prefab::deleteBake(project_, pf);
            if (!err.empty()) {
                statusMessage_ = "Delete bake failed: " + err;
            } else {
                // The bookkeeping the Asset Browser's delete does for a model:
                // per-asset settings keyed by the path go with it.
                const std::string deleted = prefabBakeDiskPath_;
                project_.textureQuality.erase(deleted);
                project_.modelLods.erase(deleted);
                project_.modelUnitMeters.erase(deleted);
                modelInfoCache_.clear();
                viewport_.invalidateAssets();
                if (prefabBakeFor_ == pf.id) prefabBakeFor_.clear();
                prefabBakeDiskKey_.clear();
                // Same marking as App::performAssetDelete, which commits rather
                // than writing - the per-asset maps cleared above live in their
                // own sections, outside this window's guard.
                commitChange();
                statusMessage_ = "Deleted " + deleted;
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ui::SecondaryButton("Cancel", scaled(120))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (erase) {
        project_.prefabs.erase(project_.prefabs.begin() + prefabSelected_);
        if (prefabSelected_ >= (int)project_.prefabs.size())
            prefabSelected_ = (int)project_.prefabs.size() - 1;
        prefabNotesEditing_ = false;
        commitChange();
        statusMessage_ = "Prefab deleted";
    }

    ImGui::End();
    commitIfEdited();
}
