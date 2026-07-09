# Progress log

Living document: what is being worked on right now, what is done, what is queued.
Each finished feature lands as its own commit.

## In progress

- (nothing - the feature marathon batch is complete; see Backlog for next steps)

## Also done after the marathon

- (15) **Film grain dropout root-caused: alpha test vs stale RGBAQ** — the
  real mechanism behind "grain vanishes for a few frames when looking at the
  fog": post fx blits send only UV+XYZ, so their vertex alpha is whatever
  RGBAQ the scene left in the GS - and the drawing environment's alpha test
  rejects alpha==0 fragments. Particles render last and fade to exactly
  alpha 0, so on frames where a fully-faded fog vertex was the last thing
  drawn, BOTH grain blits were discarded whole. Diagnosed by measurement:
  ~6 fps native-F8 screenshot bursts (300 shots via PostMessage, no window
  focus needed) scored by mean horizontal gradient in the sky region - 10/175
  frames at near-zero grain pre-fix; an untextured same-packet marker sprite
  survived those frames, proving the packet ran and pinpointing sampling/test
  state. Fix in renderer_core_postfx: pin RGBAQ (0x80) and disable the alpha
  test for the pass, restore via draw_enable_tests() after. Post-fix burst:
  316/316 frames with grain (min 3.50 vs median 3.69). Along the way two more
  fixes in vendor/tyra: (a) 2D sprites and clearScreen end their PATH3 stream
  with a data-less EOP giftag instead of draw_finish() - keeps the un-consumed
  FINISH writes (which could release align3D's barrier early) out of the GS
  while preserving the EOP bit, which is load-bearing: dropping the whole tag
  deadlocked the GIF (PATH3 never terminated, PATH1/XGKICK starved, first 3D
  frame froze at the TYRA banner; found via host-fs marker files); (b)
  endFrame arms the post fx barrier only once a pipeline configured VU1
  (Path1::isVU1Configured), so pure-2D loading frames never handshake a VU1
  that can't answer. All verified in PCSX2 SW renderer at steady 50 FPS.

- (14) **Outline close-up fixes** — two artifacts visible when standing next to a
  usable object: (a) no bottom rim - grounded objects' shells dip below the terrain
  and the ground in front z-rejects them from a low camera; shell vertices are now
  lifted just above the terrain surface, turning the bottom rim into a glow apron
  hugging the ground around the base. (b) shells washed out receding side faces -
  the camera pushback clears the front face but not glancing ones; the object is
  now repainted right after its shells (wins the GEQUAL depth test) which erases
  the wash without touching the rim outside the silhouette. Verified in PCSX2 up
  close (USE prompt range): clean side faces, ground glow at the base, 50 FPS.

- (13) **Configurable outline blur** — Preferences > Usable objects gains "Blur
  width (units)" (total rim size, 0.05-2.0) and "Blur steps" (1-8 shells; 1 = sharp
  solid edge). Baked into terrain_config.hpp as HIGHLIGHT_WIDTH / HIGHLIGHT_STEPS;
  shells are spaced evenly up to the width with alpha halving outward. Verified in
  PCSX2: width 0.7 / steps 6 produces a visibly wider, smoother glow than the
  0.35 / 4 default.

- (12) **Soft usable-object outline + draw-order fix** — the single hull rim could
  be punched through by objects drawn later in the loop (rim pixels carried the
  background's z, so e.g. a house behind the highlighted box overdrew the rim).
  Rims now render after the whole scene: four concentric shells with fading alpha
  (blur), each pushed away from the camera by a uniform scale around the eye point —
  screen silhouette unchanged, but the object's own z-buffer rejects the interior
  and the rim is depth-tested like normal geometry (one shared pushback for all
  shells; per-shell depths made the terrain cut each shell on a different line).
  Verified in PCSX2 (software renderer, 50 FPS): house directly behind the usable
  box — rim glows in front of the house with a smooth falloff.

- (11) **Usable-object highlight** — Preferences > Usable objects: "Highlight usable
  objects" + proximity (units) + color. In-game, objects marked Usable get a colored
  outline while the player is within proximity: a flat-color copy of the object grown
  ~0.12 units around its center is drawn just before it with z-test but no z-write,
  so the object overdraws the interior and only a rim survives. The no-z-write mode
  is a new engine enum (`PipelineZTest_TestOnly`, stapip + dynpip) implemented purely
  in the GS TEST register (alpha-test all-fail + AFAIL keep-zbuffer) — the VU1
  options layout is untouched. Editor viewport marks usable objects with a wire box
  in the highlight color when the pref is on (proximity is runtime-only). Verified
  in PCSX2 (software renderer, 50 FPS): near usable box shows a yellow rim, far
  usable pillar and non-usable objects stay clean; prefs UI + JSON round-trip +
  viewport preview screenshotted. Dead end for the record: a classic inverted-hull
  outline doesn't work here — the Tyra pipeline has no backface culling, so a scaled
  hull drawn normally occludes the object; the no-z-write underdraw sidesteps that.

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

- (23) **"Use" interaction + global control mapping** — objects get a "Usable"
  checkbox: when the player camera is close (USE_DISTANCE) and looking at the
  object (USE_LOOK_DOT), a built-in "USE" sprite shows bottom-center
  (res/hud/use.png, 128x32 - PS2 textures need power-of-two sizes; shipped
  into every project when missing, replace to customize). Pressing the use
  button then sets ctx.usedObject for one frame, which fires the new flow
  graph **On Used** trigger (object param, self default - drop the graph on
  the usable object itself). Buttons are no longer hardcoded: the generated
  `inc/controls.hpp` is the single mapping place (BTN_USE = Square,
  BTN_JUMP = Cross, BTN_FLY_UP/DOWN for noclip + use-interaction tuning
  constants); marker-owned, so deleting the first line makes it a per-project
  settings file. Verified in PCSX2: USE prompt renders while facing a usable
  box up close; On Used -> Log compiled (`ctx.usedObject == idx`); the actual
  Square press needs a pad test.

- (24) **Viewport camera panning** — the editor camera orbits a movable target
  now: middle-mouse drag pans in the view plane, WASD flies over the terrain
  along the camera heading (both scale with zoom, so screen-space speed feels
  constant). Picking, sculpt raycast and the gizmo all follow the moved
  camera. Tool shortcuts moved off the letters to make room: Move/Rotate/
  Scale/Sculpt are 1/2/3/4 (button labels updated). New-terrain projects
  re-center the target. Interactions need a hands-on mouse/keyboard pass.

- (25) **Rendering corruption root-caused and fixed for real** - the
  recurring "objects render twice / giant smeared polygons" was never the
  engine patches (bisected all of them - even full upstream reproduced it):
  per-object bags used frustumCulling **None**, so objects behind or far
  off-screen were submitted raw and their coordinates wrapped the GS 4096px
  raster window. PCSX2's HW renderer often masks the wrap (hence "czasem
  działa"); the SW renderer - and real hardware - show it faithfully. Fix:
  every bag (terrain, objects, sky dome) now goes through per-package frustum
  classification; the engine's bbox cache got a `bboxVersion` field on
  StaPipBag (mixed into the cache key) which the game bumps on every geometry
  rebuild, killing the stale-bbox problem that originally motivated None.
  Fast clipping mode = Precise classification + per-triangle cull (cheap, no
  wraps). The upstream "crappy guard band" bbox margins are zeroed (exact
  classification), and the VU1 guard-band experiments are fully retired
  (three variants all corrupted ADC bits; documented in vcl_sml.i). The EE
  clipper keeps the outcode + pool optimizations. Verified on the SW renderer
  (the honest one): known-bad camera positions render clean and stable,
  gameplay views correct, near-plane clipping right, 50 FPS.

- (26) **4:3 frame, HUD preview toggle, HUD flow nodes** — the viewport gets a
  "4:3" toolbar toggle that dims everything outside a centered 4:3 frame (a
  rough preview of the console picture on a TV); the HUD editor overlay maps
  into that frame when active. The HUD preview itself is now hidden by
  default ("Show in viewport" checkbox in the HUD section). New flow graph
  category **HUD**: Show HUD / Hide HUD / Toggle HUD flip all HUD images at
  runtime via ctx.hudVisible (scripts can use it too; the USE prompt is
  independent). Verified in PCSX2: Every-2-Seconds -> Toggle HUD blinks the
  crosshair while USE stays; editor overlays verified by screenshot.

- (27) **Multiple scenes** — every scene owns its objects (with their flow
  graphs); terrain/heightmap, settings, HUD and audio assets stay shared. The
  Scenes section creates (+ Scene modal), switches (click) and deletes (x)
  scenes; the first scene is the start scene. New **Switch Scene** flow node
  (Scene category) requests a change applied between frames. Memory design:
  all textures/models load once at startup for every scene, so a switch only
  rebuilds the runtime objects (vectors and per-object bags are reused/freed
  - no leaks, no VRAM churn, takes a frame). Generated code holds one object
  table per scene (SCENE_OBJECT_TABLES + per-scene PLAYER_* arrays indexed by
  currentScene); scene scripts are guarded by the active scene index and
  reset their state via a scene-generation counter on every (re)entry - a
  reused scene starts fresh. Legacy single-scene projects and old solution
  files migrate automatically. Verified in PCSX2: a two-scene ping-pong
  (Every 4 s -> Switch Scene both ways) alternates correctly and the state
  after re-entry is identical to the first visit; 50 FPS throughout.
  Trade-off noted: assets of ALL scenes stay resident (fine for editor-scale
  projects; per-scene asset streaming = future work).

- (28) **Per-scene terrain and lighting** — each scene now owns its terrain
  (size, sculpted heightmap in terrain-<scene>.heights, tiled texture) and
  its lighting (direction/ambient/diffuse/color/brightness); sky and physics
  prefs stay project-global. Preferences edit the ACTIVE scene. Generated
  code: per-scene arrays (TERRAIN_WIDTHS, HM_*_HEIGHTS tables, SCENE_LIGHT_*)
  behind accessor macros bound to a file-scope g_activeScene; loadScene()
  rebuilds the terrain mesh + sky dome on switch (vectors reused, bboxVersion
  bumped - still leak-free). Legacy project-level terrain/light migrate into
  every scene on load. Verified: legacy project renders identically at
  50 FPS; scene switching rebuilds terrain per scene.

- (29) **Particle emitters (fire/smoke/fog/sparks)** — new Emitter object
  (Effects submenu presets; cone marker in the viewport; properties: effect
  kind, pool size 1-128, particle size; color tints, scale X/Z = spawn area).
  2002-style runtime: fixed pools allocated once per scene load, one LCG,
  no trig or allocations in the per-frame path; particles are camera-facing
  color quads (no textures - alpha does the softness) with per-kind ramps
  (fire cools orange->red and shrinks, smoke grows, fog fades in/out on big
  lazy puffs, sparks burst and fall). One bag per emitter, Precise-culled,
  bboxVersion bumped per frame; rendered last (alpha over the scene).
  Show/Hide Object nodes switch emitters on/off. Verified in PCSX2: fog
  animates frame to frame at a steady 50 FPS.

- (30) **Post effects: bloom + film grain** - "shaders" the PS2 way, no pixel
  shaders involved. New engine module RendererCorePostFx (GS framebuffer
  blits at end of frame): bloom = frame downsampled to 1/8 res (bilinear),
  softened with 4 offset taps, re-added over the frame additively (the
  bilinear filter is the blur); film grain = 64x64 noise texture drawn
  subtractive+additive with independent random offsets every frame
  (zero-mean grain from unsigned GS math). ~8 textured sprites per frame,
  z writes masked, all touched GS registers restored (ALPHA/TEX1/CLAMP/
  FRAME/ZBUF/XYOFFSET - a leftover additive ALPHA made VU1 re-blend the
  whole scene and compound-brighten it). Editor: Preferences > Post effects
  sliders (0-1), baked as POSTFX_BLOOM/POSTFX_GRAIN (0-128). Verified in
  PCSX2 SW renderer at steady 50 FPS.

- (31) **PAL + NTSC viewport frames** - the single 4:3 frame became two
  labeled toggles (PAL white, NTSC yellow). PAL fills 4:3 exactly; NTSC has
  fewer active lines so the same 512x448 buffer reads slightly wider
  (~10:7). Dim outside the union; HUD preview maps into the PAL frame.
- (32) **Loading screens between scenes** - scene switches show
  res/hud/loading.png centered on black for ~0.7s (a generated
  "LOADING..." placeholder is written when missing; replace the file to
  customize; Preferences > Scenes toggle). The load itself stays
  synchronous - the hold is presentation. Verified in PCSX2 by
  ping-ponging two scenes: steady 50 FPS, no leaks.
- (33) **Sound emitter entity** - violet sphere marker; picks one of the
  imported sounds, autoplay + range + interval in the properties. In the
  game: volume falls off linearly with the distance to the player
  (channels 16-23, one per emitter). Interval 0 loops the sample
  seamlessly (tryPlay retriggers as soon as the channel frees); > 0
  retriggers every N seconds; Hide Object mutes. Verified with a WASAPI
  peak meter: near emitter 0.29, same emitter at 15/20 range units 0.03.
- (34) **Sculpt flatten mode** - a Flatten checkbox + Level height in the
  sculpt toolbar; the brush lerps terrain toward the target height with
  the same cosine falloff (strength = lerp rate). Undoable like sculpting.
- (35) **PS2 disc export + PCSX2 HostFs guard** - Project > Export PS2 ISO
  builds <project>/<name>.iso from bin/ with an in-tree ISO9660 writer
  (iso9660.cpp; no mkisofs), so file data LBAs follow load order:
  SYSTEM.CNF + ELF, optional iso-layout.txt pins, startup assets (HUD,
  sfx), per-scene textures/models, music, rest. Layout is printed to the
  build log. Engine side: FileUtils::fromCwd converts cdrom0: paths to
  ISO9660 form ('\', upper-case, ";1"), and extension/filename helpers
  strip the ";1" version (the selector trapped on "png;1" - caught by
  booting the ISO in PCSX2). Runner also flips HostFs=true in PCSX2.ini
  before launching (missing HostFs = the "Failed to load ...png" assert
  on first fopen). Verified: fixture ISO mounts on Windows byte-identical
  with the expected LBA order; my-game.iso boots in PCSX2 from cdrom0:
  and renders (screenshot check). Caveat for real DVD-Rs: keep asset
  names <=30 chars, A-Z 0-9 . _ - (exporter warns); animated obj/md2
  sequences don't get the ";1" suffix appended yet.
- (36) **Disc Layout window** (Project > Disc Layout...) - the ISO plan as
  a drag-to-reorder table (order persists to iso-layout.txt; pinned rows
  marked *, boot files locked first; Reset returns to the automatic group
  order) next to a physically-honest disc view: the spiral track drawn as
  constant-pitch rings filled inside-out, LBA->radius via the area formula
  (outer turns hold more data, like a real CLV disc), colored by load
  group with hover tooltips, click-to-select sync with the table and a
  capacity switch (fit/CD-R/DVD-5) with an over-capacity warning.
  isoexport gained plan()/saveManualOrder() (shared ordering with build),
  iso9660 gained plan() (layout without writing). Replans automatically
  after builds. Verified by driving the editor UI: reorder wrote
  iso-layout.txt, DVD-5 view shows the 2.3 MB image as a hairline at the
  hub, fit-to-data shows the ELF band + asset arcs on the outer track.
- (37) **Flow graph logic gates** - a "Logic" node category (AND, NAND, OR,
  NOT, XOR, XNOR) plus "On Condition". New boolean data plane (violet round
  pins, FlowLinkBool): every trigger now exposes a bool output = "does this
  condition hold this frame?" (Near Object -> isNear, On Button -> held,
  Every N Seconds -> the pulse, etc.). Gates fold their bool inputs (the
  bool-in pin accepts several links: AND/OR/XOR fold, NAND/NOT/XNOR negate);
  On Condition bridges back to exec and fires its "then" on the rising edge
  of the folded bool. Codegen inlines each bool as a self-contained C++
  expression (diamonds OK, cycles guard to false). Verified with a codegen
  harness: "Near AND Button -> Toggle" and "Near XOR Button -> Show" emit
  edge-gated blocks with the expected && / parity expressions.
- (38) **Point light entity** - new "Lighting > Point light" object
  (PrimitiveType 9): color (shared Color field), brightness and radius in
  the properties. Editor preview: an unshaded bulb glowing in the light
  color, a wireframe reach sphere scaled to the radius, and a **live light
  preview** - the viewport fragment shader adds up to 8 point lights on
  top of the baked directional shade (same (1-d/r)^2 * N.L formula as the
  game; flat normals from screen-space derivatives, so the shared unit
  meshes need no extra vertex data and the pool of light follows gizmo
  drags in real time). In the game the light is baked into nearby terrain
  and object vertex colors at build (additive diffuse, clamped to 1.0 like
  the directional term) - static, so zero runtime cost; no geometry and
  non-colliding. Verified: generated scene_data + terrain_game.cpp emit
  the fields and the pointLightAt bake (codegen harness); editor preview
  verified with a GDI screenshot (orange pool on the terrain, lit box
  faces, no bleed onto faces pointing away); PCSX2 boot pending.

- (39) **Post fx grain/bloom flicker fix** - film grain dropped out for a few
  frames every so often ("turns off for a fraction of a second"). Cause: the
  dynamic pipeline kicks the 3D scene on PATH1/VU1 asynchronously (double
  buffered - `sendPacket()` returns while the VIF1 DMA is still draining and VU1
  is still rasterizing), but `RendererCore::endFrame()` ran `postFx.apply()`
  with no barrier. PostFx composites over the framebuffer via PATH3 and masks z
  writes, so on frames where VU1 lagged, late scene triangles reached the GS
  after the grain sprites and drew back over them (passing the GEQUAL z-test),
  erasing the grain across all scene surfaces - bursty, exactly matching "every
  few frames". Fix: drain PATH1 before compositing with the engine's existing
  `sync.align3D()` handshake (a VU1 draw-finish tag + GS FINISH spin-wait -
  previously defined but never called in this fork), guarded by a new
  `postFx.isEnabled()` so games without post fx pay nothing. Verified in PCSX2
  SW renderer (showcase, grain=max): boots without hanging on the new spin-wait,
  steady 50 FPS, grain now fully and uniformly present over sky, terrain and
  every object across sampled moving frames.

- (40) **Memory card saves** - a save system spanning the whole chain. New
  "Save point" entity (PrimitiveType 10, Gameplay menu): a solid box that is
  implicitly usable; pressing USE on it in the game opens a 3-slot save/load
  menu. Per-object "Save state" checkbox (position/color/visibility persisted);
  project-level "Save data" values (name + fresh-game default, Project panel)
  editable and persisted per slot; new Flow Graph "Save" nodes: Set Save Value,
  Add To Save Value, Value At Least (pure bool source for logic gates) and
  Open Save Menu. Slots store scene index, player feet position + yaw
  (restored via the existing teleport request, so it survives scene switches
  and covers both player kinds), flagged objects and all save values in one
  fixed-size `SaveGameData` block sized at codegen (`SAVE_OBJECT_MAX`).
  Runtime (`save_system.gen.cpp`, always regenerated) uses **libmc** RPC with
  rom0:XMCMAN/XMCSERV (MCMAN/MCSERV fallback, nothing embedded - `-lmc` added
  to Makefile.base); the menu is pure sprites (`res/hud/save-*.png`, baked
  text rendered by a PIL script, embedded in `src/save_assets.cpp`, written
  when missing) because the engine has no font. Menu pauses gameplay (player,
  scripts, use target, object physics), with a 15-frame input grace after
  opening (the pad reports garbage transitions at boot - without it a
  spurious Cross click saved to slot 1 instantly). Hard-won mc lore: ps2sdk
  newlib `#error`s on direct fio use and does NOT route `mc0:` paths (errno
  EMLINK) - libmc is the only sane path; loading a rom0 module twice hangs
  the IOP; `mcGetInfo` out-params are junk across module variants (reported
  type=1/PS1 for an unformatted PS2 card), so card health is judged by a real
  mkdir+open probe and `sceMcResNoFormat` (-2) alone triggers a format
  (virgin PCSX2 card images are unformatted; formatting one destroys
  nothing). Host fallback (save<n>.sav next to the ELF) when no card answers.
  Verified e2e in PCSX2: probe script wrote+read a slot on the actual card
  image (values and object state round-tripped, no host .sav files, card file
  mtime moved), the OnStart-opened menu screenshot shows the panel with USED
  on the probe's slot, saving via pad (keyboard bindings + PostMessage) marked
  slot 1 USED, and loading the probe slot teleported the player, restored the
  ball position and turned the sky orange - proving Value At Least (loaded
  coins=42.5 >= 8) -> On Condition -> Set Sky fired from loaded data. Editor
  UI (Save data section, save-point in list + viewport) screenshot-verified.

- (41) **Menu generator** - author in-game menus in the editor, no font
  needed on the PS2: the editor rasterizes a Windows TTF (stb_truetype,
  Consolas Bold with Arial fallback, src/menubake.cpp) into one panel PNG
  per menu (title, entry labels, button hints, accent border) on every
  build (res/menus/*.png, always rebaked - derived data), and the game
  runtime only draws the panel + a cursor. New "Menus" section in the
  Project panel: entries with actions (Close, Switch Scene, Open Save
  Menu, Open Menu = submenus with a Triangle back-stack, Set/Add Save
  Value, Flow Event), per-menu accent color, "Title screen" flag (opens at
  boot, one per project, Back cannot dismiss it) and a **live WYSIWYG
  preview** of the exact panel pixels (in-memory bake -> GL texture,
  rebaked on change). Flow graph gained a "Menus" category: Open Menu
  (action) and On Menu Event (trigger + bool source) - menu entries with
  the "Flow event" action fire named events that graphs react to; event
  names resolve to indices at codegen (collectMenuEvents is the shared
  contract between menu_data.gen.hpp and flow_graph.gen.cpp). Menus pause
  gameplay like the save menu (player, use target, physics, scripts -
  except the frame an event fires, so On Menu Event triggers can run);
  the save menu draws on top when both are open. Layout constants
  (256-wide panel, rows at 44+i*24, pow2 canvas with transparent slack)
  live in menubake.hpp as the baker/preview/runtime contract. Verified
  e2e in PCSX2: title screen "SAVE-E2E QUEST" opens at boot (screenshot),
  pad-driven navigation (keyboard bindings + PostMessage) entered the
  orange-accent OPTIONS submenu, its "Sunset sky" Flow-Event entry turned
  the sky sunset via On Menu Event -> Set Sky while the menu stayed open
  (screenshot), Triangle popped back to the title and "Start game" closed
  it - gameplay resumed with the sunset sky kept (screenshot). Baked
  title.png inspected 1:1. Editor Menus section renders (built + GUI run);
  the ImGui preview widget shares the verified baker path.

- (42) **Pause management** - menus grew two flags. "Pauses the game"
  (default on): a pausing menu freezes gameplay under a new fullscreen dim
  overlay (res/hud/menu-dim.png, an 8x8 translucent black stretched to the
  screen, written when missing like the other built-ins; the save menu dims
  too); switched off, the menu floats over the RUNNING game - scripts,
  physics and the player keep going and pad presses reach both (by design,
  noted in the tooltip). "Open on Start button" designates the classic pause
  menu (one per project, like the title screen): Start opens it in-game and
  Start closes it again while its root shows (submenus first pop with
  Triangle). Runtime: updateGameMenu() now returns "is a PAUSING menu open"
  (MENUS[].pause from menu_data.gen.hpp; PAUSE_MENU = the Start-button
  target), so the loop gating needed no structural change. Also fixed a
  boot-window bug this exposed: the title screen's input grace was 15 frames,
  but the pad reports garbage clicks while it reconfigures for ~3.5s after
  boot (emulog "Pad: DS2 Config Finished" up to 3.4s) - one such click
  pressed "Start game" and unpaused the title, which is how the sunset-sky
  scripts betrayed it. Title grace is now 200 frames. Verified e2e in PCSX2:
  idle 13s boot keeps the title open over an unchanged (and now dimmed) blue
  scene; Start opens PAUSED (dimmed, frozen); entering the pause-off OPTIONS
  submenu un-dims and un-freezes - with the menu still open the ball fell
  from the sky and landed and EverySeconds pushed coins past 8 turning the
  sky sunset (one screenshot shows all three); Triangle back to PAUSED
  re-dims; Start resumes gameplay at full brightness (screenshot pair).

- (43) **Menu Editor window** - menu editing moved out of the cramped
  Project-panel section into a dedicated dockable "Menu Editor" window
  (Project > Menu Editor..., or click a menu in the panel's slim list, which
  now just lists menus with [title]/[start] tags). Left: menu list with
  "+ New menu"; right: properties, a Duplicate button (copies everything but
  the unique title-screen/Start-button slots), entries laid out one per row
  with reorder arrows (rows = dpad order in the game) and inline
  target/amount widgets, and the live baked preview under its own separator.
  This also fixes the reported ImGui "2 visible items with conflicting ID"
  error when deleting a Flow event entry: the old section used integer
  PushID offsets (2000/3000 ranges) and shared "##param"/"x" ids inside the
  busy Project window; the rework gives every entry a clean per-index PushID
  scope in a fresh window ID stack and every widget an explicit unique ##id
  ("x##delete", "##event", "##scene"...) - collision-proof by construction.
  Flow-graph invocation was already in place (Open Menu node, "Menus"
  category) and is now advertised in the window's empty-state hints.
  Verified: editor builds; the user drove the new window live (accent edits
  from it landed in project.json through the commit path) - the delete-entry
  repro needs their confirmation with the ImGui debug check active.

- (44) **Menu editor v2** - three usability upgrades. (a) New top-level
  "Tools" menu in the menu bar holds "Menu Editor..." (the Project-menu item
  and the Project-panel Menus section are gone - the window is the single
  home). (b) The preview gained display modes: "Panel (1:1)" plus "TV PAL" /
  "TV NTSC" - the panel composited onto a mock TV screen (the 512x448
  buffer stretched to 4:3 / ~10:7, same aspect approximations as the
  viewport TV frames), over the project's sky gradient + terrain green and
  under the pause dim when the menu pauses - so pixel-aspect distortion and
  on-screen size are visible before any boot. (c) Menus can carry a custom
  PNG ("Image: Set..." in the editor, copied into res/hud/), composited into
  the baked panel as either a logo block above the title (the panel grows,
  entry rows shift - menubake::panelLayout() is now the single geometry
  source consumed by the baker, the preview AND menu_data.gen.hpp codegen,
  so the game cursor lands on the shifted rows automatically) or a
  background stretched under everything with a dark wash for text contrast.
  Image scaling is bilinear, capped 224x160, canvas cap raised to 512 (pow2).
  Verified: title menu with a 192x72 logo baked correctly (panel content
  138->218, row0Y 44->124 in menu_data), PCSX2 boot shows the logo title
  screen with the cursor aligned on the shifted rows (screenshot). TV
  preview modes and the background image mode follow the same verified bake
  path but their look was checked in code only - a human glance in the
  editor window is welcome.

- (45) **Menu layout system** - panels stopped being a fixed centered
  256-wide stack. Per menu: texture width (128/256/512 - pow2, PS2 cap),
  normalized screen position of the panel center (like HUD images; the TV
  preview and the generated game share the same math), a "Show title" toggle
  (logo-only menus) and **layout presets** (Centered dialog / Title at the
  bottom / Corner card / Wide banner) that set width+position in one click.
  The single menu image grew into an **image list**: each entry has a slot -
  three flow slots (Above title / Above entries / Below entries) that stack
  in list order and push the text down, Background (stretched + dark wash)
  and Overlay (drawn over the text at a freeform offset) - plus a scale
  multiplier and a px offset (nudge for flow images, absolute position for
  overlays). menubake::panelLayout() stays the single geometry source
  (baker + preview + menu_data codegen), now returning per-menu panelW and a
  `clipped` flag surfaced as a red warning in the editor when content would
  blow past the 512px texture cap. Legacy "image"/"imageMode" project.json
  fields load into the list. Editor: new Layout + Images sections with
  reorder arrows per image. Verified e2e: title menu converted to the new
  format (logo above-title + a gold gem overlay at offset 206,8 scale 0.8,
  screenPos 0.5/0.7) baked correctly (panel PNG inspected), menu_data
  carries the position (0.5F, 0.7F), and the PCSX2 boot screenshot shows
  the panel sitting low on screen with both images and the cursor tracking
  the moved rows. 128/512 widths and the presets follow the same layout
  path but had no dedicated boot test.

- (46) **File-dialog freeze fix + drag&drop import** - on this machine the
  shell-based IFileOpenDialog wedges (select file, click Open -> the button
  grays and the dialog never returns), and giving it an owner HWND
  (glfwGetWin32Window) did NOT cure it - kept anyway as correctness. Two-part
  fix: (1) all FILE pickers now use the classic comdlg32 GetOpenFileNameW
  (folder picking keeps IFileOpenDialog/FOS_PICKFOLDERS - no legacy
  equivalent); (2) a dialog-free path: glfwSetDropCallback accepts PNGs
  dragged from Explorer - they copy into res/hud and, with the Menu Editor
  open, attach straight to the selected menu's image list (status bar
  reports what happened). Root cause of the shell wedge unconfirmed
  (OneDrive/shell-extension suspicion); if the legacy dialog ever wedges
  too, drag&drop is the escape hatch. Needs the user's interactive
  confirmation - the freeze never reproduced under automation.

## Backlog (rough order)

- Hands-on pass over the Flow Graph editor UX (needs a human with a mouse)
- Object physics vs objects (stacking), player physics polish (pad feel)
- Model picking uses the unit-box approximation (big models pick imprecisely)
- HUD images draggable directly in the viewport
- Textured models (.mtl/PNG) and textured terrain
- Positional audio (volume falloff by distance to an object)
- Compressed music streaming (SPU2-native ADPCM/VAG, ~3.5:1 vs 16-bit PCM) -
  needs a custom double-buffered SPU RAM streamer in the engine; audsrv only
  streams PCM and plays ADPCM one-shots
- Flow graph: more nodes (timers with reset, variables)
- Engine perf, next targets: packager allocates its package array per frame
  (poolable); the real endgame is the engine author's own TODO in
  stapip_clipper.hpp - move clipping to VU1 entirely ("too much time")
