# Progress log

Living document: what is being worked on right now, what is done, what is queued.
Each finished feature lands as its own commit.

## In progress

- (nothing - the feature marathon batch is complete; see Backlog for next steps)

## Also done after the marathon

- (10) **FPP showcase template** — third choice in New Project: seeds a fresh project
  with all features (built-in house.obj + crosshair.png embedded in the editor,
  physics ball, pillar, HUD, starter flow graph). Fresh copy every time, so the
  shared sample no longer gets wrecked by experiments. Verified in PCSX2.

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
- (4) **Sky gradient dome** — vertex-colored dome (horizon/zenith preference colors),
  same gradient in the editor viewport. Scripts changing ctx.skyColor retint the
  dome at runtime. Verified in PCSX2.
- (5) **Flow graph** — CryEngine-like visual logic (imnodes window, tab next to
  Viewport): triggers (On Start, On Button, Near Object, Every N Seconds) wired to
  actions (Set Sky Color, Show/Hide/Toggle Object, Move Object By, Set Object Color,
  Log). Graph lives in project.json, compiles to src/scripts/flow_graph.gen.cpp on
  every build. Runtime verified in PCSX2 (OnStart->SetSky retints the dome).
  Editor node UI compiled but needs a hands-on pass.
- (6) **Directional lighting** — light direction + ambient/diffuse in preferences,
  baked into vertex colors at build; terrain shaded by its up normal; viewport uses
  the same formula. Gotcha: PS2SDK math3d.h #defines LIGHT_AMBIENT - constants use
  SCENE_ prefix. Verified in PCSX2 (side light: directional shading on sphere/box).
- (7) **Custom .obj models** — "+ Model" imports a .obj into res/models/, shown in
  the viewport (shared parser, per-face normals) and compiled into the game as
  vertex data (capped 3000 tris/model). Full citizen: gizmos, physics, scripts,
  lighting. Verified in editor + PCSX2 (hand-written house model).
- (8) **Gizmo snapping** — hold Ctrl while dragging: 0.5 units / 15 deg / 0.25 scale.
- (9) **HUD from images** — "+ Image (PNG)" imports into res/hud/; position
  (normalized, center anchor) + pixel size editable; live preview overlaid on the
  viewport (stb_image); in-game rendering via Tyra Renderer2D sprites. Verified in
  PCSX2 (crosshair over the 3D scene).

- (11) **Engine optimization: fast EE clipper (patch v2)** — three engine files
  patched via the Runner (marker `/tyra/.tyra-editor-patch-3`, originals restorable
  with `git checkout` inside `/tyra`):
  - `planes_clip_algorithm.cpp`: Cohen-Sutherland outcodes - fully-visible
    triangles skip the 6-plane Sutherland-Hodgman entirely, fully-outside ones are
    rejected instantly; `clipAgainstPlane` no longer copies two 64-byte structs
    by value per edge per plane.
  - `stapip_clipper.cpp`: static vertex pool instead of a heap-allocated
    std::vector per clip call (per subpackage, per frame).
  - `stapip_qbuffer.cpp`: persistent per-buffer arrays instead of up to four
    new[]/delete[] pairs per fill call.
  Benchmark (128x128 terrain, detail 128 = 98k verts, precise clipping, FPP at
  ground level, PCSX2, 3 samples each): **12/12/12 -> 15/15/15 FPS (+25%)**,
  pixel-identical output. Real scenes with a higher clipping share should gain
  more (the pathological benchmark is partly VU1/DMA-bound).

## Backlog (rough order)

- Hands-on pass over the Flow Graph editor UX (needs a human with a mouse)
- Object physics vs objects (stacking), player physics polish (pad feel)
- Model picking uses the unit-box approximation (big models pick imprecisely)
- HUD images draggable directly in the viewport
- Textured models (.mtl/PNG) and textured terrain
- Audio (Tyra supports wav/adpcm) - trigger via flow graph action
- Multiple scenes (model exists in project.json, editor edits only "main")
- Flow graph: more nodes (timers with reset, gates, variables, sounds)
- Engine perf, next targets: packager allocates its package array per frame
  (poolable); the real endgame is the engine author's own TODO in
  stapip_clipper.hpp - move clipping to VU1 entirely ("too much time")
