# What to send upstream to ps2dev/openvcl

TyraX builds its VU1 microcode with [openvcl](https://github.com/ps2dev/openvcl)
as an alternative to Sony's unlicensed `vcl` — the whole reason being that a
publishable toolchain image cannot contain `vcl` (see
[toolchain-image.md](toolchain-image.md), "Licensing of the published image"). This
page is the part of that work that belongs to openvcl rather than to us: **eleven defects**,
one calibration mistake of our own, and the density flags. Every defect is written up with
the mechanism - what the code believed and why that is wrong - and all but section 4 with a
reproducer that fires on the stock commit below. Several need no flags at all, so they are
reachable from a plain `make` - section 10 is demonstrated that way above, and the fork's
`test/regress/` marks each such case in `cases.tsv`, so running that suite against an
unmodified checkout is how to see which.

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

## 9. `padForClipFlagWindow` exempts full-window masks, which reads a stale window

`clipReadIsPositional()` exempts a reader whose mask covers the whole CLIP window (`0x3FFFF`,
`0xFFFFFF`) from the emitter's padding, on the reasoning that such a mask asks *is anything
outside* and therefore does not care **which entry** a judgement sits in.

That is true and insufficient. It does not cover **whether the newest judgement has arrived at
all** — and `0x3FFFF` is three entries wide, so reading a row early shifts the whole window by a
vertex. The reader then judges the previous primitive.

In TyraX this put a `fcand VI01,0x3FFFF` one row after its `clipw` in 14 programs. Most of those
sites write an ADC bit the GS never consults (triangle *list*: only the third vertex kicks), but
three are at the kicking vertex, and there the test becomes `!(v1 out || v2 out || v3 of the
PREVIOUS triangle out)` — which draws an outside triangle unclipped in one case and drops a
fully-inside one in another.

Fix: pad every CLIP reader; let the mask choose the *latency*, not whether to wait at all.

**Why the existing suite does not catch it.** `test_flag_latency`'s "clipw followed by fcand"
case runs without the emitter exemption in play, so the latency tracker holds the reader on its
own and the test passes. The emitter half is only reachable when the tracker's wait is not the
binding constraint.

## 10. Pre-decrement addressing emits post-increment, and the output will not assemble

`Token.cpp` parses `(--base)` correctly - it sets the `PREDEC` modifier bit - and then, in the
branch that turns modifiers into argument flags, sets the wrong one:

```cpp
if( hasModifier( POSTINC, modifiers ) )
{
    ...
    argument.setFlags( argument.flags() | Argument::POSTINC );
}
else if( hasModifier( PREDEC, modifiers ) )
{
    ...
    argument.setFlags( argument.flags() | Argument::POSTINC );   // <- PREDEC
}
```

`Argument::PREDEC` is therefore never set on any argument in the program. The emitter's `--`
branch is unreachable dead code, and every `lqd`/`sqd` against a pre-decremented base is written
out as a post-increment.

**Reproducer** - stock `a5867c3`, no flags:

```
    lqd     v0, (--ptr)
    lqd     v1, (--ptr)
```

```
$ openvcl predec.vcl
    nop                             lqd VF01, (VI02++)
    nop                             lqd VF02, (VI02++)

$ dvp-as predec.vsm -o predec.o
predec.vsm:8: Error: bad instruction `lqd VF01,(VI02++)'
predec.vsm:9: Error: bad instruction `lqd VF02,(VI02++)'
```

So this is not a silent miscompile - the operand form `dvp-as` accepts for `lqd`/`sqd` is
`(--VIxx)` or `(VIxx++)` but not the one it is handed here, and the build fails at the assembler.
It is total, though: **no source using pre-decrement addressing can be built at all**, in any
released version.

Fix: set `Argument::PREDEC` in the second branch.

**Why nobody has hit it.** The construct has zero uses in everything we could measure: 70 real
microprograms from a shipping engine, 1360 generated stress programs, 3780 narrowed variants, and
every example in this repository contain not one `(--base)`. Post-increment, which shares the
operand path and works, has 363 sites. A bug can be both total and invisible.

## 11. FDIV and EFU latency is measured down the file, and a branch does not go down the file

`Q` and `P` have no hardware interlock. `div` writes `Q` a fixed seven cycles after it issues
(thirteen for `rsqrt`), a read before then returns the PREVIOUS quotient, and `waitq` is the
instruction that stalls until it lands. The compiler is therefore responsible for the distance,
and it measures it on the wrong timeline.

`scheduleVuProgramReadyIssueSlotsWithFlagLivenessInternal` walks the basic blocks in file order,
carrying one `VuLatencyTracker` and one running cycle count from each block into the next:

```cpp
for( block = blocks.begin(); block != blocks.end(); ++block )
{
    scheduledBlock.firstIssueCycle = program.cycleCount;
    scheduledBlock.issueSlots =
        scheduleVuBasicBlockReadyIssueSlotsWithFlagLiveness( *block, liveness,
                                                            programLatencyTracker,
                                                            program.cycleCount );
    program.cycleCount += scheduledBlock.cycleCount;
}
```

That is exactly right for a block reached by falling through and wrong for one reached by a
branch. Nothing in the compiler looks at a block's predecessors - `grep -i predecessor src/`
finds only the register allocator's same-name alias chain - so a label's entry cycle is always
the fall-through one, and a forward branch that jumps over the rows in between delivers its
target early.

**Reproducer** - the twenty-one flags TyraX passes; the shape needs a branch, so it does not
reduce further:

```
    div        q, f0[x], f1[y]
    ibgez      n0, Lskip
    mul.xyzw   f3, f3, f4
    ... five more rows ...
Lskip:
    mulq.w     f2, f1, q
```

openvcl puts twelve rows between the `div` and the `mulq` and emits no wait, because down the
file twelve is more than seven. Along the taken edge the only rows between them are the branch
and its delay slot:

```
                    nop                             div q, VF01x, VF02y      <- cycle 0
                    nop                             lq VF05, 4(VI00)
                    nop                             iaddiu VI01, VI00, 8
                    nop                             nop
                    nop                             ibgez VI01, Lskip        <- cycle 4
                    nop                             nop                      <- delay slot
                    ... six rows the branch jumps over ...
Lskip:
                    mulq.w VF03, VF02, q            nop                      <- cycle 6, needs 7
```

Sony's `vcl` on the same source pads the target block so the `mulq` sits at cycle **7** exactly,
which is what says seven is the hardware's number and not a modelling choice.

**Fix**: give each block the skew between its fall-through entry cycle and the shortest path
that reaches it, and push the outstanding `Q`/`P` ready cycles back by it before the block is
scheduled. For a branch out of a block whose own skew is `s`, issuing at cycle `X`, into a label
whose fall-through entry is `L`, that edge's skew is `s + L - X - 2` - the two being the branch
and its delay slot. A block takes the largest of its edges. A BACKWARD edge can never win that
maximum (it has `X > L`), so one pass in file order computes the whole thing.

**Scale.** 63 of 400, 73 of 480 and 71 of 480 programs in three generated stress corpora change
when the skew is applied; on TyraX's 70 real microprograms **nothing moves** - the resident,
engine and generated word counts stay at 1998 / 3908 / 9242 - because none of them puts a `Q`
consumer at a label a forward branch can shorten into. The differential oracle's residual over the three corpora goes from
**23 / 22 / 21** divergent programs to **1 / 2 / 2**, against Sony's own **2 / 3 / 1** on the same
runs - at or below the reference everywhere. All five that remain are still in the FDIV/EFU
bucket: two read `undef(P)` after an `esum`, and three read the wrong quotient in shapes this fix
does not reach.

Those five have since gone to **zero** on all three corpora, with Sony still at 2 on the same
run. Their cause was a second, independent one, and it is the TyraX fork's rather than
upstream's: `--fmac-interlock` (a fork flag) keeps a wait on the FMAC pipeline in the cycle model
and emits no instruction word for it, because the hardware stalls by itself - but FDIV and EFU
have no interlock, and only a word carries the program from one cycle to the next, so a
suppressed stall was being spent twice. Written up in the fork's `CHANGELOG.md` and in
"The hang, and the last five residuals" in `toolchain-image.md`. It is the one change in this
effort that moves the 70's word counts: 1998 / 3908 / 9242 → **1996 / 3910 / 9242**, ten `waitq`
the engine's own microprograms needed.

**What is NOT fixed by this.** A producer BELOW its consumer, feeding it through a back edge on
the next iteration, is a different question: the linear tracker has not seen the producer at all
when it schedules the consumer. openvcl happens to answer that one correctly today, by emitting
`waitq` (the linear model sees the division from the PREVIOUS block and over-waits), but nothing
makes it do so on purpose.

## 12. Twenty-one flags, all off by default

These are ours, they are measured, and they are what make openvcl competitive on this
engine: the resident VU1 program set went from 3072 instructions to **1998**, against SCE's
2028 and a ceiling of 2042. Over the engine's whole corpus of 25 it is **3908** words against
SCE's 3982, and over the 45 a project can generate, **9242** against 9264 - under Sony on all
three. Upstream may or may not want them; the measurements are in
[toolchain-image.md](toolchain-image.md).

**Two of those numbers used to be smaller, and that was the bug.** An earlier revision of this
page reported 1970 / 3874 / 9202 and called it a win. It was a build that had *deleted*
instructions the source contained - the CLIP dead-write defect in section 4 - so it was
smaller because it computed the wrong thing. The lesson is worth passing on with the flags: on
this target, a size figure is only meaningful next to evidence that the program still computes
what it used to, because the most effective way to make a VU1 program smaller is to break it.

The same revision reported openvcl running about **26% slower** than Sony's `vcl` on a
VU1-bound scene and blamed the FMAC read-after-write interlock. That was the same defect
measured from the other end: the deleted `clipw` made the clipper take a different path.
With it fixed, the two assemblers are at parity on the scenes that can see a difference at
all - 100.4 against 99.5 FPS on one, 87.84 against 87.38 on another, each with a
known-bad third arm proving the scene registers a regression when there is one.

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
