// Minimal ELF32 reader for the PS2 EE binaries this editor builds.
//
// Two jobs, both of them devkit infrastructure (docs/devkit.md):
//
//  1. **The release audit.** The debugging features (Live Link / Live Debugger /
//     Live Logic) must cost a shipped game NOTHING - not a byte of code, not a
//     static array, not a string. That is a claim, and a claim needs a check:
//     `auditRelease()` reads the built ELF and reports any devkit symbol or
//     string that survived, plus the section sizes so the cost is a number, not
//     an opinion. `tyrax-editor --audit-release <projectDir>` runs it, and the
//     Runner runs it automatically after every release build.
//
//  2. **Named memory.** The symbol table maps names to addresses, which is what
//     turns "read 0x001d4a20" into "watch `g_frameDt`" in the editor.
//
// No project.hpp, no GL, no ImGui - pure bytes in, structs out.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace elfsym {

struct Symbol {
    std::string name;
    uint32_t addr = 0;
    uint32_t size = 0;
    unsigned char type = 0;  // STT_OBJECT (1) / STT_FUNC (2) / ...
    bool object() const { return type == 1; }
    bool function() const { return type == 2; }
};

struct Section {
    std::string name;
    uint32_t addr = 0;
    uint32_t size = 0;
    uint32_t offset = 0;
    uint32_t type = 0;
};

/** A parsed ELF. `loaded` is false when the file is missing or not an ELF32
 * little-endian image (which is all the PS2 toolchain produces). */
struct Image {
    bool loaded = false;
    std::string error;
    size_t fileSize = 0;
    uint32_t entry = 0;
    std::vector<Section> sections;
    std::vector<Symbol> symbols;  // empty in a stripped ELF

    const Section* section(const std::string& name) const;
    const Symbol* symbol(const std::string& name) const;
    /** Symbols whose name contains `needle` (case-sensitive substring). */
    std::vector<const Symbol*> symbolsContaining(const std::string& needle) const;
    /** Total size of the symbols whose name contains `needle` - the honest
     * answer to "how many bytes is this feature costing me". */
    uint32_t sizeOfSymbolsContaining(const std::string& needle) const;
};

Image read(const std::string& elfPath);

/** Reads `len` bytes of a section's file image (for scanning .rodata strings). */
std::vector<unsigned char> sectionBytes(const std::string& elfPath,
                                        const Section& s);

// ------------------------------------------------------------ release audit ---

struct AuditFinding {
    std::string what;   // the symbol or string that should not be here
    std::string where;  // "symbol" / "string in .rodata" ...
    uint32_t bytes = 0;
};

struct Audit {
    bool ran = false;
    bool clean = true;              // no devkit trace at all
    std::string error;              // set when the ELF could not be read
    size_t elfBytes = 0;
    uint32_t textBytes = 0, dataBytes = 0, bssBytes = 0, rodataBytes = 0;
    int symbolCount = 0;
    std::vector<AuditFinding> findings;
    /** One-line verdict for a log ("Release audit: clean - ..."). */
    std::string summary() const;
};

/** Scans a built ELF for anything the debugging layers would have left behind.
 * Looks for the generated runtimes' symbols (livedbg / livelogic / LiveLink /
 * LiveLogic / LiveDebug), their file names in the string data, and the
 * interpreter's static tables. A release ELF must come back clean. */
Audit auditRelease(const std::string& elfPath);

// ------------------------------------------------------------ symbolization ---

/** One resolved address. `func`/`source` are empty when the toolchain could not
 * name it (a stripped ELF, an address outside the code, no container running). */
struct Location {
    uint32_t addr = 0;
    std::string func;    // demangled function name
    std::string source;  // file:line
};

/** Turns addresses from a crash report into names, using the PS2 toolchain in
 * the project's build container (`mips64r5900el-ps2-elf-addr2line`) against the
 * UNSTRIPPED copy the debug build keeps next to the ELF (bin/<name>.elf.sym -
 * Makefile.base writes it when KEEPSYM=1). The shipped ELF is stripped, which
 * is why the copy exists at all.
 *
 * Returns one Location per input address (empty fields on failure) and, when
 * something went wrong, a human-readable reason in `error`. */
std::vector<Location> symbolize(const std::string& projectDir,
                                const std::string& symElfBinRelative,
                                const std::vector<uint32_t>& addrs,
                                std::string* error = nullptr);

}  // namespace elfsym
