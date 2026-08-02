# Authoring VU programs

[docs/vu-framework.md](vu-framework.md) is the machinery: an instruction model,
a parser, a host simulator and a generator that emits VCL. This page is the
layer on top of it — **composing a VU1 microprogram or a VU0 kernel out of
stages, without writing assembly**.

Read the framework page first if you intend to add a stage. You can use one
without it.

---

## The shape of the thing

A project's own VU1 program is **a base plus an ordered list of stages**.

```
base:  which material class it replaces  (cullColor / cullTextureColor / cullTextureEnv)
stages: [ Wobble(amp, freq, speed), Desaturate(amount), ... ]
```

Each stage has up to four parameters, and each parameter is either

- a **literal**, baked into the microprogram — free, and constant for the whole
  project; or
- bound to one of **four per-mesh slots**, a quadword the game uploads next to
  the MVP matrix, so every object can drive the same effect differently.

That split is the whole design, and it comes from a hardware constraint rather
than a preference. Read the next section before authoring anything, because
everything else follows from it.

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

## Which bases exist, and why only three

| Base | Replaces | Carries |
|---|---|---|
| `cullColor` | `StaPipCullColor` | vertex colour |
| `cullTextureColor` | `StaPipCullTextureColor` | vertex colour + an ST stream |
| `cullTextureEnv` | `StaPipCullTextureEnv` | matcap — the ST slot holds a **normal** |

All three are `cull` programs, and that is not arbitrary:

- **`cull` is the only family resident in both clipping modes.** `as_is` is
  uploaded only with the EE clipper, `clip` only with VU1 clipping (the default)
  — there is no micro memory for both. A program built on `cull` draws under
  either setting.
- **`cull` is the only family that still has the object-space position.** The
  `as_is` family is fed NDC positions by the EE clipper: by the time it runs
  there is nothing left to displace in model space.
- **None of the three carries lighting**, which is what leaves
  `VU1_LIGHTS_COLORS_ADDR` free for the per-mesh parameter quadword. A lit mesh
  (`cullDirLights`, `cullTextureDirLights`) fills those addresses with light
  colours, so it cannot carry parameters and is not offered as a base.

On `cullTextureEnv` the ST slot holds an object-space normal rather than a
texture coordinate, so `Scroll UV` is refused there rather than silently
scrambling the matcap.

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

Two ways to buy room:

- **Drop material classes the project never draws.**
  `StaPipCore::setResidentClasses(mask)` removes a class's two programs from the
  upload. A project with no matcap material does not need the two `tce`
  programs. A class that is not resident falls back down to a resident relative
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
- **`vf00` is `(0, 0, 0, 1)`.** `vf00[w]` is how you get a 1.0 as a second
  operand and `vf00[x]` a 0.0. Half the arithmetic in these programs leans on
  it.
