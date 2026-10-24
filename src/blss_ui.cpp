// The reduced-raster upscaler (BLSS): its settings block in Project >
// Preferences, the live mirror of the build interlock and the VRAM line.
//
// App:: methods declared in app.hpp, own TU (the credits_ui.cpp precedent).
// The clash warning is drawn from HERE, so there is one mirror of
// templates.cpp's blssClashes() and one set of tooltips.

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include <imgui.h>

#include "app.hpp"
#include "app_internal.hpp"
#include "toolkit.hpp"

namespace {

// Strips the leading run of spaces from every line, so a line drawn indented
// under a control reads flush inside a tooltip.
std::string unindent(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    bool atLineStart = true;
    for (size_t i = 0; i < text.size(); ++i) {
        if (atLineStart && text[i] == ' ') continue;  // eat the leading run
        atLineStart = text[i] == '\n';
        out.push_back(text[i]);
    }
    return out;
}

}  // namespace

// --- the build interlock, mirrored live ---------------------------------------

App::BlssClash App::blssClashesFor(const ProjectSettings& staged,
                                   bool assumeProjectDefaultOn) const {
    // THESE ARE THE BUILD'S OWN CONDITIONS. blssClashes() in src/templates.cpp
    // emits a #error into src/gen/blss_interlock.gen.cpp for each of them, so the build
    // REFUSES the combination - this is the early warning that stops someone
    // reaching a refused build. The two must answer alike: quieter than the
    // build lets a user walk into a wall of #error, louder cries wolf on a
    // project that would have built. Mirrored rather than shared because
    // blssClashes() reads a saved Project and this has to answer for the STAGED
    // settings while the Preferences modal is open; if you edit one, edit the
    // other.
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
        // PER SCENE, exactly like blssClashes(): a scene that resolves the upscaler OFF cannot clash with
        // anything, and reporting it would send someone hunting for a portal
        // that is doing no harm. Resolved against the STAGED project default,
        // which is what project::resolvedSettings would use if the modal's
        // edits were already committed.
        //
        // ...EXCEPT WHEN THE QUESTION IS "WHAT IF I TURNED IT ON", which is the
        // one the reader of a switched-off project has. `assumeProjectDefaultOn`
        // walks the scenes that INHERIT the default as though it were on, and
        // that is not a second interlock: with it false this answers the
        // build's question verbatim, and the caller passes it exactly when it
        // is about to label the block informational. Without it the whole
        // informational branch was unreachable - a project with the feature off
        // has no scene resolving on, so blssClashesFor() came back empty and a
        // person deciding whether to switch it on was shown nothing at all,
        // which is the state that block was written to fix. A scene that
        // EXPLICITLY overrides to off is still skipped: turning the project
        // default on does not turn that scene on, so it genuinely cannot clash.
        const bool on = sc.overrides.upscaler ? sc.settings.blssEnabled
                                              : (assumeProjectDefaultOn || staged.blssEnabled);
        if (!on) continue;
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
        // Frame extrapolation - a project preference, so it is a property of the
        // SCENE row rather than of any object in it, like depth of field above.
        // Staged, because the control now sits in this very block and a person
        // ticking it must see the consequence before the modal commits.
        if (staged.frameExtrapolation) {
            BlssClashRef r = ref(-1, "");
            r.label += "  (frame extrapolation is on for the project)";
            c.extrapolation.push_back(r);
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
    // The block-in-place note under the switch: one sentence naming the
    // causes, the long reasons on hover, then one row per cause with its jump.
    std::string causes;
    auto add = [&](bool on, const char* what) {
        if (!on) return;
        if (!causes.empty()) causes += ", ";
        causes += what;
    };
    add(!c.dof.empty(), "depth of field");
    add(!c.dofNode.empty(), "a Set Depth Of Field node");
    add(!c.portals.empty(), "portals");
    add(!c.split.empty(), "split-screen");
    add(!c.extrapolation.empty(), "frame extrapolation");
    const std::string text =
        informational ? "Can't be turned on in this project yet - the build would refuse it: " +
                            causes + " need display-resolution rendering."
                      : "The build will refuse this project: the upscaler is on together with " +
                            causes + ". Turn one of the two off.";
    ImGui::PushID("##blss_clash_note");
    ui::Note(2, text.c_str());
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "Depth of field composites its blur through the GS depth test at\n"
            "display resolution; with the upscaler the depth buffer is only as\n"
            "big as the reduced render. Portals want the same display-resolution\n"
            "depth. Split-screen frames are never reduced. Frame extrapolation\n"
            "reprojects the previous frame and tears the picture with it.");
    int row = 0;
    auto rows = [&](const std::vector<BlssClashRef>& refs) {
        for (const BlssClashRef& r : refs) {
            ImGui::PushID(row++);
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", r.label.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton(r.object >= 0 ? "Select it" : "Go to the scene")) blssSelectClash(r);
            ImGui::PopID();
        }
    };
    rows(c.dof);
    rows(c.dofNode);
    rows(c.portals);
    rows(c.split);
    rows(c.extrapolation);
    ImGui::PopID();
}

// --- VRAM -----------------------------------------------------------------------

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
// the low-res target costs precisely what the z buffer saves -.
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
    if (s.blssParticles) {
        char parts[320];
        std::snprintf(
            parts, sizeof(parts),
            "    Particles only %s costs %d KB of GS VRAM on this project's %dx%d output: a\n"
            "    %dx%d colour target and a z target of the same size; the full-size\n"
            "    z-buffer stays, because the opaque scene keeps the full raster.",
            sx == 1 ? "1x2" : "2x2", 2 * target * 4 / 1024, outW, outH, lowW, lowH);
        return parts;
    }
    if (s.blssAdaptive) {
        char adaptive[320];
        std::snprintf(
            adaptive, sizeof(adaptive),
            "    Adaptive %s costs %d KB of GS VRAM on this project's %dx%d output: "
            "the full-size z-buffer\n    remains resident so native/reduced switches "
            "are allocation-free, and the %dx%d colour target is additional.",
            sx == 1 ? "1x2" : "2x2", target * 4 / 1024, outW, outH, lowW, lowH);
        return adaptive;
    }
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

// --- the settings block --------------------------------------------------------

bool App::drawBlssSettings(ProjectSettings& s) {
    bool changed = false;

    // THE ONE CLASH THE EDITOR CAN MAKE UNREACHABLE: BLSS x frame extrapolation
    // is setting against SETTING, and both settings are in this block. The other
    // four clashes are setting against scene CONTENT - you cannot grey out a
    // portal somebody placed - so they keep the live warning and the build
    // refusal.
    //
    // MUTUALLY EXCLUSIVE, SYMMETRICALLY: whichever of the two is already on
    // greys out the other. The upscaler is per SCENE and extrapolation is per
    // project, so the question is "does any scene RESOLVE the upscaler on",
    // asked of the STAGED defaults so a tick in this very dialog moves it.
    //
    // ONLY THE TICK IS EVER BLOCKED, NEVER THE UNTICK: a project can arrive with
    // both on (a hand-edited .tyra, an older editor, a Set Frame Extrapolation
    // node) and every switch that is on stays switchable off. With every scene
    // overriding the upscaler off, the project default can be ON while nothing
    // resolves on - greying it by "does anything upscale" would lock a ticked
    // box nobody could untick.
    bool upscalerOn = false;
    std::string upscalerWhere = "this project";
    if (hasProject_)
        for (const SceneData& sc : project_.scenes) {
            const bool on =
                sc.overrides.upscaler ? sc.settings.blssEnabled : s.blssEnabled;
            if (!on) continue;
            upscalerOn = true;
            if (!s.blssEnabled) upscalerWhere = "the scene " + sc.name;
            break;
        }
    const bool blockUpscaler =
        s.frameExtrapolation && !s.blssEnabled && !s.blssParticles;
    const bool blockExtrapolation =
        upscalerOn && !s.frameExtrapolation && !s.blssParticles;

    ImGui::BeginDisabled(blockUpscaler);
    if (ui::Switch("Use the upscaler (render the 3D scene smaller and reconstruct it - pays off on fill-heavy scenes)",
                   &s.blssEnabled))
        changed = true;
    ImGui::EndDisabled();
    prefHelp(
        "Renders the 3D SCENE smaller and blows it back up to fill the display\n"
        "buffer with one bilinear pass. The HUD, the menus, the text and every\n"
        "post effect still draw at FULL resolution, so 2D stays crisp.\n"
        "\n"
        "It trades GS fill for a little EE work and one composite pass, so it\n"
        "is a win on a frame with real overdraw and a loss on one without. It\n"
        "also hands back GS VRAM, because the z-buffer follows the smaller\n"
        "raster.\n"
        "\n"
        "Depth of field, portals and split-screen cannot be combined with it and\n"
        "the BUILD REFUSES the pair, naming the scene. Anything this project\n"
        "already uses is listed below, before you turn it on.\n"
        "\n"
        "This is the project DEFAULT; a scene can override it in Scene > Scene\n"
        "Preferences > Upscaler (BLSS).");
    // IN-LINE, not in the tooltip: a greyed control that explains itself only on
    // hover reads as a bug.
    if (blockUpscaler)
        ImGui::TextDisabled(
            "    Not while frame extrapolation is on (below). The two cannot be\n"
            "    combined - both rebuild a frame by reprojecting the previous one,\n"
            "    and in motion the pair tears the picture into cells that\n"
            "    disagree. Untick frame extrapolation and this frees up.");
    // The clash note sits directly under the switch it is about. ALWAYS, not
    // only when the feature is on: the person who most needs to know that this
    // project uses depth of field is the one deciding whether to tick the box.
    // With nothing on, it asks the hypothetical - "what would refuse the build
    // if you ticked the box above".
    if (!s.blssParticles) {
        BlssClash clash = blssClashesFor(s, /*assumeProjectDefaultOn=*/!upscalerOn);
        // The extrapolation row is noise when the tick it warns about has
        // already been taken away (the greyed switch says so above).
        if (blockUpscaler) clash.extrapolation.clear();
        drawBlssClashWarning(clash, !upscalerOn);
    }

    ImGui::BeginDisabled(!s.blssEnabled);
    {
        int what = s.blssParticles ? 1 : 0;
        const char* whatNames[] = {"Whole 3D scene", "Particles only (smoke, haze, fire, rain)"};
        if (ui::Combo("Draw reduced", &what, whatNames, 2)) {
            s.blssParticles = what == 1;
            changed = true;
        }
        prefHelp(
            "WHOLE 3D SCENE: everything 3D renders at the reduced raster. The\n"
            "z-buffer shrinks with it, so depth of field, portals, split screen\n"
            "and frame extrapolation cannot be combined with it.\n"
            "\n"
            "PARTICLES ONLY: the opaque scene keeps the full resolution and only\n"
            "the particle emitters draw reduced - the layer whose overdraw costs\n"
            "most in a smoke, haze or rain scene. Edges and textures stay sharp;\n"
            "whatever sits under a particle is seen at the reduced resolution.\n"
            "Every other feature keeps working. Costs a reduced colour and z\n"
            "target in GS VRAM; the full-size z-buffer stays.");

        if (ui::Switch("Adapt resolution to frame budget", &s.blssAdaptive)) changed = true;
        prefHelp(
            "Starts each scene at native 3D resolution. After four sustained\n"
            "missed display fields it switches to the selected reduced raster;\n"
            "after three seconds of full-rate headroom it briefly probes native\n"
            "again. HUD, menus, text and post effects always stay native.\n"
            "\n"
            "Both raster layouts are reserved at boot, so switching moves no GS\n"
            "memory and evicts no textures - but the z-buffer stays full-size and\n"
            "the low-resolution colour target is additional overhead.");

        int scale = s.blssScale == 1 ? 1 : 0;
        // Deliberately no resolution in these labels: the tooltip states this
        // project's real numbers, and a fixed example would contradict them.
        const char* names[] = {
            "2x2 - quarter the pixels",
            "1x2 - half height only (keeps horizontal detail)"};
        if (ui::Combo("Render scale", &scale, names, 2)) {
            s.blssScale = scale;
            changed = true;
        }
        // THE NUMBER FOR THIS PROJECT rides in the tooltip, computed live.
        prefHelp((std::string(
                      "How much of the frame the GS actually rasterises. 2x2 quarters\n"
                      "the 3D fill; 1x2 halves only the height, which is what the PS2's\n"
                      "own interlaced-field mode already does to the raster - softer\n"
                      "vertically, untouched horizontally.\n"
                      "The z-buffer is allocated at THIS size, which is where the VRAM\n"
                      "saving comes from.\n\n") +
                  unindent(blssVramLine(s)))
                     .c_str());
    }
    ImGui::EndDisabled();

    // Per-scene note: the switch above is the project DEFAULT. Said here
    // because a reader looking at "off" while one scene has it on would
    // otherwise be reading a lie.
    if (hasProject_) {
        int overriding = 0;
        for (const SceneData& sc : project_.scenes) overriding += sc.overrides.upscaler ? 1 : 0;
        if (overriding > 0)
            ImGui::TextDisabled(
                "    %d of this project's %d scenes answer this themselves, in\n"
                "    Scene > Scene Preferences > Upscaler (BLSS), and ignore what\n"
                "    is set here.",
                overriding, (int)project_.scenes.size());
        // WHAT MIXING COSTS, SAID ONCE, WHERE IT IS BEING DONE. A project whose
        // scenes disagree pins the z buffer at the full display raster for the
        // whole run, so the low-res target stays resident through the native
        // scenes as pure overhead. Asked of the STAGED defaults.
        const project::BlssUse use = project::blssUse(project_, s);
        if (use.mixed)
            ImGui::TextColored(
                ImVec4(1.0f, 0.75f, 0.35f, 1.0f),
                "    THIS PROJECT MIXES: some scenes upscale and some render at\n"
                "    the full raster, so the z-buffer is pinned at the display\n"
                "    size for the whole run and the memory saving is given up.\n"
                "    Measured free texture heap at 512x512: 0.375 MB native,\n"
                "    0.875 MB upscaled throughout, 0.125 MB mixed - i.e. 0.25 MB\n"
                "    WORSE than never turning it on. Nothing else about mixing\n"
                "    costs anything: zero evictions over ~1200 scene switches,\n"
                "    and a native scene renders pixel-identical.");
    }

    // FRAME EXTRAPOLATION LIVES HERE, next to the upscaler, because it is the
    // same question asked a different way: how does a frame reach the screen,
    // and how much of it may be reconstructed rather than rendered. It used to
    // sit four sections away under "Build", beside triple buffering, where the
    // person choosing a reconstruction never met it - and the two turn out to be
    // mutually exclusive, which is a thing you can only be told at the point of
    // choice. It is a PROJECT setting where the upscaler is per scene, which is
    // why the exclusion at the top of this function has to ask every scene.
    ImGui::SeparatorText("Frame extrapolation");
    ImGui::BeginDisabled(blockExtrapolation);
    if (ui::Switch("Present a synthesised frame between rendered ones",
                        &s.frameExtrapolation))
        changed = true;
    ImGui::EndDisabled();
    // The exclusion, ON the greyed checkbox, so hovering the thing that will not
    // tick says why. It NAMES where the upscaler is on, because "turn the
    // upscaler off" is not actionable when the answer is one scene overriding
    // the project and this dialog is not where that lives. It was five lines of
    // body text under the checkbox; a disabled control that explains itself on
    // hover is the same information without the wall.
    if (blockExtrapolation) {
        char clash[512];
        std::snprintf(clash, sizeof(clash),
                      "NOT AVAILABLE while the upscaler is on for %s.\n"
                      "\n"
                      "The two cannot be combined - both rebuild a frame by\n"
                      "reprojecting the previous one, and in motion the pair tears the\n"
                      "picture into cells that disagree. Untick the upscaler (above, or\n"
                      "in Scene > Scene Preferences for that scene) and this frees up.",
                      upscalerWhere.c_str());
        prefHelp(clash);
    }
    prefHelp(
        "Frame generation, PS2 style: after each rendered frame the game\n"
        "presents a SYNTHESISED one, re-drawing it under the camera\n"
        "extrapolated from its own motion. The world then simulates and renders\n"
        "at HALF the field rate while the television still gets a fresh picture\n"
        "every field. It costs no GS VRAM (the source is the display buffer that\n"
        "already exists) and adds no latency.\n"
        "\n"
        "CANNOT BE COMBINED WITH THE UPSCALER ABOVE, and the BUILD REFUSES the\n"
        "pair. Both rebuild a frame by reprojecting the previous one, and this\n"
        "one halves the world rate - so the camera moves twice as far between\n"
        "two rendered frames, which is the interval the upscaler's temporal pass\n"
        "reprojects across. Measured in motion, the pair tears the picture into\n"
        "cells that disagree; either feature alone is clean. Parked, both look\n"
        "right, which is why this has to be said here rather than found later.");
    ImGui::BeginDisabled(!s.frameExtrapolation);
    // ONE CONTROL, TWO STATES. This used to be a checkbox followed by a
    // "Translation plane" drag whose own zero read "rotation only" - so the
    // pair had three states, two of which (ticked, and unticked-at-zero) are
    // the only ones worth shipping and the third (unticked at some positive
    // distance) is a knob for guessing at a number the ground plane derives
    // properly. The drag is gone and the distance is pinned to zero: ticked =
    // the ground plane, unticked = rotation only.
    if (ui::Switch("Ground plane (recommended)", &s.frameExtrapolationGround)) {
        // Normalised on the first touch of this box, NOT on load: a project
        // saved with a positive distance keeps rendering exactly as it did
        // until someone changes this setting, because silently re-rendering
        // somebody's shipped project on open is worse than an unreachable
        // value. The field stays in the format and still reaches the game
        // (project.cpp, templates.cpp) - it is only unreachable from the UI,
        // which docs/frame-extrapolation.md says out loud.
        s.frameExtrapolationPlane = 0.0f;
        changed = true;
    }
    prefHelp(
        "Take the depth from the FLOOR instead of a fixed distance. A view ray\n"
        "meets the ground at w = h / -dir.y, so depth grows toward the horizon by\n"
        "itself and a ray at or above the horizon never meets it - the sky then\n"
        "does not move at all, which is the worst artefact of a fixed plane.\n"
        "Costs a few operations per grid corner and needs no depth buffer.\n"
        "\n"
        "Unticked, camera translation is not reprojected at all (rotation only).\n"
        "That is exact, but a walk in a straight line then reproduces the\n"
        "previous frame EXACTLY - the synthesised frame is a duplicate, which\n"
        "reads as judder. Which is why this is the recommended state.");
    if (ui::Switch("Always synthesise (ignore the gate)",
                        &s.frameExtrapolationForce))
        changed = true;
    prefHelp(
        "Skip the per-frame decision and always present a synthesised frame.\n"
        "\n"
        "The gate measures EE work, so a scene held back by the GS rather than\n"
        "the EE keeps it shut - examples/raytraced-mirror sits at 26 fps with the\n"
        "gate never opening. This is how such a scene gets tested at all without\n"
        "placing a Set Frame Extrapolation node. Expect it to cost frames where\n"
        "the gate would have declined.");
    ImGui::EndDisabled();
    return changed;
}
