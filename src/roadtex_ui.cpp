// -------------------------------------------------------------------------
// Tools > Road Texture Generator (docs/road-textures.md): bakes a road
// surface - asphalt, setts, gravel or dirt, with lane markings laid out
// across U - into res/materials/roads/<name>.png + .mtl, plus the editor-only
// <name>.roadtex recipe that lets the window re-open it. The pixels come from
// roadtex.cpp, the same function the --road-texture CLI and project::create
// call, so the preview is the file.
//
// App:: members declared in app.hpp, own translation unit (the particle_ui.cpp
// precedent). Applying a material to the selected Road goes through
// commitChange() like any other Properties edit.
// -------------------------------------------------------------------------
#include "app.hpp"
#include "app_internal.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "gl_loader.h"
#include "roadtex.hpp"

#include <imgui.h>

namespace fs = std::filesystem;

namespace {

const char* const kSurfaces[] = {"Asphalt", "Cobble / setts", "Gravel", "Dirt / mud",
                                 "Paving slabs", "Brick pavers"};
const char* const kStyles[] = {"None",         "Dashed",        "Solid",
                               "Double solid", "Solid | dashed", "Dashed | solid"};

// One line type's controls: style, colour (+ white/yellow presets), width,
// and dash/gap when the style has dashes. Indented under its own label.
void lineControls(const char* label, roadtex::LinePaint& l, float w) {
    ImGui::PushID(label);
    ImGui::SetNextItemWidth(w);
    ImGui::Combo(label, &l.style, kStyles, roadtex::kLineStyleCount);
    if (l.style != roadtex::kLineNone) {
        ImGui::Indent();
        ImGui::ColorEdit3("##col", l.colour, ImGuiColorEditFlags_NoInputs);
        ImGui::SameLine();
        if (ImGui::SmallButton("White"))
            for (int k = 0; k < 3; ++k) l.colour[k] = roadtex::kWhite[k];
        ImGui::SameLine();
        if (ImGui::SmallButton("Yellow"))
            for (int k = 0; k < 3; ++k) l.colour[k] = roadtex::kYellow[k];
        ImGui::SetNextItemWidth(w);
        ImGui::SliderFloat("Width", &l.width, 0.04f, 0.5f, "%.2f u");
        prefHelp("One line's width, world units (a double = two).");
        if (l.style == roadtex::kLineDashed || l.style >= roadtex::kLineSolidDashed) {
            ImGui::SetNextItemWidth(w * 0.5f - ImGui::GetStyle().ItemInnerSpacing.x);
            ImGui::DragFloat("##dash", &l.dash, 0.02f, 0.05f, 4.0f, "dash %.2f");
            ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
            ImGui::SetNextItemWidth(w * 0.5f - ImGui::GetStyle().ItemInnerSpacing.x);
            ImGui::DragFloat("##gap", &l.gap, 0.02f, 0.05f, 4.0f, "gap %.2f");
            float d = 0.0f, g = 0.0f;
            const int n = roadtex::quantizeDash(l, &d, &g);
            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Units along the road. Snapped so dash + gap\n"
                                  "divides the 4-unit texture repeat:\n"
                                  "drawn as %d x (%.2f + %.2f).",
                                  n, d, g);
        }
        ImGui::Unindent();
    }
    ImGui::PopID();
}

// The generated recipes already in the project, by name.
std::vector<std::string> recipesIn(const std::string& projectDir) {
    std::vector<std::string> out;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(fs::path(projectDir) / roadtex::kDir, ec))
        if (e.path().extension() == ".roadtex") out.push_back(e.path().stem().string());
    std::sort(out.begin(), out.end());
    return out;
}

}  // namespace

void App::openRoadTextureGenerator(const std::string& mtlRel) {
    showRoadTexGen_ = true;
    pendingFocusWindow_ = "Road Texture Generator";
    const fs::path rel(mtlRel);
    if (rel.parent_path().generic_string() != roadtex::kDir) return;
    roadtex::RoadTexParams p;
    if (!roadtex::readRecipe(project_.dir, rel.stem().string(), &p)) return;
    roadTexParams_ = p;
    roadTexName_ = rel.stem().string();
}

void App::drawRoadTextureWindow() {
    if (!showRoadTexGen_) return;
    ImGui::SetNextWindowSize(ImVec2(scaled(640), scaled(520)), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Road Texture Generator", &showRoadTexGen_)) {
        ImGui::End();
        return;
    }
    if (!hasProject_) {
        ImGui::TextDisabled("Open a project first.");
        ImGui::End();
        return;
    }
    roadtex::RoadTexParams& p = roadTexParams_;

    // --- controls (left) -----------------------------------------------------
    ImGui::BeginChild("##roadtexctl", ImVec2(scaled(330), 0));
    ImGui::SetNextItemWidth(scaled(170));
    if (ImGui::BeginCombo("Load", nullptr, ImGuiComboFlags_NoPreview)) {
        for (const std::string& n : recipesIn(project_.dir))
            if (ImGui::Selectable(n.c_str())) {
                roadtex::readRecipe(project_.dir, n, &p);
                roadTexName_ = n;
            }
        ImGui::EndCombo();
    }
    prefHelp("Re-open a texture made here (its .roadtex recipe).");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(scaled(90));
    if (ImGui::BeginCombo("##preset", "Preset", ImGuiComboFlags_None)) {
        for (const roadtex::Preset& pr : roadtex::presets())
            if (ImGui::Selectable(pr.name)) {
                p = pr.params;
                roadTexName_ = pr.name;
            }
        ImGui::EndCombo();
    }
    ImGui::SetNextItemWidth(scaled(170));
    {
        char nameBuf[96];
        std::snprintf(nameBuf, sizeof nameBuf, "%s", roadTexName_.c_str());
        if (ImGui::InputText("Name", nameBuf, sizeof nameBuf)) roadTexName_ = nameBuf;
    }
    prefHelp("File name in res/materials/roads (.png + .mtl).");

    ImGui::SeparatorText("Surface");
    ImGui::SetNextItemWidth(scaled(170));
    ImGui::Combo("Surface", &p.surface, kSurfaces, roadtex::kSurfaceCount);
    if (ImGui::Checkbox("Intersection patch", &p.intersection) && p.intersection)
        p.pavement = false;
    prefHelp("For Intersection material: no markings, tiles both ways\n"
             "(mapped one repeat per 32 units).");
    if (ImGui::Checkbox("Pavement", &p.pavement) && p.pavement) p.intersection = false;
    prefHelp("For a pavement: no markings, tiles both ways\n"
             "(one repeat per 2 x 2 units).");
    if (p.surface == roadtex::kSlabs || p.surface == roadtex::kPavers) {
        int cols = 1, rows = 1;
        roadtex::slabGrid(p, &cols, &rows);
        ImGui::SetNextItemWidth(scaled(170));
        ImGui::SliderFloat("Slab size", &p.slabSize, 0.1f, 2.0f, "%.2f u");
        prefHelp("Slab edge, units. Pavers: half x quarter of it.\n"
                 "Snapped to a whole number per tile.");
        ImGui::SameLine();
        ImGui::TextDisabled("%d x %d", cols, rows);
        ImGui::SetNextItemWidth(scaled(170));
        ImGui::SliderFloat("Joint width", &p.jointWidth, 0.0f, 0.1f, "%.3f u");
    }
    ImGui::SetNextItemWidth(scaled(170));
    ImGui::SliderFloat("Wear", &p.wear, 0.0f, 1.0f, "%.2f");
    prefHelp("Stains, patches, wheel tracks; chipped, faded paint.");
    ImGui::SetNextItemWidth(scaled(170));
    ImGui::SliderFloat("Grime", &p.grime, 0.0f, 1.0f, "%.2f");
    prefHelp("Rubber strip down each lane, dust, dark gutters.");
    ImGui::SetNextItemWidth(scaled(170));
    ImGui::SliderFloat("Cracks", &p.cracks, 0.0f, 1.0f, "%.2f");
    prefHelp("Crack lines and tar-sealed seams. All three at 0 = clean.");
    ImGui::SetNextItemWidth(scaled(170));
    ImGui::ColorEdit3("Tint", p.tint, ImGuiColorEditFlags_NoInputs);
    ImGui::SetNextItemWidth(scaled(170));
    ImGui::DragInt("Seed", &p.seed, 1.0f, 0, 99999);
    {
        int sizeIdx = p.size == 64 ? 0 : (p.size == 256 ? 2 : 1);
        const char* sizes[] = {"64 x 64", "128 x 128", "256 x 256"};
        ImGui::SetNextItemWidth(scaled(170));
        if (ImGui::Combo("Resolution", &sizeIdx, sizes, 3))
            p.size = sizeIdx == 0 ? 64 : (sizeIdx == 2 ? 256 : 128);
        prefHelp("4-bit GS VRAM: 2 / 8 / 32 KB.");
    }
    if (p.surface == roadtex::kDirt && !p.isotropic()) {
        ImGui::Checkbox("Ragged edges", &p.raggedEdges);
        prefHelp("Notches the sides (alpha 0 there only) - pairs with\n"
                 "the road's Edge fade.");
    }

    if (!p.isotropic()) {
        ImGui::SeparatorText("Markings");
        ImGui::SetNextItemWidth(scaled(170));
        ImGui::SliderInt("Lanes", &p.lanes, 0, 6);
        prefHelp("0 = no lane lines.");
        ImGui::SetNextItemWidth(scaled(170));
        ImGui::SliderFloat("Design width", &p.width, 0.0f, 24.0f,
                           p.width > 0.5f ? "%.1f u" : "auto");
        prefHelp("The road width the lines are placed for - match the\n"
                 "road's Width. Auto = lanes x 3 + 1.5.");
        const float w = scaled(150);
        ImGui::BeginDisabled(p.lanes < 2);
        lineControls("Centre line", p.centre, w);
        ImGui::EndDisabled();
        ImGui::BeginDisabled(p.lanes < 3);
        lineControls("Lane dividers", p.divider, w);
        ImGui::EndDisabled();
        lineControls("Edge lines", p.edge, w);
    }

    // --- write / apply ---------------------------------------------------------
    ImGui::Separator();
    auto writeNow = [&]() -> std::string {
        std::string err;
        const std::string mtl = roadtex::writeAssets(project_.dir, roadTexName_, p, &err);
        if (mtl.empty()) {
            roadTexStatus_ = "Not written: " + err;
            return "";
        }
        roadTexName_ = roadtex::fileStem(roadTexName_);
        viewport_.invalidateAssets();  // the viewport caches textures by path
        roadTexStatus_ = "Wrote " + mtl;
        return mtl;
    };
    if (ImGui::Button("Save")) writeNow();
    SceneObject* road = nullptr;
    if (selectedObject_ >= 0 && selectedObject_ < (int)project_.objects().size() &&
        project_.objects()[(size_t)selectedObject_].type == PrimitiveType::Road)
        road = &project_.objects()[(size_t)selectedObject_];
    ImGui::BeginDisabled(!road);
    if (ImGui::Button(p.pavement       ? "Apply as pavement material"
                      : p.intersection ? "Apply as intersection material"
                                       : "Apply to selected road") &&
        road) {
        const std::string mtl = writeNow();
        if (!mtl.empty()) {
            if (p.pavement) {
                road->roadPavementMaterial = mtl;
                // A pavement needs its kerb, and some width to show.
                road->roadKerb = true;
                if (road->roadPavement <= 0.0f) road->roadPavement = 2.5f;
            } else {
                (p.intersection ? road->roadIntersectionTexture : road->roadTexture) = mtl;
            }
            commitChange();
        }
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !road)
        ImGui::SetTooltip("Select a Road first.");
    if (!roadTexStatus_.empty()) ImGui::TextDisabled("%s", roadTexStatus_.c_str());
    ImGui::EndChild();
    ImGui::SameLine();

    // --- preview (right) -----------------------------------------------------
    if (!roadTexGl_) glGenTextures(1, &roadTexGl_);
    if (!roadTexGlValid_ || roadTexGlFor_ != p) {
        const std::vector<unsigned char> px = roadtex::generate(p);
        const int n = p.size == 64 || p.size == 256 ? p.size : 128;
        glBindTexture(GL_TEXTURE_2D, roadTexGl_);
        glUploadTexRgba(n, n, px.data());
        roadTexGlFor_ = p;
        roadTexGlValid_ = true;
    }
    ImGui::BeginChild("##roadtexpreview", ImVec2(0, 0));
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImTextureID tex = (ImTextureID)(intptr_t)roadTexGl_;
    // a grass-ish ground so ragged edges and the road's sides read
    dl->AddRectFilled(p0, ImVec2(p0.x + avail.x, p0.y + avail.y), IM_COL32(52, 74, 40, 255), 4.0f);
    if (p.isotropic()) {
        // 2 x 2 repeats: the tiling in both directions is what matters here
        const float s = std::min(avail.x, avail.y) - scaled(16);
        const ImVec2 a(p0.x + (avail.x - s) * 0.5f, p0.y + (avail.y - s) * 0.5f);
        dl->AddImage(tex, a, ImVec2(a.x + s, a.y + s), ImVec2(0, 0), ImVec2(2, 2));
    } else {
        // a strip of road at its design proportions: one repeat = 4 units
        const float W = roadtex::designWidth(p);
        const float w = std::min(avail.x - scaled(32), scaled(220));
        const float x0 = p0.x + (avail.x - w) * 0.5f;
        const float reps = (avail.y - scaled(8)) / (w * 4.0f / W);
        dl->AddImage(tex, ImVec2(x0, p0.y + scaled(4)), ImVec2(x0 + w, p0.y + avail.y - scaled(4)),
                     ImVec2(0, 0), ImVec2(1, reps));
    }
    ImGui::Dummy(avail);
    ImGui::EndChild();
    ImGui::End();
}
