# Selected physical renderer scopes (2026-10-04)

The [PCSX2 execution map](tyrax2-pcsx2-debugger.md) supplied suspects; six PS2 boots now check three selected intervals in the ordinary night vehicle-entry view. Every boot completed5400loops, with unchanged clocks and the same native ELF. This is attribution work, not a renderer optimization or a60FPS acceptance.

| Selected interval | Three On phase means, ms | Interpretation |
| --- | --- | --- |
| Terrain prepare/submit |2.858047 /2.866497 /2.858227|Whole selected terrain function, including its renderer calls and waits|
| Wheel prepare/submit |0.306488 /0.304232 /0.305761|Whole selected wheel function; lower priority as the main60Hz target|
| First terrain core invocation |1.108354 /1.113184 /1.103257|Global invocation6, count588, verified terrain caller context|

These are inclusive physical COP0 Count durations. They do not distinguish texture upload, bounds, clipping, packet copying or consumer waits. The first terrain invocation warrants a narrower route audit. The wheel interval cannot by itself explain the remaining full-frame budget gap. Its private direct-vertex-store candidate is prepared but unbuilt; no wheel optimization gain is accepted.

## Apparatus and its price

Each target has same-ELF Off/On/Off and On/Off/On boots. Each phase retains128 twelve-byte records, the same selection/depth/shape counters and reset branches. Selecting one interval adds two Count reads to the existing two-read loop sampler: four diagnostic reads per normally sampled loop,518 per On phase. The added1536-byte ring and common compiled layout/traversal are unpriced; both arms contain them.

Four On-minus-Off contrasts per target range from-0.179725 to+0.105574ms averaged over each320-loop tax window. Same-state outer-phase spreads reach0.209694ms. Sign changes forbid a uniform observer fee or guessed subtraction. The selected intervals repeat more closely than whole-loop phase means, but this does not establish full ordinary-state equality or apparatus-free FPS.

The six runs yield2304 scope records,1152 timed records and135 core calls in every sampled loop. Mode0 selects exactly one stage invocation; mode1 selects one global core ordinal with its vertex count and active producer context checked. It does not measure an aggregate of all bags. The machine record contains each raw-log/artifact/ELF/source/independent-review pin, phase statistics and contrasts.

## Quiet output and preserved evidence

The first private fixture completed emulator mechanics, but its inherited every120-frame DMA reports overlapped measurement windows. It is preserved as rejected timing apparatus. The accepted V2 disables the whole periodic report body in both arms, retaining queue bookkeeping. Its parser rejects all four report prefixes. Boot allocation, loading and sparse context/export writes remain outside the tax/sample windows; this is not a claim that the whole log contains no host I/O.

PS2client emitted a few non-UTF8 banner bytes. The raw files remain unchanged. Separate ISO8859-1-to-UTF8 copies are byte-bijective, with all diagnostic protocol rows checked as ASCII; independent review binds both files. Corresponding emulator images were inspected. There is no fresh captured PS2 raster or pixel-parity acceptance in this record.

The [machine record](tyrax2-minimal-scopes-2026-10-04.json) qualifies native source/mirror/dependency closure, actual ELF/symbol text,294 byte-equal baked assets and four native ADPCM conversions. Scope headers received actual R5900 compile-only layout verification and host O0/O2 controls. All independent physical pair audits pass. No production render code changes here.


## Scene, completion and Core aggregate follow-up

Eight further physical boots complete the V3 Scene/completion/flush pairs and the V4 same-frame Scene/Core aggregate. Each pair has same-ELF Off/On/Off and On/Off/On orders, 5400 loops per boot and 384 records per boot. The [frame/Core machine record](tyrax2-frame-core-scopes-2026-10-04.json) retains every On window, observer contrast, sparse context and source/native/ABI/raw/archive/independent-review pin. The earlier V2 machine record remains unchanged.

| Inclusive interval | Three On phase means, ms |
| --- | --- |
| V3 Scene | 17.699069 / 17.715308 / 17.658848 |
| V3 previous-frame completion | 13.884182 / 13.838951 / 13.898886 |
| V3 native recording flush | 13.983902 / 13.881169 / 13.951314 |
| V4 Scene | 17.445409 / 17.321492 / 17.437626 |
| V4 sum of 135 disjoint Core intervals | 13.905286 / 13.909963 / 14.153445 |
| V4 same-frame Scene minus Core sum | 3.540123 / 3.411528 / 3.284181 |

Columns follow order0 phase1, then order1 phases0 and2. These are physical COP0 Count elapsed intervals, including existing waits, preemption and instrumentation. V4 verifies exactly one Scene and135 nonnested Core calls within that Scene, with49794 summed bag vertices in every sampled frame and zero invalid records. The vertex sum qualifies geometry shape, not upload or copy volume. Scene minus Core is inclusive remaining Scene time: it also contains the observer's accumulator/depth maintenance outside each Core clock interval.

Selected V3 completion samples each own one rendered Pipeline presentation, with no synthetic or multiple presentation. Source places that presentation's sole existing pacing increment inside the selected completion. Completion minus its existing pacing counter therefore qualifies only an inclusive remainder of that observed completion: means0.002919 /0.003221 /0.003219ms, combined384-sample mean0.003120ms. This is not pure EE cost. Scene minus whole-loop pacing lacks the same containment proof. Flush calls completion before submission; completion, flush and Scene can overlap. Separate boots do not form a decomposition, and their durations must not be summed.

V3 has518 diagnostic Count reads per On phase; V4 has35078 (34816 scope plus262 cadence reads), versus6 in Off. The shared8100-byte arrays remain unpriced. V4 also adds nominal68 common bytes of aggregate state; this figure describes declared uint32 state, not linker placement or a priced runtime footprint. Resets, shape/depth checks, stores and layout effects remain in both arms.

| Pair | Four On-minus-Off non-pacing contrasts, ms per tax loop | Two outer spreads, ms |
| --- | --- | --- |
| V3 Scene | +0.001578 / -0.023693 / +0.106367 / +0.064496 | 0.025271 /0.041871 |
| V3 completion | -0.032482 / -0.016549 / +0.016948 / -0.007515 | 0.015933 /0.024463 |
| V3 flush | -0.129524 / -0.135958 / +0.030993 / -0.054580 | 0.006434 /0.085573 |
| V4 aggregate | -0.131810 / -0.113762 / +0.083939 / +0.233661 | 0.018048 /0.149722 |

Each contrast averages its320-loop tax window. Sign changes and adaptive response prevent a uniform observer fee or guessed subtraction. Sparse750/1155 probes report settled flags and pipelined mode, but choices change in several windows; equal endpoints cannot establish a constant interior route or full object-state equality. Interpret each timing within its own observed conditional context. V3/V4 use changed source and instrumentation, so their timing difference is not an optimization gain. These results establish neither GPU utilisation nor apparatus-free60FPS.

## Next work

Narrow the Core-side route under stable selected geometry and actual adaptive flags, distinguishing existing wait seams from CPU preparation. Any optimization needs fresh source/native identity, functional and raster controls, and both-order physical timing with comparable instrumentation. Whole Renderer begin/end remain unmeasured by these pairs; retain their presentation/pacing ownership limits when selecting a further interval.

## Selected inner work: stripped packages

A fresh V5 fixture measures the actual `StaPipCore::renderStrippedPkgs` invocation inside its owning Core and Scene. Both physical orders completed 5400 loops against the same V5 ELF. All 768 records qualify 135 Core calls and 49794 summed bag vertices per sampled frame, with two actual Work calls per frame. The [inner-work machine record](tyrax2-inner-work-scopes-2026-10-04.json) binds raw logs, reversible UTF-8 copies, launch/evidence archives, source/native/ABI authority and independent reviews. Earlier V2/V3/V4 records remain unchanged.

| Own On window | Scene mean, ms | Work sum mean, ms | Same-frame Scene minus Work, ms |
| --- | --- | --- | --- |
| Off/On/Off, middle |17.572401|1.149322|16.423079|
| On/Off/On, first |17.580723|1.125855|16.454868|
| On/Off/On, last |17.648479|1.149048|16.499431|

These are inclusive physical COP0 Count intervals. Work covers the whole stripped-package function, including its dispatch, any strip-to-list expansion and inherited waits/preemption. It excludes preceding package creation and the Core tail after this call. The two Work intervals are disjoint and source guards check the same frame, Core ordinal and active Scene on entry and exit. Scene minus Work includes other Core routes, remaining Scene work and observer seams; it is not pure EE time or GPU utilisation. The geometry sum is a shape check, not upload/copy volume.

Raw high 15 record bits carry actual Work-call counts. Read accounting remains dynamic: each timed sample uses 4+2 WorkCalls diagnostic reads; each On phase uses 518+2 sumWorkCalls; Off uses 6. The observed two calls mean 8 reads per timed sample and 1030 per On phase. MINPHASE/MINWORK reports emit at 1155, after the tax/sample windows. The common 8100-byte arrays,116 bytes of aggregate/Work global symbols, resets, depth/context checks and layout costs remain unpriced; symbol sizes do not describe padding or the whole runtime footprint.

Four On-minus-Off non-pacing contrasts are +0.017620 / -0.073908 / +0.017884 / +0.038031 ms per 320-loop tax-window average. Outer spreads are 0.091528 /0.020147 ms. Interleave choices at 750/1155 are 1→1,0→1,1→0 in the first boot and 1→0,0→0,0→1 in reverse. Settled sparse flags cannot establish constant interior routes or equal full object state. Sign-changing contrasts do not justify a uniform observer fee. Same-ELF control applies within this pair; differences against V3/V4 or a later source version are not optimization gains or apparatus-free 60 FPS acceptance.

## Selected inner work: list packages

V6 relocates the single Work bracket to `StaPipCore::renderPkgs`. A new source manifest, native ELF and matching assets bind both completed 5400-loop physical orders; the scope headers and dynamic guard/clock mechanics are byte-identical to V5. The private helper is reached by the non-stripped partial-bag route. Its Work interval includes list-package iteration, nested subpackage creation/copy/clip, dispatch and inherited waits; it excludes preceding `packager.create` and the Core tail after the helper returns.

| Own On window | Scene mean, ms | List Work sum mean, ms | Same-frame Scene minus Work, ms |
| --- | --- | --- | --- |
| Off/On/Off, middle |17.483474|0.830526|16.652948|
| On/Off/On, first |17.571061|0.827182|16.743878|
| On/Off/On, last |17.512576|0.826811|16.685765|

All 768 records qualify the 135 Core/49794 vertex shape and five actual list Work calls per sampled frame. Dynamic accounting gives 14 reads per timed sample,1536 scope+262 cadence=1798 per On phase, versus 6 Off. These values come from actual raw counts, not a fixed-call assumption. Headers preserve the unpriced common arrays/state and observer mechanics described above.

V6 On-minus-Off contrasts are -0.066420 / -0.110912 / +0.011904 / -0.016563 ms per tax-loop average; outer spreads 0.044492 /0.028467 ms. Both boots show interleave choices 0→0,0→1,0→0 at 750/1155. Same conditional-state limits apply: remaining Scene includes other routes and observer seams/waits, and neither Work nor the residual is pure EE or GPU utilisation.

V5 and V6 bracket different functions in separate boots. Their Work or residual differences are not optimization gains, and their sums do not reconstruct a frame decomposition. Same-ELF controls apply only within each pair. V6's two emulator orders were separately qualified with normal saved night vehicle-entry images; V5 has one completed emulator order in this record; this is functional/shape evidence, not a physical observer price or fresh PS2 raster parity.

## Selected inner work: the Core dispatch tail

V7 moves the sole Work local into `StaPipCore::render`, immediately before `attribReplayStart`. It covers baked/retained lookup and replay, direct and partial dispatch, package creation and nested package helpers, final buffer flush/cache finalization, and the inherited telemetry tail. Head/bounds/preparation before that point are excluded. Two early returns precede construction; no return follows it. The later Work local is destroyed before the Core owner. Queued execution after return is outside this source bracket.

| Own On window | Scene mean, ms | Dispatch tail sum mean, ms | Same-frame Scene minus tail, ms |
| --- | --- | --- | --- |
| Off/On/Off, middle | 17.971999 | 7.328954 | 10.643045 |
| On/Off/On, first | 17.861910 | 7.166582 | 10.695328 |
| On/Off/On, last | 17.873337 | 7.175225 | 10.698112 |

Both new-ELF physical orders complete 5400 loops. All 768 records retain 135 Core calls / 49794 vertices, but actual Work coverage is 134 tail entries per sampled frame. It must not be silently relabeled as all 135 Core entries. The earlier-returned Core is not attributed to a specific bag by this record. Dynamic accounting yields 272 diagnostic reads per timed sample, 34560 scope plus 262 cadence reads = 34822 per On phase, versus 6 Off. The unchanged common arrays, state, guards and source/layout costs remain unpriced.

V7's four net On-minus-Off contrasts are -0.094927 / -0.041797 / -0.106695 / -0.138852 ms per tax-loop average; outer spreads are 0.053130 / 0.032157 ms. All four are negative. The original pair proof retains a copied V6 sentence about sign changes; the consolidated V7 review explicitly supersedes that wording while preserving the evidence. Negative net contrasts and drift identify neither a fixed removable observer tax nor a renderer gain. Sparse interleave choices are 0→0, 0→0, 0→0 in the first boot and 0→0, 0→0, 1→0 in reverse. Full interior-route/state equality is not established.

The measured 10.643–10.698 ms Scene-minus-tail interval contains earlier head/preparation, other Scene work, observer seams, waits and preemption. It is not an isolated CPU budget or pure EE/GPU time. V5 strip Work, V6 list Work and V7 dispatch tail use separate boots and source identities: do not add/subtract them into a decomposition or call their differences optimization gains. Same-ELF control applies only within each pair. No apparatus-free 60 FPS acceptance follows.

## Next work after the inner scopes

Bracket the remaining preparation/head paths and wait seams under stable selected geometry and actual adaptive flags. Preserve explicit caller/ownership and dynamic read coverage, with fresh frozen source/native/runtime authority for every changed apparatus. Before selecting an offload kernel or optimization, qualify functional/raster behavior and compare both physical orders with equivalent instrumentation. Keep the inclusive residual separate from a claimed removable cost.

## Core prefix and Core-owned submission

The [new dated record](tyrax2-prefix-submit-scopes-2026-10-04.json) binds separate V8 prefix and V9 Core-owned submit fixtures to completed physical and emulator pairs. Earlier machine records remain unchanged.

### V8 SceneWithCorePrefixWorkAggregate

| Own On window | Scene mean, ms | Work sum mean, ms | Same-frame Scene minus Work, ms |
| --- | ---: | ---: | ---: |
| Off/On/Off, middle | 17.991494 | 7.102130 | 10.889365 |
| On/Off/On, first | 18.172397 | 7.128648 | 11.043749 |
| On/Off/On, last | 18.174108 | 7.116340 | 11.057767 |

Core prefix after original Core owner through close before attribReplayStart, including both early-return paths; header eligible/close guards retained. Each pair has 768 records, 135 Core calls / 49,794 vertices and three own On windows. Dynamic counts and exact per-phase reads remain in the record. Nonpacing tax contrasts (ms per 320-loop window average): -0.014478 / +0.016366 / +0.097694 / +0.106677. Outer spreads: 0.030844 / 0.008983.

### V9 SceneWithCoreOwnedSubmitWorkAggregate

| Own On window | Scene mean, ms | Work sum mean, ms | Same-frame Scene minus Work, ms |
| --- | ---: | ---: | ---: |
| Off/On/Off, middle | 17.488532 | 3.486561 | 14.001971 |
| On/Off/On, first | 17.632633 | 3.561884 | 14.070749 |
| On/Off/On, last | 17.573719 | 3.574138 | 13.999581 |

Actual Vif1Queue::submit intervals entered only with activeCore; outsideCore submits are excluded, invalid owner/context or nested eligible submissions reject records. Raw Work counts are dynamic. Each pair has 768 records, 135 Core calls / 49,794 vertices and three own On windows. Dynamic counts and exact per-phase reads remain in the record. Nonpacing tax contrasts (ms per 320-loop window average): -0.035391 / -0.030185 / +0.227367 / +0.189781. Outer spreads: 0.005206 / 0.037586.

Both scopes include observer seams, waits and preemption. The common 12-byte records / 8,100-byte arrays and 116 bytes of actual aggregate symbols do not price common code/layout/reset/state cost or stack/padding/application footprint. Core-owned submit can occur during conditional program-set flushes in prefix preparation and during dispatch-tail flushes, so these intervals overlap in source. Uniform assembly through sendObjectData is inside prefix but does not itself imply submission. The V9 supplement covers three textual direct C/C++/header sites, including the inline adapter; its waitFor(sequence) after submit returns is outside Work. This is source reachability, not per-call runtime categorization or a complete semantic callgraph. Same-ELF control applies within each pair only: do not add prefix+submit or subtract V8/V9/earlier versions to isolate preparation, claim a gain or CPU/GPU decomposition. Sparse context endpoints do not prove interior route/full-state equality; no uniform observer fee or ordinary 60 FPS acceptance follows.

## V10 batch / texture / program interval

The [October 4 V10 follow-up](tyrax2-batch-texture-scopes-2026-10-04.md) moves the selected interval to immediately before texture attribution through immediately before light attribution. Both physical orders measure 0.751–0.788 ms inclusive, with 134 actual entries per sampled frame. Dispatch and existing wait seams remain inside the interval. Its own sampler contrasts and rejected attempts are preserved separately; do not add it to overlapping V8/V9 scopes or subtract across ELFs to attribute a gain.

The subsequent [Core prefix partition](tyrax2-core-prefix-partition.md) measures three disjoint pieces in the same ELF. Use its physical orders to select a narrower scope; do not subtract or add earlier ELF prefix/submit measurements.
