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
        // Three band edges in the GS's own 0..255 scale, the steepness that
        // turns each into a hard step, a 1.0 to clamp against, and the two
        // numbers that map three steps onto a 0.55..1.0 brightness ramp.
        kThresh_ = vu::constant(c, 40.0F, 90.0F, 150.0F, 0.0F);
        kSharp_ = vu::splat(c, 0.25F);
        kOne_ = vu::splat(c, 1.0F);
        kStep_ = vu::splat(c, 0.15F);
        kFloor_ = vu::splat(c, 0.55F);
    }
    vu::Vec kWeights_, kThresh_, kSharp_, kOne_, kStep_, kFloor_;

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

        // s0.x = luminance. Band the LIGHT, not the channels: posterising r,
        // g and b apart moves the HUE - a shaded yellow (160,131,25) lands on
        // (127,127,0) and the model goes olive.
        b.mulInto(s0, c.color.val(), kWeights_.val(), vuir::MXYZ);
        b.addInto(s0, s0, vugen::Val{s0.reg, 1}, vuir::MX);
        b.addInto(s0, s0, vugen::Val{s0.reg, 2}, vuir::MX);

        // A step ramp, built from min/max - NO DIVIDE.
        //
        // The obvious formulation is scale = band / luminance, and it is a trap:
        // a divide writes Q, Q carries the perspective divide, and a Q write
        // from a script body gets scheduled into that window. On the console
        // that came out as grey stippled patches and shadows fighting for z,
        // while the host simulator - in order, no latency - showed nothing at
        // all. The framework refuses a Q write from a script now.
        //
        // So: three thresholds, each a saturating step, summed into a scale.
        // Every operation is a plain multiply-add, the scale multiplies all
        // three channels equally, and the hue is exactly preserved.
        // Luminance into ALL THREE components first. s0.y and s0.z still hold
        // the unsummed 0.587*g and 0.114*b, so subtracting the thresholds
        // straight from s0 tests the GREEN CHANNEL against t1 and the blue
        // against t2 - two of the three steps then measure the wrong thing,
        // the ramp never reaches its top, and the program reads as "everything
        // got darker" instead of "the light came in bands". vf00.xyz is 0 and
        // the broadcast rides in the second operand, which is the only slot
        // VU1 lets a broadcast sit in.
        b.addInto(s1, b.zero(), vugen::Val{s0.reg, 0}, vuir::MXYZ);
        b.subInto(s1, s1, kThresh_.val(), vuir::MXYZ);   // lum - t0,t1,t2
        b.mulInto(s1, s1, kSharp_.val(), vuir::MXYZ);    // steepen the edges
        b.maximumInto(s1, s1, b.zero(), vuir::MXYZ);
        b.minimumInto(s1, s1, kOne_.val(), vuir::MXYZ);  // 3 steps in 0..1
        b.addInto(s2, s1, vugen::Val{s1.reg, 1}, vuir::MX);
        b.addInto(s2, s2, vugen::Val{s1.reg, 2}, vuir::MX);
        b.mulInto(s2, s2, kStep_.val(), vuir::MX);       // 0..1 in four bands
        b.addInto(s2, s2, kFloor_.val(), vuir::MX);      // lift the darkest

        // xyz only: ALPHA IS WHAT THE GS BLENDS WITH, and banding it turns a
        // reflection or a shadow pass into stipple.
        b.mulInto(c.color.val(), c.color.val(), vugen::Val{s2.reg, 0},
                  vuir::MXYZ);
    }

    static constexpr float kBands = 4.0F;
};

}  // namespace

VU_PROGRAM(CellShading);
