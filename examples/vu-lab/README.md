# vu-lab example

A small scene whose job is to be **inspected**, not played. It is the fixture for
two things: the VU framework
([docs/vu-framework.md](../../docs/vu-framework.md)) - take a VU1 packet off the
running console and re-run it on your PC bit for bit - and **authoring**
([docs/vu-authoring.md](../../docs/vu-authoring.md)): it ships with a
microprogram of its own, composed out of stages, with no assembly anywhere.

Open `vu-lab.tyra` in the editor and Build & Run (`F5`), or headless:
`tyrax-editor --build <this folder> --run`.

## What is in it

Five props in a row in front of the spawn, each deliberately on a different
drawing path, plus a small 40x40 terrain:

| Object | Why it is there | VU parameters |
|---|---|---|
| `flat-ball` | Plain vertex colour. | `0.45, 0, 0, 0` - Wobble amplitude |
| `flat-box` | Plain vertex colour, and it sits at the edge of the spawn view. | `0, 1, 0, 0` - Desaturate |
| `tall-pillar` | Dead centre, and owns the scene's one flow graph (see below). | `0, 1, 0, 0` - Desaturate |
| `tex-box` | A `map_Kd` material, so the mesh carries an ST stream. | none |
| `chrome-ball` | A `refl` (matcap) material - the ST slot carries a normal instead. | none |

The scene keeps VU1 clipping on (*Preferences > Rendering*), which is the default
- so the programs that actually run are the `clip` family, not `as_is`.

## The authored program

*Tools > VU Programs* shows it: one program on the **untextured colour** class,
two stages.

| Stage | Parameter | Bound to |
|---|---|---|
| Wobble | Amplitude | mesh **X** |
| | Frequency 0.6, Speed 3 | baked into the microprogram |
| Desaturate | Amount | mesh **Y** |

That is the whole design in one screen. The program replaces a **material
class**, not an object - VU1 micro memory has no room for one program per mesh -
so the KIND of effect is shared and its STRENGTH is per mesh. `flat-ball` asks
for wobble, the pillar asks to go grey, `tex-box` and `chrome-ball` are on other
classes entirely and never see it, and anything that leaves its four numbers at
zero renders exactly as it would with no program at all.

That last claim is not a promise, it is measured. Freeze the camera
(`walkSpeed`/`lookSpeed` 0) and take two shots, one with the parameters as
shipped and one with every object's set to zero:

```
scene-only difference: 188 540 pixels, in exactly three column bands
  x 1529..1661   the pillar   (blue -> grey)
  x 1739..2168   the ball     (round -> wobbling)
  x 3039..3055   the emulator's own FPS text
```

Nothing else in the frame moves. The chrome ball, the textured box, the terrain
and the sky are pixel-identical between the two runs.

### Two things the example is shaped to teach

**A custom program only reaches packages fully inside the frustum.** `flat-box`
carries the same Desaturate parameter the pillar does, and in the spawn view it
stays RED - it is cut off by the right edge of the screen, so its package is
classified as frustum-crossing and drawn by the untouched `clip` program instead
of the overridden `cull` one. Walk toward it until it is fully on screen and it
turns grey. That is the honest limit of `setProgramOverride`, and this is what it
looks like rather than a paragraph about it.

**Dropping material classes is what makes room.** The first console run of this
example died on the engine's own assert:

```
| Assertion failed!
| VU1 pipeline programs overflow into the draw-finish program
| File : src/renderer/core/paths/path1/path1.cpp:145
```

With VU1 clipping on, the ten resident programs are five `cull` plus five
`clip`, and the clip family is big - close enough to the 2042-slot ceiling that
a custom program of any size pushes it over. vu-lab draws nothing lit, so
codegen emits `setResidentClasses(25)` (colour, textured, matcap) and the two
dropped lighting classes pay for the stages. *Tools > VU Programs > Micro
memory* is where that bar lives, and it reads the ENGINE's own `.vclpp` files -
budgeting against the generator's descriptions alone is what let this ship green
and assert on the console.

## The loop this example exists for

**1. Run it, and capture one draw.** *Debugger > VU > "Capture next VU1 packet"*.
A frame sends one DMA chain per bag flush, and the flush picker chooses which
draw you get. The file lands in `bin/vucap.bin`.

**2. Look at what the EE sent.** Headless:

```bash
tyrax-editor --dump-vucap examples/vu-lab
```

DMA tags, VIF commands, every `UNPACK` with its VU1 destination, the vertex
stream in model space.

**3. Re-run that draw on your PC and diff it against the console.**

```bash
tyrax-editor --vu-replay examples/vu-lab
```

This is the interesting one. The capture holds both halves of an experiment: the
chain the EE built (the input) and a snapshot of VU1 data memory taken once the
microprogram went idle (the output). So the input is reconstructed, fed to the
host VU1 simulator, and the result compared against what the hardware actually
produced:

```
capture: frame 4087, 8 quadwords, 1 mesh(es), flush 20 of 25
48 candidate runs kicked a packet, 25 had one to compare

REPRODUCED - the host simulator produced the console's packet exactly:
  stapip_clip_tc_vu1.vclpp   (StaPipVU1Clip_TC)
      buffer half 22, kicked quadword 132, TRIANGLE +TEX +ABE
      [ST, RGBAQ, XYZF2], 36/36 vertices identical
```

Every GS vertex matches bit for bit — screen X/Y in 12.4, the 24-bit Z, the
clamped colours and the perspective-corrected ST.

Note it reports **two** candidates for this draw (`clip_tc` and `cull_tc`), and
that is the honest answer rather than a limitation: for a mesh entirely inside
the frustum the clip program's Sutherland–Hodgman pass changes nothing, so the
two stage identical output and no amount of looking at the output can separate
them.

**4. Read the microprogram that drew it.**

```bash
tyrax-editor --vu-list vendor/tyra/engine/src/renderer/3d/pipeline/static/core/programs/clip/stapip_clip_tc_vu1.vclpp
```

## Two things this scene had to be built around

**A capture needs a flush with ONE mesh.** The replay can only reconstruct the
last mesh of a chain — everything before it has been overwritten in VU1 memory by
definition. Flush 0 here carries 14 meshes (terrain chunks) and is not
resolvable; the later flushes carry one prop each and are. The picker exists for
exactly this. Five sparse props is the whole reason the scene looks like this.

**The devkit layer disappears if nothing is instrumented.** The Live Debugger
runtime is generated from the flow-graph nodes a project has, so a scene with no
graph at all compiles `src/gen/live_debug.gen.cpp` down to an empty translation
unit — and then there is no command channel, and *"Capture next VU1 packet" does
nothing at all*. That is the zero-cost rule working as designed, but it is
surprising when you are building a capture fixture. `tall-pillar` carries a
two-node graph (`On Start` → `Set Object Visible`) that does nothing visible; it
is there to keep the devkit layer alive.

## It is also the A/B fixture for adopting generated microcode

The five `as_is` programs in `vendor/tyra` are generated (docs/vu-framework.md),
and this scene is how that was proven: with *Preferences > Rendering* set to the
**EE clipper** (`precise`) the `as_is` family is what draws, so a frame here
exercises the generated code. Handwritten build against generated build, frozen
camera, PCSX2: **0 differing pixels of 1 258 400**. The same build was then run
on a physical PS2 over ps2link and looked right on the TV.

Freezing the camera (`walkSpeed`/`lookSpeed` 0) is what makes that comparison
meaningful - without it the two shots are of different moments and a pixel diff
says nothing. The scene ships walkable; the A/B copy sets those to zero.

## What this measured, and what it cost to get right

The replay is also how the simulator's arithmetic got fixed. The first run
reproduced screen X and Y **exactly** and missed the 24-bit Z by one or two units
in the last place, every time — which is the signature of a rounding difference,
not a wrong input: the VU FPU truncates toward zero where an x86 rounds to
nearest-even, and Z is the coordinate scaled by 8388607.5 rather than 2048, so it
is where a single-ULP mantissa difference shows up first. With the host rounding
mode switched for the duration of a run, the packet matches whole.

Two earlier versions of the tool "succeeded" and were wrong, which is worth
knowing if you extend it: seeding the simulator from the whole memory snapshot
left the console's own output packet sitting in memory, so the scan found it no
matter what ran and all 25 candidates "matched"; and comparing the biggest
geometry packet in memory picked the EE's own PRIM tag at `buffer+1` followed by
the vertex array read as GS vertices — the input compared against itself. The
comparison now runs at the address the candidate program actually kicked, and
anything overlapping what the chain uploaded is discarded.
