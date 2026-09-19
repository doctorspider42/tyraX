// -------------------------------------------------------------------------
// Tools > Static Batches (docs/static-batching.md): what merged with what,
// what each batch costs in VU1 packages against its members drawn solo, and
// - the half that earns its keep - WHY every object that is not batched is
// not batched.
//
// It exists because the generated game prints two TOTALS at scene load
// ("Static batching: eligible 87, solo 22") and a total is not something
// anybody can act on. The Motor District's own history is the argument: in
// 1.98.0, 111 of its 142 objects were batchable shapes and 27 carried the
// flag, because one build-time rule was rejecting every imported model. The
// counter said 27. It took three rounds of measurement to find out why; a
// panel that names the reason per object answers it in a glance.
//
// THE GROUPING IS NOT COMPUTED HERE. It comes from staticbatch::compute, the
// host twin of the generated buildStaticBatchList, checked against it by
// examples/vehicle-playground/authoring/verify-batch-twins.py. A panel that
// re-derived the grouping would be a third implementation and would
// eventually lie about the one thing it exists to show.
//
// Its own translation unit for the reason every other *_ui.cpp is one
// (app.cpp is the build's critical path). These are App:: members.
// -------------------------------------------------------------------------
#include "app.hpp"
#include "app_internal.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include <imgui.h>

#include "staticbatch.hpp"

namespace {

// A stable, well-spread colour per batch index. Golden-ratio hue so adjacent
// batches never come out near each other, fixed saturation/value so every
// batch reads as the same KIND of thing - the colour is an identity, not a
// measurement, and shading it by size would imply otherwise.
void batchColor(int index, float* rgb) {
    const float h = std::fmod((float)index * 0.61803399f, 1.0f) * 6.0f;
    const int i = (int)h;
    const float f = h - (float)i;
    const float s = 0.62f, v = 1.0f;
    const float p = v * (1.0f - s), q = v * (1.0f - s * f),
                t = v * (1.0f - s * (1.0f - f));
    switch (i % 6) {
        case 0: rgb[0] = v, rgb[1] = t, rgb[2] = p; break;
        case 1: rgb[0] = q, rgb[1] = v, rgb[2] = p; break;
        case 2: rgb[0] = p, rgb[1] = v, rgb[2] = t; break;
        case 3: rgb[0] = p, rgb[1] = q, rgb[2] = v; break;
        case 4: rgb[0] = t, rgb[1] = p, rgb[2] = v; break;
        default: rgb[0] = v, rgb[1] = p, rgb[2] = q; break;
    }
}

// A solo object gets one colour, deliberately unlike any batch colour: a flat
// mid grey. "Not in a batch" is one state, not forty, and giving each solo
// object its own hue would make the picture look like forty tiny batches -
// the exact misreading the overlay is supposed to prevent.
constexpr float kSoloColor[3] = {0.45f, 0.45f, 0.48f};

}  // namespace

// Recomputed on demand rather than every frame: it reads every .tmdl in the
// scene, which is megabytes. `batchDirty_` is set by the commit path and by
// opening the window, which is the same "invalidate on model edit" shape the
// atlas plan uses.
void App::refreshStaticBatches() {
    batchWarnings_.clear();
    batchResult_ = staticbatch::Result();
    if (!hasProject_) return;
    const staticbatch::Inputs in = staticbatch::diskInputs(project_, &batchWarnings_);
    batchResult_ = staticbatch::compute(project_, project_.active(), in, {});
    batchDirty_ = false;
}

// Pushed into the viewport once per frame from drawUI - NOT from the window
// body, so the overlay keeps working with the panel closed (the giBakerPoll
// rule). An empty list is how the viewport is told the overlay is off.
void App::updateBatchOverlay() {
    if (!hasProject_ || !showBatchOverlay_) {
        viewport_.setBatchOverlay({});
        return;
    }
    if (batchDirty_) refreshStaticBatches();
    const SceneData& sc = project_.active();
    std::vector<BatchOverlayBox> boxes;

    // Members first, then the merged boxes on top of them.
    for (size_t oi = 0; oi < batchResult_.objects.size() && oi < sc.objects.size();
         ++oi) {
        const staticbatch::ObjectVerdict& v = batchResult_.objects[oi];
        // Only shapes that could carry geometry are worth drawing - a marker
        // has nothing to batch and would just add boxes to look past.
        const SceneObject& o = sc.objects[oi];
        if (!v.hasGeom || isObjectHiddenInEditor(o)) continue;
        // When one batch is selected, everything else drops to solo grey so
        // the selected group is readable in a dense scene.
        float rgb[3];
        bool member = false;
        if (!v.batches.empty()) {
            const int b = v.batches.front();
            if (batchSelected_ < 0 || batchSelected_ == b) {
                batchColor(b, rgb);
                member = true;
            }
        }
        if (!member) {
            rgb[0] = kSoloColor[0], rgb[1] = kSoloColor[1], rgb[2] = kSoloColor[2];
        }
        BatchOverlayBox box;
        for (int a = 0; a < 3; ++a) {
            box.min[a] = v.geomMin[a];
            box.max[a] = v.geomMax[a];
            box.color[a] = rgb[a];
        }
        boxes.push_back(box);
    }

    // THE MERGED BOX, which is the one worth having an overlay for. A batch
    // passes the frustum and the draw-distance test as a UNIT, so this box -
    // not any member's - is what decides whether the group draws. An
    // over-wide one once drew 400 pixels of geometry the unbatched scene
    // culled, and it is invisible in every other view of the scene.
    for (size_t bi = 0; bi < batchResult_.batches.size(); ++bi) {
        if (batchSelected_ >= 0 && batchSelected_ != (int)bi) continue;
        const staticbatch::Batch& b = batchResult_.batches[bi];
        BatchOverlayBox box;
        box.thick = true;
        batchColor((int)bi, box.color);
        for (int a = 0; a < 3; ++a) {
            box.min[a] = b.geomMin[a];
            box.max[a] = b.geomMax[a];
        }
        boxes.push_back(box);
    }

    // The grouping cell of the selected batch, so "why did these two not
    // merge" has a visible answer - they are usually in different cells.
    if (batchSelected_ >= 0 && batchSelected_ < (int)batchResult_.batches.size() &&
        showBatchCells_) {
        const staticbatch::Batch& b = batchResult_.batches[batchSelected_];
        const float w = b.cellW;
        BatchOverlayBox cell;
        batchColor(batchSelected_, cell.color);
        for (int a = 0; a < 3; ++a) cell.color[a] *= 0.55f;
        cell.min[0] = (float)b.cellX * w - 0.5f * batchResult_.mapW;
        cell.min[2] = (float)b.cellZ * w - 0.5f * batchResult_.mapW;
        cell.max[0] = cell.min[0] + w;
        cell.max[2] = cell.min[2] + w;
        cell.min[1] = b.geomMin[1];
        cell.max[1] = b.geomMax[1];
        boxes.push_back(cell);
    }

    viewport_.setBatchOverlay(std::move(boxes));
}

void App::drawStaticBatchesWindow() {
    if (!showStaticBatches_) return;
    ImGui::SetNextWindowSize(ImVec2(scaled(880), scaled(620)),
                             ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Static Batches", &showStaticBatches_)) {
        ImGui::End();
        return;
    }
    if (!hasProject_) {
        ImGui::TextDisabled("Open a project first.");
        ImGui::End();
        return;
    }
    if (batchDirty_) refreshStaticBatches();
    const SceneData& sc = project_.active();
    const staticbatch::Result& r = batchResult_;

    // The project-wide switch first: with it off there is nothing to look at
    // and the panel would otherwise read as broken.
    if (!project_.settings.staticBatching) {
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f),
                           "Static batching is OFF for this project.");
        ImGui::TextDisabled("Every object submits its own bag.");
        if (ImGui::Button("Open Project Preferences")) {
            showProjectPrefs_ = true;
            prefsFocusTab_ = "Rendering";
            pendingFocusWindow_ = "Project Preferences";
        }
        ImGui::End();
        return;
    }

    // --- the totals, which are the generated game's own two log lines ------
    ImGui::Text("%d batches   %d of %d eligible objects batched   %d solo",
                (int)r.batches.size(), r.batched, r.eligible,
                r.eligible - r.batched);
    prefHelp(
        "The same figures the running game logs at scene load (\"Static "
        "batching: eligible N, solo M\"). What it cannot say, and this panel "
        "can, is WHICH objects and why each one.");
    ImGui::SameLine();
    ImGui::TextDisabled("| base cell %.0f, map %.0f", r.baseCellW, r.mapW);

    if (r.anyModelUnbaked)
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f),
                           "Some models have no baked .tmdl yet - build the "
                           "project for their batching to be known.");

    ImGui::Checkbox("Show overlay", &showBatchOverlay_);
    ImGui::SameLine();
    ImGui::Checkbox("Show cell", &showBatchCells_);
    prefHelp(
        "Draws the grouping cell of the selected batch. Two objects that look "
        "like they should have merged usually sit in different cells.");
    ImGui::SameLine();
    if (ImGui::Button("Recompute")) batchDirty_ = true;

    ImGui::Separator();

    if (ImGui::BeginTabBar("##batchtabs")) {
        // --- the batches -----------------------------------------------------
        if (ImGui::BeginTabItem("Batches")) {
            ImGui::TextDisabled(
                "Packages are what the EE pays for: about 19.5 us each in the "
                "Motor District's garage pose, measured on hardware.");
            prefHelp(
                "docs/ee-submission-rearchitecture.md. Do NOT carry that "
                "figure as a constant - the same measurement reads 31.8 us in "
                "the night pose, and the bracket split does not support a "
                "pure per-package model. It is the right order of magnitude "
                "for deciding whether a merge is worth having, and nothing "
                "more.");
            const ImVec2 avail = ImGui::GetContentRegionAvail();
            if (ImGui::BeginChild("##batchlist", ImVec2(0, avail.y))) {
                if (ImGui::BeginTable("##batches", 7,
                                      ImGuiTableFlags_Borders |
                                          ImGuiTableFlags_RowBg |
                                          ImGuiTableFlags_ScrollY |
                                          ImGuiTableFlags_Resizable)) {
                    ImGui::TableSetupScrollFreeze(0, 1);
                    ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed,
                                            scaled(34));
                    ImGui::TableSetupColumn("texture");
                    ImGui::TableSetupColumn("members",
                                            ImGuiTableColumnFlags_WidthFixed,
                                            scaled(60));
                    ImGui::TableSetupColumn("cell",
                                            ImGuiTableColumnFlags_WidthFixed,
                                            scaled(90));
                    ImGui::TableSetupColumn("merged box",
                                            ImGuiTableColumnFlags_WidthFixed,
                                            scaled(120));
                    ImGui::TableSetupColumn("packages",
                                            ImGuiTableColumnFlags_WidthFixed,
                                            scaled(100));
                    ImGui::TableSetupColumn("draw",
                                            ImGuiTableColumnFlags_WidthFixed,
                                            scaled(50));
                    ImGui::TableHeadersRow();
                    for (size_t bi = 0; bi < r.batches.size(); ++bi) {
                        const staticbatch::Batch& b = r.batches[bi];
                        ImGui::TableNextRow();
                        ImGui::PushID((int)bi);
                        ImGui::TableNextColumn();
                        float rgb[3];
                        batchColor((int)bi, rgb);
                        ImGui::TextColored(ImVec4(rgb[0], rgb[1], rgb[2], 1.0f),
                                           "%d", (int)bi);
                        ImGui::TableNextColumn();
                        // Selecting a batch focuses the overlay on it.
                        const bool sel = batchSelected_ == (int)bi;
                        if (ImGui::Selectable(
                                b.texture.empty() ? "(no texture)"
                                                  : b.texture.c_str(),
                                sel, ImGuiSelectableFlags_SpanAllColumns))
                            batchSelected_ = sel ? -1 : (int)bi;
                        if (b.texture.empty() && ImGui::IsItemHovered())
                            ImGui::SetTooltip(
                                "Untextured, or a texture whose file is "
                                "missing - the game binds no texture for "
                                "either, so they share one group.");
                        ImGui::TableNextColumn();
                        ImGui::Text("%d", (int)b.members.size());
                        ImGui::TableNextColumn();
                        ImGui::Text("%.0f @ %d,%d", b.cellW, b.cellX, b.cellZ);
                        ImGui::TableNextColumn();
                        ImGui::Text("%.0f x %.0f x %.0f",
                                    b.geomMax[0] - b.geomMin[0],
                                    b.geomMax[1] - b.geomMin[1],
                                    b.geomMax[2] - b.geomMin[2]);
                        ImGui::TableNextColumn();
                        const int d = b.soloPackages - b.packages;
                        if (d > 0)
                            ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1.0f),
                                               "%d (-%d)", b.packages, d);
                        else if (d < 0)
                            ImGui::TextColored(ImVec4(0.95f, 0.5f, 0.4f, 1.0f),
                                               "%d (+%d)", b.packages, -d);
                        else
                            ImGui::Text("%d", b.packages);
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip(
                                "%d VU1 packages merged, %d if these members "
                                "drew solo.", b.packages, b.soloPackages);
                        ImGui::TableNextColumn();
                        if (b.drawDistance > 0.0f)
                            ImGui::Text("%.0f", b.drawDistance);
                        else
                            ImGui::TextDisabled("inf");
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        // --- the members of the selected batch -------------------------------
        if (ImGui::BeginTabItem("Members")) {
            if (batchSelected_ < 0 ||
                batchSelected_ >= (int)r.batches.size()) {
                ImGui::TextDisabled("Select a batch on the Batches tab.");
            } else {
                const staticbatch::Batch& b = r.batches[batchSelected_];
                ImGui::Text("Batch %d: %zu members", batchSelected_,
                            b.members.size());
                if (b.lamp >= 0 && b.lamp < (int)sc.objects.size()) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("| lit by %s",
                                        sc.objects[b.lamp].name.c_str());
                    prefHelp(
                        "A bag gets ONE dynamic light, picked from its merged "
                        "bounding sphere - so which lamp reaches an object is "
                        "part of the group key.");
                }
                if (b.stripRun > 0)
                    ImGui::TextDisabled("Triangle strip, runs of %u vertices.",
                                        b.stripRun);
                else
                    ImGui::TextDisabled("Triangle list.");
                ImGui::Separator();
                if (ImGui::BeginTable("##members", 3,
                                      ImGuiTableFlags_Borders |
                                          ImGuiTableFlags_RowBg |
                                          ImGuiTableFlags_ScrollY)) {
                    ImGui::TableSetupColumn("object");
                    ImGui::TableSetupColumn("part",
                                            ImGuiTableColumnFlags_WidthFixed,
                                            scaled(50));
                    ImGui::TableSetupColumn("exclude",
                                            ImGuiTableColumnFlags_WidthFixed,
                                            scaled(70));
                    ImGui::TableHeadersRow();
                    for (const staticbatch::Member& m : b.members) {
                        if (m.object < 0 || m.object >= (int)sc.objects.size())
                            continue;
                        ImGui::TableNextRow();
                        ImGui::PushID(m.object * 16 + m.part + 1);
                        ImGui::TableNextColumn();
                        if (ImGui::Selectable(sc.objects[m.object].name.c_str(),
                                              false))
                            selectOnly(m.object, false);
                        ImGui::TableNextColumn();
                        if (m.part >= 0)
                            ImGui::Text("%d", m.part);
                        else
                            ImGui::TextDisabled("-");
                        ImGui::TableNextColumn();
                        drawBatchExcludeCheckbox(m.object);
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
            }
            ImGui::EndTabItem();
        }

        // --- WHY NOT, the half that earns its keep ---------------------------
        if (ImGui::BeginTabItem("Not batched")) {
            ImGui::TextDisabled(
                "Build reasons are properties of the object. Runtime reasons "
                "depend on what else shares its cell.");
            ImGui::Separator();
            if (ImGui::BeginTable("##notbatched", 4,
                                  ImGuiTableFlags_Borders |
                                      ImGuiTableFlags_RowBg |
                                      ImGuiTableFlags_ScrollY |
                                      ImGuiTableFlags_Resizable)) {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("object");
                ImGui::TableSetupColumn("stage",
                                        ImGuiTableColumnFlags_WidthFixed,
                                        scaled(60));
                ImGui::TableSetupColumn("reason");
                ImGui::TableSetupColumn("exclude",
                                        ImGuiTableColumnFlags_WidthFixed,
                                        scaled(70));
                ImGui::TableHeadersRow();
                int shown = 0;
                for (size_t oi = 0;
                     oi < r.objects.size() && oi < sc.objects.size(); ++oi) {
                    const staticbatch::Reason rr = r.objects[oi].reason;
                    if (rr == staticbatch::Reason::Batched) continue;
                    // A marker that could never carry geometry is noise, not
                    // a finding: the question this tab answers is which
                    // SHAPES are missing out.
                    if (rr == staticbatch::Reason::NotABatchableShape) continue;
                    ++shown;
                    ImGui::TableNextRow();
                    ImGui::PushID((int)oi);
                    ImGui::TableNextColumn();
                    if (ImGui::Selectable(sc.objects[oi].name.c_str(), false))
                        selectOnly((int)oi, false);
                    ImGui::TableNextColumn();
                    ImGui::TextDisabled("%s", staticbatch::reasonStage(rr));
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(staticbatch::reasonLabel(rr));
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("%s", staticbatch::reasonDetail(rr));
                    ImGui::TableNextColumn();
                    drawBatchExcludeCheckbox((int)oi);
                    ImGui::PopID();
                }
                ImGui::EndTable();
                if (!shown)
                    ImGui::TextDisabled("Every batchable shape is in a batch.");
            }
            ImGui::EndTabItem();
        }

        if (!batchWarnings_.empty() && ImGui::BeginTabItem("Warnings")) {
            for (const std::string& w : batchWarnings_)
                ImGui::TextWrapped("%s", w.c_str());
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

// The one per-object lever, drawn the same way wherever it appears (here and
// in Properties) so it reads as one control and not two features.
void App::drawBatchExcludeCheckbox(int objectIndex) {
    SceneData& sc = project_.active();
    if (objectIndex < 0 || objectIndex >= (int)sc.objects.size()) return;
    bool v = sc.objects[objectIndex].batchExclude;
    // Checkbox commits on its RETURN VALUE - IsItemDeactivatedAfterEdit can
    // never be true for one (it activates and edits in the same frame), which
    // is a mistake this repo has shipped three times.
    if (ImGui::Checkbox("##batchexclude", &v)) {
        sc.objects[objectIndex].batchExclude = v;
        commitChange();
        batchDirty_ = true;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "Keep this object out of every static batch.\n"
            "Use it when one outlying member widens a batch's merged box "
            "enough to keep the whole group drawn past what the scene would "
            "cull.");
}
