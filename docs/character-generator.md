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

There is a **Mocap layout** (the *Layout* menu) that opens exactly what a
capture session needs. This window carries its own 3D preview of the character
being driven, so it takes the middle - tabbed with the Viewport and focused -
rather than a side column, which was the first attempt and squeezed the one
thing you actually watch. *Phone Link* is the opposite shape, all controls and
no picture, so it gets a narrow full-height column on the right where the
address and pairing code stay visible instead of hiding behind a tab. Output
runs along the bottom, because during a session the useful diagnostics are
printed rather than drawn.

The Director layout carries *Phone Link* too, since recording a camera move is
the other thing a paired phone does.

A layout remembers its arrangement once shown, so changing the built-in recipe
does not move a layout you have already opened - *Layout > Reset to built-in
arrangement* is what re-applies it.


Record, AirDrop, import, discover it was wrong, repeat is a bad loop. *Tools >
Mocap* closes it: pick an animated model, pick a source, and the character is
posed **as frames arrive**, in the window's own preview - the Character
Generator's multi-part path, so clothes and hair come along.

Two sources, and the distinction matters less than it looks:

- **A `.tmocap` file**, played back. Scrub it, loop it, watch the retarget.
- **The live phone link.** Start the link in *Tools > Phone Link*, type its address
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

**Calibrate (T-pose)** comes first and decides whether any of the rest is
usable. Every frame is a delta from a *rest pose*; without calibrating, that
pose is ARKit's `neutralBodySkeleton3D` - a nominal figure out of a catalogue -
so everything a real performer differs from it by, in proportions and in stance,
is a **constant error in every single frame**. Have them stand in a T-pose
facing the camera and press it: that frame becomes the rest pose, `restFix` is
computed against *their* T-pose, and the height comes off their actual stance.
Bone lengths are kept from the catalogue, because those do not change between it
and the room - only the resting angles do. It sets the heading zero too, since
at that instant they are facing the camera by construction.

A **delay** sits beside it (none / 3 / 5 / 10 seconds), because nobody can press
a button and be in a T-pose at the same instant - with the phone on a tripod
this is the only way to do it alone. Pressing again during the countdown cancels
it. The same button is **on the phone**, and it arms the same countdown.

A **recorded take** calibrates too, on the frame under the playhead - which is
what recording somebody standing in a T-pose is for. Same rule either way: the
rest ROTATIONS are replaced, the bone offsets are kept.

**Zero here** is the smaller one. It says *"the performer is facing me,
right now"*: everything after is measured from that instant, so they turn and
the character turns, they walk across the room and it walks across the room.
Without it the link takes its zero from the **first frame it sees** - whatever
they happened to be doing when tracking caught them, which is rarely the moment
you meant. It is also what to press whenever the stream *jumps* rather than
moves: tracking lost and regained, or somebody else stepping in.

**Rebind** is a different thing that used to sit next to it looking like a pair.
It rebuilds *which bone drives which* - joint matching, rest poses, the height
ratio - and is what you need after changing the character, not after changing
where the performer is standing.

**Record** writes a `.tmocap`, and only from the live source - a file is already
a take. It buffers the **source** frames rather than the retargeted pose,
because a take is reusable on any character and a baked pose is not; the result
imports through *Import clips...* like anything else. 

### What the wire carries

Only rotations, at 30 Hz - about 1.5 KB a frame for 91 joints, a quarter of what
the file format costs, because bone lengths do not change during a take. The
skeleton and its rest pose are sent **once** at connect (`bodyrest`), and a
`body` frame arriving before it is dropped: there is nothing to say which
rotation belongs to which joint, and nothing to take the retarget's delta
against. The phone joins the same server, port and handshake `tyrax-cam` uses;
`body: true` at hello is how `src/phonecam.cpp` tells the two apps apart. The
layout is written down in the phone repo's `PROTOCOL.md`.

### What ARKit does not solve, and how you find out

Point a phone at somebody and four things go wrong at once. Three of them are
one bug and one is not a bug at all, and telling them apart took measuring the
recording rather than staring at the character.

**The body would not turn round.** ARKit keeps the body's heading on the
*anchor*, not on the hips joint: across a nine-second take in which the
performer walked a full circle, `hips_joint`'s own rotation was constant **to
the bit**, while the anchor swung 177 degrees. Both paths threw that rotation
away and kept only the anchor's position, so a performer walking a circle
retargeted as one marching on the spot. The heading is now composed onto the
hips - on decode for a file, in the window for a live frame - and everything
below the hips inherits it for free. The phone sends it as four floats beside
the hips position; `writeTake` stores it, or a recorded live take would lose the
turn all over again.

**The heading has to be RELATIVE.** Composing the anchor onto the hips makes the
character turn, but composing it *absolutely* faces the character in an
arbitrary direction: ARKit's world zero is wherever the phone happened to point
when the session started, so the character came out turned some random angle
away from the camera. It is measured against the first frame seen - exactly as
the hips translation already was - and *Recentre* forgets it along with
everything else. The two halves of the same anchor have to be rebased the same
way, and for a while only one of them was.

**The hands, the head and the feet do not move** - and there is nothing to fix.
Over 277 frames, these joints' local rotations never changed by so much as a
float bit: both wrists, both ankles, both toe joints, and the head relative to
the neck. ARKit reports them, it does not *solve* them. The head still turns,
because it inherits the neck chain (which moves about 10 degrees in that take);
the wrists and ankles follow their parent bone rigidly, which is why a lifted
knee comes with a pointed foot. `mocap::load` measures this per take and says
so, because "the source has no wrist data" and "the retarget is broken" look
identical on screen and are not the same problem.

**The limbs themselves are exact.** Measured, not assumed: the angle between
each of the performer's bones and the character's, after retargeting, is
**0.0 degrees** for every limb across every frame sampled. When a pose looks
wrong, that number is where to start - if it is zero, the character is doing
precisely what the source said, and the source is what to argue with.

### Feet on the floor

Rotations do not know where the ground is. A retarget applies the performer's
joint angles to a body with its own proportions, so the ankle lands wherever
that chain of angles happens to put it - which on a real take meant **147 mm
below the floor**. Add a source that never solves the ankle, and the foot
follows the shin rigidly: a lifted knee comes with a pointed toe, like a dancer.

*Feet on the floor* (on by default, in both the Character Generator's retarget
options and the Mocap window) decides per frame whether each foot is **standing**
- low enough and slow enough - and if it is, puts the ankle back where it was set
down, levels the sole, and bends the leg with a two-bone solve to reach. Leaving
a plant needs a clearly higher foot than entering one; without that hysteresis a
foot hovering at the threshold flickers every other frame. The knee keeps
pointing where the pose already had it pointing, which is what stops the solve
inventing a direction and flipping the joint, and the leg is never allowed to
straighten completely, because at full extension there is no knee direction left.

Measured on a real 9.5-second take, for the foot that was actually on the ground:

| | off | on |
|---|---|---|
| slide while planted (mean) | 36.4 mm | **0.9 mm** |
| deepest through the floor | 146.8 mm | 4.0 mm |
| sole off level while down (mean) | 37.3° | **1.0°** |

The peak sole angle stays at 61° because that is the single frame of first
contact, before the ease-in has run - which is the intent, not a shortfall.

Two boundaries worth stating. The solve runs on the **retarget** path, so it
covers Mixamo imports, `.tmocap` files and the live link, but **not the
procedural idle/walk/run/jump**, which are authored analytically by a different
function; those measure 22.7 mm of slide and 20 mm through the floor, and
running a stateful plant over a clip that has to loop seamlessly is a way to
break a working feature for a gain nobody can see at PS2 range. And a foot the
performer never puts down never plants - correctly. In the take above the left
foot stayed bent the whole time and the character's left foot never came within
94 mm of the floor; the hips bob 177 mm in that recording, which is enough for a
23 mm difference in leg extension between the two sides to become a 240 mm
difference in how low each ankle ever gets.

### Taking the shake out

Monocular tracking re-estimates every joint from scratch each frame, so a
performer standing perfectly still arrives **shimmering** - and a retarget
faithfully passes that on to the character. Averaging fixes it and ruins
everything else: smoothing strong enough to settle a still hand puts visible lag
on a punch.

*Smooth the shake* uses a one-euro filter, whose whole idea is that **the cutoff
rises with speed**. Slow movement is mostly noise, so it filters hard; fast
movement is mostly signal, so it gets out of the way. One knob set, no mode
switch, and the two failure modes trade against each other instead of fighting.

Measured against a known signal with 2.5° of joint noise:

| | jitter | error | lag |
|---|---|---|---|
| standing still, off | 1.85° | 1.22° | |
| standing still, **on** | **0.48°** | **0.58°** | |
| slow gesture, off | 3.00° | 1.22° | 0 fr |
| slow gesture, **on** | 2.38° | 2.79° | 1 fr |
| fast punch, off | 26.39° | 1.22° | 0 fr |
| fast punch, **on** | 25.73° | 4.20° | **0 fr** |

Standing still gets four times calmer *and* twice as faithful; a fast gesture
pays three degrees and no lag at all. The settings are not taste - they came out
of a parameter sweep scored the way an eye weights the defects, because the
first attempt (weighting jitter and error equally) scored the filter barely
better than doing nothing and said more about the weights than about the filter.

One number in the textbook one-euro is wrong for a body: the **derivative
cutoff**, which smooths the speed estimate that opens the main cutoff. At the
standard 1 Hz - tuned for a mouse pointer - it cannot follow a 2 Hz gesture, so
the cutoff never opens in time and the filter sits 18° behind a punch. It is 3 Hz
here.

Anything that smooths across frames has to be told when the stream *jumps*
rather than moves, so **Recentre** clears the filter and the Vision tracker
along with the root - otherwise the character is dragged through the gap instead
of cutting across it.

### The head and the hands: a second opinion

The joints ARKit reports and never solves are not a dead end. Hand and face
tracking on iOS live in **Vision**, a different framework, and it runs perfectly
happily over the same camera frames the body tracker is already producing. The
app now runs it at 12 Hz and sends what it sees; `src/visionpose.cpp` turns that
into rotations for `head_joint` and the two wrists, writes them into the source
frame, and the retarget downstream never learns a second framework was involved.

**The phone sends observations, the editor solves.** That split is the whole
reason the geometry is C++ and not Swift: it can be tested here against
synthetic data with no device in the loop, and getting a convention wrong costs
an edit rather than a build, a tag, an AltStore round trip and a reinstall.

It took three rewrites, and each came out of the harness rather than a hunch:

- **Matching directions does not work.** Two projected directions are two
  constraints on three unknowns, and a whole family of orientations projects
  identically. Nine synthetic cases, eight wrong, by 24 to 166 degrees. The
  missing third constraint is *foreshortening* - a palm turned away projects
  shorter - so the fit uses the vectors with their lengths and solves for the
  single unknown scale. That is also why no camera intrinsics are sent: only the
  ratio matters, and distance and focal length cancel.
- **A plane cannot be told from its mirror.** The wrist and three knuckles are
  coplanar, so two poses always fit equally well and no pixel accuracy separates
  them. The **thumb** sits off that plane, which is the only reason it is on the
  wire. With it, five failing cases became one.
- **The rest-pose tie-break had to be ten times weaker.** At its first weight it
  dragged correct answers home by 8 to 19 degrees. It only has to separate poses
  that are genuinely indistinguishable.

Measured, on synthetic data with a known answer:

| | error |
|---|---|
| clean geometry, camera anywhere | **≤ 1.2°** |
| realistic landmark noise (~3 px on a 90 px hand) | 4.8° |
| a small hand (0.5% of frame) | 10° |
| 1% of frame and beyond | breaks - the mirror wins |

Frame-to-frame tracking earns its keep at the noisy end: at 0.5% noise it takes
the mean from 11.1° to 7.7°, the worst case from 95.7° to 49.5°, and **jitter
from 15.8° to 6.8°** - which is the part you see.

**When it does not work, find out which thing is broken before fixing any of
them.** Three faults look identical on a character - Vision detecting nothing,
Vision detecting while the geometry is wrong, and geometry right with an axis
convention flipped - and each is a different scale of work. *What Vision is
seeing* in the Mocap window shows the raw numbers: whether a face or a hand was
found at all, the palm's size as a percentage of the frame (below a few per
cent the landmarks are noise and the solve is guessing), the angles Vision
reported, and the angles the solver produced from them. *Log every frame to a
file* writes `vision-log.jsonl` in the project folder for going over a session
afterwards rather than reading it off a screen.

What it needs is size in frame. A face across the room is plenty; a hand at four
metres is about ninety pixels. Step closer and the wrists come alive. The Mocap
window shows how many joints Vision is actually driving, and hovering that
number says why the others are not.

### The 90-degree pelvis

The correction above nearly ended the feature. A performer standing perfectly
still, arms out, came through with the legs crossed and the torso wrung out -
in the owner's words, like a twisted gut. It reproduced identically from a live
link and from a 1.6-second recording in which nothing moved by more than two
degrees, so it was not the performance.

Measuring where each bone POINTS at rest, on both rigs, found it in one line:

| bone | character points | source points | apart |
|---|---|---|---|
| **Hips** | (0.00, 0.94, -0.34) | (-1.00, 0.00, 0.00) | **90.0°** |
| Spine2 | (0.00, 0.99, 0.11) | (0.00, 1.00, -0.07) | 10.3° |
| LeftArm | (0.66, -0.75, -0.01) | (1.00, 0.00, -0.01) | 48.9° |
| LeftUpLeg | (0.10, -0.99, 0.08) | (-0.00, -0.99, 0.10) | 5.9° |

"Hips → Spine" points **up** on the generated rig and **sideways** on ARKit's,
because the two express a root frame differently - not because anybody is posed
differently. The correction dutifully rotated the pelvis 90° and held it there,
while the legs kept their own near-identity correction, and the result was a
body wrung around its own waist.

The hips are now excluded from it, and the reason generalises: **the root has no
bone direction to correct.** Its orientation *is* the body's, which the delta
already carries. Everything below it is a real bone with a real direction, and
there the correction is doing exactly the job it exists for - the arms measure
49° apart, which is the genuine T-pose-versus-A-pose difference.

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
| `src/posefilter.cpp` | the one-euro jitter filter over a frame of joint rotations. Host-only, no GL; the sweep that set its constants is scratchpad/filter_check.cpp. |
| `src/visionpose.cpp` | head and wrist orientation from Vision's landmarks - the geometry the phone deliberately does not do. Host-only, no GL, harness-tested against synthetic poses. |
| `src/phonecam.cpp` | the link the phone joins: `bodyrest` / `body` messages into `bodySkeleton()` and `drainBodyFrames()`, alongside the camera app's own traffic. |
| `src/app.cpp` (mocap window) | `drawMocapWindow` / `mocapRebind` / `mocapApplyFrame` - both sources end in the same `charanim::applyLive`. |
| `src/gltfwrite.cpp` | `Skel` → `.glb` bytes; the exact inverse of `glbparser::parseSkel`. |
| `src/app.cpp` | `drawCharacterGeneratorWindow` / `rebuildCharacterPreview` / `addCharacterToScene`. |
| `src/viewport.cpp` | `renderCharacterPreview` on its **own** framebuffer (`charFbo_`), sharing `drawToolPreview` with the Tree Generator. |

The first three link into a host harness without ImGui or GL - the
treegen/matbake pattern - which is how the generator was developed and is the
cheap way to test a change to it.
