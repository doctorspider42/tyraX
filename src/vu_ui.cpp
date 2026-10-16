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
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <imgui.h>

#include "app.hpp"
#include "app_internal.hpp"
#include "platform.hpp"
#include "theme.hpp"
#include "toolkit.hpp"
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
    if (const fs::path binDir = platform::resourceBinDir(); !binDir.empty()) {
        const fs::path c = binDir / ".." / "vendor" / "tyra" / "engine";
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

/** Crossing-half instruction count with the built-in image aliases applied: an
 * image two classes share is uploaded ONCE (`Path1::createProgramsCache`), so
 * it is charged once.
 *
 * Which classes share what is NOT repeated here - it is read off the engine's
 * own descriptions (`vugen::engineDesc`), so the pairing lives in exactly one
 * place. It used to be spelled out as three `if (mask & ...)` lines, and a rule
 * duplicated between the generator and the bar that prices it is a rule that
 * gets changed in one of them.
 *
 * Note the aliased class charges its OWNER's body even when the owner's class
 * is not drawn at all: a project with lit meshes and no flat-coloured ones still
 * uploads the C image, because that image is what its D program IS. */
int engineCrossingInstructions(unsigned mask, int fam) {
    const vugen::Half half = fam == 1   ? vugen::Half::Clip
                             : fam == 2 ? vugen::Half::AsIs
                                        : vugen::Half::Cull;
    int total = 0;
    std::vector<std::string> charged;
    for (const ClassRow& c : kClasses) {
        if ((mask & c.bit) == 0) continue;
        const vugen::Desc d = vugen::engineDesc(c.bit, half);
        const std::string image = vugen::residentImage(d);
        if (std::find(charged.begin(), charged.end(), image) != charged.end())
            continue;  // a peer in the mask already paid for this image
        charged.push_back(image);
        int owner = classBitIndex(c.bit);
        if (!vugen::ownsResidentImage(d))
            for (int i = 0; i < 5; ++i)
                if (vugen::engineDesc(1u << i, half).asmName == image) owner = i;
        total += engineSizes().instr[owner][fam];
    }
    return total;
}

int engineResidentInstructions(unsigned mask, int fam) {
    int total = engineCrossingInstructions(mask, fam);
    for (const ClassRow& c : kClasses)
        if (mask & c.bit)
            total += engineSizes().instr[classBitIndex(c.bit)][0];
    return total;
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

namespace {

ImU32 vcol(const ImVec4& c, float a = 1.0f) {
    return ImGui::GetColorU32(ImVec4(c.x, c.y, c.z, c.w * a));
}

ImVec4 vmix(const ImVec4& a, const ImVec4& b, float t) {
    return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t,
                  a.w + (b.w - a.w) * t);
}

// One colour per material class, from the theme's meanings - the budget bar,
// its legend and the Applies-to chips use the same one.
ImVec4 classColour(int ci) {
    const theme::Semantics& s = theme::semantics();
    switch (ci) {
        case 0: return s.accent;
        case 1: return vmix(s.accent, s.ok, 0.55f);
        case 2: return s.warn;
        case 3: return s.ok;
        default: return s.special;
    }
}

// A VU script row, read from the manifest the last build wrote.
struct ScriptRow {
    std::string script, cls;
    unsigned bit = 0;
    // 0 = cull, 1 = the VU1 clipper, 2 = as_is - the same order engineSizes()
    // uses, so the program a row REPLACES is just instr[ci][fam].
    int fam = 0;
    bool boot = true;
    std::string verdict;          // "ok" | "noop" | "" (older manifest)
    bool bootFromScript = false;  // the script overrides it
    int instrs = 0;
    int delta = 0;         // against the engine program it REPLACES
    bool resident = true;  // only two of the three ever are
};

// Everything the budget strip and the Micro memory page say, computed once a
// frame from the same engine sizes and the same generated looks.
struct VuBudget {
    unsigned needed = 0, mask = 0;
    int fam = 2;           // 1 = VU1 clipping, 2 = the EE clipper (as_is)
    int total = 0;         // resident instructions, this mode
    int other = 0;         // the same with the other clipping mode
    int clipTot = 0, asIsTot = 0;  // crossing halves, per mode
    int classCull[5] = {};  // cull half per resident class
    int programs = 0;       // looks + scripts over the engine's own
    std::vector<ScriptRow> scripts;
};

VuBudget computeVuBudget(const Project& p, const std::vector<vugen::Built>& looks) {
    VuBudget b;
    b.needed = project::vuNeededClasses(p);
    b.mask = project::vuResidentClasses(p);
    const unsigned mask = b.mask;
    // Which twin each class uploads alongside its cull program depends on the
    // clipping mode, and the clip family is far bigger - so the mode is part
    // of the budget, not a detail.
    const int fam = p.settings.clipping == "vu1" ? 1 : 2;
    b.fam = fam;
    int totEmitted = engineResidentInstructions(mask, fam);
    // A custom program REPLACES its class's cull half, so only the difference
    // against the engine's own is new weight.
    for (const vugen::Built& l : looks) totEmitted += l.stageInstrs, b.programs += l.stageInstrs;
    // ... and the same for the project's own C++ scripts. Those are built
    // inside the BUILD CONTAINER - the editor has no compiler to run them
    // with - so what is read here is the manifest the last build left behind.
    std::ifstream mf(std::filesystem::path(p.dir) / "src" / "gen" / "vu_scripts.manifest");
    std::string line;
    while (std::getline(mf, line)) {
        std::vector<std::string> f;
        for (size_t at = 0;;) {
            const size_t tab = line.find('\t', at);
            f.push_back(line.substr(at, tab == std::string::npos ? std::string::npos : tab - at));
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
        r.verdict = f.size() > 7 ? f[7] : "";
        r.bootFromScript = f.size() > 8 && f[8] == "script";
        // THE SWITCH IS LIVE, the manifest is from the last build. The only
        // thing the manifest is authoritative about is WHO decides, so when
        // the panel decides, the panel's current value replaces the stale one
        // (else the estimate ignores a click until the next Build).
        if (!r.bootFromScript)
            for (const auto& e : p.vu.scriptBoot)
                if (e.first == r.script) r.boot = e.second;
        const int ci = classBitIndex(r.bit);
        // A class is THREE programs and only two are resident: the cull half,
        // plus whichever clipping twin the mode selects.
        const bool sharedClip = r.fam == 1 && r.bit != (1u << 2);
        r.delta = sharedClip ? r.instrs : r.instrs - engineSizes().instr[ci][r.fam];
        r.resident = r.fam == 0 || r.fam == fam;
        b.scripts.push_back(r);
    }
    unsigned customClipMask = 0;
    for (const ScriptRow& r : b.scripts)
        if (r.boot && r.resident && r.fam == 1 && (mask & r.bit)) customClipMask |= r.bit;
    // If every active member of a shared pair is overridden, its built-in
    // image is no longer resident.
    const unsigned cd = mask & ((1u << 0) | (1u << 1));
    if (fam == 1 && cd != 0 && (cd & ~customClipMask) == 0) totEmitted -= engineSizes().instr[0][1];
    const unsigned tctce = mask & ((1u << 3) | (1u << 4));
    if (fam == 1 && tctce != 0 && (tctce & ~customClipMask) == 0)
        totEmitted -= engineSizes().instr[3][1];
    for (const ScriptRow& r : b.scripts)
        if (r.boot && r.resident && (mask & r.bit)) totEmitted += r.delta, b.programs += r.delta;
    b.total = totEmitted;

    // The OTHER clipping mode, priced: the easiest way to blow the ceiling by
    // accident (a console run flipped vu-lab to VU1 clipping and hit the
    // engine's own overflow assert).
    const int otherFam = fam == 1 ? 2 : 1;
    int other = engineResidentInstructions(mask, otherFam);
    if (otherFam == 1) {
        unsigned ocm = 0;
        for (const ScriptRow& r : b.scripts)
            if (r.boot && r.fam == 1 && (mask & r.bit)) ocm |= r.bit;
        if (cd != 0 && (cd & ~ocm) == 0) other -= engineSizes().instr[0][1];
        if (tctce != 0 && (tctce & ~ocm) == 0) other -= engineSizes().instr[3][1];
    }
    for (const vugen::Built& l : looks) other += l.stageInstrs;
    for (const ScriptRow& r : b.scripts) {
        if (!r.boot || !(mask & r.bit)) continue;
        if (r.fam != 0 && r.fam != otherFam) continue;
        const int ci = classBitIndex(r.bit);
        other += r.instrs - engineSizes().instr[ci][r.fam];
    }
    b.other = other;
    b.clipTot = engineCrossingInstructions(mask, 1);
    b.asIsTot = engineCrossingInstructions(mask, 2);
    for (int i = 0; i < 5; ++i)
        if (mask & (1u << i)) b.classCull[i] = engineSizes().instr[i][0];
    return b;
}

// A small caps label drawn inline (no hairline), for the strip's title.
void smallCaps(const char* text) {
    const theme::Semantics& s = theme::semantics();
    ui::FontScope bold(ui::boldFont());
    ImGui::PushFont(nullptr, ImGui::GetFontSize() * 0.8f);
    char up[96];
    std::snprintf(up, sizeof up, "%s", text);
    for (char* c = up; *c; ++c)
        if (*c >= 'a' && *c <= 'z') *c = (char)(*c - 32);
    ImGui::PushStyleColor(ImGuiCol_Text, s.textDim);
    ImGui::TextUnformatted(up);
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

// A dashed rectangle (the "Add stage" row): draw-list has no dash style.
void dashedRect(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 c) {
    const float d = 6.0f, g = 4.0f;
    for (float x = a.x; x < b.x; x += d + g) {
        const float e = std::min(x + d, b.x);
        dl->AddLine(ImVec2(x, a.y), ImVec2(e, a.y), c);
        dl->AddLine(ImVec2(x, b.y), ImVec2(e, b.y), c);
    }
    for (float y = a.y; y < b.y; y += d + g) {
        const float e = std::min(y + d, b.y);
        dl->AddLine(ImVec2(a.x, y), ImVec2(a.x, e), c);
        dl->AddLine(ImVec2(b.x, y), ImVec2(b.x, e), c);
    }
}

}  // namespace

// The stages of a look (or the kernel) as ordered cards: index, name, slot
// badge, instruction estimate, enable switch; parameters as label-left
// sliders with their binding ("every mesh" / "mesh slot X") or "baked".
void App::drawVuStageList(std::vector<VuStage>& stages, bool kernel) {
    const theme::Semantics& sem = theme::semantics();
    const float fs = ImGui::GetFontSize();
    int remove = -1, moveUp = -1, moveDown = -1;
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
        if (ui::BeginCard("##stagecard", ImVec2(0.0f, 0.0f))) {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            // --- header row
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%d", (int)i + 1);
            ImGui::SameLine(0.0f, fs * 0.7f);
            {
                ui::FontScope bold(ui::boldFont());
                ImGui::PushStyleColor(ImGuiCol_Text, dead ? sem.textDim : sem.text);
                ImGui::TextUnformatted(def ? def->title : (s.kind + " (unknown)").c_str());
                ImGui::PopStyleColor();
            }
            if (dead && ImGui::IsItemHovered())
                ImGui::SetTooltip(!s.enabled
                                      ? "Disabled - not generated at all."
                                      : "Every strength is a literal zero, so this stage is\n"
                                        "not generated at all. Bind one to a mesh slot, or\n"
                                        "type a value, to make it real.");
            else if (def && ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos(fs * 26.0f);
                ImGui::TextUnformatted(def->desc);
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
            if (def) {
                ImGui::SameLine(0.0f, fs * 0.6f);
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImGui::GetStyle().FramePadding.y);
                ui::Badge(vugen::slotName(def->slot),
                          def->slot == vugen::Slot::Color     ? 3
                          : def->slot == vugen::Slot::Texture ? 0
                                                              : 4);
                if (def->needsTexture && !kernel) {
                    ImGui::SameLine(0.0f, fs * 0.5f);
                    ImGui::TextDisabled("only Textured and Reflective meshes run it");
                }
            }
            // right side: ~N instr, switch, more
            const float right = ImGui::GetWindowContentRegionMax().x;
            const float moreW = ImGui::GetFrameHeight();
            const float swW = ImGui::GetFontSize() * 1.9f;
            char ins[32];
            std::snprintf(ins, sizeof ins, "~%d instr", def ? def->perVertex : 0);
            const float iw = ImGui::CalcTextSize(ins).x;
            ImGui::SameLine(std::max(ImGui::GetCursorPosX(), right - moreW - swW - iw - fs * 1.2f));
            ImGui::TextDisabled("%s", ins);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Emitted instructions per vertex, measured by building the\n"
                                  "stage alone - a budget number, not a cycle count.");
            ImGui::SameLine(right - moreW - swW - fs * 0.4f);
            // The return value, not IsItemDeactivatedAfterEdit - a switch is
            // edited and deactivated in the SAME frame.
            if (ui::Switch("##Enabled", &s.enabled)) commitChange();
            ImGui::SameLine(right - moreW);
            if (ui::MoreButton("Stage menu", moreW)) ImGui::OpenPopup("##stagemenu");
            if (ImGui::BeginPopup("##stagemenu")) {
                if (ImGui::MenuItem("Move up", nullptr, false, i > 0)) moveUp = (int)i;
                if (ImGui::MenuItem("Move down", nullptr, false, i + 1 < stages.size()))
                    moveDown = (int)i;
                ImGui::Separator();
                if (ImGui::MenuItem("Remove stage")) remove = (int)i;
                ImGui::EndPopup();
            }

            // --- parameters, label-left
            const float capW = fs * 7.0f;
            const float bindW = kernel ? 0.0f : fs * 7.5f;
            for (int k = 0; def && k < def->paramCount; ++k) {
                ImGui::PushID(k);
                const vugen::StageParamDef& pd = def->params[k];
                const bool bound = s.bind[k] >= 0;
                ImGui::AlignTextToFramePadding();
                ImGui::TextDisabled("%s", pd.name);
                paramTip(pd.tip);
                ImGui::SameLine(capW);
                const float avail = ImGui::GetContentRegionAvail().x;
                const float valW = fs * 3.6f;
                const float sliderW = std::max(fs * 4.0f, avail - valW - bindW - fs * 1.0f);
                if (bound) {
                    ImGui::PushStyleColor(ImGuiCol_Text, sem.accent);
                    ImGui::Text("from mesh slot %s", bindLabel(s.bind[k]) + 5);
                    ImGui::PopStyleColor();
                    ImGui::SameLine(capW + sliderW + valW + fs * 0.6f);
                } else {
                    float v = s.params[k];
                    // The slider's own label is clipped: the caption names it.
                    const ImVec2 p = ImGui::GetCursorScreenPos();
                    ImGui::PushClipRect(p, ImVec2(p.x + sliderW, p.y + ImGui::GetFrameHeight()), true);
                    ImGui::SetNextItemWidth(sliderW);
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 0, 0, 0));
                    if (ImGui::SliderFloat(pd.name, &v, pd.lo, pd.hi, "%.3f")) {
                        s.params[k] = v;
                        setDirty(true);
                    }
                    ImGui::PopStyleColor();
                    const bool done = ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::PopClipRect();
                    if (done) commitChange();
                    paramTip(pd.tip);
                    ImGui::SameLine(capW + sliderW + fs * 0.4f);
                    ui::FontScope mono(ui::monoFont());
                    ImGui::Text("%.2f", s.params[k]);
                    ImGui::SameLine(capW + sliderW + valW + fs * 0.6f);
                }
                if (!pd.literalOnly && !kernel) {
                    ImGui::SetNextItemWidth(bindW);
                    int b = s.bind[k] + 1;
                    const char* items[] = {"every mesh", "mesh slot X", "mesh slot Y", "mesh slot Z",
                                           "mesh slot W"};
                    if (ImGui::Combo("##bind", &b, items, 5)) {
                        s.bind[k] = b - 1;
                        commitChange();
                    }
                    paramTip(
                        "Every mesh: one baked number for every mesh of the ticked\n"
                        "classes - the normal case. A mesh slot takes it from the\n"
                        "object's own VU parameters instead, so the strength is per\n"
                        "mesh, and a mesh that leaves it at zero renders exactly as\n"
                        "it would without the program at all.\n\n"
                        "Batched objects share a bag, and therefore share these.");
                } else if (pd.literalOnly) {
                    ImGui::TextDisabled("baked");
                    paramTip(
                        "Folded into the microprogram at build time (the\n"
                        "generator computes a reciprocal from it), so it cannot\n"
                        "come from a mesh.");
                } else {
                    ImGui::NewLine();
                }
                ImGui::PopID();
            }
            (void)dl;
        }
        ui::EndCard();
        ImGui::PopID();
    }
    if (moveUp > 0) {
        std::swap(stages[moveUp], stages[moveUp - 1]);
        commitChange();
    }
    if (moveDown >= 0 && moveDown + 1 < (int)stages.size()) {
        std::swap(stages[moveDown], stages[moveDown + 1]);
        commitChange();
    }
    if (remove >= 0) {
        stages.erase(stages.begin() + remove);
        commitChange();
    }

    // The dashed "Add stage" row names what there is to add.
    {
        std::string hint = "Add stage - ";
        int n = 0;
        for (const vugen::StageDef& d : vugen::stageDefs()) {
            if (kernel && !d.kernelSafe) continue;
            if (n++ == 4) {
                hint += "...";
                break;
            }
            if (n > 1) hint += ", ";
            hint += d.title;
        }
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float w = ImGui::GetContentRegionAvail().x, h = fs * 2.4f;
        if (ImGui::InvisibleButton("Add stage...", ImVec2(std::max(1.0f, w), h)))
            ImGui::OpenPopup("addVuStage");
        const bool hov = ImGui::IsItemHovered();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (hov) dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), vcol(sem.surface, 0.6f), 4.0f);
        dashedRect(dl, p, ImVec2(p.x + w, p.y + h), vcol(sem.textDim, hov ? 0.8f : 0.45f));
        const std::string t = "+  " + hint;
        const ImVec2 ts = ImGui::CalcTextSize(t.c_str());
        dl->PushClipRect(p, ImVec2(p.x + w, p.y + h), true);
        dl->AddText(ImVec2(p.x + std::max(fs * 0.6f, (w - ts.x) * 0.5f), p.y + (h - ts.y) * 0.5f),
                    vcol(hov ? sem.text : sem.textDim), t.c_str());
        dl->PopClipRect();
    }
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
            const std::string item = std::string(d.title) + "##add" + d.key;
            if (ImGui::Selectable(item.c_str())) {
                VuStage s;
                s.kind = d.key;
                for (int i = 0; i < d.paramCount; ++i) s.params[i] = d.params[i].def;
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
    ImGui::SetNextWindowSize(ImVec2(scaled(1200), scaled(800)), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("VU Programs", &showVuPrograms_)) {
        ImGui::End();
        return;
    }
    vuRebuildPreview();
    const VuBudget bud = computeVuBudget(project_, vuPreview_);
    const theme::Semantics& sem = theme::semantics();
    const float fs = ImGui::GetFontSize();
    const float gap = ImGui::GetStyle().ItemSpacing.x;

    // ------------------------------------------------ the budget, always on top
    if (ui::BeginCard("##vubudget", ImVec2(0.0f, 0.0f))) {
        const int lo = (bud.total + 1) / 2, hi = bud.total;
        const bool fits = hi <= 2042, otherFits = bud.other <= 2042;
        ImGui::AlignTextToFramePadding();
        smallCaps("VU1 micro memory");
        ImGui::SameLine(0.0f, fs * 0.8f);
        {
            ui::FontScope mono(ui::monoFont());
            ImGui::PushFont(nullptr, fs * 1.3f);
            ImGui::PushStyleColor(ImGuiCol_Text, lo > 2042 ? sem.danger : sem.text);
            ImGui::Text("%d-%d", lo, hi);
            ImGui::PopStyleColor();
            ImGui::PopFont();
        }
        ImGui::SameLine(0.0f, fs * 0.5f);
        ImGui::TextDisabled("of 2042 slots");
        ImGui::SameLine(0.0f, fs * 1.0f);
        ImGui::TextDisabled("a range: VCL pairs upper and lower ops when it can");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "VU1 holds 2048 instruction slots and Path1 parks the draw-finish\n"
                "helper in the top six, so a pipeline set has 2042. Overrunning it in\n"
                "a RELEASE build overwrites that helper and hangs the post-effect\n"
                "barrier forever - the engine's own guard is an assert that release\n"
                "compiles out. The exact size is only known after VCL runs.");
        // The clipping choice, priced both ways, at the right end.
        {
            char a[48], b[48];
            std::snprintf(a, sizeof a, "Clip on EE  %d###Clip on EE", bud.asIsTot);
            std::snprintf(b, sizeof b, "Clip on VU1  %d###Clip on VU1", bud.clipTot);
            const char* opts[2] = {a, b};
            int cur = project_.settings.clipping == "vu1" ? 1 : 0;
            const float w = ImGui::CalcTextSize(a).x + ImGui::CalcTextSize(b).x + fs * 3.0f;
            ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - w));
            if (ui::Segmented("##clipmode", opts, 2, &cur, w)) {
                project_.settings.clipping = cur == 1 ? "vu1" : "precise";
                commitChange();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Every material class is a PAIR: its cull program plus the twin\n"
                    "that draws whatever the frustum cut. This picks the twin - the VU1\n"
                    "clipper, precise and about twice the size, or the as_is program\n"
                    "with the EE doing the cutting. The game can flip it while it runs\n"
                    "with vuprog::setVU1Clipping().");
        }

        // The bar: stacked by what occupies it, solid to the low estimate,
        // hatched up to the high one, and Path1's six slots as a red tick.
        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float w = ImGui::GetContentRegionAvail().x, h = fs * 0.75f;
            const float r = h * 0.35f;
            dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), vcol(sem.field), r);
            const float scale = w / 2048.0f;
            const float solid = hi > 0 ? (float)lo / (float)hi : 0.0f;
            int classSum = 0;
            for (int i = 0; i < 5; ++i) classSum += bud.classCull[i];
            const int clipper = std::max(0, hi - classSum - bud.programs);
            float x = p.x;
            auto seg = [&](int n, const ImVec4& c) {
                if (n <= 0) return;
                const float sw = std::min((float)n * solid * scale, p.x + w - x);
                if (sw <= 0.0f) return;
                dl->AddRectFilled(ImVec2(x, p.y), ImVec2(x + sw, p.y + h), vcol(c));
                x += sw;
            };
            seg(clipper, sem.textDim);
            for (int i = 0; i < 5; ++i) seg(bud.classCull[i], classColour(i));
            seg(std::max(0, bud.programs), sem.text);
            const float hx = std::min(p.x + w, p.x + (float)hi * scale);
            dl->PushClipRect(ImVec2(x, p.y), ImVec2(hx, p.y + h), true);
            for (float hxx = x - h; hxx < hx; hxx += h * 0.7f)
                dl->AddLine(ImVec2(hxx, p.y + h), ImVec2(hxx + h, p.y),
                            vcol(fits ? sem.accent : sem.danger, 0.55f), 2.0f);
            dl->PopClipRect();
            const float tick = p.x + 2042.0f * scale;
            dl->AddLine(ImVec2(tick, p.y - 2.0f), ImVec2(tick, p.y + h + 2.0f), vcol(sem.danger), 2.0f);
            ImGui::Dummy(ImVec2(w, h));
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%d..%d of 2042: the clipper/twins %d, the class programs %d, your\n"
                                  "looks and scripts %+d over the engine's own. The red tick is the\n"
                                  "6-slot Path1 reserve.",
                                  lo, hi, clipper, classSum, bud.programs);
        }

        // Legend: each resident class with its range; what is left out; verdict.
        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            auto dot = [&](const ImVec4& c) {
                const ImVec2 q = ImGui::GetCursorScreenPos();
                dl->AddRectFilled(ImVec2(q.x, q.y + fs * 0.3f), ImVec2(q.x + fs * 0.5f, q.y + fs * 0.8f),
                                  vcol(c), 2.0f);
                ImGui::Dummy(ImVec2(fs * 0.5f, fs));
                ImGui::SameLine(0.0f, fs * 0.35f);
            };
            dot(sem.textDim);
            ImGui::TextDisabled("clipper");
            std::string notDrawn;
            for (int i = 0; i < 5; ++i) {
                const ClassRow& c = kClasses[i];
                if (!(bud.mask & c.bit)) {
                    if (!notDrawn.empty()) notDrawn += ", ";
                    notDrawn += c.label;
                    continue;
                }
                const int e = bud.classCull[i] + engineSizes().instr[i][bud.fam];
                ImGui::SameLine(0.0f, fs * 0.9f);
                dot(classColour(i));
                ImGui::TextDisabled("%s %d-%d", c.label, (e + 1) / 2, e);
            }
            if (bud.programs != 0) {
                ImGui::SameLine(0.0f, fs * 0.9f);
                dot(sem.text);
                ImGui::TextDisabled("yours %+d", bud.programs);
            }
            if (!notDrawn.empty()) {
                ImGui::SameLine(0.0f, fs * 0.9f);
                ImGui::TextDisabled("%s not resident \xc2\xb7 %s", notDrawn.c_str(),
                                    project_.vu.residentAuto ? "detected automatically" : "set by hand");
            }
            const char* verdict = !fits ? "DOES NOT FIT - the engine asserts"
                                  : otherFits ? "fits either way"
                                              : "fits - the other clipping mode does not";
            const float vw = ImGui::CalcTextSize(verdict).x;
            ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - vw));
            ImGui::PushStyleColor(ImGuiCol_Text, !fits ? sem.danger : otherFits ? sem.ok : sem.warn);
            ImGui::TextUnformatted(verdict);
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("With %s instead: %d..%d of 2042. The game can switch at run\n"
                                  "time with vuprog::setVU1Clipping(), between frames.",
                                  bud.fam == 1 ? "the EE clipper" : "VU1 clipping",
                                  (bud.other + 1) / 2, bud.other);
        }
    }
    ui::EndCard();

    // ------------------------------------------------ list | editor | check
    const float paneH = ImGui::GetContentRegionAvail().y;
    const float listW = std::max(fs * 13.0f, std::min(fs * 17.0f, ImGui::GetContentRegionAvail().x * 0.22f));
    if (vuSelKind_ == 0 && vuSelLook_ >= (int)project_.vu.programs.size())
        vuSelLook_ = (int)project_.vu.programs.size() - 1;
    if (vuSelKind_ == 0 && vuSelLook_ < 0) vuSelKind_ = 2;
    const bool showCheck = vuSelKind_ == 0;
    const float checkW = showCheck ? std::max(fs * 18.0f, ImGui::GetContentRegionAvail().x * 0.28f) : 0.0f;

    if (ui::BeginCard("##vulist", ImVec2(listW, paneH))) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        // A two-line row: name, dim sub line, badge at the right.
        auto row = [&](const char* id, const char* name, const char* sub, bool sel, const char* badge,
                       int badgeKind) {
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float w = ImGui::GetContentRegionAvail().x, h = fs * 2.5f;
            const bool clicked = ImGui::InvisibleButton(id, ImVec2(std::max(1.0f, w), h));
            uiscript::markLastItemChecked(sel);
            const bool hov = ImGui::IsItemHovered();
            if (sel) {
                dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), vcol(sem.accent, 0.16f), 4.0f);
                dl->AddRect(p, ImVec2(p.x + w, p.y + h), vcol(sem.accent, 0.3f), 4.0f);
            } else if (hov) {
                dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), vcol(sem.field), 4.0f);
            }
            float bw = 0.0f;
            if (badge) {
                bw = ImGui::CalcTextSize(badge).x * 0.82f + fs;
                const ImVec2 back = ImGui::GetCursorScreenPos();
                ImGui::SetCursorScreenPos(ImVec2(p.x + w - bw - fs * 0.3f, p.y + fs * 0.35f));
                ui::Badge(badge, badgeKind);
                ImGui::SetCursorScreenPos(back);
            }
            dl->PushClipRect(p, ImVec2(p.x + w - bw - fs * 0.5f, p.y + h), true);
            dl->AddText(ImVec2(p.x + fs * 0.5f, p.y + fs * 0.3f), vcol(sem.text), name);
            dl->AddText(ImGui::GetFont(), fs * 0.86f, ImVec2(p.x + fs * 0.5f, p.y + fs * 1.35f),
                        vcol(sem.textDim), sub);
            dl->PopClipRect();
            return clicked;
        };

        {
            char cnt[16];
            std::snprintf(cnt, sizeof cnt, "%d", (int)project_.vu.programs.size());
            ui::Caption("Looks", cnt);
        }
        const unsigned needed = bud.needed;
        for (size_t i = 0; i < project_.vu.programs.size(); ++i) {
            const VuProgram& pr = project_.vu.programs[i];
            int ncls = 0;
            for (const ClassRow& c : kClasses) ncls += (pr.classes & c.bit) ? 1 : 0;
            char sub[96];
            std::snprintf(sub, sizeof sub, "%d stage%s \xc2\xb7 %s", (int)pr.stages.size(),
                          pr.stages.size() == 1 ? "" : "s",
                          project::vuLookBindsPerMesh(pr) ? "per-mesh slot"
                          : ncls == 5                    ? "all 5 classes"
                                                         : (std::to_string(ncls) + " classes").c_str());
            const bool active = pr.enabled && project_.vu.activeLook == (int)i;
            char id[96];
            std::snprintf(id, sizeof id, "%s##look%zu", pr.name.c_str(), i);
            if (row(id, pr.name.c_str(), sub, vuSelKind_ == 0 && vuSelLook_ == (int)i,
                    !pr.enabled ? "not built" : active ? "active" : "built",
                    !pr.enabled ? 4 : active ? 0 : 3)) {
                vuSelKind_ = 0;
                vuSelLook_ = (int)i;
            }
        }
        if (ui::GhostButton("+ Add a look", -1.0f)) {
            VuProgram pr;
            pr.name = "look";
            // Everything the project actually draws: a treatment is
            // scene-wide by default, and narrowing it should be a decision.
            pr.classes = needed;
            project_.vu.programs.push_back(pr);
            vuSelKind_ = 0;
            vuSelLook_ = (int)project_.vu.programs.size() - 1;
            commitChange();
        }
        paramTip("A look is ONE stage list installed over every material class you tick.\n"
                 "It starts covering everything this project draws, which is what a\n"
                 "scene-wide treatment - cell shading, an underwater wobble - wants.");

        ImGui::Spacing();
        ui::Caption("VU0 kernel");
        {
            const VuKernel& k = project_.vu.kernel;
            char sub[96];
            std::snprintf(sub, sizeof sub, "%d stage%s \xc2\xb7 %d max", (int)k.stages.size(),
                          k.stages.size() == 1 ? "" : "s", k.maxElements);
            if (row("VU0 kernel##row", k.name.c_str(), sub, vuSelKind_ == 1, k.enabled ? "on" : "off",
                    k.enabled ? 0 : 4))
                vuSelKind_ = 1;
        }

        ImGui::Spacing();
        ui::Caption("Scripts", "src/vu");
        if (bud.scripts.empty()) {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextDisabled("%s", project::hasVuScripts(project_)
                                          ? "Not built yet - Build and these fill in."
                                          : "None. New script... in the Scripts panel.");
            ImGui::PopTextWrapPos();
        }
        for (size_t i = 0; i < bud.scripts.size();) {
            const std::string& name = bud.scripts[i].script;
            size_t j = i;
            int residentDelta = 0;
            while (j < bud.scripts.size() && bud.scripts[j].script == name) {
                if (bud.scripts[j].resident && (bud.mask & bud.scripts[j].bit))
                    residentDelta += bud.scripts[j].delta;
                ++j;
            }
            const bool fromScript = bud.scripts[i].bootFromScript;
            bool on = bud.scripts[i].boot;
            // Keyed by NAME, not by row index: the state is stored against the
            // script's name, and a positional id moves to another script the
            // moment a file is added.
            char box[192];
            std::snprintf(box, sizeof box, "##vuboot_%s", name.c_str());
            ImGui::BeginDisabled(fromScript);
            if (ui::Switch(box, &on) && !fromScript) {
                bool found = false;
                for (auto& e : project_.vu.scriptBoot)
                    if (e.first == name) e.second = on, found = true;
                if (!found) project_.vu.scriptBoot.push_back({name, on});
                commitChange();
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip(fromScript
                                      ? "src/vu/*.cpp decides this one - it overrides\n"
                                        "activeAtBoot(). Delete that override to use this switch."
                                      : "On at boot. Only what is ACTIVE occupies micro\n"
                                        "memory. Takes effect on the next Build.");
            ImGui::SameLine(0.0f, fs * 0.5f);
            char lbl[192];
            std::snprintf(lbl, sizeof lbl, "%s##vuscript%zu", name.c_str(), i);
            const float dw = fs * 4.0f;
            if (ImGui::Selectable(lbl, false, 0,
                                  ImVec2(std::max(1.0f, ImGui::GetContentRegionAvail().x - dw), 0)))
                vuSelKind_ = 2;
            ImGui::SameLine();
            ui::FontScope mono(ui::monoFont());
            ImGui::TextDisabled("%+d", residentDelta);
            i = j;
        }

        ImGui::Spacing();
        ui::Caption("Budget");
        if (row("Micro memory##row", "Micro memory", "classes, scripts, the other VUs",
                vuSelKind_ == 2, nullptr, 0))
            vuSelKind_ = 2;
    }
    ui::EndCard();

    ImGui::SameLine(0.0f, gap);
    const float centreW = ImGui::GetContentRegionAvail().x - (showCheck ? checkW + gap : 0.0f);
    if (ui::BeginCard("##vucentre", ImVec2(centreW, paneH))) {
        if (vuSelKind_ == 0) drawVuLook(vuSelLook_);
        else if (vuSelKind_ == 1) drawVuKernel();
        else drawVuMicroMemory(bud.mask, bud.needed, bud.fam);
    }
    ui::EndCard();

    if (showCheck) {
        ImGui::SameLine(0.0f, gap);
        if (ui::BeginCard("##vucheck", ImVec2(0.0f, paneH))) drawVuCheck(vuSelLook_);
        ui::EndCard();
    }
    ImGui::End();
}

// One look: name, Build into the game, Active at boot, the not-built note,
// Applies to as class chips with object counts, then the stage cards.
void App::drawVuLook(int li) {
    const theme::Semantics& sem = theme::semantics();
    const float fs = ImGui::GetFontSize();
    if (li < 0 || li >= (int)project_.vu.programs.size()) {
        if (ui::EmptyState(ui::Glyph::Code, "No looks yet",
                           "A look is a whole-scene vertex treatment - posterize, scroll UV, "
                           "wobble - that runs on VU1 for every mesh of the classes it claims.",
                           "Add a look")) {
            VuProgram pr;
            pr.classes = project::vuNeededClasses(project_);
            project_.vu.programs.push_back(pr);
            vuSelLook_ = 0;
            commitChange();
        }
        return;
    }
    VuProgram& pr = project_.vu.programs[(size_t)li];
    ImGui::PushID(1000 + li);

    // --- header: name, the two switches, overflow
    {
        const float right = ImGui::GetWindowContentRegionMax().x;
        const float moreW = ImGui::GetFrameHeight();
        const float sw = ImGui::CalcTextSize("Build into the game").x +
                         ImGui::CalcTextSize("Active at boot").x + fs * 7.5f;
        const float nameW = std::max(fs * 6.0f, right - ImGui::GetCursorPosX() - sw - moreW - fs);
        char nameBuf[64];
        std::snprintf(nameBuf, sizeof nameBuf, "%s", pr.name.c_str());
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::PushClipRect(p, ImVec2(p.x + nameW, p.y + fs * 2.5f), true);
        ImGui::SetNextItemWidth(nameW);
        ImGui::PushFont(ui::boldFont(), fs * 1.3f);
        if (ImGui::InputText("Name", nameBuf, sizeof nameBuf)) pr.name = nameBuf;
        const bool nameDone = ImGui::IsItemDeactivatedAfterEdit();
        ImGui::PopFont();
        ImGui::PopClipRect();
        if (nameDone) commitChange();
        ImGui::SameLine(right - sw - moreW);
        bool en = pr.enabled;
        if (ui::Switch("Build into the game", &en)) {
            pr.enabled = en;
            commitChange();
        }
        ImGui::SameLine(0.0f, fs);
        // Which look the game boots into. Every built look is in the ELF and
        // vuprog::activate(i) swaps them at run time, so this is only the
        // starting one - a switch that can be turned ON (one look is always
        // the boot look).
        ImGui::BeginDisabled(!pr.enabled);
        bool boot = pr.enabled && project_.vu.activeLook == li;
        if (ui::Switch("Active at boot", &boot) && boot) {
            project_.vu.activeLook = li;
            commitChange();
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip(pr.enabled ? "The look install() activates. The game can swap to any other\n"
                                           "with vuprog::activate(i) - see docs/vu-authoring.md."
                                         : "Turn on Build into the game first.");
        ImGui::SameLine(right - moreW);
        if (ui::MoreButton("Look menu", moreW)) ImGui::OpenPopup("##lookmenu");
        if (ImGui::BeginPopup("##lookmenu")) {
            if (ImGui::MenuItem("Duplicate")) {
                VuProgram copy = pr;
                copy.name += " copy";
                project_.vu.programs.push_back(copy);
                commitChange();
                ImGui::EndPopup();
                ImGui::PopID();
                return;
            }
            if (ImGui::MenuItem("Remove look")) {
                project_.vu.programs.erase(project_.vu.programs.begin() + li);
                // The boot look is an INDEX, so removing anything before it
                // moves it; removing the boot look itself falls back to the first.
                if (project_.vu.activeLook > li) --project_.vu.activeLook;
                else if (project_.vu.activeLook == li) project_.vu.activeLook = 0;
                commitChange();
                ImGui::EndPopup();
                ImGui::PopID();
                return;
            }
            ImGui::EndPopup();
        }
    }

    // --- what "not built" means, said once, with the way out
    auto note = [&](const ImVec4& c, const char* text, const char* tip) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(c.x, c.y, c.z, 0.10f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(c.x, c.y, c.z, 0.35f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 5.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(fs * 0.7f, fs * 0.5f));
        ImGui::BeginChild(ImGui::GetID(text), ImVec2(0, 0),
                          ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY |
                              ImGuiChildFlags_AlwaysUseWindowPadding);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(2);
        if (tip && ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(fs * 30.0f);
            ImGui::TextUnformatted(tip);
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
    };
    if (!pr.enabled)
        note(sem.accent,
             "Not built yet - the game draws without it. Turn on Build into the game, then make "
             "it active at boot or switch to it with vuprog::activate().",
             nullptr);
    // A material class is TWO resident programs and a look replaces both -
    // but in VU1-clipping mode the second is the CLIPPER, which has no
    // generated twin, so anything the frustum cuts keeps the engine's shading.
    if (pr.enabled && project_.settings.clipping == "vu1")
        note(sem.warn, "Clip on VU1 is on: meshes the frustum cuts keep the engine's own shading.",
             "A look replaces a class's cull program AND its as_is twin, but not the VU1 CLIPPER - "
             "so a mesh at the edge of the screen renders with the engine's own program and no "
             "look. Pick Clip on EE above for a look that covers every pixel.");
    if (pr.enabled && project::vuLookMovesGeometry(pr))
        note(sem.warn, "This look moves geometry: keep the amplitude small, and multi-pass props out.",
             "That stage cannot run on a package the frustum cut - those vertices are already "
             "transformed - and classification is per PACKAGE, so a mesh straddling the EDGE OF THE "
             "SCREEN is displaced only in part. It also moves only the passes whose class this look "
             "claims: a matcap prop ripples while its reflection pass stays put, and the same goes "
             "for a baked lightmap. Colour and texture stages have neither problem; "
             "vuprog::movesGeometry() reports this at run time.");
    for (const std::string& e : vuPreviewErrors_)
        if (e.compare(0, pr.name.size() + 1, pr.name + ":") == 0 ||
            e.compare(0, pr.name.size() + 3, pr.name + " - ") == 0) {
            ImGui::PushStyleColor(ImGuiCol_Text, sem.danger);
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(e.c_str());
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
        }

    // --- Applies to: class chips with object counts
    ImGui::Spacing();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("Applies to");
    {
        const unsigned needed = project::vuNeededClasses(project_);
        const bool binds = project::vuLookBindsPerMesh(pr);
        bool first = true;
        for (const ClassRow& c : kClasses) {
            ImGui::PushID((int)c.bit);
            const int n = vuObjectsInClass(c.bit);
            const bool drawn = (needed & c.bit) != 0;
            const bool blocked = binds && !project::vuClassCanBind(c.bit);
            const float w = ui::FilterChipWidth(c.label, n);
            if (first) ImGui::SameLine(0.0f, fs * 0.6f);
            else ui::ChipFlow(w, false);
            first = false;
            ImGui::BeginDisabled(blocked);
            if (!drawn) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
            if (ui::FilterChip(c.label, (pr.classes & c.bit) != 0, n)) {
                pr.classes ^= c.bit;
                commitChange();
            }
            if (!drawn) ImGui::PopStyleVar();
            ImGui::EndDisabled();
            if (blocked)
                paramTip("This look binds a parameter to a per-mesh slot, and those four numbers live "
                         "in the directional-lights colour block - which a lit class needs for its own "
                         "light colours. Make every parameter every mesh and this class opens up.");
            else if (!drawn)
                paramTip("No scene draws this class, so no program is generated for it.");
            else
                paramTip(c.what);
            ImGui::PopID();
        }
    }

    ImGui::Spacing();
    ui::Caption("Stages - run top to bottom");
    drawVuStageList(pr.stages, false);
    ImGui::PopID();
}

// The right column for a look: run the generated program on the host
// simulator, the verdict, the vertex table, and the generated .vclpp.
void App::drawVuCheck(int li) {
    const theme::Semantics& sem = theme::semantics();
    const float fs = ImGui::GetFontSize();
    // The preview entries of this look (one per class it generates for).
    std::vector<int> mine;
    if (li >= 0 && li < (int)project_.vu.programs.size())
        for (size_t i = 0; i < vuPreviewLabel_.size(); ++i)
            if (vuPreviewLabel_[i].compare(0, project_.vu.programs[(size_t)li].name.size() + 3,
                                           project_.vu.programs[(size_t)li].name + " - ") == 0)
                mine.push_back((int)i);
    {
        ui::FontScope bold(ui::boldFont());
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Check on the host");
    }
    if (mine.empty()) {
        ImGui::Spacing();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("Nothing is generated for this look yet: it needs Build into the game, "
                            "a live stage and a class the scenes draw.");
        ImGui::PopTextWrapPos();
        return;
    }
    if (std::find(mine.begin(), mine.end(), vuPreviewSel_) == mine.end()) {
        vuPreviewSel_ = mine.front();
        vuSimOut_.clear();
        vuSimRows_.clear();
    }
    const float bw = ImGui::CalcTextSize("Run it").x + ImGui::GetStyle().FramePadding.x * 2.6f;
    ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - bw);
    if (ui::PrimaryButton("Run it", bw)) vuSimulate();
    paramTip("Runs this exact microprogram in the host VU simulator over a small\n"
             "synthetic mesh, twice: with the mesh values below and with them at\n"
             "zero, so the two can be compared. No Docker, no console.");

    // Which class's program, when the look generates more than one.
    if (mine.size() > 1) {
        const std::string& cur = vuPreviewLabel_[(size_t)vuPreviewSel_];
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##vucls", cur.c_str())) {
            for (int i : mine)
                if (ImGui::Selectable(vuPreviewLabel_[(size_t)i].c_str(), i == vuPreviewSel_)) {
                    vuPreviewSel_ = i;
                    vuSimOut_.clear();
                    vuSimRows_.clear();
                }
            ImGui::EndCombo();
        }
    }
    {
        const float capW = fs * 4.0f;
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("mesh X");
        ImGui::SameLine(capW);
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::SliderFloat("##meshx", &vuSimParams_[0], -2.0f, 2.0f);
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("time");
        ImGui::SameLine(capW);
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::SliderFloat("##time", &vuSimTime_, 0.0f, 12.0f);
    }
    ImGui::Spacing();
    if (vuSimRan_) {
        const bool ok = vuSimErr_.empty() && vuSimQ_ == 0;
        const ImVec4 c = !vuSimErr_.empty() || vuSimQ_ > 0 ? sem.danger : sem.ok;
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(c.x, c.y, c.z, 0.10f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(c.x, c.y, c.z, 0.35f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(fs * 0.7f, fs * 0.5f));
        ImGui::BeginChild("##vuverdict", ImVec2(0, 0),
                          ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY |
                              ImGuiChildFlags_AlwaysUseWindowPadding);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, c);
        if (!vuSimErr_.empty())
            ImGui::TextUnformatted(vuSimErr_.c_str());
        else if (vuSimQ_ > 0)
            ImGui::Text("%d Q clobber(s): a div or rsqrt result was overwritten before anything "
                        "read it. A stage in the NDC slot must not write Q.",
                        vuSimQ_);
        else
            ImGui::TextUnformatted(vuSimSame_ ? "Ran on the host. With these values it draws the same "
                                                "vertices as at zero. No Q clobbers."
                                              : "Ran on the host. The values change the vertices; at "
                                                "zero the mesh is left as drawn. No Q clobbers.");
        ImGui::PopStyleColor();
        ImGui::PopTextWrapPos();
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);
        (void)ok;
        if (!vuSimRows_.empty() &&
            ImGui::BeginTable("##vusimrows", 3, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Vtx", ImGuiTableColumnFlags_WidthFixed, fs * 2.2f);
            ImGui::TableSetupColumn("With values");
            ImGui::TableSetupColumn("At zero");
            ImGui::TableHeadersRow();
            ui::FontScope mono(ui::monoFont());
            for (size_t r = 0; r + 7 <= vuSimRows_.size(); r += 7) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("%d", (int)(r / 7));
                ImGui::TableNextColumn();
                ImGui::Text("%d %d", vuSimRows_[r] >> 4, vuSimRows_[r + 1] >> 4);
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%d %d", vuSimRows_[r + 3] >> 4, vuSimRows_[r + 4] >> 4);
            }
            ImGui::EndTable();
            ImGui::TextDisabled("screen X Y in pixels (12.4 fixed point >> 4)");
        }
        if (!vuSimOut_.empty()) {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, sem.warn);
            ImGui::TextUnformatted(vuSimOut_.c_str());
            ImGui::PopStyleColor();
            ImGui::PopTextWrapPos();
        }
    }

    ImGui::Spacing();
    const vugen::Built& b = vuPreview_[(size_t)vuPreviewSel_];
    {
        const ImVec2 cp = ImGui::GetCursorPos();
        ui::Caption("Generated .vclpp");
        const ImVec2 after = ImGui::GetCursorPos();
        const float cw = ImGui::CalcTextSize("Copy").x + ImGui::GetStyle().FramePadding.x * 2.6f;
        ImGui::SetCursorPos(ImVec2(ImGui::GetWindowContentRegionMax().x - cw,
                                   cp.y - (ImGui::GetFrameHeight() - fs) * 0.5f));
        if (ui::GhostButton("Copy", cw)) ImGui::SetClipboardText(b.vclpp.c_str());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Copy the generated program.");
        ImGui::SetCursorPos(after);
    }
    ImGui::PushStyleColor(ImGuiCol_ChildBg, sem.field);
    ImGui::BeginChild("vclpp", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
    {
        ui::FontScope mono(ui::monoFont());
        ImGui::PushFont(nullptr, fs * 0.9f);
        ImGui::TextUnformatted(b.vclpp.c_str());
        ImGui::PopFont();
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

// The VU0 kernel page (the old tab, as a page of the list).
void App::drawVuKernel() {
    const float fs = ImGui::GetFontSize();
    VuKernel& k = project_.vu.kernel;
    {
        ui::FontScope bold(ui::boldFont());
        ImGui::TextUnformatted("VU0 kernel");
    }
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("The same stages on the other vector unit: N quadwords in, N out.");
    ImGui::PopTextWrapPos();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("VU0 has 256 quadwords of data memory TOTAL and 512 micro slots, and\n"
                          "its register file is shared with the engine's own Vec4/M4x4 math - so\n"
                          "the generated driver blocks the EE while the kernel runs.");
    ImGui::Spacing();
    {
        ui::InspectorRows rows(fs * 7.0f, fs * 9.0f);
        bool en = k.enabled;
        if (ui::Switch("Generate a VU0 kernel", &en)) {
            k.enabled = en;
            commitChange();
        }
        char name[64];
        std::snprintf(name, sizeof name, "%s", k.name.c_str());
        if (ui::InputText("Name", name, sizeof name)) k.name = name;
        if (ImGui::IsItemDeactivatedAfterEdit()) commitChange();
        paramTip("Names the generated files and the driver class.");
        if (ui::SliderInt("Batch size", &k.maxElements, 8, 112)) setDirty(true);
        if (ImGui::IsItemDeactivatedAfterEdit()) commitChange();
        paramTip("Elements per call. The input and output blocks both have to\n"
                 "fit VU0's 256 quadwords next to the parameters, which caps\n"
                 "this at 112.");
    }
    vugen::KernelDesc kd;
    kd.maxElements = k.maxElements;
    for (const VuStage& s : k.stages) {
        vugen::Stage st = vugen::makeStage(s.kind);
        st.enabled = s.enabled;
        for (int i = 0; i < 4; ++i) st.params[i].value = s.params[i];
        kd.stages.push_back(st);
    }
    const vugen::BuiltKernel bk = vugen::buildKernel(kd);
    for (const std::string& e : bk.errors) {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::semantics().danger);
        ImGui::TextWrapped("%s", e.c_str());
        ImGui::PopStyleColor();
    }
    if (bk.errors.empty() && !bk.program.code.empty()) {
        const int n = (int)bk.program.code.size();
        ui::StatusLine(0, (std::to_string(n) + " instructions -> " + std::to_string((n + 1) / 2) + ".." +
                           std::to_string(n) + " of VU0's 512 slots, " + std::to_string(bk.perElement) +
                           " per element")
                              .c_str());
    }
    ImGui::Spacing();
    ui::Caption("Stages - run top to bottom");
    drawVuStageList(k.stages, true);

    // THE OTHER WAY TO WRITE ONE: a C++ body in src/vu0/, compiled and run
    // inside the BUILD CONTAINER - what is listed here is the manifest the
    // last build left behind.
    ImGui::Spacing();
    ui::Caption("Kernels written in C++", "src/vu0");
    struct KernelRow {
        std::string name, cls;
        int instrs = 0, perElement = 0, batch = 0;
    };
    std::vector<KernelRow> rows;
    std::ifstream mf(std::filesystem::path(project_.dir) / "src" / "gen" / "vu0_scripts.manifest");
    std::string line;
    while (std::getline(mf, line)) {
        std::vector<std::string> f;
        for (size_t at = 0;;) {
            const size_t tab = line.find('\t', at);
            f.push_back(line.substr(at, tab == std::string::npos ? std::string::npos : tab - at));
            if (tab == std::string::npos) break;
            at = tab + 1;
        }
        if (f.size() < 5) continue;
        rows.push_back({f[0], f[4], std::atoi(f[1].c_str()), std::atoi(f[2].c_str()),
                        std::atoi(f[3].c_str())});
    }
    if (rows.empty()) {
        ImGui::TextDisabled("None. A .cpp in src/vu0/ that subclasses vu::Kernel gets a driver.");
    } else {
        for (const KernelRow& r : rows) {
            ImGui::BulletText("%s - Tyra::%s", r.name.c_str(), r.cls.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("%d instructions (%d..%d of 512), %d per element, batch %d", r.instrs,
                                (r.instrs + 1) / 2, r.instrs, r.perElement, r.batch);
        }
        ImGui::TextDisabled("From the last build - VU0's own 512 slots, not the VU1 budget.");
    }
}

// The Micro memory page: the resident set by hand or detected, what each
// class costs, the looks and scripts in it, and what runs on a VU outside it.
void App::drawVuMicroMemory(unsigned mask, unsigned needed, int fam) {
    const theme::Semantics& sem = theme::semantics();
    const float fs = ImGui::GetFontSize();
    const VuBudget bud = computeVuBudget(project_, vuPreview_);
    {
        ui::FontScope bold(ui::boldFont());
        ImGui::TextUnformatted("Micro memory");
    }
    ImGui::TextDisabled("What is resident on VU1 - this is the budget above.");
    ImGui::Spacing();

    ui::Caption("Material classes");
    bool autoMode = project_.vu.residentAuto;
    {
        ui::InspectorRows rows(fs * 6.0f, fs * 30.0f);
        if (ui::Switch("Detect from the scenes (keep the resident set equal to what the scenes and "
                       "prefabs draw)",
                       &autoMode)) {
            project_.vu.residentAuto = autoMode;
            if (autoMode) project_.vu.residentClasses = needed;
            commitChange();
        }
    }
    paramTip("A hand-set mask is right on the day it is set and wrong the first time\n"
             "someone adds a chrome ball - and the failure is a mesh quietly drawing in\n"
             "a simpler style, which reads as a material bug.");
    ImGui::BeginDisabled(autoMode);
    for (int ci = 0; ci < 5; ++ci) {
        const ClassRow& c = kClasses[ci];
        bool on = (mask & c.bit) != 0;
        const bool used = (needed & c.bit) != 0;
        int crossing = engineSizes().instr[ci][fam];
        if (fam == 1) {
            const unsigned without = mask & ~c.bit;
            crossing = engineCrossingInstructions(mask | c.bit, 1) - engineCrossingInstructions(without, 1);
        }
        const int emitted = engineSizes().instr[ci][0] + crossing;
        ImGui::PushID((int)c.bit);
        if (c.bit == 1u) ImGui::BeginDisabled();  // the fallback floor
        if (ui::Switch(c.label, &on)) {
            project_.vu.residentClasses = (on ? (mask | c.bit) : (mask & ~c.bit)) | 1u;
            commitChange();
        }
        if (c.bit == 1u) ImGui::EndDisabled();
        ImGui::SameLine(fs * 12.0f);
        {
            ui::FontScope mono(ui::monoFont());
            ImGui::TextDisabled("%d..%d", (emitted + 1) / 2, emitted);
        }
        ImGui::SameLine(fs * 17.0f);
        ImGui::TextDisabled("%d objects \xc2\xb7 %s", vuObjectsInClass(c.bit), c.what);
        if (used && !on) {
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, sem.warn);
            ImGui::TextUnformatted("the scene USES this");
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Dropping a class the scene draws does not crash - the engine\n"
                                  "walks down to a resident relative - but those meshes lose the\n"
                                  "feature (a lit mesh goes flat, a matcap goes plain).");
        }
        ImGui::PopID();
    }
    ImGui::EndDisabled();

    if (!vuPreview_.empty()) {
        ImGui::Spacing();
        ui::Caption("Looks", "stage lists");
        for (size_t i = 0; i < vuPreview_.size(); ++i) {
            ImGui::BulletText("%s", i < vuPreviewLabel_.size() ? vuPreviewLabel_[i].c_str() : "look");
            ImGui::SameLine(fs * 20.0f);
            ImGui::TextDisabled("%+d over the engine's", vuPreview_[i].stageInstrs);
        }
    }

    ImGui::Spacing();
    ui::Caption("VU scripts", "src/vu - from the last build");
    if (bud.scripts.empty()) {
        ImGui::TextDisabled(project::hasVuScripts(project_)
                                ? "Not built yet - run a Build and these fill in."
                                : "None. Add one from the Scripts panel, or see docs/vu-authoring.md.");
    } else {
        // ONE NODE PER SCRIPT: a script claiming five classes emits up to
        // fifteen programs, and forty near-identical rows read as nothing.
        for (size_t i = 0; i < bud.scripts.size();) {
            const std::string& name = bud.scripts[i].script;
            size_t j = i;
            int resident = 0, residentDelta = 0, noops = 0;
            while (j < bud.scripts.size() && bud.scripts[j].script == name) {
                if (bud.scripts[j].resident && (mask & bud.scripts[j].bit)) {
                    ++resident;
                    residentDelta += bud.scripts[j].delta;
                }
                if (bud.scripts[j].verdict == "noop") ++noops;
                ++j;
            }
            char header[192];
            std::snprintf(header, sizeof header, "%s##vuscriptnode%zu", name.c_str(), i);
            const bool open = ImGui::TreeNodeEx(header, ImGuiTreeNodeFlags_SpanAvailWidth);
            ImGui::SameLine(fs * 12.0f);
            ImGui::TextDisabled("%d program%s resident, %+d slots%s", resident, resident == 1 ? "" : "s",
                                residentDelta, bud.scripts[i].boot ? "" : " (off at boot)");
            if (noops) {
                ImGui::SameLine();
                ImGui::TextColored(sem.warn, "  %d change nothing", noops);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("The build compared each program against the SAME description\n"
                                      "with the script removed, and these came out identical. Usually\n"
                                      "the slot: a displacement at Slot::Color has nothing to move.");
            }
            if (open) {
                for (size_t k = i; k < j; ++k) {
                    const ScriptRow& r = bud.scripts[k];
                    ImGui::BulletText("%s%s", r.cls.c_str(),
                                      r.fam == 1 ? "  (VU1 clipper half)" : r.fam == 2 ? "  (EE-clipper half)" : "");
                    ImGui::SameLine(fs * 20.0f);
                    ImGui::TextDisabled("%d slots, %+d against the engine's%s", r.instrs, r.delta,
                                        r.resident ? "" : "  [other clipping mode]");
                }
                ImGui::TreePop();
            }
            i = j;
        }
    }

    // Everything else the console runs on a VU. None of it is in the bar, and
    // saying so answers "can I turn animation off to get room".
    ImGui::Spacing();
    ui::Caption("Also on the VUs - not in this budget");
    auto elsewhere = [&](const char* what, const char* where, const char* why) {
        ImGui::BulletText("%s", what);
        ImGui::SameLine(fs * 14.0f);
        ImGui::TextDisabled("%s", where);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", why);
    };
    elsewhere("Animated models, drawing", "VU1, four programs",
              "The dynamic pipeline uploads its own set over the same\n"
              "micro-memory addresses when it draws, so it SWAPS with\n"
              "the static set rather than sharing it.");
    elsewhere("Skeletal skinning", "VU0, macro mode",
              "COP2 instructions issued by the EE - there is no\n"
              "microprogram to upload, so it occupies no micro memory.");
    elsewhere("Particle billboards", "VU1, own small set",
              "Swapped in on demand (ensureProgramSet) - the resident\n"
              "set has no room for them.");
    if (project_.vu.kernel.enabled)
        elsewhere("Your VU0 kernel", "VU0, micro mode",
                  "Against VU0's own 512 slots. Independent of everything above.");
}

// Runs the selected program in the host simulator over a small synthetic mesh,
// twice: once with the panel's mesh parameters and once with them at zero. The
// second run is the interesting one - it is the identity the whole design rests
// on, and seeing the two side by side is what makes "a mesh that wants nothing
// gets nothing" a fact rather than a promise.
void App::vuSimulate() {
    vuSimOut_.clear();
    vuSimRows_.clear();
    vuSimErr_.clear();
    vuSimQ_ = 0;
    vuSimSame_ = true;
    vuSimRan_ = true;
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
        vuSimErr_ = "The program did not run: " + (off.ok ? on.error : off.error);
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
        for (int32_t v : {x, y, z, x0, y0, z0}) vuSimRows_.push_back((int)v);
        vuSimRows_.push_back(x == x0 && y == y0 && z == z0 ? 1 : 0);
        if (!(x == x0 && y == y0 && z == z0)) vuSimSame_ = false;
    }
    vuSimQ_ = on.qClobbers;
    // The panel draws the table and the verdict itself; the text keeps only
    // what has no other place - the simulator's warnings.
    s.clear();
    for (const vusim::Warning& w : on.warnings) s += (s.empty() ? "" : "\n") + w.text;
    vuSimOut_ = s;
}
