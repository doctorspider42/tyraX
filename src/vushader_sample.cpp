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
        // NOT every class, and the reason is build time rather than taste.
        //
        // A script is emitted per class AND per half of its pair, so claiming
        // four classes is eight microprograms through vcl. The lit and matcap
        // programs are the two that already keep ~30 of VCL's 31 registers
        // live, and adding a luminance, a divide and three scratch registers
        // to those two is what makes vcl give up: `time out.. failed to normal
        // via processing`. It still emits code - it just stops OPTIMISING, and
        // each timeout is another 45 seconds. Measured on this file: four
        // classes, 5 timeouts and just over two minutes; these two, none.
        //
        // These two cover the terrain, every box and the untextured props -
        // the bulk of any scene. Claim more when you have measured that the
        // programs you are replacing have the room.
        return vu::kColour | vu::kTextured;
    }

    // After lighting and texturing, before the colour is clamped: the last
    // point where the value is still a float and still means "light".
    vu::Slot slot() const override { return vu::Slot::Color; }

    // Built once per BUFFER, in the preamble - vu::splat hoists them there.
    // A loi inside the per-vertex body is something vcl schedules around.
    void prepare(vu::Ctx& c) {
        kWeights_ = vu::constant(c, 0.299F, 0.587F, 0.114F, 0.0F);
        kScale_ = vu::splat(c, kBands / 255.0F);
    }
    vu::Vec kWeights_, kScale_;

    void vertex(vu::Ctx& c) override {
        // Written against the RAW builder and the framework's scratch
        // registers, minting nothing of its own.
        //
        // The value layer is the readable way to write this, and it was also
        // what made the build crawl: every `a * b` mints a register, three
        // vertices are unrolled, and eighteen live temporaries put vcl's
        // allocator into `time out.. failed to normal via processing`. It still
        // emits code when that happens - it just stops optimising, and every
        // timeout is another 45 seconds of build. Scratch registers are the
        // framework's answer, and this is what using them looks like.
        vugen::Vu& b = c.raw();
        const vugen::Val one = b.zero().broadcast(3);  // vf00.w == 1.0, free
        const vugen::Val s0 = c.scratch(0).val();
        const vugen::Val s1 = c.scratch(1).val();
        const vugen::Val s2 = c.scratch(2).val();

        // s0.x = luminance. Band the LIGHT, not the channels: posterising r, g
        // and b apart moves the HUE - a shaded yellow (160,131,25) lands on
        // (127,127,0) and the model goes olive.
        b.mulInto(s0, c.color.val(), kWeights_.val(), vuir::MXYZ);
        b.addInto(s0, s0, vugen::Val{s0.reg, 1}, vuir::MX);
        b.addInto(s0, s0, vugen::Val{s0.reg, 2}, vuir::MX);

        // u = luminance * bands/255, floored at 1 so a black vertex neither
        // divides by zero nor darkens - and a modulation pass (a lightmap's
        // vertex colour is Color(0,0,0,128)) still multiplies out to zero.
        b.mulInto(s0, s0, kScale_.val(), vuir::MX);
        b.maximumInto(s0, s0, one, vuir::MX);

        // scale = (2t + 1) / (2u): the band's CENTRE, not its floor. Flooring
        // only ever moves a value DOWN - by a quarter of the range at four
        // bands - so the whole frame, sky included, came out as a grey haze.
        // The doubling is two adds and the 1 is vf00.w, so this needs no
        // constant, and this program has none to spare.
        b.truncate(s1, s0, vuir::MX);
        b.addInto(s1, s1, s1, vuir::MX);
        b.addInto(s1, s1, one, vuir::MX);
        b.addInto(s2, s0, s0, vuir::MX);
        b.divQ(s1, 0, s2, 0);

        // xyz only: ALPHA IS WHAT THE GS BLENDS WITH, and banding it turns a
        // reflection or a shadow pass into stipple.
        b.mulQInto(c.color.val(), c.color.val(), vuir::MXYZ);
    }

    static constexpr float kBands = 4.0F;
};

}  // namespace

VU_PROGRAM(CellShading);
