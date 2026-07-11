# Showcase

A larger sample project that exercises most of the editor's feature set in one
game. It is a checked-in tyra-editor project (like `script-demo`): open it in
the editor, or build it headless with

```powershell
build\tyra-editor.exe --build samples\showcase --run
```

The source of truth is `showcase.tyra` + the authored assets under `res/` +
the `terrain-*.heights` files. The generated game sources, `Dockerfile`,
`Makefile`, etc. are rewritten on every build (see `refreshGenerated` in
`src/project.cpp`); `obj/`, `bin/`, `.res-baked/` and `*.history` are not
checked in.

## What it demonstrates

- **Large terrain** — a 192×192 heightmapped valley (`vale`) with rolling
  hills and a background ridge, plus a smaller `cavern` scene.
- **Streaming layers, loaded dynamically** — the `village` and `ruins`
  districts start unloaded; two `Near Object` gate triggers `Load Layer` the
  district you approach and `Unload Layer` the other (GTA-style budget). The
  `forest` and `weather` layers stay resident.
- **Skeletal animation** — `res/models/wobbler.glb` is a cylinder skinned to a
  5-joint chain with two looping clips (`Wiggle`, `Twist`). The build bakes it
  into a `.tskl` skeletal model; several instances play it around the scenes.
- **Level of detail** — object draw-distance culling on the trees/rocks,
  animation LOD (`animLodDistance`) and baked mesh LOD (`meshLodDistance`) on
  the animated models.
- **Directional + point lights** — a warm golden-hour sun baked into the
  terrain/vertex colours, plus point lights at the campfire, village lanterns,
  ruins and cavern crystals.
- **Particle effects** — fire and smoke at the campfire, camera-following
  rain, sparks and ground fog in the ruins, and fog/fireflies in the cavern.
- **Fog** — GS distance fog tuned to the terrain scale (and a denser blue fog
  override in the cavern).
- **Post-processing & grading** — bloom + film grain, and two colour-grading
  presets (`Golden Hour` outdoors, `Nightfall` switched on in the cavern).
- **Extras** — a title-screen menu and a pause menu, a HUD crosshair, a
  first-person player with a toggleable flashlight, a save point + a save
  value (`orbs`) collected from a usable relic, usable-object highlighting, a
  gradient sky dome, ambient music and a spatial campfire sound, and a portal
  pair that switches between the two scenes.
