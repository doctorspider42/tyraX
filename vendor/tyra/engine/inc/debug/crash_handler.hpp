/*
# TyraX addition: EE crash handler.
#
# A real CPU exception (a bad pointer, an address error, a reserved
# instruction, an overflow, a BREAK) is not a TYRA_ASSERT: nothing prints it,
# nothing halts cleanly, and the game simply stops - on hardware, in silence.
# This turns that into a report.
#
# The mechanics use ps2sdk's libeedebug (`ee_dbg_install` +
# `ee_dbg_set_level1_handler`), which hands a C handler the whole register
# frame (`EE_RegFrame`), so no hand-written exception stub is needed. The
# handler itself does almost nothing: it copies the frame into a static
# CrashInfo, harvests plausible return addresses off the stack for a
# backtrace, then REDIRECTS the frame's EPC at a trampoline and returns - so
# execution resumes in ordinary, non-exception context, where the reporter
# callback may safely use stdio (writing to the host: filesystem) before the
# game halts. Doing file I/O inside the exception context instead is the
# classic way to turn a crash into a hang.
#
# This TU is only linked when someone CALLS install(): libtyra.a is an
# archive, so a game that never asks for the crash handler (a release build -
# the editor's devkit layer is what installs it) does not carry a byte of it.
# See docs/devkit.md in the editor repo.
*/
#pragma once

#include <tamtypes.h>

namespace Tyra {

struct CrashInfo {
  // What happened
  u32 excCode;       // Cause.ExcCode (0..31)
  const char* name;  // decoded, e.g. "Address error on store"
  int level;         // exception level that fired (1 = normal, 2 = error)

  // Where
  u32 epc;       // the faulting instruction
  u32 badvaddr;  // the address it touched (for TLB / address errors)
  u32 status;
  u32 cause;
  u32 errorepc;

  // Registers, low 32 bits of each (the frame keeps 128-bit slots)
  u32 gpr[32];

  // Poor-man's backtrace: $ra plus every word on the stack that looks like a
  // code address, nearest frame first. The editor symbolizes them; the wrong
  // guesses are obvious once they have names.
  static const int kMaxTrace = 24;
  u32 trace[kMaxTrace];
  int traceCount;
};

class CrashHandler {
 public:
  /** Called once, from ordinary context, after the crash is captured. Use it
   * to write a report; the game halts as soon as it returns. */
  typedef void (*Reporter)(const CrashInfo& info);

  /** Installs the level-1 (and level-2) handlers. Safe to call once at boot;
   * calling it again replaces the reporter. */
  static void install(Reporter reporter);

  /** The captured crash, valid once a crash happened (see crashed()). */
  static const CrashInfo& info();
  static bool crashed();

  /** Name for a Cause.ExcCode, for callers that format their own text. */
  static const char* causeName(u32 excCode);
};

}  // namespace Tyra
