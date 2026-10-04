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
