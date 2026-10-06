# Claude proposal trials

These private trials follow the [independent Claude review](tyrax2-night-shift.md#independent-claude-cli-review). Production renderer behavior remains unchanged. Two physical assertion-gate contrasts are accepted; companion/rebuild activation and owned TEX1 parity are qualified in PCSX2 only. None establishes full-night 60 FPS.

## Original validation cost, kind 25

The original nonempty static Core head contains **twelve** assertion statements, despite its inherited comment saying thirteen. A private gate bypasses those twelve statements in ordinary On frames. Both arms still execute all original expressions in sparse control frames at offsets 750 and 1155. The ten-statement block and two-statement block retain expression/message bytes, evaluation order and short-circuit behavior; `ENV_NORMALIZED` remains between them and outside both gates. No global `NDEBUG` change is made.

| Order | First, ms | Middle, ms | Last, ms | Saving, ms | Outer drift, ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| On/Off/On | 17.945630 | 18.123788 | 17.960161 | 0.170892 | 0.014531 |
| Off/On/Off | 18.134227 | 17.950150 | 18.143167 | 0.188547 | 0.008940 |

Each order completes 5400 engine loops with 320 priced loops per phase in the same ELF. Costs are complete elapsed loop costs minus existing pacing, including remaining waits and preemption; they are not pure EE execution time. Presentation stays around 33.37 ms. Do not compare absolute costs across other experimental ELFs or add this saving to unrelated estimates.

Cold records count actually executed original validation statements, zero bypassed statements, and requested On state separately. Hot bypass activation is inferred from source and arm state, not counted across the priced window. Common gates and bookkeeping remain unpriced against a build without the experiment. Root executes an extracted-original-expression host harness covering twelve first-failure invalid cases and three valid cases; this is not target ABI or whole-renderer malformed-input safety.

Both orders favor bypass by more than outer-control drift, but the remaining cost is still above the 16.67 ms frame budget. This prices validation in a known-good fixture; it does not authorize disabling production assertions or prove a safe general validation token. The candidate remains private.

## Companion and producer census, kind 24

The same-pose day/night/day fixture keeps all ordinary effects and uses no additional Count scopes. At offsets 750 and 1155 only, it records actual nonempty Core entries, acceptance, adjacent geometry/model/view/frustum/partition identities, bbox events and retained/baked packet events. Main/AO/emission/environment roles annotate actual generated submissions; Other includes unannotated or unknown attribution. Deferred qbuffer producer tags retain the producer role, rather than attributing deferred work to the current bag.

All six PCSX2 cold witnesses agree within their respective mood:

| Cold witness per frame | Day | Night |
| --- | ---: | ---: |
| Entered / accepted bags | 118 / 117 | 135 / 134 |
| Main / AO / emission / environment / Other | 38 / 0 / 0 / 2 / 78 | 49 / 0 / 0 / 2 / 84 |
| Same adjacent geometry / eligible exact preparation | 0 / 0 | 0 / 0 |
| Bbox hits / recomputations | 115 / 2 | 129 / 2 |
| Rescanned vertices | 1206 | 1206 |
| Retained packet-block builds | 112 | 132 |
| Retained acquisition rebuild attempts | 1 | 1 |

This weakens the adjacent companion-token proposal in this stationary scene: its AO/emission passes are inactive and no consecutive entered bags satisfy the geometry identity. It does not rule out reuse in other maps or a different submission order. Block counts are not bytes, elapsed costs or rebuild frequencies across every priced frame. Acquisition rebuild attempts may subsequently be refused.

Identity capture must follow the original `setMaxVertCount`: `clipPackageSize()` reads the member updated there. Independent review caught a stale previous-bag value before freeze and the generator was corrected. Plane keys include only initialized normal.xyz/distance values, never Vec4 padding. Model identity compares both pointer and all sixteen words; the same vertex pointer alone is insufficient.

The physical start after the assertion trial's reset received no log, no `loadelf`, no `NIGHTCONFIG` and no live artifact; ping also failed. It is preserved as a rejected **no-device-start** attempt, not a renderer runtime failure or physical census. No hardware day/night contrast or observer-tax claim is accepted. Common census code and role bookkeeping remain unpriced.

The [source follow-up](tyrax2-claude-trials-2026-10-06/primary-review/night-main11/report.md) identifies an exact-cardinality explanation for the main-role difference: `district_mood` shows eleven individually submitted night-only boxes, comprising eight windows and three garage trim/blade objects. They have dynamic lighting disabled and static batching disabled. Aggregate runtime roles do not individually attribute each object, and no elapsed cost is inferred. An effects-off night/day comparison still has different authored geometry if it leaves this visibility rule intact.

## Owned TEX1 preparation, kind 28

This narrow material-state prototype precomputes current/linear/nearest TEX1 register words for StaPipCore's private initialized LOD. Generic public renderer initialization retains original packing of the current seven mutable LOD fields unless a caller explicitly invokes the new ownership opt-in. That opt-in is public in this private prototype; it is a caller contract, not compiler-enforced exclusivity.

The existing `sendObjectData` still precedes `setInfo`: the emitted TEX1 therefore preserves the previous filter state, including the first bag. Selector maintenance is shared by both arms. The actual R5900 class-layout check records 4608 to 4672 bytes with 64-byte alignment; this extra common state is unpriced against an unmodified binary.

Actual-source host controls pass 200001 owned/control comparisons and 70000 mutable generic field cases. Native V54 and the strict 5400-loop emulator test pass. Each of six cold frames compares 268 semantic words across 134 full 64-bit TEX1 emissions, with zero differences. The On cold frames observe 134 candidate uses; Off uses original packing. All observed filters are linear and no generic fallback occurs in this scene. Host controls, rather than this runtime, exercise mixed filters and mutable generic callers. No new Count scopes or waits are added.

The stationary images remain normal. Physical saving is unknown because the console remains unreachable after the preceding reset. No production promotion is accepted; class/code footprint and common selector work need hardware pricing too.

## Hardware counters: prepare, do not yet run

The [primary review](tyrax2-claude-trials-2026-10-06/primary-review/pmu-overflow/report.md) corrects two assumptions before PMU execution. Sony's EE manual specifies 31 value bits and an overflow flag; overflow can generate an unmaskable Level 2 exception before a later endpoint check. Leaving counters active throughout phases/loading/export is unsuitable. The D-side selector also counts uncached loads, so label it a **D-side bus-read/cache-miss event**, not a pure data-cache miss rate or elapsed stall bill.

The revised unreleased source uses common per-sampled-Scene reset/enable/stop in both arms and four ordered endpoint reads only in On. This shortens exposure but does not establish a universal lifetime bound around inherited unbounded waits. Maximum event rate and safe enabled lifetime remain runtime gates. Fifty synthetic/parser controls and root-executed host guard harnesses pass; a permanent failure latch prevents later samples/phases rearming after failure. An already entered scope stops only its still-known owner. Same-value foreign ownership cannot be detected through register-value guards alone.

A standalone R5900 compile of the actual wrappers confirms MFPS/MFPC/MTPC/MTPS and seven `sync.p` instructions, with no `mtc0` Count write. This is not linked application placement or full-game native/runtime qualification. Runtime preflight explicitly blocks the unresolved lifetime gate. No observer tax, miss totals or recoverable milliseconds are claimed. `PerfTest::StartSampling` must not be used because it writes the shared COP0 Count clock. Both unreleased source revisions and the initial pre-latch harness are preserved separately from accepted renderer fixtures.

The older completion-minus-contained-pacing result remains relevant: [October 4 V3](tyrax2-minimal-scopes.md#scene-completion-and-core-aggregate-follow-up) already measured a remainder near 0.00312 ms. A raw completion duration containing pacing is not evidence of a newly discovered large GPU stall.

## Evidence and limits

Native V52-V54 bind 501 source inputs, 298 runtime assets, actual target ABI and linked VU images. A mistaken inherited audit expectation for `night_ablation.hpp` in the restored assertion qbuffer renderer is rejected separately; the corrected audit removes only that expectation and retains source/dependency/link/ABI/VU checks. Runtime preflight verifies proof hash and exact source-manifest/ELF/symbol identities against provenance and actual files.

All three renderer fixtures complete 5400 PCSX2 loops with three owned warm captures each. Root finds the car, map, lamps, shadows and HUD present without obvious stretched triangles; kind 24 visibly restores day/night/day. These are stationary views, not motion/all-scene tests or new human physical visual confirmation. PCSX2 costs are never hardware prices.

The [evidence manifest](tyrax2-claude-trials-2026-10-06/payload-manifest.json) preserves frozen source blobs, native/ABI records, both accepted physical runs, emulator captures, host controls and the rejected start. Historical draft labels in preparation reviews are superseded by root freeze/native/release records. ELF/symbol/object binaries and runtime assets are identified by hashes rather than redistributed. Primary manual citations/hashes are retained without copying the complete manual into the repository.
