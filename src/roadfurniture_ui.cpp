// -------------------------------------------------------------------------
// The Road's "Street furniture" Properties section (docs/roads.md "Street
// furniture"): lamp / tree / bollard lines along the pavement, signs at the
// stop lines and traffic lights at three- and four-way nodes. Everything it edits is
// SceneObject::roadFurniture (roadfurn::Settings); the placement itself is
// roadfurniture.cpp, the same function the codegen and the viewport call.
//
// App:: member declared in app.hpp, own translation unit (the roadtex_ui.cpp
// precedent). The caller commits the change like any other Properties edit.
// -------------------------------------------------------------------------
#include "app.hpp"
#include "app_internal.hpp"

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "roadfurniture.hpp"

#include <imgui.h>

namespace {

const char* const kSides[] = {"Both sides", "Left", "Right", "Alternate"};

}  // namespace

bool App::drawRoadFurniture(SceneObject& o) {
    bool changed = false;
    roadfurn::Settings& s = o.roadFurniture;
    const bool any = roadfurn::any({s});
    if (!ImGui::CollapsingHeader(any ? "Street furniture (on)###roadfurn" : "Street furniture###roadfurn"))
        return false;
    if (o.roadBridge)
        ImGui::TextDisabled("A bridge has parapets: its lines are skipped (signs still apply).");
    // A model picker: "" = the built-in model of that kind.
    auto modelCombo = [&](const char* label, std::string& path) {
        const std::string current =
            path.empty() ? "<built-in>" : std::filesystem::path(path).filename().string();
        bool hit = false;
        ImGui::SetNextItemWidth(propFieldWidth());
        if (ImGui::BeginCombo(label, current.c_str())) {
            if (ImGui::Selectable("<built-in>", path.empty()) && !path.empty()) {
                path.clear();
                hit = true;
            }
            for (const std::string& m : listAssetFiles("models", ".obj")) {
                const std::string rel = "res/models/" + m;
                if (ImGui::Selectable(m.c_str(), rel == path) && rel != path) {
                    path = rel;
                    hit = true;
                }
            }
            ImGui::EndCombo();
        }
        return hit;
    };
    auto lineControls = [&](const char* title, int kind, roadfurn::Line& l, float maxSpacing,
                            const char* help) {
        ImGui::PushID(title);
        ImGui::SeparatorText(title);
        ImGui::SetNextItemWidth(propFieldWidth());
        if (ImGui::SliderFloat("Spacing", &l.spacing, 0.0f, maxSpacing,
                               l.spacing > 0.0f ? "every %.1f units" : "off"))
            changed = true;
        prefHelp(help);
        if (l.spacing > 0.0f) {
            ImGui::SetNextItemWidth(propFieldWidth());
            if (ImGui::Combo("Side", &l.side, kSides, IM_ARRAYSIZE(kSides))) changed = true;
            prefHelp("Left and right are seen walking the road in the order its\n"
                     "points run. Alternate swaps sides station by station.");
            ImGui::SetNextItemWidth(propFieldWidth());
            if (ImGui::SliderFloat("Offset", &l.offset, 0.0f, 8.0f, "%.2f from the kerb"))
                changed = true;
            prefHelp("From the road edge (the kerb face) outward. On a 2.5-unit\n"
                     "pavement 0.6 is at the kerb and 1.6 mid-walk.");
            ImGui::SetNextItemWidth(propFieldWidth());
            if (ImGui::SliderFloat("Phase", &l.phase, 0.0f, l.spacing, "%.1f units")) changed = true;
            prefHelp("Shifts every station along the road. The first one stands\n"
                     "phase + half a spacing from the road's start.");
            if (modelCombo("Model", l.model)) changed = true;
            prefHelp("A project .obj, merged at build into vertex-colour chunks\n"
                     "(its texture is sampled into the vertex colours - no VRAM).\n"
                     "Its +Z faces the road; Yaw turns it. Built-in = a few dozen\n"
                     "triangles, 1 unit = 1 metre.");
            ImGui::SetNextItemWidth(propFieldWidth());
            if (ImGui::SliderFloat("Scale", &l.scale, 0.1f, 10.0f, "%.2f")) changed = true;
            if (kind != roadfurn::kTree) {
                ImGui::SetNextItemWidth(propFieldWidth());
                if (ImGui::SliderFloat("Yaw", &l.yaw, -180.0f, 180.0f, "%.0f deg")) changed = true;
            }
        }
        ImGui::PopID();
    };
    lineControls("Street lamps", roadfurn::kLamp, s.lamps, 60.0f,
                 "A lamp post this often along the pavement (or the road edge\n"
                 "without one), facing the road. Skipped wherever it would\n"
                 "stand on a road, a junction patch or its paint.");
    lineControls("Trees", roadfurn::kTree, s.trees, 60.0f,
                 "A tree this often, each turned and sized by the seed. Trees\n"
                 "keep 2 units clear of junction patches (sight lines).");
    lineControls("Bollards", roadfurn::kBollard, s.bollards, 30.0f,
                 "A bollard this often - a row of them keeps cars off a walk.");
    if (s.trees.spacing > 0.0f) {
        ImGui::SetNextItemWidth(propFieldWidth());
        if (ImGui::InputInt("Furniture seed", &s.seed)) changed = true;
        prefHelp("Another turn and size for every tree; positions stay.");
    }
    ImGui::SeparatorText("Junctions");
    const char* signKinds[] = {"None", "Give way", "Stop"};
    ImGui::SetNextItemWidth(propFieldWidth());
    if (ImGui::Combo("Signs", &s.signs, signKinds, IM_ARRAYSIZE(signKinds))) changed = true;
    prefHelp("A sign beside every stop line this road gives way at, facing\n"
             "the driver coming in. Needs Markings (the stop line).");
    if (s.signs != roadfurn::kSignNone && modelCombo("Sign model", s.signModel)) changed = true;
    if (ImGui::Checkbox("Traffic lights", &s.signals)) changed = true;
    prefHelp("A signal on every arm of this road's three- and four-way nodes,\n"
             "in place of the signs there. A junction's own Control (select\n"
             "its diamond) can turn them on or off at that node alone.");
    if (s.signals && modelCombo("Signal model", s.signalModel)) changed = true;
    // Breakable furniture (docs/roads.md "Breakable furniture", format v109).
    ImGui::SeparatorText("Breakable");
    if (ImGui::Checkbox("Cars knock it over", &s.breakable)) changed = true;
    prefHelp("Need for Speed style: a car that hits a lamp post, a sign,\n"
             "a bollard or a traffic light at least as fast as its kind's\n"
             "Break speed knocks it over - it snaps off and tumbles away,\n"
             "its light goes out, and the car keeps going a little slower.\n"
             "Slower bumps stop the car as before. Needs a vehicle in the\n"
             "project; the props stand again on a scene load.");
    if (s.breakable) {
        const char* kinds[roadfurn::kKindCount] = {"Lamps", "Trees", "Bollards", "Signs",
                                                    "Traffic lights"};
        for (int k = 0; k < roadfurn::kKindCount; ++k) {
            roadfurn::Break& b = s.brk[k];
            ImGui::PushID(k);
            if (ImGui::Checkbox(kinds[k], &b.on)) changed = true;
            if (k == roadfurn::kTree)
                prefHelp("Off by default: a tree is what stops a car.");
            if (b.on) {
                ImGui::Indent();
                ImGui::SetNextItemWidth(propFieldWidth());
                char fmtBuf[48];
                std::snprintf(fmtBuf, sizeof(fmtBuf), "%%.1f u/s (%.0f km/h)", b.speed * 3.6f);
                if (ImGui::SliderFloat("Break speed", &b.speed, 1.0f, 40.0f, fmtBuf)) changed = true;
                prefHelp("The car must be at least this fast (units per second;\n"
                         "1 unit = 1 metre) or the prop is a wall, as before.");
                ImGui::SetNextItemWidth(propFieldWidth());
                float pct = b.loss * 100.0f;
                if (ImGui::SliderFloat("Speed lost", &pct, 0.0f, 90.0f, "%.0f%%")) {
                    b.loss = pct / 100.0f;
                    changed = true;
                }
                prefHelp("The share of the car's speed the hit takes. Small for a\n"
                         "sign, more for a lamp post.");
                ImGui::Unindent();
            }
            ImGui::PopID();
        }
        const std::string current = s.breakSound.empty()
                                        ? "<auto>"
                                        : std::filesystem::path(s.breakSound).filename().string();
        ImGui::SetNextItemWidth(propFieldWidth());
        if (ImGui::BeginCombo("Hit sound", current.c_str())) {
            if (ImGui::Selectable("<auto>", s.breakSound.empty()) && !s.breakSound.empty()) {
                s.breakSound.clear();
                changed = true;
            }
            for (const std::string& snd : project_.sounds)
                if (ImGui::Selectable(snd.c_str(), snd == s.breakSound) && snd != s.breakSound) {
                    s.breakSound = snd;
                    changed = true;
                }
            ImGui::EndCombo();
        }
        prefHelp("A project sound (Project > Sounds). <auto> picks the first\n"
                 "one named like a crash (break, crash, impact, hit, smash,\n"
                 "clank, thud, knock) - none: the hit is silent, the dust and\n"
                 "sparks still fly.");
    }
    ImGui::TextDisabled("Baked at build: merged chunks (a few draws), solid poles and trunks,");
    ImGui::TextDisabled("hidden past ~%.0f units. Not scene objects.", roadfurn::kDrawDistance);
    return changed;
}
