# Motor District full night: review of the active PS2 render path

This was a read-only review. I used only Read, Glob and Grep, wrote no files and ran nothing. Every code reference is to the experiment fixture's own engine (`F:/Projects/tyrax2-lab-20261001/object-route-physical-v1/tyra/...`) and generated game (`.../object-route-physical-v1/game/src/gen/...`).

**Labels used below.** **[M]** is a measured fact from your archived same-ELF data. **[S]** is something I read in the source. **[H]** is my hypothesis. Per-bag averages are just a measured total divided by the measured call count. They still include observer effects and existing waits, and they are not new measurements.

## The three strongest remaining suspects

### Suspect 1: a fixed cost per `StaPipCore::render` call, repeated about 134 times — not any single arithmetic kernel

- **[M]** The Core preparation work before replay totals about 6.84–6.88 ms over 134–135 calls per loop (kind14). In the separate V8/V9 ELFs, the preparation work was about 7.1 ms and the owned submit/replay work about 3.5 ms. Those two are different ELFs, so they must not be added together. Object data alone (kind22) is about 2.39 ms over 134 calls.
- **[M]** Every exact single-boundary memo tried so far has given a tiny or unrepeatable gain: matrix key, clip-plane V4, 37-output cycle (0.065/0.029 ms), whole spot result (0.023/0.021 ms with 67–69 cold hits). The far-light gate made things slower.
- **[H]** Taken together, this says the bill is spread across each bag's entry: pointer chasing, branches, packet writes and code footprint. So the lever is either fewer full `render()` entries, or a cheaper entry for bags whose identity has already been proven. Making one formula faster won't do it.
- The existing experiments rule out "arithmetic inside one boundary". They do **not** rule out "how many times the whole per-bag path is entered".

### Suspect 2 (a measurement blind spot that decides which optimizations make sense): waits for the previous frame's GPU work are counted as non-pacing time

- **[S]** In pipelined mode, `RendererCore::completePipelineFrame` (`renderer_core.cpp:458–491`) waits in two places *before* the pacing timer starts at line 475:
  - `Vif1Queue::waitFor(pipelineSequence)` at line 468;
  - `sync.waitAndClear()` at line 472, which spins on the GS FINISH signal.
- **[S]** It is called through `completeBeforeSubmission` (`vif1_queue.cpp:237–252`) just before the next frame is submitted. While a frame is being recorded, `align3D` and `frameWaitVif` only record barriers (`renderer_core_sync.cpp:33`, `frame_submission.hpp:10–13`). So the EE does not block mid-recording, but it does block here.
- **[H]** Non-pacing time is therefore roughly the larger of (EE recording time) and (the previous frame's VU1/GS time), plus submit overhead.
- **[M]** The evidence that night is EE-bound is indirect: the sky retint saved 0.80/0.85 ms, and that showed up in non-pacing time. The 0.003 ms wait figure comes from an October 2 view, not from this fixture.
- **Why it matters:** if the GPU time for the full night frame is close to 16.7 ms, EE savings will just turn into wait time. If so, the ranking changes towards suspect 1's bag-count reduction, because that also removes one consumer-side FLUSHE boundary per bag.

### Suspect 3 (blind spot): elapsed-time scopes can't tell instructions apart from cache stalls

- **[M]** Earlier work already found memory effects:
  - About 20% of the per-bag uniform cost was cold packet lines (`docs/ee-submission-rearchitecture.md:1250`).
  - +0.42 ms of `prepare` came from alternating working sets in the interleave drip, and PCSX2, which emulates no data cache, did not reproduce it (same doc, around line 1455).
- **[S]** I found no prior use of the R5900 hardware event counters.
- **[H]** The night per-bag cost may be mostly D-cache and I-cache misses. That would explain why memos that remove arithmetic but keep the same memory touches recover almost nothing.

## Ranked ideas

### 1. A "companion pass" path that reuses preparation for bags already proven identical
- **Functions:** `StaPipCore::render` (`stapip_core.cpp:509–1243`) and the per-part loop in `game_physics.gen.cpp:2035–2050`: `part.bag`, then `aoBag`, `emisBag` and `renderEnvPass`, back to back.
- **[S]** The existing single-entry transform cache (`stapip_core.cpp:602–609`, `689–704`) reuses only the MVP and the object-space planes. Each following bag on the same model still repeats:
  - `getMaxVertCountByBag`, the 13 asserts, `cacher.getBBoxes`, the main-box cull, guard-band `computeClipObjectSpacePlanes`;
  - the world sphere and `pickDynLight` (`renderer_core.cpp:286`);
  - `buildSpotForBag` and the influence gate, and the fog-box test in `sendObjectData`.
- **Proposal:** the generated game declares the relationship explicitly ("same `vertices`/`count`/`model`/`bboxVersion` as the previous bag"), for example with a `renderCompanion(bag)` call. No content keys are needed.
  - Reuse: the bbox pointer, `frustumCheck`/guard-band result, MVP, clip planes and the chosen light.
  - Recompute anything that depends on material or info: texture/wrap/program/options/ALPHA, `spotLit`, `dynLightSkipSlot`, `fogDisabled`, and `maxVertCount` if the program class differs (it changes the package split).
  - FLUSHE and packet ownership stay exactly as they are.
- **First falsifying experiment:** a cold census (two witness frames, no timing) of every `render()` call, recording whether `vertices`, `count`, `info->model` and `bboxVersion` equal the previous call's, grouped by `NightAblation` category. Run it at night *and* in day. If few night calls are companions, or night adds no such calls compared with day, the idea is dead.

### 2. Split the 18.6 ms into EE recording versus waiting for the previous frame's GPU work (do this before choosing a direction)
- **Functions:** `RendererCore::completePipelineFrame` (lines 467–472) and `Vif1Queue::endRecordingFrame` (`vif1_queue.cpp:616–643`).
- **Experiment:** in the same ELF, add four Count reads per loop around `waitFor` and `waitAndClear`, plus a timestamp at the first DMA kick after `flushRecording`. Leave the pipeline on: `TYRA_VIF1_QUEUE_HOLD` disables it (`pipelineAvailable`, line 648), so that probe doesn't describe the shipped mode.
- **Falsifies:** if the wait is close to zero in every priced loop, the frame is EE-bound and ideas 1, 4, 5 and 6 stand. If the wait is large, deprioritize pure EE ideas.

### 3. R5900 hardware event counters on the existing kind14 and kind22 scopes
- **Approach:** program PCCR so that one counter counts I-cache misses and the other counts D-cache misses (check the event encodings in the EE Core User's Manual). Read them with `mfpc` at the same points as the existing Count reads, in the same ELF, in Off/On/Off order.
- **What it decides:** a high D-miss count per call points to idea 5. A high I-miss count points to idea 6. Low counts for both mean it is instruction count, and idea 1 alone is the lever.
- **Falsifies the cache hypothesis** if misses multiplied by the miss latency you measured before explain only a small fraction of the scope.

### 4. Bags restamped every frame pay for a vertex rescan and a retained-block rebuild
- **Code paths:**
  - **[S]** Rescan: `StapipBagBBoxesCacher::getBBoxes` calls `StaPipBagPackagesBBox::recalculate` (`stapip_bag_bboxes_cacher.cpp:80–97`). A count change triggers `std::make_unique` (line 90).
  - **[S]** Rebuild: `StaPipRetainedCommands::acquire` clears and recaptures blocks on any key move (`stapip_qbuffer_renderer.cpp:454–492`), with heap reallocation on a package-count change (472–473).
  - **[S]** Baked streams already have churn damping (lines 762–779), so I'm not proposing anything there.
- **[S] Unconditional restamps** in `game_lighting.gen.cpp`: 761/767, 1277, 1381/1387, 1883, 2204, 2295, 3982, 4108. Contrast the guarded ones (544, 2030, 4355/4365) and the blob key at 2452.
  - 1883 is the torch pool, which kind13 found inactive while driving. The activation of the others is unknown.
- **[M]** The kind15 "bbox lookup and size propagation" scope (0.84 ms) *includes* recalcs and is not split.
- **Change:** either the producer passes in the per-part bounds it already computes while writing the vertices, or it only restamps when its inputs change (the same pattern as the blob key).
- **First experiment:** a cold census of `stats.recalcs`, `fresh`, retained rebuilds and vertices rescanned per loop, attributed to producers. (These counters already exist under `TYRA_STAPIP_ATTRIB`; do the census without timers.) Near-zero night recalcs falsify the idea.

### 5. Hand each persistent bag its cache entry instead of a hash walk plus three pointer hops
- **[S]** Every hit goes `indexBuckets`, then `storage[i]`, then the `unique_ptr` object, then `bboxes->getVertexCount()`, then the heap `mainBBox` with its corners [0] and [7] (`stapip_bag_bboxes_cacher.cpp:119–140`, `stapip_bag_packages_bbox.cpp:41–57`).
- **Change (ownership):**
  - Put a handle (entry pointer plus generation) on the long-lived `StaPipBag`, validated by id, count, `maxVertCount` and version.
  - Store the main box inline in the cache item.
  - The generation must be bumped by `onFrameEnd` eviction and `rebuildIndex`, or the handle dangles.
- **First experiment:** only worth doing if idea 3 shows D-cache misses at least comparable to call count in the bounds scope. Otherwise drop it.

### 6. Split the object loop into two phases for I-cache locality (only if idea 3 shows I-cache misses)
- **[S]** Between consecutive bags, the loop runs a lot of game code: impostor and LOD logic, `dripHeavy`, `renderEnvPass` paint writes and `vuprog::setParams` (`game_physics.gen.cpp:1845–2096`).
- **[H]** That evicts StaPip code from the 16 KB I-cache every time.
- **Change:** run bounds, cull, light pick and fog facts for all of an object's bags (or a run of bags) first, then emit them in the *original order*. Packet order and barriers stay the same.
  - **Ownership constraint:** nothing may change a bag between the two phases. `renderEnvPass` writes paint colours just before its own render, and `dripHeavy` interleaves road bags, so it would start with plain, contiguous main/ao/emis runs only.
- **Falsifies:** the I-miss count per bag from idea 3 is low.

### 7. A per-bag descriptor of material-invariant values, built when the bag changes
- **[S]** `getCullProgramByBag` runs up to four times per bag: `stapip_core.cpp:448`, `qbuffer:2301`, `:2225` (unless the whole bag was replayed) and `:2051`. Each run walks `residentFallback`. `getMaxVertCount` divides each time (`stapip_vu1_program.cpp:149`). The TEX1/TEST/ALPHA words are rebuilt per bag (`sendObjectData` 1514–1621).
- **Change:** cache the program, `maxVertCount` and these words in the bag. Invalidate on `residentClasses`, any override, an info/material edit, or a LOD change.
- **[H]** Probably small on its own. It belongs together with idea 1.
- **First experiment:** a target disassembly instruction count for these calls on the hit route. If it is only a few dozen instructions per bag, skip it.

### 8. The 13 per-draw `TYRA_ASSERT`s are live in every game build
- **[S]** No game build defines `NDEBUG`. This is documented in the engine skill (line 308) and in `templates.cpp:605–608`, and the asserts sit at `stapip_core.cpp:528–578`. They re-dereference `info`, `color`, `texture` and `lighting` on every call.
- **Change:** run them when the bag is mutated or first submitted, not on every draw.
- **Experiment:** gate them behind a runtime boolean in the same ELF and toggle Off/On/Off. A null result falsifies it.

## Why VU0 isn't on the list

None of the leading suspects is a contiguous numeric batch. Light picking is at most 9 candidates over pointer data. Bounds are pointer chases. The memo results show that the per-bag arithmetic itself is cheap. A VU0 job would add upload and readback, a COP2 ownership fence against the macro-mode `Vec4`/`M4x4` helpers, and rounding differences. Your clip-plane V2 already failed the actual-target oracle on rounding. Nothing here amortizes those costs.

## Previous work I'm deliberately not re-proposing

Light-pick, spot or cycle memoization; the far-light gate; flashlight receiver and terrain-hull work (zero calls while driving); beam scratch (about 0.235 ms); the clip-plane specializations; generic CALL or SPR use; and removing FLUSHE or other barriers.

Which way to go depends on idea 2's result: if the wait is near zero, push idea 1 (backed by 4); if it's large, consumer-side work comes first. I haven't estimated any savings, and nothing here claims 60 FPS.
