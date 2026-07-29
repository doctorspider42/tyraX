/*
# TyraX addition: EE crash handler - see inc/debug/crash_handler.hpp.
*/

#include "debug/crash_handler.hpp"

#include <kernel.h>
#include <debug.h>  // init_scr / scr_printf - the crash screen
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
  // The report goes out FIRST, while host: is still untouched: crash.txt for
  // the editor to symbolize, and the same delimited banner in the log.
  if (g_reporter) g_reporter(g_info);

  // Then SEIZE THE SCREEN. A failed assertion may halt quietly and let the
  // editor surface it, because an assertion is a message. A CPU exception is
  // not: it is unrecoverable, and a frozen last frame is indistinguishable
  // from a hang - which is exactly how one of these cost an evening of
  // "why is my game stuck on the loading screen". So a crash always says so
  // on the TV, whether or not an editor is watching the console.
  // init_scr() is ps2sdk's kernel debug console, the same one the opt-in
  // assert screen uses; it needs no renderer state, which is the point when
  // the renderer is what died.
  init_scr();
  for (;;) {
    scr_setXY(0, 2);
    scr_printf("  ==============  TYRAX  =============\n");
    scr_printf("  |\n");
    scr_printf("  | EE CRASH - the game is dead.\n");
    scr_printf("  |\n");
    scr_printf("  | %s\n", g_info.name ? g_info.name : "unknown");
    scr_printf("  | excCode  %d (level %d)\n", (int)g_info.excCode,
               g_info.level);
    scr_printf("  | epc      0x%08x\n", (unsigned int)g_info.epc);
    scr_printf("  | badvaddr 0x%08x\n", (unsigned int)g_info.badvaddr);
    scr_printf("  | ra       0x%08x\n", (unsigned int)g_info.gpr[31]);
    scr_printf("  | sp       0x%08x\n", (unsigned int)g_info.gpr[29]);
    scr_printf("  |\n");
    scr_printf("  | Full report + backtrace: bin/crash.txt\n");
    scr_printf("  | Names for those addresses:\n");
    scr_printf("  |   tyrax-editor --symbolize <project> 0x%08x\n",
               (unsigned int)g_info.epc);
    scr_printf("  ====================================\n");
  }
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
  // LEVEL 1 ONLY. ee_dbg_install(2) - the error-level vector at 0x80000100 -
  // NEVER RETURNS: it drops interrupts, rewrites that vector under the running
  // machine, and the EE never comes back. That is the whole bug this feature
  // shipped with: the game froze on whatever frame was up, with nothing in the
  // log and no report, on real hardware and in PCSX2 alike (install(1) on the
  // same boot returns fine - measured, both ways round). Nothing is lost by
  // skipping level 2: it is the NMI / cache-error vector, while every crash
  // this feature promises to report - address error, bus error, reserved
  // instruction, overflow, trap - arrives on level 1.
  // What the install actually TAKES is not up to the table below: causes 1..3
  // (TLB modified, TLB refill on load, on store) go through SetVTLBRefillHandler
  // and 4..7 + 10..13 through SetVCommonHandler, whatever we register. And a
  // hooked cause with NO handler is a latent hang, because libeedebug's vector
  // never chains to the kernel handler it saved (that copy exists only so
  // ee_dbg_remove can put it back): the dispatcher finds nothing, the vector
  // ERETs, and the faulting instruction re-runs with nothing serviced. Harmless
  // for a fault we do handle; for a TLB REFILL - which is how a mapped access is
  // COMPLETED - it spins in the vector forever.
  // So the TLB causes are handed straight back to the kernel. They keep their
  // stock behaviour, which is the one thing this feature must not change.
  void* tlbVector[4] = {nullptr, nullptr, nullptr, nullptr};
  for (int cause = 1; cause <= 3; ++cause)
    tlbVector[cause] = GetExceptionHandler(cause);

  ee_dbg_install(1);

  for (int cause = 1; cause <= 3; ++cause)
    SetVTLBRefillHandler(cause, tlbVector[cause]);
  FlushCache(0);  // the install's own flush ran before these writes
  FlushCache(2);

  // The genuine faults, all of them ones the install routes here. Cause 9
  // (BREAK) and 15 (FP exception) are listed for completeness but ps2sdk does
  // not route them today, so they never fire - which is also why a wild pointer
  // into UNMAPPED memory is not reported: that is a TLB refill, given back
  // above. What IS reported is what the header promises: address errors, bus
  // errors, reserved instructions, coprocessor-unusable, overflow and traps.
  static const int kFaults[] = {4, 5, 6, 7, 9, 10, 11, 12, 13, 15};
  for (int fi = 0; fi < (int)(sizeof(kFaults) / sizeof(kFaults[0])); ++fi) {
    const int cause = kFaults[fi];
    ee_dbg_set_level1_handler(cause, level1Handler);
  }
}

}  // namespace Tyra
