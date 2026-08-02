# The VU framework

Writing a VU1 microprogram is the one part of this engine that stayed
hand-carved. This is the machinery that makes it ordinary work: **describe a
program in C++, generate both sides of it, and run it on the host** — no Docker,
no PCSX2, no console in the loop.

Four modules in `src/`, host-only, no GL and no `project.hpp` (the
`aobake`/`livedbg` shape), all reachable headlessly:

| Module | What it is |
|---|---|
| `vuir.{hpp,cpp}` | The instruction model everything shares. VCL-level assembly with **unlimited virtual registers**. |
| `vuasm.{hpp,cpp}` | Reads the engine's handwritten `.vclpp` into that model (the vclpp layer + VCL syntax). |
| `vusim.{hpp,cpp}` | **Runs** a program on the host: 1024 quadwords of VU1 data memory, masked fields, ACC, Q/I, clip flags. |
| `vugen.{hpp,cpp}` | The C++ DSL, the program descriptions, the `.vclpp` + EE-side emitters, the equivalence check and the micro-memory budget. |

```bash
tyrax-editor --vu-check
```

## Why this, and not "a compiler for VU"

There is no C++ compiler that targets the VU, and there should not be one here.
The VU is a 2-issue VLIW with 16 KB of instruction memory, no stack and a
separate integer unit; a general-purpose backend would lose badly to `vcl`, the
optimizer the PS2SDK already ships and which the engine's programs already go
through.

So the framework **emits VCL**, not microcode. Generated source is handed to the
same `vclpp → vcl → dvp-as` chain as the handwritten kind
(`vendor/tyra/Makefile.base`), and `vcl` still does the register allocation and
the dual-issue scheduling. That is the whole reason a generated program runs as
fast as a handwritten one — it is not a claim about the generator being clever,
it is a consequence of the generator not trying to be.

The corollary: instruction order in `vuir::Program` is **pre-schedule**. It says
what the program computes, not what cycle each op lands in.

## What was actually hard, and what this fixes

Not the instruction set. The engine ships twenty near-identical `.vclpp` files —
`as_is` / `cull` / `clip` / `billboard` crossed with which attributes a mesh
carries — around one skeleton: load the per-mesh constants, `xtop` the double
buffer, emit the GIF tag block, loop over vertex triples, `xgkick`. Each has an
EE-side twin (`*_vu1_program.cpp`) whose unpack layout, `maxVertCount`
arithmetic, GIF register list and NLOOP patch offset must agree with it, and a
shared address map (`stapip_vu1_shared_defines.h`) maintained by hand.

Every feature added since — hardware fog, the in-band ALPHA tag, the spot light,
the matcap — had to be threaded through all of that by hand. The recorded bugs
are exactly that shape: a 9-quadword tag block against a `maxVertCount` that
reserved 7, NLOOP patched at offset 8 in one family and 6 in another.

A description says it once:

```cpp
Desc d = descAsIsTextureColor();
Built b = build(d);
// b.program   - the IR, ready to simulate
// b.vclpp     - the microprogram source
// b.eeSource  - the EE-side program class, with the matching unpack layout
```

`b.tagQuads`, `b.regsPerVertex` and `b.attrStreams` come out of the same
description, so the two sides cannot drift.

## The check that makes this more than a plausible story

`vuasm` parses the handwritten program into the *same* `vuir::Program` the
generator builds. So both can be run, on identical randomized input, and their
output diffed:

```
-- generated vs handwritten, in the simulator --
  StaPipVU1AsIsC   IDENTICAL 60 trials, up to 12 vertices
  StaPipVU1AsIsTC  IDENTICAL 60 trials, up to 12 vertices
  StaPipVU1AsIsD   IDENTICAL 60 trials, up to 12 vertices
  StaPipVU1AsIsTD  IDENTICAL 60 trials, up to 12 vertices
  StaPipVU1AsIsTCE IDENTICAL 60 trials, up to 12 vertices
```

"Identical" means every quadword of the GIF packet — the tag block and the
vertex payload, from the address the program kicked — matches bit for bit. Not
"the text looks similar": the bits the GS would receive are the same, including
the `ftoi4` fixed-point rounding, the clamped colours and the packed fog
coefficient.

That check runs in milliseconds and needs no PS2.

**What it does not cover: the EE side.** `--vu-check` stages VU1 memory itself
(`stageInput`) and diffs only what the microprogram staged for the GS, so the
generated `addProgramQBufferDataToPacket` — the code that decides *where* each
per-vertex array lands — is never executed by it. That is not a theoretical gap:
the emitted `c` variant put its colour block one `qbuffer->size` too far and the
emitted `td` variant unpacked normals **on top of** the ST block, while
`--vu-check` reported both bit-identical. Both came from the layout being spelled
out three times (the microprogram's pointer arithmetic, `stageInput`, and the EE
emitter), so it is now written down once in `attrBlocks()` and the other sites
derive from it. Until the check grows an EE-side half, an adopted program still
owes a Docker build and a look at the picture.

**The output goes through `vucap::scanGifPackets`** — the very decoder the real
capture path uses (`docs/devkit.md`). It was lifted out of `vucap.cpp` for this:
a simulated run and a captured run produce the same `std::vector<uint32_t>`
memory image and are decoded by the same code, so they are directly comparable.
Do not reimplement GIF decoding on the simulator side.

## What the simulator models — and what it does not

Models: masked destination fields, the accumulator, Q and I, the clip-flag shift
register (`clipw`/`fcand`/`fcset`), integer registers with correct 16-bit
wrapping, `lq`/`sq`/`ilw`/`isw` against 1024 quadwords, branches, `xtop` and
`xgkick`.

Also modelled, because host floats get it wrong in a way that quietly invalidates
a whole run: **the VU FPU is not IEEE-754.** It has no infinities and no NaN — an
overflowing result saturates to ±`0x7F7FFFFF` and a denormal reads as zero — so
every float the simulator writes goes through `vuFloat()`. Without it one
overflow becomes an `inf`, the next subtraction turns that into a `nan`, and
everything downstream is a picture the console could never have produced. The
same reasoning sets the divide-by-zero result: hardware yields the saturated
value signed by *both* operands, never zero, and `0/0` is no exception.

The flip side is that the **move family must not be clamped**: `move`, `mr32`,
`mfir`, `ftoi0`/`ftoi4`, `lq` and `sq` copy bit patterns, and those bit patterns
are routinely integers — a packed GIF tag, an `ftoi4` fixed-point result. They
write raw bits, and `mr32` in particular is a field *rotation*, not arithmetic.
Clamping one of those as if it were a float is how you corrupt it.

Does **not** model, on purpose:

- **Cycle timing, dual-issue pairing and branch delay slots.** Those are `vcl`'s
  job, applied after this level. Simulating them would make the model disagree
  with the source a programmer reads.
- **The MAC and STATUS flag registers.** `fsand`, `fmand` and friends parse, but
  yield 0 and emit a warning saying the run is not authoritative for a program
  that branches on them. An admitted gap beats a plausible wrong answer.

**Rounding is VU rounding, and that was measured, not assumed.** The VU FPU
truncates toward zero where an x86 rounds to nearest-even. The first `--vu-replay`
against a real console reproduced screen X and Y exactly and missed the 24-bit Z
by one or two units in the last place, every time - Z is the coordinate scaled by
8388607.5 rather than 2048, so it is where a single-ULP mantissa difference first
shows. `vusim::run` switches the host rounding mode for the duration of a run;
with that, the captured packet matches whole.

Range and rounding are two separate fixes, and the capture only matched once both
were in: `vuFloat` above covers the range the VU has (saturation, no NaN, no
denormals), the rounding mode covers how it rounds inside that range.

It *does* report two things the assembler will not:

- **Q clobbering** — a `div`/`rsqrt` whose result is overwritten before anything
  reads it. This is a real programmer-level mistake, not a scheduling detail: it
  is exactly the matcap gotcha written up in `tyra_macros.i`
  (`CalculateTyraEnvStq` uses `rsqrt`, so it must run *before* the position's
  perspective divide).
- **Quadword addresses outside VU1 data memory**, which the hardware silently
  wraps.

## Micro memory

VU1 holds 2048 instruction slots and `Path1` parks the draw-finish helper at the
very top — overwriting it hangs the post-fx PATH1 barrier forever, which has
happened once already (`path1.cpp:31`). `--vu-check` reports the budget:

```
-- VU1 micro memory (2048 slots, 2042 usable below the draw-finish helper) --
  StaPipVU1AsIsC     99 instructions ->   50..  99 slots
  ...
  TOTAL                               342.. 681 slots  fits
```

A **range**, not a number, and deliberately so: `vcl` packs an upper and a lower
op into one 64-bit slot when it can, so N emitted instructions occupy between
`ceil(N/2)` and N slots, and the exact figure is only known after `vcl` runs.
Reporting a single number would be a guess dressed as a measurement. The engine's
own guard is a runtime `TYRA_ASSERT` in `createProgramsCache` — which is
**compiled out in release**, so the comment in `stapip_qbuffer_renderer.cpp`
telling you to check with `nm` after touching a program still stands.

## Loading and unloading programs at run time

This already exists and is worth knowing before reaching for it.
`StaPipQBufferRenderer::ensureProgramSet` swaps the billboard program set in and
out mid-frame (one VIF1 MPG upload; the VIF stalls it until VU1 halts), because
the resident ten-program set has no spare micro memory for it. Microcode is just
a `u32*` range in EE memory (`VU1Program`), and `createProgramsCache` assigns
destination addresses when it builds the packet — programs are relocatable, with
no address baked in.

The cost is serialization: `ensureProgramSet` waits for the DMA channel before
*and* after the upload, and it is called per bag, so a scene that interleaves
billboard and ordinary bags pays for a swap at every transition. Nothing measures
that today.

The cheaper answer for micro-memory pressure is upstream of swapping: **the
editor knows, at build time, which program variants a project can actually use**.
A project with no matcap material does not need the two `tce` programs at all.
That specialization is not implemented yet; it needs the union of what a project
*may* use (spawn-pool prefabs included) plus "generate everything" in a Live Link
build, or an object spawned at run time will find no program to draw with.

## Commands

```bash
tyrax-editor --vu-check [engineDir]
```
Parses every `.vclpp` the engine ships, builds each described program, runs both
in the simulator, diffs the GS output, prints the micro-memory budget. Exit 0
only if every handwritten program parsed and every described one matched. This
is the framework's test — there is no unit-test suite in this repo
(`tyra-testing`).

```bash
tyrax-editor --vu-emit <outDir> [engineDir]
```
Writes the generated `.vclpp` plus the matching EE-side `.cpp`/`.hpp` for every
described program. It writes to a directory you name, **not** into
`vendor/tyra`: adopting generated microcode is a change that has to be built in
Docker and looked at on hardware, so this stages it for a human to diff first.

```bash
tyrax-editor --vu-replay <projectDir> [engineDir]
```
Takes a VU1 capture off a real console (`bin/vucap.bin`) and re-runs it here,
then diffs the result against what the hardware produced. The capture holds both
halves of the experiment: the DMA chain the EE built (the input) and a snapshot
of VU1 data memory taken once the microprogram went idle (the output).

Two things the capture does not record are found by SEARCH rather than assumed,
which turned out to be the more useful design - which microprogram ran (the chain
carries an MSCAL entry address, and an address means nothing without a program
layout the host does not have) and which half of the double buffer it used. Every
candidate is tried and the ones that reproduce the hardware output are reported,
so the tool answers "which program drew this?" with evidence instead of a label.

Measured on `examples/vu-lab`: **36/36 GS vertices identical** - screen X/Y in
12.4, the 24-bit Z, the clamped colours and the perspective-corrected ST. It
narrows that draw to two candidates (`clip_tc` and `cull_tc`) and stops there,
which is the honest answer: for a mesh entirely inside the frustum the clip
program's Sutherland-Hodgman pass changes nothing, so the two stage identical
output and nothing in the output can separate them.

Two limits worth knowing before using it. Only the **last mesh of a chain** can
be replayed - everything before it has been overwritten in VU1 memory by
definition, so a flush carrying fourteen terrain chunks is not resolvable and the
Debugger's flush picker is how you get a single-mesh one. And the per-mesh
constants (matrices, tags, fog) come from an object-data chain the capture does
not contain; they are carried over from the snapshot, which is only sound while
the snapshot belongs to the mesh being replayed.

```bash
tyrax-editor --vu-list <file.vclpp> [engineDir]
```
Expands and disassembles one microprogram — what the framework sees *after* the
vclpp layer. The first thing to look at when a program does something you did not
write.

## State: what is covered

**Generated and proven bit-identical:** the five `as_is` programs (`c`, `tc`,
`d`, `td`, `tce`) — the family the EE clipper feeds.

**Parsed and simulatable, not yet described:** everything else the engine ships —
all twenty-five `.vclpp` files parse with no diagnostics, including the five
Sutherland–Hodgman clip programs and the VU0 raytracer kernel. They can be run,
traced and inspected today; they just do not have a C++ description yet.

**Validated against hardware:** a capture from `examples/vu-lab` running in
PCSX2 replays into a bit-identical GIF packet (36/36 vertices). That is the
simulator checked against a real console rather than against itself.

**Adopted in the engine:** the five `as_is` programs in `vendor/tyra` ARE the
generated ones. Built in Docker, booted in PCSX2, and the frame is
**pixel-identical** to the handwritten build (0 differing pixels of 1 258 400,
frozen camera). Regenerate them with `--vu-emit` and copy over; `--vu-check`
gates the change.

**Two traps the hardware pass exposed, both now covered by `--vu-check`.** They
are worth reading before extending the generator, because each produced code the
IR-level check called bit-identical:

1. *The emitter dropped the destination mask on `lq`/`sq`.* `vuir::disassemble`
   only appended `.xyzw` for the ops it classed as "float", and the load/store
   pair was not in that list - so `sq.xyz vertex, 2(destAddress)` printed as a
   plain `sq`, the store wrote FOUR words instead of three, and the position's W
   clobbered the fog coefficient an earlier `isw.w` had just put there. On screen:
   smeared sky triangles and a torn matcap. The IR was right the whole time; only
   its text spelling was wrong, which no comparison of IR against IR can see.
   Hence the round-trip stage: the emitted source is parsed back and re-run.
2. *A value-returning builder mints a fresh register per call.* `fogCoefficient`
   returned a new `IVal` each time, so the three vertices used three integer
   registers where the handwritten program reuses one - 13 VI names instead of
   11. VU1 has 16 and the simulator has unlimited virtual ones, so the pressure
   is invisible on the host. Library methods that need scratch now take it from
   the caller.

**On real hardware:** the generated build boots and runs on a physical PS2 over
ps2link (frames advancing, devkit channels served, 512x512 PAL), and the picture
on the TV was confirmed correct by the console's owner - clean sky, smooth
matcap on the chrome ball, the texture in place. So the adoption is verified on
hardware visually, and pixel-exactly on the emulator.

`--vu-replay` did NOT reach a bit-exact match on the hardware captures, unlike
the PCSX2 ones. The per-vertex deltas say why it is not an arithmetic gap: dz in
the millions and hardware values sitting at the clamp (65535, 0xFFFFE1), i.e. the
packet being compared is not the one that program produced. That points at the
snapshot/mesh pairing - the per-mesh constants come from an object-data chain the
capture does not contain. Whether the real VU also differs from the simulator in
the last bits is UNKNOWN and not claimed either way; settling it needs that chain
captured (see docs/backlog.md).

**Not done:** the `cull`, `clip` and `billboard` families are still handwritten. The generated programs
are proven equivalent in the simulator, but no generated microcode has been built
in Docker or run on hardware. That is the next step and it needs the full e2e
pass (`tyra-testing`), not a host check.

The `cull` and `clip` families are the useful next targets, in that order —
`cull` is `as_is` plus an MVP transform and the ADC clip check, both of which the
builder already has (`transform`, and `clipw`/`fcand` in the IR). `clip` is the
harder one: Sutherland–Hodgman with real control flow and scratch polygon buffers
in high memory, which an expression-level DSL will not express. The honest shape
there is a declarative skeleton with hand-written instruction blocks plugged into
it — 80% generated, the clipper still artisanal.

## Notes on vclpp

Two documented limitations are surfaced as `Program::notes` rather than silently
followed: `#define` expands only one level (hence "kept as a LITERAL" in
`stapip_vu1_shared_defines.h`), and macros do not nest (why the clip programs
inline their emit sequences).

The warning in `tyra_macros.i` that a `;` comment inside a `#macro` body makes
vclpp swallow the whole expansion **does not hold as a general rule** —
`vcl_sml.i`'s `VertexPersCorr` carries a commented-out line and is used by every
transforming program in the engine. Whatever the original incident was, it was
narrower than the comment claims. The parser here strips comments and says
nothing about it; generated programs sidestep the question entirely, since
`vugen` emits no macros at all.
