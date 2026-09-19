#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// Human diagnostics for the raw MIPS exception line printed by ps2link.
// Pure host-side code: no ImGui, project model or process runner dependency,
// so the parser can be exercised without starting the editor.
namespace eeexception {

struct Diagnosis {
    bool valid = false;
    uint32_t cause = 0;
    uint32_t badvaddr = 0;
    uint32_t status = 0;
    uint32_t epc = 0;
    uint32_t excCode = 0;
    std::string code;         // stable, e.g. TXE-EE-0003
    std::string title;        // architectural exception name
    std::string description;  // human diagnosis; may be heuristic
    std::string rawLine;
    std::size_t lineOffset = 0;  // byte offset when parsed from a whole log
};

/** Decode already-separated register values. `unit` is "EE" or "IOP". */
Diagnosis diagnose(uint32_t cause, uint32_t badvaddr, uint32_t status,
                   uint32_t epc, const char* unit = "EE");

/** Parse one ps2link line. Accepts BadAddr/BadVAddr and optional colons or
 * editor timestamp/[ps2] prefixes. */
bool parseLine(const std::string& line, Diagnosis& out,
               const char* unit = "EE");

/** Find the newest raw exception line in a complete runner log. */
bool parseLast(const std::string& log, Diagnosis& out,
               const char* unit = "EE");

/** Copyable friendly summary followed by the unchanged raw values. */
std::string format(const Diagnosis& d);

}  // namespace eeexception
