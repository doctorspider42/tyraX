// A VU1 microprogram, executed on the host (docs/vu-framework.md).
//
// Debugging VU1 is blind: no printf, output straight to the GS, and the only
// ground truth is a capture off a running console (src/vucap.cpp). This runs the
// program HERE instead - same instructions, same 1024-quadword data memory, same
// bit patterns landing in the GIF packet - so a microprogram can be exercised in
// milliseconds without Docker, PCSX2 or a PS2.
//
// The output is deliberately `std::vector<uint32_t>` VU1 data memory, the exact
// shape `vucap::Capture::vuMem` carries, and it is decoded by the SAME
// `vucap::scanGifPackets`. A simulated run and a captured run are therefore
// directly comparable - that is the whole point, and the reason the decoder was
// lifted out of vucap.cpp rather than reimplemented here.
//
// What this models: the logical semantics a VCL programmer writes against -
// masked fields, the ACC, Q/I, the clip flag register, integer registers and
// memory, plus the fact that the VU FPU is NOT IEEE-754 (no inf, no NaN,
// overflow saturates to 0x7F7FFFFF, denormals read as zero - `vuFloat` in the
// .cpp, and the reason the move family deliberately bypasses it).
// What it does NOT model: cycle timing, the dual-issue pairing, branch
// delay slots or the DIV latency. Those belong to VCL, which schedules the
// program after this level; simulating them would make the model disagree with
// the source the programmer reads. The one hazard that IS a programmer-level
// mistake rather than a scheduling detail - overwriting Q before it is consumed
// - is reported (`qClobbers`), because the assembler will not tell you.
//
// No GL, no ImGui, no project.hpp - the aobake/livedbg shape.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "vuir.hpp"

namespace vusim {

/** VU1 data memory: 1024 quadwords. */
constexpr int kMemQuads = 1024;
constexpr int kMemWords = kMemQuads * 4;

struct Config {
    /** What `xtop` yields - the base of the current double-buffer half, in
     * quadwords. The EE picks this via `packet2_utils_vu_add_double_buffer`;
     * a harness sets it to wherever it staged its input block. */
    int top = 0;
    /** Runaway guard. A per-vertex loop over a few hundred vertices is tens of
     * thousands of steps; anything past this is a bug in the program. */
    long long maxSteps = 4000000;
    /** Record every executed instruction into `Result::trace` (slow, and the
     * listing is what the Debugger's step view shows). */
    bool trace = false;
    /** Stop after this many instructions have been traced (0 = no limit). */
    int traceLimit = 4000;
};

/** One thing the run noticed that the hardware would have swallowed silently. */
struct Warning {
    int pc = 0;
    int line = 0;
    std::string text;
};

struct Result {
    bool ok = false;
    std::string error;      // set when the run aborted (bad op, step budget)
    long long steps = 0;
    std::vector<int> kicks;         // XGKICK addresses, in execution order
    std::vector<uint32_t> mem;      // VU1 data memory after the run
    std::vector<std::string> trace;
    std::vector<Warning> warnings;
    int divByZero = 0;      // the hardware clamps; a real program should not
    int qClobbers = 0;      // Q overwritten before anything read it
    bool hitStepLimit = false;
};

/** Runs one pass of `p` - from the top of the program until it branches back to
 * its buffer loop, hits the E bit, or exhausts the step budget. `initialMem` is
 * VU1 data memory as the EE left it (pad or truncate to kMemWords; short input
 * is zero-filled). */
Result run(const vuir::Program& p, const std::vector<uint32_t>& initialMem,
           const Config& cfg);

/** A human-readable listing of the program, one line per instruction, with the
 * labels resolved. Used by `--vu-list` and the Debugger. */
std::string listing(const vuir::Program& p);

}  // namespace vusim
