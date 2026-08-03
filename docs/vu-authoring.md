# Authoring VU programs

Two ways to put your own code on the VU, and they are not alternatives so much
as different distances from the metal:

| | |
|---|---|
| **A script** — `src/vu/*.cpp` | You write C++. It runs on the HOST at build time and leaves behind a microprogram. Anything the framework can do, you can do. **This is the main road.** |
| **A look** — the stage list in *Tools > VU Programs* | You tick classes and pick from a catalogue of stages. No code, no build container, and a live preview in the editor. The shortcut for the cases the catalogue already covers. |

Both produce the same thing - a program that REPLACES the engine's resident VU1
program for a material class - so everything this page says about classes,
budgets and the traps applies to both.

[docs/vu-framework.md](vu-framework.md) is the machinery underneath: the
instruction model, the parser, the host simulator and the emitter.

---

## Writing a script

Put a `.cpp` under `src/vu/`. That is the whole setup: the editor notices it,
copies the framework next to it (`vugen/`, so VS Code resolves the include), and
the build compiles and runs it.

```cpp
#include "vushader.hpp"

struct CellShading : vu::Program {
  const char* name() const override { return "Cell shading"; }
  unsigned classes() const override {
    return vu::kColour | vu::kLit | vu::kTextured | vu::kMatcap;
  }
  vu::Slot slot() const override { return vu::Slot::Color; }

  void vertex(vu::Ctx& c) override {
    vu::Vec lit = c.color + vu::splat(c, 46.0F);        // lift the floor
    vu::Vec q = lit * vu::splat(c, 4.0F / 255.0F);
    c.raw().truncate(q.val(), q.val(), vuir::MALL);      // floor to 4 bands
    c.color = q * vu::splat(c, 255.0F / 4.0F);
  }
};
VU_PROGRAM(CellShading);
```

That is `examples/vu-lab/src/vu/cell_shading.cpp`, unabridged, and it runs on
the console at 50 FPS.

### Turning one on when the game says so

By default a script installs at boot. Say so otherwise, in the same file:

```cpp
bool activeAtBoot() const override { return false; }
```

and the game decides:

```cpp
vuscript::activate(vuscript::kCellShading);    // a trigger, a cutscene beat
vuscript::deactivate(vuscript::kCellShading);  // the engine's program comes back
vuscript::deactivateAll();
vuscript::active(vuscript::kCellShading);      // is it on?
```

The index constants are generated from the script's `name()`, so there is
nothing to keep in step by hand.

Verified on the console (`examples/vu-lab`, TRIANGLE): with the script off the
scene renders exactly as the engine's own programs draw it, and turning it back
on returns a frame **bit-identical** to the one before the switch - 23 340 pixels
change each way, zero between the two "on" frames, no asserts, 50 FPS throughout.

**Only what is ACTIVE occupies micro memory.** That is what makes this worth
having rather than a flag inside the shader: a game can carry more programs than
fit on VU1 at once, as long as it does not turn them all on together. The cost of
a switch is one pipeline drain and one program-cache upload - fine on an event,
not fine per frame.

### Reading the micro-memory budget

The bar is the RESIDENT set: for every material class the project draws, the
engine's cull program plus its twin (the clipper under VU1 clipping, the as_is
program otherwise). The class rows show what each one costs.

A look or a script does not ADD to that - it REPLACES one of those programs, so
what the script rows report is the **difference**: `299 slots, +94 over the
engine's`. Only that +94 moves the bar. Each script appears twice per class
because a class is two programs, and the second is labelled `(frustum-cut half)`
- that is the one drawn when a mesh crosses the edge of the screen.

A script with `activeAtBoot() false` is listed but not counted, marked
`[off at boot]`.

### Where it shows up in the editor

- **Project panel > Scripts > VU programs** - every `src/vu/*.cpp`, click to open
  in VS Code. *New script...* creates one from a working stub (pick *VU program*
  instead of *Game script*).
- **Tools > VU Programs > Micro memory** - a row per emitted microprogram with
  its slot cost, counted into the same budget bar as everything else. These come
  from a manifest the last BUILD wrote: the editor has no compiler to run a
  script with, so the numbers are as stale as your last build and the panel says
  so.

There is no live preview of a script's VCL the way there is for a look, for the
same reason.

### What the body is handed

`vu::Ctx` is the program's REGISTERS, not a copy of them: reading one is free,
assigning to one writes the register the emitter keeps using afterwards.

| | |
|---|---|
| `c.position` | the vertex, in the space `slot()` names |
| `c.color` | 0..255 per channel, the GS scale (128 = "unmodulated") |
| `c.uv` | texture coordinates, when `c.hasUv()` |
| `c.normal` | WORLD space, on a lit class, when `c.hasNormal()` |
| `c.params` | the object's four numbers, from the inspector |
| `c.time` | `(seconds, sin, cos, 1)`, already range-reduced |
| `c.scratch(0..3)` | registers the framework already reserved for you |
| `c.raw()` | the builder itself - madd chains, masked writes, `loi`, `ftoi`, the sine approximation, `xgkick` |

A `vu::Vec` is one virtual VF register and every operator is one VU instruction.
That correspondence is deliberately visible: this is a way to write microcode,
not a way to pretend the VU is a GPU.

### Where it runs

`slot()` picks the point in the pipeline:

| Slot | The vertex is | Use it for |
|---|---|---|
| `ObjectSpace` | the model's own units, before the MVP multiply | moving geometry |
| `ClipSpace` | after the MVP, w = view distance | depth tricks, fog |
| `Ndc` | after the perspective divide | screen-space snapping |
| `Color` (default) | — colour after lighting and texturing, before the clamp | shading, palettes, tints |

### Three rules the compiler will not tell you

**Constants belong in the preamble, and `vu::splat` puts them there.** `loi`
writes the I register; a run of them inside the per-vertex body is something vcl
schedules around, and the hardware then reads an I the host simulator never saw.
Building constants by hand through `c.raw()` inside the body is the one way to
reproduce this - the first console run of the script above came out as rainbow
noise until the constants were hoisted.

**Registers run out before micro memory does.** VCL allocates 31, and the
directional-light program already keeps about 30 of them live. Cell shading
written in 0..1 and converted back cost two constants too many and died as `no
opt table` from vcl, inside Docker, with no line number; the same effect done in
the GS's own 0..255 scale fits. Prefer `c.scratch(n)` to long expressions, and
suspect the register ceiling first when a build fails in the assembler.

**A script's `Slot::ObjectSpace` half does not follow a mesh through the
clipper** - see "One class is two programs" below, which is the same rule the
stage list lives under.

### What the build actually does

```
src/vu/*.cpp  +  vugen/*.cpp        your script and the framework
        |  g++ (in the build container, installed once)
        v
     a generator                     runs on the HOST, at build time
        |
        v
src/gen/vu_script*.vclpp + _program.cpp + vu_scripts.gen.{hpp,cpp}
        |  vclpp -> vcl -> dvp-as    the engine's own chain
        v
     the ELF
```

The container ships no host compiler (it is a ps2dev image), so the first build
with a script installs `g++` once - about a minute, stamped, and paid again only
if the container is recreated. A mistake in your file is an ordinary C++ error
with your own filename and line number.

`vuscript::install(core)` is called by the generated game right after
`vuprog::install`, so a script wins over a look claiming the same class. With no
script the header is a stub and every call folds away.

---

## The panel

*Tools > VU Programs*, four tabs.

- **Micro memory** — the resident set as a bar against the real 2042-slot
  ceiling, one row per material class with what that class costs, and the mask
  **auto-detected from the scenes and prefabs**. A class the project draws but
  the mask drops is flagged: dropping it does not crash (the engine walks down
  to a resident relative) but those meshes lose the feature, which reads as a
  material bug rather than a settings one.
- **Programs** — the looks. Each carries a name, the classes it applies to
  (with the object count per class, so an abstract class name becomes *these
  things*) and its stage list. A new look starts covering **everything the
  project draws**: narrowing it should be a decision, not something the default
  makes for you by omission. A stage that would emit nothing is drawn dimmed
  with the reason on hover, so "I added it and nothing happened" has an answer
  on screen. Each parameter is a plain value — meaning every mesh — or a
  mesh-slot binding.
- **Generated** — the `.vclpp` this exact stage list produces, and **Run it on
  the host**: the program executed in the VU simulator over a small synthetic
  mesh, printed as GS vertices, *twice* — once with the panel's mesh parameters
  and once with them at zero. The second column is the identity the whole design
  rests on, and having it next to the first is what makes it a fact rather than
  a promise. It also reports Q clobbers, which is the one mistake the assembler
  will not catch.
- **VU0 kernel** — the same stages on the other unit, with the instruction count
  against VU0's 512 slots.

Per-mesh numbers are set in the object inspector, under **VU program**, which
first names the **class this object is in** and the look covering it. The four
sliders are labelled with what actually reads them (`Wobble Amplitude`) — from
THAT look only, because labelling them from every look in the project is worse
than saying nothing: an untextured box would offer "Scroll UV Speed U" from a
program that will never draw it. When no look covers the object's class the
sliders are gone and it says so, rather than showing four numbers that do
nothing.

## What this is for

**A look, not an effect on one object.** This is the layer for giving a whole
scene a treatment — cell shading, an underwater wobble, a posterized palette.
It is not the way to make one barrel wobble; that is an animation's job, and
paying a microprogram for it would be absurd.

That framing decides the shape of everything below, so it is worth being blunt
about it: a **literal** parameter means *every mesh of every class this look
covers*, and that is the normal case. Binding a parameter to a **per-mesh slot**
is the exception, for when the treatment has to vary in strength across the
scene.

## The shape of the thing

A look is **one ordered list of stages, installed over every material class it
claims**.

```
name:    "Underwater"
classes: Untextured + Textured + Reflective   (a StaPipProgramClass mask)
stages:  [ Wobble(amp, freq, speed), Desaturate(amount), ... ]
```

Codegen emits **one microprogram per claimed class**, all from that one list —
so you author once. A treatment that stops at one class is not a treatment: cell
shading on untextured props but not on textured ones just reads as broken.

Each stage has up to four parameters, and each parameter is either

- a **value**, baked into the microprogram — free, constant, and applied to
  every mesh of every claimed class; or
- bound to one of **four per-mesh slots**, a quadword the game uploads next to
  the MVP matrix, so individual objects can drive the same effect differently.

A stage a class cannot carry — a UV scroll on untextured geometry — is **skipped
for that class with the reason shown**, not refused. Refusing would mean a
scene-wide look could never include a stage that only some classes support,
which is most of them.

Two rules the class mask is subject to:

- **One look per class.** `setProgramOverride` replaces a slot and a slot holds
  one program, so a second look claiming a taken class is skipped and said so.
- **A per-mesh binding restricts the mask to the unlit classes.** See below —
  and note it is the *binding* that restricts, not the look: an all-values look
  reaches lit geometry perfectly well.

## Why one program per material class

VU1 micro memory holds **2048 instruction slots**, and the StaPip pipeline
already keeps ten programs resident in them (five `cull` + five `as_is` or
`clip`, depending on the clipping mode) with the top six slots reserved for
`Path1`'s draw-finish helper. There is no room for a program per object, and
`StaPipCore::setProgramOverride` reflects that: it installs your program **over
a built-in slot**, for the whole class.

So a custom program does not apply to *an object*. It applies to *every mesh
that draws with that material class*.

Two consequences you cannot design around:

1. **A stage must be the identity when its strength is zero.** This is checked,
   bit for bit, by `--vu-check` — every stage in the catalogue is built with its
   strength bound to a mesh slot, run against the engine's own handwritten
   program over randomized vertices, and must produce an identical GIF packet.
   That is what makes installing a program safe: meshes that want nothing get
   exactly the pixels they would have got. They still pay the instructions.
2. **Batched objects share a bag, and therefore share parameters.** The
   generated game merges non-moving primitives into combined bags
   (`staticBatchEligible`); one bag is one `sendObjectData`, so one parameter
   quadword. If two props need different strengths, they need to be different
   bags.

A per-object *program* would also break static batching outright — a merged bag
cannot hold two programs, so every such object would become a batch blocker.
Parameters cost neither micro memory nor batching.

## Which classes a look can claim

All five, with one condition. Every one of them is a `cull` program, and that is
not arbitrary:

- **`cull` is the only family resident in both clipping modes.** `as_is` is
  uploaded only with the EE clipper, `clip` only with VU1 clipping (the default)
  — there is no micro memory for both. A program built on `cull` draws under
  either setting.
- **`cull` is the only family that still has the object-space position.** The
  `as_is` family is fed NDC positions by the EE clipper: by the time it runs
  there is nothing left to displace in model space.

| Class | Replaces | Carries |
|---|---|---|
| Untextured | `StaPipCullColor` | vertex colour |
| Directional lights | `StaPipCullDirLights` | normals, shaded on VU1 |
| Textured + lights | `StaPipCullTextureDirLights` | ST + normals |
| Textured | `StaPipCullTextureColor` | vertex colour + an ST stream |
| Reflective | `StaPipCullTextureEnv` | matcap — the ST slot holds a **normal** |

**The condition is about per-mesh parameters, not about the classes.** The four
numbers and the clock live at `VU1_LIGHTS_COLORS_ADDR`, which a lit program
needs for its own light colours — so a look that **binds** a parameter to a mesh
slot cannot claim `Directional lights` or `Textured + lights`. A look made only
of values never reads those addresses and may claim anything.

That distinction is the difference between "cell shading works on unlit props"
and "cell shading works on the scene", so the panel greys the two lit classes
out only while a binding exists, and says why.

On `Reflective` the ST slot holds an object-space normal rather than a texture
coordinate, so `Scroll UV` is skipped there rather than silently scrambling the
matcap.

### An object's passes do not all share its class

This one is not obvious and it looks like a rendering bug when it bites.

An object is not one bag. Its main geometry is one, and a **baked lightmap adds
another** — carrying the AO atlas, which makes it a **Textured** bag *even on an
untextured mesh* (`part.aoBag->texture = aoTexBag`, lighting null). Same for the
additive emissive pass.

So a look that **displaces** geometry on the Untextured class, applied to a mesh
that has a baked lightmap, moves the main bag and leaves the lightmap pass
where it was: a translucent ghost of the undeformed shape, sitting behind the
wobbling one. That is exactly what `examples/vu-lab` showed on the console
before anyone worked out why.

Two ways out, and the first is usually the right one:

- **Turn baked lighting off for that object.** A lightmap baked for a shape the
  mesh no longer has is wrong anyway — it was computed against the undeformed
  geometry. The inspector warns about this combination by name.
- **Give the look the Textured class too**, so both passes are displaced by the
  same stages with the same per-mesh numbers (`setVuParams` is staged per
  OBJECT, before all of that object's bags, so they agree automatically). Costs
  a second microprogram and its register pressure.

The general rule, worth holding on to: **a displacement look has to claim every
class an object's passes land in.** Colour stages do not have this problem — the
lightmap pass carries its own colours.

And a lightmap is not the only second pass. Measured on the console, in this
order of nastiness:

| Object | Passes | What a displacing look does to it |
|---|---|---|
| `refl` material (matcap) | untextured base + **env pass on TextureEnv** | the base ripples, the reflection stays put and cuts through it |
| `Dynamic lighting` on | the lit bag on **Directional lights** | untouched by a Colour look — but it will tear if the look claims Colour *and* the object also has an unlit pass |
| Baked lighting | main bag + **AO atlas on Textured** | the ghost above |

And the trap inside the trap: those extra classes are exactly the ones a
displacing stage often **cannot** be compiled into. A sine on top of the matcap
or directional-light program runs out of VF registers (`no opt table`, see
below), so "just claim that class too" is not always available. When it is not,
the honest answer is that displacement and that object do not go together —
which is a scene-authoring decision, and one the panel now states rather than
leaving you to discover it on a TV.

### The honest limitation: which packages your program actually draws

The renderer classifies each package by its bounding box and sends it to the
`cull` program (entirely inside the frustum) or the `clip` program (crossing
it). Overriding `StaPipCullColor` therefore reaches **the packages that are
fully inside the view**. A package straddling the screen edge draws with the
untouched clip program, and a strong displacement will visibly pop as an object
crosses that boundary.

For an effect on props well inside the view this never shows. For anything large
enough to straddle the frustum edge, it does. There is no free fix: the `clip`
family is Sutherland–Hodgman with real control flow, it is not generated, and
replacing it with a cull-shaped program would trade the pop for triangles being
culled instead of clipped at the screen edge.

## The slots

A stage declares **when** it runs, and that is the entire pipeline model an
author has to hold:

| Slot | What the position is | Good for |
|---|---|---|
| `ObjectSpace` | the model's own coordinates, W = 1 | displacement, scale, anything a modeller would recognise |
| `ClipSpace` | after the MVP multiply; xyzw, **W is the view distance** | depth work |
| `Ndc` | after the perspective divide, before the 12.4 conversion | screen-space effects |
| `Color` | the vertex colour, before `FixColor` clamps to 0..255 | tinting, stylisation |
| `Texture` | the ST, before the perspective correction | scrolling, offsets |

Order inside the per-vertex chain, with the engine's own work shown:

```
  load vertex / st / colour
    [Texture]      <-- before the perspective divide, or your offset is scaled by distance
    [ObjectSpace]  <-- vertex AND colour both available, in model units
  spot light (flashlight)
  MVP multiply
    [ClipSpace]    <-- before the fog coefficient and the frustum test, on purpose:
                       a stage that moves a vertex in depth moves its fog with it
  fog coefficient + ADC frustum test
  perspective divide  (writes Q)
    [Ndc]          <-- NOTHING HERE MAY WRITE Q: the texture correction below
                       is still holding the divide's result
  scale to GS 12.4
  texture perspective correction (reads Q)
    [Color]        <-- before FixColor, so you may overshoot and let the clamp catch it
  FixColor
  store
```

Two of those comments are traps that cost time if ignored. **A `div` or `rsqrt`
in the `Ndc` slot silently corrupts the texture coordinates** of every textured
mesh — the simulator reports it as a Q clobber (`Result::qClobbers`) and nothing
else will. And **a `Texture` stage after the perspective correction** would have
its offset divided by W, so a fixed scroll speed would slide faster on distant
geometry.

## The stage catalogue

Cost is emitted instructions **for the whole program** — three vertices, so
divide by three for the per-vertex figure. VCL pairs an upper and a lower op
into one slot where it can, so the micro-memory cost is between half of this and
all of it.

| Stage | Slot | Cost | Parameters |
|---|---|---|---|
| **Wobble** | Object | +89 | Amplitude*, Frequency, Speed |
| **Twist** | Object | +155 | Strength*, Speed |
| **Inflate** | Object | +86 (+15 with a literal-zero Pulse) | Amount*, Pulse*, Speed |
| **Squash / stretch** | Object | +15 | X*, Y*, Z* |
| **Height shade** | Object | +32 | Amount*, Scale, Bias |
| **Depth bias** | Clip | +9 | Bias* |
| **Vertex snap** | NDC | +32 | Steps (literal only), Strength* |
| **Pulse colour** | Colour | +83 | Amount*, Speed |
| **Posterize** | Colour | +32 | Levels (literal only), Strength* |
| **Desaturate** | Colour | +29 | Amount* |
| **Scroll UV** | Texture | +16 | Speed U*, Speed V* |

`*` marks a **strength** parameter: when every one of a stage's strengths is a
literal zero, the stage is not emitted at all. Leaving an experiment in the list
at zero costs nothing. Binding a strength to a mesh slot never folds — the game
may write anything into it.

Two parameters are **literal only** (`Steps`, `Levels`): the generator folds a
reciprocal for them at build time, so there is nothing to read from a mesh. The
build refuses a mesh binding on one rather than ignoring it.

### The sine, and what it costs

Anything time-varying needs a sine, and the VU has none. The generator emits a
**parabolic approximation with one correction term** — 17 instructions per
evaluation, accurate to about **0.2%**:

```
t = angle/2pi + phase          phase = 0.5 for sin, 0.75 for cos
u = t - floor(t)               floor via the 2^23 add/sub trick
x = 2u - 1                     in [-1,1), so angle = pi*x
y = 4x(1-|x|)                  ~ sin(pi x), 5.6% error
r = y + 0.225*(y|y| - y)       ~ sin(pi x), 0.2% error
```

A Taylor series is not the alternative it looks like: it needs range reduction
to converge, and range reduction on a VU costs more than this whole
approximation.

Three things follow from the shape:

- **It is exact at every quarter turn.** sin(0) is exactly 0 and cos(0) exactly
  1, which is why a Twist at strength 0 is the bit-exact identity. That only
  holds because the cosine is phased *inside* the reduction — adding pi/2 to the
  angle instead gives cos(0) = 1 - 6e-8, which shifts the 24-bit Z by a few
  units on every vertex and breaks the identity contract.
- **The floor trick has a range.** Adding 2^23 pushes the fractional bits off
  the mantissa so the VU's truncate-toward-zero becomes a floor; that stops
  working as `angle/2pi` approaches 2^23. In practice the limit is the clock:
  wrap it (see below) and there is no problem.
- **Three vertices, three evaluations.** A stage in the per-vertex chain runs
  three times per triangle group. That is where the 89 instructions of Wobble
  come from — one sine is 17, and the loop is unrolled by three.

### The clock

`VU1_CUSTOM_TIME_ADDR` is one quadword: `(time, sin time, cos time, 1.0)`,
uploaded per mesh alongside the parameters. `time` is seconds.

**Wrap it.** The generated game does, but if you drive it yourself, a float
seconds counter loses fractional precision after a few hours and the 2^23 floor
trick loses it sooner. Wrapping at a multiple of `2*pi` keeps a stage running at
speed 1.0 continuous across the wrap; a stage at any other speed shows a
one-frame discontinuity there and there is no way around that short of a phase
per stage.

`sin time` and `cos time` are supplied because the EE computes them for free and
a stage that only needs the *whole mesh* to pulse at speed 1 can skip its own
17-instruction sine. Nothing in the shipped catalogue uses them; they are there
for a hand-written stage.

## Budgeting

`--vu-check` prints the resident set against the ceiling:

```
-- VU1 micro memory (2048 slots, 2042 usable below the draw-finish helper) --
  StaPipVU1CullC    205 instructions ->  103.. 205 slots
  ...
  TOTAL                               846..1685 slots  fits
```

The figure is a **range** and deliberately so — VCL packs an upper and a lower op
into one 64-bit slot when it can, so N emitted instructions occupy between
`ceil(N/2)` and N slots, and the exact number is only known after VCL runs.
Reporting a single number would be a guess dressed as a measurement.

**Budget against the CLIP family, not the cull one.** With VU1 clipping on (the
default) each resident class uploads its `cull` program *plus* its `clip` twin,
and the clip family is far bigger - the five clip programs measured 2162
instructions against the 2042 ceiling before they were made to share a fan
emitter. The panel's bar therefore reads the engine's own `.vclpp` files rather
than the generator's descriptions. That is not fastidiousness: budgeting against
the cull half alone showed `examples/vu-lab` comfortably green while the console
died on the engine's assert the first time it ran.

```
| Assertion failed!
| VU1 pipeline programs overflow into the draw-finish program
| File : src/renderer/core/paths/path1/path1.cpp:145
```

Two ways to buy room:

- **Drop material classes the project never draws.** This is the one that
  actually works, and codegen does it for you: the mask is derived from what the
  scenes and prefabs draw (`project::vuNeededClasses`) and emitted as
  `core.setResidentClasses(...)` at the top of `install`. vu-lab draws nothing
  lit, so it ships `setResidentClasses(25)` and the two dropped lighting classes
  are what pay for its stages. `StaPipCore::setResidentClasses(mask)` removes a
  class's two programs from the upload; a project with no matcap material does
  not need the two `tce` programs either. A class that is not resident falls back down to a resident relative
  rather than MSCAL-ing into nothing, and the colour class is forced resident as
  the floor. Safe to call at run time — it is a rebuild plus an upload, so it
  belongs at a zone or level boundary, never per bag.
- **Fold zero-strength stages.** Already automatic; the check above prints what
  was dropped and why.

The engine's own guard is a runtime `TYRA_ASSERT` in `createProgramsCache`,
**compiled out in release** — so a program set that overflows in a release build
overwrites `Path1`'s draw-finish helper at the top of micro memory and hangs the
post-fx PATH1 barrier forever. That has happened once. Check the budget before
shipping, or check with `nm` after building.

---

## How a program reaches the game

Everything below is generated at build time from the project's `vu` section. No
Makefile work: the game Makefile includes `/tyra/Makefile.base`, whose
`VCL_SOURCES` globs `src/**.vclpp` and whose link line picks up `src/**/*.cpp`.

| File | What it is |
|---|---|
| `src/gen/vu_look<i>_<c\|d\|tc\|tce\|td>.vclpp` | the microprogram — **one per (look, class)** |
| `src/gen/vu_look<i>_<...>_ai.vclpp` | its as_is twin, see "One class is two programs" below — so a look claiming four classes emits **eight** |
| `src/gen/vu_look<i>_<...>_program.{hpp,cpp}` | the EE-side program class — unpack layout, GIF register list, store stride, all from the same description |
| `inc/scripts/vu_programs.gen.hpp` | the on/off seam |
| `src/gen/vu_programs.gen.cpp` | `install` / `activate` / `setTime` / `setParams` |
| `src/gen/vu0_<name>.vclpp` + `inc/vu0_<name>.gen.hpp` + `src/gen/vu0_<name>.gen.cpp` | a VU0 kernel and its driver |

`vuprog::ENABLED` is a **compile-time constant**. A project with no program of
its own gets a header of inline no-ops and an empty `.cpp`, so every call site
folds away — the same arrangement the devkit layers use, and for the same
reason: a feature you are not using must not cost a branch.

Three call sites in the generated game, and each is where it is for a reason:

```cpp
stapip.setRenderer(&engine->renderer.core);
stapip.core.setVU1Clipping(CLIP_VU1);
vuprog::install(stapip.core);        // AFTER both - each rebuilds the cache
```

```cpp
if (vuprog::ENABLED) {
  g_vuClock += g_frameDt;
  if (g_vuClock > 6433.98F) g_vuClock -= 6433.98F;   // 2*pi*1024
  vuprog::setTime(stapip.core, g_vuClock);
}
```

```cpp
// renderObjects, per object, before its bags go out
if (vuprog::ENABLED)
  vuprog::setParams(stapip.core, runtimeObjects[i].data.vuParams);
```

`install` **must** come after `setVU1Clipping`: both rebuild the resident
program cache, and an override installed first would be rebuilt away. The
parameters are staged per OBJECT, not per bag, because every bag of one object
shares them — and a static batch is one bag for many objects, which is where the
"batched props share parameters" limitation comes from concretely.

The numbers themselves live on `SceneObjectData::vuParams` — the ordinary
per-object data table — so they are copied into `RuntimeObject::data` at scene
load like every other property, and a flow node or a script can write them at
run time without a geometry rebuild (they are not vertex data; nothing needs
`dirty`).

### One class is two programs

A material class is not one resident microprogram, it is a **pair**, and which
half draws a given mesh is decided per package, per frame:

| | drawn by |
|---|---|
| package wholly inside the frustum | the **cull** program |
| package crossing a frustum plane | the **as_is** program (EE clipper), or the **VU1 clipper** when that mode is on |

So a look that replaces only the cull half stops at the edge of the screen — and
"the edge of the screen" is where the player walks. The symptom is maddening
because it is not a per-object property: the same prop takes the effect in the
middle of the frame and drops it as you turn. Codegen therefore emits **both**
halves of every class a look claims and installs all ten slots in one
`setProgramOverrides` call.

**Colour and texture stages only, on the as_is half.** That program has no MVP
multiply — it is handed vertices the EE clipper already transformed — so a stage
that displaces "the position" there is displacing a *clip-space* coordinate. On
the console that is not a subtle error: the mesh tears and its texels smear into
black slabs. Codegen filters the twin's stage list to `Slot::Color` and
`Slot::Texture`, and a look made only of geometry stages simply has no twin.

Which leaves one real limitation, and it is worth stating plainly because it is
the thing that will surprise you: classification is per PACKAGE, not per object,
so **a mesh straddling the edge of the screen is displaced only in the packages
the frustum did not cut** — half a wobbling sphere moves and half stays put, with
a visible step between them. Colour and texture stages have no such problem; they
run on both halves.

`vuprog::movesGeometry()` reports whether the ACTIVE look displaces anything, so
a game can react to it (fade the effect near the frame edge, hold a camera off a
displaced prop). Two things that do NOT work, both measured on the console rather
than reasoned about:

- **Submitting the prop unclipped** (`fullClipChecks = false`) so the whole mesh
  takes the cull path. It does displace uniformly — and then a mesh crossing a
  frustum plane wraps the GS raster window: the matcap sphere at the frame edge
  turned into a black blob, and doing it for the terrain filled the screen with
  sky. Clipping is not optional for anything that touches the edge.
- **Big displacement.** The step at the edge is exactly the displacement, so keep
  amplitude small if props are going to walk past the frame border. Terrain and
  large meshes are the good case: they are mostly interior, and the cut packages
  are at the horizon.

The clean fix is a generated clip twin, which needs a description of the engine's
Sutherland–Hodgman clipper the generator does not have yet.

**Under VU1 clipping the twin is the clipper itself**, which the generator has no
description of at all, so there a clip-classified package keeps the stock shading
whatever the stage does. Author the look on the EE clipper (*Preferences >
Rendering*, what `examples/vu-lab` does) or accept it. The panel says so in as
many words when it sees the combination.

### Swapping the look at run time

Every look you define is **in the ELF** — all of its microprograms, a pair per class
it claims. Only the active one occupies VU1 micro memory. So the budget bar in
the panel is per look, not per project, and a second look costs you EE memory,
not VU1 memory.

```cpp
vuprog::activate(1);              // by index
vuprog::active();                 // -1 = the engine's own programs
vuprog::lookName(1);              // "Underwater"
vuprog::LOOK_COUNT;               // compile-time
```

`activate` is one `setProgramOverrides` call — the engine takes all **ten** slots
(five classes × the cull/as_is pair) at once and rebuilds the resident set
**once**, which is the whole reason that batched entry point exists. A class the
new look does not claim gets a null override, which is how the engine's own
program comes back; an index outside `0..LOOK_COUNT-1` nulls all ten, which is
how you turn every look off.

It is a pipeline drain and an upload, so: fine on an **event** — a trigger volume,
a button, entering water, a cutscene beat — and not fine per frame.

Two things to know before you build a game around it:

- **Micro memory is checked per look, at build time, against the classes that
  look claims.** Two looks that individually fit can never collide, because they
  are never resident together.
- **Per-mesh parameters do not follow the look.** `vuParams` is object data; if
  look 0 reads slot 0 as a posterize step and look 1 reads it as a wobble
  amplitude, the same number means two things. Either agree on slot meanings
  across looks or write the parameters when you switch.

`examples/vu-lab` boots into Toon and swaps to Underwater on Triangle
(`src/scripts/vu0_kernel_demo.cpp`) — that is the whole switch, three lines.

### Engine side

Two quadwords, uploaded per mesh in `StaPipQBufferRenderer::sendObjectData`:

```
VU1_CUSTOM_PARAMS_ADDR  15   the mesh's four numbers
VU1_CUSTOM_TIME_ADDR    16   (time, sin time, cos time, 1.0)
```

They sit **inside the directional-lights colour block**, which is why the upload
lives in the `if (!bag->lighting)` branch — a lit bag needs 15..18 for its light
colours and would be corrupted by them. That is also the real reason a custom
program can only be built on a colour base: the addresses are only free there.

`StaPipCore::setVuCustomEnabled` gates the whole thing off by default, so a
project without a custom program does not pay two extra unpacked quadwords per
mesh.

### Files a removed program leaves behind

`refreshGenerated` **sweeps** `src/gen/vu_look*`, `src/gen/vu_custom_*` (the old
per-class naming), `src/gen/vu0_*` and `inc/vu0_*` that the current project does
not produce. Deleting a look, or unticking one of its classes, therefore removes
its microprogram rather than orphaning it. This is not tidiness:
`Makefile.base` globs `src/**.vclpp`, so a leftover microprogram would still be
assembled and **linked**, taking micro memory for a program nobody asked for.

## The other ceiling: VF registers

Micro memory is not the limit you hit first. **VU1 has 32 float registers**,
`vf00` is hardwired, and VCL allocates the other 31 — and running out does not
produce a diagnostic you can act on. It produces this, from `vcl`, inside
Docker, minutes into a build, with no line number:

```
ERROR: no opt table.. something failed making table .. for processing!
```

So the framework estimates it. `--vu-check` prints it per program, `--vu-list`
prints it for any `.vclpp`, and the panel shows it next to the micro-memory bar:

```
-- VF register pressure (31 allocatable; past that vcl may refuse) --
  StaPipVU1CullTC  peak 27 of 31 live   (36 names)
```

The estimate **over-states**, and knowing by how much is the whole of its
usefulness: it is a linear scan (each register live from its first write to its
last read) that ignores control flow and cannot split a live range where VCL
can. Calibration, all measured rather than reasoned:

| Peak | What happened |
|---|---|
| ≤ 27 | every one of the engine's ten resident programs — all compile |
| 32 | a two-stage textured program — vcl allocated it fine |
| 35 | a two-stage colour program — fine |
| **36** | a four-stage colour program — `no opt table`, build dead |

The cliff is sharp, and it is close. **A cull-family program starts around 23**
(the MVP matrix, the spot light's seven scratch registers, three vertices, three
colours), so a stage list has roughly a dozen registers to spend. The sine is
what spends them: it needs a constants register, three scratch and the clock, so
**one time-varying stage costs about five and the second one costs almost
nothing**. That is the shape to design against — group the effects that share a
sine, not the ones that read alike.

If you go over, the ways out in order of how much they buy: drop a stage, use a
stage that does not need the sine, or split the effect across two material
classes (a textured program and a colour one each get their own 31).

---

## VU0 kernels

The same stage library, on the other vector unit, with none of the rendering
around it: **N quadwords in, N quadwords out, one stage list per element**.

```cpp
vugen::KernelDesc k;
k.title = "cloth solver";
k.stages.push_back(vugen::makeStage("wobble"));
vugen::BuiltKernel b = vugen::buildKernel(k);
// b.vclpp     - the microprogram
// b.eeHeader  - a driver class: init(), setParams(), setTime(), run(in, out, n)
// b.eeSource
```

### What is different about VU0, concretely

| | VU1 | VU0 |
|---|---|---|
| Data memory | 1024 quadwords | **256** |
| Micro memory | 2048 slots (2042 usable) | **512**, all usable |
| GIF path | PATH1, `xgkick` | none |
| Input staging | VIF1 double buffer, `xtop` | the EE stores at fixed addresses |
| Entry | starts at 0, loops per buffer | `vcallms <addr>`, once per call |
| Data memory between calls | a fresh buffer arrives | **persists** |

The persistence is the useful part and the generated driver leans on it: the
parameter quadword and the time quadword are stored once, and each `run()` only
rewrites the batch and the element count.

The default layout leaves **112 elements** each way inside those 256 quadwords:

```
  0        .x = element count for this call
  1        the four user parameters
  2        (time, sin time, cos time, 1.0)
  8..119   input elements
  120..231 output elements
```

`buildKernel` refuses a layout that does not fit rather than letting it wrap —
on hardware an address past 255 wraps silently onto something else.

### The caveat no simulator can catch

**VU0's register file is shared with COP2 macro mode**, which is the engine's own
`Vec4`/`M4x4` math. A VU0 micro program and an inline macro-mode expression are
the same 32 VF registers.

The generated driver's `run()` therefore **blocks the EE** until the kernel
finishes — it is not a simplification, it is the only safe arrangement without
saving and restoring the register file. The engine's raytracer does the same
thing for the same reason. If you want VU0 work overlapping EE work, that is a
project you own, not a flag.

### Using a kernel from a project

The generated `.vclpp` is built automatically: the game Makefile includes
`/tyra/Makefile.base`, whose `VCL_SOURCES` globs `src/**.vclpp`. There is no
Makefile work.

```cpp
// inc/scripts/script.hpp
#include "vu0_kernel_kernel.hpp"

static Tyra::TyraXVu0Kernel kernel;
static Tyra::Vec4 in[64], out[64];

// ... once per frame
kernel.setParams(0.5F, 2.0F, 0.0F, 0.0F);
kernel.setTime(elapsedSeconds);
kernel.run(in, out, 64);
```

`init()` is called for you on the first `run()` and is idempotent.

### Which stages a kernel can run

Only the ones that touch nothing but the element itself: **Wobble, Twist,
Inflate, Squash**. A kernel element is one quadword; a stage that reads a colour
or an ST has nothing to read, and `buildKernel` refuses it by name rather than
reinterpreting the fields.

---

## How this is checked

`tyrax-editor --vu-check` — no Docker, no PCSX2, no console. Six stages:

1. Every `.vclpp` the engine ships parses.
2. Every described program (all ten StaPip keeps resident) is run against its
   handwritten twin over 60 randomized draws and diffed **quadword by
   quadword** of the GIF packet.
3. The emitted source is parsed back and re-run — because everything above
   compares in-memory IR, and the file that reaches `vclpp` comes out of a
   separate text emitter. This stage has now caught two emitter bugs that the
   IR-level check called bit-identical: a dropped `.xyzw` mask on `lq`/`sq`, and
   a `loi` printed with `%g` (six significant digits), which turned the sine's
   2^23 floor bias into a different number.
4. The micro-memory budget.
5. **Every stage, both directions**: identity at zero strength (bit-exact
   against the engine's own program) and *not* identity at full strength. The
   second half exists because a stage that quietly folded to nothing would score
   full marks on the first.
6. **VU0**: the engine's raytracer kernel is run under the VU0 machine model,
   and a kernel generated from the stage library is run and compared against the
   same arithmetic done in C++ — `Squash` exactly (three multiplies, no
   approximation, so an exact comparison is fair) and `Wobble` against its
   amplitude, since the sine's error is the thing being stated rather than
   hidden.

What `--vu-check` does **not** cover is the EE side: it stages VU1 memory itself
and diffs only what the microprogram staged for the GS, so the generated
`addProgramQBufferDataToPacket` is never executed by it. An adopted program still
owes a Docker build and a look at the picture.

## Adding a stage

1. An entry in `stageDefs()` (`src/vugen.cpp`). `.desc` says what the STAGE is
   for; each parameter's `.tip` says what that ONE knob does. Mark the strength
   parameters — that flag is what makes the folding rule and the identity check
   work. Set `kernelSafe` only if the stage touches nothing but the position.
2. A branch in `emitStage()`. Take every temporary from `StageCtx::s[]` —
   **never mint a register**. VU1 has 32 VF registers and the simulator has
   unlimited virtual ones, so register pressure is invisible on the host and
   shows up as a VCL allocation failure (`no opt table`) at build time. The
   per-vertex chain is unrolled by three, so a stage that minted its own
   temporaries would multiply that pressure by three.
3. Derived literals (a reciprocal, a scale factor folded from a level count) go
   in `planStages` via `plan.lits.index()`, and are read back with
   `c.lits->read(r.extraLit[n])`.
4. Run `--vu-check`. If your stage is not the exact identity at zero strength it
   will say so, with the quadword and field that differ.

Two rules that are not obvious from the code:

- **A broadcast is only legal on the SECOND operand.** `sub.xyz d, one, f` is
  fine; `sub.xyz d, vf00[w], f` is not. That is why the context carries a real
  `one` register instead of using `vf00[w]`.
- **`vf00` is `(0, 0, 0, 1)`, and the W is a trap as well as a tool.** `vf00[w]`
  is how you get a 1.0 as a second operand and `vf00[x]` a 0.0, and half the
  arithmetic here leans on it — but `add.w dst, vf00, i` gives `1 + i`, not `i`.
  `Vu::constants` did exactly that for a week: every fourth literal in a
  constants register came out one too big, which put **1.225** where the sine's
  0.225 correction coefficient belongs. Use `mul` for the w field.

  Two things about how that survived are worth more than the bug. The host
  checks all passed, because the simulator models `vf00` correctly too — both
  sides were consistently wrong, which is the one failure mode a
  simulator-against-itself check cannot catch. And the sine check that existed
  measured its PEAK, where `y|y| - y` is exactly zero and the coefficient
  therefore does not matter at all. It was caught by a console number not
  matching a hand calculation, and it is now caught by comparing the generated
  sine against **the same series evaluated in C++ at 24 angles** — which fails
  by 0.249 with the bug reintroduced and passes at 3e-7 without it.
