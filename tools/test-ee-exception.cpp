#include "eeexception.hpp"

#include <cstdio>
#include <iostream>
#include <string>

namespace {
int failures = 0;

void expect(bool yes, const char* what) {
    if (yes) return;
    std::cerr << "FAIL: " << what << '\n';
    ++failures;
}
}  // namespace

int main() {
    eeexception::Diagnosis d;
    expect(eeexception::parseLine(
               "Cause 7000800C BadAddr 00000000 Status 70030C13 EPC 001D2874",
               d),
           "space-separated ps2link line parses");
    expect(d.code == "TXE-EE-0003", "TLB store has stable code");
    expect(d.title == "TLB store", "TLB store has an architectural title");
    expect(d.description.find("null") != std::string::npos,
           "small TLB-store address diagnoses a null write");
    expect(d.epc == 0x001D2874U && d.badvaddr == 0,
           "EPC and fault address survive parsing");

    expect(eeexception::parseLine(
               "[ps2] Cause:00000028 BadVAddr:81234567 Status:00000000 "
               "EPC:00123456",
               d),
           "colon-separated line with prefix parses");
    expect(d.code == "TXE-EE-0010" && d.title == "Reserved instruction",
           "reserved instruction is decoded");
    expect(d.description.find("invalid") != std::string::npos,
           "reserved instruction explains the likely class of failure");

    d = eeexception::diagnose(12U << 2U, 0, 0, 0);
    expect(d.code == "TXE-EE-0012" &&
               d.description.find("overflow") != std::string::npos,
           "arithmetic overflow is decoded");

    d = eeexception::diagnose(3U << 2U, 0x81234567U, 0, 0);
    expect(d.description.find("unmapped") != std::string::npos &&
               d.description.find("null") == std::string::npos,
           "far TLB-store address is not mislabeled as null");

    const std::string iopLog =
        "TYRAX IOP CRASH [TXE-IOP-0005]\n"
        "Cause 00000014 BadVAddr 00000004 Status 00000000 EPC 00001234\n";
    expect(eeexception::parseLast(iopLog, d), "latest IOP line parses");
    expect(d.code == "TXE-IOP-0005", "nearby IOP header selects IOP namespace");
    expect(d.lineOffset == iopLog.find("Cause"),
           "whole-log parsing preserves the occurrence offset");

    for (uint32_t code = 0; code < 32; ++code) {
        d = eeexception::diagnose(code << 2U, 0x10000U, 0, 0);
        char expected[32];
        std::snprintf(expected, sizeof(expected), "TXE-EE-%04u",
                      static_cast<unsigned>(code));
        expect(d.code == expected, "every ExcCode has a deterministic ID");
        expect(!d.title.empty() && !d.description.empty(),
               "every ExcCode has non-empty fallback prose");
    }

    if (failures) return 1;
    std::cout << "EE exception diagnostics: all checks passed\n";
    return 0;
}
