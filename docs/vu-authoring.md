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

And the same two ways exist on **the other vector unit** — `src/vu0/*.cpp` and
the *VU0 kernel* tab of the same panel. A kernel draws nothing: it is quadwords
in, quadwords out, and a class with a `run()` on it that your game calls. It
costs nothing out of the VU1 drawing budget, because the instructions land in
VU0's own micro memory. See [VU0 kernels](#vu0-kernels).

[docs/vu-framework.md](vu-framework.md) is the machinery underneath: the
instruction model, the parser, the host simulator and the emitter.

---

## Writing a script

Put a `.cpp` under `src/vu/`. That is the whole setup: the editor notices it,
copies the framework next to it (`vugen/`, so VS Code resolves the include), and
the build compiles and runs it.

```cpp
#include "vushader.hpp"

struct Warm : vu::Program {
  const char* name() const override { return "Warm"; }
  unsigned classes() const override { return vu::kColour | vu::kTextured; }
  vu::Slot slot() const override { return vu::Slot::Color; }

  // Once per buffer. Constants belong here - see the rules below.
  void prepare(vu::Ctx& c) override {
    kWarm_ = vu::constant(c, 1.1F, 1.0F, 0.85F, 1.0F);
  }
  vu::Vec kWarm_;

  // Once per vertex. Two VU instructions, and that is the whole program.
  void vertex(vu::Ctx& c) override { c.color.rgb() = c.color * kWarm_; }
};
VU_PROGRAM(Warm);
```

That compiles to a microprogram in the ELF and replaces the engine's own
program for the two classes it claims. `examples/vu-lab/src/vu/` has four real
ones - cell shading with an ink outline, a two-colour palette, a PS1 vertex
snap and a travelling wave - and `src/vu0/ranges.cpp` is the VU0 equivalent.

---

## The API

Everything below is `src/vushader.hpp` (copied into your project as
`vugen/vushader.hpp`, which is why VS Code can complete it).

### `vu::Program` — what you override

Only `name()` and `vertex()` have no default.

| | Default | |
|---|---|---|
| `const char* name()` | — | shown in the editor, and the stem of the generated files and of `vuscript::k<Name>` |
| `unsigned classes()` | `kColour` | which material classes this REPLACES: `kColour`, `kLit`, `kLitTextured`, `kTextured`, `kMatcap`, or `kAll` |
| `vu::Slot slot()` | `Slot::Color` | where in the pipeline the body runs — see [The slots](#the-slots) |
| `bool activeAtBoot()` | the panel's checkbox | `false` builds the program into the ELF and leaves the game to `vuscript::activate` it |
| `bool movesGeometry()` | `false` | **must** be true if the body writes `c.position` in object or clip space — see [Moving geometry](#moving-geometry-declare-it) |
| `bool shellPass()` | `false` | ask the game for a second, grown submission of every object — outlines, fur |
| `float shellWidth()` | `0.02F` | how far it grows, a fraction of the object's own size |
| `void prepare(Ctx&)` | nothing | once per buffer: build your constants here |
| `void vertex(Ctx&)` | — | once per vertex: the body |

`VU_PROGRAM(Type)` at the bottom of the file registers it. One line, no header
to edit.

### `vu::Kernel` — the VU0 equivalent

| | Default | |
|---|---|---|
| `const char* name()` | — | also names the driver class: `"Ranges"` → `Tyra::RangesKernel` |
| `int maxElements()` | `112` | elements per `run()`; both blocks must fit VU0's 256 quadwords, so the cap is 124 |
| `void prepare(Ctx&)` | nothing | once, before the element loop |
| `void element(Ctx&)` | — | once per element; `c.position` IS the element quadword |

`VU_KERNEL(Type)`, and the file goes in `src/vu0/`. Full section:
[VU0 kernels](#vu0-kernels).

### `vu::Ctx` — what the body is handed

The program's REGISTERS, not a copy of them: reading one is free, assigning to
one writes the register the emitter keeps using afterwards.

| | |
|---|---|
| `c.position` | the vertex, in the space `slot()` names. In a kernel, the element |
| `c.color` | 0..255 per channel, the GS scale (128 = "unmodulated") |
| `c.uv` | texture coordinates, when `c.hasUv()` |
| `c.normal` | WORLD space, on a lit class, when `c.hasNormal()` |
| `c.params` | the object's four numbers from the inspector — `setParams()` in a kernel |
| `c.time` | `(seconds, sin, cos, 1)`, already range-reduced |
| `c.scratch(0..3)` | registers the framework already reserved. **Four** (`kScratchCount`); the index is clamped |
| `c.raw()` | the builder itself — see below |
| `c.mark()` / `c.hoist(from)` | move instructions into the preamble; `vu::splat` and `vu::constant` do this for you |

### `vu::Vec` — one register, one instruction

A `vu::Vec` is one virtual VF register, and every operator below is exactly one
VU instruction on all four lanes. That correspondence is deliberately visible:
this is a way to write microcode, not a way to pretend the VU is a GPU.

| | |
|---|---|
| `a + b`, `a - b`, `a * b` | one instruction each, all four lanes. There is no `/` — see the Q rule |
| `a = b` | writes the register `a` NAMES (emitted as `add b, 0`), which is what makes `c.color = …` reach the packet |
| `a.x()`, `.y()`, `.z()`, `.w()` | that lane broadcast into all four — free, it is an operand mode |
| `a.rgb()` | assigning through it writes ONLY xyz. On a colour that means "leave alpha alone", and alpha is what the GS blends with |
| `a.val()`, `a.builder()` | drop to the raw `vugen::Val` / builder |

Helper functions, all one to a few instructions:

| | |
|---|---|
| `vu::splat(c, 1.5F)` | that value in every lane, built ONCE in the preamble |
| `vu::constant(c, x, y, z, w)` | four different lanes — a tint, an axis mask, or four scalars packed into one register |
| `vu::minimum(a, b)`, `vu::maximum(a, b)` | one instruction each |
| `vu::lerp(a, b, t)` | `a + (b - a) * t`, with `t` in every lane |
| `vu::dot3(a, b)` | the xyz dot product, broadcast into every lane. Three instructions; w is left out because a normal's w is not a component |
| `vu::saturate(c, a)` | clamp to 0..1 |
| `vu::quantize(c, a, steps)` | round down to `steps` levels per lane — the cell-shading primitive, four instructions, no branch and no table |

**Pack scalars into lanes.** `vu::constant(c, 1.0F, levels/255.0F, 255.0F/levels, 0.5F)`
is one register holding four numbers you then read as `.x()`, `.y()`… — and on
the lit classes that is the difference between fitting and not, because they
already keep ~30 of VCL's 31 registers live.

### `c.raw()` — the builder

The escape hatch, and not a lesser path: the framework's own stages are written
against it. `vugen::Vu` (in `vugen.hpp`) takes `vugen::Val` values and a field
mask, and writes into a destination you supply rather than minting one:

```cpp
vugen::Vu& b = c.raw();
const vugen::Val s0 = c.scratch(0).val();
b.mulInto(s0, c.position.val(), kWave_.val(), vuir::MX);   // s0.x = pos.x * k.x
b.addInto(s0, s0, vugen::Val{s0.reg, 2}, vuir::MX);        // += s0.z (a broadcast)
```

Masks are `vuir::MX`, `MY`, `MZ`, `MW`, `MXYZ`, `MALL`. A broadcast is
`vugen::Val{reg, field}` with field 0..3 — and **it is only legal on the SECOND
operand**, which is why the context carries a real `one` register instead of
using `vf00[w]`.

| Group | |
|---|---|
| Arithmetic | `addInto` `subInto` `mulInto` `minimumInto` `maximumInto` `absInto` `moveInto` `onesInto` |
| Accumulator | `mulAcc` `maddAcc` `maddInto` `msubInto` — the four-instruction madd chain a matrix multiply is |
| Divide (writes Q) | `divQ` `rsqrtQ` then `mulQInto`. **Forbidden in a VU1 script**, fine in a kernel |
| Conversion | `truncate` (round toward zero) `ftoi0Into` `ftoi4Into` `loadI` |
| Integer | `iaddiuInto` `iaddInto` `iorInto` `iandInto` `isubInto` `mtir` |
| Control flow | `label` `bind` `branch` `branchIfLez` `branchIfGtz` `branchIfEq` `branchIfNotEq` |
| Memory | `lq` `sq` `lqConst` `ilw` `isw` |

And the method library — the framework's own primitives, the same ones the
generated pipeline programs are built from:

| | |
|---|---|
| `sineApprox(dst, angle, mask, kc, one, scratch, phase)` | 17 instructions, ~0.2% — see [The sine](#the-sine-and-what-it-costs). `kc` must be `constants(1/2pi, 0.5, 2^23, 0.225)` |
| `constants(dst, x, y, z, w)` | four literals in one register, eight instructions once |
| `truncate(dst, a, mask)` | floor toward zero (`ftoi0` + `itof0`) |
| `transform(dst, m[4], v)` | a 4x4 matrix multiply as a mula/madd chain |
| `dirLightShade(...)` | three directional lights plus ambient, from an object-space normal |
| `persCorrect`, `scaleToGsFormat`, `fixColor` | the perspective divide, the GS 12.4 conversion, the 0..255 clamp |
| `envStq`, `spotLight`, `fogCoefficient`, `fogClipCheck` | matcap ST, the flashlight cone, hardware fog, the ADC frustum test |
| `clipwInto`, `fcandInto`, `xtop`, `xgkick`, `resetClipFlags` | the clipper's own primitives |

Every one of them takes its scratch registers from the CALLER, on purpose: a
value-returning API wants to mint a register per call, and register pressure is
invisible on the host (see [the other ceiling](#the-other-ceiling-vf-registers)).

---

## Six rules the compiler will not tell you

**Build constants in `prepare()`, not in `vertex()`.** Both hoist to the
preamble, but `vertex()` runs once per VERTEX and the loop is unrolled three
times, so the same constant lands in three registers. And write the body
against `c.scratch(n)` and the raw builder once it grows: every `a * b` in the
value layer mints a register, and eighteen live temporaries is enough to put
vcl into `time out.. failed to normal via processing` - it still emits code, it
just stops optimising, and each timeout is another 45 seconds of build. Moving
the sample onto scratch registers took it from several timeouts to none and the
budget from 1042..2084 to 897..1794.

**Constants belong in the preamble, and `vu::splat` puts them there.** `loi`
writes the I register; a run of them inside the per-vertex body is something vcl
schedules around, and the hardware then reads an I the host simulator never saw.
Building constants by hand through `c.raw()` inside the body is the one way to
reproduce this - the first console run of the script above came out as rainbow
noise until the constants were hoisted.

**Never write Q.** A divide, an rsqrt or a sqrt writes the Q register, and Q
carries the perspective divide - `persCorrect` puts 1/w there and both the
position and the ST ride on it. Q has a latency the assembler schedules around,
and a Q write from a script body is independent in vcl's dataflow view, so it
gets moved into that window and vertices land in the wrong place. On the console
that reads as grey stippled patches on a mesh and shadows fighting for z; on the
host it reads as nothing at all, because the simulator runs in order and models
no latency. The build refuses it now, with that explanation. Cell shading wants
a `band / luminance` and cannot have it - `examples/vu-lab` uses a step ramp
built from min/max instead, which is a plain multiply and preserves hue exactly.

**Know which one you are banding: the channels or the light.** They are
different effects and the difference is hue. Quantising r, g and b
independently MOVES the hue — at four levels a shaded yellow `(160,131,25)`
lands on `(127,127,0)`, an olive that was never in the scene — and that shift is
what reads as flat paint and ink. Quantising LUMINANCE and scaling the colour by
the ratio preserves hue exactly, which sounds more correct and looks like a
dimmer switch. `examples/vu-lab/src/vu/cell_shading.cpp` bands the channels, and
that was settled by looking at both on a TV rather than by argument: the version
people preferred was an early BUG that quantised one channel by accident.

Whichever you pick, **round to band CENTRES, not floors.** Truncation always
rounds down, so without a half-step the picture loses half a band of brightness
everywhere — which reads as "the effect just makes things darker", and did.

**Multiply, never add.** An object is drawn by more than one bag, and the extra
ones are MODULATION passes: a baked lightmap's vertex colour is literally
`Color(0, 0, 0, 128)` with the occlusion living in its texture. Add a constant
to "lift the dark end" and you lift that black to grey — the shadow pass becomes
a grey wash, dithered because it is blended, and it reads exactly like z-fighting
under the texture. Multiplying leaves zero at zero, so a modulation pass passes
through untouched. The same rule is why the sample writes `c.color.rgb()`:
alpha is what the GS blends with.

**Registers run out before micro memory does.** VCL allocates 31, and the
directional-light program already keeps about 30 of them live. Cell shading
written in 0..1 and converted back cost two constants too many and died as `no
opt table` from vcl, inside Docker, with no line number; the same effect done in
the GS's own 0..255 scale fits. Prefer `c.scratch(n)` to long expressions, and
suspect the register ceiling first when a build fails in the assembler.

**A script's `Slot::ObjectSpace` half follows a mesh through the VU1 clipper but
not through the EE one** - see "One class is two programs" below.

## Write it in VS Code, not in a text box

`src/vu/*.cpp` and `src/vu0/*.cpp` are where the program actually gets written,
so the editor's
VS Code extension treats them as first-class files rather than as anonymous C++
([docs/vscode-extension.md](vscode-extension.md)). The Scripts panel opens it;
`${workspaceFolder}/vugen` is on the generated includePath, so `c.` and
`vugen::` complete out of the framework's own headers; and the rules on this
page that are NOT expressible in a header - a Q write, a fifth scratch
register, a geometry slot without `movesGeometry()`, a displacement claiming a
subset of the classes - are reported in the Problems panel as you type instead
of by a container build or a console run. Type `vuprogram` for the skeleton.

## What the build actually does

```
src/vu/*.cpp + src/vu0/*.cpp  +  vugen/*.cpp   your code and the framework
        |  g++ (in the build container, installed once)
        v
     a generator                     runs on the HOST, at build time
        |
        v
src/gen/vu_script*.vclpp  + _program.cpp + vu_scripts.gen.{hpp,cpp}
src/gen/vu0_script*.vclpp + _kernel.cpp  + vu0_kernels.gen.hpp
        |  vclpp -> vcl -> dvp-as    the engine's own chain
        v
     the ELF
```

The container ships no host compiler (it is a ps2dev image), so the first build
with a script or a kernel installs `g++` once - about a minute, stamped, and
paid again only if the container is recreated. A mistake in your file is an
ordinary C++ error with your own filename and line number.

Neither directory reaches the PS2 compiler: `Makefile.base` excludes both from
its `src/**.cpp` glob, because handing host C++ to `mips64r5900el-ps2-elf-g++`
fails on the very first include.

`vuscript::install(core)` is called by the generated game right after
`vuprog::install`, so a script wins over a look claiming the same class. With no
script the header is a stub and every call folds away.

---

## Turning a program on and off

By default a script installs at boot. Two ways to say otherwise, and they do not
compete:

**The checkbox** in *Tools > VU Programs*, next to each script. It is project
data, so it needs no code and survives a rebuild.

**The override**, in the script itself:

```cpp
bool activeAtBoot() const override { return false; }
```

**A script that overrides wins, and the panel says so** - its checkbox goes
read-only with a tooltip naming the file. That is not a precedence rule the
framework implements; it is what C++ does with a virtual. The base
implementation returns what the checkbox asked for, an override never calls it,
and the two can therefore never disagree about who decided. The build reports
which of the two answered (`vu_scripts.manifest`, last column), which is how the
panel knows to grey the box - and the detection is exact rather than heuristic:
the generator asks each program twice, once with the default set to true and
once to false, and a program that answers the same thing both times is one that
overrides.

Either way it is only the STARTING state. At run time the game decides:

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

## Reading the micro-memory budget

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

### What actually competes for micro memory

The budget bar is easy to read as "everything the console does on a VU". It is
not - it is one thing: **the static pipeline's resident program set**. Worth
knowing what is NOT in it, because the answer to "can I turn X off to free
slots?" is usually no:

| | Runs on | Costs micro memory? |
|---|---|---|
| Static geometry (StaPip) | VU1, one program per class + its twin | **Yes - this is the bar** |
| Skeletal animation: pose | the EE | No |
| Skeletal animation: skinning | VU0 in **macro mode** (COP2 instructions issued by the EE) | **No** - macro mode has no microprogram to upload |
| Animated models, drawing | VU1, the dynamic pipeline's four programs | No - uploaded when that pipeline is used, over the same addresses, so it SWAPS with the static set rather than sharing it |
| Particle billboards | VU1, their own small set | No - swapped in on demand (`ensureProgramSet`) |
| A project's VU0 kernel | VU0 in **micro** mode | Yes, but against VU0's own 512 slots |

So a scene with no animation frees EE time and VU0 cycles, not slots: there is
nothing resident to drop. The levers that do move the bar are the resident
**classes** (the checkboxes above it, `setResidentClasses`), how big your own
programs are, and the clipping mode.

**The clipping mode is the big one.** The twin next to each cull program is the
VU1 clipper or the as_is program depending on the mode, and the clip family is
roughly twice the size: measured over the four classes `examples/vu-lab` draws,
1181 instructions against 543. A game can flip it while it runs:

```cpp
vuprog::setVU1Clipping(false);   // the EE clipper: ~638 slots back, EE time spent
vuprog::vu1Clipping();
```

It rebuilds the resident cache, so call it **between frames**, and re-installs
whatever look was active afterwards.

Check the panel first. This is the easiest way to overflow by accident — a
custom program replaces the cull half in BOTH modes while the twin beside it
doubles — so the Micro memory tab prices the mode you are *not* in and says
whether it fits. A console run that flipped vu-lab to VU1 clipping mid-game hit
the engine's own `VU1 pipeline programs overflow into the draw-finish program`
assert; that line now reads `1152..2304 of 2042 - DOES NOT FIT` before you try.

## Where it shows up in the editor

- **Project panel > Scripts > VU programs** - every `src/vu/*.cpp`, click to open
  in VS Code. *New script...* creates one from a working stub - three kinds,
  *Game script* (the EE), *VU program* (VU1) and *VU0 kernel* (`src/vu0/`).
- **Tools > VU Programs > Micro memory** reads as a LIST of what runs on the VU,
  in three groups: what is resident (the clipping mode, the material classes,
  your looks, your scripts — this is the budget), what the other clipping mode
  would cost, and what runs on a VU but is not in this budget at all (animated
  models, skinning, particle billboards, your VU0 kernels — those land in VU0's
  own 512 slots and never move this bar), each with why. The
  script rows come from a manifest the last BUILD wrote — the editor has no
  compiler to run a script with, so those numbers are as stale as your last
  build and the panel says so.

There is no live preview of a script's VCL the way there is for a look, for the
same reason.

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
  against VU0's 512 slots, and below it the project's C++ kernels
  (`src/vu0/*.cpp`) with theirs. Those rows come from the last BUILD's manifest,
  for the same reason the script rows do: the editor has no compiler to run them
  with.

Per-mesh numbers are set in the object inspector, under **VU program**, which
first names the **class this object is in** and the look covering it. The four
sliders are labelled with what actually reads them (`Wobble Amplitude`) — from
THAT look only, because labelling them from every look in the project is worse
than saying nothing: an untextured box would offer "Scroll UV Speed U" from a
program that will never draw it. When no look covers the object's class the
sliders are gone and it says so, rather than showing four numbers that do
nothing.

---

## VU0 kernels

The other vector unit, with none of the rendering around it: **N quadwords in,
N quadwords out, one body per element**. A kernel draws nothing and replaces
nothing — what a project gets back is a class with a `run()` on it, and nothing
calls it unless the game does.

Two ways to write one, exactly mirroring the VU1 side:

| | |
|---|---|
| **A kernel** — `src/vu0/*.cpp` | You write C++, subclassing `vu::Kernel`. Same authoring as a script: same value types, same four scratch registers, same `c.raw()` escape hatch. **This is the main road.** |
| **The stage list** — *Tools > VU Programs > VU0 kernel* | The stage catalogue, applied to quadwords instead of vertices. No code, and limited to what the catalogue covers. |

### Writing one

Put a `.cpp` under `src/vu0/` — or *New script…* and pick **VU0 kernel**, which
writes the stub for you. That is the whole setup; the editor copies the
framework next to it and the build compiles and runs it, exactly as for
`src/vu/`.

```cpp
#include "vushader.hpp"

struct Ranges : vu::Kernel {
  const char* name() const override { return "Ranges"; }
  int maxElements() const override { return 64; }

  void prepare(vu::Ctx& c) override {
    kGuard_ = vu::constant(c, 1e-6F, 3.0F, 0.0F, 0.0F);
  }
  vu::Vec kGuard_;

  void element(vu::Ctx& c) override {
    // c.position IS the element quadword. c.params is what setParams() sent.
    vugen::Vu& b = c.raw();
    b.subInto(c.scratch(0).val(), c.position.val(), c.params.val(), vuir::MXYZ);
    // ... dot, rsqrt, band; see examples/vu-lab/src/vu0/ranges.cpp
  }
};
VU_KERNEL(Ranges);
```

`element()` is handed the same [`vu::Ctx`](#vuctx--what-the-body-is-handed) a
`vertex()` body is — same operators, same four scratch registers, same
`c.raw()`. Two differences:

- **A kernel element is one quadword and nothing else.** `c.position` is the
  element; `c.color` and `c.uv` name that same register because there is no
  second thing to name, and `hasUv()`/`hasNormal()` are false.
- **`prepare()` matters more here.** The element loop is a real *branch*, not
  three unrolled copies, so a constant built inside `element()` is rebuilt for
  every single element — and a `loi` among them is the one construct vcl
  reorders in a way the host simulator cannot model.

### Using it from the game

The generated driver is named after the kernel — `"Noise field"` becomes
`Tyra::NoiseFieldKernel` — and one header brings them all in:

```cpp
#include "scripts/vu0_kernels.gen.hpp"

static Tyra::RangesKernel ranges;
static Tyra::Vec4 in[64], out[64];

ranges.setParams(camX, camY, camZ, 1.0F / 12.0F);
ranges.setTime(seconds);          // (t, sin t, cos t, 1) in c.time
ranges.run(in, out, count);       // count <= maxElements(). BLOCKS.
```

`init()` is called for you on the first `run()` and is idempotent. The `.vclpp`
is built automatically — `/tyra/Makefile.base` globs `src/**.vclpp`, and it
skips `src/vu0/` for the PS2 compiler the same way it skips `src/vu/`. There is
no Makefile work.

`vukernel::COUNT` and `vukernel::ENABLED` are compile-time constants, so game
code can be written before the first build and keeps compiling after the last
kernel is deleted.

### What a kernel is FOR, and what it is not

**It is a calculator, not a GPU.** 512 micro-memory slots against VU1's 2042,
and 256 quadwords of data memory — which the default layout splits into about
112 elements each way.

**It is SIMD over quadwords.** The same operation on many elements is what it is
good at. A scalar loop whose step depends on the previous step is what it is
worst at, and no amount of arranging changes that.

**There is no cross-element reduction.** No instruction adds a lane of element 7
to a lane of element 8; the quadwords are independent by construction. So
"compute an integral on VU0" — the request this feature gets most — splits in
two: VU0 evaluates the terms, and the **EE sums the output array**. That is not
a workaround, it is the division of labour the hardware describes.

**`run()` blocks the EE**, and it clobbers the COP2 register file — see the
caveat below. 32 elements is microseconds; a big batch is not free.

**It costs nothing out of the VU1 drawing budget.** These instructions are
counted against VU0's own 512 slots, and nothing else is parked there. The
micro-memory bar in *Tools > VU Programs* is about VU1 alone, and a kernel never
moves it.

The job that fits all of this at once is the oldest one a PS2 game has: **how
far away is everything?** One subtract, one dot product and one root per object,
over a list the game already has in an array, and the answers pick levels of
detail, decide what to draw and set how loud a sound is. That is
`examples/vu-lab/src/vu0/ranges.cpp`, and its numbers agree with the EE's own
`sqrtf` to 1e-6 units on the console.

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

A C++ kernel's blocks are sized from its own `maxElements()` — the output block
starts where the input block ends, which caps a batch at **124**. Either way
`buildKernel` refuses a layout that does not fit rather than letting it wrap:
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

### The one rule a kernel does NOT inherit

Everything in [Six rules the compiler will not tell you](#six-rules-the-compiler-will-not-tell-you)
applies to a kernel body — four scratch registers, constants in `prepare()`,
`loi` needs `%.9g` — **except the ban on writing Q**.

A VU1 script must never emit a divide, an `rsqrt` or a `sqrt`, because the
framework owns Q there: `persCorrect` puts 1/w in it, the position and the ST
both ride on it, and a divide vcl slides into that window silently moves the
vertex. A kernel has no framework around the body. Nothing else reads Q, and a
root is ordinary microcode — which is what makes a distance kernel possible at
all. The generator says so in a note rather than refusing it.

### Which stages a kernel can run

Only the ones that touch nothing but the element itself: **Wobble, Twist,
Inflate, Squash**. A kernel element is one quadword; a stage that reads a colour
or an ST has nothing to read, and `buildKernel` refuses it by name rather than
reinterpreting the fields.

A C++ body has no such list — that is the whole point of writing one. Both can
be present in the same kernel description, in which case the stages run first
and the body sees what they left in the element.

### Where the files land

The build container compiles and runs `src/vu0/*.cpp` and leaves behind, per
kernel:

```
  src/gen/vu0_script<i>.vclpp              the VU0 microprogram
  src/gen/vu0_script<i>_kernel.cpp         the EE driver
  inc/scripts/vu0_script<i>_kernel.hpp     its header
  inc/scripts/vu0_kernels.gen.hpp          one include for all of them
```

`vu0_script<i>` and **not** `vu0_<name>` on purpose: the panel's stage-composed
kernel is named by its author, is written by the *editor* on the host, and is
swept by the editor when it stops being generated. Two owners writing under one
prefix into one directory is how one of them ends up deleting the other's work.
Kernels join the generator's own emitted-file ledger instead, so a deleted
`src/vu0/*.cpp` takes its microprogram with it.

---

# How it works

Everything above is what you write. The rest of this page is why it is shaped
that way — read it when something surprises you.

## What this is for

**A look, not an effect on one object.** This is the layer for giving a whole
scene a treatment — cell shading, an underwater wobble, a posterized palette.
It is not the way to make one barrel wobble; that is an animation's job, and
paying a microprogram for it would be absurd.

## The shape of a look

A look is **one ordered list of stages, installed over every material class it
claims**, and codegen emits one microprogram per claimed class from that one
list. A treatment that stops at one class is not a treatment: cell shading on
untextured props but not on textured ones just reads as broken.

```
name:    "Underwater"
classes: Untextured + Textured + Reflective   (a StaPipProgramClass mask)
stages:  [ Wobble(amp, freq, speed), Desaturate(amount), ... ]
```

Each stage parameter is either a **value** baked into the microprogram — the
normal case, meaning every mesh of every claimed class — or bound to one of
**four per-mesh slots**, a quadword uploaded next to the MVP matrix, for when
the treatment has to vary in strength across the scene.

Three rules follow:

- **A stage a class cannot carry** — a UV scroll on untextured geometry — is
  skipped for that class with the reason shown, not refused. Refusing would mean
  a scene-wide look could never include a stage only some classes support, which
  is most of them.
- **One look per class.** `setProgramOverride` replaces a slot and a slot holds
  one program, so a second look claiming a taken class is skipped and said so.
- **A per-mesh BINDING restricts the mask to the unlit classes** — it is the
  binding that restricts, not the look: an all-values look reaches lit geometry
  perfectly well. See below for why.

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

All five, with one condition. The table below names each class's `cull` program
because that is the one always resident; a script also gets the crossing half
(see "A script gets all three"), and a look currently does not:

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

## Moving geometry: declare it

A program that writes `position` in `Slot::ObjectSpace` or `Slot::ClipSpace`
must say so:

```cpp
bool movesGeometry() const override { return true; }
```

The EE clipper cuts a mesh against the frustum **before** any VU program runs.
Move a vertex afterwards and it is moved past a cut computed without it, so the
mesh tears wherever it touches the edge of the screen. With the flag, the game
submits its **props** whole and they take the cull path instead — safe because a
prop is small enough to draw unclipped. The terrain and the sky keep their clip
checks; they are far too big to submit raw, and raw submission wraps the GS
raster window.

**Under VU1 clipping none of that applies**, and the generated game says so:
`fullClipChecks` is `!movesGeometry() || vuprog::vu1Clipping()`, refreshed per
frame because the mode is a run-time switch. The VU1 clip program does its own
MVP multiply, so the script displaces the vertex *before* the cut is computed
and the clipper sees the final geometry. Declaring the flag is still right — the
game may switch back to the EE clipper — it simply costs nothing while the VU1
clipper is on.

`Slot::Ndc` does **not** need it. The divide has already happened, the vertex is
in screen space, and a nudge of a few pixels stays inside the guard band.

**A geometry effect must claim EVERY class.** A colour effect may claim a
subset - an object whose base pass is posterised and whose reflection is not
still reads as one object. A displacement may not: an object frequently draws
several passes over the same vertices in different classes (a reflective ball
has a base pass and a matcap pass), and if one of them moves while the other
stays, the two copies separate and each shows through the other. On the vu-lab
ball that appears as a grey wedge from underneath - which looks exactly like the
coplanar depth bug in `docs/reflective-materials.md` and is nothing of the sort.

**`Slot::Ndc` runs in both halves; object and clip space do not.** The `as_is`
twin - the program that draws what the EE clipper cut - never divides the
position itself, so one instruction before the 12.4 conversion its vertices are
in exactly the space the cull half's `Ndc` stage works in. The same grid means
the same thing in both, with no scale compensation, and a screen-space effect
covers the whole frame.

Object and clip space are genuinely gone by then, so a displacement there does
not run in the as_is twin — and the generator does not emit that twin for a
geometry script rather than silently tearing the mesh.

**It DOES run in the clip half.** The VU1 clip program does its own MVP
multiply, so unlike `as_is` it still holds the object-space position, and a
script is woven into it at the same slots. That is what closes the hole this
section used to describe: `movesGeometry()` covers the PROPS by submitting them
whole, and it cannot cover the **terrain** — terrain chunks straddle the near
plane, the ground continues behind the camera, and a chunk submitted unclipped
wraps the GS raster window. An object-space wave therefore broke along the
chunks the clipper touched, visibly, at the edge of the frame. Run the project
in VU1 clipping and it does not: the clipper is a VU program, the script runs
inside it, and the chunk is cut *after* the displacement. The cost is micro
memory — the clip family is about twice the size of the as_is family it replaces
(see "Budget against the CLIP family").

`examples/vu-lab` demonstrates exactly this: press CIRCLE and it switches to VU1
clipping, narrows the resident classes to fit, hides the one object whose class
it dropped, and turns the Wobble script on. Press it again and all three go
back.

Two things that cost a console build each to learn:

* **There are four scratch registers** (`vu::Ctx::kScratchCount`). Asking for a
  fifth used to hand back a register with no name in it: the instruction was
  emitted, the program built, `--vu-check` passed, and the effect simply did not
  happen. `scratch()` now clamps, so the worst case is aliasing rather than
  silence — but count them.
* **A wavelength shorter than a mesh tears the mesh open.** Displacement in
  object space moves a mesh's own vertices by different amounts; long waves lift
  whole objects, which is what water looks like.
* **Do not guess the range a slot hands you.** `Slot::Ndc` here is not a -1..1
  cube: the built-in `snap` stage quantises by multiplying by the step count
  directly, so one coordinate unit IS the screen. A script that assumed the
  wider range and halved its grid came out several times too coarse and folded
  the whole scene into a horizontal band. When a slot's scale matters, copy the
  numbers from the stage in the catalogue that already works in it.

## One class, one program

A material class carries exactly one program. Installing a second over the same
class replaces the first — `overrides[slot] = program`, nothing more. So two
scripts that both claim `kColour` cannot be active together, and a demo with
several looks switches between them rather than stacking them. That is also why
the generated runtime writes **one entry per engine slot** and not one per
script: an entry per script meant an inactive script wrote `nullptr` over an
active one's program, and every look but the last-listed installed cleanly, said
so in the log, and did nothing.

## When one draw is not enough: the shell pass

A program can move a vertex. It cannot make the game draw a mesh **twice** — and
a whole family of effects is a second copy of the object by definition: an ink
outline, a fur shell, a force-field skin. `shellPass()` asks the game for that
second submission.

```cpp
bool shellPass() const override { return true; }
float shellWidth() const override { return 0.03F; }   // screen units at 1 m
```

With any shell-pass program active, the game draws every visible object once
more from a copy of its proxy that is **already grown along the surface
normals** by `shellWidth()` — a fraction of the object's own size — and pushed
behind its own depth by a scale about the **eye**. Scaling about the eye leaves
projected size untouched and multiplies depth, so the copy hides behind its own
object and the z-buffer keeps only what sticks out past the silhouette. That
sliver is the line.

**The growth is baked on the EE, not done by your program, and that is a
correctness rule rather than a preference.** The EE clipper cuts a mesh against
the frustum *before* any VU program runs. A vertex grown afterwards is grown
past a cut that was computed without it, so the line tears into blobs wherever
an object meets the edge of the screen — and turning the per-package clip checks
off to avoid that only trades it for the raster wrap that raw submission gives
anything half off-screen. The clipper has to see the final geometry, and the
only place that can be arranged is where the geometry is built.

What is left for the program is the part that cannot be baked: painting the
shell flat. It arrives with **1 in `params.x`**, and every other mesh in the
frame carries 0, so one subtract and one multiply serve both — no branch, which
matters because a branch on per-mesh data costs the dual-issue schedule for
every mesh, not only the ones that take it.

Note that black *vertices* would not do instead. A posterising program rounds to
band centres, so a black vertex comes back at half a step and the ink line is
dark grey; the colour has to be zeroed after the quantise.

`examples/vu-lab/src/vu/cell_shading.cpp` is the whole thing — one program over
four material classes, posterise plus the flat-paint multiply.

Three things to know before you use it:

* **`params.x` is reserved** while a shell-pass program is active. A project
  already using x of the mesh parameters will see those meshes paint themselves
  black.
* **The direction is the ellipsoid normal** from the proxy's centroid — radial
  is only the surface normal on a sphere, and on an unevenly scaled prop it
  leans toward the long axis and makes the line thick on one edge and thin on
  the other. A genuinely concave mesh is the honest limit of the approach.
* **It is a draw per object**, and the proxy is built at full detail whenever a
  shell program is active - a coarse stand-in's faces are chords, they run
  below the surface by more than a line is wide, and the outline breaks into
  plates. The bottom of an object standing on terrain is z-rejected by the
  ground, so the line stops where the object meets the floor.

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
  `core.setResidentClasses(...)` at the top of `install`. vu-lab draws one lit
  ball and no textured-lit mesh, so it ships `setResidentClasses(27)` and the
  dropped `td` class is what pays for its stages.
  `StaPipCore::setResidentClasses(mask)` removes a class's two programs from the
  upload; a project with no matcap material does not need the two `tce` programs
  either. A class that is not resident falls back down to a resident relative
  rather than MSCAL-ing into nothing, and the colour class is forced resident as
  the floor. Safe to call at run time — it is a rebuild plus an upload, so it
  belongs at a zone or level boundary, never per bag.
- **Narrow it further FOR A MOMENT, from the game.** `vuprog::setResidentClasses`
  and `vuprog::residentClasses` expose the same lever at run time, which is what
  makes VU1 clipping affordable for an effect that only needs it while it is on.
  `examples/vu-lab` does exactly that on CIRCLE: it drops Directional lights,
  switches to VU1 clipping, runs the Wobble, and puts both back on the way out.
  What is dropped must not be DRAWN - a mesh of a dropped class does not crash,
  it draws in the wrong style - so the demo hides the one dyn-lit ball while the
  mode is on. Measured in PCSX2: three classes with a displacing script in VU1
  clipping fit; four do not.
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
Written by the **EDITOR**, from the panel's looks and stage-composed kernel:

| File | What it is |
|---|---|
| `src/gen/vu_look<i>_<c\|d\|tc\|tce\|td>.vclpp` | the microprogram — **one per (look, class)** |
| `src/gen/vu_look<i>_<...>_ai.vclpp` | its as_is twin, see "One class is two programs" below — so a look claiming four classes emits **eight** |
| `src/gen/vu_look<i>_<...>_program.{hpp,cpp}` | the EE-side program class — unpack layout, GIF register list, store stride, all from the same description |
| `inc/scripts/vu_programs.gen.hpp` | the on/off seam |
| `src/gen/vu_programs.gen.cpp` | `install` / `activate` / `setTime` / `setParams` |
| `src/gen/vu0_<name>.vclpp` + `inc/vu0_<name>.gen.hpp` + `src/gen/vu0_<name>.gen.cpp` | the stage-composed VU0 kernel and its driver |

Written by the **BUILD CONTAINER**, from your `src/vu/*.cpp` and `src/vu0/*.cpp`
— the host has no compiler to run them with:

| File | What it is |
|---|---|
| `src/gen/vu_script<i>_<class>[_ai\|_cl].vclpp` + `_program.{hpp,cpp}` | a script's microprograms — up to three per class it claims |
| `inc/scripts/vu_scripts.gen.hpp` + `src/gen/vu_scripts.gen.cpp` | `install` / `activate` / `deactivate` / `movesGeometry` |
| `src/gen/vu0_script<i>.vclpp` + `_kernel.cpp`, `inc/scripts/vu0_script<i>_kernel.hpp` | a kernel and its EE driver |
| `inc/scripts/vu0_kernels.gen.hpp` | one include for every driver class |
| `src/gen/vu_scripts.manifest`, `vu0_scripts.manifest` | what the panel reads to price them |

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

The clean fix is a generated clip twin, and that description now exists —
`vugen` builds the whole `clip` family, proven bit-identical to the handwritten
one. **A project's C++ SCRIPT already gets it** (see below); the panel's LOOKS
do not yet, because a look is installed through `VuBuild::LookClass`, which
carries exactly two program slots per class. Until that grows a third, a look
authored under VU1 clipping keeps the stock shading on a clip-classified
package — author it on the EE clipper (*Preferences > Rendering*) or write the
effect as a script instead. The panel says so in as many words when it sees the
combination.

### A script gets all three

`src/vumain.cpp` emits every program of every class a script claims: the `cull`
half, the `clip` half, and the `as_is` half unless the script moves geometry (an
as_is program is handed NDC, so there is nothing to displace in it — the
generator skips it rather than tearing the mesh). The limitation above is
therefore a look limitation, not a framework one:

| the script's slot | cull | clip | as_is |
|---|---|---|---|
| `ObjectSpace` | yes | **yes** | not emitted |
| `ClipSpace` | yes | **yes** | not emitted |
| `Ndc` | yes | **yes**, per EMITTED vertex | yes |
| `Color`, `Texture` | yes | **yes**, per corner, then interpolated through the cuts | yes |

In the clip program the colour and texture slots run on the three input corners,
like the flashlight does, and the result is interpolated across whatever the
clipper cut — which is what Gouraud shading does across an uncut triangle
anyway. `Ndc` is the exception: it is the only slot that cannot run per corner,
because an NDC position only exists *after* the cut, per emitted vertex. It runs
inside the shared vertex emitter, at the same point the as_is family's `Ndc`
slot runs, so the same grid means the same thing in all three programs.

`--vu-check` builds every script against every one of those programs and then
proves the script CHANGED the output — comparing against the identical
description with the script removed. Running is the weaker half of the claim: a
body woven into the wrong place still runs and still draws, it just draws the
stock picture.

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

What the editor writes, the editor sweeps; what the **container** writes
(`src/gen/vu_script*` and `src/gen/vu0_script*`) it sweeps itself, against a
ledger of every file the last generator run produced. The host cannot do that
job for it — it has no compiler, so it cannot know which files the current
sources would produce — and a host sweep over container output would delete
microprograms it could not put back.

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

## How this is checked

`tyrax-editor --vu-check` — no Docker, no PCSX2, no console. Seven stages:

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
7. **Every project script and kernel**, built through the same emitters and run.
   For a script, over every class it claims and all three halves of each; for a
   kernel, under the VU0 machine model. Both are then run a second time with the
   body removed, and a body whose output matches the bodyless one is reported as
   changing NOTHING — because a body woven into the wrong place, or into a
   register the next line overwrites, still builds and still runs. A kernel is
   additionally required to produce finite numbers (an unguarded `1/sqrt(0)` is
   an infinity that passes every structural check there is) and to fit VU0's 512
   slots.

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
