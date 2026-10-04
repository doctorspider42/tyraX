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

## Next measurement

Measure the whole Scene, Renderer begin/end, actual previous-frame completion and native recording flush separately. Completion, flush and end-frame intervals overlap and can include pacing; their sum or difference is not pure EE/GPU/cache cost. Keep existing presentation counters separate. Source and frozen-fixture changes require fresh native/runtime identity; do not transfer V2 timing to the new apparatus.
