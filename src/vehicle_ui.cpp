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
#include <cstring>
#include <cfloat>
#include <filesystem>
#include <fstream>
#include <iterator>

#include "app.hpp"
#include "app_internal.hpp"
#include "placement.hpp"
#include "imgui.h"
#include "theme.hpp"

namespace {

void vehicleHelp(const char* tip) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 26);
        ImGui::TextUnformatted(tip);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

// A measured value, or the fallback when the bake could not produce one (a
// two-wheeled vehicle has no track, a wheel-less one no radius).
float r_geom(float measured, float fallback) {
    return measured > 1e-4f ? measured : fallback;
}

std::string bakeKey(const VehicleDef& v) {
    char buf[96];
    std::snprintf(buf, sizeof(buf), "|%d|%d|%d|%.3f|%d|%d|%d|", v.bodyTriBudget,
                  v.wheelTriBudget, v.mergeUntextured ? 1 : 0, v.bodyShine,
                  v.fastWheelTriBudget, v.glassOpacity < 1.0f ? 1 : 0,
                  v.drive.damage > 0.0f && v.drive.damageVisual > 0.5f &&
                      v.drive.damageLoose > 0.0f ? 1 : 0);
    char paint[96];
    std::snprintf(paint, sizeof(paint), "|%d|%.4f|%.4f|%.4f|",
                  v.paintEnabled ? 1 : 0, v.paintColor[0], v.paintColor[1],
                  v.paintColor[2]);
    return v.modelPath + buf + v.bodyReflMap + "|" + v.fastWheel + "|" +
           v.farModel + paint + v.paintMask;
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

// What the viewport draws for a body: the bake's model, with every shiny part
// that has a matte suffix (vehbake envLimits) split in two - the prefix keeps
// its reflection, the suffix is appended as a matte part - so the preview's
// cabin stays dark the way the console's env pass leaves it. Appended, so the
// lamp and glass part indices the viewport is handed stay valid.
tmdl::Model viewportBody(const tmdl::Model& body, const vehbake::Result& r) {
    tmdl::Model m = body;
    for (const auto& el : r.envLimitsList) {
        if (el.first < 0 || el.first >= (int)m.parts.size()) continue;
        tmdl::Part& p = m.parts[(size_t)el.first];
        const size_t cut = (size_t)el.second * 8;
        if (cut >= p.verts.size()) continue;
        tmdl::Part matte = p;
        matte.name += "-matte";
        matte.reflTexture.clear();
        matte.reflStrength = 0.0f;
        matte.verts.assign(p.verts.begin() + (long)cut, p.verts.end());
        matte.stripVerts.clear();
        matte.lods.clear();
        p.verts.resize(cut);
        m.parts.push_back(std::move(matte));
    }
    return m;
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
    opt.paintEnabled = v.paintEnabled;
    for (int a = 0; a < 3; ++a) opt.paintColor[a] = v.paintColor[a];
    if (!v.paintMask.empty()) opt.paintMaskPath = project_.filePath(v.paintMask);
    opt.fastWheel = v.fastWheel;
    opt.fastWheelTriBudget = v.fastWheelTriBudget;
    opt.glassSplit = v.glassOpacity < 1.0f;
    opt.loosePieces = v.drive.damage > 0.0f && v.drive.damageVisual > 0.5f &&
                      v.drive.damageLoose > 0.0f;
    if (!v.farModel.empty()) opt.farModel = project_.filePath(v.farModel);
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
    if (!v.fastWheel.empty())
        put("veh-" + v.id + "-wheelfast.tmdl", tmdl::write(c.result.fastWheel));
    if (!c.result.palettePng.empty())
        put("veh-" + v.id + "-palette.png",
            std::string((const char*)c.result.palettePng.data(),
                        c.result.palettePng.size()));
    if (!c.result.shadowPng.empty())
        put("veh-" + v.id + "-shadow.png",
            std::string((const char*)c.result.shadowPng.data(),
                        c.result.shadowPng.size()));
    for (const auto& texture : c.result.textures)
        put(fs::path(texture.path).filename().string(),
            std::string((const char*)texture.png.data(), texture.png.size()));

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
        // The lamp measurements follow the bake UNCONDITIONALLY - outside the
        // defaults guard above, on purpose: there is no authored value to
        // respect, and inside that guard a car whose wheelbase had been
        // adopted before its model grew lamp materials (the CC96) never
        // received its lamp part index at all. vehbake::adoptMeasured is the
        // one list, shared with the build's bakeProject.
        if (vehbake::adoptMeasured(v, c.result)) setDirty(true);
    }

    // A fresh bake is a different body: any damage preview of it is stale.
    if (vehDmgPreviewId_ == v.id) {
        vehDmgPreviewId_.clear();
        vehDmgPreviewDamage_ = 0.0f;
    }
    // Hand the geometry to the viewport so a placed instance draws. The
    // viewport gets the IN-MEMORY bake rather than re-reading the files: one
    // bake, and no .tmdl reader on the host that would have to agree with it.
    viewport_.setVehicleDraw(v.name, viewportBody(c.result.body, c.result), c.result.wheel,
                             ".res-baked/" + rel + "-palette.png", v.drive.wheelBase,
                             v.drive.track, v.drive.wheelRadius, v.drive.rideHeight,
                             c.result.lampPart, c.result.lampRearVerts);
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

void App::vehicleDamagePreviewHit(const VehicleDef& v, const vehiclesim::Impact& im) {
    if (v.drive.damageVisual <= 0.5f) return;
    auto it = vehicleBakes_.find(v.id);
    if (it == vehicleBakes_.end() || !it->second.ok) return;
    const vehbake::Result& r = it->second.result;
    if (vehDmgPreviewId_ != v.id) {
        vehicleDamagePreviewReset();
        vehDmgPreviewId_ = v.id;
        vehDmgPreviewBody_ = r.body;
    }
    // Every array the viewport or the console could draw at tier 0 - the list
    // and its strip twin - dents from its OWN rest copy, so both agree.
    const float md = v.drive.damageMaxDent;
    for (size_t pi = 0; pi < vehDmgPreviewBody_.parts.size() && pi < r.body.parts.size();
         ++pi) {
        tmdl::Part& dst = vehDmgPreviewBody_.parts[pi];
        const tmdl::Part& src = r.body.parts[pi];
        if (dst.verts.size() == src.verts.size())
            vehiclesim::applyDent(im, md, src.verts.data(), 8, dst.verts.data(), 8,
                                  (int)(src.verts.size() / 8), nullptr);
        if (dst.stripVerts.size() == src.stripVerts.size())
            vehiclesim::applyDent(im, md, src.stripVerts.data(), 8,
                                  dst.stripVerts.data(), 8,
                                  (int)(src.stripVerts.size() / 8), nullptr);
    }
    // Loose pieces, the runtime's rule (vehiclesim::pieceTakesHit): a piece
    // that comes off is collapsed to a point in BOTH arrays, like the console
    // collapses its range - the debris flight itself is the game's.
    vehDmgPreviewHp_.resize(r.pieces.size(), 0.0f);
    vehDmgPreviewGone_.resize(r.pieces.size(), 0);
    const float over = vehDmgPreviewOver_;
    for (size_t k = 0; k < r.pieces.size() && k < r.pieceLists.size(); ++k) {
        const vehiclesim::Piece& pc = r.pieces[k];
        if (vehDmgPreviewGone_[k] || pc.part < 0 ||
            pc.part >= (int)vehDmgPreviewBody_.parts.size())
            continue;
        const tmdl::Part& src = r.body.parts[(size_t)pc.part];
        const int lf = r.pieceLists[k].first, lc = r.pieceLists[k].second;
        float mn[3] = {1e30f, 1e30f, 1e30f}, mx[3] = {-1e30f, -1e30f, -1e30f};
        for (int i = lf; i < lf + lc && (size_t)(i * 8 + 2) < src.verts.size(); ++i)
            for (int a = 0; a < 3; ++a) {
                mn[a] = std::min(mn[a], src.verts[(size_t)i * 8 + a]);
                mx[a] = std::max(mx[a], src.verts[(size_t)i * 8 + a]);
            }
        if (!vehiclesim::pieceTakesHit(v.drive, pc.kind, im, over, mn, mx,
                                       vehDmgPreviewHp_[k]))
            continue;
        vehDmgPreviewGone_[k] = 1;
        vehDmgPreviewLost_ += std::string(vehDmgPreviewLost_.empty() ? "" : ", ") +
                              vehiclesim::pieceName(pc.kind);
    }
    for (size_t k = 0; k < r.pieces.size() && k < r.pieceLists.size(); ++k) {
        if (!vehDmgPreviewGone_[k]) continue;
        const vehiclesim::Piece& pc = r.pieces[k];
        tmdl::Part& dst = vehDmgPreviewBody_.parts[(size_t)pc.part];
        auto collapse = [](std::vector<float>& a, int first, int count) {
            if (count <= 0 || (size_t)(first + count) * 8 > a.size()) return;
            for (int i = first + 1; i < first + count; ++i)
                for (int c = 0; c < 3; ++c) a[(size_t)i * 8 + c] = a[(size_t)first * 8 + c];
        };
        collapse(dst.verts, r.pieceLists[k].first, r.pieceLists[k].second);
        if (dst.stripRun) collapse(dst.stripVerts, pc.first, pc.count);
    }
    viewport_.setVehicleDraw(v.name, viewportBody(vehDmgPreviewBody_, r), r.wheel,
                             ".res-baked/vehicles/veh-" + v.id + "-palette.png",
                             v.drive.wheelBase, v.drive.track, v.drive.wheelRadius,
                             v.drive.rideHeight, r.lampPart, r.lampRearVerts);
    vehiclePreviewKey_.clear();
}

void App::vehicleDamagePreviewReset() {
    if (vehDmgPreviewId_.empty()) return;
    const std::string id = vehDmgPreviewId_;
    vehDmgPreviewId_.clear();
    vehDmgPreviewDamage_ = 0.0f;
    vehDmgPreviewBody_ = tmdl::Model();
    vehDmgPreviewHp_.clear();
    vehDmgPreviewGone_.clear();
    vehDmgPreviewLost_.clear();
    vehiclePreviewKey_.clear();
    for (const VehicleDef& v : project_.vehicles) {
        if (v.id != id) continue;
        auto it = vehicleBakes_.find(v.id);
        if (it == vehicleBakes_.end() || !it->second.ok) return;
        const vehbake::Result& r = it->second.result;
        viewport_.setVehicleDraw(v.name, viewportBody(r.body, r), r.wheel,
                                 ".res-baked/vehicles/veh-" + v.id + "-palette.png",
                                 v.drive.wheelBase, v.drive.track, v.drive.wheelRadius,
                                 v.drive.rideHeight, r.lampPart, r.lampRearVerts);
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
    vehicleDriveAccum_ = 0.0f;
    for (int a = 0; a < 3; ++a) vehicleDriveState_.pos[a] = o.position[a];
    vehicleDriveState_.yaw = o.rotation[1];
    vehicleDriveObj_ = objectIndex;
    vehDmgPreviewSerial_ = 0;

    // The roads this scene draws, as the viewport tessellates them (same
    // height function, same junction pairing), so the test drive stands on
    // the asphalt the author sees - and the console car, which reads
    // groundSurfaceAt, stands on it too. Built once per drive: a test drive is
    // not an edit, and a road dragged mid-drive is picked up by the next one.
    vehicleDriveRoads_ = roadgen::Surface{};
    const auto terrainAt = [this](float x, float z) { return viewport_.terrainHeight(x, z); };
    for (size_t i = 0; i < objs.size(); ++i) {
        const SceneObject& r = objs[i];
        if (r.type != PrimitiveType::Road || r.roadPoints.size() < 4) continue;
        std::vector<roadgen::Vertex> tris;
        const float lift = roadgen::rankLift(r.roadRank);  // the console's lift
        const auto liftedAt = [&](float x, float z) { return terrainAt(x, z) + lift; };
        // Soft edges (1.144.0): the core, then the faded bands - a tyre on a
        // band is only partly on the road (its cover is the band's alpha).
        const roadgen::EdgeFade ef = roadgen::edgeFadeFor(r.roadWidth, r.roadEdgeFade);
        roadgen::tessellate(r.roadPoints, ef.coreWidth, liftedAt, tris, {},
                            r.roadSampleStep, ef.uInset);
        vehicleDriveRoads_.add(tris, r.roadGrip);
        if (ef.columns > 0) {
            std::vector<roadgen::SpillVertex> ev;
            roadgen::tessellateEdges(r.roadPoints, r.roadWidth, r.roadSampleStep,
                                     r.roadEdgeFade, ev);
            std::vector<roadgen::Vertex> et;
            std::vector<float> cov;
            for (const roadgen::SpillVertex& v : ev) {
                et.push_back({v.x, liftedAt(v.x, v.z) + roadgen::kLift, v.z, v.u, v.v});
                cov.push_back(v.a);
            }
            vehicleDriveRoads_.addEdge(et, r.roadGrip, cov);
        }
    }
    // Crossings (junction patches, overlays, spills): the codegen's own plan
    // (roadgen::planCrossings) over the same roads and the scene's junction
    // overrides, so the test drive stands where the console car will.
    {
        const std::vector<roadgen::CrossingRoad> cr = project::crossingRoads(objs);
        if (cr.size() >= 2)
            roadgen::addCrossingsToSurface(
                vehicleDriveRoads_, cr,
                roadgen::planCrossings(cr, project_.active().roadJunctions), terrainAt);
    }
    vehicleDriveRoads_.build();
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
    // The drive's dents go with it, like its position.
    vehicleDamagePreviewReset();
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
    // The console's pedals (vehiclesim::pedals): W is R2, S is L2 - brake
    // while rolling forward, reverse once stopped.
    float gas = vehicleDriveHoldThrottle_ ? 1.0f : 0.0f, brakeRev = 0.0f;
    in.steer += vehicleDriveSteer_;
    if (!ImGui::GetIO().WantTextInput) {
        if (ImGui::IsKeyDown(ImGuiKey_W)) gas = 1.0f;
        if (ImGui::IsKeyDown(ImGuiKey_S)) brakeRev = 1.0f;
        if (ImGui::IsKeyDown(ImGuiKey_A)) in.steer -= 1.0f;
        if (ImGui::IsKeyDown(ImGuiKey_D)) in.steer += 1.0f;
        if (ImGui::IsKeyDown(ImGuiKey_Space)) in.handbrake = true;
        if (ImGui::IsKeyDown(ImGuiKey_LeftShift)) brakeRev = 1.0f;
        // E, so nitrous is testable at all: the two branches that change
        // acceleration and top speed were unreachable in the host copy, and a
        // divergence in them would have been invisible until it shipped.
        if (ImGui::IsKeyDown(ImGuiKey_E)) in.nos = true;
    }
    vehiclesim::pedals(vehicleDriveState_.speed, gas, brakeRev, in);

    // The SAME sampler the placement snap uses, so the car drives on exactly
    // the heightfield the editor draws - and, over a road, on the road mesh
    // drawn roadgen::kLift above it: the generated runtime's groundSurfaceAt
    // (max of the two). Terrain alone put every tyre 0.12 into the asphalt on
    // both twins (docs/vehicles.md, "Wheels on the road surface").
    const vehiclesim::HeightFn ground = [this](float x, float z) {
        const float terrain =
            project_.active().terrain.enabled ? viewport_.terrainHeight(x, z) : -1e6f;
        const float road = vehicleDriveRoads_.at(x, z);
        return road > terrain ? road : terrain;
    };
    // The surface under a tyre (off-road 1.136.0, road grip 1.137.0): a road
    // triangle answers its road's grip, anything else is off the road. The
    // runtime also counts an object floor as paved (grip 1), which the test
    // drive has no model of.
    // Off the road the painted terrain layers bring their Grip (1.142.0).
    std::vector<float> layerGrips;
    for (const TerrainLayer& l : project_.active().terrainLayers)
        layerGrips.push_back(l.grip);
    const vehiclesim::SurfaceFn surface = [this, layerGrips](float x, float z) {
        vehiclesim::SurfaceSample s;
        if (vehicleDriveRoads_.at(x, z, &s.grip, &s.cover) <= -1.0e29f) s.cover = 0.0f;
        if (s.cover < 1.0f) s.terrainGrip = viewport_.terrainLayerGrip(x, z, layerGrips);
        return s;
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
    // The runtime twin's wall rules exactly (buildVehicleColliders in
    // templates.cpp): a box is a wall when its top is above feet + 0.5 and its
    // bottom below feet + 0.9 (lower tops are floors the wheels ride), and it
    // is inflated by 0.35 (the walker's radius) on both horizontal axes. The
    // test drive used the bare box with its own height band, so a car touched
    // walls 0.35 later here than on the console.
    const vehiclesim::SolidFn solid = [&](float x, float z, float feetY) {
        constexpr float kPad = 0.35f;
        for (const placement::Aabb& b : solids)
            if (x > b.mn[0] - kPad && x < b.mx[0] + kPad &&
                z > b.mn[2] - kPad && z < b.mx[2] + kPad &&
                b.mx[1] > feetY + 0.5f && b.mn[1] < feetY + 0.9f)
                return true;
        return false;
    };
    // The instance's uniform scale rides into the sim the way the runtime
    // applies it (docs/vehicles.md): the example's car IS scale 1.5, and
    // without this the test drive tuned a car the console never runs.
    // FIXED 1/50 s steps (1.135.4), the PAL console's own step: several
    // rules (the head-on scrub, the attitude spring's per-step response) act
    // once per step, so a 144 Hz editor frame fed the sim at its own rate
    // drove a different car from the one the PS2 runs at 50 fps. The
    // accumulator keeps the remainder; a long hitch runs at most 5 steps
    // rather than a burst.
    vehicleDriveAccum_ += ImGui::GetIO().DeltaTime;
    if (vehicleDriveAccum_ > 0.1f) vehicleDriveAccum_ = 0.1f;
    constexpr float kStep = 1.0f / 50.0f;
    while (vehicleDriveAccum_ >= kStep) {
        vehicleDriveAccum_ -= kStep;
        vehiclesim::step(def->drive, in, kStep, ground, vehicleDriveState_, solid,
                         o.scale[0] > 0.001f ? o.scale[0] : 1.0f, surface);
    }

    // A hit that dented: show it on the car being driven (the preview copy),
    // at the instance's scale the way the console's local vertices carry it.
    if (vehicleDriveState_.impactSerial != vehDmgPreviewSerial_) {
        vehDmgPreviewSerial_ = vehicleDriveState_.impactSerial;
        auto bk = vehicleBakes_.find(def->id);
        if (bk != vehicleBakes_.end() && bk->second.ok) {
            const tmdl::Model& body = bk->second.result.body;
            vehiclesim::Impact im;
            // The preview body is unscaled, so the impact is too.
            if (vehiclesim::impactFromDelta(def->drive, vehicleDriveState_.yaw,
                                            vehicleDriveState_.impactDv[0],
                                            vehicleDriveState_.impactDv[1], body.min,
                                            body.max, 1.0f, im, nullptr)) {
                vehDmgPreviewOver_ =
                    std::hypot(vehicleDriveState_.impactDv[0], vehicleDriveState_.impactDv[1]) -
                    std::max(def->drive.damageThreshold, 0.0f);
                vehicleDamagePreviewHit(*def, im);
                vehDmgPreviewDamage_ = vehicleDriveState_.damage;
            }
        }
    }

    for (int a = 0; a < 3; ++a) o.position[a] = vehicleDriveState_.pos[a];
    // Negated like the runtime's write: the sim's pitch is "positive = nose
    // up", a positive rotX is nose DOWN (see updateVehicles).
    vehiclesim::bodyRotation(vehicleDriveState_.pitch + vehicleDriveState_.leanPitch,
                            vehicleDriveState_.yaw,
                            vehicleDriveState_.roll + vehicleDriveState_.leanRoll,
                            o.rotation);
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

void App::drawVehiclePreview(const VehicleDef& tuning, int index) {
    ImGui::SeparatorText("Live preview");
    if (index < 0 || index >= (int)project_.vehicles.size()) {
        ImGui::TextWrapped("Add a car and choose its model to preview it.");
        if (vehicleEnginePreview_) vehicleEnginePreview_->stop();
        vehiclePreviewSound_ = false;
        vehicleAudioKey_.clear();
        return;
    }
    const VehicleDef& v = project_.vehicles[index];
    vehicleRefreshBake(index, false);
    const auto it = vehicleBakes_.find(v.id);
    if (it == vehicleBakes_.end() || !it->second.ok) {
        ImGui::TextWrapped("Choose a valid model in the Model tab.");
        if (vehicleEnginePreview_) vehicleEnginePreview_->stop();
        vehiclePreviewSound_ = false;
        vehicleAudioKey_.clear();
        return;
    }
    if (!vehiclePreview_) {
        auto preview = std::make_unique<Viewport>();
        if (!preview->init()) { ImGui::TextDisabled("Preview renderer unavailable."); return; }
        TerrainConfig floor;
        floor.width = floor.depth = 20;
        preview->setTerrain(floor, 4);
        const float grey[3] = {.24f, .26f, .29f}, tile[2] = {1, 1};
        preview->setTerrainMaterial("", grey, true, tile);
        const float horizon[3] = {.22f, .25f, .29f}, sky[3] = {.10f, .12f, .15f};
        preview->setSky(horizon, sky, true);
        preview->setGuidesVisible(false);
        const float light[3] = {-0.4f, -1.0f, -0.6f}, white[3] = {1, 1, 1};
        preview->setLighting(light, .65f, .6f, white, 1.0f);
        vehiclePreview_ = std::move(preview);
    }
    auto frameCar = [&]() {
        const float target[3] = {0, v.drive.wheelRadius + .5f, 0};
        vehiclePreview_->setCamState(.65f, .3f,
            std::max(4.0f, v.drive.wheelBase * 2.3f), target);
    };
    const std::string modelIdentity = project_.dir + "|" + v.id;
    if (vehiclePreviewModel_ != modelIdentity) {
        vehiclePreviewModel_ = modelIdentity;
        vehiclePreviewKey_.clear();
        vehicleAudioKey_.clear();
        vehiclePreviewPlay_ = vehiclePreviewSound_ = vehiclePreviewFast_ = false;
        vehiclePreviewSpeed_ = vehiclePreviewRevs_ = vehiclePreviewSpin_ = 0;
        if (vehicleEnginePreview_) vehicleEnginePreview_->stop();
        vehiclePreview_->clearVehicleDraws();
        vehiclePreview_->setProjectDir(project_.dir);
        frameCar();
    }
    const float rate = vehiclePreviewSpeed_ / 3.6f / std::max(.01f, v.drive.wheelRadius);
    const auto& baked = it->second.result;
    const bool hasFast = !baked.fastWheel.parts.empty() && tuning.drive.fastWheelSpeed > 0;
    if (!hasFast) vehiclePreviewFast_ = false;
    else if (rate > tuning.drive.fastWheelSpeed) vehiclePreviewFast_ = true;
    else if (rate < tuning.drive.fastWheelSpeed * .8f) vehiclePreviewFast_ = false;
    const std::string key = it->second.key + "|" + v.name + (vehiclePreviewFast_ ? "|fast" : "|normal") +
        "|" + std::to_string(v.drive.track) + "|" + std::to_string(v.drive.wheelBase) +
        "|" + std::to_string(v.drive.wheelRadius) + "|" + std::to_string(v.drive.rideHeight);
    if (key != vehiclePreviewKey_) {
        vehiclePreviewKey_ = key;
        // Baked textures keep their path when paint changes. Drop this tool's
        // private cache before replacing geometry so the image reflects edits.
        vehiclePreview_->clearVehicleDraws();
        vehiclePreview_->invalidateAssets();
        const auto& body = vehDmgPreviewId_ == v.id ? vehDmgPreviewBody_ : baked.body;
        vehiclePreview_->setVehicleDraw(v.name, viewportBody(body, baked),
            vehiclePreviewFast_ ? baked.fastWheel : baked.wheel,
            ".res-baked/vehicles/veh-" + v.id + "-palette.png",
            v.drive.wheelBase, v.drive.track, v.drive.wheelRadius, v.drive.rideHeight,
            baked.lampPart, baked.lampRearVerts);
    }
    if (vehiclePreviewPlay_)
        vehiclePreviewSpin_ = std::fmod(vehiclePreviewSpin_ + rate *
            std::min(ImGui::GetIO().DeltaTime, .1f), 6.2831853f);
    bool steered[4] = {true, true, false, false};
    for (size_t w = 0; w < baked.detection.wheels.size() && w < 4; ++w) {
        steered[w] = baked.detection.wheels[w].steered;
        for (const auto& row : v.wheels)
            if (row.node == baked.detection.wheels[w].nodeName) steered[w] = row.steered;
    }
    vehiclePreview_->setVehiclePreviewPose(vehiclePreviewSpin_,
        vehiclePreviewSteer_ * .0174532925f, steered);
    SceneObject car;
    car.type = PrimitiveType::Vehicle;
    car.vehicleDef = v.name;
    car.position[1] = v.drive.wheelRadius;
    const float width = ImGui::GetContentRegionAvail().x;
    const float height = std::max(scaled(180), std::min(scaled(300), width * .8f));
    const auto tex = vehiclePreview_->render((int)width, (int)height, {car}, {}, -1);
    const ImVec2 imagePos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("Vehicle preview canvas", ImVec2(width, height));
    ImGui::GetWindowDrawList()->AddImage((ImTextureID)(intptr_t)tex, imagePos,
        ImVec2(imagePos.x + width, imagePos.y + height), ImVec2(0, 1), ImVec2(1, 0));
    ImGui::SetItemTooltip("Drag to orbit. Scroll to zoom.");
    if (ImGui::IsItemHovered()) {
        const auto& io = ImGui::GetIO();
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left))
            vehiclePreview_->orbit(io.MouseDelta.x, io.MouseDelta.y);
        if (io.MouseWheel != 0) vehiclePreview_->zoom(io.MouseWheel);
    }
    if (ImGui::SmallButton("Reset view")) frameCar();
    ImGui::SameLine();
    ImGui::TextDisabled("%s wheels", vehiclePreviewFast_ ? "Fast" : "Normal");
    ImGui::Checkbox("Spin wheels", &vehiclePreviewPlay_);
    ImGui::SetNextItemWidth(-scaled(95));
    ImGui::SliderFloat("Speed", &vehiclePreviewSpeed_, 0, 240, "%.0f km/h");
    ImGui::SetNextItemWidth(-scaled(95));
    ImGui::SliderFloat("Steering", &vehiclePreviewSteer_, -40, 40, "%.0f deg");
    ImGui::SeparatorText("Engine audition");
    ImGui::SetNextItemWidth(-scaled(95));
    ImGui::SliderFloat("Revs", &vehiclePreviewRevs_, 0, 1, "%.2f");
    ImGui::TextDisabled("%.0f RPM", tuning.drive.idleRpm + vehiclePreviewRevs_ *
        (tuning.drive.redlineRpm - tuning.drive.idleRpm));
    ImGui::Checkbox("Listen to engine", &vehiclePreviewSound_);
    if (!vehicleEnginePreview_) vehicleEnginePreview_ = std::make_unique<audiopreview::EngineLoop>();
    const std::string audibleHigh = tuning.engineHighEnabled ? tuning.engineHighSound : "";
    const std::string audioKey = project_.dir + "|" + tuning.engineSound + "|" + audibleHigh;
    if (vehiclePreviewSound_) {
        const float highStart = std::clamp(tuning.engineHighStart, 0.0f, 0.95f);
        const float highMix = std::clamp((vehiclePreviewRevs_ - highStart) /
            (1.0f - highStart), 0.0f, 1.0f);
        vehicleEnginePreview_->update(tuning.enginePitchIdle + vehiclePreviewRevs_ *
            (tuning.enginePitchRedline - tuning.enginePitchIdle),
            tuning.engineHighPitchStart + highMix *
            (tuning.engineHighPitchEnd - tuning.engineHighPitchStart),
            highMix, tuning.engineVolume);
        if (vehicleAudioKey_ != audioKey) {
            const auto full = [&](const std::string& path) {
                return path.empty() ? std::string() : (std::filesystem::path(project_.dir) / path).string();
            };
            if (vehicleEnginePreview_->start(full(tuning.engineSound), full(audibleHigh)))
                vehicleAudioKey_ = audioKey;
            else vehiclePreviewSound_ = false;
        }
    } else {
        vehicleEnginePreview_->stop();
        vehicleAudioKey_.clear();
    }
    if (!vehicleEnginePreview_->error().empty())
        ImGui::TextWrapped("%s", vehicleEnginePreview_->error().c_str());
}

void App::drawVehicleWindow() {
    if (!showVehicles_ || !hasProject_) {
        if (vehicleEnginePreview_) vehicleEnginePreview_->stop();
        vehiclePreviewSound_ = false;
        vehicleAudioKey_.clear();
        return;
    }
    ImGui::SetNextWindowSizeConstraints(ImVec2(scaled(1120), scaled(520)), ImVec2(FLT_MAX, FLT_MAX));
    ImGui::SetNextWindowSize(ImVec2(scaled(1280), scaled(720)), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Vehicle Editor", &showVehicles_)) {
        if (vehicleEnginePreview_) vehicleEnginePreview_->stop();
        vehiclePreviewSound_ = false;
        vehicleAudioKey_.clear();
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
    if (vehicleSel_ == -1) vehicleSel_ = defs.empty() ? -2 : 0;

    // --- the definition list ------------------------------------------------
    ImGui::BeginChild("##vehlist", ImVec2(scaled(180), 0), true);
    if (ImGui::Selectable("Global defaults", vehicleSel_ == -2)) vehicleSel_ = -2;
    ImGui::Separator();
    for (int i = 0; i < (int)defs.size(); ++i) {
        // An explicit ##id: two definitions may not share a name, but one is
        // being TYPED for a moment during a rename, and a Selectable's label
        // is its ImGui id.
        const std::string label = defs[i].name + "##vehsel" + std::to_string(i);
        if (ImGui::Selectable(label.c_str(), vehicleSel_ == i)) vehicleSel_ = i;
    }
    ImGui::EndChild();
    ImGui::SameLine();

    ImGui::BeginChild("Vehicle settings", ImVec2(ImGui::GetContentRegionAvail().x - scaled(330), 0));
    ImGui::BeginGroup();
    if (ImGui::Button("New vehicle")) {
        VehicleDef v;
        v.id = project::newObjectId();
        v.name = uniqueName(defs, "Car");
        // A NEW car can be damaged; the struct default stays 0 so every
        // definition saved before damage existed keeps driving as it did.
        v.drive.damage = 1.0f;
        v.drive.lampGlow = 1.0f;
        v.inheritDefaults = true;
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

    if (vehicleSel_ == -1) {
        ImGui::Separator();
        ImGui::TextDisabled("No vehicle selected.");
        ImGui::EndGroup();
        ImGui::EndChild();
        if (project::sectionJson(project_, project::Section::Vehicles) != before)
            commitChange();
        ImGui::End();
        return;
    }

    const bool global = vehicleSel_ == -2;
    project::applyVehicleDefaults(project_);
    VehicleDef& v = global ? project_.vehicleDefaults : defs[vehicleSel_];
    if (!global) vehicleRefreshBake(vehicleSel_, false);
    const VehicleBakeCache* bake = nullptr;
    if (auto it = vehicleBakes_.find(v.id); it != vehicleBakes_.end()) bake = &it->second;

    ImGui::Separator();
    if (!global) {
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

    ImGui::TextDisabled(global ? "Shared defaults for all vehicles" :
        "Uses global defaults. Edit a value to customise it.");
    vehicleHelp("Each edited value becomes a local override. Use defaults resets the current section; geometry stays local.");
    const VehicleDef tuningBefore = v;
    std::vector<std::string> resetKeys;
    auto sectionDefaults = [&](const char* section) {
        if (global) return;
        bool custom = false;
        visitVehicleTuning(v, project_.vehicleDefaults,
            [&](const std::string& key, const char* group, auto&, const auto&) {
                if (std::strcmp(group, section) == 0 && vehicleTuningOverride(v, key)) custom = true;
            });
        ImGui::BeginDisabled(!custom);
        if (ImGui::SmallButton("Use defaults")) {
            visitVehicleTuning(v, project_.vehicleDefaults,
                [&](const std::string& key, const char* group, auto& value, const auto& base) {
                    if (std::strcmp(group, section) != 0) return;
                    resetKeys.push_back(key);
                    auto& keys = v.tuningOverrides;
                    keys.erase(std::remove(keys.begin(), keys.end(), key), keys.end());
                    value = base;
                });
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled(custom ? "Custom values" : "All values from defaults");
        ImGui::Separator();
    };
    auto specControl = [&](const vehiclesim::SpecField& f) {
        if (global && vehicleGeometryKey(f.key)) return;
        ImGui::PushID(f.key);
        if (std::strcmp(f.key, "damageVisual") == 0 ||
            std::strcmp(f.key, "damageMechanical") == 0) {
            bool enabled = *f.value > 0.5f;
            if (ImGui::Checkbox(f.label, &enabled)) {
                *f.value = enabled ? 1.0f : 0.0f;
                if (std::strcmp(f.key, "damageVisual") == 0) vehicleDamagePreviewReset();
            }
            if (f.tip && f.tip[0]) vehicleHelp(f.tip);
            ImGui::PopID();
            return;
        }
        if (std::strcmp(f.key, "nosCapacity") == 0) {
            bool enabled = *f.value > 0.001f;
            if (ImGui::Checkbox("Has nitrous", &enabled))
                *f.value = enabled ? 5.0f : 0.0f;
        }
        ImGui::SetNextItemWidth(scaled(180));
        ImGui::SliderFloat(f.label, f.value, f.min, f.max, "%.4g");
        if (f.tip && f.tip[0]) vehicleHelp(f.tip);
        ImGui::PopID();
    };

    if (ImGui::BeginTabBar("##vehtabs")) {
        // --- Model ----------------------------------------------------------
        if (!global && ImGui::BeginTabItem("Model")) {
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
            vehicleHelp(
                "One .glb or .fbx holding the body AND the wheels. The wheels are\n"
                "found by their geometry, so their node names do not matter.");

            ImGui::SeparatorText("Body paint colour");
            ImGui::Checkbox("Override paint colour", &v.paintEnabled);
            if (v.paintEnabled) {
                ImGui::SetNextItemWidth(scaled(220));
                ImGui::ColorEdit3("Paint colour", v.paintColor);
                ImGui::TextDisabled("Mask: %s", v.paintMask.empty()
                    ? "<none>" : v.paintMask.c_str());
                (void)pickProjectTexture("vehicle-paint-mask", v.paintMask);
                if (!v.paintMask.empty()) {
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Clear mask")) v.paintMask.clear();
                }
                vehicleHelp("Grayscale paint mask: white recolours paint; black protects glass, trim and wheels. Untextured paint materials need no mask.");
                if (v.paintMask.empty() && bake && bake->ok &&
                    !bake->result.textures.empty())
                    ImGui::TextDisabled("Choose a mask for textured paint.");
            }

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
                ImGui::Text("Imported: %d body triangles, %d per wheel", r.bodyTris, r.wheelTris);
                if (ImGui::CollapsingHeader("Import details"))
                    for (const std::string& n : r.notes) ImGui::TextWrapped("%s", n.c_str());

                if (r.detection.frontAssumed) {
                    ImGui::PushStyleColor(ImGuiCol_Text, theme::semantics().warn);
                    ImGui::TextWrapped(
                        "Check the front direction in the preview.");
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
            sectionDefaults("driving");
            const std::vector<vehiclesim::SpecField> fields =
                vehiclesim::specFields(v.drive);
            for (const vehiclesim::SpecField& f : fields) {
                if (std::strncmp(f.key, "damage", 6) == 0) continue;  // Damage tab
                if (std::strncmp(f.key, "lamp", 4) == 0) continue;    // Effects tab
                if (std::strncmp(f.key, "feel", 4) == 0) continue;    // Effects tab
                const char* group = nullptr;
                if (!global && std::strcmp(f.key, "wheelBase") == 0) group = "Wheel placement";
                if (std::strcmp(f.key, "topSpeed") == 0) group = "Power and speed";
                if (std::strcmp(f.key, "maxSteerDeg") == 0) group = "Steering and grip";
                if (std::strcmp(f.key, "offroadGrip") == 0) group = "Road surfaces";
                if (std::strcmp(f.key, "gravity") == 0) group = "Suspension and body";
                if (std::strcmp(f.key, "gears") == 0) group = "Engine and gears";
                if (std::strcmp(f.key, "nosCapacity") == 0) group = "Nitrous";
                if (std::strcmp(f.key, "fastWheelSpeed") == 0) group = "Fast wheel swap";
                if (group) ImGui::SeparatorText(group);
                specControl(f);
            }
            ImGui::EndTabItem();
        }

        // --- Damage ---------------------------------------------------------
        // The "damage*" spec fields plus test hits: the dent is computed by the
        // same vehiclesim functions the console's twin mirrors, on a copy of
        // the baked body, so what shows here is where the game will dent.
        if (ImGui::BeginTabItem("Damage")) {
            sectionDefaults("damage");
            const std::vector<vehiclesim::SpecField> fields =
                vehiclesim::specFields(v.drive);
            for (const vehiclesim::SpecField& f : fields) {
                if (std::strncmp(f.key, "damage", 6) != 0) continue;
                specControl(f);
            }
            ImGui::SeparatorText("Preview");
            auto bk = vehicleBakes_.find(v.id);
            const bool baked = bk != vehicleBakes_.end() && bk->second.ok;
            if (global) {
                ImGui::TextDisabled("Select a vehicle to preview impacts.");
            } else if (v.drive.damage <= 0.0f) {
                ImGui::TextDisabled("Damage strength 0: this car cannot be damaged.");
            } else if (!baked) {
                ImGui::TextDisabled("Import a model first.");
            } else {
                ImGui::SetNextItemWidth(scaled(220));
                ImGui::SliderFloat("Hit speed", &vehDmgTestSpeed_, 0.0f, 40.0f, "%.1f u/s");
                vehicleHelp("The speed change of the test hit - a head-on at this speed.");
                const tmdl::Model& body = bk->second.result.body;
                // Local hit directions: the velocity change points AWAY from
                // what was hit, so a front hit is a push backwards.
                const struct { const char* label; float dr, df; } hits[] = {
                    {"Hit front", 0.0f, -1.0f}, {"Hit rear", 0.0f, 1.0f},
                    {"Hit left", 1.0f, 0.0f},   {"Hit right", -1.0f, 0.0f}};
                for (int h = 0; h < 4; ++h) {
                    if (h % 2) ImGui::SameLine();
                    if (ImGui::Button(hits[h].label)) {
                        // Yaw 0: local and world axes agree.
                        vehiclesim::Impact im;
                        float add = 0.0f;
                        if (vehiclesim::impactFromDelta(
                                v.drive, 0.0f, hits[h].dr * vehDmgTestSpeed_,
                                hits[h].df * vehDmgTestSpeed_, body.min, body.max,
                                1.0f, im, &add)) {
                            vehDmgPreviewOver_ =
                                vehDmgTestSpeed_ - std::max(v.drive.damageThreshold, 0.0f);
                            vehicleDamagePreviewHit(v, im);
                            vehDmgPreviewDamage_ =
                                std::min(1.0f, vehDmgPreviewDamage_ + add);
                        }
                    }
                }
                if (ImGui::Button("Repair")) vehicleDamagePreviewReset();
                if (vehDmgPreviewId_ == v.id)
                {
                    const size_t np = bk->second.result.pieces.size();
                    if (np == 0)
                        ImGui::TextDisabled("No loose pieces found on this body.");
                    else if (vehDmgPreviewId_ == v.id && !vehDmgPreviewLost_.empty())
                        ImGui::Text("Lost: %s", vehDmgPreviewLost_.c_str());
                    else
                        ImGui::TextDisabled("%zu loose piece(s) - hit hard to knock them off.",
                                            np);
                }
                if (vehDmgPreviewId_ == v.id)
                    ImGui::Text("Damage %.0f%%   power %.0f%%%s",
                                vehDmgPreviewDamage_ * 100.0f,
                                vehiclesim::damagePerformance(v.drive, vehDmgPreviewDamage_) *
                                    100.0f,
                                v.drive.damageVisual > 0.5f &&
                                vehDmgPreviewDamage_ >= v.drive.damageSmoke
                                    ? "   engine smokes" : "");
                else
                    ImGui::TextDisabled("Every placed %s shows the preview; the "
                                        "project is not changed.", v.name.c_str());
            }
            ImGui::EndTabItem();
        }

        // --- Test drive -------------------------------------------------------
        // The reason vehiclesim is host-only: the same step() the console will
        // run, driven from the keyboard against the real scene's terrain, so
        // grip and acceleration are tuned in a "slider, feel, slider" loop
        // instead of "slider, four minutes of Docker, PCSX2".
        if (!global && ImGui::BeginTabItem("Test drive")) {
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
                ImGui::TextDisabled("W gas, S brake / reverse, A/D steer, Space handbrake");
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
                if (v.drive.damage > 0.0f)
                    ImGui::Text("Damage %.0f%%  (hit a wall to dent it)",
                                st.damage * 100.0f);
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
            sectionDefaults("driver");
            ImGui::SetNextItemWidth(scaled(220));
            ImGui::SliderFloat("Camera distance", &v.camDist, 1.0f, 20.0f, "%.2f");
            ImGui::SetNextItemWidth(scaled(220));
            ImGui::SliderFloat("Camera height", &v.camHeight, 0.0f, 10.0f, "%.2f");
            ImGui::SetNextItemWidth(scaled(220));
            ImGui::SliderFloat("Camera pitch", &v.camPitch, -30.0f, 60.0f, "%.1f");
            ImGui::Separator();
            ImGui::SetNextItemWidth(scaled(300));
            ImGui::BeginDisabled(global);
            ImGui::DragFloat3("Exit offset", v.exitOffset, 0.05f);
            ImGui::EndDisabled();
            vehicleHelp(
                "Where the player is put down on getting out, relative to the\n"
                "car: x right, y up, z forward. The driver's door.");

            // --- The readout ---------------------------------------------------
            ImGui::Separator();
            ImGui::Checkbox("Show a driver's HUD", &v.showHud);
            vehicleHelp(
                "Speed, gear and the nitrous tank, while driving.\n"
                "Drawn as runtime text, so the font gets a glyph atlas.");
            if (v.showHud) {
                fontCombo(v.hudFont);
                ImGui::SetNextItemWidth(scaled(220));
                ImGui::DragFloat("Speed reads as", &v.hudSpeedScale, 0.05f, 0.0f,
                                 100.0f, "%.2f x units/s");
                vehicleHelp(
                    "What one world unit per second should show as. A unit is\n"
                    "whatever this project decided it is, so this cannot be\n"
                    "guessed: 3.6 turns metres per second into km/h.");
            }

            ImGui::EndTabItem();
        }

        // --- Sounds -----------------------------------------------------------
        // All vehicle audio: idle/high-rev loops, tyre squeal and gear shift.
        if (ImGui::BeginTabItem("Sounds")) {
            sectionDefaults("sounds");
            const auto& sounds = project_.sounds;
            const auto loopPicker = [&](const char* label, const char* tid,
                                        std::string& path, const char* tip) {
                ImGui::SetNextItemWidth(scaled(300));
                const std::string cur = path.empty() ? "(none)" : path;
                if (ImGui::BeginCombo(label, cur.c_str())) {
                    if (ImGui::Selectable(
                            (std::string("(none)##") + tid).c_str(),
                            path.empty()))
                        path.clear();
                    for (size_t k = 0; k < sounds.size(); ++k) {
                        const std::string l =
                            sounds[k] + "##" + tid + std::to_string(k);
                        if (ImGui::Selectable(l.c_str(), path == sounds[k]))
                            path = sounds[k];
                    }
                    ImGui::EndCombo();
                }
                vehicleHelp(tip);
            };
            // Selecting a continuous vehicle sound declares loop intent to
            // both build backends; the source filename is unrestricted.
            ImGui::SeparatorText("Engine sound");
            vehicleHelp("Choose any imported WAV. The build encodes vehicle engine and tyre samples as loops; pitch follows engine speed.");
            ImGui::SetNextItemWidth(scaled(300));
            const std::string cur = v.engineSound.empty() ? "(silent)" : v.engineSound;
            if (ImGui::BeginCombo("Sample", cur.c_str())) {
                // No `changed` flag: this window compares Section::Vehicles'
                // JSON across its whole body, which covers a widget added later
                // by construction (the repo-wide sectionJson guard).
                if (ImGui::Selectable("(silent)##vehsndnone", v.engineSound.empty()))
                    v.engineSound.clear();
                for (size_t k = 0; k < sounds.size(); ++k) {
                    const std::string label =
                        sounds[k] + "##vehsnd" + std::to_string(k);
                    if (ImGui::Selectable(label.c_str(), v.engineSound == sounds[k]))
                        v.engineSound = sounds[k];
                }
                ImGui::EndCombo();
            }
            if (sounds.empty())
                ImGui::TextDisabled(
                    "No sounds in the project. Import a WAV in Project > Sounds.");
            if (!v.engineSound.empty()) {
                ImGui::SetNextItemWidth(scaled(220));
                ImGui::SliderFloat("Pitch at idle", &v.enginePitchIdle, 0.25f, 2.0f,
                                   "%.2fx");
                vehicleHelp("Playback rate at idle, as a multiple of the sample's own.");
                ImGui::SetNextItemWidth(scaled(220));
                ImGui::SliderFloat("Pitch at redline", &v.enginePitchRedline, 0.5f,
                                   4.0f, "%.2fx");
                vehicleHelp(
                    "Playback rate at the redline. The SPU2 register saturates\n"
                    "around 4x the sample's own rate.");
                ImGui::SetNextItemWidth(scaled(220));
                ImGui::SliderFloat("Volume", &v.engineVolume, 0.0f, 100.0f, "%.0f");
            }

            ImGui::Separator();
            loopPicker("High-rev loop", "vehsndhigh", v.engineHighSound,
                       "A second engine loop the base one CROSSFADES with as\n"
                       "the revs rise - the era's two-sample engine. Its pitch\n"
                       "has its own range. Any imported WAV is looped by the build.");
            ImGui::Checkbox("Enable high-rev loop", &v.engineHighEnabled);
            if (!v.engineHighSound.empty()) {
                ImGui::SetNextItemWidth(scaled(220));
                ImGui::SliderFloat("High-rev starts at", &v.engineHighStart, 0.0f, 0.95f, "%.2f of rev range");
                vehicleHelp("The high-rev recording fades in from this point to redline.");
                ImGui::SetNextItemWidth(scaled(220));
                ImGui::SliderFloat("High-rev pitch at start", &v.engineHighPitchStart, 0.25f, 4.0f, "%.2fx");
                ImGui::SetNextItemWidth(scaled(220));
                ImGui::SliderFloat("High-rev pitch at redline", &v.engineHighPitchEnd, 0.25f, 4.0f, "%.2fx");
            }
            ImGui::Separator();
            loopPicker("Tyre squeal loop", "vehsndscr", v.screechSound,
                       "Volume rides the tyre slip - the same number the smoke\n"
                       "and the telemetry read, so they always agree about when\n"
                       "a tyre has let go. Any imported WAV is looped by the build.");
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
                vehicleHelp(
                    "A ONE-SHOT played on every gear change while driving -\n"
                    "any sound qualifies, loops make no sense here. It borrows\n"
                    "a free script voice (priority 60), never the engine's.");
                if (!v.shiftSound.empty()) {
                    ImGui::SetNextItemWidth(scaled(220));
                    ImGui::SliderFloat("Shift volume", &v.shiftVolume, 0.0f,
                                       100.0f, "%.0f");
                }
            }
            ImGui::EndTabItem();
        }

        // --- Effects -----------------------------------------------------------
        // What the tyres leave behind (docs/vehicles.md, "Skid marks and
        // smoke"). An .mtl supplies the texture and the Kd tint; none = the
        // built-in tread and puff the vehicle bake generates.
        if (ImGui::BeginTabItem("Effects")) {
            sectionDefaults("effects");
            ImGui::SeparatorText("Lighting");
            ImGui::Checkbox("Headlights", &v.headlights);
            vehicleHelp(
                "Two additive beam pools painted on the terrain ahead of the\n"
                "nose - the scene lights' ground-pool trick. Reads as light,\n"
                "so it sells a NIGHT map; on a bright day map it is subtle.");
            ImGui::SeparatorText("Tyre effects");
            auto mtlPicker = [&](const char* label, std::string& path,
                                 const char* noneLabel) {
                std::string current = path.empty() ? noneLabel : path;
                if (current.rfind("res/", 0) == 0) current = current.substr(4);
                ImGui::SetNextItemWidth(scaled(260));
                if (ImGui::BeginCombo(label, current.c_str())) {
                    if (ImGui::Selectable(noneLabel, path.empty())) path.clear();
                    for (const std::string& rel : listMaterialAssets())
                        if (ImGui::Selectable(rel.substr(4).c_str(), rel == path))
                            path = rel;
                    ImGui::EndCombo();
                }
            };
            mtlPicker("Skid marks", v.skidMaterial, "<built-in tread>");
            vehicleHelp("Repeating skid texture with alpha for its shape and Kd for its tint. Atlased textures use the built-in tread instead.");
            // Tyre smoke: the built-in puff, a particle-library effect
            // (smokeEffect, which wins) or a plain material (smokeMaterial).
            // One combo, so the two fields can never both look chosen.
            {
                const ParticleEffect* fxSel =
                    project::findParticleEffect(project_, v.smokeEffect);
                std::string current = "<built-in puff>";
                if (fxSel) {
                    current = "Library: " + fxSel->name;
                } else if (!v.smokeEffect.empty()) {
                    current = "Library: " + v.smokeEffect + " (missing)";
                } else if (!v.smokeMaterial.empty()) {
                    current = v.smokeMaterial.rfind("res/", 0) == 0
                                  ? v.smokeMaterial.substr(4)
                                  : v.smokeMaterial;
                }
                ImGui::SetNextItemWidth(scaled(260));
                if (ImGui::BeginCombo("Tyre smoke", current.c_str())) {
                    if (ImGui::Selectable("<built-in puff>##vehsmokenone",
                                          v.smokeEffect.empty() && v.smokeMaterial.empty())) {
                        v.smokeEffect.clear();
                        v.smokeMaterial.clear();
                    }
                    if (!project_.particleEffects.empty())
                        ImGui::SeparatorText("Particle library");
                    for (size_t k = 0; k < project_.particleEffects.size(); ++k) {
                        const ParticleEffect& e = project_.particleEffects[k];
                        const std::string l = e.name + "##vehsmokefx" + std::to_string(k);
                        if (ImGui::Selectable(l.c_str(), v.smokeEffect == e.name)) {
                            v.smokeEffect = e.name;
                            v.smokeMaterial.clear();
                        }
                    }
                    const std::vector<std::string> mats = listMaterialAssets();
                    if (!mats.empty()) ImGui::SeparatorText("Materials");
                    for (const std::string& rel : mats) {
                        const std::string l = rel.substr(4) + "##vehsmokemtl";
                        if (ImGui::Selectable(l.c_str(),
                                              v.smokeEffect.empty() && rel == v.smokeMaterial)) {
                            v.smokeMaterial = rel;
                            v.smokeEffect.clear();
                        }
                    }
                    ImGui::EndCombo();
                }
                vehicleHelp("Choose a particle effect for smoke texture, colour and lifetime, or a material for texture and tint. Tyre slip controls emission; only the first effect layer is used.");
                if (fxSel) {
                    if (ImGui::SmallButton("Edit in Particle Editor##vehsmokeedit"))
                        openParticleEditor(fxSel->name);
                } else {
                    // A library copy of the built-in puff: same texture
                    // recipe, same sizes and life, ready to be tuned.
                    if (ImGui::SmallButton("New library smoke##vehsmokenew")) {
                        ParticleEffect fx = vehbake::tyreSmokeEffect();
                        fx.id = project::newObjectId();
                        const std::string base = fx.name;
                        for (int k = 2; project::findParticleEffect(project_, fx.name); ++k)
                            fx.name = base + " " + std::to_string(k);
                        if (particleBakeTexture(fx)) {
                            project_.particleEffects.push_back(fx);
                            v.smokeEffect = fx.name;
                            v.smokeMaterial.clear();
                            openParticleEditor(fx.name);
                        }
                    }
                    ImGui::SameLine();
                    vehicleHelp(
                        "Adds a \"Tyre smoke\" effect to the particle library that\n"
                        "looks like the built-in puff (the same generated texture,\n"
                        "sizes and life), links this car to it and opens the\n"
                        "Particle Editor on it.");
                }
            }
            // The lamp halo: the "lamp*" spec fields, shown here rather than on
            // the Driving tab (presentation, not handling).
            ImGui::SeparatorText("Lamp glow");
            {
                const std::vector<vehiclesim::SpecField> fields =
                    vehiclesim::specFields(v.drive);
                for (const vehiclesim::SpecField& f : fields) {
                    if (std::strncmp(f.key, "lamp", 4) != 0) continue;
                    specControl(f);
                }
                ImGui::Text("%zu lamp(s) measured on this body", v.lampGlows.size());
            }
            // Speed feel: the "feel*" fields - what the driven car does to the
            // game camera near its top speed and on nitrous. Runtime only; the
            // test drive here does not shake or blur.
            ImGui::SeparatorText("Speed feel");
            {
                const std::vector<vehiclesim::SpecField> fields =
                    vehiclesim::specFields(v.drive);
                for (const vehiclesim::SpecField& f : fields) {
                    if (std::strncmp(f.key, "feel", 4) != 0) continue;
                    specControl(f);
                }
                if (v.drive.nosCapacity <= 0.001f)
                    ImGui::TextDisabled("No nitrous (Driving > Nitrous seconds): "
                                        "the FOV kick and flame never fire.");
            }
            ImGui::EndTabItem();
        }

        // --- Cost -------------------------------------------------------------
        // The number that decides whether a scene can afford this vehicle at
        // all. A PS2 submit is ~1 ms of fixed EE time whatever it holds, so
        // stating the submit count is stating the frame budget.
        if (!global && ImGui::BeginTabItem("Cost")) {
            if (!bake || !bake->ok) {
                ImGui::TextDisabled("Import a model to see what it costs.");
            } else {
                const vehbake::Result& r = bake->result;
                const int submits = r.bodyParts + r.wheelParts;
                ImGui::Text("Draw submissions: %d", submits);
                ImGui::Text("Triangles: body %d + 4 wheels %d = %d", r.bodyTris,
                            r.wheelTris * 4, r.bodyTris + r.wheelTris * 4);
                ImGui::Text("Source was %d parts, %d triangles.", r.srcParts, r.srcTris);
                // The far tier: the body with the wheels baked in past
                // farDistance - an authored far model, or the decimated paint
                // (twice the distance reaches its coarser tier).
                if (r.farPart >= 0) {
                    if (v.farDistance <= 0.0f && v.trafficDistance <= 0.0f)
                        ImGui::TextWrapped("Far tier off: every instance costs the full %d "
                                    "submits at any distance.", submits);
                    else if (r.farAuthored)
                        ImGui::TextWrapped("Beyond %.0f units (%.0f for parked / AI cars): far "
                                    "model, %d submit(s), %d triangles (wheels in).",
                                    v.farDistance,
                                    v.trafficDistance > 0.0f ? v.trafficDistance
                                                             : v.farDistance,
                                    r.farSubmits, r.farTris[0]);
                    else
                        ImGui::TextWrapped("Beyond %.0f units: %d submit(s), %d triangles "
                                    "(wheels in); beyond %.0f: %d.",
                                    v.farDistance, r.farSubmits, r.farTris[0],
                                    v.farDistance * 2.0f,
                                    r.farTris.size() > 1 ? r.farTris[1] : r.farTris[0]);
                } else {
                    ImGui::TextDisabled(
                        "No far tier carries the wheels (their texture is not the\n"
                        "body's, or the body is too small to tier): the wheel bag\n"
                        "draws at every distance. A far model fixes that.");
                }
                for (const std::string& n : r.notes)
                    if (n.rfind("Far model:", 0) == 0) {
                        ImGui::PushStyleColor(ImGuiCol_Text, theme::semantics().danger);
                        ImGui::TextWrapped("%s", n.c_str());
                        ImGui::PopStyleColor();
                    }
                {
                    const std::string cur =
                        v.farModel.empty()
                            ? "Automatic"
                            : std::filesystem::path(v.farModel).filename().string();
                    ImGui::SetNextItemWidth(scaled(260));
                    if (ImGui::BeginCombo("Far model", cur.c_str())) {
                        if (ImGui::Selectable("Automatic", v.farModel.empty()))
                            v.farModel.clear();
                        for (const std::string& m : listAnimatedModelFiles()) {
                            const std::string rel = "res/models/" + m;
                            if (rel == v.modelPath) continue;
                            if (ImGui::Selectable(m.c_str(), rel == v.farModel))
                                v.farModel = rel;
                        }
                        ImGui::EndCombo();
                    }
                    vehicleHelp("A low-poly twin with the same origin, scale and body texture, wheels included. Automatic uses generated distance tiers.");
                }
                ImGui::SetNextItemWidth(scaled(200));
                ImGui::DragFloat("Far tier from", &v.farDistance, 0.5f, 0.0f, 500.0f,
                                 "%.0f units");
                vehicleHelp(
                    "Camera distance past which the car swaps to its far tier\n"
                    "(with a 10% hysteresis) and the wheel bag goes silent.\n"
                    "0 = never, for the car the player drives.");
                ImGui::SetNextItemWidth(scaled(200));
                ImGui::DragFloat("Parked / AI cars from", &v.trafficDistance, 0.5f, 0.0f,
                                 500.0f, v.trafficDistance > 0.0f ? "%.0f units" : "same");
                vehicleHelp(
                    "A traffic tier: cars NOBODY drives (parked, AI rivals)\n"
                    "swap to the far tier from this distance instead. With an\n"
                    "authored far model this can be close - a period racer's\n"
                    "traffic LOD. 0 = the same distance as above.");
                ImGui::Separator();
                ImGui::Checkbox("Edit triangle budgets (advanced)", &vehicleBudgetEdit_);
                ImGui::BeginDisabled(!vehicleBudgetEdit_);
                ImGui::SetNextItemWidth(scaled(200));
                ImGui::SliderInt("Body triangles", &v.bodyTriBudget, 100, 6000);
                ImGui::SetNextItemWidth(scaled(200));
                ImGui::SliderInt("Wheel triangles", &v.wheelTriBudget, 40, 3000);
                ImGui::EndDisabled();
                // The fast wheel (docs/vehicles.md, "A fast wheel"). The swap
                // speed is a drive tunable (Driving > Fast wheel above), so it
                // saves and reaches the game through specFields like the rest.
                {
                    int mode = v.fastWheel.empty()       ? 0
                               : v.fastWheel == "@auto" ? 1
                                                         : 2;
                    const char* modes[] = {"None", "Lower-resolution copy",
                                           "Mesh node of the model"};
                    ImGui::SetNextItemWidth(scaled(200));
                    if (ImGui::Combo("Fast wheel", &mode, modes, 3)) {
                        if (mode == 0) v.fastWheel.clear();
                        else if (mode == 1) v.fastWheel = "@auto";
                        else if (v.fastWheel.empty() || v.fastWheel == "@auto")
                            v.fastWheel = "wheel_blur";
                    }
                    vehicleHelp("Swap all wheels above Driving > Fast wheel above. Choose an automatic simplified wheel or a motion-blurred mesh node. Test the swap in Live preview.");
                    if (mode == 2) {
                        char nodeBuf[128];
                        std::snprintf(nodeBuf, sizeof(nodeBuf), "%s", v.fastWheel.c_str());
                        ImGui::SetNextItemWidth(scaled(200));
                        if (ImGui::InputText("Fast wheel node", nodeBuf, sizeof(nodeBuf)))
                            v.fastWheel = nodeBuf[0] ? std::string(nodeBuf) : std::string("@auto");
                    }
                    if (mode != 0) {
                        ImGui::BeginDisabled(!vehicleBudgetEdit_);
                        ImGui::SetNextItemWidth(scaled(200));
                        ImGui::SliderInt("Fast wheel triangles", &v.fastWheelTriBudget, 12, 3000);
                        ImGui::EndDisabled();
                    }
                }
                ImGui::Checkbox("Merge untextured materials", &v.mergeUntextured);
                vehicleHelp(
                    "Collapses every untextured material into one part, with the\n"
                    "colours in a generated palette texture. This is what takes a\n"
                    "36-part car down to two submits - turning it off is for\n"
                    "seeing what it costs, not for shipping.");
                ImGui::SetNextItemWidth(scaled(200));
                ImGui::SliderFloat("Body shine", &v.bodyShine, 0.0f, 1.0f, "%.2f");
                vehicleHelp(
                    "The paint's reflection pass. Rubber and near-black trim\n"
                    "stay MATTE (the bake splits them out - one extra submit),\n"
                    "and the wheels never shine.");
                if (v.bodyShine > 0.001f) {
                    ImGui::TextUnformatted("Reflection map");
                    ImGui::TextDisabled("%s", v.bodyReflMap.empty() ? "Dynamic sky" :
                        std::filesystem::path(v.bodyReflMap).filename().string().c_str());
                    static char reflectionFilter[64] = {};
                    if (ImGui::SmallButton("Choose texture...")) {
                        reflectionFilter[0] = '\0';
                        ImGui::OpenPopup("Reflection texture");
                    }
                    if (ImGui::BeginPopup("Reflection texture")) {
                        if (ImGui::Selectable("Dynamic sky", v.bodyReflMap.empty()))
                            v.bodyReflMap.clear();
                        ImGui::SetNextItemWidth(scaled(300));
                        ImGui::InputTextWithHint("##reflection-search", "Find texture...", reflectionFilter, sizeof(reflectionFilter));
                        ImGuiTextFilter filter(reflectionFilter);
                        ImGui::BeginChild("Reflection choices", ImVec2(scaled(300), scaled(240)));
                        for (const auto& name : listAssetFiles("textures", ".png")) {
                            if (!filter.PassFilter(name.c_str())) continue;
                            const std::string rel = "res/textures/" + name;
                            const uint32_t thumb = viewport_.assetThumb(rel, true);
                            if (thumb) {
                                ImGui::Image((ImTextureID)(intptr_t)thumb, ImVec2(scaled(32), scaled(32)));
                                ImGui::SameLine();
                            }
                            if (ImGui::Selectable(name.c_str(), v.bodyReflMap == rel))
                                v.bodyReflMap = rel;
                        }
                        ImGui::EndChild();
                        ImGui::Separator();
                        if (ImGui::Selectable("Import PNG...")) {
                            const auto path = importTextureAsset();
                            if (!path.empty()) v.bodyReflMap = path;
                        }
                        ImGui::EndPopup();
                    }
                    if (!v.bodyReflMap.empty()) {
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Use sky")) v.bodyReflMap.clear();
                    }
                    vehicleHelp("Sphere-map texture for body paint. Choose a PNG, or Use sky for the dynamic sky reflection.");
                }
                ImGui::SetNextItemWidth(scaled(200));
                ImGui::SliderFloat("Glass opacity", &v.glassOpacity, 0.05f, 1.0f, "%.2f");
                vehicleHelp(
                    "Below 1 the windows turn see-through: glass-named materials\n"
                    "get their own part, drawn last. One extra submit.");
                ImGui::TextUnformatted("Texture depth");
                ImGui::SameLine();
                drawAssetQualityCombo(v.modelPath);
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
                    ImGui::Text("All visible at once: %d draw submissions", placed * submits);
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::EndGroup();
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("Vehicle preview panel", ImVec2(0, 0), true);
    if (!global) {
        visitVehicleTuning(v, tuningBefore,
            [&](const std::string& key, const char*, auto& value, const auto& previous) {
                if (value != previous &&
                    std::find(resetKeys.begin(), resetKeys.end(), key) == resetKeys.end() &&
                    !vehicleTuningOverride(v, key)) v.tuningOverrides.push_back(key);
            });
    }
    project::applyVehicleDefaults(project_);
    drawVehiclePreview(v, global ? (defs.empty() ? -1 : 0) : vehicleSel_);
    ImGui::EndChild();

    if (project::sectionJson(project_, project::Section::Vehicles) != before)
        commitChange();
    ImGui::End();
}
