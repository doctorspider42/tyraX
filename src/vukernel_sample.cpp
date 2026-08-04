// The canonical example of a project-written VU0 kernel (src/vushader.hpp).
//
// Lives in the editor so `--vu-check` can build and run it under the VU0
// machine model; examples/vu-lab ships the same file in src/vu0/.
//
// WHY THIS JOB. A kernel earns its keep when the EE has the same small piece of
// arithmetic to do for many things at once, and the oldest such job in a PS2
// game is "how far away is everything?" - one subtract, one dot and one root
// per object, over an array the game already has. The answers pick a level of
// detail, decide what to draw, and set how loud a sound is.
#include "vushader.hpp"

namespace {

/** Distance and an LOD band, for a batch of positions.
 *
 * In:  a position in xyz. W rides through untouched, so the caller can carry an
 *      index there instead of keeping a second array in step.
 * Out: x = distance, y = LOD band (0 nearest, floored), z = SQUARED distance
 *      (free, and the right thing to sort with), w = whatever came in.
 *
 * Parameters: xyz = the camera, w = 1 / the width of a band in world units.
 */
struct Ranges : vu::Kernel {
    const char* name() const override { return "Ranges"; }

    // Not a limit on how many objects a game can check - run() takes its own
    // count - but on how much EE-blocking work happens in one call.
    int maxElements() const override { return 64; }

    void prepare(vu::Ctx& c) override {
        // x = a floor under the squared distance, so an object standing exactly
        // on the camera does not become 1/sqrt(0) and spread infinities through
        // everything downstream. y = the highest band, so a distant object
        // saturates instead of indexing off the end of a table.
        //
        // HERE and not in element(): a kernel's loop is a real branch, not
        // three unrolled copies, so a constant left in the body is rebuilt for
        // all 64 elements - and the `loi` inside it is the one construct vcl
        // reorders in a way the host simulator cannot model.
        kGuard_ = vu::constant(c, 1e-6F, 3.0F, 0.0F, 0.0F);
    }
    vu::Vec kGuard_;

    void element(vu::Ctx& c) override {
        vugen::Vu& b = c.raw();
        const vugen::Val s0 = c.scratch(0).val();
        const vugen::Val s1 = c.scratch(1).val();
        const vugen::Val s2 = c.scratch(2).val();
        const vugen::Val pos = c.position.val();
        const vugen::Val tiny = vugen::Val{kGuard_.val().reg, 0};
        const vugen::Val maxBand = vugen::Val{kGuard_.val().reg, 1};
        const vugen::Val invBand = vugen::Val{c.params.val().reg, 3};

        // The camera is xyz of the parameter quadword, so this is a plain
        // vector subtract - no broadcast, no per-lane arrangement.
        b.subInto(s0, pos, c.params.val(), vuir::MXYZ);

        // dd = dx*dx + dy*dy + dz*dz, folded into X. A broadcast is only legal
        // on the SECOND operand, which is why the folds read s1's own fields.
        b.mulInto(s1, s0, s0, vuir::MXYZ);
        b.addInto(s1, s1, vugen::Val{s1.reg, 1}, vuir::MX);
        b.addInto(s1, s1, vugen::Val{s1.reg, 2}, vuir::MX);
        b.maximumInto(s1, s1, tiny, vuir::MX);

        // dist = dd / sqrt(dd). The VU has the reciprocal root, so the root is
        // one multiply on top of it, and vf00's W is a hardwired 1.0 - the
        // numerator needs no register of its own.
        //
        // rsqrt WRITES Q, which a VU1 script must never do (the framework owns
        // Q there for the perspective divide). A kernel has no framework around
        // the body, nothing else reads Q, and the generator says so in a note
        // instead of refusing it.
        b.rsqrtQ(b.zero(), 3, s1, 0);
        b.mulQInto(s2, s1, vuir::MX);

        // band = floor(min(dist / bandWidth, maxBand)). `truncate` rounds
        // toward zero, which IS floor because a distance is never negative.
        b.mulInto(s0, s2, invBand, vuir::MX);
        b.minimumInto(s0, s0, maxBand, vuir::MX);
        b.truncate(s0, s0, vuir::MX);

        // Written LAST, because the position is the input: any of these landing
        // earlier would be read as a coordinate by the line after it.
        b.addInto(pos, b.zero(), vugen::Val{s2.reg, 0}, vuir::MX);
        b.addInto(pos, b.zero(), vugen::Val{s0.reg, 0}, vuir::MY);
        b.addInto(pos, b.zero(), vugen::Val{s1.reg, 0}, vuir::MZ);
    }
};

}  // namespace

VU_KERNEL(Ranges);
