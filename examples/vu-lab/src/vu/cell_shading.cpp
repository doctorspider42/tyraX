// The canonical example of a project-written VU1 program (src/vushader.hpp).
//
// It lives in the editor so `--vu-check` can build it, simulate it and prove
// the whole path works on the host - no Docker, no console. The same source is
// what a new project gets as its starter script, and what examples/vu-lab
// ships: this file is the thing the documentation points at when it says "a
// script is not a toy layer, it is the framework with the body written by
// someone else".
//
// This is the LIT-AND-TEXTURED half of the cell look. The flat-colour half,
// which also carries the outline, is cell_outline.cpp - two files because a
// program is emitted per material class and the outline needs a different slot,
// not because they are two effects.
#include "vushader.hpp"

namespace {

/** Cell shading: snap each colour channel to a few flat levels.
 *
 * PER CHANNEL, and that is the whole choice. Quantising r, g and b apart moves
 * the HUE - a shaded green snaps to a *different* green rather than the same
 * green darker - and that hue shift is what reads as flat paint and ink.
 * Banding the LUMINANCE instead preserves hue exactly, which sounds more
 * correct and looks like a dimmer switch, because preserving hue is what
 * shading does and a cel look is trying not to look like shading.
 *
 * That was learned the hard way: an early version of this file computed
 * luminance and banded it, and the version people preferred was an earlier
 * BUG that had been quantising one channel by accident.
 */
struct CellShading : vu::Program {
    const char* name() const override { return "Cell shading"; }

    unsigned classes() const override {
        // Every class that carries a colour anyone looks at. kColour matters
        // most in a scene like this one: with shading baked into the vertex
        // colours on the EE, the flat-colour class is where the light
        // gradients actually are.
        //
        // Matcap stays out: posterising a reflection quantises the sky it is
        // sampling, which reads as a bug rather than a style.
        return vu::kColour | vu::kTextured | vu::kLit | vu::kLitTextured;
    }

    // Ask the game for the outline pass (docs/vu-authoring.md). The growth
    // itself is NOT done here, and that is a correctness decision rather than
    // a performance one: the EE clipper cuts a mesh against the frustum BEFORE
    // any VU program runs, so a vertex grown afterwards is grown past a cut
    // that was computed without it, and the line tears wherever an object
    // meets the edge of the screen. Turning the clip checks off to dodge that
    // only trades it for the raster wrap that raw submission gives anything
    // half off-screen. The clipper has to see the final geometry, so the game
    // grows the shell once, on the EE, when it builds it.
    //
    // What is left for this program is the part that cannot be baked: painting
    // the shell flat. It arrives with 1 in x of the mesh parameters and every
    // other mesh in the frame carries 0, so one multiply serves both.
    bool shellPass() const override { return true; }
    // A FRACTION of the object's own size, not a screen width - baked
    // geometry cannot follow the camera, and an outline proportional to the
    // object is what stops small props from wearing tyres anyway.
    float shellWidth() const override { return 0.06F; }

    // After lighting and texturing, before the colour is clamped: the last
    // point where the value is still a float and still means "light".
    vu::Slot slot() const override { return vu::Slot::Color; }

    // Built once per BUFFER, in the preamble - vu::constant hoists it there.
    // A loi inside the per-vertex body is something vcl schedules around.
    void prepare(vu::Ctx& c) {
        // One register for three numbers, which is the difference between this
        // program fitting the lit classes and not: they already hold ~30 of
        // VCL's 31, and a scalar per register is what produced `time out..
        // failed to normal via processing` - vcl does not fail there, it stops
        // OPTIMISING, at 45 seconds a go.
        // x = 1, y = levels/255, z = 255/levels, w = half a step.
        // The 1.0 is FIRST on purpose: `1 - flag` takes it from the x of the
        // FIRST operand, and VU1 will only broadcast the SECOND one - parked
        // anywhere else it is unreachable there, and the subtract quietly uses
        // a different number with nothing to complain about.
        kPost_ = vu::constant(c, 1.0F, kLevels / 255.0F, 255.0F / kLevels,
                              0.5F);
    }
    vu::Vec kPost_;
    // Levels per channel. Three is poster paint, eight is nearly a gradient.
    // cell_outline.cpp carries the same number.
    static constexpr float kLevels = 4.0F;

    void vertex(vu::Ctx& c) override {
        // Written against the RAW builder and the framework's scratch
        // registers, minting nothing of its own. The value layer is the
        // readable way to write this, and it is also what made the build
        // crawl: every `a * b` mints a register, the vertex loop is unrolled
        // three times, and the live temporaries put vcl's allocator into that
        // timeout. Scratch registers are the framework's answer.
        vugen::Vu& b = c.raw();
        const vugen::Val s0 = c.scratch(0).val();
        const vugen::Val s1 = c.scratch(1).val();
        const vugen::Val down = vugen::Val{kPost_.val().reg, 1};
        const vugen::Val up = vugen::Val{kPost_.val().reg, 2};
        const vugen::Val halfStep = vugen::Val{kPost_.val().reg, 3};

        b.mulInto(s0, c.color.val(), down, vuir::MXYZ);  // into level units
        b.truncate(s0, s0, vuir::MXYZ);                  // drop the remainder
        b.addInto(s0, s0, halfStep, vuir::MXYZ);         // to the band CENTRE
        b.mulInto(s0, s0, up, vuir::MXYZ);               // back to 0..255

        // ...and flat black if this mesh is an outline shell. x of the mesh
        // parameters is 1 there and 0 for everything else in the frame, so
        // this is one subtract and one multiply rather than a branch - and a
        // branch on per-mesh data would cost the dual-issue schedule for every
        // mesh, not only the ones that take it.
        //
        // It cannot be done by handing the shell black VERTICES instead: the
        // posterise above rounds to band CENTRES, so a black vertex would come
        // out at half a step, and the ink line would be dark grey.
        b.subInto(s1, kPost_.val(), vugen::Val{c.params.val().reg, 0},
                  vuir::MX);
        b.mulInto(c.color.val(), s0, vugen::Val{s1.reg, 0}, vuir::MXYZ);

        // Half a step is not decoration. Truncation always rounds DOWN, so
        // without it every level lands at the bottom of its band and the
        // picture loses half a step of brightness everywhere - which reads as
        // "the effect just makes things darker", and did.

        // xyz only: ALPHA IS WHAT THE GS BLENDS WITH, and quantising it turns
        // a reflection or a shadow pass into stipple.
        //
        // And no divide anywhere. A divide writes Q, Q carries the perspective
        // divide, and a Q write from a script body gets scheduled into that
        // window - on the console that came out as grey stippled patches and
        // shadows fighting for z, while the host simulator, in order and
        // without latency, showed nothing at all. The framework refuses a Q
        // write from a script now.
    }
};

}  // namespace

VU_PROGRAM(CellShading);
