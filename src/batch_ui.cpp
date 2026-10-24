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
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include <imgui.h>

#include "staticbatch.hpp"
#include "theme.hpp"
#include "toolkit.hpp"

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
    batchComputedAt_ = ImGui::GetTime();
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

namespace {

ImU32 col32(const ImVec4& c, float a = 1.0f) {
    return ImGui::GetColorU32(ImVec4(c.x, c.y, c.z, c.w * a));
}

std::string fileNameOf(const std::string& path) {
    const size_t s = path.find_last_of("/\\");
    return s == std::string::npos ? path : path.substr(s + 1);
}

bool containsNoCase(const std::string& hay, const char* needle) {
    if (!needle || !*needle) return true;
    std::string h = hay, n = needle;
    for (char& c : h) c = (char)std::tolower((unsigned char)c);
    for (char& c : n) c = (char)std::tolower((unsigned char)c);
    return h.find(n) != std::string::npos;
}

// The one-click fix for a build-stage reason, where the fix is a flag the
// author set and can simply clear. Gameplay reasons (physics, pickable, a
// flow graph...) are the object doing its job: those read "expected".
struct BatchFix {
    const char* label = nullptr;  // null = no fix offered
    bool expected = false;        // the reason is the object's purpose
};

BatchFix batchFixFor(staticbatch::Reason r) {
    using R = staticbatch::Reason;
    switch (r) {
        case R::Reflected: return {"Turn off reflections", false};
        case R::ExcludedByAuthor: return {"Include in batches", false};
        case R::Physics:
        case R::Usable:
        case R::Pickable:
        case R::SaveState:
        case R::InvisibleWall:
        case R::TextureFeed:
        case R::GraphOrScripts:
        case R::RuntimeReferenced:
        case R::StreamingLayer: return {nullptr, true};
        default: return {};
    }
}

void applyBatchFix(SceneObject& o, staticbatch::Reason r) {
    if (r == staticbatch::Reason::Reflected) o.reflected = false;
    if (r == staticbatch::Reason::ExcludedByAuthor) o.batchExclude = false;
}

// A texture swatch: the image itself when the viewport has it, a hatched
// field for "no texture".
void textureSwatch(ImDrawList* dl, ImVec2 p, float s, uint32_t tex) {
    const theme::Semantics& sem = theme::semantics();
    const ImVec2 q(p.x + s, p.y + s);
    const float r = ImGui::GetStyle().FrameRounding * 0.6f;
    if (tex) {
        dl->AddImageRounded((ImTextureID)(intptr_t)tex, p, q, ImVec2(0, 0), ImVec2(1, 1),
                            IM_COL32_WHITE, r);
    } else {
        dl->AddRectFilled(p, q, col32(sem.field), r);
        dl->PushClipRect(p, q, true);
        for (float o = -s; o < s; o += s * 0.28f)
            dl->AddLine(ImVec2(p.x + o, q.y), ImVec2(p.x + o + s, p.y), col32(sem.border), 1.0f);
        dl->PopClipRect();
    }
    dl->AddRect(p, q, col32(sem.border), r);
}

// The batch's texture as a project-relative path the viewport can load: a
// .tmdl names its textures relative to res/, a material relative to the
// project. Resolved once per path (a stat per frame per row is not free).
std::string texturePathFor(const Project& p, const std::string& tex) {
    static std::map<std::string, std::string> cache;
    const std::string key = p.dir + "|" + tex;
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;
    std::error_code ec;
    std::string rel = tex;
    if (!std::filesystem::exists(p.filePath(tex), ec) &&
        std::filesystem::exists(p.filePath("res/" + tex), ec))
        rel = "res/" + tex;
    cache[key] = rel;
    return rel;
}

// A summary tile's value line: the number big in the heading cut, an
// optional dim tail after it.
void tileValue(const char* value, const ImVec4& col, const char* tail = nullptr) {
    const float fs = ImGui::GetFontSize();
    ImGui::PushFont(ui::boldFont(), fs * 1.45f);
    ImGui::PushStyleColor(ImGuiCol_Text, col);
    ImGui::TextUnformatted(value);
    ImGui::PopStyleColor();
    ImGui::PopFont();
    if (tail) {
        ImGui::SameLine(0.0f, fs * 0.4f);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + fs * 0.3f);
        ui::FontScope mono(ui::monoFont());
        ImGui::TextDisabled("%s", tail);
    }
}

}  // namespace

void App::drawStaticBatchesWindow() {
    if (!showStaticBatches_) return;
    ImGui::SetNextWindowSize(ImVec2(scaled(1200), scaled(800)), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Static Batches", &showStaticBatches_)) {
        ImGui::End();
        return;
    }
    if (!hasProject_) {
        ui::EmptyState(ui::Glyph::Layers, "No project open", "Open a project to see its batches.");
        ImGui::End();
        return;
    }
    if (batchDirty_) refreshStaticBatches();
    SceneData& sc = project_.active();
    const staticbatch::Result& r = batchResult_;
    const theme::Semantics& sem = theme::semantics();
    const float fs = ImGui::GetFontSize();
    const ImGuiStyle& st = ImGui::GetStyle();

    // --- header: where the numbers come from, and Recompute ---------------
    {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s / %s", project_.name.c_str(), sc.name.c_str());
        char when[96];
        const double age = batchComputedAt_ < 0 ? 0.0 : ImGui::GetTime() - batchComputedAt_;
        if (age < 60.0)
            std::snprintf(when, sizeof(when), "from baked .tmdl \xc2\xb7 updated %d s ago", (int)age);
        else
            std::snprintf(when, sizeof(when), "from baked .tmdl \xc2\xb7 updated %d min ago",
                          (int)(age / 60.0));
        const float bw = ImGui::CalcTextSize("Recompute").x + st.FramePadding.x * 2.6f;
        const float right = ImGui::GetWindowContentRegionMax().x;
        ImGui::SameLine(right - bw - ImGui::CalcTextSize(when).x - st.ItemSpacing.x);
        ImGui::TextDisabled("%s", when);
        ImGui::SameLine(right - bw);
        if (ui::GhostButton("Recompute", bw)) batchDirty_ = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Re-read every baked .tmdl. Scene edits recompute on their own.");
    }

    // The project-wide switch first: with it off there is nothing to look at.
    if (!project_.settings.staticBatching) {
        if (ui::EmptyState(ui::Glyph::Layers, "Static batching is off",
                           "Every object submits its own bag. The switch is in Project "
                           "Preferences > Graphics.",
                           "Open Project Preferences")) {
            showProjectPrefs_ = true;
            prefsFocusTab_ = "Rendering";
            pendingFocusWindow_ = "Project Preferences";
        }
        ImGui::End();
        return;
    }
    // Nothing baked yet: an empty table would read as a broken result.
    if (r.batches.empty() && r.anyModelUnbaked) {
        const bool busy = runner_.busy();
        if (ui::EmptyState(ui::Glyph::Layers, "Build once to see the batches",
                           "Batching is decided from the baked models (.tmdl), and this "
                           "scene's models have not been baked yet.",
                           busy ? nullptr : "Build"))
            runner_.buildAndRun(projectForBuild(), false);
        if (busy) ui::ProgressInline(-1.0f, "Building...");
        ImGui::End();
        return;
    }

    // --- summary tiles ------------------------------------------------------
    int saved = 0;
    for (const staticbatch::Batch& b : r.batches) saved += b.soloPackages - b.packages;
    const int solo = r.eligible - r.batched;
    {
        const float gap = st.ItemSpacing.x;
        const float tileH = fs * 4.9f;
        const float total = ImGui::GetContentRegionAvail().x;
        const float wide = (total - gap * 4.0f) * 0.245f;
        const float narrow = (total - gap * 4.0f - wide * 2.0f) / 3.0f;
        char buf[64], tail[64];
        // Fixed-height tiles: a scrollbar on a four-line card is noise.
        ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 0.0f);

        if (ui::BeginCard("##tile_batched", ImVec2(wide, tileH))) {
            ImGui::TextDisabled("Objects batched");
            std::snprintf(buf, sizeof(buf), "%d", r.batched);
            std::snprintf(tail, sizeof(tail), "/ %d eligible", r.eligible);
            tileValue(buf, sem.text, tail);
            const float f = r.eligible > 0 ? (float)r.batched / (float)r.eligible : 0.0f;
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float w = ImGui::GetContentRegionAvail().x, h = std::max(3.0f, fs * 0.25f);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), col32(sem.field), h);
            dl->AddRectFilled(p, ImVec2(p.x + w * f, p.y + h), col32(sem.ok), h);
            ImGui::Dummy(ImVec2(w, h));
        }
        ui::EndCard();
        ImGui::SameLine(0.0f, gap);
        if (ui::BeginCard("##tile_batches", ImVec2(narrow, tileH))) {
            ImGui::TextDisabled("Batches");
            std::snprintf(buf, sizeof(buf), "%d", (int)r.batches.size());
            tileValue(buf, sem.text);
        }
        ui::EndCard();
        ImGui::SameLine(0.0f, gap);
        if (ui::BeginCard("##tile_solo", ImVec2(narrow, tileH))) {
            ImGui::TextDisabled("Drawn solo");
            std::snprintf(buf, sizeof(buf), "%d", solo);
            tileValue(buf, solo > 0 ? sem.warn : sem.text);
        }
        ui::EndCard();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Eligible objects that still draw alone - the Drawn solo list\n"
                              "says why for each one.");
        ImGui::SameLine(0.0f, gap);
        if (ui::BeginCard("##tile_saved", ImVec2(narrow, tileH))) {
            ImGui::TextDisabled("VU1 packages saved");
            std::snprintf(buf, sizeof(buf), saved > 0 ? "-%d" : "%d", saved);
            tileValue(buf, saved > 0 ? sem.ok : sem.text);
            ImGui::PushFont(nullptr, fs * 0.86f);
            ImGui::TextDisabled("~19.5 \xc2\xb5s each in one measured pose");
            ImGui::PopFont();
        }
        ui::EndCard();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Packages are what the EE pays for: about 19.5 us each in the Motor\n"
                "District's garage pose, 31.8 us in the night pose. An order of\n"
                "magnitude for judging a merge, not a constant.");
        ImGui::SameLine(0.0f, gap);
        if (ui::BeginCard("##tile_view", ImVec2(0.0f, tileH))) {
            ui::InspectorRows rows(scaled(40), scaled(400));
            ui::Switch("Colour batches in viewport", &showBatchOverlay_);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Each batch in its own colour, solo objects grey, and the merged\n"
                                  "box drawn heavy - the box the frustum culls as a unit.");
            ui::Switch("Show the grouping cell", &showBatchCells_);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Draws the selected batch's grouping cell. Two objects that look\n"
                                  "like they should have merged usually sit in different cells.");
        }
        ui::EndCard();
        ImGui::PopStyleVar();
    }
    for (const std::string& w : batchWarnings_) ui::StatusLine(1, w.c_str());
    if (r.anyModelUnbaked)
        ui::StatusLine(1, "Some models have no baked .tmdl yet - build for their batching to be known.");

    // --- list | detail --------------------------------------------------------
    const float paneH = ImGui::GetContentRegionAvail().y;
    const float totalW = ImGui::GetContentRegionAvail().x;
    if (batchListW_ <= 0.0f) batchListW_ = totalW * 0.62f;

    if (ui::BeginCard("##batch_list", ImVec2(batchListW_, paneH))) {
        char l0[48], l1[48];
        std::snprintf(l0, sizeof(l0), "Batches  %d###Batches", (int)r.batches.size());
        std::snprintf(l1, sizeof(l1), "Drawn solo  %d###Drawn solo", solo);
        const char* modes[2] = {l0, l1};
        ui::Segmented("##batchmode", modes, 2, &batchListMode_);
        ImGui::SameLine();
        const float fw = std::min(scaled(260), ImGui::GetContentRegionAvail().x);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - fw);
        ImGui::SetNextItemWidth(fw);
        ImGui::InputTextWithHint("##batchfilter", "Filter texture or object", batchFilter_,
                                 sizeof(batchFilter_));
        ImGui::Spacing();

        if (batchListMode_ == 0) {
            if (r.batches.empty()) {
                ui::EmptyState(ui::Glyph::Layers, "No batches in this scene",
                               "No two eligible objects share a texture, cell, draw distance, "
                               "lamp and strip run. Drawn solo says why per object.");
            } else if (ImGui::BeginTable("##batches", 6,
                                         ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                                             ImGuiTableFlags_BordersInnerH |
                                             ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, fs * 3.0f);
                ImGui::TableSetupColumn("Texture", ImGuiTableColumnFlags_WidthStretch, 3.0f);
                ImGui::TableSetupColumn("Members", ImGuiTableColumnFlags_WidthFixed, fs * 4.6f);
                ImGui::TableSetupColumn("Cell", ImGuiTableColumnFlags_WidthFixed, fs * 5.6f);
                ImGui::TableSetupColumn("Packages", ImGuiTableColumnFlags_WidthFixed, fs * 5.4f);
                ImGui::TableSetupColumn("Draw", ImGuiTableColumnFlags_WidthFixed, fs * 3.2f);
                ImGui::TableHeadersRow();
                int thumbBudget = 4;
                const float rowH = fs * 1.75f;
                for (size_t bi = 0; bi < r.batches.size(); ++bi) {
                    const staticbatch::Batch& b = r.batches[bi];
                    const std::string fname = b.texture.empty() ? std::string("(no texture)")
                                                                : fileNameOf(b.texture);
                    if (batchFilter_[0]) {
                        bool hit = containsNoCase(fname, batchFilter_);
                        for (const staticbatch::Member& m : b.members)
                            if (!hit && m.object >= 0 && m.object < (int)sc.objects.size())
                                hit = containsNoCase(sc.objects[m.object].name, batchFilter_);
                        if (!hit) continue;
                    }
                    ImGui::TableNextRow(0, rowH);
                    ImGui::PushID((int)bi);
                    ImGui::TableNextColumn();
                    const bool sel = batchSelected_ == (int)bi;
                    char rowLabel[32];
                    std::snprintf(rowLabel, sizeof(rowLabel), "Batch %d", (int)bi);
                    // The row is one Selectable spanning the columns with its
                    // own text transparent: "Batch N" is the ID scripts click.
                    const ImVec2 p0 = ImGui::GetCursorScreenPos();
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 0, 0, 0));
                    if (ImGui::Selectable(rowLabel, sel,
                                          ImGuiSelectableFlags_SpanAllColumns |
                                              ImGuiSelectableFlags_AllowOverlap,
                                          ImVec2(0, rowH)))
                        batchSelected_ = sel ? -1 : (int)bi;
                    ImGui::PopStyleColor();
                    uiscript::markLastItemChecked(sel);
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    float rgb[3];
                    batchColor((int)bi, rgb);
                    const float ty = p0.y + (rowH - fs) * 0.5f;
                    // The overlay colour (= the viewport's), then the index.
                    dl->AddRectFilled(ImVec2(p0.x, ty + fs * 0.3f),
                                      ImVec2(p0.x + fs * 0.42f, ty + fs * 0.72f),
                                      ImGui::GetColorU32(ImVec4(rgb[0], rgb[1], rgb[2], 1.0f)),
                                      2.0f);
                    char t[48];
                    std::snprintf(t, sizeof(t), "%d", (int)bi);
                    {
                        ui::FontScope mono(ui::monoFont());
                        dl->AddText(ImVec2(p0.x + fs * 0.85f, ty), col32(sem.textDim), t);
                    }

                    ImGui::TableNextColumn();
                    {
                        const ImVec2 p = ImGui::GetCursorScreenPos();
                        const float s = fs * 1.05f;
                        uint32_t tex = 0;
                        if (!b.texture.empty()) {
                            const std::string tp = texturePathFor(project_, b.texture);
                            tex = viewport_.assetThumb(tp, false);
                            if (!tex && thumbBudget > 0) {
                                tex = viewport_.assetThumb(tp, true);
                                --thumbBudget;
                            }
                        }
                        textureSwatch(dl, ImVec2(p.x, p0.y + (rowH - s) * 0.5f), s, tex);
                        dl->AddText(ImVec2(p.x + s + fs * 0.5f, ty),
                                    b.texture.empty() ? col32(sem.textDim) : col32(sem.text),
                                    fname.c_str());
                    }
                    auto rightText = [&](const char* txt, ImU32 c) {
                        const ImVec2 p = ImGui::GetCursorScreenPos();
                        const float w = ImGui::GetContentRegionAvail().x;
                        ui::FontScope mono(ui::monoFont());
                        dl->AddText(ImVec2(p.x + w - ImGui::CalcTextSize(txt).x, ty), c, txt);
                    };
                    ImGui::TableNextColumn();
                    std::snprintf(t, sizeof(t), "%d", (int)b.members.size());
                    rightText(t, col32(sem.text));
                    ImGui::TableNextColumn();
                    {
                        ui::FontScope mono(ui::monoFont());
                        std::snprintf(t, sizeof(t), "%.0f @ %d,%d", b.cellW, b.cellX, b.cellZ);
                        dl->AddText(ImVec2(ImGui::GetCursorScreenPos().x, ty), col32(sem.text), t);
                    }
                    ImGui::TableNextColumn();
                    {
                        const int d = b.soloPackages - b.packages;
                        char dt[16] = "";
                        if (d != 0) std::snprintf(dt, sizeof(dt), d > 0 ? " -%d" : " +%d", d > 0 ? d : -d);
                        ui::FontScope mono(ui::monoFont());
                        const ImVec2 p = ImGui::GetCursorScreenPos();
                        const float w = ImGui::GetContentRegionAvail().x;
                        std::snprintf(t, sizeof(t), "%d", b.packages);
                        const float tw = ImGui::CalcTextSize(t).x, dw = ImGui::CalcTextSize(dt).x;
                        dl->AddText(ImVec2(p.x + w - tw - dw, ty), col32(sem.text), t);
                        if (d != 0)
                            dl->AddText(ImVec2(p.x + w - dw, ty), col32(d > 0 ? sem.ok : sem.danger), dt);
                    }
                    ImGui::TableNextColumn();
                    if (b.drawDistance > 0.0f) std::snprintf(t, sizeof(t), "%.0f", b.drawDistance);
                    else std::snprintf(t, sizeof(t), "inf");
                    rightText(t, col32(sem.textDim));
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
        } else {
            drawBatchSoloList();
        }
    }
    ui::EndCard();
    ImGui::SameLine(0.0f, 0.0f);
    ui::VSplitter("##batch_split", &batchListW_, scaled(360), scaled(320), paneH);
    if (ui::BeginCard("##batch_detail", ImVec2(0.0f, paneH))) drawBatchDetail();
    ui::EndCard();
    ImGui::End();
}

// The right pane: one batch - what it costs, what is in it, and the solo
// neighbours in its cell that did not join it (with the fix where one exists).
void App::drawBatchDetail() {
    SceneData& sc = project_.active();
    const staticbatch::Result& r = batchResult_;
    const theme::Semantics& sem = theme::semantics();
    const float fs = ImGui::GetFontSize();
    if (batchSelected_ < 0 || batchSelected_ >= (int)r.batches.size()) {
        ui::EmptyState(ui::Glyph::Layers, "No batch selected",
                       "Pick a batch on the left: its cost, its members and the objects in its "
                       "cell that still draw solo show here.");
        return;
    }
    const staticbatch::Batch& b = r.batches[batchSelected_];
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Identity: swatch, "Batch 7 · metal.png", the full path, Frame.
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float s = fs * 2.6f;
        const uint32_t tex = b.texture.empty() ? 0u : viewport_.assetThumb(texturePathFor(project_, b.texture), true);
        textureSwatch(dl, p, s, tex);
        float rgb[3];
        batchColor(batchSelected_, rgb);
        dl->AddRect(ImVec2(p.x - 1, p.y - 1), ImVec2(p.x + s + 1, p.y + s + 1),
                    ImGui::GetColorU32(ImVec4(rgb[0], rgb[1], rgb[2], 0.9f)),
                    ImGui::GetStyle().FrameRounding * 0.6f, 0, 2.0f);
        char title[160];
        std::snprintf(title, sizeof(title), "Batch %d \xc2\xb7 %s", batchSelected_,
                      b.texture.empty() ? "no texture" : fileNameOf(b.texture).c_str());
        ImGui::SetCursorScreenPos(ImVec2(p.x + s + fs * 0.7f, p.y + fs * 0.15f));
        {
            ui::FontScope bold(ui::boldFont());
            ImGui::TextUnformatted(title);
        }
        ImGui::SetCursorScreenPos(ImVec2(p.x + s + fs * 0.7f, ImGui::GetCursorScreenPos().y));
        {
            ui::FontScope mono(ui::monoFont());
            ImGui::TextDisabled("%s", b.texture.empty() ? "untextured, or the file is missing"
                                                         : b.texture.c_str());
        }
        const float bw = ImGui::CalcTextSize("Frame").x + ImGui::GetStyle().FramePadding.x * 2.0f;
        ImGui::SetCursorScreenPos(
            ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x - bw, p.y));
        if (ImGui::Button("Frame")) {
            // Select the members and move the orbit pivot onto the merged box.
            clearSelection();
            for (const staticbatch::Member& m : b.members)
                if (m.object >= 0 && m.object < (int)sc.objects.size() && !isSelected(m.object))
                    toggleSelect(m.object);
            const float c[3] = {0.5f * (b.geomMin[0] + b.geomMax[0]),
                                0.5f * (b.geomMin[1] + b.geomMax[1]),
                                0.5f * (b.geomMin[2] + b.geomMax[2])};
            viewport_.setTarget(c);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Select the members and move the camera pivot onto the merged box.");
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + s + fs * 0.8f));
    }

    // Four numbers as small tiles, two by two.
    {
        const float gap = ImGui::GetStyle().ItemSpacing.x;
        const float w = (ImGui::GetContentRegionAvail().x - gap) * 0.5f;
        const float h = fs * 3.3f;
        auto mini = [&](const char* id, const char* cap, const char* val, const char* tail,
                        const ImVec4* tailCol, const char* tail2) {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, sem.field);
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, ImGui::GetStyle().FrameRounding + 1.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(fs * 0.7f, fs * 0.5f));
            ImGui::BeginChild(id, ImVec2(w, h), ImGuiChildFlags_AlwaysUseWindowPadding,
                              ImGuiWindowFlags_NoScrollbar);
            ImGui::TextDisabled("%s", cap);
            {
                ui::FontScope mono(ui::monoFont());
                ImGui::TextUnformatted(val);
                if (tail) {
                    ImGui::SameLine();
                    ImGui::TextColored(tailCol ? *tailCol : sem.textDim, "%s", tail);
                }
                if (tail2) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s", tail2);
                }
            }
            ImGui::EndChild();
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor();
        };
        char v[64], d[24], s2[32];
        const int delta = b.soloPackages - b.packages;
        std::snprintf(v, sizeof(v), "%d", b.packages);
        std::snprintf(d, sizeof(d), delta > 0 ? "-%d" : "+%d", delta > 0 ? delta : -delta);
        std::snprintf(s2, sizeof(s2), "vs %d solo", b.soloPackages);
        const ImVec4 dc = delta > 0 ? sem.ok : sem.danger;
        mini("##m_pk", "Packages", v, delta != 0 ? d : nullptr, &dc, s2);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%d VU1 packages merged, %d if these members drew solo.", b.packages,
                              b.soloPackages);
        ImGui::SameLine(0.0f, gap);
        std::snprintf(v, sizeof(v), "%.0f u @ %d,%d", b.cellW, b.cellX, b.cellZ);
        mini("##m_cell", "Cell", v, nullptr, nullptr, nullptr);
        std::snprintf(v, sizeof(v), "%.0f \xc3\x97 %.0f \xc3\x97 %.0f", b.geomMax[0] - b.geomMin[0],
                      b.geomMax[1] - b.geomMin[1], b.geomMax[2] - b.geomMin[2]);
        mini("##m_box", "Merged box", v, nullptr, nullptr, nullptr);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Over member geometry: the frustum and the draw distance test the\n"
                              "whole group with this box, so it decides whether every member draws.");
        ImGui::SameLine(0.0f, gap);
        if (b.drawDistance > 0.0f) std::snprintf(v, sizeof(v), "%.0f u", b.drawDistance);
        else std::snprintf(v, sizeof(v), "unlimited");
        mini("##m_dd", "Draw distance", v, nullptr, nullptr, nullptr);
        ImGui::TextDisabled("%s \xc2\xb7 lit by", b.stripRun > 0 ? "Triangle strip" : "Triangle list");
        ImGui::SameLine(0.0f, fs * 0.3f);
        if (b.lamp >= 0 && b.lamp < (int)sc.objects.size())
            ImGui::TextUnformatted(sc.objects[b.lamp].name.c_str());
        else
            ImGui::TextUnformatted("no dynamic light");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("A bag gets ONE dynamic light, picked from its merged bounding\n"
                              "sphere - which lamp reaches an object is part of the group key.");
    }

    ImGui::Spacing();
    ImGui::Spacing();
    char cnt[16];
    std::snprintf(cnt, sizeof(cnt), "%d", (int)b.members.size());
    {
        // "exclude to draw solo" right-aligned on the caption's line; the
        // caption's hairline stops short of it.
        const char* hint = "exclude to draw solo";
        const ImVec2 hs = ImGui::CalcTextSize(hint);
        const ImVec2 cp = ImGui::GetCursorScreenPos();
        const float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
        ui::Caption("Members", cnt);
        dl->AddRectFilled(ImVec2(right - hs.x - fs * 0.6f, cp.y), ImVec2(right, cp.y + fs),
                          col32(sem.surface));
        dl->AddText(ImVec2(right - hs.x, cp.y), col32(sem.textDim), hint);
    }
    const float switchW = ImGui::GetFrameHeight() * 1.8f;
    for (const staticbatch::Member& m : b.members) {
        if (m.object < 0 || m.object >= (int)sc.objects.size()) continue;
        const SceneObject& o = sc.objects[m.object];
        ImGui::PushID(m.object * 16 + m.part + 1);
        const ImVec2 rp = ImGui::GetCursorScreenPos();
        const float rowW = ImGui::GetContentRegionAvail().x;
        ImGui::SetNextItemAllowOverlap();
        if (ui::ListRow(o.name.c_str(), nullptr, isSelected(m.object), ui::Glyph::Box))
            selectOnly(m.object, false);
        const float rowH = ImGui::GetItemRectSize().y;
        if (m.part >= 0) {
            char pt[24];
            std::snprintf(pt, sizeof(pt), "part %d", m.part);
            ui::FontScope mono(ui::monoFont());
            const float tw = ImGui::CalcTextSize(pt).x;
            dl->AddText(ImVec2(rp.x + rowW - switchW - fs - tw, rp.y + (rowH - fs) * 0.5f),
                        col32(sem.textDim), pt);
        }
        ImGui::SetCursorScreenPos(
            ImVec2(rp.x + rowW - switchW, rp.y + (rowH - ImGui::GetFrameHeight()) * 0.5f));
        drawBatchExcludeCheckbox(m.object);
        ImGui::SetCursorScreenPos(ImVec2(rp.x, rp.y + rowH + 2.0f));
        ImGui::Dummy(ImVec2(0, 0));
        ImGui::PopID();
    }

    // The neighbours: shapes in this batch's cell that draw solo, so "why did
    // these two not merge" has an answer per object.
    ImGui::Spacing();
    ImGui::Spacing();
    ui::Caption("Same cell, drawn solo");
    int shown = 0;
    const float w = b.cellW;
    for (size_t oi = 0; oi < r.objects.size() && oi < sc.objects.size(); ++oi) {
        const staticbatch::ObjectVerdict& v = r.objects[oi];
        if (v.reason == staticbatch::Reason::Batched ||
            v.reason == staticbatch::Reason::NotABatchableShape || !v.hasGeom || w <= 0.0f)
            continue;
        const SceneObject& o = sc.objects[oi];
        const int cx = (int)std::floor((o.position[0] + 0.5f * r.mapW) / w);
        const int cz = (int)std::floor((o.position[2] + 0.5f * r.mapW) / w);
        if (cx != b.cellX || cz != b.cellZ) continue;
        ++shown;
        drawBatchSoloRow((int)oi, false);
        if (batchDirty_) break;  // a fix landed: the verdicts are stale
    }
    if (!shown) ImGui::TextDisabled("Nothing else in this cell draws solo.");
}

// One solo object: warn dot, name (click = select), "reason · stage", and on
// the right the fix button, "expected", or nothing.
void App::drawBatchSoloRow(int oi, bool compact) {
    SceneData& sc = project_.active();
    const staticbatch::Reason rr = batchResult_.objects[oi].reason;
    const theme::Semantics& sem = theme::semantics();
    const float fs = ImGui::GetFontSize();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGui::PushID(oi);
    const BatchFix fix = batchFixFor(rr);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float rowW = ImGui::GetContentRegionAvail().x;
    // Compact = inside a reason group, which already names the reason and
    // says "expected" once: one line per object.
    const float rowH = compact ? fs * 1.5f : fs * 2.5f;
    float fixW = 0.0f;
    if (fix.label) fixW = ImGui::CalcTextSize(fix.label).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    else if (fix.expected && !compact) fixW = ImGui::CalcTextSize("expected").x;
    ImGui::SetNextItemAllowOverlap();
    if (ImGui::InvisibleButton(sc.objects[oi].name.c_str(),
                               ImVec2(std::max(1.0f, rowW - fixW - fs), rowH)))
        selectOnly(oi, false);
    const bool hov = ImGui::IsItemHovered();
    if (hov) ImGui::SetTooltip("%s", staticbatch::reasonDetail(rr));
    const bool sel = isSelected(oi);
    if (sel || hov)
        dl->AddRectFilled(p, ImVec2(p.x + rowW, p.y + rowH),
                          col32(sel ? sem.accent : sem.surface, sel ? 0.14f : 0.8f),
                          ImGui::GetStyle().FrameRounding);
    const float nameY = compact ? p.y + (rowH - fs) * 0.5f : p.y + fs * 0.25f;
    dl->AddCircleFilled(ImVec2(p.x + fs * 0.55f, nameY + fs * 0.55f), fs * 0.22f, col32(sem.warn));
    dl->AddText(ImVec2(p.x + fs * 1.2f, nameY), col32(sem.text), sc.objects[oi].name.c_str());
    if (!compact) {
        char why[96];
        std::snprintf(why, sizeof(why), "%s \xc2\xb7 %s", staticbatch::reasonLabel(rr),
                      staticbatch::reasonStage(rr));
        dl->AddText(ImGui::GetFont(), fs * 0.88f, ImVec2(p.x + fs * 1.2f, p.y + fs * 1.3f),
                    col32(sem.warn, 0.85f), why);
    }
    if (fix.label) {
        ImGui::SetCursorScreenPos(
            ImVec2(p.x + rowW - fixW, p.y + (rowH - ImGui::GetFrameHeight()) * 0.5f));
        if (ImGui::Button(fix.label)) {
            applyBatchFix(sc.objects[oi], rr);
            commitChange();
            batchDirty_ = true;
        }
    } else if (fix.expected && !compact) {
        dl->AddText(ImVec2(p.x + rowW - fixW, p.y + (rowH - fs) * 0.5f), col32(sem.textDim),
                    "expected");
    }
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + rowH));
    ImGui::Dummy(ImVec2(0, 0));
    ImGui::PopID();
}

// The Drawn solo list: every solo shape grouped by its reason, with the fix
// per group where the reason is a flag the author can clear.
void App::drawBatchSoloList() {
    SceneData& sc = project_.active();
    const staticbatch::Result& r = batchResult_;
    const float fs = ImGui::GetFontSize();
    std::vector<std::vector<int>> byReason(64);
    int total = 0;
    for (size_t oi = 0; oi < r.objects.size() && oi < sc.objects.size(); ++oi) {
        const staticbatch::Reason rr = r.objects[oi].reason;
        // A marker that could never carry geometry is noise, not a finding:
        // the question is which SHAPES are missing out.
        if (rr == staticbatch::Reason::Batched || rr == staticbatch::Reason::NotABatchableShape)
            continue;
        if (batchFilter_[0] && !containsNoCase(sc.objects[oi].name, batchFilter_)) continue;
        const size_t k = (size_t)rr;
        if (k < byReason.size()) {
            byReason[k].push_back((int)oi);
            ++total;
        }
    }
    if (!total) {
        ui::EmptyState(ui::Glyph::Layers, "Nothing draws solo",
                       batchFilter_[0] ? "No solo object matches the filter."
                                       : "Every batchable shape is in a batch.");
        return;
    }
    ImGui::BeginChild("##sololist", ImVec2(0, 0));
    for (size_t k = 0; k < byReason.size(); ++k) {
        if (byReason[k].empty()) continue;
        const staticbatch::Reason rr = (staticbatch::Reason)k;
        ImGui::PushID((int)k);
        char cnt[16];
        std::snprintf(cnt, sizeof(cnt), "%d", (int)byReason[k].size());
        char cap[96];
        // ASCII only: Caption upper-cases byte by byte.
        std::snprintf(cap, sizeof(cap), "%s - %s", staticbatch::reasonLabel(rr),
                      staticbatch::reasonStage(rr));
        ui::Caption(cap, cnt);
        // What the group means, then the group's fix.
        const BatchFix fix = batchFixFor(rr);
        ImGui::PushTextWrapPos(0.0f);
        if (fix.expected)
            ImGui::TextDisabled("%s Expected - this is the object doing its job.",
                                staticbatch::reasonDetail(rr));
        else
            ImGui::TextDisabled("%s", staticbatch::reasonDetail(rr));
        ImGui::PopTextWrapPos();
        if (fix.label && byReason[k].size() > 1) {
            char all[96];
            std::snprintf(all, sizeof(all), "%s for all %d", fix.label, (int)byReason[k].size());
            if (ImGui::Button(all)) {
                for (int oi : byReason[k]) applyBatchFix(sc.objects[oi], rr);
                commitChange();  // one undo step for the whole group
                batchDirty_ = true;
            }
        } else if (rr == staticbatch::Reason::SingletonGroup) {
            if (ImGui::Button("Show the grouping cell##solo")) showBatchCells_ = showBatchOverlay_ = true;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Usually a near miss: a different cell, draw distance, lamp or\n"
                                  "strip run. Select a nearby batch to see its cell.");
        }
        for (int oi : byReason[k]) {
            drawBatchSoloRow(oi, true);
            if (batchDirty_) break;
        }
        ImGui::Dummy(ImVec2(0, fs * 0.4f));
        ImGui::PopID();
        if (batchDirty_) break;  // a fix landed: the verdicts are stale
    }
    ImGui::EndChild();
}

// The one per-object lever, "in batch" = NOT batchExclude, drawn the same way
// wherever it appears.
void App::drawBatchExcludeCheckbox(int objectIndex) {
    SceneData& sc = project_.active();
    if (objectIndex < 0 || objectIndex >= (int)sc.objects.size()) return;
    bool in = !sc.objects[objectIndex].batchExclude;
    // A switch commits on its RETURN VALUE - IsItemDeactivatedAfterEdit can
    // never be true for one (it activates and edits in the same frame), which
    // is a mistake this repo has shipped three times.
    if (ui::Switch("##inbatch", &in)) {
        sc.objects[objectIndex].batchExclude = !in;
        commitChange();
        batchDirty_ = true;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "Turn off to keep this object out of every static batch - when one\n"
            "outlying member widens the merged box enough to keep the whole\n"
            "group drawn past what the scene would cull.");
}
