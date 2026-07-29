# blocks-terrain example

A world made of cubes, **generated on the PlayStation 2 itself** — nothing about
it is on the disc except the graph that describes it and five one-cube models.

Open `blocks-terrain.tyra` in the editor and Build & Run (`F5`), or build
headless: `tyrax-editor.exe --build <this folder> --run`.

**TRIANGLE builds a new world.** That is not a reload: the graph runs again on
the EE with a fresh seed, the old geometry is thrown away and a different
landscape is merged in its place, in a fraction of a second.

## What it demonstrates

[Runtime procedural generation](../../docs/procedural-runtime.md) — a Procedural
volume in **Runtime** mode, compiled into the game and evaluated by the console:

| | |
|---|---|
| block size | 1.5 units (deliberately shorter than the 1.8 player) |
| lattice | 44 × 44 columns, up to 14 blocks tall |
| blocks emitted | ~2 400 |
| chunk meshes / draw calls | ~9 (24-unit chunks) |
| PS2 frame rate (PCSX2, software renderer) | **50 FPS** — the PAL cap |

The interesting number is the one that is *missing*: a 44 × 44 × 14 field is
27 000 cubes, and 2 400 of them exist. The rest are never generated at all,
because nothing can see them.

## How the graph works

`Blocks Fill` builds the columns from four octaves of Perlin noise and emits one
point per **visible** block, each carrying `depth` (0 = the top of its column),
`height` (world Y) and `faces` (a 6-bit mask of which of its faces a neighbour
does not cover). Five ordinary `Filter by Attribute` branches then decide what
each block is made of, and two `Merge Points` put them back together:

```
Blocks Fill ─┬─ depth 0, height ≥ 18 ── Pick Asset (snow)  ─┐
             ├─ depth 0, height ≤ 6  ── Pick Asset (sand)  ─┼─ Merge ─┐
             ├─ depth 0, in between  ── Pick Asset (grass) ─┘         ├─ Merge ─ Output
             ├─ depth 1              ── Pick Asset (dirt)  ─┐         │
             └─ depth ≥ 2            ── Pick Asset (stone) ─┴─ Merge ─┘
```

Nothing in that chain is block-specific — it is the same filter/pick vocabulary
a forest uses, reading two attributes one source node happened to write. The
`faces` mask is honoured by the merge, which drops any triangle whose outward
normal points at a face a neighbour covers: on flat ground a block costs two
triangles instead of twelve.

## Walking on it

The block field is also the world's **collision**: one 32-bit word per column,
bit *i* = level *i* is solid. The walker reads its floor, its ceiling and its
walls out of that. Exactly one block is climbable in a stride, which is why the
cubes are 1.5 units under a 1.8-unit player — with 2-unit cubes every step up
would be a wall.

The flat terrain underneath is the bedrock the lowest layer sits on; the blocks
are what you actually stand on.

## The files

- `build-scene.py` — authors the volume, its graph and the regenerate button
  into the `.tyra`. A graph this size is unpleasant to build by hand in the node
  editor and impossible to review as JSON; this script is its readable form.
  Re-run it after `tyrax-editor --new` to rebuild the example from scratch.
- `res/models/block-*.obj` + `blocks.mtl` — five flat-coloured **unit** cubes.
  Unit, because the procedural instance scale is the block size directly; flat
  colours, because a block world is thousands of merged faces and a texture
  would be resident in GS VRAM forever for no gain at this scale.

## Things worth trying

- Raise **Emit depth** on the Blocks Fill node from 2 to 4 and watch the cliff
  faces fill in — and the instance count climb.
- Set **Relief** to 0 for a flat slab, or **Feature size** to 12 for badlands.
- Switch the volume to **Baked** in the Procedural window and press *Bake now*:
  the same graph, written to disc as ordinary chunk meshes. You lose the fresh
  world per run and the block collision, and you gain a scene that costs the
  console nothing at load.
