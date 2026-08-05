# What to send upstream to ps2dev/openvcl

TyraX builds its VU1 microcode with [openvcl](https://github.com/ps2dev/openvcl)
as an alternative to Sony's unlicensed `vcl` — the whole reason being that a
publishable toolchain image cannot contain `vcl` (see
[toolchain-image.md](toolchain-image.md), "Licensing of the published image"). This
page is the part of that work that belongs to openvcl rather than to us: one bug with
a fix, one bug without one, and four optional flags.

Nothing here has been submitted, and no pull request is open. Everything is against
upstream commit `a5867c3daf03828806ee966aca4116622da3f671`.

Our copy lives at [doctorspider42/openvcl-tyrax](https://github.com/doctorspider42/openvcl-tyrax)
(public, AFL-2.0, `NOTICE-TYRAX.md` carries the modification notice §6 requires). It
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

**The mechanism is NOT established, and the obvious guess is wrong.** "Liveness
ignores the back edge" does not survive testing: narrowing the register pool with
`.init_vf vf01-vf03` and writing a value that is live across `b begin` while the body
needs the whole pool — with and without an inner loop — makes openvcl **refuse
cleanly** (`Register allocation ran out of registers`, exit 1) rather than clobber
anything. So the trigger is narrower than "any value live across a backward branch",
and a minimal reproducer is still missing. Both attempts are in this session's
scratch as `loopcarry.vcl`.

For anyone picking it up: the engine-side workaround is to load per-batch values next
to the store that reads them, inside the loop (it costs nothing and lowers register
pressure), and the static check above is what stands in for a fix.

## 3. Four density flags, all off by default

These are ours, they are measured, and they are what make openvcl competitive on this
engine: the resident VU1 program set went from 3072 instructions to **2040**, against
SCE's 2042, without changing what any program computes (pixel-identical frames in
PCSX2). Upstream may or may not want them; the measurements are in
[toolchain-image.md](toolchain-image.md).

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
