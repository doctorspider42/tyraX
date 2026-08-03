// A travelling wave through the whole scene - water, jelly, a world that
// breathes.
//
// This is the program that shows the two things a colour effect cannot: that a
// project's own code can MOVE geometry, and that it gets a CLOCK, so the scene
// keeps moving with the EE doing nothing at all. Every vertex in the frame is
// displaced by VU1 while the CPU is free.
#include "vushader.hpp"

namespace {

/** Displace along a sine of position and time. */
struct Wobble : vu::Program {
    const char* name() const override { return "Wobble"; }

    unsigned classes() const override {
        // The unlit pair only, and that is not a compromise in THIS scene:
        // it bakes shading into the vertex colours on the EE, so its props are
        // flat-colour and its terrain is textured, and the lit classes are
        // nearly empty. Claiming them costs two more microprograms per class
        // through vcl and, for the palette, three live constant registers on
        // the class that already holds ~30 of VCL's 31 - which is exactly
        // where it failed with `failed to convert all uta linear->raw`.
        return vu::kColour | vu::kTextured;
    }

    // ObjectSpace: before the MVP multiply, which is the only place a
    // displacement is in the mesh's own units and therefore means the same
    // thing whatever the camera is doing.
    vu::Slot slot() const override { return vu::Slot::ObjectSpace; }

    // OFF at boot. Every program here claims the same material classes, so
    // only one can be resident at a time - the second override of a class
    // simply replaces the first. The demo makes that a feature: one button per
    // look, and micro memory shows exactly one of them.
    bool activeAtBoot() const override { return false; }

    // AND THIS IS THE DECLARATION THAT MATTERS. The EE clipper cuts a mesh
    // against the frustum before this program runs, so a vertex moved here is
    // moved past a cut computed without it: without the flag, every prop
    // touching the edge of the screen tears into blobs. The game submits its
    // props whole instead. The outline pass learned this the expensive way -
    // see the shell-pass note in docs/vu-authoring.md.
    bool movesGeometry() const override { return true; }

    void prepare(vu::Ctx& c) {
        // What sineApprox demands, exactly: (1/2pi, 0.5, 2^23, 0.225). The
        // 2^23 is the add/sub trick that floors a float without a branch, and
        // the 0.225 is the correction term that takes a 5.6% parabola down to
        // 0.2%.
        kSine_ = vu::constant(c, 0.15915494F, 0.5F, 8388608.0F, 0.225F);
        kOne_ = vu::splat(c, 1.0F);
        // x = how tight the wave is in world units, y = how fast it travels,
        // z = how far it pushes. Metres, roughly: a 2-unit wavelength moving
        // at a bit over one radian a second, a fifth of a unit deep. TIGHTNESS is the
        // one to be careful with: a wavelength shorter than a mesh moves that
        // mesh's own vertices by different amounts and TEARS IT OPEN - 0.5 shredded
        // every sphere in the scene. Long waves lift whole objects, which is
        // what water does.
        kWave_ = vu::constant(c, 0.12F, 1.3F, 0.18F, 0.0F);
    }
    vu::Vec kSine_, kOne_, kWave_;

    void vertex(vu::Ctx& c) override {
        vugen::Vu& b = c.raw();
        // FOUR is all there are (vu::Ctx::kScratchCount). The first draft
        // asked for a fifth and got a register with no name in it: the
        // instruction was emitted, the program built, the simulator passed,
        // and the wave simply never happened. So s0 carries the angle and then
        // the result, and the three sineApprox needs are 1, 2 and 3 - which
        // means s1 has to be finished with before the call, not after.
        const vugen::Val s0 = c.scratch(0).val();
        const vugen::Val s1 = c.scratch(1).val();
        const vugen::Val scratch[3] = {c.scratch(1).val(), c.scratch(2).val(),
                                       c.scratch(3).val()};

        const vugen::Val tight = vugen::Val{kWave_.val().reg, 0};
        const vugen::Val speed = vugen::Val{kWave_.val().reg, 1};
        const vugen::Val depth = vugen::Val{kWave_.val().reg, 2};
        const vugen::Val halfTurn = vugen::Val{kSine_.val().reg, 1};

        // angle = (x + z) * tightness + seconds * speed. Summing x and z makes
        // the crests run diagonally, which is what stops a wave over a square
        // terrain from looking like a row of blinds.
        b.mulInto(s0, c.position.val(), tight, vuir::MX);
        b.mulInto(s1, c.position.val(), tight, vuir::MZ);
        b.addInto(s0, s0, vugen::Val{s1.reg, 2}, vuir::MX);
        b.mulInto(s1, c.time.val(), speed, vuir::MX);
        b.addInto(s0, s0, vugen::Val{s1.reg, 0}, vuir::MX);

        // Seventeen instructions, and it costs the same on one field as on
        // three - so ask for one. Phase 0.5 is a sine; 0.75 would be a cosine,
        // and adding pi/2 by hand would NOT be, because pi/2 is not exactly a
        // quarter turn in float.
        b.sineApprox(s0, s0, vuir::MX, kSine_.val(), kOne_.val(), scratch,
                     halfTurn);

        // Straight up. Along the normal would look better on a sphere and
        // wrong on the terrain, whose normals all point up anyway - and the
        // flat-colour class has no normal to read.
        b.mulInto(s0, s0, depth, vuir::MX);
        b.addInto(c.position.val(), c.position.val(), vugen::Val{s0.reg, 0},
                  vuir::MY);
    }
};

}  // namespace

VU_PROGRAM(Wobble);
