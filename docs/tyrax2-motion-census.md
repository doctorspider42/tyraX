# Vehicle-free camera rotation census

This private diagnostic tests whether camera rotation increases EE package preparation in Vehicle Playground. It removes vehicle objects from every scene in a separate generated project and preserves the production renderer's decisions. No renderer optimization is introduced.

`tools/tyrax2-motion-probe.py` prepares the fixture from the current checkout. The private engine compiles FrameProfile and HardwareTrace out, suppresses healthy log output and the periodic queue arena report, and counts bag routes, materialized package records, strip expansion vertices, qbuffer stream-copy bytes, mutable snapshot bytes, baked misses/churn, reflection captures, paint updates and geometry rebuilds. Warnings/assertions remain. The ordinary cumulative pacing stamps are reused. No waits or completion fences are added.

Six 1200-loop phases sample offsets 300 through 659: parked initial view, one-degree-per-loop rotation, parked worst observed rotation angle, repeated rotation with counters disabled, repeated rotation with counters enabled, and parked worst angle again. The camera eye is captured after startup. Phase 1 selects the largest non-pacing sample and saves its angle for subsequent parked controls. Repeated sweeps use the same angular sequence. A degree per loop is deliberately frame-indexed, not a constant physical angular speed. The noon clock remains paused by DistrictMood.

Each sampled loop uses three Count reads: engine entry, immediately before renderer.beginFrame, and engine exit. The update/render split is inclusive elapsed time, including preemption and existing waits; pacing is separately available for the whole loop. Counters are not timings. The common observer footprint is unpriced; the disabled-counter sweep tests net perturbation rather than supplying a uniform correction. Different angles expose different geometry. A single selected maximum can be an outlier, so parked results and repeated sweeps must be interpreted together.

CSV output happens after each measured window. `tools/tyrax2-motion-run.py analyze <bin>` verifies frame coverage, angular sequences, accounting, disabled counters and selected-angle agreement. Native build, emulator correctness and physical measurements are separate gates.

## Physical result, 2026-10-07

The PS2 completed all 7200 loops (`MOTIONDONE valid=1`), yielding 2160 sampled rows. Raw evidence, analysis and console log are preserved. Times below use EE Count at 294,912,000 ticks/second. "Work" means inclusive loop elapsed time minus the existing pacing accumulator, **not pure EE execution time**: existing DMA/VU/GS waits and preemption are still included.

| Phase | View | Mean work ms | P95 ms | Maximum ms | Periods above 25 ms / 360 |
| --- | --- | ---: | ---: | ---: | ---: |
| 0 | Parked, initial direction | 9.405 | 9.544 | 9.627 | 0 |
| 1 | Rotation, counters on | 10.931 | 14.431 | 18.339 | 1 |
| 2 | Parked at selected 72 degrees | 13.484 | 13.550 | 13.588 | 0 |
| 3 | Rotation, counters off | 10.916 | 14.442 | 18.203 | 1 |
| 4 | Rotation, counters on again | 10.972 | 14.450 | 18.328 | 1 |
| 5 | Parked at 72 degrees again | 13.520 | 13.577 | 13.603 | 0 |

This reproduces a repeatable isolated missed refresh, **not the user's sustained rotation FPS collapse**. Mean entry-to-entry periods are 16.68 ms parked and 16.73 ms rotating. The no-counter sweep retains the same worst angle and missed-refresh count. Its mean differs by 0.015/0.056 ms from the two counted sweeps; this is a net perturbation observation, not a calibrated observer cost.

At 72 degrees, the moving sample versus the parked control has:

- Identical bag routes: 98 total, 2 outside, 85 direct, 11 partial; 180 materialized package records, 1479 strip-expansion vertices and 81,696 qbuffer stream-copy bytes.
- **79 instead of 85 whole-bag replays**, with **6 baked-stream misses instead of 0**.
- **273,072 instead of 162,768 mutable REF snapshot bytes** (+110,304 bytes). This counter excludes chain copies and is not total EE memory traffic.
- 18.339 ms versus approximately 13.5 ms of non-pacing elapsed time. The repeated counted rotation reaches 18.328 ms at the same angle. This is an association between a fallback/rebuild path and a spike, not attribution of the whole approximately 4.8 ms to copying alone.

The two counted full sweeps have exactly matching per-angle operation counts, including 192 baked misses per sweep. Means across a sweep do **not** show a blanket explosion of clipping: qbuffer copy bytes and expanded vertices are lower than in the initial parked direction. Geometry exposed by the view matters independently of motion.

Mean game-update time is 0.51-0.53 ms. Reflection capture, paint update, geometry rebuild and baked churn-skip counters are zero throughout counted windows; `envUsers=0` in all context records. These subsystems are excluded only for this fixture, not for the original vehicle-bearing/night scene.

## Interpretation and next discriminator

There is a concrete EE preparation lead: camera rotation repeatedly reaches bags whose baked streams cannot replay, causing additional preparation and mutable snapshot traffic. The cache implementation can lose entries through both the 250-frame lifetime and budget eviction, or rebuild an existing entry after a key mismatch. The current aggregate miss counter **does not distinguish these causes**. Do not increase cache limits, disable invalidation, or remove snapshot ownership based on this result alone.

The next narrowly scoped test should classify `StaPipBakedStreams::acquire` misses, lifetime retirements, budget evictions and occupancy, then test only the identified cause at matched angles. Separately reproduce the user's actual location, pitch, rotation speed and map settings before claiming the sustained FPS symptom is explained. This fixture fixes the spawn eye, forces a horizontal look, removes all vehicle scene references, suppresses HUD, and pauses noon; it is deliberately not the complete original gameplay workload.

## Cache-cause follow-up

The follow-up fixture adds mutually exclusive miss categories (new entry, bbox version, content version, primitive state, stream addresses, program, depth scale, remaining count/size mismatch), expiry and budget-eviction counts, rejected reserves, and per-loop peak live arena bytes/entry counts. It preserves the six-phase protocol and all cache policies. Categories use the existing mismatch priority, with depth scale separated from the remaining count/size group; they describe the first differing category, not every differing field. Expiry/eviction counters describe removals within sampled windows, not a per-entry causal history. Occupancy excludes graveyards, allocator overhead, retained-command storage and other renderer memory.

The analyzer accepts the original CSV and checks that classified misses sum to the aggregate when the new columns are present. The follow-up parser controls additionally reject a miss-category imbalance and a mismatched final parked angle.

### Follow-up physical result

Both counted sweeps on PS2 produced **192 new-entry misses, 192 lifetime expirations, zero key-mismatch misses, zero budget evictions and zero reserve failures**. Every counter matches between the two sweeps at every angle. Peak live baked arenas occupied **2,422,896 bytes** against the unchanged **4,194,304-byte** budget. Parked controls have zero misses and zero expirations in their sampled windows.

At 72 degrees there are again six new-entry misses, 79/85 whole-bag replays and 273,072 mutable snapshot bytes. All key-mismatch categories are zero. The same-angle parked controls replay all 85 bags and copy 162,768 snapshot bytes. The physical non-pacing times are:

| Phase | Mean ms | P95 ms | Maximum ms | Periods above 25 ms / 360 |
| --- | ---: | ---: | ---: | ---: |
| Initial parked | 9.636 | 9.766 | 9.844 | 0 |
| Counted rotation | 11.123 | 14.618 | 18.531 | 2 |
| Parked at 72 degrees | 13.715 | 13.765 | 13.793 | 0 |
| Rotation, counters disabled | 11.054 | 14.555 | 18.395 | 1 |
| Counted rotation again | 11.184 | 14.662 | 18.587 | 2 |
| Parked at 72 degrees again | 13.732 | 13.786 | 13.817 | 0 |

**Inference:** lifetime expiry is the leading explanation for repeated rebuilds in this workload, not changing geometry/material keys or sampled-window budget pressure. `StaPipBakedStreams::onFrameEnd` decrements the countdown and erases expired entries; `acquire` refreshes it to 250 frames. At approximately 60 Hz that is 4.17 seconds without a baked acquire, shorter than this six-second revolution. A partial/clipped draw need not refresh the baked entry, so "not acquired" is not synonymous with "not visible". There is no per-entry retirement history in this probe, and events outside sampled windows are not counted: matching aggregate totals are not a proof that each particular new miss corresponds to a particular observed expiry.

At this stage, the next causal discriminator was a private matched-angle lifetime-only control/candidate/control; its result is recorded below. Preserve the memory budget, invalidation and DMA ownership rules. This still does not reproduce sustained slow rotation in the original gameplay workload.

Observer qualification matters: counted sweep means exceed the disabled sweep by 0.069/0.130 ms and record **two** long periods instead of **one**. The added diagnostics can push a marginal frame over a presentation threshold. Header/storage layout, hook branches and miss-classification comparisons are common to both arms and remain unpriced. Do not subtract the two diagnostic ELFs' times to price the new counters, or present the miss/expiry census as pure EE instruction timing.

Evidence is under cache-causes, including both raw CSVs, complete logs, screenshots, native binding, build output, launch inputs and parser controls. Private fixture: `F:/Projects/tyrax2-motion-cache-20261007-v1`; physical/emulator ELF SHA-256 `849f3a8da0f1588d9e7b70264eea04d5a5aeef431f4b1bf4f40fbc09486d18b9`. Both completed 7200 loops and passed the 2160-row parser. Native source/mirror equality covers 465 files; all eight malformed parser fixtures were rejected. PCSX2 screenshots were inspected, and its counters agreed, but its elapsed times are not hardware results. The PS2 launch used the user's confirmed manual reset and the same executable/arguments/host marker as the editor. Owned emulator processes were stopped; the physical file server remains alive.

## Verification and provenance

### Lifetime-only experiment

`tools/tyrax2-motion-probe.py <new-folder> --lifetime` selects a separate protocol, analyzed with `tyrax2-motion-run.py analyze-lifetime <bin>` and checked with `selftest-lifetime`. Six 1800-loop phases all rotate through the same frame-indexed angles. Their baked-entry refresh lifetimes are 250/1000/250, repeated with counters disabled. Each phase samples offsets 1200..1559 (angles 0..359), allowing more than the longer lifetime before sampling, including after returning to the shorter policy. No cache clear is inserted. The eviction selector's "not touched this frame" ceiling follows the selected refresh lifetime; budget, maximum evictions, invalidation and immutable/DMA retirement remain unchanged. Only the private baked-stream implementation is altered, not the retained-command cache or production renderer.

The common sampler/layout/selector overhead remains unpriced. The second triplet disables counters, not their compiled hooks. The parser verifies identical geometry route/expansion/copy workloads across the counted triplet, rather than assuming that fewer misses means the same scene was rendered. Counts of materialized package records and mutable snapshots may legitimately change when replay succeeds.

The first lifetime fixture (`F:/Projects/tyrax2-motion-lifetime-20261007-v1`) passed the native build, 465-file source/mirror binding, parser positive/negative controls and all 10800 PCSX2 loops. Its counted A/B/A miss and expiry totals are **192/0/192**, with zero budget evictions or reserve failures. All checked per-angle geometry workloads agree. At 1000 frames, every direct bag replays and live arenas remain at 2,422,896 bytes; the 250-frame controls reach the same peak but retain less on average. This is a causal lifetime intervention on the emulator, **not a physical performance result or production qualification**. Screenshots were inspected for gross rendering failures, not pixel-equality proofs. Emulator evidence and its raw CSV are preserved. ELF SHA-256: `7dfd4155621582a11ae1fb6f2bc67c3b878e020cf277c86138839a75ece74060`.

The first physical launch was blocked: the scripted reset used an owned listener, `-t 10 reset`, a two-second listener grace and a further ten-second settle. The reset process did not exit within 15 seconds and was stopped; the subsequent `execee` produced no load/start output either. After the user manually reset to ps2link, attempt 2 loaded the pinned ELF and completed **10800 loops, valid=1**, with all **2160 rows** accepted. This initial launch blockage was not a renderer failure.

### Lifetime physical result

The same-ELF 250/1000/250 trial on PS2 confirms the causal effect on this workload. Counted misses and expirations are **192/0/192**; every direct bag replays in the candidate. The before/after controls match every counter at every angle. Geometry routes, expanded vertices and qbuffer-copy bytes also match the candidate at every angle. Budget evictions, failed reserves, key mismatches, reflection captures, paint updates and geometry rebuilds remain zero.

| Counters | Lifetime | Mean non-pacing ms | P95 ms | Maximum ms | Periods above 25 ms / 360 |
| --- | ---: | ---: | ---: | ---: | ---: |
| On | 250 | 11.074 | 14.613 | 18.426 | 1 |
| On | 1000 | 10.767 | 14.148 | 15.077 | 0 |
| On | 250 | 11.076 | 14.642 | 18.412 | 1 |
| Off | 250 | 10.959 | 14.466 | 18.393 | 1 |
| Off | 1000 | 10.652 | 14.012 | 14.870 | 0 |
| Off | 250 | 10.961 | 14.510 | 18.263 | 1 |

At the previously identified 72-degree angle, counted work changes **18.426 -> 15.077 -> 18.412 ms**, and mutable snapshot traffic changes **273,072 -> 162,768 -> 273,072 bytes**. With counters disabled, the same angle changes **18.393 -> 14.870 -> 18.263 ms**. Mean savings over matched angles are **0.307/0.309 ms** against the counted controls and **0.307/0.308 ms** against the disabled-counter controls. Existing waits and preemption remain included: these are not pure EE instruction times.

Peak live baked arenas stay at **2,422,896 bytes** in all counted arms, below the unchanged 4,194,304-byte budget. The candidate holds that much throughout the window, whereas the controls average 2,238,754 bytes: it retains about 184 KB more on average, not zero additional residency. These numbers exclude retired arenas, other caches and allocator overhead; no general memory-pressure safety claim follows.

This qualifies a private expiry-policy change as a remedy for the isolated rotation spike in the no-car, fixed-eye, horizontal noon fixture. It does **not** establish stable 60 FPS for normal vehicle gameplay, night views, travel through the map, other angular speeds or memory-pressure cases. No production cache policy has been changed. A production proposal should test those workloads and retained-memory behavior before choosing a general lifetime policy; 1000 frames is an experimental value, not a proven universal optimum.

Physical raw CSV, analysis and paired comparisons, complete log, launch identity and asset hashes are preserved alongside the emulator evidence. The physical file server remains running; the test parks after completion. Nine malformed lifetime-parser fixtures were rejected, including a mismatched geometry workload. `git diff --check` passed. User-owned example changes were not modified.

### Original census provenance

- Private fixture: `F:/Projects/tyrax2-motion-20261007-v2`. No production renderer changes. Tooling is a workstation diagnostic, not a portable test runner: generation uses `build/tyrax-editor.exe`, and native verification/emulator launch use the recorded WSL paths.
- Native build succeeded. Binding verifies 465 project/engine source files against the native mirrors, the mirror ELF against the shipped ELF, and one shared `MotionProbe::rows` symbol in the companion symbol ELF. The original preparation manifest predates format-warning fixes and is not authoritative; use this post-build binding.
- Physical and emulator launch ELF SHA-256: `624dc3f0ded8664b70e72a420cd2345acc5fc548a5e69e69b605f428f3675af9`. Physical asset hashes are in launch inputs. Build output retains inherited warnings; the diagnostic formatting warnings were fixed before this ELF.
- PCSX2 completed the protocol; map screenshots were inspected and the parser accepted all rows. Its matching operation counts validate the workload, **not hardware performance**. Emulator data and configuration are kept in the `emulator/` evidence directory.
- Parser controls accepted the valid fixture and rejected six malformed fixtures: missing row, angular mismatch, disabled-counter leak, broken accounting, unexpected paint update and wrong selected angle.
- Rejected attempts: an initial preparation failed an anchored package-output assertion; an initial emulator boot inherited a directory named `bin/launch.scene` and crashed in HLE file reading. That directory was moved aside, and preparation now excludes inherited `bin/`. Neither attempt is a renderer performance result. The final physical launch followed a manual reboot to ps2link after a network reset produced no response. **Correction:** absence of output was not proof that reset failed. `Runner::resetPs2Link` explicitly permits silent resets, sends once with a listener, and `launchPs2Link` allows a further ten-second settle before `execee`. Follow this sequence for subsequent scripted reloads instead of treating silence as failure.
- The physical ps2client file server remains running for the user. The test parks the camera after completion. Existing example edits and the unrelated character-generator directory were left untouched.
