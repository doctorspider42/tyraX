// Two colours and a threshold: the whole scene re-inked.
//
// Every other program here MODIFIES the colour it is given; this one REPLACES
// it, so a green terrain under a blue sky comes out sepia, or Game Boy green.
// Nearly the cheapest program in the project and the biggest change to the
// frame, which is a ratio worth knowing about.
#include "vushader.hpp"

namespace {

/** Map luminance onto a two-colour ramp. */
struct Palette : vu::Program {
    const char* name() const override { return "Palette"; }

    unsigned classes() const override {
        // The unlit pair only, and not as a compromise: this scene bakes
        // shading into vertex colours on the EE, so its props are flat-colour,
        // its terrain textured, and the lit classes nearly empty. Claiming them
        // costs three live constant registers on a class already holding ~30 of
        // VCL's 31 - which is exactly where it failed with
        // `failed to convert all uta linear->raw`.
        return vu::kColour | vu::kTextured;
    }

    // OFF at boot: every program here claims the same classes, so only one can
    // be resident and the demo makes that a feature - one button per look.
    bool activeAtBoot() const override { return false; }

    vu::Slot slot() const override { return vu::Slot::Color; }

    // EVERY constant is pre-folded on the HOST, and on this program that is not
    // micro-optimising - it is what keeps vcl's optimiser from giving up.
    // The clip half of this look is the biggest program shape there is
    // (Sutherland-Hodgman, nested loops, a scratch polygon buffer) and it
    // already holds most of VCL's 31 registers live; three constants and two
    // temporaries on top used to push it past the optimiser's time limit
    // thirty-three times in one build. See the note under vertex().
    void prepare(vu::Ctx& c) {
        // Rec.601, ALREADY DIVIDED BY 255. The dot product then lands on the
        // 0..1 blend factor directly instead of on a 0..255 luminance that
        // needs scaling afterwards - one instruction and one live lane less,
        // for a constant the host folds either way.
        kLuma_ = vu::constant(c, 0.299F / 255.0F, 0.587F / 255.0F,
                              0.114F / 255.0F, 0.0F);
        // The dark end of the ramp.
        kDark_ = vu::constant(c, 40.0F, 30.0F, 70.0F, 0.0F);
        // The SPAN, light - dark, not the light end: the body wants the
        // difference, and subtracting two constants per vertex to get a third
        // constant is work the host can do once. Warm against the cold dark -
        // a ramp between two colours of the same temperature just looks like a
        // faded photo.
        kSpan_ = vu::constant(c, 255.0F - 40.0F, 220.0F - 30.0F,
                              130.0F - 70.0F, 0.0F);
    }
    vu::Vec kLuma_, kDark_, kSpan_;

    void vertex(vu::Ctx& c) override {
        vugen::Vu& b = c.raw();
        const vugen::Val s0 = c.scratch(0).val();

        // s0.x = the blend factor, straight out of the weighted sum.
        b.mulInto(s0, c.color.val(), kLuma_.val(), vuir::MXYZ);
        b.addInto(s0, s0, vugen::Val{s0.reg, 1}, vuir::MX);
        b.addInto(s0, s0, vugen::Val{s0.reg, 2}, vuir::MX);

        // colour = dark + span * t, written STRAIGHT INTO the colour register.
        // The obvious version builds it in a second scratch and copies; there
        // is nothing to copy, because the colour is dead the moment its
        // luminance has been taken. One fewer value live across the clipper's
        // loops, which is the register that mattered.
        //
        // No clamp needed: t comes from a luminance the GS already clamped to
        // 0..255, so the result cannot leave the segment between the two ends.
        // xyz only - a re-inked alpha turns every blended pass into stipple.
        b.mulInto(c.color.val(), kSpan_.val(), vugen::Val{s0.reg, 0},
                  vuir::MXYZ);
        b.addInto(c.color.val(), c.color.val(), kDark_.val(), vuir::MXYZ);
    }
};

}  // namespace

VU_PROGRAM(Palette);
