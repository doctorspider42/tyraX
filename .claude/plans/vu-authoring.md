# VU authoring: let someone write a VU1 program without writing assembly

Follow-up to PR #159 (the VU framework). Read [`docs/vu-framework.md`](../../docs/vu-framework.md)
first — it carries the design and every trap found so far. This plan assumes that
branch is merged.

## Goal

Someone who cannot write assembly opens the editor, composes a per-vertex effect
from a library of stages, sees it simulated on the host, builds, and it runs on
VU1 on the console.

Everything underneath that sentence is already built and verified. What is left
is the user-facing half: a model to author into, codegen to turn it into a
microprogram, a panel to author in, and an example that teaches it.

## What already exists — do NOT rebuild

All of this is on `main` via PR #159 and was verified on hardware, not just
reasoned about.

**The generator (`src/vugen.{hpp,cpp}`).** A `Desc` describes a program; `build()`
returns the IR, the `.vclpp` source, and the EE-side program class. Six programs
are described (`as_is` c/tc/d/td/tce and `cull_c`) and all six generate output
**bit-identical** to the handwritten originals. The builder's method library —
`scaleToGsFormat`, `fixColor`, `fogCoefficient`, `transform`, `envStq`,
`resetClipFlags`, `makeAdcMask`, `persCorrect`, `fogClipCheck`, `spotLight` — is
what a stage should be built out of.

**The simulator (`src/vusim.cpp`)** runs a program on the host with VU float
semantics (saturation, no NaN/denormals, *and* round-toward-zero — both were
needed before a console capture matched). **The parser (`src/vuasm.cpp`)** reads
the handwritten `.vclpp`, which is what makes equivalence checkable.

**`--vu-check`** is the gate, in three stages: every shipped `.vclpp` parses;
every described program is diffed against its handwritten twin over randomized
input; and **the emitted source is parsed back and re-run** (that third stage
exists because an emitter bug shipped a broken engine build once — see Traps).
It also prints the micro-memory budget.

**`--vu-replay`** re-runs a real console capture on the host and diffs it against
what the hardware produced. Reproduces a PCSX2 capture exactly; the physical-PS2
captures are not yet bit-exact (see the backlog).

**The engine already accepts a game's own program.** Added in PR #159, all
public, all reachable as
`engine.renderer.renderer3D.staticPipeline.core.<method>`:

```cpp
void setProgramOverride(const StaPipProgramName& name, StaPipVU1Program* program);
void setResidentClasses(const u32& mask);   // StaPipClass{Color,DirLights,
u32  getResidentClasses() const;            //   TextureDirLights,TextureColor,TextureEnv,All}
```

`setProgramOverride` installs a program over a built-in slot and re-uploads the
cache. `setResidentClasses` drops material classes the project never draws, and
both are **safe to call at run time** (they rebuild and re-upload the same way
`setVU1Clipping` already does), so a level can hand micro memory from one effect
to another at a zone boundary. A class that is not resident falls back down to a
resident relative rather than MSCAL-ing into nothing; the colour class is forced
resident as the floor.

**A project-local `.vclpp` already builds.** The generated game Makefile includes
`/tyra/Makefile.base`, whose `$(TARGET)` links `$(VCL_OBJECTS)` and whose
`VCL_SOURCES` globs `src/**.vclpp`. Proven by dropping a generated program into a
scratch project and watching `obj/gen/vu_probe.o` appear on the link line. **No
Makefile work is needed.**

**The VS Code extension** speaks `.vclpp` (`tools/vscode-tyrax/vu.js`): hover on
54 instructions, and a macro/address catalogue scanned live from the engine tree.

## The four remaining pieces

### 1. Model + codegen for a user program

A project-owned VU program is a **base family plus an ordered list of stages**.
No assembly, no raw microcode body — that would defeat the point.

- **Base**: which material class it replaces. Start with `CullColor`: it is the
  only family resident in *both* clipping modes, so an effect built on it draws
  under the default project settings. (`as_is` is uploaded only with the EE
  clipper, `clip` only with VU1 clipping — there is no room for both.)
- **Stages**: `Wobble`, `Displace`, `PulseColor`, `Posterize`, `ScrollUv`,
  `RimTint`… each with up to four numeric parameters and a documented insertion
  point (before the MVP multiply / after it / on the colour). Each stage is a
  function over the builder — the same shape as the library methods above.
- Lives on `Project` with its own `Section` writer/reader pair (see
  tyra-editor-dev on the model → serialization → codegen chain; a new manifest
  key that skips that pair reaches the file but never the collaboration wire).
- Codegen emits `src/gen/vu_custom.vclpp` (built automatically, see above), the
  program class, and the registration call that installs it via
  `setProgramOverride`.

**`--vu-check` must cover it**: a user program goes through the same round-trip
stage as the built-ins, and the panel must not let you build one that fails.

### 2. Per-mesh parameters

Split *program* from *parameters*: one program per material class, but each mesh
uploads its own quadword of numbers into a free constant slot. This is the trick
the spot light and the matcap basis already use (`VU1_LIGHTS_DIRS_ADDR` /
`VU1_ENV_BASIS_ADDR` — the lights area is free in the colour programs, so it was
taken over).

Why this shape rather than per-object programs: a per-object *program* costs
micro memory it does not have, and **breaks static batching** — merged bags
cannot hold two programs, so every such object becomes a batch-blocker like the
ones in `staticBatchEligible`. Parameters cost neither. The honest limit to
document: the *kind* of effect is per material class, the *strength* is per mesh,
and batched objects share a bag and therefore share parameters.

Wire the numbers to the flow-graph number plane so they can be animated.

### 3. The VU panel (*Tools > VU Programs*)

- The resident set as a bar against the 2042-slot ceiling, per program, with
  what the project actually needs auto-detected (any matcap material? any lit
  mesh? any particles?) and dead weight flagged.
- Class toggles, with a warning when you drop something the scene uses.
- The stage editor for the user's program, with the generated VCL shown live and
  **the host simulation of its output** next to it — that is the feedback loop
  that makes this teachable.
- Buttons for the existing capture/replay loop, so it stops needing a terminal.

### 4. `examples/vu-lab` becomes the authoring demo

It is currently an *inspection* fixture. Ship it with a user-authored program so
opening the map shows the whole loop, and rewrite its README around authoring.
Keep the two constraints its shape already encodes (see Traps).

## Traps — all of these cost real time

- **The emitter dropped the `.xyzw` mask on `lq`/`sq`.** `disassemble` appended
  the suffix only for ops it classed as float. `sq.xyz` printed as `sq`, the
  store wrote four words instead of three, and the position's W clobbered the fog
  coefficient. Smeared sky and a torn matcap on hardware; the IR was correct the
  whole time. **This is why `--vu-check` parses the emitted source back.** Any new
  emitter path needs the same treatment.
- **A value-returning builder mints a fresh register per call.** `fogCoefficient`
  used to return a new `IVal`, giving 13 VI names where the handwritten program
  uses 11. VU1 has 16 and the simulator has unlimited virtual ones, so the
  pressure is **invisible on the host**. Library methods take their scratch from
  the caller. A stage library must follow that rule.
- **`residentFallback` must test residency before substituting.** The first
  version fell back unconditionally and would have demoted every lit mesh.
- **A resolvable capture needs a flush carrying ONE mesh.** Only the last mesh of
  a chain can be replayed. The Debugger's flush picker is how you get one; a
  terrain flush with fourteen chunks is not resolvable.
- **A project with no flow-graph node has no devkit layer at all** —
  `live_debug.gen.cpp` becomes an empty TU and the capture button does nothing.
  `vu-lab`'s pillar carries a two-node graph purely to keep it alive.
- **`vf00` is `(0,0,0,1)`** — `vf00[w]` is how you get 1.0 and `vf00[x]` a 0.0.
  Half the arithmetic in these programs leans on it.
- **Scripted multi-line edits to `src/*.cpp` need CRLF-aware matching.** The tree
  is CRLF; a python `replace` with `\n` patterns silently does nothing. Use the
  Edit tool or match `\r\n`.
- `PROGRESS.md` is retired — what changed and how it was verified goes in the
  commit message, reusable facts go in `docs/`.

## How to verify

1. `tyrax-editor --vu-check` — must PASS (parse, equivalence, round-trip, budget).
2. `tyrax-editor --build <project>` in Docker — exit 0.
3. **A/B in PCSX2 with a pixel diff.** Freeze the camera (`walkSpeed`/`lookSpeed`
   0) or the two shots are of different moments and the diff means nothing. To
   exercise `as_is`, set clipping to `precise`; the default `vu1` uses the clip
   family. The baseline run of this method scored 0 differing pixels of 1 258 400,
   and the broken intermediate scored 24% — the metric is sensitive.
4. The physical PS2 over ps2link (`--run-ps2 <ip>`), if one is reachable. Note a
   console with a game loaded conscripts any client as its file server, so `reset`
   only works with the link free.

## VU0

The framework is **VU1-only where it matters**, and the split is worth knowing
before anyone promises otherwise:

| Layer | VU0 today |
|---|---|
| `vuasm` parser | **works** - `vu0_rt_kernel.vclpp` (the raytracer, 504 instructions) parses with zero diagnostics |
| VS Code | **works** - same language, same hovers |
| `vusim` | runs it, but the machine model is VU1-shaped (see below) |
| `vugen` | **no** - the skeleton it generates *is* the VU1 pipeline |

The simulator hardcodes `kMemQuads = 1024`, which is VU1's 16 KB. **VU0 has
4 KB - 256 quadwords** - so an address the hardware would wrap at 256 wraps at
1024 here and the out-of-range warning never fires. That makes a VU0 run
plausible-looking and quietly wrong, which is the failure mode this codebase
keeps deciding is worse than an admitted gap.

The generator is VU1-only by construction, not by omission: its skeleton is
`xtop` the double buffer, emit the GIF tag block, loop, `xgkick`. A VU0 program
has none of that shape - the engine's raytracer is driven by `vcallms 0` from
the EE, restarts at instruction 0 on every call, re-reads data memory, uses
fixed addresses (its own comment says "xtop - fixed VU0 data-mem addresses
only"), and has no GIF path at all.

Making VU0 a first-class target, smallest first:

1. **Parameterise the memory size** in `vusim::Config` (a `Target` of VU1/VU0,
   1024 or 256 quadwords). Small, and it turns a wrong model into an honest
   one - worth doing even if nothing else here happens.
2. Model the `vcallms` entry contract in the harness: restart at 0, data memory
   persists between calls.
3. A second skeleton in `vugen` - a *kernel* shape: inputs and outputs are data
   memory ranges, no double buffer, no GIF.
4. Warn about the shared register file: VU0 macro mode (COP2) is the engine's
   own `Vec4`/`M4x4` math, and a micro program shares those registers with it.
   That caveat is already written up in `vu0_raytracer.hpp` and would need to
   reach anyone authoring a VU0 kernel.

## Open decisions

- **Which stages ship first.** Wobble is the obvious demo (visible, cheap, ~12–15
  instructions with a polynomial sine). The rest should be chosen by what a
  scene actually wants, not by what is easy to generate.
- **Named program sets.** Once classes can be dropped and swapped at run time,
  the natural next step is a *named set* switched by an Area or a flow node —
  reusing machinery that already exists. Prebuild those packets (as
  `billboardProgramsPacket` already is) so a switch is one send, not a rebuild.
- **Antialiasing is not a candidate**, despite being asked for: VU1 is per-vertex
  and AA is per-pixel. On PS2 that is the GS `AA1` bit or a post effect. Say so
  rather than generating something that cannot work.
