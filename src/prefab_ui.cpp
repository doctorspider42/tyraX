// -------------------------------------------------------------------------
// Tools > Prefabs (docs/prefabs.md): reusable groups of scene objects - their
// flow graphs included - captured once and stamped into the world by hand, by
// a procedural graph's Pick Prefab node, or by the Spawn Prefab flow node while
// the game runs.
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
#include <string>
#include <vector>

#include "prefab.hpp"

#include <imgui.h>

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

}  // namespace

void App::drawPrefabsWindow() {
    if (!showPrefabs_) return;
    ImGui::SetNextWindowSize(ImVec2(scaled(560), scaled(460)), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Prefabs", &showPrefabs_)) {
        ImGui::End();
        return;
    }
    if (!hasProject_) {
        ImGui::TextDisabled("Open a project first.");
        ImGui::End();
        return;
    }

    // --- capture -----------------------------------------------------------
    const int selCount = (int)selection_.size();
    ImGui::BeginDisabled(selCount == 0);
    if (ImGui::Button(selCount > 1
                          ? ("Create from selection (" + std::to_string(selCount) +
                             " objects)").c_str()
                          : "Create from selection")) {
        std::vector<int> sel = selection_;
        std::sort(sel.begin(), sel.end());
        const std::string base =
            (sel.size() == 1 && sel[0] < (int)project_.objects().size())
                ? project_.objects()[sel[0]].name
                : "prefab";
        Prefab pf = prefab::capture(project_.active(), sel,
                                    prefab::uniqueName(project_, base));
        if (pf.objects.empty()) {
            statusMessage_ = "Nothing to capture";
        } else {
            project_.prefabs.push_back(std::move(pf));
            prefabSelected_ = (int)project_.prefabs.size() - 1;
            saveAll("Prefab created");
        }
    }
    ImGui::EndDisabled();
    if (selCount == 0 && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip(
            "Select the objects in the viewport or the Project panel first.\n"
            "The prefab's origin is the selection's footprint centre at its\n"
            "lowest point, so clicking the ground places it the way you expect.");
    ImGui::SameLine();
    prefHelp(
        "A prefab is a piece of SCENE, not a lighter thing: members keep their "
        "materials, physics, scripts and flow graphs. Insert stamps copies into "
        "the world with fresh identities; nothing stays linked back to the "
        "prefab, so editing it later does not disturb what you already placed.");

    ImGui::Separator();

    if (project_.prefabs.empty()) {
        ImGui::TextDisabled("No prefabs yet.");
        ImGui::TextWrapped(
            "Build something out of ordinary objects - a hut, a lamp post with "
            "its light and its script, one room of a maze - select it all, and "
            "press the button above.");
        ImGui::End();
        return;
    }
    if (prefabSelected_ >= (int)project_.prefabs.size()) prefabSelected_ = 0;

    // --- list --------------------------------------------------------------
    ImGui::BeginChild("prefab-list", ImVec2(scaled(190), 0), true);
    for (int i = 0; i < (int)project_.prefabs.size(); ++i) {
        const Prefab& pf = project_.prefabs[i];
        char label[128];
        snprintf(label, sizeof(label), "%s  (%d)##pf%d", pf.name.c_str(),
                 (int)pf.objects.size(), i);
        if (ImGui::Selectable(label, prefabSelected_ == i) && prefabSelected_ != i) {
            prefabSelected_ = i;
            prefabNotesEditing_ = false;  // the buffer belonged to the old one
        }
        if (!pf.notes.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(scaled(320));
            ImGui::TextUnformatted(pf.notes.c_str());
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();

    ImGui::BeginChild("prefab-detail", ImVec2(0, 0), false);
    Prefab& pf = project_.prefabs[prefabSelected_];

    char nameBuf[96];
    snprintf(nameBuf, sizeof(nameBuf), "%s", pf.name.c_str());
    ImGui::SetNextItemWidth(scaled(220));
    if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf),
                         ImGuiInputTextFlags_EnterReturnsTrue)) {
        // Rename retargets every reference (flow nodes, Pick Prefab rows) so a
        // renamed prefab never silently stops spawning.
        if (!prefab::rename(project_, pf.name, nameBuf))
            statusMessage_ = "That prefab name is taken";
        else
            saveAll("Prefab renamed");
    }
    // Notes: read as WRAPPED prose, edited in a multiline box on click.
    //
    // The obvious widget is the wrong one twice over. A single-line InputText
    // (what this was) shows the first ~50 characters and hides the rest behind
    // a caret nobody thinks to move - the field that documents a prefab was
    // the one field you could not read. And ImGui's multiline InputText does
    // not word-wrap either: a long note stays one line and scrolls sideways.
    // So the resting state is a plain wrapped paragraph and the editor only
    // appears while it is being typed in.
    ImGui::TextUnformatted("Notes");
    const float notesH = ImGui::GetTextLineHeight() * 3.6f;
    if (prefabNotesEditing_) {
        if (prefabNotesFocus_) {
            ImGui::SetKeyboardFocusHere();
            prefabNotesFocus_ = false;
        }
        // Multiline has no Enter to commit on, so the buffer is a MEMBER: the
        // refresh-from-the-model-every-frame pattern the single-line fields use
        // works only because EnterReturnsTrue copies back on the Enter frame.
        ImGui::InputTextMultiline("##pfnotes", prefabNotesBuf_,
                                  sizeof(prefabNotesBuf_),
                                  ImVec2(-FLT_MIN, notesH),
                                  ImGuiInputTextFlags_AllowTabInput);
        if (ImGui::IsItemDeactivated()) {
            prefabNotesEditing_ = false;
            if (pf.notes != prefabNotesBuf_) {
                pf.notes = prefabNotesBuf_;
                saveAll("Prefab notes");
            }
        }
        ImGui::TextDisabled("Enter starts a new line; click away to save.");
    } else {
        ImGui::BeginChild("##pfnotesview", ImVec2(-FLT_MIN, notesH),
                          ImGuiChildFlags_Borders);
        if (pf.notes.empty())
            ImGui::TextDisabled("(click to describe what this prefab is for)");
        else
            ImGui::TextWrapped("%s", pf.notes.c_str());
        const bool clicked = ImGui::IsWindowHovered() &&
                             ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        ImGui::EndChild();
        if (clicked) {
            snprintf(prefabNotesBuf_, sizeof(prefabNotesBuf_), "%s",
                     pf.notes.c_str());
            prefabNotesEditing_ = true;
            prefabNotesFocus_ = true;
        }
    }

    if (ImGui::Button("Insert into scene")) {
        // Reuses the deferred-paste placement: the copies follow the cursor and
        // land on whatever is under it, exactly like Ctrl+V.
        pasteStaged_ = prefab::instantiate(pf, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, "");
        // Unique names within the scene, so two instances stay tellable apart.
        for (SceneObject& o : pasteStaged_) {
            std::string want = o.name;
            for (int n = 2;; ++n) {
                bool taken = false;
                for (const SceneObject& other : project_.objects())
                    taken |= (other.name == want);
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
    ImGui::SameLine();
    if (ImGui::Button("Replace from selection")) {
        if (selection_.empty()) {
            statusMessage_ = "Select the objects to capture first";
        } else {
            std::vector<int> sel = selection_;
            std::sort(sel.begin(), sel.end());
            Prefab np = prefab::capture(project_.active(), sel, pf.name);
            np.id = pf.id;
            np.notes = pf.notes;
            pf = std::move(np);
            saveAll("Prefab replaced");
        }
    }
    ImGui::SameLine();
    // The escape hatch from the instance pool: a prefab instance costs a
    // runtime record, a model costs none, so anything you want to scatter by
    // the hundred wants to be a model. One way, and the prefab stays as source.
    if (ImGui::Button("Bake to model")) {
        const prefab::BakeReport r = prefab::bakeToModel(project_, pf);
        if (!r.error.empty()) {
            statusMessage_ = "Bake failed: " + r.error;
        } else {
            statusMessage_ = "Baked " + std::to_string(r.members) +
                             " member(s) into " + r.modelPath + " - " +
                             std::to_string(r.triangles) + " triangles, " +
                             std::to_string(r.materials) + " material(s)";
            if (!r.skipped.empty())
                statusMessage_ += " | skipped " +
                                  std::to_string(r.skipped.size()) + ": " +
                                  r.skipped.front() +
                                  (r.skipped.size() > 1 ? ", ..." : "");
            for (const std::string& w : r.warnings) statusMessage_ += " | " + w;
            prefabBakeReport_ = r;
            prefabBakeFor_ = pf.id;
            prefabBakeDiskKey_.clear();  // the on-disk readout just changed
        }
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip(
            "Flattens the mergeable members into one res/models/<name>.obj (plus\n"
            "a generated .mtl carrying their colours), so the prefab can be\n"
            "scattered with Pick Asset instead of Pick Prefab.\n\n"
            "Why you would: a prefab instance takes a record from the runtime\n"
            "pool (%d of them exist), so a few dozen is the ceiling however\n"
            "cheap each one is. A model costs NO record - it merges straight\n"
            "into the chunk bags, so hundreds are fine.\n\n"
            "One way, and the result is dumb geometry: no scripts, lights,\n"
            "physics or per-member identity. The prefab stays put as the source;\n"
            "members that cannot merge are listed rather than silently dropped.",
            prefab::kMaxRuntimeInstances);
    ImGui::SameLine();
    // Deletion confirms first: prefabs live OUTSIDE the undo history (the
    // snapshot holds scenes only, like sequences and menus), so Ctrl+Z cannot
    // bring one back - a confirm modal is the only guard there is.
    if (ImGui::Button("Delete")) ImGui::OpenPopup("Delete Prefab?");
    {
        const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal("Delete Prefab?", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Delete prefab \"%s\"?", pf.name.c_str());
            ImGui::TextDisabled(
                "This cannot be undone - prefabs are outside Ctrl+Z.\n"
                "Copies already placed in scenes stay where they are.");
            // The readout that matters before deleting: graphs that spawn this
            // prefab by name would keep the name and spawn nothing.
            {
                std::vector<std::string> where;
                for (const SceneData& s : project_.scenes)
                    for (const std::string& n : prefab::referencedBy(project_, s))
                        if (n == pf.name)
                            where.push_back(s.name.empty() ? "(scene)" : s.name);
                if (!where.empty()) {
                    std::string line;
                    for (size_t i = 0; i < where.size(); ++i)
                        line += (i ? ", " : "") + where[i];
                    ImGui::TextColored(
                        ImVec4(0.95f, 0.72f, 0.25f, 1.0f),
                        "Still spawned by graphs in: %s\n"
                        "Those Spawn Prefab / Pick Prefab entries will stop "
                        "spawning.",
                        line.c_str());
                }
            }
            ImGui::Separator();
            if (ImGui::Button("Delete", ImVec2(scaled(120), 0))) {
                project_.prefabs.erase(project_.prefabs.begin() + prefabSelected_);
                if (prefabSelected_ >= (int)project_.prefabs.size())
                    prefabSelected_ = (int)project_.prefabs.size() - 1;
                saveAll("Prefab deleted");
                ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
                ImGui::EndChild();
                ImGui::End();
                return;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(scaled(120), 0)))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }

    // Baked or not - read from DISK, so the answer survives a restart (the
    // green report line only lives for the session that clicked the button).
    // Cached: the check is a file read, and the key covers a rename (the stem
    // comes from the name, so a renamed prefab's bake is a different file).
    {
        const std::string key = pf.id + "|" + pf.name;
        if (key != prefabBakeDiskKey_) {
            prefabBakeDiskKey_ = key;
            prefabBakeDiskPath_ = prefab::bakeOnDisk(project_, pf);
        }
    }
    if (!prefabBakeDiskPath_.empty()) {
        ImGui::TextColored(ImVec4(0.4f, 0.85f, 0.5f, 1.0f), "Baked: %s",
                           prefabBakeDiskPath_.c_str());
        // The fresh bake's numbers, only under the prefab they belong to -
        // drawn unconditionally they read as "the last bake applied to every
        // prefab in the list".
        const bool fresh = !prefabBakeReport_.modelPath.empty() &&
                           prefabBakeFor_ == pf.id;
        if (fresh) {
            ImGui::SameLine();
            ImGui::TextDisabled("(%d tris, %d material(s))",
                                prefabBakeReport_.triangles,
                                prefabBakeReport_.materials);
        }
        ImGui::SameLine();
        // The way BACK from a bake: it is file generation, not a scene edit,
        // so Ctrl+Z cannot cover it - deleting the output is the undo, and it
        // belongs here rather than in a dig through the Asset Browser.
        if (ImGui::SmallButton("Delete bake...")) ImGui::OpenPopup("Delete Bake?");
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip(
                "Deletes the generated .obj/.mtl - the undo for a bake, which\n"
                "Ctrl+Z cannot cover (it writes files, not scene edits). The\n"
                "prefab stays; re-bake to recreate the model.");
        if (fresh) {
            for (const std::string& s : prefabBakeReport_.skipped)
                ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.25f, 1.0f),
                                   "  not baked: %s", s.c_str());
            for (const std::string& w : prefabBakeReport_.warnings)
                ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.25f, 1.0f), "  %s",
                                   w.c_str());
        }
        const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal("Delete Bake?", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Delete the baked model of \"%s\"?", pf.name.c_str());
            ImGui::TextUnformatted(prefabBakeDiskPath_.c_str());
            // Who still draws from that file - a Pick Asset row is the likely
            // one, since scattering through it is the whole reason to bake.
            {
                int objUsers = 0, rowUsers = 0;
                for (const SceneData& s : project_.scenes)
                    for (const SceneObject& o : s.objects) {
                        if (o.type == PrimitiveType::Model &&
                            o.modelPath == prefabBakeDiskPath_)
                            ++objUsers;
                        for (const ProcNode& n : o.procGraph.nodes) {
                            if (n.type != "PickAsset") continue;
                            for (const ProcRow& r : n.rows)
                                if (r.s == prefabBakeDiskPath_) ++rowUsers;
                        }
                    }
                if (objUsers > 0 || rowUsers > 0)
                    ImGui::TextColored(
                        ImVec4(0.95f, 0.72f, 0.25f, 1.0f),
                        "Used by %d object(s) and %d Pick Asset row(s) - they\n"
                        "will show as missing until repointed.",
                        objUsers, rowUsers);
            }
            ImGui::TextDisabled(
                "The prefab itself stays; re-bake to recreate the model.");
            ImGui::Separator();
            if (ImGui::Button("Delete", ImVec2(scaled(120), 0))) {
                const std::string err = prefab::deleteBake(project_, pf);
                if (!err.empty()) {
                    statusMessage_ = "Delete bake failed: " + err;
                } else {
                    // The bookkeeping the Asset Browser's delete does for a
                    // model: per-asset settings keyed by the path go with it.
                    const std::string deleted = prefabBakeDiskPath_;
                    project_.textureQuality.erase(deleted);
                    project_.modelLods.erase(deleted);
                    project_.modelUnitMeters.erase(deleted);
                    modelInfoCache_.clear();
                    viewport_.invalidateAssets();
                    if (prefabBakeFor_ == pf.id) prefabBakeFor_.clear();
                    prefabBakeDiskKey_.clear();
                    saveAll(("Deleted " + deleted).c_str());
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(scaled(120), 0)))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    } else {
        ImGui::TextDisabled(
            "Not baked to a model - Bake to model writes res/models/%s.obj.",
            pf.name.c_str());
    }

    ImGui::Separator();

    // --- runtime cost ------------------------------------------------------
    const PrefabCost cost = prefabCost(pf);
    float mn[3], mx[3];
    prefab::bounds(pf, mn, mx);
    ImGui::Text("%d member%s, %.1f x %.1f x %.1f units", (int)pf.objects.size(),
                pf.objects.size() == 1 ? "" : "s", mx[0] - mn[0], mx[1] - mn[1],
                mx[2] - mn[2]);
    ImGui::Text("Runtime: %d merged into %d draw call%s, %d spawned object%s",
                cost.merged, cost.materials, cost.materials == 1 ? "" : "s",
                cost.spawned, cost.spawned == 1 ? "" : "s");
    ImGui::SameLine();
    prefHelp(
        "When the game spawns this prefab, static members (plain primitives and "
        "static .obj models) are merged into one geometry bag per material - a "
        "PS2 draw submit costs ~1 ms of EE time whatever it contains, so that "
        "merge is the whole reason a prefab is affordable at runtime. Members "
        "with a flow graph, scripts, physics, a streaming layer, a light or an "
        "emitter cannot be merged: each takes a slot in the 32-clone spawn pool "
        "and a submit of its own. Fewer of those = more instances you can "
        "afford.");
    if (cost.spawned > 8)
        ImGui::TextColored(ImVec4(0.95f, 0.72f, 0.25f, 1.0f),
                           "%d spawned members per instance - the runtime clone "
                           "pool holds 32 in total.",
                           cost.spawned);

    // Where it is used - the readout that answers "can I delete this".
    {
        std::vector<std::string> where;
        for (const SceneData& s : project_.scenes) {
            for (const std::string& n : prefab::referencedBy(project_, s))
                if (n == pf.name)
                    where.push_back(s.name.empty() ? "(scene)" : s.name);
        }
        if (where.empty())
            ImGui::TextDisabled("Not spawned by any graph - placed copies only.");
        else {
            std::string line;
            for (size_t i = 0; i < where.size(); ++i)
                line += (i ? ", " : "") + where[i];
            ImGui::TextWrapped("Spawned at runtime in: %s", line.c_str());
        }
    }

    ImGui::Separator();
    ImGui::TextDisabled("Members");
    ImGui::BeginChild("prefab-members", ImVec2(0, 0), true);
    if (ImGui::BeginTable("pfm", 4,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Name");
        ImGui::TableSetupColumn("Type");
        ImGui::TableSetupColumn("Local position");
        ImGui::TableSetupColumn("Runtime");
        ImGui::TableHeadersRow();
        for (const SceneObject& o : pf.objects) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(o.name.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(primitiveTypeName(o.type));
            ImGui::TableNextColumn();
            ImGui::Text("%.2f, %.2f, %.2f", o.position[0], o.position[1],
                        o.position[2]);
            ImGui::TableNextColumn();
            if (prefab::memberMerges(o))
                ImGui::TextUnformatted("merged");
            else if (prefab::memberIsMarker(o))
                ImGui::TextDisabled("marker");
            else
                ImGui::TextColored(ImVec4(0.95f, 0.72f, 0.25f, 1.0f), "own object");
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();

    ImGui::EndChild();
    ImGui::End();
}
