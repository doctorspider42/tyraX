#include "chargen_kit.hpp"

// The kit is several megabytes. cmake/embed_binary.cmake (the app icon, the
// moon map) turns a file into a C array literal, which costs a CMake regex
// over the whole hex dump and then g++ parsing millions of initializers - fine
// for 100 KB, minutes for this. The assembler's .incbin copies the bytes in
// directly. GCC and MinGW both understand it; CHARGEN_KIT_PATH comes from
// CMakeLists.txt, which also makes this file rebuild when the kit changes.
#ifndef CHARGEN_KIT_PATH
#error "CHARGEN_KIT_PATH must point at resources/chargen-kit.bin"
#endif

// Read-only data lives in .rdata on PE/COFF (MinGW) and .rodata on ELF. Both
// end with an explicit `.text`: COFF has no `.previous`.
#ifdef _WIN32
#define CHARGEN_KIT_SECTION ".section .rdata,\"dr\"\n"
#elif defined(__APPLE__)
#define CHARGEN_KIT_SECTION ".section __TEXT,__const\n"
#else
#define CHARGEN_KIT_SECTION ".section .rodata\n"
#endif

#define KIT_SYMBOL_IMPL(prefix, name) prefix##name
#define KIT_SYMBOL_JOIN(prefix, name) KIT_SYMBOL_IMPL(prefix, name)
#define KIT_STRING_IMPL(name) #name
#define KIT_STRING(name) KIT_STRING_IMPL(name)
#define KIT_SYMBOL(name) KIT_STRING(KIT_SYMBOL_JOIN(__USER_LABEL_PREFIX__, name))

__asm__(
    CHARGEN_KIT_SECTION
    ".balign 16\n"
    ".global " KIT_SYMBOL(tyrax_chargen_kit_begin) "\n"
    KIT_SYMBOL(tyrax_chargen_kit_begin) ":\n"
    ".incbin \"" CHARGEN_KIT_PATH "\"\n"
    ".global " KIT_SYMBOL(tyrax_chargen_kit_end) "\n"
    KIT_SYMBOL(tyrax_chargen_kit_end) ":\n"
    ".byte 0\n"
    ".text\n");

extern "C" const uint8_t tyrax_chargen_kit_begin[];
extern "C" const uint8_t tyrax_chargen_kit_end[];

namespace chargenkit {
const uint8_t* data() { return tyrax_chargen_kit_begin; }
size_t size() { return (size_t)(tyrax_chargen_kit_end - tyrax_chargen_kit_begin); }
}  // namespace chargenkit
