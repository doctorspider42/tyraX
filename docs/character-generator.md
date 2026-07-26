# Character generator

*Tools > Character Generator* builds a **rigged, skinned, textured human** at
the PS2's budget - 1460 triangles, 23 bones, one 256² skin - from a handful of
sliders, and drops it into the scene as an ordinary animated model.

```
macro sliders (gender / age / muscle / weight / ethnicity)
   │  weighted sum of MakeHuman's CC0 morph targets
   ▼
19158-vertex reference body (deformed)
   │  proxy741 barycentric fit          │  joint cubes -> 23 Mixamo bones
   ▼                                     ▼
741-vertex body + UVs  ────skin weights transferred────>  rigged mesh
   │  gltfwrite
   ▼
res/models/characters/<name>.glb   ← body + clothes + hair, idle/walk/run/jump
   │  the existing animated-model chain: import, preview, Animation Editor,
   ▼  .tskl bake with LODs, player avatars, NPC AI, Live Link
```

Nothing downstream knows a character was generated. That is the whole design:
the generator's output is a **plain glTF binary**, which is exactly where
[animated models](animated-models.md) already begin.

## Where the data comes from

The bodies are **MakeHuman's CC0 data set**, not MakeHuman the program. The
program is AGPL and none of it is used here; the base mesh, morph targets,
proxy meshes, rig, vertex weights and skins were explicitly released as CC0 in
2020, and the project [says outright](https://static.makehumancommunity.org/mpfb/faq/build_other_chargen.html)
that building your own character generator on them is fine. Credits are in
[README.md](../README.md#credits).

`setup.ps1` fetches about 80 MB into `vendor/mh-assets` (git-ignored, listed in
`deps.ps1`):

| File | What it is |
|---|---|
| `base.obj` | the hm08 reference mesh: 19158 vertices, 18486 quads, in decimetres |
| `targets/*.target` | 96 macro morph targets (sparse per-vertex offsets) |
| `proxy741.obj` / `.proxy` | the low-poly body and its binding to the base mesh |
| `default.mhskel` | the 163-bone reference rig |
| `default_weights.mhw` | per-bone vertex weights on the base mesh |
| `skins/*.png` | six 2048² CC0 diffuse maps |
| `clothes/*.mhclo` + `.obj` + `_diffuse.png` | five suits and a pair of shoes |
| `hair/*.mhclo` + `.obj` + `_diffuse.png` | four hairstyles (alpha-cutout cards) |

The editor never downloads anything itself. With the directory missing the
window explains how to get it and does nothing else.

## The three tricks that make it fit on a PS2

**1. The macro sliders are corners, not targets.** MakeHuman does not have a
"muscle" morph - it has targets for every *combination* of the macro axes, and a
setting is the product of one factor per axis. Gender has two levels, age four
(baby 1 year / child 10 / young 25 / old 90), muscle and weight three each, so a
body is a blend of up to 16 `universal-*` targets plus 12 `<ethnicity>-*` ones.
`chargen.cpp` reimplements that composition; the targets are loaded lazily and
cached, so a slider drag re-blends about 28 sparse arrays and nothing else.

**2. The low-poly body rides the high-poly one.** `proxy741` is a 741-vertex
mesh whose every vertex is expressed as *three base-mesh vertices with
barycentric weights, plus an offset measured in units of the base mesh's own
proportions*. Evaluate it against the deformed reference body and it follows
every morph exactly - no decimation, no re-fitting, and the UVs (which the CC0
skins are painted for) come along untouched. This is why the generator can hit
a PS2 budget without a quadric collapse melting the face.

**3. The rig is derived from the morphed mesh, not fitted to it.** MakeHuman
defines each joint as a *cube of base-mesh vertices*; the joint's position is
their centroid. Deform the body and the skeleton moves with it for free - a
child and a heavy-set adult get correctly placed hips without anything
resembling an IK solve.

## The rig

23 bones, named the way Mixamo names them (`mixamorig:Hips`,
`mixamorig:LeftForeArm`, ...). Nothing in this pipeline needs the names - the
matrix palette and every animation channel address nodes by index - but they
are what free animation libraries and retarget tools match on, and renaming a
rig afterwards is far more annoying than naming it right once.

```
Hips ─ Spine ─ Spine1 ─ Spine2 ─┬─ Neck ─ Head ─ HeadTop_End
  │                             ├─ LeftShoulder  ─ LeftArm  ─ LeftForeArm  ─ LeftHand
  │                             └─ RightShoulder ─ RightArm ─ RightForeArm ─ RightHand
  ├─ LeftUpLeg  ─ LeftLeg  ─ LeftFoot  ─ LeftToeBase
  └─ RightUpLeg ─ RightLeg ─ RightFoot ─ RightToeBase
```

The reference rig has 163 bones (fingers, toes, eyes, jaw, twist bones); each
one's weights fold into the nearest ancestor that survived, so nothing is
dropped - a finger's influence simply becomes the hand's. The toe chains are
the one exception to the ancestor walk: MakeHuman parents all five straight to
the foot, so they are matched by name onto the toe bone instead, which keeps
the ball of the foot animated.

Every bone keeps **identity rotation** at bind, so a bone's local space is world
space and the inverse bind matrix is a pure translation. That makes the rig
trivial to author against and to reason about. It also means a clip authored
for MakeHuman's own bone axes would need converting - which is fine, because
clips do not come from there.

## What the sliders do

| Control | Effect |
|---|---|
| Gender | 0 female … 1 male |
| Age | MakeHuman's scale: 0 = baby, 0.19 = child, 0.5 = young adult, 1 = old |
| Muscle / Weight | 0 min … 0.5 average … 1 max |
| Ethnicity | a 3-way mix, normalized to 1 inside the generator |
| Height | scales the finished body; feet land on `y = 0` |
| Skin | one of the CC0 diffuse maps, box-filtered to 64 / 128 / 256 |

**Height is a scale, not a morph.** MakeHuman's height and body-proportion
targets are two further 144-file sets (~120 MB) whose effect at this polygon
budget is mostly overall size, and a game wants a metre value anyway. The cost
is honest: a 1.25 m "child" preset is a child-shaped body (the age targets do
that) scaled to 1.25 m, not a separately authored short adult.

The **skin texture is forced opaque**. StaPip draws with the GS alpha test set
to "pass only when alpha != 0", so a transparent texel in a body skin would
punch a hole straight through the character - see the lightmap trap in
[ambient-occlusion.md](ambient-occlusion.md) for the same rule biting elsewhere.

Everything is **deterministic in the parameters**: identical sliders produce
byte-identical files, which is what makes a rebuild-on-drag preview cheap to
reason about and a generated character reproducible from its numbers.

## The wardrobe

Clothes, shoes and hair come from the same CC0 asset packs and fit through
**the same mechanism the body does**: a `.mhclo` file is byte-for-byte the same
barycentric binding as a `.proxy`, so a shirt is bound to the reference mesh's
shoulder vertices and therefore follows every morph, and the *same* skin
weights transfer gives it the same skeleton. There is no cloth solver and no
per-body refitting; a shirt on a heavy-set character is the same shirt.

Each garment becomes its own mesh part with its own texture, and the body
underneath is **removed**: every `.mhclo` lists the base-mesh vertices it
covers, and a body vertex whose three reference vertices are all covered drops
out with its faces. A shirt-and-trousers pass takes the body from 1460 to about
750 triangles, so a dressed character costs far less than body + garment.

Three things about the source assets shape what the generator does with them:

**They are 3.5k-16k triangles.** These are offline-render meshes. The *Detail*
setting decimates each garment to a budget (~500 / 1100 / 2200 triangles), and
the slots do not share it equally - a suit is most of the silhouette, shoes are
two small blocks at the bottom of the screen. Measured: the suits stop
simplifying around 1000 triangles and start tearing holes instead, so *Low* is
for crowds, not for a hero.

**Hair does not decimate at all.** It is a pile of separate quads with a uv
seam around every one, and the quadric collapse locks seam and border vertices
by construction - a 3678-triangle hairstyle asked for 550 came back at 2696.
Hair is thinned by dropping whole **cards**, smallest first, which is both what
actually shrinks it and what a low-poly hairstyle is: fewer, bigger strands.

**Hair is an alpha cutout**, so its texture keeps its alpha channel while the
body skin's is forced opaque - and that alpha is made **binary** with the
opaque colors dilated outward, the same rule the tree generator's leaf card
follows, because the engine's palettized tRNS→CLUT path loses a soft gradient
and bilinear filtering would ring dark fringes through it. Parts are tagged by
material prefix (`hair:` / `cloth:`) so the preview knows which to draw last;
the console needs no tag, it gets the cutout from the texture itself.

## Cost on the console

An undressed generated character is ~400 KB of PS2 RAM (a dressed one ~830 KB) (model data plus one instance's
skinned output buffers) and skins on VU0 like any other animated model. The
`.tskl` bake adds the usual two distance LODs on top. Budget guidance from
[animated models](animated-models.md) applies unchanged: a few instances are
comfortable, and the texture is the thing to watch in a crowd - GS VRAM is
~1.33 MB with no eviction, so drop the skin to 128 or 64 before dropping
triangles (see [gs-vram.md](gs-vram.md)).

## Animation

Every generated character ships with **idle / walk / run / jump**, generated
analytically by `charanim.cpp` - no motion library, no licence, no download.
Those are exactly the clip names the generated game's third-person locomotion
looks for, so a generated character dropped in as a Player avatar walks, runs
and idles with cross-fades and **no further setup**. `idle` is written first,
so a plain Model object (which autoplays the model's first clip) idles rather
than standing in the bind pose.

Two decisions carry the whole module:

**Bones are found by name, poses are composed in world space.** A `Frame`
holds one *world* rotation per bone role and `buildClip` converts to the local
rotations glTF stores (`local = inverse(parent world) * world`). That is what
makes the cycles readable: "the shin follows the thigh plus a knee bend" is a
statement about world orientation, and expressing it as a chain of
parent-relative frames instead is how animation code becomes unreadable.

**The rest stance is derived, not hardcoded.** MakeHuman's arms bind straight
out to the sides, and diagonally - down, out and slightly forward, at an angle
that changes with the body's proportions. The first attempt rotated them down
by a fixed angle and folded both elbows across the chest. `alignTo(bind
direction, target direction)` builds the minimal rotation instead, so "the
upper arm hangs down and a little out" means that for every body.

The cycles themselves are ordinary keyframe animation - sinusoidal hip and arm
swing in counterphase, a knee that only folds backward and only through the
back half of the swing, an ankle that rolls the foot off at toe-off, a lean and
counter-rotation up the spine that grow with speed, and two hip rises per
stride. Sampled at 15 keys/second and written as LINEAR quaternion channels,
with consecutive keys sign-corrected (interpolating between `q` and `-q` takes
the long way round, which reads as a limb snapping through the body).
An all-identity channel is dropped rather than written - the EE pays per
channel.

`charanim::poseMesh` does linear-blend skinning on the host, which is what lets
the editor preview PLAY a cycle: it is the same evaluation the console does on
VU0, at a scale where a full re-skin per frame is free.

## Importing animation (Mixamo and friends)

*Import clips...* retargets an existing `.glb`/`.fbx` animation library onto the
generated rig, replacing the procedural cycles. Any rig that names its bones
the Mixamo way works - which is the whole reason the generated rig carries
those names.

The conversion is one line of intent: **apply the source bone's rotation
relative to its own bind pose**.

```
target_world(bone) = source_animated_global(bone) * inverse(source_bind_global(bone))
target_local(bone) = inverse(target_world(parent)) * target_world(bone)
```

That works because the generated rig binds with identity rotations, so the
delta IS the target's world orientation - the payoff for that decision. It also
means a constant transform on the source (the -90° X flip an FBX conversion
leaves behind, a 0.01 unit scale) cancels out of the delta for free, and a rig
with different proportions simply drives the joints it matches.

Two things happen on the way in, and both are the point of doing this at all:

- **Only the bones this rig has are sampled.** A Mixamo clip carries ~65 bones
  including every finger; a 156-channel source clip lands as a 23-channel one.
- **The keys are resampled** (default 15/s, adjustable). Mixamo exports a key
  per frame per bone at 24-30 fps, and the EE evaluates those at runtime.

The hips translation is scaled by the height ratio and rebased onto the
generated bind pose, so a 1.95 m source does not lift a 1.60 m character off
the floor. *In place* strips the horizontal component - the game moves the
character, the clip animates it.

Clip names come from the source, so a merged Mixamo download arrives as
`mixamo.com_1`, `mixamo.com_2`... Rename them in *Tools > Animation Editor*,
which is non-destructive and retargets every reference for you.

## Motion capture from a phone

The same *Import clips...* also takes a **`.tmocap`** - an ARKit body-tracking
recording made by the companion app,
[tyrax-mocap](https://github.com/doctorspider42/tyrax-mocap) (iOS, sideloaded,
its own repo - the sibling of `tyrax-cam`). Point an iPhone at somebody,
record, AirDrop the take, import it. No suit, no markers, no cloud service.

It is the same retarget path a Mixamo download takes, and that is the whole
design: `src/mocap.cpp` decodes the file, renames ARKit's joints
(`left_forearm_joint`) to the Mixamo names the rig uses, and hands over an
ordinary source `Skel`. Nothing after that knows the motion came from a phone.
The 40-odd joints the rig has no bone for - fingers, toes past the ball, face -
are loaded, parent their children correctly, and simply never match, which is
how a 91-joint take lands as a ~23-channel clip.

Two things the file carries that a stream of poses would not:

- **The skeleton's rest pose.** Retargeting is a delta against the source's own
  bind, so without it there is nothing to take the delta against. ARKit
  provides it as `neutralBodySkeleton3D`, and it is written into every take
  rather than assumed.
- **The performer's height**, implied by that rest pose. The hips translation is
  scaled by it, so a 1.9 m performer does not lift a 1.55 m character off the
  floor.

Scale is *dropped* when the matrices are decomposed. ARKit's skeleton-scale
estimation puts the performer's real limb lengths in the local transforms, and
a retarget applies rotations to a body that has its own proportions - carrying
the scale across would stretch the character to match whoever stood in front of
the camera.

What it is honestly good for: this is monocular pose estimation from one
camera. Gross body motion reads well; feet slide, depth wobbles, and
self-occlusion breaks the solve. At 1500 triangles seen from five metres that
is the right fidelity tier. For a close-up cutscene it is not.

## Tools > Mocap: a performer drives a character

Record, AirDrop, import, discover it was wrong, repeat is a bad loop. *Tools >
Mocap* closes it: pick an animated model, pick a source, and the character is
posed **as frames arrive**, in the window's own preview - the Character
Generator's multi-part path, so clothes and hair come along.

Two sources, and the distinction matters less than it looks:

- **A `.tmocap` file**, played back. Scrub it, loop it, watch the retarget.
- **The live phone link.** Start *Tools > Phone Camera*'s link, type its address
  and six-digit code into the app's LIVE LINK row, and the performer moves the
  character in the editor with about a frame of lag. The window binds to the
  phone's skeleton **the moment it arrives** - a phone connects, reconnects or is
  swapped for another one at times nothing announces, so *Rebind* is there for
  when you change the character, not for getting started.

The file source is not a mock of the live one - it *is* the live one with a
different feed. Both end in the same `mocapApplyFrame` → `charanim::applyLive`,
and a streaming feature that only runs when a phone is in the room is a feature
nobody can debug. (The equivalence is measured, not asserted: posing through
`applyLive` frame by frame and posing through the clip path agree to 0.48 µm.)

**Record** writes a `.tmocap`, and only from the live source - a file is already
a take. It buffers the **source** frames rather than the retargeted pose,
because a take is reusable on any character and a baked pose is not; the result
imports through *Import clips...* like anything else. **Recentre** re-zeroes the
root, for when tracking is lost and regained and the performer has wandered.

### What the wire carries

Only rotations, at 30 Hz - about 1.5 KB a frame for 91 joints, a quarter of what
the file format costs, because bone lengths do not change during a take. The
skeleton and its rest pose are sent **once** at connect (`bodyrest`), and a
`body` frame arriving before it is dropped: there is nothing to say which
rotation belongs to which joint, and nothing to take the retarget's delta
against. The phone joins the same server, port and handshake `tyrax-cam` uses;
`body: true` at hello is how `src/phonecam.cpp` tells the two apps apart. The
layout is written down in the phone repo's `PROTOCOL.md`.

### Two things real data broke that Mixamo clips never did

Both were found by importing an actual take and looking at it, which is the
argument for having the live window at all:

- **The performer's height was measured by adding up local Y offsets.** ARKit
  expresses a bone's offset in its parent's *rotated* frame, so the thigh-to-shin
  offset reads `(0.42, 0, 0)` - along the bone, not down. A 1.71 m performer
  measured 0.13 m, the hips translation came back thirteen times too big, and the
  character flew off the top of the screen. Composing the full transform is not
  pedantry here.
- **The two rigs rest differently.** ARKit rests in a true T-pose; the generated
  rig rests in an A-pose with the arm already 40° down. A delta measured from one
  rest pose applied to a body resting somewhere else turned "arms hanging at your
  sides" into arms folded across the chest. `charanim` now computes a per-bone
  `restFix` from the two bind directions. Mixamo libraries never showed it
  because their arms are never straight down.

## What is not here yet

- **Face detail.** The 96 macro targets carry the face's overall character;
  the per-feature targets (nose, chin, ears, mouth - another ~1000 files) are
  not fetched, and at 1460 triangles most of them would not survive anyway.
- **Body proportions and height targets** - see above.

## Code map

| File | Role |
|---|---|
| `src/mhdata.cpp` | readers for the CC0 data (base mesh, targets, proxies, rig, weights). Host-only, no GL. |
| `src/chargen.cpp` | `Params` → `glbparser::Skel`: macro blend, proxy fit, rig, weight transfer, skin bake. Host-only, no GL, no `Project`. |
| `src/charanim.cpp` | procedural idle/walk/run/jump on a Mixamo-named rig, retargeting from an imported library, and host linear-blend skinning for the preview. Host-only, no GL. |
| `src/mocap.cpp` | reads `.tmocap` phone takes into a source `Skel` (ARKit joint names renamed to the rig's), and writes them - `buildSource` is shared by the file and live-link paths. Host-only, no GL. |
| `src/phonecam.cpp` | the link the phone joins: `bodyrest` / `body` messages into `bodySkeleton()` and `drainBodyFrames()`, alongside the camera app's own traffic. |
| `src/app.cpp` (mocap window) | `drawMocapWindow` / `mocapRebind` / `mocapApplyFrame` - both sources end in the same `charanim::applyLive`. |
| `src/gltfwrite.cpp` | `Skel` → `.glb` bytes; the exact inverse of `glbparser::parseSkel`. |
| `src/app.cpp` | `drawCharacterGeneratorWindow` / `rebuildCharacterPreview` / `addCharacterToScene`. |
| `src/viewport.cpp` | `renderCharacterPreview` on its **own** framebuffer (`charFbo_`), sharing `drawToolPreview` with the Tree Generator. |

The first three link into a host harness without ImGui or GL - the
treegen/matbake pattern - which is how the generator was developed and is the
cheap way to test a change to it.
