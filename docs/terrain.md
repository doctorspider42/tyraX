# The terrain, and building without one

![Sculpting and painting terrain](img/terrain-painting.png)

Every scene starts with a **terrain**: a flat ground plane, as wide and deep
as the scene, that you can sculpt and paint ([terrain
painting](terrain-painting.md)). It's what everything stands on — the player,
physics bodies, dropped props, the AI.

A scene doesn't have to have one. An interior, a platformer, a space level or
a cutscene-only scene has floors of its own, and a ground plane under them is
geometry the console draws for nothing. So the terrain is **optional per
scene**, and removing it removes it completely — not just its picture.

## Creating a scene with or without a terrain

- **New project** (*File > New Project*): the **Create terrain** checkbox, on
  by default. Width and depth stay meaningful either way — they are the
  scene's **world bounds**, the rectangle every walker is clamped to.
- **New scene** (*Project panel > + Scene*): the same checkbox.
- Headless: `tyrax-editor --new <name> <dir> [w] [d] [preset] [unitsPerMeter]
  --no-terrain`.

## Removing (or creating) the terrain later

*Tools > Terrain Editor* opens with a **Terrain in this scene** checkbox.
Turning it off removes the ground; turning it on brings it back. Both are one
undo step.

Nothing is destroyed: the heightmap you sculpted, the painted layer weights
and the layer list stay in the project, so the terrain you had comes back
exactly as it was. While it's off, the rest of the Terrain Editor is hidden —
there is nothing to sculpt, paint or bake.

## What "removed" means

In the editor:

- no ground mesh, no grid lines, no painted layer passes in the viewport,
- the **Sculpt (4)** and **Paint (6)** tools are disabled,
- surface snapping and **End** (drop to floor) rest objects on **other
  objects only** — an object over nothing keeps its height ([object
  placement](object-placement.md)),
- the ambient-occlusion **ground contact** shading is off (there is no ground
  for a prop to sit against).

In the generated game:

- no terrain chunks are built and no ground textures ship (they are not even
  listed, so they cost no [GS VRAM](gs-vram.md)), no terrain lightmap is
  baked ([ambient occlusion](ambient-occlusion.md), [global
  illumination](global-illumination.md)), no ground light pools under dynamic
  lights,
- **there is no floor.** The height sampler answers a value far below the
  world everywhere, so the player, physics bodies, thrown props and particles
  rest on the **geometry you place** — a box, a plane, a model with collision
  — and fall through the void anywhere else. Blob and projected shadows land
  on that geometry and are skipped over the void.
- The player still starts where you put it: with no terrain the spawn point's
  (or the Player object's) own **Y** is the starting height, instead of the
  ground under it.

So a terrain-less FPP or third-person scene needs a floor before the player
has anywhere to stand. The New Project dialog says so when you pick that
combination; build it anyway and the player falls at boot.

The scene's **world bounds still apply** — X/Z are clamped to width/depth as
before, and physics bodies still bounce off those walls. Only the ground is
gone.

## Limits worth knowing

- **Navigation (AI) needs a terrain.** The navmesh is a rasterization of the
  ground surface, so a scene without one has no walkable cells at all and the
  Patrol / Chase / Flee nodes hold their agents still ([navigation
  AI](navigation-ai.md)). Building AI-driven characters on placed floors is
  not supported yet.
- **Projected decals** need a receiver: with no terrain they project onto
  overlapping objects only, and a decal over empty space projects nothing.
- **Nothing catches a falling body.** There is no kill plane and no respawn:
  an object that falls off the world keeps falling (at terminal velocity, at
  a finite depth — the game does not misbehave, the object is simply gone).
  Build the floor, or a Box with collision, wherever the player can reach.
- The terrain **size** is not a terrain setting only — it is the world
  bounds, so it still matters with the ground removed.
