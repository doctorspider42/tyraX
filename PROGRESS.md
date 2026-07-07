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

- (12) **Engine optimization v3: clipping leaves the EE (the author's TODO)** —
  resolves the engine author's own comment in stapip_clipper.hpp ("clipping
  algorithm should be moved to VU1... too much time"). How: the classic PS2
  guard-band trick. The shared `PerformClipCheck` VU1 macro now tests XY against
  a 3x wider window (one extra `muli.xy` on a vertex copy; Z/W test untouched,
  coordinates stay inside the GS 4096 raster window so nothing wraps) and the
  GS scissor trims the pixels in hardware. `RenderBBox::clipFrustumCheck`
  reclassifies packages crossing only the side planes as cullable; the EE
  clipper survives solely for the near-plane band (`+1.5` unit margin), where
  perspective division would explode - the one case a scissor cannot fix.
  Applied by the Runner (marker `.tyra-editor-patch-4`, awk script swaps the
  VCL macro, VU1 microprograms force-rebuilt).
  Benchmark (same 98k-vert scene, 3 samples): **12 -> 50 FPS (4.2x, full PAL
  frame rate)**, frame pixel-clean. VU1 usage 1% -> 6%: the work moved to the
  chip that was built for it.

- (13) **Terraforming** — sculpt the terrain with a brush: *Sculpt (T)* mode in the
  viewport, LMB raises / Shift+LMB lowers (cosine falloff, radius + strength
  sliders, RMB orbits), brush ring projected onto the relief. Heightmap lives in
  `terrain.heights` (vertex grid = terrain detail; resampled when the grid config
  changes), compiled into the game as `terrain_heights.gen.hpp` with a bilinear
  sampler. The FPP player walks the relief, physics objects rest on it, terrain
  shading follows the height gradient in both the viewport and the game.
  Verified in editor + PCSX2 (generated hill + valley; the physics ball landed on
  the hilltop). Sculpting itself needs a hands-on mouse test. Not in undo history
  (saved on stroke end).

- (14) **Textures (PNG)** — the PS2-native format Tyra loads (32/24bpp + fast
  palletized 8/4bpp; power-of-two sizes recommended). Per-object texture
  (Set.../Clear in object properties; object color modulates the texture, white =
  plain) and a tiled terrain texture (preference + world-units-per-tile scale).
  UVs generated for all primitives, `vt` parsed from .obj models, terrain tiles in
  world space. PS2 side: StaPipTextureBag per bag, textures loaded once via the
  TextureRepository, modulation-correct colors (128 = 1.0). Editor viewport renders
  the same textures via stb_image + a sampler in the shader (wire passes stay
  untextured). Verified in editor + PCSX2 (bricks on sculpted terrain and a box).

- (15) **Scene light management** — light color (tints the diffuse term) and a
  global brightness multiplier (0..2), next to the existing direction/ambient/
  diffuse in Preferences > Lighting (same dialog as the sky). Shading is now
  per-channel RGB in the whole pipeline (game codegen + viewport). Verified in
  editor + PCSX2 (warm sunset light over the textured terrain).

- (16) **Player entity** — "+ Player" inserts a playable player into any scene, no
  FPP template required (works in orbit projects too; the first Player wins over
  the template camera). Per-object parameters in the properties panel: movement
  mode (**Walk FPP** — terrain relief + AABB collision + gravity/jump on X, or
  **Noclip** — free flight toward the look direction, Cross up / Square down),
  walk speed, look speed, eye height, jump speed. Left stick moves, right stick
  looks. Shown as a gold humanoid marker in the viewport (nose = facing),
  invisible in the game. Stored as `"player": {...}` in project.json, compiled
  into `scene_data.hpp` as PLAYER_* constants. Verified in editor + PCSX2
  (FPP project: camera starts at the entity on the sculpted terrain; orbit
  project: noclip camera at the entity, scene objects framed as expected).
  Interactive pad feel needs a hands-on test.

- (17) **Music playback** — background music controlled from the Flow Graph.
  New Music section in the Project panel imports WAV tracks into `res/audio/`
  (16-bit 22050 Hz stereo - the format Tyra's song player streams; the importer
  reads the WAV header and warns about anything else). Three new action nodes:
  **Play Music** (track combo, volume slider 0-100, loop checkbox), **Stop
  Music**, **Set Music Volume** - wired to `engine->audio.song` (audsrv) in the
  generated flow_graph.gen.cpp. "Play from scene start" = the existing On Start
  trigger -> Play Music. Verified in PCSX2: game boots at full frame rate and
  the emulator's WASAPI session peak meter pulses with the test melody
  (generated 22kHz arpeggio); Triangle -> Stop Music compiled in. Speaker check
  by ear is left for a human.

- (18) **Sound effects (ADPCM)** — one-shot samples from the Flow Graph. Sounds
  section in the Project panel imports WAV (16-bit 22050 Hz, mono or stereo)
  into `res/sfx/`; the Runner converts them with the PS2SDK `adpenc` tool at
  build (`bin/sfx/*.adpcm`, skipped when already up to date). New **Play Sound**
  action node: sound combo, volume slider, SPU channel slider (0-23, or "auto" =
  round-robin over all 24 - ADPCM voices cannot be stopped, so rotation avoids
  drop-outs when shots overlap). Samples load once in the generated script's
  init(), playback via `engine->audio.adpcm.tryPlay`. Verified in PCSX2: with an
  Every-2-Seconds -> Play Sound graph the emulator's audio session peak meter
  shows silence with a burst exactly every 2 s (test chirp). Speaker check by
  ear is left for a human.

- (19) **Per-object flow graphs + categorized menus** — the single global flow
  graph is gone: every scene object can carry its own graph (stored inside the
  object in project.json; legacy project-level graphs migrate to the first
  object on load). The Flow Graph tab gets a "Graph of" combo (objects with a
  graph are starred) and a "Selected object" jump button. Object-referencing
  nodes resolve their target as: incoming **object-id data link** (new square
  pins + amber links, id output on triggers and object actions) > explicit
  name > **self** (the graph's owner - the new "(self)" combo default), plus a
  "From selected" button that grabs the object selected in the editor.
  Resolution happens at codegen (one script class per object graph); copying
  an object copies its graph, and self-references follow the copy - graphs now
  work as reusable components. Insert menus modernized into category trees:
  scene objects (Simple / Gameplay / Custom - single "+ Add object" button
  instead of the overflowing button row) and flow nodes (Triggers / Object /
  Scene / Audio / Debug). Graph edits now land in undo history (graphs are
  part of scene snapshots). Verified: codegen resolves a data-link chain and
  self-references correctly (phys-demo), game boots at 50 FPS; node-editor
  interactions (pins, combos) need a hands-on mouse pass.

- (20) **Position pins + player spawning** — second data type in the flow
  graph: XYZ positions travel over green triangle pins (object ids stay on
  amber squares; pure data nodes have no exec pins). New nodes: **Get
  Position** (pure - reads the target object's position live at the consumer),
  **Set Object Position** (X/Y/Z params, overridden by an incoming position
  link; passes both the object and the position through) and **Spawn Player
  At** (Player category - teleports the Player entity, or the FPP template
  player, to the target object's position; e.g. On Button -> Spawn Player At
  a spawn point = respawn). ScriptContext gained a teleport request the game
  loops apply per template. Position links resolve at codegen into direct
  `objects[i].position` reads, so chains stay zero-cost. Also: flow zoom now
  scales the ImGui spacing vars, so node layouts shrink uniformly instead of
  drifting at low zoom. Verified via generated code + PCSX2 boot (box adopts
  the house position through a Get Position -> Set Object Position link);
  Square-button respawn needs a pad test.

- (21) **In-tree Tyra engine + WAV-aware music player** — the engine-patch
  machinery (embedded sources, awk macro swap, `/tyra/.tyra-editor-patch-N`
  markers) is gone: `vendor/tyra/engine` is now a versioned fork (Apache 2.0,
  upstream `9273416`) with all editor fixes applied directly. The Runner
  bind-mounts it read-only at `/engine-src`, checksum-rsyncs into the shared
  volume and rebuilds libtyra + relinks games only when something actually
  changed - editing the engine is now a regular workflow. New engine fix in
  the fork: `audio_song.cpp` parses the WAV header (RIFF chunk walk, done
  in-memory - fseek/ftell are unreliable over the PS2 host fs) instead of
  assuming 16-bit/22050/stereo at offset 0x30, configures audsrv from the
  file and streams exactly the data chunk; small formats get a smaller chunk
  size + fill threshold (mono 22 kHz starved audsrv's ring buffer before).
  Verified in PCSX2 with a tone/silence pattern: 44.1 kHz stereo with LIST
  metadata, mono 44.1 kHz and mono 22 kHz all play clean at correct speed
  (the old player fed metadata bytes to the speakers and halved the tempo).
  Music importer keeps files untouched and reports the format (PCM 16-bit,
  mono/stereo, 11-48 kHz; float/24-bit flagged as unplayable). Also: Spawn
  Player At now applies the target's Y rotation to the player's yaw.

- (22) **8-bit WAV crackle fix + player jump toggle** - the user's "correct in
  foobar, crackles on PS2" track turned out to be unsigned 8-bit PCM (48 kHz
  stereo): WAV stores 8-bit samples unsigned (0x80 = silence) while audsrv
  mixes them as signed, so the waveform wrapped at every zero crossing. The
  in-tree song player now converts the chunk in place (XOR 0x80) for 8-bit
  files. Verified with a quiet-tone pattern: peak 0.24 (full-scale wrap
  garbage) before, 0.02 (clean sine at the expected amplitude) after. Also:
  Player entity gets a "Can jump (X)" checkbox (PLAYER_CAN_JUMP gates the walk
  jump; jump speed hidden when off).

## Backlog (rough order)

- Hands-on pass over the Flow Graph editor UX (needs a human with a mouse)
- Object physics vs objects (stacking), player physics polish (pad feel)
- Model picking uses the unit-box approximation (big models pick imprecisely)
- HUD images draggable directly in the viewport
- Textured models (.mtl/PNG) and textured terrain
- Positional audio (volume falloff by distance to an object)
- Multiple scenes (model exists in project.json, editor edits only "main")
- Flow graph: more nodes (timers with reset, gates, variables, sounds)
- Engine perf, next targets: packager allocates its package array per frame
  (poolable); the real endgame is the engine author's own TODO in
  stapip_clipper.hpp - move clipping to VU1 entirely ("too much time")
