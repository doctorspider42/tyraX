# TyraX documentation

User-facing guides for editor features. Written for people building games
with the editor; internals live in code comments, the git log (commit messages
carry what changed and how it was verified) and the `.claude/skills/` developer
guides. What is queued is in [Backlog](backlog.md).

- [Animated models (.glb)](animated-models.md) - authoring animations in
  Blender, importing them, clip playback, flow-graph nodes and the script
  API, PS2 memory budget, troubleshooting.
- [Static models: the .tmdl pipeline and mesh LOD](model-pipeline.md) - why
  the game reads a binary model instead of your `.obj`, what that means for
  your files and the disc, distance mesh LOD for static geometry, and
  authoring your own LOD meshes instead of letting the build decimate.
- [World scale: units, meters and imports](world-scale.md) - what a unit is
  worth in this project, why a model or a camera take can land several times
  too small, the per-asset real-world size recorded at import, the viewport
  measuring tape and the object Size readout, and how to work out the scale
  your world is actually at.
- [Object scripts (Unity-style components)](object-scripts.md) - writing
  C++ scripts and attaching them to objects, the ObjectScript lifecycle
  (`self`, onStart/onUpdate/onUsed), ScriptContext reference, Empty
  objects, global scripts, performance, troubleshooting.
- [Streaming layers](streaming-layers.md) - grouping objects into layers
  the game loads/unloads from memory at runtime (GTA3-style interior
  streaming): the Layers panel, the Load / Unload / Is Layer Loaded flow
  nodes, the corridor trigger pattern, what gets freed, troubleshooting.
- [Areas (invisible volumes)](areas.md) - the box-shaped object that has no
  geometry in the game and replaces hand-typed distances: streaming-layer
  zones that bound height too, mirror / portal / camera-feed target lists
  picked up by volume instead of one name at a time (optionally re-tested
  every frame, so things that move join and leave), and the In Area trigger
  (rising edge + a live "inside" bool). Also: which radii deliberately stay
  radii.
- [Orthographic and axis views](orthographic-views.md) - the viewport's
  parallel projection and the six locked Top/Bottom/Front/Back/Right/Left
  views: the clickable axis gizmo in the corner, the other three ways to
  switch (button, View menu, numpad), what orbiting an axis view does, and
  why a parallel view draws what is behind the camera.
- [PS2 output in the viewport](ps2-viewport.md) - looking at the scene the way
  the console draws it: rasterized at the GS framebuffer size of the display
  mode, point scaled into the TV's 4:3 / 16:9 picture, field rendering's halved
  vertical resolution, and why GS pixels are 16.7% too wide. Pairs with the
  [safe areas](safe-areas.md), which draw the same rectangle as a guide.
- [Placing objects: surface snapping and deferred paste](object-placement.md) -
  inserted and pasted objects resting on the terrain or on the object below
  instead of sinking into it, the `End` drop-to-floor command, and the paste
  that follows the cursor until you click it down.
- [Endless scroller](endless-scroller.md) - the conveyor-belt object that
  tiles authored segments of scene objects along its axis forever (the
  train-window level generator): segments, belt settings, the Start / Stop /
  Set Scroller Speed flow nodes, seam rules, how the clone bake works.
- [Custom flow-graph nodes](custom-flow-nodes.md) - defining your own Flow
  Graph action nodes in `.flownode` text files (no editor rebuild): inline C++
  snippets with `{placeholders}`, or `call = fn` nodes backed by a real
  function in `flow_nodes.hpp` with input/output pins of any kind (object
  outputs work as runtime refs into built-in nodes), and how to copy a node to
  another project.
- [Loading screens](loading-screens.md) - defining named loading screens
  (background, images, baked texts, continuous/quantized progress bars),
  assigning them per scene or as the project default, how the progress bar
  tracks real load work, the built-in fallback, and which scene the game boots
  into (the start scene).
- [Credits rolls](credits.md) - end-credits screens: headings, role/name rows,
  wrapped lines, images and page breaks, scrolled or shown as cards over music,
  with a skip button and a finish action (resume / scene / menu / flow event);
  importing a roll from a text file, and the page-texture VRAM budget that
  decides how long a roll can be.
- [Custom screen effects](custom-screen-effects.md) - defining your own
  full-screen post effects (like the built-in bloom / film grain) in
  `.screenfx` text files (no editor rebuild): a small manifest plus a raw
  low-level GS-blit body, positioned in the UI Editor screen stack with numeric
  parameters, and how to copy an effect to another project.
- [The neural upscaler (BLSS)](neural-upscaler.md) - rendering the 3D scene at
  half resolution and reconstructing it with a small neural network that picks,
  per 32x32 screen tile, how much point / temporal / sharpened reconstruction
  that tile wants: the sub-pixel `XYOFFSET` jitter, the features the EE can
  produce without ever reading the framebuffer back, the free full-resolution
  history hiding in the other display buffer, and the host-side trainer
  (`--blss-train` / `--blss-eval`).
- [Emissive materials (glow)](emissive-materials.md) - making a material light
  itself so it keeps its own color in a pitch-black scene: the `Ke` brightness
  floor baked into the vertex colors (free at runtime), the white-hot core (why
  "more glow" past full strength can only mean whiter), the bloom
  **bright-pass threshold** and **spread** that turn the frame-wide soft glow
  into a halo, and **baked emissive light** - the emitter lighting the terrain,
  walls and props around it, the ambient-occlusion machinery in reverse.
- [Drone Generator (ambient music)](drone-generator.md) - the built-in
  ambient/drone generator: the oscillator/air/bell signal chain, chord
  progressions that glide, the LFO + arc modulation matrix, the FDN reverb and
  its shimmer, why a seamless loop ADDS the tail over the head instead of
  crossfading, the **timeline** (keyframes written by turning a knob, one lane
  per parameter, a playhead that seeks the audition), live audition and the
  UI/audio-thread hand-off, and the re-editable `.drone` patch sidecar.
- [Asset Browser](asset-browser.md) - the file manager over the project's
  `res/` tree: folders, thumbnails, type filters and search, the reference
  census that says who uses an asset (and which ones nothing does), moving
  files with their references following, drag & drop into the scene, safe
  renames, and why a texture never moves away from its material.
- [Materials: model preview, duplication and texture painting](material-painting.md) -
  the Material Editor's live preview on your own .obj models, duplicating a
  material together with its textures, and painting color or tiled-pattern
  strokes straight onto the mesh through its UVs (the flat PNG is the bake).
- [The terrain, and building without one](terrain.md) - the per-scene ground
  plane is optional: the New Project / New scene "Create terrain" checkbox and
  the Terrain Editor's toggle, what "removed" means in the editor and in the
  game (no mesh, no ground textures, no lightmap - and no floor: things stand
  on the geometry you place and fall through the void), what the scene size
  still means, and the limits (navigation AI needs a terrain).
- [Terrain painting](terrain-painting.md) - blending several terrain layers
  (grass/rock/path, each an `.mtl`) by painting their weights with a brush on
  the terrain in the Terrain Editor; two-pass GS splatting (vertex-alpha, full
  tiled texture detail, cost only on painted chunks), stochastic tiling
  (build-time texture bombing that breaks the tiled-grid repetition), the
  storage/undo model, and why the baked-composite approach was abandoned.
- [Procedural generation (scatter graphs)](procedural-generation.md) - the
  node graph that fills a region with instances (forests, rock fields, fence
  posts, a ring of columns): the Procedural volume, the node library
  (surface/grid/volume/curve sources and a single placed point, noise and
  terrain masks, slope/mask/distance/avoid filters, Array / Radial Array for
  exact repetition, weighted asset pools, transform variation), the Object
  Settings node that applies properties to everything the volume bakes,
  per-instance hand edits that survive re-evaluation, the live triangle budget,
  and the bake that merges instances into ordinary static chunk meshes so the
  PS2 never sees a graph.
- [Runtime procedural generation](procedural-runtime.md) - the same graph
  compiled into the game and evaluated on the EE instead: what the console can
  and cannot run (and why the window says so up front), what a runtime volume
  costs in load time and RAM, where the per-run randomness actually comes from,
  the Generate Volume node, and Blocks Fill - the block-world source that emits
  only visible blocks, tells the merge which faces to draw, and publishes its
  solid field as the world's collision.
- [Prefabs](prefabs.md) - reusable groups of scene objects (their flow graphs
  included) stamped by hand, scattered by a graph or spawned at runtime: the
  local-frame model, why instances are not linked back, and the merge/spawn
  split that decides what an instance costs on this machine.
- [Save Editor](save-editor.md) - memory card saves in one window: the browser
  title and the real 3D icon (including animated ones from a `.glb` clip), the
  slot size breakdown and why "card space used" is bigger than the byte sum
  (1 KB clusters), the save values/texts, the RAM checkpoint nodes, and the
  "checking memory card" screen.
- [The VS Code extension](vscode-extension.md) - syntax highlighting, snippets
  and validation for the `.flownode` and `.screenfx` text files: what it does,
  how the editor installs it automatically (and how to package a `.vsix` by
  hand), and how to keep it in sync when you add header keys or placeholders.
- [Live Link (edit the running game)](live-link.md) - streaming object
  moves/rotations/scales/recolors AND object adds/deletes into the game
  running in PCSX2 or on a real PS2, with no rebuild: the debug-profile
  requirement, the clickable LIVE toolbar chip (per-project on/off), what
  updates live vs what needs a build, and how the host-filesystem transport
  and the spawn-pool cloning work.
- [The log panels (errors, warnings, verbose)](log-panels.md) - the *Output* and
  *Debug* windows classify every line into error / warning / info / verbose,
  count the entries per level, colour them and let you hide a level with one
  click: what lands in each bucket, why a compiler error keeps its four
  explaining lines when verbose is hidden, and the selectable-text escape hatch.
- [Running and debugging on a real PS2](ps2link-setup.md) - the one-time
  console setup for the F6 network deploy: building the TyraX ps2link (the only
  one supported - a pinned upstream plus our patch, built in Docker), flashing
  it with an `IPCONFIG.DAT`, the editor's IP preference, what the deploy
  actually does step by step, the ports a firewall has to let through, what
  differs from PCSX2 when debugging, and a table of every failure message. Plus
  the loop for changing the patch, because we will.
- [The devkit, and its zero-cost promise](devkit.md) - the live channels, the
  crash reporting, the VU1 packet inspector, and the release audit; the live
  channels (Live Link / Live Debugger / Live Logic / Remote Pad) as one
  development kit, what a debug
  build actually costs in code and RAM, and the release audit that PROVES a
  shipped ELF carries none of it (`--audit-release`, run automatically after
  every release build).
- [The time machine (put the running game back)](time-machine.md) - the game
  captures everything it mutates a few times a second, the editor keeps a
  history of those captures in memory, and pushing one back puts the console
  where it was: what a capture holds (and what it does not yet), why the history
  never touches the disk, and the layout hash that stops a capture from landing
  in a differently built world. With Live Logic below: rewind, fix the graph,
  watch the fix play out on the situation that broke.
- [Live Logic (edit a flow graph with no rebuild)](live-logic.md) - the
  flow-graph interpreter debug builds carry: what the editor can hot-patch into
  a running game and what still needs a build, the pre-resolved instruction
  format, how a patched graph shares state (and debugger keys) with compiled
  ones, and the cost.
- [Live Debugger (step through the running game's logic)](live-debugger.md) -
  breakpoints on flow-graph nodes, pause/step/step-node of a game running on
  the console, the live node highlighting and hit counters in the Flow Graph,
  the watch table (flow variables + save values), the rewindable execution
  timeline, firing a trigger from the editor, and the file channel + symbol
  table it all rides on.
- [UI scripting (drive the editor without a human)](ui-scripting.md) - the same
  idea one level up: `--ui-script` runs the real editor and holds its mouse and
  keyboard itself, naming WIDGETS instead of pixels (`click "Remote Pad/Cross"`),
  with no window focus, no DPI arithmetic and assertions on widget state. How the
  item registry falls out of ImGui's own test-engine hooks (no imgui_test_engine
  dependency), why `dump` is where every script starts, and what it cannot reach
  (the viewport, imnodes, the gizmo).
- [Remote Pad (hold the running game's controller)](remote-pad.md) - the input
  direction of the same channel: a clickable DualShock in the editor (plus a
  keyboard mode) and a scriptable `--pad` CLI that drive the game **with no
  window focus anywhere** - what the little script language does, why the state
  expires when a driver stops refreshing it, the second `injectVirtual` overlay
  slot, and how to write an unattended input test around it.
- [Live collaboration sessions](collaboration.md) - real-time multi-user
  editing: hosting a project, joining over the LAN with a code, what syncs
  live and how conflicts resolve (host-ordered last-write-wins), presence
  highlights, the joined-project cache and mid-session file refresh, the
  host-owns-saving rule, and the trust model / v1 limitations.
- [NavMesh + NPC AI](navigation-ai.md) - the build-time navigation-grid bake
  (walkability rules, the AI navigation preferences, the viewport overlay),
  the A*-on-EE runtime, and the AI flow nodes (Patrol Waypoints / Chase
  Player / Flee From Player / Stop AI / On Player Seen) with the classic
  guard wiring, plus the deliberate era-appropriate limitations.
- [Configurable buttons & keys](input-bindings.md) - the Input Map: named
  actions instead of hardcoded pad buttons, per-project binding presets, the
  in-game *Rebind key* menu row (capture mode, overrides persisted in save
  values), the configurable sprint, and the On Action / On Key / Set Input
  Preset flow nodes. Pairs with [keyboard & mouse](keyboard-mouse.md).
- [Text icons (button glyphs in text)](text-icons.md) - `{{cross}}` /
  `{{action:jump}}` placeholders that draw a pad-button glyph inside any text:
  the seeded DualShock set the editor draws itself, overriding one with your own
  PNG, and the two paths (composited into baked sprites, blitted from a sheet in
  runtime text).
- [The editor's look: themes and the interface font](editor-theme.md) - the four
  interface themes (three of them PS2 nods) in *View > Theme*, why the choice is
  machine-global rather than project data, the system UI font the editor picks
  instead of ImGui's built-in bitmap one, and - for developers - the
  "ask for a meaning, not a colour" rule that keeps a status chip's green from
  staying green in a violet editor.
- [TV safe areas](safe-areas.md) - the viewport guides (behind the gear) for
  framing something a television will not crop: the console's picture rectangle,
  action- and title-safe insets, and the one case where PAL really does show more
  than NTSC.
- [Project format versioning & migrations](format-versioning.md) - the
  editor/format version split, what happens when you open an older or newer
  project (silent open vs the migrate-with-backup prompt vs refusal), the
  `--migrate` CLI, and the bump/migration rules for contributors.
- [Camera takes (phone-recorded 6DoF moves)](camera-takes.md) - importing a
  real ARKit camera move (CamTrackAR `.hfcs` or the app-agnostic CSV) into a
  Cutscene Director camera track: the canonical take space, the mapping and
  decimation controls in the import modal, and the acquisition/bake split the
  live link below plugs into.
- [Phone camera (live viewfinder)](phone-camera.md) - the companion iOS app as
  a viewfinder: the editor hosts a LAN link, the phone shows a live JPEG stream
  of the viewport and its ARKit pose drives that camera, and the Cutscene
  Director records the move into keyframes at a chosen density. Covers pairing
  and firewall, the mapping controls, the recording options and table-size
  budget, the WebSocket protocol, and the built-in browser test client.
- [The AI Assistant window](ai-chat.md) - the chat built into the editor: it
  answers questions about the editor from these very pages (they are baked into
  the executable) and carries out operations in the project - adding and
  changing objects, writing flow graphs, switching scenes, opening windows.
  Covers what it knows, its tool list, the read-only switch, and how edits
  relate to undo and saving.
- [AI flow-graph generation](ai-flow-graph.md) - describing game logic in
  plain language and letting an AI backend (Claude CLI, Copilot CLI or the
  OpenAI API) build the graph: backend/model/thinking preferences, what the
  model is told, validation, and the cancelable in-editor flow.
- [AI-agent CLI tools](ai-tools.md) - the headless commands
  (`--dump`, `--list-nodes`, `--dump-graph`, `--apply-graph`,
  `--refresh-gen`, `--ai-graph`, `--chat-prompt`) that let an AI assistant or
  script inspect and modify a project without the GUI.
- [AI support in projects](ai-support.md) - the "Add AI support" option
  (New Project / Project Preferences): assistant guidance files (Claude Code
  skills + `CLAUDE.md`, Copilot instructions) installed into a project, and
  the marker-based ownership rule for refreshing them.

Developer design docs (internals, not user guides):

- [Profiling the generated game](profiling.md) - the built-in debug frame
  profiler (per-phase EE time), and the manual COP0/HUD deep-dive technique
  behind it (deterministic camera orbit, in-run A/B, engine-side counters)
  with the gotchas from the usable-highlight investigation.
- [The VU framework](vu-framework.md) - describing a VU microprogram in
  C++ and generating both sides of it, plus the host simulator that runs a
  microprogram (handwritten or generated) on either vector unit with no PS2 in
  the loop: `--vu-check`, `--vu-emit`, `--vu-list`, the VU1/VU0 machine models
  and the micro-memory budget.
- [Authoring VU programs](vu-authoring.md) - the layer on top: composing a VU1
  program or a VU0 kernel out of STAGES with no assembly, the per-mesh
  parameter split and why one program serves a whole material class, the slots
  a stage can run in, what the sine costs, and how to add a stage.
- [VU1 clipping plan](vu1-clipping-plan.md) - measured EE-clipper cost on
  real hardware (2026-07-11) and the design + milestones for moving StaPip
  clipping into a VU1 microprogram.
- [GS VRAM residency](gs-vram.md) - where the 4 MB goes, what a texture
  really costs, the free-list texture heap and its eviction policy, the
  `VRAMSTAT` counters, and the measured before/after numbers.
- [BLSS reconstruction math](blss-reconstruction.md) - the twin contract
  between the neural upscaler's host trainer and its PS2 runtime: the exact
  sampling, blend equations, 8-bit truncation and grid vertex order both sides
  must agree on, and why a drift between them trains the network for a machine
  that does not exist.
