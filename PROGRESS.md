# Progress log

Living document: what is being worked on right now, what is done, what is queued.
Each finished feature lands as its own commit.

## In progress

- (4) Sky gradient dome + directional light preferences

## Done

- Core editor: project creation (orbit/FPP templates), solution files + undo history,
  scene primitives + spawn points, gizmos, wireframe view modes, preferences,
  C++ scripts with VS Code IntelliSense, engine clipping fixes, sample project.
- (1) **Runtime scene v2** — objects are mutable at runtime (`RuntimeObject` in
  ScriptContext: position/rotation/scale/color/visible + `dirty` flag), each object
  renders from its own bag rebuilt on change. Terrain keeps precise clipping;
  objects skip culling (engine bbox cache is keyed by pointer and goes stale for
  moving objects). Verified in PCSX2: falling sphere rests on the ground, 50 FPS.
- (2) **Player physics (FPP)** — gravity + jump on X (JUMP_SPEED pref), XZ collision
  with scene objects (AABB + player radius), walking on top of boxes (step 0.5).
  Compiles & boots; interactive feel needs a pad test.
- (3) **Object physics** — `Physics` checkbox per object: falls with GRAVITY pref,
  rests on the terrain. New FPP projects seed a falling ball as demo. Verified.

## Backlog (rough order)

- (4b) Directional light + ambient shading for scene objects (viewport parity)
- (5) Sky gradient dome (no textures needed) + sky preferences
- (6) Custom .obj models as scene objects (editor preview + PS2 loader)
- (7) HUD from images (PNG sprites placed in the editor, rendered by Renderer2D)
- (8) Flow graph - visual logic from ready components (CryEngine-3-like), codegen to a script
- (9) Editor QoL: gizmo snapping, object duplication in viewport, camera presets
