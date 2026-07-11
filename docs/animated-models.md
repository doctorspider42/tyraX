# Animated models (.glb)

The editor plays skeletal animations authored in Blender (or any glTF
exporter) on the PS2 - with a real skeletal runtime. At build time the
model's node hierarchy, skin (inverse bind matrices, per-vertex joints and
weights), bind-pose mesh and raw keyframe tracks are serialized to a compact
`.tskl` file. The game evaluates the pose on the EE every frame (keyframe
interpolation at authored fidelity, crossfade blending between clips), skins
the vertices through the matrix palette on **VU0 in macro mode** (the
era-correct split: animation on VU0, 3D on VU1, game code on EE) and renders
the result through the engine's **static pipeline** alongside the rest of the
scene (one vertex upload instead of DynPip's from/to double send, no VU1
program swap mid-frame, and screen-edge triangles clipped by the same EE
clipper as everything else). The whole animated pass is budgeted from real
hardware measurements - a 1092-vertex instance costs ~0.9 ms pose+skin plus
~1 ms submit on the EE, which PCSX2's fast EE completely hides: instances
whose conservative all-clips AABB is outside the frustum skip pose
evaluation, skinning and submission entirely (playback time still advances),
and instances striking the identical pose - the same clip autoplaying in
lockstep, the usual ambient-prop or enemy-pack setup - share one skinned
mesh. Two optional distance LODs stack on top (Preferences > Rendering):
**Animation LOD distance** makes far instances refresh their pose every
2nd/4th frame (playback time unaffected), and **Mesh LOD distance** bakes
~50% and ~25%-vertex variants of every part into the `.tskl`
(quadric-error collapse; attributes and skin bindings ride along
unchanged) and renders them beyond the distance. Expect visible
simplification on the reduced meshes - pick a distance at which the
model is already small on screen. You get smooth, named, scriptable, *blendable* clips at a fraction of
the memory the old baked-frame path needed.

```
Blender (rig + actions)
   │  File > Export > glTF 2.0, format "glTF Binary" (.glb)
   ▼
res/models/character.glb          ← the imported source asset
   │  every build: skeleton + bind mesh + keyframe tracks serialized
   ▼
res/models/character.tskl (+ extracted PNG textures)
   │  loaded by the engine's TsklLoader
   ▼
SkelInstance (EE pose evaluation + VU0 skinning) → StaPip (VU1 transform/light, EE clipping)
```

(Stage 1 of this feature baked clips into MD2-style morph frames - `.tanm`
files, ~10-50x more RAM, 12 fps sampling, no blending. The authoring surface
did not change when the skeletal runtime replaced it; the engine still ships
`TanmLoader` for old builds, but the editor now writes `.tskl` only.)

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
- **Keep meshes lean.** Every visible instance is skinned on VU0 each
  frame, so vertex count is now a CPU budget, not a memory one. A few
  hundred to ~2k triangles per character is the era-correct ballpark
  (3 × 1.5k-vertex characters hold a full 50 FPS with plenty of headroom).
- **Bone budget:** up to **256 matrix-palette slots** per model (bones
  plus rigidly-animated mesh nodes) - far beyond any era-correct rig.
- **Loops:** make the last keyframe equal the first. Looping playback wraps
  the clip time, so an exact match loops seamlessly.
- Apply your transforms sensibly: the model renders in the glTF scene's
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

The viewport plays the object's start clip immediately. The preview samples
clips at 12 fps and lerps between the samples (the console interpolates the
keyframes exactly), so the two match to within a pixel - what you see is
what ships.

### Object properties (Properties panel)

| Field | Meaning |
|---|---|
| **Start clip** | Clip playing at scene start (`(first)` = the file's first clip). |
| **Autoplay at scene start** | Off = the model holds the clip's first frame until a script/flow node starts it. |
| **Loop** | On = wraps forever; off = plays once and freezes on the last frame. |
| **Speed** | Playback multiplier (1.00x = authored speed). |
| **Color** | Multiplies the model's material colors (tint), like on primitives. |
| **Collision** | Box from the model's all-clips pose AABB, or none. Per-triangle mesh collision is a static-model (.obj) feature. |

Material (.mtl) overrides do not apply to .glb models - their materials come
from the file itself.

## Memory budget

The skeletal runtime stores one bind-pose mesh plus keyframe tracks - clip
length is nearly free (a key is 16-20 bytes per animated bone). Rough rule:

```
bytes ≈ vertices × 75  +  keys × 18        (+ ~72 per bone)
```

A 1 500-vertex character with three clips is ~150 KB of the console's 32 MB
(the same model was several MB as stage-1 baked frames). The importer still
warns above ~8 MB per model - with this runtime you would need an absurdly
heavy mesh to hit it.

Vertices here are *expanded triangle-list* vertices (the PS2 pipelines have
no index buffers), so the editor's reported count is higher than Blender's.
The "baked frames" number in the Assets/Properties panels describes the
editor's 12 fps viewport preview, not what the console stores.

## Triggering from the Flow Graph

Three nodes (Add node > **Animation** / **Triggers**):

- **Play Animation** (action) - starts a clip on the target object.
  - *Clip* (text): the clip name; empty = the model's first clip. Unknown
    names are ignored at runtime (a TYRA_WARN lands in the game log).
  - *Loop*: 1 = loop, 0 = play once.
  - *Speed*: playback multiplier; 0 = authored default (1.0).
  - *Fade*: crossfade seconds - the new clip blends in from whatever pose
    is currently showing instead of snapping. 0 = instant switch.
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
// crossfade into the next clip over 0.3 s instead of snapping:
playAnimation(ctx, objectIndex, "idle", true, 1.0F, /*fade=*/0.3F);
stopAnimation(ctx, objectIndex);

if (animationFinished(ctx, objectIndex)) {
  // fires the frame the clip ends (or wraps, for looping clips)
}
```

Lower level, the same state lives on `RuntimeObject`: `animClip` (index -
resolve names with `ctx.resolveClip(objectIndex, "name")`), `animPlaying`,
`animLoop`, `animSpeed`, `animRestart` (set true to apply a clip change),
`animFade` (crossfade seconds consumed by that restart), `animFinished`
(read-only, one frame). All of it is a no-op on objects that are not
animated models.

## What the build produces

On every build the editor re-serializes each referenced `.glb`:

- `res/models/<name>.tskl` - the skeletal binary (node hierarchy, matrix
  palette, keyframe tracks with 16-bit quantized rotations, bind-pose mesh
  with per-vertex joints/weights; layout documented in `src/glbparser.cpp`
  `writeTskl`, loader in `vendor/tyra/engine/.../tskl_loader`).
- `res/models/<name>_<image>.png` - textures extracted from the file.
  They flow through the texture-quantization bake (`.res-baked/`) like any
  other PNG, following the project-wide quality setting.

Sources are never modified; the output is deterministic, so `.tskl` files
need no special handling in version control (they regenerate on build).

## Limits and troubleshooting

| Symptom | Cause / fix |
|---|---|
| `unusable: ...` in Assets | The .glb is malformed or empty - re-export; the error names the reason. |
| Model invisible in-game, `TsklLoader: cannot read` in log | The `.tskl` was not written (check the build log's `[anim bake]` lines for the reason). |
| Texture renders as flat color, `Model texture missing` warning | Extracted PNG missing next to the `.tskl`, or non-POT texture (see the import warning). |
| Clip does not switch | Clip name typo - names are case-sensitive; check the Assets tooltip for the exact list. |
| Clip switch pops | Give Play Animation a *Fade* (or `playAnimation(..., fade)`) - 0.2-0.4 s covers most transitions. |
| Animation too fast/slow on NTSC vs PAL | It isn't - playback is wall-clock normalized. Compare against a stopwatch, not frames. |
| `matrix-palette slots` error on import/build | The file needs more than 256 bones + rigid mesh nodes - simplify the rig. |
| Point lights don't light the model | By design: point lights are baked into static vertex colors. Animated models receive the scene's directional light + ambient. |
| Highlight rim (usable objects) missing on animated models | Known limitation - the rim shell is built from static geometry parts. |
