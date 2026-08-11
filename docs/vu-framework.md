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
| `vusim.{hpp,cpp}` | **Runs** a program on the host, on either unit (`Target::VU1`/`VU0`): 1024 or 256 quadwords of data memory, masked fields, ACC, Q/I, clip flags. |
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
wrapping, `lq`/`sq`/`ilw`/`isw` against the target's data memory, branches, `xtop` and
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
  StaPipVU1ClipD    239 instructions ->    no image   in StaPipVU1Clip_C
  StaPipVU1ClipTCE  257 instructions ->    no image   in StaPipVU1Clip_TC

  VU1 clipping   8 images   962..1918 of 2042 slots  fits
  EE clipper    10 images   828..1649 of 2042 slots  fits
```

The listing prices every description; the two lines under it are the only totals
that mean anything, and they are what to read. **VU1 holds one set at a time**:
the five `cull` programs plus either the `clip` twins or the `as_is` twins, never
both, because the clipping mode is a run-time switch. And two of the five clip
classes are ALIASES that occupy no micro memory of their own (see the next
section), so adding them in counts one body twice. Summing all fifteen — which
this used to do — reports 1544..3077 for a machine whose largest possible
resident set is 962..1918, i.e. it says "OVERFLOWS, or maybe not" about a set
with 300-odd slots to spare. That is not a conservative estimate, it is a wrong
one.

Both totals are the ALL-CLASS worst case. A project that draws fewer material
classes uploads a subset (`setResidentClasses`), and a project's own look adds an
image per class it overrides; pricing those two is the Micro memory panel's job
(`docs/vu-authoring.md`).

A **range**, not a number, and deliberately so: `vcl` packs an upper and a lower
op into one 64-bit slot when it can, so N emitted instructions occupy between
`ceil(N/2)` and N slots, and the exact figure is only known after `vcl` runs.
Reporting a single number would be a guess dressed as a measurement. The engine's
own guard is a runtime `TYRA_ASSERT` in `createProgramsCache` — which is
**compiled out in release**, so the comment in `stapip_qbuffer_renderer.cpp`
telling you to check with `nm` after touching a program still stands.

## Shared images: five programs, three bodies

`C/D` and `TC/TCE` are ABI-compatible pairs — same input streams, same scratch
stride, same GIF register list — so each pair rides ONE resident image and
`VU1_OPTIONS_ADDR.y` selects the per-corner shading path per mesh. `TD` stays
specialised. This is the difference between 1676 and 2030 measured slots, and it
lives in three places that have to agree:

- **The description.** `Desc::sharedClipDir`/`sharedClipEnv` say "my body also
  carries a peer's path"; `Desc::residentImageAsmName`/`codeOwner` say "somebody
  else's body carries mine". Both directions are needed, and the second is the
  one every consumer reads: it is what the EE wrapper links, what the budget
  charges, and what the panel prices.
- **The EE wrapper.** `extern u32 <image>_CodeStart` is the whole mechanism. An
  aliased program declares its OWNER's symbols, so its own `.vclpp` object is
  never pulled out of `libtyra.a` and no second copy of the body reaches the
  ELF. Getting this wrong compiles, boots, draws correctly, and silently costs
  300 slots — which is why `--vu-check` compares the emitted wrapper and the one
  in `vendor/tyra` against the description, both of them, every run.
- **`Path1::createProgramsCache`**, which uploads a source range once and points
  both program objects at the one destination address.

An aliased program's `.vclpp` is still generated and still checked: it is the
REFERENCE that `--vu-check` runs the shared image's peer path against over
randomised triangles, which is the only proof a two-path image still does what
the specialised one did. It is simply never linked. A look that overrides one
peer gets its own image, and the built-in one stays for the other.

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
billboard and ordinary bags pays for a swap at every transition.
`StaPipCore::setTelemetryEnabled(true)` now measures both the transition count
(`programSetSwaps`) and total drain/upload time (`programSetWaitTicks`); read and
reset an interval with `takeTelemetry()`. It is opt-in, so the normal render path
does not execute COP0 timing reads.

The cheaper answer for micro-memory pressure is upstream of swapping: **the
editor knows, at build time, which program variants a project can actually use**.
This specialization is implemented by `vuNeededClasses`: it takes the union of
scene objects and spawn-pool prefabs, codegen calls `setResidentClasses`, and
Color is always retained as the fallback. A project with no matcap material does
not upload the two `tce` programs. The all-class fallback remains deliberately
available. The 2026-08-11 CLIP-history and packed-matcap cleanups reduced the
set to 1992 slots; exact active-plane dispatch then spent 38 slots to skip
irrelevant Sutherland-Hodgman passes. ABI-compatible clip variants now share
resident images: `C/D` use one two-stream image and `TC/TCE` one three-stream
image, selected through `VU1_OPTIONS_ADDR.y`; `TD` stays specialised. Path1
deduplicates program objects with the same source range and gives both objects
one destination address. The current exact Sony-VCL all-class set is **1676 of
the 2042 usable slots** (366 spare), down 354 slots from the active-plane build.

Clip packets use a private extension of the standard buffer header. The vertex
count remains in bits 0..9; bits 10..15 carry the six-plane mask in VU dispatch
order. The mask is computed from the actual MVP/clip-margin half-spaces in
object space, not from the view-frustum culling planes, and ORed when source
packages merge into a qbuffer. Non-clip programs retain the unmodified count
word. A zero mask is defensively encoded as all six planes, so manually built
clip qbuffers fail safe instead of silently bypassing clipping.

## Commands

```bash
tyrax-editor --vu-check [engineDir]
```
Parses every `.vclpp` the engine ships, builds each described program, runs both
in the simulator, diffs the GS output, prints the micro-memory budget. Exit 0
only if every handwritten program parsed and every described one matched. This
is the framework's test — there is no unit-test suite in this repo
(`tyra-testing`).

The check also exercises variant one of both shared clip images against the old
specialised `D` and `TCE` programs. Ordinary per-description equivalence only
selects variant zero and is not sufficient proof for a multi-entry image.

And it checks the EE wrappers, which no amount of microcode comparison can:

```
-- EE wrappers: the image they link, and the buffer ABI --
  StaPipVU1ClipD   ALIAS     StaPipVU1Clip_C   2 GS regs, 3 elements/vertex
  StaPipVU1ClipTD  OWN       StaPipVU1Clip_TD   3 GS regs, 4 elements/vertex
```

For every description, the emitted wrapper AND the one in `vendor/tyra` must
declare exactly the image the description says (`vugen::residentImage`) and agree
on the two numbers passed to `StaPipVU1Program` — the GS registers per vertex and
the per-vertex input elements the engine charges when it sizes a package. Both
are invisible everywhere else: a wrapper that links its own image where a peer's
covers it compiles, boots and draws correctly while quietly undoing the sharing,
and a wrong `elementsPerVertex` resizes the VU1 double buffer without changing an
instruction. Reverting the `D` wrapper's symbols by hand is a FAIL here while
every other section still prints IDENTICAL, which is the hole this closes.

Set `VUGEN_DUMP=1` and a mismatch also prints the whole GIF window from both
programs side by side, quadword by quadword. One differing field tells you
almost nothing; the window tells you whether the two disagree about a value, a
vertex COUNT, or where the packet starts — which are three different bugs. It is
how the clip family's `moveInto` slip was found (the fifth emitted vertex was
lerped against the origin, and only the fifth).

```bash
tyrax-editor --vu-emit <outDir> [engineDir]
```
Writes the generated `.vclpp` plus the matching EE-side `.cpp`/`.hpp` for every
described program. It writes to a directory you name, **not** into
`vendor/tyra`: adopting generated microcode is a change that has to be built in
Docker and looked at on hardware, so this stages it for a human to diff first.

The output is copyable as a whole. That is worth saying because it was not: the
wrappers for the aliased `D` and `TCE` programs used to name their own images, so
copying everything this wrote would have relinked five clip bodies and silently
put the resident set back over 2000 slots. They now declare the owner's symbols,
their `.vclpp` carries a "NOT LINKED, and not meant to be" header, and
`--vu-check` fails if either drifts.

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

The candidate list is every microprogram the engine ships **plus every one the
project generated** (`src/gen/*.vclpp`). Leaving the project's own out is not a
gap in coverage, it is a wrong answer: a packet drawn by a project's script
cannot match any shipped program, so the tool would report "not reproduced" for
exactly the case its author most wants explained.

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

**Generated and proven bit-identical: all fifteen StaPip programs** — the five
`as_is` (the family the EE clipper feeds), the five `cull` (transform and
frustum test on VU1) and the five `clip` (Sutherland–Hodgman on VU1, the family
that replaces the EE clipper). Every one of them emits the same instruction
COUNT as its handwritten file — the clip half at 299/259/322/301/282 against
299/259/322/301/282 — which is a stronger statement than equivalence alone: the
generator is not buying agreement with extra work.

**Parsed and simulatable, not yet described:** everything else the engine ships —
all twenty-five `.vclpp` files parse with no diagnostics, including the
`billboard` family and the VU0 raytracer kernel. They can be run, traced and
inspected today; they just do not have a C++ description yet.

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

**Adopted so far** — i.e. the file in `vendor/tyra` is the generator's output and
carries the "Generated by TyraX" marker:

| | `.vclpp` (microcode) | `_program.cpp` (EE wrapper) |
|---|---|---|
| `as_is` C/TC/D/TD/TCE | generated | generated |
| `clip` C, TC | generated | handwritten |
| `clip` D, TD, TCE | handwritten | handwritten |
| `cull` all five | handwritten | handwritten |
| `billboard` | handwritten (not described at all) | handwritten |

The two adopted `clip` bodies are the SHARED images: C covers D and TC covers
TCE, which is what the sharing above is made of, and both were built in Docker
and run in PCSX2 (`docs/vu1-clipping-plan.md`, M10). Their wrappers stayed
handwritten because they carry the alias by hand; `--vu-check` now holds them to
the description instead, so adopting them is a diff-and-copy rather than a
judgement call.

**Not done:** the `cull` family and the three specialised `clip` bodies are
proven equivalent in the simulator but their generated form has not been built in
Docker or run on hardware, and adoption needs the full e2e pass (`tyra-testing`),
not a host check. The `billboard` family has no description.

The `cull` family cost almost nothing to describe once `as_is` existed, and the
shape of that diff is the argument for the whole approach: five new descriptions
(seven lines each) plus **one** structural change to the shared body, because
`cull` is `as_is` with the MVP multiply, the ADC frustum test and the spot light
moved onto VU1 — all three of which the method library already had. The one
genuinely new fact the exercise turned up is that the flashlight has nowhere to
go in the lit and env variants (they compute their colour from normals, and an
env bag carries no lighting at all), which is why `spot` is
`cull && colorStream && !env` rather than just `cull`.

`clip` was expected to be the hard one — Sutherland–Hodgman with real control
flow and scratch polygon buffers in high memory, which an expression-level DSL
will not express — and the prediction was that it would end up 80% generated
with the clipper still artisanal. That turned out to be wrong, and the reason is
worth keeping: **the builder is not an expression DSL.** It has labels, forward
branches, `clipw`/`fcand`, indexed loads and stores; a nested loop reads in C++
about the way it reads in assembly, and `buildClipBody` is one function with no
hand-written instruction blocks in it at all. What the family actually needed
was nine small primitives (`moveInto`, `clipwInto`, `fcandInto`, `iorInto`,
`iandInto`, `isubInto`, `branchIfEq`, `branchIfGtz`, `branchIfLtz`) and the
shared preamble pulled out of `buildAsIsBody` so a third family could call it.

The exercise also paid for itself in bugs the cull family was hiding:

- `moveInto` wrote its source into the wrong operand slot, so `move ppos, cpos`
  copied `vf00` — zeros. No family had used it before.
- `vuir::disassemble` printed `clipw`'s first source as `dst`, so **every**
  emitted program said `clipw.xyz vf00, vertexN` — "judge the origin", i.e.
  nothing is ever outside. That is live on hardware for generated project
  programs. The round-trip stage could not catch it because the cull family's
  own trials never put a vertex outside the frustum; the clip trials do, on
  purpose (`stageInput` spreads them to ±40).
- `Vu::hoist` moved instructions without shifting the code indices that labels
  and unresolved branches remember. Harmless while the only script hook sat in a
  straight run; fatal the moment one sits inside three nested loops.

## VU1 or VU0?

Both, and the difference is **declared** rather than assumed. `vusim::Target`
picks the machine, and everything downstream sizes itself from it:

| | VU1 | VU0 |
|---|---|---|
| Data memory | 1024 quadwords (16 KB) | **256** quadwords (4 KB) |
| Micro memory | 2048 slots (2042 usable) | **512** slots, all usable |
| GIF path | PATH1 — `xgkick` | none |
| Input staging | VIF1 double buffer — `xtop` | fixed addresses, EE stores directly |
| Entry | starts at 0, loops per buffer | `vcallms <addr>`, once per call |
| Data memory between calls | a fresh buffer arrives | **persists** |

Two of those rows are the reason a target exists at all rather than a comment
saying "be careful". Address 300 is a legal VU1 quadword and a WRAP on VU0 —
under the wrong model the run looks plausible, produces numbers, and is fiction.
`xgkick` on VU0 draws nothing on hardware and silently succeeds in a VU1-shaped
simulator. Both are now warnings that name the target.

The `vcallms` contract is modelled by `vusim::runKernel`: a list of per-call
writes applied to **one** data-memory image that carries forward, which is
exactly how `vu0_raytracer.cpp` drives its kernel — the frame-static parameters
(eye, light, sky, the sphere and triangle tables) are stored once, and each row
kick only rewrites the row base and the texel count.

**`--vu-check` runs the engine's VU0 kernel, it does not merely parse it.** The
raytracer is staged with one sphere ten units out and a four-texel row that walks
off it, then executed under the VU0 model:

```
-- VU0: the raytracer kernel, run under the VU0 model --
  504 instructions, 608 steps, 256 quadwords of data memory
  sphere texel (60, 30, 15)   sky texel (160, 192, 225)
  micro memory: 494 instructions -> 247..494 of 512 VU0 slots  fits
  OK - runs, stays inside VU0's 256 quadwords, and shades what it should
```

Those numbers are checkable by hand, which is what makes the check worth having.
The sphere is colour (200, 100, 50); the hit's normal is perpendicular to the
light, so `N·L` is 0 and the shade is the kernel's bare ambient 0.30 — 200×0.3 =
60, 100×0.3 = 30, 50×0.3 = 15. The sky is a lerp between (180, 210, 235) and
(40, 90, 170) on the ray's upward component, and the last texel's ray tilts up by
0.142 — (160, 193, 226) by hand, (160, 192, 225) from the simulator, the
one-unit gaps being the VU's truncate-toward-zero rounding rather than a
disagreement. The assertions are on the *shape* of the answer (a direct-colour
texel, channels in range, the sphere's own hue, a sky between the two stops), not
on those exact triples: the kernel's shading constants are its business, and
pinning them here would make this a change detector instead of a check.

**The generator has two skeletons.** The StaPip one is the VU1 pipeline —
`xtop` a double-buffer half, emit the GIF tag block, loop over vertex triples,
`xgkick`. `buildKernel` is the VU0 shape — fixed input and output ranges in data
memory, one element per loop iteration, no double buffer, no GIF, and nothing to
branch back to. Both take the same stage list. See
[docs/vu-authoring.md](vu-authoring.md).

**What is still VU1-only, and honestly so:** most of the stage library assumes a
vertex pipeline — only the four stages that touch nothing but the position are
`kernelSafe`. The micro-memory ceiling of 2042 is a VU1
fact too (it is the room below `Path1`'s draw-finish helper; VU0 has nothing
parked in its 512 slots, hence the separate `kVu0MicroCeiling`).

And one caveat no simulator can catch, so it belongs in prose: **VU0's register
file is shared with COP2 macro mode**, which is the engine's own `Vec4`/`M4x4`
math. A VU0 micro program and an inline macro-mode expression are the same 32 VF
registers. The raytracer copes by never running concurrently with anything
(`trace()` blocks the EE); a kernel that wants to overlap EE work has to own that
problem itself.

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
