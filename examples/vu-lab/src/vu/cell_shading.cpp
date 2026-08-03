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
        return vu::kColour | vu::kLit | vu::kTextured | vu::kMatcap;
    }

    // After lighting and texturing, before the colour is clamped: the last
    // point where the value is still a float and still means "light".
    vu::Slot slot() const override { return vu::Slot::Color; }

    void vertex(vu::Ctx& c) override {
        // MULTIPLY, NEVER ADD. An object is drawn by more than one bag, and the
        // extra ones are MODULATION passes: a baked lightmap's vertex colour is
        // literally Color(0,0,0,128) with the occlusion in its texture. Adding
        // a constant lifts that black to grey, so the shadow pass turns into a
        // grey wash - dithered, because it is blended - that reads exactly like
        // z-fighting under the texture. Multiplying leaves zero at zero, so a
        // modulation pass passes through untouched.
        //
        // Straight in the GS's own 0..255 scale: a trip through 0..1 and back
        // costs two more constants, and the lit program has no room for them
        // (it keeps ~30 of VCL's 31 registers live and going over fails as
        // `no opt table`, measured on this file).
        vu::Vec lit = c.color * vu::splat(c, 1.15F);  // a little gain

        // Four bands. floor(lit * 4/255) * 255/4 - one truncate, two constants,
        // no branch and no table.
        vu::Vec q = lit * vu::splat(c, kBands / 255.0F);
        c.raw().truncate(q.val(), q.val(), vuir::MALL);
        // .rgb(), not the whole register: ALPHA IS WHAT THE GS BLENDS WITH.
        c.color.rgb() = q * vu::splat(c, 255.0F / kBands);
    }

    static constexpr float kBands = 4.0F;
};

}  // namespace

VU_PROGRAM(CellShading);
