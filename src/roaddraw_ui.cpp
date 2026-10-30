// The Draw road tool, the Road's Properties (preset card and tabs) and the
// bridge height handles (docs/roads.md "Drawing roads", "Road presets",
// "Properties", "Bridges") - App:: methods
// declared in app.hpp, own TU (the junction_ui.cpp precedent).
//
// Every DECISION here is roaddraw:: / roadpresets:: (pure, host-only, checked
// by --vehicle-check "road drawing" and driven headlessly by --draw-road).
// This file only turns the mouse into a world XZ, draws the answer and commits.
#include "app.hpp"
#include "app_internal.hpp"
#include "roadbridge.hpp"
#include "roaddetail.hpp"
#include "roaddraw.hpp"
#include "roadgen.hpp"
#include "roadpresets.hpp"
#include "roadrail.hpp"
#include "roadtex.hpp"
#include "theme.hpp"
#include "toolkit.hpp"
#include "widgets.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <map>
#include <string>

namespace {

constexpr float kTwoPi = 6.2831853f;

}  // namespace

void App::rebuildRoadSnapper() {
    std::vector<int> idx;
    std::vector<roadgen::CrossingRoad> roads = project::crossingRoads(
        project_.objects(), &idx, project_.dir,
        [this](float x, float z) { return viewport_.terrainHeight(x, z); });
    roadDraw_.snapper = std::make_unique<roaddraw::Snapper>(std::move(roads));
    roadDraw_.roadObject = std::move(idx);
    roadDraw_.scene = project_.activeScene;
    roadDraw_.serial = modelEditSerial_;
}

void App::startRoadDraw() {
    roadDraw_.active = true;
    roadDraw_.placed.clear();
    roadDraw_.hoverValid = false;
    measureMode_ = sculptMode_ = paintMode_ = false;
    roadEdit_ = false;
    roadDragPoint_ = -1;
    if (!roadpresets::find(project_.settings.roadPresets, roadDraw_.preset))
        roadDraw_.preset = roadpresets::builtins().front().key;
    rebuildRoadSnapper();
    statusMessage_ = "Draw road: click the ground to place points, double-click or Enter to finish";
}

void App::stopRoadDraw() {
    roadDraw_.active = false;
    roadDraw_.placed.clear();
    roadDraw_.hoverValid = false;
    roadDraw_.snapper.reset();
}

void App::finishRoadDraw() {
    if (!roadDraw_.snapper) return;
    const roadpresets::Preset* pr = roadpresets::find(project_.settings.roadPresets, roadDraw_.preset);
    if (!pr) pr = &roadpresets::builtins().front();
    const float upm = project_.settings.unitsPerMeter;
    const std::vector<SceneObject>& objs = project_.objects();
    // "The same look": the preset would leave the road's width and surfaces
    // as they are - then carrying it on is more of that road.
    auto sameLook = [&](int r) {
        if (r < 0 || r >= (int)roadDraw_.roadObject.size()) return false;
        const SceneObject& o = objs[(size_t)roadDraw_.roadObject[(size_t)r]];
        SceneObject t = o;
        roadpresets::apply(*pr, t, upm);
        return t.roadWidth == o.roadWidth && t.roadTexture == o.roadTexture &&
               t.roadKind == o.roadKind;
    };
    roaddraw::Options opt = roadDraw_.opt;
    const roaddraw::Plan plan = roaddraw::finish(*roadDraw_.snapper, roadDraw_.placed, opt, sameLook);
    roadDraw_.placed.clear();
    if (!plan.ok) {
        statusMessage_ = "Draw road: " + plan.error;
        return;
    }
    const int oi = roaddraw::commit(project_.objects(), roadDraw_.roadObject, plan, *pr, project_.dir, upm);
    if (oi < 0) return;
    // A material written on demand is a new asset: let the pickers see it.
    materialAssetScanTime_ = -1.0;  // a material written on demand is a new asset
    selectOnly(oi);
    commitChange();
    statusMessage_ = "Draw road: " + project_.objects()[(size_t)oi].name + " - " + plan.summary +
                     " (Ctrl+Z to undo)";
    rebuildRoadSnapper();
}

bool App::roadDrawViewport(ImVec2 imgPos, ImVec2 avail, bool imageHovered, bool overAxisGizmo) {
    RoadDrawTool& t = roadDraw_;
    if (!t.active) return false;
    if (!hasProject_ || avail.x < 1.0f || avail.y < 1.0f) return false;
    // Rebuilt on a scene switch AND on any model edit (an undo, a road moved
    // from Properties): a stale Snapper kept naming a road Ctrl+Z had removed.
    if (!t.snapper || t.scene != project_.activeScene || t.serial != modelEditSerial_) {
        t.placed.clear();
        rebuildRoadSnapper();
    }
    ImGuiIO& io = ImGui::GetIO();
    const theme::Semantics& sem = theme::semantics();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const roadpresets::Preset* pr = roadpresets::find(project_.settings.roadPresets, t.preset);
    if (!pr) pr = &roadpresets::builtins().front();

    // --- The options panel (top-left of the viewport). It owns its clicks.
    dl->ChannelsSplit(2);
    dl->ChannelsSetCurrent(1);
    const ImVec2 panelPos(imgPos.x + scaled(8.0f), imgPos.y + scaled(32.0f));
    ImGui::SetCursorScreenPos(ImVec2(panelPos.x + scaled(6.0f), panelPos.y + scaled(6.0f)));
    ImGui::BeginGroup();
    ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.35f, 1.0f), "Draw road");
    ImGui::SetNextItemWidth(scaled(180.0f));
    if (ImGui::BeginCombo("Preset##roaddraw", pr->name.c_str())) {
        for (const roadpresets::Preset* p : roadpresets::all(project_.settings.roadPresets)) {
            if (ImGui::Selectable((p->name + "##rdp-" + p->key).c_str(), p->key == t.preset))
                t.preset = p->key;
            if (ImGui::IsItemHovered() && !p->help.empty()) ImGui::SetTooltip("%s", p->help.c_str());
        }
        ImGui::EndCombo();
    }
    ImGui::Checkbox("Snap to roads##roaddraw", &t.opt.roadSnap);
    ImGui::SameLine();
    ImGui::Checkbox("15 deg##roaddraw", &t.opt.angleSnap);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Angle steps of 15 degrees from the previous segment, or from\n"
                          "the road the drawing starts on or ends on (so a T is square).\n"
                          "Hold Shift to place freely.");
    ImGui::Checkbox("Grid##roaddraw", &t.opt.gridSnap);
    if (t.opt.gridSnap) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(scaled(70.0f));
        ImGui::DragFloat("##roaddrawgrid", &t.opt.grid, 0.25f, 0.5f, 64.0f, "%.1f u");
    }
    ImGui::SameLine();
    ImGui::Checkbox("Extend at ends##roaddraw", &t.opt.extendEnds);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("A drawing that leaves an existing road's end in line with it\n"
                          "(within 45 degrees) and has the same width and surface carries\n"
                          "that road on instead of starting a second one.");
    ImGui::TextDisabled(t.placed.empty() ? "Click to start - Esc leaves the tool"
                                         : "Double-click / Enter: finish   Backspace: undo point   Esc: cancel");
    ImGui::EndGroup();
    const ImVec2 gMin = ImGui::GetItemRectMin(), gMax = ImGui::GetItemRectMax();
    const ImVec2 pMin = panelPos, pMax(gMax.x + scaled(6.0f), gMax.y + scaled(6.0f));
    (void)gMin;
    dl->ChannelsSetCurrent(0);
    dl->AddRectFilled(pMin, pMax, IM_COL32(18, 18, 22, 215), scaled(4.0f));
    dl->ChannelsMerge();
    t.panelRect = ImVec4(pMin.x, pMin.y, pMax.x, pMax.y);
    const bool overPanel = ImGui::IsMouseHoveringRect(pMin, pMax, false) ||
                           ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);

    // --- Keys (viewport hovered, not typing).
    const bool keys = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && !io.WantTextInput;
    if (keys && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        if (t.placed.empty()) {
            stopRoadDraw();
            statusMessage_ = "Draw road: off";
            return false;
        }
        t.placed.clear();
        statusMessage_ = "Draw road: cancelled";
    }
    if (keys && ImGui::IsKeyPressed(ImGuiKey_Backspace) && !t.placed.empty()) t.placed.pop_back();
    if (keys && (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) &&
        !t.placed.empty()) {
        finishRoadDraw();
        return true;
    }

    // --- Where the cursor snaps.
    const float u = (io.MousePos.x - imgPos.x) / avail.x;
    const float v = (io.MousePos.y - imgPos.y) / avail.y;
    std::vector<char> skipAll(project_.objects().size(), 1);
    float ground[3] = {0, 0, 0};
    t.hoverValid = imageHovered && !overPanel &&
                   viewport_.placementRaycast(u, v, project_.objects(), skipAll, ground);
    roaddraw::Options opt = t.opt;
    if (io.KeyShift) opt.angleSnap = false;
    if (t.hoverValid) {
        // ~14 pixels of reach beyond a road's edge, whatever the zoom.
        float g2[3];
        const float du = scaled(14.0f) / avail.x;
        if (viewport_.placementRaycast(u + du, v, project_.objects(), skipAll, g2))
            opt.tolerance = std::clamp(std::hypot(g2[0] - ground[0], g2[2] - ground[2]), 0.3f, 12.0f);
        t.hover = t.snapper->resolve(t.placed, ground[0], ground[2], opt);
    }

    // --- Clicks: place a point; a double-click finishes.
    const bool lmbCamera = nav_.scheme == NavScheme::Maya && io.KeyAlt;
    const bool owns = imageHovered && !overPanel && !overAxisGizmo;
    if (owns && !lmbCamera && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            finishRoadDraw();
            return true;
        }
        if (t.hoverValid) {
            t.placed.push_back(t.hover);
            // Back on the first point closes a loop: done.
            if (t.hover.kind == roaddraw::kClose) {
                finishRoadDraw();
                return true;
            }
        }
    }

    // --- The preview: the spline through the points and the cursor, a ghost
    // of the preset's width, the snap target and a label.
    auto toScreen = [&](float x, float z, ImVec2& out, float lift = 0.2f) {
        const float w[3] = {x, viewport_.terrainHeight(x, z) + lift, z};
        float su, sv;
        if (!viewport_.projectToImage(w, su, sv)) return false;
        out = ImVec2(imgPos.x + su * avail.x, imgPos.y + sv * avail.y);
        return true;
    };
    dl->PushClipRect(imgPos, ImVec2(imgPos.x + avail.x, imgPos.y + avail.y), true);
    // The snapped road, highlighted along its centre line.
    if (t.hoverValid && t.hover.road >= 0 && t.hover.road < t.snapper->roads()) {
        const std::vector<float>& line = t.snapper->line(t.hover.road);
        const size_t n = line.size() / 2;
        const size_t stride = std::max<size_t>(1, n / 300);
        ImVec2 prev;
        bool havePrev = false;
        for (size_t i = 0; i < n; i += stride) {
            ImVec2 p;
            const bool ok = toScreen(line[i * 2], line[i * 2 + 1], p);
            if (ok && havePrev) dl->AddLine(prev, p, IM_COL32(90, 210, 255, 200), scaled(3.0f));
            prev = p;
            havePrev = ok;
        }
    }
    std::vector<float> pts;
    for (const roaddraw::Snap& s : t.placed) pts.insert(pts.end(), {s.x, s.z});
    if (t.hoverValid) pts.insert(pts.end(), {t.hover.x, t.hover.z});
    if (pts.size() >= 4) {
        const int segs = (int)(pts.size() / 2) - 1;
        const int per = std::clamp(240 / std::max(1, segs), 4, 24);
        const int total = segs * per;
        const float hw = 0.5f * pr->width;
        ImVec2 pl, pc, pr2;
        bool havePrev = false;
        for (int k = 0; k <= total; ++k) {
            float x, z, x2, z2;
            const float tt = (float)k / (float)total;
            roadgen::splineAt(pts, tt, &x, &z);
            roadgen::splineAt(pts, std::min(1.0f, tt + 0.002f), &x2, &z2);
            float dx = x2 - x, dz = z2 - z;
            if (k == total) roadgen::splineAt(pts, tt - 0.002f, &dx, &dz), dx = x - dx, dz = z - dz;
            const float l = std::hypot(dx, dz);
            if (!(l > 1e-6f)) continue;
            const float nx = -dz / l, nz = dx / l;
            ImVec2 a, c, b;
            const bool ok = toScreen(x + nx * hw, z + nz * hw, a) && toScreen(x, z, c) &&
                            toScreen(x - nx * hw, z - nz * hw, b);
            if (ok && havePrev) {
                dl->AddQuadFilled(pl, a, b, pr2, IM_COL32(255, 205, 90, 46));
                dl->AddLine(pl, a, IM_COL32(255, 205, 90, 170), scaled(1.5f));
                dl->AddLine(pr2, b, IM_COL32(255, 205, 90, 170), scaled(1.5f));
                dl->AddLine(pc, c, IM_COL32(255, 240, 200, 230), scaled(2.0f));
            }
            pl = a, pc = c, pr2 = b;
            havePrev = ok;
        }
    }
    for (size_t i = 0; i < t.placed.size(); ++i) {
        ImVec2 p;
        if (!toScreen(t.placed[i].x, t.placed[i].z, p)) continue;
        dl->AddCircleFilled(p, scaled(i == 0 ? 6.0f : 4.5f), IM_COL32(255, 205, 90, 240));
        dl->AddCircle(p, scaled(i == 0 ? 6.0f : 4.5f), IM_COL32(20, 20, 24, 230), 0, scaled(1.2f));
    }
    if (t.hoverValid) {
        ImVec2 c;
        if (toScreen(t.hover.x, t.hover.z, c)) {
            const float r = scaled(8.0f);
            const ImU32 accent = ImGui::GetColorU32(sem.accent);
            const ImU32 col = t.hover.kind == roaddraw::kEnd      ? IM_COL32(90, 230, 120, 255)
                              : t.hover.kind == roaddraw::kCentre ? IM_COL32(90, 210, 255, 255)
                              : t.hover.kind == roaddraw::kClose  ? accent
                                                                  : IM_COL32(255, 240, 200, 235);
            if (t.hover.kind == roaddraw::kEnd) {
                dl->AddRect(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), col, 0.0f, 0, scaled(2.5f));
            } else if (t.hover.kind == roaddraw::kCentre) {
                const ImVec2 d[4] = {ImVec2(c.x, c.y - r), ImVec2(c.x + r, c.y), ImVec2(c.x, c.y + r),
                                     ImVec2(c.x - r, c.y)};
                dl->AddPolyline(d, 4, col, ImDrawFlags_Closed, scaled(2.5f));
            } else if (t.hover.kind == roaddraw::kClose) {
                dl->AddCircle(c, r * 1.3f, col, 0, scaled(2.5f));
            } else if (t.hover.kind == roaddraw::kGrid) {
                dl->AddLine(ImVec2(c.x - r, c.y), ImVec2(c.x + r, c.y), col, scaled(1.5f));
                dl->AddLine(ImVec2(c.x, c.y - r), ImVec2(c.x, c.y + r), col, scaled(1.5f));
            }
            dl->AddCircleFilled(c, scaled(3.0f), col);
            // What a click here makes.
            std::string label;
            const std::string roadName =
                t.hover.road >= 0 && t.hover.road < (int)t.roadObject.size()
                    ? project_.objects()[(size_t)t.roadObject[(size_t)t.hover.road]].name
                    : std::string();
            switch (t.hover.kind) {
                case roaddraw::kEnd: label = "end of " + roadName + " (carry on / corner)"; break;
                case roaddraw::kCentre:
                    label = (t.placed.empty() ? "T from " : "T / crossing on ") + roadName;
                    break;
                case roaddraw::kClose: label = "close the loop"; break;
                case roaddraw::kGrid: label = "grid"; break;
                default: break;
            }
            char buf[64];
            if (t.hover.angled) {
                std::snprintf(buf, sizeof buf, "%s%.0f deg", label.empty() ? "" : "  ", t.hover.angle);
                label += buf;
            }
            if (!t.placed.empty()) {
                const float d = std::hypot(t.hover.x - t.placed.back().x, t.hover.z - t.placed.back().z);
                std::snprintf(buf, sizeof buf, "%s%.1f u", label.empty() ? "" : "  ", d);
                label += buf;
            }
            if (!label.empty()) {
                const ImVec2 at(c.x + scaled(12.0f), c.y + scaled(8.0f));
                const ImVec2 sz = ImGui::CalcTextSize(label.c_str());
                dl->AddRectFilled(ImVec2(at.x - scaled(3.0f), at.y - scaled(2.0f)),
                                  ImVec2(at.x + sz.x + scaled(3.0f), at.y + sz.y + scaled(2.0f)),
                                  IM_COL32(18, 18, 22, 210), scaled(3.0f));
                dl->AddText(at, IM_COL32(240, 240, 240, 255), label.c_str());
            }
        }
    }
    dl->PopClipRect();
    (void)kTwoPi;
    return owns;
}

bool App::bridgeHandles(ImVec2 imgPos, ImVec2 avail, bool imageHovered) {
    if (roadDraw_.active || !hasProject_ || avail.x < 1.0f || avail.y < 1.0f) {
        bridgeDragPoint_ = -1;
        return false;
    }
    std::vector<SceneObject>& objs = project_.objects();
    if (selectedObject_ < 0 || selectedObject_ >= (int)objs.size()) {
        bridgeDragPoint_ = -1;
        return false;
    }
    SceneObject& o = objs[(size_t)selectedObject_];
    if (o.type != PrimitiveType::Road || !o.roadBridge || o.roadPoints.size() < 4) {
        bridgeDragPoint_ = -1;
        return false;
    }
    roadbridge::onPointsReshaped(o);  // heights sized to the points
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float base = roadgen::kLift + roadgen::rankLift(o.roadRank);
    auto project = [&](float x, float y, float z, ImVec2& out) {
        const float w[3] = {x, y, z};
        float su, sv;
        if (!viewport_.projectToImage(w, su, sv)) return false;
        out = ImVec2(imgPos.x + su * avail.x, imgPos.y + sv * avail.y);
        return true;
    };
    const float rise = scaled(24.0f);  // the handle sits this far above the deck marker
    const float half = scaled(6.0f);
    int hot = -1;
    dl->PushClipRect(imgPos, ImVec2(imgPos.x + avail.x, imgPos.y + avail.y), true);
    const int n = roadgen::controlCount(o.roadPoints);
    for (int k = 0; k < n; ++k) {
        const float x = o.roadPoints[(size_t)k * 2], z = o.roadPoints[(size_t)k * 2 + 1];
        const float g = viewport_.terrainHeight(x, z);
        const float h = roadbridge::heightOf(o, k);
        ImVec2 foot, deck;
        if (!project(x, g, z, foot) || !project(x, g + base + h, z, deck)) continue;
        // The stem: ground to deck, dashed, so a high deck reads as height.
        const float len = std::hypot(deck.x - foot.x, deck.y - foot.y);
        const int dashes = std::max(1, (int)(len / scaled(8.0f)));
        for (int d = 0; d < dashes; d += 2) {
            const float a0 = (float)d / (float)dashes, a1 = std::min(1.0f, (float)(d + 1) / (float)dashes);
            dl->AddLine(ImVec2(foot.x + (deck.x - foot.x) * a0, foot.y + (deck.y - foot.y) * a0),
                        ImVec2(foot.x + (deck.x - foot.x) * a1, foot.y + (deck.y - foot.y) * a1),
                        IM_COL32(255, 220, 60, 200), scaled(1.5f));
        }
        const ImVec2 c(deck.x, deck.y - rise);
        dl->AddLine(deck, c, IM_COL32(255, 220, 60, 200), scaled(1.5f));
        const bool over = imageHovered && std::fabs(io.MousePos.x - c.x) <= half + scaled(2.0f) &&
                          std::fabs(io.MousePos.y - c.y) <= half + scaled(2.0f);
        if (over && hot < 0) hot = k;
        const bool active = bridgeDragPoint_ == k;
        dl->AddRectFilled(ImVec2(c.x - half, c.y - half), ImVec2(c.x + half, c.y + half),
                          active ? IM_COL32(255, 140, 40, 255)
                          : over ? IM_COL32(255, 240, 150, 255)
                                 : IM_COL32(255, 220, 60, 235));
        dl->AddRect(ImVec2(c.x - half, c.y - half), ImVec2(c.x + half, c.y + half),
                    IM_COL32(20, 20, 24, 235), 0.0f, 0, scaled(1.2f));
        // Up/down chevrons: this one moves vertically.
        dl->AddTriangleFilled(ImVec2(c.x, c.y - half + scaled(1.5f)), ImVec2(c.x - scaled(3.0f), c.y - scaled(1.0f)),
                              ImVec2(c.x + scaled(3.0f), c.y - scaled(1.0f)), IM_COL32(20, 20, 24, 235));
        dl->AddTriangleFilled(ImVec2(c.x, c.y + half - scaled(1.5f)), ImVec2(c.x + scaled(3.0f), c.y + scaled(1.0f)),
                              ImVec2(c.x - scaled(3.0f), c.y + scaled(1.0f)), IM_COL32(20, 20, 24, 235));
        char lab[32];
        std::snprintf(lab, sizeof lab, "%.1f", h);
        dl->AddText(ImVec2(c.x + half + scaled(4.0f), c.y - scaled(7.0f)), IM_COL32(255, 240, 200, 240), lab);
        // A named item so --ui-script can reach it ("Bridge height 2").
        const ImVec2 a(std::max(c.x - half, imgPos.x), std::max(c.y - half, imgPos.y));
        const ImVec2 b(std::min(c.x + half, imgPos.x + avail.x), std::min(c.y + half, imgPos.y + avail.y));
        if (b.x > a.x && b.y > a.y) {
            const ImVec2 cursor = ImGui::GetCursorScreenPos();
            ImGui::SetCursorScreenPos(a);
            ImGui::InvisibleButton(("Bridge height " + std::to_string(k + 1)).c_str(),
                                   ImVec2(b.x - a.x, b.y - a.y));
            ImGui::SetCursorScreenPos(cursor);
        }
    }
    dl->PopClipRect();

    auto rayY = [&](int k, float* y) {
        const float u = (io.MousePos.x - imgPos.x) / avail.x;
        const float v = (io.MousePos.y - imgPos.y) / avail.y;
        float ro[3], rd[3];
        viewport_.cameraRay(u, v, ro, rd);
        return roaddraw::verticalHandleY(ro, rd, o.roadPoints[(size_t)k * 2],
                                         o.roadPoints[(size_t)k * 2 + 1], y);
    };
    if (bridgeDragPoint_ < 0 && hot >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        float y;
        const float x = o.roadPoints[(size_t)hot * 2], z = o.roadPoints[(size_t)hot * 2 + 1];
        const float deckY = viewport_.terrainHeight(x, z) + base + roadbridge::heightOf(o, hot);
        bridgeDragPoint_ = hot;
        // The handle floats above the deck on screen: keep that offset, so the
        // grab does not jump.
        bridgeDragGrab_ = rayY(hot, &y) ? deckY - y : 0.0f;
    }
    if (bridgeDragPoint_ >= 0) {
        const int k = bridgeDragPoint_;
        if (k >= n) {
            bridgeDragPoint_ = -1;
            return false;
        }
        float y;
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && rayY(k, &y)) {
            const float x = o.roadPoints[(size_t)k * 2], z = o.roadPoints[(size_t)k * 2 + 1];
            float hNew = roaddraw::bridgeHeightFor(y + bridgeDragGrab_,
                                                   viewport_.terrainHeight(x, z) + base, true);
            if (io.KeyCtrl) hNew = std::round(hNew);
            o.roadHeights[(size_t)k] = hNew;
        }
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            char buf[64];
            std::snprintf(buf, sizeof buf, "Bridge height %d: %.1f", k + 1, o.roadHeights[(size_t)k]);
            bridgeDragPoint_ = -1;
            commitChange();
            statusMessage_ = buf;
        }
        return true;
    }
    return hot >= 0;
}

// --- The Road's Properties ---------------------------------------------------
// docs/roads.md "Properties". The panel SHOWS the road - a cross-section whose
// edges drag the width, an edge detail, a plan of the furniture, a height
// profile of a bridge - and keeps the numbers next to the pictures. Every
// control edits an existing SceneObject road field (no format change); the
// tabs, the open folds and the off-memo are editor state only.

namespace {

ImVec4 mixc(ImVec4 a, ImVec4 b, float t) {
    return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
}
ImU32 u32(ImVec4 c, float alpha = 1.0f) {
    c.w *= alpha;
    return ImGui::GetColorU32(c);
}

// The materials a picture depicts. Physical things, so they are data like
// the pad's face colours - but derived from the theme, so a light theme
// draws a light asphalt rather than a black hole.
struct Paint {
    ImU32 ground, asphalt, kerb, pavement, line, dim, accent, lamp, tree, bollard, sign, ballast, sleeper, rail;
};
Paint paint() {
    const theme::Semantics& s = theme::semantics();
    Paint p;
    p.ground = u32(mixc(s.field, s.ok, 0.10f));
    p.asphalt = u32(mixc(s.field, s.text, 0.13f));
    p.kerb = u32(mixc(s.field, s.text, 0.55f));
    p.pavement = u32(mixc(s.field, s.text, 0.32f));
    p.line = u32(mixc(s.field, s.text, 0.80f));
    p.dim = u32(s.textDim);
    p.accent = u32(s.accent);
    p.lamp = u32(s.warn);
    p.tree = u32(s.ok, 0.85f);
    p.bollard = u32(mixc(s.textDim, s.text, 0.4f));
    p.sign = u32(s.danger);
    p.ballast = u32(mixc(s.field, s.text, 0.25f));
    p.sleeper = u32(mixc(mixc(s.field, s.warn, 0.35f), s.text, 0.10f));
    p.rail = u32(mixc(s.field, s.text, 0.70f));
    return p;
}

// Upper-case caption in the small heading cut, with an optional accent link
// at the right end. Returns the link's click.
bool captionRow(const char* text, const char* link = nullptr, const char* note = nullptr) {
    const theme::Semantics& sem = theme::semantics();
    ImGuiWindow* w = ImGui::GetCurrentWindow();
    const float W = ImGui::GetContentRegionAvail().x;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = ImGui::GetTextLineHeight();
    std::string up;
    for (const char* c = text; *c && !(c[0] == '#' && c[1] == '#'); ++c) up += (char)std::toupper((unsigned char)*c);
    {
        ui::FontScope bold(ui::boldFont());
        const float fs = IM_TRUNC(ImGui::GetFontSize() * 0.80f);
        w->DrawList->AddText(ImGui::GetFont(), fs, ImVec2(p.x, p.y + (h - fs) * 0.5f), u32(sem.textDim), up.c_str());
    }
    bool clicked = false;
    if (link) {
        const float lw = ImGui::CalcTextSize(link, nullptr, true).x;
        ImGui::SetCursorScreenPos(ImVec2(p.x + W - lw, p.y));
        clicked = ui::Link(link);
    } else if (note && *note) {
        const ImVec2 ns = ImGui::CalcTextSize(note);
        w->DrawList->AddText(ImVec2(p.x + W - ns.x, p.y), u32(sem.textDim, 0.85f), note);
    }
    ImGui::SetCursorScreenPos(p);
    ImGui::Dummy(ImVec2(W, h));
    return clicked;
}

void dimText(const char* fmt, ...) IM_FMTARGS(1);
void dimText(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    ImGui::PushStyleColor(ImGuiCol_Text, theme::semantics().textDim);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextV(fmt, args);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    va_end(args);
}

// The spline's length, sampled finely enough for a panel readout.
float roadLength(const std::vector<float>& pts) {
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

bool nearly(float a, float b) { return std::fabs(a - b) <= 1e-4f * std::max(1.0f, std::fabs(b)); }

// The fields a preset owns, compared one by one: the "N changes" chip, its
// hover list and the choice of the preset a road is closest to.
int presetDiffs(const SceneObject& a, const SceneObject& b, std::vector<std::string>* names) {
    int n = 0;
    auto add = [&](bool differs, const char* name) {
        if (!differs) return;
        ++n;
        if (names) names->push_back(name);
    };
    add(!nearly(a.roadWidth, b.roadWidth), "Width");
    add(!nearly(a.roadSampleStep, b.roadSampleStep), "Sampling");
    add(!nearly(a.roadGrip, b.roadGrip), "Grip");
    add(a.roadRank != b.roadRank, "Priority");
    add(!nearly(a.roadSpill, b.roadSpill), "Spill");
    add(!nearly(a.roadEdgeFade, b.roadEdgeFade), "Soft edge");
    add(a.roadMarkings != b.roadMarkings, "Markings");
    add(a.roadKerb != b.roadKerb, "Kerbs");
    add(a.roadKerb && (!nearly(a.roadKerbHeight, b.roadKerbHeight) || !nearly(a.roadKerbWidth, b.roadKerbWidth)),
        "Kerb size");
    add(!nearly(a.roadPavement, b.roadPavement), "Pavement");
    add(a.roadPavementMaterial != b.roadPavementMaterial, "Pavement material");
    add(a.roadTexture != b.roadTexture, "Surface material");
    add(a.roadIntersectionTexture != b.roadIntersectionTexture, "Junction material");
    add(a.roadKind != b.roadKind, "Kind");
    add(a.roadKind != 0 && a.roadTracks != b.roadTracks, "Tracks");
    add(a.roadKind != 0 && !nearly(a.roadRailGauge, b.roadRailGauge), "Gauge");
    add(!nearly(a.roadDetails, b.roadDetails), "Wear & details");
    add(a.roadDetails > 0.0f && a.roadDetailSeed != b.roadDetailSeed, "Detail seed");
    const roadfurn::Settings &fa = a.roadFurniture, &fb = b.roadFurniture;
    add(!(fa.lamps == fb.lamps), "Street lamps");
    add(!(fa.trees == fb.trees), "Trees");
    add(!(fa.bollards == fb.bollards), "Bollards");
    add(fa.seed != fb.seed, "Furniture seed");
    add(fa.signs != fb.signs || fa.signModel != fb.signModel, "Signs");
    add(fa.signals != fb.signals || fa.signalModel != fb.signalModel, "Traffic lights");
    bool brk = fa.breakable != fb.breakable || fa.breakSound != fb.breakSound;
    for (int k = 0; k < roadfurn::kKindCount; ++k) brk |= !(fa.brk[k] == fb.brk[k]);
    add(brk, "Breakable");
    return n;
}

// Picture-tile drawings (ui::TileDrawFn): the three kinds and the four wear
// levels, in the stroke weight of the editor's glyphs.
void kindRoad(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32 ink, bool) {
    const float cx = (mn.x + mx.x) * 0.5f, h = mx.y - mn.y, s = std::min(h, (mx.x - mn.x) * 0.6f);
    const float top = mn.y + (h - s) * 0.5f, bot = top + s, t = s * 0.07f;
    dl->AddLine(ImVec2(cx - s * 0.45f, bot), ImVec2(cx - s * 0.15f, top), ink, t);
    dl->AddLine(ImVec2(cx + s * 0.45f, bot), ImVec2(cx + s * 0.15f, top), ink, t);
    for (int i = 0; i < 3; ++i) {
        const float y0 = top + s * (0.08f + 0.33f * (float)i), y1 = y0 + s * 0.18f;
        dl->AddLine(ImVec2(cx, y0), ImVec2(cx, y1), ink, t);
    }
}
void kindRail(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32 ink, bool) {
    const float cx = (mn.x + mx.x) * 0.5f, h = mx.y - mn.y, s = std::min(h, (mx.x - mn.x) * 0.6f);
    const float top = mn.y + (h - s) * 0.5f, bot = top + s, t = s * 0.07f;
    dl->AddLine(ImVec2(cx - s * 0.35f, bot), ImVec2(cx - s * 0.18f, top), ink, t);
    dl->AddLine(ImVec2(cx + s * 0.35f, bot), ImVec2(cx + s * 0.18f, top), ink, t);
    for (int i = 0; i < 4; ++i) {
        const float f = 0.12f + 0.25f * (float)i, y = top + s * f, half = s * (0.24f + 0.16f * f);
        dl->AddLine(ImVec2(cx - half, y), ImVec2(cx + half, y), ink, t * 0.9f);
    }
}
void kindTram(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32 ink, bool) {
    const float cx = (mn.x + mx.x) * 0.5f, h = mx.y - mn.y, s = std::min(h, (mx.x - mn.x) * 0.6f);
    const float top = mn.y + (h - s) * 0.5f, bot = top + s, t = s * 0.07f;
    dl->AddLine(ImVec2(cx - s * 0.45f, bot), ImVec2(cx - s * 0.15f, top), ink, t);
    dl->AddLine(ImVec2(cx + s * 0.45f, bot), ImVec2(cx + s * 0.15f, top), ink, t);
    dl->AddLine(ImVec2(cx - s * 0.12f, bot), ImVec2(cx - s * 0.04f, top), ink, t * 0.8f);
    dl->AddLine(ImVec2(cx + s * 0.12f, bot), ImVec2(cx + s * 0.04f, top), ink, t * 0.8f);
}
void wearPatch(ImDrawList* dl, ImVec2 mn, ImVec2 mx, int level) {
    const Paint pt = paint();
    const float h = mx.y - mn.y, w = std::min(mx.x - mn.x, h * 1.9f);
    const ImVec2 a((mn.x + mx.x - w) * 0.5f, mn.y), b(a.x + w, mx.y);
    dl->AddRectFilled(a, b, pt.asphalt, 3.0f);
    const ImU32 dark = u32(mixc(theme::semantics().field, ImVec4(0, 0, 0, 1), 0.45f));
    const ImU32 light = u32(mixc(theme::semantics().field, theme::semantics().text, 0.24f));
    struct Spot { float x, y, r; bool lite; };
    static const Spot spots[] = {{0.72f, 0.38f, 0.13f, false}, {0.24f, 0.62f, 0.12f, false},
                                 {0.62f, 0.66f, 0.20f, true},  {0.86f, 0.30f, 0.15f, false},
                                 {0.18f, 0.32f, 0.10f, true},  {0.45f, 0.30f, 0.09f, false}};
    const int count = level == 0 ? 0 : level == 1 ? 1 : level == 2 ? 3 : 6;
    for (int i = 0; i < count; ++i) {
        const Spot& s = spots[i];
        dl->AddCircleFilled(ImVec2(a.x + w * s.x, a.y + h * s.y), h * s.r, s.lite ? light : dark, 12);
    }
    if (level == 3) dl->AddLine(ImVec2(a.x + w * 0.1f, a.y + h * 0.85f), ImVec2(a.x + w * 0.5f, a.y + h * 0.55f), dark, 1.2f);
}
void wear0(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32, bool) { wearPatch(dl, mn, mx, 0); }
void wear1(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32, bool) { wearPatch(dl, mn, mx, 1); }
void wear2(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32, bool) { wearPatch(dl, mn, mx, 2); }
void wear3(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32, bool) { wearPatch(dl, mn, mx, 3); }

// A drag of a value stored in world units, shown and typed in metres
// (docs/world-scale.md: unitsPerMeter converts). Caption-left row.
bool metresDrag(const char* label, float* units, float upm, float speedM, float minU, float maxU, const char* fmt) {
    float m = *units / upm;
    if (!ui::DragFloat(label, &m, speedM, minU / upm, maxU / upm, fmt, ImGuiSliderFlags_AlwaysClamp)) return false;
    *units = std::clamp(m * upm, minU, maxU);
    return true;
}
bool metresSlider(const char* label, float* units, float upm, float minU, float maxU, const char* fmt) {
    float m = *units / upm;
    if (!ui::SliderFloat(label, &m, minU / upm, maxU / upm, fmt, ImGuiSliderFlags_AlwaysClamp)) return false;
    *units = std::clamp(m * upm, minU, maxU);
    return true;
}

// Picture height for a card that spans the panel: a fixed share of the font,
// so a zoomed interface keeps its proportions.
float picHeight(float lines) { return IM_TRUNC(ImGui::GetFontSize() * lines); }

}  // namespace

std::string App::roadSubtitle(const SceneObject& o) {
    const float upm = project_.settings.unitsPerMeter > 0.0f ? project_.settings.unitsPerMeter : 1.0f;
    const char* kind = o.roadKind == roadrail::kRail ? "Railway" : o.roadKind == roadrail::kTram ? "Tram street" : "Road";
    const int pts = roadgen::controlCount(o.roadPoints);
    int nodes = 0;
    const roadgen::CrossingPlan& plan = sceneCrossings();
    int me = -1;
    for (size_t k = 0; k < crossingRoadList_.size(); ++k)
        if (crossingRoadList_[k].id == o.id) me = (int)k;
    if (me >= 0)
        for (const roadgen::Crossing& c : plan.crossings)
            if (c.has(me)) ++nodes;
    char buf[160];
    std::snprintf(buf, sizeof buf, "%s \xc2\xb7 %d point%s \xc2\xb7 %.0f m \xc2\xb7 %d %s%s", kind, pts, pts == 1 ? "" : "s",
                  roadLength(o.roadPoints) / upm, nodes,
                  o.roadKind == roadrail::kRail ? (nodes == 1 ? "crossing" : "crossings")
                                                : (nodes == 1 ? "junction" : "junctions"),
                  o.roadBridge ? " \xc2\xb7 bridge" : "");
    return buf;
}

int App::roadTextureLanes(const std::string& mtlRel) {
    // A regenerated texture changes its recipe: forget everything now and then.
    static double cachedAt = -10.0;
    const double now = ImGui::GetTime();
    if (now - cachedAt > 3.0) {
        roadLanesCache_.clear();
        cachedAt = now;
    }
    auto it = roadLanesCache_.find(mtlRel);
    if (it != roadLanesCache_.end()) return it->second;
    int lanes = -1;
    const std::filesystem::path rel(mtlRel);
    roadtex::RoadTexParams p;
    if (rel.parent_path().generic_string() == roadtex::kDir &&
        roadtex::readRecipe(project_.dir, rel.stem().string(), &p) && !p.isotropic() &&
        p.surface != roadtex::kBallast)
        lanes = p.lanes;
    roadLanesCache_[mtlRel] = lanes;
    return lanes;
}

bool App::roadMaterialTile(const char* label, const char* title, std::string& path, const char* noneLabel,
                           float width) {
    // The tile shows the material's texture itself (its first map_Kd, flat):
    // a road surface reads better as the strip it is than as a lit sphere.
    static std::map<std::string, std::string> resolved;
    static double resolvedAt = -10.0;
    if (ImGui::GetTime() - resolvedAt > 3.0) {
        resolved.clear();
        resolvedAt = ImGui::GetTime();
    }
    std::string png;
    if (!path.empty()) {
        auto it = resolved.find(path);
        if (it == resolved.end()) it = resolved.emplace(path, project::resolveRoadTexture(project_, path)).first;
        png = it->second;
    }
    const uint32_t tex = png.empty() ? 0 : viewport_.assetThumb(png, true);
    std::string sub = path.empty() ? std::string(noneLabel) : std::filesystem::path(path).stem().string();
    const std::string popup = std::string("##matpick_") + label;
    bool changed = false;
    if (ui::AssetTile(label, (ImTextureID)(intptr_t)tex, ui::Glyph::Road, title, sub.c_str(), width))
        ImGui::OpenPopup(popup.c_str());
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("%s\nClick to pick another material.", path.empty() ? noneLabel : path.c_str());
    ImGui::SetNextWindowSizeConstraints(ImVec2(scaled(240), 0), ImVec2(scaled(420), scaled(420)));
    if (ImGui::BeginPopup(popup.c_str())) {
        if (ImGui::Selectable(noneLabel, path.empty()) && !path.empty()) {
            path.clear();
            changed = true;
        }
        ImGui::SeparatorText("Materials");
        const std::vector<std::string> materials = listMaterialAssets();
        for (const std::string& rel : materials) {
            const std::string item = rel.rfind("res/", 0) == 0 ? rel.substr(4) : rel;
            if (ImGui::Selectable(item.c_str(), rel == path) && rel != path) {
                path = rel;
                changed = true;
            }
        }
        if (materials.empty()) ImGui::TextDisabled("No .mtl assets yet.");
        ImGui::SeparatorText("Direct textures (legacy)");
        for (const std::string& name : listAssetFiles("textures", ".png")) {
            const std::string rel = "res/textures/" + name;
            if (ImGui::Selectable(name.c_str(), rel == path) && rel != path) {
                path = rel;
                changed = true;
            }
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Import material (.mtl)...")) {
            const std::string imported = importMaterialAsset();
            if (!imported.empty()) {
                path = imported;
                changed = true;
            }
        }
        if (ImGui::MenuItem("Import texture (.png, legacy)...")) {
            const std::string imported = importTextureAsset();
            if (!imported.empty()) {
                path = imported;
                changed = true;
            }
        }
        std::string ext = std::filesystem::path(path).extension().string();
        for (char& c : ext) c = (char)std::tolower((unsigned char)c);
        if (ext == ".mtl" && ImGui::MenuItem("Edit in Material Editor")) activateAsset(path);
        ImGui::EndPopup();
    }
    if (changed) resolved.erase(path);
    return changed;
}

bool App::drawRoadPresetControls(SceneObject& o) {
    bool changed = false;
    const float upm = project_.settings.unitsPerMeter;
    std::vector<roadpresets::Preset>& mine = project_.settings.roadPresets;
    // The preset this road is closest to (exactly it: 0 changes). There is no
    // stored "applied preset" - the road's own fields say which one it is.
    const roadpresets::Preset* best = nullptr;
    int bestDiffs = 1 << 30;
    for (const roadpresets::Preset* p : roadpresets::all(mine)) {
        SceneObject t = o;
        roadpresets::apply(*p, t, upm);
        const int d = presetDiffs(o, t, nullptr);
        if (d < bestDiffs) {
            bestDiffs = d;
            best = p;
        }
    }
    std::vector<std::string> diffNames;
    if (best) {
        SceneObject t = o;
        roadpresets::apply(*best, t, upm);
        presetDiffs(o, t, &diffNames);
    }
    // Every selected road takes a preset (a multi-selection applies to all).
    auto applyPreset = [&](const roadpresets::Preset& p) {
        std::vector<int> targets;
        for (int idx : selection_)
            if (idx >= 0 && idx < (int)project_.objects().size() &&
                project_.objects()[(size_t)idx].type == PrimitiveType::Road)
                targets.push_back(idx);
        if (targets.empty()) targets.push_back(selectedObject_);
        roadpresets::ensureMaterials(project_.dir, p);
        materialAssetScanTime_ = -1.0;  // a material written on demand is a new asset
        for (int idx : targets) {
            SceneObject& r = idx == selectedObject_ ? o : project_.objects()[(size_t)idx];
            roadpresets::apply(p, r, upm);
        }
        roadPresetPick_ = p.key;
        changed = true;
        statusMessage_ = "Road preset " + p.name + " applied to " + std::to_string(targets.size()) + " road(s)";
    };

    const float fs = ImGui::GetFontSize();
    ui::BeginInsetCard("##roadpresetcard", IM_TRUNC(fs * 0.55f));
    {
        std::string shown = best ? best->name : "Custom";
        if (ui::BeginCombo("Preset", shown.c_str())) {
            for (const roadpresets::Preset* pp : roadpresets::all(mine)) {
                std::string label = pp->name;
                if (pp == best && bestDiffs == 0) label += "  (this road)";
                if (ImGui::Selectable((label + "##rpp-" + pp->key).c_str(), pp == best)) applyPreset(*pp);
                if (ImGui::IsItemHovered() && !pp->help.empty()) ImGui::SetTooltip("%s", pp->help.c_str());
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("Picking a preset sets every road field it owns - width, the\n"
                              "surface and junction materials (generated into\n"
                              "res/materials/roads when missing), kerbs, pavement, details,\n"
                              "street furniture, markings, kind and tracks, priority, grip,\n"
                              "spill and soft edge - on every selected road. Points and\n"
                              "bridge heights are kept.");
    }
    {
        char chip[48];
        if (bestDiffs == 0) std::snprintf(chip, sizeof chip, "matches");
        else std::snprintf(chip, sizeof chip, "%d change%s", bestDiffs, bestDiffs == 1 ? "" : "s");
        ui::StatePill(chip, bestDiffs == 0 ? 4 : 1);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip) && best) {
            std::string list;
            for (const std::string& n : diffNames) list += "\n  " + n;
            if (diffNames.empty()) ImGui::SetTooltip("Exactly the %s preset.", best->name.c_str());
            else ImGui::SetTooltip("Differs from %s in:%s", best->name.c_str(), list.c_str());
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(bestDiffs == 0 || !best);
        if (ui::Link("Revert") && best) applyPreset(*best);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled) && best)
            ImGui::SetTooltip("Apply %s again: every field back to the preset.", best->name.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("\xc2\xb7");
        ImGui::SameLine();
        if (ui::Link("Save as preset...")) {
            std::snprintf(roadPresetName_, sizeof roadPresetName_, "%s", o.name.c_str());
            ImGui::OpenPopup("##roadsavepreset");
        }
    }
    if (ImGui::BeginPopup("##roadsavepreset")) {
        ImGui::TextUnformatted("Keep this road's look as a project preset");
        ImGui::SetNextItemWidth(scaled(220));
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::InputText("Name##roadpresetname", roadPresetName_, sizeof roadPresetName_);
        if (ImGui::Button("Save preset")) {
            roadpresets::Preset p = roadpresets::fromRoad(o, roadPresetName_);
            bool replaced = false;
            for (roadpresets::Preset& q : mine)
                if (q.key == p.key) {
                    q = p;
                    replaced = true;
                }
            if (!replaced) mine.push_back(p);
            // The Preferences window edits a copy of the settings and writes it
            // back whenever it differs - keep that copy in step, or an open
            // Preferences window would drop the preset again on its next frame.
            prefSettings_.roadPresets = mine;
            roadPresetPick_ = p.key;
            changed = true;
            statusMessage_ = "Road preset \"" + p.name + "\" saved in the project";
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("Saved in the .tyra; listed after the built-ins here and in the\n"
                              "Draw road tool. A preset of the same name is replaced.");
        if (best && bestDiffs == 0 && !mine.empty() && best >= &mine.front() && best <= &mine.back()) {
            ImGui::SameLine();
            if (ui::DangerButton("Delete preset")) {
                const std::string name = best->name;
                mine.erase(mine.begin() + (best - &mine.front()));
                prefSettings_.roadPresets = mine;
                changed = true;
                statusMessage_ = "Road preset \"" + name + "\" deleted";
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }
    ui::EndInsetCard();
    return changed;
}

bool App::drawRoadProperties(SceneObject& o) {
    bool committed = false;
    if (drawRoadPresetControls(o)) committed = true;
    ImGui::Spacing();

    // Tabs by goal; the bar is only a selector (the solid objects' pattern):
    // contents are drawn after it at window scope, so every field keeps its
    // ID and --ui-script name. "###" ids keep a tab while its caption changes
    // with the kind (Road/Track, Junctions/Crossings).
    const bool rail = o.roadKind == roadrail::kRail;
    if (rail && roadTab_ == 1) roadTab_ = 0;
    if (ui::BeginQuietTabBar("##roadtabs")) {
        const bool force = roadTabFrame_ != ImGui::GetFrameCount() - 1;
        const char* tabs[4] = {rail ? "Track###rtab0" : "Road###rtab0", "Edges###rtab1", "Furniture###rtab2",
                               rail ? "Crossings###rtab3" : "Junctions###rtab3"};
        for (int i = 0; i < 4; ++i) {
            const bool off = rail && i == 1;
            if (off) ImGui::BeginDisabled();
            const ImGuiTabItemFlags f = force && i == roadTab_ ? ImGuiTabItemFlags_SetSelected : 0;
            if (ui::QuietTab(tabs[i], i == roadTab_, f) && !force && !off) roadTab_ = i;
            if (off) {
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    ImGui::SetTooltip("A railway has no kerbs, pavement or wear decals.");
            }
        }
        ui::EndQuietTabBar();
        roadTabFrame_ = ImGui::GetFrameCount();
    }
    ImGui::Spacing();
    switch (roadTab_) {
        case 1: committed |= drawRoadTabEdges(o); break;
        case 2: committed |= drawRoadFurniture(o); break;
        case 3: committed |= drawRoadTabJunctions(o); break;
        default: committed |= drawRoadTabRoad(o); break;
    }
    return committed;
}

bool App::drawRoadTabRoad(SceneObject& o) {
    bool committed = false;
    const theme::Semantics& sem = theme::semantics();
    const Paint pt = paint();
    const float upm = project_.settings.unitsPerMeter > 0.0f ? project_.settings.unitsPerMeter : 1.0f;
    const float fs = ImGui::GetFontSize();
    const float gap = IM_TRUNC(ImGui::GetStyle().ItemSpacing.x);
    const bool rail = o.roadKind == roadrail::kRail, tram = o.roadKind == roadrail::kTram;

    // --- Kind: three picture tiles -------------------------------------------
    {
        const float W = ImGui::GetContentRegionAvail().x;
        const float tw = IM_TRUNC((W - gap * 2.0f) / 3.0f), th = IM_TRUNC(fs * 3.6f);
        static const char* const kLabels[] = {"Road kind", "Railway kind", "Tram street kind"};
        static const char* const kText[] = {"Road", "Railway", "Tram street"};
        static const ui::TileDrawFn kDraw[] = {kindRoad, kindRail, kindTram};
        for (int k = 0; k < 3; ++k) {
            if (k) ImGui::SameLine(0.0f, gap);
            if (ui::OptionTile(kLabels[k], kText[k], kDraw[k], o.roadKind == k, k == 2 ? W - (tw + gap) * 2.0f : tw, th) &&
                o.roadKind != k) {
                const int before = o.roadKind;
                o.roadKind = k;
                if (k == roadrail::kRail && before != roadrail::kRail)
                    roadpresets::applyRailway(o, project_.dir, upm);
                committed = true;
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                ImGui::SetTooltip(k == 0   ? "A street."
                                  : k == 1 ? "The strip is a ballast bed with sleepers and steel rails on it;\n"
                                             "where it crosses a road the rails turn flush and get a crossing\n"
                                             "panel. Choosing it sets the ballast materials (made here if the\n"
                                             "project has none), width, priority Track, no spill, kerbs or\n"
                                             "markings. Rails are vertex colour only - no texture, no VRAM."
                                           : "A street with rails set flush into it.");
        }
    }
    ImGui::Spacing();

    // --- Cross-section --------------------------------------------------------
    ui::BeginInsetCard("##roadxsec");
    captionRow("Cross-section", nullptr, rail ? "width follows the tracks" : "drag an edge to resize");
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float W = ImGui::GetContentRegionAvail().x, H = picHeight(rail ? 5.4f : 6.2f);
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(W, H));
        const float wM = o.roadWidth / upm;
        const bool kerb = o.roadKerb && !rail;
        const float kerbM = kerb ? o.roadKerbWidth / upm : 0.0f;
        const float pavM = kerb ? o.roadPavement / upm : 0.0f;
        const float spanM = wM + 2.0f * (kerbM + pavM) + (kerb ? 0.0f : wM * 0.25f);
        // The scale is frozen while an edge is dragged, so the handle stays under
        // the cursor instead of the drawing zooming away from it.
        ImGuiStorage* st = ImGui::GetStateStorage();
        const ImGuiID scaleKey = ImGui::GetID("##xsecscale");
        float s = (W * 0.86f) / std::max(spanM, 1.0f);
        if (st->GetBool(ImGui::GetID("##xsecdrag"), false)) s = st->GetFloat(scaleKey, s);
        st->SetFloat(scaleKey, s);
        const float cx = p0.x + W * 0.5f, yRoad = p0.y + H * 0.56f, yGround = p0.y + H;
        const float slab = IM_TRUNC(fs * 0.75f);  // the surface drawn as a slab, not a hairline
        const float hw = wM * 0.5f * s;
        dl->AddRectFilled(ImVec2(p0.x, yRoad + slab), ImVec2(p0.x + W, yGround), pt.ground);
        if (rail) {
            // Ballast bed (a trapezoid), the sleepers and the rails of each track.
            const float bedTop = yRoad - fs * 0.5f;
            dl->AddQuadFilled(ImVec2(cx - hw, yRoad + slab), ImVec2(cx - hw + fs * 0.9f, bedTop),
                              ImVec2(cx + hw - fs * 0.9f, bedTop), ImVec2(cx + hw, yRoad + slab), pt.ballast);
            const float spacing = roadrail::kTrackSpacingRail;  // metres (drawing only)
            const int tracks = std::clamp(o.roadTracks, 1, 2);
            const float gaugeM = o.roadRailGauge / upm;
            for (int t = 0; t < tracks; ++t) {
                const float tc = cx + (tracks == 2 ? (t == 0 ? -0.5f : 0.5f) * spacing * s : 0.0f);
                const float sl = roadtex::kSleeperLength * 0.5f * s;
                dl->AddRectFilled(ImVec2(tc - sl, bedTop - fs * 0.18f), ImVec2(tc + sl, bedTop), pt.sleeper);
                for (int side = -1; side <= 1; side += 2) {
                    const float rx = tc + side * gaugeM * 0.5f * s;
                    dl->AddRectFilled(ImVec2(rx - fs * 0.12f, bedTop - fs * 0.55f), ImVec2(rx + fs * 0.12f, bedTop - fs * 0.18f),
                                      pt.rail);
                }
                if (t == 0) {
                    char g[16];
                    std::snprintf(g, sizeof g, "%.3f", gaugeM);
                    ui::DimensionLine(dl, ImVec2(tc - gaugeM * 0.5f * s, bedTop - fs * 1.3f),
                                      ImVec2(tc + gaugeM * 0.5f * s, bedTop - fs * 1.3f), g, pt.accent);
                }
            }
            if (tracks == 2) {
                char d[48];
                std::snprintf(d, sizeof d, "tracks %.1f m apart", roadrail::kTrackSpacingRail);
                const ImVec2 ds = ImGui::CalcTextSize(d);
                dl->AddText(ImVec2(cx - ds.x * 0.5f, yGround - ds.y - fs * 0.2f), pt.dim, d);
            }
        } else {
            // Pavement, kerb, asphalt - the kerb drawn at its true height x2 so a
            // 15 cm step is visible at panel scale.
            const float kerbH = kerb ? std::max(o.roadKerbHeight / upm * s * 3.0f, 3.0f)  /* x3: a kerb reads at panel scale */ : 0.0f;
            dl->AddRectFilled(ImVec2(cx - hw, yRoad), ImVec2(cx + hw, yRoad + slab), pt.asphalt);
            for (int side = -1; side <= 1; side += 2) {
                const float e = cx + side * hw;
                if (kerb) {
                    const float k1 = e + side * kerbM * s;
                    dl->AddRectFilled(ImVec2(std::min(e, k1), yRoad - kerbH), ImVec2(std::max(e, k1), yRoad + slab),
                                      pt.kerb);
                    if (pavM > 0.0f) {
                        const float p1 = k1 + side * pavM * s;
                        dl->AddRectFilled(ImVec2(std::min(k1, p1), yRoad - kerbH + 1.0f),
                                          ImVec2(std::max(k1, p1), yRoad + slab), pt.pavement);
                        char t[24];
                        std::snprintf(t, sizeof t, "%.1f m", pavM);
                        const ImVec2 ts = ImGui::CalcTextSize(t);
                        dl->AddText(ImVec2((k1 + p1) * 0.5f - ts.x * 0.5f, yRoad - kerbH - ts.y - fs * 0.25f), pt.dim, t);
                    }
                }
            }
            // Lane lines, where the texture says how many lanes it paints.
            const int lanes = roadTextureLanes(o.roadTexture);
            if (lanes >= 2) {
                for (int i = 1; i < lanes; ++i) {
                    const float x = cx - hw + (2.0f * hw) * (float)i / (float)lanes;
                    const bool centre = i * 2 == lanes;
                    dl->AddLine(ImVec2(x - fs * 0.35f, yRoad + slab * 0.2f), ImVec2(x + fs * 0.35f, yRoad + slab * 0.2f),
                                centre ? u32(sem.warn) : pt.line, 2.0f);
                }
            }
            if (tram) {
                const int tracks = std::clamp(o.roadTracks, 1, 2);
                for (int t = 0; t < tracks; ++t) {
                    const float tc = cx + (tracks == 2 ? (t == 0 ? -0.5f : 0.5f) * roadrail::kTrackSpacingTram * s : 0.0f);
                    for (int side = -1; side <= 1; side += 2) {
                        const float rx = tc + side * o.roadRailGauge / upm * 0.5f * s;
                        dl->AddRectFilled(ImVec2(rx - 1.5f, yRoad), ImVec2(rx + 1.5f, yRoad + slab * 0.35f), pt.rail);
                    }
                }
            }
            char wt[24];
            std::snprintf(wt, sizeof wt, "%.1f m", wM);
            const float dimY = p0.y + H * 0.22f;
            dl->AddLine(ImVec2(cx - hw, dimY - fs * 0.4f), ImVec2(cx - hw, yRoad - fs * 0.3f), pt.accent);
            dl->AddLine(ImVec2(cx + hw, dimY - fs * 0.4f), ImVec2(cx + hw, yRoad - fs * 0.3f), pt.accent);
            ui::DimensionLine(dl, ImVec2(cx - hw + 2.0f, dimY), ImVec2(cx + hw - 2.0f, dimY), wt, pt.accent);
            if (lanes >= 2) {
                char lt[32];
                const int half = lanes / 2;
                for (int side = -1; side <= 1; side += 2) {
                    const int n = side < 0 ? half : lanes - half;
                    std::snprintf(lt, sizeof lt, "%d lane%s", n, n == 1 ? "" : "s");
                    const ImVec2 ls = ImGui::CalcTextSize(lt);
                    dl->AddText(ImVec2(cx + side * hw * 0.5f - ls.x * 0.5f, yRoad + slab + fs * 0.35f), pt.dim, lt);
                }
            }
            // The two edge handles: dragging either moves both edges (the road
            // is symmetric about its spline).
            bool dragging = false;
            for (int side = -1; side <= 1; side += 2) {
                const ImVec2 hc(cx + side * hw, yRoad);
                if (ui::DragHandle(side < 0 ? "Road edge left" : "Road edge right", hc, IM_TRUNC(fs * 0.38f))) {
                    const float dx = ImGui::GetIO().MouseDelta.x * (float)side;
                    o.roadWidth = std::clamp(o.roadWidth + 2.0f * dx / s * upm, 1.0f, 24.0f);
                }
                dragging |= ImGui::IsItemActive();
                if (ImGui::IsItemDeactivated()) committed = true;
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip) && !ImGui::IsItemActive())
                    ImGui::SetTooltip("Drag to change the width (both edges move).");
            }
            st->SetBool(ImGui::GetID("##xsecdrag"), dragging);
        }
    }
    if (rail || tram) {
        static const char* const kTracks[] = {"One", "Two"};
        int ti = std::clamp(o.roadTracks, 1, 2) - 1;
        if (ui::ChoiceRow("Tracks", kTracks, 2, &ti)) {
            o.roadTracks = ti + 1;
            if (rail) roadpresets::applyRailway(o, project_.dir, upm, false);
            committed = true;
        }
        prefHelp(rail ? "One or two tracks on the bed, 4 m apart (the ballast\nmaterial and width follow)."
                      : "One or two tracks down the street, 3 m apart.");
        // Gauge: the common ones as one click, the exact value beside them.
        static const char* const kGauges[] = {"Standard", "Metre", "Narrow"};
        static const float kGaugeM[] = {1.435f, 1.0f, 0.762f};
        int gi = -1;
        for (int i = 0; i < 3; ++i)
            if (std::fabs(o.roadRailGauge - kGaugeM[i] * upm) < 0.002f * upm) gi = i;
        const float exactW = IM_TRUNC(fs * 4.2f);
        ui::SetNextRowFieldTrim(exactW + gap);
        if (ui::ChoiceRow("Gauge", kGauges, 3, &gi) && gi >= 0) {
            o.roadRailGauge = kGaugeM[gi] * upm;
            committed = true;
        }
        prefHelp("Between the rails' inner faces: standard 1.435 m, metre gauge\n"
                 "1.0 m, narrow 0.762 m. The rail profile scales with it.");
        ImGui::SameLine(0.0f, gap);
        ImGui::SetNextItemWidth(exactW);
        if (ImGui::DragFloat("##gaugeexact", &o.roadRailGauge, 0.005f, 0.3f, 3.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp)) {
        }
        committed |= ImGui::IsItemDeactivatedAfterEdit();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("Exact gauge, world units (0.3 - 3).");
    }
    {
        // The two numbers the drawing does not carry by itself.
        float wM = o.roadWidth / upm;
        ui::KitDrag f[2];
        f[0].v = &wM;
        f[0].speed = 0.05f;
        f[0].vmin = 1.0f / upm;
        f[0].vmax = 24.0f / upm;
        f[0].fmt = "Width %.1f m";
        f[0].name = "Width";
        f[1].v = &o.roadGrip;
        f[1].speed = 0.01f;
        f[1].vmin = 0.1f;
        f[1].vmax = 1.5f;
        f[1].fmt = "Grip %.2f x";
        f[1].name = "Surface grip";
        if (ui::DragRow("##roadnumbers", f, 2)) o.roadWidth = std::clamp(wM * upm, 1.0f, 24.0f);
        committed |= ImGui::IsItemDeactivatedAfterEdit();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("WIDTH - full width of the surface.\n"
                              "GRIP - tyre grip on this road, multiplying every vehicle's own:\n"
                              "1 = asphalt, ~0.7 gravel, ~0.3 ice. A junction takes the lower\n"
                              "of its two roads; terrain off the road uses each vehicle's\n"
                              "Off-road grip instead.");
        const int lanes = rail ? -1 : roadTextureLanes(o.roadTexture);
        if (lanes >= 1) dimText("Lanes: %d, read from the generated texture's recipe", lanes);
    }
    ui::EndInsetCard();
    ImGui::Spacing();

    // --- Surface materials ----------------------------------------------------
    if (captionRow(rail ? "Bed" : "Surface", "Generate texture...")) openRoadTextureGenerator(o.roadTexture);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("Tools > Road Texture Generator");
    {
        const float W = ImGui::GetContentRegionAvail().x, tw = IM_TRUNC((W - gap) * 0.5f);
        if (roadMaterialTile("Surface material", rail ? "Bed" : "Road", o.roadTexture, "<none - untextured grey>", tw))
            committed = true;
        ImGui::SameLine(0.0f, gap);
        if (roadMaterialTile("Intersection material", rail ? "Crossings" : "Junctions", o.roadIntersectionTexture,
                             "<none>", W - tw - gap))
            committed = true;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("When two roads of the SAME priority meet and both name this\n"
                              "material, TyraX generates a terrain-hugging junction patch at\n"
                              "build. Different priorities never make a patch: the higher road\n"
                              "runs through.");
    }
    ImGui::Spacing();

    // --- Where it meets other roads ---------------------------------------------
    captionRow("Where it meets other roads");
    {
        static const char* const kRanks[] = {"Track", "Local", "Main"};
        if (ui::ChoiceRow("Priority", kRanks, 3, &o.roadRank)) committed = true;
        prefHelp("Which road wins a crossing. A higher priority runs straight\n"
                 "through and covers the lower one - a mud track stops at the\n"
                 "asphalt's edge instead of fighting it. Equal priorities meet in\n"
                 "an intersection-material junction.");
        static const char* const kWhat[] = {
            "Stops at every Local or Main road it meets; meets other tracks in a junction.",
            "Runs through tracks, stops at Main roads; meets Local roads in a junction.",
            "Runs straight through Local roads and tracks; meets Main roads in a junction."};
        dimText("%s", kWhat[std::clamp(o.roadRank, 0, 2)]);
        static const char* const kMarks[] = {"None", "Stop lines", "+ Zebras"};
        if (ui::ChoiceRow("Markings", kMarks, 3, &o.roadMarkings, !rail)) committed = true;
        prefHelp(rail ? "A railway is never painted: the planner turns its markings off."
                      : "Paint at this road's junctions. A stop line marks where it\n"
                        "gives way; zebras cross each arm of a 3+-way junction.");
        if (rail) dimText("A railway is never kerbed or painted; where it crosses a road the rails turn flush and a "
                          "crossing panel is laid.");
    }
    ImGui::Spacing();

    // --- Shape ------------------------------------------------------------------
    captionRow("Shape");
    ui::BeginInsetCard("##roadshape");
    {
        const int np = roadgen::controlCount(o.roadPoints);
        const bool loop = roadgen::isClosed(o.roadPoints);
        const char* editLabel = roadEdit_ ? "Stop editing (Esc)" : "Edit in viewport";
        const float bw = ImGui::CalcTextSize(editLabel).x + ImGui::GetStyle().FramePadding.x * 2.0f;
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float W = ImGui::GetContentRegionAvail().x;
        ImGui::BeginGroup();
        {
            ui::FontScope bold(ui::boldFont());
            ImGui::TextUnformatted("Points");
        }
        ImGui::TextDisabled("%d points \xc2\xb7 %s", np, loop ? "closed loop" : "open ends");
        ImGui::EndGroup();
        const float gh = ImGui::GetItemRectSize().y;
        ImGui::SetCursorScreenPos(ImVec2(p.x + W - bw, p.y + (gh - ImGui::GetFrameHeight()) * 0.5f));
        if (ui::SecondaryButton(editLabel)) {
            roadEdit_ = !roadEdit_;
            if (roadEdit_ && roadDraw_.active) stopRoadDraw();
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("Click the ground to APPEND a point, click a point to DRAG it,\n"
                              "click the line between points to INSERT one there.\n"
                              "Shift+click a marker to DELETE it. Drag the final point onto\n"
                              "the first and release to CLOSE a loop. Esc stops; Ctrl+Z\n"
                              "undoes each operation.");
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + gh));
        ImGui::Dummy(ImVec2(0, 0));
        ImGui::Separator();
        if (ui::Switch("Bridge (Deck follows each point's height)", &o.roadBridge)) {
            roadbridge::onPointsReshaped(o);
            committed = true;
        }
        prefHelp("The deck runs through each point's Height above the terrain\n"
                 "instead of hugging the ground between points: it spans a dip\n"
                 "by itself, a raised point makes an overpass. Parapets, piers\n"
                 "and abutments where it stands clear of the ground.");
        if (o.roadBridge && np >= 2) {
            // The deck profile: one handle per point, dragged up and down.
            roadbridge::onPointsReshaped(o);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const float PW = ImGui::GetContentRegionAvail().x, PH = picHeight(4.2f);
            const ImVec2 q = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(PW, PH));
            float top = 1.0f;
            for (float h : o.roadHeights) top = std::max(top, h);
            top = std::max(top * 1.25f, 2.0f);
            const float x0 = q.x + fs * 0.6f, x1 = q.x + PW - fs * 0.6f;
            const float yb = q.y + PH - fs * 0.5f, yt = q.y + fs * 0.9f;
            dl->AddLine(ImVec2(q.x, yb), ImVec2(q.x + PW, yb), u32(sem.border));
            auto px = [&](int i) { return np == 1 ? x0 : x0 + (x1 - x0) * (float)i / (float)(np - 1); };
            auto py = [&](float h) { return yb - (yb - yt) * (h / top); };
            for (int i = 0; i + 1 < np; ++i)
                dl->AddLine(ImVec2(px(i), py(o.roadHeights[(size_t)i])), ImVec2(px(i + 1), py(o.roadHeights[(size_t)i + 1])),
                            pt.accent, 2.0f);
            int hi = 0;
            for (int i = 0; i < np; ++i)
                if (o.roadHeights[(size_t)i] > o.roadHeights[(size_t)hi]) hi = i;
            for (int i = 0; i < np; ++i) {
                char hl[32];
                std::snprintf(hl, sizeof hl, "Height point %d", i + 1);
                if (ui::DragHandle(hl, ImVec2(px(i), py(o.roadHeights[(size_t)i])), IM_TRUNC(fs * 0.3f),
                                   ImGuiMouseCursor_ResizeNS)) {
                    float& h = o.roadHeights[(size_t)i];
                    h = std::clamp(h - ImGui::GetIO().MouseDelta.y * top / (yb - yt), 0.0f, roadbridge::kMaxHeight);
                }
                if (ImGui::IsItemDeactivated()) committed = true;
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip) || ImGui::IsItemActive())
                    ImGui::SetTooltip("Point %d: deck %.1f m above the terrain", i + 1,
                                      o.roadHeights[(size_t)i] / upm);
            }
            char tl[24];
            std::snprintf(tl, sizeof tl, "%.1f m", o.roadHeights[(size_t)hi] / upm);
            const ImVec2 ts = ImGui::CalcTextSize(tl);
            dl->AddText(ImVec2(std::clamp(px(hi) - ts.x * 0.5f, q.x, q.x + PW - ts.x), py(o.roadHeights[(size_t)hi]) - ts.y - fs * 0.45f),
                        u32(sem.text), tl);
        }
        bool loopNow = loop;
        if (np >= 3) {
            if (ui::Switch("Closed loop", &loopNow)) {
                if (loopNow) {
                    const float x = o.roadPoints[0], z = o.roadPoints[1];
                    o.roadPoints.insert(o.roadPoints.end(), {x, z});
                } else {
                    o.roadPoints.resize(o.roadPoints.size() - 2);
                }
                roadbridge::onPointsReshaped(o);
                committed = true;
            }
        }
        // The exact numbers: the point table, folded (the viewport is the
        // usual way to move them).
        char sum[48];
        std::snprintf(sum, sizeof sum, "%d x/z%s", np, o.roadBridge ? " + heights" : "");
        if (ui::Disclosure("Point list", sum)) {
            int removeAt = -1, insertAfter = -1;
            const bool closed = roadgen::isClosed(o.roadPoints);
            const int n = roadgen::controlCount(o.roadPoints);
            for (int i = 0; i < n; ++i) {
                ImGui::PushID(i);
                float* xz = &o.roadPoints[(size_t)i * 2];
                ImGui::SetNextItemWidth(scaled(150));
                if (ui::DragFloat2(("Point " + std::to_string(i + 1)).c_str(), xz, 0.25f, 0.0f, 0.0f, "%.1f")) {
                    if (closed && i == 0) {
                        o.roadPoints[o.roadPoints.size() - 2] = o.roadPoints[0];
                        o.roadPoints.back() = o.roadPoints[1];
                    }
                    roadbridge::onPointsReshaped(o);
                }
                committed |= ImGui::IsItemDeactivatedAfterEdit();
                if (o.roadBridge) {
                    roadbridge::onPointsReshaped(o);
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(scaled(56));
                    ui::DragFloat("Height", &o.roadHeights[(size_t)i], 0.1f, 0.0f, roadbridge::kMaxHeight, "%.1f");
                    committed |= ImGui::IsItemDeactivatedAfterEdit();
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("+")) insertAfter = i;
                ImGui::SameLine();
                if (n > (closed ? 3 : 2) && ImGui::SmallButton("-")) removeAt = i;
                ImGui::PopID();
            }
            if (insertAfter >= 0) {
                // Midway to the next point (or extended past the end).
                const size_t at = (size_t)(insertAfter + 1) * 2;
                float nx, nz;
                if (insertAfter + 1 < n || closed) {
                    nx = 0.5f * (o.roadPoints[at - 2] + o.roadPoints[at]);
                    nz = 0.5f * (o.roadPoints[at - 1] + o.roadPoints[at + 1]);
                } else {
                    nx = 2.0f * o.roadPoints[at - 2] - o.roadPoints[at - 4];
                    nz = 2.0f * o.roadPoints[at - 1] - o.roadPoints[at - 3];
                }
                o.roadPoints.insert(o.roadPoints.begin() + (long)at, {nx, nz});
                roadbridge::onPointInserted(o, insertAfter + 1);
                committed = true;
            }
            if (removeAt >= 0 && roadgen::removeControl(o.roadPoints, removeAt)) {
                roadbridge::onPointRemoved(o, removeAt);
                committed = true;
            }
        }
        ImGui::Spacing();
        if (ui::SecondaryButton("Draw road...")) {
            roadDraw_.preset = roadPresetPick_.empty() ? roadDraw_.preset : roadPresetPick_;
            startRoadDraw();
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("The Draw road tool (8), with this road's preset.");
        ImGui::SameLine();
        ImGui::BeginDisabled(o.roadBridge);
        if (ui::SecondaryButton("Align terrain to road")) alignTerrainToRoad(selectedObject_);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip(o.roadBridge ? "Not for a bridge: it would fill the gap under the deck."
                                           : "Flattens the heightfield to the road's interpolated line, with\n"
                                             "a smooth shoulder falloff. Undoable like any edit.");
    }
    ui::EndInsetCard();
    ImGui::Spacing();

    // --- Advanced: what a person tuning cost or crossings needs -------------
    {
        const SceneObject defaults;
        int changedN = 0;
        changedN += !nearly(o.roadSampleStep, defaults.roadSampleStep);
        changedN += !nearly(o.roadSpill, defaults.roadSpill);
        char sum[64];
        if (changedN) std::snprintf(sum, sizeof sum, "sampling, spill, position \xc2\xb7 %d changed", changedN);
        else std::snprintf(sum, sizeof sum, "sampling, spill, position");
        if (ui::Disclosure("Advanced##road", sum)) {
            if (ui::SliderFloat("Longitudinal spacing", &o.roadSampleStep, 1.0f, 2.0f, "%.2f m")) {
            }
            committed |= ImGui::IsItemDeactivatedAfterEdit();
            prefHelp("Distance between geometry rows along the spline. 1 m follows\n"
                     "sharp terrain folds most closely; up to 2 m reduces road\n"
                     "triangles and VU1 packages. Inspect crests and tight bends.");
            if (metresSlider("Spill onto higher roads", &o.roadSpill, upm, 0.0f, 8.0f, "%.1f m")) {
            }
            committed |= ImGui::IsItemDeactivatedAfterEdit();
            prefHelp("Where this road crosses a higher-priority one, its surface\n"
                     "carries on over the other road's edge for this far and fades\n"
                     "out - mud trailed onto the asphalt. Grip fades with it. 0 = a\n"
                     "clean edge. No effect against equal or lower priorities.");
            // The object's own position is cosmetic for a road (its points are
            // absolute world XZ): kept editable here, out of the way.
            ui::AxisDrag3("Position", o.position, 0.1f);
            committed |= ImGui::IsItemDeactivatedAfterEdit();
            prefHelp("Where the road's marker sits. Cosmetic: the points are world\n"
                     "positions, so moving this does not move the road.");
        }
    }
    ImGui::Spacing();
    ui::StatusLine(0, o.roadBridge ? "Bridge: deck, parapets and piers baked on the host at build"
                                   : "Tessellated onto the terrain at build");
    return committed;
}

bool App::drawRoadTabEdges(SceneObject& o) {
    bool committed = false;
    const theme::Semantics& sem = theme::semantics();
    const Paint pt = paint();
    const float upm = project_.settings.unitsPerMeter > 0.0f ? project_.settings.unitsPerMeter : 1.0f;
    const float fs = ImGui::GetFontSize();
    const float gap = IM_TRUNC(ImGui::GetStyle().ItemSpacing.x);
    if (o.roadKind == roadrail::kRail) {
        ui::Note(3, "A railway has no kerbs, pavement or wear decals.");
        return false;
    }

    // --- Edge detail: pavement | kerb | road, true scale --------------------
    ui::BeginInsetCard("##roadedge");
    captionRow("Road edge \xc2\xb7 both sides", nullptr, o.roadKerb ? "drag the handles" : nullptr);
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float W = ImGui::GetContentRegionAvail().x, H = picHeight(6.0f);
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(W, H));
        const float kerbW = o.roadKerbWidth / upm, kerbH = o.roadKerbHeight / upm, pav = o.roadPavement / upm;
        ImGuiStorage* st = ImGui::GetStateStorage();
        const ImGuiID scaleKey = ImGui::GetID("##edgescale"), dragKey = ImGui::GetID("##edgedrag");
        const float spanM = std::max(pav, 1.0f) + kerbW + 2.5f;
        float s = (W * 0.9f) / spanM;
        if (st->GetBool(dragKey, false)) s = st->GetFloat(scaleKey, s);
        st->SetFloat(scaleKey, s);
        const float yRoad = p0.y + H * 0.66f, yGround = p0.y + H;
        const float xKerb = p0.x + W * 0.05f + std::max(pav, 1.0f) * s;  // kerb's road-side face
        dl->AddRectFilled(ImVec2(p0.x, yRoad + fs * 0.6f), ImVec2(p0.x + W, yGround), pt.ground);
        dl->AddRectFilled(ImVec2(xKerb, yRoad), ImVec2(p0.x + W, yRoad + fs * 0.6f), pt.asphalt);
        bool dragging = false;
        if (o.roadKerb) {
            const float kh = std::max(kerbH * s * 2.0f, 3.0f);  // x2: the step reads at panel scale
            const float xIn = xKerb - kerbW * s;  // kerb's back = pavement's front
            dl->AddRectFilled(ImVec2(xIn, yRoad - kh), ImVec2(xKerb, yRoad + fs * 0.6f), pt.kerb);
            if (pav > 0.0f) {
                const float xOut = xIn - pav * s;
                dl->AddRectFilled(ImVec2(xOut, yRoad - kh + 1.0f), ImVec2(xIn, yRoad + fs * 0.6f), pt.pavement);
                // Slab joints every 2 m (the pavement-slabs tile).
                for (float m = 2.0f; m < pav; m += 2.0f)
                    dl->AddLine(ImVec2(xIn - m * s, yRoad - kh + 1.0f), ImVec2(xIn - m * s, yRoad + fs * 0.6f), pt.ground);
                char t[24];
                std::snprintf(t, sizeof t, "%.1f m", pav);
                const float dy = yRoad - kh - fs * 1.6f;
                dl->AddLine(ImVec2(xOut, dy - fs * 0.5f), ImVec2(xOut, yRoad - kh - fs * 0.2f), pt.accent);
                dl->AddLine(ImVec2(xIn, dy - fs * 0.5f), ImVec2(xIn, yRoad - kh - fs * 0.2f), pt.accent);
                ui::DimensionLine(dl, ImVec2(xOut + 2.0f, dy), ImVec2(xIn - 2.0f, dy), t, pt.accent);
                if (ui::DragHandle("Pavement edge", ImVec2(xOut, yRoad - kh), IM_TRUNC(fs * 0.36f))) {
                    o.roadPavement = std::clamp(o.roadPavement - ImGui::GetIO().MouseDelta.x / s * upm, 0.0f, 6.0f);
                }
                dragging |= ImGui::IsItemActive();
                if (ImGui::IsItemDeactivated()) committed = true;
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip) && !ImGui::IsItemActive())
                    ImGui::SetTooltip("Drag to change the pavement's width.");
            }
            // The kerb's top width: on the dimension row, just past the
            // pavement's arrow, so the two never share a pixel.
            char kt[24];
            std::snprintf(kt, sizeof kt, "top %.2f", kerbW);
            const ImVec2 ks = ImGui::CalcTextSize(kt);
            dl->AddText(ImVec2(xIn + fs * 0.35f, yRoad - kh - fs * 1.6f - ks.y * 0.5f), u32(sem.text), kt);
            char ht[24];
            std::snprintf(ht, sizeof ht, "%.2f", kerbH);
            ui::DimensionLine(dl, ImVec2(xKerb + fs * 1.2f, yRoad - kh), ImVec2(xKerb + fs * 1.2f, yRoad), nullptr, pt.accent);
            dl->AddLine(ImVec2(xKerb, yRoad - kh), ImVec2(xKerb + fs * 1.6f, yRoad - kh), pt.accent);
            dl->AddText(ImVec2(xKerb + fs * 1.6f, yRoad - kh * 0.5f - ImGui::GetTextLineHeight() * 0.5f), u32(sem.text), ht);
            if (ui::DragHandle("Kerb top", ImVec2((xIn + xKerb) * 0.5f, yRoad - kh), IM_TRUNC(fs * 0.36f),
                               ImGuiMouseCursor_ResizeNS)) {
                o.roadKerbHeight = std::clamp(o.roadKerbHeight - ImGui::GetIO().MouseDelta.y / (s * 2.0f) * upm, 0.02f, 0.5f);
            }
            dragging |= ImGui::IsItemActive();
            if (ImGui::IsItemDeactivated()) committed = true;
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip) && !ImGui::IsItemActive())
                ImGui::SetTooltip("Drag to change the kerb's height.");
            if (o.roadDetails > 0.0f) {
                // A gully at the kerb face: what kerbed roads get among their details.
                dl->AddRectFilled(ImVec2(xKerb + fs * 0.3f, yRoad), ImVec2(xKerb + fs * 1.3f, yRoad + fs * 0.25f),
                                  u32(mixc(sem.field, ImVec4(0, 0, 0, 1), 0.4f)));
            }
        } else {
            const char* t = "No kerb: the road meets the ground";
            const ImVec2 ts = ImGui::CalcTextSize(t);
            dl->AddText(ImVec2(p0.x + (W - ts.x) * 0.5f, p0.y + H * 0.25f), pt.dim, t);
        }
        st->SetBool(dragKey, dragging);
    }
    ui::EndInsetCard();
    ImGui::Spacing();

    // --- Kerbs ---------------------------------------------------------------
    // A bridge has parapets, not kerbs: the kerb and pavement controls are
    // greyed on one (the values are kept for when it stops being a bridge).
    const bool bridge = o.roadBridge;
    ui::BeginInsetCard("##roadkerbs");
    if (ui::CardHeader("Kerbs", bridge ? "A bridge has parapets, not kerbs" : "Solid: wheels ride up, walkers step onto them",
                       &o.roadKerb, nullptr, 0, !bridge))
        committed = true;
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("A concrete kerb along both edges, baked at build: it stops where\n"
                          "the road enters a junction patch and runs around the patch's\n"
                          "rounded corners instead (only between roads that both have\n"
                          "kerbs). Vertex colour only - no texture, no VRAM. Its top is\n"
                          "solid. The game skips kerbs farther than ~60 units away.");
    if (o.roadKerb) {
        ImGui::Separator();
        ImGui::BeginDisabled(bridge);
        static const char* const kHeights[] = {"Low", "Standard", "High"};
        static const float kHeightM[] = {0.10f, 0.15f, 0.20f};
        int hi = -1;
        for (int i = 0; i < 3; ++i)
            if (std::fabs(o.roadKerbHeight - kHeightM[i] * upm) < 0.002f * upm) hi = i;
        const float exactW = IM_TRUNC(fs * 3.6f);
        ui::SetNextRowFieldTrim(exactW + gap);
        if (ui::ChoiceRow("Height", kHeights, 3, &hi) && hi >= 0) {
            o.roadKerbHeight = kHeightM[hi] * upm;
            committed = true;
        }
        prefHelp("Above the road surface: low 10 cm (a drop kerb), standard 15 cm,\n"
                 "high 20 cm. The exact value is beside it.");
        ImGui::SameLine(0.0f, gap);
        ImGui::SetNextItemWidth(exactW);
        float hM = o.roadKerbHeight / upm;
        if (ImGui::DragFloat("##kerbheight", &hM, 0.005f, 0.02f / upm, 0.5f / upm, "%.2f m", ImGuiSliderFlags_AlwaysClamp))
            o.roadKerbHeight = std::clamp(hM * upm, 0.02f, 0.5f);
        committed |= ImGui::IsItemDeactivatedAfterEdit();
        if (metresDrag("Top width", &o.roadKerbWidth, upm, 0.005f, 0.05f, 1.0f, "%.2f m")) {
        }
        committed |= ImGui::IsItemDeactivatedAfterEdit();
        prefHelp("The flat top of the kerb, outward from the road edge. 0.25 m is a\n"
                 "standard granite kerb.");
        ImGui::EndDisabled();
    }
    ui::EndInsetCard();
    ImGui::Spacing();

    // --- Pavement ----------------------------------------------------------
    ui::BeginInsetCard("##roadpavement");
    {
        const std::string memoKey = o.id + "/pavement";
        bool pavOn = o.roadPavement > 0.0f;
        const char* why = bridge ? "A bridge has parapets, not a pavement"
                          : o.roadKerb ? "Wraps the junction corners behind the kerb"
                                       : "Needs kerbs";
        if (ui::CardHeader("Pavement", why, &pavOn, nullptr, 0, o.roadKerb && !bridge)) {
            if (pavOn) {
                auto it = roadOffMemo_.find(memoKey);
                o.roadPavement = it != roadOffMemo_.end() && it->second > 0.0f ? it->second : 2.5f * upm;
            } else {
                roadOffMemo_[memoKey] = o.roadPavement;
                o.roadPavement = 0.0f;
            }
            committed = true;
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("A walk behind each kerb, at the kerb's height, baked at build. It\n"
                              "wraps the junction corners with the kerb, narrows where it would\n"
                              "reach another road and follows higher or lower ground. Solid: the\n"
                              "player walks on it, a car bumps up onto it. Needs kerbs.");
        if (o.roadKerb && o.roadPavement > 0.0f) {
            ImGui::Separator();
            ImGui::BeginDisabled(bridge);
            const float tileW = IM_TRUNC(fs * 8.5f);
            if (roadMaterialTile("Pavement material", "Pavement", o.roadPavementMaterial, "<none - untextured concrete>",
                                 tileW))
                committed = true;
            ImGui::SameLine(0.0f, gap);
            ImGui::BeginGroup();
            float m = o.roadPavement / upm;
            ui::KitDrag f;
            f.v = &m;
            f.speed = 0.05f;
            f.vmin = 0.05f / upm;
            f.vmax = 6.0f / upm;
            f.fmt = "Width %.2f m";
            f.name = "Pavement width";
            if (ui::DragRow("##pavwidth", &f, 1)) o.roadPavement = std::clamp(m * upm, 0.0f, 6.0f);
            committed |= ImGui::IsItemDeactivatedAfterEdit();
            ImGui::PushStyleColor(ImGuiCol_Text, sem.textDim);
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted("Its material tiles every 2 m both ways.");
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
            ImGui::EndGroup();
            ImGui::EndDisabled();
        }
    }
    ui::EndInsetCard();
    ImGui::Spacing();

    // --- Wear & details ------------------------------------------------------
    captionRow("Wear & details");
    {
        static const char* const kLabels[] = {"Wear none", "Wear light", "Wear lived-in", "Wear worn out"};
        static const char* const kText[] = {"None", "Light", "Lived-in", "Worn out"};
        static const float kLevel[] = {0.0f, 0.3f, 0.6f, 1.0f};
        static const ui::TileDrawFn kDraw[] = {wear0, wear1, wear2, wear3};
        int exact = -1, nearest = 0;
        for (int i = 0; i < 4; ++i) {
            if (std::fabs(o.roadDetails - kLevel[i]) < 0.005f) exact = i;
            if (std::fabs(o.roadDetails - kLevel[i]) < std::fabs(o.roadDetails - kLevel[nearest])) nearest = i;
        }
        const float W = ImGui::GetContentRegionAvail().x;
        const float tw = IM_TRUNC((W - gap * 3.0f) / 4.0f), th = IM_TRUNC(fs * 4.0f);
        for (int i = 0; i < 4; ++i) {
            if (i) ImGui::SameLine(0.0f, gap);
            if (ui::OptionTile(kLabels[i], kText[i], kDraw[i], exact == i, i == 3 ? W - (tw + gap) * 3.0f : tw, th,
                               exact < 0 && nearest == i)) {
                o.roadDetails = kLevel[i];
                if (o.roadDetails > 0.0f) roaddetail::ensureAtlas(project_.dir);
                committed = true;
            }
        }
        dimText("Manholes, gullies (kerbed roads), repair patches, cracks and oil, baked as decals; hidden past ~50 "
                "units.");
        if (ui::SliderFloat("Density", &o.roadDetails, 0.0f, 1.0f, o.roadDetails > 0.0f ? "%.2f" : "none")) {
            if (o.roadDetails > 0.0f) roaddetail::ensureAtlas(project_.dir);
        }
        committed |= ImGui::IsItemDeactivatedAfterEdit();
        prefHelp("How dense the decals are (0 = none). One shared 8 KB texture,\n"
                 "res/materials/roads/road-details.png - repaint it freely. Never on a\n"
                 "junction patch or under node paint.");
        if (o.roadDetails > 0.0f) {
            const float bw = ImGui::CalcTextSize("Shuffle").x + ImGui::GetStyle().FramePadding.x * 2.0f;
            const float rowRight = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
            ui::SetNextRowFieldTrim(bw + gap);
            if (ui::DragInt("Detail seed", &o.roadDetailSeed, 0.2f)) {
            }
            committed |= ImGui::IsItemDeactivatedAfterEdit();
            prefHelp("Another arrangement at the same density.");
            // The field's item rect includes its (clipped) label, so the
            // button is placed from the row's right edge, not after the item.
            const float rowY = ImGui::GetItemRectMin().y;
            ImGui::SameLine();
            ImGui::SetCursorScreenPos(ImVec2(rowRight - bw, rowY));
            if (ui::SecondaryButton("Shuffle")) {
                ++o.roadDetailSeed;
                committed = true;
            }
        }
    }
    ImGui::Spacing();

    // --- Soft edge -----------------------------------------------------------
    if (o.roadBridge) {
        ImGui::BeginDisabled();
        float zero = 0.0f;
        ui::SliderFloat("Soft edge", &zero, 0.0f, 4.0f, "n/a");
        ImGui::EndDisabled();
        dimText("Off on a bridge deck: a deck ends in its parapets.");
    } else {
        if (metresSlider("Soft edge", &o.roadEdgeFade, upm, 0.0f, 4.0f, o.roadEdgeFade > 0.0f ? "%.1f m" : "hard")) {
        }
        committed |= ImGui::IsItemDeactivatedAfterEdit();
        prefHelp("The outer this-many units on each side fade into the terrain\n"
                 "instead of ending in a hard line - a dirt track. Snaps to the\n"
                 "road's 0.5-unit lateral grid; grip fades to the terrain's with it.\n"
                 "A texture whose alpha is ragged at the edges looks organic.");
    }
    ImGui::Spacing();
    {
        char line[160];
        const float len = roadLength(o.roadPoints) / upm;
        if (o.roadKerb && o.roadPavement > 0.0f)
            std::snprintf(line, sizeof line, "Kerbs 2 x %.0f m \xc2\xb7 pavement 2 x %.1f m wide", len, o.roadPavement / upm);
        else if (o.roadKerb)
            std::snprintf(line, sizeof line, "Kerbs 2 x %.0f m \xc2\xb7 no pavement", len);
        else
            std::snprintf(line, sizeof line, "No kerbs: the surface ends at the ground");
        ui::StatusLine(o.roadKerb ? 0 : 3, line);
    }
    return committed;
}

bool App::drawRoadTabJunctions(SceneObject& o) {
    bool committed = false;
    const bool rail = o.roadKind == roadrail::kRail;
    const roadgen::CrossingPlan& plan = sceneCrossings();
    const auto& objs = project_.objects();
    int me = -1;
    for (size_t k = 0; k < crossingRoadList_.size(); ++k)
        if (crossingRoadList_[k].id == o.id) me = (int)k;
    captionRow(rail ? "Level crossings" : "Junctions on this road");
    int shown = 0;
    if (me >= 0) {
        for (size_t ci = 0; ci < plan.crossings.size(); ++ci) {
            const roadgen::Crossing& c = plan.crossings[ci];
            if (!c.has(me)) continue;
            ++shown;
            std::string others;
            bool otherRail = false;
            for (int r : c.roads) {
                if (r == me || r < 0 || (size_t)r >= crossingRoadObj_.size()) continue;
                const SceneObject& other = objs[(size_t)crossingRoadObj_[(size_t)r]];
                if (!others.empty()) others += ", ";
                others += other.name;
                otherRail |= other.roadKind == roadrail::kRail;
            }
            std::string what;
            if (rail || otherRail) what = "level crossing";
            else if (c.kind == roadgen::kCrossPatch) what = c.transition ? "width change" : "patch";
            else if (c.kind == roadgen::kCrossThrough) what = c.winner == me ? "runs through" : "covered";
            else what = "overlap";
            char arms[24];
            std::snprintf(arms, sizeof arms, "%d-way", c.arms);
            what = std::string(arms) + " \xc2\xb7 " + what + (c.override >= 0 ? " \xc2\xb7 override" : "");
            const std::string label = std::to_string(shown) + "  " + (others.empty() ? std::string("(own end)") : others) +
                                      "##junction" + std::to_string(ci);
            if (ui::ListRow(label.c_str(), what.c_str(), false, ui::Glyph::Road)) selectJunction((int)ci);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                ImGui::SetTooltip("Select it (also its diamond in the viewport) to override\n"
                                  "who wins, its patch material, grip and control.");
        }
    }
    const auto& ovs = project_.active().roadJunctions;
    for (size_t oi = 0; oi < ovs.size() && oi < plan.overrideCrossing.size(); ++oi) {
        if (plan.overrideCrossing[oi] >= 0) continue;
        if (ovs[oi].roadA != o.id && ovs[oi].roadB != o.id) continue;
        ++shown;
        ImGui::TextColored(theme::semantics().danger, "Orphaned junction override");
        ImGui::SameLine();
        if (ImGui::SmallButton(("Show##orphan" + std::to_string(oi)).c_str())) selectJunction(-2 - (int)oi);
    }
    if (shown == 0)
        dimText(rail ? "No road crosses this railway." : "No other road meets this one.");
    ImGui::Spacing();
    dimText("Who runs through is the Priority on the Road tab; a junction's own overrides are on it.");
    return committed;
}
