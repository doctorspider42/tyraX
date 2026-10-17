// Tools > Vehicle Editor (docs/vehicles.md) - App:: methods declared in
// app.hpp, own TU (the prefab_ui.cpp precedent).
//
// Layout: list (Global defaults, then vehicles) | preview (the hero: Wheels &
// measures overlay, Baked cost card, scrubbers) | inspector (Model / Handling /
// Damage / Feel / Cost, inheritance shown per value). Drive it is the header
// action.
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
#include "platform.hpp"
#include "toolkit.hpp"
#include "uiscript.hpp"
#include "update.hpp"
#include "widgets.hpp"
#include <imgui_internal.h>

namespace {

void vehicleHelp(const char* tip) {
    // On an inspector row the help lives on the caption (dotted underline).
    if (ui::RowHelp(tip)) return;
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

// What the console draws past the far distance (vehicleLodTier = 1 and
// applyGeoLod): every part that has tiers shows its first one (a part with
// none stays detailed), the parts farHideMask names are skipped. Indices are
// kept (a hidden part is emptied, not removed) so lampPart stays valid.
tmdl::Model farTierBody(const tmdl::Model& body, const vehbake::Result& r) {
    tmdl::Model m = body;
    for (size_t pi = 0; pi < m.parts.size(); ++pi) {
        tmdl::Part& p = m.parts[pi];
        p.stripVerts.clear();
        if (pi < 31 && ((r.farHideMask >> pi) & 1)) {
            p.verts.clear();
            p.ao.clear();
        } else if (!p.lods.empty()) {
            p.verts = p.lods.front().verts;
            p.ao = p.lods.front().ao;
        }
        p.lods.clear();
    }
    return m;
}

// The far tier exists to be shown: a far part that actually carries a tier.
bool hasFarTier(const vehbake::Result& r) {
    return r.farPart >= 0 && r.farPart < (int)r.body.parts.size() &&
           !r.body.parts[(size_t)r.farPart].lods.empty();
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
            solids.push_back(placement::worldAabb(all[i], aabbFn, &project_.modelCollision));
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

// The preview column: the car as the hero (orbit/zoom), a "Wheels & measures"
// overlay that tags each wheel on the car and draws the wheelbase, the baked
// cost as a glass card, and the scrubbers (speed, steering, revs, Listen,
// Spin wheels) as a card under it.
void App::drawVehiclePreview(const VehicleDef& tuning, int index) {
    const theme::Semantics& sem = theme::semantics();
    if (index < 0 || index >= (int)project_.vehicles.size()) {
        ui::EmptyState(ui::Glyph::Box, "No vehicle to preview",
                       "Add a vehicle (+ in the list) and choose its model.");
        if (vehicleEnginePreview_) vehicleEnginePreview_->stop();
        vehiclePreviewSound_ = false;
        vehicleAudioKey_.clear();
        return;
    }
    const VehicleDef& v = project_.vehicles[index];
    vehicleRefreshBake(index, false);
    const auto it = vehicleBakes_.find(v.id);
    if (it == vehicleBakes_.end() || !it->second.ok) {
        ui::EmptyState(ui::Glyph::Box, "Nothing to preview yet",
                       it != vehicleBakes_.end() && !it->second.error.empty()
                           ? "The import failed - see the Model tab."
                           : "Choose a .glb/.fbx model on the Model tab.");
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
    const bool farTier = hasFarTier(baked);
    if (!farTier || vehDmgPreviewId_ == v.id) vehiclePreviewFar_ = false;
    const bool showFar = vehiclePreviewFar_;
    const std::string key = it->second.key + "|" + v.name + (vehiclePreviewFast_ ? "|fast" : "|normal") +
        (showFar ? "|far" : "|near") +
        "|" + std::to_string(v.drive.track) + "|" + std::to_string(v.drive.wheelBase) +
        "|" + std::to_string(v.drive.wheelRadius) + "|" + std::to_string(v.drive.rideHeight);
    if (key != vehiclePreviewKey_) {
        vehiclePreviewKey_ = key;
        // Baked textures keep their path when paint changes. Drop this tool's
        // private cache before replacing geometry so the image reflects edits.
        vehiclePreview_->clearVehicleDraws();
        vehiclePreview_->invalidateAssets();
        const auto& body = vehDmgPreviewId_ == v.id ? vehDmgPreviewBody_ : baked.body;
        // Far: the far tier carries the wheels baked in, and the console
        // stops the wheel bag while it shows - so no wheel mesh.
        static const tmdl::Model kNoWheel;
        vehiclePreview_->setVehicleDraw(v.name, showFar ? farTierBody(baked.body, baked) : viewportBody(body, baked),
            showFar ? kNoWheel : (vehiclePreviewFast_ ? baked.fastWheel : baked.wheel),
            ".res-baked/vehicles/veh-" + v.id + "-palette.png",
            v.drive.wheelBase, v.drive.track, v.drive.wheelRadius, v.drive.rideHeight,
            baked.lampPart, baked.lampRearVerts);
    }
    if (vehiclePreviewPlay_)
        vehiclePreviewSpin_ = std::fmod(vehiclePreviewSpin_ + rate *
            std::min(ImGui::GetIO().DeltaTime, .1f), 6.2831853f);
    bool steered[4] = {true, true, false, false};
    bool driven[4] = {false, false, true, true};
    for (size_t w = 0; w < baked.detection.wheels.size() && w < 4; ++w) {
        steered[w] = baked.detection.wheels[w].steered;
        driven[w] = baked.detection.wheels[w].driven;
        for (const auto& row : v.wheels)
            if (row.node == baked.detection.wheels[w].nodeName) {
                steered[w] = row.steered;
                driven[w] = row.driven;
            }
    }
    vehiclePreview_->setVehiclePreviewPose(vehiclePreviewSpin_,
        vehiclePreviewSteer_ * .0174532925f, steered);
    SceneObject car;
    car.type = PrimitiveType::Vehicle;
    car.vehicleDef = v.name;
    car.position[1] = v.drive.wheelRadius;

    const bool driving = vehicleDriveObj_ >= 0;
    const float cardH = driving ? ImGui::GetTextLineHeightWithSpacing() * 5.0f + scaled(24)
                                : ImGui::GetFrameHeightWithSpacing() * 2.0f + scaled(24);
    const float width = std::max(scaled(160), ImGui::GetContentRegionAvail().x);
    const float height = std::max(scaled(180), ImGui::GetContentRegionAvail().y - cardH - scaled(8));
    const auto tex = vehiclePreview_->render((int)width, (int)height, {car}, {}, -1);
    const ImVec2 imagePos = ImGui::GetCursorScreenPos();
    ImGui::SetNextItemAllowOverlap();  // the overlay controls sit on top
    ImGui::InvisibleButton("Vehicle preview canvas", ImVec2(width, height));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float r = ImGui::GetStyle().FrameRounding + 3.0f;
    dl->AddImageRounded((ImTextureID)(intptr_t)tex, imagePos,
        ImVec2(imagePos.x + width, imagePos.y + height), ImVec2(0, 1), ImVec2(1, 0), IM_COL32_WHITE, r);
    ImGui::SetItemTooltip("Drag to orbit. Scroll to zoom.");
    if (ImGui::IsItemHovered()) {
        const auto& io = ImGui::GetIO();
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left))
            vehiclePreview_->orbit(io.MouseDelta.x, io.MouseDelta.y);
        if (io.MouseWheel != 0) vehiclePreview_->zoom(io.MouseWheel);
    }

    // World -> canvas, from the preview's own orbit camera (Viewport's
    // cameraRay: FOV 50 degrees, eye from yaw/pitch/distance around target).
    float yaw, pitch, dist, tgt[3];
    vehiclePreview_->camState(yaw, pitch, dist, tgt);
    const float eye[3] = {tgt[0] + dist * std::cos(pitch) * std::cos(yaw), tgt[1] + dist * std::sin(pitch),
                          tgt[2] + dist * std::cos(pitch) * std::sin(yaw)};
    float fwd[3] = {tgt[0] - eye[0], tgt[1] - eye[1], tgt[2] - eye[2]};
    {
        const float l = std::sqrt(fwd[0] * fwd[0] + fwd[1] * fwd[1] + fwd[2] * fwd[2]);
        for (float& c : fwd) c /= std::max(l, 1e-6f);
    }
    float right[3] = {-fwd[2], 0.0f, fwd[0]};  // fwd x up
    {
        const float l = std::sqrt(right[0] * right[0] + right[2] * right[2]);
        right[0] /= std::max(l, 1e-6f), right[2] /= std::max(l, 1e-6f);
    }
    const float up[3] = {right[1] * fwd[2] - right[2] * fwd[1], right[2] * fwd[0] - right[0] * fwd[2],
                         right[0] * fwd[1] - right[1] * fwd[0]};
    const float th = std::tan(50.0f * 3.14159265f / 180.0f * 0.5f), aspect = width / height;
    auto toScreen = [&](float x, float y, float z, ImVec2& out) {
        const float d[3] = {x - eye[0], y - eye[1], z - eye[2]};
        const float zc = d[0] * fwd[0] + d[1] * fwd[1] + d[2] * fwd[2];
        if (zc < 1e-3f) return false;
        const float xc = (d[0] * right[0] + d[1] * right[1] + d[2] * right[2]) / (zc * th * aspect);
        const float yc = (d[0] * up[0] + d[1] * up[1] + d[2] * up[2]) / (zc * th);
        out = ImVec2(imagePos.x + (0.5f + 0.5f * xc) * width, imagePos.y + (0.5f - 0.5f * yc) * height);
        return true;
    };

    // Wheels & measures: tags on the four hubs (front/rear and left/right
    // from the bake's own detection) and the wheelbase as a dimension line.
    if (vehicleShowMeasures_) {
        const float hx = 0.5f * v.drive.track, hz = 0.5f * v.drive.wheelBase;
        const float wy = v.drive.wheelRadius;
        const float hub[4][3] = {{-hx, wy, hz}, {hx, wy, hz}, {-hx, wy, -hz}, {hx, wy, -hz}};
        static const char* const kTags[4] = {"FL", "FR", "RL", "RR"};
        // Draw the two wheels nearer the camera last, so their tags win.
        int order[4] = {0, 1, 2, 3};
        std::sort(order, order + 4, [&](int a, int b) {
            auto dd = [&](int w) {
                const float dx = hub[w][0] - eye[0], dz = hub[w][2] - eye[2];
                return dx * dx + dz * dz;
            };
            return dd(a) > dd(b);
        });
        for (int k = 0; k < 4; ++k) {
            const int w = order[k];
            ImVec2 s;
            if (!toScreen(hub[w][0], hub[w][1] - wy * 0.9f, hub[w][2], s)) continue;
            const char* role = steered[w] && driven[w] ? "steer+drive" : steered[w] ? "steer" : driven[w] ? "drive" : "free";
            const float fs = ImGui::GetFontSize() * 0.86f;
            ImFont* f = ImGui::GetFont();
            const float tw = f->CalcTextSizeA(fs, FLT_MAX, 0, kTags[w]).x;
            const float rw = f->CalcTextSizeA(fs, FLT_MAX, 0, role).x;
            const float pad = scaled(5), gap = scaled(5);
            const float bw = pad + tw + gap + rw + pad * 1.6f, bh = fs + pad * 1.4f;
            const ImVec2 b0(s.x - bw * 0.5f, s.y), b1(s.x + bw * 0.5f, s.y + bh);
            const bool hot = vehicleHoverWheel_ == w;
            ui::OverlayGlassPanel(dl, b0, b1);
            if (hot) dl->AddRect(b0, b1, ImGui::GetColorU32(sem.accent), bh * 0.3f, 0, 2.0f);
            dl->AddText(f, fs, ImVec2(b0.x + pad, b0.y + pad * 0.7f), ImGui::GetColorU32(sem.text), kTags[w]);
            const ImVec4 rc = steered[w] ? sem.accent : driven[w] ? sem.ok : sem.textDim;
            const ImVec2 r0(b0.x + pad + tw + gap - pad * 0.4f, b0.y + pad * 0.35f);
            dl->AddRectFilled(r0, ImVec2(r0.x + rw + pad * 0.8f, b1.y - pad * 0.35f),
                              ImGui::GetColorU32(ImVec4(rc.x, rc.y, rc.z, 0.18f)), bh * 0.3f);
            dl->AddText(f, fs, ImVec2(r0.x + pad * 0.4f, b0.y + pad * 0.7f), ImGui::GetColorU32(rc), role);
            if (hot) {
                ImVec2 c;
                if (toScreen(hub[w][0], hub[w][1], hub[w][2], c))
                    dl->AddCircle(c, std::max(scaled(14), (s.y - c.y) * 1.05f), ImGui::GetColorU32(sem.accent), 32, 2.0f);
            }
        }
        // The wheelbase on the side facing the camera, just above the floor.
        const float side = (eye[0] >= 0.0f ? 1.0f : -1.0f) * (hx + wy * 0.6f);
        ImVec2 a, b;
        if (toScreen(side, 0.02f, hz, a) && toScreen(side, 0.02f, -hz, b)) {
            const ImU32 lc = ImGui::GetColorU32(sem.ok);
            dl->AddLine(a, b, lc, 1.5f);
            dl->AddLine(ImVec2(a.x, a.y - scaled(5)), ImVec2(a.x, a.y + scaled(5)), lc, 1.5f);
            dl->AddLine(ImVec2(b.x, b.y - scaled(5)), ImVec2(b.x, b.y + scaled(5)), lc, 1.5f);
            char lab[48];
            std::snprintf(lab, sizeof lab, "wheelbase %.3f m", v.drive.wheelBase / std::max(0.001f, project_.settings.unitsPerMeter));
            ui::FontScope mono(ui::monoFont());
            const ImVec2 ts = ImGui::CalcTextSize(lab);
            const ImVec2 m((a.x + b.x) * 0.5f - ts.x * 0.5f - scaled(6), (a.y + b.y) * 0.5f - ts.y * 0.5f - scaled(3));
            ui::OverlayGlassPanel(dl, m, ImVec2(m.x + ts.x + scaled(12), m.y + ts.y + scaled(6)));
            dl->AddText(ImVec2(m.x + scaled(6), m.y + scaled(3)), lc, lab);
        }
    }

    // Overlay: Near | Far, Reset view | Spin wheels | Wheels & measures, top left.
    float overlayRight = imagePos.x;
    {
        const float pad = scaled(10);
        const char* mText = "Wheels & measures";
        const char* sText = vehiclePreviewFast_ ? "Spin wheels (fast)" : "Spin wheels";
        const float w0 = ui::OverlayTextWidth("Reset view"), w1 = ui::OverlayTextWidth(mText),
                    w2 = ui::OverlayTextWidth(sText);
        const float wn = ui::OverlayTextWidth("Near"), wf = ui::OverlayTextWidth("Far");
        const ImVec2 c0(imagePos.x + pad, imagePos.y + pad);
        const ImVec2 c1(c0.x + wn + wf + w0 + w1 + w2 + ui::OverlayPad() * 2.0f + scaled(40),
                        c0.y + ui::OverlayRowHeight() + ui::OverlayPad() * 2.0f);
        ui::OverlayGlassPanel(dl, c0, c1);
        overlayRight = c1.x;
        ImGui::SetCursorScreenPos(ImVec2(c0.x + ui::OverlayPad(), c0.y + ui::OverlayPad()));
        // Near | Far: the model the console draws close up, or the far tier
        // it swaps to past the far distance (wheels baked in, glass an
        // authored far model paints hidden).
        const bool farOk = farTier && vehDmgPreviewId_ != v.id;
        if (ui::OverlayTextButton("Near", "Near", !vehiclePreviewFar_)) vehiclePreviewFar_ = false;
        uiscript::markLastItemChecked(!vehiclePreviewFar_);
        ImGui::SameLine(0.0f, scaled(2));
        if (ui::OverlayTextButton("Far", "Far", vehiclePreviewFar_, farOk)) vehiclePreviewFar_ = true;
        uiscript::markLastItemChecked(vehiclePreviewFar_);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip(farOk ? "The far tier: what the console draws past the far distance\n"
                                      "(wheels baked in, one bag fewer)."
                                    : vehDmgPreviewId_ == v.id
                                          ? "The damage preview shows the near model."
                                          : "This car has no far tier (Cost tab).");
        ImGui::SameLine(0.0f, scaled(16));
        if (ui::OverlayTextButton("Reset view", "Reset view", false)) frameCar();
        ImGui::SameLine(0.0f, scaled(8));
        if (ui::OverlayTextButton("Spin wheels", sText, vehiclePreviewPlay_)) vehiclePreviewPlay_ = !vehiclePreviewPlay_;
        uiscript::markLastItemChecked(vehiclePreviewPlay_);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("Turns the wheels at the Speed below; past the fast-wheel\n"
                              "threshold the fast wheel swaps in.");
        ImGui::SameLine(0.0f, scaled(8));
        if (ui::OverlayTextButton("Wheels & measures", mText, vehicleShowMeasures_))
            vehicleShowMeasures_ = !vehicleShowMeasures_;
        uiscript::markLastItemChecked(vehicleShowMeasures_);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("Tags each wheel with its role (steered / driven) and draws\n"
                              "the measured wheelbase. Hover a row of the Model tab's\n"
                              "wheel table to ring that wheel.");
    }

    // Baked cost, glass card top right: what one of these costs the console.
    {
        const int submits = baked.bodyParts + baked.wheelParts;
        char tris[32], subs[16], far[64];
        std::snprintf(tris, sizeof tris, "%d", baked.bodyTris + baked.wheelTris * 4);
        std::snprintf(subs, sizeof subs, "%d", submits);
        far[0] = 0;
        if (baked.farPart >= 0 && (v.farDistance > 0.0f || v.trafficDistance > 0.0f))
            std::snprintf(far, sizeof far, "%d - %d subs", baked.farTris.empty() ? 0 : baked.farTris[0],
                          baked.farSubmits);
        const float fs = ImGui::GetFontSize();
        const float lh = fs + scaled(4);
        const float cw = scaled(200), chh = scaled(14) + lh * (far[0] ? 4.0f : 3.0f);
        ImVec2 k0(imagePos.x + width - scaled(10) - cw, imagePos.y + scaled(10));
        if (k0.x < overlayRight + scaled(10)) k0.y = imagePos.y + height - scaled(10) - chh;  // no room up top
        if (cw < width * 0.6f) {
            ui::OverlayGlassPanel(dl, k0, ImVec2(k0.x + cw, k0.y + chh));
            float y = k0.y + scaled(8);
            dl->AddText(ui::boldFont() ? ui::boldFont() : ImGui::GetFont(), fs * 0.8f, ImVec2(k0.x + scaled(10), y),
                        ImGui::GetColorU32(sem.textDim), "BAKED COST");
            y += lh;
            auto rowL = [&](const char* l, const char* val) {
                dl->AddText(ImVec2(k0.x + scaled(10), y), ImGui::GetColorU32(sem.textDim), l);
                ui::FontScope mono(ui::monoFont());
                const float vw = ImGui::CalcTextSize(val).x;
                dl->AddText(ImVec2(k0.x + cw - scaled(10) - vw, y), ImGui::GetColorU32(sem.text), val);
                y += lh;
            };
            rowL("Triangles", tris);
            rowL("Draw submissions", subs);
            if (far[0]) {
                char l[32];
                std::snprintf(l, sizeof l, "Beyond %.0f u", v.farDistance > 0.0f ? v.farDistance : v.trafficDistance);
                rowL(l, far);
            }
        }
    }
    ImGui::SetCursorScreenPos(ImVec2(imagePos.x, imagePos.y + height + scaled(8)));

    // The card under the preview: scrubbers, or the test drive's telemetry.
    ui::BeginCard("##vehscrub", ImVec2(width, cardH - scaled(4)));
    if (driving) {
        const vehiclesim::DriveState& st = vehicleDriveState_;
        ImGui::TextDisabled("W gas, S brake / reverse, A/D steer, Space handbrake, E nitrous");
        ImGui::Text("Speed %.2f u/s   slip %.2f   steer %.1f deg", st.speed, st.lateral, st.steerAngle);
        ImGui::Text("Gear %s%d   rpm %.0f   nitrous %.0f%%%s", st.gear < 0 ? "R" : "",
                    st.gear < 0 ? 1 : st.gear + 1, st.rpm - st.limiterDip, st.nos * 100.0f,
                    st.nosActive ? "  (boosting)" : "");
        const float mag = std::fabs(st.speed) + std::fabs(st.lateral);
        ImGui::Text("Pitch %.1f  roll %.1f  %s   sideways %.0f%%%s", st.pitch, st.roll,
                    st.grounded ? "on the ground" : "airborne",
                    mag > 0.01f ? 100.0f * std::fabs(st.lateral) / mag : 0.0f,
                    v.drive.damage > 0.0f ? "" : "");
        ui::Switch("Hold throttle", &vehicleDriveHoldThrottle_);
        ImGui::SameLine(0.0f, scaled(16));
        ImGui::SetNextItemWidth(scaled(160));
        ImGui::SliderFloat("Steer", &vehicleDriveSteer_, -1.0f, 1.0f, "%.2f");
    } else {
        const float half = (ImGui::GetContentRegionAvail().x - scaled(20)) * 0.5f;
        const float labW = ImGui::CalcTextSize("Steering").x + scaled(10);
        auto scrub = [&](const char* label, float* val, float lo, float hi, const char* fmt) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(label);
            ImGui::SameLine(0.0f, 0.0f);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, labW - ImGui::CalcTextSize(label).x));
            ImGui::SetNextItemWidth(half - labW);
            ImGui::SliderFloat(label, val, lo, hi, fmt);
        };
        // The sliders keep their labels as IDs (scripts name 'Speed',
        // 'Steering', 'Revs'); the right-hand label is clipped by a width
        // that ends where the next column starts.
        const float x0 = ImGui::GetCursorPosX();
        ImGui::BeginGroup();
        ImGui::PushClipRect(ImGui::GetCursorScreenPos(),
                            ImVec2(ImGui::GetCursorScreenPos().x + half, ImGui::GetCursorScreenPos().y + 200), true);
        scrub("Speed", &vehiclePreviewSpeed_, 0, 240, "%.0f km/h");
        scrub("Revs", &vehiclePreviewRevs_, 0, 1, "%.2f");
        ImGui::PopClipRect();
        ImGui::EndGroup();
        ImGui::SameLine(x0 + half + scaled(20));
        ImGui::BeginGroup();
        const ImVec2 g0 = ImGui::GetCursorScreenPos();
        ImGui::PushClipRect(g0, ImVec2(g0.x + half, g0.y + 200), true);
        scrub("Steering", &vehiclePreviewSteer_, -40, 40, "%.0f deg");
        ImGui::PopClipRect();
        ui::Switch("Listen to engine", &vehiclePreviewSound_);
        ImGui::SameLine(0.0f, scaled(12));
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%.0f rpm", tuning.drive.idleRpm + vehiclePreviewRevs_ *
            (tuning.drive.redlineRpm - tuning.drive.idleRpm));
        ImGui::EndGroup();
    }
    if (vehicleEnginePreview_ && !vehicleEnginePreview_->error().empty())
        ImGui::TextColored(sem.warn, "%s", vehicleEnginePreview_->error().c_str());
    ui::EndCard();

    if (!vehicleEnginePreview_) vehicleEnginePreview_ = std::make_unique<audiopreview::EngineLoop>();
    const std::string audibleHigh = tuning.engineHighEnabled ? tuning.engineHighSound : "";
    const std::string audioKey = project_.dir + "|" + tuning.engineSound + "|" + audibleHigh;
    if (vehiclePreviewSound_) {
        // Revs at the top of the slider bounce off the rev limiter exactly
        // like the game's flat-out engine (vehiclesim::revLimiterStep).
        const float range = std::max(tuning.drive.redlineRpm - tuning.drive.idleRpm, 1.0f);
        const float rpm = tuning.drive.idleRpm + vehiclePreviewRevs_ * range;
        const float dip = vehiclesim::revLimiterStep(tuning.drive, vehiclePreviewLimiter_,
            rpm >= vehiclesim::revLimiterOnset(tuning.drive), ImGui::GetIO().DeltaTime);
        const float revs = vehiclePreviewRevs_ - dip * vehiclesim::revLimiterDipRpm(tuning.drive) / range;
        const float highStart = std::clamp(tuning.engineHighStart, 0.0f, 0.95f);
        const float highMix = std::clamp((revs - highStart) /
            (1.0f - highStart), 0.0f, 1.0f);
        vehicleEnginePreview_->update(tuning.enginePitchIdle + revs *
            (tuning.enginePitchRedline - tuning.enginePitchIdle),
            tuning.engineHighPitchStart + highMix *
            (tuning.engineHighPitchEnd - tuning.engineHighPitchStart),
            highMix, tuning.engineVolume * (1.0f - 0.5f * std::clamp(tuning.drive.revLimiter, 0.0f, 1.0f) * dip));
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
}

void App::drawVehicleWindow() {
    if (!showVehicles_ || !hasProject_) {
        if (vehicleEnginePreview_) vehicleEnginePreview_->stop();
        vehiclePreviewSound_ = false;
        vehicleAudioKey_.clear();
        return;
    }
    ImGui::SetNextWindowSizeConstraints(ImVec2(scaled(1000), scaled(560)), ImVec2(FLT_MAX, FLT_MAX));
    ImGui::SetNextWindowSize(ImVec2(scaled(1280), scaled(780)), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Vehicle Editor", &showVehicles_)) {
        if (vehicleEnginePreview_) vehicleEnginePreview_->stop();
        vehiclePreviewSound_ = false;
        vehicleAudioKey_.clear();
        ImGui::End();
        return;
    }
    const theme::Semantics& sem = theme::semantics();

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

    bool pendingNew = false;
    auto newVehicle = [&] { pendingNew = true; };
    auto createVehicle = [&] {
        VehicleDef nv;
        nv.id = project::newObjectId();
        nv.name = uniqueName(defs, "Car");
        // A NEW car can be damaged; the struct default stays 0 so every
        // definition saved before damage existed keeps driving as it did.
        nv.drive.damage = 1.0f;
        nv.drive.lampGlow = 1.0f;
        nv.inheritDefaults = true;
        defs.push_back(std::move(nv));
        vehicleSel_ = (int)defs.size() - 1;
        vehicleTab_ = 0;  // a new car needs its model first
    };
    int duplicateAt = -1, deleteAt = -1;

    const bool global = vehicleSel_ == -2;
    project::applyVehicleDefaults(project_);
    VehicleDef& v = global ? project_.vehicleDefaults : defs[vehicleSel_];
    if (!global) vehicleRefreshBake(vehicleSel_, false);
    const VehicleBakeCache* bake = nullptr;
    if (auto it = vehicleBakes_.find(v.id); it != vehicleBakes_.end()) bake = &it->second;

    // Which placed instance a test drive takes: the first of this definition
    // in the active scene.
    int inst = -1;
    if (!global) {
        const std::vector<SceneObject>& objs = project_.objects();
        for (int i = 0; i < (int)objs.size(); ++i)
            if (objs[i].type == PrimitiveType::Vehicle && objs[i].vehicleDef == v.name) {
                inst = i;
                break;
            }
    }

    // --- header: count, Drive it, docs ------------------------------------------
    {
        const float rowH = ImGui::GetFrameHeight();
        const float y0 = ImGui::GetCursorPosY();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%d vehicle%s", (int)defs.size(), defs.size() == 1 ? "" : "s");
        const bool driving = vehicleDriveObj_ >= 0;
        const char* label = driving ? "Stop driving" : "Drive it";
        // Drive it = the design's green "go" primary (Run's colour) with a
        // play triangle; room for the triangle in both states keeps it still.
        const float bw = ImGui::CalcTextSize(label).x + ImGui::GetFontSize() * 1.1f +
                         ImGui::GetStyle().FramePadding.x * 3.0f;
        ImGui::SameLine(ImGui::GetContentRegionMax().x - bw - rowH - ImGui::GetStyle().ItemSpacing.x);
        ImGui::SetCursorPosY(y0);
        if (driving) {
            if (ui::SecondaryButton(label, bw)) vehicleDriveStop();
        } else {
            // The reason vehiclesim is host-only: the same step() the console
            // runs, driven from the keyboard against the real scene's terrain,
            // so grip is tuned in a "slider, feel, slider" loop.
            if (ui::GoButton(label, bw, inst >= 0)) {
                vehicleDriveHoldThrottle_ = false;
                vehicleDriveSteer_ = 0.0f;
                vehicleDriveStart(inst);
            }
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("%s", driving ? "Puts the car back where it was."
                                    : global ? "Select a vehicle to drive it."
                                    : inst < 0 ? "Place one in this scene first (Add object > Gameplay > Vehicle)."
                                               : "Drive the placed car in the viewport (W/S/A/D, Space, E).\n"
                                                 "Puts it back where it was when you stop.");
        ImGui::SameLine();
        ImGui::SetCursorPosY(y0);
        if (ui::HelpButton("Vehicle docs", rowH, "Open docs/vehicles.md"))
            platform::openUrl(std::string("https://github.com/") + update::kRepo + "/blob/main/docs/vehicles.md");
    }

    const float bodyH = ImGui::GetContentRegionAvail().y;
    const float listW = scaled(230), inspW = scaled(380);
    const float gap = ImGui::GetStyle().ItemSpacing.x;

    // --- the definition list ------------------------------------------------
    ImGui::BeginChild("##vehlist", ImVec2(listW, bodyH), ImGuiChildFlags_None);
    {
        // Global defaults: the base every vehicle inherits from, pinned on top.
        if (ui::ListRow("Global defaults", "base", global, ui::Glyph::Layers)) vehicleSel_ = -2;
        uiscript::markLastItemChecked(global);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("Settings every vehicle inherits until it changes them.");
        ImGui::Dummy(ImVec2(0, scaled(6)));
        const float capY = ImGui::GetCursorPosY();
        ui::Caption("Vehicles");
        const float pw = ImGui::GetFrameHeight();
        ImGui::SetCursorPos(ImVec2(ImGui::GetContentRegionMax().x - pw, capY - scaled(5)));
        if (ui::GhostButton("+###New vehicle", pw)) newVehicle();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("New vehicle");
        ImGui::SetCursorPosY(capY + ImGui::GetFontSize() + scaled(8));
        const float rowH = ImGui::GetFontSize() * 2.9f;
        int budget = 2;
        for (int i = 0; i < (int)defs.size(); ++i) {
            const VehicleDef& d = defs[i];
            const bool sel = vehicleSel_ == i;
            // An explicit ##id: two definitions may not share a name, but one
            // is being TYPED for a moment during a rename, and the label is
            // the row's ImGui id.
            const std::string label = d.name + "##vehsel" + std::to_string(i);
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float w = ImGui::GetContentRegionAvail().x;
            if (ImGui::InvisibleButton(label.c_str(), ImVec2(w, rowH))) vehicleSel_ = i;
            uiscript::markLastItemChecked(sel);
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Duplicate")) duplicateAt = i;
                ImGui::PushStyleColor(ImGuiCol_Text, sem.danger);
                if (ImGui::MenuItem("Delete")) deleteAt = i;
                ImGui::PopStyleColor();
                ImGui::EndPopup();
            }
            const bool hov = ImGui::IsItemHovered();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const float rr = ImGui::GetStyle().FrameRounding + 1.0f;
            if (sel) dl->AddRectFilled(p, ImVec2(p.x + w, p.y + rowH), ImGui::GetColorU32(sem.surface), rr);
            else if (hov)
                dl->AddRectFilled(p, ImVec2(p.x + w, p.y + rowH),
                                  ImGui::GetColorU32(ImVec4(sem.surface.x, sem.surface.y, sem.surface.z, 0.5f)), rr);
            // The model's own thumbnail (the Asset Browser's renderer).
            const float ts = rowH - scaled(10);
            const ImVec2 t0(p.x + scaled(6), p.y + scaled(5));
            dl->AddRectFilled(t0, ImVec2(t0.x + ts * 1.25f, t0.y + ts), ImGui::GetColorU32(sem.ground), rr);
            {
                // A side silhouette in the car's paint (dim without one), under
                // the thumbnail: the row has a picture even before the model's
                // render exists.
                const ImU32 pc = d.paintEnabled
                                     ? ImGui::ColorConvertFloat4ToU32(
                                           ImVec4(d.paintColor[0], d.paintColor[1], d.paintColor[2], 1.0f))
                                     : ImGui::GetColorU32(sem.textDim);
                const float W = ts * 1.25f, H = ts;
                const float bx0 = t0.x + W * 0.12f, bx1 = t0.x + W * 0.88f, by0 = t0.y + H * 0.48f,
                            by1 = t0.y + H * 0.68f;
                dl->AddRectFilled(ImVec2(bx0, by0), ImVec2(bx1, by1), pc, H * 0.06f);
                const ImVec2 cab[4] = {ImVec2(t0.x + W * 0.32f, by0), ImVec2(t0.x + W * 0.40f, t0.y + H * 0.33f),
                                       ImVec2(t0.x + W * 0.62f, t0.y + H * 0.33f), ImVec2(t0.x + W * 0.72f, by0)};
                dl->AddConvexPolyFilled(cab, 4, pc);
                dl->AddCircleFilled(ImVec2(t0.x + W * 0.28f, by1), H * 0.09f, ImGui::GetColorU32(sem.surface));
                dl->AddCircleFilled(ImVec2(t0.x + W * 0.72f, by1), H * 0.09f, ImGui::GetColorU32(sem.surface));
            }
            uint32_t thumb = d.modelPath.empty() ? 0 : viewport_.assetThumb(d.modelPath, false);
            if (!thumb && !d.modelPath.empty() && budget > 0) {
                thumb = viewport_.assetThumb(d.modelPath, true);
                --budget;
            }
            if (thumb)
                dl->AddImage((ImTextureID)(intptr_t)thumb, ImVec2(t0.x + ts * 0.125f, t0.y),
                             ImVec2(t0.x + ts * 1.125f, t0.y + ts), ImVec2(0, 1), ImVec2(1, 0));
            const float tx = t0.x + ts * 1.25f + scaled(10);
            const float fs = ImGui::GetFontSize();
            int custom = 0;
            for (const std::string& k : d.tuningOverrides) custom += !vehicleGeometryKey(k);
            char sub[128];
            const std::string file = d.modelPath.empty() ? std::string("no model")
                                                         : std::filesystem::path(d.modelPath).filename().string();
            if (!d.inheritDefaults) std::snprintf(sub, sizeof sub, "%s", file.c_str());
            else if (custom == 0) std::snprintf(sub, sizeof sub, "%s - all defaults", file.c_str());
            else std::snprintf(sub, sizeof sub, "%s - %d custom", file.c_str(), custom);
            dl->PushClipRect(p, ImVec2(p.x + w - 2, p.y + rowH), true);
            {
                ui::FontScope bold(ui::boldFont());
                dl->AddText(ImVec2(tx, p.y + rowH * 0.5f - fs), ImGui::GetColorU32(sem.text), d.name.c_str());
            }
            dl->AddText(ImGui::GetFont(), fs * 0.86f, ImVec2(tx, p.y + rowH * 0.5f + 1.0f),
                        ImGui::GetColorU32(sem.textDim), sub);
            dl->PopClipRect();
        }
        if (defs.empty())
            if (ui::EmptyState(ui::Glyph::Box, "No vehicles yet",
                               "A vehicle is defined once from an imported .glb and placed as often as you like.",
                               "New vehicle##empty"))
                newVehicle();
    }
    ImGui::EndChild();
    ImGui::SameLine(0.0f, gap);

    // --- the preview: the hero -------------------------------------------------
    const float previewW = ImMax(scaled(240), ImGui::GetContentRegionAvail().x - inspW - gap);
    ImGui::BeginChild("Vehicle preview panel", ImVec2(previewW, bodyH), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    drawVehiclePreview(v, global ? (defs.empty() ? -1 : 0) : vehicleSel_);
    ImGui::EndChild();
    ImGui::SameLine(0.0f, gap);

    // --- the inspector -----------------------------------------------------------
    ImGui::BeginChild("Vehicle settings", ImVec2(0, bodyH), ImGuiChildFlags_None);
    if (!global) {
        char name[128];
        std::snprintf(name, sizeof(name), "%s", v.name.c_str());
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, ImGui::GetStyle().FramePadding.y));
        ImGui::PushFont(ui::boldFont(), ImGui::GetFontSize() * 1.3f);
        ImGui::SetNextItemWidth(-FLT_MIN);
        // Applied on commit, not per keystroke: retargeting every instance on
        // each character would rewrite the scenes once per letter.
        ImGui::InputText("##Name", name, sizeof(name));
        const bool renamed = ImGui::IsItemDeactivatedAfterEdit();
        ImGui::PopFont();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
        if (renamed) renameVehicleDef(vehicleSel_, uniqueName(defs, name));
    } else {
        ImGui::PushFont(ui::boldFont(), ImGui::GetFontSize() * 1.3f);
        ImGui::TextUnformatted("Global defaults");
        ImGui::PopFont();
    }
    {
        int custom = 0;
        for (const std::string& k : v.tuningOverrides) custom += !vehicleGeometryKey(k);
        ImGui::PushTextWrapPos(0.0f);
        if (global)
            ImGui::TextDisabled("What every vehicle starts from. A value a vehicle changes is marked there with a dot.");
        else if (!v.inheritDefaults)
            ImGui::TextDisabled("Uses its own values (made before global defaults existed).");
        else if (custom == 0)
            ImGui::TextDisabled("All values follow Global defaults. Edit one and it gets a dot and a reset.");
        else
            ImGui::TextDisabled("%d custom value%s (dot); the rest follow Global defaults.", custom,
                                custom == 1 ? "" : "s");
        ImGui::PopTextWrapPos();
    }
    ImGui::Dummy(ImVec2(0, scaled(2)));

    const VehicleDef tuningBefore = v;
    std::vector<std::string> resetKeys;
    // Resets ONE inherited value: drop the override, take the default back.
    auto resetKey = [&](const std::string& key) {
        visitVehicleTuning(v, project_.vehicleDefaults,
            [&](const std::string& k, const char*, auto& value, const auto& base) {
                if (k != key) return;
                value = base;
            });
        auto& keys = v.tuningOverrides;
        keys.erase(std::remove(keys.begin(), keys.end(), key), keys.end());
        resetKeys.push_back(key);
    };
    // "Reset to defaults" for one section, right-aligned, only when it has
    // custom values (the per-value dots say which).
    auto sectionDefaults = [&](const char* section) {
        if (global || !v.inheritDefaults) return;
        bool custom = false;
        visitVehicleTuning(v, project_.vehicleDefaults,
            [&](const std::string& key, const char* group, auto&, const auto&) {
                if (std::strcmp(group, section) == 0 && vehicleTuningOverride(v, key)) custom = true;
            });
        if (!custom) return;
        const std::string lbl = std::string("Reset to defaults##") + section;
        const float w = ImGui::CalcTextSize("Reset to defaults").x + ImGui::GetStyle().FramePadding.x * 2.6f;
        ImGui::SetCursorPosX(ImGui::GetContentRegionMax().x - w);
        if (ui::GhostButton(lbl.c_str(), w)) {
            visitVehicleTuning(v, project_.vehicleDefaults,
                [&](const std::string& key, const char* group, auto& value, const auto& base) {
                    if (std::strcmp(group, section) != 0) return;
                    resetKeys.push_back(key);
                    auto& keys = v.tuningOverrides;
                    keys.erase(std::remove(keys.begin(), keys.end(), key), keys.end());
                    value = base;
                });
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("Every custom value in this section follows Global defaults again.");
    };
    const float capMin = scaled(110), capMax = scaled(150);
    // One tuned value as an inspector row with its inheritance ON the row: a
    // dot + reset when this vehicle overrides it, a `model` badge when it is
    // measured from the model (wheel placement).
    auto tunedRow = [&](const char* key, const char* label, auto&& widget) {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float avail = ImGui::GetContentRegionAvail().x;
        const float cw = IM_TRUNC(ImClamp(avail * 0.34f, capMin, capMax));
        const bool measured = vehicleGeometryKey(key);
        const bool custom = !global && v.inheritDefaults && !measured && vehicleTuningOverride(v, key);
        const float s = ImGui::GetFontSize() * 0.95f;
        const float fh = ImGui::GetFrameHeight();
        if (custom) {
            ImGui::SetCursorScreenPos(ImVec2(p.x + cw - s - ImGui::GetStyle().ItemInnerSpacing.x, p.y + (fh - s) * 0.5f));
            const std::string rl = std::string("Reset ") + label + "##rst_" + key;
            if (ImGui::InvisibleButton(rl.c_str(), ImVec2(s, s))) resetKey(key);
            const bool hov = ImGui::IsItemHovered();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                ImGui::SetTooltip("Back to Global defaults");
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 c(p.x + cw - s * 0.5f - ImGui::GetStyle().ItemInnerSpacing.x, p.y + fh * 0.5f);
            const ImU32 col = ImGui::GetColorU32(hov ? sem.text : sem.textDim);
            dl->PathArcTo(c, s * 0.32f, 3.14159f * 0.15f, 3.14159f * 1.75f, 12);
            dl->PathStroke(col, 0, 1.4f);
            const ImVec2 tip(c.x + s * 0.32f * std::cos(3.14159f * 0.15f), c.y + s * 0.32f * std::sin(3.14159f * 0.15f));
            dl->AddTriangleFilled(ImVec2(tip.x - s * 0.18f, tip.y - s * 0.02f), ImVec2(tip.x + s * 0.14f, tip.y - s * 0.12f),
                                  ImVec2(tip.x + s * 0.06f, tip.y + s * 0.2f), col);
            ImGui::SetCursorScreenPos(p);
        }
        widget();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float textW = ImGui::CalcTextSize(label, ImGui::FindRenderedTextEnd(label)).x;
        if (custom) {
            // The caption in full ink plus the dot: this value is this car's.
            const float maxX = p.x + cw - s - ImGui::GetStyle().ItemInnerSpacing.x * 2.0f;
            dl->PushClipRect(p, ImVec2(maxX, p.y + fh), true);
            dl->AddText(ImVec2(p.x, p.y + ImGui::GetStyle().FramePadding.y), ImGui::GetColorU32(sem.text), label,
                        ImGui::FindRenderedTextEnd(label));
            dl->PopClipRect();
            const float dx = std::min(p.x + textW + scaled(7), maxX - scaled(3));
            dl->AddCircleFilled(ImVec2(dx, p.y + fh * 0.5f), scaled(3), ImGui::GetColorU32(sem.accent), 12);
        } else if (measured && !global) {
            const char* b = "model";
            const float bfs = ImGui::GetFontSize() * 0.75f;
            const float bw = ImGui::GetFont()->CalcTextSizeA(bfs, FLT_MAX, 0, b).x + bfs;
            const float bx = p.x + cw - bw - ImGui::GetStyle().ItemInnerSpacing.x;
            if (bx > p.x + textW + scaled(4)) {
                dl->AddRectFilled(ImVec2(bx, p.y + fh * 0.5f - bfs * 0.6f), ImVec2(bx + bw, p.y + fh * 0.5f + bfs * 0.6f),
                                  ImGui::GetColorU32(sem.field), bfs * 0.5f);
                dl->AddText(ImGui::GetFont(), bfs, ImVec2(bx + bfs * 0.5f, p.y + fh * 0.5f - bfs * 0.5f),
                            ImGui::GetColorU32(sem.textDim), b);
            }
        }
    };
    auto specControl = [&](const vehiclesim::SpecField& f) {
        if (global && vehicleGeometryKey(f.key)) return;
        ImGui::PushID(f.key);
        if (std::strcmp(f.key, "damageVisual") == 0 ||
            std::strcmp(f.key, "damageMechanical") == 0) {
            bool enabled = *f.value > 0.5f;
            tunedRow(f.key, f.label, [&] {
                if (ui::Switch(f.label, &enabled)) {
                    *f.value = enabled ? 1.0f : 0.0f;
                    if (std::strcmp(f.key, "damageVisual") == 0) vehicleDamagePreviewReset();
                }
            });
            if (f.tip && f.tip[0]) vehicleHelp(f.tip);
            ImGui::PopID();
            return;
        }
        if (std::strcmp(f.key, "nosCapacity") == 0) {
            bool enabled = *f.value > 0.001f;
            if (ui::Switch("Has nitrous", &enabled))
                *f.value = enabled ? 5.0f : 0.0f;
        }
        tunedRow(f.key, f.label, [&] { ui::SliderFloat(f.label, f.value, f.min, f.max, "%.4g"); });
        if (f.tip && f.tip[0]) vehicleHelp(f.tip);
        ImGui::PopID();
    };

    if (ui::BeginQuietTabBar("##vehtabs")) {
        static int lastFrame = -10;
        const bool force = lastFrame != ImGui::GetFrameCount() - 1;
        static const char* const kTabs[] = {"Model##vtab", "Handling##vtab", "Damage##vtab", "Feel##vtab",
                                            "Cost##vtab"};
        if (global && (vehicleTab_ == 0 || vehicleTab_ == 4)) vehicleTab_ = 1;
        for (int i = 0; i < 5; ++i) {
            if (global && (i == 0 || i == 4)) continue;  // per-vehicle only
            const ImGuiTabItemFlags fl = force && i == vehicleTab_ ? ImGuiTabItemFlags_SetSelected : 0;
            if (ui::QuietTab(kTabs[i], i == vehicleTab_, fl) && !force) vehicleTab_ = i;
        }
        ui::EndQuietTabBar();
        lastFrame = ImGui::GetFrameCount();
    }
    ImGui::BeginChild("##vehtabbody", ImVec2(0, 0), ImGuiChildFlags_None);
    {
        ui::InspectorRows rows(capMin, capMax);
        // --- Model ----------------------------------------------------------
        if (!global && vehicleTab_ == 0) {
            // The same picker shape the Properties panel uses for a Model:
            // the project's own res/models assets, never a free-text path.
            const std::string current =
                v.modelPath.empty()
                    ? "<none>"
                    : std::filesystem::path(v.modelPath).filename().string();
            if (ui::BeginCombo("Model file", current.c_str())) {
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

            ui::Caption("Body paint");
            ui::Switch("Override paint colour", &v.paintEnabled);
            if (v.paintEnabled) {
                ui::ColorHex("Paint colour", v.paintColor);
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

            ui::Caption("Import");
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
                if (ui::Section("Import details"))
                    for (const std::string& n : r.notes) ImGui::TextWrapped("%s", n.c_str());

                if (r.detection.frontAssumed) {
                    ImGui::PushStyleColor(ImGuiCol_Text, theme::semantics().warn);
                    ImGui::TextWrapped(
                        "Check the front direction in the preview.");
                    ImGui::PopStyleColor();
                }
                ui::Switch("Flip front/rear", &v.flipFront);

                ui::Caption("Wheels");
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextDisabled("Found by shape and position - node names don't matter (a "
                                    "mirrored name is fine). Hover a row to ring it on the car.");
                ImGui::PopTextWrapPos();
                vehicleHoverWheel_ = -1;
                if (ImGui::BeginTable("##wheels", 4,
                                      ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
                    ImGui::TableSetupColumn("Wheel");
                    ImGui::TableSetupColumn("Node");
                    ImGui::TableSetupColumn("Steered", ImGuiTableColumnFlags_WidthFixed);
                    ImGui::TableSetupColumn("Driven", ImGuiTableColumnFlags_WidthFixed);
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
                        ImGui::Text("%s%s", w.front ? "F" : "R", w.left ? "L" : "R");
                        ImGui::SameLine();
                        ImGui::TextDisabled("%s %s", w.front ? "front" : "rear", w.left ? "left" : "right");
                        ImGui::TableNextColumn();
                        {
                            ui::FontScope mono(ui::monoFont());
                            ImGui::TextDisabled("%s", w.nodeName.c_str());
                        }
                        ImGui::SetItemTooltip("radius %.3f, width %.3f", w.radius, w.width);
                        ImGui::TableNextColumn();
                        ImGui::PushID((int)i * 2);
                        ui::Switch("##st", &row->steered);
                        ImGui::PopID();
                        ImGui::TableNextColumn();
                        ImGui::PushID((int)i * 2 + 1);
                        ui::Switch("##dr", &row->driven);
                        ImGui::PopID();
                        // Hovering anywhere on the row rings that wheel in the
                        // preview (the order is the detection's: FL FR RL RR).
                        const ImRect rr(ImVec2(ImGui::GetWindowPos().x, ImGui::GetItemRectMin().y - 2.0f),
                                        ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowWidth(),
                                               ImGui::GetItemRectMax().y + 2.0f));
                        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
                            ImGui::IsMouseHoveringRect(rr.Min, rr.Max, false))
                            vehicleHoverWheel_ = (int)(w.front ? 0 : 2) + (w.left ? 0 : 1);
                    }
                    ImGui::EndTable();
                }
            }
        }

        // --- Handling ------------------------------------------------------
        // Widgets are DERIVED from vehiclesim::specFields(), so a tunable added
        // to DriveSpec appears here, saves, loads and gets its tooltip by
        // existing in that one list. Groups collapse; the first two start open.
        if (vehicleTab_ == 1) {
            sectionDefaults("driving");
            const std::vector<vehiclesim::SpecField> fields =
                vehiclesim::specFields(v.drive);
            bool open = true;
            bool first = true;
            for (const vehiclesim::SpecField& f : fields) {
                if (std::strncmp(f.key, "damage", 6) == 0) continue;  // Damage tab
                if (std::strncmp(f.key, "lamp", 4) == 0) continue;    // Feel tab
                if (std::strncmp(f.key, "feel", 4) == 0) continue;    // Feel tab
                if (std::strncmp(f.key, "exhaust", 7) == 0) continue; // Feel tab
                const char* group = nullptr;
                if (!global && std::strcmp(f.key, "wheelBase") == 0) group = "Wheel placement";
                if (std::strcmp(f.key, "topSpeed") == 0) group = "Power and speed";
                if (std::strcmp(f.key, "maxSteerDeg") == 0) group = "Steering and grip";
                if (std::strcmp(f.key, "offroadGrip") == 0) group = "Road surfaces";
                if (std::strcmp(f.key, "gravity") == 0) group = "Suspension and body";
                if (std::strcmp(f.key, "gears") == 0) group = "Engine and gears";
                if (std::strcmp(f.key, "nosCapacity") == 0) group = "Nitrous";
                if (std::strcmp(f.key, "fastWheelSpeed") == 0) group = "Fast wheel swap";
                if (group) {
                    const bool startOpen = first || std::strcmp(group, "Power and speed") == 0;
                    open = ui::Section(group, startOpen ? ImGuiTreeNodeFlags_DefaultOpen : 0);
                    first = false;
                    if (open && std::strcmp(group, "Wheel placement") == 0 && bake && bake->ok) {
                        // Measured by the bake; Re-measure takes the model's
                        // numbers back after an edit.
                        const float w = ImGui::CalcTextSize("Re-measure").x + ImGui::GetStyle().FramePadding.x * 2.6f;
                        ImGui::SetCursorPosX(ImGui::GetContentRegionMax().x - w);
                        if (ui::GhostButton("Re-measure", w)) {
                            const auto& sp = bake->result.spec;
                            v.drive.wheelBase = r_geom(sp.wheelBase, v.drive.wheelBase);
                            v.drive.track = r_geom(sp.track, v.drive.track);
                            v.drive.wheelRadius = r_geom(sp.wheelRadius, v.drive.wheelRadius);
                            v.drive.rideHeight = r_geom(sp.rideHeight, v.drive.rideHeight);
                            v.drive.bodyOverhang = r_geom(sp.bodyOverhang, v.drive.bodyOverhang);
                        }
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                            ImGui::SetTooltip("Wheelbase, track, radius, ride height and overhang\n"
                                              "as measured from the model.");
                    }
                }
                if (open) specControl(f);
            }
        }

        // --- Damage ---------------------------------------------------------
        // The "damage*" spec fields plus test hits: the dent is computed by the
        // same vehiclesim functions the console's twin mirrors, on a copy of
        // the baked body, so what shows here is where the game will dent.
        if (vehicleTab_ == 2) {
            sectionDefaults("damage");
            const std::vector<vehiclesim::SpecField> fields =
                vehiclesim::specFields(v.drive);
            for (const vehiclesim::SpecField& f : fields) {
                if (std::strncmp(f.key, "damage", 6) != 0) continue;
                specControl(f);
            }
            ui::Caption("Preview");
            auto bk = vehicleBakes_.find(v.id);
            const bool baked = bk != vehicleBakes_.end() && bk->second.ok;
            if (global) {
                ImGui::TextDisabled("Select a vehicle to preview impacts.");
            } else if (v.drive.damage <= 0.0f) {
                ImGui::TextDisabled("Damage strength 0: this car cannot be damaged.");
            } else if (!baked) {
                ImGui::TextDisabled("Import a model first.");
            } else {
                ui::SliderFloat("Hit speed", &vehDmgTestSpeed_, 0.0f, 40.0f, "%.1f u/s");
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
        }

        // --- Feel: driver, sounds, effects ------------------------------------
        if (vehicleTab_ == 3) {
            if (ui::Section("Driver", ImGuiTreeNodeFlags_DefaultOpen)) {
            sectionDefaults("driver");
            tunedRow("camDist", "Camera distance", [&] { ui::SliderFloat("Camera distance", &v.camDist, 1.0f, 20.0f, "%.2f"); });
            tunedRow("camHeight", "Camera height", [&] { ui::SliderFloat("Camera height", &v.camHeight, 0.0f, 10.0f, "%.2f"); });
            tunedRow("camPitch", "Camera pitch", [&] { ui::SliderFloat("Camera pitch", &v.camPitch, -30.0f, 60.0f, "%.1f"); });
            ImGui::Separator();
            ImGui::BeginDisabled(global);
            ui::AxisDrag3("Exit offset", v.exitOffset, 0.05f);
            ImGui::EndDisabled();
            vehicleHelp(
                "Where the player is put down on getting out, relative to the\n"
                "car: x right, y up, z forward. The driver's door.");

            // --- The readout ---------------------------------------------------
            ImGui::Separator();
            tunedRow("showHud", "Show a driver's HUD", [&] { ui::Switch("Show a driver's HUD", &v.showHud); });
            vehicleHelp(
                "Speed, gear and the nitrous tank, while driving.\n"
                "Drawn as runtime text, so the font gets a glyph atlas.");
            if (v.showHud) {
                fontCombo(v.hudFont);
                tunedRow("hudSpeedScale", "Speed reads as", [&] { ui::DragFloat("Speed reads as", &v.hudSpeedScale, 0.05f, 0.0f,
                                 100.0f, "%.2f x units/s"); });
                vehicleHelp(
                    "What one world unit per second should show as. A unit is\n"
                    "whatever this project decided it is, so this cannot be\n"
                    "guessed: 3.6 turns metres per second into km/h.");
            }
            bool card = v.tutorialSeconds > 0.0f;
            if (ui::Switch("Controls card on entry", &card)) {
                v.tutorialSeconds = card ? 8.0f : 0.0f;
                // The card's steering rows want stick glyphs.
                if (card) project::ensureStickIcons(project_);
            }
            vehicleHelp(
                "The first time each car is entered: its controls, with the\n"
                "buttons they are bound to now. Rows tick off as tried.");
            if (card) {
                if (!v.showHud) fontCombo(v.hudFont);
                ui::DragFloat("Card stays", &v.tutorialSeconds, 0.1f, 1.0f, 60.0f,
                                 "%.0f s");
                if (v.tutorialSeconds < 1.0f) v.tutorialSeconds = 1.0f;
            }

            }
            if (ui::Section("Sounds", ImGuiTreeNodeFlags_DefaultOpen)) {
            sectionDefaults("sounds");
            const auto& sounds = project_.sounds;
            const auto loopPicker = [&](const char* label, const char* tid,
                                        std::string& path, const char* tip) {
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
            ui::Caption("Engine sound");
            vehicleHelp("Choose any imported WAV. The build encodes vehicle engine and tyre samples as loops; pitch follows engine speed.");
            const std::string cur = v.engineSound.empty() ? "(silent)" : v.engineSound;
            if (ui::BeginCombo("Sample", cur.c_str())) {
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
                tunedRow("enginePitchIdle", "Pitch at idle", [&] { ui::SliderFloat("Pitch at idle", &v.enginePitchIdle, 0.25f, 2.0f,
                                   "%.2fx"); });
                vehicleHelp("Playback rate at idle, as a multiple of the sample's own.");
                tunedRow("enginePitchRedline", "Pitch at redline", [&] { ui::SliderFloat("Pitch at redline", &v.enginePitchRedline, 0.5f,
                                   4.0f, "%.2fx"); });
                vehicleHelp(
                    "Playback rate at the redline. The SPU2 register saturates\n"
                    "around 4x the sample's own rate.");
                tunedRow("engineVolume", "Volume", [&] { ui::SliderFloat("Volume", &v.engineVolume, 0.0f, 100.0f, "%.0f"); });
            }

            ImGui::Separator();
            loopPicker("High-rev loop", "vehsndhigh", v.engineHighSound,
                       "A second engine loop the base one CROSSFADES with as\n"
                       "the revs rise - the era's two-sample engine. Its pitch\n"
                       "has its own range. Any imported WAV is looped by the build.");
            tunedRow("engineHighEnabled", "Enable high-rev loop", [&] { ui::Switch("Enable high-rev loop", &v.engineHighEnabled); });
            if (!v.engineHighSound.empty()) {
                tunedRow("engineHighStart", "High-rev starts at", [&] { ui::SliderFloat("High-rev starts at", &v.engineHighStart, 0.0f, 0.95f, "%.2f of rev range"); });
                vehicleHelp("The high-rev recording fades in from this point to redline.");
                tunedRow("engineHighPitchStart", "High-rev pitch at start", [&] { ui::SliderFloat("High-rev pitch at start", &v.engineHighPitchStart, 0.25f, 4.0f, "%.2fx"); });
                tunedRow("engineHighPitchEnd", "High-rev pitch at redline", [&] { ui::SliderFloat("High-rev pitch at redline", &v.engineHighPitchEnd, 0.25f, 4.0f, "%.2fx"); });
            }
            ImGui::Separator();
            loopPicker("Tyre squeal loop", "vehsndscr", v.screechSound,
                       "Volume rides the tyre slip - the same number the smoke\n"
                       "and the telemetry read, so they always agree about when\n"
                       "a tyre has let go. Any imported WAV is looped by the build.");
            if (!v.screechSound.empty()) {
                tunedRow("screechVolume", "Squeal volume", [&] { ui::SliderFloat("Squeal volume", &v.screechVolume, 0.0f,
                                   100.0f, "%.0f"); });
            }
            ImGui::Separator();
            {
                const std::string cur =
                    v.shiftSound.empty() ? "(none)" : v.shiftSound;
                if (ui::BeginCombo("Gear shift", cur.c_str())) {
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
                    tunedRow("shiftVolume", "Shift volume", [&] { ui::SliderFloat("Shift volume", &v.shiftVolume, 0.0f,
                                       100.0f, "%.0f"); });
                }
            }
            }
            if (ui::Section("Effects")) {
            sectionDefaults("effects");
            ui::Caption("Lighting");
            tunedRow("headlights", "Headlights", [&] { ui::Switch("Headlights", &v.headlights); });
            vehicleHelp(
                "Two additive beam pools painted on the terrain ahead of the\n"
                "nose - the scene lights' ground-pool trick. Reads as light,\n"
                "so it sells a NIGHT map; on a bright day map it is subtle.");
            ui::Caption("Tyre effects");
            auto mtlPicker = [&](const char* label, std::string& path,
                                 const char* noneLabel) {
                std::string current = path.empty() ? noneLabel : path;
                if (current.rfind("res/", 0) == 0) current = current.substr(4);
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
                if (ui::BeginCombo("Tyre smoke", current.c_str())) {
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
            // the Handling tab (presentation, not handling).
            ui::Caption("Lamp glow");
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
            ui::Caption("Speed feel");
            {
                const std::vector<vehiclesim::SpecField> fields =
                    vehiclesim::specFields(v.drive);
                for (const vehiclesim::SpecField& f : fields) {
                    if (std::strncmp(f.key, "feel", 4) != 0) continue;
                    specControl(f);
                }
                if (v.drive.nosCapacity <= 0.001f)
                    ImGui::TextDisabled("No nitrous (Handling > Nitrous seconds): "
                                        "the FOV kick and flame never fire.");
            }
            // Exhaust: where the nitrous flame, the shift backfire and the
            // smoke come out - the model's "exhaust" empties.
            ui::Caption("Exhaust");
            {
                const std::vector<vehiclesim::SpecField> fields =
                    vehiclesim::specFields(v.drive);
                for (const vehiclesim::SpecField& f : fields) {
                    if (std::strncmp(f.key, "exhaust", 7) != 0) continue;
                    specControl(f);
                }
                if (!global) {
                    if (v.exhausts.empty())
                        ImGui::TextDisabled("No exhaust markers: two guessed pipes");
                    else
                        ImGui::Text("%zu exhaust pipe(s) marked on this body", v.exhausts.size());
                    vehicleHelp(
                        "Add an empty named \"exhaust\" (exhaust.001, ...) at each\n"
                        "pipe's opening, its arrow pointing out of the pipe.\n"
                        "Without one the effects use two pipes under the rear bumper.");
                }
            }
            }
        }

        // --- Cost -------------------------------------------------------------
        // The number that decides whether a scene can afford this vehicle at
        // all. A PS2 submit is ~1 ms of fixed EE time whatever it holds, so
        // stating the submit count is stating the frame budget.
        if (!global && vehicleTab_ == 4) {
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
                    if (ui::BeginCombo("Far model", cur.c_str())) {
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
                ui::DragFloat("Far tier from", &v.farDistance, 0.5f, 0.0f, 500.0f,
                                 "%.0f units");
                vehicleHelp(
                    "Camera distance past which the car swaps to its far tier\n"
                    "(with a 10% hysteresis) and the wheel bag goes silent.\n"
                    "0 = never, for the car the player drives.");
                ui::DragFloat("Parked / AI cars from", &v.trafficDistance, 0.5f, 0.0f,
                                 500.0f, v.trafficDistance > 0.0f ? "%.0f units" : "same");
                vehicleHelp(
                    "A traffic tier: cars NOBODY drives (parked, AI rivals)\n"
                    "swap to the far tier from this distance instead. With an\n"
                    "authored far model this can be close - a period racer's\n"
                    "traffic LOD. 0 = the same distance as above.");
                ImGui::Separator();
                ui::Switch("Edit triangle budgets (advanced)", &vehicleBudgetEdit_);
                ImGui::BeginDisabled(!vehicleBudgetEdit_);
                ui::SliderInt("Body triangles", &v.bodyTriBudget, 100, 6000);
                ui::SliderInt("Wheel triangles", &v.wheelTriBudget, 40, 3000);
                ImGui::EndDisabled();
                // The fast wheel (docs/vehicles.md, "A fast wheel"). The swap
                // speed is a drive tunable (Handling > Fast wheel swap), so it
                // saves and reaches the game through specFields like the rest.
                {
                    int mode = v.fastWheel.empty()       ? 0
                               : v.fastWheel == "@auto" ? 1
                                                         : 2;
                    const char* modes[] = {"None", "Lower-resolution copy",
                                           "Mesh node of the model"};
                    if (ui::Combo("Fast wheel", &mode, modes, 3)) {
                        if (mode == 0) v.fastWheel.clear();
                        else if (mode == 1) v.fastWheel = "@auto";
                        else if (v.fastWheel.empty() || v.fastWheel == "@auto")
                            v.fastWheel = "wheel_blur";
                    }
                    vehicleHelp("Swap all wheels above Handling > Fast wheel swap. Choose an automatic simplified wheel or a motion-blurred mesh node. Test the swap with Spin wheels and Speed in the preview.");
                    if (mode == 2) {
                        char nodeBuf[128];
                        std::snprintf(nodeBuf, sizeof(nodeBuf), "%s", v.fastWheel.c_str());
                        if (ui::InputText("Fast wheel node", nodeBuf, sizeof(nodeBuf)))
                            v.fastWheel = nodeBuf[0] ? std::string(nodeBuf) : std::string("@auto");
                    }
                    if (mode != 0) {
                        ImGui::BeginDisabled(!vehicleBudgetEdit_);
                        ui::SliderInt("Fast wheel triangles", &v.fastWheelTriBudget, 12, 3000);
                        ImGui::EndDisabled();
                    }
                }
                ui::Switch("Merge untextured materials", &v.mergeUntextured);
                vehicleHelp(
                    "Collapses every untextured material into one part, with the\n"
                    "colours in a generated palette texture. This is what takes a\n"
                    "36-part car down to two submits - turning it off is for\n"
                    "seeing what it costs, not for shipping.");
                ui::SliderFloat("Body shine", &v.bodyShine, 0.0f, 1.0f, "%.2f");
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
                ui::SliderFloat("Glass opacity", &v.glassOpacity, 0.05f, 1.0f, "%.2f");
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
        }
    }
    ImGui::EndChild();
    ImGui::EndChild();

    if (!global) {
        visitVehicleTuning(v, tuningBefore,
            [&](const std::string& key, const char*, auto& value, const auto& previous) {
                if (value != previous &&
                    std::find(resetKeys.begin(), resetKeys.end(), key) == resetKeys.end() &&
                    !vehicleTuningOverride(v, key)) v.tuningOverrides.push_back(key);
            });
    }
    project::applyVehicleDefaults(project_);

    if (pendingNew) createVehicle();
    if (duplicateAt >= 0 && duplicateAt < (int)defs.size()) {
        VehicleDef d = defs[duplicateAt];
        d.id = project::newObjectId();
        d.name = uniqueName(defs, d.name + " copy");
        defs.push_back(std::move(d));
        vehicleSel_ = (int)defs.size() - 1;
    }
    if (deleteAt >= 0 && deleteAt < (int)defs.size()) {
        // Instances keep their name reference. That is deliberate: a delete
        // must not silently edit scenes, and a dangling name is reported by
        // the instance's own Properties row where the author can see it.
        vehicleBakes_.erase(defs[deleteAt].id);
        defs.erase(defs.begin() + deleteAt);
        if (vehicleSel_ >= (int)defs.size()) vehicleSel_ = (int)defs.size() - 1;
        if (vehicleSel_ < 0) vehicleSel_ = -2;
    }

    if (project::sectionJson(project_, project::Section::Vehicles) != before)
        commitChange();
    ImGui::End();
}
