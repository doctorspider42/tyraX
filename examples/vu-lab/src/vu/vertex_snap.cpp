// The PlayStation 1 look, in one truncate.
//
// The first PlayStation had no floating-point rasteriser: vertices landed on
// whole screen pixels and anything that moved jittered between them. This is
// not an approximation of that - it is exactly what quantising the screen
// position does. Also the cheapest program in the project by a wide margin,
// which is half the reason it is here: a look need not be expensive.
#include "vushader.hpp"

namespace {

/** Quantise the screen position to a coarse grid. */
struct VertexSnap : vu::Program {
    const char* name() const override { return "Vertex snap"; }

    unsigned classes() const override {
        // EVERY class, and for a program that MOVES vertices that is the only
        // correct answer, not a preference. An object often draws SEVERAL
        // passes over the same vertices in different classes - a reflective
        // ball has a base pass and a matcap pass - and if one snaps to the grid
        // while the other does not, the copies separate by up to a cell and
        // each shows through the other. On the vu-lab ball that reads as a grey
        // wedge, which looks exactly like the coplanar depth bug and is not.
        // (A COLOUR effect may claim a subset: a posterised base pass next to
        // an unposterised reflection still looks like one object.)
        //
        // Affordable here because this is three instructions and one constant
        // register, so even the register-tight classes carry it. A heavier
        // displacement (wobble.cpp) has to weigh that.
        return vu::kAll;
    }

    // Ndc: after the perspective divide, before the scale into GS 12.4. The
    // only slot where "one screen pixel" is a constant - do this in object
    // space and near geometry snaps to a fine grid while far geometry snaps to
    // a coarse one, which reads as a bug rather than as a console.
    vu::Slot slot() const override { return vu::Slot::Ndc; }

    // OFF at boot: every program here claims the same classes, so only one can
    // be resident and the demo makes that a feature - one button per look.
    bool activeAtBoot() const override { return false; }

    // NOT movesGeometry: the clipper has already run and the vertex is in
    // screen space, so a few pixels stay well inside the guard band. It is
    // displacement in OBJECT or CLIP space that outruns the clipper.

    void prepare(vu::Ctx& c) {
        // steps and 1/steps, NOT steps/2 and 2/steps. The first draft assumed
        // this slot hands over a -1..1 NDC; the values are far smaller, so the
        // grid came out several times too coarse and the scene collapsed into a
        // horizontal band a few levels tall. One coordinate unit IS the screen
        // here - these are the built-in `snap` stage's numbers, not a guess.
        kGrid_ = vu::constant(c, 0.0F, kCells, 1.0F / kCells, 0.0F);
    }
    vu::Vec kGrid_;
    // Grid divisions across the screen: 160 is roughly PS1, 640 is subtle, and
    // far below 160 it stops being a wobble and becomes a fold.
    static constexpr float kCells = 160.0F;

    void vertex(vu::Ctx& c) override {
        vugen::Vu& b = c.raw();
        const vugen::Val s0 = c.scratch(0).val();
        const vugen::Val toCells = vugen::Val{kGrid_.val().reg, 1};
        const vugen::Val toScreen = vugen::Val{kGrid_.val().reg, 2};

        // x and y only. Snapping Z would quantise DEPTH, and every coplanar
        // surface would z-fight in stripes.
        const uint8_t xy = (uint8_t)(vuir::MX | vuir::MY);
        b.mulInto(s0, c.position.val(), toCells, xy);
        b.truncate(s0, s0, xy);
        b.mulInto(c.position.val(), s0, toScreen, xy);
    }
};

}  // namespace

VU_PROGRAM(VertexSnap);
