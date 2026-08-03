// The ink line around a cell-shaded object - the other half of the look.
//
// The outline is not a colour effect. It is a SECOND COPY of the object,
// grown along its own normals and pushed behind its own depth, so the z-buffer
// keeps nothing of it but the sliver sticking out past the silhouette. That
// sliver is the line. The engine already draws shells this way for the
// usable-object highlight (RIM mode, terrain_game.cpp) - this program is what
// makes the growth follow the surface instead of scaling about the centre,
// which is the difference between a line of even width and one that fattens at
// the ends of anything long.
//
// It owns the flat-colour class alone, and that is deliberate: cell shading
// bands LIGHT, and an unlit flat colour has none - one colour, one band, no
// visible effect. So the two programs divide the classes rather than fight
// over them.
#include "vushader.hpp"

namespace {

/** Grow along the normal, paint flat - gated by the mesh's own parameters.
 *
 * The normal arrives in the VERTEX COLOUR, encoded as 0..255 around 128, which
 * is what lets an outline ride the flat-colour class without adding a stream
 * to it: a colour bag carries positions and colours and nothing else. The game
 * bakes those encoded normals once, when it builds the shell proxy.
 *
 * Ordinary flat-colour objects go through this program untouched, and not by a
 * branch: their outline width is zero, so the offset multiplies to zero and
 * the paint-over factor evaluates to one. A branch on VU1 costs more than the
 * arithmetic it skips, and a branch that depends on per-mesh data breaks the
 * dual-issue schedule for every mesh, not just the ones that take it.
 */
struct CellOutline : vu::Program {
    const char* name() const override { return "Cell outline"; }

    // The flat-colour class only - see the file comment.
    unsigned classes() const override { return vu::kColour; }

    // ObjectSpace: the one slot where the position is still in the mesh's own
    // units AND the colour is already in hand. A width in object units is a
    // width a modeller can reason about; the same number applied after the MVP
    // would mean something different for every object.
    vu::Slot slot() const override { return vu::Slot::ObjectSpace; }

    // Ask the game for the second submission. A program can move a vertex; it
    // cannot make the game draw a mesh twice, and a grown copy is a second
    // draw by definition.
    bool shellPass() const override { return true; }
    // Screen units at one metre. The game scales it by distance, so the line
    // keeps its weight whether the object is at arm's length or across the
    // map - which is what "drawn" looks like and what a constant object-space
    // width does not give you.
    float shellWidth() const override { return 0.03F; }

    void prepare(vu::Ctx& c) {
        // xyz = the 128 the encoded normal is centred on, w = its 1/128 scale.
        kEnc_ = vu::constant(c, 128.0F, 128.0F, 128.0F, 1.0F / 128.0F);
        // The banding half - the same numbers cell_shading.cpp uses, because
        // it is the same look and a seam between two sets of thresholds would
        // show wherever a flat-colour object stands next to a lit one.
        kWeights_ = vu::constant(c, 0.299F, 0.587F, 0.114F, 0.0F);
        kThresh_ = vu::constant(c, 64.0F, 128.0F, 192.0F, 0.25F);  // .w = sharp
        // one / floor / step / the steepness that turns any non-zero width
        // into a full paint-over. The 1.0 is FIRST on purpose: `1 - gate`
        // takes it from the x of the first operand, and VU1 will only
        // broadcast the SECOND one - so a 1.0 parked in z would be
        // unreachable there and the subtract would quietly use the step
        // instead. The same 1.0 doubles as the clamp ceiling, where it IS
        // reachable as a broadcast.
        kRamp_ = vu::constant(c, 1.0F, 0.5F, 0.16667F, 1000.0F);
    }
    vu::Vec kEnc_, kWeights_, kThresh_, kRamp_;

    void vertex(vu::Ctx& c) override {
        vugen::Vu& b = c.raw();
        const vugen::Val s0 = c.scratch(0).val();
        const vugen::Val s1 = c.scratch(1).val();
        const vugen::Val s2 = c.scratch(2).val();

        const vugen::Val s3 = c.scratch(3).val();
        const vugen::Val s4 = c.scratch(4).val();

        const vugen::Val width = vugen::Val{c.params.val().reg, 0};
        const vugen::Val invEnc = vugen::Val{kEnc_.val().reg, 3};
        const vugen::Val sharp = vugen::Val{kThresh_.val().reg, 3};
        const vugen::Val one = vugen::Val{kRamp_.val().reg, 0};
        const vugen::Val floorV = vugen::Val{kRamp_.val().reg, 1};
        const vugen::Val step = vugen::Val{kRamp_.val().reg, 2};
        const vugen::Val steep = vugen::Val{kRamp_.val().reg, 3};

        // Decode the normal out of the colour and walk the vertex along it.
        // The width already carries the zero that switches this off, so no
        // ordinary object pays anything but three multiplies it ignores.
        b.subInto(s0, c.color.val(), kEnc_.val(), vuir::MXYZ);
        b.mulInto(s0, s0, invEnc, vuir::MXYZ);   // -1..1
        b.mulInto(s0, s0, width, vuir::MXYZ);    // object units, 0 = off
        b.addInto(c.position.val(), c.position.val(), s0, vuir::MXYZ);

        // gate = min(width * steep, 1): 0 for every ordinary mesh, 1 for any
        // outline however thin.
        b.mulInto(s1, c.params.val(), steep, vuir::MX);
        b.minimumInto(s1, s1, one, vuir::MX);
        b.subInto(s2, kRamp_.val(), vugen::Val{s1.reg, 0}, vuir::MX);  // 1-gate

        // AND THE BANDING, on the same class and in the same program - which
        // is the only way it can happen here. A flat-colour class carries no
        // runtime lighting, so this scene bakes its shading straight into the
        // VERTEX COLOURS on the EE; those objects are exactly the ones with a
        // visible light gradient, and they are all on this class. Handing the
        // class to an outline and the banding to the lit classes put the two
        // halves of one look on opposite sides of the scene. Slot::ObjectSpace
        // is what makes the merge possible: the position and the colour are
        // both in hand there, and with no lighting to wait for, the colour is
        // already final.
        b.mulInto(s3, c.color.val(), kWeights_.val(), vuir::MXYZ);
        b.addInto(s3, s3, vugen::Val{s3.reg, 1}, vuir::MX);
        b.addInto(s3, s3, vugen::Val{s3.reg, 2}, vuir::MX);  // luminance
        b.addInto(s4, b.zero(), vugen::Val{s3.reg, 0}, vuir::MXYZ);
        b.subInto(s4, s4, kThresh_.val(), vuir::MXYZ);
        b.mulInto(s4, s4, sharp, vuir::MXYZ);
        b.maximumInto(s4, s4, b.zero(), vuir::MXYZ);
        b.minimumInto(s4, s4, one, vuir::MXYZ);
        b.addInto(s3, s4, vugen::Val{s4.reg, 1}, vuir::MX);
        b.addInto(s3, s3, vugen::Val{s4.reg, 2}, vuir::MX);
        b.mulInto(s3, s3, step, vuir::MX);
        b.addInto(s3, s3, floorV, vuir::MX);  // the band, 0.5 .. 1.0

        // One multiply carries both: the band for an ordinary mesh, and zero
        // for a shell - whose "colour" is an encoded normal nobody wants to
        // look at, so painting it black is both the style and the cleanup.
        b.mulInto(s3, s3, vugen::Val{s2.reg, 0}, vuir::MX);
        b.mulInto(c.color.val(), c.color.val(), vugen::Val{s3.reg, 0},
                  vuir::MXYZ);

        // Alpha is untouched on purpose: it is what the GS blends with, and an
        // outline that fiddles with it turns into stipple against anything
        // drawn with blending (docs/vu-authoring.md, the hardware rules).
    }
};

}  // namespace

VU_PROGRAM(CellOutline);
