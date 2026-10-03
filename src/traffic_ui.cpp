// Road traffic in the editor (docs/traffic.md): View > Lanes and the Traffic
// block of Project > Preferences > World. App:: methods declared in app.hpp,
// own TU (the batch_ui.cpp precedent). Nothing here decides anything: the
// overlay draws roadlanes::buildScene - the codegen's own graph - and the
// settings are the ProjectSettings::traffic the codegen reads.
#include <algorithm>
#include <cmath>
#include <cstdio>

#include "app.hpp"
#include "app_internal.hpp"

void App::drawLanesOverlay(ImVec2 imgPos, ImVec2 avail) {
    const int scene = project_.activeScene;
    // Rebuilt when the model or the scene changes - not mid-drag, a road
    // being dragged would rebuild every frame.
    if (scene != lanesScene_ ||
        (lanesSerial_ != modelEditSerial_ && !ImGui::IsMouseDown(ImGuiMouseButton_Left))) {
        lanesGraph_ = roadlanes::buildScene(project_, scene);
        lanesScene_ = scene;
        lanesSerial_ = modelEditSerial_;
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(imgPos, ImVec2(imgPos.x + avail.x, imgPos.y + avail.y), true);
    auto toScreen = [&](const float* w, ImVec2& out) {
        float u, v;
        if (!viewport_.projectToImage(w, u, v)) return false;
        out = ImVec2(imgPos.x + u * avail.x, imgPos.y + v * avail.y);
        return true;
    };
    auto polyline = [&](const std::vector<float>& pts, ImU32 col, float width, bool arrow) {
        ImVec2 prev;
        bool havePrev = false;
        for (size_t i = 0; i + 2 < pts.size(); i += 3) {
            ImVec2 p;
            const bool ok = toScreen(&pts[i], p);
            if (ok && havePrev) dl->AddLine(prev, p, col, width);
            prev = p;
            havePrev = ok;
        }
        // A chevron at the middle: which way the lane runs.
        const size_t n = pts.size() / 3;
        if (!arrow || n < 2) return;
        const size_t m = n / 2;
        const size_t a = m > 0 ? m - 1 : 0;
        ImVec2 pa, pb;
        if (!toScreen(&pts[a * 3], pa) || !toScreen(&pts[m * 3], pb)) return;
        const float dx = pb.x - pa.x, dy = pb.y - pa.y, l = std::sqrt(dx * dx + dy * dy);
        if (l < 1e-3f) return;
        const float ux = dx / l, uy = dy / l, s = scaled(6.0f);
        dl->AddTriangleFilled(ImVec2(pb.x + ux * s, pb.y + uy * s),
                              ImVec2(pb.x - ux * s - uy * s * 0.6f, pb.y - uy * s + ux * s * 0.6f),
                              ImVec2(pb.x - ux * s + uy * s * 0.6f, pb.y - uy * s - ux * s * 0.6f), col);
    };
    for (const roadlanes::Lane& l : lanesGraph_.lanes)
        polyline(l.pts, IM_COL32(90, 170, 255, 220), scaled(2.0f), true);
    for (const roadlanes::Connection& c : lanesGraph_.conns) {
        ImU32 col = c.givesWay ? IM_COL32(255, 220, 60, 200) : IM_COL32(90, 230, 120, 200);
        if (c.group == 0) col = IM_COL32(255, 150, 50, 210);
        if (c.group == 1) col = IM_COL32(200, 120, 255, 210);
        polyline(c.pts, col, scaled(1.5f), false);
    }
    // Lanes that stop: a dead end, or no legal exit (red).
    for (size_t li = 0; li < lanesGraph_.lanes.size(); ++li) {
        if (!lanesGraph_.laneOut[li].empty()) continue;
        const std::vector<float>& p = lanesGraph_.lanes[li].pts;
        ImVec2 e;
        if (p.size() >= 3 && toScreen(&p[p.size() - 3], e))
            dl->AddCircleFilled(e, scaled(4.0f), IM_COL32(255, 70, 60, 230));
    }
    dl->PopClipRect();
    ImGui::SetCursorScreenPos(ImVec2(imgPos.x + 8, imgPos.y + avail.y - scaled(22.0f)));
    ImGui::TextColored(ImVec4(0.55f, 0.75f, 1.0f, 1.0f), "Lanes: %zu lanes, %zu connections, %d dead end(s)%s",
                       lanesGraph_.lanes.size(), lanesGraph_.conns.size(), lanesGraph_.deadEnds,
                       lanesGraph_.warnings.size() > (size_t)lanesGraph_.deadEnds ? ", see --road-lanes" : "");
}

void App::drawTrafficSettings(TrafficSettings& t) {
    if (!ImGui::CollapsingHeader("Traffic")) return;
    ImGui::SliderInt("Ambient cars", &t.cars, 0, 16, t.cars > 0 ? "%d per scene" : "off");
    prefHelp("AI cars that drive the roads by themselves - lanes, stop lines,\n"
             "give way and traffic lights (docs/traffic.md). Placed out of view\n"
             "around the player and recycled. Each costs a vehicle's sim and draw.");
    ImGui::BeginDisabled(t.cars <= 0);
    // The definitions to draw from: none ticked = every one.
    if (ImGui::BeginCombo("Vehicles", t.vehicles.empty() ? "every definition"
                                                         : (std::to_string(t.vehicles.size()) +
                                                            " definition(s)").c_str())) {
        for (const VehicleDef& v : project_.vehicles) {
            if (v.modelPath.empty()) continue;
            bool on = std::find(t.vehicles.begin(), t.vehicles.end(), v.name) != t.vehicles.end();
            if (ImGui::Checkbox(v.name.c_str(), &on)) {
                if (on) t.vehicles.push_back(v.name);
                else t.vehicles.erase(std::remove(t.vehicles.begin(), t.vehicles.end(), v.name),
                                      t.vehicles.end());
            }
        }
        ImGui::EndCombo();
    }
    ImGui::DragFloat("Spawn radius", &t.radius, 1.0f, 20.0f, 2000.0f, "%.0f units");
    prefHelp("Cars live within this range of the player. With road streaming on\n"
             "it is held 20 units inside the road stream radius.");
    ImGui::DragFloat("Density", &t.density, 0.05f, 0.1f, 20.0f, "%.2f cars / 100 units of lane");
    ImGui::DragFloat("Lane speed", &t.speed, 0.1f, 2.0f, 40.0f, "%.1f units/s");
    ImGui::DragFloat("Green", &t.green, 0.1f, 2.0f, 120.0f, "%.1f s");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(scaled(70.0f));
    ImGui::DragFloat("Amber", &t.amber, 0.1f, 1.0f, 10.0f, "%.1f s");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(scaled(70.0f));
    ImGui::DragFloat("All red", &t.allRed, 0.1f, 0.0f, 10.0f, "%.1f s");
    prefHelp("One phase of the traffic lights. The two directions take turns:\n"
             "green, amber, then all red while the junction clears.");
    ImGui::Checkbox("Left-hand traffic", &t.leftHand);
    ImGui::EndDisabled();
    t.cars = std::clamp(t.cars, 0, 32);
    t.radius = std::clamp(t.radius, 20.0f, 2000.0f);
    t.density = std::clamp(t.density, 0.1f, 20.0f);
    t.speed = std::clamp(t.speed, 2.0f, 40.0f);
}
