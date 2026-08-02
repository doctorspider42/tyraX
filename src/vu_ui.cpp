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
    vuPreviewErrors_.clear();
    for (const VuProgram& pr : project_.vu.programs) {
        vugen::Desc d = vugen::descCustomBase(pr.base);
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
        for (const std::string& e : vuPreview_.back().errors)
            vuPreviewErrors_.push_back(vugen::customBaseTitle(pr.base) +
                                       std::string(": ") + e);
    }
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
            ImGui::EndTabItem();
        }

        // --- the programs --------------------------------------------------
        if (ImGui::BeginTabItem("Programs")) {
            ImGui::TextWrapped(
                "A program replaces one MATERIAL CLASS, not one object - VU1 "
                "micro memory has no room for a program per mesh. So the kind "
                "of effect is per class and its STRENGTH is per mesh: bind a "
                "parameter to a mesh slot and set it per object in the "
                "inspector. A mesh whose numbers are all zero renders exactly "
                "as it would with no program at all.");
            ImGui::Spacing();

            for (const std::string& e : vuPreviewErrors_) {
                ImGui::PushStyleColor(ImGuiCol_Text, theme::semantics().danger);
                ImGui::TextWrapped("%s", e.c_str());
                ImGui::PopStyleColor();
            }

            int removeProg = -1;
            for (size_t i = 0; i < project_.vu.programs.size(); ++i) {
                VuProgram& pr = project_.vu.programs[i];
                ImGui::PushID((int)(1000 + i));
                ImGui::SeparatorText(vugen::customBaseTitle(pr.base));
                bool en = pr.enabled;
                if (ImGui::Checkbox("Build this program", &en)) {
                    pr.enabled = en;
                    commitChange();
                }
                ImGui::SameLine(ImGui::GetWindowWidth() - scaled(90));
                if (ImGui::SmallButton("Remove")) removeProg = (int)i;
                if (i < vuPreview_.size()) {
                    const vugen::Built& b = vuPreview_[i];
                    ImGui::TextDisabled(
                        "%d instructions, %d of them from stages",
                        (int)b.program.code.size(), b.stageInstrs);
                    for (const std::string& d : b.droppedStages)
                        ImGui::TextDisabled("  not generated: %s", d.c_str());
                }
                drawVuStageList(pr.stages, false);
                ImGui::PopID();
            }
            if (removeProg >= 0) {
                project_.vu.programs.erase(project_.vu.programs.begin() +
                                           removeProg);
                commitChange();
            }

            ImGui::Spacing();
            if (ImGui::Button("Add program...")) ImGui::OpenPopup("addVuProg");
            if (ImGui::BeginPopup("addVuProg")) {
                for (const std::string& base : vugen::customBases()) {
                    bool taken = false;
                    for (const VuProgram& pr : project_.vu.programs)
                        if (pr.base == base) taken = true;
                    if (taken) continue;
                    const std::string item = std::string(
                        vugen::customBaseTitle(base)) + "##base" + base;
                    if (ImGui::Selectable(item.c_str())) {
                        VuProgram pr;
                        pr.base = base;
                        project_.vu.programs.push_back(pr);
                        commitChange();
                    }
                }
                ImGui::EndPopup();
            }
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
                        std::string(vugen::customBaseTitle(
                            project_.vu.programs[i].base)) +
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
