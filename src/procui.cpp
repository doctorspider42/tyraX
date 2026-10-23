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
#include "theme.hpp"
#include "toolkit.hpp"
#include "uiscript.hpp"

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

void App::addScatterVolume(bool commit) {
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
    // The id the window addresses this volume by is stamped by the commit, so a
    // caller that takes the commit over (the AI Assistant's add_object, which
    // owes its whole call ONE undo step) owns setting procVolumeId_ after it.
    if (commit) {
        commitChange();
        procVolumeId_ = project_.objects()[procVolume_].id;
    }
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
    std::vector<Viewport::ScatterPreview::AssetSettings> assetSettings;
    merged.type = ProcType::Points;
    procOwnFirst_ = procOwnCount_ = 0;  // no edited volume until one is found
    double ms = 0.0;
    int nodesRun = 0, candidates = 0;
    for (int idx : vols) {
        const SceneObject& vol = project_.objects()[idx];
        if (vol.procGraph.frozen) {
            if (idx == procVolume_) procBudget_ = procbake::Report{};
            continue;
        }
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
        const ProcNode* output = procgraph::outputNode(vol.procGraph);
        for (const std::string& asset : r.assets) {
            Viewport::ScatterPreview::AssetSettings settings;
            settings.drawDistance = output ? procgraph::num(*output, "draw") : 0.0f;
            const ProcRow* row = procgraph::poolRow(vol.procGraph, "PickAsset", asset);
            if (row && procRowHasLod(*row)) {
                settings.lod = row->lod;
                settings.lodDistance = row->v[3];
            }
            assetSettings.push_back(std::move(settings));
        }
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
        for (int idx : vols)
            if (project_.objects()[idx].procGraph.frozen)
                sp.frozenSources.push_back(project_.objects()[idx].id);
        sp.assets = merged.assets;
        sp.assetSettings = std::move(assetSettings);
        sp.instances = merged.instances;
        sp.prefabObjects = std::move(prefabObjs);
    }
    sp.mask = merged.mask;
    sp.curve = merged.curve;
    // Handles: the control points of the curve node being edited, plus the
    // selected instance when the override tool is on (one highlighted marker
    // is all the feedback either tool needs).
    if (procCurveNode_ != 0 && procVolume_ >= 0 &&
        procVolume_ < (int)project_.objects().size() &&
        !project_.objects()[procVolume_].procGraph.frozen) {
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
    // The opt-in GI pass (ProjectSettings::giAutoBake): stale scene caches are
    // re-baked so the build reads fresh light instead of silently shipping the
    // pre-GI fallback. BEFORE pre-lit, which gathers from the solved scene. The
    // cache is on disk, so the model is untouched - only the viewport has to
    // re-read it (the giBakerPoll rule).
    if (hasProject_ && project_.settings.giAutoBake && project_.settings.giEnabled &&
        !giBaker_.running()) {
        const gibake::StaleReport rep = gibake::bakeStale(project_, nullptr);
        if (rep.baked) {
            applyProjectToViewport();
            statusMessage_ = "Baked GI for " + std::to_string(rep.baked) +
                             " stale scene(s) before the build";
        }
        if (rep.failed)
            statusMessage_ = "GI bake failed for " + std::to_string(rep.failed) +
                             " scene(s) - they ship the pre-GI lighting";
    }
    // The opt-in pre-lit pass (ProjectSettings::prelitAutoBake): the same
    // "stale gets baked first" contract as the volumes above, for the same
    // reason - what runs on the console should agree with the scene. It is
    // synchronous like the procedural bake (one gibake solve per scene with
    // work, seconds), and it edits the model, so it must be the editor's own
    // commit rather than something the Runner does to its copy.
    if (hasProject_ && project_.settings.prelitAutoBake) {
        std::string lastLine;
        const litbake::StaleReport rep = litbake::bakeStale(
            project_, litBakeParams_, "",
            [&](const std::string& l) { lastLine = l; });
        if (rep.baked || rep.failed) {
            commitChange();
            prelitStatusKey_ = ~0ull;
            viewport_.invalidateAssets();
        }
        if (rep.failed)
            statusMessage_ = "Pre-lit bake failed: " + rep.firstError;
        else if (rep.baked)
            statusMessage_ = "Pre-lit " + std::to_string(rep.baked) +
                             " stale object(s) before the build";
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

// Pin colors per data type, so a glance says what can connect to what. From the
// theme's semantics, as the design's legend has them: points = accent (blue in
// Graphite), mask = special (pink), curve = ok (green).
static ImU32 procTypeColor(ProcType t) {
    const theme::Semantics& sem = theme::semantics();
    switch (t) {
        case ProcType::Points: return ImGui::GetColorU32(sem.accent);
        case ProcType::Mask: return ImGui::GetColorU32(sem.special);
        case ProcType::Curve: return ImGui::GetColorU32(sem.ok);
    }
    return ImGui::GetColorU32(sem.textDim);
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
                   "70/25/5 and 14/5/1 pick the same way. The second line is an "
                   "optional distant version: another model that replaces the "
                   "row's instances past its distance.";
        case ProcRowKind::Prefabs:
            return "Pool rows: a prefab (Tools > Prefabs), its weight, and the "
                   "scale range one point may draw from. Weights are relative - "
                   "34/26/26/14 is simply 'the first one a bit more often'. The "
                   "picker can also capture a scene object as a one-member "
                   "prefab, which is how a primitive gets scattered. The second "
                   "line is an optional distant version: one model that replaces "
                   "the whole prefab's merged geometry past its distance "
                   "(runtime volumes).";
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

namespace {

ImU32 pcol(const ImVec4& c, float a = 1.0f) {
    return ImGui::GetColorU32(ImVec4(c.x, c.y, c.z, c.w * a));
}

// "4,812" - counts on nodes and in the inspector are read, not parsed.
std::string thousands(long v) {
    std::string s = std::to_string(v < 0 ? -v : v);
    for (int i = (int)s.size() - 3; i > 0; i -= 3) s.insert((size_t)i, ",");
    return v < 0 ? "-" + s : s;
}

// One parameter's value as text, for the node's one-line summary.
std::string procParamText(const Project& proj, const ProcNode& n, const ProcParamDef& p) {
    char b[64];
    switch (p.kind) {
        case ProcParamKind::Float: std::snprintf(b, sizeof b, "%.3g", procgraph::num(n, p.key)); return b;
        case ProcParamKind::Int: return std::to_string(procgraph::inum(n, p.key));
        case ProcParamKind::Bool: return procgraph::flag(n, p.key) ? "on" : "off";
        case ProcParamKind::Enum: {
            int v = procgraph::inum(n, p.key), i = 0;
            std::string cur;
            for (const char* c = p.choices;; ++c) {
                if (*c == '|' || *c == 0) {
                    if (i == v) return cur;
                    cur.clear();
                    ++i;
                    if (*c == 0) break;
                } else {
                    cur += *c;
                }
            }
            return "?";
        }
        case ProcParamKind::ObjectName: {
            const std::string s = procgraph::str(n, p.key);
            return s.empty() ? (p.emptyLabel && *p.emptyLabel ? p.emptyLabel : "terrain") : s;
        }
        case ProcParamKind::Attr: {
            const std::string s = procgraph::str(n, p.key);
            return s.empty() ? std::string(procattr::kSlope) : s;
        }
        case ProcParamKind::TerrainLayer: {
            const int v = procgraph::inum(n, p.key);
            const auto& layers = proj.active().terrainLayers;
            if (v < 0) return "base";
            return v < (int)layers.size() ? layers[(size_t)v].name : "missing";
        }
        case ProcParamKind::Text: return procgraph::str(n, p.key);
    }
    return "";
}

// The node's recipe in one line: the pool's names, or its first parameters.
std::string procNodeSummary(const Project& proj, const ProcNode& n, const ProcNodeType& t) {
    std::string s;
    if (t.rows == ProcRowKind::Assets || t.rows == ProcRowKind::Prefabs) {
        for (const ProcRow& r : n.rows) {
            if (!s.empty()) s += " \xc2\xb7 ";
            std::string b = baseNameOf(r.s);
            const size_t dot = b.rfind('.');
            if (dot != std::string::npos && t.rows == ProcRowKind::Assets) b.resize(dot);
            s += b.empty() ? "?" : b;
        }
        return s.empty() ? "empty pool" : s;
    }
    if (t.rows == ProcRowKind::Points) return std::to_string(n.rows.size()) + " points";
    int shown = 0;
    for (const ProcParamDef& p : t.params) {
        if (shown == 2) break;
        if (!s.empty()) s += " \xc2\xb7 ";
        s += std::string(p.key) + " " + procParamText(proj, n, p);
        ++shown;
    }
    if (t.rows == ProcRowKind::Settings && !n.rows.empty())
        s += (s.empty() ? "" : " \xc2\xb7 ") + std::to_string(n.rows.size()) + " props";
    return s;
}

}  // namespace

// A "nice" drag/snap step for a span: 1, 2 or 5 times a power of ten, about
// 1/200 of it - so a range knob lands on 0.5 or 5, never on 0.45.
static float procNiceStep(float span) {
    const float raw = std::max(1e-4f, span / 200.0f);
    const float mag = std::pow(10.0f, std::floor(std::log10(raw)));
    const float f = raw / mag;
    return (f < 1.5f ? 1.0f : f < 3.5f ? 2.0f : f < 7.5f ? 5.0f : 10.0f) * mag;
}

// What the track of a min..max control spans for an attribute or a terrain
// source: the natural range where there is one, else one that contains the
// values with room for the falloff.
static void procRangeView(const char* unit, float lo, float hi, float fall, float* v0, float* v1) {
    if (std::strcmp(unit, "deg") == 0) {
        *v0 = 0.0f;
        *v1 = 90.0f;
    } else if (std::strcmp(unit, "unit") == 0) {
        *v0 = 0.0f;
        *v1 = 1.0f;
    } else if (std::strcmp(unit, "signed") == 0) {
        *v0 = -1.0f;
        *v1 = 1.0f;
    } else {
        const float span = std::max(1.0f, hi - lo);
        *v0 = std::min(0.0f, lo - std::max(fall, span * 0.25f));
        *v1 = hi + std::max(fall, span * 0.25f);
    }
    if (lo < *v0) *v0 = lo;
    if (hi > *v1) *v1 = hi;
}

// The unit a per-point attribute is measured in, for the range view and the
// field format.
static const char* procAttrUnit(const std::string& a) {
    if (a == procattr::kSlope) return "deg";
    if (a == procattr::kMask || a == procattr::kRandom || a == procattr::kCurveT) return "unit";
    if (a == procattr::kNormalY) return "signed";
    return "world";
}

// One sentence of a node's description: the inspector's summary line (the
// whole paragraph is behind the header's help button).
static std::string procFirstSentence(const char* s) {
    if (!s) return {};
    const char* e = std::strstr(s, ". ");
    return e ? std::string(s, e + 1) : std::string(s);
}

// A mask as a grid of grey cells - no texture upload for a thumbnail that
// changes on every drag.
static void procMaskThumb(const procgen::Mask& m, float w) {
    const float fs = ImGui::GetFontSize();
    const float aspect = m.sizeX > 0.0f ? m.sizeZ / m.sizeX : 1.0f;
    const float h = std::clamp(w * aspect, fs * 3.5f, fs * 8.0f);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(w, h));
    if (m.w <= 0 || m.h <= 0 || m.v.empty()) return;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const int cols = std::min(m.w, std::max(8, (int)(w / std::max(3.0f, fs * 0.28f))));
    const int rows = std::min(m.h, std::max(4, (int)(h / std::max(3.0f, fs * 0.28f))));
    const float cw = w / (float)cols, ch = h / (float)rows;
    for (int y = 0; y < rows; ++y) {
        // Row 0 of the mask is the volume's -Z edge; draw it at the bottom so
        // the thumbnail reads like the top-down viewport.
        const int my = std::min(m.h - 1, (int)(((float)(rows - 1 - y) + 0.5f) / (float)rows * (float)m.h));
        for (int x = 0; x < cols; ++x) {
            const int mx = std::min(m.w - 1, (int)(((float)x + 0.5f) / (float)cols * (float)m.w));
            const float v = std::clamp(m.v[(size_t)my * (size_t)m.w + (size_t)mx], 0.0f, 1.0f);
            const int g = (int)(12.0f + v * 220.0f);
            dl->AddRectFilled(ImVec2(p.x + cw * (float)x, p.y + ch * (float)y),
                              ImVec2(p.x + cw * (float)(x + 1) + 0.5f, p.y + ch * (float)(y + 1) + 0.5f),
                              IM_COL32(g, g, g + 4, 255));
        }
    }
    dl->AddRect(p, ImVec2(p.x + w, p.y + h), ImGui::GetColorU32(theme::semantics().border), 3.0f);
}

// The selected node's parameters and tables in the inspector. Every node type
// has a laid-out form below (docs/editor-theme.md, "Node inspector"): values
// read together share a row, min..max pairs are one range control, short
// enums are segments, switches are chips, guard rails sit in a collapsed
// Advanced row, and a field only shows while it means something. A parameter
// a layout does not place is still drawn after it, generically - a new
// registry entry can never go missing from the panel.
bool App::drawProcNodeParams(ProcNode& n, const ProcNodeType& t, const std::vector<std::string>& models) {
    bool changed = false;
    const float fs = ImGui::GetFontSize();
    const theme::Semantics& sem = theme::semantics();
    const SceneObject* volObj = procVolume_ >= 0 && procVolume_ < (int)project_.objects().size()
                                    ? &project_.objects()[(size_t)procVolume_]
                                    : nullptr;
    const procgen::Cache* cache = volObj && procCaches_.count(volObj->id) ? &procCaches_[volObj->id] : nullptr;

    std::vector<const char*> drawn;
    auto isDrawn = [&](const char* key) {
        for (const char* k : drawn)
            if (std::strcmp(k, key) == 0) return true;
        return false;
    };
    auto mark = [&](const char* key) {
        if (!isDrawn(key)) drawn.push_back(key);
    };
    auto def = [&](const char* key) -> const ProcParamDef* {
        for (const ProcParamDef& p : t.params)
            if (std::strcmp(p.key, key) == 0) return &p;
        return nullptr;
    };
    auto tipBox = [&](const char* tip) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(scaled(320.0f));
        ImGui::TextUnformatted(tip);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    };
    auto paramTip = [&](const ProcParamDef* p) {
        if (!p || !p->tip || !*p->tip) return;
        ui::RowHelp(p->tip);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) tipBox(p->tip);
    };
    auto setNum = [&](const ProcParamDef& p, float v) {
        n.nums[p.key] = std::clamp(v, p.lo, p.hi);
        changed = true;
    };
    auto hint = [&](const char* text) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", text);
        ImGui::PopTextWrapPos();
    };

    // The row's distant version (docs/procedural-generation.md, "Distant
    // version per row"): the model that replaces every instance of the row
    // past a distance, and that distance - one line of the pool card.
    auto poolLod = [&](ProcRow& row, float w) {
        bool hit = false;
        const float gap = ImGui::GetStyle().ItemSpacing.x;
        const float distW = std::max(40.0f, fs * 5.0f);
        ImGui::SetNextItemWidth(row.lod.empty() ? w : std::max(40.0f, w - distW - gap));
        const std::string label = row.lod.empty() ? "+ distant version" : "far: " + baseNameOf(row.lod);
        if (ImGui::BeginCombo("##lod", label.c_str())) {
            if (ImGui::Selectable("(none)##lodnone", row.lod.empty()) && !row.lod.empty()) {
                row.lod.clear();
                hit = true;
            }
            for (size_t mi = 0; mi < models.size(); ++mi) {
                const std::string rel = "res/models/" + models[mi];
                if (rel == row.s) continue;  // itself is not a distant version
                const std::string id = models[mi] + "##lod" + std::to_string(mi);
                if (ImGui::Selectable(id.c_str(), rel == row.lod) && rel != row.lod) {
                    row.lod = rel;
                    if (row.v[3] <= 0.0f) row.v[3] = 40.0f;
                    hit = true;
                }
            }
            if (models.empty()) ImGui::TextDisabled("no .obj files in res/models");
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("Distant version: past the distance this model replaces every\n"
                              "instance of the row - a prefab's merged members as a whole.\n"
                              "Collision stays the near one's. Prefab rows: runtime volumes.");
        if (!row.lod.empty()) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(distW);
            if (ImGui::DragFloat("##lodd", &row.v[3], 0.5f, 0.0f, 2000.0f,
                                 row.v[3] > 0.0f ? "past %.0f u" : "off")) {
                row.v[3] = std::max(0.0f, row.v[3]);
                hit = true;
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                ImGui::SetTooltip("Switch distance in world units, measured to the instance's\n"
                                  "origin (baked: to its chunk). 0 = never switch.");
        }
        return hit;
    };

    // --- the generic drawing of one parameter (pickers, and the fallback)
    auto generic = [&](const ProcParamDef& p) {
        mark(p.key);
        switch (p.kind) {
            case ProcParamKind::Float: {
                float v = procgraph::num(n, p.key);
                if (ui::DragFloat(p.label, &v, (p.hi - p.lo) / 400.0f, p.lo, p.hi)) setNum(p, v);
                break;
            }
            case ProcParamKind::Int: {
                int v = procgraph::inum(n, p.key);
                if (ui::DragInt(p.label, &v, 1.0f, (int)p.lo, (int)p.hi)) setNum(p, (float)v);
                break;
            }
            case ProcParamKind::Bool: {
                bool v = procgraph::flag(n, p.key);
                if (ui::Switch(p.label, &v)) setNum(p, v ? 1.0f : 0.0f);
                break;
            }
            case ProcParamKind::Enum: {
                // "a|b|c" -> a combo; the stored value is the index.
                std::vector<std::string> opts;
                std::string cur;
                for (const char* c = p.choices;; ++c) {
                    if (*c == '|' || *c == 0) {
                        opts.push_back(cur);
                        cur.clear();
                        if (*c == 0) break;
                    } else {
                        cur += *c;
                    }
                }
                int v = std::clamp(procgraph::inum(n, p.key), 0, (int)opts.size() - 1);
                const ImGuiID id = ImGui::GetID(p.label);
                const bool open = ui::BeginCombo(p.label, opts[(size_t)v].c_str());
                uiscript::nameItem(id, p.label);
                if (open) {
                    for (int i = 0; i < (int)opts.size(); ++i)
                        if (ImGui::Selectable(opts[(size_t)i].c_str(), i == v) && i != v) setNum(p, (float)i);
                    ImGui::EndCombo();
                }
                break;
            }
            case ProcParamKind::ObjectName: {
                std::string cur = procgraph::str(n, p.key);
                const char* none = p.emptyLabel && *p.emptyLabel ? p.emptyLabel : "(terrain)";
                const ImGuiID id = ImGui::GetID(p.label);
                const bool open = ui::BeginCombo(p.label, cur.empty() ? none : cur.c_str());
                uiscript::nameItem(id, p.label);
                if (open) {
                    if (ImGui::Selectable(none, cur.empty())) {
                        n.strs[p.key] = "";
                        changed = true;
                    }
                    // ##id per entry: nothing stops two scene objects from
                    // carrying the same name, and a Selectable's label is its id.
                    const std::vector<SceneObject>& objs = project_.objects();
                    for (size_t oi = 0; oi < objs.size(); ++oi) {
                        const SceneObject& o = objs[oi];
                        if (n.type == "FilterPlacement" && std::strcmp(p.key, "roadtarget") == 0 &&
                            o.type != PrimitiveType::Road)
                            continue;
                        if (o.type == PrimitiveType::Scatter) continue;
                        if (!o.procSource.empty()) continue;
                        const std::string oid = o.name + "##o" + std::to_string(oi);
                        if (ImGui::Selectable(oid.c_str(), cur == o.name) && cur != o.name) {
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
                const ImGuiID id = ImGui::GetID(p.label);
                const bool open = ui::BeginCombo(p.label, ("@" + cur).c_str());
                uiscript::nameItem(id, p.label);
                if (open) {
                    for (const char* a : {procattr::kSlope, procattr::kHeight, procattr::kMask, procattr::kSize,
                                          procattr::kRandom, procattr::kCurveT, procattr::kDist, procattr::kNormalY})
                        if (ImGui::Selectable(a, cur == a) && cur != a) {
                            n.strs[p.key] = a;
                            changed = true;
                        }
                    ImGui::EndCombo();
                }
                break;
            }
            case ProcParamKind::TerrainLayer: {
                // Painted terrain materials by name (-1 = the base material).
                // NOT clamped for display: a stored index past the end
                // evaluates as zero coverage.
                const std::vector<TerrainLayer>& layers = project_.active().terrainLayers;
                const int v = procgraph::inum(n, p.key);
                auto nameOf = [&](int i) {
                    if (i < 0) return std::string("Base material");
                    if (i >= (int)layers.size()) return "Layer " + std::to_string(i) + " (missing)";
                    return layers[(size_t)i].name;
                };
                const ImGuiID id = ImGui::GetID(p.label);
                const bool open = ui::BeginCombo(p.label, nameOf(v).c_str());
                uiscript::nameItem(id, p.label);
                if (open) {
                    if (ImGui::Selectable("Base material##pl-1", v < 0) && v != -1) setNum(p, -1.0f);
                    for (int i = 0; i < (int)layers.size(); ++i) {
                        const std::string lid = layers[(size_t)i].name + "##pl" + std::to_string(i);
                        if (ImGui::Selectable(lid.c_str(), i == v) && i != v) setNum(p, (float)i);
                    }
                    if (layers.empty()) ImGui::TextDisabled("no painted layers - add them in Terrain");
                    ImGui::EndCombo();
                }
                break;
            }
            case ProcParamKind::Text: {
                char buf[96] = {};
                std::snprintf(buf, sizeof(buf), "%s", procgraph::str(n, p.key).c_str());
                if (ui::InputText(p.label, buf, sizeof(buf))) {
                    n.strs[p.key] = buf;
                    changed = true;
                }
                break;
            }
        }
        paramTip(&p);
    };

    // --- the kit, bound to this node's parameters
    auto picker = [&](const char* key) {
        if (const ProcParamDef* p = def(key)) generic(*p);
    };
    auto field = [&](const char* key, const char* fmt) {
        const ProcParamDef* p = def(key);
        if (!p) return;
        mark(key);
        if (p->kind == ProcParamKind::Int) {
            int v = procgraph::inum(n, key);
            const float speed = std::max(1.0f, std::fabs((float)v) * 0.02f);
            if (ui::DragInt(p->label, &v, speed, (int)p->lo, (int)p->hi, fmt ? fmt : "%d",
                            ImGuiSliderFlags_AlwaysClamp))
                setNum(*p, (float)v);
        } else {
            float v = procgraph::num(n, key);
            if (ui::DragFloat(p->label, &v, (p->hi - p->lo) / 400.0f, p->lo, p->hi, fmt ? fmt : "%.3g",
                              ImGuiSliderFlags_AlwaysClamp))
                setNum(*p, v);
        }
        paramTip(p);
    };
    auto pct = [&](const char* key) {
        const ProcParamDef* p = def(key);
        if (!p) return;
        mark(key);
        float v = procgraph::num(n, key);
        if (ui::PercentSlider(p->label, &v)) setNum(*p, v);
        paramTip(p);
    };
    // Two or three values on one line, each field naming itself in its format.
    // pct = a 0..1 value shown as a percentage.
    struct PF {
        const char* key;
        const char* fmt;
        bool pct = false;
    };
    auto drags = [&](std::initializer_list<PF> fieldsIn) {
        std::vector<PF> fl(fieldsIn);
        std::vector<const ProcParamDef*> ps;
        std::vector<float> vals;
        std::vector<ui::KitDrag> kd;
        for (const PF& f : fl) {
            const ProcParamDef* p = def(f.key);
            if (!p) return;
            ps.push_back(p);
            mark(f.key);
            vals.push_back(procgraph::num(n, f.key) * (f.pct ? 100.0f : 1.0f));
        }
        for (size_t i = 0; i < fl.size(); ++i) {
            ui::KitDrag d;
            d.v = &vals[i];
            d.isInt = ps[i]->kind == ProcParamKind::Int;
            d.vmin = ps[i]->lo * (fl[i].pct ? 100.0f : 1.0f);
            d.vmax = ps[i]->hi * (fl[i].pct ? 100.0f : 1.0f);
            d.speed = fl[i].pct ? 0.5f
                      : d.isInt ? std::max(0.1f, std::fabs(vals[i]) * 0.02f)
                                : (ps[i]->hi - ps[i]->lo) / 400.0f;
            d.fmt = fl[i].fmt;
            d.name = ps[i]->label;
            kd.push_back(d);
        }
        const std::string id = std::string("##row_") + fl[0].key;
        if (ui::DragRow(id.c_str(), kd.data(), (int)kd.size()))
            for (size_t i = 0; i < fl.size(); ++i) {
                const float v = vals[i] / (fl[i].pct ? 100.0f : 1.0f);
                if (v != procgraph::num(n, fl[i].key)) setNum(*ps[i], v);
            }
        // The group is the last item: the tip of the field under the mouse.
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
            const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
            const int i = std::clamp((int)((ImGui::GetMousePos().x - a.x) / std::max(1.0f, b.x - a.x) *
                                           (float)ps.size()),
                                     0, (int)ps.size() - 1);
            if (ps[(size_t)i]->tip && *ps[(size_t)i]->tip) tipBox(ps[(size_t)i]->tip);
        }
    };
    auto vec3 = [&](const char* label, const char* kx, const char* ky, const char* kz, const char* fmt) {
        const ProcParamDef* p[3] = {def(kx), def(ky), def(kz)};
        if (!p[0] || !p[1] || !p[2]) return;
        float v[3];
        float lo = p[0]->lo, hi = p[0]->hi;
        for (int i = 0; i < 3; ++i) {
            mark(p[i]->key);
            v[i] = procgraph::num(n, p[i]->key);
            lo = std::min(lo, p[i]->lo);
            hi = std::max(hi, p[i]->hi);
        }
        if (ui::AxisDrag3(label, v, 0.1f, lo, hi, fmt))
            for (int i = 0; i < 3; ++i)
                if (v[i] != procgraph::num(n, p[i]->key)) setNum(*p[i], v[i]);
        paramTip(p[0]);
    };
    auto range = [&](const char* name, const char* loKey, const char* hiKey, const char* fallKey, float view0,
                     float view1, const char* fmt, bool gradient) {
        const ProcParamDef* pl = def(loKey);
        const ProcParamDef* ph = def(hiKey);
        const ProcParamDef* pf = fallKey ? def(fallKey) : nullptr;
        if (!pl || !ph) return;
        mark(loKey);
        mark(hiKey);
        if (pf) mark(fallKey);
        float lo = procgraph::num(n, loKey), hi = procgraph::num(n, hiKey);
        float fall = pf ? procgraph::num(n, fallKey) : 0.0f;
        ui::RangeSpec s;
        s.vmin = std::min(pl->lo, ph->lo);
        s.vmax = std::max(pl->hi, ph->hi);
        s.viewMin = view0;
        s.viewMax = view1;
        s.speed = procNiceStep(view1 - view0);
        s.fmt = fmt;
        s.gradient = gradient;
        s.name = name;
        if (pf) {
            s.falloff = &fall;
            s.falloffMax = pf->hi;
        }
        const std::string id = std::string("##range_") + loKey;
        if (ui::RangeDrag(id.c_str(), &lo, &hi, s)) {
            if (lo != procgraph::num(n, loKey)) setNum(*pl, lo);
            if (hi != procgraph::num(n, hiKey)) setNum(*ph, hi);
            if (pf && fall != procgraph::num(n, fallKey)) setNum(*pf, fall);
        }
        if (pl->tip && *pl->tip && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) tipBox(pl->tip);
    };
    auto seg = [&](const char* key, std::initializer_list<const char*> labels, bool caption) {
        const ProcParamDef* p = def(key);
        if (!p) return;
        mark(key);
        std::vector<const char*> L(labels);
        // An Enum stores the choice index; an Int shown as segments (Octaves
        // 1..6) stores the value, so segment 0 is its lower bound.
        const int base = p->kind == ProcParamKind::Int ? (int)p->lo : 0;
        int v = std::clamp(procgraph::inum(n, key) - base, 0, (int)L.size() - 1);
        const bool hit = caption ? ui::SegmentedRow(p->label, L.data(), (int)L.size(), &v)
                                 : ui::Segmented(p->label, L.data(), (int)L.size(), &v, -1.0f);
        if (hit) setNum(*p, (float)(v + base));
        paramTip(p);
    };
    // An Invert switch read as what it keeps.
    auto keep = [&](const char* key, const char* off, const char* on) {
        const ProcParamDef* p = def(key);
        if (!p) return;
        mark(key);
        int v = procgraph::flag(n, key) ? 1 : 0;
        const char* L[2] = {off, on};
        if (ui::Segmented(p->label, L, 2, &v, -1.0f)) setNum(*p, (float)v);
        paramTip(p);
    };
    auto chips = [&](std::initializer_list<const char*> keys) {
        bool first = true;
        for (const char* key : keys) {
            const ProcParamDef* p = def(key);
            if (!p) continue;
            mark(key);
            bool v = procgraph::flag(n, key);
            ui::ChipFlow(ui::ToggleChipWidth(p->label), first);
            first = false;
            if (ui::ToggleChip(p->label, &v)) setNum(*p, v ? 1.0f : 0.0f);
            if (p->tip && *p->tip && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) tipBox(p->tip);
        }
    };
    auto advanced = [&](const std::string& summary) { return ui::Disclosure("Advanced", summary.c_str()); };
    auto num = [&](const char* key) { return procgraph::num(n, key); };
    auto inum = [&](const char* key) { return procgraph::inum(n, key); };
    auto flag = [&](const char* key) { return procgraph::flag(n, key); };
    char sb[96];

    ImGui::PushID(n.id);
    {
        ui::InspectorRows rows(fs * 6.0f, fs * 8.0f);
        const std::string& ty = n.type;
        if (ty == "ScatterSurface") {
            picker("target");
            field("density", "%.2f / 100 u\xc2\xb2");
            field("lift", "%.1f u");
            std::snprintf(sb, sizeof sb, "cap %s pts", thousands(inum("max")).c_str());
            if (advanced(sb)) field("max", "%d points");
            else mark("max");
        } else if (ty == "ScatterGrid") {
            ui::KitSection("Cells");
            drags({{"spacing", "Spacing %.1f u"}, {"jitter", "Jitter %.0f%%", true}});
            field("lift", "%.1f u");
            ui::KitSection("Stack", inum("levels") <= 1 ? "1 level = flat" : nullptr);
            drags({{"levels", "Levels %.0f"}, {"levelstep", "%.1f u apart"}});
            chips({"snap"});
        } else if (ty == "BlocksFill") {
            ui::KitSection("Blocks");
            field("block", "%.2f u");
            drags({{"floor", "Floor %.0f"}, {"levels", "Max %.0f blocks"}});
            ui::KitSection("Terrain noise");
            field("scale", "%.0f u");
            drags({{"octaves", "Octaves %.0f"}, {"relief", "Relief %.2f"}});
            std::snprintf(sb, sizeof sb, "depth %d \xc2\xb7 base Y %.3g", inum("depth"), num("base"));
            if (advanced(sb)) {
                field("depth", "%d blocks");
                field("base", "%.2f");
            } else {
                mark("depth");
                mark("base");
            }
        } else if (ty == "ScatterVolume") {
            field("count", "%d points");
        } else if (ty == "ScatterCurve") {
            // Count 0 means "use Spacing": one stored field, two modes.
            const ProcParamDef* pc = def("count");
            int byCount = inum("count") > 0 ? 1 : 0;
            const char* modes[2] = {"Spacing", "Count"};
            ui::KitSection("Distribute by");
            if (ui::Segmented("Distribute by", modes, 2, &byCount, -1.0f) && pc)
                setNum(*pc, byCount ? std::max(1.0f, std::max(pc->lo, 10.0f)) : 0.0f);
            if (byCount) {
                drags({{"count", "%.0f points"}, {"jitter", "Jitter %.0f%%", true}});
                mark("spacing");
            } else {
                drags({{"spacing", "Every %.2f u"}, {"jitter", "Jitter %.0f%%", true}});
                mark("count");
            }
            ui::KitSection("Placement");
            field("offset", "%.1f u");
            chips({"align", "snap"});
        } else if (ty == "Point") {
            picker("target");
            vec3("Offset", "x", "y", "z", "%.2f");
            chips({"snap"});
        } else if (ty == "Curve") {
            chips({"closed"});
        } else if (ty == "NoiseMask") {
            if (cache)
                if (std::shared_ptr<const procgen::Mask> m = procgen::cachedMask(*cache, n.id))
                    procMaskThumb(*m, ImGui::GetContentRegionAvail().x);
            seg("kind", {"Perlin", "Ridged", "Cells", "Warped"}, false);
            field("scale", "%.0f u");
            seg("octaves", {"1", "2", "3", "4", "5", "6"}, true);
            ui::KitSection("Output range");
            range("Range", "low", "high", nullptr, 0.0f, 1.0f, "%.2f", true);
            chips({"invert"});
        } else if (ty == "TerrainMask") {
            if (cache)
                if (std::shared_ptr<const procgen::Mask> m = procgen::cachedMask(*cache, n.id))
                    procMaskThumb(*m, ImGui::GetContentRegionAvail().x);
            const int src = std::clamp(inum("source"), 0, 3);
            seg("source", {"Height", "Slope", "Curvature", "Material"}, false);
            if (src == 3) picker("layer");
            else mark("layer");
            static const char* const kUnit[4] = {"world", "deg", "signed", "unit"};
            static const char* const kFmt[4] = {"%.1f u", "%.0f\xc2\xb0", "%.2f", "%.2f"};
            static const char* const kBand[4] = {"Keep heights", "Keep slopes", "Keep curvature", "Keep coverage"};
            float v0, v1;
            procRangeView(kUnit[src], num("min"), num("max"), num("falloff"), &v0, &v1);
            std::snprintf(sb, sizeof sb, "falloff \xc2\xb1%.3g", num("falloff"));
            ui::KitSection(kBand[src], sb);
            range("Band", "min", "max", "falloff", v0, v1, kFmt[src], false);
            chips({"invert"});
        } else if (ty == "MaskCombine") {
            if (cache)
                if (std::shared_ptr<const procgen::Mask> m = procgen::cachedMask(*cache, n.id))
                    procMaskThumb(*m, ImGui::GetContentRegionAvail().x);
            seg("op", {"Mul", "Add", "Sub", "Min", "Max", "Blend"}, false);
            if (inum("op") == 5) pct("blend");
            else mark("blend");
        } else if (ty == "MaskRemap") {
            // The transfer curve, from the same formula remapMask applies.
            {
                const float w = ImGui::GetContentRegionAvail().x, h = fs * 5.0f;
                const ImVec2 p = ImGui::GetCursorScreenPos();
                ImGui::Dummy(ImVec2(w, h));
                ImDrawList* dl = ImGui::GetWindowDrawList();
                dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), ImGui::GetColorU32(sem.field), 4.0f);
                const float lo = num("low"), hi = num("high"), gam = std::max(0.05f, num("gamma"));
                const bool inv = flag("invert");
                std::vector<ImVec2> pts;
                for (int i = 0; i <= 64; ++i) {
                    const float x = (float)i / 64.0f;
                    float r = hi > lo ? std::clamp((x - lo) / (hi - lo), 0.0f, 1.0f) : (x >= lo ? 1.0f : 0.0f);
                    r = std::pow(r, gam);
                    if (inv) r = 1.0f - r;
                    pts.push_back(ImVec2(p.x + 4.0f + x * (w - 8.0f), p.y + h - 4.0f - r * (h - 8.0f)));
                }
                dl->AddPolyline(pts.data(), (int)pts.size(), ImGui::GetColorU32(sem.special), 0, 2.0f);
                dl->AddRect(p, ImVec2(p.x + w, p.y + h), ImGui::GetColorU32(sem.border), 4.0f);
            }
            ui::KitSection("Input range");
            range("Input", "low", "high", nullptr, 0.0f, 1.0f, "%.2f", true);
            field("gamma", "%.2f");
            chips({"invert"});
        } else if (ty == "FilterRange") {
            picker("attr");
            std::string attr = procgraph::str(n, "attr");
            if (attr.empty()) attr = procattr::kSlope;
            const char* unit = procAttrUnit(attr);
            float v0, v1;
            procRangeView(unit, num("min"), num("max"), num("falloff"), &v0, &v1);
            // The incoming values as a histogram: the range is set on data.
            std::vector<float> vals;
            const ProcGraph* g = volObj ? &volObj->procGraph : nullptr;
            const ProcLink* in = g ? procgraph::linkTo(*g, n.id, 0) : nullptr;
            if (cache && in && procgen::cachedAttribute(*cache, in->fromNode, attr, vals) && !vals.empty()) {
                if (std::strcmp(unit, "world") == 0) {
                    const auto mm = std::minmax_element(vals.begin(), vals.end());
                    v0 = std::min(v0, *mm.first);
                    v1 = std::max(v1, *mm.second);
                    v0 = std::max(v0, *mm.first - (*mm.second - *mm.first) * 0.1f);
                    v1 = std::min(v1, *mm.second + (*mm.second - *mm.first) * 0.1f);
                    v0 = std::min(v0, num("min"));
                    v1 = std::max(v1, num("max"));
                }
                constexpr int kBins = 32;
                int bins[kBins] = {};
                int peak = 1;
                for (float v : vals) {
                    const int b = std::clamp((int)((v - v0) / std::max(1e-6f, v1 - v0) * kBins), 0, kBins - 1);
                    peak = std::max(peak, ++bins[b]);
                }
                const float w = ImGui::GetContentRegionAvail().x, h = fs * 3.0f;
                const ImVec2 p = ImGui::GetCursorScreenPos();
                ImGui::Dummy(ImVec2(w, h));
                ImDrawList* dl = ImGui::GetWindowDrawList();
                const float bw = w / (float)kBins;
                const float lo = num("min"), hi = num("max");
                const bool inv = flag("invert");
                for (int b = 0; b < kBins; ++b) {
                    const float mid = v0 + ((float)b + 0.5f) / (float)kBins * (v1 - v0);
                    const bool kept = (mid >= lo && mid <= hi) != inv;
                    const float bh = std::max(1.0f, (float)bins[b] / (float)peak * (h - 2.0f));
                    dl->AddRectFilled(ImVec2(p.x + bw * (float)b + 1.0f, p.y + h - bh),
                                      ImVec2(p.x + bw * (float)(b + 1) - 1.0f, p.y + h),
                                      kept ? ImGui::GetColorU32(sem.accent) : ImGui::GetColorU32(sem.border), 1.5f);
                }
            }
            const char* fmt = std::strcmp(unit, "deg") == 0 ? "%.0f\xc2\xb0" : "%.3g";
            range("Keep", "min", "max", "falloff", v0, v1, fmt, false);
            keep("invert", "Keep inside", "Keep outside");
        } else if (ty == "FilterMask") {
            ui::KitSection("Mask fade", "black \xe2\x86\x92 white");
            range("Fade", "low", "high", nullptr, 0.0f, 1.0f, "%.2f", true);
            pct("strength");
            keep("invert", "Keep bright", "Keep dark");
        } else if (ty == "FilterDistance") {
            field("radius", "%.2f u");
            chips({"bysize"});
        } else if (ty == "FilterAvoid") {
            picker("target");
            seg("mode", {"Keep outside", "Keep inside"}, false);
            drags({{"radius", "Radius %.1f u"}, {"falloff", "Falloff %.1f u"}});
        } else if (ty == "FilterPlacement") {
            ui::KitSection("Roads");
            seg("roads", {"Ignore", "Avoid", "Only on"}, false);
            if (inum("roads") != 0) picker("roadtarget");
            else mark("roadtarget");
            std::snprintf(sb, sizeof sb, "clearance %.3g u", num("clearance"));
            ui::KitSection("Overlap", sb);
            chips({"collisions", "scene"});
            field("clearance", "%.1f u");
            bool mat = flag("material");
            if (ui::KitSectionSwitch("Restrict terrain material", &mat))
                if (const ProcParamDef* p = def("material")) setNum(*p, mat ? 1.0f : 0.0f);
            mark("material");
            if (mat) {
                picker("layer");
                pct("coverage");
            } else {
                mark("layer");
                mark("coverage");
            }
        } else if (ty == "Merge") {
            ImGui::TextDisabled("Nothing to set.");
        } else if (ty == "Limit") {
            field("max", "%d instances");
            const ProcGraph* g = volObj ? &volObj->procGraph : nullptr;
            const ProcLink* in = g ? procgraph::linkTo(*g, n.id, 0) : nullptr;
            const int incoming = cache && in ? procgen::cachedPointCount(*cache, in->fromNode) : -1;
            if (incoming >= 0) {
                const int cap = std::max(1, inum("max"));
                ui::Meter(std::min(1.0f, (float)incoming / (float)cap));
                std::snprintf(sb, sizeof sb, incoming > cap ? "%s of %s - the cap cuts %s" : "%s of %s - not cutting",
                              thousands(incoming).c_str(), thousands(cap).c_str(),
                              thousands(incoming - cap).c_str());
                ImGui::TextDisabled("%s", sb);
            }
        } else if (ty == "Vary") {
            ui::KitSection("Rotate");
            drags({{"yaw", "Yaw 0-%.0f\xc2\xb0"}, {"tilt", "Tilt \xc2\xb1%.1f\xc2\xb0"}});
            pct("align");
            ui::KitSection("Scale", "\xc3\x97 pool size");
            range("Scale", "scalemin", "scalemax", nullptr, 0.0f, std::max(2.0f, num("scalemax") * 1.25f),
                  "%.2f\xc3\x97", false);
            ui::KitSection("Position");
            field("jitter", "%.2f u");
        } else if (ty == "SetAttribute") {
            picker("attr");
            ui::KitSection("Value range", "mapped from 0..1");
            drags({{"min", "at 0: %.3g"}, {"max", "at 1: %.3g"}});
        } else if (ty == "Array") {
            field("count", "%d copies");
            vec3("Step", "dx", "dy", "dz", "%.2f");
            ui::KitSection("Each copy");
            drags({{"yaw", "Turn %+.0f\xc2\xb0"}, {"scale", "Scale \xc3\x97%.2f"}});
            chips({"local", "snap"});
        } else if (ty == "RadialArray") {
            drags({{"count", "%.0f copies"}, {"radius", "Radius %.1f u"}});
            drags({{"start", "Start %.0f\xc2\xb0"}, {"sweep", "Sweep %.0f\xc2\xb0"}});
            seg("axis", {"Y (flat)", "X", "Z"}, true);
            chips({"face", "snap"});
        } else if (ty == "Output") {
            const bool instanced = inum("mode") == 1;
            ui::KitSection("Draw as");
            seg("mode", {"Merged", "Instanced"}, false);
            hint(instanced ? "~60 B per instance; sun + ambient only, no probe GI, AO or lamps; Pick Asset "
                             "points only (prefabs and blocks still merge)."
                           : "Every copy baked into chunk meshes; full baked lighting; EE RAM grows with "
                             "every copy.");
            ui::KitSection("Budget");
            field("budget", "%d tris");
            ui::KitSection("Chunks");
            drags({{"cell", "Size %.0f u"}, {"draw", num("draw") > 0.0f ? "Draw %.0f u" : "Draw: always"}});
            if (!instanced) seg("detail", {"Full", "Half", "Quarter"}, true);
            else mark("detail");
            {
                bool first = true;
                if (const ProcParamDef* p = def("shadow")) {
                    mark("shadow");
                    bool v = flag("shadow");
                    ui::ChipFlow(ui::ToggleChipWidth(p->label), first);
                    first = false;
                    if (ui::ToggleChip(p->label, &v)) setNum(*p, v ? 1.0f : 0.0f);
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) tipBox(p->tip);
                }
                if (const ProcParamDef* p = def("collide")) {
                    mark("collide");
                    bool v = inum("collide") == 1;
                    ui::ChipFlow(ui::ToggleChipWidth("Box collision"), first);
                    if (ui::ToggleChip("Box collision", &v)) setNum(*p, v ? 1.0f : 0.0f);
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) tipBox(p->tip);
                }
            }
            // The console's working buffer exists only for a runtime volume.
            if (volObj && volObj->procGraph.runtime) {
                std::snprintf(sb, sizeof sb, "\xe2\x89\x88 %d KB RAM", (inum("maxinst") * 80 + 1023) / 1024);
                ui::KitSection("Console buffer", sb);
                field("maxinst", "%d points");
            } else {
                mark("maxinst");
            }
        }
        // Whatever the layout did not place, generically and last.
        for (const ProcParamDef& p : t.params)
            if (!isDrawn(p.key)) generic(p);
    }

    // --- variable-length tables: the asset pool, the prefab pool, the curve's
    // control points, the generated objects' settings.
    const bool assets = t.rows == ProcRowKind::Assets;
    if (assets || t.rows == ProcRowKind::Prefabs) {
        // A pool row is a card: what it places, its share of the points, the
        // weight and size range on one line, the distant version on the next.
        const ImVec4* const kPoolCols[6] = {&sem.accent, &sem.ok, &sem.special, &sem.warn,
                                                   &sem.danger, &sem.textDim};
        float total = 0.0f;
        for (const ProcRow& r : n.rows) total += std::max(0.0f, r.v[0]);
        std::snprintf(sb, sizeof sb, "%d %s", (int)n.rows.size(), assets ? "models" : "prefabs");
        ui::KitSection("Pool", sb);
        procRowsHelpTip(t.rows, scaled(320.0f));
        if (total > 0.0f) {
            // The shares as one bar, coloured like the cards' swatches.
            const float w = ImGui::GetContentRegionAvail().x, h = std::max(4.0f, fs * 0.4f);
            const ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(w, h));
            ImDrawList* dl = ImGui::GetWindowDrawList();
            float x = p.x;
            for (size_t r = 0; r < n.rows.size(); ++r) {
                const float sw = std::max(0.0f, n.rows[r].v[0]) / total * w;
                if (sw <= 0.0f) continue;
                dl->AddRectFilled(ImVec2(x, p.y), ImVec2(x + sw - 1.0f, p.y + h),
                                  ImGui::GetColorU32(*kPoolCols[r % 6]), h * 0.5f);
                x += sw;
            }
        }
        int removeRow = -1;
        for (size_t r = 0; r < n.rows.size(); ++r) {
            ProcRow& row = n.rows[r];
            ImGui::PushID((int)(assets ? r : 3000 + r));
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(sem.field.x, sem.field.y, sem.field.z, 0.55f));
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(fs * 0.5f, fs * 0.45f));
            ImGui::BeginChild("##card", ImVec2(0, 0),
                              ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY |
                                  ImGuiChildFlags_AlwaysUseWindowPadding);
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor();
            const float cardW = ImGui::GetContentRegionAvail().x;
            // Line 1: swatch, what it places, share, the row menu.
            {
                const float sw = std::max(4.0f, fs * 0.35f), menuW = ImGui::GetFrameHeight();
                const ImVec2 p = ImGui::GetCursorScreenPos();
                ImGui::GetWindowDrawList()->AddRectFilled(
                    ImVec2(p.x, p.y + fs * 0.2f), ImVec2(p.x + sw, p.y + ImGui::GetFrameHeight() - fs * 0.2f),
                    ImGui::GetColorU32(*kPoolCols[r % 6]), sw * 0.5f);
                ImGui::SetCursorScreenPos(ImVec2(p.x + sw + fs * 0.4f, p.y));
                char share[16];
                std::snprintf(share, sizeof share, "%.0f%%", total > 0.0f ? std::max(0.0f, row.v[0]) / total * 100.0f : 0.0f);
                const float shareW = ImGui::CalcTextSize("100%").x + fs * 0.5f;
                ImGui::SetNextItemWidth(std::max(fs * 4.0f, cardW - sw - fs * 0.4f - shareW - menuW - fs * 0.4f));
                if (assets) {
                    // The BASENAME, not the stored path: "res/models/" is the
                    // same on every row and eats the width the name needs.
                    const std::string label = row.s.empty() ? "(pick a model)" : baseNameOf(row.s);
                    if (ImGui::BeginCombo("##asset", label.c_str())) {
                        for (size_t mi = 0; mi < models.size(); ++mi) {
                            const std::string& m = models[mi];
                            const std::string rel = "res/models/" + m;
                            const std::string id = m + "##m" + std::to_string(mi);
                            if (ImGui::Selectable(id.c_str(), rel == row.s) && rel != row.s) {
                                row.s = rel;
                                changed = true;
                            }
                        }
                        if (models.empty()) ImGui::TextDisabled("no .obj files in res/models");
                        ImGui::Separator();
                        ImGui::TextDisabled("a primitive or anything else in the scene?\nuse a Pick Prefab node");
                        ImGui::EndCombo();
                    }
                    if (!row.s.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                        ImGui::SetTooltip("%s", row.s.c_str());
                } else {
                    const std::string label = row.s.empty() ? "(pick a prefab)" : row.s;
                    if (ImGui::BeginCombo("##prefab", label.c_str())) {
                        // Two lists that overlap by construction (capturing
                        // "box-1" makes a prefab "box-1" while the object
                        // stays): every entry carries an explicit ##id.
                        for (size_t pi = 0; pi < project_.prefabs.size(); ++pi) {
                            const Prefab& pf = project_.prefabs[pi];
                            const std::string id = pf.name + "##pf" + std::to_string(pi);
                            if (ImGui::Selectable(id.c_str(), pf.name == row.s) && pf.name != row.s) {
                                row.s = pf.name;
                                changed = true;
                            }
                        }
                        if (project_.prefabs.empty())
                            ImGui::TextDisabled("no prefabs yet - pick from the scene below, or Tools > Prefabs");
                        // Capturing ONE scene object makes an ordinary
                        // one-member prefab, so a scattered box travels the
                        // path a scattered room does.
                        ImGui::Separator();
                        ImGui::TextDisabled("Capture from the scene");
                        const std::vector<SceneObject>& objs = project_.objects();
                        bool anyCapturable = false;
                        for (size_t oi = 0; oi < objs.size(); ++oi) {
                            const SceneObject& o = objs[oi];
                            if (o.type == PrimitiveType::Scatter) continue;
                            if (!o.procSource.empty()) continue;
                            anyCapturable = true;
                            const std::string id =
                                o.name + "  (" + primitiveTypeName(o.type) + ")##cap" + std::to_string(oi);
                            if (!ImGui::Selectable(id.c_str())) continue;
                            Prefab np = prefab::capture(project_.active(), {(int)oi},
                                                        prefab::uniqueName(project_, o.name));
                            if (np.objects.empty()) continue;
                            project_.prefabs.push_back(std::move(np));
                            row.s = project_.prefabs.back().name;
                            changed = true;
                            statusMessage_ = "Captured \"" + o.name + "\" as the prefab \"" + row.s +
                                             "\" - the original object stays in the scene, delete it if "
                                             "it was only a template";
                        }
                        if (!anyCapturable) ImGui::TextDisabled("(nothing in the scene to capture)");
                        ImGui::EndCombo();
                    }
                }
                ImGui::SameLine(0.0f, fs * 0.4f);
                {
                    ui::FontScope mono(ui::monoFont());
                    const float tw = ImGui::CalcTextSize(share).x;
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + shareW - tw - fs * 0.2f);
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted(share);
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                    ImGui::SetTooltip("Share of the points this row receives (its weight / the pool's total)");
                ImGui::SameLine(cardW - menuW + ImGui::GetStyle().WindowPadding.x);
                if (ui::MoreButton("Row menu", menuW)) ImGui::OpenPopup("##rowmenu");
                if (ImGui::BeginPopup("##rowmenu")) {
                    if (ImGui::MenuItem("Remove from pool")) removeRow = (int)r;
                    ImGui::EndPopup();
                }
            }
            // Line 2: weight and size range.
            {
                ui::KitDrag d[3];
                d[0].v = &row.v[0];
                d[0].speed = 0.5f;
                d[0].vmin = 0.0f;
                d[0].vmax = 1000.0f;
                d[0].fmt = "Weight %.0f";
                d[0].name = "Weight";
                d[1].v = &row.v[1];
                d[1].speed = 0.01f;
                d[1].vmin = 0.05f;
                d[1].vmax = 20.0f;
                d[1].fmt = "%.2f\xc3\x97 min";
                d[1].name = "Scale min";
                d[2].v = &row.v[2];
                d[2].speed = 0.01f;
                d[2].vmin = 0.05f;
                d[2].vmax = 20.0f;
                d[2].fmt = "%.2f\xc3\x97 max";
                d[2].name = "Scale max";
                if (ui::DragRow("##poolnums", d, 3)) changed = true;
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
                    const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
                    procPoolColTip(std::clamp((int)((ImGui::GetMousePos().x - a.x) / std::max(1.0f, b.x - a.x) * 3.0f), 0, 2));
                }
            }
            // Line 3: the distant version.
            if (poolLod(row, cardW)) changed = true;
            ImGui::EndChild();
            ImGui::PopID();
        }
        if (removeRow >= 0) {
            n.rows.erase(n.rows.begin() + removeRow);
            changed = true;
        }
        if (ui::GhostButton(assets ? "+ Add model" : "+ Add prefab", -1.0f)) {
            ProcRow row;
            row.v[0] = 50.0f;
            row.v[1] = assets ? 0.9f : 1.0f;
            row.v[2] = assets ? 1.1f : 1.0f;
            if (assets && !models.empty()) row.s = "res/models/" + models.front();
            if (!assets && !project_.prefabs.empty()) row.s = project_.prefabs.front().name;
            n.rows.push_back(row);
            changed = true;
        }
    } else if (t.rows == ProcRowKind::Points) {
        const bool editing = procCurveNode_ == n.id;
        if (editing ? ui::GhostButton("Stop drawing in viewport", -1.0f)
                    : ui::PrimaryButton("Draw in viewport", -1.0f)) {
            procCurveNode_ = editing ? 0 : n.id;
            procCurvePoint_ = -1;
            procOverrideMode_ = false;
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("While this is on, clicking the terrain appends a control point;\n"
                              "pick a point below and click again to move it.");
        std::snprintf(sb, sizeof sb, "%d", (int)n.rows.size());
        ui::KitSection("Control points", sb);
        int removeRow = -1;
        for (size_t r = 0; r < n.rows.size(); ++r) {
            ImGui::PushID((int)(1000 + r));
            const bool sel = editing && procCurvePoint_ == (int)r;
            if (ImGui::RadioButton("##sel", sel)) procCurvePoint_ = sel ? -1 : (int)r;
            ImGui::SameLine();
            ImGui::SetNextItemWidth(std::max(fs * 8.0f, ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() -
                                                            ImGui::GetStyle().ItemSpacing.x));
            if (ImGui::DragFloat3("##p", n.rows[r].v, 0.1f, 0.0f, 0.0f, "%.1f")) changed = true;
            ImGui::SameLine();
            if (ui::MoreButton("Point menu", ImGui::GetFrameHeight())) ImGui::OpenPopup("##ptmenu");
            if (ImGui::BeginPopup("##ptmenu")) {
                if (ImGui::MenuItem("Remove point")) removeRow = (int)r;
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        if (removeRow >= 0) {
            n.rows.erase(n.rows.begin() + removeRow);
            if (procCurvePoint_ >= (int)n.rows.size()) procCurvePoint_ = -1;
            changed = true;
        }
    } else if (t.rows == ProcRowKind::Settings) {
        // Every property the bake can set on the generated objects, each one
        // ticked to override it - the list is short and fixed
        // (procObjectProps), so it is shown whole rather than built up row by
        // row. A ticked property is a row of the stored table; unticking
        // removes it, exactly as the old "x" did.
        // A wide caption column: the names are long, the values short.
        ui::InspectorRows rows(fs * 10.0f, fs * 14.0f);
        int removeRow = -1;
        for (const ProcObjProp& prop : procObjectProps()) {
            int at = -1;
            for (size_t r = 0; r < n.rows.size(); ++r)
                if (n.rows[r].s == prop.key) at = (int)r;
            bool on = at >= 0;
            const bool wasOn = on;
            const bool hit = ui::OverrideRow(prop.label, &on, "not set", [&] {
                if (at < 0) return false;  // ticked this frame: the row is added below
                ProcRow& row = n.rows[(size_t)at];
                if (prop.kind == ProcObjPropKind::Bool) {
                    bool v = row.v[0] >= 0.5f;
                    if (ui::Switch("##v", &v)) {
                        row.v[0] = v ? 1.0f : 0.0f;
                        return true;
                    }
                    return false;
                }
                if (ImGui::DragFloat("##v", &row.v[0], (prop.hi - prop.lo) / 400.0f, prop.lo, prop.hi,
                                     row.v[0] < 0.0f ? "project default" : row.v[0] == 0.0f ? "never" : "%.0f u",
                                     ImGuiSliderFlags_AlwaysClamp))
                    return true;
                return false;
            });
            if (prop.tip && *prop.tip && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) tipBox(prop.tip);
            if (!hit) continue;
            changed = true;
            if (on && !wasOn) {
                ProcRow row;
                row.s = prop.key;
                row.v[0] = prop.def;
                n.rows.push_back(row);
            } else if (!on && wasOn) {
                removeRow = at;
            }
        }
        if (removeRow >= 0) n.rows.erase(n.rows.begin() + removeRow);
    }
    ImGui::PopID();
    return changed;
}

// Auto-arrange: columns by the longest path from a source, rows by order in
// the graph - the recipe reads left to right. Positions are edits (saved).
void App::procAutoArrange(ProcGraph& g, float canvasW) {
    std::map<int, int> depth;
    for (const ProcNode& n : g.nodes) depth[n.id] = 0;
    for (size_t pass = 0; pass < g.nodes.size(); ++pass) {
        bool moved = false;
        for (const ProcLink& l : g.links)
            if (depth[l.toNode] < depth[l.fromNode] + 1 && depth[l.fromNode] + 1 < 64) {
                depth[l.toNode] = depth[l.fromNode] + 1;
                moved = true;
            }
        if (!moved) break;
    }
    // A long chain wraps into bands of as many columns as the canvas holds,
    // so a 13-step recipe reads as three rows instead of one strip 3000 px wide.
    const float fs = ImGui::GetFontSize();
    const float colW = fs * 14.5f, rowH = fs * 4.6f;
    const int perBand = std::max(3, (int)(canvasW / colW));
    std::map<int, int> rowsIn;
    int tallest = 1;
    for (const ProcNode& n : g.nodes)
        if (n.type != "ObjectSettings") tallest = std::max(tallest, ++rowsIn[depth[n.id]]);
    rowsIn.clear();
    const float bandH = (float)tallest * rowH + fs * 2.0f;
    for (ProcNode& n : g.nodes) {
        if (n.type == "ObjectSettings") continue;
        const int d = depth[n.id];
        const int r = rowsIn[d]++;
        n.pos[0] = (float)(d % perBand) * colW;
        n.pos[1] = (float)(d / perBand) * bandH + (float)r * rowH;
    }
    procPositionsApplied_ = false;
    procFramePending_ = 6;
}

void App::drawProceduralWindow() {
    if (!showProcedural_ || !hasProject_) return;
    ImGui::SetNextWindowSize(ImVec2(scaled(1200.0f), scaled(800.0f)), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Procedural", &showProcedural_)) {
        ImGui::End();
        return;
    }
    const theme::Semantics& sem = theme::semantics();
    const float fs = ImGui::GetFontSize();

    const std::vector<int> vols = procVolumes();
    if (vols.empty()) {
        if (ui::EmptyState(ui::Glyph::Sparkle, "No procedural volume in this scene",
                           "A volume's box is the region a graph fills - scattered over the "
                           "ground, along a curve, or repeated exactly - baked to static meshes "
                           "at build, or generated on the console.",
                           "Add a procedural volume"))
            addScatterVolume();
        ImGui::End();
        return;
    }
    {
        int byId = -1;
        for (int idx : vols)
            if (!procVolumeId_.empty() && project_.objects()[idx].id == procVolumeId_) byId = idx;
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

    // Per-volume state for the list, recomputed once per model edit (the
    // staleness hash walks the terrain and every object).
    if (!procStaleValid_ || procStaleSerial_ != modelEditSerial_) {
        procStaleSerial_ = modelEditSerial_;
        procStaleValid_ = true;
        procVolState_.clear();
        for (int idx : vols) {
            const SceneObject& v = project_.objects()[idx];
            const ProcGraph& vg = v.procGraph;
            const int st = vg.runtime ? 3
                           : vg.frozen ? 2
                           : vg.bakedHash != procgen::bakeHash(project_, project_.active(), v) ? 1
                                                                                             : 0;
            procVolState_[v.id] = st;
            if (st == 1) {
                if (!procStaleSince_.count(v.id)) procStaleSince_[v.id] = ImGui::GetTime();
            } else {
                procStaleSince_.erase(v.id);
            }
        }
        procStale_ = procVolState_[project_.objects()[procVolume_].id] == 1;
    }

    // ------------------------------------------------------------- volumes
    const float paneH = ImGui::GetContentRegionAvail().y;
    const float listW = std::max(fs * 12.0f, std::min(fs * 16.0f, ImGui::GetContentRegionAvail().x * 0.19f));
    const float inspW = std::max(fs * 20.0f, std::min(fs * 27.0f, ImGui::GetContentRegionAvail().x * 0.3f));
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    bool newVolume = false;
    if (ui::BeginCard("##procvols", ImVec2(listW, paneH))) {
        char cnt[16];
        std::snprintf(cnt, sizeof cnt, "%d", (int)vols.size());
        {
            ui::FontScope bold(ui::boldFont());
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Volumes");
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", cnt);
        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImGui::GetFrameHeight());
        if (ui::OverlayIconButton("New volume", ui::Glyph::Plus, ImGui::GetFrameHeight(), false, true,
                                  nullptr))
            newVolume = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("New procedural volume");
        ImGui::Spacing();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float footH = fs * 3.4f;
        ImGui::BeginChild("##vollist", ImVec2(0, -footH));
        for (int idx : vols) {
            const SceneObject& v = project_.objects()[idx];
            const int st = procVolState_.count(v.id) ? procVolState_[v.id] : 0;
            const bool sel = idx == procVolume_;
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float w = ImGui::GetContentRegionAvail().x, h = fs * 2.5f;
            char id[160];
            std::snprintf(id, sizeof id, "%s##vol%d", v.name.c_str(), idx);
            if (ImGui::InvisibleButton(id, ImVec2(std::max(1.0f, w), h)) && !sel) {
                procVolume_ = idx;
                procVolumeId_ = v.id;
                procPositionsApplied_ = false;
                procPreviewNode_ = 0;
                procCurveNode_ = 0;
                procSelInstance_ = 0;
                procSeedPreview_ = 0;  // the simulated seed was that volume's
                procInspectNode_ = 0;
            }
            uiscript::markLastItemChecked(sel);
            const bool hov = ImGui::IsItemHovered();
            if (sel) {
                dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), pcol(sem.accent, 0.16f), 4.0f);
                dl->AddRect(p, ImVec2(p.x + w, p.y + h), pcol(sem.accent, 0.3f), 4.0f);
            } else if (hov) {
                dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), pcol(sem.field), 4.0f);
            }
            static const char* kState[4] = {"baked", "stale", "frozen", "runtime"};
            const int kind = st == 0 ? 0 : st == 1 ? 1 : 3;
            const ImVec4 dotC = st == 0 ? sem.ok : st == 1 ? sem.warn : sem.accent;
            dl->AddCircleFilled(ImVec2(p.x + fs * 0.6f, p.y + fs * 0.85f), fs * 0.22f, pcol(dotC));
            const float bw = ImGui::CalcTextSize(kState[st]).x * 0.82f + fs;
            {
                const ImVec2 back = ImGui::GetCursorScreenPos();
                ImGui::SetCursorScreenPos(ImVec2(p.x + w - bw - fs * 0.3f, p.y + fs * 0.35f));
                ui::Badge(kState[st], kind);
                ImGui::SetCursorScreenPos(back);
            }
            dl->PushClipRect(p, ImVec2(p.x + w - bw - fs * 0.5f, p.y + h), true);
            dl->AddText(ImVec2(p.x + fs * 1.2f, p.y + fs * 0.3f), pcol(sem.text), v.name.c_str());
            char sub[64];
            std::snprintf(sub, sizeof sub, "%d nodes \xc2\xb7 seed %u", (int)v.procGraph.nodes.size(),
                          (unsigned)v.procGraph.seed);
            dl->AddText(ImGui::GetFont(), fs * 0.86f, ImVec2(p.x + fs * 1.2f, p.y + fs * 1.35f),
                        pcol(sem.textDim), sub);
            dl->PopClipRect();
        }
        ImGui::Dummy(ImVec2(0.0f, 0.0f));  // the badge moved the cursor: submit an item at it
        ImGui::EndChild();
        ImGui::Separator();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("Click a volume in the viewport to open its graph here.");
        ImGui::PopTextWrapPos();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("examples/procedural has a forest, an orchard, crystals, a colonnade,\n"
                              "a fence and a cairn - one graph each.");
    }
    ui::EndCard();
    if (newVolume) {
        addScatterVolume();
        ImGui::End();
        return;
    }

    SceneObject& vol = project_.objects()[procVolume_];
    ProcGraph& g = vol.procGraph;
    bool changed = false;
    const bool stale = procStale_;

    // The asset pickers need the model list, but listAssetFiles walks
    // res/models recursively - once a second is plenty.
    if (ImGui::GetTime() - procModelsAt_ > 1.0) {
        procModels_ = listAssetFiles("models", ".obj");
        procModelsAt_ = ImGui::GetTime();
    }
    const std::vector<std::string>& models = procModels_;

    // Frame the graph whenever a volume is opened: a canvas that opens on
    // empty grid with the nodes off its edge is the first thing that went
    // wrong here.
    bool volumeSwitched = false;
    if (procFrameVolume_ != procVolumeId_) {
        procFrameVolume_ = procVolumeId_;
        procFramePending_ = 6;
        volumeSwitched = true;
    }

    // ------------------------------------------------------------- graph
    ImGui::SameLine(0.0f, gap);
    const float graphW = ImGui::GetContentRegionAvail().x - inspW - gap;
    ui::BeginCard("##procgraph", ImVec2(graphW, paneH));
    if (procEditorCtx_) ImNodes::EditorContextSet((ImNodesEditorContext*)procEditorCtx_);
    // Node ids are per graph and small, so another volume's selection would
    // select (and preview) an unrelated node here.
    if (volumeSwitched) {
        ImNodes::ClearNodeSelection();
        ImNodes::ClearLinkSelection();
        procInspectNode_ = 0;
        procPreviewNode_ = 0;
    }

    const float zoom = procZoom_;
    const ImVec2 canvasOrigin = ImGui::GetCursorScreenPos();
    const ImVec2 canvasSize = ImGui::GetContentRegionAvail();
    // A zoomable canvas scales its FONT (PushFont), never the window font
    // scale - that no longer reaches BeginChild content (the node editor).
    ImGui::PushFont(nullptr, std::round(fs * zoom));
    const ImGuiStyle& gstyle = ImGui::GetStyle();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                        ImVec2(gstyle.ItemSpacing.x * zoom, gstyle.ItemSpacing.y * zoom));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        ImVec2(gstyle.FramePadding.x * zoom, gstyle.FramePadding.y * zoom));
    ImNodesStyle& nstyle = ImNodes::GetStyle();
    const ImNodesStyle savedStyle = nstyle;
    nstyle.GridSpacing *= zoom;
    nstyle.NodeCornerRounding *= zoom;
    nstyle.NodePadding = ImVec2(savedStyle.NodePadding.x * zoom, savedStyle.NodePadding.y * zoom);
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
            if (n.type != "ObjectSettings") ImNodes::SetNodeGridSpacePos(n.id, ImVec2(n.pos[0] * zoom, n.pos[1] * zoom));
    }

    const procgen::Cache* cache = procCaches_.count(vol.id) ? &procCaches_[vol.id] : nullptr;
    const float nodeW = fs * 12.0f * zoom;
    // While the viewport shows one node, only the graph up to it is evaluated:
    // the counts below it are from an earlier pass, so they are drawn dim.
    std::vector<int> stalePast;
    if (procPreviewNode_ != 0) {
        std::vector<int> todo{procPreviewNode_};
        while (!todo.empty()) {
            const int at = todo.back();
            todo.pop_back();
            for (const ProcLink& l : g.links)
                if (l.fromNode == at &&
                    std::find(stalePast.begin(), stalePast.end(), l.toNode) == stalePast.end()) {
                    stalePast.push_back(l.toNode);
                    todo.push_back(l.toNode);
                }
        }
    }
    for (ProcNode& n : g.nodes) {
        if (n.type == "ObjectSettings") continue;
        const ProcNodeType* t = procNodeType(n.type);
        if (!t) continue;
        // Compact cards: the title (with the output type's colour as a mark),
        // a one-line summary of the recipe, and the points leaving the node.
        const bool inspected = n.id == procInspectNode_;
        const bool previewed = n.id == procPreviewNode_ && procPreviewNode_ != 0;
        ImNodes::PushColorStyle(ImNodesCol_TitleBar, pcol(sem.surface));
        ImNodes::PushColorStyle(ImNodesCol_TitleBarHovered, pcol(sem.surface));
        ImNodes::PushColorStyle(ImNodesCol_TitleBarSelected, pcol(sem.surface));
        ImNodes::PushColorStyle(ImNodesCol_NodeOutline,
                                previewed ? pcol(sem.warn) : inspected ? pcol(sem.accent) : pcol(sem.border));
        ImNodes::BeginNode(n.id);
        ImNodes::BeginNodeTitleBar();
        {
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const ImU32 mark = t->outs.empty() ? pcol(sem.textDim) : procTypeColor(t->outs.front().type);
            ImGui::GetWindowDrawList()->AddRectFilled(
                ImVec2(p.x, p.y + 1.0f), ImVec2(p.x + 3.0f * zoom, p.y + ImGui::GetFontSize() - 1.0f), mark,
                1.0f);
            ImGui::SetCursorScreenPos(ImVec2(p.x + 7.0f * zoom, p.y));
            ImGui::PushStyleColor(ImGuiCol_Text, n.bypass ? sem.textDim : sem.text);
            ImGui::TextUnformatted(t->title);
            const std::string nodeLabel = std::string("Node: ") + t->title;
#ifdef IMGUI_ENABLE_TEST_ENGINE
            if (GImGui->TestEngineHookItems) {
                const ImGuiID id = ImGui::GetID(nodeLabel.c_str());
                ImGuiTestEngineHook_ItemAdd(GImGui, id, GImGui->LastItemData.Rect, nullptr);
                ImGuiTestEngineHook_ItemInfo(GImGui, id, nodeLabel.c_str(), 0);
            }
#endif
            ImGui::PopStyleColor();
            if (n.bypass) {
                ImGui::SameLine();
                ImGui::TextDisabled("(bypassed)");
            }
            ImGui::SameLine(0.0f, 0.0f);
            ImGui::Dummy(ImVec2(std::max(0.0f, p.x + nodeW - ImGui::GetCursorScreenPos().x), 1.0f));
        }
        ImNodes::EndNodeTitleBar();

        const std::string summary = procNodeSummary(project_, n, *t);
        const int count = cache ? procgen::cachedPointCount(*cache, n.id) : -1;
        const std::string countText = count >= 0 ? thousands(count) : "";
        const float countW = countText.empty() ? 0.0f : ImGui::CalcTextSize(countText.c_str()).x;
        auto summaryLine = [&](float w) {
            const ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(w, ImGui::GetFontSize()));
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->PushClipRect(p, ImVec2(p.x + w, p.y + ImGui::GetFontSize() + 2.0f), true);
            dl->AddText(p, pcol(sem.textDim), summary.c_str());
            dl->PopClipRect();
        };
        const float sumW = nodeW - countW - 8.0f * zoom;
        // Row 0: the summary rides the first input (its pin at the left),
        // the count the first output (pin at the right).
        if (!t->ins.empty()) {
            ImNodes::PushColorStyle(ImNodesCol_Pin, procTypeColor(t->ins[0].type));
            ImNodes::BeginInputAttribute(procInPin(n.id, 0), t->ins[0].type == ProcType::Points
                                                                  ? ImNodesPinShape_CircleFilled
                                                                  : ImNodesPinShape_QuadFilled);
            summaryLine(t->outs.empty() ? nodeW : sumW);
            ImNodes::EndInputAttribute();
            ImNodes::PopColorStyle();
        } else {
            summaryLine(t->outs.empty() ? nodeW : sumW);
        }
        if (!t->outs.empty()) {
            ImGui::SameLine(0.0f, 0.0f);
            ImNodes::PushColorStyle(ImNodesCol_Pin, procTypeColor(t->outs[0].type));
            ImNodes::BeginOutputAttribute(procOutPin(n.id, 0), t->outs[0].type == ProcType::Points
                                                                    ? ImNodesPinShape_CircleFilled
                                                                    : ImNodesPinShape_QuadFilled);
            const ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(nodeW - sumW, ImGui::GetFontSize()));
            if (!countText.empty())
                ImGui::GetWindowDrawList()->AddText(ImVec2(p.x + nodeW - sumW - countW, p.y),
                                                    std::find(stalePast.begin(), stalePast.end(), n.id) != stalePast.end()
                                                        ? pcol(sem.textDim, 0.6f)
                                                        : pcol(inspected ? sem.warn : sem.text),
                                                    countText.c_str());
            ImNodes::EndOutputAttribute();
            ImNodes::PopColorStyle();
        }
        // Further inputs and outputs: one small labelled row each.
        for (size_t i = 1; i < t->ins.size(); ++i) {
            ImNodes::PushColorStyle(ImNodesCol_Pin, procTypeColor(t->ins[i].type));
            ImNodes::BeginInputAttribute(procInPin(n.id, (int)i), t->ins[i].type == ProcType::Points
                                                                       ? ImNodesPinShape_CircleFilled
                                                                       : ImNodesPinShape_QuadFilled);
            const bool linked = procgraph::linkTo(g, n.id, (int)i) != nullptr;
            ImGui::PushStyleColor(ImGuiCol_Text, linked || t->ins[i].optional ? sem.textDim : sem.danger);
            ImGui::TextUnformatted(t->ins[i].label);
            ImGui::PopStyleColor();
            ImNodes::EndInputAttribute();
            ImNodes::PopColorStyle();
        }
        for (size_t i = 1; i < t->outs.size(); ++i) {
            ImNodes::PushColorStyle(ImNodesCol_Pin, procTypeColor(t->outs[i].type));
            ImNodes::BeginOutputAttribute(procOutPin(n.id, (int)i), t->outs[i].type == ProcType::Points
                                                                         ? ImNodesPinShape_CircleFilled
                                                                         : ImNodesPinShape_QuadFilled);
            const float lw = ImGui::CalcTextSize(t->outs[i].label).x;
            ImGui::Dummy(ImVec2(nodeW - lw, 1.0f));
            ImGui::SameLine(0.0f, 0.0f);
            ImGui::TextDisabled("%s", t->outs[i].label);
            ImNodes::EndOutputAttribute();
            ImNodes::PopColorStyle();
        }
        // A required input left open is the one in-node warning worth keeping.
        if (!t->ins.empty() && !t->ins[0].optional && !procgraph::linkTo(g, n.id, 0)) {
            ImGui::PushStyleColor(ImGuiCol_Text, sem.danger);
            ImGui::Text("needs %s", t->ins[0].label);
            ImGui::PopStyleColor();
        }
        ImNodes::EndNode();
        ImNodes::PopColorStyle();
        ImNodes::PopColorStyle();
        ImNodes::PopColorStyle();
        ImNodes::PopColorStyle();
    }

    for (const ProcLink& l : g.links) {
        const ProcNode* from = procgraph::node(g, l.fromNode);
        const ProcNodeType* ft = from ? procNodeType(from->type) : nullptr;
        const ProcType ty =
            (ft && l.fromPin < (int)ft->outs.size()) ? ft->outs[l.fromPin].type : ProcType::Points;
        ImNodes::PushColorStyle(ImNodesCol_Link, procTypeColor(ty));
        ImNodes::Link(l.id, procOutPin(l.fromNode, l.fromPin), procInPin(l.toNode, l.toPin));
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

    // Frame all: fit the zoom (never above 1) then centre the nodes' box.
    if (procFramePending_ > 0 && std::any_of(g.nodes.begin(), g.nodes.end(),
        [](const ProcNode& n) { return n.type != "ObjectSettings"; })) {
        float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
        for (const ProcNode& n : g.nodes) {
            if (n.type == "ObjectSettings") continue;
            const ImVec2 gp = ImNodes::GetNodeGridSpacePos(n.id);
            const ImVec2 d = ImNodes::GetNodeDimensions(n.id);
            x0 = std::min(x0, gp.x), y0 = std::min(y0, gp.y);
            x1 = std::max(x1, gp.x + d.x), y1 = std::max(y1, gp.y + d.y);
        }
        const float bw = std::max(1.0f, x1 - x0), bh = std::max(1.0f, y1 - y0);
        const float margin = fs * 3.0f;
        const float fit = std::clamp(zoom * std::min((canvasSize.x - margin * 2.0f) / bw,
                                                     (canvasSize.y - margin * 3.0f) / bh),
                                     0.4f, 1.0f);
        if (std::fabs(fit - zoom) > 0.04f && procFramePending_ > 1) {
            procZoom_ = fit;
            procPositionsApplied_ = false;
            --procFramePending_;
        } else {
            // Centred when it fits; when it cannot (zoom stops at 0.4), the
            // START of the recipe - its top-left - rather than its middle.
            const float px = bw + margin * 2.0f <= canvasSize.x ? canvasSize.x * 0.5f - (x0 + x1) * 0.5f
                                                                : margin - x0;
            const float py = bh + margin * 3.0f <= canvasSize.y ? canvasSize.y * 0.5f - (y0 + y1) * 0.5f
                                                                : margin * 2.0f - y0;
            ImNodes::EditorContextResetPanning(ImVec2(px, py));
            procFramePending_ = 0;
        }
    }

    nstyle = savedStyle;
    ImGui::PopStyleVar(2);
    ImGui::PopFont();

    // Selection drives the inspector, and the viewport preview follows it.
    {
        const int numSel = ImNodes::NumSelectedNodes();
        if (numSel == 1) {
            int id = 0;
            ImNodes::GetSelectedNodes(&id);
            if (id != procInspectNode_) {
                procInspectNode_ = id;
                const ProcNode* sn = procgraph::node(g, id);
                const ProcNodeType* st = sn ? procNodeType(sn->type) : nullptr;
                const bool isOutput = st && std::strcmp(st->category, "Output") == 0;
                if (!g.frozen) procPreviewNode_ = isOutput ? 0 : id;
            }
        }
        if (procInspectNode_ && !procgraph::node(g, procInspectNode_)) procInspectNode_ = 0;
    }

    for (ProcNode& n : g.nodes) {
        if (n.type == "ObjectSettings") continue;
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

    // --- canvas overlays: tools, what the viewport shows, the legend -------
    // Each is its own small child window: the node editor is a child too, and
    // only a LATER sibling draws (and takes clicks) above it.
    bool openAdd = false;
    {
        const float pad = ui::OverlayPad(), rowH = ui::OverlayRowHeight();
        const float m = fs * 0.6f;
        const ImGuiWindowFlags of = ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar |
                                    ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings;
        auto beginOverlay = [&](const char* id, ImVec2 a, ImVec2 size) {
            ImGui::SetCursorScreenPos(a);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::BeginChild(id, size, ImGuiChildFlags_None, of);
            ImGui::PopStyleVar();
            ui::OverlayPanel(ImGui::GetWindowDrawList(), a, ImVec2(a.x + size.x, a.y + size.y));
        };
        const char* addText = "Add node  Tab";
        // The design's "open the Procedural layout": the docked desk (graph under
        // the viewport, Properties beside it) is one click away from the tool
        // that wants it, shown only while another layout is active.
        int procLayout = -1;
        for (int i = 0; i < (int)project_.windowLayouts.size(); ++i)
            if (project_.windowLayouts[i].recipe == (int)LayoutRecipe::Procedural) {
                procLayout = i;
                break;
            }
        if (procLayout == project_.activeLayout) procLayout = -1;
        const float w0 = ui::OverlayTextWidth(addText), w1 = ui::OverlayTextWidth("Frame all  F"),
                    w2 = ui::OverlayTextWidth("Auto-arrange"),
                    w3 = procLayout >= 0 ? ui::OverlayTextWidth("Procedural layout") : 0.0f;
        const ImVec2 a(canvasOrigin.x + m, canvasOrigin.y + m);
        beginOverlay("##proctools", a, ImVec2(w0 + w1 + w2 + w3 + pad * 2.0f, rowH + pad * 2.0f));
        ImGui::SetCursorScreenPos(ImVec2(a.x + pad, a.y + pad));
        if (ui::OverlayTextButton("Add node", addText, false)) openAdd = true;
        ImGui::SetCursorScreenPos(ImVec2(a.x + pad + w0, a.y + pad));
        if (ui::OverlayTextButton("Frame all", "Frame all  F", false)) procFramePending_ = 6;
        ImGui::SetCursorScreenPos(ImVec2(a.x + pad + w0 + w1, a.y + pad));
        if (ui::OverlayTextButton("Auto-arrange", "Auto-arrange", false, !g.nodes.empty())) {
            procAutoArrange(g, canvasSize.x * 0.95f);
            changed = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Lay the graph out left to right by how far each node is from a source.");
        if (procLayout >= 0) {
            ImGui::SetCursorScreenPos(ImVec2(a.x + pad + w0 + w1 + w2, a.y + pad));
            if (ui::OverlayTextButton("Procedural layout", "Procedural layout", false))
                switchLayout(procLayout);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Switch to the Procedural layout: this window docked under the viewport,\n"
                                  "Properties beside it - so the graph and the world it fills are both in view.");
        }
        ImGui::EndChild();

        if (procPreviewNode_ != 0) {
            const ProcNode* pn = procgraph::node(g, procPreviewNode_);
            const ProcNodeType* pt = pn ? procNodeType(pn->type) : nullptr;
            char txt[160];
            std::snprintf(txt, sizeof txt, "Viewport shows %s", pt ? pt->title : "?");
            const float tw = ImGui::CalcTextSize(txt).x + fs * 1.2f;
            const float bw = ui::OverlayTextWidth("Show final output");
            // Top right when the tools leave room, else under them.
            const float cardW = tw + bw + pad * 2.0f;
            float x1 = canvasOrigin.x + canvasSize.x - m;
            ImVec2 b(x1 - cardW, canvasOrigin.y + m);
            if (b.x <= a.x + w0 + w1 + w2 + w3 + pad * 3.0f) {
                b = ImVec2(a.x, a.y + rowH + pad * 2.0f + m * 0.6f);
                x1 = b.x + cardW;
            }
            {
                beginOverlay("##procshows", b, ImVec2(x1 - b.x, rowH + pad * 2.0f));
                ImDrawList* dl = ImGui::GetWindowDrawList();
                dl->AddRectFilled(ImVec2(b.x + pad + fs * 0.1f, b.y + pad + rowH * 0.5f - fs * 0.22f),
                                  ImVec2(b.x + pad + fs * 0.54f, b.y + pad + rowH * 0.5f + fs * 0.22f),
                                  pcol(sem.warn), 2.0f);
                dl->AddText(ImVec2(b.x + pad + fs * 0.9f, b.y + pad + (rowH - fs) * 0.5f), pcol(sem.text), txt);
                ImGui::SetCursorScreenPos(ImVec2(x1 - pad - bw, b.y + pad));
                if (ui::OverlayTextButton("Show final output", "Show final output", false))
                    procPreviewNode_ = 0;
                ImGui::EndChild();
            }
        }

        // Legend, bottom-left: pin colours and what the numbers mean.
        {
            const char* items[3] = {"points", "mask", "curve"};
            const ProcType types[3] = {ProcType::Points, ProcType::Mask, ProcType::Curve};
            const char* tail = "numbers = points leaving the node";
            float w = pad * 2.0f;
            for (const char* it : items) w += fs * 1.6f + ImGui::CalcTextSize(it).x;
            w += ImGui::CalcTextSize(tail).x;
            const ImVec2 c(canvasOrigin.x + m, canvasOrigin.y + canvasSize.y - m - rowH - pad * 2.0f);
            if (w < canvasSize.x * 0.8f) {
                beginOverlay("##proclegend", c, ImVec2(w, rowH + pad * 2.0f));
                ImDrawList* dl = ImGui::GetWindowDrawList();
                float x = c.x + pad;
                const float y = c.y + pad + (rowH - fs) * 0.5f;
                for (int i = 0; i < 3; ++i) {
                    dl->AddLine(ImVec2(x, y + fs * 0.55f), ImVec2(x + fs * 0.9f, y + fs * 0.55f),
                                procTypeColor(types[i]), 2.0f);
                    x += fs * 1.2f;
                    dl->AddText(ImVec2(x, y), pcol(sem.text), items[i]);
                    x += ImGui::CalcTextSize(items[i]).x + fs * 0.4f;
                }
                dl->AddText(ImVec2(x, y), pcol(sem.textDim), tail);
                ImGui::EndChild();
            }
        }
    }
    const bool canvasKeys = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) &&
                            !ImGui::GetIO().WantTextInput;
    if (canvasKeys && editorHovered && ImGui::IsKeyPressed(ImGuiKey_Tab, false)) openAdd = true;
    if (canvasKeys && editorHovered && ImGui::IsKeyPressed(ImGuiKey_F, false)) procFramePending_ = 6;

    // New link: the pin id space encodes node and slot, so both ends resolve
    // without a lookup. procgraph::addLink validates (type, cycles) and
    // replaces whatever fed the target input.
    int startPin = 0, endPin = 0;
    if (ImNodes::IsLinkCreated(&startPin, &endPin)) {
        auto isOut = [](int pin) { return (pin % 32) >= 8; };
        if (isOut(startPin) != isOut(endPin)) {
            const int outPin = isOut(startPin) ? startPin : endPin;
            const int inPin = isOut(startPin) ? endPin : startPin;
            const std::string err =
                procgraph::linkError(g, outPin / 32, outPin % 32 - 8, inPin / 32, inPin % 32);
            if (err.empty()) {
                procgraph::addLink(g, outPin / 32, outPin % 32 - 8, inPin / 32, inPin % 32);
                changed = true;
            } else {
                statusMessage_ = "Cannot connect: " + err;
            }
        }
    }

    // Delete / copy / paste, same conventions as the flow editor.
    const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
    if (focused && editorHovered && ImGui::IsKeyPressed(ImGuiKey_Delete)) {
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
                if (procInspectNode_ == id) procInspectNode_ = 0;
                changed = true;
            }
            ImNodes::ClearNodeSelection();
        }
    }
    if (focused && editorHovered && !ImGui::GetIO().WantTextInput) {
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_C)) {
            const int numSel = ImNodes::NumSelectedNodes();
            if (numSel > 0) {
                std::vector<int> ids(numSel);
                ImNodes::GetSelectedNodes(ids.data());
                procClipboard_ = ProcGraph{};
                for (int id : ids)
                    if (const ProcNode* n = procgraph::node(g, id)) procClipboard_.nodes.push_back(*n);
                auto copied = [&](int id) {
                    for (const ProcNode& n : procClipboard_.nodes)
                        if (n.id == id) return true;
                    return false;
                };
                for (const ProcLink& l : g.links)
                    if (copied(l.fromNode) && copied(l.toNode)) procClipboard_.links.push_back(l);
                statusMessage_ = "Copied " + std::to_string(procClipboard_.nodes.size()) + " node(s)";
            }
        }
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_V) && !procClipboard_.nodes.empty()) {
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

    // Right-click a node: preview / bypass / delete. Right-click the canvas
    // (or Tab, or Add node): add. The target is remembered in procCtxNode_,
    // NOT in procDescNode_ (the hover tracker resets on the popup).
    int ctxNode = -1;
    if (editorHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        if (ImNodes::IsNodeHovered(&ctxNode)) {
            procCtxNode_ = ctxNode;
            ImGui::OpenPopup("##proc_node_ctx");
        } else {
            openAdd = true;
        }
    }
    if (openAdd) {
        procAddAt_ = ImGui::IsMousePosValid() && editorHovered
                         ? ImGui::GetMousePos()
                         : ImVec2(canvasOrigin.x + canvasSize.x * 0.4f, canvasOrigin.y + canvasSize.y * 0.4f);
        ImGui::OpenPopup("##proc_add_node");
    }
    if (ImGui::BeginPopup("##proc_node_ctx")) {
        ProcNode* n = procgraph::node(g, procCtxNode_);
        if (!n) {
            ImGui::CloseCurrentPopup();
        } else {
            const ProcNodeType* t = procNodeType(n->type);
            ImGui::TextDisabled("%s", t ? t->title : n->type.c_str());
            ImGui::Separator();
            if (ImGui::MenuItem("Preview this node", nullptr, procPreviewNode_ == n->id, !g.frozen))
                procPreviewNode_ = procPreviewNode_ == n->id ? 0 : n->id;
            if (ImGui::MenuItem("Bypass", nullptr, n->bypass)) {
                n->bypass = !n->bypass;
                changed = true;
            }
            if (ImGui::MenuItem("Delete")) {
                if (procPreviewNode_ == n->id) procPreviewNode_ = 0;
                if (procCurveNode_ == n->id) procCurveNode_ = 0;
                if (procInspectNode_ == n->id) procInspectNode_ = 0;
                procgraph::removeNode(g, n->id);
                procCtxNode_ = -1;
                changed = true;
            }
        }
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("##proc_add_node")) {
        for (const char* cat : {"Sources", "Masks", "Filters", "Repeat", "Attributes", "Output"}) {
            if (!ImGui::BeginMenu(cat)) continue;
            for (const ProcNodeType& t : procNodeTypes()) {
                if (std::strcmp(t.category, cat) != 0 || std::strcmp(t.key, "ObjectSettings") == 0) continue;
                if (ImGui::MenuItem(t.title)) {
                    const int id = procgraph::addNode(g, t.key, 0.0f, 0.0f);
                    if (id) {
                        ImNodes::SetNodeScreenSpacePos(id, procAddAt_);
                        // The model pool is the one thing a fresh node cannot
                        // guess - seed it with the first .obj.
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
    ui::EndCard();

    // ------------------------------------------------------------- inspector
    ImGui::SameLine(0.0f, gap);
    ui::BeginCard("##procinsp", ImVec2(0.0f, paneH));
    const int abort = drawProcInspector(vol, g, stale, changed, models);
    ui::EndCard();
    if (abort) {
        ImGui::End();
        return;  // the bake changed the objects vector under us
    }

    if (changed) {
        pruneProcOverrides(g);
        commitChange();
    }
    ImGui::End();
}

// The right pane: the volume, the bake decision and its state, the budget,
// the selected node's parameters, then seed / preview / instance editing.
// Returns non-zero when the objects vector changed (bake, clear, freeze,
// runtime switch) - every reference into it is then dangling.
int App::drawProcInspector(SceneObject& vol, ProcGraph& g, bool stale, bool& changed,
                           const std::vector<std::string>& models) {
    const theme::Semantics& sem = theme::semantics();
    const float fs = ImGui::GetFontSize();

    // --- identity
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float s = fs * 2.4f;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + s, p.y + s), pcol(sem.ok, 0.14f), 5.0f);
        ui::DrawGlyph(dl, ui::Glyph::Sparkle, ImVec2(p.x + s * 0.5f, p.y + s * 0.5f), s * 0.5f, pcol(sem.ok));
        ImGui::SetCursorScreenPos(ImVec2(p.x + s + fs * 0.6f, p.y + fs * 0.1f));
        {
            ui::FontScope bold(ui::boldFont());
            ImGui::TextUnformatted(vol.name.c_str());
        }
        ImGui::SetCursorScreenPos(ImVec2(p.x + s + fs * 0.6f, ImGui::GetCursorScreenPos().y));
        ImGui::TextDisabled("Procedural volume \xc2\xb7 %d nodes \xc2\xb7 seed %u", (int)g.nodes.size(),
                            (unsigned)g.seed);
        const float bsz = ImGui::GetFrameHeight();
        ImGui::SetCursorScreenPos(ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x - bsz,
                                         p.y + (s - bsz) * 0.5f));
        if (ui::OverlayIconButton("Select in scene", ui::Glyph::Eye, bsz, false, true, nullptr))
            selectOnly(procVolume_);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Select the volume in the scene");
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + s + fs * 0.6f));
    }

    // --- baked or runtime: the decision that changes what everything means
    {
        int mode = g.runtime ? 1 : 0;
        const char* modes[2] = {"Baked at build", "Runtime on PS2"};
        ImGui::BeginDisabled(g.frozen);
        const bool modeChanged = ui::Segmented("##procmode", modes, 2, &mode, -1.0f);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip(
                "Baked: the editor evaluates the graph and writes finished chunk\n"
                "meshes - the console loads geometry and never learns a graph\n"
                "existed. Costs disc space and one world, forever.\n\n"
                "Runtime: the graph is COMPILED into the game and evaluated on\n"
                "the EE, so the world can be different every boot and the\n"
                "geometry is never shipped. Costs load time, RAM, and a smaller\n"
                "set of nodes.");
        if (modeChanged) {
            g.runtime = mode == 1;
            changed = true;
            // Entering runtime throws the baked chunks away, because they
            // would draw on top of what the console generates.
            if (g.runtime) {
                procbake::clearVolume(project_, project_.active(), vol.id);
                commitChange();
                statusMessage_ = "Runtime mode: cleared the baked chunks of " + vol.name;
                return 1;
            }
        }
    }
    ImGui::Spacing();

    // --- the state card
    {
        const ImVec4 c = g.runtime ? sem.accent : g.frozen ? sem.accent : stale ? sem.warn : sem.ok;
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(c.x, c.y, c.z, 0.08f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(c.x, c.y, c.z, 0.45f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(fs * 0.7f, fs * 0.6f));
        ImGui::BeginChild("##bakecard", ImVec2(0, 0),
                          ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY |
                              ImGuiChildFlags_AlwaysUseWindowPadding);
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(2);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        auto headline = [&](const char* text, const char* detail) {
            const ImVec2 p = ImGui::GetCursorScreenPos();
            dl->AddCircleFilled(ImVec2(p.x + fs * 0.3f, p.y + fs * 0.55f), fs * 0.22f, pcol(c));
            ImGui::SetCursorScreenPos(ImVec2(p.x + fs * 0.9f, p.y));
            {
                ui::FontScope bold(ui::boldFont());
                ImGui::PushStyleColor(ImGuiCol_Text, c);
                ImGui::TextUnformatted(text);
                ImGui::PopStyleColor();
            }
            if (detail && *detail) {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", detail);
            }
        };
        if (g.runtime) {
            const std::vector<procrt::Issue> issues = procrt::capability(g);
            headline("Runs on the PS2", issues.empty() ? "every node is supported"
                                                       : (std::to_string(issues.size()) +
                                                          " node(s) the console cannot run")
                                                             .c_str());
            for (const procrt::Issue& i : issues) {
                ImGui::PushStyleColor(ImGuiCol_Text, sem.danger);
                ImGui::PushTextWrapPos(0.0f);
                ImGui::Text("- %s", i.text.c_str());
                ImGui::PopTextWrapPos();
                ImGui::PopStyleColor();
            }
            ui::InspectorRows rows(fs * 4.0f, fs * 30.0f);
            bool atStart = g.runAtStart;
            if (ui::Switch("Generate at scene start", &atStart)) {
                g.runAtStart = atStart;
                changed = true;
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                ImGui::SetTooltip(
                    "On: the world is built during the scene load, inside the\n"
                    "loading screen's progress bar. Off: nothing appears until a\n"
                    "Generate Volume flow node fires.");
            int seedMode = g.seedMode;
            const char* sm[2] = {"Same world every run", "New world every run"};
            if (ui::Segmented("##procseedmode", sm, 2, &seedMode, -1.0f)) {
                g.seedMode = seedMode;
                changed = true;
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                ImGui::SetTooltip(
                    "\"Same\" uses the Seed below - reproducible, and what you want\n"
                    "while authoring. \"New\" rolls one from the console clock at\n"
                    "generation time.");
        } else {
            char ago[64] = "";
            if (stale && procStaleSince_.count(vol.id)) {
                const double s = ImGui::GetTime() - procStaleSince_[vol.id];
                if (s < 60.0) std::snprintf(ago, sizeof ago, "changed %d s ago", (int)s);
                else std::snprintf(ago, sizeof ago, "changed %d min ago", (int)(s / 60.0));
            }
            if (g.frozen) headline("Frozen", "graph edits apply after unfreezing");
            else if (stale) headline("Bake is stale", ago);
            else headline("Baked - up to date", nullptr);

            const float moreW = ImGui::GetFrameHeight();
            const float frozenW = ImGui::CalcTextSize("Frozen").x + fs * 3.0f;
            const float bakeW = ImGui::GetContentRegionAvail().x - frozenW - moreW - fs;
            ImGui::BeginDisabled(g.frozen);
            const bool bakeClicked = ui::PrimaryButton("Bake now", bakeW, !g.frozen);
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                ImGui::SetTooltip("Writes the merged chunk meshes and the scene objects that draw them.\n"
                                  "A build does this automatically for every stale volume - this is\n"
                                  "for looking at the result now.");
            if (bakeClicked) {
                ImGui::EndChild();
                const procbake::Report rep = bakeProcVolume(procVolume_);
                procStatus_ = "Baked " + std::to_string(rep.instances) + " instances into " +
                              std::to_string(rep.chunks) + " chunk meshes (" +
                              std::to_string(rep.triangles) + " triangles)";
                for (const std::string& w : rep.warnings) procStatus_ += " | " + w;
                statusMessage_ = procStatus_;
                return 1;
            }
            ImGui::SameLine(0.0f, fs * 0.5f);
            bool frozen = g.frozen;
            {
                ImGui::BeginGroup();
                ImGui::SetCursorPosY(ImGui::GetCursorPosY());
                const bool flip = ui::Switch("Frozen", &frozen);
                ImGui::EndGroup();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                    ImGui::SetTooltip("Bake the current layout and keep it unchanged by roads, terrain\n"
                                      "and graph edits. Builds keep these chunks, even after reopening.");
                if (flip) {
                    ImGui::EndChild();
                    const std::string id = vol.id;
                    const procbake::Report rep =
                        procbake::setFrozen(project_, project_.active(), id, frozen, &procCaches_[id]);
                    if (rep.error.empty()) {
                        procPreviewNode_ = procCurveNode_ = 0;
                        procSelInstance_ = procSeedPreview_ = 0;
                        procOverrideMode_ = false;
                        commitChange();
                        statusMessage_ = frozen ? "Frozen procedural bake" : "Unfrozen procedural volume";
                    } else {
                        statusMessage_ = "Cannot freeze: " + rep.error;
                    }
                    return 1;  // freezing can insert/remove chunks
                }
            }
            ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - moreW);
            if (ui::MoreButton("Bake menu", moreW)) ImGui::OpenPopup("##bakemenu");
            if (ImGui::BeginPopup("##bakemenu")) {
                if (ImGui::MenuItem("Clear bake", nullptr, false, !g.frozen)) {
                    ImGui::EndPopup();
                    ImGui::EndChild();
                    procbake::clearVolume(project_, project_.active(), vol.id);
                    commitChange();
                    statusMessage_ = "Cleared the baked chunks of " + vol.name;
                    return 1;
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Remove the chunk meshes this volume baked.");
                ImGui::EndPopup();
            }
        }
        ImGui::EndChild();
    }

    // --- the budget, labelled, and four numbers under it
    const ProcNode* outNode = procgraph::outputNode(g);
    const int budget = outNode ? std::max(1, procgraph::inum(*outNode, "budget")) : 20000;
    ImGui::Spacing();
    if (procPreviewNode_ != 0 && !g.frozen) {
        // The budget is the FINAL output's; a node preview stops short of it.
        ImGui::TextDisabled("Triangle budget");
        ImGui::SameLine();
        if (ui::GhostButton("Measure the final output")) procPreviewNode_ = 0;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("The viewport shows one node, so the graph is evaluated only up to it.\n"
                              "Show the final output to measure the triangles the bake will write.");
    } else if (g.frozen) {
        const int chunks = (int)std::count_if(project_.objects().begin(), project_.objects().end(),
                                              [&](const SceneObject& o) { return o.procSource == vol.id; });
        ImGui::TextDisabled("Frozen bake \xc2\xb7 %d generated objects", chunks);
    } else {
        const int budgetTriangles = procBudget_.instanced ? procBudget_.sharedTriangles : procBudget_.triangles;
        ImGui::TextDisabled(procBudget_.instanced ? "Shared mesh budget" : "Triangle budget");
        const std::string a = thousands(budgetTriangles), b = " / " + thousands(budget);
        const float bw = ImGui::CalcTextSize((a + b).c_str()).x;
        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - bw);
        const bool over = budgetTriangles > budget;
        ImGui::PushStyleColor(ImGuiCol_Text, over ? sem.danger : sem.text);
        {
            ui::FontScope bold(ui::boldFont());
            ImGui::TextUnformatted(a.c_str());
        }
        ImGui::PopStyleColor();
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::TextDisabled("%s", b.c_str());
        {
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float w = ImGui::GetContentRegionAvail().x, h = std::max(3.0f, fs * 0.25f);
            const float f = std::clamp((float)budgetTriangles / (float)budget, 0.0f, 1.0f);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), pcol(sem.field), h);
            dl->AddRectFilled(p, ImVec2(p.x + w * f, p.y + h), pcol(over ? sem.danger : sem.ok), h);
            ImGui::Dummy(ImVec2(w, h + 2.0f));
        }
        if (procBudget_.instanced) {
            ImGui::TextDisabled("All placements (near): %s tris", thousands(procBudget_.triangles).c_str());
            ImGui::TextWrapped("Shared budget includes near + far meshes. Visible cost depends on LOD and culling; this is not an FPS estimate.");
        }
        {
            ui::FontScope mono(ui::monoFont());
            ImGui::PushFont(nullptr, fs * 0.86f);
            char cells[4][48];
            std::snprintf(cells[0], 48, "%s inst", thousands(procInstances_).c_str());
            if (procBudget_.instanced)
                std::snprintf(cells[1], 48, "%d models", procBudget_.models);
            else
                std::snprintf(cells[1], 48, "%d chunks", procBudget_.chunks);
            std::snprintf(cells[2], 48, "~%.0f KB geo", procBudget_.vertexBytes / 1024.0);
            std::snprintf(cells[3], 48, "%.1f ms", procLastMs_);
            const float cw = ImGui::GetContentRegionAvail().x / 4.0f;
            const float x0 = ImGui::GetCursorPosX();
            for (int i = 0; i < 4; ++i) {
                if (i) ImGui::SameLine(x0 + cw * i + (i == 3 ? cw - ImGui::CalcTextSize(cells[3]).x : 0.0f));
                ImGui::TextDisabled("%s", cells[i]);
            }
            ImGui::PopFont();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%d nodes re-ran on the last evaluation.", procNodesRun_);
        if (procFraction_ < 1.0f)
            ImGui::TextDisabled("preview at %.0f%% density while dragging", procFraction_ * 100.0f);
    }
    for (const std::string& w : procResult_.warnings) {
        ImGui::PushStyleColor(ImGuiCol_Text, sem.warn);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::Text("! %s", w.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }
    for (const procgraph::ProcIssue& i : procgraph::validate(g)) {
        ImGui::PushStyleColor(ImGuiCol_Text, sem.danger);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::Text("- %s", i.text.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }

    // --- the selected node: what it is, its parameters, in -> out
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ProcNode* sn = procgraph::node(g, procInspectNode_);
    const ProcNodeType* st = sn ? procNodeType(sn->type) : nullptr;
    if (!sn || !st) {
        ImGui::TextDisabled("Select a node in the graph to edit it here.");
    } else {
        // The header: what the node is and the three verbs on it. The
        // description is one sentence here; the whole paragraph and a line
        // per control are behind the help button.
        const bool isOutput = sn->type == "Output";
        const bool canBypass = !st->ins.empty() && !isOutput;
        ImGuiStorage* store = ImGui::GetStateStorage();
        const ImGuiID helpKey = ImGui::GetID("##prochelpopen");
        bool helpOpen = store->GetBool(helpKey, false);
        auto pinName = [](ProcType ty) {
            return ty == ProcType::Mask ? "mask" : ty == ProcType::Curve ? "curve" : "points";
        };
        std::string sub = st->category;
        sub += " \xc2\xb7 ";
        for (size_t i = 0; i < st->ins.size(); ++i) sub += (i ? ", " : "") + std::string(pinName(st->ins[i].type));
        sub += st->ins.empty() ? "\xe2\x86\x92 " : " \xe2\x86\x92 ";
        for (size_t i = 0; i < st->outs.size(); ++i) sub += (i ? ", " : "") + std::string(pinName(st->outs[i].type));
        if (st->outs.empty()) sub += "result";
        const std::string cat = st->category;
        const ImVec4 tint = cat == "Sources"      ? sem.ok
                            : cat == "Masks"      ? sem.special
                            : cat == "Filters"    ? sem.warn
                            : cat == "Attributes" ? sem.accent
                            : cat == "Repeat"     ? ImVec4(0.31f, 0.82f, 0.77f, 1.0f)
                                                  : sem.text;
        const ui::Glyph icon = cat == "Sources"      ? ui::Glyph::Sparkle
                               : cat == "Masks"      ? ui::Glyph::Mask
                               : cat == "Filters"    ? ui::Glyph::Funnel
                               : cat == "Attributes" ? ui::Glyph::Sliders
                               : cat == "Repeat"     ? ui::Glyph::Copies
                                                     : ui::Glyph::Box;
        ui::HeaderButton hb[3];
        int nb = 0, iPreview = -1, iBypass = -1, iHelp = -1;
        if (!isOutput) {
            iPreview = nb;
            hb[nb++] = {"Preview this node", ui::Glyph::Eye, procPreviewNode_ == sn->id};
        }
        if (canBypass) {
            iBypass = nb;
            hb[nb++] = {"Bypass", ui::Glyph::Power, sn->bypass};
        }
        iHelp = nb;
        hb[nb++] = {"Node help", ui::Glyph::Help, helpOpen};
        int hov = -1;
        const int hit = ui::NodeHeader(st->title, sub.c_str(), icon, tint, hb, nb, &hov);
        if (hit == iPreview && iPreview >= 0 && !g.frozen) procPreviewNode_ = procPreviewNode_ == sn->id ? 0 : sn->id;
        if (hit == iBypass && iBypass >= 0) {
            sn->bypass = !sn->bypass;
            changed = true;
        }
        if (hit == iHelp) store->SetBool(helpKey, helpOpen = !helpOpen);
        if (hov == iHelp && !helpOpen) {
            ImGui::BeginTooltip();
            procNodeDoc(*st, scaled(360.0f));
            ImGui::EndTooltip();
        }
        if (st->desc && *st->desc) {
            ImGui::PushTextWrapPos(0.0f);
            if (helpOpen) procNodeDoc(*st, 0.0f);
            else ImGui::TextDisabled("%s", procFirstSentence(st->desc).c_str());
            ImGui::PopTextWrapPos();
        }
        if (sn->bypass && canBypass) {
            ImGui::PushStyleColor(ImGuiCol_Text, sem.warn);
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted("Bypassed - its first input passes through untouched.");
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
        }
        // In -> out: what this node keeps of what reaches it - read before
        // the knobs that decide it.
        const procgen::Cache* cache = procCaches_.count(vol.id) ? &procCaches_[vol.id] : nullptr;
        const int out = cache ? procgen::cachedPointCount(*cache, sn->id) : -1;
        int in = -1;
        if (const ProcLink* l = procgraph::linkTo(g, sn->id, 0))
            in = cache ? procgen::cachedPointCount(*cache, l->fromNode) : -1;
        if (out >= 0) {
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_ChildBg, sem.field);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(fs * 0.7f, fs * 0.45f));
            ImGui::BeginChild("##inout", ImVec2(0, 0),
                              ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
            if (in >= 0) {
                ImGui::TextDisabled("In");
                ImGui::SameLine();
                ImGui::TextUnformatted(thousands(in).c_str());
                ImGui::SameLine();
                ImGui::TextDisabled("\xe2\x86\x92 out");
                ImGui::SameLine();
                ImGui::TextUnformatted(thousands(out).c_str());
                if (in > 0) {
                    char pct[32];
                    const int d = (int)std::lround(100.0 * (out - in) / (double)in);
                    std::snprintf(pct, sizeof pct, "%+d%%", d);
                    const float pw = ImGui::CalcTextSize(pct).x;
                    ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - pw);
                    ImGui::PushStyleColor(ImGuiCol_Text, d < 0 ? sem.danger : d > 0 ? sem.ok : sem.textDim);
                    ImGui::TextUnformatted(pct);
                    ImGui::PopStyleColor();
                }
            } else {
                ImGui::TextDisabled("Out");
                ImGui::SameLine();
                ImGui::TextUnformatted(thousands(out).c_str());
                ImGui::SameLine();
                ImGui::TextDisabled("points");
            }
            ImGui::EndChild();
            ImGui::PopStyleVar();
            ImGui::PopStyleColor();
        }
        ImGui::Spacing();
        if (drawProcNodeParams(*sn, *st, models)) changed = true;
        if (isOutput) {
            int set = 0;
            if (const ProcNode* s = procgraph::settingsNode(g)) set = (int)s->rows.size();
            char right[48];
            std::snprintf(right, sizeof right, "%d of %d set", set, (int)procObjectProps().size());
            ui::KitSection("Every generated object", right);
            // Keep the old storage node so existing bakes and project files
            // retain exactly the same settings and hashes. It is now edited
            // only here, never as an unconnected canvas card.
            const ProcNode* stored = procgraph::settingsNode(g);
            ProcNode settings = stored ? *stored : ProcNode{};
            settings.type = "ObjectSettings";
            if (!stored) settings.id = -1;
            if (drawProcNodeParams(settings, *procNodeType("ObjectSettings"), models)) {
                if (stored) {
                    procgraph::node(g, settings.id)->rows = std::move(settings.rows);
                } else {
                    const int id = procgraph::addNode(g, "ObjectSettings", 0.0f, 0.0f);
                    procgraph::node(g, id)->rows = std::move(settings.rows);
                }
                changed = true;
            }
        }
    }

    // --- seed, preview, instance editing
    ImGui::Spacing();
    ImGui::Separator();
    ui::Caption("Seed");
    {
        int seed = (int)g.seed;
        const float rw = ImGui::CalcTextSize("Reseed").x + ImGui::GetStyle().FramePadding.x * 2.6f;
        const ImVec2 sp = ImGui::GetCursorScreenPos();
        const float sx = ImGui::GetCursorPosX();
        const float sw = ImGui::GetContentRegionAvail().x - rw - ImGui::GetStyle().ItemSpacing.x;
        ImGui::PushClipRect(sp, ImVec2(sp.x + sw, sp.y + ImGui::GetFrameHeight()), true);
        ImGui::SetNextItemWidth(sw);
        if (ImGui::InputInt("Seed", &seed, 0)) {
            g.seed = (uint32_t)std::max(1, seed);
            changed = true;
        }
        ImGui::PopClipRect();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("The one knob that reshuffles everything. Every random draw comes from\n"
                              "(seed, node, point) - so editing one node never disturbs the rest.");
        ImGui::SameLine(sx + sw + ImGui::GetStyle().ItemSpacing.x);
        if (ImGui::Button("Reseed", ImVec2(rw, 0))) {
            g.seed = g.seed * 1664525u + 1013904223u;
            if (g.seed == 0) g.seed = 1;
            changed = true;
        }
    }
    {
        ui::InspectorRows rows(fs * 4.0f, fs * 30.0f);
        // The same flag the View menu carries.
        ui::Switch("Show preview (draw what the volumes generate)", &showProcPreview_);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip(g.frozen ? "Show the saved frozen chunks. Hiding them keeps the bake intact."
                                       : "Off = work on what is underneath; a finished forest hides the\n"
                                         "ground it grows on. The graph keeps being evaluated either way.\n"
                                         "Also View > Procedural preview.");
        ImGui::BeginDisabled(g.frozen);
        if (ui::Switch("Edit instances (move or delete single trees; edits survive re-bakes)",
                       &procOverrideMode_)) {
            procCurveNode_ = 0;
            procSelInstance_ = 0;
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("Click an instance in the viewport to move, rotate, rescale or delete it\n"
                              "by hand. The edit is stored against the point's stable identity.");
    }

    // --- the per-instance override editor (FILT-05)
    if (procOverrideMode_) {
        const procgen::Instance* sel = nullptr;
        for (const procgen::Instance& inst : procResult_.instances)
            if (inst.key == procSelInstance_) sel = &inst;
        if (!sel) {
            ImGui::TextDisabled("Click an instance in the viewport. Ctrl+click removes it.");
        } else {
            ProcOverride& ov = procOverrideFor(g, procSelInstance_);
            ImGui::Text("Instance at %.1f, %.1f, %.1f", sel->pos[0], sel->pos[1], sel->pos[2]);
            ui::InspectorRows rows(fs * 4.0f, fs * 6.0f);
            if (ui::AxisDrag3("Offset", ov.offset, 0.05f)) changed = true;
            if (ui::AxisDrag3("Rotate", ov.rotate, 1.0f)) changed = true;
            if (ui::DragFloat("Scale x", &ov.scale, 0.01f, 0.05f, 20.0f)) changed = true;
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
            ImGui::Text("%d manual edit%s", (int)g.overrides.size(), g.overrides.size() == 1 ? "" : "s");
            if (procResult_.overridesOrphaned > 0) {
                ImGui::SameLine();
                ImGui::TextDisabled("(%d no longer match a point)", procResult_.overridesOrphaned);
            }
            if (ImGui::SmallButton("Clear all edits")) {
                g.overrides.clear();
                procSelInstance_ = 0;
                changed = true;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Drop unmatched")) {
                // An override whose point is gone is kept by default (the point
                // may come back when a slider moves) - this is the cleanup.
                std::vector<uint64_t> live;
                for (const procgen::Instance& i : procResult_.instances) live.push_back(i.key);
                g.overrides.erase(std::remove_if(g.overrides.begin(), g.overrides.end(),
                                                 [&](const ProcOverride& o) {
                                                     return std::find(live.begin(), live.end(), o.key) ==
                                                            live.end();
                                                 }),
                                  g.overrides.end());
                changed = true;
            }
        }
    }

    // --- seed simulator (runtime volumes only): a runtime volume is a
    // program, and with "New world every run" the authored seed is not the
    // seed the player gets - this runs the graph on several seeds here.
    if (g.runtime) {
        ImGui::Spacing();
        if (ui::Section("Seed simulator##procseedsim")) {
            const int budget2 = budget;
            ImGui::SetNextItemWidth(fs * 6.0f);
            ImGui::SliderInt("Seeds", &procSeedCount_, 2, 24);
            ImGui::SameLine();
            if (ImGui::Button("Simulate")) runProcSeedSweep(procVolume_, procSeedCount_);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                ImGui::SetTooltip("Evaluates this graph once per seed at FULL density - about\n"
                                  "%.0f ms each, so %d seeds costs roughly %.1f s.",
                                  procLastMs_, procSeedCount_, procLastMs_ * procSeedCount_ / 1000.0);
            if (procSeedPreview_ != 0) {
                ImGui::TextColored(sem.accent, "viewport: seed %u", (unsigned)procSeedPreview_);
                ImGui::SameLine();
                if (ImGui::SmallButton("Back to the authored seed")) procSeedPreview_ = 0;
            } else {
                ImGui::TextDisabled("viewport: the authored seed (%u)", (unsigned)g.seed);
            }
            if (g.seedMode == 1)
                ImGui::TextColored(sem.warn, "New world every run - the player gets one of these.");
            const bool mine = procSeedTrialsFor_ == vol.id && !procSeedTrials_.empty();
            if (!mine) {
                ImGui::TextDisabled("Press Simulate to see what other seeds would build.");
            } else {
                int minI = INT32_MAX, maxI = 0, minT = INT32_MAX, maxT = 0, over = 0;
                for (const ProcSeedTrial& t : procSeedTrials_) {
                    minI = std::min(minI, t.instances);
                    maxI = std::max(maxI, t.instances);
                    minT = std::min(minT, t.triangles);
                    maxT = std::max(maxT, t.triangles);
                    if (t.triangles > budget2) ++over;
                }
                if (ImGui::BeginTable("procseeds", 5,
                                      ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp |
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
                        const bool shown = authored ? procSeedPreview_ == 0 : procSeedPreview_ == t.seed;
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::PushID((int)i);
                        char lab[64];
                        std::snprintf(lab, sizeof(lab), "%u%s", (unsigned)t.seed, authored ? " (authored)" : "");
                        if (ImGui::Selectable(lab, shown, ImGuiSelectableFlags_SpanAllColumns))
                            procSeedPreview_ = authored ? 0 : t.seed;
                        ImGui::TableNextColumn();
                        ImGui::Text("%d", t.instances);
                        ImGui::TableNextColumn();
                        if (t.triangles > budget2) ImGui::TextColored(sem.danger, "%d", t.triangles);
                        else ImGui::Text("%d", t.triangles);
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
                ImGui::Text("%d seeds: %d-%d instances, %d-%d triangles", (int)procSeedTrials_.size(), minI,
                            maxI, minT, maxT);
                if (over > 0)
                    ImGui::TextColored(sem.danger, "%d of %d seeds exceed the %d-triangle budget.", over,
                                       (int)procSeedTrials_.size(), budget2);
                else
                    ImGui::TextColored(sem.ok, "every simulated seed fits the %d-triangle budget.", budget2);
            }
        }
    } else if (procSeedPreview_ != 0) {
        procSeedPreview_ = 0;  // a baked volume shows what it bakes
    }
    return 0;
}
