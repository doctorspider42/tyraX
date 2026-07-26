/*
# TyraX addition: EE crash handler - see inc/debug/crash_handler.hpp.
*/

#include "debug/crash_handler.hpp"

#include <kernel.h>
#include <ee_debug.h>
#include <ps2_debug.h>
#include <string.h>

namespace Tyra {
namespace {

CrashInfo g_info;
bool g_crashed = false;
CrashHandler::Reporter g_reporter = nullptr;

// The text window used to decide "does this stack word look like a return
// address". _ftext/_etext come from the ps2sdk link script; the fallback covers
// the standard EE load address in case a future linkfile drops them.
extern "C" {
extern u8 _ftext __attribute__((weak));
extern u8 _etext __attribute__((weak));
}

inline u32 textStart() {
  const u32 a = (u32)&_ftext;
  return a ? a : 0x00100000U;
}
inline u32 textEnd() {
  const u32 a = (u32)&_etext;
  return a ? a : 0x02000000U;
}

// Where the exception returns to. Runs in ORDINARY context (the kernel's ERET
// cleared EXL), so stdio and the IOP-served host: filesystem are usable again.
void crashTrampoline() {
  if (g_reporter) g_reporter(g_info);
  // Dead game: idle the EE and leave the last frame on screen, exactly like a
  // failed TYRA_ASSERT does (the editor is what shows the dump).
  for (;;) SleepThread();
}

void capture(EE_RegFrame* f, int level) {
  if (g_crashed) return;  // first crash wins; a second is noise
  g_crashed = true;

  memset(&g_info, 0, sizeof(g_info));
  g_info.level = level;
  g_info.status = f->status;
  g_info.cause = f->cause;
  g_info.epc = f->epc;
  g_info.errorepc = f->errorepc;
  g_info.badvaddr = f->badvaddr;
  g_info.excCode = (f->cause >> 2) & 0x1F;
  g_info.name = CrashHandler::causeName(g_info.excCode);

  // The frame keeps each GPR as a 128-bit slot; the low word is what C code
  // deals in. zero..ra are laid out in register order (see ps2_debug.h).
  const u32* slots = (const u32*)&f->zero[0];
  for (int i = 0; i < 32; ++i) g_info.gpr[i] = slots[i * 4];

  // Backtrace: $ra first, then plausible code addresses on the stack. No frame
  // pointers on -O3 MIPS, so this is a scan, not a walk - the editor names them
  // and the real frames stand out.
  const u32 lo = textStart(), hi = textEnd();
  auto push = [&](u32 a) {
    if (a < lo || a >= hi || (a & 3)) return;
    for (int i = 0; i < g_info.traceCount; ++i)
      if (g_info.trace[i] == a) return;  // dedupe
    if (g_info.traceCount < CrashInfo::kMaxTrace)
      g_info.trace[g_info.traceCount++] = a;
  };
  push(g_info.gpr[31]);  // $ra
  const u32 sp = g_info.gpr[29];
  if (sp >= 0x00080000U && sp < 0x02000000U) {
    const u32* p = (const u32*)sp;
    for (int i = 0; i < 512 && g_info.traceCount < CrashInfo::kMaxTrace; ++i)
      push(p[i]);
  }

  // Resume in ordinary context instead of re-running the faulting instruction.
  f->epc = (u32)&crashTrampoline;
}

int level1Handler(EE_RegFrame* f) {
  capture(f, 1);
  return 0;
}
int level2Handler(EE_RegFrame* f) {
  capture(f, 2);
  return 0;
}

}  // namespace

const char* CrashHandler::causeName(u32 excCode) {
  switch (excCode) {
    case 0: return "Interrupt";
    case 1: return "TLB modified";
    case 2: return "TLB refill on load/fetch";
    case 3: return "TLB refill on store";
    case 4: return "Address error on load/fetch (bad pointer?)";
    case 5: return "Address error on store (bad pointer?)";
    case 6: return "Bus error on fetch";
    case 7: return "Bus error on load/store";
    case 8: return "Syscall";
    case 9: return "Breakpoint (BREAK)";
    case 10: return "Reserved instruction (corrupt code / bad call?)";
    case 11: return "Coprocessor unusable";
    case 12: return "Arithmetic overflow";
    case 13: return "Trap";
    case 15: return "Floating point exception";
    default: return "Unknown exception";
  }
}

const CrashInfo& CrashHandler::info() { return g_info; }
bool CrashHandler::crashed() { return g_crashed; }

void CrashHandler::install(Reporter reporter) {
  g_reporter = reporter;
  static bool installed = false;
  if (installed) return;
  installed = true;
  // Both levels: level 1 is the ordinary exception vector, level 2 the error
  // vector (NMI / cache error / a level-1 fault taken in exception mode).
  ee_dbg_install(3);
  // Hook the FAULT causes only. Two must be left to the kernel or the game
  // stops dead the moment the handlers go in - measured, not theorised:
  //   cause 0  = Interrupt. Hooking it hijacks vblank/timer/DMA dispatch, so
  //              threads never run again (the game freezes with the last frame
  //              up and nothing in the log - which is exactly how this bug
  //              first showed up).
  //   cause 8  = Syscall. Everything above the metal goes through it.
  // TLB refill (2, 3) and TLB modified (1) are left alone too: they are how a
  // TLB-mapped access is SERVICED, so a handler that redirects them turns
  // ordinary memory traffic into a crash.
  static const int kFaults[] = {4, 5, 6, 7, 9, 10, 11, 12, 13, 15};
  for (int fi = 0; fi < (int)(sizeof(kFaults) / sizeof(kFaults[0])); ++fi) {
    const int cause = kFaults[fi];
    ee_dbg_set_level1_handler(cause, level1Handler);
    ee_dbg_set_level2_handler(cause, level2Handler);
  }
}

}  // namespace Tyra
