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
        // Straight in the GS's own 0..255 scale, and that is not laziness: a
        // trip through 0..1 and back costs two more constants, and the lit
        // program has no room for them - the directional-light class already
        // keeps ~30 of VCL's 31 registers live, and going over does not
        // degrade, it fails as `no opt table` out of vcl (measured, on this
        // very file).
        //
        // Lift the floor first. Without it the darkest band is pure black and
        // the result reads as "unlit" rather than "shaded", the same reason a
        // toon ramp rarely starts at zero.
        vu::Vec lit = c.color + vu::splat(c, 46.0F);  // 0.18 of 255

        // Four bands. floor(lit * 4/255) * 255/4 - one truncate, two constants,
        // no branch and no table.
        vu::Vec steps = vu::splat(c, kBands / 255.0F);
        vu::Vec back = vu::splat(c, 255.0F / kBands);
        vu::Vec q = lit * steps;
        c.raw().truncate(q.val(), q.val(), vuir::MALL);
        c.color = q * back;
    }

    static constexpr float kBands = 4.0F;
};

}  // namespace

VU_PROGRAM(CellShading);
