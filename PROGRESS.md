# Progress log

Living document: what is being worked on right now, what is done, what is queued.
Each finished feature lands as its own commit.

## In progress

- (nothing - the feature marathon batch is complete; see Backlog for next steps)

## Also done after the marathon

- (34) **Skeletal animation skinning moved from the EE FPU to VU0 (macro
  mode)** — the backlog's era-correct split (animation on VU0, 3D on VU1,
  game code on EE), engine-fork only, zero authoring/codegen changes. The
  `SkelInstance::skinParts` vertex loop is now COP2 inline asm (the
  `M4x4::cross` lqc2/vmadda pattern, no microprogram): matrix-palette blend,
  position+normal transform, vrsqrt normal normalization and the skinned
  AABB (vmini/vmax, kept resident in $vf20/$vf21 across the loop; the
  min.w slot carries the epsilon that replaces the old `len > 1e-6` guard).
  The constructor repacks bind data into 16-byte-aligned qwords (positions
  w=1, normals w=0 so the translation column drops out), prenormalizes the
  u8 weights to floats summing 1, and sorts each vertex's joints by
  descending weight so the loop dispatches on influence count: 1 bone = 4
  matrix loads and no blend, 2 bones = 2-matrix blend (the hot case: 95% of
  the test character), 3-4 = full blend, 0 = zero matrix (vertex collapses
  to the origin exactly like the EE loop). A first, dispatch-less version
  that always blended 4 matrices measured *slower* than the EE loop in
  PCSX2 (median EE 38.7% vs ~37%) - PCSX2's EE JIT prices a COP2 op like an
  FPU op, so op count is everything there; the dispatch version restored
  parity. Pose evaluation (mulM4/fromTrs/slerp) deliberately stays plain C
  for bit-parity with the editor's preview math. Verified (Docker + PCSX2
  SW renderer): skelpar frozen-pose scene (skinned char x2 poses + rigid
  flag, tint) pixel-compared EE vs VU0 - **548 900 game-area pixels, 0
  differ** (both the naive and the dispatch version); skelperf (3 animated
  1440-vert characters, 1368 verts with 2 influences / 72 with 1) holds 50
  FPS with EE median 37.0% (6 samples) vs EE-loop baseline 37.2% (6
  samples, range 34-41%) - emulator parity, the real win is expected on
  hardware where one COP2 FMAC does 4 lanes (real-HW run still pending the
  ps2link memory-card install). The 0- and >=3-influence paths compile but
  no test asset exercises them (the >=3 block is the pixel-verified naive
  blend with sorted slots). Docs updated (animated-models.md).

- (33) **Custom particle kind: full physics knobs (jets, leaks, steam)** —
  sixth emitter kind "Custom" exposes the simulation instead of a preset:
  **Speed** (u/s, +-20% jitter) along the emitter's **+Y axis rotated by the
  object rotation** (emitters gained the Rotation field for this - tilt 90
  deg = a horizontal pipe leak; the viewport cone marker rotates with it),
  **Spread** (cone half-angle built from a per-emitter orthonormal tangent
  basis), **Gravity** (u/s^2, negative = buoyant steam), **Weight** (air
  drag ~ 1/weight applied after the pull, so a natural terminal velocity
  emerges - heavy water keeps falling, light steam brakes to a drift),
  **Lifetime** (+-25% jitter), **Grow** (size multiplier reached at death),
  **Opacity** (base alpha, fades in the last quarter of life) and **Die on
  terrain** (per-frame terrainHeightAt check; the particle vanishes and
  respawns - water soaks into the ground instead of clipping through).
  Serialized inside the emitter JSON block only for kind "custom" (8 new
  SceneObjectData columns emitted for every object; defaults keep old
  projects identical); the editor preview runs the same math (shared
  rotateEuler matching the game's rotated()). Add-menu preset: a small
  water-like jet. Verified: editor builds clean; fixture got a "pipe-leak"
  (rot 90 deg X, speed 7, weight 3, die-on-terrain) and a "steam-1"
  (gravity -1.5, weight 0.25, grow 3, opacity 0.35) - generated
  scene_data.hpp rows checked column-by-column, full Docker build to ELF
  OK; editor screenshots show the water arc bending down onto the terrain
  and the growing translucent steam plume, plus the new Properties knobs
  (Speed/Spread/Gravity/Weight/Lifetime/Grow/Opacity + Rotation); PCSX2 SW
  renderer at 50 FPS renders both (240 live particles with the rain from
  (32)); a zoomed pixel check confirmed the steam stays neutral gray (a
  suspected color bug was a contrast illusion against the sky). Hands-on
  pass left for a human: knob feel while watching the live preview.

- (32) **Particle emitters v2: live viewport preview, rain, textures,
  enabled + follow-player** — emitters no longer render as scaled cones in
  the editor: the viewport now runs the same per-kind particle simulation as
  the generated game (spawn/velocity/size/alpha formulas copied from
  `updateParticles()` — the twin-formula comment marks both sides) on
  per-emitter CPU pools, drawn as alpha-blended camera-facing quads through a
  new small shader (pos + RGBA + UV dynamic buffer, depth-test on / z-write
  off, drawn last like the game). A fixed-size cone marker remains for
  selection/gizmo (dimmed when the emitter is disabled); picking still uses
  the scale box = spawn area. New **Rain** kind (4): thin world-up streaks
  (size = streak length) falling 14-20 u/s from the emitter height and dying
  exactly on the terrain (`terrainHeightAt` per drop), preset under the
  Effects add-menu (area 20x20 at y=12, count 96). New per-emitter options:
  **texture** via the shared Material combo (first material's map_Kd on
  fixed per-quad UVs through a StaPipTextureBag; color tints the texture,
  same in the viewport), **Density (count)** cap raised 128 -> 256,
  **Enabled** (off = starts invisible; the existing Show/Hide/Toggle Object
  flow nodes enable/disable emitters at runtime — that mapping is the
  documented on/off switch), and **Follow player** (the emitter position
  becomes an offset from the camera — rain that tracks the player instead of
  covering the map; editor previews it in place). Data model: emitterEnabled
  / emitterFollowPlayer (+ operator==), `"enabled"`/`"followPlayer"` in the
  emitter JSON block, `emitEnabled`/`emitFollow` columns in
  SceneObjectData. gl_loader grew glDepthMask. Verified: editor builds
  clean; scratch fpp project with a follow-player rain emitter + a disabled
  fire emitter carrying a material — generated scene_data.hpp row checked
  (kind 4, count 96, enabled/follow flags, material index resolved), full
  Docker build to ELF OK; GUI screenshots show animated rain streaks in the
  viewport, the dimmed disabled-fire marker and the new Properties fields;
  `.tyra` round-trips the new keys on reopen+resave; PCSX2 (SW renderer,
  50 FPS) shows rain falling around the FPP camera and the disabled emitter
  correctly absent. Hands-on pass left for a human: checkbox/combo feel and
  a textured emitter in-game (codegen + texture-bag path compiled and
  mirrors the terrain texturing, but no PNG-textured emitter was booted).

- (32) **Flow graph node overhaul** — one batch of user-requested block fixes.
  **Trimmed pins**: On Start / On Button / Every N Seconds lose their object
  and bool outputs, Near Object / On Used / On Animation Finished lose the
  bool output (the logic-gate plane keeps its pure sources: Value At Least,
  Get Bool, Int At Least, the new Is Visible); stale links whose pins no
  longer exist are pruned when a graph is opened - imnodes must never see a
  link to an unsubmitted pin. **On Button** now offers all 16 pad buttons
  (D-pad, L1-L3/R1-R3, Start, Select - codegen uses the PadButtons field name
  as-is). **Delay** (new "Time" category): exec-through action, fires its
  "after >" output N seconds later (fractions fine, everyFrames-based
  countdown member per node; re-trigger restarts). **Move Object To**: glides
  the target toward X/Y/Z or a live position link at Speed units/s
  (g_frameDt-based per-frame step; FlowNode::num grew to 4 floats for the
  Speed slot). **Play Animation clip picker**: the node resolves its target
  like codegen (object link chain > name > self) and offers the .glb's clip
  list from glbInfo instead of free text. **Object getters**: new pure
  Is Visible bool source; Self exposes a position output. **Save texts**:
  Project::saveTexts (name + default, "Save data" panel), stored in fixed
  32-byte slots in SaveGameData (SAVE_VERSION 2 - old card saves are
  rejected), zeroed/defaulted at boot, Set/Get Save Text flow nodes,
  ScriptContext::saveTexts. **Text link plane** (cyan pins, pin ids widened
  to 16 slots per node - never persisted, so safe): Log Message takes any
  number of text inputs (appended space-separated after its static text);
  pure text sources Get Save Value, Get Save Text, Get Int As Text, and a
  "Convert" category with Position To Text / Bool To Text. **Fixes**: Set
  Save Value's combo and drag were both labeled "Value" in one ID scope
  (ImGui conflict error) - numeric params now live in their own PushID;
  Value At Least / Int At Least explain in-node that they are per-frame bool
  sources for On Condition / gates; flow node str/str2 are JSON-escaped on
  save (quotes in Log messages used to corrupt the .tyra). Verified: editor
  builds clean; a scratch project exercising every new node round-trips
  through save/load (host harness incl. escaped quotes), generates the
  expected flow_graph.gen.cpp (inspected: delay countdown, mover blocks,
  TYRA_LOG concatenation, snprintf into saveTexts) and compiles+links on the
  PS2 toolchain in Docker; Flow Graph panel screenshot shows the new
  nodes/pins; samples/script-demo regenerated. In-game behavior (delay
  timing, mover feel, pad buttons) still needs a human PCSX2/pad pass.

- (31) **Per-stick deadzones + Project panel cleanup** — the Input deadzone
  splits into "Left stick deadzone" (movement) and "Right stick deadzone"
  (camera): worn pads rarely drift equally, and one shared value forced the
  healthy stick to pay for the drifting one. ANALOG_DEADZONE ->
  ANALOG_DEADZONE_L/_R in terrain_config.hpp, axis helpers take the deadzone
  per call site; a legacy "stickDeadzone" key in old .tyra files seeds both.
  The Project panel's Build button row is gone (superseded by the top-level
  Build menu) - the panel keeps only a build-progress/failure line. Verified:
  codegen emits both constants, game compiles on the PS2 toolchain, editor
  builds clean.

- (30) **ps2client TCP_NODELAY (~100x host: throughput), Build menu + Clean,
  orphaned-adpcm sweep** — three fixes from the second real-hardware session.
  **TCP_NODELAY**: file serving to the console ran at ~4 KB/s (424 KB texture
  = 106 s, boot = 10 minutes, streamed music sounded like a crashed GameBoy -
  the audsrv ring starved at 44.1 kHz stereo needing 176 KB/s). Root cause:
  ps2client sets no socket options and ps2link fileio is synchronous small
  request/response exchanges - Nagle on the PC side colliding with the PS2's
  delayed ACKs stalls every exchange ~200 ms. `tools/ps2client` now ships a
  patched binary (TCP_NODELAY on the request socket; nodelay.patch + README
  vendored, stock binary kept for comparison, .gitignore whitelists it).
  Measured on the console: the same texture in ~1 s, the same obj 59 s -> 1 s,
  full boot ~30 s. Music needs an ear re-check but now has ~10x bandwidth
  headroom. **Build menu**: VS-style top-level Build menu in the menu bar
  (Build Ctrl+Shift+B / F5 / F6 variants moved out of the Project dropdown;
  Project keeps Preferences + ISO/disc entries; the Project-panel buttons
  stay) plus **Clean** - wipes host bin\ and the container game volume's
  obj+bin, next build starts from scratch. **Orphan sweep**: .adpcm whose
  source WAV vanished from res/sfx (sound removed in the editor) lived
  forever in the container volume and the copy-back rsync resurrected them
  on the host every build - the sfx step now deletes them in-container.
  Verified: menu screenshot (entries, shortcuts, disabled-without-IP PS2
  items), deploy to the real console with the patched ps2client (timestamped
  logs above), editor + Docker game build clean after all changes.

- (29) **Real-pad feedback round: stick deadzone, HUD/particle race fix, PS2
  buttons in the Project panel** — three findings from playing on the real
  console. **Deadzone**: the hardcoded 0.20 stick deadzone becomes
  Preferences > Input > "Stick deadzone" (0-0.9, ANALOG_DEADZONE in
  terrain_config.hpp); motion above the threshold now rescales from zero
  instead of stepping at the edge, so raising it for a drifting pad only
  costs stick range. Applied in both player paths (walk/noclip); verified:
  codegen emits the constant and the game compiles on the PS2 toolchain.
  **HUD/particle race** (engine): on real hardware particles flickered out
  in a rectangle around the HUD crosshair every few frames. Sprites go out
  over PATH3 with only a GIF-channel wait, racing the tail of the async
  PATH1/VU1 scene; when the sprite won, it stamped z=max across its whole
  rect (transparent margins included) and the late particle quads z-failed
  inside it. Renderer2D now drains PATH1 once per frame before the first
  sprite (new RendererCore::drained3DFor2D, reset in beginFrame), gated on
  Path1::isVU1Configured like the post fx barrier so pure-2D frames (loading
  screen) cannot spin on a FINISH that VU1 never delivers. Verified in PCSX2
  SW renderer: fire emitter + centered HUD sprite, steady 50 FPS, no
  dropouts; the actual race needs a re-check on the console. **UI**: Build &
  Run on PS2 / Run on PS2 buttons in the Project panel's Build row (disabled
  with a tooltip until the ps2link IP is set), matching the Project-menu F6
  entries.

- (28) **Run on PS2 — real-hardware fixes (game verified running on a PS2)** —
  the first live deploys surfaced four independent landmines, each verified on
  the actual console:
  (a) **execee arguments never arrive** — ps2link passes args in a
  non-standard way that needs a newer newlib crt0 than the `h4570/tyra`
  toolchain image ships, so `-ps2link` silently never reached main() and the
  game reset the IOP, killing ps2link (symptom: rom0:ROMVER opened, every
  host: open dead). Detection now uses a `bin/ps2link.run` marker: written by
  the PS2 deploy, deleted by PCSX2 launches, probed by the game over host:
  BEFORE the engine boots (argv stays as a fallback). Excluded from ISOs.
  (b) **sbv_patch_fileio corrupts ps2link's fileio** — it pokes jumps at
  fixed offsets valid for Sony's ROM FILEIO; ps2link's environment runs
  PS2SDK's FILEIO_service reporting the same 1.1 version (the patch's only
  guard) with a different code layout. Skipped under keepIopResident, as are
  iomanX/fileXio loads (ps2link provides both; a second iomanX re-hooks ioman
  onto an empty device table).
  (c) **libpng's chunked freads over the network** made a 400 KB texture take
  minutes (every small read is an EE->IOP->TCP round trip): png_loader now
  reads the whole file with large sequential freads (no fseek - unreliable on
  host fs) and decodes from memory; verified in PCSX2 after the change.
  (d) **the shared engine volume races parallel checkouts** — every git
  worktree (parallel Claude sessions) rsyncs its own vendor/tyra over
  `tyra-engine-shared`, endlessly rolling back other checkouts' engine fixes
  (one game even compiled against headers another session had reverted
  seconds earlier). The volume name now carries an FNV-1a hash of the engine
  source path: same checkout shares, parallel checkouts isolate.
  QoL from the same session: Output-panel lines are timestamped (profiling
  slow console boots), TYRA_LOG streams live into the Output panel under
  ps2link (writeLogsToFile off - ps2link forwards EE printf over UDP), the
  post-build step drops stale bin/sfx WAV copies host-side (they shipped in
  ISOs) and warns when an sfx .adpcm exceeds 1 MB (SPU RAM is 2 MB; a 15 MB
  song imported as a sound effect was most of a 10-minute network boot).

- (27) **Run on PS2 — network deploy to a real console (ps2link)** — the game
  boots on a physical PS2 over ethernet straight from the editor: no ISO, no
  SMB/OPL. The console runs ps2link (one-time memory-card install; package +
  IPCONFIG.DAT prepared under F:\PS2\ps2link-mc for this machine) and the
  editor drives it with ps2client (`tools/ps2client`, fetched by setup.ps1
  alongside a ps2link release). Runner gains `buildAndRunPs2`: the usual
  Docker build, then `ps2client reset` + 3s + `execee host:<name>.elf
  -ps2link` with **cwd = bin/**, so the game's `host:` cwd maps to bin/
  exactly like a PCSX2 run and every asset is served from this PC over the
  network (the PS2 connects back to TCP 18193; logs arrive over UDP 18194 —
  both firewall rules created, Private profile, program-scoped). The execee
  process IS the file server, so it stays alive across the session (killed on
  the next deploy/exit; its output streams into the Output panel as "[ps2]"
  lines, giving live console logs in the editor). Engine:
  `IrxLoader::keepIopResident` skips the boot-time `SifIopReset` that would
  unload ps2link mid-boot; generated main.cpp sets it only when launched with
  `-ps2link`, so PCSX2 boots keep the stock reset path (zero regression
  surface). UI: Project > Build && Run on PS2 (F6) / Run on PS2 (no build),
  gated on a new editor-side pref "PS2 (ps2link) IP" (Preferences > Real PS2;
  persisted in the .tyra editor block); CLI: `--build <dir> --run-ps2 [ip]`
  (stays alive relaying console logs while the file server runs). Liveness
  gotcha: ps2link commands are UDP fire-and-forget — reset/execee "succeed"
  against a dead IP — so success is declared only when the console's first
  log line arrives (15s window), verified both ways: offline IP now fails
  with a helpful message (was a false "Game running"); PCSX2 path re-verified
  end-to-end after the engine change (SW renderer, 50 FPS). Real-hardware
  run pending the one-time ps2link memory-card install.

- (26) **Animated models stage 2: true skeletal runtime (.tskl) + crossfade
  blending** — the baked-morph-frame backend from (25) is replaced by a real
  skeletal one; the entire authoring surface (.glb import, clip names,
  Properties fields, flow nodes, script API, RuntimeObject anim fields and
  animFinished semantics) is unchanged. The editor now serializes what
  `glbparser` always parsed instead of sampling it: the .glb parse was
  split into a shared `parseGlb` (used by both `bake()` - still the
  viewport-preview/import-validation path - and the new `parseSkel`), and
  `writeTskl` emits a `.tskl` binary: node hierarchy with bind-pose TRS,
  matrix palette (skin joints with IBMs; rigidly-animated mesh nodes become
  weight-255 identity-IBM slots, so rigid + skinned share ONE runtime path
  and .tanm writing is gone), keyframe tracks (times f32, rotations
  16-bit-quantized quats, constant tracks collapsed to 1 key, CUBICSPLINE
  degraded to linear like stage 1, times rebased per clip) and the expanded
  bind-pose triangle lists with u8 joints/weights (sum-normalized to 255,
  max 256 palette slots). Engine fork additions: `TsklLoader` (whole-file
  read, bounds-checked, validates hierarchy/palette/joints once at load) and
  `SkelInstance` — per-object playback (current + fading layer, per-channel
  O(1) key cursors), EE pose evaluation (slerp/step sampling identical to
  the editor's math, parents-first hierarchy walk, palette = global x IBM),
  crossfade as a local-TRS blend (lerp T/S, sign-fixed nlerp R) before one
  hierarchy walk, and EE skinning of positions + normals into an owned
  single-frame DynamicMesh that DynPip renders as verticesFrom==verticesTo
  (interpolation 0) — zero new VU1 code, stage-1 lighting/tint/texture
  plumbing reused, frame bboxes refreshed from the skinned AABB every skin.
  Codegen: `GameAnimModel` holds the shared SkelModel + per-part textures,
  `setupAnimObject` builds a SkelInstance per object (fresh material ids
  re-linked, ambient = part color x object color), the render pass applies
  animRestart via `play(clip, loop, fade)` and advances by `g_frameDt *
  animSpeed` (wall-clock, PAL/NTSC-safe); collision still uses the clip-0
  frame-0 AABB (now stored in the .tskl). New surface (M2): optional
  **Fade** param on Play Animation (seconds, 0 = instant), `playAnimation(
  ..., fade)` default arg + `RuntimeObject::animFade` — both backward
  compatible; docs/animated-models.md rewritten for the skeletal pipeline.
  Import status now reports the skeletal RAM estimate. Verified: host
  harness + python cross-checker re-evaluates the .tskl pose/skinning math
  at every stage-1 baked frame time and compares vertices (generated
  skinned+rigid 2-clip .glb: 180 comparisons, and a 1440-vert 4-bone 2-clip
  character: 89 280 comparisons; worst pos err 9.5e-4 = quat quantization);
  e2e in Docker+PCSX2 SW renderer: frozen-pose scene rendered by stage-1
  (.tanm, editor built from origin/main) vs stage-2 (.tskl) differs in 17
  of 580 800 pixels (0.003%, sub-pixel silhouette shifts); 3 animated
  1440-vert characters hold 50 FPS / 100% speed (3 samples, EE 34-37% vs
  stage-1's 40%); debug free-RAM overlay: 27.7 MB free vs 23.8 MB stage-1
  (+3.9 MB from one character model; .tanm 2.15 MB -> .tskl 60 KB on disk);
  crossfade: flow graph EverySeconds -> Play Animation(fade 2s) captured in
  0.4s-burst screenshots shows a smooth twist->wave blend with consecutive-
  frame pixel deltas in the same band as normal playback (no pop; fade
  codegen inspected in flow_graph.gen.cpp). Sample regenerated + rebuilt in
  Docker. VU1 skinning (M3) stays in the backlog — EE headroom says it is
  not yet the bottleneck.

- (25) **Animated models: .glb import baked to PS2 morph frames (stage 1)** —
  the engine's dormant dynamic pipeline (DynPip: MD2-style two-frame VU1
  interpolation) is now wired end to end. OBJ has no animation, so animated
  models come in as **.glb** (Blender: glTF Binary export): the new
  `src/glbparser.cpp` (hand-rolled on top of json.cpp, no new deps) parses
  the GLB container, samples every named clip at 12 fps, CPU-skins the
  vertices (4-joint matrix palette, rigid node animation too, LINEAR/STEP +
  CUBICSPLINE fallback) and, at every build, `refreshGenerated` writes a
  compact `.tanm` binary + extracted PNG textures (JPEG transcoded) next to
  the source. Engine grew `TanmLoader` (fork addition; whole-file read, no
  fseek) building a `DynamicMesh`, and a fix for upstream's out-of-bounds
  `DynamicMeshAnimation::restart()` (double-indexed the sequence - crashed
  for any clip not starting at frame 0). The generated game keeps StaPip and
  DynPip initialized side by side and swaps VU1 programs per frame
  (`reinitVU1Programs`; `usePipeline` would reallocate everything), renders
  animated objects with per-instance DynamicMesh copies (shared frames, own
  clip state, object-color tint via the copy's material ambient) and a
  directional light matching the baked static look (point lights stay
  baked-only). Per-object data: start clip / autoplay / loop / speed
  (serialized under `"anim"`); collision uses the baked frame-0 AABB (box
  only). Scripts get `playAnimation/stopAnimation/animationFinished` +
  `ctx.resolveClip`; the flow graph gets **Play Animation**, **Stop
  Animation** and the **On Animation Finished** trigger (End + every loop
  wrap, via the engine callback). The editor previews clips in the viewport
  (CPU lerp into dynamic VBOs, same math as VU1) and shows clips/warnings in
  Assets + Properties (memory estimate warns above ~8 MB baked).
  Verified: glbparser unit-tested against a generated skinned+rigid 2-clip
  .glb (frame counts, AABB, skinning spot-checks); full e2e in Docker+PCSX2
  (SW renderer, 50 FPS steady): two textured animated objects play different
  clips ("bend" visibly bends between screenshots), flow-graph
  Play/Stop/OnFinished compile into flow_graph.gen.cpp and run without
  crashing, editor viewport plays the same clips with textures. Hands-on
  pass still worth doing: Properties clip combo + gizmo feel on animated
  objects (screenshots could not click the UI). Stage 2 (true skeletal
  runtime) is specced in the Backlog.

- (24) **Texture quantization: project-wide palette textures + per-asset
  quality override** — the PS2-native "texture compression". The GS has no
  DXT-style format; era games shipped palettized PSMT8/PSMT4 textures, and
  the engine's PNG loader already eats indexed PNGs directly - so the editor
  now produces them. **Preferences > Rendering > Textures** picks the
  project-wide quality (full 32-bit / 256 colors / 16 colors - new projects
  default to 4-bit, existing ones load as "none" so their output never
  changes silently), and every model/material row in **Assets** gets a
  quality combo override; when several assets share a texture the HIGHEST
  requested quality wins - "everything 4-bit, but the hero stays full color"
  works per design. Non-destructive: sources in res/ are never touched; a
  build-time bake (runner, before the docker sync) mirrors res/ into
  `.res-baked/` quantizing PNGs per policy (hud/fonts exempt - UI
  legibility), and the generated Makefile's RESDIR now points at the mirror.
  New `src/pngquant.cpp`: median-cut over RGBA (pixel-weighted, alpha
  counted double), Floyd-Steinberg dithering when lossy, lossless
  pass-through for images already within the palette budget, and a
  hand-rolled indexed PNG writer (PLTE + tRNS, deflate via stb's zlib)
  matching what the engine's 4/8bpp paths expect (even width required for
  4bpp). `src/texbake.cpp` resolves the per-PNG policy by parsing every
  .obj/.mtl asset (objparser), mirrors/cleans the bake dir and reports
  counts to the build log. Verified: pngquant host tests (16 asserts: IHDR
  depth/type, palette bounds, alpha hole survives, opaque stays opaque,
  2-color image lossless); e2e in PCSX2 (SW renderer, 50 FPS): global 4-bit
  + walls.mtl pinned to full - .res-baked shows models/bricks.png as
  depth-4 type-3 (623 -> 136 B) while the pinned material stays 32-bit, HUD
  untouched, and two boxes side by side (4-bit palette vs full color)
  render identically from the GS's PSMT4 and 32-bit paths. Editor viewport
  still previews the full sources (quantized preview = follow-up; the
  Preferences/Assets combos need a hands-on GUI pass). Sample regenerated
  (Makefile RESDIR + .gitignore .res-baked).

- (23) **Materials replace per-object textures** — the loose "slap a PNG on
  an object" texture is gone; .mtl material libraries are the one texturing
  mechanism. Every solid object gets a **Material combo** in Properties
  listing the project's .mtl assets (a new `res/materials` folder for
  universal libraries + the models' own mtls under `res/models`):
  primitives take the file's FIRST material (Kd tint + map_Kd on their UVs,
  still modulated by the object color), models use the assigned .mtl as an
  **override** that replaces their own libraries (usemtl names resolve
  against it) - one "walls" library can repaint/retexture many objects.
  Data model: `SceneObject.texturePath` -> `materialPath` (old "texture"
  keys are dropped on load). Codegen: models are keyed by the (obj, mtl)
  PAIR (`MODEL_PATHS` + `MODEL_MTLS`), primitives get `MATERIAL_PATHS` +
  `SceneObjectData.material`, and the game grew `loadMaterials()` (first
  material of each library: Kd + probed map_Kd via the texture repository).
  Engine: `LeanObjLoader::load` takes an optional override .mtl (replaces
  mtllib/sibling; textures then resolve relative to the override) and a new
  `LeanObjLoader::loadMtl` parses standalone libraries - both mirrored in
  the editor's objparser. Editor: viewport draws primitive materials and
  model overrides identically to the game; the Assets section swaps its
  Textures list for **Materials** (res/materials, per-file material summary
  + missing-texture flags, `Import .mtl...` copies the library with its
  textures, references rewritten); `res/textures` survives only for the
  tiled terrain texture (its Pick... popup gained the Import PNG... item).
  Verified in PCSX2 (SW renderer, 50 FPS): a box assigned `walls.mtl`
  renders brick-textured, and a model assigned `repaint.mtl` switches from
  brick walls + dark roof to green walls + yellow roof (usemtl-name match);
  the editor viewport shows the identical result, the Material combo and
  the red MISSING flags in Properties. fpp + empty presets rebuilt clean in
  Docker; sample regenerated.

- (22) **Missing textures fail soft + are visible in the editor** — a model
  whose .mtl referenced a texture that never made it into the project used to
  kill the game at boot ("Failed to load ... png_loader.cpp:39" assert - the
  texture repository trusts its callers). The generated game now probes every
  texture file first (models AND the scene TEXTURE_PATHS): a missing one logs
  a TYRA_WARN and the affected part draws in its Kd/object color. The editor
  surfaces the problem instead of hiding it: the Properties material summary
  paints missing textures red ("walls (textures/t.png) - MISSING" + a hint
  that paths resolve relative to the .obj), and the Assets section flags such
  models with a "missing textures!" marker (tooltip lists the paths).
  Texture rows in Assets also got a hover thumbnail (PNG preview + size,
  reusing the HUD texture cache). Verified: reproduced the crash scenario
  (res/models/tower/tower.obj with map_Kd textures/t_C_3.png, no such file) -
  the game now boots at 50 FPS with "Model texture missing:
  models/tower/textures/t_C_3.png" in the game log; editor builds clean.

- (21) **Sibling-.mtl matching, asset subfolders, Add-menu restructure** —
  three usability follow-ups. **Implicit MTL**: a `.mtl` named like the `.obj`
  next to it is picked up even without a `mtllib` line (the common exporter
  convention); explicit mtllib files still parse afterwards and win on name
  clashes. Implemented in BOTH parsers (editor `objparser` + engine
  `LeanObjLoader` - they must stay in sync) and the import copies the implicit
  library like an explicit one (sanitized stems keep matching). **Subfolders**:
  asset listing/pickers (`listAssetFiles`) and the audio rescan are recursive,
  so `res/models/props/tree.obj` or `res/sfx/steps/wood.wav` just work; the
  Runner's adpenc loop covers two levels of sfx subfolders (glob fan-out - the
  quoting-hostile docker/cmd pipeline rules out find) and the bin/sfx WAV
  cleanup follows. Codegen/ISO paths already carried full relative paths.
  **Menus**: the add palette starts with `Object -> Simple / Model` (instead
  of a top-level Simple and a separate Model menu), and the top-bar Scene
  menu nests everything under `Scene > Add`. Verified: parser host test (obj
  without mtllib gets both materials from the sibling); PCSX2 run at 50 FPS
  with mtllib stripped from the test house (bricks still textured = engine
  sibling matching) plus a model under `res/models/props/` with its own
  mtl+texture (loads clean - no LeanObjLoader warnings in the game log);
  editor builds clean.

- (20) **Pick-from-project asset flow + Assets section + MTL visibility** —
  the object/terrain pickers no longer open file dialogs: textures pick from
  `res/textures` (object texture, Project Preferences and Scene Preferences
  terrain texture - all through one `pickProjectTexture()` popup), model
  objects get a **Model combo** over `res/models`, sounds already picked from
  the project list. Importing moved to one place: a new **Assets** section in
  the Project panel lists `res/models` (with tri/material counts) and
  `res/textures` straight from disk, with `Import .obj...` / `Import PNG...`
  buttons (model import copies the .mtl + textures as before but no longer
  auto-creates an object - add it from the Add menu's Model submenu, which now
  only lists project models). The Music/Sounds/HUD import buttons are renamed
  `Import...` so it is obvious they copy into the project. Since materials are
  a property of the .obj/.mtl file (not of the object), the Properties panel
  now shows a read-only summary for models: triangle count + each MTL material
  with its map_Kd texture or "(color)". Verified: editor builds clean; GUI
  screenshot shows the Assets section, the Model combo, the materials summary
  ("walls (bricks.png), roof (color)" for the test house) and the Pick...
  texture flow on the mtltest project.

- (19) **Asset import rework: drop-into-res + rescan, in-editor WAV converter** —
  assets no longer have to go through the import dialogs. WAVs dropped by hand
  into `res/audio` / `res/sfx` are picked up by a rescan (runs on project open
  + Rescan buttons in the Music/Sounds sections); entries whose file vanished
  are removed like a manual delete (flow-node references cleared). The Add
  palette's Custom menu lists `.obj` files already in `res/models` ("From
  res/models", no copy), and the object Texture row gets a "Project..." picker
  over `res/textures`. **WAV converter** (`src/wavconvert.cpp`): rewrites any
  readable WAV (integer PCM 8/16/24/32-bit + 32-bit float, mono/stereo, box
  low-pass on downsample) as 16-bit PCM **in place** - the project keeps one
  copy of each asset instead of source+converted pairs. Sfx imports convert to
  22050 Hz automatically; music converts only unplayable formats (float/24-bit
  / out-of-range rates); hand-dropped files get a warning marker + Convert
  button (format checks cached per file, not per frame). Also stopped shipping
  dead weight: the Runner deletes `bin/sfx/*.wav` after adpenc (the Makefile's
  `cp -r res/*` used to leave source WAVs next to the .adpcm, tripling each
  sfx and landing on the ISO), and the ISO exporter skips `bin/log.txt`.
  Verified: converter round-trips checked by a host harness (44.1k float
  stereo, 8-bit 11k mono upsample, 24-bit 48k downsample - format + RMS of a
  sine preserved; garbage rejected, original untouched); editor builds clean;
  Rescan buttons + pickers visible in the GUI. Hands-on drop-a-file-and-rescan
  pass left for a human.

- (18) **MTL materials, runtime model loading and mesh collision** — models
  went from "baked gray blob" to the full 2002 experience. **Engine** (new,
  marked "Added by tyra-editor"): `LeanObjLoader` - a lightweight OBJ+MTL
  loader (per-material split, Kd + map_Kd, flat per-face normals and V-flip
  matching the editor parser 1:1, sequential reads - no fseek, all paths
  through `FileUtils::fromCwd` so host: and cdrom0: both work);
  `CollisionMesh` - triangle-soup collider with an XZ uniform grid
  (raycast + resolveSphere with a walkable-slope filter); `Ray::intersectTriangle`
  (Moller-Trumbore). Plus a real bug fix: `TyraDebug::writeInLogFile` opened
  `cdrom0:LOG.TXT;1` for WRITE on every TYRA_LOG when booted from a disc image,
  wedging the CDVD driver before the first frame - guarded to skip read-only
  media (this had silently broken every ISO boot of a logging game).
  **Editor**: `objparser` now reads mtllib/usemtl/Kd/map_Kd into per-material
  submeshes + model AABB; the viewport renders one part per material (map_Kd
  textures, Kd baked into vertex colors); model import copies the .mtl and its
  textures next to the .obj, rewriting references to the sanitized names.
  **Codegen**: `model_data.gen.hpp` no longer bakes vertices into the ELF
  (3000-tri cap gone) - it emits `MODEL_PATHS` and the game loads models once
  at startup via LeanObjLoader, one StaPip bag per material part (object
  texture still overrides all parts), per-material textures de-duplicated
  through the TextureRepository. **Collision modes** per object ("collision"
  in the .tyra, combo/checkbox in properties): Box (default; models now use
  their real mesh AABB instead of the unit scale box), Mesh (models: the
  player walks the triangles - ground by a local-space downward raycast,
  steep faces push a chest-height sphere out; honors full rotation + scale),
  None (decoration). Both walkers (FPP template + Player entity) share one
  `collidePlayer()`; emitters/markers no longer block the FPP player.
  Verified: CollisionMesh host tests (12 asserts: ramp heights, wall push,
  slope threshold, 2048-tri grid); editor + PCSX2 SW renderer at 50 FPS - two
  houses with brick map_Kd walls + dark red Kd roof, one rotated 30 deg,
  identical in the viewport; mesh collision proven numerically via TYRA_LOG
  (teleport above the rotated house -> rests at exactly y=2 = roof; spawn
  0.05 into a wall -> pushed out to the analytically predicted XZ to 5
  decimals); ISO export boots from cdrom0: and renders (models + MTL + PNG
  loaded from the disc). Interactive walk-into-walls pad feel left for a
  human.

- (17) **Two project presets + per-scene override of scene-visual settings** —
  tidy-up of project creation and preferences. **New project presets** cut from
  three (orbit / fpp / showcase) to two: `empty` (orbit camera, no objects) and
  `fpp` (FPP game template + a single Player entity, nothing else). `create()`'s
  `gameTemplate` arg became `preset`; the showcase content (house/pillar/HUD/two
  flow graphs), the FPP spawn+box+ball seed and the CLI `[orbit|fpp|showcase]`
  are gone (`--new ... [empty|fpp]`). **Per-scene overrides**: the scene-visual
  half of `ProjectSettings` (lighting, sky, clipping, terrain texture, post-FX,
  usable-highlight) can now be overridden per scene. `SceneData` dropped its
  loose lighting/terrainTexture fields for a full `ProjectSettings settings` +
  `SceneOverrides overrides` (one bool per category, all off by default);
  `project::resolvedSettings(p, scene)` returns the project defaults with each
  active category swapped for the scene's values. Everything downstream reads
  through it: the viewport (`applyProjectToViewport`), ISO export, and codegen.
  A new **Scene > Scene Preferences** dialog mirrors Project Preferences with an
  "Override project settings" checkbox per category — off = the widgets are
  grayed and preview the inherited project value, on = editable from that value.
  Project Preferences now edits only the project-wide defaults (it no longer
  writes lighting into the active scene, which also fixed a latent bug where the
  loader unconditionally overwrote every scene's lighting from the project
  settings, so per-scene lighting never survived a reload).

  Codegen: sky/clipping/post-FX/highlight moved from scalar `constexpr` in
  `terrain_config.hpp` to `SCENE_COUNT` arrays in `scene_data.hpp` (like the
  existing per-scene lighting), reached through `SKY_R = SKY_RS[g_activeScene]`
  style accessor macros. Those macros (and the whole SCENE_*/TERRAIN_* set)
  moved out of the game-cpp prolog into `scene_data.hpp`, and `g_activeScene`
  became a real extern global (defined once in the game cpp) — so user scripts,
  which include `scene_data.hpp` via `script.hpp`, keep seeing `SKY_R` etc.
  (the first Docker build caught this: the example script failed to compile
  until the macros were visible to its TU). `loadScene` now re-applies the
  scene's clip mode, sky horizon/clear color and bloom/grain on every switch,
  not just at boot.

  Verified: editor builds clean; headless `--new` gives empty (0 objects) and
  fpp (1 player) as expected; the `.tyra` round-trips the new `settings` +
  `overrides` blocks (load exercised by `--build`). Docker-built both variants
  to an ELF. A hand-authored 2-scene project (scene 1 overriding sky black +
  highlight on, scene 0 inheriting) generated the right split arrays
  (`SKY_RS = {63.75, 5.1}`, `HIGHLIGHT_USABLES = {false, true}`, while
  un-overridden `CLIP_PRECISES`/`SCENE_BRIGHTNESSES` stay equal) and still
  compiled + linked. Not yet booted in PCSX2: the runtime *visual* effect of a
  per-scene switch (loadScene re-application) and the Scene Preferences dialog's
  graying are the standard hands-on human checks.

- (16) **Single `.tyra` project file + per-project window layout** — normalized
  the on-disk project. Previously a project was `project.json` (game data,
  tracked) plus a gitignored `<name>.tyra` solution (editor state + undo) plus
  binary `terrain-*.heights`, and the ImGui window layout lived in a global
  `imgui.ini` in the cwd (shared across all projects). Now the whole project is
  one `<name>.tyra` file: game data + editor-side state (selection, gizmo, view
  mode) + the ImGui docking layout, so the window arrangement is restored per
  project. `project.json` is gone (no backward compat); `load()` finds the
  single `*.tyra` in the dir. The undo history moved to a sidecar
  `<name>.history` (JSON, gitignored — churny, rewritten on every edit); heights
  stay binary sidecars as before. `io.IniFilename` is nulled so ImGui never
  writes `imgui.ini`; the layout is captured via `SaveIniSettingsToMemory` into
  the `.tyra` whenever ImGui settles a layout change (and on graceful exit), and
  applied via `LoadIniSettingsFromMemory` in `attachProject`. Generated
  `.gitignore` drops `*.tyra` (now the tracked source) and adds `*.history`.
  `saveSolution/loadSolution` → `saveHistory/loadHistory`; a `jsonEscape` helper
  handles the layout's newlines/brackets. Sample migrated (`project.json` →
  `script-demo.tyra`, pure rename). Verified headless (`--new showcase`: exactly
  one `.tyra` + one `.history`, no `project.json`, `.gitignore` correct) and in
  the GUI: opened the project (loads fine), the live layout autosaved into the
  `.tyra` (`"layout"` grew 14→1036 chars, no `imgui.ini` written anywhere), then
  hand-widened the stored Project panel (383→700), reopened, and the wider panel
  was honored — proving the load→apply→save→reload round-trip. Migrated sample
  also opens correctly (title, objects, viewport). Interactive drag-to-rearrange
  feel is the standard human check; the persistence mechanism is verified.

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

- (47) **Per-menu fonts + text sizes** - menus pick their typeface and text
  scale. GameMenu::fontPath: "" = the default chain (Consolas Bold ->
  Arial Bold -> Arial), "res/fonts/x.ttf" = a font imported into the project
  (travels with it - reproducible builds), bare "impact.ttf" = a stock
  Windows font resolved via \Windows\Fonts. The Font combo in the Menu
  Editor lists project fonts, an existence-checked curated set of stock
  Windows faces (Arial/Comic Sans/Courier/Georgia/Impact/Segoe/Times/
  Trebuchet/Verdana bolds) and an "Import TTF..." action; dropping a
  .ttf/.otf on the editor window imports it and assigns it to the selected
  menu (same flow as PNG drops). Title and entry pixel sizes are editable
  (10-48 / 8-32); the entry size drives the row pitch (rowH = entrySize+9)
  and the title size the title block, all through menubake::panelLayout -
  MenuData.rowH was already per-menu data, so the game cursor follows
  automatically. menubake now caches fonts per path (map) instead of one
  static; a missing/unreadable font falls back to the default chain rather
  than failing the bake. Verified: bake + codegen check with impact.ttf and
  enlarged sizes on the e2e project (panel PNG + menu_data row geometry),
  PCSX2 boot screenshot.

- (48) **Output panel autoscroll fix** - the Output window now reliably sticks
  to the bottom as new build/launch lines arrive, and lets go the moment the
  user scrolls up (to read or select). The old implementation reconstructed
  InputTextMultiline's internal child-window name and drove it via
  FindWindowByName/SetScrollY - fragile against ImGui internals and it silently
  did nothing. Replaced with the canonical pattern: our own scrolling BeginChild
  owns the scrollbars, the read-only InputTextMultiline is sized to its content
  (still mouse-selectable) inside it, and SetScrollHereY(1.0f) fires only while
  GetScrollY >= GetScrollMaxY (one-frame lag keeps us pinned across appends).
  Note: this shows only the editor's build/launch pipeline; in-game TYRA_LOG
  (Flow Graph "Debug > Log Message") is printf on the EE and lands in PCSX2's
  console / emulog.txt, not here (PCSX2 is launched without an inherited pipe).
  Verified: clean build; behavior matches ImGui's official log-autoscroll
  example (live confirmation needs an interactive editor run).

- (49) **Sound emitters silent for filenames with spaces** - an autoplay sound
  emitter with a sound whose file had a space in the name (e.g. "Norwegian
  Horror Saga.wav") produced no audio in-game. Root cause was the wav->adpcm
  conversion loop in runner.cpp: `$f`/`$o` were unquoted, so a spaced filename
  word-split into multiple arguments - `basename` errored ("extra operand"),
  `[ ! $o -nt $f ]` errored ("too many arguments") which the `if` read as false,
  and adpenc was silently skipped. No `.adpcm` was produced, so at runtime
  `audio.adpcm.load` failed and the emitter played nothing. Inner double-quotes
  around the variables would be the obvious fix but cannot survive the
  cmd.exe /S + docker.exe argv unquoting layers (see Runner::exec), and single
  quotes would block expansion - so the loop now sets an empty `IFS`, which
  disables word splitting on the unquoted expansions while leaving the `*.wav`
  glob (pathname expansion, IFS-independent) intact. Verified: reproduced the
  broken loop locally to confirm the skip; editor rebuilt; `--build` on
  F:\Tyra-Projects\new-new-york now runs adpenc on the spaced file and produces
  bin/sfx/"Norwegian Horror Saga.adpcm" (16 MB) next to the ELF where before
  only the raw .wav was present.

- (50) **Sanitize asset filenames on import** - belt-and-suspenders for (49):
  every asset copied into res/ now runs its filename through
  `sanitizeAssetName` (app.cpp), which folds anything outside [A-Za-z0-9._-] to
  '_'. Applied at all import sites - models, object/terrain textures, HUD
  images, music, sound effects, menu fonts/images, and the Explorer drag-drop
  handler - so both the copied file and the relative path stored in the model
  use the same pipeline-safe name. This keeps spaces (and shell/ISO-special
  chars) out of the adpenc loop, Makefiles and the ISO9660 writer, which stores
  identifiers verbatim and so relies on clean input (sanitizing inside the ISO
  writer instead would desync the on-disc names from the paths baked into the
  game). Note: only affects newly imported assets - a project that already
  references a spaced file keeps that name until re-imported (runtime still
  works via the (49) hostfs fix). Verified: editor rebuilds clean; each import
  site audited to use the sanitized name for both the copy destination and the
  stored path.

- (51) **Debug window: game + emulator logs, configurable emulator path** -
  added a dockable "Debug" window (tabbed next to Output) that tails a log file
  from disk so game output is visible without leaving the editor. Two selectable
  sources: (a) **Game log** - the game's own `TYRA_LOG`/`TYRA_WARN`/`TYRA_ERROR`
  output and assertion dumps, and (b) **Emulator log** - PCSX2's `emulog.txt`
  (boot progress, BIOS/ELF-load errors). The window reads the last 1 MB on a
  Reload button or, while "Auto" is on, at most twice a second (per-frame reads
  would be wasteful on a large log), and follows the tail unless the user
  scrolls up (Output-window pattern). "Clear log" best-effort truncates the file.
  The game log is the key part: `TYRA_LOG` was previously a dead channel because
  it `printf`s to the EE console, which does not reach emulog (see tyra-testing),
  and the runner builds with plain `make` (no `NDEBUG`, so the macros are *not*
  stripped). Tyra already knows how to append logs to a host-side `log.txt`
  (`TyraDebug::writeInLogFile` via `FileUtils::fromCwd`) when the
  `Tyra::Info::writeLogsToFile` static is set - generated games just never
  flipped it. So the generated `src/main.cpp` bootstrap (templates.cpp) now sets
  it before constructing the Engine, and `main.cpp` was moved into
  `refreshGenerated`'s always-regenerated set (it is a 6-line editor-owned entry
  point; game logic lives in the ownable `terrain_game.cpp`/scripts) so existing
  projects pick it up on the next build. `bin/log.txt` is deleted before each
  launch (runner) so the window shows only the current run; no engine fork edit
  was needed. Because all 54 engine `TYRA_LOG` sites are init-time or edge-case
  warnings (none per-frame), routing them to a file is safe. Also added
  `Project > Preferences > Emulator`: a PCSX2 executable path (Browse.../Clear).
  The path is editor-side state (stored in the `.tyra` `editor` object next to
  selection/gizmo/layout, escaped like `layout` - NOT in ProjectSettings, so it
  is never baked into codegen, never per-scene, not part of undo); both
  Build && Run and the emulator-log lookup prefer it over the Program Files
  auto-detect (`resolveEmulator`), and a configured-but-missing path reports
  itself specifically instead of silently falling back. Verified: clean rebuild
  (all TUs); `--new` project serializes `"emulatorPath": ""` and stays valid
  JSON; a backslash Windows path written into the `.tyra` round-trips (jsonEscape
  `\`->`\\`, parser `\\`->`\`) and `--build` loads it and reaches the build stage
  with no parse error; fresh `--new` `main.cpp` carries the
  `Tyra::Info::writeLogsToFile = true` line, and a deliberately overwritten
  `main.cpp` is rewritten back by `refreshGenerated` on the next `--build`. NOT
  verified here (needs a PCSX2 + BIOS run, which launches an emulator window on
  the active machine): the actual host-fs write of `log.txt` from the running
  ELF and the live tail rendering. This reuses upstream Tyra's own logging path,
  so the host-fs write is expected to work where asset *reads* already do
  (HostFs is read-write), but it wants a hands-on run to confirm end to end.

- (52) **Sound emitters: oversized samples crash/silent - safe load + SPU2
  budget guard** - after (49)/(50) fixed the spaced-filename conversion, a
  reimported sound still played nothing. Root cause was the sample itself: a
  ~5 min stereo 44.1 kHz WAV (56 MB) becomes ~16 MB ADPCM, but sound emitters
  are audsrv one-shots loaded whole into SPU2's ~2 MB sample RAM, so it can
  never fit. Worse, the engine's `AudioAdpcm::load` read the file into a
  variable-length array on the EE stack (`u8 data[size]`) sized via fseek/ftell
  (unreliable over host fs) - a guaranteed stack overflow / bogus size for
  anything large, and it ignored the audsrv load result. Three changes:
  (a) engine `audio_adpcm.cpp` - read incrementally into a heap buffer (no
  fseek/ftell, no stack VLA), check the audsrv result, and return nullptr on
  any failure; `tryPlay` treats null as a benign no-op. (b) codegen - the
  emitter update loop skips null samples. (c) editor - import estimates the
  ADPCM footprint (~2/7 of the WAV) and warns when a single sound exceeds the
  ~2 MB SPU2 budget, and the Sounds panel shows each sound's estimated size and
  a running "SPU2 sample RAM: ~X / 2.0 MB" total that turns red over budget.
  Verified end-to-end: built a scratch fpp project with a short mono 22050 Hz
  tone on an autoplay emitter; libtyra rebuilt with the engine change (clean
  compile + link in Docker), beep.wav -> 5 KB adpcm, game boots at 50 FPS, and
  the WASAPI render-endpoint peak meter read a sustained 0.44 while running and
  0.00 after quit - i.e. the emitter is audible. The oversized new-new-york
  sample now degrades to silence without crashing (game boots fine). Editor UI
  warning path is compile-verified (native file dialog can't be driven headless).

- (53) **Positional stereo for sound emitters (audsrv upgrade + panning)** -
  emitters played dead-center regardless of position: the image's PS2SDK ships
  an old audsrv whose only ADPCM volume call sets the SPU2 voice's L and R
  levels equally, and audsrv has no pan RPC at all. Upstream ps2sdk added
  per-channel L/R in `66ae317d` ("Implement positional audio in adpcm",
  2023-01: IOP `audsrv_adpcm_set_volume(ch, voll, volr)` + EE
  `audsrv_adpcm_set_volume_and_pan(ch, vol, pan)` with pan -100..100), but the
  audsrv build system was later rewritten to need srxfixup + a newer toolchain
  (`00f199ae`, 2025-01) that the image cannot run. Solution: built audsrv from
  the last pre-srxfixup commit (`e78a9cb2`, pinned; builds with the image's
  `mipsel-ps2-irx-` toolchain and has the pan feature) and vendored the three
  artifacts in `vendor/tyra/audsrv-pan/` (audsrv.irx + libaudsrv.a + audsrv.h,
  README has the rebuild recipe; marked binary in .gitattributes so the
  vendor/tyra LF rule can't corrupt them). The runner overlays them over
  `$PS2SDK` at the start of every build. Engine: `AudioAdpcm::setVolumeAndPan`
  added; old `setVolume` still compiles via the header's 2-arg back-compat
  macro (centered). Codegen: `updateSoundEmitters` computes pan by projecting
  the horizontal emitter direction onto the camera's right axis (same right
  vector as the particle billboards) and calls setVolumeAndPan.
  Two pitfalls burned into the runner logic: (a) the `.irx-em` make rule
  depends only on the `.irx-em` text file, NOT the IRX binary it embeds - so
  swapping the SDK's audsrv.irx did nothing until the stamp check also deletes
  `obj/irx/audsrv.o` (bin2s re-runs, libtyra re-embeds). Diagnosed via nm: the
  embedded `audsrv_irx` symbol was still the old module's size. (b) with the
  old IRX + new EE lib, the 3-word volume RPC is read as the old 2-word one -
  data[1] (the L level) becomes the mono volume, so pan=100 gave total silence
  and pan=70 gave quiet-equal-both - measurements that first looked like a
  PCSX2 downmix. (c) the first shipped pan was MIRRORED (user report: sound on
  the left heard on the right): the right vector was borrowed from the particle
  billboards ((fwd.z, -fwd.x)), whose sign was never validated because
  billboard quads are symmetric. In this right-handed Y-up world screen-right
  is fwd x up = (-fwd.z, fwd.x). The original meter test only proved
  pan-sign <-> speaker-side consistency, not formula <-> screen-side - the
  emitter was off-screen and invisible. Re-verified with the loop actually
  closed: a visible red box + emitter at the same spot, screenshot shows the
  box on the LEFT half of the screen and the WASAPI per-channel meter reads
  L=0.298/R=0.086 (and the pre-fix runs measured the full matrix:
  centered -> equal 0.39/0.39, pan +-70 -> ~4-9x asymmetry; instrumented
  TYRA_LOG confirmed dist/vol/pan). Diagnostic logging removed after
  verification; editor + game build clean.

- (54) **Properties window + per-type property cleanup + collapsible Project
  sections** - object properties moved out of the Project panel into a new
  "Properties" dock window (default layout: below Project in the left column;
  projects saved before this change get it docked there automatically - a
  pending-dock pass splits the Project node when the stored layout ini has no
  `[Window][Properties]` section). The properties themselves are now gated by
  what the game actually reads per type (matrix derived from the generated
  runtime: geometry build, collision, use-target and physics loops all skip
  marker types): sound emitters/point lights/spawn points/player no longer
  offer texture, rotation, scale, physics or "usable" (e.g. a texture set on a
  sound emitter was a dead setting - only geometry types 0-3/5/10 ever bind
  textures); color hidden for pure markers (sound/spawn/player) since they
  draw in fixed editor colors; "save state" limited to solids + emitters +
  sound emitters (lights are baked at build time); the Type combo now only
  converts between the four primitive shapes instead of also offering
  spawn-point/model/player conversions that silently produced misconfigured
  objects (it also indexed past its 7-name array for types 7-10). Project
  panel sections (Scenes, Scene objects, HUD, Music, Sounds, Save data,
  Scripts) are CollapsingHeaders now - Scenes + Scene objects default open,
  the rest collapsed. Verified: editor built clean; scratch project with a
  player + injected sound emitter + box (`.tyra` edited directly, selection
  preset) screenshotted per case - sound emitter shows only
  name/type/position/save-state/sound params, box shows the full set, and a
  layout with the Properties section stripped re-docks it under Project on
  load. Sections collapse/expand state confirmed in the same screenshots.

- (55) **Flow graph: Self node + typed variables (Set/Get Int, Bool,
  Position)** - "Self" is a pure data node exposing the graph's owner as an
  object output; object params already defaulted to self when empty, this
  makes the reference explicit and wireable (it needed zero codegen changes -
  resolveTarget's chain walk ends on a node with no object input and no
  explicit name, which is exactly self). New "Variables" node category:
  named game-global values in three separate namespaces (int / bool /
  position), zeroed at boot, kept across scene switches, NOT saved to the
  memory card (Save values remain the persistent store). Setters (Set Int,
  Set Bool, Set Position) run on exec; Set Position accepts a position link
  that overrides its X/Y/Z params (so Get Position on an object -> Set
  Position stores a live object position). Readers are pure data sources in
  the house style: Get Bool -> bool output for logic gates / On Condition,
  Get Position -> position output for Set Object Position / Spawn Player At,
  Int At Least -> bool (mirrors the save Value At Least). A variable exists
  by being named on any node - codegen collects names across every scene's
  graphs into static flowInt/flowBool/flowPos arrays at the top of
  flow_graph.gen.cpp (statics, not ScriptContext - script.hpp is
  user-ownable, so adding context fields would break owned copies). Editor:
  VarName params are free text + a "Pick..." popup of same-type names used
  anywhere in the project (typo guard); Set Bool renders a checkbox; the
  node registry drives pins/serialization so project.cpp needed no changes.
  Verified: editor builds clean; a graph exercising every new node (Self ->
  Get Position -> Set Position "home" on On Start, Set Bool/Set Int, Get
  Bool + Int At Least OR-folded into On Condition -> Hide Object, Every N
  Seconds -> Set Object Position fed by Get Position "home") was injected
  into a scratch project's .tyra; the generated flow_graph.gen.cpp is
  exactly right (statics with name comments, self resolves to the owner
  index, rising-edge OnCondition folds both variable reads) and the full
  Docker PS2 build compiled it clean (bin/propwin.elf linked). GUI
  screenshot confirms the nodes render with pins/params (Flow Graph tab
  forced via the layout ini's Selected TabId - ImHashStr("#TAB", seed =
  ImHashStr(name)), CRC32c in this imgui). Not boot-tested in PCSX2; the
  generated code paths are the same ctx.objects/flag mechanics as existing
  nodes.

- (56) **Sound emitters: "Play on player" (2D stereo) property** - for
  dialogs/narration: a sound that always plays "on the player" at full
  volume, centered, regardless of the emitter's position. New
  `SceneObject::soundOnPlayer` (saved as `"onPlayer"` in the sound block,
  defaults false on old projects), `SceneObjectData::sndOnPlayer` in the
  always-regenerated scene_data.hpp, and a branch in the terrain_game
  template's updateSoundEmitters that skips the whole distance/range/pan
  computation (vol=100, pan=0) when set - visibility mute and the
  interval/loop retrigger logic still apply. Properties UI: checkbox under
  Autoplay; Range is hidden while it's on (no falloff to range) and the help
  text switches to the dialog wording. Verified: editor builds clean;
  `onPlayer: true` set on a scratch project's emitter produces a `1` in the
  right SceneObjectData slot (neighbors 0), the generated
  updateSoundEmitters carries the sndOnPlayer branch, and the full Docker
  PS2 build compiled + linked. GUI screenshot shows the ticked checkbox,
  hidden Range and swapped help text (load path of the flag proven by the
  ticked box). Not ear-tested; the playback path below the vol/pan values
  is unchanged from (53).
- (57) **Window docking lost when opening a project mid-session** - user
  report: docking didn't seem to persist and panels scattered after loading a
  project. Root cause: `attachProject()` called
  `ImGui::LoadIniSettingsFromMemory` mid-frame (File > Open / Ctrl+O fire from
  inside drawUI), which imgui explicitly does not support between
  NewFrame/EndFrame (commented-out assert in imgui.cpp) - the dock settings
  handler clears and rebuilds all dock nodes while the current frame's windows
  still reference the old ones, so the layout applied half-broken. ~5 s later
  the `io.WantSaveIniSettings` autosave then wrote that mangled layout back
  into the freshly opened project's .tyra, destroying its good saved docking
  (hence "doesn't save"). Second overwrite path: a WantSave pending from
  before the open captured the *previous* project's on-screen layout into the
  new project's file in the same frame. Fix: `attachProject()` only sets
  `layoutLoadPending_`; the run() loop applies the layout at the frame
  boundary (before NewFrame), where imgui's ApplyAll handlers re-dock existing
  windows properly; `saveProject()` keeps the stored layout string while a
  load is pending instead of capturing the stale screen. The startup path
  (project dir on the command line) goes through the same deferred path.
  Verified: clean build; scratch project run 1 saves a layout with
  `[Docking][Data]` into the .tyra on exit, run 2 restores it through the
  deferred path (screenshot: Project left, Viewport/Flow Graph center,
  Output/Debug bottom) and the layout string round-trips byte-identical. The
  mid-session File > Open path needs a hands-on mouse test (no synthetic
  input from automation), but it now runs the exact same deferred code path
  as the verified startup load.

- (58) **Target system (PAL/NTSC) + debug/release build profiles** - Project >
  Preferences grew a "Build" section: **Target system** (Auto / NTSC 60 Hz /
  PAL 50 Hz) and **Profile** (Release / Debug) with "Show FPS" and "Show memory
  usage" checkboxes that are grayed out unless the profile is Debug. All four
  live in `ProjectSettings` (saved in the `.tyra` `settings` block, defaults
  `auto`/`release`/off - old projects load unchanged). Engine: new
  `Tyra::VideoMode` enum threaded `EngineOptions.videoMode` -> `Renderer::init`
  -> `RendererSettings`; `RendererCoreGS::allocateBuffers` keeps
  `graph_initialize` (region auto-detect) for Auto and otherwise runs the same
  ps2sdk call sequence with a forced `GRAPH_MODE_PAL/NTSC` (verified identical
  by disassembling libgraph's `graph_initialize`; the 512x448 framebuffer is
  kept for both signals). Generated `main.cpp` passes the mode via
  `EngineOptions` - and must also set `options.writeLogsToFile = true`, because
  the options ctor re-applies that flag and silently reset the static set the
  line before (log.txt vanished until this was spotted). Debug HUD: constexpr
  `DEBUG_SHOW_FPS/MEM` in `terrain_config.hpp` (forced false in release, so the
  overlay folds away), a `drawDebugHud()` helper in the game-cpp prolog drawing
  `FPS n` / `MEM n.n MB` (from `Info::getFps()` / `getAvailableRAM()`, the
  latter sampled every ~2s - it malloc-probes the heap) with an 8x8 CP437-style
  glyph strip the editor bakes into `res/hud/debugfont.png` (glyphs on a 16px
  stride so bilinear sampling can't bleed neighbors; written by
  `refreshGenerated` only when a debug overlay is on). Verified e2e in PCSX2
  (software renderer, Europe/PAL BIOS): forced PAL -> status bar PAL, 50 VPS,
  overlay reads "FPS 50 / MEM 27.9 MB"; forced NTSC on the same PAL BIOS ->
  NTSC, 60 VPS, "FPS 59" (proves the force, not region detect); release
  profile -> overlay gone, still forced NTSC. **Testing pitfall that cost an
  hour**: the first e2e ran the project from the session scratchpad whose path
  is ~185 chars - PS2 `loadelf` truncates the `host:` path (emulog showed a
  mangled `secname`) and the ELF jumped to null before the banner; looked
  exactly like an engine-ABI bug. Scratch projects for PCSX2 must live in a
  short path (e.g. `%TEMP%\tyra-editor-test\`). The Preferences graying uses
  the standard staged-copy + `BeginDisabled` pattern; the modal itself wasn't
  exercised by hand (no input injection) - one human click-through pending.

- (59) **Wall-clock normalization: same game speed on PAL and NTSC** - all
  generated game logic was per-frame with 50 FPS baked in (`GRAVITY/(50*50)`,
  `JUMP_SPEED/50`, `EverySeconds * 50`, particle `dt = 1/50`...), so an NTSC
  build ran 20% fast. Engine: GS init now resolves `VideoMode::Auto` to the
  console's real region (`graph_get_region`) and writes it back into
  `RendererSettings`, which gained `getRefreshRate()` (50/60); the Auto and
  forced paths collapsed into one explicit `graph_set_mode` sequence. Codegen:
  `scene_data.hpp` declares `g_frameRate` / `g_frameDt` (1/rate) /
  `g_frameScale` (50/rate - the "tuned at 50 Hz" conversion factor) plus an
  inline `everyFrames(seconds)` helper, all defined in the game cpp and set in
  `TerrainGame::init()`; every per-frame site now multiplies through them:
  FPP + Player-entity look/walk/fly/gravity/jump, orbit step, object physics,
  particles, sound-emitter retrigger, flow-graph `EverySeconds` (emitted as
  `frame % everyFrames(N)` instead of a compile-time constant), loading-screen
  hold, save feedback and the debug-HUD MEM refresh. User scripts see the
  globals through `scene_data.hpp` via `script.hpp`. Verified with a wall-clock
  measurement, not by eye: an `EverySeconds(1s) -> Log("TICK")` graph, counting
  TICK lines in the host-side log.txt against real time - forced NTSC 60 Hz:
  0.998 ticks/s over 60 s; forced PAL 50 Hz: 0.998 ticks/s over 45 s (the old
  codegen would read 1.2/s on NTSC). Both modes verified booting at their
  vsync rate (status bar 50/60 VPS). Legacy V1 templates untouched (their
  byte-identical match must keep working).


## Backlog (rough order)

- Hands-on pass over the Flow Graph editor UX (needs a human with a mouse)
- Object physics vs objects (stacking), player physics polish (pad feel)
- Model picking uses the unit-box approximation (big models pick imprecisely -
  the parser now exposes the real AABB, the viewport pick could use it)
- HUD images draggable directly in the viewport
- Positional audio (volume falloff by distance to an object)
- Compressed music streaming (SPU2-native ADPCM/VAG, ~3.5:1 vs 16-bit PCM) -
  needs a custom double-buffered SPU RAM streamer in the engine; audsrv only
  streams PCM and plays ADPCM one-shots
- Flow graph: more nodes (timers with reset, variables)
- Animations: measure VU0 skinning (entry 34) on real hardware once ps2link
  is installed - PCSX2 prices COP2 ops like FPU ops so it can only show
  parity; the SIMD win (one FMAC = 4 lanes) is a hardware-only effect.
- Animations stage 3 (optional) - VU1 skinning microprogram: matrix palette
  in VU memory, weights in the vertex stream, new VCL program (respect the
  vcl_sml.i history first). Measure EE headroom before starting - skinning
  now runs on VU0 in macro mode (entry 34) at ~37% EE load for 3 characters
  in PCSX2, so the EE is not the bottleneck yet. Palette per batch limited
  by VU memory (~24-32 bones).
- Engine perf, next targets: packager allocates its package array per frame
  (poolable); the real endgame is the engine author's own TODO in
  stapip_clipper.hpp - move clipping to VU1 entirely ("too much time")
