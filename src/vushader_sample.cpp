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
        // Everything with a texture or a light. kColour is missing because
        // cell_outline.cpp owns it - two programs cannot claim one class, the
        // second override simply replaces the first - and that file posterises
        // with these same numbers, so the look does not change at the seam.
        //
        // Matcap stays out: posterising a reflection quantises the sky it is
        // sampling, which reads as a bug rather than a style.
        return vu::kTextured | vu::kLit | vu::kLitTextured;
    }

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
        // y = levels/255, z = 255/levels, w = half a step.
        kPost_ = vu::constant(c, 0.0F, kLevels / 255.0F, 255.0F / kLevels,
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
        const vugen::Val down = vugen::Val{kPost_.val().reg, 1};
        const vugen::Val up = vugen::Val{kPost_.val().reg, 2};
        const vugen::Val halfStep = vugen::Val{kPost_.val().reg, 3};

        b.mulInto(s0, c.color.val(), down, vuir::MXYZ);  // into level units
        b.truncate(s0, s0, vuir::MXYZ);                  // drop the remainder
        b.addInto(s0, s0, halfStep, vuir::MXYZ);         // to the band CENTRE
        b.mulInto(c.color.val(), s0, up, vuir::MXYZ);    // back to 0..255

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
