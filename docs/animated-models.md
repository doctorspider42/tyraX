# Animated models (.glb)

The editor plays skeletal animations authored in Blender (or any glTF
exporter) on the PS2. The console never sees a skeleton: at build time every
animation clip is sampled and *baked* into MD2-style morph frames (one vertex
snapshot per sampled frame), and the game renders them through the engine's
dynamic pipeline, which interpolates between two frames on VU1. You get
smooth, named, scriptable clips; the PS2 gets flat vertex arrays it is
extremely good at.

```
Blender (rig + actions)
   │  File > Export > glTF 2.0, format "glTF Binary" (.glb)
   ▼
res/models/character.glb          ← the imported source asset
   │  every build: clips sampled at 12 fps, CPU-skinned
   ▼
res/models/character.tanm (+ extracted PNG textures)
   │  loaded by the engine's TanmLoader
   ▼
DynamicMesh + DynPip (VU1 lerps between baked frames)
```

A true skeletal runtime (bone tracks, clip blending, far less RAM) is the
planned stage 2 - see the backlog in `PROGRESS.md`. Everything described
here - the .glb import, clip names, flow-graph nodes and script API - stays
the same when that lands; only the runtime backend swaps.

## Authoring in Blender

Export with **File > Export > glTF 2.0**, format **glTF Binary (.glb)** -
one self-contained file with geometry, materials, textures and animations.

Supported (Blender's default export fits):

- skinned meshes - up to **4 bone influences per vertex** (Blender's
  exporter enforces this; extra weights are pruned/renormalized)
- rigid node animation (objects moved/rotated/scaled by keyframes, no
  armature needed - fans, doors, pistons)
- multiple named clips: each **action** exported as its own animation
  (tick "Group by NLA Track" / use the NLA editor, or export actions -
  the animation *name* in the file becomes the clip name in the editor)
- materials: base color factor + base color texture (embedded; PNG kept
  as-is, JPEG transcoded to PNG automatically)
- LINEAR and STEP keyframe interpolation (CUBICSPLINE degrades to linear
  through its keyframe values)

Not supported (imports with a warning, feature is skipped):

- morph targets / shape keys (`weights` channels)
- texture transforms, non-embedded (external) images
- sparse accessors, Draco compression, .gltf text form

Practical guidance:

- **Textures must be power-of-two** (PS2 requirement) and ideally ≤ 256 px.
  The importer warns about non-POT textures; the game cannot load them.
- **Keep meshes lean.** Baking multiplies vertices by frames - see the
  memory budget below. A few hundred to ~2k triangles per character is the
  era-correct ballpark.
- **Loops:** make the last keyframe equal the first. Looping playback
  interpolates last frame → first frame, so an exact match loops seamlessly.
- Apply your transforms sensibly: the baked vertices are in the glTF scene's
  world space; the object's position/rotation/scale in the editor transform
  the whole model on top of that.

## Importing and placing

1. **Project > Assets > Import model...** and pick the `.glb` (the file
   dialog also accepts `.obj` for static models). The file is copied to
   `res/models/`; a validation bake runs immediately and the status bar
   reports clips, vertex count, baked frames and any warnings.
2. The Assets list shows the model as `animated: N clip(s), M verts`;
   hover for the clip names and warnings.
3. **Add object > Model >** *your file* `(animated)`, or pick the file in
   an existing model object's **Model** combo.

The viewport plays the object's start clip immediately - the preview uses
the exact interpolation math the PS2 runs, so what you see is what ships.

### Object properties (Properties panel)

| Field | Meaning |
|---|---|
| **Start clip** | Clip playing at scene start (`(first)` = the file's first clip). |
| **Autoplay at scene start** | Off = the model holds the clip's first frame until a script/flow node starts it. |
| **Loop** | On = wraps forever; off = plays once and freezes on the last frame. |
| **Speed** | Playback multiplier (1.00x = authored speed). |
| **Color** | Multiplies the model's material colors (tint), like on primitives. |
| **Collision** | Box from the baked frame-0 AABB, or none. Per-triangle mesh collision is a static-model (.obj) feature. |

Material (.mtl) overrides do not apply to .glb models - their materials come
from the file itself.

## Memory budget

Baked frames are the trade-off of stage 1. Every sampled frame stores
position + normal (+ UV when textured) per vertex, ~48 bytes per vertex per
frame on the PS2. Rough rule:

```
bytes ≈ vertices × baked frames × 48        (frames = clip seconds × 12)
```

A 1 000-vertex character with 3 s of animation ≈ 1.7 MB of the console's
32 MB. The importer warns above ~8 MB per model. If you hit it: fewer/shorter
clips, leaner mesh, or wait for stage 2.

Vertices here are *expanded triangle-list* vertices (the PS2 pipelines have
no index buffers), so the editor's reported count is higher than Blender's.

## Triggering from the Flow Graph

Three nodes (Add node > **Animation** / **Triggers**):

- **Play Animation** (action) - starts a clip on the target object.
  - *Clip* (text): the clip name; empty = the model's first clip. Unknown
    names are ignored at runtime (a TYRA_WARN lands in the game log).
  - *Loop*: 1 = loop, 0 = play once.
  - *Speed*: playback multiplier; 0 = authored default (1.0).
  - Target: an incoming **object link**, otherwise **self** (the node's
    text field holds the clip, not an object name - wire the target in).
- **Stop Animation** (action) - freezes the target on its current pose.
  Play Animation resumes/restarts it.
- **On Animation Finished** (trigger) - fires the frame the watched
  object's clip reaches its last frame: **once** for a non-looping clip,
  **every wrap** for a looping one. Also usable as a bool source for the
  logic gates. Watched object: name parameter, object link, or self.

Typical patterns:

```
On Used ──▶ Play Animation ("open", loop 0)      a door that opens on USE
On Animation Finished ──▶ Hide Object            despawn after a death clip
Near Object ──▶ Play Animation ("wave")          greet the approaching player
```

## Triggering from scripts

`inc/scripts/script.hpp` (regenerated with the API while its ownership
marker is intact) provides:

```cpp
// find the object index once, then:
playAnimation(ctx, objectIndex, "attack", /*loop=*/false, /*speed=*/1.5F);
stopAnimation(ctx, objectIndex);

if (animationFinished(ctx, objectIndex)) {
  // fires the frame the clip ends (or wraps, for looping clips)
}
```

Lower level, the same state lives on `RuntimeObject`: `animClip` (index -
resolve names with `ctx.resolveClip(objectIndex, "name")`), `animPlaying`,
`animLoop`, `animSpeed`, `animRestart` (set true to apply a clip change),
`animFinished` (read-only, one frame). All of it is a no-op on objects that
are not animated models.

## What the build produces

On every build the editor re-bakes each referenced `.glb`:

- `res/models/<name>.tanm` - the baked binary (header + clip table +
  per-material frames; layout documented in `src/glbparser.cpp`, loader in
  `vendor/tyra/engine/.../tanm_loader`).
- `res/models/<name>_<image>.png` - textures extracted from the file.
  They flow through the texture-quantization bake (`.res-baked/`) like any
  other PNG, following the project-wide quality setting.

Sources are never modified; the bake is deterministic, so `.tanm` files need
no special handling in version control (they regenerate on build).

## Limits and troubleshooting

| Symptom | Cause / fix |
|---|---|
| `unusable: ...` in Assets | The .glb is malformed or empty - re-export; the error names the reason. |
| Model invisible in-game, `TanmLoader: cannot read` in log | The `.tanm` was not baked (check the build log's `[anim bake]` lines for the reason). |
| Texture renders as flat color, `Model texture missing` warning | Extracted PNG missing next to the `.tanm`, or non-POT texture (see the import warning). |
| Clip does not switch | Clip name typo - names are case-sensitive; check the Assets tooltip for the exact list. |
| Animation too fast/slow on NTSC vs PAL | It isn't - playback is wall-clock normalized. Compare against a stopwatch, not frames. |
| Editor warns about memory | vertices × frames grew too big - shorten clips, reduce the mesh, or split the model. |
| Point lights don't light the model | By design in stage 1: point lights are baked into static vertex colors. Animated models receive the scene's directional light + ambient. |
| Highlight rim (usable objects) missing on animated models | Known stage-1 limitation - the rim shell is built from static geometry parts. |
