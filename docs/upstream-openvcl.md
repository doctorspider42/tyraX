# What to send upstream to ps2dev/openvcl

TyraX builds its VU1 microcode with [openvcl](https://github.com/ps2dev/openvcl)
as an alternative to Sony's unlicensed `vcl` — the whole reason being that a
publishable toolchain image cannot contain `vcl` (see
[toolchain-image.md](toolchain-image.md), "Licensing of the published image"). This
page is the part of that work that belongs to openvcl rather than to us: two bugs with
a fix, one bug without one, one calibration mistake of our own, and the density flags.

Nothing here has been submitted, and no pull request is open. Everything is against
upstream commit `a5867c3daf03828806ee966aca4116622da3f671`.

Our copy lives at [doctorspider42/openvcl-tyrax](https://github.com/doctorspider42/openvcl-tyrax)
(public, AFL-2.0, its `README.md` carries the modification notice §6 requires — it used
to be a separate `NOTICE-TYRAX.md`, folded in so the notice is on the first screen). It
keeps upstream's history, so `compare/upstream-a5867c3...tyrax` is the whole diff —
which is what any of the reports below would be offered from, when we decide to offer
them.

## 1. CLIPw with an implied `w` is rejected — and the program is emitted anyway

**The fail-safe is the bug, more than the parse.** `CLIPw`'s second operand can only
be the w component; the ISA gives it no other meaning, and Sony's vcl infers it. Given

```
	clipw.xyz	vf01, vf01
```

openvcl reports `Invalid argument (clipw.xyz vf01, >>> vf01 <<<)` and exits 1 — fine
so far — **but it still writes a complete `.vsm`, with the `clipw` missing and the
`fcand` that reads its clip flags still there.** A build that checks stderr for
"error", or that only checks the assembler produced a file, ships a program that tests
clip flags nobody set. That is what happened here: 21 of this engine's 25 microprograms
were silently emitted without their frustum test, and the first sign of it was
geometry that should have been culled reaching the GS.

Minimal reproducer (`clipw.vcl`):

```
.syntax new
.name ClipwRepro
.vu
.init_vf_all
.init_vi_all
--enter
--endenter
	lq		vf01, 0(vi00)
	clipw.xyz	vf01, vf01
	fcand		vi01, 0x3FFFF
	iaddiu		vi02, vi01, 0
	isw.x		vi02, 1(vi00)
--exit
--endexit
```

```
$ openvcl clipw.vcl > out.vsm ; echo $?
clipw.vcl(9) : Invalid argument (clipw.xyz vf01, >>> vf01 <<<)
1
$ grep -c clipw out.vsm    # the fcand survives, the clipw does not
0
```

Two independent fixes, and the second is worth doing whatever happens to the first:

1. **Accept the implied component** — default a missing selector on a `wcomp` operand
   to `w`. This is [`docker/openvcl-clipw-upstream.patch`](../docker/openvcl-clipw-upstream.patch)
   (59 lines, `src/Token.h` + `src/Token.cpp`): a `WCOMP` argument modifier, parsed
   and then defaulted in `process()`. All 419 upstream tests stay green, and it is
   what lets this engine's sources compile unchanged under both assemblers.
2. **Do not write output when an operand was rejected.** Silent wrong code is worse
   than a failed build, and any consumer that trusts the exit code is fine either way.

## 2. A value live across a batch loop gets reused inside it

`stapip_as_is_c` loads five GIF tags before its `begin:` label, stores them once per
batch, and loops with `b begin`. openvcl put the `gifSetTag` in VF01 and then used
VF01 as the per-vertex colour inside the same body:

```
lq VF01, 19(VI00)              ; gifSetTag, loaded once, above begin:
...
sq VF01, 0(VI07)               ; written into the GIF tag block, every batch
...
add.xyzw VF01, VF00, VF02      ; and VF01 is the per-vertex colour inside the loop
```

The first batch draws correctly and every one after it writes a colour where the GS
expects a GIFtag, so its geometry never reaches the GS — 50 FPS of missing terrain, no
diagnostic. Sony's vcl keeps them apart. Nine of the engine's programs had it; a
static check (per register *component*: written before the loop, read in the body
before the body writes it, written in the body) reports 9 of 25 for openvcl and 0 of
25 for `vcl`.

**The obvious guess is wrong**, and it cost months before the real mechanism turned up
below. "Liveness
ignores the back edge" does not survive testing: narrowing the register pool with
`.init_vf vf01-vf03` and writing a value that is live across `b begin` while the body
needs the whole pool — with and without an inner loop — makes openvcl **refuse
cleanly** (`Register allocation ran out of registers`, exit 1) rather than clobber
anything. So the trigger is narrower than "any value live across a backward branch",
and a minimal reproducer is still missing. Both attempts are in this session's
scratch as `loopcarry.vcl`.

**Since resolved, and the guard is the mechanism.** `extendLoopDirectiveRange` extends
live ranges over a loop body, but the extension is all-or-nothing and returns early when
the set would not fit the register file — which is exactly a silent licence to emit wrong
code, because a value carried across the back edge then keeps a range ending at its last
use *inside* the body and its register is handed to another name. The register-allocator
work for §3 below covers this too, gated behind `--loop-liveness-always`, which both
narrows the extended set to the aliases actually carried (first access inside the body is
a read) and drops the early return so allocation fails loudly instead.

Isolated by ablation on five engine programs whose sources have exactly this shape — GIF
tags loaded above `begin:`, stored once per batch:

| invocation | result |
|---|---|
| no flags | `as_is_c`, `as_is_tc`, `as_is_tce` clobber VF01 and 3–4 more |
| `--loop-liveness-always` alone | clean |
| nine other flags without it | the same three clobber |

So this is **still live in upstream's default configuration**, and the flag is what closes
it. In an emulator, the same five programs then render 0 of 514600 pixels different from
`vcl`'s build of them.

Still open: a *minimal* reproducer. Four attempts failed to make the guard fire (a 4–12
register pool needing all of it refuses cleanly instead; 42 float aliases against 32
registers, and 20 integer aliases against 16, both keep the extension; forcing order via
a read-back of the just-stored quadword likewise). The programs that do trip it all have
an **inner** loop, so the range extension runs twice over nested ranges — the untried
hypothesis.

## 3. A loop-carried update lands in a register nobody reads

This one *is* established, with a mechanism and a fix, and it is the most important of
the three. A name written at the bottom of a loop and read at the top is one variable,
but openvcl spawns a fresh `Alias` for every write, so it is two objects — and they get
two registers. `stapip_clip_tc`'s edge loop, with names and final registers from a
patched `--show-reg-alloc`:

```
VF15 <- ppos #112  ranges: [228-286]      what edgeCross reads
VF16 <- pstq #113  ranges: [229-286]
VF17 <- pcol #114  ranges: [230-286]
VF22 <- ppos #126  ranges: [281-281]      what edgeAdvance writes
VF18 <- pstq #127  ranges: [282-282]
VF18 <- pcol #128  ranges: [283-283]
```

`move ppos, cpos` writes VF22 while every reader keeps reading VF15, so the update is
lost and each iteration sees the previous iteration's value forever. And because nothing
downstream reads those tail aliases, each gets a live range **one line long**; two
one-line ranges do not intersect, so `pstq` and `pcol` legally share VF18 and the second
write destroys the first.

`RegisterAllocator::extendLoopDirectiveRange` already computes exactly the set this needs
— the aliases whose first access inside the loop is a read. The fix ties every in-loop
write of such a name to the alias its readers use, via the existing two-address chain
(`Alias::setSameNamePredecessor`, the thing that makes `isubiu x, x, 1` write the register
it read); the chain pre-pass then gives the whole chain one register. It changes no
program size on this engine's 25 microprograms, and upstream's tests stay green.

Worth sending alongside it: **`--show-reg-alloc` cannot currently answer the question
that matters.** It prints ranges for anonymous aliases, before allocation runs, so
"which two values share a register?" is unanswerable. Three small additions fix it — an
`Alias` debug name recorded in `BranchState::updateDependency`, and a
`=== Final assignment ===` block printing name → register after allocation. Every finding
above came out of that dump.

## 4. The CLIP flag is positional - flag visibility is not one number

Ours, and a warning about our own calibration rather than a bug in upstream's defaults
(upstream uses 4, which is right). `--sce-latencies` lowered flag visibility to 1 for
MAC flags and the CLIP flag alike, from the shortest gaps SCE's output contains. MAC
flags summarise the last FMAC; the CLIP flag register is a 24-bit shift window that
every `CLIP` pushes six new bits into, so reading it early returns a **different
vertex's** answer with the mask selecting the wrong window position. A full-window test
(`fcand VI01,0x3FFFF`) tolerates it; the single-bit tests in a Sutherland-Hodgman edge
loop do not. Over 25 real microprograms, positional tests sit at gap 3 (x36) and 4 (x5)
in SCE's output and at gap 1 (x72) in openvcl's.

Note how SCE reaches the source's semantics: it issues `clipw` #N+1 *before* reading
#N's flags, exactly because #N+1's bits are not visible yet. Any scheduler that moves a
flag reader has to model that window, not a scalar latency.

And the constraint has to be enforced where the words are. openvcl's scheduler honours a
cycle-level CLIP latency correctly, yet still emitted reads one to three rows after their
CLIP: a wait the hardware interlocks is a cycle that costs no instruction, so four cycles
can come out as two rows. The window is counted in instructions, so it belongs in the
emitter - `CodeGenerator::padForClipFlagWindow` in our copy, which walks the rows already
emitted and pads to distance, treating a label as a barrier.

**Half of it, at least. The scheduler holds the other half, and the two disagreed with each
other for a year.** The emitter exempts a reader whose mask covers the whole window
(`0x3FFFF`, `0xFFFFFF`: "is anything outside" gets the same answer from any position), while
`VuLatencyTracker` applied the four-cycle CLIP latency to every reader under a bare
`if( readsClip )`. Being the earlier and stricter of the two, the scheduler won, and the
emitter's exemption could never pay. Worth 22 words on our generated corpus once the
scheduler was taught the same test - `docs/toolchain-image.md`, "22 of those words were
openvcl contradicting itself". Whatever else the CLIP window needs, it needs the scheduler
and the emitter to be answering the same question.

Two corrections to what is above, both measured after it was written. The distances quoted
here come from `clipgap.py`, which looks only at the FIRST reader after each CLIP and counts
MAC readers too - a different instrument from the per-reader walk used later, so the numbers
are not comparable rather than contradictory. And the claim that SCE never places a
positional read closer than three rows is **false**: 89 of them are closer across the 70
programs, four of them at 1, 0, 3 and 2 rows in `stapip_billboard_c_vu1`. SCE pairs a reader
with a `clip` several rows back rather than the nearest one, which is what modelling the
shift window buys and what we still do not do.

## 5. `RNEXT` and `RXOR` are declared as if they were not read-modify-write

**This one fires on stock upstream at `a5867c3` with no flags at all, and it is two constants.**
`src/VuInstructionInfo.cpp` declares `RNEXT` as a pure *read* of R and `RXOR` as a pure *write*.
Both are read-modify-write: `RNEXT` advances the LFSR and then copies it out; `RXOR` folds a
source field into the current R.

Because neither is a *writer* in the table, the dependency graph contains **no edge at all**
between an `rnext` and a following `rget`, and the scheduler will hoist the `rget` above it, so
it reads the un-advanced value:

```
	rinit	R, 0x3F800000
	rnext.x	vf01, R          ; advances R, then copies
	rget.x	vf02, R          ; scheduled ABOVE the rnext
```

Two more consequences appear as soon as a dead-code pass exists. An `rnext` whose destination VF
is dead is deleted outright, silently dropping an LFSR step — its `implicitWrites` is `NONE`, so
an observability check is never even consulted. And `rinit R,seed` followed by two `rxor` loses
the `rinit` and the first `rxor`, leaving the second folding into whatever R held on entry.

Fix: declare both `reads=R, writes=R`. Nothing else has to change — the observability walk checks
`implicitReads` before `implicitWrites`, so a correctly declared read-modify-write is handled by
construction. Reproducers for all three are `r_order.vcl`, `r_rnext.vcl` and `r_rxor.vcl`.

## 6. The status register is not modelled, and `fsand` can be scheduled before what it reads

`FSAND`/`FSEQ`/`FSOR`/`FSSET` carry `reads=NONE, writes=NONE`, and no FMAC declares a status
write. On stock upstream, an `fsand` is therefore scheduled *before* the `mul.x` whose flags it
exists to read, and a dead-code pass deletes that `mul.x` outright because "nobody reads MAC".

The compiler already disagrees with itself here: `isVuMacReader()` in `VuSchedulingRules.cpp`
**does** list `fsand`/`fseq`/`fsor`, and `declaredHardwareResource()` maps `out_hw_status` onto
`VU_RESOURCE_MAC` — but the latency tracker asks `tokenReadsImplicitResource`, which uses the
table that says no.

**The fix is not "mark every FMAC a status writer".** The sticky bits (`ZS/SS/US/OS/IS/DS`)
*accumulate* until `fsset` clears them, so there is no kill to model at all; adding a resource bit
under the current pairwise builder would put a WAW edge between every pair of FMACs and destroy
every schedule. What this needs is an **accumulating-resource** class: writes commute, so no WAW,
only RAW and WAR against readers and against `fsset`. Reproducer `status_sticky.vcl`.

**Implemented here, and the shape is worth copying.** `FSSET` is declared **read-and-write**;
that single declaration makes it a hard barrier under the RAW/WAR tests that already exist, so
nothing has to learn what a "clear" is — the same construction that fixes `RNEXT`/`RXOR` above.
The dependency pass edges every contributor since the last clear into every reader and never
flushes its list at a reader, and emits no writer-to-writer edge at all. The non-sticky half
(Z/S/U/O/I/D) is handled by reading the reader's own immediate: a mask naming only sticky bits
pins nothing, a mask naming a non-sticky bit pins the last contributor. Measured on a synthetic
corpus: zero extra rows when nothing reads status, one row when something does.

Deliberately not modelled: a trailing live-out barrier for an accumulating resource. Two upstream
tests assert that dead MAC/CLIP WAW edges may be dropped, and they are right — for a value that
is "every contributor OR-ed" there is no single writer producing the value that leaves the block,
so any order satisfies the contract. `out_hw_status` names one register with two halves, and
"the flags of whichever instruction happens to be last" is not a contract a scheduler can keep.

## 7. `--drop-dead-writes` deletes an ACC write that a later write does not cover

ACC is per-field; the observability walk carries one resource bit. So

```
	add.xy	acc, vf00, envConsts[w]
	add.z	acc, vf00, envConsts[z]
	madd.x	stq1, envConsts, dotR[x]
```

loses the `.xy` write — the walk sees "another ACC writer" and never checks that `.z` does not
cover `.xy`. Whatever the previous iteration left in `ACC.xy` is then accumulated into. In TyraX
this deleted the seed of the environment-mapping S/T accumulation in 8 programs, 3 instructions
each, and **environment mapping stopped rendering entirely** — visible, not subtle.

Fix: carry a pending field mask alongside the resource and retire only the lanes the later write
names. Reproducers `acc_fields.vcl` (fails before, passes after) and `acc_fields_covered.vcl`
(control: a genuinely covering write must still kill).

A note for anyone writing a scanner for this: VCL lets the **destination operand** select the
accumulator form, so `add.xy acc, …` is an ACC writer while matching none of
`adda|mula|madda|msuba`. An audit that greps for the accumulator mnemonics reports zero
occurrences and is wrong.

## 8. The flag dependency pass flushes its writer list at a reader

`addPreciseImplicitFlagDependencies()` clears `pendingWriters` once it has edged them into a
reader. So the **second** reader of the same chain gets no edge to those writers at all, and the
scheduler may hoist it above them.

Fires on stock upstream at `a5867c3` with no flags — reproducer
`acc_second_reader_upstream.vcl`, where `maddx.y` is emitted at row 19 while the `addaw.xy` and
`addaz.z` it reads sit at rows 22 and 23. Two more, `clip_second_reader.vcl` (a
`fcand VI02,0x8` emitted *above* its `clipw`) and `acc_second_reader.vcl`, fail before and pass
after.

In TyraX this reordered the environment-map accumulation in two programs, so one of every three
per-triangle `stq.z` values was built from the scale chain's leftover `ACC.z` rather than from
`envConsts.z`.

Fix: do not clear the list at a reader; keep contributors edged into every subsequent reader
until an actual kill. One deleted statement, plus a counter to keep the fan-in incremental rather
than quadratic. Zero words on a 70-program corpus, three programs reordered.

## 9. Twenty-one flags, all off by default

These are ours, they are measured, and they are what make openvcl competitive on this
engine: the resident VU1 program set went from 3072 instructions to **1970**, against SCE's
2028 and a ceiling of 2042, without changing what any program computes (pixel-identical
frames in PCSX2). Over the engine's whole corpus of 25 it is 3874 words against SCE's 3982,
and over the 45 a project can generate, 9202 against 9264 - **under Sony on all three**.
Upstream may or may not want them; the measurements are in
[toolchain-image.md](toolchain-image.md).

What they do NOT buy is speed. On a VU1-bound scene in PCSX2, with only the assembler
different, openvcl runs about **26% slower** than Sony's `vcl`, and the flag that flipped the
last size corpus did not move a single frame. The cause is the FMAC read-after-write
interlock, not size - see "Measured on the console" in that file.

The table below lists the four this section was originally written about. The full twenty-one,
grouped by what they do, are in the fork's own README at
[doctorspider42/openvcl-tyrax](https://github.com/doctorspider42/openvcl-tyrax) and in
`VCL_FLAGS` in both Dockerfiles, which are kept byte-identical to each other.

| flag | what it stops paying for | evidence |
|---|---|---|
| `--fmac-interlock` | a VF-to-VF wait as emitted nops — the FMAC pipeline interlocks | SCE emits gap-1 VF dependencies throughout |
| `--sce-latencies` | flag visibility 4 → 1, integer load result at issue+3 | calibrated against the minimum gaps SCE's own output contains |
| `--branch-interlock` | a bubble before a branch whose operand came from a load or flag reader | SCE writes `ilw.x VI01,8(VI00)` / `iblez VI01,...` adjacent and annotates it `STALL_LATENCY ?3` |
| `--branch-bubble-on-dependency` | the pre-branch bubble when the row above produces none of the branch's operands | `branchNeedsPreBubble()` looks only at the branch's own flags, so it bubbles before *every* conditional branch: 80 stall sites against SCE's 36 |

The last one is the interesting report even if the flag is not wanted: emitting that
word unconditionally is most of the size difference between openvcl and Sony's vcl on
real code, and the information needed to decide it per branch is already in the
scheduler's slots.

Two further flags exist (`--schedule-flag-readers`, `--emit-delay-fillers`) and are
described in the same page.
