# The flashlight

A **Player** object can carry a torch: *Properties > Flashlight* on the player,
with a colour, a reach, a cone half-angle and an optional pad button to switch
it on and off. The `Set Flashlight` flow node is the master switch — a project
can start dark and hand the torch over later.

It reaches the world in two ways at once, and the difference between them is
worth knowing before you tune anything.

## The two halves

**The cone** is a fill. The engine's spot light (`setSpotLight`) is evaluated
**per vertex on VU1** — a cone term and a distance falloff added on top of the
baked vertex colours, with no N·L term (the colour pipelines carry no normals).
It is what lights the small things the pool cannot reach: props, trees, the
avatar.

It is deliberately dimmer than the pool (0.7 of the torch's colour) and given a
much **softer edge**, because on a coarse mesh that edge *is* the artifact. The
term is evaluated per vertex, so a crisp cone edge crossing a triangle metres
wide shows up as that triangle, and a model with few of them reads as a bag of
bright shards. A wide gentle ramp spreads the same change over enough geometry
that the interpolation stops being visible; the price is a fuzzier beam edge,
which is what a torch beam has anyway.

It is only ever as fine as the mesh it lands on, which is why **the terrain does
not take it at all**. A terrain cell is never smaller than one world unit, so on
the ground that term is not a cone: it is a Gouraud diamond that moves in
cell-sized steps, and a footprint smaller than one cell — looking at your own
feet — lit nothing whatsoever. The ground's light comes from the pool below
instead. Running both is the worst of the two, a soft ellipse sitting inside a
blocky wedge, so a project with a flashlight sets `spotLit = false` on its
terrain bags and one without is untouched.

Two consequences worth knowing:

- **A bright torch flattens what it touches.** With no N·L, every vertex inside
  the cone gets the same addition whichever way its face points, so a light
  colour near white saturates a prop into one flat silhouette. Keeping the
  colour around 0.6 leaves the baked moonlight shading showing through, and the
  prop keeps its shape. This is a real limit of the colour pipelines, not a
  tuning accident — it is also why the terrain and any wall the pool lands on
  take no cone at all.
- **The pool's brightness is aimed, not authored.** Its additive gain is solved
  for a peak of 0.8 from whatever colour the project set, so the middle never
  clips — see the gobo section.

**The pool** is the picture. Where the beam lands, the game drops one additive
patch and takes its texture coordinates from the **beam's own frustum**. So the
shape of the light is a *texture*, per pixel, and the ground's tessellation has
no say in it. This is the half you are looking at when you shine a torch at the
floor.

It is a real projection, not a decal with the right picture on it. The patch
does not carry finished `u,v` — it carries the projection's **numerator and
denominator**, because VU1 emits texture coordinates scaled by each vertex's
`1/w` and the GS divides `S/Q` per pixel:

```
S = 0.5 * fwd + k * (e · right)     k   = 0.43 / tan(halfAngle)
T = 0.5 * fwd - k * (e · up)        fwd = e · forward   (e = point - lens)
Q = fwd
```

Both `S` and `Q` are affine in the world position, so interpolating them across
a triangle is exact and `S/Q` per pixel is the true projective mapping. Finished
`u,v` would be exact at the vertices and wrong between them — which reads, on a
patch of a few dozen triangles, as a *fan of triangles* rather than a pool of
light, however finely you cut it.

## A scene light with the same trick

A dynamic point light can opt into a **Spot** style (*Properties > Point
light > Spot (cone)*, dynamic lights only): the light becomes a cone pointing
down the object's local -Y - unrotated it shines straight down, a ceiling
lamp or a street light; the rotation gizmo aims it. Nearby meshes take the
cone per vertex through the same engine slot the torch uses, and the
footprint on the ground is the flashlight's projection on a scene light: the
pool patch takes the gobo's projective STQ from the LIGHT's own frustum
instead of the round corona, so a lamp's pool is shaped per pixel however
coarse the ground is. It follows *Set Light*, flicker and Move Object like
any dynamic light, and pairs naturally with the visible beam (*Beam: corona*)
- night-walk's street lamp is the demo. Spot lights do not take part in the
torch's shadow machinery: no receivers, no volumes - one torch is the
per-pixel protagonist, a scene spot is set dressing that finally lights its
own street.

The corona itself is a depth-tested additive billboard, and two details keep
it clean on the fixture that carries it. It is drawn **pulled toward the
camera** (a quarter of the light's radius, capped at three quarters of the
camera distance, its size scaled down by the same fraction so the picture
does not move): a sprite centred exactly on the bulb slices through the
lamp's own pole and arm, and the GS's fixed-point z cuts the soft glow on a
jagged, stair-stepped seam that wanders as you move. Pulled clear, the glow
blooms **over** the thin fixture - which is what a glow does in a real lens -
while a wall between you and the lamp still hides it. And it bakes at
**128x128** (the 2D lens-flare sprites stay 64): up close the billboard can
cover a third of the screen, and a 64-texel radial gradient contours in
visible steps at that magnification.

**The editor viewport draws both halves of a beam** — the corona with that
same pull, and *Beam: corona + shaft*'s eight-segment cone — from the same
sprite bake and the same numbers, so a lamp is placed against the picture the
console will draw rather than against an unlit marker. It draws in every
shading mode, because a beam is geometry the game submits and not a
simulation of how the console shades. One thing it deliberately leaves out:
the runtime **level** the console multiplies the beam by. Flicker, *Set
Light* and a streamed-out light are all runtime state, so the viewport
previews a beam at its authored brightness, exactly as it previews the light
itself — a glow pulsing over a rock-steady pool of light would be a new lie
rather than less of one.

## What the pool does

- **Follows the beam.** The patch is laid out along the beam's run across the
  ground, not axis-aligned, because a beam meeting the floor at a grazing angle
  reaches much further than it is wide. What you see is an ellipse that
  stretches as you lower the torch, and a circle when you point it straight down.
- **Follows the relief.** On terrain, each patch vertex sits on the ground under
  it, so a pool crossing a ridge bends over it.
- **Lands on what you built.** Placed floors, platforms, walls and props take the
  pool as well as the terrain, so a torch works in a room made of geometry —
  including in a scene with [no terrain at all](terrain.md), which had no pool
  before.
- **Fades honestly.** It dims with distance, and it dims at grazing angles: the
  same cone spread over four times the ground really is four times weaker per
  square metre.

**The pool is never lit by anything, including the torch that owns it.** It *is*
the light. That sounds obvious and was the single most expensive bug on the way
here: opting a bag out of the scene's dynamic lights leaves it taking the
*global* flashlight, so VU1 was drawing the per-vertex cone straight across the
projected pool, on a patch whose vertices are metres apart. It looks like hard
diagonal seams following the patch's triangulation, and it survived a fix for
the texture mapping, one for the near plane and one for the depth test — because
it was none of those. Overlays that ARE light or shadow (the pools, the blob
shadows, the projected shadows) set `spotLit = false` for this reason.

**The patch reaches as far as the beam does.** The footprint of a beam runs away
as it flattens — the lower edge of the cone meets a level floor further and
further off until it never does — and a patch that stops while the beam is still
lit cuts the pool along a straight line across the ground, which is the one
artifact that reads as *wrong* rather than as *cheap*. So the patch is stretched
to cover that distance, bounded by the light's own reach (past which there is
nothing to draw) and by a fill-rate backstop.

The brightness then falls off with reach and with the grazing angle — the same
cone spread over four times the ground really is four times weaker per square
metre — and **both of those are monotonic in where you point**, which matters
more than it sounds. An earlier version faded the pool by how much of its
footprint it managed to cover instead, and that quantity is *not* monotonic: the
distance needed explodes as the beam's elevation approaches the cone's
half-angle, while the patch itself keeps growing with the landing distance. The
result was a band a few degrees wide where the pool vanished completely and came
back beyond it. A fade that is not monotonic in the thing the player is moving
is worse than the artifact it hides.

**It lands on solid geometry the era's own way.** Shine the beam at a wall, a
shed, anything solid, and the object the beam meets is rendered a **second
time**, additively, with the gobo's projective STQ per vertex — so the light
lands on its *real triangles*, per pixel, whatever their count or orientation.
This is the same pattern as the reflective env pass and the emissive atlas
pass — one more very simple GS pass over geometry that is already there — and
it is, as far as the surviving accounts go, how the PS2 survival-horror games
did their torch: the GS grinding many trivial passes. Nothing about the
projection changes for it — `goboST` is a function of the world position alone.

Three things worth knowing about how the receivers are found and drawn:

- **The light budget is SHARED between receivers, and it is split fairly.**
  The second pass has a 3997-vertex ceiling per frame, and it used to be first
  come first served by distance: one detailed model in the beam filled the
  whole buffer and every receiver behind it got **nothing** - no torch light on
  the wall two metres past it, and therefore no shadow on that wall either,
  since a mask can only darken light that is drawn. ("I shine at the robot and
  the light on the wall disappears", measured as `recv[0] sliceVerts=3999,
  recv[1] sliceVerts=0`.) Each receiver gets an equal share now, plus whatever
  the ones in front of it did not use - so a heavy model lights *partially*
  (its far triangles keep the cheap per-vertex cone) rather than taking the
  wall's light with it. The wall is what the pool is for.
- Receivers are **everything the cone touches** — the nearest three solids whose
  oriented boxes (the same rotated basis an [Area](areas.md) uses) intersect the
  beam — not merely the object the beam hits. That distinction closed two
  reports: the wall *behind* a caster stayed dark, so a shadow had nothing to be
  carved out of; and a shed with the beam at its *feet* took no projected light
  at all and fell back to the per-vertex cone — the old hard triangles, on the
  very surface this pass exists for. All receivers draw from one bag.
- The second pass draws at **equal depth** with no bias — same world-space
  floats, same identity matrix — so it never paints over anything standing in
  front of the receiver, and never z-fights it.
- Animated models and physics bodies are not re-rendered (skinned buffers and
  local-space vertices respectively); they are small, and the cone covers them.

One era artifact comes with the era's method: the pass has no shadows of its
own, so a beam on a hut's outer wall also lights the *inner* face of that wall
for someone standing inside. The era’s games spent their shadow volumes on
exactly this; here it is simply the deal.

**Both patches are live at once.** A beam sweeping off the floor and up a wall
lights the two together for as long as it straddles the join, so the flashlight
carries a floor patch and a wall patch and draws both; the depth buffer decides
where each one shows, and their own falloffs retire the one that no longer
matters. One patch that had to belong to one surface or the other made that
moment a blink — the floor pool vanished and a wall pool appeared in the same
frame.

**And a big flat box never takes the per-vertex cone** — nor does whatever
object the beam is currently on, once its hit face is wall-sized. The spot is evaluated at a box face's four corners and
Gouraud-interpolated between them, and both of its terms vary over metres — so on
anything wall-sized it draws a hard diagonal of light across the face, one
triangle bright and the other not. A wall does not want that term at any time:
it is lit by the pool when the pool is there, and by the moon when it is not.

The rule is a property of the object, not of the aim:

- **Box primitives only.** A model's bounding box says nothing about how its
  surface is tessellated, and a baked scatter chunk has an enormous one — a
  whole grouping cell of trees — so a size test applied to models would strip
  the cone from a whole forest.
- **Larger than about 1.4 units** across its biggest face. Below that the cone
  is kept, deliberately: the pool lands on ONE face, and a crate lit all over
  reads better than a crate with one bright patch and four black sides.
- A **statically batched** wall is drawn from a merged bag it may share with
  other objects, so the switch can only be thrown when that batch holds nothing
  but that wall (the usual case for a big lone one: batches group by cell and
  material). A wall grouped with company keeps its cone, because one torch must
  not darken everything batched beside it.
- **It is one patch of 3x3 cells** (54 vertices). With the mapping exact per
  pixel the grid no longer draws the light at all — it only decides which ground
  the pool *covers* and how closely it follows the relief — so it is deliberately
  coarse. It also sits right under the camera, where most of its triangles cross
  a frustum plane and the EE clipper replaces each with up to two: a bag that
  outgrows one VU1 package drops the overflow, and a dropped triangle is a hole
  in the middle of the light.

## The gobo

The pool's texture *is* the shape of the light. The editor bakes one into
`res/hud/flashlight-gobo.png` (128x128) for any project that can show a
flashlight: a hot centre, a soft penumbra, the faint ring a dish reflector
throws, and two low-frequency lobes so the circle is not perfectly round. It
costs about 6% of the console's ~1.08 MB texture heap ([GS VRAM](gs-vram.md)),
and a project with no flashlight never loads it.

The pool's gain is solved so its peak lands at 0.8, not at 1.0. That matters
more than it sounds: the blend is `Cs*FIX/128 + Cd`, and a peak at or over 1.0
clips — at which point the outline of the clipped region is the patch's own
piecewise-linear texture mapping, i.e. straight segments and corners where its
cells meet. A clipped pool looks *ragged*, and dimmer, because the falloff
around its core is the part that got eaten.

To use your own, set *Properties > Flashlight > Pool texture* to a PNG in
`res/hud/`. Rules:

- The pool is an **additive** bag (`Cs*FIX + Cd`), so the shape lives in **RGB**
  and alpha is ignored. A white image draws a bright rectangle.
- Keep the outer border **black**. The texture is bound with GS **CLAMP** (the
  pool's STs genuinely leave 0..1 — the default REPEAT would draw the beam a
  second time beside itself), so everything outside the frustum samples that
  border.
- The image is a real gobo: a cross, a cracked lens or a grille shape maps
  through the beam and lands on the ground as that shape.
- Power-of-two sides, like every PS2 texture.

## Tuning

| Symptom | Knob |
|---|---|
| The lit circle is too small / too wide | *Cone half-angle* — it sizes both halves |
| The pool dies too close | *Reach* — the fade is measured against it |
| Props look like flat white cutouts | *Light colour* — the cone has no N·L, so a near-white torch saturates them |
| Nothing lights at all | *Enabled* is the master; a `Set Flashlight` node may have turned it off |
| The beam has no shadows worth seeing | *Held right* / *Held below eye* — see below; at 0,0 the light is your eye |

## Off the eye

A first-person torch sits, by default, exactly where the camera is. That is the
cheapest thing to compute and the worst thing to look at: **a light on the view
axis lights precisely the surfaces it hides.** Its shadows fall behind their
casters, where you cannot see them, and every prop is lit flat-on with no
modelling at all.

*Properties > Flashlight > **Held right** and **Held below eye*** move the
light's origin off that axis, in world units — a torch in a hand rather than in
an eye socket. **0,0 is the default and returns the eye exactly**, so nothing
moves in an existing project until you ask for it. About **0.2 right and 0.3
down** reads as hand-held; the value is clamped to a metre either way, past
which it is a lamp on a pole and the per-vertex cone (a coarse fill that has
always been eye-shaped) stops agreeing with the pool.

Three things follow, and the middle one is the reason people ask for this:

- **the pool moves with the light.** The beam still AIMS where you look — a
  torch is pointed, not converged — so an origin 0.25 units down puts the lit
  ellipse 0.25 units nearer your feet at every distance. That is the offset
  doing its job, not drift.
- **shadows get somewhere to fall.** With the light off the axis, a caster's
  shadow is thrown clear of the caster instead of hiding behind it. Measured on
  a barrel three metres out: dark pixels inside the lit pool went from **896**
  (light in the eye - two slivers at its edges) to **5994** (0.2 / 0.25 - a
  shadow you can see).
- **props gain a lit side and a dark side.** The top of a barrel lit from below
  the eye is no longer the brightest thing in frame, which is most of what
  "torchlit" looks like.

The offset is taken in the **beam's** frame, never the world's: right is the
beam crossed with world up, and down is the beam's own up negated. So the light
never slides ALONG the beam - which would quietly change its reach, and could
drop it PAST a near caster, where a shadow volume points back at the eye and
z-pass counting is wrong. Aiming straight up or down leaves "right" undefined;
there the offset stops meaning anything and the eye is used unchanged (verified:
a vertical beam still lands its pool).

One origin feeds everything the torch does - the projection that shapes the
pool, the receiver collection, the wall hit, the march that lands the pool, the
shadow volumes, and the per-vertex cone in both game templates. What stays the
CAMERA on purpose is the aim direction, the receiver height cap (a wall taller
than the player is not a floor) and the shadow volumes' front/back
classification, which is a question about the eye rather than about the light.

## What the editor shows

The viewport previews the **cone**, per pixel, with the exact formula VU1 runs.
It does not draw the pool. So the editor is a good guide to reach, angle and
colour, and the console is where you judge the gobo — the two are answering
different questions rather than disagreeing.

## The shadow

A caster in the beam throws its silhouette **away from the player** — onto the
ground, and onto the wall behind it — and the shadow swings with every step and
every turn, because the light *is* the player. This is the
[projected-shadow](ambient-occlusion.md) machinery with the torch as one more
candidate light source: the caster's silhouette is rendered from the torch's own
position into a shadow-map slot, the ground patch samples it as before, and the
wall behind the caster is re-rendered — the same second-pass trick as the light —
with the silhouette sampled through the light's view-proj, exactly per pixel.

**Both modes need the torch to be OFF THE EYE to show anything** ("Off the
eye" above). A light on the view axis lands its shadow exactly behind its
caster on screen: the volume mode leaks a rim, and the silhouette mode - which
draws the *right* shape - draws it precisely where the caster hides it, so it
looks like nothing at all while quietly taking the slot the moon had been
using. Set *Held right* / *Held below eye* first, then judge either mode.

**The technique is a project setting** (*Preferences > Rendering > Flashlight
shadow volumes*), because the two answers trade different things:

| | Silhouette slots (default) | Shadow volumes |
| --- | --- | --- |
| Shadow shape | the caster's **mesh**, rendered from the torch — soft-edged, the same picture the sun's shadows make | the caster's **mesh**, silhouette-extruded (a model past 1200 triangles casts from a **decimated shadow proxy** the build bakes for it) — but a **primitive always extrudes its BOX**, which is why a sphere casts a hard-edged rectangle here |
| Who occludes | objects with *Cast shadow (projected)*, nearest four | **every solid in the beam**, no flag, no limit |
| Occlusion | patches on ground and wall; light still leaks through unflagged solids | **exact per pixel** against the real z buffer |
| Cost | four 64×64 silhouette renders | the volume fill each frame + a count band in GS VRAM: 512 KB at 32-bit colour, 256 KB at 16-bit |

Volumes are the survival-horror era's own arrangement, on its own hardware
trick. Each occluder is extruded away from the torch into a closed volume — a
model from its **real triangles** (the lit faces, pushed 5 cm down their rays,
are the near caps; their projection at the light's range the far caps; and the
silhouette edges, where a lit and an unlit face meet, become the extruded side
walls — an *open* edge, and these models are not watertight, silhouettes
whenever its one face is lit). A primitive extrudes its box. A model past
**1200 triangles** is too dear to classify on the EE every frame, so the build
bakes it a **shadow proxy**: every part welded together by position (a shadow
has no uv seams), decimated with the same quadric collapse the mesh LODs use
but with open borders free to slide along themselves, down to under the
budget, and stored positions-only in the `.tmdl` (~40 KB for a 1200-triangle
proxy; only baked while the preference is on). A 6194-triangle rifle casts
from 877 triangles and its shadow still shows the sight, the grip and the hole
in the trigger guard - where it used to cast a hard rectangle, its bounding
box. Only a model the decimator cannot bring under the budget (the build log
says so, `[model bake] ...shadow proxy could not be decimated`) still falls
back to up to three tight sub-boxes (median split, then leaves merge back
wherever splitting bought nothing).

Such a volume is thoroughly **concave** and overlaps itself constantly, which
is exactly what 1-bit destination alpha cannot express: the GS cannot COUNT in
the alpha channel — blending never writes A — so a set/clear order over a
concave volume lies. The era's answer, reproduced here: the GS *can* add and
subtract in **color** channels. The volume's camera-front faces add +32 and
its back faces subtract it, both plain TestOnly z against the scene's depth,
into a dedicated 32-bit count target that shares the scene's own z buffer.
Any pixel the light cannot reach ends net-positive; everything else returns to
exact zero, whatever the overlap count.

**Why that target is 32-bit, and why it is a BAND** — the most expensive thing
this feature knows, and it was found on a physical console after every
emulator test had passed. A GS colour buffer and the z buffer it is
depth-tested against must share **page geometry**: 32- and 24-bit pages are
64×32 pixels, 16-bit pages are 64×64. The count target started out PSMCT16
(to fit VRAM) over the scene's 32-bit z, and on hardware the depth comparison
then read shifted words for half of every page — the torch's light landed in a
**checkerboard of 32-pixel tiles** wherever a volume was counted. PCSX2
addresses each buffer from its own PSM, so it showed nothing at all, at any
vantage, ever.

A full raster at 32 bits is 1 MB, which no project can spare, so the target is
a **band** (256 rows × the raster width = 512 KB, exactly what the broken
16-bit full-raster target cost) and `FRAME.FBP` is slid by whole page *rows* so
the band covers the volumes' screen rect. `ZBP` never moves, which is what
keeps the 1:1 correspondence with the scene's depth exact; the band's first row
must be a multiple of its own page-row height for the slide to be expressible.
A shadow region
taller than the band is counted band by band — the mask is an OR, so the bands
compose and a tall shadow costs fill, not coverage. (The resolve of a slid band
samples the target at its **own** base with `V = y − bandY0` — it used to bind
the texture to the slid base *and* subtract `bandY0`, which for every band but
the first read the memory below the band instead of the band; found by reading
the address arithmetic, and invisible in a first-person torch because a shadow
missing only in the bottom half of the screen reads as the torch's own
"hides behind its caster" rule.) Then **one resolve pass per frame**
samples the count target as a texture with `TEXA.AEM = 1` — an all-zero texel
expands to alpha 0, anything else to 0x80 — and ORs *count > 0* into the
framebuffer's destination-alpha MSB through an alpha test. Counting is exact
over any pile of volumes, so every caster lands in ONE bracket — one clear,
one resolve, scissored to the volumes' projected screen bbox, and the far
caps are skipped outright (they only ever subtract at pixels beyond the
light's range, where the reach falloff has already taken the light to zero).
Each of those three was measured, not assumed: per-caster brackets with
full-raster clears and resolves halved PCSX2's software renderer on the
night yard's four-caster vantage (50 → 25 FPS); the scissor, the folded
single-drain bracket entry and the dropped far caps put it back at 50.
Every torch light pass then
draws with the GS's destination-alpha test (`TEST.DATE`), only where the mask
says lit. The mask gates *light*; nothing ever paints darkness. Stand behind a
crate and the torch genuinely does not reach you.

Two geometric facts shape the implementation. **The torch sits exactly in the
eye**, and a light in the eye casts shadows exactly hidden behind their own
casters — so the volumes extrude from a *virtual* torch pushed a short way
down the beam (5% of the range, clamped to 0.5–2 units): the hand-held
parallax that makes every shadow diverge and show around what casts it, the
same reason the silhouette mode projects with a wider FOV. And **face
orientation is geometric, never winding-trusted**: caps orient toward/away
from the light, side walls away from an interior sample — a model with flipped
winding degrades to casting from its back faces, whose silhouette is the same.

**It works at either colour depth, and getting there cost two more GS rules.**
The first is `FBA`, the GS's "alpha correction": with `FBA = 1` the hardware
forces the **MSB of every alpha it writes to 1**. That is a convenience for
1-bit-alpha targets and death to a mask that lives in exactly that bit - the
per-frame clear wrote alpha 0, the GS stored 1, `TEST.DATE` read *shadow* over
the whole raster, and every DATE-gated torch pass was discarded: in a 16-bit
project the projected pool simply did not draw. Who sets it is known now:
**ps2sdk's `draw_setup_environment` programs `FBA = 1` for a 16-bit frame
PSM** (disassembled from `libdraw.a` — the register at `0x4A + context` gets
`(psm & ~8) == 2`, i.e. `PSMCT16`/`PSMCT16S` — and 0 for a 32-bit one), which
is why only 16-bit projects ever saw it. The engine zeroes it right after that
call (`RendererCoreGS::initDrawingEnvironment`, the same shape as the REPEAT
re-assert) and **both** mask brackets re-assert it at the top of the frame —
the counting path's `maskClear()` and the convex path's `begin()`. That second
one mattered: the first fix went into `maskClear()` alone, so the pool came
back for exactly as long as counting was allowed at 16-bit and vanished again
the moment the count target was refused there and the convex path took over —
the per-vertex cone still lit the props (it is not DATE-gated), the ground and
the walls stayed dark.

The second:
The mask writes have to touch alpha and nothing else, and `FBMSK`'s bit
positions are **always 32-bit RGBA8** - R 0..7, G 8..15, B 16..23, A 24..31 -
whatever PSM the framebuffer is in; the GS maps them onto a 16-bit target's
5551 layout itself. Reasoning from the 16-bit PIXEL layout instead (two pixels
per word, alpha at bit 15 of each half) gives `0x7FFF7FFF`, which exposes bit
15 - the top bit of GREEN - so every "alpha only" write also halved green
wherever the torch lit something and a 16-bit project came back magenta,
(208, 56, 144) measured on a warm cream lamp post. One constant, `0x00FFFFFF`,
is correct at both depths.

**And it can take the last of the texture heap, which does not look like a
VRAM problem at all.** The band is 512 KB at 32-bit colour, and a project in a
512x512 display mode has about that much heap in the first place - so switching
the volumes on can leave the textures with nothing, and the symptom is not a
missing shadow: it is every texture in the scene evicting and re-uploading
*once a frame*. Measured on a hand-made scene (two models, a wall, PAL full
height, 32-bit colour): `VRAMSTAT` reads **0.375 MB free and 0 re-uploads**
with the volumes off, **0.000 MB and ~1.6 re-uploads per frame** with them on.
Preferences warns beside the switch when the band is most of what is left; the
authoritative number is the game's own `VRAMSTAT` line in `bin/log.txt`. The
cheapest fix is 16-bit colour, which halves both the display buffers and the
band.

If the count target's VRAM is refused (it is claimed at boot, right after the
projected-shadow slots, and `allocateBuffer` refuses rather than evicts), the
volumes fall back to the convex sub-boxes with the old 1-bit set/clear — one
bracket per convex piece, a sliver artifact where pieces overlap.

**Counting works at BOTH colour depths, and the one release where it did not
was a misdiagnosis worth writing down.** In a 16-bit project the resolve laid
**dashed green marks down two fixed screen columns** over whatever the torch had
lit, standing still in screen space as the camera moved. It was bisected to that
one pass (forcing its alpha test to fail cleared a sweep), and from there the
blame went to the *masked write at a `PSMCT16` destination* - so counting was
refused at 16-bit and the volumes fell back to convex boxes.

**That blame was wrong, and two console measurements say so.**

The first is a **probe with no shadows in it at all**: the same flat sprite
drawn into a `PSMCT16` frame four times, through `FBMSK` `0xFFFFFFFF`,
`0x00FFFFFF`, `0x7FFF7FFF` and `0`, each rect beside the next, plus a strip per
mask whose alpha is cleared, half-set and then revealed with a `DATE`-gated
sprite. On the console it reads **exactly as it does in PCSX2**: `0x00FFFFFF`
leaves the colour untouched and its alpha half reaches the mask bit per pixel.
The mask constant is right and the destination format is innocent.

The second is a **paired sweep one knob apart**, eight vantages of
`examples/night-walk` at 16-bit colour, same pad script, fresh boot per arm:

| `countResolve()`'s `TEX0` base | frames with green | pixels |
| --- | --- | --- |
| the **slid** band base (what shipped until 1.59.1) | **8 of 8** | 800-4 800 |
| the band's **own** base (the fix) | **0 of 8** | 0 |

Flipping the knob back brings them straight back (A-B-A), and the columns are
the ones the field screenshot showed, to within two pixels. PCSX2 shows nothing
in either arm at any vantage tried - a hardware-only symptom, which is what made
the first diagnosis so easy to get wrong.

The reading fault was **double compensation**: the count pass writes pixel
`(x, y)` through a `FRAME` slid by `bandY0` worth of page rows, so that pixel's
texel lives at the band's own base with `V = y - bandY0`. Binding the texture to
the slid base *as well* made every band but the first sample `bandY0` rows
**below** the band - at 16-bit, 256 KB below: the top of the scene z buffer and
the projected-shadow slots.

**One half of it is still open**: why garbage texels sampled by a pass that only
writes alpha end up *tinting* pixels at all. The marks also appear above the
band boundary, where the slide is zero, so the mechanism is not simply "band 1
reads rubbish". What is settled is that they follow that one register and
nothing else, 8 of 8 against 0 of 8. If they ever come back, start there.

So `allocateCount()` runs at either depth again: the band is `PSMCT32` and
512 KB at 32-bit colour, `PSMCT16` and 256 KB at 16-bit (its page geometry
follows the z buffer, as above), and a 16-bit project gets mesh-shaped shadow
volumes like any other. Verified on the console: eight vantages clean, VRAM
2.18 MB, pools and shadows drawing.

**And then the band's own format ate the whole feature at 32-bit colour, for
one release, in silence.** The count is turned into the mask bit by `TEXA`'s
AEM expansion — *an all-zero texel is transparent, anything else is `0x80`* —
and the GS only expands an alpha it has to **invent**: `PSMCT16` (one bit) and
`PSMCT24` (none at all). A `PSMCT32` texel carries its own alpha byte and
`TEXA` is ignored for it. The count pass deliberately writes **alpha 0** (the
count lives in the RGB channels, so the band's A bit can never trip the
resolve), so with the band bound as `PSMCT32` the resolve sampled alpha 0 for
every pixel, failed its `ATEST != 0` on every pixel, and wrote **not one mask
bit**: at 32-bit colour the torch cast no shadow whatsoever, while 16-bit —
where AEM does apply — worked. That asymmetry is why it survived a console
pass and a screenshot review: every shot that proved the feature had been
taken while chasing the 16-bit bugs above. The resolve binds `PSMCT24` when
the band is 32-bit — the same memory, minus the byte we do not want.

Diagnosing it took one probe and one question, the recipe in the
`tyra-engine-dev` skill: **skip `maskClear()` for one build**. The alpha then
starts each frame at the 0x80 the repaint left, so a live gate must discard
*everything* the torch draws. Half the picture went dark and the ground pool
did not — which said the gate was live, the mask was empty, and the fault was
between the count pass and the frame's alpha. Nothing else had to be guessed.

Four rules keep the volumes honest, each paid for with a report from the
yard. The occluder slots go to the four candidates NEAREST the torch, never
in object-table order (three merged facades used to eat every slot and the
props between the torch and them never cast). A thin thing - a lamp post, a
sign - never claims a receiver slot; it keeps its cheap per-vertex cone (the
slot it stole was the facade's). Self-shadowing is excluded BY CONSTRUCTION
under counting: **the near caps are the caster's faces turned AWAY from the
light**, so the volume starts at its far side and the caster is never inside
it, and the whole mask can be built before any light pass draws.

That cap used to sit on the LIT faces (pushed 0.05 down their rays, which
only ever broke a depth tie), and the difference is not subtle - it is most
of what a shadow looked like. A volume capped on the lit side **contains its
own occluder**: every surface the real mesh recesses behind the hull that
stands in for it counted as shadow. A barrel came back with its panel lines
in stripes of black, and - because a model past 1200 triangles was, at the
time, represented by a BOX - a hard-edged rectangle of that box's footprint sat
on the ground around it, which reads as the shadow and is not one. Capping on the unlit
faces costs nothing and moves nothing: the silhouette ring is shared by both
halves, so the shape on screen is identical.

Where the 1-bit fallback instead
walks casters and receivers TOGETHER, sorted by distance, each receiver's
light drawn before its own volume enters the mask (a proud proxy used to
swallow its own caster whole - "a black hole" - and that interleave is what
makes the class impossible there). And once
the last DATE-gated pass has drawn, the raster's ALPHA is
repainted to the neutral 0x80 - the mask lives in the framebuffer's alpha,
and the SDTV flicker filter blends its two read circuits by that very
channel, so a mask left in place is SHOWN by the CRTC as translucent wedges.

The per-vertex cone also arms one frame AFTER the toggle: the receivers'
cone-off flags are computed after the scene has drawn, so the enable frame
used to hit every big receiver with the full blocky per-vertex term once -
the pre-projector look, strobing when the toggle was spammed.

What the silhouette mode needs and costs:

- **"Cast shadow" (projected)** on the caster, like any projected shadow; the
  engine has four slots per frame and the nearest casters win them.
- The torch competes with the scene's lights for each caster and usually wins at
  night (a strong point light you stand next to can still outbid it).
- **Line of sight is checked** — a caster on the far side of a wall is a routine
  arrangement for a light that walks around, and without the check its
  silhouette painted *through* the wall.
- A torch level with the caster throws **no ground shadow** (there is no ground
  the ray reaches) but still paints the wall — the era’s signature shot.
- In first person the shadow hides *behind* its caster from your point of view —
  the light is your eye. It reveals itself at the edges (the silhouette is
  bigger than the caster by the light's divergence), on casters off the beam's
  axis, and whenever the camera is anywhere other than the torch.

### How much of a volume shadow you will actually SEE

Read this before filing "the shadows are missing", because the honest answer
is geometry rather than a bug. **A light on the view axis casts every shadow
exactly behind the thing that casts it.** In first person the torch *is* your
eye, so the only part of a correct shadow that can reach the screen is a rim
as wide as the virtual torch's parallax - `volPush`, 5% of the beam's range,
clamped to 0.5-2 units. Beside a barrel three metres away that rim is a
sliver a few pixels across. It is not a defect and no amount of mask fixing
widens it.

What *does* show a volume shadow in full:

- **a surface well behind the caster** - a wall several metres past it, where
  the shadow's silhouette is magnified far beyond the caster's own screen
  size (the wall in `examples/night-walk`);
- **a caster off the beam's axis**, which throws its shadow across the pool
  rather than into its own hiding place;
- **a third-person camera**, where the torch really is off the view axis, and
  the shadows are as plain as any other light's;
- **an offset torch** — *Held right* / *Held below eye* ("Off the eye" above),
  which is the knob that changes this answer: the same barrel measured 896 dark
  pixels inside the pool with the light in the eye and 5994 at 0.2 / 0.25.

Two ways to widen the rim were tried and measured, and both are dead ends
worth not repeating: dropping the virtual torch to chest height (0.55 units)
moves a few degrees at a caster's distance and changed almost nothing; and
widening `volPush` past a caster's own distance **disqualifies that caster**,
since nothing closer than `volPush + 0.3` may cast at all (at 3.5 units the
prop the torch was pointed at stopped casting entirely). For big, obvious,
always-visible shadows use the sun/moon's per-object *Projected silhouette*
([shadows.md](shadows.md)); that light is never where the camera is.

One era artifact, inherited honestly: the wall pass has no self-shadowing, so
the silhouette lands on the far wall even when the beam's own light got there
through the gap *beside* the caster rather than through it. The era spent its
shadow volumes on exactly this class of correctness; here four slots and a
64×64 silhouette are the whole budget.

## What is still on the model, and what to do about it

A model with few triangles, lit by the cone, shows them: the shading changes
across a triangle and the triangle is metres wide, so you see its edges. The
soft cone above takes most of the sting out of it, and there is no version of a
per-vertex term that fixes the rest — it is the term's own resolution.

Three things that actually help, in the order they are worth trying:

1. **Build big architecture out of boxes**, or make sure a big model is its own
   static batch. Then it is a receiver like a wall: the pool lands on its face
   per pixel and it takes no cone at all.
2. **Keep the torch modest and the scene dark.** The banding is a *contrast*
   artifact: it is invisible on a surface that is only gently lit, and glaring
   on one the torch has saturated.
3. **Tessellate what you shine at.** A wall of two triangles cannot be lit
   per-vertex; the same wall as a 4x4 grid can.

Games of this era leaned on the same three, plus one thing this engine does not
have: their environments were *lightmapped*, so the torch was a projected
texture on top of a surface whose lighting was already baked per pixel, and
almost nothing depended on per-vertex dynamic light.

## See also

- [Terrain distance detail](terrain-lod.md) — why a finer heightmap is not the
  fix for a coarse-looking torch, and what it *is* the fix for.
- [Emissive materials](emissive-materials.md) — the other additive-pool trick,
  for lights that live in the world.
- [GS VRAM](gs-vram.md) — what a resident texture costs.
