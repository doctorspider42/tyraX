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
normals). It lights everything: props, models, the ground, the walls of a room.

It is also only ever as fine as the mesh it lands on. A terrain cell is never
smaller than one world unit, so on the ground the cone is a Gouraud diamond
across a handful of vertices, and a footprint smaller than one cell — which is
what you get looking at your own feet — lights nothing at all.

**The pool** is the picture. Where the beam lands, the game drops one additive
patch and takes its texture coordinates from the **beam's own frustum** — the
same projective mapping a [projected shadow](ambient-occlusion.md) uses to
sample its silhouette. So the shape of the light is a *texture*, per pixel, and
the ground's tessellation has no say in it. This is the half you are looking at
when you shine a torch at the floor.

## What the pool does

- **Follows the beam.** The patch is laid out along the beam's run across the
  ground, not axis-aligned, because a beam meeting the floor at a grazing angle
  reaches much further than it is wide. What you see is an ellipse that
  stretches as you lower the torch, and a circle when you point it straight down.
- **Follows the relief.** On terrain, each patch vertex sits on the ground under
  it, so a pool crossing a ridge bends over it.
- **Lands on what you built.** Placed floors, platforms and props are receivers
  too, so a torch works in a room made of geometry — including in a scene with
  [no terrain at all](terrain.md), which had no pool before.
- **Fades honestly.** It dims with distance, and it dims at grazing angles: the
  same cone spread over four times the ground really is four times weaker per
  square metre.

Two limits it keeps on purpose:

- **It is a floor effect.** A beam aimed at a wall taller than the player lights
  that wall through the per-vertex cone only — there is no patch on a vertical
  surface. Aim at a crate lower than your eye and the pool does land on its top.
- **It is one patch of 4x4 cells** (96 vertices — one VU1 package). The
  projective mapping is exact at the vertices and linear between them, so a very
  long stretched pool is slightly approximate in the middle. It is the same
  budget the projected shadows work to.

## The gobo

The pool's texture *is* the shape of the light. The editor bakes one into
`res/hud/flashlight-gobo.png` (128x128) for any project that can show a
flashlight: a hot centre, a soft penumbra, the faint ring a dish reflector
throws, and two low-frequency lobes so the circle is not perfectly round. It
costs about 6% of the console's ~1.08 MB texture heap ([GS VRAM](gs-vram.md)),
and a project with no flashlight never loads it.

To use your own, set *Properties > Flashlight > Pool texture* to a PNG in
`res/hud/`. Rules:

- The pool is an **additive** bag (`Cs*FIX + Cd`), so the shape lives in **RGB**
  and alpha is ignored. A white image draws a bright rectangle.
- Keep the outer border **black**. The projected coordinates are clamped, so a
  lit edge texel smears outward into a hard rectangle.
- The image is a real gobo: a cross, a cracked lens or a grille shape maps
  through the beam and lands on the ground as that shape.
- Power-of-two sides, like every PS2 texture.

## Tuning

| Symptom | Knob |
|---|---|
| The lit circle is too small / too wide | *Cone half-angle* — it sizes both halves |
| The pool dies too close | *Reach* — the fade is measured against it |
| The pool is too bright / washed out | The gobo's own levels, or *Light colour* |
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
