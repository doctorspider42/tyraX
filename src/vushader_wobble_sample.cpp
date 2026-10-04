// A travelling wave through the whole scene - water, jelly, a world that
// breathes.
//
// The program that shows the two things a colour effect cannot: a project's own
// code can MOVE geometry, and it gets a CLOCK - so the scene keeps moving with
// the EE doing nothing at all.
#include "vushader.hpp"

namespace {

/** Displace along a sine of position and time. */
struct Wobble : vu::Program {
    const char* name() const override { return "Wobble"; }

    unsigned classes() const override {
        // EVERY class, for the reason vertex_snap.cpp spells out: a
        // DISPLACEMENT may not claim a subset, or an object's passes separate
        // and each shows through the other.
        //
        // Expensive to claim everywhere (sineApprox is seventeen instructions
        // and wants three scratch registers on top of the two the wave needs),
        // so if vcl reports `no opt table` or `failed to convert all uta
        // linear->raw` on the lit or matcap classes, the answer is a cheaper
        // wave, not a narrower claim.
        return vu::kAll;
    }

    // ObjectSpace: before the MVP multiply, the only place a displacement is in
    // the mesh's own units and means the same thing whatever the camera does.
    vu::Slot slot() const override { return vu::Slot::ObjectSpace; }

    // OFF at boot: every program here claims the same classes, so only one can
    // be resident and the demo makes that a feature - one button per look.
    bool activeAtBoot() const override { return false; }

    // THE DECLARATION THAT MATTERS. The EE clipper cuts a mesh before this runs,
    // so a vertex moved here is moved past a cut computed without it: without
    // the flag, every prop touching the screen edge tears into blobs. The game
    // submits its props whole instead.
    bool movesGeometry() const override { return true; }

    void prepare(vu::Ctx& c) {
        // Exactly what sineApprox demands: (1/2pi, 0.5, 2^23, 0.225). The 2^23
        // floors a float without a branch; the 0.225 takes a 5.6% parabola down
        // to 0.2%.
        kSine_ = vu::constant(c, 0.15915494F, 0.5F, 8388608.0F, 0.225F);
        kOne_ = vu::splat(c, 1.0F);
        // x = tightness, y = speed, z = depth. TIGHTNESS is the one to be
        // careful with: a wavelength shorter than a mesh moves that mesh's own
        // vertices by different amounts and TEARS IT OPEN - 0.5 shredded every
        // sphere in the scene. Long waves lift whole objects, which is what
        // water does.
        kWave_ = vu::constant(c, 0.12F, 1.3F, 0.18F, 0.0F);
    }
    vu::Vec kSine_, kOne_, kWave_;

    void vertex(vu::Ctx& c) override {
        vugen::Vu& b = c.raw();
        // FOUR is all there are (vu::Ctx::kScratchCount). The first draft asked
        // for a fifth and got a register with no name in it: the instruction
        // was emitted, the program built, the simulator passed, and the wave
        // simply never happened. So s0 carries the angle and then the result,
        // sineApprox gets 1..3, and s1 must be finished with BEFORE the call.
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

        // Seventeen instructions, and one field costs the same as three - so
        // ask for one. Phase 0.5 is a sine, 0.75 a cosine; adding pi/2 by hand
        // would NOT be, because pi/2 is not exactly a quarter turn in float.
        b.sineApprox(s0, s0, vuir::MX, kSine_.val(), kOne_.val(), scratch,
                     halfTurn);

        // Straight up. Along the normal would look better on a sphere and wrong
        // on the terrain, whose normals point up anyway - and the flat-colour
        // class has no normal to read.
        b.mulInto(s0, s0, depth, vuir::MX);
        b.addInto(c.position.val(), c.position.val(), vugen::Val{s0.reg, 0},
                  vuir::MY);
    }
};

}  // namespace

VU_PROGRAM(Wobble);
