// The canonical example of a project-written VU1 program (src/vushader.hpp).
//
// Lives in the editor so `--vu-check` can build and simulate it on the host;
// examples/vu-lab ships the same file. The flat-colour half of the cell look,
// which also carries the outline, is cell_outline.cpp - two files because a
// program is emitted per material class and the outline needs a different slot.
#include "vushader.hpp"

namespace {

/** Cell shading: snap each colour channel to a few flat levels.
 *
 * PER CHANNEL, deliberately. Quantising r, g and b apart moves the HUE - a
 * shaded green snaps to a *different* green - and that shift is what reads as
 * flat paint. Banding the LUMINANCE preserves hue exactly and looks like a
 * dimmer switch. (Found by accident: the version people preferred was an early
 * BUG that quantised one channel.)
 */
struct CellShading : vu::Program {
    const char* name() const override { return "Cell shading"; }

    // Matcap stays out: posterising a reflection quantises the sky it samples,
    // which reads as a bug rather than a style.
    unsigned classes() const override {
        return vu::kColour | vu::kTextured | vu::kLit | vu::kLitTextured;
    }

    // Ask the game for the outline pass. The growth is NOT done here: the EE
    // clipper cuts a mesh before any VU program runs, so a vertex grown
    // afterwards is grown past a cut computed without it and the line tears at
    // the screen edge. Submitting unclipped instead trades that for GS raster
    // wrap. So the game grows the shell on the EE, and what is left for this
    // program is painting it flat.
    bool shellPass() const override { return true; }
    // A FRACTION of the object's size, not a screen width: baked geometry
    // cannot follow the camera, and this stops small props wearing tyres.
    float shellWidth() const override { return 0.06F; }

    // After lighting and texturing, before the clamp: the last point where the
    // value is still a float and still means "light".
    vu::Slot slot() const override { return vu::Slot::Color; }

    void prepare(vu::Ctx& c) {
        // ONE register for three numbers, and that is the difference between
        // fitting the lit classes and not: they already hold ~30 of VCL's 31,
        // and a scalar per register produced `time out.. failed to normal via
        // processing` - vcl does not fail there, it stops OPTIMISING, at 45 s
        // a go.
        //
        // x = 1, y = levels/255, z = 255/levels, w = half a step. The 1.0 is
        // FIRST because `1 - flag` takes it from the FIRST operand and VU1 will
        // only broadcast the SECOND - parked elsewhere it is unreachable there,
        // and the subtract quietly uses a different number.
        kPost_ = vu::constant(c, 1.0F, kLevels / 255.0F, 255.0F / kLevels,
                              0.5F);
    }
    vu::Vec kPost_;
    // cell_outline.cpp carries the same number. 3 is poster paint, 8 a gradient.
    static constexpr float kLevels = 4.0F;

    void vertex(vu::Ctx& c) override {
        // Raw builder and framework scratch, minting nothing. The value layer
        // is the readable way to write this and it is also what made the build
        // crawl: every `a * b` mints a register, the vertex loop is unrolled
        // three times, and the live temporaries hit vcl's allocator timeout.
        vugen::Vu& b = c.raw();
        const vugen::Val s0 = c.scratch(0).val();
        const vugen::Val s1 = c.scratch(1).val();
        const vugen::Val down = vugen::Val{kPost_.val().reg, 1};
        const vugen::Val up = vugen::Val{kPost_.val().reg, 2};
        const vugen::Val halfStep = vugen::Val{kPost_.val().reg, 3};

        b.mulInto(s0, c.color.val(), down, vuir::MXYZ);  // into level units
        b.truncate(s0, s0, vuir::MXYZ);                  // drop the remainder
        // Half a step is not decoration: truncation always rounds DOWN, so
        // without it every level sits at the bottom of its band and the picture
        // loses half a step of brightness everywhere - which reads as "the
        // effect just makes things darker", and did.
        b.addInto(s0, s0, halfStep, vuir::MXYZ);         // to the band CENTRE
        b.mulInto(s0, s0, up, vuir::MXYZ);               // back to 0..255

        // ...and flat black if this mesh is an outline shell. x of the mesh
        // parameters is 1 there and 0 everywhere else, so this is a subtract
        // and a multiply rather than a branch - and a branch on per-mesh data
        // would cost the dual-issue schedule for every mesh, not only the ones
        // that take it. Handing the shell black VERTICES instead does not work:
        // the posterise rounds to band CENTRES, so black comes out dark grey.
        b.subInto(s1, kPost_.val(), vugen::Val{c.params.val().reg, 0},
                  vuir::MX);
        b.mulInto(c.color.val(), s0, vugen::Val{s1.reg, 0}, vuir::MXYZ);

        // xyz only: ALPHA IS WHAT THE GS BLENDS WITH, and quantising it turns a
        // reflection or a shadow pass into stipple. And no divide anywhere - a
        // Q write from a script body gets scheduled into the perspective
        // divide's window; on the console that was grey stipple and z-fighting
        // shadows, while the host simulator showed nothing. vugen refuses it.
    }
};

}  // namespace

VU_PROGRAM(CellShading);
