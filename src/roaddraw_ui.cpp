// The Draw road tool, road presets in Properties and the bridge height handles
// (docs/roads.md "Drawing roads", "Road presets", "Bridges") - App:: methods
// declared in app.hpp, own TU (the junction_ui.cpp precedent).
//
// Every DECISION here is roaddraw:: / roadpresets:: (pure, host-only, checked
// by --vehicle-check "road drawing" and driven headlessly by --draw-road).
// This file only turns the mouse into a world XZ, draws the answer and commits.
#include "app.hpp"
#include "app_internal.hpp"
#include "roadbridge.hpp"
#include "roaddraw.hpp"
#include "roadpresets.hpp"
#include "theme.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
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

bool App::drawRoadPresetControls(SceneObject& o) {
    bool changed = false;
    const float upm = project_.settings.unitsPerMeter;
    std::vector<roadpresets::Preset>& mine = project_.settings.roadPresets;
    // The preset this road already is, if any.
    const roadpresets::Preset* current = nullptr;
    for (const roadpresets::Preset* p : roadpresets::all(mine))
        if (roadpresets::matches(*p, o, upm)) current = p;
    if (roadPresetPick_.empty() || !roadpresets::find(mine, roadPresetPick_))
        roadPresetPick_ = current ? current->key : roadpresets::builtins().front().key;
    const roadpresets::Preset* pick = roadpresets::find(mine, roadPresetPick_);
    // Field on its own line, its button under it: a button beside a field
    // ran off a default-width Properties panel.
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##roadpreset", pick ? pick->name.c_str() : "")) {
        for (const roadpresets::Preset* p : roadpresets::all(mine)) {
            std::string label = p->name;
            if (p == current) label += "  (this road)";
            if (ImGui::Selectable((label + "##rpp-" + p->key).c_str(), p->key == roadPresetPick_))
                roadPresetPick_ = p->key;
            if (ImGui::IsItemHovered() && !p->help.empty()) ImGui::SetTooltip("%s", p->help.c_str());
        }
        ImGui::EndCombo();
    }
    if (ImGui::Button("Apply preset") && pick) {
        // Every selected road (the multi-selection case: apply to all of them).
        std::vector<int> targets;
        for (int idx : selection_)
            if (idx >= 0 && idx < (int)project_.objects().size() &&
                project_.objects()[(size_t)idx].type == PrimitiveType::Road)
                targets.push_back(idx);
        if (targets.empty()) targets.push_back(selectedObject_);
        roadpresets::ensureMaterials(project_.dir, *pick);
        materialAssetScanTime_ = -1.0;  // a material written on demand is a new asset
        for (int idx : targets) {
            SceneObject& r = idx == selectedObject_ ? o : project_.objects()[(size_t)idx];
            roadpresets::apply(*pick, r, upm);
        }
        changed = true;
        statusMessage_ = "Road preset " + pick->name + " applied to " + std::to_string(targets.size()) +
                         " road(s)";
    }
    prefHelp("Sets every road field the preset owns - width, the surface and\n"
             "junction materials (generated into res/materials/roads when the\n"
             "project lacks them), kerbs, pavement, details, street furniture,\n"
             "markings, kind and tracks, rank, grip, spill and edge fade - on\n"
             "every selected road. Points and bridge heights are kept.");
    ImGui::TextDisabled(current ? "This road is the %s preset." : "This road matches no preset%s",
                        current ? current->name.c_str() : ".");
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputText("##roadpresetname", roadPresetName_, sizeof roadPresetName_);
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
    }
    prefHelp("Keeps this road's look as a preset of the project (saved in the\n"
             ".tyra): it is listed after the built-ins here and in the Draw\n"
             "road tool. A preset of the same name is replaced.");
    if (current && !mine.empty() && current >= &mine.front() && current <= &mine.back()) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Delete preset")) {
            const std::string name = current->name;
            mine.erase(mine.begin() + (current - &mine.front()));
            prefSettings_.roadPresets = mine;
            changed = true;
            statusMessage_ = "Road preset \"" + name + "\" deleted";
        }
    }
    return changed;
}
