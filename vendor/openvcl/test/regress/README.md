# Regression suite

Nine bugs in this compiler were silent: the program assembled, fitted micro
memory, rendered a picture, and computed the wrong thing. Four of them shipped.
Two cost something measurable — 21% of a game's frame rate, and environment
mapping not rendering at all — and both survived every pixel comparison and every
GIF-packet comparison anyone ran, because neither changes a pixel on the scenes
those comparisons used.

This directory is the command that says you have reintroduced one. Without it,
the next person to touch the scheduler finds out the way we did: by measuring a
frame rate that made no sense, twice, months apart.

```sh
./run.sh /path/to/built/openvcl
```

Exit status is 0 only if every case passes. A failure names the bug, not a diff.

## Why several kinds of assertion

Each of the four shipped bugs needed a *different* kind of check to be visible at
all. That is the argument for keeping all of them rather than picking one:

| kind | what it asserts | which bug needed it |
|---|---|---|
| **COUNT** | an instruction the source contains is still emitted | 4 of 7 `clipw` deleted per clip program; 3 `adda` deleted per env-mapped program |
| **ORDER** | a flag reader observes the writers the source gives it — the CLIP shift register and the ACC chains replayed (`lib/p8-flagorder.py`) | the dependency pass flushing its writer list at a reader; a full-window `fcand` reading before the newest judgement landed |
| **VALUE** | the expression stored at each `sq`/`isw` is the one the source computes, register naming abstracted away (`lib/pa-dag.py`, `lib/pb-dag.py`) | catches all four, by value rather than by shape |

`COUNT` looks the crudest and is the one that would have caught the two most
expensive bugs on the day they landed.

**`TIME` is the odd one out and the reason is worth stating.** Every other kind
begins by waiting for a `.vsm`, so not one of them can see a compiler that never
produces one. `--loop-liveness-always` built a ring in the register allocator's
same-name chain on one control-flow shape and the walk of that chain never
returned: 66 minutes, then stopped rather than finished. That is worse than a
compile error — a project build hangs with no diagnosis at all — and it is the
only failure in this compiler's history that a user would have met head-on. A
`TIME` case names a bound in seconds and asserts the compile finishes inside it.
The bound is not a performance budget: `loop_chain_ring` takes 0.55 s and its
bound is 60, so a slow machine cannot turn it red, and the failure it reports is
non-termination rather than slowness. Every other case is compiled under a
generous default bound for the same reason — a suite that hangs reports nothing.

## Rules that keep this suite honest

**Every instrument here is controlled against Sony's `vcl`.** If a check reports
a violation in the reference assembler's output, the check is wrong until proven
otherwise. That control forced **nine** corrections on one oracle and **thirteen**
on another during development — rows of two `nop`s not counted, `move` lowered to
`max d,s,s` not folded, register arrays read as field selectors, offsets that are
sums, a stall annotation charged to the wrong side of its row. Every one of those
would have been reported as "Sony's compiler is broken".

**A case may need a different configuration, and one form says so.** Most
defects here are visible with the twenty-one flags the TyraX toolchain passes,
because that is the build it ships. Some live in a flag's OFF path - the default
one, which is what a plain `ps2dev/openvcl` checkout does - and a suite that
only ever compiles one configuration cannot assert those at all. A case whose
`arg` reads `no:--the-flag` is compiled with the standard list minus that one
flag, so a failure still names one thing rather than a configuration.
`loop_pressure_carry` is the first: with `--loop-liveness-always` on it was
correct all along, and the guard it is about is skipped.

**A case needs its control.** `acc_fields` asserts a non-covering ACC write is
kept; `acc_fields_covered` asserts a genuinely covering one still kills. Without
the second, the first is satisfied by a compiler that has stopped eliminating
dead code at all.

**Some cases fail on stock upstream and that is intended.** Several of these bugs
are upstream's, not the fork's, and are written up with reproducers in the TyraX
repo (`docs/upstream-openvcl.md`). Running the suite against an unmodified
`ps2dev/openvcl` is a way to see which.

## The corpus is not a substitute

These are hand-written straight-line and small-loop programs. They exist because
the real 70 microprograms are large, loop-heavy, and were checked for years by
instruments that could not see any of this. The value oracle
(`lib/pb-dag.py`, path-sensitive with bounded unrolling) does cover the real 70 —
that lives with the fuzzing harness, not here, because it needs both assemblers.
