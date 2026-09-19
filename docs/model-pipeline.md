# Static models: the .tmdl pipeline and mesh LOD

![Browsing model assets before placing them](img/asset-browser.png)

You author static geometry as `.obj` and nothing about that changes. What
changed is what the **game** reads: the build compiles every `.obj` a scene
uses into a binary `.tmdl` next to it, and that is what ships on the disc.

(Animated `.glb`/`.fbx` models have had their own binary format, `.tskl`, all
along - see [animated-models.md](animated-models.md).)

## How big is this model, really?

Selecting a static model shows three numbers in *Properties*:

```
4281 triangles, 12843 vertices (3351 unique positions)
```

The gap between them is the interesting part:

- **unique positions** is the `v` count - what the modelling tool told you;
- **vertices** is what actually reaches VU1: three per triangle, with a corner
  split wherever its normal, UV or material differs. This is the number the
  pipeline cuts into VU1-sized chunks (*Debugger > Stats* reports how big a
  chunk is - it is the VU1 buffer's capacity for that vertex layout, e.g. 108),
  and the number a frame's vertex budget is spent in.

A vertex count far above `3 x positions` is paying for split corners - hard
edges and material boundaries everywhere. The cottage above is 1.28x, which is
ordinary; a smooth-shaded sphere would be near 1.0x.

Animated `.glb` models report their own vertex count in the same place.

## Why the game stopped reading .obj

An `.obj` is text. Reading one on the PS2 meant parsing ASCII on a 300 MHz
CPU every single time the model loaded: a string stream per line, float
parsing per number, a normal computed per face, a material lookup per
`usemtl` group. On a 9 216-vertex model that measured **286 ms** - and with
[streaming layers](streaming-layers.md), where a layer's assets load one per
frame, a third of a second lands inside one frame as a visible hitch.

The build now does all of that once. Reading the same model as `.tmdl` takes
**39 ms**, and the whole load (textures included) went from 306 ms to 59 ms.
The PS2 side is a sequential read plus a memory copy per material.

The `.tmdl` carries the triangulated mesh, flat face normals, the resolved
material assignment (including a per-object `.mtl` override), texture-atlas
UV rectangles already folded into the UVs, texture paths as the game will
open them, the LOD levels below - and, when the project has *Flashlight shadow
volumes* on and the model is over the volumes' 1200-triangle budget, a
positions-only **shadow proxy** decimated under that budget, which is what the
torch extrudes the model's shadow from (docs/flashlight.md, "The shadow").

## Whole-model frustum rejection

A static model with at least three material parts gets one conservative AABB
over all of its baked vertices. The generated game tests that box before it
submits the individual parts in the main view and in portal through-views. A
fully off-screen district therefore costs one six-plane AABB classification,
not one pipeline entry and a set of package-bound tests per material. Objects
that touch the view retain the existing per-part and per-package culling, so a
large building crossing a screen edge does not turn into one oversized draw.

The box is rebuilt only when the object's geometry is rebuilt. It remains
conservative across mesh-LOD tiers, which only remove vertices, and follows the
matrix fast path in object space. A custom VU program that moves geometry skips
this early rejection because its displaced vertices may leave the baked box.

Consecutive material bags that point at the same model matrix also reuse their
MVP and object-space frustum planes within the frame. The cache key includes the
model and view-projection values, not only their addresses: physics may mutate a
matrix in place, while portal and split-screen passes replace the camera. Frame
end clears the cache, and projection-only (`TyraMP`) submissions bypass it.

## Compact static-model batching

> **To see what this does to your own scene**, open *Tools > Static Batches*
> or run `--batch-report`: it lists every batch with its members, merged box
> and VU1 packages, and names the reason for every object that is not batched.
> [static-batching.md](static-batching.md) explains how to read it, and why
> the merged box is the number that matters.

Static batching also accepts compact, immutable imported models when they do
not use distance LOD, impostors, reflections, dynamic lighting or another
per-object runtime path. Each material part joins the batch for its actual
loaded texture and coarse world cell; atlas-backed materials therefore merge
even when their source material names differ. Singleton groups are discarded,
and a model whose horizontal footprint exceeds half a cell stays solo. Runtime
mutation demotes every part of that object from its batches before drawing it
through the normal path.

Those spatial limits are intentional. An earlier Aster experiment grouped
model parts by material across districts: it destroyed culling, and even
per-district groups widened four bags enough to cost roughly 3.8-4.5 ms in
PCSX2. The retained path targets repeated props rather than architecture and
keeps the whole-model reject above for large or LOD-switched meshes.

### Draw distance on a batch

A per-object draw distance used to disqualify an object from batching
outright, and on a real scene that one line was the whole story. **Every one
of the Motor District's 70 imported models carries `drawDistance = 145`**, so
the exclusion kept all of them solo and left static batching holding 27
objects - the walls, aprons and pavements - out of 142. That is the exact
population compact static-model batching was written for, rejected by a rule
that had nothing to do with geometry.

The cut-off now belongs to the batch:

- **It joins the group key**, next to the texture and the world cell, so every
  member of a batch shares one number. Objects that leave it at 0 group
  exactly as they did before, and two props with different cut-offs never land
  in the same bag.
- **`renderStaticBatches` tests it once per batch**, against the nearest point
  of the box over its members' *positions* - the same centres the solo path's
  `beyondDrawDistance()` measures. The box is rebuilt whenever the batch is,
  which includes demotion.

It is deliberately **not** routed through the shown snapshot that handles
hide/show. A cut-off crossed while the player drives is a per-frame flip, and
re-baking a batch every frame costs far more than the submit it saves - the
same reasoning that demotes a per-frame-animated member.

The trade this makes is worth stating plainly: **a member can outlive its own
draw distance, by at most the spread of its batch (bounded by the grouping
cell). It can never disappear early.** Over-drawing costs GS fill; vanishing
early would be a visible pop, and these frames are bag-bound rather than
fill-bound.

**A static count over the authored scene predicted −27% bags in the garage
pose; the running game says +1.7%, and the running game is right.** The
projection counted batches, which is the number that falls (8 → 48 for 18 →
65 objects). What the EE pays for is bags *submitted*, and a batch's bounds
are the union of its members, so it passes the frustum where its members
individually would not. Counting groups is not counting draws. The measured
tables are in [profiling.md](profiling.md), "What it actually costs".

The guard that actually protects culling is the half-cell footprint limit
above, and this change does not touch it — but the same widened-bounds effect
it exists to prevent is what shows up here at a smaller scale, which is why
the grouping cell was the next lever to measure. It has been measured, and the
section below is what it found.

### Why the cell is bounded by the draw distance

The cell was a quarter of the map, floored at 48 units. **A quarter of the map
is a fraction of the wrong thing**: it grows with the world, so the bigger the
map the coarser the cull, which is backwards. A 320-unit district got an
80-unit cell and a 2048-unit map got a **512-unit** one.

That is how the feature that wins 0.46 ms on the Motor District lost **3.20 ms,
29% of the frame**, on `examples/large-terrain`
([the second map](engine-performance-on-a-second-map.md)). It merged **1,100
objects into 4 batches**. Each of those four bags is one bounding box and one
cut-off test, and this map's cones carry `drawDistance` 60 — so props that
individually vanish at 60 units stayed drawn while the camera was within 60
units of a **512-unit** box.

Read that mechanism carefully, because it is not quite the one `#269`
predicted. The warning was about the **frustum**; what dominated here was the
**draw-distance** test, which `renderStaticBatches` applies once per batch to
the nearest point of the member-centre box. Both widen with the cell, so
bounding the cell fixes both — but the draw-distance half is what made this map
lose four times what the district gained.

**The cell is now never wider than the draw distance its members share.**
`drawDistance` was already a group key (every member of a batch agrees about
it), so it is a per-group length *the scene states about itself* — not a
constant anyone tuned, and not a property of how big the map is. The grid is
per draw-distance class, and classes never merge because the key keeps them
apart.

| map | terrain | draw distance | cell before | cell after |
| --- | ---: | ---: | ---: | ---: |
| Motor District | 320 | 145 / 0 | 80 | **80** (unchanged) |
| large-terrain | 2048 | 60 / 80 | 512 | **60** |

**The district is unchanged by construction**, which is the point of deriving
the bound rather than picking one: `min(80, 145)` is still 80, so the map the
feature was tuned on does not move at all — its 65 objects in 48 batches, its
counters and its picture are identical either side.

On large-terrain the counts return **exactly** to what the scene costs with
batching switched off, while still merging 1,085 objects into 198 batches:

| per frame | batching off | batching on (before) | batching on (after) |
| --- | ---: | ---: | ---: |
| objects batched | — | 1,100 in **4** batches | 1,085 in **198** batches |
| widest cell | — | 512 | **60** |
| triangles | 10,297 | **12,392** | **10,297** |
| submitted vertices | 30,891 | 37,176 | 30,891 |
| packet flushes | 21 | **33** | **21** |
| VU1 packages | 320 | **825** | **320** |
| pixels differing from unbatched | — | **400** | **0** |

The last row is the one to read first. The old batching did not merely cost
time: it **drew cones the unbatched scene culls**, 400 pixels of them. The
bounded cell renders the frame byte-identically to no batching at all, so the
saving is now free rather than paid for in over-draw.

**Why a tightness ratio was rejected.** The obvious alternative — batch only
when the members fill their union — measures the wrong thing for this engine.
The district's win comes precisely from merging small props that are *sparse*
in their cell: three boxes of span 12 in an 80-unit cell fill 6.7% of it. Any
ratio strict enough to catch a 512-unit cell also throws away the batches that
pay. What costs is extent against the cull distance, which a fill ratio cannot
see.

### The case the bound does not cover

`drawDistance` 0 means unlimited, and an unlimited member states no length, so
those groups keep the base cell. That hole is real, and it was measured rather
than assumed: `examples/large-terrain` with every draw distance zeroed, which
is the adversarial scene for this rule.

| per frame | batching off | batching on (cell 512) |
| --- | ---: | ---: |
| triangles | 21,417 | 26,460 (+23.5%) |
| packet flushes | 200 | **114 (-43%)** |
| VU1 packages | 1,551 | 1,932 |

**That is a trade, not the dominated loss the draw-distance case was.** With
cut-offs in play, batching was worse on *both* axes at once — more triangles
**and** more flushes — which is why it could only lose. With no cut-offs the
frustum widening costs geometry, but the merge still removes 43% of the packet
flushes, and on this console a submit is the expensive half. Whether that pays
is a hardware question and is not settled here.

So, the honest range of the rule: it removes the regression wherever the
members have a draw distance, it is inert where the cell already fits (the
district, and any small map), and on unlimited-distance content it leaves the
existing behaviour alone rather than guessing. A scene of many wide-spread
objects with no draw distance at all is the shape to measure next.

**Note the pixel caveat on that adversarial scene**: its own repeats are not
byte-identical (283-419 px of 200,704 differ between two captures of one arm),
so only its counts are quoted above. The two real maps are clean instruments -
three captures per arm, byte-identical - which is what lets their pixel rows
mean anything.

### A batch must keep the strips, or it costs more than it saves

This one was measured, not reasoned about, and it nearly sank the whole
change. `rebuildStaticBatch` re-emits each member into the combined array —
and it read `GameModelPart::verts`, the **triangle list**. So batching a
stripped model silently threw away the strip the build had baked for it, and
on a district of stripped models that cost more than the submits it saved.
In the garage-day pose, per 50-frame window (PCSX2, software renderer, frozen
camera, counters read from the game's own `bin/log.txt`):

| counter | before | naive batching | strip-aware |
| --- | ---: | ---: | ---: |
| cull packages | 41 175 | 41 825 | **40 925** |
| strip packages | 25 925 | 22 025 | **25 675** |
| vertices per frame | 54 930 | 56 754 | **55 602** |
| packet flushes | 5 900 | 6 000 | 6 000 |

**Naive batching moved every EE-side counter the wrong way** — +3.3%
vertices, +1.7% bags — while `strip` fell by 3 900 packages, which is the
fingerprint: the batched parts had come back as lists. (`trianglesCull`
*fell* 5.8% at the same time, and that is exactly the trap the
`StaPipTelemetry` header warns about: a strip counts `size - 2` primitives
including its degenerates and a list counts `size / 3`, so the triangle
counter is meaningless across a representation change. Read `verts`.)

The fix is to concatenate the **runs**. A batch whose members share a run
length holds their strips end to end and pins `packageSize` to that same
number, so every package is exactly one run of one member and no package
boundary can splice two members into one triangle — the same contract
`pinPackageSize` already enforces for a solo stripped bag, reused. `stripRun`
therefore joins the group key (0 = plain list, which is every primitive and
any part whose strips came out no smaller), and the strip/list choice is
**all-or-nothing per batch**: one array carries one topology, so emitting one
member's strip beside another's list would hand the strip's vertices to a
triangle-list walk.

**Each member has to be padded up to a whole run, and missing that made the
first attempt a silent no-op.** `meshstrip` chops a strip into runs of *at
most* `kRun` vertices and pads only the runs before the last — the final run
need only be a multiple of 3. For a solo bag that is harmless, because the
short run is the end of the array. Concatenate another member after it and
the next member starts mid-package. Repeating the last vertex until the array
is back on a run boundary is the same trick the baker uses between runs, and
it costs at most 71 degenerate vertices per member. The first version instead
*refused* to strip any batch whose member was not already a whole number of
runs, which is every batch — it built, it ran, and every counter came back
byte-identical to the unstripped arm, which is exactly what a no-op looks
like.

### A batch gets ONE dynamic light, so the lamp is part of the key too

`StaPipCore::render` gives every bag a single light slot, picked from that
bag's world bounding sphere. A batch is one bag, so **merging a lamp-lit prop
with an unlit one shades both from whichever lamp the merged sphere happens to
pick** — the lit one can lose its highlight, the unlit one gain one.

This is not hypothetical. Letting the district's models batch put 7 of 49
batches in exactly that state: a streetlight standing directly under a night
lamp merged with one standing under nothing, because the two share the `metal`
texture and a grouping cell. Grouping by the reaching lamp as well takes it to
**0 of 50** — the whole cost is one extra batch.

The key is deliberately coarse: the object's centre against each dynamic
lamp's authored radius, nearest one wins, −1 for "no lamp reaches this". It is
not the engine's brightness × live-level × falloff score and does not need to
be. Its only job is to keep "a lamp reaches this" apart from "nothing reaches
this", and since every member of a lamp's group sits inside that lamp's
radius, the merged sphere stays in its neighbourhood. When the lamps are off —
all of them, in daylight — every bag picks nothing and the members agree for
that reason instead.

Two limits worth knowing. The key uses object centres, so a mesh long enough
for a lamp to reach one end and not its centre is grouped by its centre. And
an object standing between two lamps is keyed to the nearer one; it can still
be merged with objects whose second-nearest lamp differs, which the engine's
own per-frame pick then resolves the same way it always did for a solo bag.

## Triangle strips

Every static model now ships **twice**: as the flat triangle list the build has
always produced, and as the same surface written out as **triangle strips**. The
render bag draws the strip; everything else - the collider, the shadow proxy,
the decal projector, the flashlight's receiver passes - keeps reading the list.

The reason is not the GS and it is not the DMA. On a measured Motor District
garage frame ([vu1-and-dma-cache-cost.md](vu1-and-dma-cache-cost.md)) render
submission is 40.2 ms, of which VIF1 wait is 5.8 and the rest is the EE getting
work ready: bounding boxes 4.8, per-bag preparation 4.6, packet construction
3.0, the `send_packet2` bracket 2.4 and about 8 ms of package creation and
classification. **Every one of those scales with the number of VU1 packages,
which scales with the vertex count.** An unindexed triangle list packages,
transfers and transforms a shared corner once per triangle that uses it; a
strip submits it once and the GS takes one vertex per triangle after the first
two. So the lever is fewer vertices, and it pays on the EE before it pays
anywhere else.

**Measured on the eleven baked district models: 13 176 list vertices become
9 648, a 0.732x count.** Per part it ranges from 0.417x (a ground quad grid) to
1.000x (one part that did not strip at all - see below). It is not the 0.35x a
terrain grid reaches, and the reason is visible in the numbers: these models are
flat-shaded, so each face has its OWN normals and a strip cannot cross a face
boundary. A quad is six list vertices and four strip ones - 0.75 - and most of
the district's walls, beams and posts are quads. Smooth-normal geometry does far
better.

### What it costs on the console

A strip changes nothing about the microprograms. The per-vertex ADC judgement
the cull programs already write is `fcand 0x3FFFF` over the last three `clipw`
results, which for a strip is exactly the triangle that vertex kicks - so the
whole feature is **zero VU1 instructions** and micro memory is unchanged at
1862 of 2042 words. What changes is one field of the GIF tag
(`PRIM_TRIANGLE_STRIP` instead of `PRIM_TRIANGLE`) and the order of the
vertices in the array.

Measured in PCSX2 on the district's garage-day pose, the same fixture built
twice with one knob moved (the `.tmdl` baked with strips or without; one
engine, one editor):

| per 50-frame window | list | strip |
| --- | ---: | ---: |
| VU1 packages (cull route) | 56 625 | **50 525** |
| submitted vertices per frame | 76 951 | **68 235** |
| packet flushes | 6 450 | 6 400 |
| clip-routed packages | 1 425 | 1 425 |

**11.3% of the frame's vertices and 10.8% of its VU1 packages, gone.** Less than
the models' own 26.8% because the models are about two fifths of this view's
geometry. The rest is the roads and the terrain, and **as of 1.96.0 both of
those are stripped too** - see [roads.md](roads.md), "Triangle strips", and
[terrain.md](terrain.md). They are grids with no face boundaries for a strip to
stop at, so they reach **0.355-0.374x** where these flat-shaded models reached
0.732x, and they are where the geometry actually is: 93 150 road vertices in 90
chunks against 13 176 in all eleven models. Neither uses `meshstrip` - a
ribbon's and a heightfield's rows *are* the strip, and the road half runs on the
EE at scene load where a general stripifier could not - but both keep this
page's run contract exactly.

Packet flushes barely move, and that is not a disappointment: a flush happens
per BAG (`flushBuffers` at the end of each `render`), not per package, so it
counts objects rather than vertices.

### The rules the format keeps

The strip is chopped at build time into independent **runs** of exactly
`meshstrip::kRun` = 75 vertices, and the game pins `StaPipBag::packageSize` to
that number. That is the whole trick: a VU1 package is a contiguous slice of the
bag's array, so making the runs BE the packages means no package boundary can
ever splice two unrelated vertices into one triangle, and no overlap has to be
repeated at runtime.

- 75 is the smallest package any static program class derives, so one baked
  number is legal for every pass an object can take. The game checks that
  against the engine at runtime and keeps the list if it ever stops being true
  (`stripRun <= minPackageSize()`), which is what makes a `.tmdl` baked by a
  different editor safe rather than merely lucky: a run longer than the engine
  derives falls back to the triangle list.
  **75 is also very nearly the largest number available, which is why the run
  cannot simply be made longer to cut the package count.** It was 72 until the
  rounding step in `getMaxVertCount` was relaxed from a multiple of 9 to a
  multiple of 3, which is the whole of the available slack at the shipping
  memory layout; the whole of VU1 data memory caps a six-quadword-per-vertex
  package at **81**, and only with the clipping scratch moved out of its fixed
  addresses entirely — 144 would want 1.73x that memory. The derivation, the
  sweep, and what reaching 81 would still cost are in
  [render-submission-attribution.md](render-submission-attribution.md),
  "Round three" (the bound) and "Round four" (the change).
- Every run length is a multiple of 3 - the VU1 vertex loops step by three, and
  a count that is not runs off into VU1 memory. The padding repeats the last
  vertex, which makes a degenerate triangle the GS rasterises to nothing.
- Separate strips inside one run are joined by repeating a vertex either side
  of the seam. Winding parity is NOT preserved and does not need to be, because
  nothing in this engine backface-culls.
- A part whose strips come out no smaller than its list keeps the list and is
  not marked stripped. A cube is the honest example: 36 list vertices against
  24 unique ones plus 10 of join is 34, and 34 is not worth a second copy.

### The weld key is a property of the BAG, not of the mesh

Two corners may share one strip vertex only when every attribute the GS
receives for them is identical, so **which attributes those are decides whether
a mesh strips at all**. `meshstrip::Weld` names the two answers.

`Weld::kFull` — position, normal and UV — is the answer for anything the
pipeline shades, and it is what `bakeStaticModels` uses for every ordinary
model. Its cost is that a **flat-shaded** mesh gives every face its own normals
and therefore almost no shared corners: measured on the Motor District's
imported cars, **2 242 unique corners out of 2 280**, so the strip comes out
**1.65x** the list and `build()` correctly refuses it. That refusal is right and
must not be "fixed" — welding a hard normal crease is what makes a model look
melted.

`Weld::kNoNormal` — position and UV only — is the answer for a bag the pipeline
renders **unlit**: no lighting bag, one flat colour, so the normal is never
read and is not an attribute the GS receives. The emitted vertex still carries
the normal of the corner it was welded from, so the array stays a well-formed
8-float mesh; it is simply not the array to shade. On the same three refused
wheels it strips to **0.64–0.76x**.

There is exactly one caller today, and the gate is a property of the consumer:
the **vehicle wheel** (`vehbake`, see [vehicles.md](vehicles.md), "The wheel
batch is a strip"), whose bag is a flat-grey unlit batch. The car BODY is not
stripped on this key and must not be — it is lit. **Using `kNoNormal` for a bag
that is lit is a rendering bug, not a slower render**: neighbouring faces would
take one face's normal.

### A hand-written grid strip flips the quad diagonal, and that is a picture change

Worth its own heading because it is invisible in every count.
`meshstrip` cannot bite you here — it re-triangulates a welded mesh and is
checked against the triangle multiset — but the two grid emitters that do NOT
go through it (roads and terrain, and now the projected-shadow receiver patch)
lay their own strips out by hand, and the obvious layout is wrong.

A quad has two triangulations. A triangle list writes `p00 p10 p11` +
`p00 p11 p01`, i.e. the **`p00`–`p11`** diagonal. A strip's shared edge is its
trailing pair, so walking a row as (near, far), (near, far)… splits every cell
along the **other** one, `p01`–`p10`. Emit the FAR corner of each pair first and
the list's diagonal comes back.

On a flat quad the two are the same picture, which is why this survives casual
inspection. On a quad whose four corners are at four heights — a patch that
follows the terrain, a road on a slope — they are **two different surfaces**,
and the STs interpolated across them differ with it. A strip that changes a
silhouette is not a saving.

The counts cannot see it — same vertices, same packages, same everything — so
the only check that can catch it is a picture, on a pose where the receiver is
not flat.

**And the Motor District has no such pose, which is how this nearly went the
other way.** The projected-shadow patch was found flipping its diagonal by
READING it. The round that fixed it had a candidate differing from its control
in 54 pixels, assumed those 54 were the flip, and rebuilt every arm around the
fix — after which the candidate differed from the control in **exactly the same
54 pixels, the same set, pixel for pixel**. The flip was worth zero there,
because `patchY` goes flat the moment the patch lands on geometry and in that
scene it does; the 54 were the vehicle wheels' re-triangulation all along, and a
one-knob arm is what finally said so.

Two lessons, and the second is the one that generalises:

- **A fixture can be completely silent about a defect whose fix you are
  shipping.** "The captures did not move" is not evidence that a geometry change
  is correct; it can equally mean the fixture never exercises it.
- **Attributing a difference to the thing you just changed is a guess.** The
  only thing that turns it into a measurement is an arm with one knob in it.

### A re-triangulation is not bit-exact on the GS, and cannot be made so

The check above is about a strip that draws the WRONG surface. This one is about
a strip that draws the RIGHT surface and still does not produce the same
framebuffer, which is a different thing and has to be budgeted for rather than
fixed.

A strip submits the same vertices in a different order and groups them into
different triangles. The GS derives each triangle's ST and colour gradients from
its three vertices in fixed point, and resolves an equal-`z` tie in favour of
whatever is drawn last. Both of those are functions of the triangle ORDER, so a
re-triangulated surface can land a texel coordinate on the other side of a texel
boundary, and on a **palettized** texture one texel is a whole colour index.

Measured: the Motor District's vehicle WHEELS, stripped, differ from the list in
**54 pixels of a 512x512 self-capture, worst channel step 1**, in one of two day
poses, against arms that were each byte-identical over three repeats. The pixels
are on the two side cars' tyres, and the geometry is provably the same — the
host property test expands the strip back and finds the identical 314, 28 and
139 surface triangles, none lost and none invented.

So **"byte-identical captures" is not an available acceptance gate for a change
that re-triangulates a textured surface**, and asking for one will send you
hunting a bug that is not there. What IS available, and what this repo used:
the triangle multiset on the host, the vertex and package counts, and a pixel
budget stated in advance. A grid emitter that only re-ORDERS whole triangles
(the receiver patch, once its diagonal is right) can still be exactly zero, and
was.

### What the triangle counters count

`StaPipTelemetry`'s `triangles*` fields are **GS primitives**, not surface
triangles, and for a strip those are not the same number. A strip array
deliberately carries degenerate triangles - two repeated vertices either side of
every seam where one run holds more than one strip, plus the padding at the tail
of every run but a bag's last - and `size - 2` counts all of them. Both
rasterise to nothing; both are primitives the GS is asked to set up.

So **one surface reports more triangles as a strip than as a list**. The garage
day pose reports 25 650 for the list build and 38 427 for the strip build, and
that 1.498x is a measurement rather than a miscount: the same frame submits
11.3% fewer *vertices* (76 951 -> 68 235) and asks for about half as many more
primitives, nearly all of them zero-area. The saving is on the EE, which pays
per package; the primitives are what it costs on the GS.

The consequence is the one that costs a check: **`trianglesCull` cannot show
that two arms draw the same geometry** when the arms differ in representation.
The runtime cannot recover the surface count either - the degenerate total
depends on how the bake packed the runs, and nothing in a package records it.
Use `verticesSubmitted` for the EE bill, and the **producer's own build-time
surface count** for equality: `ROADSTRIP scene N ... triangles T` and
`TERRAINSTRIP scene N ... triangles T` in `bin/log.txt` are computed with
degenerates dropped and must be identical in both arms. Within one
representation the counters compare as they always did.

Two outright miscounts live in `StaPipCore` and are **not** fixed yet:
`recordGuardBandPackage` charges `package.size / 3` with no strip branch, so
the `guard=` half of `FTCLIP` is computed on a different rule from the `cull=`
it is documented as a subset of (25 against 73 for a 75-vertex run);
and `recordOutsideBag` charges a whole bag `count - 2` when the bag is sliced
into `ceil(count / maxVertCount)` runs that are each their own strip, so it
over-counts by `2 * (packages - 1)`.

### Clipping

`clip_*` and the EE clipper both loop by whole triangles over a triangle LIST,
so a stripped package that genuinely crosses a VU1 clip plane is **expanded back
into a list on the EE** and clipped exactly as before. The expansion goes into
the qbuffer copy pool - the one whose double-buffered lifetime the DMA already
relies on - in chunks carrying the same triangle budget a list subpackage does,
and the buffer it produces emits `PRIM_TRIANGLE`, so one bag can mix the two.

It is rarer than it sounds, because the guard band sends most screen-straddling
packages down the cull path whole ([vu1-clipping.md](vu1-clipping.md)). At the
garage-day pose **not one** stripped package needed it; from inside a building,
where walls cross the near plane in every direction, 29.5 per frame did. The
cost is a 3x vertex expansion for exactly those packages - which is what the
list path would have sent for the same triangles anyway. The strip is a saving
on the packages that need no cutting, and that is nearly all of them.

### What the picture does

A geometry change cannot be argued correct from the instruction stream, so it
was compared pixel for pixel: PCSX2 software renderer, frozen camera, the
game's own `--capture-frame` (the GS raster, not a window grab). **Three
captures of one arm are byte-identical**, which is what makes the comparison
mean anything.

| pose | pixels differing | of those, by more than 8/255 | mean abs difference |
| --- | ---: | ---: | ---: |
| garage day (no clipping) | 1 756 of 200 704 (0.875%) | 9 (0.004%) | 0.015/255 |
| inside a tower, high (1 625 expansions) | 1 992 (0.993%) | 953 (0.475%) | 0.235/255 |
| inside a tower, floor in view | 12 860 (6.407%) | 12 493 | 3.243/255 |

The first two are what a topology change looks like: isolated pixels along
silhouettes and window frames, where a shared edge is claimed by a different
triangle and Gouraud interpolation from a different corner rounds differently.
Nothing is missing and nothing is corrupt.

**The third row is a pre-existing artefact being MOVED, not a new one**, and it
is worth knowing the shape of. At that pose the building's floor is very nearly
coplanar with the ground, and both arms draw a dithered hatch along the
crossover - the double-surface fight described under `StaPipBag::packageSize`.
The strip changes which VU1 route some of those triangles take (cull with the
divide on VU1, or clip), the last bits of z move with it, and the crossover
line lands ~15 pixels further along. Read it as: this change does not create
z-fighting, and it will re-shuffle any that a scene already has.

## What this means for your project

- **You keep working with `.obj`.** Import it, replace it, re-export from
  Blender over the top of it - the build re-compiles it on every build.
- **The `.obj` no longer ships.** Only the `.tmdl` goes into `bin/` and into
  an exported ISO, so the disc never carries both copies. The `.mtl` still
  ships (a material library can also be assigned to primitives).
- **The binary is bigger than the text**, usually around 1.7x: an `.obj`
  shares vertices between faces through indices, while the game needs a flat
  triangle list with a normal per corner - which is what it always built in
  RAM anyway. Memory use is unchanged; you trade disc space for load time.
- **Nothing to configure.** There is no switch: a static model referenced by
  a scene object is compiled.
- **An `.obj` carries no unit**, so the importer asks for one (the **Model
  size** dialog, and the **Size...** button next to the model in the Assets
  list afterwards). It records how many meters one unit of the file measures;
  combined with the project's world scale that is the scale objects made from
  the model are inserted at. The file itself is never rewritten - see
  docs/world-scale.md.
- A model that cannot be parsed is reported in the build log
  (`[model bake] ...`) and simply renders nothing, exactly like a missing
  animated model. The Asset Browser flags the same problem earlier.

## Mesh LOD: fewer triangles far away

**Project > Preferences > Rendering > Mesh LOD distance** turns distance
levels on for the whole project (`off` = no levels at all). An object farther
from the camera than the distance renders a reduced mesh, and past twice the
distance a further reduced one. This used to apply only to animated models,
because the `.tskl` had somewhere to put the reduced meshes and an `.obj` did
not; static models get it now too.

The saving is real because vertex count is what static geometry costs the
PS2: every vertex is transformed, classified against the frustum, clipped if
it crosses the screen edge, and pushed through VU1. A test scene with 12
copies of a 9 216-vertex model went from a hard 25 FPS to a steady 50 with
levels on (27.1 ms of scene time down to 11.5 ms).

**Per object:** any static model object can override the project value in its
Properties - **Override mesh LOD**, next to the material summary. Unchecked,
the project preference applies; checked, that object uses its own distance,
and dragging the value to `0` turns levels off for it (the hero prop next to
a crowd of scenery that always decimates). An override above zero also makes
the build produce the levels for that model even when the project preference
is `off`.

**Cost:** each level is more data in the `.tmdl` (the test model grew from
295 KB to 497 KB with two levels) and more RAM per object that actually gets
far enough away to use it. Levels are shaded and kept per object the first
time they are needed, so an object that never leaves the near band pays
nothing. This is why `off` bakes nothing at all.

**Tuning:** pick a distance at which the model is already small on screen.
If you can see the switch from a normal gameplay camera, the distance is too
short. The editor viewport always shows the full mesh.

## Your own LOD meshes

Automatic decimation is a quadric-error collapse: it protects UV seams and
silhouette borders, and it refuses to touch meshes too small to gain
anything - which also means some models barely shrink. When you want control,
model the levels yourself.

In the **Asset Browser** (*Tools > Asset Browser*), a selected model has a
**LOD...** button in the inspector:

- **Level 1** shows past the mesh LOD distance, **Level 2** past twice it.
- Pick any other `.obj` in the project (the list shows each candidate's
  triangle count), or leave a level on **(auto - decimate)**.
- Clearing a level also clears the coarser one - a chain with a hole in it
  has no meaning.
- A model with hand-authored levels shows `[N custom LOD]` next to it.

Requirements the build checks, per level:

1. **The same materials** - the same `usemtl` names in the same order as the
   full mesh. The levels share the model's materials and textures, so the
   material list has to line up.
2. **Fewer vertices** than the level before it.

If a level fails either check, the build logs why and **decimates that model
automatically instead** - the whole custom chain is dropped rather than
shipping something half-broken. Watch the build output after assigning
levels:

```
[model bake] res/models/tree.obj: custom LOD res/models/tree_lod1.obj has a
different material set than the model (same usemtl names in the same order
are required) - decimating instead
```

The level files live in `res/models` like any other model. They do not ship
separately - their geometry is folded into the model's `.tmdl` - and they do
not need their own scene objects. A practical setup is `tree.obj`,
`tree_lod1.obj`, `tree_lod2.obj` exported from the same source with the
material names kept intact.

## The shadow proxy

Distinct from the LOD levels and baked whether or not mesh LOD is on: with
**Flashlight shadow volumes** enabled, a model past 1200 triangles gets one
extra, positions-only mesh under that budget, from which the torch extrudes
its shadow volume (a per-frame EE classification of every triangle is what
the budget protects). It is welded across all parts by position and decimated
with open borders unlocked, so it keeps the outline rather than the surface -
which is all a shadow needs. About 40 KB per model; the game tries the real
mesh first and reads the proxy only when the real one is over budget. A model
the decimator cannot bring under budget prints
`[model bake] <model>: N triangles and the shadow proxy could not be decimated
under 1200` and casts its bounding sub-boxes, as every big model used to.

## What LOD never touches

- **Collision.** The collider and the model's bounding box always come from
  the full mesh, so what you walk into never changes with camera distance.
- **Physics bodies in flight** and objects with a **camera/mirror texture
  feed** keep the full mesh - both depend on the exact vertex buffers the
  game builds for them.
- **Mirrors, portals, camera feeds and reflection probes** re-use whichever
  level the main camera picked for that frame (the same approximation
  animated models make).

## Troubleshooting

**A model disappeared after a build.** Check the build log for a
`[model bake]` line: the `.obj` failed to parse, so no `.tmdl` was written.
The Asset Browser shows the same models it can read.

**A level never seems to show.** Distances are measured from the camera to
the object's center, in world units - the same units as object positions. A
level also needs to have been produced: with the project preference `off`,
only objects whose own override is above zero get levels.

**The decimated mesh looks too rough.** Push the distance out, or author the
level yourself (above). Small parts of a model may keep their full detail:
the decimator skips meshes that are already small, and levels are decimated
per material, so material borders never move.

**An exported ISO still contains the .obj.** `bin/` accumulates - the build
deletes a superseded `.obj` from it, but if you see stale files, a
*Project > Clean* followed by a build gives you an exact `bin/`.
