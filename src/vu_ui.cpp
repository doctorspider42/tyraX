// Tools > VU Programs - authoring a VU1 microprogram or a VU0 kernel out of
// stages, with no assembly (docs/vu-authoring.md).
//
// App:: methods declared in app.hpp, own TU - the assetbrowser.cpp precedent.
//
// The thing that makes this panel worth having rather than a form over a struct
// is that it can ANSWER the two questions authoring a microprogram actually
// raises, without Docker and without a console:
//
//   "does it fit?"    - the micro-memory bar, against the real 2042-slot
//                       ceiling, with what the SCENE needs auto-detected
//   "what does it do?" - the generated VCL, and the host simulation of that
//                       exact program's output next to it
//
// Both come from the same `vugen::build` codegen calls, so nothing here is a
// second answer that can drift from the build.

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

#include <imgui.h>

#include "app.hpp"
#include "app_internal.hpp"
#include "platform.hpp"
#include "theme.hpp"
#include "vuasm.hpp"
#include "vugen.hpp"
#include "vusim.hpp"

namespace {

// The engine's StaPipProgramClass bits, in the order the panel lists them.
struct ClassRow {
    unsigned bit;
    const char* label;
    const char* what;
};
const ClassRow kClasses[] = {
    {1u << 0, "Colour", "vertex colour only - the fallback for everything"},
    {1u << 1, "Directional lights", "meshes shaded on VU1 from the scene lights"},
    {1u << 2, "Textured + lights", "the same, with a texture"},
    {1u << 3, "Textured", "vertex colour + a map_Kd texture"},
    {1u << 4, "Reflective (matcap)", "materials with a refl map"},
};

/** Emitted instruction count per (class, family), read from the ENGINE's own
 * .vclpp files rather than from the descriptions.
 *
 * This has to be the real files, and the reason is the whole point of the bar:
 * `setProgramsCache` uploads a cull program plus its CLIPPED-or-as_is twin per
 * class, and the clip family is much bigger than anything the generator
 * describes - the five clip programs alone measured 2162 instructions against
 * the 2042 ceiling before they were made to share a fan emitter. A budget
 * computed from the cull half only says a program fits when it does not, which
 * is exactly what happened: examples/vu-lab tripped the engine's own overflow
 * assert on the first console run while this panel showed green.
 *
 * Parsed once and cached - 25 files is a few milliseconds, and the window
 * rebuilds every frame. */
struct EngineSizes {
    bool loaded = false;
    // [class bit index][0 = cull, 1 = clip, 2 = as_is]
    int instr[5][3] = {};
};

EngineSizes& engineSizes() {
    static EngineSizes s;
    if (s.loaded) return s;
    s.loaded = true;  // one attempt; a missing engine tree leaves zeros
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path engine = "vendor/tyra/engine";
    const std::string exe = platform::exePath();
    if (!exe.empty()) {
        const fs::path c =
            fs::path(exe).parent_path() / ".." / "vendor" / "tyra" / "engine";
        if (fs::exists(c / "Makefile", ec)) engine = fs::weakly_canonical(c, ec);
    }
    static const char* kStems[5][3] = {
        {"cull/stapip_cull_c_vu1", "clip/stapip_clip_c_vu1", "as_is/stapip_as_is_c_vu1"},
        {"cull/stapip_cull_d_vu1", "clip/stapip_clip_d_vu1", "as_is/stapip_as_is_d_vu1"},
        {"cull/stapip_cull_td_vu1", "clip/stapip_clip_td_vu1", "as_is/stapip_as_is_td_vu1"},
        {"cull/stapip_cull_tc_vu1", "clip/stapip_clip_tc_vu1", "as_is/stapip_as_is_tc_vu1"},
        {"cull/stapip_cull_tce_vu1", "clip/stapip_clip_tce_vu1", "as_is/stapip_as_is_tce_vu1"},
    };
    for (int c = 0; c < 5; ++c)
        for (int f = 0; f < 3; ++f) {
            vuasm::Options opt;
            opt.includeRoot = engine.string();
            vuir::Program prog;
            std::string err;
            const std::string path =
                (engine / "src" / "renderer" / "3d" / "pipeline" / "static" /
                 "core" / "programs" / (std::string(kStems[c][f]) + ".vclpp"))
                    .string();
            if (!vuasm::parseFile(path, opt, prog, err)) continue;
            int n = 0;
            for (const vuir::Instr& in : prog.code)
                if (in.op != vuir::Op::Label && in.op != vuir::Op::Barrier &&
                    in.op != vuir::Op::Cont && in.op != vuir::Op::Nop)
                    ++n;
            s.instr[c][f] = n;
        }
    return s;
}

int classBitIndex(unsigned bit) {
    for (int i = 0; i < 5; ++i)
        if (bit == (1u << i)) return i;
    return 0;
}

/** A tooltip on the widget just submitted. Deliberately a local copy rather
 * than a shared helper: the flow-graph editor's version carries extra
 * bookkeeping for its node canvas, and the ONE rule that matters here is the
 * ORDER - a tooltip is a window and ImGui's "last item" is context-global, so
 * this must be called AFTER every IsItem* query on the widget it documents, or
 * the edit those queries commit silently stops happening. */
void paramTip(const char* tip) {
    if (!tip || !*tip) return;
    if (!ImGui::IsItemHovered()) return;
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(400.0f);
    ImGui::TextUnformatted(tip);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

const char* bindLabel(int slot) {
    switch (slot) {
        case 0: return "mesh X";
        case 1: return "mesh Y";
        case 2: return "mesh Z";
        case 3: return "mesh W";
        default: return "value";
    }
}

}  // namespace

// Build every enabled program once per frame the window is open. Cheap
// (milliseconds) and it is what keeps the listing, the budget and the
// simulation from being three answers to one question.
void App::vuRebuildPreview() {
    vuPreview_.clear();
    vuPreviewLabel_.clear();
    vuPreviewErrors_.clear();
    // One entry per (look, CLASS) pair - which is what the build emits, so the
    // panel and the build cannot disagree about how many microprograms exist.
    const unsigned resident = project::vuResidentClasses(project_);
    unsigned taken = 0;
    for (const VuProgram& pr : project_.vu.programs) {
        if (!pr.enabled) continue;
        const bool binds = project::vuLookBindsPerMesh(pr);
        for (unsigned cls : vugen::customClasses()) {
            if ((pr.classes & cls) == 0) continue;
            if ((resident & cls) == 0) {
                vuPreviewErrors_.push_back(
                    pr.name + ": claims " + project::vuClassName(cls) +
                    ", which this project does not draw - not generated.");
                continue;
            }
            if (taken & cls) {
                vuPreviewErrors_.push_back(
                    pr.name + ": " + project::vuClassName(cls) +
                    " is already taken by an earlier look. One program per "
                    "class - a slot holds one.");
                continue;
            }
            if (binds && !project::vuClassCanBind(cls)) {
                vuPreviewErrors_.push_back(
                    pr.name + ": binds a per-mesh parameter, so it cannot go on " +
                    project::vuClassName(cls) + " - that class needs those "
                    "addresses for its light colours. Make every parameter a "
                    "value, or drop the class.");
                continue;
            }
            // A stage list that folds away is not a look: it would install a
            // byte-for-byte copy of the engine's own program over the engine's
            // own slot. The BUILD already refuses it - saying so here is what
            // stops "I added three looks and the budget did not move" from
            // looking like a broken estimate.
            bool anyLive = false;
            for (const VuStage& s : pr.stages) {
                if (!s.enabled) continue;
                vugen::Stage probe = vugen::makeStage(s.kind);
                for (int i = 0; i < 4; ++i) {
                    probe.params[i].value = s.params[i];
                    probe.params[i].meshSlot = s.bind[i];
                }
                if (!vugen::stageIsNoOp(probe)) anyLive = true;
            }
            if (!anyLive) {
                vuPreviewErrors_.push_back(
                    pr.name +
                    ": no live stage yet - every strength is a literal zero, so "
                    "nothing is generated and it costs no micro memory. Add a "
                    "stage, or give one a strength above zero.");
                break;  // one line per look, not one per class
            }
            vugen::Desc d = vugen::descForClass(cls);
            for (const VuStage& s : pr.stages) {
                vugen::Stage st = vugen::makeStage(s.kind);
                st.enabled = s.enabled;
                for (int i = 0; i < 4; ++i) {
                    st.params[i].value = s.params[i];
                    st.params[i].meshSlot = s.bind[i];
                }
                d.stages.push_back(st);
            }
            vuPreview_.push_back(vugen::build(d));
            vuPreviewLabel_.push_back(pr.name + " - " +
                                      project::vuClassName(cls));
            for (const std::string& e : vuPreview_.back().errors)
                vuPreviewErrors_.push_back(vuPreviewLabel_.back() + ": " + e);
            taken |= cls;
        }
    }
}

// How many objects in the project a class actually draws - the number that
// turns an abstract class name into "these things".
int App::vuObjectsInClass(unsigned classBit) const {
    int n = 0;
    for (const SceneData& sc : project_.scenes)
        for (const SceneObject& o : sc.objects)
            if (project::vuClassOfObject(project_, o) == classBit) ++n;
    for (const Prefab& pf : project_.prefabs)
        for (const SceneObject& o : pf.objects)
            if (project::vuClassOfObject(project_, o) == classBit) ++n;
    return n;
}

void App::drawVuStageList(std::vector<VuStage>& stages, bool kernel) {
    int remove = -1, moveUp = -1;
    for (size_t i = 0; i < stages.size(); ++i) {
        VuStage& s = stages[i];
        const vugen::StageDef* def = vugen::stageDef(s.kind);
        ImGui::PushID((int)i);
        // A stage that emits nothing is drawn dimmed rather than hidden - "I
        // added it and nothing happened" has to have an answer on screen.
        vugen::Stage probe = vugen::makeStage(s.kind);
        probe.enabled = s.enabled;
        for (int k = 0; k < 4; ++k) {
            probe.params[k].value = s.params[k];
            probe.params[k].meshSlot = s.bind[k];
        }
        const bool dead = vugen::stageIsNoOp(probe);
        if (dead)
            ImGui::PushStyleColor(ImGuiCol_Text, theme::semantics().textDim);
        const std::string head =
            std::string(def ? def->title : (s.kind + " (unknown)")) +
            (def ? std::string("   [") + vugen::slotName(def->slot) + "]" : "");
        const bool open = ImGui::TreeNodeEx(head.c_str(),
                                            ImGuiTreeNodeFlags_DefaultOpen);
        if (dead) ImGui::PopStyleColor();
        if (dead && ImGui::IsItemHovered())
            ImGui::SetTooltip(
                !s.enabled ? "Disabled - not generated at all."
                           : "Every strength is a literal zero, so this stage is\n"
                             "not generated at all. Bind one to a mesh slot, or\n"
                             "type a value, to make it real.");
        if (open) {
            if (def) {
                ImGui::TextWrapped("%s", def->desc);
                ImGui::Spacing();
            }
            ImGui::Checkbox("Enabled", &s.enabled);
            if (ImGui::IsItemDeactivatedAfterEdit()) commitChange();
            for (int k = 0; def && k < def->paramCount; ++k) {
                ImGui::PushID(k);
                const vugen::StageParamDef& pd = def->params[k];
                const bool bound = s.bind[k] >= 0;
                ImGui::SetNextItemWidth(scaled(150));
                if (bound) {
                    ImGui::TextDisabled("%s", pd.name);
                    ImGui::SameLine(scaled(160));
                    ImGui::TextDisabled("<- %s", bindLabel(s.bind[k]));
                } else {
                    float v = s.params[k];
                    if (ImGui::DragFloat(pd.name, &v, (pd.hi - pd.lo) * 0.005f,
                                         pd.lo, pd.hi, "%.3f")) {
                        s.params[k] = v;
                        setDirty(true);
                    }
                    if (ImGui::IsItemDeactivatedAfterEdit()) commitChange();
                }
                paramTip(pd.tip);
                if (!pd.literalOnly && !kernel) {
                    ImGui::SameLine(scaled(300));
                    ImGui::SetNextItemWidth(scaled(110));
                    int b = s.bind[k] + 1;
                    const char* items[] = {"value", "mesh X", "mesh Y",
                                           "mesh Z", "mesh W"};
                    if (ImGui::Combo("##bind", &b, items, 5)) {
                        s.bind[k] = b - 1;
                        commitChange();
                    }
                    paramTip(
                        "Take this number from the object's own VU parameters\n"
                        "instead of baking it into the microprogram. That is how\n"
                        "one program serves a whole material class: the effect is\n"
                        "shared, the strength is per mesh, and a mesh that leaves\n"
                        "it at zero renders exactly as it would without the\n"
                        "program at all.\n\n"
                        "Batched objects share a bag, and therefore share these.");
                } else if (pd.literalOnly) {
                    ImGui::SameLine(scaled(300));
                    ImGui::TextDisabled("(baked)");
                    paramTip(
                        "Folded into the microprogram at build time (the\n"
                        "generator computes a reciprocal from it), so it cannot\n"
                        "come from a mesh.");
                }
                ImGui::PopID();
            }
            if (def) {
                ImGui::Spacing();
                ImGui::TextDisabled("about %d instructions", def->perVertex);
            }
            ImGui::TreePop();
        }
        ImGui::SameLine(ImGui::GetWindowWidth() - scaled(70));
        if (i > 0 && ImGui::SmallButton("^")) moveUp = (int)i;
        ImGui::SameLine();
        if (ImGui::SmallButton("X")) remove = (int)i;
        ImGui::PopID();
        ImGui::Separator();
    }
    if (moveUp > 0) {
        std::swap(stages[moveUp], stages[moveUp - 1]);
        commitChange();
    }
    if (remove >= 0) {
        stages.erase(stages.begin() + remove);
        commitChange();
    }

    if (ImGui::Button("Add stage...")) ImGui::OpenPopup("addVuStage");
    if (ImGui::BeginPopup("addVuStage")) {
        vugen::Slot last = (vugen::Slot)255;
        for (const vugen::StageDef& d : vugen::stageDefs()) {
            if (kernel && !d.kernelSafe) continue;
            if (d.slot != last) {
                if (last != (vugen::Slot)255) ImGui::Separator();
                ImGui::TextDisabled("%s", vugen::slotName(d.slot));
                last = d.slot;
            }
            // Every entry gets an explicit ##id: a Selectable's LABEL is its
            // ImGui id, and two stage lists in one window would otherwise
            // collide by construction.
            const std::string item =
                std::string(d.title) + "##add" + d.key;
            if (ImGui::Selectable(item.c_str())) {
                VuStage s;
                s.kind = d.key;
                for (int i = 0; i < d.paramCount; ++i)
                    s.params[i] = d.params[i].def;
                stages.push_back(s);
                commitChange();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos(scaled(420));
                ImGui::TextUnformatted(d.desc);
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
        }
        ImGui::EndPopup();
    }
}

void App::drawVuProgramsWindow() {
    if (!showVuPrograms_ || !hasProject_) return;
    ImGui::SetNextWindowSize(ImVec2(scaled(760), scaled(620)),
                             ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("VU Programs", &showVuPrograms_)) {
        ImGui::End();
        return;
    }
    vuRebuildPreview();

    if (ImGui::BeginTabBar("vuTabs")) {
        // --- micro memory -------------------------------------------------
        if (ImGui::BeginTabItem("Micro memory")) {
            ImGui::TextWrapped(
                "VU1 holds 2048 instruction slots and Path1 parks the "
                "draw-finish helper in the top six, so a pipeline set has 2042. "
                "Overrunning it in a RELEASE build overwrites that helper and "
                "hangs the post-effect barrier forever - the engine's own guard "
                "is an assert that release compiles out.");
            ImGui::Spacing();

            const unsigned needed = project::vuNeededClasses(project_);
            const unsigned mask = project::vuResidentClasses(project_);
            // Which twin each class uploads alongside its cull program depends
            // on the clipping mode, and the clip family is far bigger - so the
            // mode is part of the budget, not a detail.
            const int fam = project_.settings.clipping == "vu1" ? 1 : 2;
            int totEmitted = 0;
            for (const ClassRow& c : kClasses) {
                if ((mask & c.bit) == 0) continue;
                const int ci = classBitIndex(c.bit);
                totEmitted += engineSizes().instr[ci][0] +
                              engineSizes().instr[ci][fam];
            }
            // A custom program REPLACES its class's cull half, so only the
            // difference against the engine's own is new weight.
            for (const vugen::Built& b : vuPreview_) totEmitted += b.stageInstrs;
            // ... and the same for the project's own C++ scripts. Those are
            // built inside the BUILD CONTAINER - the editor has no compiler to
            // run them with - so what is read here is the manifest the last
            // build left behind. Stale until the next build, and labelled that
            // way, which is still better than the panel pretending a screen
            // full of microprograms does not exist.
            struct ScriptRow {
                std::string script, cls;
                unsigned bit = 0;
                // 0 = cull, 1 = the VU1 clipper, 2 = as_is - the same order
                // engineSizes() uses, so the program a row REPLACES is just
                // instr[ci][fam].
                int fam = 0;
                bool boot = true;
                std::string verdict;  // "ok" | "noop" | "" (older manifest)
                bool bootFromScript = false;  // the script overrides it
                int instrs = 0;
                int delta = 0;  // against the engine program it REPLACES
                bool resident = true;  // only two of the three ever are
            };
            std::vector<ScriptRow> scriptRows;
            {
                std::ifstream mf(std::filesystem::path(project_.dir) / "src" /
                                 "gen" / "vu_scripts.manifest");
                std::string line;
                while (std::getline(mf, line)) {
                    std::vector<std::string> f;
                    for (size_t at = 0;;) {
                        const size_t tab = line.find('	', at);
                        f.push_back(line.substr(at, tab == std::string::npos
                                                       ? std::string::npos
                                                       : tab - at));
                        if (tab == std::string::npos) break;
                        at = tab + 1;
                    }
                    if (f.size() < 7) continue;
                    ScriptRow r;
                    r.script = f[0];
                    r.cls = f[1];
                    r.instrs = std::atoi(f[2].c_str());
                    r.bit = (unsigned)std::atoi(f[4].c_str());
                    r.fam = f[5] == "twin" ? 2 : f[5] == "clip" ? 1 : 0;
                    r.boot = f[6] == "boot";
                    // Written by the build (the editor has no compiler for a
                    // project's C++), absent in a manifest from before it did.
                    r.verdict = f.size() > 7 ? f[7] : "";
                    r.bootFromScript = f.size() > 8 && f[8] == "script";
                    // THE CHECKBOX IS LIVE, the manifest is from the last
                    // build. The only thing the manifest is authoritative
                    // about here is WHO decides - and that cannot change by
                    // ticking a box - so when the panel is the one deciding,
                    // the panel's current value replaces the stale one.
                    //
                    // Both of the things below read r.boot: the micro-memory
                    // estimate, and the checkbox itself. Without this the
                    // estimate ignored the tick until the next Build, and the
                    // box visibly snapped back to the old value one frame
                    // after being clicked - it wrote the project and then
                    // re-read the manifest.
                    if (!r.bootFromScript)
                        for (const auto& e : project_.vu.scriptBoot)
                            if (e.first == r.script) r.boot = e.second;
                    // A script REPLACES the engine's program for that class,
                    // so only the difference is new weight. Adding the whole
                    // thing on top is how this bar first said 3056 of 2042 for
                    // a set that fits.
                    const int ci = classBitIndex(r.bit);
                    // A class is THREE programs and only two are resident:
                    // the cull half always, plus whichever of the clipping
                    // twins the mode selects. The other one sits in the ELF
                    // costing nothing - counting it is what made this estimate
                    // claim both modes cost the same.
                    r.delta = r.instrs - engineSizes().instr[ci][r.fam];
                    r.resident = r.fam == 0 || r.fam == fam;
                    if (r.boot && r.resident && (mask & r.bit))
                        totEmitted += r.delta;
                    scriptRows.push_back(r);
                }
            }
            const int totLo = (totEmitted + 1) / 2, totHi = totEmitted;
            const float frac = (float)totHi / 2042.0f;
            char bar[64];
            std::snprintf(bar, sizeof bar, "%d..%d of 2042", totLo, totHi);
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram,
                                  totLo > 2042   ? theme::semantics().danger
                                  : totHi > 2042 ? theme::semantics().warn
                                                 : theme::semantics().ok);
            ImGui::ProgressBar(frac > 1.0f ? 1.0f : frac,
                               ImVec2(-FLT_MIN, 0), bar);
            ImGui::PopStyleColor();
            ImGui::TextDisabled(
                "A range, not a number: VCL packs an upper and a lower op into "
                "one 64-bit slot when it can, so the exact size is only known "
                "after it runs.");
            ImGui::Spacing();
            ImGui::Separator();

            // WHAT RUNS THERE, as a list, because that is the question this tab
            // is actually asked. The material classes below are an
            // implementation axis - a reader wants "clipping, my cell shading,
            // the stock shading" and has to translate. So the clipping mode
            // leads, as the one resident feature that is neither a class nor a
            // program of yours.
            ImGui::SeparatorText("Resident on VU1 - this is the budget");
            {
                bool onVu1 = project_.settings.clipping == "vu1";
                if (ImGui::Checkbox("Clipping on VU1", &onVu1)) {
                    project_.settings.clipping = onVu1 ? "vu1" : "precise";
                    commitChange();
                }
                ImGui::SameLine(scaled(250));
                int clipTot = 0, asIsTot = 0;
                for (const ClassRow& c : kClasses) {
                    if ((mask & c.bit) == 0) continue;
                    const int ci = classBitIndex(c.bit);
                    clipTot += engineSizes().instr[ci][1];
                    asIsTot += engineSizes().instr[ci][2];
                }
                ImGui::TextDisabled("%d slots, against %d for the EE clipper",
                                    clipTot, asIsTot);
                if (ImGui::IsItemHovered() || ImGui::IsItemHovered())
                    ImGui::SetTooltip(
                        "Every material class is a PAIR: its cull program plus\n"
                        "the twin that draws whatever the frustum cut. This\n"
                        "picks which twin - the VU1 clipper, precise and about\n"
                        "twice the size, or the as_is program with the EE doing\n"
                        "the cutting. The game can flip it while it runs with\n"
                        "vuprog::setVU1Clipping().");
            }
            ImGui::Spacing();

            bool autoMode = project_.vu.residentAuto;
            if (ImGui::Checkbox("Detect from the scenes", &autoMode)) {
                project_.vu.residentAuto = autoMode;
                if (autoMode) project_.vu.residentClasses = needed;
                commitChange();
            }
            paramTip(
                "Keep the resident set equal to what the project's scenes and\n"
                "prefabs actually draw. A hand-set mask is right on the day it\n"
                "is set and wrong the first time someone adds a chrome ball -\n"
                "and the failure is a mesh quietly drawing in a simpler style,\n"
                "which reads as a material bug.");

            ImGui::BeginDisabled(autoMode);
            for (const ClassRow& c : kClasses) {
                bool on = (mask & c.bit) != 0;
                const bool used = (needed & c.bit) != 0;
                const int ci = classBitIndex(c.bit);
                const int emitted =
                    engineSizes().instr[ci][0] + engineSizes().instr[ci][fam];
                const int lo = (emitted + 1) / 2, hi = emitted;
                ImGui::PushID((int)c.bit);
                if (c.bit == 1u) ImGui::BeginDisabled();  // the fallback floor
                if (ImGui::Checkbox(c.label, &on)) {
                    project_.vu.residentClasses =
                        (on ? (mask | c.bit) : (mask & ~c.bit)) | 1u;
                    commitChange();
                }
                if (c.bit == 1u) ImGui::EndDisabled();
                ImGui::SameLine(scaled(220));
                ImGui::TextDisabled("%d..%d slots   %s", lo, hi, c.what);
                if (used && !on) {
                    ImGui::SameLine();
                    ImGui::PushStyleColor(ImGuiCol_Text, theme::semantics().warn);
                    ImGui::TextUnformatted("  the scene USES this");
                    ImGui::PopStyleColor();
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip(
                            "Dropping a class the scene draws does not crash -\n"
                            "the engine walks down to a resident relative - but\n"
                            "those meshes lose the feature (a lit mesh goes\n"
                            "flat, a matcap goes plain).");
                }
                ImGui::PopID();
            }
            ImGui::EndDisabled();

            // The OTHER clipping mode, priced. This is the biggest lever there
            // is - the clip family is roughly twice the as_is family - and it
            // is also the easiest way to blow the ceiling by accident, because
            // a custom program replaces the cull half in BOTH modes while the
            // twin it sits next to doubles in size. Measured the hard way: a
            // console run flipped vu-lab to VU1 clipping at run time and hit
            // the engine's own overflow assert.
            {
                int other = 0;
                const int otherFam = fam == 1 ? 2 : 1;
                for (const ClassRow& c : kClasses) {
                    if ((mask & c.bit) == 0) continue;
                    const int ci = classBitIndex(c.bit);
                    other += engineSizes().instr[ci][0] +
                             engineSizes().instr[ci][otherFam];
                }
                for (const vugen::Built& b : vuPreview_) other += b.stageInstrs;
                for (const ScriptRow& r : scriptRows) {
                    if (!r.boot || !(mask & r.bit)) continue;
                    // Same rule the other way round: a crossing-half program
                    // only counts in the mode that makes it resident.
                    if (r.fam != 0 && r.fam != otherFam) continue;
                    const int ci = classBitIndex(r.bit);
                    other += r.instrs - engineSizes().instr[ci][r.fam];
                }
                ImGui::Spacing();
                const bool fits = other <= 2042;
                ImGui::PushStyleColor(ImGuiCol_Text, fits ? theme::semantics().ok
                                                          : theme::semantics().warn);
                ImGui::TextWrapped(
                    "With %s instead: %d..%d of 2042 - %s. The game can switch "
                    "at run time with vuprog::setVU1Clipping(), between frames.",
                    fam == 1 ? "the EE clipper" : "VU1 clipping",
                    (other + 1) / 2, other,
                    fits ? "fits" : "DOES NOT FIT, the engine asserts");
                ImGui::PopStyleColor();
            }

            // The project's own C++ programs. Not editable here and not
            // previewable here - they are compiled by the build container - but
            // they are the biggest thing in this budget when they exist, and a
            // budget screen that omits them is worse than useless.
            // The looks belong in this list too, or "what is running there"
            // is only two thirds true.
            if (!vuPreview_.empty()) {
                ImGui::Spacing();
                ImGui::SeparatorText("Looks (stage lists)");
                for (size_t i = 0; i < vuPreview_.size(); ++i) {
                    ImGui::Bullet();
                    ImGui::SameLine();
                    ImGui::Text("%s", i < vuPreviewLabel_.size()
                                          ? vuPreviewLabel_[i].c_str()
                                          : "look");
                    ImGui::SameLine(scaled(430));
                    ImGui::TextDisabled("+%d over the engine's",
                                        vuPreview_[i].stageInstrs);
                }
            }

            ImGui::Spacing();
            ImGui::SeparatorText("VU scripts (src\\vu)");
            if (scriptRows.empty()) {
                ImGui::TextDisabled(
                    project::hasVuScripts(project_)
                        ? "Not built yet - run a Build and these fill in."
                        : "None. Add one from the Scripts panel, or see "
                          "docs/vu-authoring.md.");
            } else {
                ImGui::TextDisabled(
                    "From the last build - the editor cannot compile these "
                    "itself, so the numbers are as stale as your last Build.");
                // One emitted program, inside its script's node.
                auto drawScriptRow = [&](const ScriptRow& r) {
                    ImGui::Bullet();
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s%s", r.cls.c_str(),
                                        r.fam == 1   ? "  (VU1 clipper half)"
                                        : r.fam == 2 ? "  (EE-clipper half)"
                                                     : "");
                    ImGui::SameLine(scaled(430));
                    // The number that matters is the DIFFERENCE: the class row
                    // above already counts the engine's program, and this
                    // replaces it.
                    if (r.delta >= 0)
                        ImGui::TextDisabled("%d slots, +%d over the engine's",
                                            r.instrs, r.delta);
                    else
                        ImGui::TextDisabled("%d slots, %d under the engine's",
                                            r.instrs, -r.delta);
                    if (!r.resident) {
                        ImGui::SameLine();
                        ImGui::TextDisabled("  [other clipping mode]");
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip(
                                "In the ELF but not on VU1: this half is the one the\n"
                                "OTHER clipping mode uploads, so it costs nothing now.");
                    }
                    if (r.verdict == "noop") {
                        ImGui::SameLine();
                        ImGui::TextColored(theme::semantics().warn,
                                           "  changes nothing");
                    }
                };
                // ONE NODE PER SCRIPT, not one row per emitted program. A
                // script claiming five classes emits up to fifteen programs -
                // three per class - and the flat list that produced was forty
                // rows of nearly identical text that no one could read down.
                // The script is the thing a person turns on and off; the
                // programs are how it is implemented.
                for (size_t i = 0; i < scriptRows.size();) {
                    const std::string& name = scriptRows[i].script;
                    size_t j = i;
                    int resident = 0, residentDelta = 0, noops = 0;
                    while (j < scriptRows.size() && scriptRows[j].script == name) {
                        if (scriptRows[j].resident && (mask & scriptRows[j].bit)) {
                            ++resident;
                            residentDelta += scriptRows[j].delta;
                        }
                        if (scriptRows[j].verdict == "noop") ++noops;
                        ++j;
                    }
                    const bool boot = scriptRows[i].boot;
                    const bool fromScript = scriptRows[i].bootFromScript;
                    // ON AT BOOT, as a checkbox - and only a DEFAULT. A script
                    // that overrides activeAtBoot() answers for itself, which
                    // is not a precedence rule the framework implements but
                    // simply what C++ does with a virtual; the build reports
                    // which programs took that path and the box goes read-only
                    // for them, because a control that silently does nothing is
                    // worse than no control.
                    bool on = boot;
                    // Keyed by NAME, not by row index: the checkbox stores its
                    // state against the script's name, and an ImGui id built
                    // from a position moves to a different script the moment
                    // the list is reordered - which it is, by adding a file.
                    char box[192];
                    std::snprintf(box, sizeof box, "##vuboot_%s", name.c_str());
                    if (fromScript) ImGui::BeginDisabled();
                    if (ImGui::Checkbox(box, &on) && !fromScript) {
                        bool found = false;
                        for (auto& e : project_.vu.scriptBoot)
                            if (e.first == name) e.second = on, found = true;
                        if (!found) project_.vu.scriptBoot.push_back({name, on});
                        commitChange();
                    }
                    if (fromScript) ImGui::EndDisabled();
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip(
                            fromScript
                                ? "src\\vu\\*.cpp decides this one - it overrides\n"
                                  "activeAtBoot(). Delete that override to use this box."
                                : "On at boot. Only what is ACTIVE occupies micro\n"
                                  "memory, so a game can carry more programs than fit\n"
                                  "at once. Takes effect on the next Build.");
                    ImGui::SameLine();
                    char header[192];
                    std::snprintf(header, sizeof header,
                                  "%s##vuscript%zu", name.c_str(), i);
                    const bool open = ImGui::TreeNodeEx(
                        header, ImGuiTreeNodeFlags_SpanAvailWidth);
                    ImGui::SameLine(scaled(190));
                    ImGui::TextDisabled("%d program%s resident, %+d slots",
                                        resident, resident == 1 ? "" : "s",
                                        residentDelta);
                    if (noops) {
                        ImGui::SameLine();
                        ImGui::TextColored(theme::semantics().warn,
                                           "  %d change nothing", noops);
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip(
                                "The build compared each program against the SAME\n"
                                "description with the script removed, and these\n"
                                "came out identical - the body runs and draws the\n"
                                "stock picture. Usually the slot: a displacement\n"
                                "at Slot::Color has nothing left to move.");
                    }
                    if (open) {
                        for (size_t k = i; k < j; ++k) drawScriptRow(scriptRows[k]);
                        ImGui::TreePop();
                    }
                    i = j;
                }
            }

            // Everything else the console runs on a VU. None of it is in the
            // bar, and saying so is the point: the first thing a budget screen
            // is asked is "can I turn animation off to get room", and the
            // answer is that there is nothing resident to turn off.
            ImGui::Spacing();
            ImGui::SeparatorText("Also on the VUs - not in this budget");
            auto elsewhere = [&](const char* what, const char* where,
                                 const char* why) {
                ImGui::Bullet();
                ImGui::SameLine();
                ImGui::TextDisabled("%s", what);
                ImGui::SameLine(scaled(250));
                ImGui::TextDisabled("%s", where);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", why);
            };
            elsewhere("Animated models, drawing", "VU1, four programs",
                      "The dynamic pipeline uploads its own set over the same\n"
                      "micro-memory addresses when it draws, so it SWAPS with\n"
                      "the static set rather than sharing it.");
            elsewhere("Skeletal skinning", "VU0, macro mode",
                      "COP2 instructions issued by the EE - there is no\n"
                      "microprogram to upload, so it occupies no micro memory\n"
                      "at all. Pose evaluation runs on the EE.");
            elsewhere("Particle billboards", "VU1, own small set",
                      "Swapped in on demand (ensureProgramSet) - the resident\n"
                      "set has no room for them.");
            if (project_.vu.kernel.enabled)
                elsewhere("Your VU0 kernel", "VU0, micro mode",
                          "Against VU0's own 512 slots - see the VU0 kernel\n"
                          "tab. Independent of everything above.");

            ImGui::EndTabItem();
        }

        // --- the programs --------------------------------------------------
        if (ImGui::BeginTabItem("Programs")) {
            ImGui::TextWrapped(
                "A LOOK is one stage list installed over every material class "
                "you tick - a whole-scene treatment, which is what VU1 is for. "
                "One prop that wiggles is an animation's job. So a plain VALUE "
                "means every mesh of every ticked class, and that is the normal "
                "case; binding a parameter to a mesh slot is the exception, for "
                "when the treatment has to vary in strength. A mesh whose "
                "numbers are all zero renders exactly as it would with no "
                "program at all.");
            // A material class is TWO resident programs and a look replaces
            // both - but in VU1-clipping mode the second one is the CLIPPER,
            // which has no generated twin. Anything the frustum cuts then
            // keeps the engine's own shading, which reads as "my effect only
            // applies to some objects" and is impossible to guess at.
            if (!project_.vu.programs.empty() &&
                project_.settings.clipping == "vu1") {
                ImGui::PushStyleColor(ImGuiCol_Text,
                                      theme::semantics().warn);
                ImGui::TextWrapped(
                    "VU1 clipping is on. A look replaces a class's cull "
                    "program AND its as_is twin, but not the VU1 CLIPPER - so "
                    "a mesh the frustum cuts (anything at the edge of the "
                    "screen) renders with the engine's own program and no "
                    "look. Switch Preferences > Rendering to the EE clipper "
                    "for a look that covers every pixel.");
                ImGui::PopStyleColor();
                ImGui::Spacing();
            }
            // Even on the EE clipper, the twin has no MVP multiply, so only
            // colour and texture stages travel with a clipped package.
            {
                bool moves = false;
                for (const VuProgram& pr : project_.vu.programs)
                    if (pr.enabled && project::vuLookMovesGeometry(pr))
                        moves = true;
                if (moves) {
                    ImGui::PushStyleColor(ImGuiCol_Text,
                                          theme::semantics().warn);
                    ImGui::TextWrapped(
                        "A look here moves geometry. That stage cannot run on "
                        "a package the frustum cut - those vertices are "
                        "already transformed - and classification is per "
                        "PACKAGE, so a mesh straddling the EDGE OF THE SCREEN "
                        "is displaced only in part, with a step where the two "
                        "halves meet. It also moves only the passes whose "
                        "class this look claims: a matcap prop ripples while "
                        "its reflection pass stays put and cuts through it, "
                        "and the same goes for a baked lightmap. Keep the "
                        "amplitude small, and keep multi-pass props out of a "
                        "displaced scene. Colour and texture stages have "
                        "neither problem; vuprog::movesGeometry() reports this "
                        "at run time.");
                    ImGui::PopStyleColor();
                    ImGui::Spacing();
                }
            }
            ImGui::Spacing();

            for (const std::string& e : vuPreviewErrors_) {
                ImGui::PushStyleColor(ImGuiCol_Text, theme::semantics().danger);
                ImGui::TextWrapped("%s", e.c_str());
                ImGui::PopStyleColor();
            }

            const unsigned needed = project::vuNeededClasses(project_);
            int removeProg = -1;
            for (size_t i = 0; i < project_.vu.programs.size(); ++i) {
                VuProgram& pr = project_.vu.programs[i];
                ImGui::PushID((int)(1000 + i));
                ImGui::SeparatorText(pr.name.c_str());
                char nameBuf[64];
                std::snprintf(nameBuf, sizeof nameBuf, "%s", pr.name.c_str());
                ImGui::SetNextItemWidth(scaled(200));
                if (ImGui::InputText("Name", nameBuf, sizeof nameBuf))
                    pr.name = nameBuf;
                if (ImGui::IsItemDeactivatedAfterEdit()) commitChange();
                ImGui::SameLine();
                bool en = pr.enabled;
                if (ImGui::Checkbox("Build it", &en)) {
                    pr.enabled = en;
                    commitChange();
                }
                ImGui::SameLine();
                // Which look the game boots into. Every look is in the ELF and
                // `vuprog::activate(i)` swaps them at run time, so this is only
                // the starting one - but it IS what you see, so it belongs
                // here rather than in the .tyra by hand.
                ImGui::BeginDisabled(!pr.enabled);
                bool boot = project_.vu.activeLook == (int)i;
                if (ImGui::RadioButton("Active at boot", boot)) {
                    project_.vu.activeLook = (int)i;
                    commitChange();
                }
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(
                        "The look install() activates. The game can swap to "
                        "any other with vuprog::activate(i) - see "
                        "docs/vu-authoring.md.");
                ImGui::SameLine(ImGui::GetWindowWidth() - scaled(90));
                if (ImGui::SmallButton("Remove")) removeProg = (int)i;

                // Which classes this look covers. This is the whole point of a
                // look: a treatment that stops at one material class is not a
                // treatment - cell shading on untextured props but not textured
                // ones just reads as broken.
                const bool binds = project::vuLookBindsPerMesh(pr);
                ImGui::TextDisabled("Applies to:");
                for (const ClassRow& c : kClasses) {
                    ImGui::PushID((int)c.bit);
                    bool on = (pr.classes & c.bit) != 0;
                    const bool drawn = (needed & c.bit) != 0;
                    const bool blocked = binds && !project::vuClassCanBind(c.bit);
                    ImGui::BeginDisabled(blocked);
                    char lbl[96];
                    const int n = vuObjectsInClass(c.bit);
                    std::snprintf(lbl, sizeof lbl, "%s (%d object%s)", c.label,
                                  n, n == 1 ? "" : "s");
                    if (ImGui::Checkbox(lbl, &on)) {
                        pr.classes = on ? (pr.classes | c.bit)
                                        : (pr.classes & ~c.bit);
                        commitChange();
                    }
                    ImGui::EndDisabled();
                    if (blocked)
                        paramTip(
                            "This look binds a parameter to a per-mesh slot, and "
                            "those four numbers live in the directional-lights "
                            "colour block - which a lit class needs for its own "
                            "light colours. Make every parameter a plain value "
                            "and this class opens up.");
                    else if (!drawn && on)
                        paramTip("Nothing in the project draws with this class, "
                                 "so no program is generated for it.");
                    ImGui::PopID();
                }
                drawVuStageList(pr.stages, false);
                ImGui::PopID();
            }
            if (removeProg >= 0) {
                project_.vu.programs.erase(project_.vu.programs.begin() +
                                           removeProg);
                // The boot look is an INDEX, so removing anything before it
                // moves it. Removing the boot look itself falls back to the
                // first one rather than leaving a dangling index.
                if (project_.vu.activeLook > removeProg)
                    --project_.vu.activeLook;
                else if (project_.vu.activeLook == removeProg)
                    project_.vu.activeLook = 0;
                commitChange();
            }

            ImGui::Spacing();
            if (ImGui::Button("Add a look")) {
                VuProgram pr;
                pr.name = "look";
                // Everything the project actually draws: a treatment is
                // scene-wide by default, and narrowing it should be a decision
                // the author makes on purpose rather than one the default makes
                // for them by omission.
                pr.classes = needed;
                project_.vu.programs.push_back(pr);
                commitChange();
            }
            paramTip(
                "A look is ONE stage list installed over every material class "
                "you tick. It starts covering everything this project draws, "
                "which is what a scene-wide treatment - cell shading, an "
                "underwater wobble - actually wants.");
            ImGui::Spacing();
            ImGui::TextDisabled(
                "A stage a class cannot carry is skipped for that class with the "
                "reason shown,\nnot refused - a UV scroll has no UV on "
                "untextured geometry.");
            ImGui::EndTabItem();
        }

        // --- the generated source, and what it computes ---------------------
        if (ImGui::BeginTabItem("Generated")) {
            if (vuPreview_.empty()) {
                ImGui::TextDisabled("No program yet - add one on the Programs tab.");
            } else {
                if (vuPreviewSel_ >= (int)vuPreview_.size()) vuPreviewSel_ = 0;
                for (size_t i = 0; i < vuPreview_.size(); ++i) {
                    if (i) ImGui::SameLine();
                    const std::string tab =
                        (i < vuPreviewLabel_.size() ? vuPreviewLabel_[i]
                                                    : std::string("?")) +
                        "##sel" + std::to_string(i);
                    if (ImGui::RadioButton(tab.c_str(), vuPreviewSel_ == (int)i))
                        vuPreviewSel_ = (int)i;
                }
                const vugen::Built& b = vuPreview_[vuPreviewSel_];
                ImGui::Spacing();
                if (ImGui::Button("Run it on the host")) vuSimulate();
                paramTip(
                    "Runs this exact microprogram in the host VU simulator over\n"
                    "a small synthetic mesh and prints the GS vertices it\n"
                    "staged, plus the same run with the mesh parameters at zero\n"
                    "so the two can be compared. No Docker, no console.");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(scaled(200));
                ImGui::SliderFloat("mesh X", &vuSimParams_[0], -2.0f, 2.0f);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(scaled(160));
                ImGui::SliderFloat("time", &vuSimTime_, 0.0f, 12.0f);

                if (!vuSimOut_.empty()) {
                    ImGui::BeginChild("vusim", ImVec2(0, scaled(150)), true);
                    ImGui::TextUnformatted(vuSimOut_.c_str());
                    ImGui::EndChild();
                }
                ImGui::Separator();
                ImGui::BeginChild("vclpp", ImVec2(0, 0), true,
                                  ImGuiWindowFlags_HorizontalScrollbar);
                ImGui::TextUnformatted(b.vclpp.c_str());
                ImGui::EndChild();
            }
            ImGui::EndTabItem();
        }

        // --- the VU0 kernel -------------------------------------------------
        if (ImGui::BeginTabItem("VU0 kernel")) {
            ImGui::TextWrapped(
                "The same stages on the other vector unit, with none of the "
                "rendering around them: N quadwords in, N out, one stage list "
                "per element. VU0 has 256 quadwords of data memory TOTAL and "
                "512 micro slots, and its register file is shared with the "
                "engine's own Vec4/M4x4 math - so the generated driver blocks "
                "the EE while the kernel runs.");
            ImGui::Spacing();
            VuKernel& k = project_.vu.kernel;
            bool en = k.enabled;
            if (ImGui::Checkbox("Generate a VU0 kernel", &en)) {
                k.enabled = en;
                commitChange();
            }
            char name[64];
            std::snprintf(name, sizeof name, "%s", k.name.c_str());
            ImGui::SetNextItemWidth(scaled(200));
            if (ImGui::InputText("Name", name, sizeof name)) k.name = name;
            if (ImGui::IsItemDeactivatedAfterEdit()) commitChange();
            paramTip("Names the generated files and the driver class.");
            ImGui::SetNextItemWidth(scaled(200));
            if (ImGui::SliderInt("Batch size", &k.maxElements, 8, 112))
                setDirty(true);
            if (ImGui::IsItemDeactivatedAfterEdit()) commitChange();
            paramTip(
                "Elements per call. The input and output blocks both have to\n"
                "fit VU0's 256 quadwords next to the parameters, which caps\n"
                "this at 112.");
            ImGui::Spacing();
            drawVuStageList(k.stages, true);

            vugen::KernelDesc kd;
            kd.maxElements = k.maxElements;
            for (const VuStage& s : k.stages) {
                vugen::Stage st = vugen::makeStage(s.kind);
                st.enabled = s.enabled;
                for (int i = 0; i < 4; ++i) st.params[i].value = s.params[i];
                kd.stages.push_back(st);
            }
            const vugen::BuiltKernel bk = vugen::buildKernel(kd);
            ImGui::Separator();
            for (const std::string& e : bk.errors) {
                ImGui::PushStyleColor(ImGuiCol_Text, theme::semantics().danger);
                ImGui::TextWrapped("%s", e.c_str());
                ImGui::PopStyleColor();
            }
            if (bk.errors.empty() && !bk.program.code.empty()) {
                const int n = (int)bk.program.code.size();
                ImGui::Text("%d instructions -> %d..%d of VU0's 512 slots", n,
                            (n + 1) / 2, n);
                ImGui::TextDisabled("%d instructions per element",
                                    bk.perElement);
            }

            // THE OTHER WAY TO WRITE ONE, and the one that is not limited to
            // what the catalogue happens to cover: a C++ body in src/vu0/, the
            // VU0 twin of src/vu/. Those are compiled and run inside the BUILD
            // CONTAINER - the editor has no compiler to run them with - so what
            // is listed here is the manifest the last build left behind, the
            // same arrangement the script rows above use.
            ImGui::Spacing();
            ImGui::SeparatorText("Kernels written in C++ (src/vu0/*.cpp)");
            {
                struct KernelRow {
                    std::string name, cls;
                    int instrs = 0, perElement = 0, batch = 0;
                };
                std::vector<KernelRow> rows;
                std::ifstream mf(std::filesystem::path(project_.dir) / "src" /
                                 "gen" / "vu0_scripts.manifest");
                std::string line;
                while (std::getline(mf, line)) {
                    std::vector<std::string> f;
                    for (size_t at = 0;;) {
                        const size_t tab = line.find('	', at);
                        f.push_back(line.substr(at, tab == std::string::npos
                                                       ? std::string::npos
                                                       : tab - at));
                        if (tab == std::string::npos) break;
                        at = tab + 1;
                    }
                    if (f.size() < 5) continue;
                    rows.push_back({f[0], f[4], std::atoi(f[1].c_str()),
                                    std::atoi(f[2].c_str()),
                                    std::atoi(f[3].c_str())});
                }
                if (rows.empty()) {
                    ImGui::TextDisabled(
                        "None. A .cpp in src/vu0/ that subclasses vu::Kernel "
                        "gets a driver of its own - see docs/vu-authoring.md.");
                } else {
                    for (const KernelRow& r : rows) {
                        ImGui::BulletText("%s - Tyra::%s", r.name.c_str(),
                                          r.cls.c_str());
                        ImGui::SameLine();
                        ImGui::TextDisabled(
                            "%d instructions (%d..%d of 512), %d per element, "
                            "batch %d",
                            r.instrs, (r.instrs + 1) / 2, r.instrs,
                            r.perElement, r.batch);
                    }
                    ImGui::TextDisabled(
                        "From the last build. These are VU0's OWN 512 slots - "
                        "a kernel costs nothing out of the VU1 budget above.");
                }
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

// Runs the selected program in the host simulator over a small synthetic mesh,
// twice: once with the panel's mesh parameters and once with them at zero. The
// second run is the interesting one - it is the identity the whole design rests
// on, and seeing the two side by side is what makes "a mesh that wants nothing
// gets nothing" a fact rather than a promise.
void App::vuSimulate() {
    vuSimOut_.clear();
    if (vuPreviewSel_ < 0 || vuPreviewSel_ >= (int)vuPreview_.size()) return;
    const vugen::Built& b = vuPreview_[vuPreviewSel_];
    if (b.program.code.empty()) return;

    const int top = 22;  // VU1_STAPIP_LAST_ITEM_ADDR + 1
    const int verts = 3;
    auto bits = [](float f) {
        uint32_t x;
        std::memcpy(&x, &f, 4);
        return x;
    };
    auto stage = [&](const float params[4], float time) {
        std::vector<uint32_t> mem((size_t)vusim::kMemWords, 0u);
        auto putf = [&](int qw, int f, float v) {
            mem[(size_t)qw * 4 + f] = bits(v);
        };
        auto puti = [&](int qw, int f, uint32_t v) {
            mem[(size_t)qw * 4 + f] = v;
        };
        // A plausible view-projection: the clip W has to stay positive, since
        // the program divides by it.
        const float mvp[4][4] = {{1.2f, 0.0f, 0.0f, 0.0f},
                                 {0.0f, 1.6f, 0.0f, 0.0f},
                                 {0.0f, 0.0f, -1.002f, -1.0f},
                                 {0.0f, 0.0f, -2.0f, 60.0f}};
        for (int r = 0; r < 4; ++r)
            for (int f = 0; f < 4; ++f) putf(r, f, mvp[r][f]);
        puti(8, 0, 0u);           // multi-colour
        putf(8, 2, -255.0f / 900.0f);
        putf(8, 3, 255.0f * 1000.0f / 900.0f);
        puti(19, 0, 1u), puti(19, 1, 1u << 28), puti(19, 2, 0xEu);
        for (int f = 0; f < 4; ++f) putf(15, f, params[f]);
        putf(16, 0, time);
        putf(16, 1, std::sin(time));
        putf(16, 2, std::cos(time));
        putf(16, 3, 1.0f);
        putf(top, 0, 2048.0f), putf(top, 1, 2048.0f), putf(top, 2, 8388607.5f);
        puti(top, 3, (uint32_t)verts);
        puti(top + 1, 0, (uint32_t)verts | (1u << 15));
        const int regs = b.regsPerVertex;
        puti(top + 1, 1, (1u << 14) | (0x13u << 15) | ((uint32_t)regs << 28));
        int addr = top + 2;
        const float pos[3][3] = {
            {-2.0f, 0.0f, 1.0f}, {2.0f, 0.5f, -1.0f}, {0.0f, 3.0f, 0.0f}};
        for (int i = 0; i < verts; ++i) {
            for (int f = 0; f < 3; ++f) putf(addr + i, f, pos[i][f]);
            putf(addr + i, 3, 1.0f);
        }
        addr += verts;
        if (b.regsPerVertex == 3) {  // an ST stream
            for (int i = 0; i < verts; ++i) {
                putf(addr + i, 0, (float)i * 0.5f);
                putf(addr + i, 1, 1.0f - (float)i * 0.25f);
                putf(addr + i, 2, 1.0f);
            }
            addr += verts;
        }
        for (int i = 0; i < verts; ++i) {
            putf(addr + i, 0, 200.0f), putf(addr + i, 1, 120.0f);
            putf(addr + i, 2, 40.0f), putf(addr + i, 3, 128.0f);
        }
        vusim::Config cfg;
        cfg.top = top;
        return vusim::run(b.program, mem, cfg);
    };

    const float zero[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    const vusim::Result off = stage(zero, 0.0f);
    const vusim::Result on = stage(vuSimParams_, vuSimTime_);
    if (!off.ok || !on.ok || off.kicks.empty()) {
        vuSimOut_ = "the program did not run: " +
                    (off.ok ? on.error : off.error);
        return;
    }
    char line[256];
    const int start = on.kicks.front();
    const int tag = b.tagQuads;
    const int regs = b.regsPerVertex;
    std::string s =
        "GS vertices - screen X/Y in 12.4 fixed point, then the 24-bit Z.\n"
        "'zero' is the same program with the mesh parameters at 0: it must\n"
        "match what the engine's own program would have drawn.\n\n";
    for (int i = 0; i < verts; ++i) {
        const size_t q = (size_t)(start + tag + i * regs + (regs == 3 ? 2 : 1)) * 4;
        if (q + 3 >= on.mem.size()) break;
        const int32_t x = (int32_t)on.mem[q], y = (int32_t)on.mem[q + 1],
                      z = (int32_t)on.mem[q + 2];
        const int32_t x0 = (int32_t)off.mem[q], y0 = (int32_t)off.mem[q + 1],
                      z0 = (int32_t)off.mem[q + 2];
        std::snprintf(line, sizeof line,
                      "v%d   live %7d %7d %9d      zero %7d %7d %9d%s\n", i, x,
                      y, z, x0, y0, z0,
                      (x == x0 && y == y0 && z == z0) ? "   (same)" : "");
        s += line;
    }
    if (on.qClobbers) {
        std::snprintf(line, sizeof line,
                      "\n%d Q CLOBBER(S): a div or rsqrt result was overwritten "
                      "before anything read it.\nA stage in the NDC slot must "
                      "not write Q - the texture correction still needs it.\n",
                      on.qClobbers);
        s += line;
    }
    for (const vusim::Warning& w : on.warnings) s += "\n" + w.text;
    vuSimOut_ = s;
}
