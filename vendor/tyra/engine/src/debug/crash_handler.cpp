/*
# TyraX addition: EE crash handler - see inc/debug/crash_handler.hpp.
*/

#include "debug/crash_handler.hpp"

#include <kernel.h>
#include <debug.h>  // init_scr / scr_printf - the crash screen
#include <ee_debug.h>
#include <ps2_debug.h>
#include <stdio.h>
#include <string.h>

// libeedebug's level-1 vector (ee_exceptions.S); what ee_dbg_install routes
// causes through, and what install() below points them at explicitly.
extern "C" void __ee_level1_ex_vector();

// The text window the backtrace scan trusts. At FILE scope on purpose: inside
// the anonymous namespace below, GCC gives even an extern "C" variable a
// mangled internal name, so the weak references these once were never met the
// linker's _ftext/_etext and read 0 - the fallback window took all of RAM and
// the backtrace filled up with stack addresses (seen on a PS2, 2026-11-08).
extern "C" {
extern u8 _ftext;
extern u8 _etext;
}

namespace Tyra {
namespace {

CrashInfo g_info;
bool g_crashed = false;
CrashHandler::Reporter g_reporter = nullptr;

// A fault RAISED BY THE REPORT ITSELF (stdio over SIF, the host: file write).
// Before this existed a second fault re-ran its faulting instruction forever
// (capture() ignored it and left EPC alone) - a silent hang that looks exactly
// like "the handler never ran". Now it is recorded and the EE goes straight to
// the screen, which needs no SIF.
struct SecondFault {
  u32 excCode, epc, badvaddr, ra;
};
SecondFault g_second;
volatile int g_faultCount = 0;

// The text window used to decide "does this stack word look like a return
// address". _ftext/_etext come from the ps2sdk link script (declared at file
// scope above).

inline u32 textStart() {
  const u32 a = (u32)&_ftext;
  return a ? a : 0x00100000U;
}
inline u32 textEnd() {
  const u32 a = (u32)&_etext;
  return a ? a : 0x02000000U;
}

void paintCrashScreen();

// Where a fault taken WHILE REPORTING returns to: the screen only, no SIF.
// A fault in the screen code itself (the third) parks the EE in deadLoop.
void deadLoop() {
  for (;;) SleepThread();
}
void secondFaultTrampoline() {
  paintCrashScreen();
  deadLoop();
}

// Where the exception returns to. Runs in ORDINARY context (the kernel's ERET
// cleared EXL), so stdio and the IOP-served host: filesystem are usable again.
void crashTrampoline() {
  // A line in the LOG first, synchronously: stdout reaches ps2client's console
  // (and the editor's Output) even when the report file below does not, and
  // it says the handler ran at all - the question a missing crash.txt leaves
  // open (docs/backlog.md, "devlink: what is left").
  printf("====ERR: TYRAX EE CRASH: %s (excCode %d) epc 0x%08x badvaddr 0x%08x "
         "ra 0x%08x - writing bin/crash.txt\n",
         g_info.name ? g_info.name : "unknown", (int)g_info.excCode,
         (unsigned int)g_info.epc, (unsigned int)g_info.badvaddr,
         (unsigned int)g_info.gpr[31]);
  fflush(stdout);

  // Then the report: crash.txt for the editor to symbolize.
  if (g_reporter) g_reporter(g_info);
  printf("====ERR: TYRAX EE CRASH: report %s\n",
         g_reporter ? "written" : "skipped - no reporter installed");
  fflush(stdout);

  // Then SEIZE THE SCREEN. A failed assertion may halt quietly and let the
  // editor surface it, because an assertion is a message. A CPU exception is
  // not: it is unrecoverable, and a frozen last frame is indistinguishable
  // from a hang - which is exactly how one of these cost an evening of
  // "why is my game stuck on the loading screen". So a crash always says so
  // on the TV, whether or not an editor is watching the console.
  // init_scr() is ps2sdk's kernel debug console, the same one the opt-in
  // assert screen uses; it needs no renderer state, which is the point when
  // the renderer is what died.
  // Paint ONCE, then idle the EE with SleepThread - the same halt a failed
  // assertion has always used. Looping the printf (what the upstream assert
  // screen does) would busy-wait the EE for as long as the crash is up, for no
  // benefit: the debug console's framebuffer holds the text by itself. Idling
  // instead leaves the other threads schedulable, and that is worth real money
  // over ps2link: measured on hardware, a console sitting on this crash screen
  // still answers, so the next deploy is a redeploy from the editor rather than
  // a walk over to the console to press Reset.
  paintCrashScreen();
  deadLoop();  // dead game, live console
}

void paintCrashScreen() {
  init_scr();
  {
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
    if (g_faultCount > 1) {
      scr_printf("  |\n");
      scr_printf("  | AND A SECOND FAULT WHILE REPORTING IT:\n");
      scr_printf("  | excCode %d epc 0x%08x badvaddr 0x%08x\n",
                 (int)g_second.excCode, (unsigned int)g_second.epc,
                 (unsigned int)g_second.badvaddr);
      scr_printf("  | (bin/crash.txt is probably missing)\n");
    }
    scr_printf("  ====================================\n");
  }
}

void capture(EE_RegFrame* f, int level) {
  const int n = ++g_faultCount;
  if (g_crashed) {
    // First crash wins the report; a later one only must not loop.
    if (n == 2) {
      g_second.excCode = (f->cause >> 2) & 0x1F;
      g_second.epc = f->epc;
      g_second.badvaddr = f->badvaddr;
      g_second.ra = f->ra[0];
      f->epc = (u32)&secondFaultTrampoline;
    } else {
      f->epc = (u32)&deadLoop;
    }
    return;
  }
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
  // The TLB causes (1..3) USED to be handed straight back, on the theory that a
  // TLB refill is how a mapped access is completed. Not on a PS2: there is no
  // demand paging - RAM, the uncached and UCAB windows and the scratchpad are
  // mapped by fixed entries at boot - so a refill means an access to nothing,
  // i.e. a null or wild pointer, the COMMONEST crash there is. ps2link has
  // always treated it as fatal (its own handler takes 1..3), and games run
  // under ps2link without ever tripping it. Handing them back meant exactly
  // the crash that most needs a report produced none: measured 2026-11-08 on a
  // PS2, a store through a wild pointer landed in ps2link's handler and no
  // bin/crash.txt was written (docs/backlog.md).
  //
  // So every cause this handler reports is routed to libeedebug's vector HERE,
  // explicitly, whether or not ee_dbg_install() did it: the script debugger
  // installs libeedebug too and hands every cause but 9 back to its previous
  // owner, and which of the two runs first must not decide whether crashes are
  // reported. Cause 9 (BREAK) is the script debugger's and is left alone.
  void* before5 = GetExceptionHandler(5);
  const int installed1 = ee_dbg_install(1);  // -1 = someone installed it first

  for (int cause = 1; cause <= 3; ++cause) {
    SetVTLBRefillHandler(cause, (void*)__ee_level1_ex_vector);
    ee_dbg_set_level1_handler(cause, level1Handler);
  }
  // Address errors, bus errors, reserved instruction, coprocessor unusable,
  // overflow, trap.
  static const int kFaults[] = {4, 5, 6, 7, 10, 11, 12, 13};
  for (int fi = 0; fi < (int)(sizeof(kFaults) / sizeof(kFaults[0])); ++fi) {
    const int cause = kFaults[fi];
    SetVCommonHandler(cause, (void*)__ee_level1_ex_vector);
    ee_dbg_set_level1_handler(cause, level1Handler);
  }
  FlushCache(0);  // the install's own flush ran before these writes
  FlushCache(2);
  // Whether the faults really come HERE: an install that returned -1 (someone
  // installed libeedebug first) or a vector that is still the previous owner's
  // leaves level1Handler registered and never called - which is how a crash
  // ended in ps2link's handler with no crash.txt (docs/backlog.md).
  printf("LOG: crash handler: ee_dbg_install(1) = %d, cause 5 vector %p -> %p, "
         "TLB store %p\n",
         installed1, before5, GetExceptionHandler(5), GetExceptionHandler(3));
  fflush(stdout);
}

}  // namespace Tyra
