# Runtime procedural generation

The other half of [procedural generation](procedural-generation.md): a
Procedural volume has a **mode**, at the top of the *Tools > Procedural* window:

| Mode | Where the graph runs | What ships |
|---|---|---|
| **Baked** (default) | in the editor, at build time | finished chunk meshes; the console never learns a graph existed |
| **Runtime** | on the EE, while the game runs | the graph, compiled to C++ (`src/gen/procedural.gen.cpp`); no geometry at all |

A volume is one or the other. Baking a runtime volume would defeat the point,
and running a baked one would need nodes the console has no data for.

Working demos: [examples/blocks-terrain](../examples/blocks-terrain) (a block
world generated at boot and re-generated on a button) and
[examples/cube](../examples/cube) (prefab rooms on a 3D lattice).

---

## What you get, and what it costs

**Get:** a world that is different every run, and a map that takes no disc
space — two dozen kilobytes of compiled generator instead of a megabyte of
merged meshes.

**Pay:**

- **Load time.** Generation happens inside the scene load and is the one part
  that can take visible time. It reports into the loading screen's progress bar
  (see below) rather than freezing on the last drawn frame.
- **RAM.** The working buffer is real memory: about 80 bytes per point, held
  for the whole generation. The *Output* node's **Runtime instance cap** is
  that number — set it just above what the preview reports, not at the maximum.
- **A much smaller vocabulary.** The console has a heightmap, a few models and
  32 MB. It does not have your `.obj` files, your splat map, or your scene
  graph.

That last one is written down rather than discovered: the moment you switch to
Runtime, the window lists every node this graph cannot run, under the budget
bar.

---

## What the console can run

| Category | Runs at runtime |
|---|---|
| Sources | Scatter on Surface (**terrain only**), Scatter on Grid (including its 3D *Levels*), Scatter in Volume, Single Point, **Blocks Fill** |
| Masks | Noise Mask, Terrain Mask (height / slope / curvature — **not** a painted terrain material), Combine Masks, Remap Mask |
| Filters | Filter by Attribute, Filter by Mask, Minimum Distance, Merge Points, Limit Count |
| Repeat | Array, Radial Array |
| Attributes | Pick Asset, **Pick Prefab**, Vary Transform, Set Attribute |
| Output | Output (Object Settings is accepted and ignored — there are no generated scene objects to set properties on) |

Not available: **Curve** and **Scatter along Curve**, **Keep Away From**
(both need geometry the console does not carry), and **Terrain Mask > Terrain
material** (the splat map is a build-time asset). *Scatter on Surface* with a
named object is refused for the same reason — clear the field and it scatters
over the terrain.

Two costs worth knowing before you reach for them: **Minimum Distance** is a
greedy O(n²) sweep on the console (fine for hundreds of points, not for
thousands), and a node feeding two consumers is **evaluated twice** — the
correct dataflow meaning (each branch gets its own copy of the cloud), and why
"one source, five filter branches, one merge" costs five passes over the
source.

---

## Determinism, and where the randomness comes from

The compiled generator is a faithful twin of the editor's evaluator: same hash,
same Halton sequence, same per-point channels. Given the same seed, the preview
in the viewport and the world on the console are the same world — which is what
makes a runtime volume authorable at all.

**Seed** in the window is that seed, and the mode next to it decides what
happens at runtime:

- *Same world every run* — reproducible, and what you want while authoring.
- *New world every run* — rolled from the console clock at generation time.

Be honest with yourself about the second one: at scene load the clock has only
the few milliseconds of jitter the boot took. The variety you can actually feel
comes from **regenerating on a player action** — a *Generate Volume* node fired
from a button, where the player's own timing is the entropy. Both examples do
exactly that (TRIANGLE).

### The seed simulator

Once a volume rolls its own seed, the number in the *Seed* box stops describing
what a player will get — the viewport is showing one draw out of many. The
**Seed simulator** (the collapsed section under the tool row, runtime volumes
only) answers that: press *Simulate* and the editor evaluates the graph on N
seeds — the authored one first, then the sequence *Reseed* itself hands out —
and tabulates what each world costs.

| | |
|---|---|
| a row | click it to show that world in the viewport; the rest of the scene stays on its own seeds |
| *Use* | adopt that seed as the graph's, which is how you keep a layout you liked |
| the summary | the instance and triangle SPREAD over the sweep, and how many seeds blow the Output node's triangle budget |

That last line is the point of the feature. A runtime volume that fits the
budget on the seed you authored with, and overruns it on one boot in eight, is
a bug you would otherwise meet on the console. The simulated seed is a way of
LOOKING at the graph, never an edit: it does not touch the `.tyra`, does not
make a bake stale, and is dropped when you switch volumes.

One trial is one full evaluation, so sixteen seeds is sixteen times the
millisecond figure in the readout above — the tooltip says so, and it is why
this is a button and not a live number.

## Generate Volume

*Flow Graph > **Procedural** > Generate Volume* — the node that runs a runtime
volume by name:

- `generate` — build it. num[0] Seed: `0` = the volume's authored seed, `-1` =
  roll a fresh one, anything else = use that value. **So re-rolling the world is
  one node with Seed `-1`** — that is what `examples/cube` fires from TRIANGLE.
- `clear` — throw the generated geometry away.

The Seed also accepts a **wired number**, which is the difference between a
random world and a *chosen* one: feed it a save value, a level counter or any
Math chain and the same value always rebuilds the same world. That covers
"restore the map this save game had" as well as "level 7 always looks like
level 7" — no geometry stored either way. (A wired number means Live Logic
cannot hot-patch that graph; see [live-logic.md](live-logic.md).)

With *Generate at scene start* off, nothing appears until this node fires —
which is how you stage a world in pieces, or build it only after a cutscene.

---

### The prefab instance pool is a second budget

A *Pick Prefab* point costs a **prefab-instance record** on top of its
triangles, and the generated game holds `MAX_PREFAB_INSTANCES` of them (48 -
`prefab::kMaxRuntimeInstances`, one constant read by both codegen and the
editor). Past that the runtime logs `Spawn Prefab: instance pool full` and
builds nothing more.

This is the budget that bites first when you scatter prefabs rather than
models, and it is nastier than the triangle one because of the order points
come out in: *Scatter on Grid* runs its level loop outermost, so what survives
is the bottom of every stack - a graph asking for eight levels renders as **one
row**, which looks like a generation bug rather than a ceiling. The window
warns as soon as the count crosses it.

A **model** picked with *Pick Asset* costs no instance record at all - it
merges straight into the chunk bags. For hundreds of copies of one simple
shape, that is the cheaper node.

## How it draws

The same way the bake does, one step further: instances of one asset inside one
world **chunk** are merged into a single vertex bag the game built itself, and
the frame draws a handful of bags. Chunk size, per-chunk draw distance and the
triangle budget on the *Output* node mean exactly what they mean for a bake.

What generated geometry does NOT get, because it does not exist at build time:
a lightmap region (it is lit from the light-probe grid instead), a place in the
static-batch list, and an entry in the scene table. It is not a scene object
and nothing can address it.

**Collision** comes from two sources. Merged members with collision enabled
contribute conservative world AABBs the walker tests (see
[prefabs](prefabs.md)), and a **Blocks Fill** node publishes its solid field as
the world's ground.

---

## Blocks Fill

The block-world source, and the one node whose value is in what it does *not*
emit.

It fills the volume's footprint with columns of cubes from layered noise (or
from a mask you feed it), then walks the field and emits **only blocks with an
exposed face** — a solid volume's interior is invisible, and generating it just
to filter it away would cost thousands of points for nothing. Each emitted
block carries a **`faces`** attribute: a 6-bit mask of which of its faces a
neighbour does not cover. The merge honours it by dropping any source triangle
whose outward normal points at a covered face, so a flat plain costs one quad
per block instead of twelve triangles.

It also writes **`depth`** (0 = the top block of its column, 1 = the one under
it, …) and **`height`** (world Y), which is how a block world gets grass on top,
dirt under it and stone below that — with the ordinary *Filter by Attribute* and
*Pick Asset* nodes, not a special one:

```
Blocks Fill ─┬─ depth = 0, height ≥ 18 ─ Pick Asset (snow)  ─┐
             ├─ depth = 0, height ≤ 6  ─ Pick Asset (sand)  ─┤
             ├─ depth = 0, in between  ─ Pick Asset (grass) ─┼─ Merge ─ Output
             ├─ depth = 1              ─ Pick Asset (dirt)  ─┤
             └─ depth ≥ 2              ─ Pick Asset (stone) ─┘
```

Nothing in that chain is block-specific. `faces` is a plain attribute for the
same reason: any asset merged anywhere in the graph honours it, and a node that
does not know about it passes it through like every other attribute.

**Emit depth** is how many blocks below a column's top are drawn. The rest of
the column still exists for collision — you simply cannot see it. 1 is a shell
one block thick (fine on flat ground, seams on cliffs), 2–3 covers ordinary
terrain.

### The block field is the ground

In a runtime volume the solid field is published as the world's collision: the
walker gets its floor, ceiling and walls from a bitfield lookup (one 32-bit
word per column — which is why a block world is capped at **32 levels** and at
32 768 columns). **Exactly one block is climbable in a stride.** That is the
rule every block game uses and the one that decides whether a landscape of
cubes is walkable at all — so make the block **shorter than the player**: a
1.5-unit cube under a 1.8-unit walker works, a 2-unit cube means every step up
is a wall.

Only one Blocks Fill per runtime volume — there is one field.

---

## The loading screen

Generation is counted into the loading screen's work total and reports progress
as it goes, so the bar reflects the real load instead of reaching 100 % and
sitting there. Nothing to configure: if the project has a loading screen
(*Preferences*, on by default — see [loading screens](loading-screens.md)), a
runtime volume shows up in it.

---

## Troubleshooting

**Nothing appears, and the game log says `no model for asset ...`.** The asset
pool names a model the build did not ship. Re-run the build: the model table is
collected from the pools, so a fresh entry needs a codegen pass.

**"N node(s) the console cannot run".** The list under it says which and why.
Either replace the node or switch the volume back to Baked.

**The instance cap is reached** (the game log says so). Raise *Runtime instance
cap* on the Output node, or thin the graph — the generator stops rather than
overrun its buffer.

**The world is the same every run** with *New world every run* set. Check that
something actually re-generates: at boot the clock has barely moved. Wire a
*Generate Volume* with Seed −1 to a button.

**The player falls through the blocks.** The block field belongs to the volume
that generated it; a *clear* leaves nothing to stand on. And blocks only carry
collision in **runtime** mode — a baked block volume is ordinary static meshes,
which collide the way any baked chunk does (i.e. not at all unless the Output
node's Collision is on).
