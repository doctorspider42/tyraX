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

Developer design docs (internals, not user guides):

- [VU1 clipping plan](vu1-clipping-plan.md) - measured EE-clipper cost on
  real hardware (2026-07-11) and the design + milestones for moving StaPip
  clipping into a VU1 microprogram.
