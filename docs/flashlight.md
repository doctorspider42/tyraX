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
for someone standing inside. Silent Hill spent its shadow volumes on exactly
this; here it is simply the deal.

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

What it needs and what it costs:

- **"Cast shadow" (projected)** on the caster, like any projected shadow; the
  engine has four slots per frame and the nearest casters win them.
- The torch competes with the scene's lights for each caster and usually wins at
  night (a strong point light you stand next to can still outbid it).
- **Line of sight is checked** — a caster on the far side of a wall is a routine
  arrangement for a light that walks around, and without the check its
  silhouette painted *through* the wall.
- A torch level with the caster throws **no ground shadow** (there is no ground
  the ray reaches) but still paints the wall — which is the Silent Hill shot.
- In first person the shadow hides *behind* its caster from your point of view —
  the light is your eye. It reveals itself at the edges (the silhouette is
  bigger than the caster by the light's divergence), on casters off the beam's
  axis, and whenever the camera is anywhere other than the torch.

One era artifact, inherited honestly: the wall pass has no self-shadowing, so
the silhouette lands on the far wall even when the beam's own light got there
through the gap *beside* the caster rather than through it. Silent Hill spent
its shadow volumes on exactly this class of correctness; here four slots and a
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
