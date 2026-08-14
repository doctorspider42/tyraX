# The flashlight

A **Player** object can carry a torch: *Properties > Flashlight* on the player,
with a colour, a reach, a cone half-angle and an optional pad button to switch
it on and off. The `Set Flashlight` flow node is the master switch — a project
can start dark and hand the torch over later.

It reaches the world in two ways at once, and the difference between them is
worth knowing before you tune anything.

## The two halves

**The cone** is a real light. The engine's spot light (`setSpotLight`) is
evaluated **per vertex on VU1** — a cone term and a distance falloff added on
top of the baked vertex colours, with no N·L term (the colour pipelines carry no
normals). It lights props, models and the walls of a room.

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
  tuning accident.
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

**It lands on walls too.** Shine the beam at a wall and the pool appears *on the
wall*, in its plane, clipped to its edges. Nothing about the projection changes
for this — `goboST` is a function of the world position alone, so it does not
care what the surface is — only the patch has to lie somewhere else.

The beam is intersected against the solid boxes in reach and the nearest face it
enters wins, with faces pointing mostly up left to the ground path. Two things
worth knowing about that test:

- It is an **oriented** box, not an axis-aligned one. Everything else in the
  generated game that asks "what is under here" ignores rotation, because it is
  asking about a footprint and a footprint has no facing. A wall's facing is the
  whole question here: on an AABB, a wall turned thirty degrees to the world
  would take its light on a face that is not where the wall is. So the ray goes
  into the box's own frame (the same rotated basis an [Area](areas.md) uses), and
  the patch is built there.
- The pool is **clipped to the face**, so the light stops at the wall's edge, as
  light does. It does not wrap round a corner onto the next wall: one patch, one
  surface, the nearest one the beam meets.

One limit it keeps on purpose:

- **A large flat face is still washed by the cone.** The per-vertex spot lights
  a box face from its four corners, so at close range the whole face comes up
  evenly and the projected pool reads as a hotspot on top of it rather than as
  the only light there. Keep the torch colour modest (see below) and the pool
  does the talking.
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

## See also

- [Terrain distance detail](terrain-lod.md) — why a finer heightmap is not the
  fix for a coarse-looking torch, and what it *is* the fix for.
- [Emissive materials](emissive-materials.md) — the other additive-pool trick,
  for lights that live in the world.
- [GS VRAM](gs-vram.md) — what a resident texture costs.
