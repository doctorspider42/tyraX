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

    void prepare(vu::Ctx& c) {
        // Rec.601, the same weights every program here uses, so "brightness"
        // means one thing across the project.
        kLuma_ = vu::constant(c, 0.299F, 0.587F, 0.114F, 0.0F);
        // The dark end, with the 1/255 that turns a 0..255 luminance into the
        // 0..1 blend factor packed into its spare w.
        kDark_ = vu::constant(c, 40.0F, 30.0F, 70.0F, 1.0F / 255.0F);
        // Warm against the cold dark: a ramp between two colours of the same
        // temperature just looks like a faded photo.
        kLight_ = vu::constant(c, 255.0F, 220.0F, 130.0F, 0.0F);
    }
    vu::Vec kLuma_, kDark_, kLight_;

    void vertex(vu::Ctx& c) override {
        vugen::Vu& b = c.raw();
        const vugen::Val s0 = c.scratch(0).val();
        const vugen::Val s1 = c.scratch(1).val();
        const vugen::Val inv255 = vugen::Val{kDark_.val().reg, 3};

        // s0.x = luminance, then scaled into 0..1 as the blend factor.
        b.mulInto(s0, c.color.val(), kLuma_.val(), vuir::MXYZ);
        b.addInto(s0, s0, vugen::Val{s0.reg, 1}, vuir::MX);
        b.addInto(s0, s0, vugen::Val{s0.reg, 2}, vuir::MX);
        b.mulInto(s0, s0, inv255, vuir::MX);

        // colour = dark + (light - dark) * t. Whole-register subtract and a
        // broadcast scale, so the ramp is three instructions however many
        // colours are in it. No clamp needed: t comes from a luminance the GS
        // already clamped to 0..255, so the result stays on the segment.
        b.subInto(s1, kLight_.val(), kDark_.val(), vuir::MXYZ);
        b.mulInto(s1, s1, vugen::Val{s0.reg, 0}, vuir::MXYZ);
        // xyz only - a re-inked alpha turns every blended pass into stipple.
        b.addInto(c.color.val(), kDark_.val(), s1, vuir::MXYZ);
    }
};

}  // namespace

VU_PROGRAM(Palette);
