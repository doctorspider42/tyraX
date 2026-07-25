# Streaming layers

Layers group scene objects so the game can **unload them from memory and
stream them back in at runtime** — the way GTA3 kept only the part of the
world around you in the PS2's 32 MB of RAM. Walk into a corridor, drop the
building behind you, start pulling in the next one; by the time you reach
the far door it is already there.

Layers also double as an editor convenience: each layer has an eye toggle
that hides its objects while you edit a crowded scene.

A ready-to-run demonstration lives in
[examples/layer-streaming](../examples/layer-streaming) — two buildings
connected by a corridor, with trigger markers that swap the buildings as
you walk through.

## Creating layers

Open the **Layers** section in the Project panel (it starts collapsed until
the scene has layers):

- **+ Layer** adds a layer. Names are per scene and must be unique.
- The **eye checkbox** (leftmost) shows/hides the layer's objects in the
  editor only — the viewport skips them for rendering *and* clicking, the
  object list dims them with a `[hidden]` tag. This never affects the game.
- **start** — the layer is resident (in memory) when the scene starts. Off
  = the layer waits for a *Set Layer Loaded* flow node (its **load** pin).
- The number on the right is how many objects the layer currently holds.
- **x** deletes the layer. Its objects stay in the scene and just become
  unassigned (always resident).
- Rename by typing in the name field — object assignments and flow-graph
  references follow the rename automatically.

Assign an object to a layer with the **Layer** combo in the Properties
window (`<none>` = no layer). Objects without a layer are always resident:
terrain, sky, HUD, music and sounds are never streamed either.

Layers are per scene. Every scene in the project has its own layer list.

## Loading and unloading at runtime

Three flow-graph nodes (category *Scene*):

- **Set Layer Loaded**, **load** pin — starts streaming the layer in. Missing assets (models,
  materials, their textures) are loaded **one per frame**, then the layer's
  objects activate a few per frame. Nothing stalls: the cost is spread over
  the walk toward the area. Request it early (a corridor, an elevator, a
  bend in the road) and the pop-in stays out of sight. One asset is still one
  frame, so a heavy asset is one long frame — this is why static models ship
  as a binary the console reads instead of parsing
  ([model-pipeline.md](model-pipeline.md)); it cut a 9 216-vertex model's load
  from 306 ms to 59 ms.
- **Set Layer Loaded**, **unload** pin — the layer's objects drop out of the game the same
  frame (rendering, player collision, USE prompts, sound emitters, object
  physics and particles all stop), and every asset that no other resident
  layer uses is freed — including its share of texture memory. Assets
  shared with still-resident layers are reference-counted and stay.
- **Is Layer Loaded** — a pure boolean output: true once the layer is fully
  resident (all assets in, all objects active). Wire it through *On
  Condition* to fire an exec pulse the moment a load completes, or into any
  logic gate.

The classic corridor setup uses four invisible **Empty** markers with
*Near Object* triggers (radius tuned to the corridor width), placed in
walking order:

```
[building A] .. unload-B .. load-B ...... load-A .. unload-A .. [building B]
```

Walking A→B: `unload-B` fires first (a no-op, B is not loaded), then
`load-B` starts the stream, and by `unload-A` you are far enough that A can
vanish behind you. Walking back B→A works the same way mirrored. Requests
are idempotent — loading an already-loaded layer or unloading an unloaded
one does nothing, so bidirectional traffic needs no extra logic.

## Auto-streaming zones (no flow graph at all)

Tick **auto-stream** on a layer and the game streams it by proximity instead
of by node: the layer loads while a player is inside its zone and unloads once
they leave it (plus a hysteresis band, so pacing along the border doesn't
thrash). Requests are edge-triggered — issued only when a player crosses the
boundary — so *Set Layer Loaded* can still override a zone until the next
crossing. With auto-stream on, the initial residency comes from where the
player spawns, not from the *start* flag (which greys out).

The zone has two shapes:

- **A circle** — center X/Z + radius, typed into the row (*Center on sel.*
  drops the center on the selected object). Infinite in Y: it covers the whole
  column of space above and below.
- **An Area object** — pick one in the combo next to the checkbox and the
  zone becomes that area's box. Being a box, it **bounds height too**, so one
  floor of a building can stream on its own; and because the area is read
  live, a flow node that moves the area moves the zone with it. See
  [areas.md](areas.md).

Both are tested against the player's position (and player 2's while active: a
zone loads when EITHER player enters and unloads only once both have left).

## What happens to object state

Unloading a layer **discards its runtime state**. When the layer comes
back, its objects reset to what was authored in the editor — position,
color, visibility, animation defaults — exactly like a scene reload (and
exactly like GTA3 interiors resetting when you re-enter). If something must
survive a round trip, keep it in save values or flow variables and restore
it from an `Is Layer Loaded` edge.

Details worth knowing:

- **Scene starts** honor each layer's *start* flag; a scene switch or a
  loaded save resets layers to those defaults.
- **Point lights** are baked into static vertex colors at build/rebuild
  time, so a light inside an unloaded layer keeps tinting neighbouring
  geometry — the same behavior as hiding a light with *Set Object Visible*.
- **Flow nodes targeting objects in an unloaded layer** are safe:
  Show/Hide/Move write to the object's dormant state, which is discarded on
  the next activation.
- **Scene switching** now also evicts assets the new scene's resident
  layers don't use, so multi-scene projects no longer pay for every scene
  at once.

## Memory notes

- The debug build profile's **Show memory usage** overlay (Project >
  Preferences > Build) is the easiest way to watch layers work: MEM drops
  when a layer unloads and climbs while one streams in.
- What a layer frees: per-object vertex/color/UV buffers and draw bags,
  model geometry and collision meshes, animated-model data, and textures
  (via reference counting — a PNG used by two layers is freed only when
  both are out).
- What stays: the compiled scene tables (a few hundred bytes per object in
  the ELF's read-only data) and anything outside the layer.

## Troubleshooting

- **The layer never shows up** — check its *start* flag or make sure some
  trigger actually fires the *load* pin; watch `Is Layer Loaded` with a *Log*
  node. Unknown layer names in flow nodes compile to a comment (check
  `src/gen/flow_graph.gen.cpp` if in doubt).
- **Objects pop in too close** — move the *load* trigger earlier
  along the approach; the stream needs roughly one frame per asset plus a
  few frames of activation.
- **The player falls through where a building used to be** — that is what
  unloading means: collision goes with the layer. Keep floors/terrain
  outside layers, or unload only once the player has left the area.
- **A texture disappeared from an object that is still loaded** — both
  objects must be in *resident* layers; an object with no layer never loses
  its assets.
