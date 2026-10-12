# troubleshoot/tyra-engine-dev-debug.md

Debugging a GS pass you cannot see (the one-probe discipline), the quiet-halt assert,
and the EE crash handler. Symptom first.

## Debugging a GS pass you cannot see: one probe, one question

**Cause:** a multi-stage GS pipeline (build a mask -> resolve it -> gate a later pass on
it) fails silently, and reasoning about which stage is at fault is what costs the days.
This found three separate faults in one evening (the 16-bit FBA regression, the green
count-band marks, and a torch that lit no walls), and every wrong turn came from a probe
that answered a DIFFERENT question than the one being asked.
**Fix:** make each stage VISIBLE, one build at a time, and make each probe answer exactly
one question:
1. **Is the CONSUMER gated at all?** Force the mask to its extreme — have the resolve
   paint the "shadow" value over its whole rect. If the gated pass disappears, the gate
   works and the fault is upstream. (Do not force the ALPHA TEST instead: that changes
   which fragments are written, not what value they write.)
2. **What does the intermediate buffer actually hold?** Drop the write mask for one build
   (`FBMSK = 0`) so the resolve paints the buffer's texels into the visible frame as
   COLOUR. A count buffer written with N = 32 shows up as (32,32,32) — a screenshot plus a
   five-line histogram then tells you both the VALUE and its SHAPE.
3. **Which inputs reached the stage?** One `TYRA_LOG` per frame-group in the generated
   game (take ownership of `src/terrain_game.cpp` by deleting its marker line first)
   beats any amount of reading: `recvN=2, recv[0] obj=1 sliceVerts=3999, recv[1] obj=4
   sliceVerts=0` named a shared-budget bug in one line.
4. **A/B the whole feature.** Build the same scene with the feature's switch off and diff
   the frames: "0 pixels changed" is the fastest proof that a pass contributes nothing.
Rules the same evening paid for: **A probe that skips a pass also skips whatever that pass
restores** — the raster restore rides in the same packet, so the rest of the frame then
draws somewhere else. **Forcing a test to pass is not forcing a value to be written** —
with `TFX = DECAL` the fragment's alpha comes from the TEXEL (through `TEXA`), so an
all-zero texel still writes zero however permissive the test is. **Instrument OUTSIDE the
loop you are perturbing**, and log once every N frames — a `TYRA_LOG` per frame over
`host:` is network I/O that changes the timing you are measuring. **Revert the engine
probes before anything else** when you are done: they live in `vendor/tyra`, shared by
every project on the machine, and a forgotten `FBMSK = 0` looks exactly like a new
rendering bug to whoever builds next (it was reported back as "jakieś pojebane rzeczy się
dzieją").

## `TYRA_LOG` wedged every ISO boot

**Cause:** it opened `cdrom0:LOG.TXT` for write.
**Fix:** a guard in `debug.cpp` so `TYRA_LOG` never opens `cdrom0:LOG.TXT` for write.

## A failed assert seized the whole screen

**Cause:** upstream's `TYRA_ASSERT` / `TYRA_TRAP` ran `init_scr()` + an infinite
`scr_printf` loop.
**Fix:** a **quiet-halt assert** (`debug/debug.hpp` `TyraDebug::trap`): it still prints the
dump to the console / host `log.txt` (with the stable `======= TYRA =======` …
`================` delimiters the editor parses), then `for(;;) SleepThread()` so the last
frame stays up and the editor's error dialog surfaces it. The full-screen dump is gated
behind `Tyra::Info::drawAssertScreen` (default **off**, `info.{hpp,cpp}`) for standalone
hardware debugging — nothing wires it on in generated games. The engine's error blocks now
print `==============  TYRAX  =============` (`inc/debug/debug.hpp`, two places); the
editor parses that and the old TYRA banner both. `TYRA_SOFT_ERROR` (the non-fatal variant)
is documented in `tyra-engine-dev-files.md`.

## The EE crash handler and its vector traps

**Cause:** a TyraX addition (`src/debug/crash_handler.cpp` / `crash_handler.hpp`, docs/devkit.md in the editor
repo) turns a real CPU exception into a report instead of a silent freeze. Built on ps2sdk's **libeedebug**
(`ee_dbg_install` + `ee_dbg_set_level1/2_handler`), which hands a C handler the whole
`EE_RegFrame` — so there is no hand-written exception stub to maintain.
**Fix — three properties to preserve:**
- **It lives in its own TU and is only linked when someone calls `CrashHandler::install()`.**
  `libtyra.a` is an archive, so a game that never installs it (any release build — the
  editor's devkit layer is what installs) carries zero bytes. Do not add a global
  constructor or a reference from other engine code.
- **The handler does the minimum and gets OUT of exception context**: copy the frame, scan
  the stack for plausible return addresses, then set `frame->epc` to a trampoline and
  return. The report is written from the trampoline, where stdio and the IOP-served host:
  filesystem work again. File I/O inside the exception context is the classic way to turn a
  crash into a hang.
- **Install LEVEL 1 ONLY.** `ee_dbg_install(2)` never returns: it drops interrupts and
  rewrites the error-level vector at 0x80000100 under the running machine — that froze every
  debug build that turned the feature on, hardware and PCSX2 alike, with nothing in the log.
  `ee_dbg_install(1)` returns fine on the same boot; level 2 is only NMI / cache error.
- **What the install hooks is not up to your handler table**, and a hooked cause with no
  handler is an infinite exception loop. From disassembling `libeedebug.a` (the image ships
  no sources): `ee_dbg_install(1)` routes causes **1..3** through `SetVTLBRefillHandler` and
  **4..7 + 10..13** through `SetVCommonHandler` regardless of what you register, and its
  vector **always ERETs** — it never chains to the kernel handler it saved (that copy is
  only for `ee_dbg_remove`). So an unhandled
  hooked cause returns to the faulting instruction with nothing serviced; fatal for a TLB
  refill. The handler therefore hands causes 1..3 straight back with `SetVTLBRefillHandler`
  after installing. Causes 0 (Interrupt), 8 (Syscall), 9, 14 and 15 are never routed by the
  install at all — which is why narrowing the registered list alone never fixed anything.
  (`ee_dbg_set_level2_handler` also bounds-checks `cause < 4`.)
- **A crash takes the screen** (`init_scr`/`scr_printf`) before idling — an assertion may
  halt quietly, an exception may not, because a frozen last frame is indistinguishable from
  a hang.

## Catching is a hardware-only test

**Cause:** **PCSX2 cannot *produce* an EE exception at all** — a forced overflow, an illegal
opcode, a write to address 0 (main RAM starts there) and a misaligned load all pass through
unharmed — so catching is a hardware-only test, even though the old install hang reproduced
in both.
**Fix:** verified on hardware (2026-07-29): a forced signed-overflow `add` produced
`CRASH: Arithmetic overflow`, `bin/crash.txt` and a `--symbolize` hit on the exact source
line.

## The VU1 packet tap is a devkit-only hook

**Cause:** the engine carries a deliberately tiny tap
(`renderer/3d/pipeline/static/core/stapip_vu_tap.{hpp,cpp}`) — the null function pointer
`Tyra::g_vuPacketHook` (plus `g_vuMemHook`, called after the send with all of VU1 data
memory) and the branch testing it in `StaPipQBufferRenderer::sendPacket()`, once per bag
flush, never per vertex.
**Fix:** the capture itself lives in the generated game's devkit TU under `#ifndef
NDEBUG`; a release build links none of it. Walk chains by the REF rule in
`tyra-engine-dev-vu1.md`; the free-RAM probe is in `tyra-engine-dev-measurement.md`.
