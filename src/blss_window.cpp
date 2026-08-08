// Tools > Neural Upscaler (BLSS): the window. Its host half - the job that
// runs the editor's own CLI and the parsers that read the tool's tables back -
// is blss_ui.cpp; this file only draws.
//
// App:: methods declared in app.hpp, own TU (the credits_ui.cpp precedent).
// The clash warnings and the five project settings are drawn from HERE by both
// this window and Project > Preferences, so there is one mirror of
// templates.cpp's blssClashes() and one set of tooltips, not two.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
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
    BlssClash c;
    if (!hasProject_) return c;
    const auto dofActive = [](float amount, float focus) {
        return (int)(amount * 128.0f + 0.5f) > 0 && focus > 0.0f;
    };
    const bool splitPref = staged.multiplayer == "split";
    for (const SceneData& sc : project_.scenes) {
        // project::resolvedSettings()' post-fx branch, against the STAGED
        // project settings: dofAmount/dofFocus come from the scene only when it
        // overrides that group, and nothing else in that function - the
        // ambience preset overlay included - touches either field.
        const ProjectSettings& fx = sc.overrides.postFx ? sc.settings : staged;
        if (dofActive(fx.dofAmount, fx.dofFocus)) c.dof = true;
        int players = 0;
        for (const SceneObject& o : sc.objects) {
            if (o.type == PrimitiveType::Player) ++players;
            if (o.type == PrimitiveType::Portal && !o.portalTarget.empty())
                for (const SceneObject& t : sc.objects)
                    if (&t != &o && t.type == PrimitiveType::Portal &&
                        t.name == o.portalTarget)
                        c.portals = true;
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
                if (dofActive(n.num[2], posWired ? 1.0f : n.num[0])) c.dofNode = true;
            }
        }
        if (splitPref && players >= 2) c.split = true;
    }
    return c;
}

void App::drawBlssClashWarning(const BlssClash& c) {
    if (!c.any()) return;
    const ImVec4 warn(1.0f, 0.6f, 0.2f, 1.0f);
    ImGui::TextColored(warn,
                       "    Cannot be combined with what this project uses,\n"
                       "    and the BUILD WILL REFUSE IT:");
    if (c.dof || c.dofNode)
        ImGui::TextColored(
            warn,
            "      - Depth of field composites its blur through the GS depth\n"
            "        test at display resolution, and with this on the depth\n"
            "        buffer is only as big as the reduced render.");
    if (c.dofNode)
        ImGui::TextColored(
            warn,
            c.dof ? "          ...and a Set Depth Of Field flow node turns it\n"
                    "          on at runtime as well: set that node's Mode to\n"
                    "          Off, or delete it."
                  : "          The authored amount is 0, but a Set Depth Of\n"
                    "          Field flow node turns it ON at runtime. Set\n"
                    "          that node's Mode to Off, or delete it.");
    if (c.portals)
        ImGui::TextColored(
            warn,
            "      - Portals want that same display-resolution depth, and a\n"
            "        through-view carves its opening from inside the scene\n"
            "        pass with a display-sized raster window. Only a LINKED\n"
            "        pair renders one, so unlinking is a fix too.");
    if (c.split)
        ImGui::TextColored(
            warn,
            "      - Split-screen frames are never reduced at all (only the\n"
            "        single-view path is), and scene depth writes are masked\n"
            "        outside the reduced pass - so both halves would render\n"
            "        full-resolution with no working depth buffer.");
    ImGui::TextColored(warn, "    Turn one of the two off; either side resolves it.");
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
        "PROOF OF CONCEPT, and the FIRST thing to know is which network you\n"
        "have. A net fitted to the built-in procedural corpus was measured on\n"
        "a real project (examples/procedural, 72 frames, six camera moves) at\n"
        "-0.40 dB against plain bilinear - WORSE than leaving this off - while\n"
        "paying half a composite pass more, because that corpus' most\n"
        "predictive channels are out of range on a scene it did not see. The\n"
        "same trainer fitted to THAT PROJECT'S OWN SCENES scored +0.06 dB at\n"
        "1.19 passes against an oracle ceiling of +0.77. So: train on your\n"
        "project, in Tools > Neural Upscaler (BLSS), with the corpus set to\n"
        "this project and 'Fit every shot' on. That is the net to ship.\n"
        "\n"
        "AND SOME SCENES HAVE NOTHING TO WIN. On examples/showcase the ORACLE\n"
        "itself - the best any per-tile weighting can do - scores +0.00 dB at\n"
        "1.00 passes: soft ground texture, low-poly props, nothing that\n"
        "aliases. Run the window's Evaluate tab on your project BEFORE turning\n"
        "this on; it says so in one line.\n"
        "\n"
        "On the built-in corpus, held out shot by shot, it is +0.42 dB (13\n"
        "shots x 3 seeds = 39 fold-runs, sd 0.40, 3 of the 39 below bilinear) -\n"
        "but that is a number about the bestiary, not about your game. No BLSS\n"
        "frame has ever been TIMED, in the emulator or on hardware, so whether\n"
        "it is faster on your scene is genuinely unknown, and nobody has\n"
        "watched the picture in PCSX2 since the training objective last\n"
        "changed.\n"
        "\n"
        "VRAM it gives back: the z-buffer shrinks with the render, which\n"
        "returns more than the reduced-resolution target costs (measured at\n"
        "PAL 512x512: 0.227 MB of texture VRAM free with this off, 0.727 MB\n"
        "with it on). Fill it costs: up to five full-screen composite passes,\n"
        "which is more than the reduced render saves whenever the network\n"
        "asks for every kernel everywhere.\n"
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
        "is built with random weights and says so in its boot log.");
    if (s.blssEnabled)
        ImGui::TextColored(
            ImVec4(0.65f, 0.65f, 0.65f, 1.0f),
            "    Proof of concept, and it has to be trained on THIS PROJECT: a net\n"
            "    fitted to the built-in procedural corpus measured -0.40 dB on a real\n"
            "    project, i.e. worse than leaving this off, where one fitted to that\n"
            "    project's own scenes measured +0.06 against a ceiling of +0.77. Some\n"
            "    scenes have no ceiling at all - on examples/showcase the oracle itself\n"
            "    is +0.00 dB. Tools > Neural Upscaler (BLSS) trains it and its Evaluate\n"
            "    tab answers 'will this scene benefit' in one line. No frame of it has\n"
            "    ever been timed on console or hardware; look at it in PCSX2 too.");
    if (s.blssEnabled) drawBlssClashWarning(blssClashesFor(s));

    ImGui::BeginDisabled(!s.blssEnabled);
    {
        int scale = s.blssScale == 1 ? 1 : 0;
        const char* names[] = {
            "2x2 - quarter the pixels (256x224 of a PAL 512x448 frame)",
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
            "The z-buffer is allocated at THIS size, so on a 512x448 output\n"
            "2x2 hands 672 KB of VRAM back and 1x2 hands back 448 KB.\n"
            "Train the network at the scale you ship - the Train tab follows\n"
            "this setting. A blss.net is a bare list of floats and records nothing\n"
            "about how it was trained, so a mismatch costs quality without\n"
            "saying anything.");

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
            "It is also the switch to reach for if the picture SHAKES. A\n"
            "visible sub-pixel bob was seen in the emulator before the\n"
            "training objective was retuned; the host's stability metric\n"
            "improved with the retune, but nobody has watched a television\n"
            "since, so it is neither confirmed fixed nor confirmed present.\n"
            "It is a RUNTIME switch only: the trainer always labels with the\n"
            "temporal kernel available, so turning it off does not need and\n"
            "does not get a different network.");

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
    blssNetBytes_ = 0;
    blssNetWhen_.clear();
    blssNetArgs_.clear();
    blssNetArgsStale_ = false;
    const std::string path = blssNetPath();
    if (path.empty()) return;
    std::error_code ec;
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
        "bestiary was measured ON a real project (examples/procedural, 72\n"
        "frames, six camera moves) at -0.40 dB against plain bilinear - WORSE\n"
        "than switching the feature off - while paying half a composite pass\n"
        "more. The same trainer fitted to that project's own scenes scored\n"
        "+0.06 dB at 1.19 passes, and the oracle's ceiling there is +0.77.\n"
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
        ImGui::TextDisabled("    %s", project_.dir.c_str());
        ImGui::TextDisabled(
            "    Primitives, static .obj and the terrain, walked / panned / orbited /\n"
            "    whipped / pitched / strafed from the scene's bounds and the player start,\n"
            "    plus any authored Cutscene Director camera track. Animated .glb is skipped -\n"
            "    it goes down the dynamic pipeline, which does not feed the upscaler at all.");
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
                           "    13 hand-written procedural shots. Measured at -0.40 dB - worse "
                           "than no BLSS -\n    on a real project. Use it to reproduce the "
                           "documented fold tables, not to ship a game.");
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
    // A different project means different everything: the net beside it, the
    // images it dumped, the tables measured against it. Detected here rather
    // than in the window body so the window cannot be opened onto the previous
    // project's numbers.
    const std::string dump =
        hasProject_ ? (fs::path(project_.dir) / "blss-compare").string() : std::string();
    if (dump != blssDumpDir_) {
        blssDumpDir_ = dump;
        blssEval_ = blssui::EvalTable{};
        blssCv_ = blssui::CvTable{};
        blssFeat_ = blssui::FeatureTable{};
        blssLastError_.clear();
        blssImagesDirty_ = true;
        blssStatusChecked_ = -1.0e9;
    }
    if (blssJobSeen_ == blssJob_.version()) return;
    blssJobSeen_ = blssJob_.version();
    const std::string text = blssJob_.log();
    const blssui::Kind k = blssJob_.kind();
    const std::vector<std::string> errs = blssui::parseErrors(text);
    blssLastError_.clear();
    for (const std::string& e : errs) blssLastError_ += (blssLastError_.empty() ? "" : "\n") + e;

    switch (k) {
        case blssui::Kind::Eval: {
            blssEval_ = blssui::parseEval(text);
            blssImagesDirty_ = true;
            break;
        }
        case blssui::Kind::Cv: blssCv_ = blssui::parseCv(text); break;
        case blssui::Kind::Features: blssFeat_ = blssui::parseFeatures(text); break;
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
    if (!showBlss_ || !hasProject_) return;
    ImGui::SetNextWindowSize(ImVec2(scaled(1060.0f), scaled(820.0f)), ImGuiCond_FirstUseEver);
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

    drawBlssHeader();

    const float outMax = std::max(scaled(60.0f), ImGui::GetContentRegionAvail().y * 0.7f);
    const float outH = std::clamp(blssOutputH_, scaled(60.0f), outMax);
    ImGui::BeginChild("blsstabs", ImVec2(0, -(outH + scaled(14.0f))), false);
    if (ImGui::BeginTabBar("blsstabbar")) {
        if (ImGui::BeginTabItem("Train")) {
            drawBlssTrainTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Evaluate")) {
            drawBlssEvalTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Cross-validate")) {
            drawBlssCvTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Compare")) {
            drawBlssImagesTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Inputs")) {
            drawBlssFeaturesTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Project settings")) {
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

    // "A project with BLSS on and no net is a build failure that used to be
    // silent" - so this line is the first thing the window says.
    ImGui::TextUnformatted("Project network:");
    ImGui::SameLine();
    if (blssNetPresent_) {
        ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1.0f), "%s  -  %zu bytes, %s",
                           blssNetName_, blssNetBytes_, blssNetWhen_.c_str());
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
        }
    } else if (s.blssEnabled) {
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
                           "none - the game will be built with RANDOM weights");
        ImGui::TextDisabled(
            "    The build does not fail, it just reconstructs from an untrained net and "
            "says so in the boot log. Train one on the Train tab.");
    } else {
        ImGui::TextDisabled("none");
    }
    ImGui::Spacing();
    drawBlssCorpusChoice();
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
            "A net fitted here scored -0.40 dB on a real project - worse than leaving the\n"
            "upscaler off. Switch the corpus in the header before training one to ship.");
    ImGui::Spacing();

    ImGui::BeginDisabled(blssJob_.running());
    // The action first, because the defaults below it are the ones every
    // number in the docs was measured at - the common case is to press this.
    // Named "Train the network" and not "Train": the TAB is called Train, and
    // two items with one name in one window is a target a UI script (and a
    // person tabbing through) cannot tell apart - the first scripted run of
    // this window pressed the tab and reported success.
    if (ImGui::Button("Train the network", ImVec2(scaled(190.0f), 0))) {
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
    ImGui::SameLine();
    if (ImGui::Button("Restore defaults", ImVec2(scaled(150.0f), 0))) {
        blssTrainFrames_ = 156;
        blssTrainEpochs_ = 400;
        blssTrainSeed_ = 0xB1557u;
        blssTrainDecay_ = 1e-4f;
        blssTrainFill_ = blss::kFillWeight;
        blssTrainFlicker_ = blss::kFlickerWeight;
        blssTrainAllShots_ = true;
        blssTrainStandardise_ = false;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(the defaults are the swept-and-chosen configuration)");
    ImGui::Spacing();

    ImGui::SetNextItemWidth(scaled(160));
    ImGui::InputText("Write to", blssNetName_, sizeof(blssNetName_));
    prefHelp(
        "Relative to the project directory. 'blss.net' is the one the build\n"
        "bakes in; any other name is a candidate you can evaluate without\n"
        "replacing what the game ships.");

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
        "the per-seed fold MEAN is 0.01 dB, against 0.40 dB from fold to fold.\n"
        "The shipped default is B1557.");
    ImGui::Checkbox("Fit every shot (--all-shots)", &blssTrainAllShots_);
    prefHelp(
        "ON, and on a project corpus it should stay on. The console runs the\n"
        "frames the net was fitted to, so fitting all of them is the point -\n"
        "measured on examples/procedural, that net scores +0.06 dB against an\n"
        "oracle ceiling of +0.77, while a net trained on the bestiary scores\n"
        "-0.40 on the same frames.\n"
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
    ImGui::SetNextItemWidth(scaled(160));
    ImGui::Text("Sharpen k: %.2f (from Project settings)", project_.settings.blssSharpen);
    prefHelp(
        "The oracle charges for the two sharpen passes at the project's own\n"
        "k, so the labels depend on it. Change it on the Project settings\n"
        "tab; training follows it rather than offering a second value that\n"
        "could disagree with what the game ships.");
    ImGui::Text("Render scale: %s (from Project settings)",
                project_.settings.blssScale == 1 ? "1x2" : "2x2");

    ImGui::SeparatorText("Fit");
    ImGui::SetNextItemWidth(scaled(160));
    ImGui::DragInt("Epochs", &blssTrainEpochs_, 1.0f, 20, 4000);
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

    ImGui::EndDisabled();
}

// ---------------------------------------------------------------- evaluate ---

namespace {
// Two-decimal dB with a sign, and green/red by which side of zero it is on.
void marginText(double v) {
    const ImVec4 good(0.45f, 0.85f, 0.45f, 1.0f), bad(1.0f, 0.45f, 0.35f, 1.0f);
    ImGui::TextColored(v >= 0.0 ? good : bad, "%+.2f", v);
}

// Below this many dB the oracle - the best ANY per-tile weighting can do under
// the exact GS composite - is indistinguishable from plain bilinear, and no
// network can beat a bound of zero. Measured: on examples/showcase the oracle
// scores +0.07 dB held-out and +0.00 dB on the rest, at 1.01 and 1.00 passes.
// Soft ground texture, low-poly props, nothing that aliases: a fact about the
// scene, not about the trainer.
constexpr double kNoHeadroomDb = 0.10;
}  // namespace

// WILL THIS SCENE BENEFIT AT ALL - the answer --blss-eval has always contained
// and has always buried in the sixth row of a table. The oracle row IS the
// scene's ceiling, so it answers the question the PSNR columns only imply.
void App::drawBlssVerdict() {
    // The arithmetic is blssui::summarise() - a pure function of the parsed
    // table, so it is checkable from the host-only harness. This function only
    // chooses words and colours.
    const blssui::EvalSummary sum = blssui::summarise(blssEval_);
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
            "here, at %.2f passes. There is nothing to reconstruct: a half-resolution "
            "render of this content, blown up bilinearly, is already as close to the "
            "supersampled truth as the composite can get. That is a fact about the scene - "
            "soft textures, low-poly silhouettes, nothing that aliases - and no amount of "
            "training moves it.",
            ceiling, orcP);
        ImGui::TextDisabled(
            "    What it costs to turn on anyway: the reduced render gives VRAM back, and "
            "the composite\n    takes it away in fill. Above 1.00 passes you are paying "
            "for nothing.");
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
                               "    That is exactly what a bestiary-trained net does on a real "
                               "project (-0.40 dB,\n    measured). Switch the corpus above to "
                               "this project and train again.");
        else
            ImGui::TextColored(warn,
                               "    Check that the net was trained on THIS corpus, at this "
                               "render scale and this\n    sharpen strength - a blss.net records "
                               "none of that, so a mismatch is silent.");
    } else {
        ImGui::TextColored(good, "%+.2f dB over plain bilinear, at %.2f passes.", margin, netP);
        ImGui::TextWrapped(
            "The scene's own ceiling is %+.2f dB at %.2f passes (the oracle), so the network "
            "has captured %.0f%% of what is there to capture. 1.00 passes IS plain bilinear "
            "and 5.00 is every kernel everywhere, so read the two together.",
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
            "scenes have none: on examples/showcase the oracle itself scores +0.00 dB at "
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
    ImGui::Checkbox("Write the comparison images", &blssEvalDump_);
    prefHelp("Ground truth / native / bilinear / point / temporal / sharpen / BLSS /\n"
             "oracle PNGs of the first held-out frame, plus the weight field. The\n"
             "Compare tab shows them.");
    if (blssEvalDump_) ImGui::TextDisabled("    %s", blssDumpDir_.c_str());

    ImGui::Spacing();
    if (ImGui::Button("Run the evaluation", ImVec2(scaled(190.0f), 0))) {
        std::vector<std::string> a{"--blss-eval"};
        for (const std::string& c : blssCommonArgs()) a.push_back(c);
        a.push_back("-i");
        a.push_back(blssNetName_);
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
    ImGui::EndDisabled();

    if (!blssEval_.ok()) {
        ImGui::Spacing();
        ImGui::TextDisabled("No table yet. Run it, or look at the output pane below.");
        return;
    }
    // The verdict FIRST, because it is the question that was asked; the table
    // below is where it comes from.
    drawBlssVerdict();
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

    ImGui::Spacing();
    {
        // The shot count is only known once the corpus has been built - 13 for
        // the bestiary, six per scene for a project - so with Folds at 0 this
        // says what it costs per shot rather than inventing a total.
        const int seeds = std::max(1, blssCvSeeds_);
        if (blssCvFolds_ > 0)
            ImGui::TextDisabled("%d fold-run(s): %d corpus render(s) plus %d training(s).",
                                seeds * blssCvFolds_, seeds, seeds * blssCvFolds_);
        else
            ImGui::TextDisabled(
                "%d corpus render(s), then one training per shot per corpus (13 shots on the "
                "bestiary,\nsix camera moves per scene on a project).",
                seeds);
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
    if (ImGui::BeginTable("blsscv", 3 + seedCols,
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

    const char* modes[] = {"Wipe", "Side by side", "A only", "B only"};
    ImGui::SetNextItemWidth(scaled(160));
    ImGui::Combo("View", &blssImgMode_, modes, 4);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(scaled(160));
    ImGui::SliderFloat("Zoom", &blssZoom_, 0.5f, 4.0f, "%.1fx");
    if (blssImgMode_ == 0) {
        ImGui::SetNextItemWidth(scaled(340));
        ImGui::SliderFloat("Wipe", &blssWipe_, 0.0f, 1.0f, "%.2f");
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
    if (ImGui::Button("Report the input channels", ImVec2(scaled(220.0f), 0))) {
        std::vector<std::string> a{"--blss-eval", "--features"};
        for (const std::string& c : blssCommonArgs()) a.push_back(c);
        a.push_back("--frames");
        a.push_back(std::to_string(blssFeatFrames_));
        a.push_back("--fill-weight");
        a.push_back(numArg(blssTrainFill_));
        blssStart(blssui::Kind::Features, a, 0);
    }
    ImGui::EndDisabled();

    if (!blssFeat_.ok()) {
        ImGui::Spacing();
        ImGui::TextDisabled("No channel report yet. Run it, or look at the output pane below.");
        return;
    }
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
