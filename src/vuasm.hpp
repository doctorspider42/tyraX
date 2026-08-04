// Reads the engine's handwritten VU1 sources into the framework's IR
// (docs/vu-framework.md).
//
// This is what makes the framework checkable rather than merely plausible: the
// twenty .vclpp programs in vendor/tyra are the reference implementation, and
// with them parsed into `vuir::Program` the simulator can run the HANDWRITTEN
// program and a GENERATED one on the same input and diff the GS output. No
// Docker, no PCSX2, no console.
//
// It implements the vclpp layer (#include, #define, #macro/`Name{ args }`) plus
// VCL instruction syntax, with two of vclpp's documented limitations surfaced as
// notes rather than silently followed:
//   - #define expands only ONE level, so an alias-of-an-alias reaches dvp-as
//     unresolved (hence "kept as a LITERAL" in stapip_vu1_shared_defines.h);
//   - macros do not nest, so a `Name{ }` call inside a macro body reaches the
//     assembler verbatim (why the clip programs inline their emit sequences).
// Both land in `Program::notes`.
//
// Note on the "a ';' comment inside a #macro body makes vclpp swallow the whole
// expansion" warning in tyra_macros.i: it does not hold as a general rule -
// vcl_sml.i's VertexPersCorr carries a commented-out line and is used by every
// transforming program in the engine. Whatever the original incident was, it was
// narrower than the comment claims, so this parser simply strips comments and
// says nothing. Generated programs sidestep the question entirely: `vugen` emits
// no macros at all.
//
// No GL, no ImGui, no project.hpp - the aobake/livedbg shape.
#pragma once

#include <string>
#include <vector>

#include "vuir.hpp"

namespace vuasm {

struct Options {
    /** Root the #include paths are resolved against - the engine directory
     * (vendor/tyra/engine), since the sources include "src/..." and "inc/...". */
    std::string includeRoot;
    /** Extra -D style definitions, applied before the file's own. */
    std::vector<std::pair<std::string, std::string>> defines;
};

/** Parses one .vclpp (or plain .vcl) file. Returns false with `error` set on a
 * syntax error; non-fatal remarks land in `out.notes`. */
bool parseFile(const std::string& path, const Options& opt, vuir::Program& out,
               std::string& error);

/** Same, for source already in memory (`name` is used in diagnostics). */
bool parseText(const std::string& text, const std::string& name,
               const Options& opt, vuir::Program& out, std::string& error);

}  // namespace vuasm
