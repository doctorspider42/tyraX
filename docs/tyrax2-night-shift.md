# Paused-cycle and object-data follow-ups

Three private experiments complete the follow-up to the [fixed-hour clock](tyrax2-paused-clock.md). None establishes 60 FPS in Motor District full night. Production remains at `e611a3bb1`: retained sky geometry and set-on-change paused mood are shipped; the additional cycle and local-light memo candidates remain private.

## Physical results

Every accepted order runs 5400 engine loops in one source-bound native ELF, with 320 priced loops per phase. The table reports complete non-pacing loop elapsed costs, including existing waits. It is not pure EE execution time. On/Off/On saving is middle minus mean outer phases; Off/On/Off saving is mean outer phases minus middle. The observer rows instead report enabled net tax. Do not subtract costs across different ELFs or add the small candidate estimates to earlier gains.

| Experiment / order | First, ms | Middle, ms | Last, ms | Saving or observer tax, ms | Outer drift, ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| Exact cycle reuse, On/Off/On | 18.451655 | 18.527035 | 18.472098 | saving 0.065159 | 0.020443 |
| Exact cycle reuse, Off/On/Off | 18.494494 | 18.474153 | 18.511038 | saving 0.028613 | 0.016544 |
| Object-route clocks, On/Off/On | 19.362170 | 19.107405 | 19.379206 | tax 0.263283 | 0.017036 |
| Object-route clocks, Off/On/Off | 19.112491 | 19.368945 | 19.133501 | tax 0.245949 | 0.021009 |
| Whole local-light result, On/Off/On | 18.627338 | 18.659355 | 18.644647 | saving 0.023362 | 0.017309 |
| Whole local-light result, Off/On/Off | 18.648106 | 18.634468 | 18.662256 | saving 0.020713 | 0.014150 |

Presentation remains approximately 33.37 ms in all accepted trials. The light-result estimates are close to control variation and too small to establish a useful production improvement. Fixed content keys and disabled gates are common code; their cost against a build without the experiment is not priced.

## Cycle evaluation reuse, kind 21

Both arms pause exact midnight and retain the promoted colors-only sky path. The private district script still sets the same hour each frame in both arms, so this contrast isolates evaluation reuse rather than pricing the production setter change. A cache keyed by scene and exact input-hour bits stores all 37 cycle output words; reset invalidates it. Sparse target witnesses at offsets 750 and 1155 recompute the original evaluation outside the priced window and compare all 37 words with zero differences. No additional timed Count scopes are introduced.

Both orders favor reuse, but the effect is small. The fixture has grading enabled: with grading disabled, original evaluation leaves `g_mixColor` untouched, whereas indiscriminate restoration would overwrite it. General production reuse would need to preserve assigned-field semantics and qualify lifecycle/mutable-state changes. This private result is not that qualification, and the candidate is not promoted.

## Five object-data regions, kind 22

This observer uses the production set-on-change mood and plain production day/night header, without cycle reuse. It splits the active route into five disjoint scopes:

| Region | On/Off/On mean On, ms | Off/On/Off middle On, ms | What it includes |
| --- | ---: | ---: | --- |
| uniformHead | 0.456568 | 0.452286 | Packet head/reset, FLUSHE emission, wrap, MVP and directional uniforms |
| spotLocalAndUpload | 0.765139 | 0.766948 | Local-light preparation, influence gate, clipper light and spot/custom uploads |
| uniformTail | 0.937221 | 0.939469 | Clip constants, color/options/fog, texture/test state, billboard/environment basis and ALPHA |
| clipperAndInfo | 0.135525 | 0.134997 | MVP pointer assignment and LOD/primitive information |
| routeFacts | 0.097950 | 0.097514 | Compiled-out telemetry hooks and remaining route predicates before replay |

Each scope observes 42880 calls and 85760 Count reads per enabled 320-loop window. Cold witnesses show 134 calls per region. The approximately 2.39 ms sum is observed work with instrumentation; net observer tax cannot be uniformly subtracted from individual scopes. `setBagMayClip` is outside these scopes, so this is not an exact reconstitution of the previous parent scope. Writing FLUSHE into a packet does not by itself establish a CPU wait.

The head's packet-QW witness is an absolute sum of packet cursor positions (1803 in the cold frame), not an incremental byte-production count: reset can decrease the cursor. Tail witnesses observe zero synchronous clip-capture waits in the checked cold frames, not a proof that every timed frame avoids them. The comparatively large tail cost includes numerical fog preparation and register packing, not just memory copying.

## Whole local-light result, kind 23

The third lead reuses the complete enabled `StaPipClipperSpot` result rather than only the affine inverse. Thirty-two bounded direct-mapped entries use exact 29-word content keys: all 16 model words, 12 float light inputs and the point-light flag. Full-key comparison resolves hash collisions; any non-finite float input falls back to the original function. Results are stored by value. Selection, influence rejection, uniform upload, barriers and packet ownership are retained.

Sparse target checks observe 134 calls, 71 enabled/eligible preparations and 67-69 hits when enabled; they compare 1128 semantic words (134 flags plus 14 words for each active result), with zero differences in both accepted orders. These witnesses establish cold activation, not a hit census of every priced frame. Host actual-source controls also exercise 300000 repeated comparisons, 2900000 individual key mutations and 4000 special-key cases. They do not predict R5900 rounding; special non-finite cases in an unused matrix word establish fallback gating, not every non-finite arithmetic result.

The first native trial is explicitly rejected. Its comparator read position/direction fields of inactive results even though Tyra's empty `Vec4` constructor does not initialize them. Differences appeared in control arms too, and `NIGHTDONE valid=0` disqualifies its price. V2 changes only the oracle: compare the enabled flag when inactive, all semantic fields when active. The initial host stub initialized vectors and missed this problem; the corrected host stub matches the empty target constructor. Undefined inactive storage must never become an exact-output oracle.

## Qualification and archive

Four native builds (V48-V51), actual shared-cache source/link audits and target ABI checks pass. Each fixture binds 501 source inputs and 298 runtime assets. Resident VU images and ABI sizes remain unchanged. Host parser controls test both orders and reject malformed activation/comparison/read counts. Six accepted physical runs complete 32400 loops; the invalid V1 oracle run is preserved separately.

Each accepted fixture also completes 5400 PCSX2 loops with three owned warm captures. Root visual review finds the car, map, lamps, shadows and HUD present without visible stretched triangles in the stationary night view. Emulator costs are not hardware prices; no new human physical visual feedback, motion or all-scene qualification was obtained for these private candidates.

[The immutable evidence archive](tyrax2-night-shift-2026-10-06/payload-manifest.json) preserves source blobs, native/ABI records, strict physical logs, ownership and arrival records, parser controls, host differential harnesses, rejected oracle evidence and emulator captures. ELF/symbol and runtime asset hashes are retained instead of their binaries. Original host paths identify the observed environment; replay requires reconstructing the recorded fixture/helper layout and the pinned toolchain. Historical inherited status labels in helper proofs do not rename these experiments; kind, source and ELF bindings identify them.

The remaining question is whether uniform preparation and packet scheduling can be reduced together without moving the same work into a more expensive ownership or synchronization path. These results rule out a large win from these two exact memo boundaries in the tested stationary full-night pose; they do not prove a hardware ceiling for all equivalent renderers.

## Independent Claude CLI review

After the three experiments, the requested Claude CLI audit completed with 79 read-only Read/Glob/Grep calls. Its [unaltered eight-idea report](tyrax2-night-shift-2026-10-06/claude-review/report.md) and [root assessment](tyrax2-night-shift-2026-10-06/claude-review/root-assessment.md) are archived with the prompt, invocation, output and completion hashes. No returned suggestion was implemented or priced as part of this audit.

The strongest implementation lead is shared geometric preparation across consecutive main/AO/emission/environment passes. The generated per-part loop really contains these calls; whether enough identical, owned companion bags are active is still unmeasured. First census actual adjacent geometry/model/version identities and producer rebuilds in the same day/night pose, then qualify a narrow preparation token with per-pass material/program/light state and all consumer fences retained. Packet order and borrowed-data lifetime remain mandatory.

The report also proposes refreshing completion waits, EE hardware cache counters, producer restamp control, direct bbox handles, two-phase preparation, material descriptors and validated assertion reuse. These are hypotheses. A correction matters: [October 4 V3](tyrax2-minimal-scopes.md#scene-completion-and-core-aggregate-follow-up) already measured a contained completion-minus-pacing remainder near 0.00312 ms; its raw 13.8 ms duration includes pacing. A refresh may test the new fixture, but this is not evidence of a newly found large GPU wait. Hardware miss counters require verified event semantics and measured overhead, and cannot be treated as a scalar stall-time bill. Small memo gains likewise do not alone prove that all numerical work is cheap.
