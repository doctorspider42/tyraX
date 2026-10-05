# Core prefix partition on physical PS2

A private same-ELF trial partitions the static renderer's preparation prefix
into three disjoint elapsed scopes without changing scene rendering behavior.
It follows the small [night producer bill](tyrax2-night-producers.md) and
qualifies where to investigate the larger model preparation region.

## Boundaries and observer

| Stage | Included work | End boundary |
|---|---|---|
| 0 headBoundsPackager | Core entry, validation, fog, size/cache, transform key, bounds/planes, MVP and package preparation; includes empty/outside returns | Before attribTextureStart |
| 1 textureProgramLightFacts | Batch eligibility, texture/wrap, program selection, light sphere/pick, BLSS and fog facts | Before attribObjectDataStart |
| 2 objectDataRoute | Object uniforms, clipper MVP, info, trace scaffolding and route predicates | Before attribReplayStart |

Baked/retained lookup, geometry dispatch and later consumption are outside.
Emitting a FLUSHE into a packet is not itself an EE wait: do not attribute the
consumer barrier duration to sendObjectData without a synchronous call-site
witness. Barrier removal is not qualified by this trial.
Stages 3/4 in the inherited five-slot protocol are reserved and must remain zero.
No parent scope overlaps these brackets. Early returns close stage 0 naturally.
Existing waits and preemption inside a bracket remain included; these numbers
are neither pure EE arithmetic nor recoverable frame-time savings.

Kind14 toggles only Count brackets in Off/On/Off and On/Off/On orders. Each On
window prices 320 loops, offsets 800..1119; counters at 750/1155 are sparse
workload witnesses. Count reads for these scopes are accounted separately from
ordinary sampling. No GPU hold, extra fence or per-object host logging is added.
Clocks stop after loop 5400, including when the final On variant is left running.
Common disabled branches, storage, cold guards, code layout and stack footprint
are retained in both arms and unpriced. Whole-loop observer contrasts must not
be uniformly subtracted from individual scope times.

The qualified native fixture binds 501 source inputs, 298 assets, linked ABI
and sixteen unchanged resident VU images. Four postimages differ: three debug
headers and stapip_core.cpp. Production engine/generator files are unchanged.
ELF SHA-256: dc028309d44562488f82b5a2481cfbc27bd1fef8629fe839e0acdfc8cb21a63b.
Source SHA-256: cc5d182958f5f466ebd1969ece2c82828a92fb9bae32464794913b95b9b937e1.

## Results

Both physical orders complete 5400 loops, 384 ordinary samples and 15 chunks.

| Disjoint scope | Order 0 On (ms/loop) | Order 1 mean of two On windows |
|---|---:|---:|
| headBoundsPackager | 3.043186 | 3.058405 |
| textureProgramLightFacts | 1.568881 | 1.589968 |
| objectDataRoute | 2.229568 | 2.228969 |
| Sum | 6.841635 | 6.877342 |

Each On window records 43,200 entered stage-0 calls and 42,880 calls in
each later stage: 135/134/134 per loop, with 86,400/85,760/85,760 additional
Count reads. Across three On windows this is 773,280 additional scoped reads.
Reserved stages stay zero. Sparse witnesses show one outside-frustum return,
no empty return, 49,794 input vertices and 49,788 continuing vertices;
114 bags request light selection, zero use BLSS or bag.lighting.
Those sparse facts are not whole-window light outcomes or geometry totals.

Enabled observation increases the ordinary non-pacing loop mean by
0.110062/0.119633 ms in forward/reverse orders;
outer-arm spreads are 0.014985/0.006033 ms.
This resolves a repeatable positive net enabled tax for this fixture, including
induced scheduling/overlap; it is not a pure Count instruction cost or a
per-stage correction. Common disabled footprint remains unpriced.
All rendered periods remain approximately 33.3667 ms: no 60 Hz gain is claimed.

The largest measured region is head/bounds/package preparation. Next isolate
validation/size/transform lookup, bbox lookup and plane/frustum/MVP/package
preparation in that region. Object-data routing is the next large region;
separate packet writes/local preparation from actual synchronous waits before
proposing offload. Texture/program/light facts must likewise be split before
attributing their elapsed time to dynamic-light arithmetic.

## Evidence and qualification limits

[Archived evidence](tyrax2-core-prefix-partition-2026-10-05/) retains both
physical orders, raw logs, strict reports, source postimages, native provenance,
ABI checks, release authority and host/physical parser controls. Binary artifacts
are represented by hashes. An initial ping-responsive launch produced no output;
its owned client was stopped and a network reset preceded the accepted retry.
That attempt is retained separately as excluded-no-start, never a completed run.
No emulator result or new human visual confirmation is claimed by this trial.
It preserves rendering paths, but scope observation alone does not qualify
raster correctness or a production optimization. The earlier prefix/submit ELF
numbers must not be subtracted from, or added to, this disjoint partition.

## Five-part bounds follow-up (kind15)

A separate native ELF partitions the first region into five disjoint scopes.
Both physical orders complete 5400 loops, with a physical restart required after
an excluded freepad DMA Busy boot. Stage-0 empty returns and stage-2 outside
returns preserve the original route. Additional clocks stop after loop5400.

| Scope | Forward On ms/loop | Reverse two-On mean ms/loop |
|---|---:|---:|
| Head, size derivation and transform key | 0.997331 | 0.994844 |
| Bbox lookup and size propagation | 0.842189 | 0.832024 |
| Frustum planes and main-box cull | 0.617173 | 0.616187 |
| MVP and transform cache writes | 0.251277 | 0.250232 |
| Clip planes and package setup | 0.658575 | 0.657439 |

Each On window has 43,200 timed calls in stages0/1/2 and 42,880 in stages3/4,
with two additional Count reads per call. Sparse flags include 131 precisely
culled bags, one outside return, 63 bags entering package classification and
108/110 reusable transform keys in witnesses. These are cold snapshots.
Net enabled observer tax is +0.144231/+0.184171 ms; outer spreads
0.013513/0.010208 ms. Common disabled footprint is unpriced. Do not subtract
this ELF's times from kind14: five brackets change layout and stack footprint.
The result motivates separately qualified exact matrix-key and clip-plane
specialization candidates, not blanket removal of culling or assertions.

[Bounds source and evidence](tyrax2-core-bounds-2026-10-05/) includes both
completed orders, the excluded busy boot, native/source/ABI proof and 14
synthetic parser rejections. No new human image or emulator qualification is
claimed for the kind15 observer itself. ELF SHA-256:
b60abfddb43244c49217c57db9703bd438d254c555be5fc6bb34e79c680980bd.
