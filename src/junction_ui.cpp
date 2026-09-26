// Junction overrides (docs/roads.md, "Junction overrides") - App:: methods
// declared in app.hpp, own TU (the credits_ui.cpp precedent).
//
// A crossing is not an object. The editor finds the active scene's crossings
// with the same roadgen::planCrossings the codegen, the viewport and the test
// drive call, draws a diamond on each while a road (or a junction) is
// selected, and edits SceneData::roadJunctions - the per-scene override list
// the planner matches by road-id pair + nearest position.
#include "app.hpp"
#include "app_internal.hpp"
#include "roadgen.hpp"
#include "theme.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>

namespace {

uint64_t fnv(uint64_t h, const void* data, size_t size) {
    const auto* p = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < size; ++i) h = (h ^ p[i]) * 1099511628211ULL;
    return h;
}

std::string fileStem(const std::string& path) {
    return std::filesystem::path(path).stem().string();
}

}  // namespace

const roadgen::CrossingPlan& App::sceneCrossings() {
    const std::vector<SceneObject>& objs = project_.objects();
    std::vector<int> idx;
    std::vector<roadgen::CrossingRoad> roads = project::crossingRoads(objs, &idx);
    uint64_t h = 1469598103934665603ULL;
    const int scene = project_.activeScene;
    h = fnv(h, &scene, sizeof(scene));
    for (size_t k = 0; k < roads.size(); ++k) {
        const roadgen::CrossingRoad& r = roads[k];
        h = fnv(h, r.id.data(), r.id.size() + 1);
        h = fnv(h, r.points.data(), r.points.size() * sizeof(float));
        h = fnv(h, &r.width, sizeof(r.width));
        h = fnv(h, &r.grip, sizeof(r.grip));
        h = fnv(h, &r.rank, sizeof(r.rank));
        h = fnv(h, r.intersection.data(), r.intersection.size() + 1);
        h = fnv(h, &idx[k], sizeof(int));
    }
    for (const roadgen::JunctionOverride& j : project_.active().roadJunctions) {
        h = fnv(h, j.roadA.data(), j.roadA.size() + 1);
        h = fnv(h, j.roadB.data(), j.roadB.size() + 1);
        h = fnv(h, &j.x, sizeof(j.x));
        h = fnv(h, &j.z, sizeof(j.z));
        h = fnv(h, &j.winner, sizeof(j.winner));
        h = fnv(h, j.material.data(), j.material.size() + 1);
        h = fnv(h, &j.grip, sizeof(j.grip));
    }
    if (h != crossingPlanSig_) {
        crossingPlanSig_ = h;
        crossingPlan_ = roadgen::planCrossings(roads, project_.active().roadJunctions,
                                               /*withDecals=*/false);
        crossingRoadList_ = std::move(roads);
        crossingRoadObj_ = std::move(idx);
    }
    return crossingPlan_;
}

int App::selectedCrossing() {
    if (junctionSel_.active && junctionSel_.scene != project_.activeScene)
        junctionSel_.active = false;
    if (!junctionSel_.active || junctionSel_.orphan >= 0) return -1;
    const roadgen::CrossingPlan& plan = sceneCrossings();
    int best = -1;
    float bestD = 1e30f;
    for (size_t ci = 0; ci < plan.crossings.size(); ++ci) {
        const roadgen::Crossing& c = plan.crossings[ci];
        const std::string& a = crossingRoadList_[(size_t)c.a].id;
        const std::string& b = crossingRoadList_[(size_t)c.b].id;
        if (!((a == junctionSel_.a && b == junctionSel_.b) ||
              (a == junctionSel_.b && b == junctionSel_.a)))
            continue;
        const float d = std::hypot(c.shape.x - junctionSel_.x, c.shape.z - junctionSel_.z);
        const float tol = std::max(1.0f, std::min(crossingRoadList_[(size_t)c.a].width,
                                                  crossingRoadList_[(size_t)c.b].width));
        if (d <= tol && d < bestD) {
            bestD = d;
            best = (int)ci;
        }
    }
    // Follow the crossing as the roads move under it.
    if (best >= 0) {
        junctionSel_.x = plan.crossings[(size_t)best].shape.x;
        junctionSel_.z = plan.crossings[(size_t)best].shape.z;
    }
    return best;
}

void App::selectJunction(int crossing) {
    const roadgen::CrossingPlan& plan = sceneCrossings();
    selectOnly(-1);
    junctionSel_ = JunctionSel{};
    junctionSel_.scene = project_.activeScene;
    if (crossing <= -2) {  // an orphaned override, by index
        const int oi = -crossing - 2;
        const auto& ovs = project_.active().roadJunctions;
        if (oi >= (int)ovs.size()) return;
        junctionSel_.active = true;
        junctionSel_.orphan = oi;
        junctionSel_.a = ovs[(size_t)oi].roadA;
        junctionSel_.b = ovs[(size_t)oi].roadB;
        junctionSel_.x = ovs[(size_t)oi].x;
        junctionSel_.z = ovs[(size_t)oi].z;
        statusMessage_ = "Junction override: orphaned (its crossing no longer exists)";
        return;
    }
    if (crossing < 0 || crossing >= (int)plan.crossings.size()) return;
    const roadgen::Crossing& c = plan.crossings[(size_t)crossing];
    junctionSel_.active = true;
    junctionSel_.a = crossingRoadList_[(size_t)c.a].id;
    junctionSel_.b = crossingRoadList_[(size_t)c.b].id;
    junctionSel_.x = c.shape.x;
    junctionSel_.z = c.shape.z;
    const auto& objs = project_.objects();
    statusMessage_ = "Junction: " + objs[(size_t)crossingRoadObj_[(size_t)c.a]].name +
                     " x " + objs[(size_t)crossingRoadObj_[(size_t)c.b]].name;
}

// Where the markers are, and which one is under `mouse` (a crossing index, or
// -(2 + i) for orphaned override i, or -1). ONE function for the overlay and
// the click, the screenIcons rule.
int App::junctionMarkers(ImVec2 imgPos, ImVec2 avail, bool draw, ImVec2 mouse) {
    if (!hasProject_ || avail.x < 1.0f || avail.y < 1.0f) return -1;
    const std::vector<SceneObject>& objs = project_.objects();
    const bool roadSelected = selectedObject_ >= 0 && selectedObject_ < (int)objs.size() &&
                              objs[(size_t)selectedObject_].type == PrimitiveType::Road;
    if (junctionSel_.active && junctionSel_.scene != project_.activeScene)
        junctionSel_.active = false;
    if (!roadSelected && !junctionSel_.active) return -1;
    const roadgen::CrossingPlan& plan = sceneCrossings();
    const int sel = selectedCrossing();
    const theme::Semantics& sem = theme::semantics();
    ImDrawList* dl = draw ? ImGui::GetWindowDrawList() : nullptr;
    if (dl) dl->PushClipRect(imgPos, ImVec2(imgPos.x + avail.x, imgPos.y + avail.y), true);
    int hit = -1;
    float hitDepth = 1e30f;
    auto marker = [&](float x, float z, float lift, int code, bool overridden,
                      bool orphan, bool selected) {
        const float w[3] = {x, viewport_.terrainHeight(x, z) + roadgen::kLift + lift, z};
        float u = 0.0f, v = 0.0f, depth = 0.0f;
        if (!viewport_.projectToImage(w, u, v, &depth)) return;
        const ImVec2 c(imgPos.x + u * avail.x, imgPos.y + v * avail.y);
        const float r = scaled(selected ? 9.0f : 7.0f);
        const bool over = std::fabs(mouse.x - c.x) + std::fabs(mouse.y - c.y) <= r + scaled(2.0f);
        if (over && depth < hitDepth) {
            hit = code;
            hitDepth = depth;
        }
        if (!dl) return;
        const ImVec2 pts[4] = {ImVec2(c.x, c.y - r), ImVec2(c.x + r, c.y),
                               ImVec2(c.x, c.y + r), ImVec2(c.x - r, c.y)};
        const ImU32 fill = orphan       ? ImGui::GetColorU32(sem.danger)
                           : overridden ? ImGui::GetColorU32(sem.accent)
                                        : IM_COL32(235, 235, 240, over ? 255 : 200);
        dl->AddConvexPolyFilled(pts, 4, fill);
        dl->AddPolyline(pts, 4, selected ? ImGui::GetColorU32(sem.accent)
                                         : IM_COL32(20, 20, 24, 220),
                        ImDrawFlags_Closed, scaled(selected ? 2.5f : 1.2f));
        if (orphan)
            dl->AddText(ImVec2(c.x + r + scaled(3.0f), c.y - scaled(7.0f)),
                        ImGui::GetColorU32(sem.danger), "orphaned");
    };
    for (size_t ci = 0; ci < plan.crossings.size(); ++ci) {
        const roadgen::Crossing& c = plan.crossings[ci];
        const float lift = std::max(roadgen::rankLift(crossingRoadList_[(size_t)c.a].rank),
                                    roadgen::rankLift(crossingRoadList_[(size_t)c.b].rank));
        marker(c.shape.x, c.shape.z, lift, (int)ci, c.override >= 0, false,
               sel == (int)ci);
    }
    const auto& ovs = project_.active().roadJunctions;
    for (size_t oi = 0; oi < ovs.size() && oi < plan.overrideCrossing.size(); ++oi)
        if (plan.overrideCrossing[oi] < 0)
            marker(ovs[oi].x, ovs[oi].z, 0.0f, -2 - (int)oi, true, true,
                   junctionSel_.active && junctionSel_.orphan == (int)oi);
    if (dl) dl->PopClipRect();
    return hit;
}

void App::drawJunctionProperties() {
    std::vector<roadgen::JunctionOverride>& ovs = project_.active().roadJunctions;
    const std::vector<SceneObject>& objs = project_.objects();
    auto nameOf = [&](const std::string& id) {
        for (const SceneObject& o : objs)
            if (o.id == id) return o.name;
        return std::string("<deleted road>");
    };
    auto selectRoad = [&](const std::string& id) {
        for (size_t i = 0; i < objs.size(); ++i)
            if (objs[i].id == id) {
                junctionSel_.active = false;
                selectOnly((int)i);
                return;
            }
    };
    ImGui::SeparatorText("Junction");
    if (junctionSel_.scene != project_.activeScene) {
        junctionSel_.active = false;
        return;
    }

    // An orphaned override: its crossing is gone. Say so; keep it until asked.
    if (junctionSel_.orphan >= 0) {
        const int oi = junctionSel_.orphan;
        if (oi >= (int)ovs.size()) {
            junctionSel_.active = false;
            return;
        }
        ImGui::Text("%s  x  %s", nameOf(ovs[(size_t)oi].roadA).c_str(),
                    nameOf(ovs[(size_t)oi].roadB).c_str());
        ImGui::TextColored(theme::semantics().danger,
                           "Orphaned: no crossing of these roads near %.1f, %.1f.",
                           ovs[(size_t)oi].x, ovs[(size_t)oi].z);
        prefHelp(
            "The override is kept so a road moved back brings it back.\n"
            "It changes nothing while orphaned; delete it if it is stale.");
        if (ImGui::Button("Delete override")) {
            ovs.erase(ovs.begin() + oi);
            junctionSel_.active = false;
            commitChange();
            statusMessage_ = "Junction override deleted";
        }
        return;
    }

    const int ci = selectedCrossing();
    if (ci < 0) {
        ImGui::TextDisabled("This crossing no longer exists.");
        if (ImGui::Button("Close")) junctionSel_.active = false;
        return;
    }
    const roadgen::Crossing c = crossingPlan_.crossings[(size_t)ci];
    const std::string& idA = crossingRoadList_[(size_t)c.a].id;
    const std::string& idB = crossingRoadList_[(size_t)c.b].id;
    const std::string nameA = nameOf(idA), nameB = nameOf(idB);
    ImGui::Text("%s  x  %s", nameA.c_str(), nameB.c_str());
    ImGui::TextDisabled("at %.1f, %.1f", c.shape.x, c.shape.z);

    // What the build makes here, from the plan itself.
    std::string result;
    if (c.kind == roadgen::kCrossPatch)
        result = "patch" + (c.material.empty() ? std::string(" (untextured)")
                                               : " (" + fileStem(c.material) + ")") +
                 (c.patchDuplicate ? ", merged with a patch nearby" : "");
    else if (c.kind == roadgen::kCrossThrough)
        result = (c.winner == c.a ? nameA : nameB) + " runs through";
    else
        result = "overlap - no patch";
    char gripBuf[32];
    std::snprintf(gripBuf, sizeof(gripBuf), ", grip %.2f",
                  c.kind == roadgen::kCrossPatch ? c.grip
                  : c.overlay                    ? c.overlayGrip
                                                 : 0.0f);
    if (c.kind == roadgen::kCrossPatch || c.overlay) result += gripBuf;
    ImGui::Text("Result: %s", result.c_str());
    prefHelp(
        "Auto follows the roads: equal ranks with the same intersection\n"
        "material make a patch, different ranks let the higher road run\n"
        "through (the lower one spills onto it).");

    // The override as the crossing sees it: A/B in the crossing's order.
    roadgen::JunctionOverride cur;
    const bool had = c.override >= 0 && c.override < (int)ovs.size();
    if (had) {
        cur = ovs[(size_t)c.override];
        if (cur.roadA != idA) {  // stored the other way round
            std::swap(cur.roadA, cur.roadB);
            if (cur.winner == roadgen::kWinnerRoadA) cur.winner = roadgen::kWinnerRoadB;
            else if (cur.winner == roadgen::kWinnerRoadB) cur.winner = roadgen::kWinnerRoadA;
        }
    } else {
        cur.roadA = idA;
        cur.roadB = idB;
    }
    bool changed = false, commit = false;

    const std::string labels[4] = {"Auto", "Patch", nameA + " wins", nameB + " wins"};
    ImGui::SetNextItemWidth(scaled(260));
    if (ImGui::BeginCombo("Winner", labels[std::clamp(cur.winner, 0, 3)].c_str())) {
        for (int k = 0; k < 4; ++k) {
            const std::string item = labels[k] + "##winner" + std::to_string(k);
            if (ImGui::Selectable(item.c_str(), cur.winner == k) && cur.winner != k) {
                cur.winner = k;
                changed = commit = true;
            }
        }
        ImGui::EndCombo();
    }
    prefHelp(
        "Auto: the rank rule. Patch: a junction patch whatever the ranks.\n"
        "A road that wins runs through here - drawn over the other, which\n"
        "spills onto it - regardless of rank. Only this crossing changes.");
    if (drawRoadSurfaceCombo("Patch material", "junction-material", cur.material,
                             "<auto - the roads' intersection material>")) {
        changed = commit = true;
    }
    prefHelp(
        "A road material for this crossing's patch. Setting one makes a\n"
        "patch here even across ranks or different intersection materials.");
    bool ownGrip = cur.grip > 0.0f;
    if (ImGui::Checkbox("Own grip", &ownGrip)) {
        cur.grip = ownGrip ? (c.kind == roadgen::kCrossPatch ? c.grip : 1.0f) : 0.0f;
        changed = commit = true;
    }
    prefHelp(
        "Auto: a patch takes the lower of the two roads, a winner its own.\n"
        "Own grip sets the tyre grip of this crossing's surface.");
    if (ownGrip) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(scaled(140));
        if (ImGui::SliderFloat("Grip##junction", &cur.grip, 0.1f, 1.5f, "%.2f"))
            changed = true;
        commit |= ImGui::IsItemDeactivatedAfterEdit();
    }
    if (had && ImGui::Button("Reset to auto")) {
        cur.winner = roadgen::kWinnerAuto;
        cur.material.clear();
        cur.grip = 0.0f;
        changed = commit = true;
    }
    if (changed) {
        // Stamp where the crossing is NOW, so later point edits are measured
        // from here.
        cur.x = c.shape.x;
        cur.z = c.shape.z;
        const bool isAuto = cur.winner == roadgen::kWinnerAuto && cur.material.empty() &&
                            cur.grip <= 0.0f;
        if (had && isAuto)
            ovs.erase(ovs.begin() + c.override);
        else if (had)
            ovs[(size_t)c.override] = cur;
        else if (!isAuto)
            ovs.push_back(cur);
    }
    if (commit) {
        commitChange();
        statusMessage_ = "Junction: " + nameA + " x " + nameB;
    }

    ImGui::Separator();
    if (ImGui::Button("Frame in viewport")) {
        // Pivot on the crossing, close enough to read the patch.
        float yaw = 0, pitch = 0, dist = 0, t[3];
        viewport_.camState(yaw, pitch, dist, t);
        const float at[3] = {c.shape.x, viewport_.terrainHeight(c.shape.x, c.shape.z),
                             c.shape.z};
        // Looking down steeply enough that the corner buildings stay out of
        // the way.
        viewport_.setCamState(yaw, std::max(pitch, 0.9f), std::min(dist, 26.0f), at);
    }
    ImGui::SameLine();
    if (ImGui::Button(("Select " + nameA + "##junctionA").c_str())) selectRoad(idA);
    ImGui::SameLine();
    if (ImGui::Button(("Select " + nameB + "##junctionB").c_str())) selectRoad(idB);
}
