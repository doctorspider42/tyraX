# tyra-editor documentation

User-facing guides for editor features. Written for people building games
with the editor; internals live in code comments, `PROGRESS.md` (feature log
+ verification notes) and the `.claude/skills/` developer guides.

- [Animated models (.glb)](animated-models.md) - authoring animations in
  Blender, importing them, clip playback, flow-graph nodes and the script
  API, PS2 memory budget, troubleshooting.
- [Object scripts (Unity-style components)](object-scripts.md) - writing
  C++ scripts and attaching them to objects, the ObjectScript lifecycle
  (`self`, onStart/onUpdate/onUsed), ScriptContext reference, Empty
  objects, global scripts, performance, troubleshooting.
- [Streaming layers](streaming-layers.md) - grouping objects into layers
  the game loads/unloads from memory at runtime (GTA3-style interior
  streaming): the Layers panel, the Load / Unload / Is Layer Loaded flow
  nodes, the corridor trigger pattern, what gets freed, troubleshooting.
- [Custom flow-graph nodes](custom-flow-nodes.md) - defining your own Flow
  Graph action nodes in `.flownode` text files (no editor rebuild): inline C++
  snippets with `{placeholders}`, or `call = fn` nodes backed by a real
  function in `flow_nodes.hpp` with input/output pins of any kind (object
  outputs work as runtime refs into built-in nodes), and how to copy a node to
  another project.
- [Loading screens](loading-screens.md) - defining named loading screens
  (background, images, baked texts, continuous/quantized progress bars),
  assigning them per scene or as the project default, how the progress bar
  tracks real load work, and the built-in fallback.
- [Custom screen effects](custom-screen-effects.md) - defining your own
  full-screen post effects (like the built-in bloom / film grain) in
  `.screenfx` text files (no editor rebuild): a small manifest plus a raw
  low-level GS-blit body, positioned in the UI Editor screen stack with numeric
  parameters, and how to copy an effect to another project.
- [Camera takes (phone-recorded 6DoF moves)](camera-takes.md) - importing a
  real ARKit camera move (CamTrackAR `.hfcs` or the app-agnostic CSV) into a
  Cutscene Director camera track: the canonical take space, the mapping and
  decimation controls in the import modal, and the acquisition/bake split that
  keeps the door open for live phone streaming.

Developer design docs (internals, not user guides):

- [Profiling the generated game](profiling.md) - the built-in debug frame
  profiler (per-phase EE time), and the manual COP0/HUD deep-dive technique
  behind it (deterministic camera orbit, in-run A/B, engine-side counters)
  with the gotchas from the usable-highlight investigation.
- [VU1 clipping plan](vu1-clipping-plan.md) - measured EE-clipper cost on
  real hardware (2026-07-11) and the design + milestones for moving StaPip
  clipping into a VU1 microprogram.
