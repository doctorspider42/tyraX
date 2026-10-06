# Public dynamic-light reception: independent source review

Reviewed 2026-10-06. This is a source review of an ongoing production diff, not build, image, performance or hardware qualification. No production sources or devices were changed by this reviewer. The editor development skill was consulted for model/serialization/codegen/viewport ownership.

## Verdict at this snapshot

No concrete blocker found in the editor/model/scene inheritance changes or the minimal engine seam. Generated runtime ownership, shared animation illumination and wheel class/lifetime changes are still being implemented and are not covered by this interim verdict. Completion must not be inferred from this report.

## Model, transport and inheritance

`DynamicLightReceivers` has a fixed small underlying type and defaults to All. Settings equality and SceneOverrides equality explicitly include it. The project writer uses exact all/players tokens, and the shared Settings reader starts by assigning ProjectSettings{}, so missing fields, including partial Settings transport, resolve All. Wrong-type/unknown tokens resolve All via exact string comparison. Scene reader resets both local value and new override flag before reading, while scene serialization retains the local token even when inheritance is disabled. The independently controlled override therefore does not inherit old directional-light override flags accidentally.

resolvedSettings starts from project settings and replaces this field only for the explicit receiver override. Scene preferences retain the stored local enum across dialog reopening, but display the project enum in the disabled widget. Re-enabling deliberately restores the local value; it does not erase it while disabled. Ordinary scene equality/history transport includes the fields. Project preferences retain the existing non-scene-undo contract. Version/format advance to1.173.0/v96 is additive. The existing sizeof guard still needs actual compiler evidence; this source audit cannot establish ABI size from a declaration.

## Preview reception and default behavior

App supplies the first two authored Player object IDs, independently of selection. The viewport resolves each visible object by stable nonempty ID; vehicles remain parked world recipients because authoring preview has no runtime driving identity. Procedural/scatter/prefab draws are world recipients. Terrain/road pool stand-ins use their existing separate flag, retaining projected ground effects. Reflected objects and avatar draws resolve their own IDs. Mirror and portal overlays restore world/effect reception instead of inheriting the last reflected avatar.

For All, every ordinary draw uploads receive=1, so the new shader predicates preserve the previous live-light branches. Restriction gates only styleW<1.9 scene live terms and camera light, leaving baked point terms available. Asset previews use lightCount=0 and unlit draws, with animation/thumb flashlight disabled; no new illumination regression from the otherwise persistent receiver uniform was demonstrated. No GL execution or screenshot was performed by this audit. General existing shared-uniform behavior is not newly qualified.

## Engine default, keys and memory

PipelineInfoBag initializes dynamicLightReceive=true. Core render defaults to the typed FromInfo option and combines descriptor opt-in with a per-call Suppress flag. Existing dynLightPick=false retains its fallback-light meaning; existing spotLit=false still wins. Suppression bypasses dynamic picking and explicitly selects the persistent disabled kNoSpotLight object, avoiding null-as-flashlight fallback. No shared info descriptor is mutated and no global receiver state survives the call.

Core sets the selected light and emits object data before opening retained/baked caches. sendObjectData rebuilds local spot semantics, clipper spot and spotActive/options for the current call. The retained cache holds geometry command blocks and uses program/geometry/primitive keys; current object uniforms precede replay and therefore do not require receiver state in those geometry keys. The per-call enum and boolean are consumed synchronously; persistent disabled light is not a stack pointer handed to DMA. This does not qualify newly generated callers or future consolidation that might cache mixed receiver classes.

## Remaining release evidence

Review final generated runtime routes, actual receiver classification, directional/scalar illumination, primary/secondary view propagation, driven versus parked wheel class storage, and scene/spawn reuse before a final source verdict. Root owns compiler/ABI, executable qualification and runtime witnesses. Neither source review nor private archived player-only results establish a performance gain for the public option.
