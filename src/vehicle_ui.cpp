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
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>

#include "app.hpp"
#include "app_internal.hpp"
#include "placement.hpp"
#include "imgui.h"
#include "theme.hpp"

namespace {

// A measured value, or the fallback when the bake could not produce one (a
// two-wheeled vehicle has no track, a wheel-less one no radius).
float r_geom(float measured, float fallback) {
    return measured > 1e-4f ? measured : fallback;
}

std::string bakeKey(const VehicleDef& v) {
    char buf[80];
    std::snprintf(buf, sizeof(buf), "|%d|%d|%d|%.3f|", v.bodyTriBudget,
                  v.wheelTriBudget, v.mergeUntextured ? 1 : 0, v.bodyShine);
    return v.modelPath + buf + v.bodyReflMap;
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
    VehicleDef& v = project_.vehicles[index];
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
    opt.bodyShine = v.bodyShine;
    opt.bodyReflMap = vehbake::binReflPath(v.bodyReflMap);
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

    // Adopt the model's OWN measurements - but only while the definition still
    // carries the untouched defaults, so an author who set a wider track keeps
    // it across re-imports. Without this the console drives a 0.32 wheel on a
    // car whose baked wheel is 0.19: the numbers are measured by the bake and
    // then nothing carries them into the definition codegen reads.
    {
        const vehiclesim::DriveSpec def{};
        if (v.drive.wheelBase == def.wheelBase && v.drive.track == def.track &&
            v.drive.wheelRadius == def.wheelRadius) {
            v.drive.wheelBase = r_geom(c.result.spec.wheelBase, def.wheelBase);
            v.drive.track = r_geom(c.result.spec.track, def.track);
            v.drive.wheelRadius = r_geom(c.result.spec.wheelRadius, def.wheelRadius);
            v.drive.rideHeight = r_geom(c.result.spec.rideHeight, def.rideHeight);
            v.drive.bodyOverhang = r_geom(c.result.spec.bodyOverhang, def.bodyOverhang);
            setDirty(true);
        }
    }

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

void App::vehicleDriveStart(int objectIndex) {
    if (!hasProject_) return;
    std::vector<SceneObject>& objs = project_.objects();
    if (objectIndex < 0 || objectIndex >= (int)objs.size()) return;
    if (objs[objectIndex].type != PrimitiveType::Vehicle) return;
    vehicleDriveStop();
    const SceneObject& o = objs[objectIndex];
    for (int a = 0; a < 3; ++a) {
        vehicleDriveHome_[a] = o.position[a];
        vehicleDriveHome_[3 + a] = o.rotation[a];
    }
    vehicleDriveState_ = vehiclesim::DriveState{};
    for (int a = 0; a < 3; ++a) vehicleDriveState_.pos[a] = o.position[a];
    vehicleDriveState_.yaw = o.rotation[1];
    vehicleDriveObj_ = objectIndex;
}

void App::vehicleDriveStop() {
    if (vehicleDriveObj_ < 0) return;
    std::vector<SceneObject>& objs = project_.objects();
    if (vehicleDriveObj_ < (int)objs.size()) {
        SceneObject& o = objs[vehicleDriveObj_];
        for (int a = 0; a < 3; ++a) {
            o.position[a] = vehicleDriveHome_[a];
            o.rotation[a] = vehicleDriveHome_[3 + a];
        }
    }
    vehicleDriveObj_ = -1;
}

// One step of the test drive. Deliberately NOT a commitChange path: the object
// is moved in place and put back when the drive ends, so a drive leaves the
// project exactly as it found it and never enters undo.
void App::vehicleDriveTick() {
    if (!hasProject_ || vehicleDriveObj_ < 0) return;
    std::vector<SceneObject>& objs = project_.objects();
    if (vehicleDriveObj_ >= (int)objs.size()) {
        vehicleDriveObj_ = -1;
        return;
    }
    SceneObject& o = objs[vehicleDriveObj_];
    const VehicleDef* def = nullptr;
    for (const VehicleDef& v : project_.vehicles)
        if (v.name == o.vehicleDef) def = &v;
    if (!def) return;

    // WantTextInput, not WantCaptureKeyboard: the latter is true whenever any
    // window has focus, which is always while the Vehicle Editor is open - it
    // gated the throttle off entirely and the car sat at 0.00 with the key
    // held. What must not steal a keystroke is an ACTIVE TEXT FIELD (typing a
    // top speed must not also floor the throttle), and that is what this asks.
    vehiclesim::DriveInput in;
    if (vehicleDriveHoldThrottle_) in.throttle += 1.0f;
    in.steer += vehicleDriveSteer_;
    if (!ImGui::GetIO().WantTextInput) {
        if (ImGui::IsKeyDown(ImGuiKey_W)) in.throttle += 1.0f;
        if (ImGui::IsKeyDown(ImGuiKey_S)) in.throttle -= 1.0f;
        if (ImGui::IsKeyDown(ImGuiKey_A)) in.steer -= 1.0f;
        if (ImGui::IsKeyDown(ImGuiKey_D)) in.steer += 1.0f;
        if (ImGui::IsKeyDown(ImGuiKey_Space)) in.handbrake = true;
        if (ImGui::IsKeyDown(ImGuiKey_LeftShift)) in.brake = 1.0f;
        // E, so nitrous is testable at all: the two branches that change
        // acceleration and top speed were unreachable in the host copy, and a
        // divergence in them would have been invisible until it shipped.
        if (ImGui::IsKeyDown(ImGuiKey_E)) in.nos = true;
    }

    // The SAME sampler the placement snap uses, so the car drives on exactly
    // the heightfield the editor draws - and the console walks.
    const vehiclesim::HeightFn ground = [this](float x, float z) {
        return project_.active().terrain.enabled ? viewport_.terrainHeight(x, z) : -1e6f;
    };
    // Walls, from placement's own boxes - approximate (world AABBs rather
    // than the console's slide resolver), but the same four corners and the
    // same refusal, so a pillar stops the test drive the way it stops the
    // game. Rebuilt per tick: a test drive is one car in an authored scene.
    std::vector<placement::Aabb> solids;
    {
        const aobake::ModelAabbFn aabbFn = placementModelAabb();
        const std::vector<SceneObject>& all = project_.objects();
        for (int i = 0; i < (int)all.size(); ++i) {
            if (i == vehicleDriveObj_) continue;
            if (!placement::collides(all[i])) continue;
            solids.push_back(placement::worldAabb(all[i], aabbFn));
        }
    }
    const vehiclesim::SolidFn solid = [&](float x, float z, float feetY) {
        for (const placement::Aabb& b : solids)
            if (x > b.mn[0] && x < b.mx[0] && z > b.mn[2] && z < b.mx[2] &&
                feetY + 1.0f > b.mn[1] && feetY < b.mx[1])
                return true;
        return false;
    };
    vehiclesim::step(def->drive, in, ImGui::GetIO().DeltaTime, ground,
                     vehicleDriveState_, solid);

    for (int a = 0; a < 3; ++a) o.position[a] = vehicleDriveState_.pos[a];
    // Negated like the runtime's write: the sim's pitch is "positive = nose
    // up", a positive rotX is nose DOWN (see updateVehicles).
    o.rotation[0] = -(vehicleDriveState_.pitch + vehicleDriveState_.leanPitch);
    o.rotation[1] = vehicleDriveState_.yaw;
    o.rotation[2] = -(vehicleDriveState_.roll + vehicleDriveState_.leanRoll);
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

        // --- Test drive -------------------------------------------------------
        // The reason vehiclesim is host-only: the same step() the console will
        // run, driven from the keyboard against the real scene's terrain, so
        // grip and acceleration are tuned in a "slider, feel, slider" loop
        // instead of "slider, four minutes of Docker, PCSX2".
        if (ImGui::BeginTabItem("Test drive")) {
            // Which placed instance to drive - the first one of this
            // definition in the active scene.
            int inst = -1;
            const std::vector<SceneObject>& objs = project_.objects();
            for (int i = 0; i < (int)objs.size(); ++i)
                if (objs[i].type == PrimitiveType::Vehicle && objs[i].vehicleDef == v.name) {
                    inst = i;
                    break;
                }
            if (inst < 0) {
                ImGui::TextDisabled(
                    "Place one in this scene first (Add object > Gameplay > Vehicle).");
            } else if (vehicleDriveObj_ == inst) {
                if (ImGui::Button("Stop driving")) vehicleDriveStop();
                ImGui::SameLine();
                ImGui::TextDisabled("W/S throttle, A/D steer, Shift brake, Space handbrake");
                ImGui::Separator();
                const vehiclesim::DriveState& st = vehicleDriveState_;
                ImGui::Checkbox("Hold throttle", &vehicleDriveHoldThrottle_);
                ImGui::SetNextItemWidth(scaled(220));
                ImGui::SliderFloat("Steer", &vehicleDriveSteer_, -1.0f, 1.0f, "%.2f");
                ImGui::Separator();
                ImGui::Text("Speed %.2f u/s   slip %.2f   steer %.1f deg", st.speed,
                            st.lateral, st.steerAngle);
                ImGui::Text("Gear %s%d   rpm %.0f   nitrous %.0f%%%s",
                            st.gear < 0 ? "R" : "", st.gear < 0 ? 1 : st.gear + 1,
                            st.rpm, st.nos * 100.0f,
                            st.nosActive ? "  (boosting - E)" : "  (E to boost)");
                ImGui::Text("Pitch %.1f  roll %.1f  %s", st.pitch, st.roll,
                            st.grounded ? "on the ground" : "airborne");
                // Slip against speed is the number that says whether the grip
                // setting is doing anything - a car that never slips is on
                // rails whatever the slider says.
                const float mag = std::fabs(st.speed) + std::fabs(st.lateral);
                ImGui::Text("Sideways fraction of travel: %.0f%%",
                            mag > 0.01f ? 100.0f * std::fabs(st.lateral) / mag : 0.0f);
            } else {
                if (ImGui::Button("Drive it")) {
                    vehicleDriveHoldThrottle_ = false;
                    vehicleDriveSteer_ = 0.0f;
                    vehicleDriveStart(inst);
                }
                ImGui::SameLine();
                ImGui::TextDisabled("Puts the car back where it was when you stop.");
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

            // --- The readout ---------------------------------------------------
            ImGui::Separator();
            ImGui::Checkbox("Show a driver's HUD", &v.showHud);
            prefHelp(
                "Speed, gear and the nitrous tank, while driving.\n"
                "Drawn as runtime text, so the font gets a glyph atlas.");
            if (v.showHud) {
                fontCombo(v.hudFont);
                ImGui::SetNextItemWidth(scaled(220));
                ImGui::DragFloat("Speed reads as", &v.hudSpeedScale, 0.05f, 0.0f,
                                 100.0f, "%.2f x units/s");
                prefHelp(
                    "What one world unit per second should show as. A unit is\n"
                    "whatever this project decided it is, so this cannot be\n"
                    "guessed: 3.6 turns metres per second into km/h.");
            }

            // --- Engine note --------------------------------------------------
            // The list is the project's own sounds, and only the LOOPING ones:
            // the loop lives in the encoded sample (adpenc -L over a
            // *-loop.wav), so a one-shot picked here would play for a fifth of
            // a second and stop. Offering it would be offering a broken choice.
            ImGui::Separator();
            ImGui::TextUnformatted("Engine sound");
            prefHelp(
                "A looping sample whose PITCH follows the engine speed.\n"
                "Only *-loop.wav sounds appear: the loop is baked into the\n"
                "sample by the build, so a one-shot cannot be held.");
            std::vector<const std::string*> loops;
            for (const std::string& snd : project_.sounds) {
                const size_t slash = snd.rfind('/');
                const std::string base = slash == std::string::npos
                                             ? snd
                                             : snd.substr(slash + 1);
                if (base.size() >= 9 &&
                    base.compare(base.size() - 9, 9, "-loop.wav") == 0)
                    loops.push_back(&snd);
            }
            ImGui::SetNextItemWidth(scaled(300));
            const std::string cur = v.engineSound.empty() ? "(silent)" : v.engineSound;
            if (ImGui::BeginCombo("Sample", cur.c_str())) {
                // No `changed` flag: this window compares Section::Vehicles'
                // JSON across its whole body, which covers a widget added later
                // by construction (the repo-wide sectionJson guard).
                if (ImGui::Selectable("(silent)##vehsndnone", v.engineSound.empty()))
                    v.engineSound.clear();
                for (size_t k = 0; k < loops.size(); ++k) {
                    const std::string label =
                        *loops[k] + "##vehsnd" + std::to_string(k);
                    if (ImGui::Selectable(label.c_str(), v.engineSound == *loops[k]))
                        v.engineSound = *loops[k];
                }
                ImGui::EndCombo();
            }
            if (loops.empty())
                ImGui::TextDisabled(
                    "No looping sounds in the project. Add a WAV named "
                    "*-loop.wav under res/sfx.");
            if (!v.engineSound.empty()) {
                ImGui::SetNextItemWidth(scaled(220));
                ImGui::SliderFloat("Pitch at idle", &v.enginePitchIdle, 0.25f, 2.0f,
                                   "%.2fx");
                prefHelp("Playback rate at idle, as a multiple of the sample's own.");
                ImGui::SetNextItemWidth(scaled(220));
                ImGui::SliderFloat("Pitch at redline", &v.enginePitchRedline, 0.5f,
                                   4.0f, "%.2fx");
                prefHelp(
                    "Playback rate at the redline. The SPU2 register saturates\n"
                    "around 4x the sample's own rate.");
                ImGui::SetNextItemWidth(scaled(220));
                ImGui::SliderFloat("Volume", &v.engineVolume, 0.0f, 100.0f, "%.0f");
            }
            ImGui::EndTabItem();
        }

        // --- Sounds -----------------------------------------------------------
        // The sound pack past the base engine loop (which lives in Driver,
        // next to the pitch curve it rides): the high-rev loop it crossfades
        // with, the tyre squeal riding the ONE slip number, and the gear
        // change one-shot.
        if (ImGui::BeginTabItem("Sounds")) {
            std::vector<const std::string*> loops2;
            for (const std::string& snd : project_.sounds) {
                const size_t slash = snd.rfind('/');
                const std::string base = slash == std::string::npos
                                             ? snd
                                             : snd.substr(slash + 1);
                if (base.size() >= 9 &&
                    base.compare(base.size() - 9, 9, "-loop.wav") == 0)
                    loops2.push_back(&snd);
            }
            const auto loopPicker = [&](const char* label, const char* tid,
                                        std::string& path, const char* tip) {
                ImGui::SetNextItemWidth(scaled(300));
                const std::string cur = path.empty() ? "(none)" : path;
                if (ImGui::BeginCombo(label, cur.c_str())) {
                    if (ImGui::Selectable(
                            (std::string("(none)##") + tid).c_str(),
                            path.empty()))
                        path.clear();
                    for (size_t k = 0; k < loops2.size(); ++k) {
                        const std::string l =
                            *loops2[k] + "##" + tid + std::to_string(k);
                        if (ImGui::Selectable(l.c_str(), path == *loops2[k]))
                            path = *loops2[k];
                    }
                    ImGui::EndCombo();
                }
                prefHelp(tip);
            };
            ImGui::TextDisabled(
                "The base engine loop and its pitch curve live in the Driver "
                "tab.");
            loopPicker("High-rev loop", "vehsndhigh", v.engineHighSound,
                       "A second engine loop the base one CROSSFADES with as\n"
                       "the revs rise - the era's two-sample engine. Both ride\n"
                       "the same authored pitch curve. Loops only (*-loop.wav).");
            ImGui::Separator();
            loopPicker("Tyre squeal loop", "vehsndscr", v.screechSound,
                       "Volume rides the tyre slip - the same number the smoke\n"
                       "and the telemetry read, so they always agree about when\n"
                       "a tyre has let go. Loops only (*-loop.wav).");
            if (!v.screechSound.empty()) {
                ImGui::SetNextItemWidth(scaled(220));
                ImGui::SliderFloat("Squeal volume", &v.screechVolume, 0.0f,
                                   100.0f, "%.0f");
            }
            ImGui::Separator();
            {
                ImGui::SetNextItemWidth(scaled(300));
                const std::string cur =
                    v.shiftSound.empty() ? "(none)" : v.shiftSound;
                if (ImGui::BeginCombo("Gear shift", cur.c_str())) {
                    if (ImGui::Selectable("(none)##vehsndshift",
                                          v.shiftSound.empty()))
                        v.shiftSound.clear();
                    for (size_t k = 0; k < project_.sounds.size(); ++k) {
                        const std::string l = project_.sounds[k] +
                                              "##vehshift" + std::to_string(k);
                        if (ImGui::Selectable(l.c_str(),
                                              v.shiftSound == project_.sounds[k]))
                            v.shiftSound = project_.sounds[k];
                    }
                    ImGui::EndCombo();
                }
                prefHelp(
                    "A ONE-SHOT played on every gear change while driving -\n"
                    "any sound qualifies, loops make no sense here. It borrows\n"
                    "a free script voice (priority 60), never the engine's.");
                if (!v.shiftSound.empty()) {
                    ImGui::SetNextItemWidth(scaled(220));
                    ImGui::SliderFloat("Shift volume", &v.shiftVolume, 0.0f,
                                       100.0f, "%.0f");
                }
            }
            if (loops2.empty())
                ImGui::TextDisabled(
                    "No looping sounds in the project. Add a WAV named "
                    "*-loop.wav under res/sfx.");
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
                ImGui::SetNextItemWidth(scaled(200));
                ImGui::SliderFloat("Body shine", &v.bodyShine, 0.0f, 1.0f, "%.2f");
                prefHelp(
                    "The paint's reflection pass. Rubber and near-black trim\n"
                    "stay MATTE (the bake splits them out - one extra submit),\n"
                    "and the wheels never shine.");
                if (v.bodyShine > 0.001f) {
                    ImGui::SetNextItemWidth(scaled(300));
                    // Plain buffer: imgui_stdlib is not in the build (the
                    // facts_ui growString note).
                    char rm[192];
                    std::snprintf(rm, sizeof(rm), "%s", v.bodyReflMap.c_str());
                    if (ImGui::InputText("Reflection map", rm, sizeof(rm)))
                        v.bodyReflMap = rm;
                    prefHelp(
                        "A res/ image used as a SPHERE MAP - vertical light\n"
                        "streaks are the era's wet-lacquer look\n"
                        "(tools/nfs-streak-map.py generates one). Empty = the\n"
                        "dynamic \"@sky\" env map, which mirrors the real sky\n"
                        "but reads faint: a smooth gradient has no features\n"
                        "you can see move.");
                }
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
