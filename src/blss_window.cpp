// Tools > Neural Upscaler (BLSS): the window. Its host half - the job that
// runs the editor's own CLI and the parsers that read the tool's tables back -
// is blss_ui.cpp; this file only draws.
//
// App:: methods declared in app.hpp, own TU (the credits_ui.cpp precedent).
// The clash warnings and the five project settings are drawn from HERE by both
// this window and Project > Preferences, so there is one mirror of
// templates.cpp's blssClashes() and one set of tooltips, not two.

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <imgui.h>

#include "app.hpp"
#include "app_internal.hpp"
#include "blss.hpp"  // kFillWeight/kFlickerWeight defaults, readPng - read-only
#include "blss_ui.hpp"
#include "gl_loader.h"
#include "platform.hpp"

namespace fs = std::filesystem;

// ===========================================================================
// App:: - the window
// ===========================================================================

namespace {

// A float as a CLI argument. std::to_string gives six decimals, so a command
// line the window RECORDS as the net's provenance came out reading
// `--fill-weight 16.000000 --flicker-weight 0.000000`, which is noise in the
// one line a person reads to answer "what was this trained with".
std::string numArg(double v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%g", v);
    return buf;
}

// Where a checkout keeps the PNG materials the corpus prefers over its
// procedural ones. Not required - blsscorpus falls back to checkers and noise,
// which is what makes --blss-train work in a clean tree - but a corpus with
// real textures is the one every number in the docs was measured on, so the
// window looks for it and says which it found.
std::string discoverAssets() {
    const std::string exe = platform::exePath();
    std::error_code ec;
    fs::path dir = fs::path(exe).parent_path();
    for (int up = 0; up < 3 && !dir.empty(); ++up) {
        const fs::path cand = dir / "examples";
        if (fs::is_directory(cand, ec)) return cand.string();
        dir = dir.parent_path();
    }
    return "examples";
}

// The comparison PNGs --blss-eval --dump writes, in the order they are worth
// looking at. `blss-net-weights.png` is one pixel per TILE (16x14 at 512x448),
// so it is magnified with nearest sampling rather than shown at size.
// The tab order, named. `blssTabSelect_` is how a click on a thumbnail or on a
// verdict button takes the reader to the tab that shows it, and it used to be
// raw indices scattered over three call sites - which is a silent mis-navigation
// waiting for the next tab to be inserted anywhere but the end.
//
// The order is the ORDER OF THE WORK: decide what to shoot, fit it, measure it,
// look at it, then check the inputs against a real console frame.
enum BlssTab {
    kTabShots = 0,
    kTabTrain,
    kTabEval,
    kTabCv,
    kTabCompare,
    kTabInputs,
    kTabProbe,
    kTabSettings
};

struct DumpImage {
    const char* file;
    const char* label;
    const char* tip;
};
const DumpImage kDumpImages[] = {
    {"blss-truth.png", "ground truth",
     "The 4x supersampled render, box-resolved. What every PSNR column is\n"
     "measured against, and something no console can produce."},
    {"blss-native.png", "native full-res",
     "The scene rendered at full resolution with one sample - the honest\n"
     "'no BLSS' reference. NOT a ceiling: the truth above is supersampled,\n"
     "so a good temporal reconstruction can beat this."},
    {"blss-bilinear.png", "bilinear (the baseline)",
     "The half-res render blown up bilinearly - one full-screen pass, and\n"
     "the number every margin in this window is measured against."},
    {"blss-net.png", "BLSS (the trained net)",
     "The composite the network asked for, through the same 8-bit GS\n"
     "arithmetic the console executes."},
    {"blss-oracle.png", "oracle (upper bound)",
     "What the best possible per-tile weights achieve under the same\n"
     "objective. The gap to BLSS is what the network failed to learn."},
    {"blss-point.png", "point only", "Every tile reconstructed with nearest sampling."},
    {"blss-temporal.png", "temporal only", "Every tile reconstructed from the history."},
    {"blss-sharpen.png", "sharpen only", "Every tile with the unsharp mask at full strength."},
    {"blss-net-weights.png", "weight field (1 px per tile)",
     "The network's decision for the first held-out frame: red = point,\n"
     "green = temporal, blue = sharpen. 16x14 at 512x448 output, so it is\n"
     "magnified with nearest sampling."},
};

// The plan as the corpus will execute it. ONE derivation, here, so the preview
// and the totals cannot disagree with each other - and it deliberately mirrors
// the rule blssscene::loadProject is asked to follow (see the report that goes
// with this change), including the part that keeps every published fold table
// reproducible: WITH A DEFAULT PLAN NOTHING CHANGES, budget cap included.
struct PlannedShot {
    std::string scene, name, kind;
    int frames = 0;          // 0 = an equal share of what is left
    bool authored = false;   // from Project::blssShots::shots
    bool take = false;       // from a Cutscene Director track
    std::string problem;     // non-empty = it will NOT be shot, and why
};

// The corpus' own per-scene shot budget (blssscene::kShotsPerScene). Restated
// rather than included because blssscene.hpp is the other agent's file and this
// window needs only the number; the preview says out loud when it binds.
constexpr int kBlssShotsPerScene = 6;

// How many Cutscene Director tracks a scene contributes. Approximate on
// purpose and said so where it is drawn: blssscene drops a take whose camera
// bindings do not resolve, and this cannot see that without repeating the
// resolution - which would be a second answer to a question that module owns.
int takeCountFor(const Project& p) {
    int n = 0;
    for (const Sequence& q : p.sequences)
        if (q.cameraEnabled && q.cameraKeys.size() >= 2) ++n;
    return n;
}

std::vector<PlannedShot> planShots(const Project& p) {
    std::vector<PlannedShot> out;
    const BlssShotPlan& pl = p.blssShots;
    const bool custom = !pl.isDefault();
    for (size_t si = 0; si < p.scenes.size(); ++si) {
        const SceneData& sc = p.scenes[si];
        const std::string scene = sc.name.empty() ? std::string("scene") : sc.name;
        const size_t before = out.size();
        // 1 - authored cutscene tracks, capped at half the per-scene budget
        //     exactly as blssscene caps them today.
        if (pl.authoredTakes) {
            const int takes = std::min(takeCountFor(p), kBlssShotsPerScene / 2);
            for (int i = 0; i < takes; ++i) {
                PlannedShot s;
                s.scene = scene;
                s.name = scene + " take";
                s.kind = "take";
                s.take = true;
                out.push_back(std::move(s));
            }
        }
        // 2 - the author's own vantages. NEVER capped: an explicit statement
        //     that is silently dropped is the exact failure this tab exists to
        //     stop, and a plan is only worth authoring if it is obeyed.
        for (size_t i = 0; i < pl.shots.size(); ++i) {
            const BlssShot& b = pl.shots[i];
            if (project::blssShotScene(p, b) != (int)si) continue;
            PlannedShot s;
            s.scene = scene;
            s.name = project::blssShotLabel(p, b, (int)i);
            s.kind = b.move ? "authored move" : "authored still";
            s.frames = b.frames;
            s.authored = true;
            if (!b.enabled) {
                s.problem = "switched off";
            } else {
                float a[3], la[3], c[3], lc[3];
                if (!project::blssResolveShot(p, b, a, la, c, lc))
                    s.problem = (!b.camera.empty() || !b.cameraTo.empty())
                                    ? "names a Camera object this scene does not have"
                                    : "its eye and look-at are the same point";
            }
            out.push_back(std::move(s));
        }
        // 3 - the automatic moves. With a DEFAULT plan they fill to the old
        //     per-scene budget, which is what makes an untouched project render
        //     the identical corpus it always did.
        for (int m = 0; m < kBlssAutoMoveCount; ++m) {
            if (!pl.autoMove[m]) continue;
            if (!custom && (int)(out.size() - before) >= kBlssShotsPerScene) break;
            PlannedShot s;
            s.scene = scene;
            s.name = scene + " " + project::blssAutoMoveName(m);
            s.kind = project::blssAutoMoveKind(m);
            s.frames = pl.autoFrames[m];
            out.push_back(std::move(s));
        }
    }
    return out;
}

// How the frame budget lands. Shots that asked for a count get it; the rest
// share what is left, which is what "0 = its equal share" has to mean for the
// two kinds to coexist. Returns false when the explicit counts alone overrun
// the budget - a state the panel must SAY rather than silently rescale, because
// a rescaled "24 frames" is a number the author did not ask for.
bool splitFrames(const std::vector<PlannedShot>& plan, int budget, std::vector<int>& out,
                 int& shortfall) {
    out.assign(plan.size(), 0);
    shortfall = 0;
    int fixed = 0, sharing = 0;
    for (const PlannedShot& s : plan) {
        if (!s.problem.empty()) continue;
        if (s.frames > 0)
            fixed += s.frames;
        else
            ++sharing;
    }
    const int left = budget - fixed;
    if (left < sharing) {
        shortfall = sharing - std::max(0, left);
        return false;
    }
    const int each = sharing > 0 ? left / sharing : 0;
    int spare = sharing > 0 ? left - each * sharing : 0;
    for (size_t i = 0; i < plan.size(); ++i) {
        if (!plan[i].problem.empty()) continue;
        if (plan[i].frames > 0) {
            out[i] = plan[i].frames;
        } else {
            out[i] = each + (spare > 0 ? 1 : 0);
            if (spare > 0) --spare;
        }
    }
    return true;
}

}  // namespace

// --- the five project settings, drawn from ONE place -------------------------

App::BlssClash App::blssClashesFor(const ProjectSettings& staged) const {
    // THESE ARE THE BUILD'S OWN CONDITIONS. blssClashes() in src/templates.cpp
    // emits a #error into inc/scene_data.hpp for each of them, so the build
    // REFUSES the combination - this is the early warning that stops someone
    // reaching a refused build. The two must answer alike: quieter than the
    // build lets a user walk into a wall of #error, louder cries wolf on a
    // project that would have built. Mirrored rather than shared because
    // blssClashes() reads a saved Project and this has to answer for the STAGED
    // settings while the Preferences modal is open; if you edit one, edit the
    // other. It lives HERE, in one function, because it used to be inlined in
    // the Preferences dialog and this window would have been a second copy -
    // and two mirrors of one interlock drift the day either is touched.
    //
    // Every condition is the one the GENERATED GAME takes, not the coarser
    // question "does this project mention the feature at all":
    //   - depth of field per SCENE (a scene can override the whole post-fx
    //     group), quantised the way POSTFX_DOFS is and gated on a non-zero
    //     focus distance, because applyPostFx runs the pass only for
    //     `dof > 0 && dofFocus > 0`;
    //   - ...AND the Set Depth Of Field flow node, which raises it at RUNTIME
    //     in a project whose authored amount is 0 everywhere;
    //   - a portal only when its target resolves to another Portal in the same
    //     scene: renderPortalView skips target < 0, so an unlinked portal is a
    //     tinted surface and no clash;
    //   - split screen only when the preference AND a scene with a second
    //     Player object, which is what PLAYER2_INDEXES gates the split branch
    //     on.
    //
    // POSTFX_DOFS stores the amount in 1/128ths, so an amount under 1/128 is
    // depth of field that never draws.
    //
    // WHAT CAUSED IT IS CARRIED, not just THAT it happened. The walk below has
    // the scene and the object in hand at the moment it decides; four bools
    // threw that away, and on a ten-scene project "a Set Depth Of Field flow
    // node turns it on at runtime" left the reader hunting through every graph
    // for a node the editor had already found.
    BlssClash c;
    if (!hasProject_) return c;
    const auto dofActive = [](float amount, float focus) {
        return (int)(amount * 128.0f + 0.5f) > 0 && focus > 0.0f;
    };
    const bool splitPref = staged.multiplayer == "split";
    for (size_t si = 0; si < project_.scenes.size(); ++si) {
        const SceneData& sc = project_.scenes[si];
        const auto ref = [&](int obj, const std::string& what) {
            BlssClashRef r;
            r.scene = (int)si;
            r.object = obj;
            r.label = sc.name + (what.empty() ? "" : " > " + what);
            return r;
        };
        // project::resolvedSettings()' post-fx branch, against the STAGED
        // project settings: dofAmount/dofFocus come from the scene only when it
        // overrides that group, and nothing else in that function - the
        // ambience preset overlay included - touches either field.
        const ProjectSettings& fx = sc.overrides.postFx ? sc.settings : staged;
        if (dofActive(fx.dofAmount, fx.dofFocus)) {
            BlssClashRef r = ref(-1, "");
            char buf[96];
            std::snprintf(buf, sizeof(buf), "  (amount %.2f, focus %.1f%s)", fx.dofAmount,
                          fx.dofFocus, sc.overrides.postFx ? ", this scene's override" : "");
            r.label += buf;
            c.dof.push_back(r);
        }
        int players = 0, firstExtraPlayer = -1;
        for (size_t oi = 0; oi < sc.objects.size(); ++oi) {
            const SceneObject& o = sc.objects[oi];
            if (o.type == PrimitiveType::Player) {
                ++players;
                if (players == 2) firstExtraPlayer = (int)oi;
            }
            if (o.type == PrimitiveType::Portal && !o.portalTarget.empty())
                for (const SceneObject& t : sc.objects)
                    if (&t != &o && t.type == PrimitiveType::Portal &&
                        t.name == o.portalTarget) {
                        BlssClashRef r = ref((int)oi, o.name);
                        r.label += " -> " + t.name;
                        c.portals.push_back(r);
                    }
            // Mode 1 turns depth of field off and mode 2 restores the scene's
            // authored value (which the loop above already caught when it is
            // non-zero), so only mode 0 sets its own. The amount is a literal in
            // the emitted code - nothing can wire it - but a wired POSITION
            // replaces Focus with the live player-to-point distance, which is
            // > 0.
            for (const FlowNode& n : o.flowGraph.nodes) {
                if (n.type != "SetDof" || (int)n.num[3] != 0) continue;
                bool posWired = false;
                for (const FlowLink& l : o.flowGraph.links)
                    posWired |= (l.kind == FlowLinkPos && l.toNode == n.id);
                if (dofActive(n.num[2], posWired ? 1.0f : n.num[0])) {
                    BlssClashRef r = ref((int)oi, o.name);
                    char buf[80];
                    std::snprintf(buf, sizeof(buf), "  (Set Depth Of Field node %d, amount %.2f)",
                                  n.id, n.num[2]);
                    r.label += buf;
                    c.dofNode.push_back(r);
                }
            }
        }
        if (splitPref && players >= 2) {
            BlssClashRef r = ref(firstExtraPlayer,
                                 firstExtraPlayer >= 0 ? sc.objects[(size_t)firstExtraPlayer].name
                                                       : std::string());
            r.label += "  (the second Player object)";
            c.split.push_back(r);
        }
    }
    return c;
}

// Take the reader to what the warning is about. The editor already knows how to
// select and frame an object; the only extra step is the SCENE, because
// selection indices address the active scene's object list and nothing else -
// which is exactly why a clash in scene 7 was unfindable before.
void App::blssSelectClash(const BlssClashRef& r) {
    if (!hasProject_ || r.scene < 0 || r.scene >= (int)project_.scenes.size()) return;
    if (project_.activeScene != r.scene) {
        project_.activeScene = r.scene;
        clearSelection();
        cancelPastePlacement();  // staged copies belong to the old scene
        flowGraphObject_ = -1;
        flowPositionsApplied_ = false;
        applyProjectToViewport();  // terrain/lighting are per scene
    }
    if (r.object < 0 || r.object >= (int)project_.objects().size()) {
        clearSelection();
        return;
    }
    selectOnly(r.object);
    viewport_.setTarget(project_.objects()[(size_t)r.object].position);
    navFocusedIndex_ = r.object;  // keep orbit-around-selection in sync
}

void App::drawBlssClashWarning(const BlssClash& c, bool informational) {
    if (!c.any()) return;
    // WARNING when the feature is on (the build will refuse this project),
    // INFORMATIONAL when it is off - because the reader most in need of this
    // block is the one deciding whether to switch the feature on, and until now
    // that reader saw nothing at all: the whole block was inside
    // `if (s.blssEnabled)`, so the conflicts appeared only once it was too late
    // to have avoided them.
    const ImVec4 col = informational ? ImVec4(0.70f, 0.70f, 0.78f, 1.0f)
                                     : ImVec4(1.0f, 0.6f, 0.2f, 1.0f);
    int uid = 0;
    // One "Select it" per named cause. A clash on a ten-scene project is a
    // needle; the walk that found it had the scene and the object in hand.
    const auto list = [&](const std::vector<BlssClashRef>& refs) {
        for (const BlssClashRef& r : refs) {
            ImGui::Indent(scaled(28.0f));
            ImGui::TextColored(col, "%s", r.label.c_str());
            ImGui::SameLine();
            ImGui::PushID(uid++);
            // The scene-level rows (depth of field is a post-fx setting, not an
            // object) have nothing to select, so they offer the scene instead.
            if (ImGui::SmallButton(r.object >= 0 ? "Select it" : "Go to the scene"))
                blssSelectClash(r);
            ImGui::PopID();
            ImGui::Unindent(scaled(28.0f));
        }
    };

    ImGui::TextColored(col,
                       informational
                           ? "    If you turn this on, the BUILD WILL REFUSE this project - it "
                             "cannot be\n    combined with what the project already uses:"
                           : "    Cannot be combined with what this project uses,\n"
                             "    and the BUILD WILL REFUSE IT:");
    if (!c.dof.empty() || !c.dofNode.empty())
        ImGui::TextColored(
            col,
            "      - Depth of field composites its blur through the GS depth\n"
            "        test at display resolution, and with this on the depth\n"
            "        buffer is only as big as the reduced render.");
    list(c.dof);
    if (!c.dofNode.empty())
        ImGui::TextColored(
            col,
            !c.dof.empty() ? "          ...and a Set Depth Of Field flow node turns it\n"
                             "          on at runtime as well: set that node's Mode to\n"
                             "          Off, or delete it."
                           : "          The authored amount is 0, but a Set Depth Of\n"
                             "          Field flow node turns it ON at runtime. Set\n"
                             "          that node's Mode to Off, or delete it.");
    list(c.dofNode);
    if (!c.portals.empty())
        ImGui::TextColored(
            col,
            "      - Portals want that same display-resolution depth, and a\n"
            "        through-view carves its opening from inside the scene\n"
            "        pass with a display-sized raster window. Only a LINKED\n"
            "        pair renders one, so unlinking is a fix too.");
    list(c.portals);
    if (!c.split.empty())
        ImGui::TextColored(
            col,
            "      - Split-screen frames are never reduced at all (only the\n"
            "        single-view path is), and scene depth writes are masked\n"
            "        outside the reduced pass - so both halves would render\n"
            "        full-resolution with no working depth buffer.");
    list(c.split);
    ImGui::TextColored(col, "    Turn one of the two off; either side resolves it.");
}

// The engine's own buffer arithmetic, as a host twin. RendererCoreGSVRam::
// getSize() rounds a 32bpp buffer's WIDTH up to a multiple of 64 and then its
// size up to a GS page (2048 words); RendererCoreGS::allocateVramBuffers()
// allocates the z buffer at the RASTER size, which is what makes the whole
// saving exist. Keep in step with vendor/tyra/.../renderer_core_gs_vram.cpp.
static int blssBufferWords(int w, int h) {
    if (w <= 0 || h <= 0) return 0;
    const int aligned = w > 16 ? ((w + 63) & ~63) : w;
    const int words = aligned * h;
    return (words + 2047) & ~2047;
}

// WHAT THE REDUCED RENDER IS WORTH ON THIS PROJECT'S RASTER, which is the one
// number a reader can act on - and the tooltip it replaces quoted a measurement
// taken at PAL 512x512 to somebody whose game is 512x448.
//
// z shrinks with the raster and the low-res colour target is new, so the net is
// (z at output) - (z at raster) - (the low-res target), and at 32bpp the target
// is exactly the same size as the z it displaced. That makes 1x2 EXACTLY ZERO -
// the low-res target costs precisely what the z buffer saves - which nothing in
// this window or in the documentation used to say.
std::string App::blssVramLine(const ProjectSettings& s) const {
    if (!hasProject_) return "";
    const DisplayModeInfo& dm = project::displayModeInfo(project::bootDisplayMode(s));
    const int outW = dm.bufW;
    const int outH = dm.halfHeight ? dm.logicalH / 2 : dm.logicalH;
    const int sx = s.blssScale == 1 ? 1 : 2, sy = 2;
    const int lowW = outW / sx, lowH = outH / sy;
    const int zOut = blssBufferWords(outW, outH);
    const int zLow = blssBufferWords(lowW, lowH);
    const int target = blssBufferWords(lowW, lowH);
    const int net = zOut - zLow - target;
    char buf[320];
    if (net > 0)
        std::snprintf(buf, sizeof(buf),
                      "    %s on this project's %dx%d output hands back %d KB of GS VRAM: the "
                      "z-buffer\n    follows the raster (%d KB back) and the %dx%d target costs "
                      "%d KB of it.",
                      sx == 1 ? "1x2" : "2x2", outW, outH, net * 4 / 1024,
                      (zOut - zLow) * 4 / 1024, lowW, lowH, target * 4 / 1024);
    else if (net == 0)
        std::snprintf(buf, sizeof(buf),
                      "    %s hands back NOTHING on this project's %dx%d output: the %dx%d "
                      "target costs\n    exactly what the z-buffer saves (%d KB each way). Pick "
                      "%s if VRAM is the reason.",
                      sx == 1 ? "1x2" : "2x2", outW, outH, lowW, lowH, target * 4 / 1024,
                      sx == 1 ? "2x2" : "1x2");
    else
        std::snprintf(buf, sizeof(buf),
                      "    %s COSTS %d KB of GS VRAM on this project's %dx%d output - the "
                      "low-res target is\n    bigger than the z-buffer it shrinks.",
                      sx == 1 ? "1x2" : "2x2", -net * 4 / 1024, outW, outH);
    return buf;
}

bool App::drawBlssSettings(ProjectSettings& s) {
    // ONE definition of the five settings and their tooltips, drawn by both
    // Project > Preferences (against the staged prefSettings_) and this window
    // (against the live project). Two copies of this block is how a warning
    // regresses on one side only.
    bool changed = false;
    if (ImGui::Checkbox("Reconstruct from a reduced-resolution render", &s.blssEnabled))
        changed = true;
    prefHelp(
        "Renders the 3D SCENE at reduced resolution into its own GS target,\n"
        "then reconstructs the display buffer from it: a small neural network\n"
        "trained on the host picks, per 32x32 screen tile, how much crisp\n"
        "point sampling, previous frame and sharpening to blend. The HUD,\n"
        "the menus, the text and every post effect still draw at FULL\n"
        "resolution, so 2D stays crisp.\n"
        "\n"
        "A TRAINED NETWORK SHIPS WITH THE EDITOR, so a project with no\n"
        "blss.net is built with that one rather than with random weights. It\n"
        "was fitted on seven example projects AND the built-in bestiary (55\n"
        "shots) and measured leave-one-PROJECT-out at +0.29 dB on a project it\n"
        "has never seen, against +0.31 dB for that project's own net - a tie\n"
        "inside fold sds of 0.37 and 0.34.\n"
        "\n"
        "Training on your own scenes still reaches the highest number of all\n"
        "(+0.41 dB in distribution, which is what the console runs) and takes\n"
        "about ten seconds, so it is worth doing - but it is an OPTIMISATION\n"
        "and no longer a prerequisite. A net fitted to the bestiary ALONE is\n"
        "still a lottery, -0.34 dB on average over seven projects and -1.09 at\n"
        "worst, which is exactly why the shipped default's corpus holds the\n"
        "bestiary and real projects together.\n"
        "\n"
        "AND SOME SCENES HAVE NOTHING TO WIN. On examples/showcase the ORACLE\n"
        "itself - the best any per-tile weighting can do - scores +0.02 dB at\n"
        "1.00 passes: soft ground texture, low-poly props, nothing that\n"
        "aliases. Run the window's Evaluate tab on your project BEFORE turning\n"
        "this on; it says so in one line.\n"
        "\n"
        "On the built-in corpus, held out shot by shot, it is +0.41 dB (13\n"
        "shots x 3 seeds = 39 fold-runs, sd 0.34, 3 of the 39 below bilinear,\n"
        "re-run at the activation table that ships) - but that is a number\n"
        "about the bestiary, not about your game.\n"
        "\n"
        "IT HAS A REGIME, AND BOTH SIDES OF IT ARE MEASURED. The feature costs\n"
        "4.60 ms of EE per frame and keeps only 24.5 % of the scene's GS fill\n"
        "(the render is half in EACH axis, so a quarter of the pixels). At the\n"
        "calibrated 0.587 ms per full-screen blended textured pass that is\n"
        "break-even at 11.5 full-screen coverages. Both numbers are FITTED over\n"
        "five load points on hardware, not assumed. On a real PS2:\n"
        "  examples/upscaler-lab, 58.7 coverages: 52.95 -> 32.42 ms, 1.63x\n"
        "  blssrig, a handful of coverages:       9.42 -> 19.25 ms, a loss\n"
        "'Will the frame get faster?' in Tools > Neural Upscaler (BLSS)\n"
        "estimates YOUR scene's overdraw and says which side of that line it\n"
        "falls on. No emulator number is admissible here - PCSX2 under-reports\n"
        "GS fill by 76x.\n"
        "\n"
        "VRAM it gives back: the z-buffer shrinks with the render, which\n"
        "returns more than the reduced-resolution target costs - 448 KB at 2x2\n"
        "on a 512x448 output, and EXACTLY ZERO at 1x2, where the low-res target\n"
        "costs precisely what the z-buffer saves. Fill it costs back: 0.50 ms\n"
        "of composite, measured on hardware, and up to five full-screen passes\n"
        "when the network asks for every kernel everywhere.\n"
        "\n"
        "Reflections, camera feeds and projected shadows work with it - they\n"
        "used to switch it off in the middle of the frame. Depth of field,\n"
        "portals and split-screen do not, and the BUILD REFUSES the pair: the\n"
        "generated scene_data.hpp carries an #error naming the feature and the\n"
        "scene it is in. The warning below is the same check, live.\n"
        "\n"
        "Train the network in Tools > Neural Upscaler (BLSS), or with\n"
        "'tyrax-editor --blss-train' in the project directory. Check it with\n"
        "cross-validation, never with one split. Without a blss.net the game\n"
        "is built with the net the editor ships and says which one it got in\n"
        "its boot log.");
    if (s.blssEnabled)
        ImGui::TextColored(
            ImVec4(0.65f, 0.65f, 0.65f, 1.0f),
            "    A trained net ships with the editor, so this builds with that one until\n"
            "    you train your own: +0.29 dB on a project it has never seen against\n"
            "    +0.31 for that project's own net. Retraining here is worth a fraction of\n"
            "    a dB on a scene that HAS a ceiling - and some scenes have none at all, on\n"
            "    examples/showcase the oracle itself is +0.02 dB. Tools > Neural Upscaler\n"
            "    (BLSS) answers 'will this scene benefit' in one line before you spend\n"
            "    anything. On a frame with too little fill it also measured 9.83 ms a frame\n"
            "    SLOWER (9.42 -> 19.25), and textured primitives and terrain can vanish\n"
            "    with it on - both are open work.");
    // ALWAYS, not only when the feature is on. The person who most needs to
    // know that this project uses depth of field is the one deciding whether to
    // tick the box above; drawing it only for `s.blssEnabled` meant the answer
    // arrived one click too late, and the first thing they would then hit is a
    // build refusing with a wall of #error.
    drawBlssClashWarning(blssClashesFor(s), !s.blssEnabled);

    ImGui::BeginDisabled(!s.blssEnabled);
    {
        int scale = s.blssScale == 1 ? 1 : 0;
        // Deliberately no resolution in these labels. They used to read "(256x224
        // of a PAL 512x448 frame)", which is a different frame from the one most
        // projects boot in - and now sits directly above a line that states this
        // project's real numbers, so the two would contradict each other.
        const char* names[] = {
            "2x2 - quarter the pixels",
            "1x2 - half height only (keeps horizontal detail; cheaper to reconstruct)"};
        ImGui::SetNextItemWidth(scaled(420));
        if (ImGui::Combo("Render scale", &scale, names, 2)) {
            s.blssScale = scale;
            changed = true;
        }
        prefHelp(
            "How much of the frame the GS actually rasterises. 2x2 quarters\n"
            "the 3D fill; 1x2 halves only the height, which is what the PS2's\n"
            "own interlaced-field mode already does to the raster - softer\n"
            "vertically, untouched horizontally.\n"
            "The z-buffer is allocated at THIS size, which is where the VRAM\n"
            "saving comes from - the line under this combo works it out for\n"
            "the display mode THIS project boots in, rather than quoting a\n"
            "measurement taken at some other resolution.\n"
            "Train the network at the scale you ship - the Train tab follows\n"
            "this setting. A blss.net is a bare list of floats and records nothing\n"
            "about how it was trained, so a mismatch costs quality without\n"
            "saying anything.");
        // THE NUMBER FOR THIS PROJECT, live, under the control that decides it.
        {
            const std::string vram = blssVramLine(s);
            if (!vram.empty()) {
                if (s.blssScale == 1)
                    // 1x2's saving is exactly zero and that is worth colour: it
                    // is the one setting whose headline benefit is absent, and
                    // neither the UI nor the docs ever said so.
                    ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.35f, 1.0f), "%s", vram.c_str());
                else
                    ImGui::TextDisabled("%s", vram.c_str());
            }
        }

        ImGui::SetNextItemWidth(scaled(120));
        ImGui::DragFloat("Sharpen strength", &s.blssSharpen, 0.01f, 0.0f, 1.0f, "%.2f");
        // Asked BEFORE prefHelp: a tooltip is a window and ImGui's "last item"
        // is context-global, so a prefHelp between the widget and this query
        // silently answers about the tooltip. One undo entry per drag, not one
        // per frame of it.
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        s.blssSharpen = std::clamp(s.blssSharpen, 0.0f, 1.0f);
        prefHelp(
            "The k of the unsharp mask that recovers detail the reduced\n"
            "render lost. The network decides WHERE to sharpen; this decides\n"
            "how hard. 0 = never sharpen (the two extra composite passes are\n"
            "then skipped everywhere, which is also the cheapest setting).\n"
            "It is an input to the trainer as well - the oracle's fill term\n"
            "charges for the sharpen passes at THIS strength - so the Train\n"
            "and Evaluate tabs pass it through.");

        if (ImGui::Checkbox("Temporal reuse (reproject the previous frame)", &s.blssTemporal))
            changed = true;
        prefHelp(
            "Lets the network blend in the previously presented frame,\n"
            "reprojected per grid vertex. This is where the anti-aliasing\n"
            "comes from - two sub-pixel jitter phases averaged is a real 2x\n"
            "supersample, and the GS has no MSAA to offer instead. Off =\n"
            "spatial only: no ghosting on fast motion, and no AA either.\n"
            "It averages the two sub-pixel jitter phases where the network asks\n"
            "for it - but do NOT read that as a cure for a picture that shakes.\n"
            "Measured: the upscaler-lab net puts 72-78% of its weight on this\n"
            "pass and the picture still bobs, at the same magnitude as one that\n"
            "leans on it far less. 'Sub-pixel jitter' below is the switch that\n"
            "actually settles it, and its price is written under it.\n"
            "It is a RUNTIME switch only: the trainer always labels with the\n"
            "temporal kernel available, so turning it off does not need and\n"
            "does not get a different network.");

        if (ImGui::Checkbox("Sub-pixel jitter (the samples temporal reuse averages)",
                            &s.blssJitter))
            changed = true;
        prefHelp(
            "STABILITY AGAINST RECONSTRUCTION, and both sides are measured.\n"
            "Read the whole table before you touch it: this is not a free cure\n"
            "for the bob.\n"
            "\n"
            "On, the raster origin moves a quarter of a pixel every frame. Those\n"
            "offset samples are what the reconstruction is BUILT on, so most of\n"
            "what the upscaler is worth comes from them. Off, every frame\n"
            "samples the same place: a plain spatial upscale, and a still\n"
            "picture.\n"
            "\n"
            "                                  jitter ON   jitter OFF\n"
            "   trained margin over bilinear     +0.69 dB     +0.26 dB\n"
            "   the scene's own oracle CEILING   +0.84 dB     +0.27 dB\n"
            "   folds below bilinear                 0/18         3/18\n"
            "   pixels moving > 2/255 per frame      16.3%         0.0%\n"
            "   mean consecutive change          0.77/255     0.00/255\n"
            "   peak of that alternation           40/255        none\n"
            "\n"
            "The dB rows were measured on examples/upscaler-lab. So were the\n"
            "stability rows, re-measured with the camera AND the particle\n"
            "emitters frozen and 40 back-to-back captures (the earlier figures\n"
            "here came from an instrument whose own noise floor was above the\n"
            "artefact). Jitter off is BYTE-IDENTICAL frame to frame, exactly\n"
            "like BLSS switched off - there is no floor to subtract.\n"
            "\n"
            "So turning it off does not merely cost what the network captured:\n"
            "it removes most of what there was to capture, and the scene's\n"
            "CEILING falls with it. What the price buys is a picture that does\n"
            "not bob - and it DEFAULTS TO OFF, because a person watching three\n"
            "builds that differed in nothing else called the jittered one an\n"
            "earthquake.\n"
            "\n"
            "Do not expect the temporal pass to fuse the two phases instead:\n"
            "upscaler-lab's net puts 72-78% of its weight on temporal and bobs\n"
            "at the same magnitude anyway.\n"
            "\n"
            "NOT a runtime switch. The trainer renders and evaluates through the\n"
            "project's own sampler, so flipping this makes the existing blss.net\n"
            "stale - re-train, or the net is fitted to a distribution the\n"
            "console does not produce. The net's provenance line above says so\n"
            "when the two have drifted apart.");

        // THREE ENTRIES, BECAUSE THE FIELD HAS THREE VALUES. project.cpp clamps
        // blssDebugView to 0..2 and the engine reads 2 as the feature-spread
        // logger; a two-entry combo displayed a project that had 2 as "Off",
        // i.e. the UI lied about the project's own data, and writing the widget
        // back silently reset it. View 2 is also the only instrument that has
        // ever caught a host/console divergence (the whole-mesh proxy, the sky
        // dome), so it must stay reachable without a text editor.
        int debug = std::clamp(s.blssDebugView, 0, 2);
        const char* debugNames[] = {
            "Off", "Tint by winning kernel (red = point, green = temporal, blue = sharpen)",
            "Log the feature spread to bin/log.txt (no tint)"};
        ImGui::SetNextItemWidth(scaled(420));
        if (ImGui::Combo("Debug view", &debug, debugNames, 3)) {
            s.blssDebugView = debug;
            changed = true;
        }
        prefHelp(
            "1 paints each tile by the reconstruction the network chose there -\n"
            "the fastest way to see whether it is making sensible decisions on\n"
            "your content.\n"
            "\n"
            "2 IS THE INSTRUMENT. Once a second the game writes a block into\n"
            "bin/log.txt: BLSSGRID (tile grid and proxy count), BLSSFEAT\n"
            "(min/mean/max of each of the six input channels over the grid, in\n"
            "the trainer's own names and order), BLSSOUT (the three weights the\n"
            "net asked for), BLSSFILL (the passes that cost) and BLSSWORST (the\n"
            "tiles nothing described). Paste a BLSSFEAT line into\n"
            "'tyrax-editor --blss-eval --features --probe \"<line>\"' and the\n"
            "tool places the CONSOLE'S OWN distribution inside the corpus' -\n"
            "which is how the whole-mesh bag proxy and the sky dome were found,\n"
            "and the only way to know the network is being fed what it was\n"
            "trained on. It does not tint the picture.\n"
            "\n"
            "Both ship in the build, so set this back to Off before you hand\n"
            "the game to anyone.");
        if (debug == 2)
            ImGui::TextDisabled(
                "    The game writes BLSSFEAT into bin/log.txt about once a second; feed one\n"
                "    line to --blss-eval --features --probe to place it in this corpus' "
                "distribution.");
    }
    ImGui::EndDisabled();
    return changed;
}

// --- the project's network ---------------------------------------------------

std::string App::blssNetPath() const {
    if (!hasProject_) return "";
    return (fs::path(project_.dir) / blssNetName_).string();
}

// The provenance sidecar. A blss.net is a bare list of floats and carries no
// topology, no date and no settings (blss.hpp says so out loud), so "trained
// with what?" has no answer in the file. When THIS editor trains one it writes
// the exact command line next to it; when the net is newer than the sidecar the
// window says the provenance is unknown rather than showing a stale one. No
// timestamp is stored - the net's own mtime is the date, which is the same
// no-clock-to-get-wrong rule the chat store follows.
void App::blssRefreshNetStatus() {
    blssNetPresent_ = false;
    blssNetAside_ = false;
    blssNetBytes_ = 0;
    blssNetWhen_.clear();
    blssNetArgs_.clear();
    blssNetArgsStale_ = false;
    const std::string path = blssNetPath();
    if (path.empty()) return;
    std::error_code ec;
    blssNetAside_ = fs::is_regular_file(path + ".off", ec);
    if (!fs::is_regular_file(path, ec)) return;
    blssNetPresent_ = true;
    blssNetBytes_ = (size_t)fs::file_size(path, ec);
    const auto netTime = fs::last_write_time(path, ec);
    if (!ec) {
        const auto sys = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            netTime - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
        const std::time_t t = std::chrono::system_clock::to_time_t(sys);
        char buf[64] = {};
        std::tm tmv{};
#ifdef _WIN32
        localtime_s(&tmv, &t);
#else
        localtime_r(&t, &tmv);
#endif
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tmv);
        blssNetWhen_ = buf;
    }
    const std::string side = path + ".args";
    std::ifstream in(side);
    if (!in) return;
    std::getline(in, blssNetArgs_);
    in.close();
    std::error_code ec2;
    const auto sideTime = fs::last_write_time(side, ec2);
    // The sidecar is written after the net, so a net that is NEWER was produced
    // by something else - the CLI, a copy, another branch.
    if (!ec && !ec2 && netTime > sideTime) blssNetArgsStale_ = true;
}

void App::blssWriteNetSidecar(const std::string& netPath, const std::string& command) {
    std::ofstream out(netPath + ".args", std::ios::trunc);
    if (out) out << command << "\n";
}

// --- which net this project ships --------------------------------------------

// THE SWITCH IS ONE FILE. templates.cpp's blssBake() reads <project>/blss.net
// and falls back to the net embedded in the editor, so "use the shipped
// default" IS "there is no blss.net here" - and a radio that pretended to a
// setting the build never reads would be a second answer to a question codegen
// has already answered. Setting one aside is therefore a RENAME and the other
// radio brings it back: ten seconds of training is cheap to redo and still not
// something a single click should destroy. The `.args` / `.meta` sidecars
// travel with it, or a restored net comes back with its provenance detached and
// the window reports "trained outside this editor" about its own work.
void App::blssSetNetAside(bool aside) {
    const std::string net = blssNetPath();
    if (net.empty()) return;
    const std::string off = net + ".off";
    const std::string from = aside ? net : off;
    const std::string to = aside ? off : net;
    std::error_code ec;
    fs::rename(from, to, ec);
    if (ec) {
        blssLastError_ = "could not move " + from + ": " + ec.message();
        return;
    }
    for (const char* side : {".args", ".meta"}) {
        std::error_code e2;
        if (fs::is_regular_file(from + side, e2)) fs::rename(from + side, to + side, e2);
    }
    blssStatusChecked_ = -1.0e9;  // re-read now, not in up to a second
    blssRefreshNetStatus();
    statusMessage_ = aside ? "BLSS: this project now builds with the editor's default net"
                           : "BLSS: this project's own net is back in use";
}

void App::drawBlssNetSource() {
    const std::string net = blssNetPath();
    if (net.empty()) return;

    ImGui::TextUnformatted("Network the build bakes:");
    ImGui::SameLine();
    int mode = blssNetPresent_ ? 1 : 0;
    ImGui::BeginDisabled(blssJob_.running());
    if (ImGui::RadioButton("Use the shipped default", &mode, 0) && blssNetPresent_)
        blssSetNetAside(true);
    ImGui::SameLine();
    // Nothing to select when the project has never been trained AND nothing was
    // set aside - the Train tab is the way in, and the caption below says so.
    ImGui::BeginDisabled(!blssNetPresent_ && !blssNetAside_);
    if (ImGui::RadioButton("This project's own net", &mode, 1) && !blssNetPresent_)
        blssSetNetAside(false);
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    prefHelp(
        "Codegen bakes this project's own blss.net when there is one and the\n"
        "net embedded in the editor when there is not - so this radio moves\n"
        "one file. Choosing the default RENAMES blss.net to blss.net.off (with\n"
        "its provenance sidecars); choosing your own net renames it back.\n"
        "Nothing is deleted and nothing is retrained.\n"
        "\n"
        "The shipped default was fitted on seven example projects AND the\n"
        "built-in bestiary and scores +0.29 dB on a project it has never seen,\n"
        "against +0.31 dB for that project's own net - a tie inside fold sds of\n"
        "0.37 and 0.34. So the default is a real answer, not a placeholder, and\n"
        "the game's boot log names whichever one it got.");

    if (blssNetPresent_) return;

    // THE DEFAULT'S PROVENANCE, READ OUT OF THE SIDECAR IT SHIPPED WITH rather
    // than typed here - the one thing a line about the shipped net must not do
    // is drift from the shipped net. Cached: it parses an embedded string and
    // cannot change while the editor runs.
    static const blss::Provenance kDef = blss::defaultProvenance();
    if (!kDef.present) {
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
                           "    The editor's own net could not be read - this build would fall "
                           "back to RANDOM weights.\n    kNetVersion or the topology moved and "
                           "resources/blss-default.net was not refitted.");
        return;
    }
    // "seven example projects and the built-in bestiary", counted rather than
    // stated, so a refit that changes the corpus changes this line with it.
    int projects = 0;
    bool bestiary = false;
    {
        std::istringstream in(kDef.corpus);
        std::string tok;
        while (in >> tok) {
            if (tok == "bestiary")
                bestiary = true;
            else
                ++projects;
        }
    }
    char corpus[128];
    std::snprintf(corpus, sizeof(corpus), "%d project%s%s", projects, projects == 1 ? "" : "s",
                  bestiary ? " and the built-in bestiary" : "");
    ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1.0f),
                       "    the editor's own net  -  %s, %d shots, %s, sub-pixel jitter %s",
                       corpus, kDef.shots, blss::scaleName(kDef.scale).c_str(),
                       kDef.jitter > 0 ? "on" : "off");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Corpus:\n  %s\n\nFitted with:\n  %s\n\nIt carries no timestamp and no "
                          "editor version on purpose:\nre-running that command reproduces both "
                          "files byte for byte.",
                          kDef.corpus.c_str(), kDef.command.c_str());
    ImGui::TextDisabled(
        "    Measured leave-one-PROJECT-out at +0.29 dB on a project it has never seen, against\n"
        "    +0.31 dB for that project's own net - a tie inside fold sds of 0.37 and 0.34.");
    if (blssNetAside_)
        ImGui::TextDisabled("    This project's own net is set aside as %s.off - the other radio "
                            "brings it back.",
                            blssNetName_);

    if (ImGui::Button("Retrain for this project", ImVec2(scaled(200.0f), 0)))
        blssTabSelect_ = kTabTrain;
    ImGui::SameLine();
    ImGui::TextDisabled("worth a fraction of a dB on a scene that HAS a ceiling - an "
                        "optimisation,\nnot a prerequisite. Ten seconds; the buttons below say "
                        "whether there is anything to win.");
}

// --- running a verb ----------------------------------------------------------

std::vector<std::string> App::blssCommonArgs() const {
    std::vector<std::string> a;
    // THE PROJECT DIRECTORY IS POSITIONAL AND IT COMES FIRST, because parseCli
    // takes the first bare word as the corpus to render.
    //
    // ABSOLUTE, and it has to be for two reasons. The job runs with cwd = the
    // project directory, so a Project::dir that came in relative (the CLI can
    // open one that way) would be resolved against ITSELF and find nothing. And
    // this string is what the provenance sidecar records next to blss.net -
    // "trained on ." is not an answer to "trained on what".
    if (blssCorpusProject_) {
        std::error_code ec;
        const fs::path abs = fs::absolute(project_.dir, ec);
        a.push_back(ec ? project_.dir : abs.string());
    }
    a.push_back("--assets");
    a.push_back(blssAssets_);
    a.push_back("--sharpen");
    a.push_back(numArg(project_.settings.blssSharpen));
    if (project_.settings.blssScale == 1) a.push_back("--scale-1x2");
    // State the jitter choice rather than letting the trainer resolve it. On a
    // project corpus this is a NO-OP - the tool would read the same blssJitter
    // out of the same project - but it is what puts the choice on the command
    // line the sidecar keeps, which is the only way drawBlssProvenanceDrift can
    // later tell a jitter-off net from a jitter-on one. The bestiary keeps its
    // own default (jitter on): its fold tables are published numbers, and this
    // project's sampler is not a fact about them.
    if (blssCorpusProject_)
        a.push_back(project_.settings.blssJitter ? "--jitter" : "--no-jitter");
    return a;
}

// --- which corpus, and it is the decision this whole window turns on ----------

void App::drawBlssCorpusChoice() {
    ImGui::TextUnformatted("Corpus:");
    ImGui::SameLine();
    int mode = blssCorpusProject_ ? 0 : 1;
    ImGui::BeginDisabled(blssJob_.running());
    if (ImGui::RadioButton("This project's own scenes", &mode, 0)) blssCorpusProject_ = true;
    ImGui::SameLine();
    if (ImGui::RadioButton("Procedural bestiary", &mode, 1)) blssCorpusProject_ = false;
    ImGui::EndDisabled();
    prefHelp(
        "WHICH FRAMES THE NETWORK IS FITTED TO, and the measurement that makes\n"
        "this the first control in the window rather than a footnote.\n"
        "\n"
        "The console runs YOUR scene. A net fitted to the built-in procedural\n"
        "bestiary ALONE is a lottery on one: measured over seven projects it is\n"
        "-0.34 dB on average against plain bilinear and -1.09 dB at worst,\n"
        "i.e. worse than switching the feature off. The net the editor SHIPS\n"
        "is not that net - its corpus is the bestiary and seven real projects\n"
        "together, and it scores +0.29 dB on a project it has never seen.\n"
        "\n"
        "The mechanism is not subtle: texDetail is identically ZERO over all\n"
        "16 128 tiles of that project (it has no textures) and it is the\n"
        "bestiary's channel most correlated with the temporal weight, while\n"
        "edgeDens saturates in 63% of its tiles against 29% of the bestiary's.\n"
        "The bestiary net's temporal gate is driven by inputs that are out of\n"
        "range, so it asks for 62-93% temporal occupancy where the oracle asks\n"
        "for 7-30%.\n"
        "\n"
        "The bestiary is the FALLBACK - it is what a project with nothing\n"
        "drawable falls back to, and what the docs' 13-shot fold tables were\n"
        "measured on. It is not the net to ship.");
    if (blssCorpusProject_) {
        // ONE LINE, not seven. What the walk covers now belongs on the Training
        // shots tab, which is where a reader can act on it - and every line
        // spent here is a line the tabs below do not get, on a window whose
        // header had grown taller than its content.
        ImGui::TextDisabled("    %s", project_.dir.c_str());
        ImGui::TextDisabled(
            "    Primitives, static .obj, the terrain and animated .glb/.fbx. The Training "
            "shots tab decides\n    which camera moves it sees - and lets you add your own.");
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
                           "    13 hand-written procedural shots. A net fitted to these ALONE is "
                           "a lottery on a real\n    project: -0.34 dB mean and -1.09 dB worst "
                           "over seven of them. Use it to reproduce the\n    documented fold "
                           "tables, not to ship a game - the net the editor ships holds these "
                           "AND\n    seven real projects.");
    }
}

// The same fact, one line, above every table that was produced by a run - so
// "which corpus is this?" never needs a scroll back up to the header.
void App::drawBlssCorpusReminder() {
    if (blssCorpusProject_)
        ImGui::TextDisabled("Corpus: this project's own scenes (%s).", project_.dir.c_str());
    else
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
                           "Corpus: the procedural bestiary, NOT this project.");
}

// How many camera moves the corpus will have. Only the cross-validation cost
// estimate needs it (its fold count defaults to one per shot), and it is a
// GUESS on a project - blssscene derives six moves per scene from the bounds
// and the player start, plus any authored Cutscene Director camera track, and
// only the tool knows how many of those resolved.
int App::blssExpectedShots() const {
    if (!blssCorpusProject_) return 13;  // the bestiary, docs/neural-upscaler.md
    // THE SAME DERIVATION THE TRAINING SHOTS TAB DRAWS, not a second guess: the
    // author can now switch moves off and add vantages, so "six per scene" is
    // no longer even approximately right - and this number is what every cost
    // estimate in the window divides by.
    int live = 0;
    for (const PlannedShot& s : planShots(project_))
        if (s.problem.empty()) ++live;
    return std::max(1, live);
}

void App::blssStart(blssui::Kind kind, const std::vector<std::string>& args, int epochs) {
    if (!hasProject_ || blssJob_.running()) return;
    const std::string exe = platform::exePath();
    if (exe.empty()) {
        blssLastError_ = "The editor could not locate its own executable.";
        return;
    }
    blssLastError_.clear();
    blssPendingNet_.clear();
    if (kind == blssui::Kind::Train) blssPendingNet_ = blssNetPath();
    blssJob_.start(kind, exe, args, project_.dir, epochs);
}

// Called every frame from drawUI, NOT from the window body - a run that
// finishes while the tab is shut still lands (the giBakerPoll rule).
void App::blssPoll() {
    // The in-process half, polled from here for the same reason the subprocess
    // half is: a run that finishes while the window is shut still has to land.
    blssCoverageTick();
    // A different project means different everything: the net beside it, the
    // images it dumped, the tables measured against it. Detected here rather
    // than in the window body so the window cannot be opened onto the previous
    // project's numbers.
    const std::string dump =
        hasProject_ ? (fs::path(project_.dir) / "blss-compare").string() : std::string();
    if (dump != blssDumpDir_) {
        blssDumpDir_ = dump;
        blssEval_ = blssui::EvalTable{};
        blssSummary_ = blssui::EvalSummary{};
        blssCv_ = blssui::CvTable{};
        blssFeat_ = blssui::FeatureTable{};
        blssHealth_ = blssui::CorpusHealth{};
        blssProbe_ = blssui::ProbeTable{};
        blssProbeVerdict_ = blssui::ProbeVerdict{};
        blssProbeLine_[0] = '\0';
        blssProbeSource_.clear();
        blssProbeNote_.clear();
        blssScenes_.clear();
        blssShotSel_ = -1;
        blssLookThrough_ = false;
        blssCov_ = blss::CoverageReport{};
        blssSpeed_ = blssui::SpeedEstimate{};
        blssLastError_.clear();
        blssImagesDirty_ = true;
        blssStatusChecked_ = -1.0e9;
    }
    if (blssJobSeen_ == blssJob_.version()) return;
    blssJobSeen_ = blssJob_.version();
    const std::string text = blssJob_.log();
    const blssui::Kind k = blssJob_.kind();
    // WHAT THE CORPUS LOADER SAID IT FOUND, from whichever verb just ran - all
    // of them print it. It is the only way the Training shots tab can tell "the
    // trainer honoured my plan" from "the trainer is still shooting its six
    // defaults", which are otherwise indistinguishable from the outside.
    {
        const std::vector<blssui::CorpusScene> scenes = blssui::parseCorpusScenes(text);
        if (!scenes.empty()) blssScenes_ = scenes;
    }
    // WHICH NET THE RUN ACTUALLY LOADED, from its own announce line. Every verb
    // that evaluates a network prints it, and with a default shipping it is the
    // only thing that separates "I measured my project's net" from "I measured
    // the editor's net on my project" - the two look identical in a table.
    {
        const blssui::NetSource ns = blssui::parseNetSource(text);
        if (ns.ok) blssNetSource_ = ns;
    }
    const std::vector<std::string> errs = blssui::parseErrors(text);
    blssLastError_.clear();
    for (const std::string& e : errs) blssLastError_ += (blssLastError_.empty() ? "" : "\n") + e;

    switch (k) {
        case blssui::Kind::Eval:
        case blssui::Kind::Headroom: {
            blssEval_ = blssui::parseEval(text);
            // TWO SOURCES, and the tool's own arithmetic wins where it exists.
            // `[blss] verdict ...` is one line printed by the run itself over
            // the whole corpus; summarise() re-derives the same figures from
            // whatever the table parser managed to read back, and additionally
            // knows whether there was a BLSS row at all. So: take the ceiling
            // from the line when it is there, and the net half from the table.
            blssSummary_ = blssui::summarise(blssEval_);
            const blssui::EvalSummary line = blssui::parseVerdictLine(text);
            if (line.ok && !blssSummary_.ok) {
                blssSummary_ = line;
            } else if (line.ok) {
                blssSummary_.bilinear = line.bilinear;
                blssSummary_.oracle = line.oracle;
                blssSummary_.native = line.native;
                blssSummary_.oracleMargin = line.oracleMargin;
                blssSummary_.oraclePasses = line.oraclePasses;
            }
            blssImagesDirty_ = true;
            break;
        }
        case blssui::Kind::Cv: blssCv_ = blssui::parseCv(text); break;
        case blssui::Kind::Features:
            blssFeat_ = blssui::parseFeatures(text);
            // THE THIRD VERDICT, derived once here for the same reason the
            // speed one is: a sentence a reader will quote must not shimmer
            // between two roundings. An empty table gives Unknown, which reads
            // as unmeasured rather than as a pass.
            blssHealth_ = blssui::corpusHealth(blssFeat_);
            // The probe table only exists when the run carried --probe, so a
            // plain Inputs run correctly clears it.
            blssProbe_ = blssui::parseProbe(text);
            blssProbeVerdict_ = blssui::probeVerdict(blssProbe_);
            break;
        case blssui::Kind::Train:
            if (blssJob_.exitCode() == 0 && !blssPendingNet_.empty())
                blssWriteNetSidecar(blssPendingNet_, blssJob_.command());
            break;
        case blssui::Kind::Emit:
        case blssui::Kind::None: break;
    }
    blssRefreshNetStatus();
    if (blssJob_.exitCode() == 0)
        statusMessage_ = std::string("BLSS: ") + blssui::kindName(k) + " finished";
}

// --- comparison images -------------------------------------------------------

void App::blssReleaseImages() {
    for (BlssImage& im : blssImages_)
        if (im.tex) glDeleteTextures(1, &im.tex);
    blssImages_.clear();
    if (blssDiffTex_) glDeleteTextures(1, &blssDiffTex_);
    blssDiffTex_ = 0;
    blssDiffA_ = blssDiffB_ = -1;
    blssDiffAmpBuilt_ = -1.0f;
}

// |A - B|, amplified. THE ONE VIEW THAT MAKES A 0.4 dB GAP VISIBLE: side by
// side, and even under the wipe, two reconstructions of the same frame look
// identical to a reader who is not already looking for the difference - which
// is exactly the reader this window is for. At 8x the disagreement is a picture
// of WHERE the network spent its passes.
//
// Computed on the CPU from the pixels blssReloadImages kept, and only when the
// pair or the amplification changes: it is a full-image pass, not a per-frame
// one.
void App::blssRebuildDiff() {
    if (blssImgA_ < 0 || blssImgB_ < 0 || blssImgA_ >= (int)blssImages_.size() ||
        blssImgB_ >= (int)blssImages_.size())
        return;
    const BlssImage& A = blssImages_[(size_t)blssImgA_];
    const BlssImage& B = blssImages_[(size_t)blssImgB_];
    if (blssDiffA_ == blssImgA_ && blssDiffB_ == blssImgB_ &&
        blssDiffAmpBuilt_ == blssDiffAmp_ && blssDiffTex_)
        return;
    blssDiffA_ = blssImgA_;
    blssDiffB_ = blssImgB_;
    blssDiffAmpBuilt_ = blssDiffAmp_;
    blssDiffPeak_ = blssDiffMean_ = 0.0;
    // Different sizes means different things - the weight field is 16x14 and
    // the frames are 512x448 - so there is nothing honest to subtract.
    if (A.w != B.w || A.h != B.h || A.px.empty() || B.px.empty()) {
        if (blssDiffTex_) glDeleteTextures(1, &blssDiffTex_);
        blssDiffTex_ = 0;
        blssDiffW_ = blssDiffH_ = 0;
        return;
    }
    const size_t n = (size_t)A.w * A.h;
    std::vector<unsigned char> out(n * 4, 255);
    double sum = 0.0;
    int peak = 0;
    for (size_t i = 0; i < n; ++i) {
        for (int c = 0; c < 3; ++c) {
            const int d = std::abs((int)A.px[i * 4 + c] - (int)B.px[i * 4 + c]);
            sum += d;
            peak = std::max(peak, d);
            const int v = (int)(d * blssDiffAmp_ + 0.5f);
            out[i * 4 + (size_t)c] = (unsigned char)std::min(255, v);
        }
    }
    // The RAW difference, not the amplified one - the amplification is a
    // magnifying glass, and a caption quoting what it shows would be a number
    // about the magnifying glass.
    blssDiffMean_ = sum / (double)(n * 3);
    blssDiffPeak_ = peak;
    if (!blssDiffTex_) glGenTextures(1, &blssDiffTex_);
    glBindTexture(GL_TEXTURE_2D, blssDiffTex_);
    glUploadTexRgba(A.w, A.h, out.data());
    blssDiffW_ = A.w;
    blssDiffH_ = A.h;
}

void App::blssReloadImages() {
    blssReleaseImages();
    blssImagesDirty_ = false;
    if (blssDumpDir_.empty()) return;
    for (const DumpImage& d : kDumpImages) {
        const std::string path = (fs::path(blssDumpDir_) / d.file).string();
        std::error_code ec;
        if (!fs::is_regular_file(path, ec)) continue;
        blss::Image img;
        if (!blss::readPng(img, path) || img.w <= 0 || img.h <= 0) continue;
        BlssImage im;
        im.label = d.label;
        im.tip = d.tip;
        im.path = path;
        im.w = img.w;
        im.h = img.h;
        im.px = img.px;  // kept for the difference view - see blssRebuildDiff
        glGenTextures(1, &im.tex);
        glBindTexture(GL_TEXTURE_2D, im.tex);
        glUploadTexRgba(img.w, img.h, img.px.data());
        // The weight field is one pixel per tile; LINEAR would smear the very
        // per-tile decision it exists to show.
        if (img.w <= 64) {
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        }
        blssImages_.push_back(im);
    }
    if (blssImgA_ >= (int)blssImages_.size()) blssImgA_ = 0;
    if (blssImgB_ >= (int)blssImages_.size()) blssImgB_ = 0;
    // Default the A/B pair to the comparison that answers the question: what
    // bilinear gives you against what the network gives you.
    for (int i = 0; i < (int)blssImages_.size(); ++i) {
        if (blssImages_[i].label == std::string("bilinear (the baseline)")) blssImgA_ = i;
        if (blssImages_[i].label == std::string("BLSS (the trained net)")) blssImgB_ = i;
    }
}

// ---------------------------------------------------------------- the window ---

void App::drawBlssWindow() {
    if (!showBlss_ || !hasProject_) {
        // GIVE THE CAMERA BACK when the window goes away. "Look through this
        // shot" parks the viewport at a training vantage and holds it there
        // every frame; closing the window that explains why would otherwise
        // leave a camera nobody can move and no visible reason for it.
        blssLookThrough_ = false;
        return;
    }
    // CLAMPED TO THE SCREEN, and that is not cosmetic: `scaled()` multiplies by
    // the UI scale, so at the 250 % a 4K laptop runs this window's nominal
    // 1060x820 asks for 2650x2050 and opens TALLER THAN THE FRAMEBUFFER. The
    // bottom of it - the output pane every table in here is parsed out of, and
    // now the shot plan - is then simply unreachable, and it is invisible from
    // the code because the literals look modest.
    const ImVec2 room = ImGui::GetMainViewport()->WorkSize;
    ImGui::SetNextWindowSize(ImVec2(std::min(scaled(1060.0f), room.x * 0.95f),
                                    std::min(scaled(820.0f), room.y * 0.95f)),
                             ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Neural Upscaler (BLSS)", &showBlss_)) {
        ImGui::End();
        return;
    }
    if (blssAssets_[0] == '\0') {
        const std::string a = discoverAssets();
        std::snprintf(blssAssets_, sizeof(blssAssets_), "%s", a.c_str());
    }
    // The net status is two stat() calls plus a one-line read; throttle it so a
    // window left open does not poll the disk at frame rate.
    const double now = ImGui::GetTime();
    if (now - blssStatusChecked_ > 1.0) {
        blssStatusChecked_ = now;
        blssRefreshNetStatus();
    }

    // THE THREE BANDS, AND WHICH OF THEM MAY GROW. This window is a header, a
    // tab strip and the tool's raw output, and until now the tabs got whatever
    // the other two left - which is the wrong way round, because the tabs are
    // where the TABLES are and reading them is the entire point of the window.
    //
    // Measured at a 1600x900 window (791 px of body, i.e. TIGHTER than the
    // 1017 px work area of a 1080p screen): the header is 514 px once a
    // coverage answer renders, the output pane takes its 150, and the tab child
    // came out **111 px**. At that height the Evaluate tab's own controls - the
    // Network field, Frames, the deadzone, *Run the evaluation* - are submitted
    // BELOW the child's bottom edge, so they are neither visible nor clickable
    // (`--ui-script` reports rects outside the child and a click on one lands on
    // whatever is really there, which is how this survived a scripted check).
    //
    // So both of the other bands are now bounded and the tabs get a FLOOR.
    const float body = ImGui::GetContentRegionAvail().y;
    const float outGap = scaled(14.0f);  // the splitter plus its two spacings
    // The output pane is the raw stdout every table is parsed out of - worth
    // having on screen, never worth two thirds of the window. It was 70 % of
    // what the header left over.
    const float outMax = std::max(scaled(60.0f), body * 0.35f);
    const float outH = std::clamp(blssOutputH_, scaled(60.0f), outMax);
    const float rest = std::max(scaled(120.0f), body - outH - outGap);
    // What is left is split header/tabs, tabs first. The header's own content
    // grows by a screenful the moment an answer renders, so it is CAPPED and
    // scrolls past the cap; its top - the net source, the corpus switch and the
    // three question buttons - stays where it was.
    const float tabsMin = std::min(scaled(420.0f), rest * 0.60f);
    const float headMax = std::max(scaled(90.0f), rest - tabsMin);
    ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f), ImVec2(FLT_MAX, headMax));
    ImGui::BeginChild("blsshdr", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AutoResizeY);
    drawBlssHeader();
    ImGui::EndChild();

    ImGui::BeginChild("blsstabs", ImVec2(0, -(outH + outGap)), false);
    if (ImGui::BeginTabBar("blsstabbar")) {
        // A click on a verdict thumbnail (or on "Train the network" under the
        // headroom answer) has to be able to take the reader to the tab that
        // shows it, or the strip is decoration. Consumed here, once.
        const int want = blssTabSelect_;
        blssTabSelect_ = -1;
        const auto flag = [&](int i) {
            return want == i ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
        };
        if (ImGui::BeginTabItem("Training shots", nullptr, flag(kTabShots))) {
            drawBlssShotsTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Train", nullptr, flag(kTabTrain))) {
            drawBlssTrainTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Evaluate", nullptr, flag(kTabEval))) {
            drawBlssEvalTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Cross-validate", nullptr, flag(kTabCv))) {
            drawBlssCvTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Compare", nullptr, flag(kTabCompare))) {
            drawBlssImagesTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Inputs", nullptr, flag(kTabInputs))) {
            drawBlssFeaturesTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Console probe", nullptr, flag(kTabProbe))) {
            drawBlssProbeTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Project settings", nullptr, flag(kTabSettings))) {
            ImGui::Spacing();
            if (drawBlssSettings(project_.settings)) commitChange();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::EndChild();

    // The splitter, then the tool's own output. It is always on screen because
    // every table above is PARSED out of it - a number with its source one
    // glance away is falsifiable, a number without one is trusted.
    ImGui::InvisibleButton("##blsssplit", ImVec2(-1, scaled(6.0f)));
    if (ImGui::IsItemActive()) blssOutputH_ -= ImGui::GetIO().MouseDelta.y;
    if (ImGui::IsItemHovered() || ImGui::IsItemActive())
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
    drawBlssOutput(outH);
    ImGui::End();
}

void App::drawBlssHeader() {
    const ProjectSettings& s = project_.settings;
    if (!s.blssEnabled)
        ImGui::TextDisabled(
            "The upscaler is OFF for this project - nothing here reaches the game until "
            "it is on (Project settings tab).");

    // WHICH OF THE TWO NETS THIS PROJECT SHIPS. This used to be a status line
    // whose worst branch read "none - the game will be built with RANDOM
    // weights"; a trained net now ships with the editor, so that state is
    // unreachable and the question became a choice.
    drawBlssNetSource();
    if (blssNetPresent_) {
        // WEIGHTS AND TOPOLOGY, not a byte count. "500 bytes" tells a reader
        // nothing they can use; "123 weights, 6->12->3" is the same file
        // described in the units the rest of this window and the documentation
        // are written in. The count is derived from the topology rather than
        // typed, so it follows kFeatures/kHidden the way every other figure in
        // this feature does.
        constexpr int kWeights = blss::kHidden * blss::kFeatures + blss::kHidden +
                                 blss::kOutputs * blss::kHidden + blss::kOutputs;
        ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1.0f), "%s  -  %d weights, %d->%d->%d, %s",
                           blssNetName_, kWeights, blss::kFeatures, blss::kHidden,
                           blss::kOutputs, blssNetWhen_.c_str());
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "%zu bytes on disk: a four-byte magic, a u32 version and the weights\n"
                "in declaration order. It carries NO topology, so a file written by a\n"
                "differently shaped net cannot be told apart by reading it - the\n"
                "version number is the only guard.",
                blssNetBytes_);
        if (blssNetArgs_.empty())
            ImGui::TextDisabled(
                "    Trained outside this editor - a blss.net records nothing about how it "
                "was made.");
        else if (blssNetArgsStale_)
            ImGui::TextDisabled(
                "    Replaced since this editor last trained it; the recorded settings are "
                "stale.");
        else {
            ImGui::TextDisabled("    Trained here with:");
            ImGui::SameLine();
            ImGui::TextWrapped("%s", blssNetArgs_.c_str());
            // A blss.net records nothing about the settings it was fitted
            // under, so the sidecar is the ONLY place a mismatch can be caught -
            // and a mismatch is silent everywhere else: the game reconstructs
            // happily with a net trained for a different sharpen strength or a
            // different render scale, just worse.
            drawBlssProvenanceDrift();
        }
    }
    ImGui::Spacing();
    drawBlssCorpusChoice();
    ImGui::Spacing();
    drawBlssHappyPath();
    ImGui::Spacing();
    // BAKING IT IN is one button because the build already does it: codegen
    // reads <project>/blss.net every time and rewrites inc/blss_net.gen.hpp, so
    // training here IS baking. This runs `--blss-emit` for the case where the
    // header itself is wanted - to read it, or to refresh it without a build.
    ImGui::BeginDisabled(blssJob_.running() || !blssNetPresent_);
    if (ImGui::Button("Write inc/blss_net.gen.hpp", ImVec2(scaled(230.0f), 0))) {
        std::vector<std::string> a{"--blss-emit", "-i", blssNetName_, "-o",
                                   (fs::path("inc") / "blss_net.gen.hpp").string()};
        blssStart(blssui::Kind::Emit, a, 0);
    }
    ImGui::EndDisabled();
    prefHelp(
        "Exactly `--blss-emit`: the trained weights as a C++ constant table plus\n"
        "the forward pass, which is what the PS2 compiles. The BUILD does this for\n"
        "you from the same blss.net every time, so the button is for looking at\n"
        "the header or refreshing it without a build. It runs the emitter's\n"
        "self-test first - a weight of exactly zero used to be spelled `0F` and\n"
        "no compiler accepts that.");
    ImGui::SameLine();
    if (ImGui::Button("Reveal the project folder", ImVec2(scaled(200.0f), 0)))
        platform::revealInFileManager(project_.dir);

    ImGui::Spacing();
    if (blssJob_.running()) {
        const float p = blssJob_.progress();
        if (p >= 0.0f)
            ImGui::ProgressBar(p, ImVec2(-scaled(160.0f), 0.0f));
        else
            ImGui::ProgressBar(-1.0f * (float)ImGui::GetTime(), ImVec2(-scaled(160.0f), 0.0f),
                               "working");
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(scaled(150.0f), 0))) blssJob_.cancel();
        ImGui::Text("%s %s  -  %.0f s", blssui::kindName(blssJob_.kind()),
                    blssJob_.status().c_str(), blssJob_.seconds());
    } else if (blssJob_.kind() != blssui::Kind::None) {
        const int rc = blssJob_.exitCode();
        if (rc == 0)
            ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1.0f), "%s finished in %.0f s",
                               blssui::kindName(blssJob_.kind()), blssJob_.seconds());
        else if (rc == -2)
            ImGui::TextDisabled("%s cancelled after %.0f s", blssui::kindName(blssJob_.kind()),
                                blssJob_.seconds());
        else
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "%s FAILED (exit %d)",
                               blssui::kindName(blssJob_.kind()), rc);
    }
    if (!blssLastError_.empty())
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "%s", blssLastError_.c_str());
    ImGui::Separator();
}

// --- the happy path ----------------------------------------------------------

namespace {
// A flag's value out of a recorded command line. The sidecar holds the exact
// argv the window ran, shell-quoted, so a token match is enough - and a token
// that is not there means the run took the default, which is what `def` is for.
bool argValue(const std::string& cmd, const char* flag, std::string& out) {
    std::string needle = std::string(" ") + flag + " ";
    const size_t at = cmd.find(needle);
    if (at == std::string::npos) return false;
    size_t b = at + needle.size();
    while (b < cmd.size() && cmd[b] == ' ') ++b;
    size_t e = b;
    while (e < cmd.size() && cmd[e] != ' ') ++e;
    out = cmd.substr(b, e - b);
    // The job shell-quotes every argument, so strip whatever it used.
    while (!out.empty() && (out.front() == '"' || out.front() == '\'')) out.erase(0, 1);
    while (!out.empty() && (out.back() == '"' || out.back() == '\'')) out.pop_back();
    return !out.empty();
}
}  // namespace

void App::drawBlssProvenanceDrift() {
    if (blssNetArgs_.empty()) return;
    const ProjectSettings& s = project_.settings;
    std::vector<std::string> drift;
    std::string v;
    // --sharpen: the oracle charges for the two sharpen passes at THIS k, so
    // the labels the net was fitted to depend on it.
    const float trainedSharpen = argValue(blssNetArgs_, "--sharpen", v) ? (float)std::atof(v.c_str())
                                                                       : 0.5f;
    if (std::fabs(trainedSharpen - s.blssSharpen) > 0.005f) {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "--sharpen %.2f, but the project now says %.2f",
                      trainedSharpen, s.blssSharpen);
        drift.push_back(buf);
    }
    // --scale-1x2 is a switch, not a value: absent means 2x2.
    const bool trained1x2 = blssNetArgs_.find("--scale-1x2") != std::string::npos;
    if (trained1x2 != (s.blssScale == 1))
        drift.push_back(std::string("render scale ") + (trained1x2 ? "1x2" : "2x2") +
                        ", but the project now says " + (s.blssScale == 1 ? "1x2" : "2x2"));
    // Sub-pixel jitter is FITTED IN, not switched at runtime: the corpus is
    // rendered through the project's own sampler, so a net trained one way and
    // shipped the other is fitted to a distribution the console never produces.
    // A sidecar written before the window started stating the flag says nothing
    // about jitter, and silence is not drift - hence the "known" test.
    const bool jitterKnown = blssNetArgs_.find("--jitter") != std::string::npos ||
                             blssNetArgs_.find("--no-jitter") != std::string::npos;
    if (jitterKnown) {
        const bool trainedJitter = blssNetArgs_.find("--no-jitter") == std::string::npos;
        if (trainedJitter != s.blssJitter)
            drift.push_back(std::string("sub-pixel jitter ") + (trainedJitter ? "on" : "off") +
                            ", but the project now says " + (s.blssJitter ? "on" : "off"));
    }
    // Which corpus. A net fitted to the bestiary ALONE and shipped on a project
    // is the mistake this whole window is arranged around: measured over seven
    // real projects it averages -0.34 dB and reaches -1.09 dB on the worst.
    const bool trainedOnProject =
        blssNetArgs_.find("--blss-train") != std::string::npos &&
        blssNetArgs_.find(fs::path(project_.dir).filename().string()) != std::string::npos;
    if (!trainedOnProject && !blssNetArgs_.empty())
        drift.push_back("no project directory in the command line - this looks like a "
                        "BESTIARY-trained net");
    if (drift.empty()) return;
    ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.35f, 1.0f),
                       "    This net does not match the project's current settings - RETRAIN:");
    for (const std::string& d : drift)
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.35f, 1.0f), "      - %s", d.c_str());
    ImGui::TextDisabled("    A blss.net stores no settings, so nothing downstream will "
                        "complain: the game just reconstructs worse.");
}

// --- the speed half: how much fill does this scene ask the GS for -----------

// IN-PROCESS, and it is the one verb in this window that is. Every other button
// here spawns `tyrax-editor --blss-<verb>` because the driver it needs lives in
// blss.cpp's anonymous namespace and a re-implementation would be a second
// answer to "what does --blss-eval measure". This one has no CLI verb behind
// it, writes no file, and takes about a second: `blss::measureCoverage` IS the
// public API, there is nothing to re-implement, and a process would cost more
// than the work.
void App::blssStartCoverage() {
    if (!hasProject_ || blssCovRunning_.load()) return;
    if (blssCovThread_.joinable()) blssCovThread_.join();
    blssCovCancel_.store(false);
    blssCovRunning_.store(true);
    blssCovStarted_ = ImGui::GetTime();
    blssCovSeconds_ = 0.0;
    blss::CoverageConfig cfg;
    cfg.projectDir = project_.dir;
    // The project's OWN raster, so the coverages are per the frame the console
    // will present rather than per a nominal 512x448.
    const DisplayModeInfo& dm = project::displayModeInfo(project::bootDisplayMode(project_.settings));
    cfg.outW = dm.bufW;
    cfg.outH = dm.halfHeight ? dm.logicalH / 2 : dm.logicalH;
    blssCovThread_ = std::thread([this, cfg]() {
        blssCovOut_ = blss::measureCoverage(cfg, &blssCovCancel_);
        // The version bump is the HANDOVER, and it must be the last write: the
        // UI thread reads blssCovOut_ only after seeing it move (the Runner /
        // giBakerPoll idiom).
        blssCovVersion_.fetch_add(1);
        blssCovRunning_.store(false);
    });
}

void App::blssCoverageTick() {
    if (blssCovRunning_.load()) {
        blssCovSeconds_ = ImGui::GetTime() - blssCovStarted_;
        return;
    }
    if (blssCovSeen_ == blssCovVersion_.load()) return;
    blssCovSeen_ = blssCovVersion_.load();
    if (blssCovThread_.joinable()) blssCovThread_.join();
    blssCov_ = blssCovOut_;
    // The estimate is derived ONCE, here, rather than per frame: a verdict a
    // reader is going to quote must not shimmer between two roundings.
    // Priced at the raster the report was COUNTED at, which it echoes back -
    // a coverage is a fraction of one screen and one full-screen pass costs
    // 14.3 % more at 512x512 than at 512x448, so the break-even moves with the
    // display mode (13.1 against 11.4).
    blssSpeed_ = blssCov_.ok ? blssui::speedFrom(blssCov_.mean,
                                                 (double)blssCov_.outW * blssCov_.outH)
                             : blssui::SpeedEstimate{};
    if (blssCov_.ok)
        statusMessage_ = "BLSS: coverage estimate finished";
    else if (!blssCov_.err.empty())
        blssLastError_ = "coverage estimate: " + blssCov_.err;
}

// The per-shot table, behind a collapsing header. A project whose mean is 15
// because one scene rasterises 30 and another rasterises 1 has been told
// something useful only if it can see which is which - examples/showcase is
// exactly that project.
void App::drawBlssCoverageDetail() {
    if (!blssCov_.ok) return;
    if (!ImGui::CollapsingHeader("Coverage per camera move, and what the count could not see"))
        return;
    // WHAT IT CANNOT SEE, said out loud rather than quietly left out of the
    // total. Every item here can only make the real figure BIGGER, which is
    // exactly what makes the estimate a floor rather than a guess - and it is
    // one click from the number instead of five lines above the tabs.
    std::string blind =
        "    Not counted: the sky dome (about one more coverage); particle lifetimes and "
        "drift - the emitter\n    term places a pool through its spawn box and averages the "
        "per-kind size curve rather than\n    simulating it";
    if (blssCov_.sawCutout)
        blind += "; alpha-tested cutouts, which the GS rasterises and then throws away "
                 "(counted here as solid)";
    if (blssCov_.sawDisabledEmitter)
        blind += "; emitters that start disabled, which a flow node can turn on";
    if (!blssCov_.sawAnimated)
        blind += "; nothing animated was found, so a character walking into frame is not in "
                 "this figure";
    blind +=
        ".\n    The HUD, the menus and the post effects are NOT counted - and NOT reduced "
        "either: BLSS shrinks\n    the 3D scene's fill and nothing else.";
    ImGui::TextDisabled("%s", blind.c_str());
    if (ImGui::BeginTable("blsscovtbl", 6,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("scene");
        ImGui::TableSetupColumn("camera move");
        ImGui::TableSetupColumn("kind");
        ImGui::TableSetupColumn("geometry");
        ImGui::TableSetupColumn("emitters");
        ImGui::TableSetupColumn("worst frame");
        ImGui::TableHeadersRow();
        for (const blss::CoverageShot& s : blssCov_.shots) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(s.scene.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(s.name.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(s.move.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%.1f", s.geom);
            ImGui::TableNextColumn();
            ImGui::Text("%.1f", s.emit);
            ImGui::TableNextColumn();
            const bool over = s.peak >= blssSpeed_.breakEven;
            ImGui::TextColored(over ? ImVec4(0.45f, 0.85f, 0.45f, 1.0f)
                                    : ImVec4(0.65f, 0.65f, 0.65f, 1.0f),
                               "%.1f", s.peak);
        }
        ImGui::EndTable();
    }
    ImGui::TextDisabled(
        "    %d frame(s) over %zu camera move(s), %zu triangle(s). Counted at a quarter-linear\n"
        "    raster - a coverage is a ratio, so the answer is the same to two decimals at "
        "512x448.\n"
        "    These rows ARE the shot plan: switch a move off, or add the player's own vantage,\n"
        "    on the Training shots tab and this table follows - the count and the trainer read\n"
        "    one plan (project::blssResolveShot), so a row here is a frame the corpus renders.",
        blssCov_.frames, blssCov_.shots.size(), blssCov_.triangles);
}

// ONE BUTTON, ABOVE THE TABS, AND IT IS WORTH MORE THAN THE REST OF THE WINDOW.
// The only question most people have is "should I turn this on", the answer has
// always been in `--blss-eval`'s oracle row, and until now the window's advice
// was to run Evaluate first - which could not be done at all without a
// blss.net, which only Train could produce, which is twenty minutes of work
// before the first fact. The net-free `--blss-eval` needs no network: the
// ORACLE is the best any per-tile weighting can do under the exact GS
// composite, so it bounds every network that could ever be trained here.
//
// AND THERE ARE TWO QUESTIONS, not one. That oracle row answers "will the
// PICTURE improve" and says nothing at all about speed - which is now the
// feature's actual selling point and the half with a hardware model behind it.
// So the second button counts the scene's overdraw and the two answers are
// combined below into one recommendation.
void App::drawBlssHappyPath() {
    const int cores = std::max(1, (int)std::thread::hardware_concurrency());
    const blssui::Cost cost = blssui::estimate(blssui::Kind::Headroom, blssEvalFrames_, 0, cores,
                                               1, 0, blssExpectedShots());
    ImGui::BeginDisabled(blssJob_.running());
    if (ImGui::Button("Will the picture improve?", ImVec2(scaled(230.0f), scaled(30.0f)))) {
        // NO `-i`. That is the whole feature: with a network the run measures
        // that network, without one it measures the SCENE.
        std::vector<std::string> a{"--blss-eval"};
        for (const std::string& c : blssCommonArgs()) a.push_back(c);
        a.push_back("--frames");
        a.push_back(std::to_string(blssEvalFrames_));
        a.push_back("--deadzone");
        a.push_back(numArg(blssEvalDeadzone_));
        blssStart(blssui::Kind::Headroom, a, 0);
    }
    ImGui::EndDisabled();
    prefHelp(
        "Runs `--blss-eval` on this corpus with NO network and reads the ORACLE\n"
        "row - the best any per-tile weighting can do under the exact GS\n"
        "composite. No network can beat it, so it is the SCENE'S OWN CEILING and\n"
        "it answers 'is there anything here to reconstruct' before you spend an\n"
        "afternoon training something.\n"
        "\n"
        "Some scenes have no ceiling at all: on examples/showcase the oracle\n"
        "itself scores +0.02 dB over plain bilinear at 1.00 passes - soft ground\n"
        "texture, low-poly props, nothing that aliases. That is a fact about the\n"
        "content and no amount of training moves it.\n"
        "\n"
        "It renders the whole corpus, which is most of the cost, so the estimate\n"
        "next to the button is the same corpus render the Train tab pays for.");

    ImGui::SameLine();
    ImGui::BeginDisabled(blssCovRunning_.load());
    if (ImGui::Button("Will the frame get faster?", ImVec2(scaled(230.0f), scaled(30.0f))))
        blssStartCoverage();
    ImGui::EndDisabled();
    prefHelp(
        "Counts how many times over this project's own scenes paint the screen,\n"
        "and puts that against the MEASURED break-even.\n"
        "\n"
        "BLSS trades GS fill for EE work at a price both halves of which were\n"
        "measured on a real PS2: it keeps 24.5 % of the scene's fill (the render\n"
        "is half in EACH axis, so a quarter of the pixels) and costs 4.60 ms of\n"
        "EE plus 0.50 ms of composite fill. At the calibrated 0.587 ms per\n"
        "full-screen blended textured pass that is break-even at 11.5 full-screen\n"
        "coverages. Above the line it is a large win; below it, a straight loss\n"
        "of up to ~5.1 ms a frame.\n"
        "\n"
        "The retention and the cost are FITTED over five hardware load points\n"
        "(two runs per arm), not assumed: saved = 0.7548 x fill - 5.10 ms, with\n"
        "a residual RMS of 0.093 ms.\n"
        "\n"
        "It walks the same scenes and the same camera moves the corpus uses and\n"
        "counts every fragment the rasteriser produces - the GS has no early-z\n"
        "and no backface culling, so a hidden wall and the far side of a box are\n"
        "both real fill.\n"
        "\n"
        "IT IS A FLOOR, not a measurement. What it cannot see is listed under the\n"
        "answer; the console is the only thing that settles it.\n"
        "\n"
        "AND IT IS AN INDEX, NOT MILLISECONDS. On examples/upscaler-lab - the one\n"
        "scene whose fill has also been measured on hardware - the count reads\n"
        "about 1.3x the console's blended-pass equivalents, and it holds that\n"
        "ratio at 1.5, 20, 47 and 79 coverages, so it tracks the real fill and\n"
        "over-states its scale. Two things about it are already known: 0.587 ms\n"
        "was calibrated on a 512x512 buffer while a coverage here is per the\n"
        "project's own raster (512x448 = 14 % fewer pixels), and a magnified 128\n"
        "square puff is cheaper per pixel than the probe's 1:1 framebuffer blit.\n"
        "The headroom above the break-even is what the answer is really about.\n"
        "\n"
        "Run it without the GUI with `tyrax-editor --blss-coverage <projectDir>`.\n"
        "Same function, same numbers, machine-readable [blss] lines.");

    // THE THIRD QUESTION, and the one that catches what the other two cannot
    // see. "Will the picture improve" reads the oracle and "will the frame get
    // faster" counts overdraw; NEITHER of them notices that the corpus a net
    // would be FITTED to is missing a channel entirely - which is how a net
    // came to measure -0.48 dB on a project whose own ceiling was +0.345.
    // (Both figures at ONE sampler. The -0.40/+0.06/+0.77 row this comment used
    // to quote mixed two: the ceiling was measured with jitter ON and the
    // margins with it off, so no two of those three numbers were comparable.)
    ImGui::SameLine();
    ImGui::BeginDisabled(blssJob_.running());
    if (ImGui::Button("Is the corpus good enough?", ImVec2(scaled(230.0f), scaled(30.0f)))) {
        std::vector<std::string> a{"--blss-eval", "--features"};
        for (const std::string& c : blssCommonArgs()) a.push_back(c);
        a.push_back("--frames");
        a.push_back(std::to_string(blssFeatFrames_));
        a.push_back("--fill-weight");
        a.push_back(numArg(blssTrainFill_));
        blssStart(blssui::Kind::Features, a, 0);
    }
    ImGui::EndDisabled();
    prefHelp(
        "Reports the six input channels over this corpus and turns the numbers\n"
        "into sentences: which channel is constant, which sits against its clamp,\n"
        "and what that means for a network fitted to these frames.\n"
        "\n"
        "It is the question the other two buttons cannot answer. A channel that\n"
        "is identically zero over the whole corpus is a channel the network does\n"
        "NOT HAVE - and the console will still feed it a real value. That is the\n"
        "mechanism behind a bestiary-only net's -0.34 dB average (-1.09 at\n"
        "worst) over seven real projects: texDetail is identically zero on five\n"
        "of them while being the bestiary's channel most correlated with the\n"
        "temporal weight.\n"
        "\n"
        "The full per-channel table is on the Inputs tab; what a run says here is\n"
        "the one-line verdict and the findings behind it.");
    // ONE caption for the three, in their own order. Three stacked buttons with
    // a sentence each cost four lines of a header that had already grown taller
    // than the tabs under it - and the three sentences agreed on the half worth
    // saying ("no network needed") and repeated it three times.
    {
        const blssui::Cost feat = blssui::estimate(blssui::Kind::Features, blssFeatFrames_, 0,
                                                   cores, 1, 0, blssExpectedShots());
        // humanDuration() already says "about", so the caption must not say it
        // again - "About about 45 seconds" is what the first draft printed.
        ImGui::TextDisabled(
            "None of the three needs a trained network. In order: %s, about a second, %s "
            "on this machine (%d cores).",
            blssui::humanDuration(cost.total).c_str(), blssui::humanDuration(feat.total).c_str(),
            cores);
    }
    if (blssCovRunning_.load())
        ImGui::TextDisabled("    counting overdraw... %.0f s", blssCovSeconds_);
    ImGui::Spacing();
    drawBlssAnswer();
}

// --- the two verdicts as ONE answer -----------------------------------------

// A reader wants "should I turn this on", and until now this window had two
// separate answers to it: a picture verdict here and no speed verdict anywhere.
// The combination has cases neither half has alone, and the most common one is
// the flat no - no headroom AND below the break-even, which is most scenes.
//
// The arithmetic is blssui::recommend() / blssui::speedFrom(), pure functions
// checkable from the host-only harness. This chooses words and colours.
void App::drawBlssAnswer() {
    const bool haveQ = blssSummary_.ok;
    const bool haveS = blssCov_.ok && blssSpeed_.ok;
    const blssui::Recommendation rec =
        blssui::recommend(blssSummary_, haveQ, blssSpeed_, haveS);

    const ImVec4 bad(1.0f, 0.45f, 0.35f, 1.0f), warn(1.0f, 0.75f, 0.35f, 1.0f),
        good(0.45f, 0.85f, 0.45f, 1.0f), dim(0.65f, 0.65f, 0.65f, 1.0f);
    ImGui::SeparatorText("The answer");
    if (!haveQ && !haveS) {
        ImGui::TextDisabled(
            "    Press them. The first says whether the picture has anything to gain, the "
            "second whether\n    the frame gets shorter, the third whether a network fitted to "
            "this corpus can learn\n    anything at all - and the honest answer for most scenes "
            "is none of the three.");
        // Still drawn, because "not measured" is the answer here and hiding it
        // would let the block look complete when a third of it is missing.
        drawBlssHealth(/*compact=*/true);
        return;
    }
    ImVec4 col = dim;
    switch (rec.verdict) {
        case blssui::Recommendation::Verdict::Off: col = bad; break;
        case blssui::Recommendation::Verdict::On: col = good; break;
        case blssui::Recommendation::Verdict::SpeedOnly: col = good; break;
        case blssui::Recommendation::Verdict::QualityOnly: col = warn; break;
        // Mixed is amber and never green: it is either half an answer or a
        // margin too thin to act on, and a green one of those reads as a yes.
        default: col = warn; break;
    }
    ImGui::TextColored(col, "%s", rec.headline.c_str());
    ImGui::TextWrapped("%s", rec.why.c_str());
    // THE THIRD HALF OF THE ANSWER, and it is deliberately part of the SAME
    // block rather than a separate panel: "the picture has room and the frame
    // gets shorter" is not a recommendation to act on if the corpus a net would
    // be fitted to cannot teach it anything. Unknown until measured, and it
    // says so rather than staying quiet.
    drawBlssHealth(/*compact=*/true);

    // The speed half's own paragraph: the range, the assumption that produces
    // it, and the one hardware A/B that exists, so the reader can see how far
    // the estimate is from a measurement.
    if (haveS) {
        const blssui::SpeedEstimate& sp = blssSpeed_;
        if (sp.band == blssui::SpeedEstimate::Band::Win) {
            ImGui::TextWrapped(
                "    Expect roughly %.1f-%.1fx, the two ends being 'the frame is 60 %% fill' "
                "and 'the frame is nothing but fill'. Measured on examples/upscaler-lab: "
                "1.63x at %.1f blended-pass equivalents (52.95 -> 32.42 ms, real PS2) - which "
                "landed on the LOW end of its own range.",
                sp.lo, sp.hi, blssui::fill::kAnchorCoverages);
        } else if (sp.band == blssui::SpeedEstimate::Band::Marginal) {
            ImGui::TextWrapped(
                "    No speedup is quoted here on purpose: %.1f ms a frame is smaller than the "
                "things this count cannot see, so any multiplier would be a decimal place "
                "with nothing behind it. Break-even is %.1f coverages; the two hardware "
                "points either side of it are examples/upscaler-lab at %.1f (1.63x) and "
                "blssrig at a handful (-5.1 ms).",
                sp.savedMs, sp.breakEven, blssui::fill::kAnchorCoverages);
        } else {
            ImGui::TextWrapped(
                "    The floor of that loss is %.1f ms - BLSS' own bill with no fill at all to "
                "trade against it. Measured: blssrig, a terrain and six slabs, loses by 4.6 ms "
                "of EE plus its composite.",
                blssui::fill::kEeCostMs + blssui::fill::kCompositeGsMs);
        }
        ImGui::TextDisabled(
            "    Mean %.1f coverages, p95 %.1f (geometry %.1f counted + emitters %.1f "
            "estimated). A FLOOR, not\n    a measurement - see below for what it could not "
            "see. Only the console settles it.",
            blssCov_.mean, blssCov_.p95, blssCov_.geomMean, blssCov_.emitMean);
        // WHOSE FRAME IS THIS. The headline is a mean over the corpus' camera
        // moves, and on a scene with real overdraw those moves disagree wildly -
        // this fixture's own six span 36 to 88. A mean over them is nobody's
        // frame, and it was quietly read as one for a week: the hardware A/B ran
        // the game's own camera and the estimator answered about six synthetic
        // ones, and the two numbers were compared as though they described the
        // same picture. So say which cameras, say the spread, and point at the
        // tab where the player's own vantage can be added to the set.
        if (!blssCov_.shots.empty()) {
            double lo = blssCov_.shots.front().geom + blssCov_.shots.front().emit, hi = lo;
            for (const blss::CoverageShot& s : blssCov_.shots) {
                const double t = s.geom + s.emit;
                lo = std::min(lo, t);
                hi = std::max(hi, t);
            }
            ImGui::TextDisabled(
                "    Averaged over the corpus' %zu camera move(s), whose own totals run %.1f to "
                "%.1f - so the mean is\n    no single camera's frame. To put the vantage the "
                "player actually stands at into that set, add it\n    under Training shots > "
                "Your own vantages; the count walks the same plan the trainer does.",
                blssCov_.shots.size(), lo, hi);
        }
        // ...AND WHAT ONE COUNTED COVERAGE IS WORTH, measured rather than
        // assumed. See the kAnchorCoverages block in blss_ui.hpp: the count
        // tracks the console's fill proportionally and over-reads it by about a
        // third, so it is an overdraw INDEX and the break-even it is held
        // against carries that much slack.
        ImGui::TextDisabled(
            "    On the one fixture where both instruments have run, this count reads about "
            "1.3x the hardware's\n    blended-pass equivalents - at every load from 1.5 to 79 "
            "coverages, so it is proportional. Read it\n    as an overdraw index against the "
            "%.1f break-even at this project's %dx%d raster, not as milliseconds.",
            blssSpeed_.breakEven, blssCov_.outW, blssCov_.outH);
        drawBlssCoverageDetail();
    }
    if (haveQ && !haveS)
        ImGui::TextDisabled(
            "    The speed half is the other button, and on this feature it is the half with "
            "the bigger number.");
    if (haveS && !haveQ)
        ImGui::TextDisabled(
            "    The picture half is the other button - it needs no network either.");

    // THE VRAM SIDE, the third measured benefit and the one that is exactly
    // zero at 1x2. Same arithmetic the Project settings tab prints, from the
    // same function, so the two cannot disagree.
    const std::string vram = blssVramLine(project_.settings);
    if (!vram.empty()) ImGui::TextDisabled("%s", vram.c_str());

    // The headroom branch offers the next step; keep that, because "train one"
    // is the action a reader with headroom wants and it is two clicks away.
    if (haveQ && blssSummary_.oracleMargin >= blssui::kNoHeadroomDb && !blssSummary_.haveNet) {
        ImGui::BeginDisabled(blssJob_.running());
        if (ImGui::Button("Train a network for this scene", ImVec2(scaled(240.0f), 0))) {
            blssStartTraining();
            blssTabSelect_ = kTabTrain;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        const int cores = std::max(1, (int)std::thread::hardware_concurrency());
        const blssui::Cost c = blssui::estimate(blssui::Kind::Train, blssTrainFrames_,
                                                blssTrainEpochs_, cores, 1, 0,
                                                blssExpectedShots());
        ImGui::TextDisabled("%s on this machine (%d cores), at the Train tab's settings.",
                            blssui::humanDuration(c.total).c_str(), cores);
    }
    if (!blssCorpusProject_ && haveQ)
        ImGui::TextColored(warn,
                           "    The picture half was measured on the BESTIARY, not on this "
                           "project. It is not an answer about your game.");
}

void App::drawBlssOutput(float height) {
    ImGui::BeginChild("blssout", ImVec2(0, height), true,
                      ImGuiWindowFlags_HorizontalScrollbar);
    const std::string cmd = blssJob_.command();
    if (cmd.empty())
        ImGui::TextDisabled(
            "The tool's output appears here. Every table in this window is parsed out of "
            "it, so this is where you check one.");
    else
        ImGui::TextDisabled("> %s", cmd.c_str());
    const std::string text = blssJob_.log();
    if (!text.empty()) {
        ImGui::TextUnformatted(text.c_str());
        // Follow the tail while a run is live, but leave the scroll alone once
        // it has finished so a table can be read.
        if (blssJob_.running()) ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
}

// ------------------------------------------------------------------- train ---

void App::drawBlssTrainTab() {
    ImGui::Spacing();
    ImGui::TextWrapped(
        "Renders the corpus, labels every frame with the oracle (the weights that minimise "
        "the objective under the exact GS composite), fits the %d-%d-%d MLP to those labels "
        "and writes the result next to the project. The corpus render and the oracle run on "
        "every core the machine has and are seeded, so the same settings give the same "
        "blss.net whatever the core count; the fit itself is sequential and is about a "
        "third of the wall clock. The editor stays usable while it runs.",
        blss::kFeatures, blss::kHidden, blss::kOutputs);
    ImGui::Spacing();
    drawBlssCorpusReminder();
    if (blssCorpusProject_)
        ImGui::TextDisabled(
            "This is the net to ship: the console runs the frames it was fitted on.");
    else
        ImGui::TextColored(
            ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
            "A net fitted to these ALONE is a lottery on a real project: -0.34 dB mean and\n"
            "-1.09 worst over seven of them. Switch the corpus in the header before training\n"
            "one to ship - or ship the editor's own net, whose corpus is these AND seven real\n"
            "projects.");
    ImGui::Spacing();

    ImGui::BeginDisabled(blssJob_.running());
    // The action first, because the defaults below it are the ones every
    // number in the docs was measured at - the common case is to press this.
    // Named "Train the network" and not "Train": the TAB is called Train, and
    // two items with one name in one window is a target a UI script (and a
    // person tabbing through) cannot tell apart - the first scripted run of
    // this window pressed the tab and reported success.
    if (ImGui::Button("Train the network", ImVec2(scaled(190.0f), 0))) blssStartTraining();
    ImGui::SameLine();
    if (ImGui::Button("Restore defaults", ImVec2(scaled(150.0f), 0))) blssRestoreTrainDefaults();
    prefHelp(
        "Every field this window owns: the ones on this tab including the\n"
        "advanced ones, the network file name and materials path, and the\n"
        "Evaluate / Cross-validate / Inputs settings - their four frame counts\n"
        "above all, which are only comparable when they agree.\n"
        "It leaves the CORPUS switch alone: which project you are measuring is\n"
        "a statement about your game, not a training default.");
    ImGui::SameLine();
    // WHAT IT COSTS, before it is pressed. Train, Evaluate and especially
    // Cross-validate are minutes to tens of minutes and used to say nothing at
    // all about their price; the model is blssui::estimate(), calibrated
    // against real runs, and the comment there has the measurements.
    {
        const int cores = std::max(1, (int)std::thread::hardware_concurrency());
        const blssui::Cost cost = blssui::estimate(blssui::Kind::Train, blssTrainFrames_,
                                                   blssTrainEpochs_, cores, 1, 0,
                                                   blssExpectedShots());
        ImGui::TextDisabled("%s on this machine (%d cores).",
                            blssui::humanDuration(cost.total).c_str(), cores);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "About %s to render the corpus, %s to label it with the oracle and\n"
                "%s to fit - the last of which is sequential SGD and is the phase no\n"
                "core count can help. An estimate calibrated on one machine and one\n"
                "project; a scene with far more triangles renders its corpus\n"
                "proportionally slower.",
                blssui::humanDuration(cost.corpus).c_str(),
                blssui::humanDuration(cost.oracle).c_str(),
                blssui::humanDuration(cost.fit).c_str());
    }
    ImGui::Spacing();

    ImGui::SetNextItemWidth(scaled(160));
    ImGui::InputText("Write to", blssNetName_, sizeof(blssNetName_));
    prefHelp(
        "Relative to the project directory. 'blss.net' is the one the build\n"
        "bakes in; any other name is a candidate you can evaluate without\n"
        "replacing what the game ships.");

    ImGui::SeparatorText("Corpus");
    ImGui::SetNextItemWidth(scaled(160));
    ImGui::DragInt("Frames", &blssTrainFrames_, 1.0f, 6, 520);
    prefHelp(
        "Split evenly over the shots, so this and the shot count move\n"
        "together: 156 gives 12 frames each over the bestiary's 13 shots, and\n"
        "26 each over a one-scene project's six camera moves. A shot with three\n"
        "frames teaches the temporal channel almost nothing - its history is\n"
        "one frame deep and the first frame of a shot has none at all.\n"
        "It is linear in every phase: the corpus render, the oracle and the\n"
        "fit all scale with it.");
    drawBlssFrameDrift(blssTrainFrames_);
    ImGui::SetNextItemWidth(scaled(160));
    ImGui::DragInt("Epochs", &blssTrainEpochs_, 1.0f, 20, 4000);
    prefHelp(
        "Adam steps over the whole sample set. 400 is the shipped default and\n"
        "the one every number in docs/neural-upscaler.md was measured at.\n"
        "The fit is sequential, so this is the one setting on this tab whose\n"
        "cost no core count reduces - it is about a third of a training run.");
    ImGui::Checkbox("Fit every shot (--all-shots)", &blssTrainAllShots_);
    prefHelp(
        "ON, and on a project corpus it should stay on. The console runs the\n"
        "frames the net was fitted to, so fitting all of them is the point -\n"
        "fitting the project reaches +0.41 dB in distribution, the highest\n"
        "number this feature has, while a net fitted to the bestiary alone is\n"
        "-0.34 dB on average over seven real projects.\n"
        "\n"
        "Off keeps a strided third of the shots out, so a later Evaluate on the\n"
        "same corpus measures content this net has not seen. That is a\n"
        "measurement, not a shipping configuration, and it costs the net real\n"
        "quality: on the bestiary, leave-one-out scored +0.31 dB where\n"
        "leave-two-out scored +0.10 on identical content.\n"
        "\n"
        "With it on the Evaluate tab's held-out columns mean nothing - every\n"
        "row is in-distribution.");
    if (!blssTrainAllShots_)
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.35f, 1.0f),
                           "    A third of the corpus is withheld - this is a measurement "
                           "net, not the one to ship.");

    ImGui::Text("Sharpen k: %.2f, render scale %s (both from Project settings)",
                project_.settings.blssSharpen,
                project_.settings.blssScale == 1 ? "1x2" : "2x2");
    prefHelp(
        "The oracle charges for the two sharpen passes at the project's own\n"
        "k, so the labels depend on it - and the corpus renders at the\n"
        "project's own scale. Change either on the Project settings tab;\n"
        "training follows them rather than offering a second value that could\n"
        "disagree with what the game ships.");

    // EVERYTHING BELOW IS RESEARCH, and it was the default view. Ten controls
    // for a decision that needs two: these six are all measured, all set where
    // the measurement said, and two of them have tooltips that say in so many
    // words that turning them up makes the feature WORSE. That is an argument
    // for folding them away, not for deleting them - a knob measured and set
    // off is not the same as a knob that was never there, which is why every
    // tooltip below is preserved verbatim.
    ImGui::Spacing();
    if (!ImGui::CollapsingHeader("Advanced - measured, and set where the measurement said")) {
        ImGui::EndDisabled();
        return;
    }
    ImGui::Indent(scaled(12.0f));

    ImGui::SetNextItemWidth(scaled(160));
    {
        int seed = (int)blssTrainSeed_;
        if (ImGui::InputInt("Corpus / init seed", &seed, 0, 0,
                            ImGuiInputTextFlags_CharsHexadecimal))
            blssTrainSeed_ = (unsigned)(seed < 0 ? 0 : seed);
    }
    prefHelp(
        "Hexadecimal. Seeds both the corpus and the weight initialisation.\n"
        "It matters far less than it looks: under cross-validation the sd of\n"
        "the per-seed fold MEAN is 0.01 dB, against 0.34 dB from fold to fold.\n"
        "The shipped default is B1557.");

    ImGui::SetNextItemWidth(scaled(160));
    ImGui::InputText("Materials from", blssAssets_, sizeof(blssAssets_));
    prefHelp(
        "An examples/ tree whose res/materials and res/models PNGs the corpus\n"
        "textures its shots with. Missing is not an error - the corpus falls\n"
        "back to procedural checkers, noise and foliage, which is what makes\n"
        "training work in a clean checkout - but every number in\n"
        "docs/neural-upscaler.md was measured with the real textures.");
    {
        std::error_code ec;
        if (!fs::is_directory(blssAssets_, ec))
            ImGui::TextDisabled("    not a directory - the corpus will use procedural "
                                "materials only");
    }

    ImGui::SeparatorText("Objective");
    ImGui::TextDisabled(
        "What the ORACLE is asked to minimise. A term that is not in here is a term the\n"
        "network is structurally unable to learn - this feature shipped that mistake four\n"
        "times. Changing either weight changes the LABELS, so it is only meaningful after\n"
        "a re-train.");
    ImGui::SetNextItemWidth(scaled(160));
    ImGui::DragFloat("Fill weight", &blssTrainFill_, 0.5f, 0.0f, 64.0f, "%.1f");
    prefHelp(
        "What one full-screen composite pass costs the oracle, in the same\n"
        "units as the error it trades against. 0 reproduces the old\n"
        "fill-blind objective, which asked for every kernel everywhere.\n"
        "16 is the middle of a measured PLATEAU (12..24), not a knee - the\n"
        "sharp knee at 6 the docs used to print was an artefact of a single\n"
        "held-out split. Sweep it WITH the weight decay; a sweep of one at\n"
        "the wrong value of the other measures neither.");
    ImGui::SetNextItemWidth(scaled(160));
    ImGui::DragFloat("Flicker weight", &blssTrainFlicker_, 0.005f, 0.0f, 0.5f, "%.3f");
    prefHelp(
        "Penalty on differing from the reprojected history - temporal\n"
        "stability. It ships at 0 AFTER being measured twice, which is not\n"
        "the same as being deleted: cross-validated it costs 0.02 dB and\n"
        "moves the flicker column not at all. The reason is the form, not the\n"
        "weight - MSE against the history is minimised by the picture\n"
        "FREEZING, which is free on a static shot and ghosting on a dolly.");

    ImGui::SeparatorText("Fit");
    ImGui::SetNextItemWidth(scaled(160));
    ImGui::DragFloat("Weight decay", &blssTrainDecay_, 1e-5f, 0.0f, 1e-2f, "%.5f");
    prefHelp(
        "1e-4, measured. This network's failure mode is variance, not\n"
        "capacity: 1e-5 is barely regularisation (+0.36 dB, 3.92 passes),\n"
        "1e-4 is +0.55 at 3.00, and 1e-3 over-smooths the weight field into\n"
        "asking for every kernel again (+0.40 at 4.34). Set it WITH the fill\n"
        "weight - decay pulls the outputs toward the mean oracle answer,\n"
        "which is nonzero, so more decay means more fill unless the\n"
        "objective charges for it.");
    ImGui::Checkbox("Standardise the inputs (--standardise)", &blssTrainStandardise_);
    prefHelp(
        "Fixes a real defect - the eight channels have wildly different\n"
        "scales - and makes the feature WORSE, which is the whole diagnosis\n"
        "of this network in one switch. It fits the training shots better and\n"
        "cross-validates at +0.24 dB against +0.40, with 9 folds below\n"
        "bilinear instead of 5. Kept because a knob measured and set off is\n"
        "not the same as a knob deleted.");

    ImGui::Unindent(scaled(12.0f));
    ImGui::EndDisabled();
}

// --- the shared training action, and the shared defaults ---------------------

// Pressed from TWO places now - the Train tab and the verdict's "Train the
// network" - so the argument list is built once. Two copies is how one of them
// silently stops passing --all-shots.
void App::blssStartTraining() {
    std::vector<std::string> a{"--blss-train"};
    for (const std::string& c : blssCommonArgs()) a.push_back(c);
    a.push_back("-o");
    a.push_back(blssNetName_);
    a.push_back("--frames");
    a.push_back(std::to_string(blssTrainFrames_));
    a.push_back("--epochs");
    a.push_back(std::to_string(blssTrainEpochs_));
    a.push_back("--seed");
    char seedbuf[32];
    std::snprintf(seedbuf, sizeof(seedbuf), "0x%X", blssTrainSeed_);
    a.push_back(seedbuf);
    a.push_back("--weight-decay");
    a.push_back(numArg(blssTrainDecay_));
    a.push_back("--fill-weight");
    a.push_back(numArg(blssTrainFill_));
    a.push_back("--flicker-weight");
    a.push_back(numArg(blssTrainFlicker_));
    if (blssTrainAllShots_) a.push_back("--all-shots");
    if (blssTrainStandardise_) a.push_back("--standardise");
    blssStart(blssui::Kind::Train, a, blssTrainEpochs_);
}

// EVERY field, not the eight it used to cover. Restore defaults left the four
// frame counts, the deadzone, the cross-validation settings and the corpus
// switch wherever they had been dragged - so "restore defaults" produced a
// configuration that was not the documented one, silently, which is the worst
// thing a button with that name can do.
void App::blssRestoreTrainDefaults() {
    blssTrainFrames_ = 156;
    blssTrainEpochs_ = 400;
    blssTrainSeed_ = 0xB1557u;
    blssTrainDecay_ = 1e-4f;
    blssTrainFill_ = blss::kFillWeight;
    blssTrainFlicker_ = blss::kFlickerWeight;
    blssTrainAllShots_ = true;
    blssTrainStandardise_ = false;
    blssEvalFrames_ = 156;
    blssEvalDeadzone_ = 8.0f;
    blssEvalDump_ = true;
    blssCvFrames_ = 156;
    blssCvEpochs_ = 400;
    blssCvSeeds_ = 1;
    blssCvFolds_ = 0;
    blssCvSweep_ = false;
    std::snprintf(blssCvSweepList_, sizeof(blssCvSweepList_), "0,1,2,3,4,6,8,12,16,24");
    blssFeatFrames_ = 156;
    std::snprintf(blssNetName_, sizeof(blssNetName_), "blss.net");
    const std::string a = discoverAssets();
    std::snprintf(blssAssets_, sizeof(blssAssets_), "%s", a.c_str());
}

// FOUR INDEPENDENT FRAME FIELDS, one per verb, and they are only comparable
// when they agree. A net fitted to 156 frames and evaluated against 36 is not a
// wrong answer, it is an answer to a different question - and nothing said so.
void App::drawBlssFrameDrift(int mine) {
    const int others[] = {blssTrainFrames_, blssEvalFrames_, blssCvFrames_, blssFeatFrames_};
    int lo = others[0], hi = others[0];
    for (int v : others) {
        lo = std::min(lo, v);
        hi = std::max(hi, v);
    }
    if (lo == hi) return;
    ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.35f, 1.0f),
                       "    The four tabs' frame counts have drifted apart (%d..%d; this one is "
                       "%d).",
                       lo, hi, mine);
    ImGui::TextDisabled(
        "    A table measured over a different number of frames than the net was fitted to is "
        "not\n    a comparison. 'Restore defaults' on the Train tab puts all four back to 156.");
}

// ---------------------------------------------------------------- evaluate ---

namespace {
// Two-decimal dB with a sign, and green/red by which side of zero it is on.
void marginText(double v) {
    const ImVec4 good(0.45f, 0.85f, 0.45f, 1.0f), bad(1.0f, 0.45f, 0.35f, 1.0f);
    ImGui::TextColored(v >= 0.0 ? good : bad, "%+.2f", v);
}

// The no-headroom threshold now lives in blss_ui.hpp next to the arithmetic
// that reads it (blssui::recommend), because the combined verdict has to apply
// exactly the same test the picture-only one does.
using blssui::kNoHeadroomDb;
}  // namespace

// WILL THIS SCENE BENEFIT AT ALL - the answer --blss-eval has always contained
// and has always buried in the sixth row of a table. The oracle row IS the
// scene's ceiling, so it answers the question the PSNR columns only imply.
//
// `sum` may come from a run that had NO network (the header's "Will this scene
// benefit?" button), in which case only the ceiling half of the verdict exists
// and the block ends with the button that produces the other half. `compact`
// drops the explanatory paragraphs for the header, where the same verdict sits
// above six tabs and cannot have five lines of prose.
void App::drawBlssVerdict(const blssui::EvalSummary& sum, bool compact) {
    // The arithmetic is blssui::summarise() / blssui::parseVerdictLine() - pure
    // functions of the tool's output, so they are checkable from the host-only
    // harness. This function only chooses words and colours.
    if (!sum.ok) return;
    const double ceiling = sum.oracleMargin, margin = sum.netMargin;
    const double netP = sum.netPasses, orcP = sum.oraclePasses;

    const ImVec4 bad(1.0f, 0.45f, 0.35f, 1.0f), warn(1.0f, 0.75f, 0.35f, 1.0f),
        good(0.45f, 0.85f, 0.45f, 1.0f);
    ImGui::SeparatorText("The answer");
    if (ceiling < kNoHeadroomDb) {
        ImGui::TextColored(bad, "THIS SCENE WILL NOT BENEFIT. Leave the upscaler off.");
        ImGui::TextWrapped(
            "The ORACLE - the best any per-tile weighting can do under the exact GS "
            "composite, which no network can beat - scores %+.2f dB over plain bilinear "
            "here, at %.2f passes.%s",
            ceiling, orcP,
            compact ? "" :
                    " There is nothing to reconstruct: a half-resolution "
                    "render of this content, blown up bilinearly, is already as close to the "
                    "supersampled truth as the composite can get. That is a fact about the "
                    "scene - soft textures, low-poly silhouettes, nothing that aliases - and "
                    "no amount of training moves it.");
        if (!compact)
            ImGui::TextDisabled(
                "    What it costs to turn on anyway: the reduced render gives VRAM back, and "
                "the composite\n    takes it away in fill. Above 1.00 passes you are paying "
                "for nothing.");
    } else if (!sum.haveNet) {
        // THE HEADROOM BRANCH: a net-free run knows the ceiling and nothing
        // about any network, so it says the ceiling and offers the next step
        // instead of inventing a margin it did not measure.
        ImGui::TextColored(good, "Headroom: %+.2f dB available at %.2f passes.", ceiling, orcP);
        ImGui::TextWrapped(
            "That is the ORACLE - the best any per-tile weighting can do under the exact GS "
            "composite. A trained network reaches some fraction of it, never more.%s",
            compact ? "" :
                    " Train one on this corpus and evaluate it to find out how much of that "
                    "fraction you actually get.");
        // THE BUTTON THAT USED TO BE HERE IS IN THE HEADER'S ANSWER BLOCK, and
        // it is not here as well on purpose: an ImGui label IS its id, this tab
        // and that block are submitted in the same frame, and two "Train a
        // network for this scene" buttons in one window collide silently -
        // neither a person tabbing through nor a --ui-script run can tell them
        // apart. One verb, one place.
        ImGui::TextDisabled(
            "    The button that starts it is with the combined answer at the top of this "
            "window.");
    } else if (margin < 0.0) {
        ImGui::TextColored(bad, "THE NETWORK YOU HAVE IS WORSE THAN NOT USING IT: %+.2f dB.",
                           margin);
        ImGui::TextWrapped(
            "It costs %.2f passes to score below the one pass plain bilinear costs. The "
            "scene itself has room - the oracle reaches %+.2f dB - so this is the NET, not "
            "the content.",
            netP, ceiling);
        if (!blssCorpusProject_)
            ImGui::TextColored(warn,
                               "    That is exactly what a bestiary-only net does on a real "
                               "project (-0.34 dB mean,\n    -1.09 worst, over seven of them). "
                               "Switch the corpus above to this project and train again.");
        else
            ImGui::TextColored(warn,
                               "    Check that the net was trained on THIS corpus, at this "
                               "render scale and this\n    sharpen strength - a blss.net records "
                               "none of that, so a mismatch is silent.");
    } else {
        ImGui::TextColored(good, "%+.2f dB over plain bilinear, at %.2f passes.", margin, netP);
        if (compact)
            ImGui::Text("The scene's ceiling is %+.2f dB at %.2f passes, so the network has "
                        "captured %.0f%% of it.",
                        ceiling, orcP, ceiling > 0.0 ? 100.0 * margin / ceiling : 0.0);
        else
            ImGui::TextWrapped(
                "The scene's own ceiling is %+.2f dB at %.2f passes (the oracle), so the "
                "network has captured %.0f%% of what is there to capture. 1.00 passes IS "
                "plain bilinear and 5.00 is every kernel everywhere, so read the two "
                "together.",
                ceiling, orcP, ceiling > 0.0 ? 100.0 * margin / ceiling : 0.0);
    }
    if (!blssCorpusProject_)
        ImGui::TextColored(warn,
                           "    Measured on the BESTIARY, not on this project. It is not an "
                           "answer about your game.");
}

void App::drawBlssEvalTab() {
    ImGui::Spacing();
    ImGui::TextWrapped(
        "Runs the trained net and every fixed kernel over the corpus and prints PSNR, "
        "flicker and occupancy for each. It closes the temporal loop - every frame's "
        "history is the previous frame's real composite, which is what the console has.");
    ImGui::Spacing();
    drawBlssCorpusReminder();
    if (blssCorpusProject_)
        ImGui::TextWrapped(
            "ON A PROJECT CORPUS THIS IS THE MEASUREMENT THAT DECIDES. It answers 'will this "
            "scene benefit at all' - the ORACLE row is the scene's own ceiling, and some "
            "scenes have none: on examples/showcase the oracle itself scores +0.02 dB at "
            "1.00 passes, so no network can win there. Run this before you enable the "
            "upscaler on a game. The verdict is under the table.");
    else
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.35f, 1.0f),
                           "Its held-out columns are ONE draw of the bestiary's split. Use the "
                           "Cross-validate tab for\nanything you intend to act on there.");
    ImGui::Spacing();

    ImGui::BeginDisabled(blssJob_.running());
    ImGui::SetNextItemWidth(scaled(120));
    ImGui::InputText("Network", blssNetName_, sizeof(blssNetName_));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(scaled(90));
    ImGui::DragInt("Frames", &blssEvalFrames_, 1.0f, 13, 520);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(scaled(110));
    ImGui::DragFloat("Inference deadzone", &blssEvalDeadzone_, 0.5f, 0.0f, 32.0f, "%.1f alpha");
    prefHelp(
        "An output whose GS alpha byte would be at most this is snapped to\n"
        "zero before it becomes vertex alpha - because a logistic cannot emit\n"
        "zero, and a weight worth alpha 2 is invisible in the picture and a\n"
        "WHOLE full-screen pass on the console. 8 is the shipped value and\n"
        "buys a whole pass (2.85 -> 1.85) for 0.02 dB. It never touches the\n"
        "labels, so changing it needs no re-train.");
    drawBlssFrameDrift(blssEvalFrames_);
    ImGui::Checkbox("Write the comparison images", &blssEvalDump_);
    prefHelp("Ground truth / native / bilinear / point / temporal / sharpen / BLSS /\n"
             "oracle PNGs of the first held-out frame, plus the weight field. The\n"
             "Compare tab shows them, and the strip under the verdict is the three\n"
             "that answer the question.");
    if (blssEvalDump_) ImGui::TextDisabled("    %s", blssDumpDir_.c_str());

    ImGui::Spacing();
    if (ImGui::Button("Run the evaluation", ImVec2(scaled(190.0f), 0))) {
        std::vector<std::string> a{"--blss-eval"};
        for (const std::string& c : blssCommonArgs()) a.push_back(c);
        // Only ask for a net that is actually there. --blss-eval's net-free mode
        // is gated on -i being GIVEN, not on the file resolving, so passing it
        // unconditionally turns "this project has never been trained" into exit 1
        // instead of the headroom answer - which is the one answer that project
        // most needs. Stat it here rather than trusting blssNetPresent_: that is
        // refreshed once a second, and the Network field above can be retyped
        // inside that second.
        {
            std::error_code ec;
            const std::string net = blssNetPath();
            if (!net.empty() && fs::is_regular_file(net, ec)) {
                a.push_back("-i");
                a.push_back(blssNetName_);
            }
        }
        a.push_back("--frames");
        a.push_back(std::to_string(blssEvalFrames_));
        a.push_back("--deadzone");
        a.push_back(numArg(blssEvalDeadzone_));
        if (blssEvalDump_) {
            a.push_back("--dump");
            a.push_back(blssDumpDir_);
        }
        blssStart(blssui::Kind::Eval, a, 0);
    }
    ImGui::SameLine();
    {
        const int cores = std::max(1, (int)std::thread::hardware_concurrency());
        const blssui::Cost cost = blssui::estimate(blssui::Kind::Eval, blssEvalFrames_, 0, cores,
                                                   1, 0, blssExpectedShots());
        ImGui::TextDisabled("%s on this machine (%d cores).",
                            blssui::humanDuration(cost.total).c_str(), cores);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Most of it is not the corpus. Evaluation CLOSES THE TEMPORAL LOOP -\n"
                "every frame's history is the previous frame's real composite, which is\n"
                "what the console has - so its unit of parallelism is a SHOT RUN and not\n"
                "a frame: six camera moves are about six chains however many cores are\n"
                "watching. Measured, 156 frames: 138 s at one thread, 46.6 at six, 39.1\n"
                "at twenty-four, where the corpus renders in 3.3. An estimate; treat a\n"
                "factor of two as normal.");
    }
    ImGui::EndDisabled();

    if (!blssEval_.ok()) {
        ImGui::Spacing();
        ImGui::TextDisabled("No table yet. Run it, or look at the output pane below.");
        return;
    }
    // The verdict FIRST, because it is the question that was asked; the table
    // below is where it comes from, and the pictures under it are what the
    // decibels are actually about.
    drawBlssVerdict(blssSummary_, /*compact=*/false);
    drawBlssVerdictThumbs();
    // WHICH NET THE ROWS BELOW CAME FROM, in the run's own words. Above the
    // table and not under it: a reader who has to scroll past the numbers to
    // learn they are about a different network has already quoted them.
    if (blssNetSource_.ok) {
        const bool own = blssNetSource_.kind == "project";
        ImGui::TextColored(own ? ImVec4(0.45f, 0.85f, 0.45f, 1.0f)
                               : ImVec4(1.0f, 0.75f, 0.35f, 1.0f),
                           "Net: %s  -  corpus %s, %s, jitter %s", blssNetSource_.label().c_str(),
                           blssNetSource_.corpus.c_str(), blssNetSource_.scale.c_str(),
                           blssNetSource_.jitter.c_str());
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "The `[blss] net source=` line this run printed before it measured\n"
                "anything. A net that LOADS is not the same thing as the net you meant\n"
                "to measure: with a default shipping, an empty project directory and a\n"
                "trained one produce the same table shape and different networks.");
        for (const std::string& f : blssNetSource_.refused)
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "    REFUSED: %s", f.c_str());
        for (const std::string& w : blssNetSource_.warnings)
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.35f, 1.0f), "    warning: %s", w.c_str());
    }
    for (const blssui::EvalSplit& sp : blssEval_.splits) {
        ImGui::SeparatorText(sp.heldOut ? "Held-out shots" : "Training shots");
        ImGui::TextDisabled("%s", sp.caption.c_str());
        const int cols = 7 + (int)sp.shots.size();
        if (!ImGui::BeginTable(sp.heldOut ? "blssevalheld" : "blssevaltrain", cols,
                               ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                   ImGuiTableFlags_SizingStretchProp))
            continue;
        ImGui::TableSetupColumn("method");
        ImGui::TableSetupColumn("PSNR");
        ImGui::TableSetupColumn("flicker");
        ImGui::TableSetupColumn("point");
        ImGui::TableSetupColumn("temp");
        ImGui::TableSetupColumn("sharp");
        ImGui::TableSetupColumn("passes");
        for (int s : sp.shots) {
            char h[16];
            std::snprintf(h, sizeof(h), "shot%d", s);
            ImGui::TableSetupColumn(h);
        }
        ImGui::TableHeadersRow();
        for (const blssui::EvalRow& r : sp.rows) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (r.network)
                ImGui::TextColored(ImVec4(0.55f, 0.80f, 1.0f, 1.0f), "%s", r.name.c_str());
            else
                ImGui::TextUnformatted(r.name.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%.3f", r.psnr);
            ImGui::TableNextColumn();
            ImGui::Text("%.2f", r.flicker);
            if (r.occ) {
                ImGui::TableNextColumn();
                ImGui::Text("%.1f%%", r.point);
                ImGui::TableNextColumn();
                ImGui::Text("%.1f%%", r.temporal);
                ImGui::TableNextColumn();
                ImGui::Text("%.1f%%", r.sharpen);
                ImGui::TableNextColumn();
                ImGui::Text("%.2f", r.passes);
            } else {
                for (int c = 0; c < 4; ++c) {
                    ImGui::TableNextColumn();
                    ImGui::TextDisabled("-");
                }
            }
            for (size_t j = 0; j < sp.shots.size(); ++j) {
                ImGui::TableNextColumn();
                if (j < r.perShot.size())
                    ImGui::Text("%.2f", r.perShot[j]);
                else
                    ImGui::TextDisabled("-");
            }
        }
        ImGui::EndTable();
    }
    ImGui::Spacing();
    ImGui::TextDisabled(
        "1.00 passes is plain bilinear and 5.00 is every kernel everywhere, so a decibel\n"
        "bought at 5 passes is not a win. Compare flicker against the native row, which is\n"
        "the honest floor for a given camera move, not against zero. The oracle row is the\n"
        "regression test: BLSS falling well below it is a host/engine parity break.");
}

// THE PICTURE, UNDER THE VERDICT. "Is it actually better" is a question about
// pixels and the Compare tab says so in its own first line - and then the
// answer lived on the fourth tab behind two tables. Three thumbnails is enough
// to see whether the difference is anywhere at all; clicking one opens the
// Compare tab on that view, at size.
void App::drawBlssVerdictThumbs() {
    if (blssImagesDirty_) blssReloadImages();
    const auto find = [&](const char* label) -> int {
        for (int i = 0; i < (int)blssImages_.size(); ++i)
            if (blssImages_[i].label == label) return i;
        return -1;
    };
    const int bil = find("bilinear (the baseline)");
    const int net = find("BLSS (the trained net)");
    if (bil < 0 || net < 0) return;

    ImGui::Spacing();
    const float w = scaled(190.0f);
    const BlssImage& B = blssImages_[(size_t)bil];
    const float h = B.w > 0 ? w * (float)B.h / (float)B.w : w * 0.875f;
    // An ImageButton does not DRAW its label, so the label is pure id - which
    // makes it the one chance these have to be nameable at all. `##thumb` and a
    // PushID would have left three identical unnamed rects that neither a
    // scripted run nor a person tabbing through could tell apart.
    const auto thumb = [&](int idx, const char* id, const char* caption, const char* tip) {
        ImGui::BeginGroup();
        if (ImGui::ImageButton(id, (ImTextureID)(intptr_t)blssImages_[(size_t)idx].tex,
                               ImVec2(w, h))) {
            blssImgA_ = bil;
            blssImgB_ = net;
            blssImgMode_ = 0;
            blssTabSelect_ = kTabCompare;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\n\nClick to open the Compare tab.", tip);
        ImGui::TextDisabled("%s", caption);
        ImGui::EndGroup();
    };
    thumb(bil, "Compare the bilinear baseline", "bilinear (the baseline)",
          "One full-screen pass of the half-res render, blown up bilinearly.\n"
          "Every margin in this window is measured against this.");
    ImGui::SameLine();
    thumb(net, "Compare the BLSS composite", "BLSS (the trained net)",
          "The composite the network asked for, through the same 8-bit GS\n"
          "arithmetic the console executes.");
    ImGui::SameLine();
    // The difference gets the same treatment as the two frames, because it is
    // the only one of the three in which a fraction of a decibel is visible.
    {
        const int wasA = blssImgA_, wasB = blssImgB_;
        blssImgA_ = bil;
        blssImgB_ = net;
        blssRebuildDiff();
        blssImgA_ = wasA;
        blssImgB_ = wasB;
        ImGui::BeginGroup();
        if (blssDiffTex_) {
            if (ImGui::ImageButton("Compare the difference", (ImTextureID)(intptr_t)blssDiffTex_,
                                   ImVec2(w, h))) {
                blssImgA_ = bil;
                blssImgB_ = net;
                blssImgMode_ = 4;  // Difference
                blssTabSelect_ = kTabCompare;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "|bilinear - BLSS|, amplified %.0fx. Raw difference: mean %.2f,\n"
                    "peak %.0f of 255 per channel. A black frame here means the network\n"
                    "changed nothing, whatever the decibel column says.\n\n"
                    "Click to open the Compare tab on this view.",
                    blssDiffAmp_, blssDiffMean_, blssDiffPeak_);
            ImGui::TextDisabled("difference x%.0f", blssDiffAmp_);
        } else {
            ImGui::Dummy(ImVec2(w, h));
            ImGui::TextDisabled("difference: n/a");
        }
        ImGui::EndGroup();
    }
    // The images above are click-through, but an ImageButton has no label for
    // ImGui to report - so it can be reached by a cursor and by nothing else.
    // These two carry the same two destinations with names on them, which is
    // what makes the strip discoverable AND drivable.
    if (ImGui::SmallButton("Open the Compare tab")) {
        blssImgA_ = bil;
        blssImgB_ = net;
        blssImgMode_ = 0;
        blssTabSelect_ = kTabCompare;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Open the difference view")) {
        blssImgA_ = bil;
        blssImgB_ = net;
        blssImgMode_ = 4;
        blssTabSelect_ = kTabCompare;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("- at size, with a wipe and a zoom.");
}

// ----------------------------------------------------------- cross-validate ---

void App::drawBlssCvTab() {
    ImGui::Spacing();
    ImGui::TextWrapped(
        "Leave-one-shot-out: every shot is held out in turn and its own net is trained on "
        "the rest. It ignores the Network field - it trains what it measures.");
    ImGui::Spacing();
    drawBlssCorpusReminder();
    if (blssCorpusProject_) {
        // WHAT A HELD-OUT DECIBEL MEANS DEPENDS ON WHAT THE SHOTS ARE, and on a
        // project corpus it is not what it is on the bestiary. Saying so here is
        // the whole point: the tab would otherwise hand back a confident number
        // that answers a question nobody asked.
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.35f, 1.0f),
                           "ON A PROJECT CORPUS THIS DOES NOT MEASURE WHAT YOU WILL SHIP.");
        ImGui::TextWrapped(
            "The bestiary's 13 shots are 13 KINDS OF CONTENT, so holding one out asks 'does "
            "this generalise to content it has not seen'. A project's shots are one scene "
            "seen from six CAMERA MOVES, so holding one out asks whether a corpus of walks, "
            "pans and orbits predicts a strafe - and the measured answer on "
            "examples/procedural is NO: -0.17 dB, 9 of 18 fold-runs below plain bilinear, "
            "1.22 passes. That is the same wall the bestiary hit at five shots (+0.10 dB) "
            "before it grew to thirteen.");
        ImGui::TextWrapped(
            "It does not matter for the net you ship, because the console runs the frames "
            "the net was fitted on: train with 'Fit every shot' and read the Evaluate tab. "
            "Do not quote a project corpus' held-out decibel. What this tab IS good for here "
            "is finding the move the net falls apart on - the per-fold rows, not the mean.");
    } else {
        ImGui::TextWrapped(
            "On the bestiary each shot is a different KIND of content, so this is the number "
            "to act on: a single held-out split is a sample of size ONE, and quoting one is "
            "the mistake this feature made five times.");
    }
    ImGui::Spacing();

    ImGui::BeginDisabled(blssJob_.running());
    ImGui::SetNextItemWidth(scaled(90));
    ImGui::DragInt("Frames", &blssCvFrames_, 1.0f, 13, 520);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(scaled(90));
    ImGui::DragInt("Epochs", &blssCvEpochs_, 1.0f, 20, 4000);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(scaled(70));
    ImGui::DragInt("Corpora", &blssCvSeeds_, 0.1f, 1, 8);
    prefHelp(
        "--cv-seeds: repeats the whole fold table on N independently generated\n"
        "corpora, so it carries a SPREAD and not just a mean. A mean without a\n"
        "spread is how this feature published noise twice. Each one costs\n"
        "another full corpus render plus 13 trainings.");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(scaled(70));
    ImGui::DragInt("Folds", &blssCvFolds_, 0.1f, 0, 48);
    prefHelp(
        "--cv-folds: holds out only the first N shots in turn (0 = every shot).\n"
        "That is how a before/after survives the corpus growing under it: same\n"
        "held-out content, more training content, is the only comparison that\n"
        "means anything.\n"
        "The bestiary has 13 shots; a project has six camera moves per scene,\n"
        "so a two-scene project has twelve.");
    ImGui::SetNextItemWidth(scaled(110));
    ImGui::DragFloat("Weight decay", &blssTrainDecay_, 1e-5f, 0.0f, 1e-2f, "%.5f");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(scaled(80));
    ImGui::DragFloat("Fill weight", &blssTrainFill_, 0.5f, 0.0f, 64.0f, "%.1f");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(scaled(110));
    ImGui::DragFloat("Inference deadzone", &blssEvalDeadzone_, 0.5f, 0.0f, 32.0f, "%.1f alpha");
    ImGui::Checkbox("Sweep the deadzone in the same run", &blssCvSweep_);
    prefHelp(
        "The deadzone is an INFERENCE knob - it never reaches the labels - so\n"
        "N values over the same folds cost N evaluations of the same trained\n"
        "nets rather than N trainings. That is the difference between\n"
        "sweeping it and guessing.");
    if (blssCvSweep_) {
        ImGui::SetNextItemWidth(scaled(280));
        ImGui::InputText("Alpha values", blssCvSweepList_, sizeof(blssCvSweepList_));
    }
    drawBlssFrameDrift(blssCvFrames_);

    ImGui::Spacing();
    {
        // A COUNT IS NOT A COST, and this tab used to print only the count -
        // for the button in this window that takes tens of minutes. The shot
        // count is only known once the corpus has been built (13 for the
        // bestiary, six per scene for a project), so with Folds at 0 the
        // estimate says which assumption it made.
        const int seeds = std::max(1, blssCvSeeds_);
        const int cores = std::max(1, (int)std::thread::hardware_concurrency());
        const int shots = blssExpectedShots();
        const blssui::Cost cost = blssui::estimate(blssui::Kind::Cv, blssCvFrames_, blssCvEpochs_,
                                                   cores, seeds, blssCvFolds_, shots);
        if (blssCvFolds_ > 0)
            ImGui::TextDisabled("%d fold-run(s): %d corpus render(s) plus %d training(s).",
                                seeds * blssCvFolds_, seeds, cost.trainings);
        else
            ImGui::TextDisabled(
                "%d corpus render(s), then one training per shot per corpus - %d expected "
                "here\n(13 shots on the bestiary, six camera moves per scene on a project), so "
                "%d training(s).",
                seeds, shots, cost.trainings);
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.35f, 1.0f), "%s on this machine (%d cores).",
                           blssui::humanDuration(cost.total).c_str(), cores);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "About %s of corpus render, %s of oracle labelling and %s of fitting\n"
                "and per-fold evaluation. The fit dominates, it is sequential SGD, and\n"
                "it is paid once PER FOLD - which is why this is the expensive button.\n"
                "An estimate; treat a factor of two as normal, and note the shot count\n"
                "is a guess until the corpus has loaded.",
                blssui::humanDuration(cost.corpus).c_str(),
                blssui::humanDuration(cost.oracle).c_str(),
                blssui::humanDuration(cost.fit).c_str());
    }
    if (ImGui::Button("Run cross-validation", ImVec2(scaled(190.0f), 0))) {
        std::vector<std::string> a{"--blss-eval", "--cv"};
        for (const std::string& c : blssCommonArgs()) a.push_back(c);
        a.push_back("--frames");
        a.push_back(std::to_string(blssCvFrames_));
        a.push_back("--epochs");
        a.push_back(std::to_string(blssCvEpochs_));
        a.push_back("--cv-seeds");
        a.push_back(std::to_string(blssCvSeeds_));
        if (blssCvFolds_ > 0) {
            a.push_back("--cv-folds");
            a.push_back(std::to_string(blssCvFolds_));
        }
        a.push_back("--weight-decay");
        a.push_back(numArg(blssTrainDecay_));
        a.push_back("--fill-weight");
        a.push_back(numArg(blssTrainFill_));
        a.push_back("--deadzone");
        a.push_back(numArg(blssEvalDeadzone_));
        if (blssCvSweep_ && blssCvSweepList_[0]) {
            a.push_back("--deadzone-sweep");
            a.push_back(blssCvSweepList_);
        }
        blssStart(blssui::Kind::Cv, a, blssCvEpochs_);
    }
    ImGui::EndDisabled();

    if (!blssCv_.ok()) {
        ImGui::Spacing();
        ImGui::TextDisabled("No fold table yet. Run it, or look at the output pane below.");
        return;
    }

    ImGui::SeparatorText("Margin over plain bilinear on the held-out shot, dB");
    if (!blssCv_.caption.empty()) ImGui::TextDisabled("%s", blssCv_.caption.c_str());
    const int seedCols = (int)blssCv_.seeds.size();
    // #, held-out shot, one per seed, mean, sd - FOUR fixed columns, not three.
    // At 3 + seedCols the last TableSetupColumn overflowed ("called too many
    // times!" on stderr) and the sd column was dropped from the table the whole
    // cross-validation tab exists to show.
    if (ImGui::BeginTable("blsscv", 4 + seedCols,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("#");
        ImGui::TableSetupColumn("held-out shot");
        for (const std::string& s : blssCv_.seeds)
            ImGui::TableSetupColumn(("seed " + s).c_str());
        ImGui::TableSetupColumn("mean");
        ImGui::TableSetupColumn("sd");
        ImGui::TableHeadersRow();
        for (const blssui::CvFold& f : blssCv_.folds) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%d", f.index);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(f.shot.c_str());
            for (int i = 0; i < seedCols; ++i) {
                ImGui::TableNextColumn();
                if (i < (int)f.perSeed.size())
                    marginText(f.perSeed[(size_t)i]);
                else
                    ImGui::TextDisabled("-");
            }
            ImGui::TableNextColumn();
            marginText(f.mean);
            ImGui::TableNextColumn();
            ImGui::Text("%.2f", f.sd);
        }
        if (!blssCv_.seedMeans.empty() || blssCv_.runs > 0) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted("");
            ImGui::TableNextColumn();
            ImGui::TextUnformatted("mean over folds");
            for (int i = 0; i < seedCols; ++i) {
                ImGui::TableNextColumn();
                if (i < (int)blssCv_.seedMeans.size())
                    marginText(blssCv_.seedMeans[(size_t)i]);
                else
                    ImGui::TextDisabled("-");
            }
            ImGui::TableNextColumn();
            marginText(blssCv_.mean);
            ImGui::TableNextColumn();
            ImGui::Text("%.2f", blssCv_.seedSd);
        }
        ImGui::EndTable();
    }
    if (blssCv_.runs > 0) {
        ImGui::Spacing();
        ImGui::Text("Overall");
        ImGui::SameLine();
        marginText(blssCv_.mean);
        ImGui::SameLine();
        ImGui::Text("dB, sd %.2f over %d fold-run(s); %d of %d BELOW bilinear; %.2f passes "
                    "(sd %.2f).",
                    blssCv_.sd, blssCv_.runs, blssCv_.below, blssCv_.runs, blssCv_.passes,
                    blssCv_.passesSd);
        ImGui::TextDisabled(
            "The sd of the per-seed fold-mean is %.2f - THAT is what one plain Evaluate run "
            "is estimating,\nand the %.2f above is how much the answer moves with which "
            "shot you hold out. They are\nnot the same spread, and confusing them is how "
            "this feature blamed the training seed for\nfour commits.",
            blssCv_.seedSd, blssCv_.sd);
    }

    bool haveAbs = false;
    for (const blssui::CvFold& f : blssCv_.folds) haveAbs = haveAbs || f.have;
    if (haveAbs) {
        ImGui::SeparatorText("Per fold: PSNR on the held-out shot");
        if (ImGui::BeginTable("blsscvabs", 9,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_SizingStretchProp)) {
            const char* heads[] = {"#",      "held-out shot", "native",  "bilinear", "BLSS",
                                   "oracle", "passes",        "flicker", "in-dist"};
            for (const char* h : heads) ImGui::TableSetupColumn(h);
            ImGui::TableHeadersRow();
            for (const blssui::CvFold& f : blssCv_.folds) {
                if (!f.have) continue;
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("%d", f.index);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(f.shot.c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%.2f", f.native);
                ImGui::TableNextColumn();
                ImGui::Text("%.2f", f.bilinear);
                ImGui::TableNextColumn();
                if (f.blss >= f.bilinear)
                    ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1.0f), "%.2f", f.blss);
                else
                    ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "%.2f", f.blss);
                ImGui::TableNextColumn();
                ImGui::Text("%.2f", f.oracle);
                ImGui::TableNextColumn();
                ImGui::Text("%.2f", f.passes);
                ImGui::TableNextColumn();
                ImGui::Text("%.2f", f.flicker);
                ImGui::TableNextColumn();
                marginText(f.inDist);
            }
            ImGui::EndTable();
        }
        ImGui::TextDisabled(
            "in-dist is BLSS - bilinear on that fold's TRAINING shots: the control that says "
            "the fold\ntrained at all. A held-out number under a collapsed in-dist number "
            "means nothing.");
    }

    if (!blssCv_.deadzone.empty()) {
        ImGui::SeparatorText("Inference deadzone sweep - the same nets, evaluated again");
        if (ImGui::BeginTable("blsscvdz", 7,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_SizingStretchProp)) {
            const char* heads[] = {"alpha", "margin", "passes", "point",
                                   "temp",  "sharp",  "below bilinear"};
            for (const char* h : heads) ImGui::TableSetupColumn(h);
            ImGui::TableHeadersRow();
            for (const blssui::CvDeadzone& d : blssCv_.deadzone) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                if (d.thisBuild)
                    ImGui::TextColored(ImVec4(0.55f, 0.80f, 1.0f, 1.0f), "%.1f  <-- this run",
                                       d.alpha);
                else
                    ImGui::Text("%.1f", d.alpha);
                ImGui::TableNextColumn();
                marginText(d.margin);
                ImGui::TableNextColumn();
                ImGui::Text("%.2f", d.passes);
                ImGui::TableNextColumn();
                ImGui::Text("%.1f%%", d.point);
                ImGui::TableNextColumn();
                ImGui::Text("%.1f%%", d.temporal);
                ImGui::TableNextColumn();
                ImGui::Text("%.1f%%", d.sharpen);
                ImGui::TableNextColumn();
                ImGui::Text("%d/%d", d.below, d.runs);
            }
            ImGui::EndTable();
        }
        ImGui::TextDisabled(
            "Read it as the MARGINAL PRICE OF A PASS - that is the only way it decides "
            "anything.");
    }
}

// ----------------------------------------------------------------- compare ---

void App::drawBlssImagesTab() {
    ImGui::Spacing();
    if (blssImagesDirty_) blssReloadImages();
    ImGui::TextWrapped(
        "The first held-out frame, reconstructed every way the table above measures. "
        "'Is it actually better' is a question about pixels, not decibels.");
    ImGui::SameLine();
    if (ImGui::SmallButton("Reload")) blssImagesDirty_ = true;

    if (blssImages_.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled(
            "No images in %s.\nRun Evaluate with 'Write the comparison images' ticked.",
            blssDumpDir_.c_str());
        return;
    }

    std::vector<const char*> names;
    for (const BlssImage& im : blssImages_) names.push_back(im.label.c_str());
    blssImgA_ = std::clamp(blssImgA_, 0, (int)names.size() - 1);
    blssImgB_ = std::clamp(blssImgB_, 0, (int)names.size() - 1);

    ImGui::SetNextItemWidth(scaled(240));
    ImGui::Combo("A", &blssImgA_, names.data(), (int)names.size());
    ImGui::SameLine();
    if (ImGui::Button("<-> swap")) std::swap(blssImgA_, blssImgB_);
    ImGui::SetNextItemWidth(scaled(240));
    ImGui::Combo("B", &blssImgB_, names.data(), (int)names.size());

    // Difference is the fifth entry rather than the first because the wipe is
    // the right default, but it is the one view that makes a fraction of a
    // decibel visible at all - see blssRebuildDiff.
    const char* modes[] = {"Wipe", "Side by side", "A only", "B only", "Difference |A-B|"};
    ImGui::SetNextItemWidth(scaled(180));
    ImGui::Combo("View", &blssImgMode_, modes, 5);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(scaled(160));
    ImGui::SliderFloat("Zoom", &blssZoom_, 0.5f, 4.0f, "%.1fx");
    if (blssImgMode_ == 0) {
        ImGui::SetNextItemWidth(scaled(340));
        ImGui::SliderFloat("Wipe", &blssWipe_, 0.0f, 1.0f, "%.2f");
    }
    if (blssImgMode_ == 4) {
        ImGui::SetNextItemWidth(scaled(340));
        ImGui::SliderFloat("Amplify", &blssDiffAmp_, 1.0f, 32.0f, "%.0fx");
        prefHelp(
            "|A - B| per channel, multiplied by this and clamped. Two\n"
            "reconstructions of one frame that differ by a few tenths of a\n"
            "decibel are indistinguishable side by side and obvious at 8x, and\n"
            "WHERE they differ is the useful half: it is a picture of where the\n"
            "network spent its composite passes. The caption under the image\n"
            "reports the RAW difference, not the amplified one.");
    }

    const BlssImage& A = blssImages_[(size_t)blssImgA_];
    const BlssImage& B = blssImages_[(size_t)blssImgB_];
    ImGui::TextDisabled("A: %s  (%dx%d)   B: %s  (%dx%d)", A.label.c_str(), A.w, A.h,
                        B.label.c_str(), B.w, B.h);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("A: %s\n%s\n\nB: %s\n%s", A.tip.c_str(), A.path.c_str(),
                          B.tip.c_str(), B.path.c_str());

    ImGui::BeginChild("blssimg", ImVec2(0, 0), true,
                      ImGuiWindowFlags_HorizontalScrollbar);
    const auto drawOne = [&](const BlssImage& im) {
        // A 16x14 weight field wants to be the same size on screen as the
        // 512x448 frames it describes, or it is a stamp nobody can read.
        const float base = im.w <= 64 ? 512.0f : (float)im.w;
        const float aspect = im.h > 0 ? (float)im.h / (float)im.w : 1.0f;
        const float w = scaled(base) * blssZoom_;
        return ImVec2(w, w * aspect);
    };
    if (blssImgMode_ == 1) {
        const ImVec2 sa = drawOne(A);
        ImGui::Image((ImTextureID)(intptr_t)A.tex, sa);
        ImGui::SameLine();
        ImGui::Image((ImTextureID)(intptr_t)B.tex, drawOne(B));
    } else if (blssImgMode_ == 2) {
        ImGui::Image((ImTextureID)(intptr_t)A.tex, drawOne(A));
    } else if (blssImgMode_ == 3) {
        ImGui::Image((ImTextureID)(intptr_t)B.tex, drawOne(B));
    } else if (blssImgMode_ == 4) {
        blssRebuildDiff();
        if (!blssDiffTex_) {
            ImGui::TextDisabled(
                "A and B are different sizes (%dx%d against %dx%d), so there is nothing "
                "honest to\nsubtract - the weight field is one pixel per tile.",
                A.w, A.h, B.w, B.h);
        } else {
            const float w = scaled((float)blssDiffW_) * blssZoom_;
            ImGui::Image((ImTextureID)(intptr_t)blssDiffTex_,
                         ImVec2(w, w * (float)blssDiffH_ / (float)blssDiffW_));
            ImGui::TextDisabled(
                "|%s - %s| x%.0f.  Raw difference: mean %.2f, peak %.0f of 255 per channel.",
                A.label.c_str(), B.label.c_str(), blssDiffAmp_, blssDiffMean_, blssDiffPeak_);
        }
    } else {
        // The wipe: A whole, then the right-hand slice of B on top of it, with
        // matching UVs so the two halves are the same pixels of the same frame.
        const ImVec2 size = drawOne(A);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float w = std::clamp(blssWipe_, 0.0f, 1.0f);
        dl->AddImage((ImTextureID)(intptr_t)A.tex, p, ImVec2(p.x + size.x, p.y + size.y));
        if (w < 1.0f)
            dl->AddImage((ImTextureID)(intptr_t)B.tex, ImVec2(p.x + size.x * w, p.y),
                         ImVec2(p.x + size.x, p.y + size.y), ImVec2(w, 0.0f), ImVec2(1.0f, 1.0f));
        dl->AddLine(ImVec2(p.x + size.x * w, p.y), ImVec2(p.x + size.x * w, p.y + size.y),
                    IM_COL32(255, 220, 60, 220), 1.5f);
        ImGui::Dummy(size);
        ImGui::TextDisabled("left of the line: %s     right: %s", A.label.c_str(),
                            B.label.c_str());
    }
    ImGui::EndChild();
}

// ------------------------------------------------------------------ inputs ---

void App::drawBlssFeaturesTab() {
    ImGui::Spacing();
    ImGui::TextWrapped(
        "What the %d input channels actually look like over the corpus, and how each "
        "correlates with what the oracle asked for. A channel that is constant is a "
        "channel the network's weights cannot use - a saturated feature is a feature it "
        "does not have. This is the first place to look when the answer to 'why does it "
        "not help here' is wanted, before the topology.",
        blss::kFeatures);
    ImGui::Spacing();
    drawBlssCorpusReminder();
    ImGui::TextDisabled(
        "Run it on BOTH corpora to see why a bestiary-trained net misbehaves on a project:\n"
        "texDetail is identically zero over an untextured project's tiles and it is the\n"
        "bestiary's channel most correlated with the temporal weight (r = +0.251), so that\n"
        "net's temporal gate is driven by an input that is out of range on your scene.");
    ImGui::Spacing();

    ImGui::BeginDisabled(blssJob_.running());
    ImGui::SetNextItemWidth(scaled(160));
    ImGui::DragInt("Frames", &blssFeatFrames_, 1.0f, 13, 520);
    drawBlssFrameDrift(blssFeatFrames_);
    if (ImGui::Button("Report the input channels", ImVec2(scaled(220.0f), 0))) {
        std::vector<std::string> a{"--blss-eval", "--features"};
        for (const std::string& c : blssCommonArgs()) a.push_back(c);
        a.push_back("--frames");
        a.push_back(std::to_string(blssFeatFrames_));
        a.push_back("--fill-weight");
        a.push_back(numArg(blssTrainFill_));
        blssStart(blssui::Kind::Features, a, 0);
    }
    ImGui::SameLine();
    {
        const int cores = std::max(1, (int)std::thread::hardware_concurrency());
        const blssui::Cost cost = blssui::estimate(blssui::Kind::Features, blssFeatFrames_, 0,
                                                   cores, 1, 0, blssExpectedShots());
        ImGui::TextDisabled("%s on this machine (%d cores).",
                            blssui::humanDuration(cost.total).c_str(), cores);
    }
    ImGui::EndDisabled();

    if (!blssFeat_.ok()) {
        ImGui::Spacing();
        ImGui::TextDisabled("No channel report yet. Run it, or look at the output pane below.");
        return;
    }
    // THE DIAGNOSIS BEFORE THE TABLE. Nobody reading a column of standard
    // deviations knows that "texDetail sd 0.000" means "your corpus has no
    // textured surfaces, so the network cannot learn the channel that predicts
    // texture aliasing" - and that sentence is the entire value of this tab.
    ImGui::SeparatorText("Is this corpus good enough to train on?");
    drawBlssHealth(/*compact=*/false);

    ImGui::SeparatorText("Distribution and correlation with the oracle");
    if (!blssFeat_.caption.empty()) ImGui::TextDisabled("%s", blssFeat_.caption.c_str());
    if (ImGui::BeginTable("blssfeat", 10,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_SizingStretchProp)) {
        const char* heads[] = {"channel", "mean",  "sd",     "min",    "max",
                               "% at 0",  "% at 1", "r:point", "r:temp", "r:sharp"};
        for (const char* h : heads) ImGui::TableSetupColumn(h);
        ImGui::TableHeadersRow();
        for (const blssui::FeatureRow& r : blssFeat_.rows) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(r.name.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%.3f", r.mean);
            ImGui::TableNextColumn();
            // A channel that does not move is a channel the net cannot use.
            if (r.sd < 0.02)
                ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "%.3f", r.sd);
            else
                ImGui::Text("%.3f", r.sd);
            ImGui::TableNextColumn();
            ImGui::Text("%.3f", r.min);
            ImGui::TableNextColumn();
            ImGui::Text("%.3f", r.max);
            ImGui::TableNextColumn();
            ImGui::Text("%.1f%%", r.at0);
            ImGui::TableNextColumn();
            // Saturation is the diagnosis that explained the one shot the
            // network loses on, so it is coloured rather than left to be read.
            if (r.at1 >= 50.0)
                ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "%.1f%%", r.at1);
            else if (r.at1 >= 25.0)
                ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.35f, 1.0f), "%.1f%%", r.at1);
            else
                ImGui::Text("%.1f%%", r.at1);
            ImGui::TableNextColumn();
            ImGui::Text("%+.3f", r.rPoint);
            ImGui::TableNextColumn();
            ImGui::Text("%+.3f", r.rTemporal);
            ImGui::TableNextColumn();
            ImGui::Text("%+.3f", r.rSharpen);
        }
        ImGui::EndTable();
    }
    ImGui::TextDisabled(
        "'%% at 1' is how much of the corpus sits against the channel's clamp. depth reads "
        "1.0 for\nanything closer than %g world units, which is why an indoor corridor - a "
        "few units wide - is\nthe shot the network has always lost on: there it decides "
        "from one input fewer than it has.",
        (double)blss::kDepthRef);

    if (!blssFeat_.perShot.empty()) {
        ImGui::SeparatorText("Per-shot mean");
        ImGui::TextDisabled(
            "A channel that is the same number in every column cannot tell the shots apart, "
            "which is what it exists to do.");
        const int cols = 2 + (int)blssFeat_.shots.size();
        if (ImGui::BeginTable("blssfeatshot", cols,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_SizingStretchProp |
                                  ImGuiTableFlags_ScrollX)) {
            ImGui::TableSetupColumn("channel");
            for (const std::string& s : blssFeat_.shots) ImGui::TableSetupColumn(s.c_str());
            ImGui::TableSetupColumn("spread");
            ImGui::TableHeadersRow();
            for (const blssui::FeatureShotRow& r : blssFeat_.perShot) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(r.name.c_str());
                for (size_t i = 0; i < blssFeat_.shots.size(); ++i) {
                    ImGui::TableNextColumn();
                    if (i < r.means.size())
                        ImGui::Text("%.3f", r.means[i]);
                    else
                        ImGui::TextDisabled("-");
                }
                ImGui::TableNextColumn();
                if (r.spread < 0.02)
                    ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "%.3f", r.spread);
                else
                    ImGui::Text("%.3f", r.spread);
            }
            ImGui::EndTable();
        }
    }
}

// ========================================================================== //
// Training shots - what the corpus is allowed to see
// ========================================================================== //
//
// THE HOLE THIS FILLS. Everything else in this window MEASURES; this is the one
// tab that lets a user change the answer. The corpus shoots a project from six
// camera moves derived from its bounds and its player start, and the failure
// this feature keeps producing is a distribution mismatch - a net fitted to
// frames the console does not draw averages -0.34 dB over seven real projects
// and -1.09 dB on the worst of them, i.e. worse than leaving the upscaler off.
// A corpus is only as good as its coverage
// of the content the PLAYER will look at, and the person who knows where the
// player stands is the author, not a heuristic over an AABB.
//
// The Inputs tab is the other half of the loop: it says which channel is dead
// or pinned, and this is where that is fixed.

void App::drawBlssShotsTab() {
    ImGui::Spacing();
    ImGui::TextWrapped(
        "WHAT THE CORPUS IS ALLOWED TO SEE. The network is fitted to these frames and the "
        "console runs your game - so a shot list that misses the places a player stands is "
        "the single most expensive mistake available here. Measured: a net fitted to frames "
        "the console does not draw averages -0.34 dB over seven real projects and -1.09 at "
        "worst, i.e. worse than leaving the upscaler off, because the channels it leaned on "
        "were out of range on the real content.");
    ImGui::Spacing();
    if (!blssCorpusProject_) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
                           "The corpus is set to the PROCEDURAL BESTIARY, which has its own 13 "
                           "hand-written shots.\nNothing on this tab reaches it - switch the "
                           "corpus in the header to this project first.");
        ImGui::Spacing();
    }

    BlssShotPlan& pl = project_.blssShots;
    bool changed = false;

    // --- the six automatic moves ---------------------------------------------
    ImGui::SeparatorText("The automatic camera moves");
    ImGui::TextDisabled(
        "Derived per scene from its bounds and the player start, aimed at the area-weighted "
        "centroid of\nits triangles. Turn one off when it shoots content your game never "
        "shows - an empty sky pan is a\nsixth of the corpus spent on frames where bilinear IS "
        "the ground truth.");
    if (ImGui::BeginTable("blssautomoves", 3,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("shoot it", ImGuiTableColumnFlags_WidthFixed, scaled(120.0f));
        ImGui::TableSetupColumn("frames", ImGuiTableColumnFlags_WidthFixed, scaled(110.0f));
        ImGui::TableSetupColumn("what it teaches");
        ImGui::TableHeadersRow();
        for (int m = 0; m < kBlssAutoMoveCount; ++m) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            // THE MOVE'S NAME IS THE CHECKBOX'S LABEL, not a column beside it,
            // and that is the difference between a control a scripted run can
            // assert and one it cannot: a label hidden behind `##` leaves the
            // widget with nothing to name, so `expect-checked` can never see
            // it. A label IS an id here, so this also gives the six rows six
            // identities for free.
            if (ImGui::Checkbox(project::blssAutoMoveName(m), &pl.autoMove[m])) changed = true;
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            char flbl[64];
            std::snprintf(flbl, sizeof(flbl), "##frames-%s", project::blssAutoMoveName(m));
            ImGui::BeginDisabled(!pl.autoMove[m]);
            if (ImGui::DragInt(flbl, &pl.autoFrames[m], 0.2f, 0, 400,
                               pl.autoFrames[m] > 0 ? "%d" : "share"))
                changed = true;
            ImGui::EndDisabled();
            ImGui::TableNextColumn();
            ImGui::TextWrapped("%s", project::blssAutoMoveWhy(m));
        }
        ImGui::EndTable();
    }
    ImGui::Spacing();
    if (ImGui::Checkbox("Also shoot the Cutscene Director's camera tracks", &pl.authoredTakes))
        changed = true;
    prefHelp(
        "A camera track is already a polyline of (eye, look-at) keys - the exact\n"
        "shape the corpus wants - and an author who framed a shot has told the\n"
        "trainer which part of the scene matters, for the cost of a copy. Up to\n"
        "half the per-scene budget, and a track whose camera bindings do not\n"
        "resolve in that scene contributes nothing.");

    // --- the author's own vantages -------------------------------------------
    ImGui::SeparatorText("Your own vantages");
    ImGui::TextDisabled(
        "Aim these at the places a player actually stands: the corridor they walk down, the "
        "arena they\nfight in, the doorway they come through. That is coverage of the content "
        "the game will SHOW,\nwhich is the whole point of training on your own project.");

    const bool haveScene = !project_.scenes.empty();
    ImGui::BeginDisabled(!haveScene);
    if (ImGui::Button("Add the viewport camera", ImVec2(scaled(200.0f), 0))) {
        BlssShot s;
        float eye[3], target[3];
        viewport_.currentCamera(eye, target);
        for (int k = 0; k < 3; ++k) {
            s.eye[k] = s.eye2[k] = eye[k];
            s.look[k] = s.look2[k] = target[k];
        }
        if (project_.activeScene >= 0 && project_.activeScene < (int)project_.scenes.size())
            s.scene = project_.scenes[(size_t)project_.activeScene].name;
        s.name = "shot " + std::to_string(pl.shots.size() + 1);
        pl.shots.push_back(std::move(s));
        blssShotSel_ = (int)pl.shots.size() - 1;
        changed = true;
    }
    ImGui::EndDisabled();
    prefHelp(
        "Takes the editor viewport's current eye and look-at. Frame the shot the\n"
        "way you want the corpus to see it and press this - it is the fastest way\n"
        "to say 'train on THIS', and it costs no arithmetic.");
    ImGui::SameLine();
    // A selected Camera object is the other natural source, and it keeps the
    // shot LIVE: the vantage follows the object instead of freezing a copy of
    // its numbers that goes stale the moment somebody nudges the camera.
    const SceneObject* selCam = nullptr;
    if (hasProject_ && selectedObject_ >= 0 &&
        selectedObject_ < (int)project_.objects().size() &&
        project_.objects()[(size_t)selectedObject_].type == PrimitiveType::Camera)
        selCam = &project_.objects()[(size_t)selectedObject_];
    ImGui::BeginDisabled(selCam == nullptr);
    if (ImGui::Button("Add the selected Camera object", ImVec2(scaled(230.0f), 0)) && selCam) {
        BlssShot s;
        s.camera = selCam->name;
        s.scene = project_.scenes[(size_t)project_.activeScene].name;
        s.name = selCam->name;
        pl.shots.push_back(std::move(s));
        blssShotSel_ = (int)pl.shots.size() - 1;
        changed = true;
    }
    ImGui::EndDisabled();
    prefHelp(
        "The shot then FOLLOWS that object - move the camera and the corpus moves\n"
        "with it, where the numbers above are a copy taken once. Renaming the\n"
        "object retargets the shot like any other by-name reference.");
    if (!selCam) {
        ImGui::SameLine();
        ImGui::TextDisabled("select a Camera object to use it as a vantage");
    }

    if (pl.shots.empty()) {
        ImGui::TextDisabled(
            "    No authored shots. The automatic moves above are all the corpus will see.");
    } else if (ImGui::BeginTable("blssshots", 6,
                                 ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                     ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("on", ImGuiTableColumnFlags_WidthFixed, scaled(30.0f));
        ImGui::TableSetupColumn("name");
        ImGui::TableSetupColumn("scene");
        ImGui::TableSetupColumn("vantage");
        ImGui::TableSetupColumn("frames", ImGuiTableColumnFlags_WidthFixed, scaled(80.0f));
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, scaled(130.0f));
        ImGui::TableHeadersRow();
        int erase = -1;
        for (size_t i = 0; i < pl.shots.size(); ++i) {
            BlssShot& s = pl.shots[i];
            ImGui::PushID((int)i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (ImGui::Checkbox("##on", &s.enabled)) changed = true;
            ImGui::TableNextColumn();
            char name[128];
            std::snprintf(name, sizeof(name), "%s", s.name.c_str());
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::InputText("##name", name, sizeof(name))) s.name = name;
            if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(s.scene.empty() ? "(first)" : s.scene.c_str());
            ImGui::TableNextColumn();
            if (!s.camera.empty())
                ImGui::Text("Camera '%s'%s", s.camera.c_str(),
                            s.move && !s.cameraTo.empty() ? " -> ..." : "");
            else
                ImGui::Text("%.1f %.1f %.1f%s", s.eye[0], s.eye[1], s.eye[2],
                            s.move ? "  (moving)" : "");
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::DragInt("##frames", &s.frames, 0.2f, 0, 400,
                               s.frames > 0 ? "%d" : "share"))
                changed = true;
            ImGui::TableNextColumn();
            if (ImGui::SmallButton(blssShotSel_ == (int)i ? "Close" : "Edit"))
                blssShotSel_ = blssShotSel_ == (int)i ? -1 : (int)i;
            ImGui::SameLine();
            if (ImGui::SmallButton("Delete")) erase = (int)i;
            ImGui::PopID();
        }
        ImGui::EndTable();
        if (erase >= 0) {
            pl.shots.erase(pl.shots.begin() + erase);
            if (blssShotSel_ >= (int)pl.shots.size()) blssShotSel_ = -1;
            changed = true;
        }
    }

    // --- the editor for one shot ---------------------------------------------
    if (blssShotSel_ >= 0 && blssShotSel_ < (int)pl.shots.size()) {
        BlssShot& s = pl.shots[(size_t)blssShotSel_];
        ImGui::SeparatorText("Editing this shot");
        ImGui::PushID("blssshotedit");
        // Scene, BY NAME - a scene index rots the moment one is deleted, and a
        // shot pointing at the wrong scene is worse than one pointing at none:
        // the second is reported, the first is silently the wrong content.
        {
            int cur = 0;
            std::vector<const char*> names;
            names.push_back("(the first scene)");
            for (size_t i = 0; i < project_.scenes.size(); ++i) {
                names.push_back(project_.scenes[i].name.c_str());
                if (project_.scenes[i].name == s.scene) cur = (int)i + 1;
            }
            ImGui::SetNextItemWidth(scaled(220));
            if (ImGui::Combo("Scene", &cur, names.data(), (int)names.size())) {
                s.scene = cur == 0 ? std::string() : project_.scenes[(size_t)cur - 1].name;
                changed = true;
            }
        }
        const int si = project::blssShotScene(project_, s);
        // Camera pickers, one per key. Every entry gets an explicit id: a
        // Selectable's LABEL is its id, so two identical names in one popup
        // collide silently and no scripted run can catch it.
        const auto cameraCombo = [&](const char* label, std::string& slot) {
            std::vector<std::string> opts;
            opts.push_back("(use the numbers below)");
            if (si >= 0)
                for (const SceneObject& o : project_.scenes[(size_t)si].objects)
                    if (o.type == PrimitiveType::Camera) opts.push_back(o.name);
            int cur = 0;
            for (size_t i = 1; i < opts.size(); ++i)
                if (opts[i] == slot) cur = (int)i;
            std::vector<const char*> raw;
            for (const std::string& o : opts) raw.push_back(o.c_str());
            ImGui::SetNextItemWidth(scaled(220));
            if (ImGui::Combo(label, &cur, raw.data(), (int)raw.size())) {
                slot = cur == 0 ? std::string() : opts[(size_t)cur];
                changed = true;
            }
            // A name that survived a scene change but matches nothing here is
            // the one failure mode that silently removes a shot from the corpus.
            if (!slot.empty() && cur == 0)
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
                                   "    '%s' is not a Camera object in this scene - this shot "
                                   "will NOT be shot.",
                                   slot.c_str());
        };
        cameraCombo("Camera object", s.camera);
        if (s.camera.empty()) {
            ImGui::SetNextItemWidth(scaled(260));
            if (ImGui::DragFloat3("Eye", s.eye, 0.1f, 0.0f, 0.0f, "%.2f")) changed = true;
            ImGui::SetNextItemWidth(scaled(260));
            if (ImGui::DragFloat3("Looking at", s.look, 0.1f, 0.0f, 0.0f, "%.2f")) changed = true;
            ImGui::SameLine();
            if (ImGui::SmallButton("Grab the viewport")) {
                viewport_.currentCamera(s.eye, s.look);
                changed = true;
            }
        }
        if (ImGui::Checkbox("It moves (a second key)", &s.move)) changed = true;
        prefHelp(
            "Off is a still standpoint, and that is a legitimate shot rather than a\n"
            "degenerate one: with a frozen camera the history is perfect, and the\n"
            "network has to learn NOT to spend passes there. On gives the shot a\n"
            "second vantage and the corpus interpolates between the two.");
        if (s.move) {
            ImGui::Indent(scaled(16.0f));
            cameraCombo("Camera object (end)", s.cameraTo);
            if (s.cameraTo.empty()) {
                ImGui::SetNextItemWidth(scaled(260));
                if (ImGui::DragFloat3("Eye (end)", s.eye2, 0.1f, 0.0f, 0.0f, "%.2f"))
                    changed = true;
                ImGui::SetNextItemWidth(scaled(260));
                if (ImGui::DragFloat3("Looking at (end)", s.look2, 0.1f, 0.0f, 0.0f, "%.2f"))
                    changed = true;
                ImGui::SameLine();
                if (ImGui::SmallButton("Grab the viewport (end)")) {
                    viewport_.currentCamera(s.eye2, s.look2);
                    changed = true;
                }
            }
            ImGui::Unindent(scaled(16.0f));
        }
        ImGui::SetNextItemWidth(scaled(160));
        if (ImGui::DragFloat("Field of view", &s.fovDeg, 0.5f, 20.0f, 140.0f, "%.0f deg"))
            changed = true;
        // The cheapest possible check that a vantage points where the author
        // meant: put the editor's own camera there. Anything else is reading six
        // numbers and imagining a frame.
        if (ImGui::Button("Look through this shot", ImVec2(scaled(190.0f), 0))) {
            float a[3], la[3], b[3], lb[3], fov = s.fovDeg;
            if (project::blssResolveShot(project_, s, a, la, b, lb, &fov)) {
                for (int k = 0; k < 3; ++k) blssLookEye_[k] = a[k], blssLookAt_[k] = la[k];
                blssLookFov_ = fov;
                blssLookThrough_ = true;
                statusMessage_ =
                    "BLSS: looking through " + project::blssShotLabel(project_, s, blssShotSel_);
            } else {
                statusMessage_ = "BLSS: that shot does not resolve - see the warning above";
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Give the camera back", ImVec2(scaled(170.0f), 0))) {
            blssLookThrough_ = false;
            viewport_.clearCameraOverride();
        }
        if (blssLookThrough_)
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.35f, 1.0f),
                               "    The viewport is parked at this shot's FIRST key - the orbit "
                               "camera is on hold.");
        ImGui::PopID();
    }

    drawBlssShotPlanPreview();
    if (changed) commitChange();
}

// WHAT WILL ACTUALLY BE SHOT, resolved the way the corpus resolves it. A plan
// is only worth authoring if the author can see its consequence, and the two
// consequences that matter are which shots got dropped and how the frame budget
// landed on the survivors.
void App::drawBlssShotPlanPreview() {
    ImGui::SeparatorText("The shot list this will produce");
    const std::vector<PlannedShot> plan = planShots(project_);
    std::vector<int> frames;
    int shortfall = 0;
    const bool fits = splitFrames(plan, blssTrainFrames_, frames, shortfall);
    int live = 0;
    for (const PlannedShot& s : plan)
        if (s.problem.empty()) ++live;

    if (live == 0) {
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
                           "NOTHING WOULD BE SHOT. The trainer falls back to the procedural "
                           "bestiary, and a net fitted\nto that alone averages -0.34 dB on a "
                           "real project (-1.09 at worst). Turn a move back on\nor add a "
                           "vantage.");
    } else if (project_.blssShots.isDefault()) {
        ImGui::TextDisabled(
            "%d shot(s) - the default plan, so this project's corpus is byte-identical to what "
            "it has\nalways been. Nothing above has been authored yet.",
            live);
    } else {
        ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1.0f),
                           "%d shot(s) over %zu scene(s), %d training frame(s) split between "
                           "them.",
                           live, project_.scenes.size(), blssTrainFrames_);
    }
    if (!fits)
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
                           "    The explicit frame counts alone overrun the budget - %d shot(s) "
                           "would get none at all.\n    Raise 'Frames' on the Train tab, or "
                           "lower a count.",
                           shortfall);

    if (ImGui::BeginTable("blssplan", 5,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, scaled(30.0f));
        ImGui::TableSetupColumn("scene");
        ImGui::TableSetupColumn("shot");
        ImGui::TableSetupColumn("move");
        ImGui::TableSetupColumn("frames", ImGuiTableColumnFlags_WidthFixed, scaled(160.0f));
        ImGui::TableHeadersRow();
        int n = 0;
        for (size_t i = 0; i < plan.size(); ++i) {
            const PlannedShot& s = plan[i];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (s.problem.empty())
                ImGui::Text("%d", n++);
            else
                ImGui::TextDisabled("-");
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(s.scene.c_str());
            ImGui::TableNextColumn();
            if (!s.problem.empty())
                ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "%s", s.name.c_str());
            else if (s.authored)
                ImGui::TextColored(ImVec4(0.55f, 0.80f, 1.0f, 1.0f), "%s", s.name.c_str());
            else
                ImGui::TextUnformatted(s.name.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(s.kind.c_str());
            ImGui::TableNextColumn();
            if (!s.problem.empty())
                ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "dropped: %s",
                                   s.problem.c_str());
            else if (s.frames > 0)
                ImGui::Text("%d  (asked for)", frames[i]);
            else
                ImGui::TextDisabled("%d", frames[i]);
        }
        ImGui::EndTable();
    }
    ImGui::TextDisabled(
        "A take row is an ESTIMATE: the trainer drops a Cutscene Director track whose camera\n"
        "bindings do not resolve in its own scene, and this cannot see that without "
        "re-deriving\nthem. Every other row is the corpus' own resolution, called from here.");

    // DID THE TRAINER ACTUALLY DO THIS? A plan the tool ignores looks exactly
    // like a plan it obeys, so the last run's own per-scene report is compared
    // against what was asked for. It is the falsifiability rule this whole
    // window is built on, applied to the one thing in it that is an INPUT.
    if (!blssScenes_.empty()) {
        int reported = 0;
        for (const blssui::CorpusScene& s : blssScenes_) reported += s.shots;
        ImGui::Spacing();
        if (reported == live)
            ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1.0f),
                               "The last run reported %d shot(s) over %zu scene(s) - the plan "
                               "above is what it shot.",
                               reported, blssScenes_.size());
        else
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.35f, 1.0f),
                               "The last run reported %d shot(s); this plan asks for %d. Either "
                               "the run predates the plan,\nor this build of the trainer does "
                               "not read it yet - check the [blss] scene lines below.",
                               reported, live);
        for (const blssui::CorpusScene& s : blssScenes_)
            ImGui::TextDisabled("    %s: %d shot(s), %zu triangle(s)", s.name.c_str(), s.shots,
                                s.triangles);
    }
}

// ========================================================================== //
// Console probe - place a REAL frame in this corpus' distribution
// ========================================================================== //
//
// This instrument already existed and was a chore: the engine writes a
// `BLSSFEAT` line into the game's log once a second under debug view 2, and the
// user had to find it, copy it, and paste it into a command line. It is the
// only thing that can answer "is the network being fed what it was trained on",
// and it is what found both host/console divergences this feature has had - so
// the friction was on the one step nobody should skip.
//
// THE TRAP THIS PANEL EXISTS AROUND: bin/log.txt does NOT exist when the game
// runs over ps2link. templates.cpp sets `writeLogsToFile = !ps2link`, because
// under ps2link the EE console is better - printf goes over the network and the
// editor shows it live in the Output panel. So there are two sources, the panel
// tries both, and it SAYS which one it used rather than showing an empty box.

void App::drawBlssProbeTab() {
    ImGui::Spacing();
    ImGui::TextWrapped(
        "Takes a feature vector the CONSOLE measured and places it inside this corpus' own "
        "distribution, per channel. It is the only way to know the network is being fed what "
        "it was trained on - and a channel outside the corpus' range is not interpolation, it "
        "is a 12-unit hidden layer extrapolating. That is the mechanism behind a bestiary-only "
        "net's -0.34 dB average over seven real projects, and -1.09 at worst.");
    ImGui::Spacing();
    drawBlssCorpusReminder();
    ImGui::Spacing();

    // --- step 1: the debug view ---------------------------------------------
    ImGui::SeparatorText("1 - let the game log its own feature spread");
    const int dv = std::clamp(project_.settings.blssDebugView, 0, 2);
    if (dv == 2) {
        ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1.0f),
                           "Debug view 2 is ON: the game writes a BLSSFEAT line about once a "
                           "second.");
        ImGui::SameLine();
        if (ImGui::SmallButton("Turn it off again")) {
            project_.settings.blssDebugView = 0;
            commitChange();
        }
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.35f, 1.0f),
                           "Debug view 2 is OFF, so the game logs nothing to probe.");
        if (ImGui::Button("Turn on debug view 2", ImVec2(scaled(200.0f), 0))) {
            project_.settings.blssDebugView = 2;
            commitChange();
            statusMessage_ = "BLSS: debug view 2 on - rebuild and run the game";
        }
        prefHelp(
            "Sets the project's own 'Debug view' to 2 (Project settings tab). It\n"
            "does not tint the picture - it logs BLSSGRID / BLSSFEAT / BLSSOUT /\n"
            "BLSSFILL / BLSSWORST once a second. It IS compiled into the build, so\n"
            "set it back to Off before handing the game to anyone.");
    }
    ImGui::TextDisabled(
        "    Then REBUILD and run the game (the debug view is compiled in - saving is not "
        "enough), and\n    play the part of the game you care about. Come back here and press "
        "the button below.");

    // --- step 2: find the line ----------------------------------------------
    ImGui::SeparatorText("2 - read the line out of the game's log");
    if (ImGui::Button("Read the game's log", ImVec2(scaled(190.0f), 0))) {
        const std::string where = blssFindFeatLine();
        if (where.empty()) {
            blssProbeSource_.clear();
            blssProbeNote_ =
                "No BLSSFEAT line found. Either the game has not run with debug view 2 since "
                "the last build, or it ran over ps2link - see below.";
        } else {
            blssProbeSource_ = where;
            blssProbeNote_.clear();
        }
    }
    prefHelp(
        "Looks in the project's own bin/log.txt first (that is where a PCSX2 run\n"
        "writes it, through host: fs), then in this editor's Output panel, which\n"
        "carries the [ps2] stream from a ps2link deploy. It takes the LAST\n"
        "BLSSFEAT line, because that describes the frame the player was looking\n"
        "at most recently.");
    // GUARDED, because an unconditional SameLine() with nothing to put on it
    // hands the line to whatever is submitted NEXT - which here is the
    // multi-line ps2link explanation, and it came out beside the button.
    if (!blssProbeSource_.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("from %s", blssProbeSource_.c_str());
    }

    // The ps2link half, stated plainly instead of shown as an empty panel.
    {
        std::error_code ec;
        const std::string logPath = (fs::path(project_.dir) / "bin" / "log.txt").string();
        const bool haveLog = fs::is_regular_file(logPath, ec);
        if (!haveLog)
            ImGui::TextColored(
                ImVec4(1.0f, 0.75f, 0.35f, 1.0f),
                "    There is no bin/log.txt in this project. That is EXPECTED for a game run "
                "on real\n    hardware over ps2link: the engine sends its log over the network "
                "instead of writing a\n    file, so the lines arrive in this editor's Output "
                "panel while the session is live. Either\n    copy a BLSSFEAT line from there "
                "into the box below, or run the game in PCSX2 once -\n    a PCSX2 run writes "
                "the file through host: fs and this button finds it by itself.");
        else
            ImGui::TextDisabled("    %s", logPath.c_str());
    }
    if (!blssProbeNote_.empty())
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "    %s", blssProbeNote_.c_str());

    // A REAL LABEL, not `##blssfeatline`. A label hidden behind `##` leaves the
    // widget with nothing for a scripted run to name, and this is the one field
    // in the window a test has to be able to fill.
    ImGui::SetNextItemWidth(-scaled(150.0f));
    ImGui::InputText("BLSSFEAT line", blssProbeLine_, sizeof(blssProbeLine_));
    prefHelp(
        "Editable, so a line copied from anywhere - the Output panel, a teammate's\n"
        "log, a screenshot typed back in - can be probed. The parser ignores the\n"
        "`BLSSFEAT` prefix and any punctuation between fields, so paste it whole.");

    // --- step 3: run it ------------------------------------------------------
    ImGui::SeparatorText("3 - place it in the corpus");
    const bool haveLine = blssProbeLine_[0] != '\0';
    ImGui::BeginDisabled(blssJob_.running() || !haveLine);
    if (ImGui::Button("Place this frame in the corpus", ImVec2(scaled(250.0f), 0)))
        blssRunProbe();
    ImGui::EndDisabled();
    ImGui::SameLine();
    {
        const int cores = std::max(1, (int)std::thread::hardware_concurrency());
        const blssui::Cost cost =
            blssui::estimate(blssui::Kind::Features, blssFeatFrames_, 0, cores, 1, 0,
                             blssExpectedShots());
        if (!haveLine)
            ImGui::TextDisabled("no line yet - read the log, or paste one above.");
        else
            ImGui::TextDisabled("%s on this machine (%d cores) - it renders the corpus first.",
                                blssui::humanDuration(cost.total).c_str(), cores);
    }

    if (!blssProbe_.ok()) {
        ImGui::Spacing();
        ImGui::TextDisabled(
            "No probe table yet. The output pane below carries whatever the last run printed.");
        return;
    }

    // --- the verdict + the table --------------------------------------------
    const ImVec4 bad(1.0f, 0.45f, 0.35f, 1.0f), warn(1.0f, 0.75f, 0.35f, 1.0f),
        good(0.45f, 0.85f, 0.45f, 1.0f);
    ImGui::SeparatorText("The answer");
    ImVec4 col = warn;
    if (blssProbeVerdict_.level == blssui::ProbeVerdict::Level::Mismatch) col = bad;
    if (blssProbeVerdict_.level == blssui::ProbeVerdict::Level::Matches) col = good;
    ImGui::TextColored(col, "%s", blssProbeVerdict_.headline.c_str());
    ImGui::TextWrapped("%s", blssProbeVerdict_.why.c_str());

    if (ImGui::BeginTable("blssprobe", 8,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_SizingStretchProp)) {
        const char* heads[] = {"channel",      "console min/mean/max", "spread", "corpus range",
                               "percentile",   "support",              "band",   "verdict"};
        for (const char* h : heads) ImGui::TableSetupColumn(h);
        ImGui::TableHeadersRow();
        for (const blssui::ProbeRow& r : blssProbe_.rows) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(r.name.c_str());
            if (ImGui::IsItemHovered()) {
                const char* why = blssui::channelPurpose(r.name);
                if (why[0]) ImGui::SetTooltip("%s", why);
            }
            if (!r.given) {
                for (int c = 0; c < 6; ++c) {
                    ImGui::TableNextColumn();
                    ImGui::TextDisabled("-");
                }
                ImGui::TableNextColumn();
                ImGui::TextColored(warn, "not in the line");
                continue;
            }
            ImGui::TableNextColumn();
            ImGui::Text("%.3f / %.3f / %.3f", r.lo, r.mid, r.hi);
            ImGui::TableNextColumn();
            // No spread means the same number in every tile of the console's
            // frame, i.e. a network making no per-tile decision from it at all.
            if (r.spread <= 1e-4)
                ImGui::TextColored(bad, "%.3f", r.spread);
            else
                ImGui::Text("%.3f", r.spread);
            ImGui::TableNextColumn();
            ImGui::Text("%.3f..%.3f", r.corpusLo, r.corpusHi);
            ImGui::TableNextColumn();
            ImGui::Text("%.1f%%", r.pct);
            ImGui::TableNextColumn();
            if (r.supp < 1.0)
                ImGui::TextColored(bad, "%.1f%%", r.supp);
            else
                ImGui::Text("%.1f%%", r.supp);
            ImGui::TableNextColumn();
            ImGui::Text("%.1f%%", r.band);
            ImGui::TableNextColumn();
            ImGui::TextColored(r.outOfRange ? bad : (r.noSupport || r.constant) ? warn : good,
                               "%s", r.verdict.c_str());
        }
        ImGui::EndTable();
    }
    // `%%`, not `%`: ImGui::TextDisabled is printf-style, so a literal percent
    // sign is a conversion. The first draft of this caption rendered as
    // "134014523040f the tiles is 134014076620f the gradient" - which is what a
    // format string reading past its (absent) arguments looks like, and it is
    // one keystroke away in every explanatory line this window has.
    ImGui::TextDisabled(
        "'support' is how much of the corpus lies within +-0.05 of the console's MEAN, and it "
        "is the\ncolumn that decides interpolation against extrapolation - 1%% of the tiles is "
        "1%% of the\ngradient. 'band' is how much of the corpus falls inside the console's own "
        "min..max, i.e. how\nmuch of what was taught this frame can even reach.");
    ImGui::Spacing();
    ImGui::TextDisabled(
        "This is ONE frame. Probe the parts of your game that look different from each other -\n"
        "an interior corridor and an open vista put wholly different numbers in the depth and\n"
        "coverage channels, and a corpus can be right about one and wrong about the other.");
}

// The freshest BLSSFEAT line, from whichever source has one, and a description
// of that source for the panel to print. bin/log.txt first - a PCSX2 run writes
// it through host: fs and it is the whole log; then the editor's own Output
// panel, which is where the [ps2] stream from a ps2link deploy lands, and the
// ONLY source that exists for a game running on real hardware.
std::string App::blssFindFeatLine() {
    if (!hasProject_) return "";
    std::error_code ec;
    const std::string logPath = (fs::path(project_.dir) / "bin" / "log.txt").string();
    if (fs::is_regular_file(logPath, ec)) {
        // The tail is enough and bounded: the engine writes a block a second,
        // so a long session is megabytes and only the end of it is current.
        const std::string tail = readTextFileTail(logPath, 256 * 1024);
        const std::string line = blssui::lastFeatLine(tail);
        if (!line.empty()) {
            std::snprintf(blssProbeLine_, sizeof(blssProbeLine_), "%s", line.c_str());
            return "the project's bin/log.txt";
        }
    }
    const std::string fromRunner = blssui::lastFeatLine(logOut_.text);
    if (!fromRunner.empty()) {
        std::snprintf(blssProbeLine_, sizeof(blssProbeLine_), "%s", fromRunner.c_str());
        return "this editor's Output panel (the ps2link [ps2] stream)";
    }
    return "";
}

void App::blssRunProbe() {
    std::vector<std::string> a{"--blss-eval", "--features"};
    for (const std::string& c : blssCommonArgs()) a.push_back(c);
    a.push_back("--frames");
    a.push_back(std::to_string(blssFeatFrames_));
    a.push_back("--fill-weight");
    a.push_back(numArg(blssTrainFill_));
    a.push_back("--probe");
    a.push_back(blssProbeLine_);
    blssStart(blssui::Kind::Features, a, 0);
}

// ========================================================================== //
// "Is this corpus good enough to train on" - the third verdict
// ========================================================================== //
//
// The window already answers "will the picture improve" and "will the frame get
// faster". This is the third question, and it is the one that catches the
// failure the other two cannot see: a corpus so thin that the net fitted to it
// makes the game WORSE. The arithmetic is blssui::corpusHealth() - pure, so all
// four verdicts are walkable from a harness; this chooses words and colours.
//
// It follows the speed verdict's standard: an UNMEASURED corpus reads as
// unmeasured, never as fine.
void App::drawBlssHealth(bool compact) {
    const ImVec4 bad(1.0f, 0.45f, 0.35f, 1.0f), warn(1.0f, 0.75f, 0.35f, 1.0f),
        good(0.45f, 0.85f, 0.45f, 1.0f);
    ImVec4 col = warn;
    switch (blssHealth_.verdict) {
        case blssui::CorpusHealth::Verdict::Unusable: col = bad; break;
        case blssui::CorpusHealth::Verdict::Good: col = good; break;
        case blssui::CorpusHealth::Verdict::Thin: col = warn; break;
        default: col = warn; break;
    }
    if (!blssHealth_.ok) {
        // Deliberately TextDisabled and deliberately not green: nothing has
        // been measured, and the one thing this must never do is look like a
        // pass. The prose is stated HERE rather than read off the struct
        // because the struct is also default-constructed before any run, and a
        // "Corpus:" with nothing after it is the reassuring blank this whole
        // verdict exists to avoid.
        ImGui::TextDisabled(
            "Corpus: not measured - so nothing here can tell you whether it is any good.");
        if (!compact)
            ImGui::TextWrapped(
                "Press 'Is the corpus good enough?' above. It reports the six input channels "
                "over these frames and says, in one line, whether a network fitted to them can "
                "learn anything at all - which is a different question from whether the scene "
                "has headroom, and the one that catches a corpus missing a channel outright.");
        return;
    }
    ImGui::TextColored(col, "Corpus: %s", blssHealth_.headline.c_str());
    ImGui::TextWrapped("%s", blssHealth_.why.c_str());
    if (compact) {
        if (!blssHealth_.findings.empty())
            ImGui::TextDisabled("    %zu finding(s) named on the Inputs tab.",
                                blssHealth_.findings.size());
        return;
    }
    for (const blssui::CorpusFinding& f : blssHealth_.findings) {
        const ImVec4 fc = f.level == blssui::CorpusFinding::Level::Fatal ? bad
                          : f.level == blssui::CorpusFinding::Level::Warn ? warn
                                                                          : ImVec4(0.65f, 0.65f,
                                                                                   0.65f, 1.0f);
        ImGui::TextColored(fc, "  - %s", f.what.c_str());
        ImGui::TextDisabled("      %s", f.fix.c_str());
    }
}
