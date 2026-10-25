// -------------------------------------------------------------------------
// The Road's Furniture tab in Properties (docs/roads.md "Street furniture",
// "Properties"): a plan of the first metres with every station on it, one
// switchable card per line (street lamps, trees, bollards), the junction signs
// and signals, and the Breakable card. Everything it edits is
// SceneObject::roadFurniture (roadfurn::Settings); the placement itself is
// roadfurniture.cpp, the same function the codegen and the viewport call.
//
// App:: member declared in app.hpp, own translation unit (the roadtex_ui.cpp
// precedent). The caller commits the change like any other Properties edit.
// -------------------------------------------------------------------------
#include "app.hpp"
#include "app_internal.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "roadfurniture.hpp"
#include "roadgen.hpp"
#include "roadrail.hpp"
#include "theme.hpp"
#include "toolkit.hpp"
#include "widgets.hpp"

#include <imgui.h>
#include <imgui_internal.h>

namespace {

ImU32 u32(ImVec4 c, float a = 1.0f) {
    c.w *= a;
    return ImGui::GetColorU32(c);
}
ImVec4 mixc(ImVec4 a, ImVec4 b, float t) {
    return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
}

// What the Side choice shows, left to right, and the roadfurn::Side each is.
const char* const kSideText[] = {"Left", "Both", "Right", "Alternate"};
const int kSideValue[] = {roadfurn::kLeft, roadfurn::kBoth, roadfurn::kRight, roadfurn::kAlternate};
const char* sideWord(int side) {
    switch (side) {
    case roadfurn::kLeft: return "left side";
    case roadfurn::kRight: return "right side";
    case roadfurn::kAlternate: return "alternating sides";
    default: return "both sides";
    }
}

struct LineInfo {
    const char* title;     // card title and switch name
    const char* addLabel;  // the dashed row while it is off
    const char* addHint;
    const char* noun;      // "posts", "trees", "bollards"
    int kind;
    float defaultSpacingM;
    float maxSpacing;
    ui::Glyph glyph;
    const char* help;
};

float roadLen(const std::vector<float>& pts) {
    const int n = (int)(pts.size() / 2);
    if (n < 2) return 0.0f;
    const int steps = std::max(16, (n - 1) * 24);
    float len = 0.0f, px = 0.0f, pz = 0.0f;
    for (int i = 0; i <= steps; ++i) {
        float x, z;
        roadgen::splineAt(pts, (float)i / (float)steps, &x, &z);
        if (i) len += std::sqrt((x - px) * (x - px) + (z - pz) * (z - pz));
        px = x;
        pz = z;
    }
    return len;
}

// Stations along `len` units: phase + half a spacing, then every spacing.
int stationsOn(const roadfurn::Line& l, float len) {
    if (l.spacing <= 0.0f) return 0;
    const float first = l.phase + l.spacing * 0.5f;
    if (first > len) return 0;
    const int along = 1 + (int)std::floor((len - first) / l.spacing);
    return l.side == roadfurn::kBoth ? along * 2 : along;
}

}  // namespace

bool App::drawRoadFurniture(SceneObject& o) {
    bool changed = false;
    roadfurn::Settings& s = o.roadFurniture;
    const theme::Semantics& sem = theme::semantics();
    const float upm = project_.settings.unitsPerMeter > 0.0f ? project_.settings.unitsPerMeter : 1.0f;
    const float fs = ImGui::GetFontSize();
    const float gap = IM_TRUNC(ImGui::GetStyle().ItemSpacing.x);
    const float len = roadLen(o.roadPoints);
    const ImU32 cLamp = u32(sem.warn), cTree = u32(sem.ok, 0.9f), cBollard = u32(mixc(sem.textDim, sem.text, 0.4f));

    // --- Plan: the first 60 m from above -----------------------------------
    ui::BeginInsetCard("##furnplan");
    {
        const float spanM = 60.0f;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        {
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float W = ImGui::GetContentRegionAvail().x;
            ui::FontScope bold(ui::boldFont());
            const float cfs = IM_TRUNC(fs * 0.80f);
            dl->AddText(ImGui::GetFont(), cfs, p, u32(sem.textDim), "FROM ABOVE \xc2\xb7 FIRST 60 M");
            // Legend at the right end.
            float x = p.x + W;
            const char* names[3] = {"bollard", "tree", "lamp"};
            const ImU32 cols[3] = {cBollard, cTree, cLamp};
            for (int i = 0; i < 3; ++i) {
                const ImVec2 ts = ImGui::CalcTextSize(names[i]);
                x -= ts.x;
                dl->AddText(ImVec2(x, p.y), u32(sem.textDim), names[i]);
                x -= fs * 0.55f;
                dl->AddCircleFilled(ImVec2(x, p.y + ts.y * 0.5f), fs * 0.22f, cols[i], 10);
                x -= fs * 0.8f;
            }
            ImGui::Dummy(ImVec2(W, ImGui::GetTextLineHeight()));
        }
        const float W = ImGui::GetContentRegionAvail().x, H = IM_TRUNC(fs * 5.2f);
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(W, H));
        const float sx = W / (spanM * upm);  // px per unit along the road
        const float walk = IM_TRUNC(H * 0.2f);
        const bool pav = o.roadKerb && o.roadPavement > 0.0f;
        const ImU32 asphalt = u32(mixc(sem.field, sem.text, 0.13f)), pavC = u32(mixc(sem.field, sem.text, 0.28f));
        const float yTop = p0.y + walk, yBot = p0.y + H - walk;
        dl->AddRectFilled(ImVec2(p0.x, p0.y), ImVec2(p0.x + W, p0.y + H), u32(mixc(sem.field, sem.ok, 0.08f)));
        if (pav) {
            dl->AddRectFilled(ImVec2(p0.x, p0.y), ImVec2(p0.x + W, yTop), pavC);
            dl->AddRectFilled(ImVec2(p0.x, yBot), ImVec2(p0.x + W, p0.y + H), pavC);
        }
        if (o.roadKind == roadrail::kRail) {
            // A ballast bed with its rails (one or two tracks).
            dl->AddRectFilled(ImVec2(p0.x, yTop), ImVec2(p0.x + W, yBot), u32(mixc(sem.field, sem.text, 0.22f)));
            const int tracks = std::clamp(o.roadTracks, 1, 2);
            const float mid = (yTop + yBot) * 0.5f, band = yBot - yTop;
            for (int t = 0; t < tracks; ++t) {
                const float tc = tracks == 2 ? mid + (t == 0 ? -0.25f : 0.25f) * band : mid;
                for (int r = -1; r <= 1; r += 2)
                    dl->AddLine(ImVec2(p0.x, tc + (float)r * band * 0.07f), ImVec2(p0.x + W, tc + (float)r * band * 0.07f),
                                u32(mixc(sem.field, sem.text, 0.7f)), 1.5f);
            }
        } else {
            dl->AddRectFilled(ImVec2(p0.x, yTop), ImVec2(p0.x + W, yBot), asphalt);
            for (float x = p0.x + fs * 0.3f; x < p0.x + W; x += fs * 1.4f)
                dl->AddLine(ImVec2(x, (yTop + yBot) * 0.5f),
                            ImVec2(std::min(x + fs * 0.8f, p0.x + W), (yTop + yBot) * 0.5f),
                            u32(mixc(sem.field, sem.text, 0.7f)), 1.2f);
        }
        // A stop line and the sign, when this road gives way somewhere and paints it.
        if (s.signs != roadfurn::kSignNone && o.roadMarkings > 0) {
            const float xs = p0.x + W - fs * 0.8f;
            dl->AddLine(ImVec2(xs, yTop + 2.0f), ImVec2(xs, (yTop + yBot) * 0.5f), u32(sem.text), 3.0f);
            dl->AddRectFilled(ImVec2(xs - fs * 0.25f, yTop - fs * 0.55f), ImVec2(xs + fs * 0.25f, yTop - fs * 0.05f),
                              u32(sem.danger), 2.0f);
        }
        auto stations = [&](const roadfurn::Line& l, ImU32 col, float r, bool square, float* firstTwo) {
            if (l.spacing <= 0.0f) return;
            int k = 0;
            for (float d = l.phase + l.spacing * 0.5f; d * sx < W; d += l.spacing, ++k) {
                const float x = p0.x + d * sx;
                const bool left = l.side == roadfurn::kLeft || l.side == roadfurn::kBoth ||
                                  (l.side == roadfurn::kAlternate && k % 2 == 0);
                const bool right = l.side == roadfurn::kRight || l.side == roadfurn::kBoth ||
                                   (l.side == roadfurn::kAlternate && k % 2 == 1);
                const float off = std::min(l.offset * sx, walk * 0.8f);
                for (int side = 0; side < 2; ++side) {
                    if (side == 0 ? !left : !right) continue;
                    const float y = side == 0 ? yTop - off : yBot + off;
                    if (square) dl->AddRectFilled(ImVec2(x - r, y - r), ImVec2(x + r, y + r), col, 1.0f);
                    else dl->AddCircleFilled(ImVec2(x, y), r, col, 12);
                }
                if (firstTwo && k < 2) firstTwo[k] = x;
            }
        };
        float lampX[2] = {-1.0f, -1.0f};
        stations(s.trees, cTree, fs * 0.42f, false, nullptr);
        stations(s.bollards, cBollard, fs * 0.13f, true, nullptr);
        stations(s.lamps, cLamp, fs * 0.24f, false, lampX);
        if (lampX[1] > 0.0f) {
            char t[24];
            std::snprintf(t, sizeof t, "%.0f m", s.lamps.spacing / upm);
            const float y = yTop + fs * 0.75f;
            ui::DimensionLine(dl, ImVec2(lampX[0], y), ImVec2(lampX[1], y), t, u32(sem.accent));
        }
        if (o.roadBridge)
            ui::Note(1, "A bridge has parapets: these lines are skipped on it (signs still apply).");
    }
    ui::EndInsetCard();
    ImGui::Spacing();

    // --- The three lines ------------------------------------------------------
    static const LineInfo kLines[3] = {
        {"Street lamps", "Add street lamps", "light the street at night", "posts", roadfurn::kLamp, 15.0f, 60.0f,
         ui::Glyph::Bulb,
         "A lamp post this often along the pavement (or the road edge\n"
         "without one), facing the road. Skipped wherever it would\n"
         "stand on a road, a junction patch or its paint."},
        {"Trees", "Add trees", "shade the pavement", "trees", roadfurn::kTree, 12.0f, 60.0f, ui::Glyph::Sparkle,
         "A tree this often, each turned and sized by the seed. Trees\n"
         "keep 2 units clear of junction patches (sight lines)."},
        {"Bollards", "Add bollards", "keep cars off a walk", "bollards", roadfurn::kBollard, 3.0f, 30.0f,
         ui::Glyph::Box, "A bollard this often - a row of them keeps cars off a walk."},
    };
    const ImU32 dots[3] = {cLamp, cTree, cBollard};
    int props = 0;
    for (int li = 0; li < 3; ++li) {
        const LineInfo& info = kLines[li];
        roadfurn::Line& l = s.line(info.kind);
        props += stationsOn(l, len);
        ImGui::PushID(info.title);
        const std::string memoKey = o.id + "/furn/" + info.title;
        if (l.spacing <= 0.0f) {
            if (ui::AddRow(info.addLabel, info.addHint)) {
                auto it = roadOffMemo_.find(memoKey);
                l.spacing = it != roadOffMemo_.end() && it->second > 0.0f ? it->second : info.defaultSpacingM * upm;
                ImGui::GetStateStorage()->SetBool(ImGui::GetID("open"), true);
                changed = true;
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", info.help);
            ImGui::PopID();
            ImGui::Spacing();
            continue;
        }
        ui::BeginInsetCard("##furnline");
        bool on = true;
        ImGuiStorage* st = ImGui::GetStateStorage();
        bool open = st->GetBool(ImGui::GetID("open"), li == 0);
        char sub[128];
        const int n = stationsOn(l, len);
        const std::string model = l.model.empty() ? "built-in" : std::filesystem::path(l.model).filename().string();
        if (open)
            std::snprintf(sub, sizeof sub, "~%d %s \xc2\xb7 %s", n, info.noun, info.addHint);
        else
            std::snprintf(sub, sizeof sub, "every %.0f m \xc2\xb7 %s \xc2\xb7 %s", l.spacing / upm, sideWord(l.side),
                          model.c_str());
        if (ui::CardHeader(info.title, sub, &on, &open, dots[li])) {
            roadOffMemo_[memoKey] = l.spacing;
            l.spacing = 0.0f;
            changed = true;
        }
        st->SetBool(ImGui::GetID("open"), open);
        if (open && l.spacing > 0.0f) {
            ImGui::Separator();
            // Model tile | side + the two distances.
            const float tile = IM_TRUNC(fs * 4.2f);
            uint32_t tex = 0;
            if (!l.model.empty()) {
                tex = viewport_.assetThumb(l.model, false);
                if (!tex) tex = viewport_.assetThumb(l.model, true);
            }
            if (ui::ThumbTileEx("Model", (ImTextureID)(intptr_t)tex, true, info.glyph, false, tile, model.c_str(),
                                nullptr))
                ImGui::OpenPopup("##furnmodel");
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                ImGui::SetTooltip("A project .obj, merged at build into vertex-colour chunks\n"
                                  "(its texture is sampled into the vertex colours - no VRAM).\n"
                                  "Its +Z faces the road; Yaw turns it. Built-in = a few dozen\n"
                                  "triangles, 1 unit = 1 metre.");
            if (ImGui::BeginPopup("##furnmodel")) {
                if (ImGui::Selectable("<built-in>", l.model.empty()) && !l.model.empty()) {
                    l.model.clear();
                    changed = true;
                }
                for (const std::string& m : listAssetFiles("models", ".obj")) {
                    const std::string rel = "res/models/" + m;
                    if (ImGui::Selectable(m.c_str(), rel == l.model) && rel != l.model) {
                        l.model = rel;
                        changed = true;
                    }
                }
                ImGui::EndPopup();
            }
            ImGui::SameLine(0.0f, gap);
            ImGui::BeginGroup();
            {
                int si = 1;
                for (int i = 0; i < 4; ++i)
                    if (kSideValue[i] == l.side) si = i;
                if (ui::Choice("Side", kSideText, 4, &si, -1.0f)) {
                    l.side = kSideValue[si];
                    changed = true;
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                    ImGui::SetTooltip("Left and right are seen walking the road in the order its\n"
                                      "points run. Alternate swaps sides station by station.");
                float spM = l.spacing / upm, offM = l.offset / upm;
                ui::KitDrag f[2];
                f[0].v = &spM;
                f[0].speed = 0.1f;
                f[0].vmin = 0.5f / upm;
                f[0].vmax = info.maxSpacing / upm;
                f[0].fmt = "every %.1f m";
                f[0].name = "Spacing";
                f[1].v = &offM;
                f[1].speed = 0.02f;
                f[1].vmin = 0.0f;
                f[1].vmax = 8.0f / upm;
                f[1].fmt = "kerb + %.2f m";
                f[1].name = "Offset";
                if (ui::DragRow("##furnfields", f, 2)) {
                    l.spacing = std::clamp(spM * upm, 0.5f, info.maxSpacing);
                    l.offset = std::clamp(offM * upm, 0.0f, 8.0f);
                }
                changed |= ImGui::IsItemDeactivatedAfterEdit();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                    ImGui::SetTooltip("%s\n\nOFFSET - from the road edge (the kerb face) outward. On a\n"
                                      "2.5 m pavement 0.6 is at the kerb and 1.6 mid-walk.",
                                      info.help);
            }
            ImGui::EndGroup();
            // The rest behind "Adjust...": the summary says what they are.
            const ImGuiID adjId = ImGui::GetID("adjust");
            bool adjust = st->GetBool(adjId, false);
            char rest[128];
            if (info.kind == roadfurn::kTree)
                std::snprintf(rest, sizeof rest, "Start %.1f m \xc2\xb7 scale %.2f \xc2\xb7 seed %d", l.phase / upm, l.scale,
                              s.seed);
            else
                std::snprintf(rest, sizeof rest, "Start %.1f m \xc2\xb7 scale %.2f \xc2\xb7 %s", l.phase / upm, l.scale,
                              std::fabs(l.yaw) < 0.5f ? "faces the road" : "turned");
            ImGui::TextDisabled("%s", rest);
            // Right-aligned after the summary (measured from where the line
            // ended, not from the cursor - that is already on the next line).
            const float lw = ImGui::CalcTextSize(adjust ? "Done" : "Adjust...").x;
            ImGui::SameLine();
            const float room = ImGui::GetContentRegionAvail().x;
            if (room > lw) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + room - lw);
            if (ui::Link(adjust ? "Done##adj" : "Adjust...")) st->SetBool(adjId, !adjust);
            if (adjust) {
                float phM = l.phase / upm;
                if (ui::SliderFloat("Phase", &phM, 0.0f, l.spacing / upm, "%.1f m")) l.phase = phM * upm;
                changed |= ImGui::IsItemDeactivatedAfterEdit();
                prefHelp("Shifts every station along the road. The first one stands\n"
                         "phase + half a spacing from the road's start.");
                ui::SliderFloat("Scale", &l.scale, 0.1f, 10.0f, "%.2f");
                changed |= ImGui::IsItemDeactivatedAfterEdit();
                if (info.kind != roadfurn::kTree) {
                    ui::SliderFloat("Yaw", &l.yaw, -180.0f, 180.0f, "%.0f deg");
                    changed |= ImGui::IsItemDeactivatedAfterEdit();
                } else {
                    if (ui::DragInt("Furniture seed", &s.seed, 0.2f)) {
                    }
                    changed |= ImGui::IsItemDeactivatedAfterEdit();
                    prefHelp("Another turn and size for every tree; positions stay.");
                }
            }
        }
        ui::EndInsetCard();
        ImGui::PopID();
        ImGui::Spacing();
    }

    // --- At this road's junctions ---------------------------------------------
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ui::FontScope bold(ui::boldFont());
        ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), IM_TRUNC(fs * 0.8f), p, u32(sem.textDim),
                                            "AT THIS ROAD'S JUNCTIONS");
        ImGui::Dummy(ImVec2(1.0f, ImGui::GetTextLineHeight()));
    }
    {
        static const char* const kSigns[] = {"None", "Give way", "Stop"};
        if (ui::ChoiceRow("Signs", kSigns, 3, &s.signs)) changed = true;
        prefHelp("A sign beside every stop line this road gives way at, facing\n"
                 "the driver coming in. Needs Markings (the stop line).");
        if (s.signs != roadfurn::kSignNone && o.roadMarkings == 0)
            ImGui::TextColored(sem.warn, "No stop line to stand at: Markings is None (Road tab)");
        auto modelCombo = [&](const char* label, std::string& path) {
            const std::string current =
                path.empty() ? "<built-in>" : std::filesystem::path(path).filename().string();
            bool hit = false;
            if (ui::BeginCombo(label, current.c_str())) {
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
        if (s.signs != roadfurn::kSignNone && modelCombo("Sign model", s.signModel)) changed = true;
        if (ui::Switch("Traffic lights (Signal heads at 3- and 4-way nodes)", &s.signals)) changed = true;
        prefHelp("A signal on every arm of this road's three- and four-way nodes,\n"
                 "in place of the signs there. A junction's own Control (select\n"
                 "its diamond) can turn them on or off at that node alone.");
        if (s.signals && modelCombo("Signal model", s.signalModel)) changed = true;
    }
    ImGui::Spacing();

    // --- Breakable -------------------------------------------------------------
    ui::BeginInsetCard("##furnbreak");
    if (ui::CardHeader("Breakable", "A fast car knocks it over \xc2\xb7 On Prop Broken", &s.breakable)) changed = true;
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("Need for Speed style: a car that hits a lamp post, a sign,\n"
                          "a bollard or a traffic light at least as fast as its kind's\n"
                          "Break speed knocks it over - it snaps off and tumbles away,\n"
                          "its light goes out, and the car keeps going a little slower.\n"
                          "Slower bumps stop the car as before. Needs a vehicle in the\n"
                          "project; the props stand again on a scene load.");
    if (s.breakable) {
        ImGui::Separator();
        static const char* const kKinds[roadfurn::kKindCount] = {"Lamps", "Trees", "Bollards", "Signs",
                                                                 "Traffic lights"};
        roadBreakSel_ = std::clamp(roadBreakSel_, 0, roadfurn::kKindCount - 1);
        for (int k = 0; k < roadfurn::kKindCount; ++k) {
            const roadfurn::Break& b = s.brk[k];
            char detail[24] = "";
            if (b.on) std::snprintf(detail, sizeof detail, "%.0f km/h", b.speed / upm * 3.6f);
            const float w = ui::DetailChipWidth(kKinds[k], detail);
            ui::ChipFlow(w, k == 0);
            if (ui::DetailChip(kKinds[k], detail, b.on, roadBreakSel_ == k)) roadBreakSel_ = k;
        }
        ImGui::TextDisabled("Click a kind for its break speed and speed lost");
        roadfurn::Break& b = s.brk[roadBreakSel_];
        char sw[64];
        std::snprintf(sw, sizeof sw, "%s break###brkon", kKinds[roadBreakSel_]);
        if (ui::Switch(sw, &b.on)) changed = true;
        if (roadBreakSel_ == roadfurn::kTree) prefHelp("Off by default: a tree is what stops a car.");
        if (b.on) {
            char fmtBuf[48];
            std::snprintf(fmtBuf, sizeof fmtBuf, "%%.1f u/s");  // km/h is on the kind's chip
            ui::SliderFloat("Break speed", &b.speed, 1.0f, 40.0f, fmtBuf);
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            prefHelp("The car must be at least this fast (units per second) or the\n"
                     "prop is a wall, as before.");
            float pct = b.loss * 100.0f;
            if (ui::SliderFloat("Speed lost", &pct, 0.0f, 90.0f, "%.0f%%")) b.loss = pct / 100.0f;
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            prefHelp("The share of the car's speed the hit takes. Small for a\n"
                     "sign, more for a lamp post.");
        }
        const std::string current =
            s.breakSound.empty() ? "<auto>" : std::filesystem::path(s.breakSound).filename().string();
        if (ui::BeginCombo("Hit sound", current.c_str())) {
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
    ui::EndInsetCard();
    ImGui::Spacing();

    char line[160];
    if (props > 0)
        std::snprintf(line, sizeof line, "~%d props \xc2\xb7 baked at build \xc2\xb7 hidden past %.0f m", props,
                      roadfurn::kDrawDistance / upm);
    else
        std::snprintf(line, sizeof line, "No lines on this road");
    ui::StatusLine(props > 0 ? 0 : 3, line);
    return changed;
}
