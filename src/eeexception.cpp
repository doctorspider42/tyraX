#include "eeexception.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace eeexception {
namespace {

const char* titleFor(uint32_t code) {
    switch (code) {
        case 0: return "Interrupt";
        case 1: return "TLB modification";
        case 2: return "TLB load / instruction fetch";
        case 3: return "TLB store";
        case 4: return "Address error on load / instruction fetch";
        case 5: return "Address error on store";
        case 6: return "Bus error on instruction fetch";
        case 7: return "Bus error on data access";
        case 8: return "System call";
        case 9: return "Breakpoint";
        case 10: return "Reserved instruction";
        case 11: return "Coprocessor unusable";
        case 12: return "Arithmetic overflow";
        case 13: return "Trap";
        case 15: return "Floating-point exception";
        default: return "Unknown CPU exception";
    }
}

const char* descriptionFor(uint32_t code, uint32_t badvaddr) {
    const bool nearNull = badvaddr < 0x00010000U;
    switch (code) {
        case 0: return "Unexpected interrupt reached the crash handler.";
        case 1: return "A write targeted a read-only mapped memory page.";
        case 2:
            return nearNull ? "Likely null/near-null pointer read or call."
                            : "A read or instruction fetch targeted unmapped memory.";
        case 3:
            return nearNull ? "Likely null/near-null pointer write."
                            : "A write targeted unmapped memory.";
        case 4:
            return nearNull ? "Likely null/near-null pointer read or call."
                            : "Invalid or unaligned read/instruction fetch.";
        case 5:
            return nearNull ? "Likely null/near-null pointer write."
                            : "Invalid or unaligned write.";
        case 6: return "The CPU could not fetch an instruction from memory.";
        case 7: return "The CPU could not complete a data-memory access.";
        case 8: return "A system call reached the crash handler unexpectedly.";
        case 9: return "A breakpoint instruction stopped execution.";
        case 10:
            return "The CPU encountered an invalid or unsupported instruction.";
        case 11:
            return "An instruction used a disabled or unavailable coprocessor.";
        case 12: return "Signed integer arithmetic overflowed.";
        case 13: return "A trap condition stopped execution.";
        case 15: return "A floating-point operation raised an exception.";
        default: return "Unknown CPU exception; keep the raw register dump.";
    }
}

bool hexAfter(const std::string& line, const char* key, uint32_t& out) {
    const size_t found = line.find(key);
    if (found == std::string::npos) return false;
    size_t at = found + std::char_traits<char>::length(key);
    while (at < line.size() &&
           (line[at] == ':' || line[at] == '=' || line[at] == ' ' ||
            line[at] == '\t'))
        ++at;
    if (at + 2 <= line.size() && line[at] == '0' &&
        (line[at + 1] == 'x' || line[at + 1] == 'X'))
        at += 2;
    const char* first = line.c_str() + at;
    char* end = nullptr;
    const unsigned long value = std::strtoul(first, &end, 16);
    if (end == first) return false;
    out = static_cast<uint32_t>(value);
    return true;
}

}  // namespace

Diagnosis diagnose(uint32_t cause, uint32_t badvaddr, uint32_t status,
                   uint32_t epc, const char* unit) {
    Diagnosis d;
    d.valid = true;
    d.cause = cause;
    d.badvaddr = badvaddr;
    d.status = status;
    d.epc = epc;
    d.excCode = (cause >> 2U) & 0x1FU;
    char code[32];
    std::snprintf(code, sizeof(code), "TXE-%s-%04u", unit ? unit : "EE",
                  static_cast<unsigned>(d.excCode));
    d.code = code;
    d.title = titleFor(d.excCode);
    d.description = descriptionFor(d.excCode, badvaddr);
    return d;
}

bool parseLine(const std::string& line, Diagnosis& out, const char* unit) {
    uint32_t cause = 0, badvaddr = 0, status = 0, epc = 0;
    if (!hexAfter(line, "Cause", cause)) return false;
    if (!hexAfter(line, "BadVAddr", badvaddr) &&
        !hexAfter(line, "BadAddr", badvaddr))
        return false;
    if (!hexAfter(line, "Status", status) || !hexAfter(line, "EPC", epc))
        return false;
    out = diagnose(cause, badvaddr, status, epc, unit);
    out.rawLine = line;
    return true;
}

bool parseLast(const std::string& log, Diagnosis& out, const char* unit) {
    size_t end = log.size();
    while (end > 0) {
        size_t start = log.rfind('\n', end - 1);
        start = start == std::string::npos ? 0 : start + 1;
        std::string line = log.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.find("Cause") != std::string::npos) {
            const size_t eeNew = log.rfind("TYRAX EE CRASH", start);
            const size_t eeOld = log.rfind("EE Exception handler", start);
            const size_t iopNew = log.rfind("TYRAX IOP CRASH", start);
            const size_t iopOld = log.rfind("IOP Exception handler", start);
            const size_t ee =
                eeNew == std::string::npos
                    ? eeOld
                    : (eeOld == std::string::npos ? eeNew
                                                  : std::max(eeNew, eeOld));
            const size_t iop = iopNew == std::string::npos
                                   ? iopOld
                                   : (iopOld == std::string::npos
                                          ? iopNew
                                          : std::max(iopNew, iopOld));
            const char* inferred =
                iop != std::string::npos &&
                        (ee == std::string::npos || iop > ee)
                    ? "IOP"
                    : unit;
            if (parseLine(line, out, inferred)) {
                out.lineOffset = start;
                return true;
            }
        }
        if (start == 0) break;
        end = start - 1;
    }
    return false;
}

std::string format(const Diagnosis& d) {
    if (!d.valid) return {};
    char raw[160];
    std::snprintf(raw, sizeof(raw),
                  "Cause %08X  BadVAddr %08X  Status %08X  EPC %08X",
                  static_cast<unsigned>(d.cause),
                  static_cast<unsigned>(d.badvaddr),
                  static_cast<unsigned>(d.status),
                  static_cast<unsigned>(d.epc));
    return d.code + ": " + d.title + "\n" + d.description +
           "\nFault address 0x" + [&] {
               char hex[9];
               std::snprintf(hex, sizeof(hex), "%08X",
                             static_cast<unsigned>(d.badvaddr));
               return std::string(hex);
           }() +
           ", instruction 0x" + [&] {
               char hex[9];
               std::snprintf(hex, sizeof(hex), "%08X",
                             static_cast<unsigned>(d.epc));
               return std::string(hex);
           }() +
           "\n\n" + raw;
}

}  // namespace eeexception
