# Main-only eleven-box v2 failure diagnosis

Read-only audit of frozen v2 source plus the parent-supplied current emulator observation. No new candidate, source mutation, build, emulator/console operation, elapsed estimate or optimization gain claim. Source pins bind the reviewed code; the runtime observations below are explicitly parent-reported, not independently parsed by this audit.

The reported cold observations are zero admitted/accepted candidate groups, eleven original main submissions, eleven individually owned carriers,396 semantic vertex comparisons with zero mismatches, and all three groups demoted. WHY has firstDiff147 for each group, keyReady1/keyAdmitted0, reasons1050880 for groups0/1 and1051136 for group2.

## Exact key147 decoding

The exact key built by `nightMainReplace` starts with16 VP words and24 semantic frustum words. Index40 is dynLightCount. Each light then contributes14 words from `nightMainLightKey`, in this order: enabled, point, position.xyz, direction.xyz, color.rgb, range, cutoff, softness. The fallback spotlight occupies41..54; scene dynamic light slot0 starts55. Scene slot6 starts139, and offset8 is color.r. Therefore **key147 is dynamic scene light slot6.color.r**, not camera position, the clock field or geometry storage.

The authored scene has eight dynamic lamps. `collectScenePointLights` collects their scene order, with an eight-light capacity (`game/inc/game_runtime.gen.hpp:604-615`). During the full-night visible-light workload the slots correspond to the authored lamps1..8. Slot6 is **District night lamp7**, authored at(-9,5.7,-14), warm color(.75,.52,.23), brightness1.15, radius16, downward48-degree cone, flicker.025. The source table entry is the line commented `District night lamp 7` in `game/src/gen/scene_objects.gen.cpp`; authoring sets flicker only for i==6 (`examples/vehicle-playground/authoring/build-district.py:255-262`). The mapping assumes this reported full-night light inventory; the index decoding itself does not require the inventory assumption.

This field is intentionally live. `updateDynLights` advances g_dynLightTime unless gameplay is paused, then computes a clamped wobble from sin(time*11.7+p) and sin(time*23.3+p*2.1). It multiplies intensity by `1-lightFlicker*(1-w)`, then sets each channel to authoredColor*128*brightness*mult, capped at255 (`game_runtime.gen.hpp:635-670`). Lamp7 red is approximately110.4*mult, below the clamp throughout this small flicker. Daynight::setPaused pauses the sky cycle; it does not set g_gameplayPaused or freeze this lamp clock. Delaying startup to600 therefore cannot make this intentional flicker settle.

The v2 exact key includes **every active scene light**, not just a group's selected light. The red change invalidates all three cached qualifications, including groups that did not use lamp7. That is conservative but broad invalidation. The record identifies the changed field; it does not supply old/new channel bits or the exact frame/time of the first change.

## The initial qualification had already failed

1050880 decomposes into256 (different picked light),2048 (exact key changed),1048576 (light-key subsection changed).

1051136 decomposes into512 (different effective spot-enabled/filter result),2048 (exact key changed),1048576 (light-key subsection changed).

WHY reason bits are accumulated history. keyReady1 means a qualification key was captured; keyAdmitted0 means that initial full-inside/lighting qualification **did not pass**. Later flicker invalidation is not the sole reason for zero activation. Simply removing permanent light-key demotion would leave the original failed cached admission false.

The qualification compares each member's box and finally the combined box. Groups0/1 encountered a different selected-light pointer somewhere in that sequence. Group2 encountered a different effective spot enable result after the production bounds filter. The supplied WHY records do not identify the offending member or distinguish member-versus-member from member-versus-combined-box failure. The shared witness lightKey describes the last matching prefix, not every member's actual pick. Do not invent specific lamp assignments or claim every member disagrees.

Production pickDynLight scores candidates using light intensity, nearest-point distance from the bag sphere and an approximate cone penalty (`tyra/engine/src/renderer/core/renderer_core.cpp:284-333`). The combined sphere is wider and changes that score. QBufferRenderer then constructs the actual object-space spotlight and may disable it over each original local bbox using stapipSpotHasNoInfluence. A combined bbox can intersect the light cone when an individual box is rejected. The group therefore can require a different shader/uniform path even though all authored rows have dynLit0 and identical source geometry/colors. DynLit0 controls probe/directional shader lighting; it does not disable this dynamic spot selection.

These are precisely the semantic differences the candidate was designed to refuse. The zero geometry mismatch proves the copying oracle succeeded for these cold arrays; it does not override the observed lighting guard failures.

## What safe continuation would require

Per-frame qualification can be safe if it recomputes every member and merged selected light plus effective production bbox filter for the current state, and falls back for mismatches. Persistent copied arrays need not be regenerated merely because a lamp's intensity flickers. This changes candidate admission policy and pays repeated EE selection/filter/key work; no positive activation or net gain follows from this source diagnosis. Current reported prequalification fails for all three groups, so repeating the same grouping on a nearby frame is not an established remedy.

A finer grouping could partition **contiguous original draws** into groups whose effective lighting inputs match and whose merged bbox/sphere also passes that same test. Testing only individual selected-light equality is insufficient; the combined pick/filter must agree too. Singleton groups preserve semantics but remove no bag. Repartitioning would need explicit per-member/current-combined pick witnesses, original opaque/depth/order guards, unchanged fully-inside domain and safe descriptor/array lifetimes. No such subgroup is identified as eligible by these records. Neither generic nearest-lamp grouping nor the existing authored material grouping is sufficient.

The eleven geometry rows are not proven intrinsically unbatchable in every view or lighting configuration. The exact three proposed groups are **unqualified under the reported scene/view/light state**. A later authored option that explicitly disables both scene-light reception and fallback spotlight reception for these objects would create a different declared rendering contract; with dynLightPick suppressed and spot disabled, those light keys could be irrelevant. It must be treated as that optional visual policy, not an equivalent full-light optimization or an excuse to relax the current guards.

Recommendation: retain v1/v2 as honest zero-activation diagnostics. Do not run a hardware speed claim for this candidate or create another fixture without a new explicit design decision. Proceed with the independently requested public receiver option; revisiting batching under that option is a separate experiment.
