// The canonical example of a project-written VU1 program (src/vushader.hpp).
//
// It lives in the editor so `--vu-check` can build it, simulate it and prove
// the whole path works on the host - no Docker, no console. The same source is
// what a new project gets as its starter script, and what examples/vu-lab
// ships: this file is the thing the documentation points at when it says "a
// script is not a toy layer, it is the framework with the body written by
// someone else".
#include "vushader.hpp"

namespace {

/** Cell shading: quantise the shaded colour into a few flat bands.
 *
 * On a LIT class the colour reaching this point is already the directional
 * lighting result, so banding it is exactly the cartoon look - the light wraps
 * around the model in steps instead of a gradient. On an unlit class there is
 * no lighting to band, so the same code posterises the vertex colour, which is
 * the honest thing for it to do and keeps one program covering both.
 */
struct CellShading : vu::Program {
    const char* name() const override { return "Cell shading"; }

    // Every class the scene might draw. A program made only of VALUES - no
    // per-mesh parameter binding - may claim the lit classes too, because the
    // four per-mesh numbers are what collides with the light colours, not the
    // program (docs/vu-authoring.md, "Which classes a look can claim").
    unsigned classes() const override {
        return vu::kColour | vu::kLit | vu::kTextured | vu::kMatcap;
    }

    // After lighting and texturing, before the colour is clamped: the last
    // point where the value is still a float and still means "light".
    vu::Slot slot() const override { return vu::Slot::Color; }

    void vertex(vu::Ctx& c) override {
        // The GS carries colour as 0..255 with 128 meaning "unmodulated", so
        // the work happens in 0..1 and goes back at the end. Hoisted: both
        // constants are per-vertex otherwise, and a splat is an instruction.
        vu::Vec toUnit = vu::splat(&c.raw(), 1.0F / 255.0F);
        vu::Vec toGs = vu::splat(&c.raw(), 255.0F);

        vu::Vec lit = c.color * toUnit;
        // Lift the floor a little before banding. Without it the darkest band
        // is pure black and a cartoon look reads as "unlit" rather than
        // "shaded" - the same reason a toon ramp rarely starts at zero.
        vu::Vec floorLift = vu::splat(&c.raw(), 0.18F);
        lit = vu::saturate(lit + floorLift);

        // Four bands. This is the whole effect: one call, four instructions,
        // no table and no branch.
        vu::Vec banded = vu::quantize(lit, 4.0F);

        c.color = banded * toGs;
    }
};

}  // namespace

VU_PROGRAM(CellShading);
