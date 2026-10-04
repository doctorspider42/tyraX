// The VU1 instruction model the whole VU framework is built on (docs/vu-framework.md).
//
// One representation, three consumers: `vuasm` PARSES the engine's handwritten
// .vclpp into it, `vugen` BUILDS it from a C++ description, and `vusim` RUNS it
// on the host. That is what lets the framework claim a generated program behaves
// like the handwritten one - both sides end up as this, and the simulator
// executes both.
//
// Deliberately NOT the hardware encoding. This is VCL-level assembly with
// UNLIMITED virtual registers: VCL owns scheduling and register allocation
// (`.init_vf_all`), so the framework never has to, and the emitted text goes
// through exactly the same optimizer the handwritten programs do. The cost of
// that choice is that instruction ORDER here is pre-schedule - it says what the
// program computes, not what cycle each op lands in.
//
// No GL, no ImGui, no project.hpp - the aobake/livedbg shape.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace vuir {

// Destination field mask bits (the ".xyzw" suffix on a VU instruction).
enum : uint8_t { MX = 1, MY = 2, MZ = 4, MW = 8, MXYZ = 7, MALL = 15 };

// Reserved register indices. Index 0 is the hardwired constant in both files:
// vf00 = (0,0,0,1), vi00 = 0. Everything above is virtual.
enum : int16_t { kVf00 = 0, kVi00 = 0, kAcc = -2, kQ = -3, kI = -4, kR = -5 };

// No broadcast: the operand is read field-by-field instead of splatted.
enum : uint8_t { kNoBc = 4 };

enum class Op : uint8_t {
    Nop,
    // --- float, "upper" pipe ---
    Add, Sub, Mul, Mula, Madd, Madda, Msub, Msuba, Mini, Max,
    Move, Mr32, Abs, Ftoi0, Ftoi4, Itof0, Clipw,
    // --- "lower" pipe ---
    Div, Rsqrt, Sqrt, Loi, Mtir, Mfir,
    Fcand, Fcor, Fceq, Fcset, Fcget,
    // The MAC/STATUS flag family. Parsed so a program that uses them still
    // loads, but their semantics are NOT modelled - `vusim` warns and yields 0,
    // because a silently wrong flag would be worse than an admitted gap.
    Fmand, Fmeq, Fmor, Fsand, Fseq, Fsor, Fsset,
    Lq, Sq, Ilw, Isw,
    Iadd, Iaddi, Iaddiu, Isub, Iand, Ior,
    B, Ibeq, Ibne, Iblez, Ibgez, Ibgtz, Ibltz,
    Xtop, Xitop, Xgkick,
    // --- structural ---
    Label,    // no-op that names code[i] as a branch target
    Barrier,  // VCL "--barrier": do not schedule across this point
    Cont,     // VCL "--cont": the double-buffer continuation point
    End,      // the E bit: the microprogram stops here
};

/** Where the second source operand comes from. */
enum class Src : uint8_t { Vf, I, Q };

/** One instruction. Operand slots are shared across ops; see `disassemble` for
 * which ones each op reads. */
struct Instr {
    Op op = Op::Nop;
    uint8_t mask = MALL;   // destination field mask
    int16_t dst = 0;       // VF/VI register, or kAcc/kQ (never a bare 0 by accident)
    int16_t s1 = 0;        // first source register
    int16_t s2 = 0;        // second source register (see s2kind)
    uint8_t bc1 = kNoBc;   // broadcast field of s1 (div/rsqrt/mtir read one field)
    uint8_t bc2 = kNoBc;   // broadcast field of s2
    Src s2kind = Src::Vf;
    int16_t base = kVi00;  // VI base register for lq/sq/ilw/isw
    int32_t imm = 0;       // memory offset, iaddiu immediate, fcand mask
    float fimm = 0.0f;     // loi payload
    int32_t target = -1;   // branch target: index into Program::code
    int32_t line = 0;      // source line, for diagnostics
    /** For Op::Label this is the label's NAME; on any other op it is a trailing
     * comment the .vclpp emitter prints. Generated source has to be readable by
     * whoever has to debug it on hardware, so the generator names what it emits
     * (which address constant an offset came from, which macro a run of
     * instructions replaces). */
    std::string comment;
};

/** A parsed or generated microprogram. Registers are INTERNED: instructions
 * carry indices into `vfNames`/`viNames`, so simulation never hashes a string
 * and emission can print the original spelling back. */
struct Program {
    std::string name;
    std::vector<Instr> code;
    std::vector<std::string> vfNames;  // [0] == "vf00"
    std::vector<std::string> viNames;  // [0] == "vi00"
    std::vector<std::string> notes;    // non-fatal remarks from the producer

    Program() : vfNames{"vf00"}, viNames{"vi00"} {}

    int16_t vf(const std::string& name);  // intern, creating on first use
    int16_t vi(const std::string& name);
    int16_t findVf(const std::string& name) const;  // -1 when absent
    int16_t findVi(const std::string& name) const;
};

/** One instruction back as VCL-syntax text (used by the .vclpp emitter, the
 * disassembly listing and every diagnostic). */
std::string disassemble(const Program& p, const Instr& in);

/** Field letter for a broadcast index, "" for kNoBc. */
const char* fieldName(uint8_t bc);

/** ".xyzw"-style suffix for a destination mask, "" when the mask is MALL. */
std::string maskSuffix(uint8_t mask);

}  // namespace vuir
