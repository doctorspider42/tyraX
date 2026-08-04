# vu-lab example

A small scene whose job is to be **inspected**, not played. It is the fixture
for the VU framework ([docs/vu-framework.md](../../docs/vu-framework.md)) and
for authoring ([docs/vu-authoring.md](../../docs/vu-authoring.md)).

The headline is `src/vu/cell_shading.cpp` - **ordinary C++, in this project,
that ends up as a VU1 microprogram in the ELF**. The posterise is four
instructions:

```cpp
b.mulInto(s0, c.color.val(), down, vuir::MXYZ);  // into level units
b.truncate(s0, s0, vuir::MXYZ);                  // drop the remainder
b.addInto(s0, s0, halfStep, vuir::MXYZ);         // to the band CENTRE
b.mulInto(s0, s0, up, vuir::MXYZ);               // back to 0..255
```

`down`, `up` and `halfStep` are three lanes of one constant register built in
`prepare()` - one register for three numbers is what makes this fit the lit
class, which already keeps ~30 of VCL's 31 live. Two more instructions paint the
outline shell flat, and that is the whole program. It replaces the engine's
resident program on all four material classes the scene draws, so every prop and
the terrain come out in four flat bands. Measured on the console: **50 FPS**, no
asserts, 218 to 260 instructions per class.

Open `vu-lab.tyra` in the editor and Build & Run (`F5`), or headless:
`tyrax-editor --build <this folder> --run`. The first build with a script
installs a host compiler into the container once (~1 min).

**TRIANGLE takes the script off VU1 and puts it back** - the scene falls back to
the engine's own shading and returns to cell shading, which is how a game frees
micro memory for something else at a trigger or a cutscene beat.

**CIRCLE is the geometry one, and it rearranges VU1 around itself.** It selects
the Wobble - a wave in OBJECT space - and at the same time switches to VU1
clipping and narrows the resident classes to Colour + Textured + Reflective;
pressing it again puts all three back. That combination is the point, not a
detail:

- An object-space displacement under the **EE clipper** happens after the mesh
  has already been cut, so props have to be submitted whole to compensate and
  the **terrain cannot be** - a chunk straddling the near plane wraps the GS
  raster window if it is drawn unclipped. The wave broke along the chunks at the
  edge of the frame.
- Under **VU1 clipping** the clipper is a VU program that does its own MVP
  multiply, so the script runs inside it and the chunk is cut *after* the
  displacement. `fullClipChecks` stays on.
- The clip family is about twice the size of the as_is family it replaces, so
  all four of this project's classes do not fit under the 2042-slot ceiling.
  Dropping Directional lights buys the room; the one dyn-lit ball is **hidden**
  while the mode is on, because a class that is not resident draws in the wrong
  style rather than not at all.

Measured in PCSX2: enters and leaves cleanly, no micro-memory assert, 50 FPS
both ways. The switch is `enterClipMode`/`leaveClipMode` in
`src/scripts/vu_look_switch.cpp` - `vuprog::setResidentClasses` and
`vuprog::setVU1Clipping`, about ten lines.

Edit the script, press Build, and it is a different microprogram - try `2.0F`
bands, or `c.position` at `vu::Slot::ObjectSpace` instead of the colour.

The same authoring exists for the **other vector unit**: `src/vu0/ranges.cpp` is
a `vu::Kernel`, which draws nothing and instead hands the game a class with a
`run()` on it. It costs nothing out of the VU1 budget above - those instructions
live in VU0's own 512 slots. See [the other kernel](#the-other-kernel-written-in-c)
below.

The project also carries three **looks** - the stage-list shortcut, no code -
switched OFF, so the script is unambiguous. Turn one on in *Tools > VU Programs*
to see the other half of the feature; what they demonstrate is below.

## What is in it

Five props in a row in front of the spawn, each deliberately on a different
drawing path, plus a small 40x40 terrain:

| Object | Why it is there | VU parameters |
|---|---|---|
| `flat-ball` | Plain vertex colour, **baked lighting off** (it is displaced). | `0, 1, 0, 0` - Desaturate amount |
| `lit-ball` | `Dynamic lighting` on, so it is in a LIT class - the one a look can only reach with plain values. | none |
| `flat-box` | Plain vertex colour, and it sits at the edge of the spawn view. | none |
| `tall-pillar` | Dead centre, and owns the scene's one flow graph (see below), including the Display Text node the VU0 readout draws through. | `0, 1, 0, 0` - Desaturate |
| `tex-box` | A `map_Kd` material, so the mesh carries an ST stream. | none |
| `chrome-ball` | A `refl` (matcap) material - the ST slot carries a normal instead. | none |

## What it authors

*Tools > VU Programs* shows all of it. The four **C++ scripts** are what the
project actually runs, and they become **43 microprograms** - a script emits one
per class it claims, times up to three halves each (cull, the VU1 clipper, and
the as_is twin unless it moves geometry). Alongside them sit **two VU0 kernels**,
one of each kind, and three **looks** left in the project switched off so the
scripts are unambiguous.

| | Classes | Body | Parameters |
|---|---|---|---|
| **Cell shading** (boot) | Untextured + Textured + both lit | `src/vu/cell_shading.cpp` | shell flag ← mesh **X** |
| **Palette** | Untextured + Textured | `src/vu/palette.cpp` | none |
| **Vertex snap** | all five | `src/vu/vertex_snap.cpp` | none |
| **Wobble** | all five | `src/vu/wobble.cpp` | none - and it moves geometry |
| `points` kernel | VU0 | the stage list, from the panel | values |
| `Ranges` kernel | VU0 | `src/vu0/ranges.cpp` | camera position, band width |
| ~~Toon / Underwater / Power down~~ | — | stage lists, switched OFF | see below |

**One button per look**, and one at a time is the thing being shown rather than
a limitation worked around: a material class carries ONE program, so picking a
look swaps what is resident on VU1. That is `vuscript::activate(want)` in
`src/scripts/vu_look_switch.cpp`.

Every program is in the ELF; only the active one occupies VU1 micro memory,
which is why the budget bar in the panel is per look. Measured on the console
(PAL, frozen camera) back when the three looks were the active set: 50 FPS in
Toon at EE 38%, 49.4 in Underwater at EE 41%, 50 in Power down at EE 43%.

The scene is on the **EE clipper** (*Preferences > Rendering*) rather than the
default VU1 clipping, and that is the point of the setting here rather than an
oversight: a material class is a *pair* of resident programs, the second of which
draws whatever the frustum cuts. A look replaces both halves — but the VU1
clipper has no generated twin, so under VU1 clipping every prop at the edge of
the screen would silently keep the engine's own shading. See
[docs/vu-authoring.md](../../docs/vu-authoring.md), "One class is two programs".

`Toon` is the shape the feature is for, and it reaches a **lit** class because
every one of its parameters is a plain value: the four per-mesh numbers live in
the directional-lights colour block, so it is the *binding* that would have shut
it out, not the class. `lit-ball` is there to make that path real rather than
theoretical. `Scroll UV` is skipped on Reflective (its ST slot carries a normal)
and on Directional lights (no ST at all), each with the reason shown.

**What Underwater does NOT cover, and why it is worth seeing.** `chrome-ball`
draws twice - an untextured base plus an env (matcap) pass on its own class - and
a look can only displace the pass whose class it claims. So the base ripples, the
reflection stays where it was and cuts through it. The fix is not available here:
a sine on the matcap program runs out of VF registers. That is the shape of the
rule in the docs - a displacing look has to claim EVERY class an object's passes
land in - made visible, which is what this scene is for. `lit-ball` is the same
story from the other side: its lit pass is on a class no look here claims, so it
sits still while the scene ripples around it.

`flat-ball` has **Baked lighting off**, and that is not tidying: a lightmap pass
carries the AO atlas, which makes it a *Textured* bag even on an untextured
mesh, so `Underwater` displaced the ball's main geometry and left its lightmap
behind as a translucent ghost of the undeformed sphere. See
[docs/vu-authoring.md](../../docs/vu-authoring.md) — the inspector now warns
about that combination.

That is the whole design in one screen. A look replaces a **material class**, not
an object - VU1 micro memory has no room for one program per mesh - so the KIND
of effect is shared and its STRENGTH is per mesh. `flat-ball` asks for wobble,
the pillar asks to go grey, and anything that leaves its four numbers at zero
renders exactly as it would with no program at all (`--vu-check` proves the
bit-exact zero-strength identity on the host, for every stage of every class).

What the swap is worth is measured, not asserted. Freeze the camera
(`walkSpeed`/`lookSpeed` 0), shoot one frame in Toon, press TRIANGLE, shoot
another:

```
scene rect        111 482 / 1 254 500 px differ
  sky                    0 px   a separate pass - no material class, no look
  the yellow ball   40 763 / 50 400   hard toon bands -> smooth shading
  the pillar        18 720 / 18 850   posterized -> desaturated
  the terrain          666 / 252 000  flat tiles: already at the posterize step
  the red box            4 / 61 250   likewise
```

The last two lines are the interesting ones. A look is global, but posterizing a
surface that is already one flat colour is a no-op — what a look changes is what
had a gradient to quantize. Nothing outside the pipeline moves at all.

## The VU0 kernel, actually running

`src/scripts/vu0_compute.cpp` is a **user-owned** script - the editor writes
generated sources into `src/gen/` and never touches `src/scripts/`, which is
also the only place a kernel can be driven from: a kernel is a compute job with
no place in the scene pipeline, so nothing calls it unless your game does. It
does nothing but run the two kernels and report what came back; the pad and the
resident-program juggling live next door in `vu_look_switch.cpp`.

Each frame it feeds 32 points to the kernel, takes the first one back, and
writes it into the pillar's per-mesh VU parameters. So the pillar's colour is
the output of a **VU0** computation, rendered by a **VU1** microprogram:

```
VU0 computes  ->  the EE reads it back  ->  VU1 renders with it
```

The console logs one line at startup, and it is worth checking by hand because
it is checkable by hand:

```
LOG: VU0 kernel: point 0 in (4, 0) out (4, -0.108301)
```

Point 0 is `(4, 0, 0, 1)`. Wobble's angle is `(x + z) * 0.8 = 3.2` rad; the
generated series gives `sin(3.2) ~= -0.05776`; times the amplitude 1.5 is
`-0.0866`; Squash then scales Y by 1.25, giving **-0.1083**. X is untouched
because Squash's X is 0. Getting a different number by hand is how the
`Vu::constants` w-field bug was found - see docs/vu-authoring.md.

### The other kernel, written in C++

`src/vu0/ranges.cpp` is a `vu::Kernel` — the VU0 twin of `src/vu/*.cpp`, and
the way out of the stage catalogue when the arithmetic you want is not in it.
It answers the oldest question a PS2 game has, for the whole scene in one call:

> how far away is everything?

One subtract, one dot product and one square root per object, over the array
the game already has. The answers pick levels of detail, decide what is worth
drawing and set how loud a sound is; the demo uses them to name the nearest
prop, which is what a *press USE* prompt needs.

**And it is on screen**, top centre, updating as you walk:

```
VU0: nearest #2  2.1u  LOD 0
```

That line is the whole round trip in one readable place — **VU0** computed the
distances, the **EE** folded them to a winner (VU0 has no cross-element
reduction, so finding a minimum is the CPU's job by construction), and **VU1**
drew the answer. A kernel whose only output is a log line is a claim; this is a
demonstration.

The slot it draws into comes from a **Display Text** node on `tall-pillar`'s
graph that nothing ever fires: the node exists to allocate the slot and to say
where and how big, while the string and the on/off are the script's
(`ctx.dynTextBuf`, `ctx.dynTextRequest`). That is the general pattern for a
runtime readout — a node for the placement, a script for the content.

```
LOG: VU0 Ranges: nearest object is #0 at 1.8 units, LOD band 0
LOG: VU0 Ranges: 39 objects, worst disagreement with the EE 9.53674e-07 units
```

The second line is the point. The script computes the same distances with
`sqrtf` on the EE and prints the worst disagreement — **1e-6 units**, which is
single-precision float and not an approximation anyone chose. "The kernel ran"
and "the kernel is right" are different claims.

Two things it makes concrete that the stage-composed one cannot:

- **The square root.** `rsqrt` writes Q, which a VU1 script must never do — the
  framework owns Q there for the perspective divide. A kernel has no framework
  around the body, so a root is ordinary microcode. The generator says so in a
  note instead of refusing it.
- **No cross-element reduction.** Finding the nearest object is a loop on the
  EE, because VU0 has no instruction that folds one element into another. VU0
  computes the terms; the CPU folds them. Every "compute a sum on VU0" idea
  lands here.

### Three things the example is shaped to teach

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

**Two stages per program is not an arbitrary demo size.** A third one on the
colour program does not build: VCL runs out of VF registers and dies with `no
opt table`, because a cull-family program already keeps ~23 of the 31 live and
the sine costs about five more. The measured cliff is exact - 35 allocated, 36
did not - and the *Micro memory* tab prints the estimate next to the slot bar
for this reason. The fix when you hit it is to split the effect across two
material classes, which is what this example does: the wobble is on the colour
program and the UV scroll is on the textured one, each with its own 31.

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
