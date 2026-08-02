// Writing a VU1 microprogram in C++ (docs/vu-framework.md).
//
// The engine's VU1 programs are twenty near-identical .vclpp files: the same
// skeleton (load per-mesh constants, xtop the double buffer, emit the GIF tag
// block, loop over vertex triples, xgkick) around a small per-vertex body that
// differs by which attributes the mesh carries. Every feature added since -
// hardware fog, the in-band ALPHA tag, the spot light, the matcap - had to be
// threaded through all of them BY HAND, and each program's EE-side twin
// (unpack layout, maxVertCount, the GIF register list, the NLOOP patch offset)
// had to be kept in step by hand as well. That duplication, not the VU
// instruction set, is what makes this code hard to touch.
//
// So: describe a program once, in C++, and generate BOTH sides from it.
//
//   Desc d = descAsIsTextureColor();
//   Built b = build(d);            // b.program (IR), b.vclpp, b.eeSource, ...
//
// The backend emits VCL - NOT raw microcode. VCL still does the register
// allocation and the dual-issue scheduling, which is the whole reason a
// generated program is as fast as a handwritten one: it goes through the same
// optimizer, from source it would have accepted anyway. The framework never
// tries to out-schedule it.
//
// And because `Built::program` is the same `vuir::Program` the parser produces
// from the handwritten file, `equivalence()` can run BOTH in the host simulator
// on identical randomized input and diff the GS output bit for bit. That is the
// claim this module is built to support, and it needs no PS2 to check.
//
// No GL, no ImGui, no project.hpp - the aobake/livedbg shape.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "vuir.hpp"

namespace vugen {

// ---------------------------------------------------------------------------
// The builder
// ---------------------------------------------------------------------------

/** A value living in a virtual VF register. Registers are unlimited here; VCL
 * allocates the real 32 when it compiles the emitted source. */
struct Val {
    int16_t reg = 0;
    uint8_t bc = vuir::kNoBc;  // reading this value splats the named field
    Val broadcast(int field) const { return Val{reg, (uint8_t)field}; }
};

/** A value in a virtual integer register. */
struct IVal {
    int16_t reg = 0;
};

/** A branch target. Bind it with `Vu::bind` - forward branches are fine. */
struct Lbl {
    int id = -1;
};

class Vu {
   public:
    explicit Vu(vuir::Program& program);

    // --- registers -------------------------------------------------------
    Val named(const std::string& name);   // a stable, readable name
    IVal inamed(const std::string& name);
    Val tmp(const char* hint = "t");      // a fresh short-lived register
    Val zero() const { return Val{vuir::kVf00, vuir::kNoBc}; }
    IVal izero() const { return IVal{vuir::kVi00}; }

    // --- memory ----------------------------------------------------------
    Val lq(IVal base, int offset, uint8_t mask = vuir::MALL, const char* hint = "t");
    /** A per-mesh constant, addressed off vi00. */
    Val lqConst(int address, uint8_t mask = vuir::MALL, const char* hint = "k");
    void sq(Val value, IVal base, int offset, uint8_t mask = vuir::MALL);
    IVal ilw(IVal base, int offset, int field, const char* hint = "n");
    void isw(IVal value, IVal base, int offset, int field);

    // --- float arithmetic -------------------------------------------------
    Val add(Val a, Val b, uint8_t mask = vuir::MALL);
    Val sub(Val a, Val b, uint8_t mask = vuir::MALL);
    Val mul(Val a, Val b, uint8_t mask = vuir::MALL);
    Val minimum(Val a, Val b, uint8_t mask = vuir::MALL);
    Val maximum(Val a, Val b, uint8_t mask = vuir::MALL);
    Val move(Val a, uint8_t mask = vuir::MALL);
    /** acc = a * b (mula). */
    void mulAcc(Val a, Val b, uint8_t mask = vuir::MALL);
    /** acc += a * b (madda). */
    void maddAcc(Val a, Val b, uint8_t mask = vuir::MALL);
    /** dst = acc + a * b (madd), writing INTO `dst`. */
    void maddInto(Val dst, Val a, Val b, uint8_t mask = vuir::MALL);
    /** dst = acc - a * b (msub). */
    void msubInto(Val dst, Val a, Val b, uint8_t mask = vuir::MALL);
    /** loi - loads the I register. Ops taking `useI` read it. */
    void loadI(float value);
    Val minimumI(Val a, uint8_t mask = vuir::MALL);
    Val addI(Val a, uint8_t mask = vuir::MALL);
    /** q = a[fa] / b[fb]. */
    void divQ(Val a, int fa, Val b, int fb);
    void rsqrtQ(Val a, int fa, Val b, int fb);
    Val mulQ(Val a, uint8_t mask = vuir::MALL);
    Val ftoi4(Val a, uint8_t mask = vuir::MALL);
    Val ftoi0(Val a, uint8_t mask = vuir::MALL);
    /** In-place variants, for when the handwritten macro overwrote its input
     * (the emitted code must match instruction for instruction to stay
     * bit-identical, and an extra `move` would not). */
    void ftoi4Into(Val dst, Val a, uint8_t mask = vuir::MALL);
    void ftoi0Into(Val dst, Val a, uint8_t mask = vuir::MALL);
    void mulInto(Val dst, Val a, Val b, uint8_t mask = vuir::MALL);
    void addInto(Val dst, Val a, Val b, uint8_t mask = vuir::MALL);
    void minimumIInto(Val dst, Val a, uint8_t mask = vuir::MALL);
    void minimumInto(Val dst, Val a, Val b, uint8_t mask = vuir::MALL);
    void maximumInto(Val dst, Val a, Val b, uint8_t mask = vuir::MALL);
    void mulQInto(Val dst, Val a, uint8_t mask = vuir::MALL);

    // --- integer ----------------------------------------------------------
    IVal iadd(IVal a, IVal b, const char* hint = "i");
    void iaddInto(IVal dst, IVal a, IVal b);
    IVal iaddiu(IVal a, int imm, const char* hint = "i");
    void iaddiuInto(IVal dst, IVal a, int imm);
    IVal ior(IVal a, IVal b, const char* hint = "i");
    IVal iand(IVal a, IVal b, const char* hint = "i");
    IVal mtir(Val a, int field, const char* hint = "i");

    // --- control ----------------------------------------------------------
    /** `name` becomes the label in the emitted source - generated microcode
     * gets debugged on hardware, so it should read like the handwritten kind. */
    Lbl label(const char* name);
    void bind(Lbl l);
    void branch(Lbl l);
    void branchIfLez(IVal a, Lbl l);
    void branchIfNotEq(IVal a, IVal b, Lbl l);
    void xtop(IVal dst);
    void xgkick(IVal address);
    /** VCL scheduling markers, reproduced so the emitted source matches the
     * shape the handwritten programs use around XGKICK. */
    void barrier();
    void cont();

    // --- the method library the framework offers --------------------------
    /** ScaleVertexToGSFormat: NDC -> the GS 12.4 screen format, in place.
     * acc = scale * 1.0; v = acc + v * scale; v = ftoi4(v)  (xyz only, so the
     * clip-space W survives for fog and the perspective divide). */
    void scaleToGsFormat(Val vertex, Val scale);
    /** FixColor: clamp to 0..255 and convert to integer bits, in place. */
    void fixColor(Val color);
    /** CalculateTyraFog: the GS fog coefficient from the vertex's clip W,
     * already shifted into the F field position of a packed XYZF2. */
    /** `dst` and `scratch` are supplied by the caller and REUSED across the
     * three vertices on purpose. Minting a fresh register per call is what a
     * value-returning API wants to do, and it is a trap here: VU1 has 16
     * integer registers, the simulator has unlimited virtual ones, so the
     * pressure is invisible to `--vu-check` and only shows on hardware. */
    void fogCoefficient(IVal dst, Val vertex, Val fogParams, Val scratch);
    /** MatrixMultiplyVertex: dst = m * v, the four-register mula/madd chain. */
    void transform(Val dst, const Val m[4], Val v);
    /** CalculateTyraEnvStq: turns the object-space normal carried in the ST
     * slot into a sphere-map (matcap) ST, in place.
     *   s = 0.5 + 0.5 * dot(normalize(n), cameraRight)
     *   t = 0.5 - 0.5 * dot(normalize(n), cameraUp)
     * The normal is RE-NORMALIZED first because the EE clipper lerps it across
     * clip cuts and a lerped normal is short. That costs an rsqrt, which WRITES
     * Q - so this must run BEFORE the position's perspective divide, or the
     * later mulq picks up the wrong Q. `scratch` supplies four temporaries. */
    void envStq(Val stq, Val envRight, Val envUp, Val envConsts,
                const Val scratch[4]);

   private:
    vuir::Program* p_;
    int tmpSeq_ = 0;
    std::vector<int> labelIndex_;                    // label id -> code index
    std::vector<std::string> labelNames_;            // label id -> emitted name
    std::vector<std::pair<int, int>> fixups_;        // code index -> label id
    friend struct Finisher;
    void emit(vuir::Instr in);

   public:
    /** Resolves forward branches. Call once, after the body is built. */
    void finish();
};

// ---------------------------------------------------------------------------
// Program descriptions
// ---------------------------------------------------------------------------

/** One StaPip "as is" program - the family the EE clipper feeds, i.e. positions
 * that are ALREADY in NDC with the clip-space W kept for fog and for the texture
 * perspective divide. */
struct Desc {
    std::string vclName;      // #vuprog name, e.g. "StaPipVU1AsIsTC"
    std::string asmName;      // .name / linker symbol stem, "StaPipVU1As_Is_TC"
    std::string fileStem;     // "stapip_as_is_tc_vu1"
    std::string className;    // "StaPipAsIsTCVU1Program"
    std::string programEnum;  // "StaPipAsIsTextureColor"
    std::string title;        // human label, "StaPip - As is - TC"
    bool texture = false;     // carries an ST stream
    bool dirLights = false;   // carries normals, shades on VU
    bool env = false;         // matcap: the ST slot holds an object-space normal
};

/** The five as_is variants, exactly as the engine ships them. */
Desc descAsIsColor();
Desc descAsIsTextureColor();
Desc descAsIsDirLights();
Desc descAsIsTextureDirLights();
Desc descAsIsTextureEnv();
std::vector<Desc> allAsIsDescs();

/** Everything generated for one program. */
struct Built {
    vuir::Program program;   // simulate this
    std::string vclpp;       // the .vclpp source, ready for the Docker build
    std::string eeHeader;    // inc/.../<stem>_program.hpp
    std::string eeSource;    // src/.../<stem>_program.cpp
    int tagQuads = 0;        // GIF tag block, in quadwords
    int regsPerVertex = 0;   // GS registers per vertex (the store stride)
    int attrStreams = 0;     // per-vertex input arrays the EE unpacks
    std::vector<std::string> notes;
};

Built build(const Desc& d);

// ---------------------------------------------------------------------------
// Checking
// ---------------------------------------------------------------------------

/** Result of running two programs on identical input and diffing what they
 * staged for the GS. */
struct Equivalence {
    bool ran = false;
    bool identical = false;
    std::string error;
    int trials = 0;
    int vertices = 0;
    int firstBadTrial = -1;
    std::string detail;      // what differed, when something did
    int handwrittenInstrs = 0;
    int generatedInstrs = 0;
};

/** Runs `a` and `b` over `trials` randomized vertex buffers (deterministic in
 * `seed`) and compares every quadword of the GIF packet each staged. This is
 * the framework's central check: not "the text looks similar" but "the bits the
 * GS would receive are the same". */
Equivalence equivalence(const vuir::Program& a, const vuir::Program& b,
                        const Desc& d, int trials, uint32_t seed);

// ---------------------------------------------------------------------------
// Micro-memory budget
// ---------------------------------------------------------------------------

/** VU1 micro memory holds 2048 instruction slots, and `Path1` parks the
 * draw-finish helper at the very top - overwriting it hangs the post-fx PATH1
 * barrier forever (see path1.cpp). The pipeline set must fit below that. */
constexpr int kMicroMemSlots = 2048;

struct BudgetEntry {
    std::string name;
    int emitted = 0;   // instructions the framework emitted
    int slotsMin = 0;  // if VCL paired every one of them
    int slotsMax = 0;  // if it paired none
};

struct Budget {
    std::vector<BudgetEntry> entries;
    int totalMin = 0;
    int totalMax = 0;
    int ceiling = 2042;  // measured headroom below the draw-finish helper
    bool certainlyFits() const { return totalMax <= ceiling; }
    bool certainlyOverflows() const { return totalMin > ceiling; }
};

/** Instruction counts are a RANGE on purpose: VCL packs an upper and a lower op
 * into one 64-bit slot when it can, so N emitted instructions occupy between
 * ceil(N/2) and N slots. The exact number is only known after VCL runs - which
 * is why the engine's own guard is a runtime assert. Reporting the range is
 * honest; reporting a single number would not be. */
Budget budget(const std::vector<std::pair<std::string, const vuir::Program*>>& set);

}  // namespace vugen
