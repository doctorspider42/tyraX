# Animated models (.glb, .fbx)

The editor plays skeletal animations authored in Blender (or any glTF
exporter) on the PS2 - with a real skeletal runtime. **FBX files import
too** - see [Importing FBX](#importing-fbx) below; everything in this
document applies to them identically, because an imported .fbx is parsed
into the exact same data the .glb path produces. At build time the
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
mesh. Two optional distance LODs stack on top - see
[Performance: draw distance and LOD](#performance-draw-distance-and-lod).
You get smooth, named, scriptable, *blendable* clips at a fraction of
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
| **Material** | Optional `.mtl` **override** on top of the built-in materials — see below. `(model's own)` = the materials baked into the file. |
| **Collision** | Box from the model's all-clips pose AABB, or none. Per-triangle mesh collision is a static-model (.obj) feature. |
| **Model yaw offset** | Content-forward correction in degrees around the model's own Y, applied between scale and rotation (viewport preview matches). A model authored facing **±X** (a common Blender habit — facing the red axis; both the glTF and FBX exporters treat Blender's **-Y** as front) walks sideways as an avatar or AI agent; set **±90** here and the mesh renders turned while the authored rotation, the avatar's turn-to-face and AI facing stay convention-pure. |

### Material override (.mtl)

By default an animated model draws with the materials baked into the `.glb`/
`.fbx` (base color + texture per part, listed read-only under **Materials**).
Assigning a **Material** (`.mtl` asset) overrides them — exactly like a static
`.obj` model, and it is an option *besides* the built-in materials, not a
replacement of the workflow: leave it `(model's own)` and nothing changes.

The override resolves by **name**: each of the model's parts (the entries in
the read-only Materials list) is matched against a `newmtl` of the same name in
the assigned file. A match takes that material's color (`Kd`) and texture
(`map_Kd`); a part the override does **not** name falls back to plain white and
untextured — a full replace, the same rule the `.obj` override uses. So name
your `.mtl` entries to match the part names shown in the panel (e.g. a model
whose part is `WobblerBody` needs a `newmtl WobblerBody`).

The override is resolved into the `.tskl` **at build time** (the part colors and
textures are baked in), so it costs the game nothing at runtime. Two objects
sharing one model but different overrides bake to separate `.tskl` files
automatically. Reflection (`refl`) is a static-vertex-color effect with no
skeletal-runtime slot, so a reflective material assigned to an animated model
tints/textures as usual but does not add a reflection pass.

## Importing FBX

**Project > Assets > Import model...** accepts `.fbx` next to `.glb` - both
land in `res/models/` and flow through the same pipeline (`.tskl`
serialization, viewport preview, LODs, locomotion mapping). The reader is the
vendored [ufbx](https://github.com/ufbx/ufbx) library, so binary and ASCII
FBX from Blender, Maya and 3ds Max all load. Differences from `.glb` worth
knowing:

- **Axes and units are auto-normalized** to the glTF convention
  (right-handed Y-up, meters): a Maya/Max rig authored in centimeters
  imports at the same size as its .glb twin. Geometry transforms (Maya
  pivots) are baked into the vertices.
- **Animation curves are resampled**, not translated: each FBX take is
  sampled at 24 Hz and keyframe-reduced per channel (RDP), which sidesteps
  the FBX curve soup (rotation orders, pre/post rotations, pivots) while
  keeping any authored pose within half a frame. Takes named
  `Armature|Walk` shorten to `Walk` (the full name is kept on a collision).
- **External textures are copied in**: a .glb embeds its textures, an .fbx
  often references image files - import copies the referenced files next to
  the copied .fbx so the project stays self-contained (non-PNG images are
  transcoded; power-of-two still required, like every PS2 texture).
- **Not supported** (imports with a warning, piece skipped): blend
  shapes/morph targets, procedural textures.

## Third-person player avatars

A **third-person Player** (`+ Add object > Gameplay > Player`, mode *Third
person*) reuses this whole pipeline for the visible character - the avatar is
just the Player object's own animated `.glb` model, baked, skinned, LOD'd and
rendered exactly like any other animated model. Nothing new to author: import a
rigged `.glb`, assign it to the Player, and map its clips to locomotion states
in *Properties*:

| Field | Meaning |
| --- | --- |
| **Idle / Walk clip** | Required; fall back to the model's first clip if left unset. |
| **Run clip** | Optional (`<none>` = walk covers all speeds). |
| **Jump clip** | Optional (`<none>` = holds walk/idle while airborne). |
| **Run at** | Planar-speed fraction (of full walk speed) where the run clip takes over. |
| **Distance / Height / Shoulder** | The camera rig offset in the camera's own frame: back, up, sideways. `Shoulder` 0 = centered behind, ~0.6 = over-the-shoulder, negative = the left shoulder. |
| **Turn rate** | How fast the avatar turns to face its movement direction. |

**Over-the-shoulder:** `Shoulder` slides the *whole* rig — the eye and the
look-at alike — along the camera's right vector, so the avatar sits off-center
in frame. (Offsetting only the eye would just angle the camera back at the
player and keep them centered, which is not an over-the-shoulder shot.) The
offset is itself spring-armed, so a shoulder cam cannot slide into a wall the
player is hugging; that second cast costs nothing when `Shoulder` is 0.

The camera rides a **spring arm**: each frame the boom is cast from the avatar's
head toward the desired eye, and the camera stops at the first blocker so it
never enters walls, props or the terrain. Two extra **whisker casts**, splayed
~20° to either side of the boom, spot walls the camera is about to sweep behind
and ease the boom in *ahead* of the hit, so approaching cover reads as a smooth
dolly-in instead of a snap; a hard clamp at the straight ray's hit distance
remains the never-clip guarantee for anything the whiskers missed, and the boom
eases back out when the way is clear.
*Distance* is therefore the maximum boom length, not a guarantee. Objects set to
collision **none** are ignored by the arm (the camera passes through them),
which doubles as the opt-out for scenery that should never shove the camera.
The arm tests **AABBs only**, even for mesh-collision models - camera collision
needs no triangle precision, and the cost has to fit a per-frame EE budget.

The runtime auto-selects idle/walk/run/jump from the player's **actual planar
speed** each frame, cross-fades on change (0.18 s) and matches playback speed to
the movement so the feet do not slide - **no state machine, no scripting**. The
override still works: a script or flow-graph **Play Animation** on the Player
fires any one-shot (wave, attack) that plays to the end before locomotion
resumes, and scripts attached to the Player see the avatar as `self`. A Cutscene
Director sequence's **Hide player** flag drops the avatar for the duration.

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

## Performance: draw distance and LOD

Every **visible** animated instance costs real EE time each frame (pose
evaluation + VU0 skinning + submission - roughly 2 ms per 1 000-vertex
instance on hardware; emulators hide it). Instances outside the camera
frustum are already free, and instances playing the same clip in lockstep
(autoplayed props/packs) share one skinning automatically. When a scene
still gets heavy, three stacking tools bring it back under the 20 ms PAL
budget - two are **per-project preferences**, one is **per object**:

| Setting | Where | Scope | What it does |
|---|---|---|---|
| **Draw distance** | Properties panel of the selected object | per object | Farther than this from the camera the object is not drawn at all. Collision, sounds, scripts and animation time keep running. `0` = always drawn. Works on static objects too. |
| **Animation LOD distance** | Project > Preferences > **Rendering** | per project | Instances farther than this refresh their pose/skinning every 2nd frame, beyond twice the distance every 4th (staggered across objects). Playback time is unaffected - the pose catches up on the next refresh. `off` (0) = every instance skins every frame. |
| **Mesh LOD distance** | Project > Preferences > **Rendering** | per project | The build bakes ~50% and ~25%-vertex variants of every animated model into the `.tskl` (quadric-error decimation; skin weights and uvs are preserved, never blended). Instances render the 50% mesh beyond the distance and the 25% one beyond twice the distance. `off` (0) = no LODs baked or kept in RAM. |

Both preferences live in the project file (`.tyra`), so every project tunes
its own values. Distances are world units from the camera to the object
center, the same units as object positions.

**Per-object overrides:** any animated object (and any Player avatar) can
override either preference in its Properties - **Override animation LOD** /
**Override mesh LOD** next to the playback fields. Unchecked (the default)
the project preference applies; checked, the object uses its own distance,
and dragging the value to `0` turns that LOD off for this object entirely
(a hero character that must never decimate next to a crowd that always
does). A mesh-LOD override > 0 also makes the build bake the decimated
chains for that object's model even when the project preference is `off`.
In a **two-player** scene the two Player objects each carry their own set,
so the P1 and P2 avatars tune independently - e.g. keep both full-detail
in split screen (each is small in the *other* player's half anyway), or
decimate only the second player's avatar.

Tuning guidance:

- Start with **Draw distance** on scenery that genuinely disappears at
  range - it is free and exact.
- **Animation LOD** is safe to enable broadly: at distance a 25 Hz pose
  refresh is invisible. It pays off most with *desynchronized* crowds
  (instances triggered at different times) - synced ones already share a
  skinning.
- **Mesh LOD** trades visual fidelity for the biggest submission savings.
  The reduced meshes are visibly simpler, so pick a distance at which the
  model is already small on screen - if you can see the difference from
  gameplay camera positions, the distance is too short. It also costs RAM
  and `.tskl` size (about +75% of the mesh data per model), which is why
  `off` bakes no chains at all.
- Numbers from the stress scene that drove this feature (15 desynchronized
  1 092-vertex spiders, 12 on screen at once): the animated pass went from
  22.9 ms (a hard 25 FPS) to 11.1 ms with both LODs enabled.

## Triggering from the Flow Graph

Three nodes (Add node > **Animation** / **Triggers**):

- **Animation** (action), **play** pin - starts a clip on the target object.
  - *Clip* (text): the clip name; empty = the model's first clip. Unknown
    names are ignored at runtime (a TYRA_WARN lands in the game log).
  - *Loop*: 1 = loop, 0 = play once.
  - *Speed*: playback multiplier; 0 = authored default (1.0).
  - *Fade*: crossfade seconds - the new clip blends in from whatever pose
    is currently showing instead of snapping. 0 = instant switch.
  - Target: an incoming **object link**, otherwise **self** (the node's
    text field holds the clip, not an object name - wire the target in).
- **Animation** (action), **stop** pin - freezes the target on its current
  pose (the params are ignored). The play pin resumes/restarts it.
- **On Animation Finished** (trigger) - fires the frame the watched
  object's clip reaches its last frame: **once** for a non-looping clip,
  **every wrap** for a looping one. Also usable as a bool source for the
  logic gates. Watched object: name parameter, object link, or self.

Typical patterns:

```
On Used ──▶ Animation ▸play ("open", loop 0)     a door that opens on USE
On Animation Finished ──▶ Set Object Visible ▸hide   hide after a death clip
Near Object ──▶ Animation ▸play ("wave")         greet the approaching player
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
| Clip switch pops | Give the Animation node a *Fade* (or `playAnimation(..., fade)`) - 0.2-0.4 s covers most transitions. |
| Animation too fast/slow on NTSC vs PAL | It isn't - playback is wall-clock normalized. Compare against a stopwatch, not frames. |
| `matrix-palette slots` error on import/build | The file needs more than 256 bones + rigid mesh nodes - simplify the rig. |
| Point lights don't light the model | By design: point lights are baked into static vertex colors. Animated models receive the scene's directional light + ambient. |
| Highlight rim (usable objects) missing on animated models | Known limitation - the rim shell is built from static geometry parts. |
