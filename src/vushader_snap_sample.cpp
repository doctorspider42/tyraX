// The PlayStation 1 look, in one truncate.
//
// The first PlayStation had no floating-point rasteriser: vertices landed on
// whole screen pixels, so anything that moved a little jittered between them.
// That wobble is the single most recognisable thing about the era, and it is
// not an approximation of anything - it is exactly what quantising the screen
// position does. This program does it deliberately, on hardware that no longer
// has to.
//
// It is also the cheapest program in this project by a wide margin, and it
// exists partly to make that point: a look does not have to be expensive to be
// worth putting on VU1.
#include "vushader.hpp"

namespace {

/** Quantise the screen position to a coarse grid. */
struct VertexSnap : vu::Program {
    const char* name() const override { return "Vertex snap"; }

    // Everything visible. The effect is about WHERE a vertex lands, so it has
    // nothing to do with how the surface is coloured, and a scene where only
    // half the meshes jittered would look broken rather than retro.
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

    // Ndc: after the perspective divide, before the scale into the GS's 12.4
    // fixed point. This is the only slot where "one screen pixel" is a
    // constant - do the same thing in object space and near geometry snaps to
    // a fine grid while far geometry snaps to a coarse one, which reads as a
    // bug rather than as a console.
    vu::Slot slot() const override { return vu::Slot::Ndc; }

    // OFF at boot. Every program here claims the same material classes, so
    // only one can be resident at a time - the second override of a class
    // simply replaces the first. The demo makes that a feature: one button per
    // look, and micro memory shows exactly one of them.
    bool activeAtBoot() const override { return false; }

    // NOT movesGeometry. The clipper has already run and the vertex is in
    // screen space; a nudge of a few pixels stays well inside the guard band.
    // It is displacement in OBJECT or CLIP space that outruns the clipper.

    void prepare(vu::Ctx& c) {
        // NDC spans -1..1, so a grid of N cells across the screen is a step of
        // 2/N. y is the reciprocal, z the step itself.
        kGrid_ = vu::constant(c, 0.0F, kCells * 0.5F, 2.0F / kCells, 0.0F);
    }
    vu::Vec kGrid_;
    // Cells across the screen. 160 is a PS1-ish 320-wide raster seen at half
    // resolution; 40 is unmistakable and slightly seasick.
    static constexpr float kCells = 110.0F;

    void vertex(vu::Ctx& c) override {
        vugen::Vu& b = c.raw();
        const vugen::Val s0 = c.scratch(0).val();
        const vugen::Val toCells = vugen::Val{kGrid_.val().reg, 1};
        const vugen::Val toNdc = vugen::Val{kGrid_.val().reg, 2};

        // x and y only. Snapping Z as well would quantise DEPTH, and every
        // surface that shares a plane with another would start z-fighting in
        // stripes - the same failure the reflection pass spent a commit on.
        const uint8_t xy = (uint8_t)(vuir::MX | vuir::MY);
        b.mulInto(s0, c.position.val(), toCells, xy);
        b.truncate(s0, s0, xy);
        b.mulInto(c.position.val(), s0, toNdc, xy);
    }
};

}  // namespace

VU_PROGRAM(VertexSnap);
