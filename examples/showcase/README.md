# Showcase: Worlds Collide

The flagship TyraX example is a three-act playable tour built to put the editor
and the PS2 runtime under real pressure. It moves from a warm fantasy village,
through an unstable portal laboratory, to a rain-soaked neon city. The project
uses production-style CC0 art rather than placeholder geometry, but remains a
normal checked-in TyraX project that can be opened and changed in the editor.

```powershell
build\tyrax-editor.exe --build examples\showcase --run
```

## The tour

1. **Elysian Village** opens with the `Dawn of Worlds` cinematic. Explore the
   modular houses and market, cross the paired spatial portals, then use the
   rift gateway to trigger `Rift Ignition` and enter the laboratory.
2. **Rift Lab** combines animated creatures, patrol/chase navigation, physics
   props, particles, floor/ceiling teleport portals and a live VU0 ray-traced
   quantum mirror. Use the city uplink to continue.
3. **Neon City** is the final stress scene: dense city blocks, animated
   traffic, a police-car cinematic, rooftop portals, rain, steam, neon point
   lights and distant geometry culling.

Use the left stick and camera controls to explore. Press **Square** at a
highlighted gateway, **Circle** to toggle the flashlight, and **Start** for the
options menu. Cinematics are skippable, suppress the gameplay HUD and suspend
the player flashlight while they own the camera.

## Engine features on display

- three scenes and five timeline cinematics with camera, object and effects
  tracks, HUD suppression and exact `Play Sequence` `after` chaining;
- same-scene spatial portals, object teleportation and scene-transition
  gateways driven by flow graphs;
- animated FBX creatures with animation/mesh LOD, navigation waypoints,
  patrol graphs and player-sighting chase behaviour;
- a VU0 per-pixel ray-traced mirror plus ambient occlusion and baked global
  illumination;
- heightmapped terrain, draw-distance budgets and static batching;
- directional, point and flickering dynamic lights, transition bloom, grading,
  fog, rain, steam, fire, smoke and sparks;
- physics bodies and graph-driven impulses;
- HUD prompts, usable-object highlights, first-person controls, a flashlight,
  pause/options menus, loading screen and performance overlays.

The scenes deliberately use different colour, fog and grading profiles. This
makes scene transitions read as complete acts rather than three variations of
one test room.

## Rebuilding the authored project

`build-showcase.py` is the deterministic high-level authoring source for the
97 objects, three heightmaps, sequences and graphs. It makes large structural
changes reviewable and prevents hand-edited JSON drift. After changing it, run:

```powershell
python examples\showcase\build-showcase.py
build\tyrax-editor.exe --refresh-gen examples\showcase
```

The two scene gateways connect `Play Sequence.after` directly to `Switch Scene`;
there is deliberately no duration-shaped `Delay` to drift away from an edited
or skipped cutscene.

The checked-in source of truth is `showcase.tyra`, `objects/*.json`, authored
assets under `res/`, the terrain height files and the explicit GI bake under
`.res-baked/gi/`. Generated C++, baked `.tmdl`/`.tskl` model data, extracted FBX
textures, `obj/`, `bin/` and history files are regenerated and ignored.

## Asset sources and PS2 budgets

All third-party art is CC0. The village buildings are assembled from Keith at
Fertile Soil Productions' Modular Village Pack; Quaternius' Medieval Village
MegaKit supplies the gateway and market props, and the laboratory uses the
Sci-Fi Essentials Kit. The city combines Kenney's Retro Urban Kit with
GGBotNet's PSX Style Cars. See `THIRD-PARTY-NOTICES.txt` for source links and
license notes.

Source textures were downsampled to 128 or 256 pixels and material paths were
made project-relative. The village kit uses its original flat-colour materials;
its modules are assembled into complete cottage, tavern and watchtower meshes
so each building remains a single model submission. The wall-module pivot sits
half a cell behind its rendered face. The stone rows compensate for that pivot
and fit the `Stucco` footprint rather than the wider roof-eave bounds, keeping
the lower story directly beneath the building instead of turning it into
detached foundation panels or a perimeter wall. Blender's collapse
decimator reduced the five heaviest static meshes and all three skinned
creatures while preserving their UVs, materials, armatures and animation
actions. The final animated meshes are 634, 746 and 719 triangles.
Already-low-poly assets were left untouched. The
village buildings use a 10-unit per-object mesh-LOD threshold, keeping their
full silhouettes nearby and switching to baked 50%/25% tiers at gameplay
distance. The showcase keeps the PS2's memory and fill-rate limits visible
instead of hiding them behind an emulator: far props are culled, skeletal
updates have an LOD radius, particle pools are bounded, and the FPS/free-memory
overlays are enabled in the debug profile. All three compact scenes stay
resident; scene layers are deliberately not demonstrated here because toggling
nearby geometry made spatial-portal views pop. Layer streaming belongs in a
genuinely large-map example where its memory saving outweighs that presentation
cost.
