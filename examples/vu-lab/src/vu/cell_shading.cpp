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
        // Band the LIGHT, not the channels.
        //
        // Posterising r, g and b independently is the obvious thing and it is
        // wrong: at four levels a shaded yellow (160,131,25) lands on
        // (127,127,0), so the hue slides to olive and the model looks dirty
        // rather than stylised. Cell shading quantises BRIGHTNESS and keeps the
        // colour, so: luminance, band it, scale the original colour by the
        // ratio. That ratio is also why this MULTIPLIES - an object's extra
        // bags are modulation passes whose vertex colour is black, and zero
        // times anything is still zero.
        //
        // Two constants, and not one more: this program has to fit next to the
        // directional-light code, which already keeps ~30 of VCL's 31 registers
        // live. The version with four died as `no opt table` inside Docker.
        vu::Vec u = vu::dot3(c.color, vu::constant(c, 0.299F, 0.587F, 0.114F,
                                                   0.0F)) *
                    vu::splat(c, kBands / 255.0F);

        // vf00.w is 1.0 - a free operand, no constant and no loi. It doubles as
        // the divide-by-zero guard and as the floor of the darkest band, so a
        // black vertex comes out unchanged instead of black-on-black.
        u = vu::maximum(u, vu::Vec(&c.raw(), c.raw().zero().broadcast(3)));

        // scale = the band's CENTRE over the actual value, not its floor.
        //
        // Flooring is the obvious quantiser and it is the reason the first
        // console run came out washed out: floor() only ever moves a value
        // DOWN, by up to a quarter of the range at four bands, so every
        // surface in the scene - sky included - lost brightness and the whole
        // frame read as a grey haze. Centring the band keeps the average.
        //
        // (2t + 1) / (2u), which is (t + 0.5) / u without needing a 0.5: the
        // doubling is two adds and the 1 comes from vf00.w, so this costs no
        // constant at all - and constants are what this program has none of.
        vugen::Val t = c.scratch(0).val();
        c.raw().truncate(t, u.val(), vuir::MALL);
        c.raw().addInto(t, t, t, vuir::MALL);
        c.raw().addInto(t, t, c.raw().zero().broadcast(3), vuir::MALL);
        vugen::Val u2 = c.scratch(1).val();
        c.raw().addInto(u2, u.val(), u.val(), vuir::MALL);
        c.raw().divQ(t, 0, u2, 0);
        // .rgb(), not the whole register: ALPHA IS WHAT THE GS BLENDS WITH.
        c.color.rgb() =
            vu::Vec(&c.raw(), c.raw().mulQ(c.color.val(), vuir::MXYZ));
    }

    static constexpr float kBands = 4.0F;
};

}  // namespace

VU_PROGRAM(CellShading);
