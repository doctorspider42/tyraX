// Writing a VU1 program in ordinary C++ (docs/vu-authoring.md, "Scripts").
//
// This is the layer a PROJECT writes against. `vugen` describes a program the
// way the framework thinks about it - virtual registers, masked fields, madd
// chains - which is the right model for reproducing the engine's twenty
// handwritten programs bit for bit, and the wrong one for "I want the lighting
// to come out in four bands".
//
// So: value types whose operators APPEND to the same program the built-in
// stages write into. Nothing is interpreted and nothing runs on the console
// that a shader author wrote - the code below executes on the HOST, at build
// time, and what it leaves behind is VCL that vcl schedules like any other.
// A `Vec` is a virtual register, an operator is one VU instruction, and that
// correspondence is deliberately visible: this is a way to write microcode, not
// a way to pretend the VU is a GPU.
//
//   struct CellShading : vu::Program {
//     const char* name() const override { return "Cell shading"; }
//     unsigned classes() const override { return vu::kLit | vu::kColour; }
//     void vertex(vu::Ctx& c) override {
//       c.color = vu::quantize(c.color, 4.0F) * vu::splat(c, 1.05F);
//     }
//   };
//   VU_PROGRAM(CellShading);
//
// The same layer authors a VU0 COMPUTE kernel - `vu::Kernel` at the bottom of
// this file, dropped in `src/vu0/` rather than `src/vu/`. Same value types,
// same scratch contract, same `c.raw()`; a body per ELEMENT instead of per
// vertex, and what comes out is a function the EE calls rather than a look.
//
// The one cost to keep in mind is the one the whole framework keeps warning
// about: every temporary is a VF register, VCL allocates 31 of them, and a
// cull-family program already keeps ~23 live. Long expressions do not fail
// gracefully - they fail as `no opt table` out of vcl. Reach for `c.scratch(n)`
// when a value is only needed for a line or two.
#pragma once

#include <vector>

#include "vugen.hpp"

namespace vu {

// --- which material classes a program claims -------------------------------
//
// The engine keeps one resident VU1 program per class, so a program REPLACES a
// class rather than being attached to an object. The names are the ones the
// editor shows (docs/vu-authoring.md, "Why one program per material class").
enum ClassBit : unsigned {
    kColour = 1u << 0,   // vertex colour only - the fallback for everything
    kLit = 1u << 1,      // shaded on VU1 from the scene's directional lights
    kLitTextured = 1u << 2,
    kTextured = 1u << 3,  // vertex colour + a map_Kd texture
    kMatcap = 1u << 4,    // a refl material; the ST slot carries a normal
    kAll = 0x1Fu,
};

/** WHERE in the pipeline the body runs. The default is where most effects
 * belong: after lighting and texturing, before the colour is clamped. */
using Slot = vugen::Slot;

// --- values ----------------------------------------------------------------

/** One virtual VF register: four floats, and every operator below is one VU
 * instruction on all four fields at once. Scalars are the same thing with a
 * field broadcast, which is why `dot` returns a Vec you read with `.x()`. */
class Vec {
   public:
    Vec() = default;
    Vec(vugen::Vu* b, vugen::Val v)
        : b_(b), v_(v), bid_(b ? b->id() : 0) {}

    vugen::Val val() const { return v_; }
    vugen::Vu* builder() const { return b_; }
    bool valid() const { return b_ != nullptr; }

    /** Read one field in every lane - the VU's broadcast, free as an operand. */
    Vec x() const { return {b_, v_.broadcast(0)}; }
    Vec y() const { return {b_, v_.broadcast(1)}; }
    Vec z() const { return {b_, v_.broadcast(2)}; }
    Vec w() const { return {b_, v_.broadcast(3)}; }

    /** The same value, but assigning to it writes ONLY xyz. On a colour that
     * means "leave alpha alone", and it is not a nicety: alpha is what the GS
     * blends with, so a shading effect that quantises it too turns a
     * reflection or a shadow pass into stippled patches. Anything that touches
     * c.color should ask itself whether it meant c.color.rgb(). */
    Vec rgb() const {
        Vec r(b_, v_);
        r.writeMask_ = vuir::MXYZ;
        return r;
    }

    Vec operator+(const Vec& o) const { return {b_, b_->add(v_, o.v_)}; }
    Vec operator-(const Vec& o) const { return {b_, b_->sub(v_, o.v_)}; }
    Vec operator*(const Vec& o) const { return {b_, b_->mul(v_, o.v_)}; }

    /** Write into the register this value NAMES, not into a new one. That is
     * what makes `c.color = ...` reach the packet: `c.color` is the program's
     * colour register, and the emitter keeps using it after the script. */
    Vec& operator=(const Vec& o) {
        if (this == &o) return *this;
        // REBIND, don't write, when this value came from a different program.
        //
        // One vu::Program instance builds every microprogram the project needs
        // - one per class, twice over for the pair - so a Vec kept in a member
        // outlives the program it was minted in. Writing through it then emits
        // a store to a register the CURRENT program never named, and vcl says
        // so in the least helpful way available: "can't use '?' in the name of
        // a register: 'vf??'". Comparing the builder's ID is what tells the two
        // cases apart - and it has to be the id, not the pointer: successive
        // build() calls put their Vu at the same stack address.
        if (!b_ || bid_ != o.bid_) {  // fresh binding, or another program's
            b_ = o.b_;
            v_ = o.v_;
            bid_ = o.bid_;
            return *this;
        }
        // Copy = add zero. One instruction, exact, and it writes the register
        // this value NAMES rather than minting another one.
        b_->addInto(v_, o.v_, b_->zero(), writeMask_);
        return *this;
    }

   private:
    vugen::Vu* b_ = nullptr;
    vugen::Val v_{};
    uint8_t writeMask_ = vuir::MALL;
    int bid_ = 0;  // which builder minted it - see Vu::id
};

class Ctx;  // defined below - the constant helpers take one

/** A constant in every lane, built ONCE per buffer rather than once per
 * vertex. That is not an optimisation, it is a correctness rule: `loi` writes
 * the I register, and a run of them inside the per-vertex body is something vcl
 * schedules around - the hardware then reads an I the host simulator never saw.
 * So the instructions are emitted here and immediately hoisted into the
 * preamble, which is what `Ctx::hoist` is for. */
inline Vec splat(Ctx& c, float value);
inline Vec constant(Ctx& c, float x, float y, float z, float w);

inline Vec minimum(const Vec& a, const Vec& b) {
    return {a.builder(), a.builder()->minimum(a.val(), b.val())};
}
inline Vec maximum(const Vec& a, const Vec& b) {
    return {a.builder(), a.builder()->maximum(a.val(), b.val())};
}
/** x*(1-t) + y*t, with t in every lane. */
inline Vec lerp(const Vec& a, const Vec& c, const Vec& t) {
    return a + (c - a) * t;
}

/** The dot product of the xyz lanes, broadcast into every lane. Three
 * instructions; w is left out because a normal's w is not a component. */
inline Vec dot3(const Vec& a, const Vec& c) {
    vugen::Vu* b = a.builder();
    vugen::Val m = b->mul(a.val(), c.val());
    vugen::Val s = b->add(vugen::Val{m.reg, vuir::kNoBc}, vugen::Val{m.reg, 1});
    s = b->add(s, vugen::Val{m.reg, 2});
    return {b, vugen::Val{s.reg, 0}};
}

// --- what the body is handed ------------------------------------------------

/** The registers of the program being generated. Assigning to one of these
 * WRITES it; reading one is free. */
class Ctx {
   public:
    explicit Ctx(vugen::ScriptCtx& s)
        : s_(s),
          position(s.b, s.position),
          color(s.b, s.color),
          uv(s.b, s.st),
          normal(s.b, s.normal),
          params(s.b, s.params),
          time(s.b, s.time) {}

    Vec position;  // the vertex, in the space this program's slot names
    Vec color;     // 0..255 per channel, the GS scale - 128 is "1.0" for alpha
    Vec uv;        // only meaningful when hasUv()
    Vec normal;    // WORLD space, and only on a lit class - see hasNormal()
    Vec params;    // the object's four numbers, from the inspector
    Vec time;      // (seconds, sin, cos, 1) - wrapped, so the sine stays exact

    bool hasUv() const { return s_.hasSt; }
    bool hasNormal() const { return s_.hasNormal; }

    /** A register the framework already reserved for you. Prefer these over
     * long expressions: every temporary a `+` mints is another register VCL
     * has to fit into 31, and running out is a vcl failure with no line
     * number. Indices 0..3 are always available. */
    Vec scratch(int i) {
        // CLAMPED, because the alternative is silent nonsense. Only
        // Desc::scriptScratch registers are actually named before a script
        // runs; the rest of the array is default-constructed, and asking for
        // one of those used to hand back a Val with no register in it. The
        // emitter wrote the instruction anyway, the program built, --vu-check
        // passed, and the effect simply did not happen - which cost a console
        // build and a bisection to find. Reusing a real register can at worst
        // alias; an unnamed one cannot even fail loudly.
        const int last = kScratchCount - 1;
        return {s_.b, s_.scratch[i < 0 ? 0 : (i > last ? last : i)]};
    }
    /** How many scratch registers `scratch()` can actually give you. */
    static constexpr int kScratchCount = 4;

    /** Move the instructions emitted since `from` into the preamble. Every
     * constant a script builds goes through this - see vu::splat. */
    void hoist(int from) {
        s_.preambleAt += s_.b->hoist(from, s_.preambleAt);
    }
    int mark() const { return s_.b->mark(); }

    /** The escape hatch: the raw builder, with everything the framework's own
     * programs use - madd chains, masked writes, loi, ftoi, the sine
     * approximation, xgkick. A script is not a sandbox. */
    vugen::Vu& raw() { return *s_.b; }

   private:
    vugen::ScriptCtx& s_;
};

inline Vec splat(Ctx& c, float value) {
    const int from = c.mark();
    vugen::Val t = c.raw().tmp("k");
    c.raw().constants(t, value, value, value, value);
    c.hoist(from);
    return {&c.raw(), t};
}

/** Four different lanes, for a colour tint or an axis mask. */
inline Vec constant(Ctx& c, float x, float y, float z, float w) {
    const int from = c.mark();
    vugen::Val t = c.raw().tmp("k");
    c.raw().constants(t, x, y, z, w);
    c.hoist(from);
    return {&c.raw(), t};
}

/** Clamp to 0..1. Note vf00 is (0,0,0,1), so the zero has to be built rather
 * than borrowed - the w lane of vf00 is ONE, and using it as "zero" silently
 * pins alpha to 1.0. */
inline Vec saturate(Ctx& c, const Vec& a) {
    return minimum(maximum(a, splat(c, 0.0F)), splat(c, 1.0F));
}

/** Round DOWN to `steps` levels per lane - the cell-shading primitive. The 2^23
 * trick, not a float floor: adding 2^23 to a positive float pushes the fraction
 * out of the mantissa and subtracting it back leaves the integer. Four
 * instructions, no branch and no table. */
inline Vec quantize(Ctx& c, const Vec& a, float steps) {
    Vec scaled = a * splat(c, steps);
    vugen::Val t = c.raw().tmp("q");
    c.raw().truncate(t, scaled.val(), vuir::MALL);
    return Vec{&c.raw(), t} * splat(c, 1.0F / steps);
}

// --- the program ------------------------------------------------------------

/** Derive from this, override three things, register it with VU_PROGRAM. The
 * generator instantiates it once per class it claims, on the host, at build
 * time. */
/** What `Program::activeAtBoot()` answers when a script does NOT override it.
 *
 * The editor's checkbox writes this; the generator sets it per program before
 * asking. A script that DOES override the method wins automatically, because
 * that is what C++ does with a virtual - there is no precedence rule anywhere
 * in the framework, and no way for the two to disagree about who decided. */
void setBootDefault(bool on);
bool bootDefault();

class Program {
   public:
    virtual ~Program() = default;
    /** Shown in the editor and used for the generated file names. */
    virtual const char* name() const = 0;
    /** Which material classes this program replaces (a ClassBit mask). */
    virtual unsigned classes() const { return kColour; }
    /** Where the body runs. Colour is after lighting and texturing and before
     * the clamp; ObjectSpace is before the MVP multiply, which is where you
     * move geometry. */
    virtual Slot slot() const { return Slot::Color; }
    /** Whether the game installs this at boot.
     *
     * `false` means the program is BUILT and sits in the ELF costing nothing
     * but EE memory, and the game decides when it goes on VU1:
     *
     *     vuscript::activate(vuscript::kMyProgram);   // at a trigger, a cutscene
     *     vuscript::deactivate(vuscript::kMyProgram); // engine's own program back
     *
     * That is one pipeline drain and one upload, so it belongs on an event and
     * not in a per-frame update. Micro memory is what makes this worth caring
     * about: only what is ACTIVE occupies VU1, so two heavy programs can exist
     * in one game as long as they are not resident together. */
    virtual bool activeAtBoot() const { return bootDefault(); }

    /** Whether this program DISPLACES vertices - moves them somewhere the
     * frustum classification did not expect them to be.
     *
     * It has to be declared, because the EE clipper runs FIRST: it cuts a mesh
     * against the frustum before any VU program sees it, so a vertex moved
     * afterwards is moved past a cut that was computed without it, and the
     * mesh tears wherever it meets the edge of the screen. A game whose active
     * script says yes submits its PROPS whole, taking the cull path instead,
     * which is safe because a prop is small enough to draw unclipped - the
     * terrain and the sky are not, and keep their clip checks.
     *
     * `Slot::Ndc` does not need this: the perspective divide has already
     * happened there, the vertex is in screen space, and a nudge of a few
     * pixels stays inside the guard band. It is displacement in OBJECT or CLIP
     * space that outruns the clipper. */
    virtual bool movesGeometry() const { return false; }

    /** Whether this program needs the game to draw a SHELL PASS for it.
     *
     * A program in `Slot::ObjectSpace` can move a vertex, but it cannot make
     * the game submit a mesh twice - and an outline, a fur layer or any other
     * grown-copy effect is a second submission of the same object by
     * definition. Returning true asks the game for one: each visible object is
     * drawn once more from its low-detail proxy, as a flat-colour bag whose
     * per-vertex colours carry the outward normal encoded around 128, pushed
     * behind its own depth about the eye so the z-buffer keeps only what
     * sticks out past the silhouette.
     *
     * The shell arrives with `shellWidth()` in x of the mesh parameters and
     * zero in every ordinary object's, which is what lets ONE program serve
     * both without a branch. */
    virtual bool shellPass() const { return false; }

    /** How far the shell grows, in SCREEN units at one metre - the game scales
     * it by distance so a line keeps its thickness across the scene. Ignored
     * unless shellPass() is true. */
    virtual float shellWidth() const { return 0.02F; }

    /** Called ONCE, where the preamble ends, before any per-vertex code.
     * Build constants here and keep them in members: `vu::splat` inside
     * `vertex()` is hoisted to the preamble too, but it runs once PER VERTEX
     * and the vertex loop is unrolled three times - so the same constant ends
     * up in three registers, and registers are what this program has least
     * of. */
    virtual void prepare(Ctx&) {}

    /** Called once per VERTEX. The loop, the packet and the GIF tag are the
     * framework's - this is the part that is yours. */
    virtual void vertex(Ctx& c) = 0;
};

// --- the kernel -------------------------------------------------------------

/** The same authoring, on the other vector unit: a VU0 COMPUTE kernel.
 *
 * A project drops this in `src/vu0/` instead of `src/vu/`, and what it gets
 * back is not a look - it is a function the EE calls:
 *
 *     Tyra::IntegrandKernel k;
 *     k.setParams(1.0F, 2.0F, 0.0F, 0.0F);
 *     k.setTime(seconds);
 *     k.run(in, out, count);        // count quadwords in, count out
 *
 * `element()` is the body, called once per element, and it is handed the same
 * `vu::Ctx` a `vertex()` body is - the same operators, the same four scratch
 * registers, the same `c.raw()` escape hatch, `prepare()` for the constants.
 * The ONE difference is what is in it: a kernel element is one quadword and
 * nothing else, so `c.position` IS the element (`c.color` and `c.uv` name the
 * same register, because there is no second thing to name), `hasUv()` and
 * `hasNormal()` are false, and there is no colour, no ST and no normal to read.
 *
 * Four things about VU0 that are not true of VU1, all of them cheap to say and
 * expensive to discover (docs/vu-authoring.md, "VU0 kernels"):
 *
 *   - It is a CALCULATOR, not a GPU. 512 micro slots against VU1's 2042, and
 *     256 quadwords of data memory - which the default layout splits into ~112
 *     elements in and ~112 out.
 *   - `run()` BLOCKS the EE and clobbers the COP2 register file the engine's
 *     own Vec4/M4x4 math uses. 32 elements is microseconds; a batch is not free.
 *   - It is SIMD over quadwords: the same operation on many elements. A scalar
 *     loop whose step depends on the previous step does not belong here.
 *   - There is NO cross-element reduction. A sum is something the EE does over
 *     the output array afterwards - VU0 computes the terms.
 *
 * And one thing that is pure gain: these instructions are counted against VU0's
 * own 512 slots. A kernel costs NOTHING out of the VU1 drawing budget. */
class Kernel {
   public:
    virtual ~Kernel() = default;
    /** Shown in the editor, and what the driver class is named after: "Noise
     * field" becomes `Tyra::NoiseFieldKernel`. */
    virtual const char* name() const = 0;

    /** The most elements ONE `run()` may carry. Both blocks have to fit VU0's
     * 256 quadwords next to the parameters, which caps this at 124; the
     * generator refuses a larger one rather than letting the address wrap.
     *
     * Bigger is not better. The EE stores every element in and reads every
     * element back out, blocked the whole time, so the batch is a latency
     * choice - one the caller can also make per call, since `run()` takes its
     * own count. */
    virtual int maxElements() const { return 112; }

    /** Called ONCE, before the element loop. Constants belong here for a
     * sharper reason than on VU1: the kernel's loop is a real branch, not an
     * unrolled run, so a constant built in `element()` is rebuilt on every
     * single element unless `vu::splat` hoists it out - and `loi` inside a loop
     * is the one thing vcl schedules in a way the host simulator cannot see. */
    virtual void prepare(Ctx&) {}

    /** Called once per ELEMENT. `c.position` is the element quadword; whatever
     * it holds when this returns is what the EE reads back. */
    virtual void element(Ctx& c) = 0;
};

/** Programs and kernels register themselves at static-init time; the
 * generator's main() walks the lists. Returns the count so the macro can define
 * a variable. */
int registerProgram(Program* p);
const std::vector<Program*>& registeredPrograms();
int registerKernel(Kernel* k);
const std::vector<Kernel*>& registeredKernels();

}  // namespace vu

/** One line at the bottom of the file. The instance is a function-local static
 * so registration cannot race with another translation unit's. */
#define VU_PROGRAM(Type)                                     \
    namespace {                                              \
    Type g_vuProgramInstance_##Type;                         \
    const int g_vuProgramRegistered_##Type =                 \
        ::vu::registerProgram(&g_vuProgramInstance_##Type);  \
    }

/** The same line at the bottom of a src/vu0/ file. */
#define VU_KERNEL(Type)                                     \
    namespace {                                             \
    Type g_vuKernelInstance_##Type;                         \
    const int g_vuKernelRegistered_##Type =                 \
        ::vu::registerKernel(&g_vuKernelInstance_##Type);   \
    }
