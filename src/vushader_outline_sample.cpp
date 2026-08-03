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
        // POSTERISE PER CHANNEL, which is the look, and not the luminance
        // banding this file used to do. Quantising r, g and b apart moves the
        // HUE, and that hue shift is what reads as ink and flat paint rather
        // than as a dimmer switch - a shaded green snaps to a different green
        // instead of the same green darker. Banding the luminance instead
        // preserves hue perfectly and therefore looks like shading, which is
        // exactly what a cel look is trying not to look like.
        //
        // x = 1, y = levels/255, z = 255/levels, w = half a step.
        // The 1.0 is FIRST on purpose: `1 - gate` takes it from the x of the
        // FIRST operand, and VU1 will only broadcast the SECOND one - parked
        // anywhere else it would be unreachable there and the subtract would
        // quietly use a different number with nothing to complain about.
        kPost_ = vu::constant(c, 1.0F, kLevels / 255.0F, 255.0F / kLevels,
                              0.5F);
    }
    vu::Vec kEnc_, kPost_;
    // Levels per channel. Three is poster paint, eight is nearly a gradient.
    static constexpr float kLevels = 4.0F;

    void vertex(vu::Ctx& c) override {
        vugen::Vu& b = c.raw();
        const vugen::Val s0 = c.scratch(0).val();
        const vugen::Val s1 = c.scratch(1).val();
        const vugen::Val s2 = c.scratch(2).val();

        const vugen::Val s3 = c.scratch(3).val();

        const vugen::Val width = vugen::Val{c.params.val().reg, 0};
        const vugen::Val invEnc = vugen::Val{kEnc_.val().reg, 3};
        const vugen::Val one = vugen::Val{kPost_.val().reg, 0};
        const vugen::Val down = vugen::Val{kPost_.val().reg, 1};
        const vugen::Val up = vugen::Val{kPost_.val().reg, 2};
        const vugen::Val halfStep = vugen::Val{kPost_.val().reg, 3};

        // Decode the normal out of the colour and walk the vertex along it.
        // The width already carries the zero that switches this off, so no
        // ordinary object pays anything but three multiplies it ignores.
        b.subInto(s0, c.color.val(), kEnc_.val(), vuir::MXYZ);
        b.mulInto(s0, s0, invEnc, vuir::MXYZ);   // -1..1
        b.mulInto(s0, s0, width, vuir::MXYZ);    // object units, 0 = off
        b.addInto(c.position.val(), c.position.val(), s0, vuir::MXYZ);

        // gate = min(width * 255/levels, 1): 0 for every ordinary mesh, 1 for
        // any outline however thin. The scale-up factor doubles as the
        // steepness - it is 64 at four levels, and the thinnest width anyone
        // would author saturates that - which saves a constant on a register
        // file that has none to spare.
        b.mulInto(s1, c.params.val(), up, vuir::MX);
        b.minimumInto(s1, s1, one, vuir::MX);
        b.subInto(s2, kPost_.val(), vugen::Val{s1.reg, 0}, vuir::MX);  // 1-gate

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
        b.mulInto(s3, c.color.val(), down, vuir::MXYZ);   // into level units
        b.truncate(s3, s3, vuir::MXYZ);                   // drop the remainder
        b.addInto(s3, s3, halfStep, vuir::MXYZ);          // to the band CENTRE
        b.mulInto(s3, s3, up, vuir::MXYZ);                // back to 0..255

        // Half a step is not decoration. Truncation always rounds DOWN, so
        // without it every level lands at the bottom of its band and the whole
        // picture loses half a step of brightness - which looks exactly like
        // "the effect just darkens things", and was that complaint the first
        // time it came up.

        // One multiply finishes both jobs: the posterised colour for an
        // ordinary mesh, and zero for a shell - whose "colour" is an encoded
        // normal nobody wants to look at, so painting it black is the style
        // and the cleanup at once.
        b.mulInto(c.color.val(), s3, vugen::Val{s2.reg, 0}, vuir::MXYZ);

        // Alpha is untouched on purpose: it is what the GS blends with, and an
        // outline that fiddles with it turns into stipple against anything
        // drawn with blending (docs/vu-authoring.md, the hardware rules).
    }
};

}  // namespace

VU_PROGRAM(CellOutline);
