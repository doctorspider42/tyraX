// -------------------------------------------------------------------------
// Procedural scatter authoring: Tools > Procedural (the scatter-graph editor
// and its bake), plus the Scatter volume's own verbs - see
// docs/procedural-generation.md.
//
// Its own translation unit for the reason every other *_ui.cpp is one: app.cpp
// was a single 26k-line TU and therefore the whole build's critical path.
// These are still App:: members declared in app.hpp.
// -------------------------------------------------------------------------
#include "app.hpp"
#include "app_internal.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "prefab.hpp"
#include "procbake.hpp"
#include "procgen.hpp"
#include "procgraph.hpp"
#include "procrt.hpp"

#include <imgui.h>
#include <imgui_internal.h>  // ImPow (the graph zoom curve)
#include <imnodes.h>

// ---------------------------------------------------------------------------
// Procedural scatter (Tools > Procedural) - docs/procedural-generation.md
// ---------------------------------------------------------------------------

std::vector<int> App::procVolumes() const {
    std::vector<int> out;
    if (!hasProject_) return out;
    const std::vector<SceneObject>& objs = project_.objects();
    for (size_t i = 0; i < objs.size(); ++i)
        if (objs[i].type == PrimitiveType::Scatter) out.push_back((int)i);
    return out;
}

void App::addScatterVolume() {
    int counter = 0;
    std::string name;
    for (;;) {
        name = "procedural-" + std::to_string(++counter);
        bool taken = false;
        for (const SceneObject& o : project_.objects()) taken |= (o.name == name);
        if (!taken) break;
    }
    SceneObject o;
    o.name = name;
    o.type = PrimitiveType::Scatter;
    o.castShadow = false;
    o.collisionMode = 2;  // the region itself is not geometry
    // Most of the terrain, and tall enough that surface points always fall
    // inside the box (the volume clips its own output in Y as well).
    const TerrainConfig& t = project_.active().terrain;
    o.scale[0] = (float)t.width * 0.8f;
    o.scale[2] = (float)t.depth * 0.8f;
    o.scale[1] = 80.0f;
    o.position[0] = o.position[2] = 0.0f;
    o.position[1] = 20.0f;
    o.procGraph = procgraph::starterGraph();
    project_.objects().push_back(o);
    selectOnly((int)project_.objects().size() - 1);
    procVolume_ = (int)project_.objects().size() - 1;
    procPositionsApplied_ = false;
    procPreviewNode_ = 0;
    showProcedural_ = true;
    commitChange();  // stamps the object id the window addresses it by
    procVolumeId_ = project_.objects()[procVolume_].id;
    statusMessage_ = "Added " + name +
                     " - fill its Pick Asset pool, then Bake (Tools > Procedural)";
}

ProcOverride& App::procOverrideFor(ProcGraph& g, uint64_t key) {
    for (ProcOverride& o : g.overrides)
        if (o.key == key) return o;
    ProcOverride o;
    o.key = key;
    g.overrides.push_back(o);
    return g.overrides.back();
}

void App::pruneProcOverrides(ProcGraph& g) {
    g.overrides.erase(
        std::remove_if(g.overrides.begin(), g.overrides.end(),
                       [](const ProcOverride& o) {
                           return !o.removed && o.asset < 0 && o.scale == 1.0f &&
                                  o.offset[0] == 0.0f && o.offset[1] == 0.0f &&
                                  o.offset[2] == 0.0f && o.rotate[0] == 0.0f &&
                                  o.rotate[1] == 0.0f && o.rotate[2] == 0.0f;
                       }),
        g.overrides.end());
}

void App::runProcSeedSweep(int objectIndex, int count) {
    procSeedTrials_.clear();
    if (!hasProject_ || objectIndex < 0 ||
        objectIndex >= (int)project_.objects().size())
        return;
    const SceneObject& vol = project_.objects()[objectIndex];
    if (vol.type != PrimitiveType::Scatter || vol.procGraph.empty()) return;
    procSeedTrialsFor_ = vol.id;

    // Seed 0 of the sweep is the AUTHORED one, so the table always contains the
    // world the graph currently describes and every other row is read against
    // it. The rest walk the same LCG "Reseed" uses, which means the sweep shows
    // exactly the seeds pressing Reseed would hand out - not an unrelated
    // sequence that happens to also be random.
    std::vector<uint32_t> seeds;
    seeds.push_back(vol.procGraph.seed ? vol.procGraph.seed : 1u);
    uint32_t s = seeds.front();
    for (int i = 1; i < count; ++i) {
        s = s * 1664525u + 1013904223u;
        if (s == 0) s = 1;
        seeds.push_back(s);
    }

    for (uint32_t sd : seeds) {
        procgen::Options opt;
        // A fresh serial per trial and a scratch cache: the sweep must not
        // evict the live preview's memo, which is what makes editing the graph
        // afterwards feel instant.
        opt.contextSerial = modelEditSerial_ ^ ((uint64_t)sd << 24) ^ 0x5EEDu;
        opt.seedOverride = sd;
        procgen::Cache scratch;
        const procgen::Result r = procgen::evaluate(project_, project_.active(),
                                                    vol, opt, &scratch);
        const procbake::Report est = procbake::estimate(project_, vol, r);
        ProcSeedTrial t;
        t.seed = sd;
        t.instances = (int)r.instances.size();
        t.triangles = est.triangles;
        t.chunks = est.chunks;
        t.warnings = (int)r.warnings.size();
        t.millis = r.millis;
        procSeedTrials_.push_back(t);
    }
}

void App::updateProcPreview() {
    const std::vector<int> vols = procVolumes();
    if (vols.empty()) {
        if (procPreviewValid_) {
            viewport_.setScatterPreview({});
            procPreviewValid_ = false;
            procResult_ = procgen::Result{};
            procInstances_ = procCandidates_ = procNodesRun_ = 0;
            procBudget_ = procbake::Report{};
        }
        return;
    }

    // The whole invalidation rule: modelEditSerial_ covers every model edit
    // (graph parameters, the volume transform, the terrain, other objects),
    // and procPreviewNode_ is the only editor-side input that changes what is
    // evaluated. A drag in the graph editor commits per frame like the flow
    // editor, so this re-runs while the slider moves - which is exactly what
    // the per-node cache and the progressive fraction below are for.
    const uint64_t serial = modelEditSerial_ ^ ((uint64_t)procPreviewNode_ << 40) ^
                            ((uint64_t)project_.activeScene << 56) ^
                            ((uint64_t)procSeedPreview_ << 8);
    // Which volume is being EDITED is not part of the cache key (that would
    // throw away every node cache on a volume switch) but it does have to
    // re-trigger the pass: the budget readout is filled from the edited
    // volume's own result, and the window picks that volume on its first
    // frame - after this function has already run once.
    const uint64_t trigger = serial ^ ((uint64_t)(procVolume_ + 1) << 48) ^
                             (showProcPreview_ ? 0ull : 0x9E3779B97F4A7C15ull);
    if (procPreviewValid_ && trigger == procPreviewSerial_ && procFraction_ >= 1.0f)
        return;

    // Progressive density (GRAF-05): while a control is being dragged, spend at
    // most ~25 ms per frame by evaluating a PREFIX of the point sequence. The
    // sequence is prefix-stable, so the coarse pass is a subset of the full one
    // - the layout the author sees does not change when the rest fills in.
    float fraction = 1.0f;
    const bool dragging = ImGui::IsAnyItemActive() || ImGui::IsMouseDragging(ImGuiMouseButton_Left);
    if (dragging && procLastMs_ > 25.0)
        fraction = std::clamp((float)(25.0 / procLastMs_), 0.05f, 1.0f);

    procgen::Result merged;
    merged.type = ProcType::Points;
    procOwnFirst_ = procOwnCount_ = 0;  // no edited volume until one is found
    double ms = 0.0;
    int nodesRun = 0, candidates = 0;
    for (int idx : vols) {
        const SceneObject& vol = project_.objects()[idx];
        if (vol.procGraph.empty()) continue;
        procgen::Options opt;
        opt.contextSerial = serial;
        opt.fraction = fraction;
        // Node isolation applies only to the volume being edited; the others
        // keep showing their finished output so the scene stays readable.
        if (procPreviewNode_ != 0 && idx == procVolume_)
            opt.previewNode = procPreviewNode_;
        // Same rule for the simulated seed: only the volume being edited is
        // shown under another seed, so the rest of the scene stays the world
        // the project describes.
        if (procSeedPreview_ != 0 && idx == procVolume_)
            opt.seedOverride = procSeedPreview_;
        const procgen::Result r =
            procgen::evaluate(project_, project_.active(), vol,
                              opt, &procCaches_[vol.id]);
        ms += r.millis;
        nodesRun += r.nodesEvaluated;
        candidates += r.candidates;
        if (idx == procVolume_) {
            procOwnFirst_ = (int)merged.instances.size();
            procOwnCount_ = (int)r.instances.size();
            procBudget_ = procbake::estimate(project_, vol, r);
            if (r.type != ProcType::Points) {
                merged.type = r.type;
                merged.mask = r.mask;
                merged.curve = r.curve;
            }
        }
        // Asset AND prefab indices are per volume - shift both as the lists
        // concatenate. Missing the prefab half meant two volumes' prefab pools
        // aliased onto each other's names.
        const int base = (int)merged.assets.size();
        const int pbase = (int)merged.prefabs.size();
        merged.assets.insert(merged.assets.end(), r.assets.begin(), r.assets.end());
        merged.prefabs.insert(merged.prefabs.end(), r.prefabs.begin(),
                              r.prefabs.end());
        for (procgen::Instance inst : r.instances) {
            if (inst.asset >= 0) inst.asset += base;
            if (inst.prefab >= 0) inst.prefab += pbase;
            merged.instances.push_back(inst);
        }
        for (const std::string& w : r.warnings)
            if (std::find(merged.warnings.begin(), merged.warnings.end(), w) ==
                merged.warnings.end())
                merged.warnings.push_back(w);
        merged.overridesApplied += r.overridesApplied;
        merged.overridesOrphaned += r.overridesOrphaned;
    }
    // Prefab instances have no mesh of their own - a Pick Prefab point carries a
    // prefab index and no asset at all - so unless they are expanded here the
    // viewport draws NOTHING for them while the readout cheerfully reports
    // hundreds of instances. Expansion goes through prefab::instantiate, the
    // same function Insert into scene and the runtime spawner use, so the
    // preview cannot invent a placement the world would not produce.
    //
    // Capped: one prefab is tens of objects, so a few hundred points is already
    // thousands of draws. Past the cap the preview is truncated and SAYS so -
    // silently showing part of a world is the one outcome worse than showing
    // none of it.
    std::vector<SceneObject> prefabObjs;
    if (showProcPreview_) {
        const int kMaxPreviewObjects = 6000;
        int placed = 0;
        bool truncated = false;
        for (const procgen::Instance& inst : merged.instances) {
            if (inst.prefab < 0 || inst.prefab >= (int)merged.prefabs.size()) continue;
            const Prefab* pf = prefab::find(project_, merged.prefabs[inst.prefab]);
            if (!pf || pf->objects.empty()) continue;
            if (placed + (int)pf->objects.size() > kMaxPreviewObjects) {
                truncated = true;
                break;
            }
            // Only yaw: that is all a prefab instance carries on the console
            // (the spawner is a yaw plus a translation), so previewing the full
            // Euler would show a world the game cannot build.
            std::vector<SceneObject> objs =
                prefab::instantiate(*pf, inst.pos[0], inst.pos[1], inst.pos[2],
                                    inst.rot[1], inst.scale, "");
            placed += (int)objs.size();
            for (SceneObject& o : objs) prefabObjs.push_back(std::move(o));
        }
        if (truncated)
            merged.warnings.push_back(
                "prefab preview truncated at " + std::to_string(kMaxPreviewObjects) +
                " objects - the console still builds them all");
    }

    procResult_ = merged;
    procLastMs_ = ms;
    procNodesRun_ = nodesRun;
    procCandidates_ = candidates;
    procInstances_ = (int)merged.instances.size();
    procFraction_ = fraction;
    procPreviewSerial_ = trigger;
    procPreviewValid_ = true;
    ++procPreviewVersion_;

    Viewport::ScatterPreview sp;
    sp.version = procPreviewVersion_;
    // View > Procedural preview hides the generated GEOMETRY only. The mask and
    // curve overlays and the edit handles stay: they are the graph's authoring
    // tools, not its output, and a "Preview this node" that showed nothing
    // because a different toggle is off would be its own bug report.
    if (showProcPreview_) {
        sp.assets = merged.assets;
        sp.instances = merged.instances;
        sp.prefabObjects = std::move(prefabObjs);
    }
    sp.mask = merged.mask;
    sp.curve = merged.curve;
    // Handles: the control points of the curve node being edited, plus the
    // selected instance when the override tool is on (one highlighted marker
    // is all the feedback either tool needs).
    if (procCurveNode_ != 0 && procVolume_ >= 0 &&
        procVolume_ < (int)project_.objects().size()) {
        const ProcNode* cn =
            procgraph::node(project_.objects()[procVolume_].procGraph, procCurveNode_);
        if (cn && cn->type == "Curve") {
            for (const ProcRow& r : cn->rows) {
                sp.handles.push_back(r.v[0]);
                sp.handles.push_back(r.v[1]);
                sp.handles.push_back(r.v[2]);
            }
            sp.activeHandle = procCurvePoint_;
        }
    } else if (procOverrideMode_ && procSelInstance_) {
        for (const procgen::Instance& inst : merged.instances)
            if (inst.key == procSelInstance_) {
                sp.handles = {inst.pos[0], inst.pos[1] + 0.5f, inst.pos[2]};
                sp.activeHandle = 0;
            }
    }
    viewport_.setScatterPreview(std::move(sp));
}

int App::pickProcInstance(float u, float v) const {
    float o[3], d[3];
    viewport_.cameraRay(u, v, o, d);
    int best = -1;
    float bestT = 1e9f;
    const size_t from = (size_t)std::max(0, procOwnFirst_);
    const size_t to = std::min(procResult_.instances.size(),
                               from + (size_t)std::max(0, procOwnCount_));
    for (size_t i = from; i < to; ++i) {
        const procgen::Instance& inst = procResult_.instances[i];
        // Bounding sphere around the instance's own footprint: generous enough
        // to catch a thin trunk, tight enough not to swallow its neighbours.
        const float r = 0.9f * (inst.scale > 0.01f ? inst.scale : 1.0f);
        const float cx = inst.pos[0] - o[0];
        const float cy = inst.pos[1] + r - o[1];
        const float cz = inst.pos[2] - o[2];
        const float tca = cx * d[0] + cy * d[1] + cz * d[2];
        if (tca < 0.0f) continue;
        const float d2 = cx * cx + cy * cy + cz * cz - tca * tca;
        if (d2 > r * r) continue;
        if (tca < bestT) {
            bestT = tca;
            best = (int)i;
        }
    }
    return best;
}

procbake::Report App::bakeProcVolume(int objectIndex) {
    procbake::Report rep;
    if (objectIndex < 0 || objectIndex >= (int)project_.objects().size()) return rep;
    const std::string id = project_.objects()[objectIndex].id;
    if (id.empty()) {
        project::ensureObjectIds(project_);
        return bakeProcVolume(objectIndex);
    }
    rep = procbake::bakeVolume(project_, project_.active(), id,
                               &procCaches_[id]);
    commitChange();
    return rep;
}

// The project a build compiles: every stale Scatter volume is baked to its
// chunk meshes FIRST, so what runs on the console always matches the graph.
// This cannot live in project::refreshGenerated (which the Runner calls on its
// own copy): baking mutates the model - it inserts the chunk objects the scene
// table, live link and the disc layout then all see, and the editor must own
// that edit so it lands in the .tyra and on the undo stack.
Project& App::projectForBuild() {
    if (hasProject_ && procbake::anyStale(project_)) {
        const procbake::Report rep = bakeStaleProcVolumes();
        if (rep.volumes > 0) {
            statusMessage_ = "Baked " + std::to_string(rep.instances) +
                             " procedural instances into " +
                             std::to_string(rep.chunks) + " chunk meshes";
            for (const std::string& w : rep.warnings) statusMessage_ += " | " + w;
        }
    }
    return project_;
}

procbake::Report App::bakeStaleProcVolumes() {
    const procbake::Report rep = procbake::bakeAll(project_, false);
    if (rep.volumes > 0) commitChange();
    return rep;
}

// Pin id space of the procedural editor: node * 32 + slot, inputs 0..7,
// outputs 8..15. (The flow editor uses its own 16-wide space; these two
// canvases never share ids because they run in separate editor contexts.)
static int procInPin(int node, int slot) { return node * 32 + slot; }
static int procOutPin(int node, int slot) { return node * 32 + 8 + slot; }

// Pin colors per data type, so a glance says what can connect to what.
static ImU32 procTypeColor(ProcType t) {
    switch (t) {
        case ProcType::Points: return IM_COL32(120, 200, 130, 255);
        case ProcType::Mask: return IM_COL32(110, 160, 230, 255);
        case ProcType::Curve: return IM_COL32(230, 190, 90, 255);
    }
    return IM_COL32(200, 200, 200, 255);
}

// What a node's variable-length table means, column by column. The rows editor
// is drawn generically from ProcRowKind, so its columns are the one part of a
// node that the parameter tips cannot reach - and on a pool node the table IS
// the node.
static const char* procRowsHelp(ProcRowKind k) {
    switch (k) {
        case ProcRowKind::Assets:
            return "Pool rows: a model from res/models, its weight, and the "
                   "scale range one point may draw from. Weights are relative - "
                   "70/25/5 and 14/5/1 pick the same way.";
        case ProcRowKind::Prefabs:
            return "Pool rows: a prefab (Tools > Prefabs), its weight, and the "
                   "scale range one point may draw from. Weights are relative - "
                   "34/26/26/14 is simply 'the first one a bit more often'. The "
                   "picker can also capture a scene object as a one-member "
                   "prefab, which is how a primitive gets scattered.";
        case ProcRowKind::Points:
            return "Control points of the curve, in world XYZ. Edit in viewport "
                   "turns terrain clicks into new points.";
        case ProcRowKind::Settings:
            return "One row per property, applied to every object the bake "
                   "generates.";
        case ProcRowKind::None: break;
    }
    return nullptr;
}

// File name of an asset path. A pool row is a column of "res/models/..."
// strings whose shared prefix is exactly the part that does not identify it,
// and the node is the one place where width is scarce.
static std::string baseNameOf(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

// Hover help for the just-submitted rows header.
static void procRowsHelpTip(ProcRowKind k, float wrap) {
    const char* h = procRowsHelp(k);
    if (!h || !ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) return;
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(wrap);
    ImGui::TextUnformatted(h);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

// Hover help for one column of a pool row. The drags are unlabelled by
// necessity (they have to fit on a node), so this is the only place the
// numbers say what they are.
static void procPoolColTip(int col) {
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) return;
    static const char* const kTips[3] = {
        "Weight: how often this row is drawn, RELATIVE to the other rows. "
        "0 disables it without losing the row.",
        "Scale min: the smallest size a point may draw for this row. Equal to "
        "the max = every copy the same size.",
        "Scale max: the largest size a point may draw for this row. Vary "
        "Transform multiplies whatever is picked here.",
    };
    ImGui::SetTooltip("%s", kTips[col]);
}

// The node's documentation, drawn identically by the add-menu tooltip and by
// the node hover: what it does, then ONE LINE PER CONTROL. A paragraph that
// explains the idea but names none of the knobs sitting right under it is the
// failure this replaces - the reader is looking at "w 34" and "1.00 1.00" and
// the text talks about draw calls.
static void procNodeDoc(const ProcNodeType& t, float wrap) {
    ImGui::PushTextWrapPos(wrap);
    ImGui::TextUnformatted(t.title);
    if (t.desc && *t.desc) ImGui::TextDisabled("%s", t.desc);
    bool anyTip = false;
    for (const ProcParamDef& p : t.params) anyTip |= (p.tip && *p.tip);
    if (!t.params.empty()) {
        ImGui::Separator();
        for (const ProcParamDef& p : t.params) {
            ImGui::TextUnformatted(p.label);
            if (p.tip && *p.tip) {
                ImGui::SameLine(0.0f, 0.0f);
                ImGui::TextDisabled(" - %s", p.tip);
            } else if (anyTip) {
                // Nothing to add is a fine answer, but say it rather than
                // leaving a bare label that reads like a truncated line.
                ImGui::SameLine(0.0f, 0.0f);
                ImGui::TextDisabled(" - %s",
                                    p.kind == ProcParamKind::Bool ? "on/off" : "");
            }
        }
    }
    if (const char* rh = procRowsHelp(t.rows)) {
        ImGui::Separator();
        ImGui::TextDisabled("%s", rh);
    }
    ImGui::PopTextWrapPos();
}

void App::drawProceduralWindow() {
    if (!showProcedural_ || !hasProject_) return;
    ImGui::SetNextWindowSize(ImVec2(scaled(980.0f), scaled(620.0f)), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Procedural", &showProcedural_)) {
        ImGui::End();
        return;
    }

    const std::vector<int> vols = procVolumes();
    if (vols.empty()) {
        ImGui::TextWrapped(
            "A procedural graph lives on a Procedural volume: the object's box is "
            "the region the graph works in, and its output is baked to ordinary "
            "static meshes when you build. The graph decides HOW the box is "
            "filled - scattered over the ground, along a curve, or repeated "
            "exactly with Array / Radial Array.");
        ImGui::Spacing();
        ImGui::TextDisabled(
            "Same thing as + Add object > Procedural volume in the Project panel.");
        ImGui::Spacing();
        if (ImGui::Button("Add a procedural volume")) addScatterVolume();
        ImGui::End();
        return;
    }
    {
        int byId = -1;
        for (int idx : vols)
            if (!procVolumeId_.empty() && project_.objects()[idx].id == procVolumeId_)
                byId = idx;
        if (byId >= 0) {
            procVolume_ = byId;
        } else if (std::find(vols.begin(), vols.end(), procVolume_) == vols.end()) {
            procVolume_ = vols.front();
            procVolumeId_ = project_.objects()[procVolume_].id;
            procPositionsApplied_ = false;
            procPreviewNode_ = 0;
            procSeedPreview_ = 0;
        } else {
            procVolumeId_ = project_.objects()[procVolume_].id;
        }
    }

    // --- header: which volume, bake state ----------------------------------
    ImGui::SetNextItemWidth(scaled(200.0f));
    if (ImGui::BeginCombo("Volume", project_.objects()[procVolume_].name.c_str())) {
        for (int idx : vols) {
            const bool sel = idx == procVolume_;
            if (ImGui::Selectable(project_.objects()[idx].name.c_str(), sel) && !sel) {
                procVolume_ = idx;
                procVolumeId_ = project_.objects()[idx].id;
                procPositionsApplied_ = false;
                procPreviewNode_ = 0;
                procCurveNode_ = 0;
                procSelInstance_ = 0;
                procSeedPreview_ = 0;  // the simulated seed was that volume's
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Select in scene")) selectOnly(procVolume_);
    ImGui::SameLine();
    if (ImGui::SmallButton("New volume")) {
        addScatterVolume();
        ImGui::End();
        return;
    }

    SceneObject& vol = project_.objects()[procVolume_];
    ProcGraph& g = vol.procGraph;
    bool changed = false;

    // Staleness hashes the whole terrain and every object, so it is computed
    // once per model edit rather than once per frame (modelEditSerial_ is the
    // exact invalidation signal - see commitChange).
    if (!procStaleValid_ || procStaleSerial_ != modelEditSerial_) {
        procStaleSerial_ = modelEditSerial_;
        procStaleValid_ = true;
        procStale_ = g.bakedHash != procgen::bakeHash(project_, project_.active(), vol);
    }
    const bool stale = procStale_;
    ImGui::SameLine();

    // --- baked or runtime ---------------------------------------------------
    // The one decision that changes what everything else means, so it sits
    // first: a baked volume is finished geometry on the disc, a runtime one is
    // a program the console runs. See docs/procedural-runtime.md.
    {
        int mode = g.runtime ? 1 : 0;
        ImGui::SetNextItemWidth(scaled(150.0f));
        if (ImGui::Combo("##procmode", &mode, "Baked (build time)\0Runtime (on the console)\0")) {
            g.runtime = mode == 1;
            changed = true;
            // Leaving runtime mode leaves nothing behind; entering it throws
            // the baked chunks away, because they would draw on top of what
            // the console generates.
            if (g.runtime) {
                procbake::clearVolume(project_, project_.active(), vol.id);
                commitChange();
                statusMessage_ =
                    "Runtime mode: cleared the baked chunks of " + vol.name;
                ImGui::End();
                return;  // the objects vector changed under us
            }
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip(
                "Baked: the editor evaluates the graph and writes finished chunk\n"
                "meshes - the console loads geometry and never learns a graph\n"
                "existed. Costs disc space and one world, forever.\n\n"
                "Runtime: the graph is COMPILED into the game and evaluated on\n"
                "the EE, so the world can be different every boot and the\n"
                "geometry is never shipped. Costs load time, RAM, and a much\n"
                "smaller set of nodes - the list is under the budget bar.");
    }
    ImGui::SameLine();

    if (g.runtime) {
        // Runtime volumes have no bake, so the whole bake row is replaced by
        // what a runtime volume actually needs stated.
        bool atStart = g.runAtStart;
        if (ImGui::Checkbox("Generate at scene start", &atStart)) {
            g.runAtStart = atStart;
            changed = true;
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip(
                "On: the world is built during the scene load, inside the\n"
                "loading screen's progress bar. Off: nothing appears until a\n"
                "Generate Volume flow node fires - which is how you regenerate\n"
                "on a button press, or stage a world in pieces.");
        ImGui::SameLine();
        int seedMode = g.seedMode;
        ImGui::SetNextItemWidth(scaled(140.0f));
        if (ImGui::Combo("##procseedmode", &seedMode,
                         "Same world every run\0New world every run\0")) {
            g.seedMode = seedMode;
            changed = true;
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip(
                "\"Same\" uses the Seed below - reproducible, and what you want\n"
                "while authoring. \"New\" rolls one from the console clock at\n"
                "generation time; the real variety comes from regenerating on a\n"
                "button press, where the player's own timing is the entropy.");

        // The capability list is the honest part of the feature: it names what
        // this graph cannot do on the console instead of quietly doing nothing.
        const std::vector<procrt::Issue> issues = procrt::capability(g);
        if (issues.empty()) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.4f, 0.85f, 0.5f, 1.0f), "runs on console");
        } else {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.35f, 1.0f),
                               "%d node(s) the console cannot run",
                               (int)issues.size());
        }
        for (const procrt::Issue& i : issues)
            ImGui::TextColored(ImVec4(0.9f, 0.55f, 0.5f, 1.0f), "- %s",
                               i.text.c_str());
    } else if (ImGui::Button(stale ? "Bake now *" : "Bake now")) {
        const procbake::Report rep = bakeProcVolume(procVolume_);
        procStatus_ = "Baked " + std::to_string(rep.instances) + " instances into " +
                      std::to_string(rep.chunks) + " chunk meshes (" +
                      std::to_string(rep.triangles) + " triangles)";
        for (const std::string& w : rep.warnings) procStatus_ += " | " + w;
        statusMessage_ = procStatus_;
        // The bake inserted/removed chunk objects, so every reference into the
        // objects vector below this point is dangling - finish the frame here.
        ImGui::End();
        return;
    }
    if (!g.runtime) {
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip(
                "Writes the merged chunk meshes and the scene objects that draw "
                "them.\nA build does this automatically for every stale volume - "
                "this button is for when you want to look at the result now.");
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear bake")) {
            procbake::clearVolume(project_, project_.active(), vol.id);
            commitChange();
            statusMessage_ = "Cleared the baked chunks of " + vol.name;
            ImGui::End();
            return;  // objects vector changed under us
        }
        ImGui::SameLine();
        if (stale)
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.25f, 1.0f), "bake is stale");
        else
            ImGui::TextColored(ImVec4(0.4f, 0.85f, 0.5f, 1.0f), "baked");
    }

    // --- live budget (BAKE-03) ---------------------------------------------
    const ProcNode* outNode = procgraph::outputNode(g);
    const int budget = outNode ? std::max(1, procgraph::inum(*outNode, "budget")) : 20000;
    ImGui::Text("%d instances (of %d candidates) | %d chunks | %d triangles | ~%.0f KB "
                "| %d nodes run, %.1f ms",
                procInstances_, procCandidates_, procBudget_.chunks,
                procBudget_.triangles, procBudget_.vertexBytes / 1024.0, procNodesRun_,
                procLastMs_);
    {
        const float frac = std::clamp((float)procBudget_.triangles / (float)budget,
                                      0.0f, 1.0f);
        const bool over = procBudget_.triangles > budget;
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram,
                              over ? ImVec4(0.9f, 0.35f, 0.3f, 1.0f)
                                   : ImVec4(0.35f, 0.7f, 0.45f, 1.0f));
        char label[64];
        std::snprintf(label, sizeof(label), "%d / %d tris", procBudget_.triangles, budget);
        ImGui::ProgressBar(frac, ImVec2(scaled(220.0f), 0.0f), label);
        ImGui::PopStyleColor();
    }
    if (procFraction_ < 1.0f) {
        ImGui::SameLine();
        ImGui::TextDisabled("(preview at %.0f%% density while dragging)",
                            procFraction_ * 100.0f);
    }
    for (const std::string& w : procResult_.warnings)
        ImGui::TextColored(ImVec4(0.95f, 0.8f, 0.35f, 1.0f), "! %s", w.c_str());
    {
        const std::vector<procgraph::ProcIssue> issues = procgraph::validate(g);
        for (const procgraph::ProcIssue& i : issues)
            ImGui::TextColored(ImVec4(0.9f, 0.55f, 0.5f, 1.0f), "- %s", i.text.c_str());
    }

    // --- tool row -----------------------------------------------------------
    ImGui::Separator();
    int seed = (int)g.seed;
    ImGui::SetNextItemWidth(scaled(90.0f));
    if (ImGui::InputInt("Seed", &seed)) {
        g.seed = (uint32_t)std::max(1, seed);
        changed = true;
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip(
            "The one knob that reshuffles everything. Every random draw comes from "
            "(seed, node, point) - so editing one node never disturbs the rest.");
    ImGui::SameLine();
    if (ImGui::SmallButton("Reseed")) {
        g.seed = g.seed * 1664525u + 1013904223u;
        if (g.seed == 0) g.seed = 1;
        changed = true;
    }
    ImGui::SameLine();
    if (procPreviewNode_ != 0) {
        const ProcNode* pn = procgraph::node(g, procPreviewNode_);
        const ProcNodeType* pt = pn ? procNodeType(pn->type) : nullptr;
        ImGui::TextColored(ImVec4(0.6f, 0.85f, 1.0f, 1.0f), "Showing: %s",
                           pt ? pt->title : "?");
        ImGui::SameLine();
        if (ImGui::SmallButton("Show final output")) procPreviewNode_ = 0;
    } else {
        ImGui::TextDisabled("Showing the final output (right-click a node > Preview)");
    }
    ImGui::SameLine();
    // The same flag the View menu carries - stated here too because this is the
    // window you are in when the preview gets in your way.
    ImGui::Checkbox("Show preview", &showProcPreview_);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip(
            "Draw what the volumes generate. Off = work on what is underneath;\n"
            "a finished forest hides the ground it grows on. The graph keeps\n"
            "being evaluated either way, so the numbers above, the warnings and\n"
            "the seed simulator stay live - and mask/curve node previews and the\n"
            "curve handles are still drawn, because those are tools rather than\n"
            "output. Also View > Procedural preview.");
    ImGui::SameLine();
    if (ImGui::Checkbox("Edit instances", &procOverrideMode_)) {
        procCurveNode_ = 0;
        procSelInstance_ = 0;
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip(
            "Click an instance in the viewport to move, rotate, rescale or delete "
            "it by hand. The edit is stored against the point's stable identity, "
            "so it survives every later re-evaluation of the graph.");

    // --- the per-instance override editor (FILT-05) -------------------------
    if (procOverrideMode_) {
        ImGui::Indent(scaled(8.0f));
        const procgen::Instance* sel = nullptr;
        for (const procgen::Instance& inst : procResult_.instances)
            if (inst.key == procSelInstance_) sel = &inst;
        if (!sel) {
            ImGui::TextDisabled(
                "Click an instance in the viewport. Ctrl+click removes it.");
        } else {
            ProcOverride& ov = procOverrideFor(g, procSelInstance_);
            ImGui::Text("Instance at %.1f, %.1f, %.1f", sel->pos[0], sel->pos[1],
                        sel->pos[2]);
            ImGui::SetNextItemWidth(scaled(200.0f));
            if (ImGui::DragFloat3("Offset", ov.offset, 0.05f)) changed = true;
            ImGui::SetNextItemWidth(scaled(200.0f));
            if (ImGui::DragFloat3("Rotate", ov.rotate, 1.0f)) changed = true;
            ImGui::SetNextItemWidth(scaled(90.0f));
            if (ImGui::DragFloat("Scale x", &ov.scale, 0.01f, 0.05f, 20.0f))
                changed = true;
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove instance")) {
                ov.removed = true;
                procSelInstance_ = 0;
                changed = true;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Revert")) {
                ov = ProcOverride{};
                ov.key = procSelInstance_;
                changed = true;
            }
        }
        if (!g.overrides.empty()) {
            ImGui::Text("%d manual edit%s", (int)g.overrides.size(),
                        g.overrides.size() == 1 ? "" : "s");
            if (procResult_.overridesOrphaned > 0) {
                ImGui::SameLine();
                ImGui::TextDisabled("(%d no longer match a point)",
                                    procResult_.overridesOrphaned);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Clear all edits")) {
                g.overrides.clear();
                procSelInstance_ = 0;
                changed = true;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Drop unmatched")) {
                // An override whose point is gone is kept by default (the point
                // may come back when a slider moves) - this is the explicit
                // cleanup.
                std::vector<uint64_t> live;
                for (const procgen::Instance& i : procResult_.instances)
                    live.push_back(i.key);
                g.overrides.erase(
                    std::remove_if(g.overrides.begin(), g.overrides.end(),
                                   [&](const ProcOverride& o) {
                                       return std::find(live.begin(), live.end(),
                                                        o.key) == live.end();
                                   }),
                    g.overrides.end());
                changed = true;
            }
        }
        ImGui::Unindent(scaled(8.0f));
    }

    // --- seed simulator (runtime volumes only) ------------------------------
    // A baked volume has one world and you are looking at it. A RUNTIME one is
    // a program, and with "New world every run" the seed in the graph is not
    // the seed the player gets - so "what does this graph produce" is a
    // question about a spread, and the only way to answer it used to be to
    // build the game and boot it. This runs the graph on several seeds right
    // here: the table is the spread, clicking a row previews that world in the
    // viewport, and the summary is the honest budget answer (the worst seed is
    // the one that ships).
    if (g.runtime) {
        if (ImGui::TreeNodeEx("Seed simulator##procseedsim",
                              ImGuiTreeNodeFlags_SpanAvailWidth)) {
            ImGui::Indent(scaled(8.0f));
            ImGui::SetNextItemWidth(scaled(110.0f));
            ImGui::SliderInt("Seeds", &procSeedCount_, 2, 24);
            ImGui::SameLine();
            if (ImGui::Button("Simulate")) runProcSeedSweep(procVolume_, procSeedCount_);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                ImGui::SetTooltip(
                    "Evaluates this graph once per seed at FULL density - about\n"
                    "%.0f ms each, so %d seeds costs roughly %.1f s. That is why\n"
                    "it is a button and not a live readout.",
                    procLastMs_, procSeedCount_,
                    procLastMs_ * procSeedCount_ / 1000.0);
            ImGui::SameLine();
            if (procSeedPreview_ != 0) {
                ImGui::TextColored(ImVec4(0.6f, 0.85f, 1.0f, 1.0f),
                                   "viewport: seed %u", (unsigned)procSeedPreview_);
                ImGui::SameLine();
                if (ImGui::SmallButton("Back to the authored seed"))
                    procSeedPreview_ = 0;
            } else {
                ImGui::TextDisabled("viewport: the authored seed (%u)",
                                    (unsigned)g.seed);
            }
            if (g.seedMode == 1)
                ImGui::TextColored(
                    ImVec4(0.95f, 0.8f, 0.35f, 1.0f),
                    "This volume rolls a new seed every run - the player gets one "
                    "of these, not the authored one.");

            const bool mine = procSeedTrialsFor_ == vol.id && !procSeedTrials_.empty();
            if (!mine) {
                ImGui::TextDisabled(
                    "Press Simulate to see what other seeds would build.");
            } else {
                int minI = INT32_MAX, maxI = 0, minT = INT32_MAX, maxT = 0, over = 0;
                for (const ProcSeedTrial& t : procSeedTrials_) {
                    minI = std::min(minI, t.instances);
                    maxI = std::max(maxI, t.instances);
                    minT = std::min(minT, t.triangles);
                    maxT = std::max(maxT, t.triangles);
                    if (t.triangles > budget) ++over;
                }
                if (ImGui::BeginTable("procseeds", 5,
                                      ImGuiTableFlags_RowBg |
                                          ImGuiTableFlags_SizingStretchProp |
                                          ImGuiTableFlags_ScrollY,
                                      ImVec2(0, scaled(150.0f)))) {
                    ImGui::TableSetupColumn("Seed");
                    ImGui::TableSetupColumn("Instances");
                    ImGui::TableSetupColumn("Triangles");
                    ImGui::TableSetupColumn("Chunks");
                    ImGui::TableSetupColumn("");
                    ImGui::TableHeadersRow();
                    for (size_t i = 0; i < procSeedTrials_.size(); ++i) {
                        const ProcSeedTrial& t = procSeedTrials_[i];
                        const bool authored = t.seed == g.seed;
                        const bool shown = authored ? procSeedPreview_ == 0
                                                    : procSeedPreview_ == t.seed;
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::PushID((int)i);
                        char lab[64];
                        std::snprintf(lab, sizeof(lab), "%u%s", (unsigned)t.seed,
                                      authored ? " (authored)" : "");
                        if (ImGui::Selectable(lab, shown,
                                              ImGuiSelectableFlags_SpanAllColumns))
                            procSeedPreview_ = authored ? 0 : t.seed;
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                            ImGui::SetTooltip(
                                "Show this world in the viewport (%.0f ms to "
                                "evaluate%s).",
                                t.millis,
                                t.warnings ? ", with warnings" : "");
                        ImGui::TableNextColumn();
                        ImGui::Text("%d", t.instances);
                        ImGui::TableNextColumn();
                        if (t.triangles > budget)
                            ImGui::TextColored(ImVec4(0.9f, 0.35f, 0.3f, 1.0f), "%d",
                                               t.triangles);
                        else
                            ImGui::Text("%d", t.triangles);
                        ImGui::TableNextColumn();
                        ImGui::Text("%d", t.chunks);
                        ImGui::TableNextColumn();
                        if (!authored && ImGui::SmallButton("Use")) {
                            g.seed = t.seed;
                            procSeedPreview_ = 0;
                            changed = true;
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
                ImGui::Text("%d seeds: %d-%d instances, %d-%d triangles",
                            (int)procSeedTrials_.size(), minI, maxI, minT, maxT);
                if (over > 0)
                    ImGui::TextColored(ImVec4(0.9f, 0.35f, 0.3f, 1.0f),
                                       "%d of %d seeds exceed the %d-triangle "
                                       "budget - the console gets the worst one "
                                       "eventually.",
                                       over, (int)procSeedTrials_.size(), budget);
                else
                    ImGui::TextColored(ImVec4(0.4f, 0.85f, 0.5f, 1.0f),
                                       "every simulated seed fits the %d-triangle "
                                       "budget.",
                                       budget);
            }
            ImGui::Unindent(scaled(8.0f));
            ImGui::TreePop();
        }
    } else if (procSeedPreview_ != 0) {
        procSeedPreview_ = 0;  // a baked volume shows what it bakes
    }

    // --- node canvas --------------------------------------------------------
    ImGui::Separator();
    if (procEditorCtx_) ImNodes::EditorContextSet((ImNodesEditorContext*)procEditorCtx_);

    const float zoom = procZoom_;
    const ImVec2 canvasOrigin = ImGui::GetCursorScreenPos();
    ImGui::SetWindowFontScale(zoom);
    const ImGuiStyle& gstyle = ImGui::GetStyle();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                        ImVec2(gstyle.ItemSpacing.x * zoom, gstyle.ItemSpacing.y * zoom));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        ImVec2(gstyle.FramePadding.x * zoom,
                               gstyle.FramePadding.y * zoom));
    ImNodesStyle& nstyle = ImNodes::GetStyle();
    const ImNodesStyle savedStyle = nstyle;
    nstyle.GridSpacing *= zoom;
    nstyle.NodeCornerRounding *= zoom;
    nstyle.NodePadding = ImVec2(savedStyle.NodePadding.x * zoom,
                                savedStyle.NodePadding.y * zoom);
    nstyle.NodeBorderThickness *= zoom;
    nstyle.LinkThickness *= zoom;
    nstyle.PinCircleRadius *= zoom;
    nstyle.PinQuadSideLength *= zoom;
    nstyle.PinHoverRadius *= zoom;
    nstyle.PinOffset *= zoom;

    ImNodes::BeginNodeEditor();
    if (!procPositionsApplied_) {
        procPositionsApplied_ = true;
        for (const ProcNode& n : g.nodes)
            ImNodes::SetNodeGridSpacePos(n.id, ImVec2(n.pos[0] * zoom, n.pos[1] * zoom));
    }

    // The asset pickers need the model list, but listAssetFiles walks
    // res/models recursively - once a second is plenty for a folder the user
    // edits from outside the editor.
    if (ImGui::GetTime() - procModelsAt_ > 1.0) {
        procModels_ = listAssetFiles("models", ".obj");
        procModelsAt_ = ImGui::GetTime();
    }
    const std::vector<std::string>& models = procModels_;

    for (ProcNode& n : g.nodes) {
        const ProcNodeType* t = procNodeType(n.type);
        if (!t) continue;
        // Title bar color by role: sources green, masks blue, filters/attrs
        // neutral, the Output amber (there is exactly one).
        ImU32 title = IM_COL32(60, 80, 140, 255);
        if (std::strcmp(t->category, "Sources") == 0) title = IM_COL32(40, 110, 60, 255);
        else if (std::strcmp(t->category, "Masks") == 0) title = IM_COL32(45, 85, 150, 255);
        else if (std::strcmp(t->category, "Repeat") == 0) title = IM_COL32(95, 60, 130, 255);
        else if (std::strcmp(t->category, "Output") == 0) title = IM_COL32(140, 100, 40, 255);
        if (n.id == procPreviewNode_) title = IM_COL32(30, 130, 170, 255);
        if (n.bypass) title = IM_COL32(90, 90, 90, 255);
        ImNodes::PushColorStyle(ImNodesCol_TitleBar, title);
        ImNodes::BeginNode(n.id);
        ImNodes::BeginNodeTitleBar();
        ImGui::TextUnformatted(t->title);
        if (n.bypass) {
            ImGui::SameLine();
            ImGui::TextDisabled("(bypassed)");
        }
        ImNodes::EndNodeTitleBar();

        ImGui::PushID(n.id);
        // Wide enough that an enum, an object name or a pool row READS. A node
        // whose every combo says "(terra" or "(pick a" is a node you have to
        // click to understand, and these are read far more often than edited.
        const float itemW = 168.0f * zoom;
        const float poolW = 236.0f * zoom;
        ImGui::PushItemWidth(itemW);

        // The numeric half of a pool row - weight and the scale range - shared
        // by the asset and prefab pools, which are deliberately the same shape
        // (one graph mixing scattered models with scattered rooms should not
        // need two mental models). Returns true when the row was edited, and
        // sets `remove` when its x was pressed.
        auto poolNumbers = [&](ProcRow& row, bool& remove) {
            bool hit = false;
            const float bw = ImGui::CalcTextSize("x").x + ImGui::GetStyle().FramePadding.x * 2.0f;
            const float gap = ImGui::GetStyle().ItemSpacing.x;
            const float cell = std::max(40.0f, (poolW - bw - gap * 3.0f) / 3.0f);
            ImGui::SetNextItemWidth(cell);
            hit |= ImGui::DragFloat("##w", &row.v[0], 0.5f, 0.0f, 1000.0f, "w %.0f");
            procPoolColTip(0);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(cell);
            hit |= ImGui::DragFloat("##smin", &row.v[1], 0.01f, 0.05f, 20.0f, "x%.2f");
            procPoolColTip(1);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(cell);
            hit |= ImGui::DragFloat("##smax", &row.v[2], 0.01f, 0.05f, 20.0f, "x%.2f");
            procPoolColTip(2);
            ImGui::SameLine();
            if (ImGui::SmallButton("x")) remove = true;
            return hit;
        };

        // Input pins.
        for (size_t i = 0; i < t->ins.size(); ++i) {
            ImNodes::PushColorStyle(ImNodesCol_Pin, procTypeColor(t->ins[i].type));
            ImNodes::BeginInputAttribute(procInPin(n.id, (int)i),
                                         t->ins[i].type == ProcType::Points
                                             ? ImNodesPinShape_CircleFilled
                                             : ImNodesPinShape_QuadFilled);
            const bool linked = procgraph::linkTo(g, n.id, (int)i) != nullptr;
            if (linked || t->ins[i].optional)
                ImGui::TextUnformatted(t->ins[i].label);
            else
                ImGui::TextColored(ImVec4(0.95f, 0.6f, 0.5f, 1.0f), "%s",
                                   t->ins[i].label);
            ImNodes::EndInputAttribute();
            ImNodes::PopColorStyle();
        }

        // Parameters.
        for (const ProcParamDef& p : t->params) {
            switch (p.kind) {
                case ProcParamKind::Float: {
                    float v = procgraph::num(n, p.key);
                    if (ImGui::DragFloat(p.label, &v, (p.hi - p.lo) / 400.0f, p.lo,
                                         p.hi)) {
                        n.nums[p.key] = std::clamp(v, p.lo, p.hi);
                        changed = true;
                    }
                    break;
                }
                case ProcParamKind::Int: {
                    int v = procgraph::inum(n, p.key);
                    if (ImGui::DragInt(p.label, &v, 1.0f, (int)p.lo, (int)p.hi)) {
                        n.nums[p.key] = (float)std::clamp(v, (int)p.lo, (int)p.hi);
                        changed = true;
                    }
                    break;
                }
                case ProcParamKind::Bool: {
                    bool v = procgraph::flag(n, p.key);
                    if (ImGui::Checkbox(p.label, &v)) {
                        n.nums[p.key] = v ? 1.0f : 0.0f;
                        changed = true;
                    }
                    break;
                }
                case ProcParamKind::Enum: {
                    // "a|b|c" -> a combo; the stored value is the index.
                    std::vector<std::string> opts;
                    std::string cur;
                    for (const char* c = p.choices; ; ++c) {
                        if (*c == '|' || *c == 0) {
                            opts.push_back(cur);
                            cur.clear();
                            if (*c == 0) break;
                        } else {
                            cur += *c;
                        }
                    }
                    int v = std::clamp(procgraph::inum(n, p.key), 0,
                                       (int)opts.size() - 1);
                    if (ImGui::BeginCombo(p.label, opts[v].c_str())) {
                        for (int i = 0; i < (int)opts.size(); ++i)
                            if (ImGui::Selectable(opts[i].c_str(), i == v) && i != v) {
                                n.nums[p.key] = (float)i;
                                changed = true;
                            }
                        ImGui::EndCombo();
                    }
                    break;
                }
                case ProcParamKind::ObjectName: {
                    std::string cur = procgraph::str(n, p.key);
                    const char* none = p.emptyLabel && *p.emptyLabel
                                           ? p.emptyLabel
                                           : "(terrain)";
                    if (ImGui::BeginCombo(p.label, cur.empty() ? none : cur.c_str())) {
                        if (ImGui::Selectable(none, cur.empty())) {
                            n.strs[p.key] = "";
                            changed = true;
                        }
                        for (const SceneObject& o : project_.objects()) {
                            if (o.type == PrimitiveType::Scatter) continue;
                            if (!o.procSource.empty()) continue;
                            if (ImGui::Selectable(o.name.c_str(), cur == o.name) &&
                                cur != o.name) {
                                n.strs[p.key] = o.name;
                                changed = true;
                            }
                        }
                        ImGui::EndCombo();
                    }
                    break;
                }
                case ProcParamKind::Attr: {
                    std::string cur = procgraph::str(n, p.key);
                    if (cur.empty()) cur = procattr::kSlope;
                    if (ImGui::BeginCombo(p.label, cur.c_str())) {
                        for (const char* a :
                             {procattr::kSlope, procattr::kHeight, procattr::kMask,
                              procattr::kSize, procattr::kRandom, procattr::kCurveT,
                              procattr::kDist, procattr::kNormalY})
                            if (ImGui::Selectable(a, cur == a) && cur != a) {
                                n.strs[p.key] = a;
                                changed = true;
                            }
                        ImGui::EndCombo();
                    }
                    break;
                }
                case ProcParamKind::Text: {
                    char buf[96] = {};
                    std::snprintf(buf, sizeof(buf), "%s",
                                  procgraph::str(n, p.key).c_str());
                    if (ImGui::InputText(p.label, buf, sizeof(buf))) {
                        n.strs[p.key] = buf;
                        changed = true;
                    }
                    break;
                }
            }
            if (p.tip && *p.tip && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos(scaled(320.0f));
                ImGui::TextUnformatted(p.tip);
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
        }

        // Variable-length tables: the asset pool and the curve's control points.
        if (t->rows == ProcRowKind::Assets) {
            ImGui::TextDisabled("Pool (model, weight, scale min/max)");
            procRowsHelpTip(t->rows, scaled(320.0f));
            int removeRow = -1;
            for (size_t r = 0; r < n.rows.size(); ++r) {
                ImGui::PushID((int)r);
                ProcRow& row = n.rows[r];
                ImGui::SetNextItemWidth(poolW);
                // The BASENAME, not the stored path: "res/models/" is the same
                // on every row and eats the width the actual name needs.
                const std::string label =
                    row.s.empty() ? "(pick a model)" : baseNameOf(row.s);
                if (ImGui::BeginCombo("##asset", label.c_str())) {
                    for (const std::string& m : models) {
                        const std::string rel = "res/models/" + m;
                        if (ImGui::Selectable(m.c_str(), rel == row.s) && rel != row.s) {
                            row.s = rel;
                            changed = true;
                        }
                    }
                    if (models.empty())
                        ImGui::TextDisabled("no .obj files in res/models");
                    // Where the other half of the answer lives. Asked as "you
                    // cannot pick a primitive here" - true, and this node is
                    // never going to be the place for it, so it points.
                    ImGui::Separator();
                    ImGui::TextDisabled("a primitive or anything else in the "
                                        "scene?\nuse a Pick Prefab node");
                    ImGui::EndCombo();
                }
                if (!row.s.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                    ImGui::SetTooltip("%s", row.s.c_str());
                bool rm = false;
                if (poolNumbers(row, rm)) changed = true;
                if (rm) removeRow = (int)r;
                ImGui::PopID();
            }
            if (removeRow >= 0) {
                n.rows.erase(n.rows.begin() + removeRow);
                changed = true;
            }
            if (ImGui::SmallButton("+ asset")) {
                ProcRow row;
                row.v[0] = 50.0f;
                row.v[1] = 0.9f;
                row.v[2] = 1.1f;
                if (!models.empty()) row.s = "res/models/" + models.front();
                n.rows.push_back(row);
                changed = true;
            }
        } else if (t->rows == ProcRowKind::Prefabs) {
            // The prefab pool. Same shape as the asset pool - a prefab is
            // simply a different KIND of thing to place, and keeping the two
            // rows identical is what lets one graph mix scattered models with
            // scattered rooms without a second mental model.
            ImGui::TextDisabled("Pool (prefab, weight, scale min/max)");
            procRowsHelpTip(t->rows, scaled(320.0f));
            int removeRow = -1;
            for (size_t r = 0; r < n.rows.size(); ++r) {
                ImGui::PushID((int)(3000 + r));
                ProcRow& row = n.rows[r];
                ImGui::SetNextItemWidth(poolW);
                const std::string label =
                    row.s.empty() ? "(pick a prefab)" : row.s;
                if (ImGui::BeginCombo("##prefab", label.c_str())) {
                    for (const Prefab& pf : project_.prefabs)
                        if (ImGui::Selectable(pf.name.c_str(), pf.name == row.s) &&
                            pf.name != row.s) {
                            row.s = pf.name;
                            changed = true;
                        }
                    if (project_.prefabs.empty())
                        ImGui::TextDisabled("no prefabs yet - pick from the scene "
                                            "below, or Tools > Prefabs");
                    // "You cannot scatter a primitive" - you can, and this is
                    // where. Capturing ONE scene object makes an ordinary
                    // one-member prefab, so a scattered box travels the exact
                    // path a scattered room does: merged into the chunk bags,
                    // costed by the Prefabs window, runnable on the console.
                    // A second mechanism for "scatter a scene object" would be
                    // the same feature with its own bugs.
                    ImGui::Separator();
                    ImGui::TextDisabled("Capture from the scene");
                    const std::vector<SceneObject>& objs = project_.objects();
                    bool anyCapturable = false;
                    for (size_t oi = 0; oi < objs.size(); ++oi) {
                        const SceneObject& o = objs[oi];
                        // Not a volume (it would scatter itself) and not this
                        // bake's own output.
                        if (o.type == PrimitiveType::Scatter) continue;
                        if (!o.procSource.empty()) continue;
                        anyCapturable = true;
                        if (!ImGui::Selectable(o.name.c_str())) continue;
                        Prefab np = prefab::capture(
                            project_.active(), {(int)oi},
                            prefab::uniqueName(project_, o.name));
                        if (np.objects.empty()) continue;
                        project_.prefabs.push_back(std::move(np));
                        row.s = project_.prefabs.back().name;
                        changed = true;
                        statusMessage_ =
                            "Captured \"" + o.name + "\" as the prefab \"" + row.s +
                            "\" - the original object stays in the scene, delete "
                            "it if it was only a template";
                    }
                    if (!anyCapturable)
                        ImGui::TextDisabled("(nothing in the scene to capture)");
                    ImGui::EndCombo();
                }
                bool rm = false;
                if (poolNumbers(row, rm)) changed = true;
                if (rm) removeRow = (int)r;
                ImGui::PopID();
            }
            if (removeRow >= 0) {
                n.rows.erase(n.rows.begin() + removeRow);
                changed = true;
            }
            if (ImGui::SmallButton("+ prefab")) {
                ProcRow row;
                row.v[0] = 50.0f;
                row.v[1] = 1.0f;
                row.v[2] = 1.0f;
                if (!project_.prefabs.empty()) row.s = project_.prefabs.front().name;
                n.rows.push_back(row);
                changed = true;
            }
        } else if (t->rows == ProcRowKind::Points) {
            ImGui::TextDisabled("%d control points", (int)n.rows.size());
            const bool editing = procCurveNode_ == n.id;
            if (ImGui::SmallButton(editing ? "Editing in viewport" : "Edit in viewport")) {
                procCurveNode_ = editing ? 0 : n.id;
                procCurvePoint_ = -1;
                procOverrideMode_ = false;
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                ImGui::SetTooltip(
                    "While this is on, clicking the terrain appends a control "
                    "point; pick a point below and click again to move it.");
            int removeRow = -1;
            for (size_t r = 0; r < n.rows.size(); ++r) {
                ImGui::PushID((int)(1000 + r));
                const bool sel = editing && procCurvePoint_ == (int)r;
                if (ImGui::RadioButton("##sel", sel)) procCurvePoint_ = sel ? -1 : (int)r;
                ImGui::SameLine();
                ImGui::SetNextItemWidth(150.0f * zoom);
                if (ImGui::DragFloat3("##p", n.rows[r].v, 0.1f)) changed = true;
                ImGui::SameLine();
                if (ImGui::SmallButton("x")) removeRow = (int)r;
                ImGui::PopID();
            }
            if (removeRow >= 0) {
                n.rows.erase(n.rows.begin() + removeRow);
                if (procCurvePoint_ >= (int)n.rows.size()) procCurvePoint_ = -1;
                changed = true;
            }
        } else if (t->rows == ProcRowKind::Settings) {
            // One row per property the bake should set on every generated
            // object. The list of what CAN be set is procObjectProps(), so the
            // node grows a new switch without touching this code.
            ImGui::TextDisabled("Applied to every generated object");
            int removeRow = -1;
            for (size_t r = 0; r < n.rows.size(); ++r) {
                ImGui::PushID((int)(2000 + r));
                ProcRow& row = n.rows[r];
                const ProcObjProp* prop = procObjectProp(row.s);
                ImGui::SetNextItemWidth(150.0f * zoom);
                if (ImGui::BeginCombo("##prop",
                                      prop ? prop->label : "(unknown)")) {
                    for (const ProcObjProp& p : procObjectProps()) {
                        const bool sel = row.s == p.key;
                        if (ImGui::Selectable(p.label, sel) && !sel) {
                            row.s = p.key;
                            row.v[0] = p.def;
                            changed = true;
                        }
                    }
                    ImGui::EndCombo();
                }
                if (prop && prop->tip && *prop->tip &&
                    ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
                    ImGui::BeginTooltip();
                    ImGui::PushTextWrapPos(scaled(320.0f));
                    ImGui::TextUnformatted(prop->tip);
                    ImGui::PopTextWrapPos();
                    ImGui::EndTooltip();
                }
                ImGui::SameLine();
                if (prop && prop->kind == ProcObjPropKind::Bool) {
                    bool on = row.v[0] >= 0.5f;
                    if (ImGui::Checkbox("##v", &on)) {
                        row.v[0] = on ? 1.0f : 0.0f;
                        changed = true;
                    }
                } else {
                    ImGui::SetNextItemWidth(70.0f * zoom);
                    const float lo = prop ? prop->lo : 0.0f;
                    const float hi = prop ? prop->hi : 1.0f;
                    if (ImGui::DragFloat("##v", &row.v[0], (hi - lo) / 400.0f, lo,
                                         hi, "%.1f")) {
                        row.v[0] = std::clamp(row.v[0], lo, hi);
                        changed = true;
                    }
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("x")) removeRow = (int)r;
                ImGui::PopID();
            }
            if (removeRow >= 0) {
                n.rows.erase(n.rows.begin() + removeRow);
                changed = true;
            }
            if (ImGui::SmallButton("+ property")) {
                // Offer the first property this node does not already carry -
                // two rows for one field would be a silent contradiction.
                const ProcObjProp* next = nullptr;
                for (const ProcObjProp& p : procObjectProps()) {
                    bool taken = false;
                    for (const ProcRow& r : n.rows) taken |= (r.s == p.key);
                    if (!taken) {
                        next = &p;
                        break;
                    }
                }
                if (next) {
                    ProcRow row;
                    row.s = next->key;
                    row.v[0] = next->def;
                    n.rows.push_back(row);
                    changed = true;
                }
            }
        }

        // Output pins, right-aligned-ish (imnodes right-aligns the pin itself).
        for (size_t i = 0; i < t->outs.size(); ++i) {
            ImNodes::PushColorStyle(ImNodesCol_Pin, procTypeColor(t->outs[i].type));
            ImNodes::BeginOutputAttribute(procOutPin(n.id, (int)i),
                                          t->outs[i].type == ProcType::Points
                                              ? ImNodesPinShape_CircleFilled
                                              : ImNodesPinShape_QuadFilled);
            ImGui::TextUnformatted(t->outs[i].label);
            ImNodes::EndOutputAttribute();
            ImNodes::PopColorStyle();
        }

        ImGui::PopItemWidth();
        ImGui::PopID();
        ImNodes::EndNode();
        ImNodes::PopColorStyle();
    }

    for (const ProcLink& l : g.links) {
        const ProcNode* from = procgraph::node(g, l.fromNode);
        const ProcNodeType* ft = from ? procNodeType(from->type) : nullptr;
        const ProcType ty = (ft && l.fromPin < (int)ft->outs.size())
                                ? ft->outs[l.fromPin].type
                                : ProcType::Points;
        ImNodes::PushColorStyle(ImNodesCol_Link, procTypeColor(ty));
        ImNodes::Link(l.id, procOutPin(l.fromNode, l.fromPin),
                      procInPin(l.toNode, l.toPin));
        ImNodes::PopColorStyle();
    }

    ImNodes::MiniMap(0.15f, ImNodesMiniMapLocation_BottomRight);
    const bool editorHovered = ImNodes::IsEditorHovered();
    ImNodes::EndNodeEditor();

    // Hover description (ProcNodeType::desc - the node's documentation).
    {
        int hovered = -1;
        if (ImNodes::IsNodeHovered(&hovered) && !ImGui::IsAnyMouseDown()) {
            if (hovered != procDescNode_) {
                procDescNode_ = hovered;
                procDescSince_ = ImGui::GetTime();
            } else if (ImGui::GetTime() - procDescSince_ > 0.6) {
                const ProcNode* hn = procgraph::node(g, hovered);
                const ProcNodeType* ht = hn ? procNodeType(hn->type) : nullptr;
                if (ht) {
                    ImGui::BeginTooltip();
                    procNodeDoc(*ht, scaled(360.0f));
                    ImGui::EndTooltip();
                }
            }
        } else {
            procDescNode_ = -1;
        }
    }

    nstyle = savedStyle;
    ImGui::PopStyleVar(2);
    ImGui::SetWindowFontScale(1.0f);

    for (ProcNode& n : g.nodes) {
        const ImVec2 pos = ImNodes::GetNodeGridSpacePos(n.id);
        n.pos[0] = pos.x / zoom;
        n.pos[1] = pos.y / zoom;
    }

    if (editorHovered && ImGui::GetIO().MouseWheel != 0.0f) {
        float next = procZoom_ * ImPow(1.1f, ImGui::GetIO().MouseWheel);
        next = std::clamp(next, 0.4f, 1.8f);
        if (next != procZoom_) {
            const float ratio = next / procZoom_;
            ImVec2 pan = ImNodes::EditorContextGetPanning();
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            const float relX = mouse.x - canvasOrigin.x;
            const float relY = mouse.y - canvasOrigin.y;
            pan.x = relX - ratio * (relX - pan.x);
            pan.y = relY - ratio * (relY - pan.y);
            ImNodes::EditorContextResetPanning(pan);
            procZoom_ = next;
            procPositionsApplied_ = false;
        }
    }

    // New link: the pin id space encodes node and slot, so both ends resolve
    // without a lookup. procgraph::addLink does the validation (type match,
    // cycles) and replaces whatever fed the target input.
    int startPin = 0, endPin = 0;
    if (ImNodes::IsLinkCreated(&startPin, &endPin)) {
        auto isOut = [](int pin) { return (pin % 32) >= 8; };
        if (isOut(startPin) != isOut(endPin)) {
            const int outPin = isOut(startPin) ? startPin : endPin;
            const int inPin = isOut(startPin) ? endPin : startPin;
            const std::string err =
                procgraph::linkError(g, outPin / 32, outPin % 32 - 8, inPin / 32,
                                     inPin % 32);
            if (err.empty()) {
                procgraph::addLink(g, outPin / 32, outPin % 32 - 8, inPin / 32,
                                   inPin % 32);
                changed = true;
            } else {
                statusMessage_ = "Cannot connect: " + err;
            }
        }
    }

    // Delete / copy / paste, same conventions as the flow editor.
    const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
    if (focused && ImGui::IsKeyPressed(ImGuiKey_Delete)) {
        const int numLinks = ImNodes::NumSelectedLinks();
        if (numLinks > 0) {
            std::vector<int> ids(numLinks);
            ImNodes::GetSelectedLinks(ids.data());
            for (int id : ids)
                for (size_t i = 0; i < g.links.size(); ++i)
                    if (g.links[i].id == id) {
                        g.links.erase(g.links.begin() + i);
                        changed = true;
                        break;
                    }
        }
        const int numNodes = ImNodes::NumSelectedNodes();
        if (numNodes > 0) {
            std::vector<int> ids(numNodes);
            ImNodes::GetSelectedNodes(ids.data());
            for (int id : ids) {
                procgraph::removeNode(g, id);
                if (procPreviewNode_ == id) procPreviewNode_ = 0;
                if (procCurveNode_ == id) procCurveNode_ = 0;
                changed = true;
            }
            ImNodes::ClearNodeSelection();
        }
    }
    if (focused && !ImGui::GetIO().WantTextInput) {
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_C)) {
            const int numSel = ImNodes::NumSelectedNodes();
            if (numSel > 0) {
                std::vector<int> ids(numSel);
                ImNodes::GetSelectedNodes(ids.data());
                procClipboard_ = ProcGraph{};
                for (int id : ids)
                    if (const ProcNode* n = procgraph::node(g, id))
                        procClipboard_.nodes.push_back(*n);
                auto copied = [&](int id) {
                    for (const ProcNode& n : procClipboard_.nodes)
                        if (n.id == id) return true;
                    return false;
                };
                for (const ProcLink& l : g.links)
                    if (copied(l.fromNode) && copied(l.toNode))
                        procClipboard_.links.push_back(l);
                statusMessage_ = "Copied " +
                                 std::to_string(procClipboard_.nodes.size()) + " node(s)";
            }
        }
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_V) &&
            !procClipboard_.nodes.empty()) {
            std::vector<std::pair<int, int>> idMap;
            for (const ProcNode& src : procClipboard_.nodes) {
                ProcNode n = src;
                n.id = g.nextId++;
                n.pos[0] += 24.0f;
                n.pos[1] += 24.0f;
                idMap.push_back({src.id, n.id});
                g.nodes.push_back(n);
            }
            auto mapId = [&](int old) {
                for (const auto& pr : idMap)
                    if (pr.first == old) return pr.second;
                return -1;
            };
            for (const ProcLink& src : procClipboard_.links) {
                ProcLink l = src;
                l.id = g.nextId++;
                l.fromNode = mapId(src.fromNode);
                l.toNode = mapId(src.toNode);
                if (l.fromNode > 0 && l.toNode > 0) g.links.push_back(l);
            }
            procPositionsApplied_ = false;
            changed = true;
        }
    }

    // Right-click a node: preview it / bypass it. Right-click the canvas: add.
    // The target is remembered in procCtxNode_ and NOT in procDescNode_ - the
    // latter is the hover tracker above, which resets to -1 as soon as the
    // cursor sits on the popup instead of the node.
    int ctxNode = -1;
    if (editorHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        if (ImNodes::IsNodeHovered(&ctxNode)) {
            procCtxNode_ = ctxNode;
            ImGui::OpenPopup("##proc_node_ctx");
        } else {
            ImGui::OpenPopup("##proc_add_node");
        }
    }
    if (ImGui::BeginPopup("##proc_node_ctx")) {
        ProcNode* n = procgraph::node(g, procCtxNode_);
        if (!n) {
            ImGui::CloseCurrentPopup();
        } else {
            const ProcNodeType* t = procNodeType(n->type);
            ImGui::TextDisabled("%s", t ? t->title : n->type.c_str());
            ImGui::Separator();
            if (ImGui::MenuItem("Preview this node", nullptr,
                                procPreviewNode_ == n->id))
                procPreviewNode_ = procPreviewNode_ == n->id ? 0 : n->id;
            if (ImGui::MenuItem("Bypass", nullptr, n->bypass)) {
                n->bypass = !n->bypass;
                changed = true;
            }
            if (ImGui::MenuItem("Delete")) {
                if (procPreviewNode_ == n->id) procPreviewNode_ = 0;
                if (procCurveNode_ == n->id) procCurveNode_ = 0;
                procgraph::removeNode(g, n->id);
                procCtxNode_ = -1;
                changed = true;
            }
        }
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("##proc_add_node")) {
        const ImVec2 clickPos = ImGui::GetMousePosOnOpeningCurrentPopup();
        for (const char* cat :
             {"Sources", "Masks", "Filters", "Repeat", "Attributes", "Output"}) {
            if (!ImGui::BeginMenu(cat)) continue;
            for (const ProcNodeType& t : procNodeTypes()) {
                if (std::strcmp(t.category, cat) != 0) continue;
                if (ImGui::MenuItem(t.title)) {
                    const int id = procgraph::addNode(g, t.key, 0.0f, 0.0f);
                    if (id) {
                        ImNodes::SetNodeScreenSpacePos(id, clickPos);
                        // The model pool is the one thing a fresh node cannot
                        // guess - seed it with the first .obj so a new Pick
                        // Asset node is immediately valid.
                        if (std::string(t.key) == "PickAsset" && !models.empty()) {
                            ProcRow row;
                            row.s = "res/models/" + models.front();
                            row.v[0] = 100.0f;
                            row.v[1] = 0.9f;
                            row.v[2] = 1.1f;
                            procgraph::node(g, id)->rows.push_back(row);
                        }
                        changed = true;
                    }
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
                    ImGui::BeginTooltip();
                    procNodeDoc(t, scaled(360.0f));
                    ImGui::EndTooltip();
                }
            }
            ImGui::EndMenu();
        }
        ImGui::EndPopup();
    }

    if (changed) {
        pruneProcOverrides(g);
        commitChange();
    }
    ImGui::End();
}
