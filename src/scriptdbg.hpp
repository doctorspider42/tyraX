// Script debugger - the mailbox the game and the editor's GDB server share.
//
// A debug build with the script debugger on carries a BREAK handler
// (src/gen/script_debug.gen.cpp, generated from templates::scriptDebugSource).
// When the game hits a `break` it publishes its register frame here and spins
// until the host says go. The host reaches this block over PCSX2's PINE socket,
// which can only read and write guest RAM - so this layout IS the protocol.
// The generated runtime spells every offset out of the constants below; change
// one and both sides move together. See docs/script-debugger.md.
//
// No GL, no ImGui, no project.hpp.
#pragma once

#include <cstdint>
#include <string>

namespace scriptdbg {

/** The mailbox's symbol in the unstripped ELF (bin/<name>.elf.sym). */
inline constexpr const char* kMailboxSymbol = "g_tyraxScriptDbg";
inline constexpr uint32_t kMagic = 0x42444754u;  // "TGDB"
inline constexpr uint32_t kVersion = 1;

// Mailbox offsets (bytes).
inline constexpr uint32_t kOffMagic = 0x00;
inline constexpr uint32_t kOffVersion = 0x04;
inline constexpr uint32_t kOffState = 0x08;      // 0 running, 1 stopped
inline constexpr uint32_t kOffResume = 0x0C;     // host writes 1: continue
inline constexpr uint32_t kOffFrame = 0x10;      // EE_RegFrame* of the stop
inline constexpr uint32_t kOffStops = 0x14;      // bumped on every stop
inline constexpr uint32_t kOffFcr31 = 0x18;
inline constexpr uint32_t kOffPause = 0x1C;      // host writes 1: stop at the next poll
inline constexpr uint32_t kOffInstalled = 0x20;  // the game routed BREAK to us
inline constexpr uint32_t kOffPatchCount = 0x24;
inline constexpr uint32_t kOffFpr = 0x30;        // 32 x u32
inline constexpr uint32_t kOffPatches = 0xB0;    // kMaxPatches x {addr, value}
inline constexpr uint32_t kMaxPatches = 64;
inline constexpr uint32_t kMailboxSize = kOffPatches + kMaxPatches * 8;

// ps2sdk's EE_RegFrame (common/include/ps2_debug.h): 32 quadword GPRs, then
// the CP0 / HI / LO words.
inline constexpr uint32_t kFrameGpr = 0;  // + n * 16, low 64 bits meaningful
inline constexpr uint32_t kFrameStatus = 512;
inline constexpr uint32_t kFrameCause = 516;
inline constexpr uint32_t kFrameEpc = 520;
inline constexpr uint32_t kFrameBadVAddr = 528;
inline constexpr uint32_t kFrameHi = 532;
inline constexpr uint32_t kFrameLo = 540;

/** The trap the host plants (a plain `break`). */
inline constexpr uint32_t kBreak = 0x0000000Du;
/** The pause poll's own trap (`break 7`); the handler steps over it. */
inline constexpr uint32_t kPollBreak = 0x0007000Du;

/** "C:\a\b" -> "/mnt/c/a/b": a Windows path as WSL's default automount sees it.
 * The native build compiles from those paths (tools/toolchain/native-build.ps1
 * asks `wslpath`, which gives the same answer under the default mount root), so
 * they are what the game's DWARF names and what GDB in WSL can open. A path
 * that is not drive-absolute comes back with only its separators flipped. */
inline std::string wslPath(const std::string& win) {
    std::string out;
    size_t i = 0;
    if (win.size() >= 2 && win[1] == ':' &&
        ((win[0] >= 'A' && win[0] <= 'Z') || (win[0] >= 'a' && win[0] <= 'z'))) {
        out = "/mnt/";
        out += (char)(win[0] | 0x20);
        i = 2;
    }
    for (; i < win.size(); ++i) out += win[i] == '\\' ? '/' : win[i];
    return out;
}

}  // namespace scriptdbg
