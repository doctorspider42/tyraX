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
2. A **Model size** dialog asks how big the thing is in the real world. Both
   importers normalize to meters, so a Mixamo character arrives ~1.7 units
   tall and the default answer is already right - unless the project works at
   a different **world scale**, in which case this is what makes the character
   match the world instead of standing knee-high in it. See
   docs/world-scale.md; the **Size...** button in the Assets list changes it
   later.
3. The Assets list shows the model as `animated: N clip(s), M verts`;
   hover for the clip names and warnings.
4. **Add object > Model >** *your file* `(animated)`, or pick the file in
   an existing model object's **Model** combo. The object is created at the
   scale the recorded size implies.

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
| **Speed** | Playback multiplier (1.00x = authored speed, itself scaled by the project's animation fps and the clip's own time scale - see [Animation editor](#animation-editor)). |
| **Color** | *Not used by animated models.* Their look comes from the model's own materials (or the `.mtl` override below); the console folds only the part color into the lit pass, so the field only tints the placeholder box shown while a model is missing. |
| **Material** | Optional `.mtl` **override** on top of the built-in materials — see below. `(model's own)` = the materials baked into the file. |
| **Collision** | Box from the model's all-clips pose AABB, or none - turned by the model yaw offset like the mesh is ([collision-boxes.md](collision-boxes.md)). Per-triangle mesh collision is a static-model (.obj) feature. |
| **Model faces / Yaw correction** | Tell the editor which way the imported mesh points in its own file: **+Z** is already correct, **-Z** fixes a backwards model with an explicit 180° turn, and **±X** fixes a sideways model with a 90° turn. The correction rotates only the mesh around its own Y (viewport preview matches); authored object rotation, avatar turn-to-face and AI facing stay convention-pure. The angle below the preset remains available for unusual files. |
| **Foot IK** | Optional game-side ground placement for a two-legged rig. Maps hip/knee/ankle bones, sweeps a four-corner sole footprint, plants slow feet, shifts/leans the pelvis over support, stabilizes knee bend, aligns shoes to normals and clears stair edges during swing. |
| **Neural landing prediction (VU0)** | Experimental learned residual on top of Foot IK. A 16-16-5 network reads current motion plus causal filtered gap/velocity, lip memory and swing phase; it advises a raycast-ranked foothold fan and bounded clearance/early release. Collision and procedural IK remain authoritative. |

### Foot IK: feet that stop at the floor

Turn on **Foot IK** for an animated Model or a third-person Player when a walk
clip has to follow stairs, curbs or uneven ground. **Auto-detect bones** handles
common Blender, Mixamo and humanoid names; otherwise select the left and right
hip, knee and ankle explicitly. Each leg must be a real descendant chain in
the imported hierarchy. The editor marks an invalid mapping before build and
the game leaves the original animation untouched if a mapping still cannot be
resolved.

The solver runs after normal clip evaluation and before VU0 skinning. It uses
the previous **unadjusted clip** ankle positions for stable vertical probes, supports
terrain, mesh-collision models and collision-box tops, and ignores the object
being solved. A sole must stay inside the contact band at stance-like horizontal
speed for a short confirmation window before it may plant; a fast descending
swing is never turned into a contact merely because it passed within probe
range. A plant releases as soon as the authored foot begins a measured toe-off
or pulls far enough away, then the ground correction fades quickly so the rising
foot belongs to the animation instead of being dragged through a release-height
band. Reading the unadjusted pose is
essential: feeding the already locked result back into the contact detector
would make the foot report that it never moved and stretch the leg behind the
 walking character. The lowest requested foot correction
 also lowers the common pelvis, then an analytic two-bone solve rotates the hip
 and knee. A filtered authored knee pole guides the solve, and approaches full
 authority near straight-leg singularities where a raw cross product can flip
 sides. The authored ankle yaw is preserved while an optional pitch/roll
 tilt follows the supporting surface normal. This is deterministic
procedural IK — there is no model inference or runtime allocation in the loop.

While walking, a foot that is measurably descending **and horizontally slowing
into stance** gets an additional capture range of *Plant distance + 55% of Max
pelvis drop*. This is intended for
split-level motion, such as walking with one foot on a curb and the other on the
road. The extra range is unavailable to a level or rising swing foot, which
keeps the broader snap from stealing the airborne half of the gait. A new
contact fades in with a 0.14-second smoothstep envelope instead of applying a
large correction on its first frame, reducing the visible touchdown kick. A
released contact keeps its old target while its weight smoothsteps to zero over
0.08 seconds; target height and influence therefore cannot jump on the same
frame when a probe crosses a stair edge.

Descending stairs also use a short **down-reach** phase before contact. When a
sole ray proves that a tread is both reachable and lower than the opposite
planted foot, and the authored foot is moving down, the solver gradually
extends only the ankle's vertical target toward that tread. It does not lock
the foot or alter its horizontal path, so a fast swing can finish the step
without hovering while flat-ground toe-off remains free.
The ordinary speed and confirmation gates still decide when the real plant may
begin. This geometry-backed phase works with neural prediction disabled; the
network may improve the upcoming XZ candidate but never invents support.

During the unlocked swing, two short probes look ahead in the character's
travel direction (or the ankle direction for an in-place animation). The
current support normal predicts the continuous uphill rise; only an abrupt
excess above that plane counts as a lip. When either probe finds one, the
solver releases a stale lower-step contact and adds
a filtered vertical clearance to carry the shoe over the riser before normal
contact logic may plant it. This is separate from landing prediction and works
with the neural option disabled.

The same swing also sweeps four approximate shoe corners at two points between
the previous and current sole poses. This closes the gap where a fast toe can
cross a thin stair riser between vertical raycasts. It is deliberately a small
raycast footprint rather than a general convex-physics sweep: every lift still
comes from scene collision, it allocates nothing, and it is cheap enough for
the PS2. A footprint sample contributes clearance only when it finds support
more than 2.5 cm above the current support plane; equal-height flat ground must
never manufacture toe clearance or prevent the next plant.

When neural prediction is enabled, its residual seeds a five-point landing
fan. Each candidate is raycast and scored for center, toe and heel support;
the selected point may be on another tread, unlike the old same-height-only
residual. The target is cached and filtered, guides the airborne ankle by at
most 16 cm, and still needs the normal descending/contact gates before it can
lock. A missing surface simply removes the candidate.

Planted-foot weights also define a tiny support polygon. The runtime shifts the
pelvis by at most 4.5 cm toward its weighted center, leans the pelvis subtree
gently toward the average support normal, and supplies each active leg with a
low-weight pole hint from the authored knee. The engine applies those body
changes first and re-solves both analytic legs afterwards, so balance motion
does not drag locked shoes away from their contacts.

Third-person stair climbing separates the player's collision height from its
presentation height. Collision still moves onto a walkable step immediately,
but the avatar root and camera follow through an 8/s low-pass filter while the
feet solve against the real surfaces. This removes the one-frame whole-body
hop without weakening collision or delaying the player capsule. Airborne
motion follows the physical height directly; the filter resumes on contact so
a landing onto a raised surface cannot reintroduce the same one-frame jump.

Standing at an edge uses a separate **rest contact**. After the whole object has
been stationary for 0.1 seconds, an ankle that is itself nearly still may reach
down through the normal plant band by up to *Plant distance + Max pelvis drop*.
That lets one leg remain on a step while the other finds the lower floor instead
of hovering at the height of the supported foot. Rest contact releases as soon
as the object moves, so it cannot turn a walking swing into a ground magnet. The
common pelvis correction is low-pass filtered independently of the leg weights;
when a probe sits on an edge and alternates between two surface heights, the
feet react without making the whole body chatter by the same amount.

Probe, plant, release and pelvis fields are in project world units. The sole
offset is in the model's own units and follows the object's scale:

- **Sole offset** is the ankle-to-floor gap. Raise it if shoes sink; lower it if
  they hover.
- **Probe up/down** is the vertical search range around the animated sole.
- **Plant distance** is how close the animated sole must be to ground before it
  may lock.
- **Release distance** is how far the animation may pull from a planted foot
  before it unlocks.
- **Max pelvis drop** caps how far the body may crouch to reach the lower foot.
- **Max foot tilt** caps the pitch/roll added to a planted ankle. `0°` keeps the
  authored ankle orientation; the default `35°` follows ordinary slopes while
  refusing extreme triangle normals.
- **Toe clearance** is the extra world-space gap requested above a higher
  surface detected ahead of an airborne shoe. `0` disables swing clearance.

Foot IK is deliberately opt-in. Disabled objects keep pose sharing and the old
animation cost. Enabled objects need their own corrected pose and therefore do
not share a skinned mesh with lockstep instances. Ground placement currently
runs in the generated game; the editor viewport continues to show the authored
clip so the raw animation remains easy to inspect.

The authored checkbox is the master capability, but it can be switched while
the game is running. **Set Foot IK** in the flow graph exposes `enable`,
`disable` and `toggle` exec inputs for any named animated object (or the graph's
owner when its Object field is empty). Scripts can write the same
`RuntimeObject::footIkEnabled` flag. Disabling immediately restores the raw
animation pose and clears planted targets, filters and landing history; a later
enable therefore starts from the current pose and terrain instead of snapping
back to a contact remembered on an old stair. Enabling an object whose authored
Foot IK checkbox is off does nothing.

#### Experimental neural landing prediction

**Neural landing prediction (VU0)** is an optional learned layer inside Foot
IK, not a replacement for the stair solver. Its deterministic 16-input,
16-hidden-ReLU, 5-output network consumes the object's planar velocity, current
and previous ankle velocity, sole-to-ground gap, a ground sample 80 ms ahead,
the support normal, two slope-removed ahead-probe residuals, a filtered gap and
vertical velocity, decaying lip memory and swing phase. It predicts a small
horizontal landing residual, contact confidence, extra stair-clearance intent
and early-release intent.

The committed weights are trained by `tools/train-foot-neural.py` (seed 2002)
from domain-randomized synthetic slopes/lips plus the labelled PCSX2 trajectories
in `tools/data/foot-neural-real.csv`. A trace run records both feet frame by
frame; the regression runner looks up the next verified plant within an
18-frame causal window for the landing label and copies the procedural solver's
raycast-proven clearance/release decisions. Already planted frames are omitted
unless they contain an emergency stair release, preventing long static contacts
from drowning the short useful events. The whole training path is deterministic
and dependency-free, and runtime needs neither a model file nor an allocator.

The multiply-accumulate work uses VU0 **macro mode**. It occupies no VU0 micro
memory, so it can coexist with project VU0 kernels and the experimental
raytraced mirror. The setting's **Neural influence** scales the result from 0
to 1; 0 still evaluates the model for telemetry but applies no residual.

The network never creates contact or an obstacle. Its residual centers a small
fan whose candidates, toe and heel are raycast against real collision. The best
supported cached point may guide the swing by at most 16 cm and may lock only
through the ordinary descending/contact gates. Step heights, pelvis correction,
reach limits and the analytic leg solve stay fully procedural. The stair heads
are gated separately: ordinary probes or the swept footprint must
first prove an abrupt lip; learned clearance may then add at most 35% of the
authored **Toe clearance**, and learned release may only loosen a nearly rising
foot which is still stuck to the lower tread. This deliberately prevents a
confident but wrong inference from planting a foot in empty space, lifting it on
a smooth hill or reintroducing a vertical touchdown kick.

### Material override (.mtl)

By default an animated model draws with the materials baked into the `.glb`/
`.fbx` (base color + texture per part). Assigning a **Material** (`.mtl` asset)
overrides them — exactly like a static `.obj` model, and it is an option
*besides* the built-in materials, not a replacement of the workflow: leave it
`(model's own)` and nothing changes.

The override resolves by **name**: each of the model's parts is matched against
a `newmtl` of the same name in the assigned file. A match takes that material's
color (`Kd`) and texture (`map_Kd`); a part the override does **not** name falls
back to plain white and untextured — a full replace, the same rule the `.obj`
override uses. So name your `.mtl` entries to match the model's part names
(e.g. a model whose part is `WobblerBody` needs a `newmtl WobblerBody`).

Because an animated model has no sibling `.mtl` to assign, the **Material**
picker has a **+ New material from this model...** entry: it extracts the
model's built-in materials — part names, base colors, embedded textures — into
a new `res/materials/<model>.mtl`, assigns it and opens the **Material Editor**
previewed **on the model itself** (bind pose). That is the one-click way to
start editing an animated model's look: its own materials become an editable
override you recolor/repaint, and what the editor shows matches what the console
bakes. (A part whose material is **unnamed** can't be name-matched — name it in
the modelling tool first.)

The override is resolved into the `.tskl` **at build time** (the part colors and
textures are baked in), so it costs the game nothing at runtime. Two objects
sharing one model but different overrides bake to separate `.tskl` files
automatically. Reflection (`refl`) is a static-vertex-color effect with no
skeletal-runtime slot, so a reflective material assigned to an animated model
tints/textures as usual but does not add a reflection pass.

## Animation editor

**Tools > Animation Editor** edits a model's clips **non-destructively**: the
`.glb`/`.fbx` on disk is never rewritten. Your changes are stored in the
project file and folded into the `.tskl` at build time, so the console
receives clips that are already retimed, trimmed and renamed and pays nothing
for it at runtime. The panel previews the model playing the clip with your
staged values, and the scene viewport applies exactly the same numbers to
every placed object - what you scrub is what ships.

Pick a model at the top, a clip on the left, and edit:

| Field | Meaning |
|---|---|
| **Name in game** | The name scripts, flow nodes and the Start clip picker use. Empty = the name authored in the file. Renaming retargets the clip references of objects using this model (including Animation nodes in their own graphs). |
| **Time scale** | Playback speed of this clip. `2.00x` plays it twice as fast (half as long). Stacks on top of the project's animation fps below; the object's **Speed** property and a flow node's **Speed** param still multiply on top at runtime. |
| **Trim start / Trim end** | Cut the clip down to a range of the **source** animation. Seconds as authored, so changing the speed never moves the handles. The trimmed clip is rebased to start at 0 and gets interpolated boundary poses, so it starts and ends exactly where you cut. |
| **Loop by default** | Seeds the **Loop** checkbox of objects that later pick this clip as their Start clip. Objects already placed keep their setting, and a flow node's own Loop param always wins at runtime. |

A line under the fields spells out the result (`authored 2.000 s -> ships as
0.400 s (2.50x)`), edited clips are marked with `*` in the list, and **Reset
this clip** puts it back to exactly what the file contains.

Because the edits are baked at build time, a running game cannot receive them
over [Live Link](live-link.md) - the LIVE chip turns amber (rebuild) when you
retime a clip.

### Preview lighting

The preview shades the model with the **scene's** ambience, on purpose: what
you scrub is what ships. On a deliberately dark scene - a cavern preset, a low
brightness - that also makes the preview unreadable, so the combo next to
**Wireframe** picks what the preview bakes with instead:

- **Scene ambience** (default) - the light the object will really get in game.
- **Neutral studio** - the engine's default directional light, bright and
  neutral. Reach for this to judge geometry, a pose or a texture.
- **any ambience preset** of the project - preview the model under the mood it
  will be placed in without switching the scene over to it.

It only affects the preview: the scene, the project file and the build are
untouched. The choice is a machine setting (`editor.ini`), shared with the
Material Editor's identical combo - see [material painting](material-painting.md).

### Project-wide animation fps

glTF and FBX store keyframe times in **seconds** and no frame rate at all. So a
clip animated for 30 fps but exported from a Blender scene running at 24 fps
arrives 25% too long, and plays visibly too slow in game - with nothing in the
file for the importer to detect it by.

**Project > Preferences > Rendering > Animation fps** is the one-line fix:
the left number is the fps the clips were **exported at**, the right one the
fps they **should play at**. The build scales every clip of every model by that
ratio (`30/24 = 1.250x faster`). Equal values - the default 24/24 - change
nothing, and projects saved before this setting existed keep playing exactly as
they did.

Use the per-clip **Time scale** above for a single clip that is off; use this
for the usual case where a whole export is uniformly wrong.

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
| **Face camera (strafe)** | The avatar keeps facing the camera instead of turning into the movement direction — sideways/backward movement then plays the directional clips below. |
| **Back / Strafe left / Strafe right clip** | Optional directional locomotion (`<none>` = the walk clip covers that direction). Only shown — and only active — with *Face camera* on. |
| **Style** | The camera rig: **Orbit (behind)** = free look (right stick orbits), **Top-down** / **Isometric** / **Fixed angle** = the camera is pinned to a set angle for camera-locked games. Top-down and Isometric are presets of Fixed angle — picking them seeds the angles below, which stay editable. |
| **Angle / Direction** | Fixed styles only: elevation above the horizon (85 = nearly straight down, ~35 = the classic isometric slant) and the world heading the camera looks along (which way is "up" on screen). |
| **Right stick rotates** | Fixed styles only: let the player orbit the pinned view with the right stick (the elevation stays locked). |
| **Distance / Height / Shoulder** | The camera rig offset in the camera's own frame: back, up, sideways. `Shoulder` 0 = centered behind, ~0.6 = over-the-shoulder, negative = the left shoulder. |
| **Turn rate** | How fast the avatar turns to face its movement direction (or the camera, with *Face camera* on). |

**Camera styles:** the fixed styles pin `entPitch` every frame (and the yaw
unless *Right stick rotates* is on), so the camera holds its authored angle
while the left stick moves the avatar **relative to the camera heading** —
exactly the control scheme a top-down or isometric game wants. Everything else
is unchanged: the spring arm still shortens the boom against terrain and
geometry, *Distance* sets how far out the camera rides, and locomotion clips
keep auto-selecting from speed.

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
The arm tests **collision boxes only**, even for mesh-collision models - camera
collision needs no triangle precision, and the cost has to fit a per-frame EE
budget. The box is the model's own bounds, oriented with the object (including
the *Model yaw offset* below, so an X-forward-authored avatar's box turns with
its mesh); *View > Collision boxes* and *Preferences > Build > Show collision
boxes* draw exactly what the arm tests. See [collision-boxes.md](collision-boxes.md).

The runtime auto-selects idle/walk/run/jump from the player's **actual planar
speed** each frame, cross-fades on change (0.18 s) and matches playback speed to
the movement so the feet do not slide - **no state machine, no scripting**.
This is measured rather than guessed: while baking `.tskl` v4 the editor samples
both detected ankle bones through every clip, records the horizontal sweep per
second as that clip's authored gait speed, and the player compares it with the
distance actually moved. Changing *Walk speed*, the model scale or analog-stick
deflection therefore changes cadence consistently. Older `.tskl` files and rigs
whose foot names cannot be detected retain the legacy stick-fraction fallback.

**Directional locomotion:** by default the avatar turns to face where it walks,
so every step is a forward step and one walk clip covers everything. Turn on
**Face camera (strafe)** and the avatar keeps facing the camera while the stick
moves it in any direction - the runtime then splits the movement direction into
four 90°-ish sectors relative to the facing (within 60° of straight ahead =
walk/run, within 60° of straight back = **Back clip**, the side quadrants =
**Strafe left/right clip**) and cross-fades between them as the direction
changes. Any unmapped direction falls back to the walk clip, so you can adopt
this incrementally - map only a back clip and sidesteps still walk. The
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
| **Mesh LOD distance** | Project > Preferences > **Rendering** | per project | The build bakes ~50% and ~25%-vertex variants of every model - animated ones into the `.tskl`, static ones into the `.tmdl` (quadric-error decimation; skin weights and uvs are preserved, never blended). Objects render the 50% mesh beyond the distance and the 25% one beyond twice the distance. `off` (0) = no LODs baked or kept in RAM. Static models can use hand-authored levels instead - see [model-pipeline.md](model-pipeline.md). |

Both preferences live in the project file (`.tyra`), so every project tunes
its own values. Distances are world units from the camera to the object
center, the same units as object positions.

**Per-object overrides:** any animated object (and any Player avatar) can
override either preference in its Properties - **Override animation LOD** /
**Override mesh LOD** next to the playback fields. A static model object
carries the mesh-LOD row too (animation LOD means nothing to it). Unchecked (the default)
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
| Everything from Blender plays too slow (or too fast) | The export's frame rate does not match what the animation was made for - set *Preferences > Rendering > Animation fps* (see [Project-wide animation fps](#project-wide-animation-fps)). |
| One clip is off while the rest are fine | *Tools > Animation Editor* > **Time scale** on that clip. |
| `matrix-palette slots` error on import/build | The file needs more than 256 bones + rigid mesh nodes - simplify the rig. |
| Point lights don't light the model | By design: point lights are baked into static vertex colors. Animated models receive the scene's directional light + ambient. |
| Highlight rim (usable objects) missing on animated models | Known limitation - the rim shell is built from static geometry parts. |
