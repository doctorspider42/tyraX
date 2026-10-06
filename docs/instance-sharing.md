# Sharing model geometry between instances

Instance sharing lets every placed copy of an imported model draw from one
model-space copy of that model's mesh, held once in EE RAM, instead of each
copy keeping a world-space copy of its own. It is on by default
(*Project Preferences > Rendering > Share model geometry between instances*).
This page explains what it saves, what it costs, which objects stay out of it
and why, and how it fits with [static batching](static-batching.md).

## Why

A generated city runs out of the PS2's 32 MB of EE RAM long before frame time
becomes a problem ([examples/big-city](../examples/big-city/README.md), "Limits
found"). Before 1.173 every static model instance baked its own copy of the
mesh on first render: a world-space position, a lit colour and a texture
coordinate for every vertex, 48 bytes a vertex. Instances of one model shared
nothing, and [static batching](static-batching.md) merged the members into a
second world-space copy.

## How it works

- **One model-space bake per part.** The first shared instance of a model part
  builds its positions (w = 1) and texture coordinates from the loaded
  `.tmdl`, using the same triangle-strip-or-list choice a solo bake makes. The
  model owns it (`GameModel::sharedParts`), and every instance part holds a
  reference to it. LOD tiers are shared the same way, built the first time an
  instance needs one.
- **The transform rides in the matrix.** A shared instance draws under its own
  `objMat`: position, rotation **and scale** (a solo bake and the physics
  matrix path bake the scale into the vertices instead). VU1 applies it. The
  engine already did the rest: the frustum-box cache is keyed by vertex
  pointer and version, so all instances of a part share one set of
  model-space package boxes, and each bag classifies them against planes moved
  into its own object space.
- **Only the colours are per instance.** Lighting depends on the world
  position and normal (the sun's N.L, GI probes, baked point lights, emissive
  pools), so each instance still lights its vertices, through the same
  `shadeVertexColor` the solo bake uses, transforming each model-space vertex
  and normal on the fly without storing the result. The colours come out
  byte-identical to the solo bake's.
- **Equal colour arrays are one array.** Colours are pooled by content (a hash
  plus a byte compare). In a city of a few rotations, most instances of a
  model light identically: at the downtown pose of the shipped (streamed)
  big-city, 458 shared objects with 667 parts draw 351 distinct colour arrays
  (135 KB); the 1 km version with batching off, 1 656 parts and 897 arrays.
  Prelit models, whose colours are the object tint alone, share one array per
  tint.

## What it saves, and what it costs

All numbers are PCSX2 (2026-10-03), debug build. `MEM` and `SCENE` are the
HUD's, read off a `--capture-frame` 60 s after boot; the object geometry is
the game's `MEMSTAT` line (below). Profile Totals are `--profile-frame`, three
captures, and are attribution (the profile drains the pipeline). PCSX2
emulates no EE data cache, so read every millisecond here as a direction, not
as a console number.

### The streamed city (where it matters most)

The shipped `examples/big-city` is a 1.4 km city of 51 auto-streamed district
layers (2 420 objects). Layered objects are never statically batched, so
before 1.173 every resident instance carried its own world-space bake.
Sharing on against sharing off, same build otherwise:

| pose | HUD MEM off -> on | HUD SCENE off -> on | MEMSTAT object geometry, on |
|---|---:|---:|---|
| downtown walker (-42, -100) | 19.9 -> **18.4 MB** | 6.76 -> 6.84 ms | 458 shared objects / 667 parts: 23 KB meshes + 135 KB colours (351 arrays); 151 solo objects 522 KB; was 609 solo objects 1 875 KB |
| car at spawn | 19.6 -> **18.2 MB** | 5.01 -> 5.07 ms | |
| driving south (`--pad "hold r2; wait 30"`), +12 s | 20.7 -> **19.1 MB** | 8.32 -> 7.82 ms | 42.0 FPS on, 41.9 off |
| driving, +22 s | 18.0 -> **17.0 MB** | 4.13 -> 4.42 ms | |

About 1.5 MB at every pose, a frame time within the emulator's noise, and the
downtown picture differs from sharing off in ONE pixel (the transform now runs
on VU1). Layers loaded and unloaded around the car on both runs (`LAYER n
load|unload`, 15 events in 30 s); a streamed-out instance drops its references
and the colour pool is swept right after the unload, and an instance that
streams back in rebuilds against the (re-)loaded model's bake.

The profile's `Loop_coarse_cull_included` is the line that caught the one cost
sharing added: testing each shared object's coarse box in its own object space
moved six planes per object per frame, 0.90 ms against 0.41 ms solo over the
downtown pose's 370 in-range objects. The box is kept in world space now (the
model box's corners through `objMat`, boxed again at rebuild): 0.40 ms.

### The 1 km city, batched

Before streaming, `examples/big-city` was a 1 km city of 1 283 objects with
everything resident and 1 117 of them in 149 static batches. These rows are
from that version, at the frozen walker (-42, -100), and predate the
world-space coarse box above (so the two batching-off rows overstate the
frame cost of sharing):

| arm | HUD MEM | object geometry (MEMSTAT) | HUD SCENE | profile Total |
|---|---:|---|---:|---:|
| 1.172, batching on | 29.1 MB | (no MEMSTAT in 1.172) | 11.95 ms | - |
| 1.173, batching on, sharing off | 27.5 MB | batches 3 244 KB, 179 solo parts 802 KB | 11.93 ms | 16.6-19.8 ms |
| **1.173, batching on, sharing on (default)** | **27.3 MB** | batches 3 244 KB, 154 solo parts 609 KB, 14 shared objects | 11.82 ms | 17.5-17.8 ms |
| 1.173, batching off, sharing off | 28.5 MB | 1 810 solo parts 4 004 KB | 12.69 ms | 22.3-23.2 ms |
| 1.173, batching off, sharing on | **25.3 MB** | 1 656 shared parts: geometry 36 KB, colours 354 KB (897 arrays) | 13.40 ms | 22.6-25.0 ms |

Pictures: the default arm is pixel-identical to 1.172 below the HUD, and the
two batching-off arms differ from each other in 2 pixels (by at most 7 levels
of one channel - the transform now happens on VU1 instead of the EE). Batching
on and off differ from each other, by about 1 400 pixels, exactly as they did
before this change (a batch is culled as a unit).

Read it in three steps:

- **Most of the 1.172 city's excess was growth slack**, not geometry. A batch
  grew by `push_back` - up to twice what it holds - and kept the capacity for
  the whole scene. Batches now trim it (`BagArray::shrink_to_fit`) and a solo
  model bake reserves its exact size: 1.6 MB of HUD MEM on this city, with no
  other change, no frame-time cost and an identical picture.
- **With batching on, sharing changes little here**, because 1 117 of the
  city's 1 283 objects are in batches and a batch member never draws its own
  bake. Sharing helps whatever does not batch: models too big for their
  batching cell, objects in a streaming layer, spawned clones, single
  instances.
- **Batching off plus sharing is the smallest arm** (2.0 MB under the default,
  3.2 MB under batching off without sharing) **and the slowest one**: every
  instance is its own submit again, and each bag does its own transform,
  object-space planes and light setup (SCENE 13.40 against 12.69 ms - most
  of that gap was the coarse-box planes, since moved to world space; on the
  streamed city the same comparison is now 6.84 against 6.76 ms).

**The defaults: sharing on, static batching unchanged (on).** On the measured
1 km scene batching is worth 1.6 ms of HUD SCENE (about 5 ms of profile
attribution); sharing costs nothing where batching applies and saves memory
everywhere else - most of all in a streamed city, where nothing batches. A
scene that is out of EE RAM has a new lever: turning batching off now *saves*
memory instead of costing it, at the frame-time price above.

**The headroom that buys.** The 1 km city regenerated with street furniture
on every road (`--set FURNITURE_CORE_ONLY=False`: 1 057 trees, 570 lamps, 118
parked cars, 2 557 objects) runs out of EE RAM at scene load with batching on,
in 1.172 and 1.173 alike (a black screen after `ROADINDEX`). With batching off
and sharing on it boots and runs: HUD MEM 30.0 MB, SCENE 14.18 ms at the same
pose (11.82 for the batched 1 283-object city), the same 30 FPS the debug
build shows there. MEMSTAT: object tables 2.7 MB, instance parts and their bags 2.8 MB,
shared colours 1.1 MB (3 083 arrays for 4 305 parts), shared geometry 36 KB.

That breakdown also says where the next megabyte is: **the per-instance
bookkeeping**, not the geometry. A `GeoPart` and its four bags cost about
650 bytes whatever the part draws - most of it room for passes a static prop
never uses - and each object carries about a kilobyte of runtime and geometry
state (docs/backlog.md).

### The other examples

A/B against the 1.172 editor, same pose, PCSX2:

| example | HUD MEM | picture |
|---|---:|---|
| vehicle-playground (Motor District, batched + 38 shared objects) | 24.0 -> 23.0 MB | identical below the HUD; SCENE 7.04 -> 7.09 ms |
| showcase (prelit models, portals, mirrors) | 22.9 -> 21.3 MB | same (window shots; animated water) |
| night-walk (prelit, lamps) | - | identical |
| impostor-grove (impostors stay solo) | - | identical |
| probe-lighting (GI probes) | - | differs only on the animated wobblers (their animation phase) |
| raytraced-mirror | - | same (window shots; spinning props) |

## Who stays solo

An object keeps the classic world-space bake when something reads its
vertices in world space, moves them, or depends on per-instance vertex data
(`instanceShareEligible`):

| object | why |
|---|---|
| physics bodies, pickables, the matrix path's own users (vehicles, scrollers) | they move; the physics fast path has its own local bake |
| usable objects | the USE highlight's hull and ground apron walk the vertices in world space |
| dynamic lighting | its own lit bag and per-part normals |
| a reflective part (`refl`) | the env pass's per-vertex normals and paint |
| texture feeds | colours and STs rewritten per object |
| impostors | the representation swaps per frame |
| a project VU1 program, or a VU script that moves geometry | it sees the vertices before the transform |
| zero or mirrored scale | nothing to gain; a negative determinant is one more thing for a matrix to get right |
| **non-uniform scale where a dynamic light can reach** | see below |

**Non-uniform scale and dynamic lights.** The engine lights a bag in object
space, with ONE light range per mesh (`buildSpotForBag`): exact under a
uniform scale, an ellipse under a stretched one. So a non-uniformly scaled
instance is shared only when the scene's player has no flashlight and no
dynamic lamp's radius reaches it (the authored positions; the same rule
batching uses to key on lamps). A flow node that switches the torch on in a
scene whose player had it off is the case this does not cover. The engine's
light PICK radius uses the longest basis column now, so a stretched bag is
never under-sized for the pick.

## Passes that need world space

Three passes copy an object's triangles in world space and draw them coplanar
with the base pass at its exact depth: the flashlight's receiver pools (two
variants) and the projected shadow's wall patch. A shared instance cannot
promise that depth (VU1 computes it from model space through the matrix), so
those passes call `unshareObject`, which rebuilds the object as a solo bake
for good (`ObjectGeometry::noShare`). Only objects the torch or a shadow
actually lands on pay it.

Everything else works from the shared bake, through the bag:

- the coarse whole-object box is the world-space box of the shared parts'
  model boxes through `objMat`, built at rebuild, so `coarseObjectOutside`
  tests it against the world planes like a solo bake's (StaPip itself still
  classifies the bag's model-space package boxes through the matrix);
- the projected-shadow silhouette reads the floor height through the matrix,
  and when it has to clamp a buried part it does so on a world-space copy for
  that one submit;
- mirrors compose `mirrorMat * objMat`; portals' exit clipping already
  transformed by the bag's matrix; the reflection probe, camera feeds and the
  reflection proxy box draw the bag as it is;
- the outline shell (a VU-script shell pass) builds its proxy by transforming
  the shared bake through `objMat`.

## Traps worth knowing

- **Do not use `std::shared_ptr` in the generated game.** This toolchain builds
  libstdc++'s shared_ptr with the mutex lock policy, so every control block
  runs `pthread_mutex_init` - one EE kernel semaphore each. The first version
  of this feature used it; a city's few hundred shared parts exhausted the
  kernel's semaphores during the first frame, and from then on every `host:`
  open failed: no `bin/log.txt` lines, no Live Debugger (`--capture-frame`
  timed out), magenta checkerboards where the HUD's lazily loaded textures
  should be, while the game kept drawing. It looked like a hang. The shared
  arrays use a plain single-threaded counter (`ShRef`). Found with
  `pcsx2-capture.py run --states 3` and an `addr2line` of the EE PC in each
  savestate: the main thread was rendering normally.
- **The engine's command caches are keyed by the vertex array.** Both the
  retained command blocks and the baked VIF streams took one entry per
  (vertex array, package size), so hundreds of bags drawing one array with
  different colour arrays fought over one entry and rebuilt it on every
  submit. An entry's identity now includes the stream set (colours, STs,
  normals). Instances whose colours pooled to one array share one entry, which
  is the point.

## Checking it: MEMSTAT

A debug build with the HUD's MEM line on writes a `MEMSTAT` line into
`bin/log.txt` 60, 300, 900 and 1500 frames after a scene load:

```
MEMSTAT used 25896 KB | object tables 1361 KB | parts+bags 1119 KB | solo 152 objects 154 parts 6306 verts 609 KB | shared 1131 objects 1656 parts, geometry 36 KB, colours 897 arrays 354 KB | batches 0 0 verts 0 KB | model sources 461 KB | engine baked 1807 KB retained 3 KB
```

`used` is the same reading as the HUD's MEM, at 1 KB resolution - but taken at
a different moment, and in every arm measured here it sat 0.3-0.6 MB above the
HUD figure read off a capture; compare MEMSTAT with MEMSTAT and HUD with HUD.
Every other field is the capacity of what it names, slack included. `engine baked` is the baked VIF
stream cache, which holds a copy of every baked bag's vertex payload and is
capped at 4 MB.

## Not verified

- A physical PS2: everything above is PCSX2. The frame-time column in
  particular wants a console pass before anyone tunes the default by it.
- Streaming: one 30 s drive per arm (15 layer loads and unloads), MEM and
  pixels checked at four points; not a long session, and not the Live Link
  edit-rebuild-unshare cycles on a shared object.
- `examples/showcase` logs `Vif1: Unknown VifCmd` and stops answering the
  Live Debugger in PCSX2 - with the 1.172 editor and engine too, so it is not
  this change; its A/B was a window screenshot (same picture, MEM 22.9 ->
  21.3 MB).
