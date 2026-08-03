# IOP compute — running your own code on the PS2's other CPU

Every PlayStation 2 contains a second general-purpose processor that has nothing
to do with graphics: the **IOP**, an R3000A. It is there so a PS2 can be a PS1 —
in a fat console it is *literally* the PlayStation's CPU, clocked up to 36.864
MHz; in the late slims (SCPH-75000 and later) it is a "Deckard" PowerPC running
an R3000A emulation. Sony's documentation is clear that it is an I/O processor:
it belongs to the pad, the memory card, the disc and the sound chip, and game
code has no business on it.

This feature is the "and yet". With it on, a project builds **its own IRX
module** from **its own C** and the EE hands that module integer work over SIF
RPC while it gets on with the frame.

It is off by default, and everything below is about the one question that
matters: **when is this a good idea, and how do you know the console you are on
can actually do it.**

## The short version

- Turn on *Project > Preferences > Build > **IOP compute***.
- Write a function in **`iop/user_jobs.c`** and add it to the table at the
  bottom of that file.
- Call it — from a script (`txiop::call` / `txiop::submit` + `txiop::poll`) or
  from the **Run IOP Job** flow node.
- **Always check `txiop::available()` first** (or wire the **IOP Available**
  node). It is false whenever the IOP cannot be trusted, and every entry point
  is a no-op in that state — so the same code runs correctly on hardware that
  cannot help, it just runs on the EE.

## Why there is no "am I a fat PS2?" check

The honest answer is that console identity is not readable. `rom0:ROMVER` tells
you which **BIOS** is loaded, which under PCSX2 says nothing whatsoever about
whether the IOP is a real R3000A or an emulation — an emulator with a v2.30 BIOS
image reports "Deckard era" while running a perfectly native IOP core. Gating a
feature on that would be a guess dressed up as a fact, and the cost of guessing
wrong is gameplay work running on the wrong CPU.

So the gate is a **measurement**, performed once at boot by `txiop::init()`:

1. Load the module out of the game's own ELF.
2. Bind its SIF RPC server, with a bounded number of attempts — "there is no IOP
   compute" must be an *answer*, never a hang.
3. Make the IOP compute a known integer hash and check the result **against the
   EE computing the same hash**.

`txiop::available()` is true only if all three succeeded. The hash check is the
strong one: it proves the IOP ran *our* code and got it *right*.

Alongside it, `txiop::info()` reports what was found — free IOP memory, the IOP
memory-control register, the measured round-trip cost, and the calibration time
on both CPUs. Those are **numbers for your game to reason about**, not gates.
A project that only wants the IOP when it is genuinely fast can compare
`info().ratio` itself.

## The numbers

Measured in PCSX2 on a 20 000-iteration integer hash. Read them as orders of
magnitude, not as hardware truth — see the next section.

| | measured | what it means for a design |
|---|---|---|
| One RPC round trip | **~110 µs** on a bare IOP, **145-200 µs** in a real game (four runs) | A 50 Hz frame is 20 ms, so one call is around 1% of it *before the job runs*. A job that takes 10 µs is slower here than on the EE. It varies run to run because the IOP is servicing other things - treat it as a floor, not a constant. |
| Same integer loop, IOP vs EE | **~6.2-6.4× slower** | The IOP is not a second fast core. It is a slow core that is otherwise idle. |
| Free IOP RAM | **1.68 MB** with nothing else loaded, **1.40 MB** in a real game | The difference IS audsrv, padman, sio2man and the `host:` filesystem. Enough for real working sets (a 128×128 A\* needs ~180 KB), but the IOP is not yours alone. |

The shape that pays off: **chunky, integer, and latency-tolerant.** Work whose
answer can arrive a few frames later, and whose input and output are small.
Pathfinding is the canonical fit. Anything per-object per-frame is not, and
anything with a `float` in it is not — **the IOP has no FPU**, so floating point
there is a software-emulated crawl. Pass fixed point.

## What PCSX2 can and cannot tell you

PCSX2 emulates a full IOP, so **the whole functional story is testable in the
emulator**: the module loads, the RPC binds, jobs run, results come back
correct, the availability gate reports true and false in the right situations.
Every claim in this document about *behaviour* was verified there.

Three things need a real console, and no amount of emulator testing substitutes:

1. **The real performance ratio.** PCSX2's IOP timing is not cycle-accurate. On
   hardware the clocks are 294.912 MHz (EE) against 36.864 MHz (IOP).
2. **Contention.** On a console the IOP really is streaming audio through
   audsrv and really is serving `host:` in a dev build. Whether a long job
   makes audio crackle is a hardware question. This is why the module's worker
   thread runs at a deliberately **low priority** — a compute thread that
   outranks the pad and the sound buys frame rate and pays for it in artefacts.
3. **The Deckard branch.** PCSX2 emulates a pre-Deckard IOP, so "this is a fat"
   is checkable there and "this is a Deckard slim" is not. Custom IRX modules do
   load on slims, but their throughput is a different question that only a slim
   answers.

## The trap that will cost you an afternoon

The PS2 BIOS's loadfile RPC **refuses to load a module from a buffer** until
`sbv_patch_enable_lmb()` has been applied. Without it, `SifExecModuleBuffer`
allocates the IOP-side buffer, returns a value that looks exactly like a module
id, sets the module result to 0 — and **loads nothing.** No error, no log line,
and the only symptom is an RPC that never binds.

The Tyra engine already applies that patch in `IrxLoader::applyRpcPatches`
(`vendor/tyra/engine/src/irx/irx_loader.cpp`), which is the only reason this
feature needs no engine change at all. `txiop::init()` applies it again anyway,
so the runtime is self-contained if that ever changes. **If IOP compute ever
silently stops working, this is the first thing to check.**

## Writing a job

`iop/user_jobs.c` is **user-ownable**: it carries the `// Generated by TyraX.
Delete this line to take ownership of this file.` marker, so it is regenerated
while that line is intact and left alone forever once you remove it. Delete the
line before you write anything you want to keep.

A job is one function:

```c
static int jobSumSquares(const int* in, int inCount, int* out, int outMax) {
  int acc = 0;
  if (outMax < 1) return 0;
  for (int i = 0; i < inCount; i++) acc += in[i] * in[i];
  out[0] = acc;
  return 1;  // words written
}
```

and one line in the table:

```c
TxiopJobFn txiopJobs[] = {
    jobSumSquares,
    jobMandelIter,
};
```

**The index is the job number** game code and the flow node use. Appending is
safe; reordering silently repoints every caller.

Three constraints, all consequences of what the IOP is:

- **Integer only** — no FPU.
- **No libc** — this is a bare IRX. No `malloc`, no `printf`, no `memcpy` unless
  you add its import block to `iop/imports.lst`. Static arrays and plain loops.
- **Be worth the trip** — one call costs 150-200 µs in a real game before your
  code runs.

Payload size is 60 words each way (`TXIOP_MAX_WORDS`), single-sourced into both
`iop/txiop.h` and `inc/txiop.gen.hpp`. It is small on purpose: the IOP is for
chewing on a little data for a long time, not for being fed a lot of it.

## Calling a job from a script

```cpp
#include "txiop.gen.hpp"

// Blocking - boot-time work only.
if (txiop::available()) {
  const int args[3] = {cx, cy, 512};
  int out[4];
  if (txiop::call(1, args, 3, out, 4) > 0) escape = out[0];
} else {
  escape = escapeOnEe(cx, cy, 512);   // the path that always exists
}
```

In a frame, use the asynchronous pair instead — this is the shape the feature
exists for:

```cpp
// Somewhere in the update: hand the work over and carry on.
if (!txiop::pending() && txiop::available())
  txiop::submit(1, args, 3);

// In a later frame: pick the answer up.
int out[4], n = 0;
if (txiop::poll(out, 4, &n) && n > 0) escape = out[0];
```

One job is in flight at a time; `submit()` returns false while another is
pending, and `poll()` returns true exactly once per completed job.

### One request at a time, and what that means for mixing callers

The module registers **one** RPC packet buffer, so it serves one request at a
time and the API enforces that rather than letting two callers stomp each
other's payload: `submit()` returns false while a job is pending, and the
blocking `call()` returns -1.

That last one is a trap worth knowing before you hit it. A scene that scripts
the IOP every few frames AND carries a **Run IOP Job** node will have the node
fail — not because the IOP is missing, but because the script is using it — and
the node's only honest report is its `no IOP` output. It looks exactly like
absent hardware. examples/iop-compute hit this during development: its script
submits a row continuously, so the demo graph was reduced to reporting
availability and the job-running node lives in this document instead.

So: pick one caller per scene, or gate them against each other in your own code.
`txiop::pending()` is there for that.

## Calling a job from a flow graph

Two nodes, in the **IOP** category:

- **IOP Available** — a pure bool source. Wire it into a Branch, or into
  anything that takes a condition.
- **Run IOP Job** — an action. *Job* is the table index, *Arg 1..3* are the
  integers handed over, and the result's first word lands in the flow variable
  named on the node (read it back with *Get Int*). It has **two exec outputs**:
  `done` and `no IOP`, so the fallback is part of the graph rather than
  something to remember.

**Run IOP Job blocks.** That is the honest simple model for a graph, and it is
why the node's tooltip says to keep graph jobs short and move long work into a
script. If a graph node ever needs to overlap a long job with the frame, that is
a second node with a submit/poll shape, not a change to this one.

**Live Logic cannot patch a graph containing Run IOP Job** — the interpreter has
no opcode for it and its blocks are straight instruction lists with no branching,
so `livelogic::capability()` reports the graph as unpatchable rather than
pretending. Editing such a graph needs a rebuild.

## What gets generated, and what it costs when off

With the preference **on**:

| file | ownership |
|---|---|
| `iop/txiop.h` | generated — the wire protocol, shared by both sides |
| `iop/txiop.c` | generated — the module: one RPC server, dispatch into the table |
| `iop/imports.lst` | generated — the IOP kernel imports the module links against |
| `iop/user_jobs.c` | **user-ownable** — your code |
| `src/gen/txiop.irx-em` | generated — the two lines that make `bin2s` embed the built IRX into the ELF |
| the IRX build rules in `Makefile` | generated |

With it **off**, none of those exist and a stale `src/gen/txiop.irx-em` is
**deleted** — that file's mere existence is what makes `Makefile.base` run
`bin2s` and the rules build a module, so leaving one behind would keep invoking
the IOP toolchain for a project that no longer wants it. `iop/` itself is left in
place: `user_jobs.c` is your code, and turning a preference off must not delete
it.

The EE-side pair (`inc/txiop.gen.hpp`, `src/gen/txiop.gen.cpp`) is **always**
generated, because the header is the on/off seam: with the feature off,
`txiop::ENABLED` is false, `available()` is a compile-time false, every entry
point is an inline no-op and the runtime is an empty translation unit. That is
what lets a script or a graph call it unconditionally and still compile.

Unlike the devkit layers (Live Link, the Live Debugger, the Remote Pad), **IOP
compute is a game feature and ships in release builds** — it is not gated on the
debug profile and `--audit-release` does not flag it. One consequence worth
knowing: `TYRA_LOG`/`TYRA_WARN` compile out under `NDEBUG`, so the boot line
reporting what `init()` found is only visible in a debug build.

## Where the module is loaded

`src/main.cpp` (generated, always overwritten) calls `txiop::init()` **after**
`Tyra::Engine engine(options)` and before the game runs. The order is not
optional: the Engine's `IrxLoader` is what resets the IOP and applies the SBV
patch above. Doing it there also keeps the ~10 ms calibration in the boot
instead of in a frame.

## Verifying a change

The functional layer is fully scriptable — no console, no human:

```bash
build/tyrax-editor --new iopdemo ~/tyra-projects 100 100 fpp
# turn the preference on in the project's .tyra, then:
build/tyrax-editor --refresh-gen ~/tyra-projects/iopdemo   # inspect the generated iop/
build/tyrax-editor --build ~/tyra-projects/iopdemo --run   # Docker + PCSX2
grep -i "IOP compute" ~/tyra-projects/iopdemo/bin/log.txt
```

A debug build's `bin/log.txt` carries the boot line with the job count, free IOP
memory, the round-trip cost and both calibration timings — which is the whole
gate reported as data. Turn the preference off, rebuild, and the same grep must
come back empty and the ELF must contain no IRX: that negative test is what
distinguishes "the gate works" from "the gate happens to be true".

Keep fixture paths short (`~/tyra-projects/<name>`): PCSX2's `host:` loader
silently refuses an ELF path over ~145 characters.

## See also

- `examples/iop-compute/` — a project that computes on the IOP and falls back to
  the EE.
- [docs/devkit.md](devkit.md) — the layers that are debug-only, and why this one
  is not.
- `vendor/tyra/engine/src/irx/irx_loader.cpp` — how the engine loads the modules
  this rides alongside.
