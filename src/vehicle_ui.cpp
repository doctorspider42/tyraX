// Tools > Vehicle Editor (docs/vehicles.md) - App:: methods declared in
// app.hpp, own TU (the prefab_ui.cpp precedent).
//
// A vehicle is defined ONCE per project and placed as often as you like, so
// this window edits Project::vehicles and the scene knows only a name. Two
// things shape the file:
//
//   * Definitions are project-wide, so commitChange() dirties and syncs but
//     pushes no undo step (History carries the scenes alone). A window that is
//     mostly sliders needs an undo, so it keeps its own - the Material Editor
//     and the Menu Editor's Style tab already made that call.
//
//   * The import BAKE parses a .glb/.fbx and decimates it. That cannot happen
//     per frame, so it is cached per definition and keyed on everything it
//     depends on; the window draws the last result and re-bakes when the key
//     moves.

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>

#include "app.hpp"
#include "app_internal.hpp"
#include "imgui.h"
#include "theme.hpp"

namespace {

std::string bakeKey(const VehicleDef& v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "|%d|%d|%d", v.bodyTriBudget, v.wheelTriBudget,
                  v.mergeUntextured ? 1 : 0);
    return v.modelPath + buf;
}

// Unique "Car 1", "Car 2", ... - a definition is referenced BY NAME, so two
// with one name would make every instance ambiguous.
std::string uniqueName(const std::vector<VehicleDef>& defs, const std::string& base) {
    auto taken = [&](const std::string& n) {
        for (const VehicleDef& v : defs)
            if (v.name == n) return true;
        return false;
    };
    if (!taken(base)) return base;
    for (int i = 2; i < 1000; ++i) {
        const std::string n = base + " " + std::to_string(i);
        if (!taken(n)) return n;
    }
    return base;
}

}  // namespace

bool App::vehicleBodyBounds(const SceneObject& o, float* mn, float* mx) {
    if (o.type != PrimitiveType::Vehicle || o.vehicleDef.empty()) return false;
    for (const VehicleDef& v : project_.vehicles) {
        if (v.name != o.vehicleDef) continue;
        auto it = vehicleBakes_.find(v.id);
        if (it == vehicleBakes_.end() || !it->second.ok) return false;
        const tmdl::Model& b = it->second.result.body;
        for (int a = 0; a < 3; ++a) mn[a] = b.min[a], mx[a] = b.max[a];
        // The wheels stick out past the body sideways and below it; a
        // collision box that stops at the paint lets the player walk into the
        // arches. Union the wheel envelope in from the anchors.
        const vehiclesim::DriveSpec& s = v.drive;
        mn[0] = std::min(mn[0], -0.5f * s.track - s.wheelRadius * 0.5f);
        mx[0] = std::max(mx[0], 0.5f * s.track + s.wheelRadius * 0.5f);
        mn[1] = std::min(mn[1], -s.rideHeight);
        return true;
    }
    return false;
}

void App::vehicleRefreshBake(int index, bool force) {
    if (index < 0 || index >= (int)project_.vehicles.size()) return;
    const VehicleDef& v = project_.vehicles[index];
    if (v.modelPath.empty()) return;
    VehicleBakeCache& c = vehicleBakes_[v.id];
    const std::string key = bakeKey(v);
    if (!force && c.key == key) return;
    c.key = key;
    c.error.clear();
    vehbake::Options opt;
    opt.bodyTriBudget = v.bodyTriBudget;
    opt.wheelTriBudget = v.wheelTriBudget;
    opt.mergeUntextured = v.mergeUntextured;
    // The palette is baked into the merged part's texture field, so the name
    // here has to be the path the game will actually open. Everything the bake
    // produces is a derived artifact and lives under .res-baked/ with the
    // other bakes; the build copies it next to the ELF.
    const std::string rel = "vehicles/veh-" + v.id;
    opt.paletteTexture = rel + "-palette.png";
    c.ok = vehbake::build(project_.filePath(v.modelPath), opt, c.result, c.error);
    if (!c.ok) return;

    // Write the three files. A viewport preview causing a disk write reads
    // oddly until you notice that the console needs exactly these bytes: one
    // bake, two consumers, so there is no second answer to what this car is.
    namespace fs = std::filesystem;
    const fs::path dir = fs::path(project_.dir) / ".res-baked" / "vehicles";
    std::error_code ec;
    fs::create_directories(dir, ec);
    auto put = [&](const std::string& name, const std::string& bytes) {
        const fs::path p = dir / name;
        // Compare before writing: this runs whenever a budget slider settles,
        // and a fresh mtime on an asset the build reads is a rebuild nobody
        // asked for (the refreshGenerated rule).
        std::ifstream in(p, std::ios::binary);
        if (in) {
            const std::string old((std::istreambuf_iterator<char>(in)),
                                  std::istreambuf_iterator<char>());
            if (old == bytes) return;
        }
        in.close();
        std::ofstream(p, std::ios::binary).write(bytes.data(), (std::streamsize)bytes.size());
    };
    put("veh-" + v.id + "-body.tmdl", tmdl::write(c.result.body));
    put("veh-" + v.id + "-wheel.tmdl", tmdl::write(c.result.wheel));
    if (!c.result.palettePng.empty())
        put("veh-" + v.id + "-palette.png",
            std::string((const char*)c.result.palettePng.data(),
                        c.result.palettePng.size()));

    // Hand the geometry to the viewport so a placed instance draws. The
    // viewport gets the IN-MEMORY bake rather than re-reading the files: one
    // bake, and no .tmdl reader on the host that would have to agree with it.
    viewport_.setVehicleDraw(v.name, c.result.body, c.result.wheel,
                             ".res-baked/" + rel + "-palette.png", v.drive.wheelBase,
                             v.drive.track, v.drive.wheelRadius, v.drive.rideHeight);
}

// Keeps every definition's bake current, one per frame at most. Called from
// drawUI, NOT from the window body (the giBakerPoll rule): a placed vehicle
// has to draw whether or not the Vehicle Editor is open, and on a project with
// several cars baking them all in one frame would stall the editor for as long
// as parsing that many .fbx files takes.
void App::vehicleTick() {
    if (!hasProject_) return;
    for (int i = 0; i < (int)project_.vehicles.size(); ++i) {
        const VehicleDef& v = project_.vehicles[i];
        if (v.modelPath.empty()) continue;
        auto it = vehicleBakes_.find(v.id);
        if (it != vehicleBakes_.end() && it->second.key == bakeKey(v)) continue;
        vehicleRefreshBake(i, false);
        return;  // one per frame
    }
}

void App::renameVehicleDef(int index, const std::string& newName) {
    if (index < 0 || index >= (int)project_.vehicles.size()) return;
    const std::string oldName = project_.vehicles[index].name;
    if (newName.empty() || newName == oldName) return;
    project_.vehicles[index].name = newName;
    // An instance stores the NAME, so it has to follow - the renameFont rule.
    // Miss this and every placed car silently loses its definition.
    for (SceneData& sc : project_.scenes)
        for (SceneObject& o : sc.objects)
            if (o.type == PrimitiveType::Vehicle && o.vehicleDef == oldName)
                o.vehicleDef = newName;
    for (Prefab& pf : project_.prefabs)
        for (SceneObject& o : pf.objects)
            if (o.type == PrimitiveType::Vehicle && o.vehicleDef == oldName)
                o.vehicleDef = newName;
}

void App::drawVehicleWindow() {
    if (!showVehicles_ || !hasProject_) return;
    ImGui::SetNextWindowSize(ImVec2(scaled(760), scaled(560)), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Vehicle Editor", &showVehicles_)) {
        ImGui::End();
        return;
    }

    // The section guard: a widget added later that forgets its own `changed`
    // flag is still caught, because the whole body is compared before/after.
    const std::string before =
        project::sectionJson(project_, project::Section::Vehicles);

    std::vector<VehicleDef>& defs = project_.vehicles;
    if (vehicleSel_ >= (int)defs.size()) vehicleSel_ = (int)defs.size() - 1;
    // Opening the window on a project that HAS vehicles must not show an empty
    // right-hand pane: with nothing selected every tab is hidden, and the
    // window reads as a feature that does not work.
    if (vehicleSel_ < 0 && !defs.empty()) vehicleSel_ = 0;

    // --- the definition list ------------------------------------------------
    ImGui::BeginChild("##vehlist", ImVec2(scaled(180), 0), true);
    for (int i = 0; i < (int)defs.size(); ++i) {
        // An explicit ##id: two definitions may not share a name, but one is
        // being TYPED for a moment during a rename, and a Selectable's label
        // is its ImGui id.
        const std::string label = defs[i].name + "##vehsel" + std::to_string(i);
        if (ImGui::Selectable(label.c_str(), vehicleSel_ == i)) vehicleSel_ = i;
    }
    ImGui::EndChild();
    ImGui::SameLine();

    ImGui::BeginGroup();
    if (ImGui::Button("New vehicle")) {
        VehicleDef v;
        v.id = project::newObjectId();
        v.name = uniqueName(defs, "Car");
        defs.push_back(std::move(v));
        vehicleSel_ = (int)defs.size() - 1;
    }
    ImGui::SameLine();
    if (ImGui::Button("Duplicate") && vehicleSel_ >= 0) {
        VehicleDef v = defs[vehicleSel_];
        v.id = project::newObjectId();
        v.name = uniqueName(defs, v.name + " copy");
        defs.push_back(std::move(v));
        vehicleSel_ = (int)defs.size() - 1;
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete") && vehicleSel_ >= 0) {
        // Instances keep their name reference. That is deliberate: a delete
        // must not silently edit scenes, and a dangling name is reported by
        // the instance's own Properties row where the author can see it.
        vehicleBakes_.erase(defs[vehicleSel_].id);
        defs.erase(defs.begin() + vehicleSel_);
        if (vehicleSel_ >= (int)defs.size()) vehicleSel_ = (int)defs.size() - 1;
    }

    if (vehicleSel_ < 0 || defs.empty()) {
        ImGui::Separator();
        ImGui::TextDisabled("No vehicle selected.");
        ImGui::EndGroup();
        if (project::sectionJson(project_, project::Section::Vehicles) != before)
            commitChange();
        ImGui::End();
        return;
    }

    VehicleDef& v = defs[vehicleSel_];
    vehicleRefreshBake(vehicleSel_, false);
    const VehicleBakeCache* bake = nullptr;
    if (auto it = vehicleBakes_.find(v.id); it != vehicleBakes_.end()) bake = &it->second;

    ImGui::Separator();
    {
        char name[128];
        std::snprintf(name, sizeof(name), "%s", v.name.c_str());
        ImGui::SetNextItemWidth(scaled(240));
        if (ImGui::InputText("Name", name, sizeof(name)) ) {
            // Applied on commit, not per keystroke: retargeting every instance
            // on each character would rewrite the scenes once per letter.
        }
        if (ImGui::IsItemDeactivatedAfterEdit())
            renameVehicleDef(vehicleSel_, uniqueName(defs, name));
    }

    if (ImGui::BeginTabBar("##vehtabs")) {
        // --- Model ----------------------------------------------------------
        if (ImGui::BeginTabItem("Model")) {
            // The same picker shape the Properties panel uses for a Model:
            // the project's own res/models assets, never a free-text path.
            const std::string current =
                v.modelPath.empty()
                    ? "<none>"
                    : std::filesystem::path(v.modelPath).filename().string();
            ImGui::SetNextItemWidth(scaled(320));
            if (ImGui::BeginCombo("Model file", current.c_str())) {
                const std::vector<std::string> anim = listAnimatedModelFiles();
                for (const std::string& m : anim) {
                    const std::string rel = "res/models/" + m;
                    if (ImGui::Selectable(m.c_str(), rel == v.modelPath) &&
                        rel != v.modelPath) {
                        v.modelPath = rel;
                        // The wheel rows belong to the OLD file's nodes.
                        v.wheels.clear();
                    }
                }
                if (anim.empty())
                    ImGui::TextDisabled(
                        "No .glb/.fbx models - import one in Project > Assets.");
                ImGui::EndCombo();
            }
            prefHelp(
                "One .glb or .fbx holding the body AND the wheels. The wheels are\n"
                "found by their geometry, so their node names do not matter.");

            ImGui::Separator();
            if (!bake || v.modelPath.empty()) {
                ImGui::TextDisabled("Pick a model to see what the importer finds.");
            } else if (!bake->ok) {
                ImGui::PushStyleColor(ImGuiCol_Text, theme::semantics().danger);
                ImGui::TextWrapped("Import failed: %s", bake->error.c_str());
                ImGui::PopStyleColor();
            } else {
                const vehbake::Result& r = bake->result;
                // Every line the importer decided, verbatim. A detection
                // nobody can check is a detection nobody should trust.
                for (const std::string& n : r.notes) ImGui::BulletText("%s", n.c_str());

                if (r.detection.frontAssumed) {
                    ImGui::PushStyleColor(ImGuiCol_Text, theme::semantics().warn);
                    ImGui::TextWrapped(
                        "The front end was assumed, not read. If the car drives "
                        "backwards, flip it:");
                    ImGui::PopStyleColor();
                }
                ImGui::Checkbox("Flip front/rear", &v.flipFront);

                ImGui::Separator();
                if (ImGui::BeginTable("##wheels", 4,
                                      ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchProp)) {
                    ImGui::TableSetupColumn("Wheel");
                    ImGui::TableSetupColumn("Node");
                    ImGui::TableSetupColumn("Steered");
                    ImGui::TableSetupColumn("Driven");
                    ImGui::TableHeadersRow();
                    for (size_t i = 0; i < r.detection.wheels.size(); ++i) {
                        const vehiclesim::Wheel& w = r.detection.wheels[i];
                        // The author's row for this wheel, created on demand.
                        VehicleWheel* row = nullptr;
                        // Matched by the NODE NAME the bake reports: an index
                        // means nothing across a re-import of an edited model.
                        const std::string node = w.nodeName;
                        for (VehicleWheel& vw : v.wheels)
                            if (vw.node == node) row = &vw;
                        if (!row) {
                            VehicleWheel vw;
                            vw.node = node;
                            vw.steered = w.steered;
                            vw.driven = w.driven;
                            v.wheels.push_back(vw);
                            row = &v.wheels.back();
                        }
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::Text("%s %s", w.front ? "front" : "rear",
                                    w.left ? "left" : "right");
                        ImGui::TableNextColumn();
                        ImGui::Text("%s", w.nodeName.c_str());
                        ImGui::SetItemTooltip("radius %.3f, width %.3f", w.radius, w.width);
                        ImGui::TableNextColumn();
                        ImGui::PushID((int)i * 2);
                        ImGui::Checkbox("##st", &row->steered);
                        ImGui::PopID();
                        ImGui::TableNextColumn();
                        ImGui::PushID((int)i * 2 + 1);
                        ImGui::Checkbox("##dr", &row->driven);
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
            }
            ImGui::EndTabItem();
        }

        // --- Driving --------------------------------------------------------
        // Widgets are DERIVED from vehiclesim::specFields(), so a tunable added
        // to DriveSpec appears here, saves, loads and gets its tooltip by
        // existing in that one list.
        if (ImGui::BeginTabItem("Driving")) {
            const std::vector<vehiclesim::SpecField> fields =
                vehiclesim::specFields(v.drive);
            for (const vehiclesim::SpecField& f : fields) {
                ImGui::SetNextItemWidth(scaled(220));
                ImGui::SliderFloat(f.label, f.value, f.min, f.max, "%.4g");
                if (f.tip && f.tip[0]) prefHelp(f.tip);
            }
            ImGui::EndTabItem();
        }

        // --- Camera and doors ------------------------------------------------
        if (ImGui::BeginTabItem("Driver")) {
            ImGui::SetNextItemWidth(scaled(220));
            ImGui::SliderFloat("Camera distance", &v.camDist, 1.0f, 20.0f, "%.2f");
            ImGui::SetNextItemWidth(scaled(220));
            ImGui::SliderFloat("Camera height", &v.camHeight, 0.0f, 10.0f, "%.2f");
            ImGui::SetNextItemWidth(scaled(220));
            ImGui::SliderFloat("Camera pitch", &v.camPitch, -30.0f, 60.0f, "%.1f");
            ImGui::Separator();
            ImGui::SetNextItemWidth(scaled(300));
            ImGui::DragFloat3("Exit offset", v.exitOffset, 0.05f);
            prefHelp(
                "Where the player is put down on getting out, relative to the\n"
                "car: x right, y up, z forward. The driver's door.");
            ImGui::EndTabItem();
        }

        // --- Cost -------------------------------------------------------------
        // The number that decides whether a scene can afford this vehicle at
        // all. A PS2 submit is ~1 ms of fixed EE time whatever it holds, so
        // stating the submit count is stating the frame budget.
        if (ImGui::BeginTabItem("Cost")) {
            if (!bake || !bake->ok) {
                ImGui::TextDisabled("Import a model to see what it costs.");
            } else {
                const vehbake::Result& r = bake->result;
                const int submits = r.bodyParts + r.wheelParts;
                ImGui::Text("Submits per vehicle: %d  (~%.1f ms of EE time)", submits,
                            submits * 1.0f);
                ImGui::Text("Triangles: body %d + 4 wheels %d = %d", r.bodyTris,
                            r.wheelTris * 4, r.bodyTris + r.wheelTris * 4);
                ImGui::Text("Source was %d parts, %d triangles.", r.srcParts, r.srcTris);
                ImGui::Separator();
                ImGui::SetNextItemWidth(scaled(200));
                ImGui::SliderInt("Body triangles", &v.bodyTriBudget, 100, 6000);
                ImGui::SetNextItemWidth(scaled(200));
                ImGui::SliderInt("Wheel triangles", &v.wheelTriBudget, 40, 3000);
                ImGui::Checkbox("Merge untextured materials", &v.mergeUntextured);
                prefHelp(
                    "Collapses every untextured material into one part, with the\n"
                    "colours in a generated palette texture. This is what takes a\n"
                    "36-part car down to two submits - turning it off is for\n"
                    "seeing what it costs, not for shipping.");
                if (ImGui::Button("Re-import now")) vehicleRefreshBake(vehicleSel_, true);

                // How many of these are placed, and what that totals.
                int placed = 0;
                for (const SceneData& sc : project_.scenes)
                    for (const SceneObject& o : sc.objects)
                        if (o.type == PrimitiveType::Vehicle && o.vehicleDef == v.name)
                            ++placed;
                ImGui::Separator();
                ImGui::Text("Placed in this project: %d instance(s)", placed);
                if (placed > 0)
                    ImGui::Text("If they were all on screen at once: %d submits, ~%.0f ms.",
                                placed * submits, placed * submits * 1.0f);
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::EndGroup();

    if (project::sectionJson(project_, project::Section::Vehicles) != before)
        commitChange();
    ImGui::End();
}
