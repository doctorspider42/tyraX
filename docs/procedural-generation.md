# Procedural generation (scatter graphs)

*Tools > Procedural* — a node graph that fills a region with instances: forests,
rock fields, fence posts, orchards, roadside props. You author the RULES; the
editor turns them into ordinary static geometry when you build.

The rule that shapes the whole feature: **the graph runs on the PC, the console
gets a finished mesh.** The PlayStation 2 never evaluates a graph, never
transforms an instance, never knows the feature exists. It loads chunk meshes
the way it loads any other model.

---

## Why it is built this way (read this before tuning anything)

On real hardware every static-pipeline submit costs roughly **0.7–1.5 ms of
fixed EE overhead**, whatever the vertex count. "Draw one tree 500 times" is
therefore not a rendering strategy on this machine — 500 submits is half a
second per frame. So the bake **merges** the instances of one asset inside one
world chunk into a single mesh, and the game draws a handful of chunks.

Three consequences you will feel:

- **Triangles are the budget, not instances.** 500 trees of 600 triangles is
  300 000 triangles of static geometry — impossible. 500 trees of 120
  triangles is 60 000, still too much for a whole scene. The *Output* node has
  a live triangle budget and the window warns the moment you cross it.
  Author scatter assets **low-poly on purpose**: the Tree Generator's
  poly-budget levers (levels, sides/rings, leaf count) get a usable tree down
  to 100–150 triangles.
- **Nothing about an instance changes at runtime.** Merged geometry has no
  per-instance identity: no per-instance color, no runtime moving, hiding or
  scripting. Anything that has to move in the game is a hand-placed object,
  not scattered content.
- **Chunk size is a real trade.** Bigger chunks = fewer draw calls but coarser
  culling (a chunk is drawn whole or not at all). The default 48 units suits a
  128×128 map.

---

## The pieces

### The Procedural volume

A procedural graph lives on a **Procedural volume** — an object type you add
with *+ Add object > Procedural volume...*, or with *New volume* in the
Procedural window. Both are the same verb and land in the same place: the graph
editor with the new volume selected. Its **transform is the region**: position =
centre, scale = box size, Y rotation yaws the footprint. Move or resize it with
the ordinary gizmo and the content follows.

The object says nothing about HOW the box gets filled — that is the graph's job,
and the graph can scatter, follow a curve, or repeat something exactly. (The
type is still written to the project file as `scatter`, and the enum is still
`PrimitiveType::Scatter`; only the name you read changed, because naming the
region after one of its source nodes made it look like a choice of method.)

The volume itself is authoring-only: a wireframe box in the editor, nothing at
all in the game. It is not click-selectable in the viewport (its box usually
covers half the map, which would swallow every click) — select it in the
Project panel or from the Procedural window, then the gizmo works as usual.

A scene can hold as many volumes as you like: one per species mix, one per
region, one for the roadside lamps.

### The graph

Nodes pass **data** along links (there is no execution order to think about,
unlike the Flow Graph):

| Data | What it is |
|---|---|
| **points** | a point cloud: transforms plus named per-point attributes |
| **mask** | a 2D field over the volume's footprint, 0..1 — "how much, where" |
| **curve** | a Catmull-Rom curve in world space |

A pin only accepts its own type; the editor refuses the rest with a message,
and a link that would close a cycle is rejected outright.

**Attributes are the extension mechanism.** Every point carries named floats —
`slope` (degrees), `height`, `nx/ny/nz` (surface normal), `mask`, `size`,
`random`, `t` (along a curve), `dist` — and any node can read them or write new
ones. A node that does not know an attribute passes it through untouched, which
is why the library composes without a combinatorial explosion of node types.

### The node library

**Sources**

- **Scatter on Surface** — the workhorse: points over the terrain (or over one
  named object's mesh, sampled proportionally to triangle area) inside the
  volume. Writes normal / slope / height.
- **Scatter on Grid** — a lattice with optional jitter: orchards, fence posts,
  city blocks.
- **Scatter in Volume** — 3D fill, no surface snapping (hanging props, debris).
- **Scatter along Curve** — instances every N units along a curve, optionally
  offset to one side and yawed to follow it. Writes `t`.
- **Curve** — control points edited in the viewport (see below).
- **Single Point** — ONE point, placed exactly: at the volume's centre, at a
  named object, plus an XYZ offset. Nothing random happens here; it is the head
  of the analytic side of the graph (below).

**Masks**

- **Noise Mask** — Perlin / ridged / Worley cells / warped Perlin, with a
  feature size and a range remap. The one node that turns an even carpet into
  a forest with clearings.
- **Terrain Mask** — the terrain as a mask: height band, slope band, curvature
  (ridges vs hollows) or the weight of a hand-painted terrain layer. "Grass in
  the valleys, rocks on the ridges" is this node twice.
- **Combine Masks** — multiply (= AND), add, subtract, min, max, blend.
- **Remap Mask** — rescale and bend a mask's response (the density curve).

**Filters**

- **Filter by Attribute** — keep points whose attribute lies in a range, with a
  **soft edge**: inside the falloff band points thin out gradually instead of
  ending on a straight line.
- **Filter by Mask** — thin a cloud by a mask; also writes the sampled value as
  the `mask` attribute.
- **Minimum Distance** — enforce breathing room (a spatial-hash Poisson pass).
  With *Scale with size* a big tree holds more space than a sapling.
- **Keep Away From** — clear (or restrict to) the area around an object, around
  every solid object in the scene, or around a curve. Writes `dist`.
- **Merge Points** — up to four clouds into one.
- **Limit Count** — truncate to a budget. Because points arrive in
  low-discrepancy order, keeping the first N thins the cloud **evenly** instead
  of cutting off a corner.

**Repeat** — the analytic counterpart to the scatter sources: exact copies at
exact places, no randomness anywhere. Both nodes copy **every** point that
reaches them, so they work on one *Single Point* ("this pillar, twelve times
around a circle") and on a whole scattered field alike ("every bush, three times
up the cliff").

- **Array** — `count` copies along a straight line: a world XYZ step, plus an
  optional yaw and scale increment per copy. A stack is Step Y = the asset's
  height with *Snap* off; a row along the ground is Step X/Z with *Snap* on.
  *Step in point space* rotates the step by each point's own yaw, so posts march
  along the fence they were scattered on rather than along world X.
- **Radial Array** — `count` copies around a circle centred **on** the incoming
  point: radius, axis (Y = the flat ring), start angle and sweep (360 = a full
  circle that does not double up at the seam; less = an arc spread end to end).
  *Turn with the ring* yaws each copy to face outward.

Copies inherit the source point's attributes and its picked asset, and each one
gets its own stable identity — so a hand edit sticks to "copy 7 of that point"
and survives every later re-evaluation. Because they multiply their input, both
nodes stop at 200 000 points and say so rather than eating the frame.

**Attributes**

- **Pick Asset** — assign each point a model from a weighted pool (pine 70,
  birch 25, dead 5) plus a size inside the row's scale range.
- **Vary Transform** — random yaw, a scale range, tilt jitter, position jitter
  and **align to normal** (0 = always upright, 1 = fully laid over on the
  slope). The node that stops 500 copies from looking like 500 copies.
- **Set Attribute** — sample a mask into a named attribute, remapped into a
  value range: the bridge between the mask world and the point world.

**Output** — the terminal node: chunk size, per-chunk draw distance, cast
shadow, collision, **instance detail** (decimate the source mesh once before
merging) and the triangle budget. One per graph.

**Object Settings** — the "and all of them are like this" node: a list of
properties applied to **every scene object this volume bakes**. It has no pins,
because it is not a step in the chain — it states a fact about the whole output,
so it sits beside the graph rather than in it.

| Property | What it does |
|---|---|
| **Mesh LOD distance** | past this distance the chunk draws its decimated variant. Setting it here is also what makes the build **bake** the ~50% / ~25% tiers for these meshes, so a scattered forest gets mesh LOD without turning it on project-wide. -1 = follow the project preference, 0 = never decimate |
| **Baked lighting** | whether the chunk may take a per-texel lightmap from the GI bake (on by default — baked geometry never moves, which is exactly when a lightmap is correct) |
| **Show in reflections** | render the chunks into the dynamic environment map too. One extra render per marked object per frame, and a volume makes many objects — mean it |

This is the answer to "I want all of them to have X". Editing one generated
chunk in the Properties panel is not: a bake rebuilds its objects, so the next
density change makes new chunks that know nothing about the edit. Draw distance,
cast shadow, collision and the streaming layer are deliberately **not** in this
list — they live on Output (and the layer on the volume itself), so every field
has exactly one place.

Every node's tooltip (add menu and hover) is its full description — the
registry entry is the documentation.

---

## Working in the window

The header line is the whole state of the bake: instances, candidates, chunks,
triangles, estimated console RAM, how many nodes actually re-ran, and the
evaluation time, plus a budget bar that turns red when you cross the Output's
limit. Under it, any problems: unconnected required inputs, an empty asset
pool, a missing Output.

- **Right-click the canvas** to add a node, **right-click a node** for
  *Preview this node* / *Bypass* / *Delete*. Mouse wheel zooms, Delete removes
  the selection, Ctrl+C/Ctrl+V copy nodes (within or across graphs).
- **Preview this node** (UX-01) shows what THAT node produced instead of the
  final result — a filtered cloud, or a mask draped over the terrain in colour.
  This is the debugging tool: if the forest is empty, walk the chain until the
  points disappear.
- **Bypass** passes a node's first input straight through: an instant A/B of one
  step.
- **Seed** reshuffles everything; **Reseed** rolls a new one.

### Editing a curve

Add a **Curve** node, press *Edit in viewport*, then click the terrain to append
control points. Select a point's radio button in the node and click again to move
it there; the list also has numeric fields and per-point delete. The curve draws
as an amber line with handle spheres.

### Hand edits that survive re-evaluation

Procedural tools usually lose here: you nudge one tree, change a density slider,
and your edit is gone. Tick **Edit instances** and click an instance in the
viewport:

- drag-free **click on the ground** moves the selected instance there,
- **Offset / Rotate / Scale ×** fields nudge it precisely,
- **Ctrl+click** (or *Remove instance*) deletes it,
- *Revert* restores it to what the graph generated.

Those edits are stored against the point's **stable identity**, not its index in
an array. Change the density upstream, add a node, re-open the project — the
edits still apply to the same instances. The window shows how many edits exist,
how many no longer match a point (kept, because a point may come back when a
slider moves) and offers an explicit *Drop unmatched*.

---

## Determinism, stability, speed

Three properties the evaluator is built around. They are what make the tool
usable rather than merely clever:

1. **Determinism.** Every random draw comes from
   `hash(seed, node id, point key, channel)` — never a running counter. The
   result cannot depend on evaluation order, on thread count, or on how many
   unrelated nodes sit in the graph. Adding a disconnected node in another
   branch does not reshuffle the forest.
2. **Prefix stability.** Generators emit points from a fixed low-discrepancy
   (Halton) sequence, and density decides how MANY of them are used. Raising
   density therefore **adds points between the existing ones** instead of
   producing a different layout — which is also what keeps a hand edit attached
   to its instance, and what makes the progressive preview honest (10 % of the
   points is the first 10 % of the final ones, spread over the whole area).
3. **Caching.** Each node memoizes its output under a hash of its parameters
   and its inputs' hashes, so dragging a slider at the end of a twenty-node
   graph re-runs one node. The "nodes run" counter in the header is that number.

While a slider is being dragged the evaluation is allowed ~25 ms per frame and
falls back to a **fraction of the full density** (the readout says so); it
snaps to the full result as soon as you let go.

---

## The bake

Baking is automatic: **every build re-bakes the volumes whose graph, region,
terrain or referenced objects changed** — in the GUI (any Build/Run/Export path)
and headlessly (`--build`, `--refresh-gen`). *Bake now* in the window does it
immediately when you want to look at the result; the indicator next to it reads
`baked` or `bake is stale`.

What a bake writes, per volume:

- one merged mesh per (asset, chunk) as
  `res/models/<asset dir>/procgen-<volume>-<asset>-x<i>z<j>.obj`. It sits in the
  source asset's own folder on purpose: the same `mtllib` line then resolves
  unchanged, so textures, per-asset texture-quality overrides and atlasing
  behave exactly as they do for the source model;
- one **scene object** per chunk, named `<volume>#<asset>-x<i>z<j>`, positioned
  at the chunk centre (so per-chunk draw distance and frustum culling measure
  the right distance) and marked as generated.

Those chunk objects are ordinary Model objects, which is the point: the
`.tmdl` bake, distance mesh LOD, texture quantization, the disc layout and
frustum culling all work on them with no special case. They are **not drawn in
the editor viewport** — the live graph preview stands in for them, and drawing
both would double every tree. Re-baking matches chunks by name, so object
identity (and with it live link and collaboration) survives.

Deleting a volume deletes its chunk objects and their mesh files. *Clear bake*
does the same without deleting the volume.

---

## Budgets, in practice

Numbers from the demo scene used to verify the feature (128×128 map, 69
instances of two Tree-Generator trees at 132 and 380 triangles, chunk size 48):

| | |
|---|---|
| instances | 69 |
| chunk meshes / draw calls | 18 |
| triangles | 13 572 |
| PS2 frame rate (PCSX2, software renderer, vsync off) | 109 FPS |

That is comfortably above the 50 FPS PAL cap, i.e. there is room — but note how
few instances it takes to reach 13 k triangles. Halving the tree count or
halving the source detail is a bigger win than any renderer trick.

**Instance detail** (Output node) decimates the source mesh once before merging.
It helps trunks and rocks; it does **not** help a canopy made of alpha-cutout
leaf cards, because every card is its own border-locked quad — a card canopy can
only be made cheaper by authoring fewer, bigger cards.

---

## Limits (deliberate, for now)

- **Models only.** The asset pool takes `.obj` models from `res/models`
  (the Tree Generator is a good source). Primitives are not scatterable — make
  a model.
- **Uniform scale per instance.** Merged geometry, PS2 economy.
- **No per-instance color / material variation.** Variation comes from the
  asset pool and the transform.
- **No procedural terrain.** The graph READS the terrain (height, slope,
  curvature, painted layers) but does not generate it — terrain is sculpted and
  painted in the Terrain Editor.
- **No spline geometry extrusion** (roads, walls). Curves place instances and
  clear space; they do not build a mesh yet.
- **Painted density masks** are not in yet: mask density comes from noise, the
  terrain and object/curve distance. Painting the terrain layer you want and
  reading it with a *Terrain Mask (Painted layer)* covers most of that need
  today.

---

## Troubleshooting

**Nothing appears.** Check the problem list under the budget bar. The usual
causes: the Pick Asset pool is empty, no Output node, or a filter cut
everything — right-click nodes and *Preview this node* down the chain to find
where the points vanish.

**Instances float above or sink into the ground.** The terrain changed after the
bake: the indicator will say `bake is stale`; build (or *Bake now*).

**Trees inside a building.** Add a **Keep Away From** node with an empty Object
field — with no curve connected it clears every solid object in the scene.

**A hard line where the forest stops.** Raise *Falloff* on the filter that ends
it; that is exactly what the parameter is for.

**Over budget.** In order of effect: lower the density, use a lower-poly asset,
set *Instance detail* to Half/Quarter, add a *Limit Count*, or give the chunks a
*Draw distance*.
