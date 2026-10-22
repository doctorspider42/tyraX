# Frame pipeline Flow Graph nodes

Set Frame Pipeline and Get Frame Pipeline (Requested) expose the existing renderer request through scene-object Flow Graphs.

Both nodes live in the **Scene** category. **Set Frame Pipeline** takes an execution input and an **On** choice: Off or On. Its **after** execution output continues the graph after queuing the request; it does not wait for application. The generated game applies the request between frames, before the next `beginFrame`, in both FPP and ORBIT games. Several requests before that boundary overwrite the same pending value; the last one wins. A pipeline request does not open the display-mode confirmation prompt or close the game menu.

An unchanged request is consumed without calling the engine setter again. This matters for On Update graphs: the engine setter completes a pending frame even for an identical value, which would present it early and break the intended overlap. The comparison uses the requested setting, so repeated On remains a no-op while compatibility fallback is active too.

**Get Frame Pipeline (Requested)** is a pure boolean output. Wire it into Branch, On Condition or another boolean consumer. It reads `RendererCore::getFramePipeline()`, which reports the engine's current requested setting. It does not read the pending graph request. Reading immediately after Set therefore returns the previous applied request until the next frame boundary applies the setter.

Requested On does not guarantee active CPU/GPU overlap. Field rendering, BLSS, unlimited triple buffering or unavailable frame banks can make the renderer use compatibility fallback while the getter remains true. The getter does not report actual overlap, a measured performance gain or a pending GPU job. The existing engine owns the fallback and synchronization behavior; these nodes do not change it.

Use **On Start → Set Frame Pipeline** to choose a runtime request, or **On Button → Set Frame Pipeline** for a fixed On/Off control. To toggle, connect Get Frame Pipeline (Requested) to Branch and use separate Off and On setter nodes on its true and false outputs. Two toggles before application still read the same applied state; this is deliberately not a pending-value toggle.

These nodes require a game rebuild. Live Logic's existing unsupported-node diagnostic rejects graphs containing either node; they have no live interpreter opcode. Node keys and their parameters use the existing generic graph serialization, so there is no project-format migration. The project's authored Frame Pipeline setting still determines the initial engine request before graph changes.

## Qualification (1.172.0)

The Windows Release editor retained all nodes and links in16 authored FPP/ORBIT graph roundtrips. Native builds and stationary PCSX2 scenes passed73/134 alternating requests, with inspected normal box/ground images. Fully staged PS2 builds passed177/99 observed alternating requests and the initial pending-state getter check without texture/audio loading errors. Physical TV raster was not captured.

Actual extracted helper controls passed1181 checks at each of O0/O2, including repeated requests during inactive fallback and retained display confirmation behavior. Both missing-guard and wrong-active-state mutants were rejected. The actual Live Logic capability function rejected both nodes by their registered titles. The [machine record](frame-pipeline-flow-nodes-2026-10-04.json) pins the sources, native runtime evidence and private controls. This authoring convenience makes no FPS gain claim.
