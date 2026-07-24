# Tree Generator

*Tools > Tree Generator* authors low-poly trees procedurally and bakes them into
the project as ordinary assets. Inspired by [EZ-Tree](https://github.com/dgreenheck/ez-tree)
(MIT), reimplemented host-side in `src/treegen.cpp`.

The design goal was deliberately narrow: **not** a runtime forest system, just a
way to stop hunting for tree models. You dial a tree in, the editor writes
`.obj` + `.mtl` + two PNGs into `res/models/trees/`, and a normal **Model**
object enters the scene. Nothing about the PS2 side changes — no new object
type, no serialization field, no codegen, no engine work. A generated tree is
indistinguishable from a tree you modeled in Blender and imported.

## Why it costs nothing on the PS2

The generator is a **host-only module** (the `stochtile` / `matbake` /
`decalproj` pattern): no GL, no `Project` dependency, pure functions over a
`Params` struct. Generation happens in the editor; the console only ever loads a
static mesh with two materials.

Transparency comes free too: the static pipeline already alpha-tests and
alpha-blends material textures, so leaves are the classic textured cards. The
leaf PNG bakes **hard binary alpha** (0 or 255) on purpose — the engine's
tRNS→CLUT path loses a soft gradient, and a hard cutout is what the era did
anyway. Opaque colors are dilated into the transparent margin so bilinear
sampling never rings a dark fringe around a leaf.

## Determinism

Everything is a pure function of `Params`, and `seed` drives every random draw,
so the same knobs always produce byte-identical output. Consequences worth
knowing:

- **"Five oak variants" is five seeds**, not a mesh library — a generated tree
  is ~50 KB of `.obj`, so variants are cheap to keep in the project.
- Each branch derives its own RNG stream from `(parent seed, child index)` via a
  splitmix mixer, so nudging one slider does **not** reshuffle the whole tree:
  siblings and their subtrees keep their shapes. Without this, every drag of
  "Trunk sides" would regenerate a different tree and the tool would feel random
  instead of adjustable.
- **Roll** re-seeds (same shape family, new layout); switching **Preset** keeps
  the current seed, because a preset is a shape, not a dice roll.

## The poly budget

The whole point is trees that fit a PS2 frame, so tessellation is explicit
rather than emergent. Radial sides and length rings **interpolate from the trunk
values down to the `*Min` values on the outermost branch level** — the trunk
gets 6 sides, twigs get 3, and detail lands where it reads.

The six presets land at **437–943 triangles** each (measured, see PROGRESS).
The window shows a live triangle count, green under 1800, amber past it, red
past 3000 with a nudge to trim — advisory, never blocking (the importer's own
≤3000-tri guidance for models still applies).

Practical ceiling is **instance count, not tree cost**: models don't join static
batching (only primitives do), so every tree is its own draw bag. A dozen or two
per scene is comfortable; a forest wants a different technique (billboards, or
batched primitives).

## What the UI gives you

A parameter panel on the left, a live turntable preview on the right (drag to
orbit, wheel to zoom, *Spin* runs the turntable, *Wireframe* shows the
tessellation), a triangle readout, a name field and **Add to scene**.

The preview renders from the **in-memory** mesh and textures — nothing touches
disk or the shared asset caches until you add — so slider drags stay instant.
It renders into its **own framebuffer**, not the Material Editor's: both tools
can be open at once and size their previews independently.

Parameter groups: **Trunk** (height, base radius, root flare, taper,
gnarliness, upward pull), **Branches** (levels 1–4, children per level, tilt +
jitter, length/radius ratios, spawn start), **Leaves** (count, size, aspect, how
many outer levels get them, foliage style), **Appearance** (bark style, four
colors) and **Detail** (the poly-budget levers above).

Presets: **Oak**, **Birch**, **Spruce**, **Poplar**, **Dead tree** (no leaves),
**Bush**.

Bark styles bake tileable procedural textures (rough ridges, birch lenticels,
cracked plates); foliage styles bake the leaf card (broadleaf cluster, needle
sprig, single blade).

## Files it writes

For a tree named `oak`, into `res/models/trees/`:

| File | What |
|---|---|
| `oak.obj` | the mesh, two `usemtl` groups (`bark`, `leaf`) |
| `oak.mtl` | the two materials, each with its `map_Kd` |
| `oak-bark.png` | tileable bark, 128² RGBA |
| `oak-leaf.png` | leaf card, 128² RGBA with a hard alpha cutout |

Positions and UVs are de-duplicated on exact bits (tube rings share most
corners — roughly a 2.4× smaller vertex list than raw triangle corners). A
bare tree (leaf count 0) writes no leaf material or PNG.

Adding a tree whose name is taken picks the next free `-N` suffix rather than
overwriting, so re-adding never clobbers an earlier tree that scene objects
still reference.

## The AMD driver crash this shook out (fixed)

Generated trees turned out to reliably trip a **GL driver bug** that predates
this tool: opening the Tree Generator, or adding a tree while the Material
Editor was open, killed the editor ~50-100% of the time — an access violation
(`0xc0000005`) inside `atio6axx.dll`, at the same fault offset every time.

gdb put it in `glTexImage2D` reached from `Viewport::glTexture`, with every
argument valid (128², RGBA8, power-of-two, non-null pixels). So the arguments
were never the problem — the single-call *form* was. Every RGBA upload now goes
through **`glUploadTexRgba()`** (`gl_loader.h`), which allocates the level empty
and fills it with `glTexSubImage2D`; that path does not fault. Verified 0/12
crashes across the three paths that previously failed 4/4, 3/4 and on every
attempt.

If you are adding a texture upload anywhere in the editor, use that helper
rather than handing pixels to `glTexImage2D` — see PROGRESS entry 101.

## Where the code lives

| File | Role |
|---|---|
| `src/treegen.hpp/.cpp` | `Params`, `generate()`, the two texture bakers, `writeAssets()`, `presets()` — host-only, no GL |
| `src/app.cpp` | `drawTreeGeneratorWindow()` (the tool), `rebuildTreePreview()`, `addTreeToScene()` |
| `src/viewport.cpp` | `renderTreePreview()` + its own framebuffer |

`addTreeToScene()` hands off to the existing `addModelObject()`, so naming,
selection and `commitChange()` behave exactly like any other model import.
