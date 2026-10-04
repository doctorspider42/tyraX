# TyraX2 EE preparation and VU0 audit

## Debugger execution map, 2026-10-04

Automated PCSX2 control now works through a separate WSL/Xvfb session. Independent raw-state audits close all135 core calls in one detailed frame by actual SP/RA. Core intervals occupy1722875of2129751 observed Scene cycles; wheels retain113259cycles outside core, whereas terrain and roads retain6541/22375. These are inclusive, heavily observed emulator intervals. SaveState drains VU/GS and changes later waits; no physical conversion or subtraction across the broader/detailed protocols is valid. The [acquisition and scope report](tyrax2-pcsx2-debugger.md) explains controls, tail calls and the minimal paired physical follow-up.

## Copied Bag producer partition, 2026-10-04

The private V5 census completes both PS2 observer orders and both emulator orders: 384 records, 768 frozen Scene loops and three exact RGB/converted-alpha rasters per boot. Its twelve origin buckets partition the existing 129,360 copied Bag bytes per frame: UnknownBag86,448, LightReceiverBatch34,272, TerrainRoadBatch2,304, BeamCone1,728, Corona2,304 and ParticleFX2,304; the other six buckets are zero. UnknownBag means producer coverage remains incomplete. It is distinct from the storage category UnknownOther, which remains2,640 bytes. Pool remains39,840 bytes, and the original copied total remains171,840 bytes across421 REFs.

All26 original V4 counters match V5 in all24 warm/sample windows. Labels describe successful copied snapshot publication; they grant no immutable lifetime or borrowing permission and zero tagged volume does not prove a producer absent. A follow-up source audit finds active untagged wheel/debris and material-pass constructors, but source presence does not attribute the86,448 bytes to them.

The observer adds1.525626–1.532746ms of inclusive nonpacing time in this serialized fixture, against0.000899/0.001596ms outer spreads. Stage wall means remain33.32655–33.32797ms. These are this observer's paired results, not an optimization or an ordinary production FPS result. Common descriptor initialization, tagging, resets and compiled-on layout remain unpriced. Actual EE layouts are Bag48bytes/Qbuffer76bytes/Metrics616bytes/Scope248bytes, versus compiled-out Bag44/Qbuffer44; class counts and full application footprint are unknown. The [machine record](tyrax2-producer-origin-census-2026-10-04.json) pins the independent four-archive/pair proof and preserves all earlier machine-record hashes.

The copied-byte partition is complete at its declared coverage. Next rank actual execution with debugger scopes, then validate selected intervals and observer cost on PS2; another broader producer census is not the current next step.

## Selected static-base copied-REF census, 2026-10-04

All seven whitelisted base parts (objects 21, 23, 25, 27, 29, 41 and 43) were admitted, with zero object or part skip reasons. Their 21 position/color/ST ranges represented 22,176 used bytes. Every physical On sample nevertheless published zero copied SelectedStaticBase REF bytes. BagDeclared remained 129,360 B/frame across 229 REFs; Pool contributed 39,840 B and UnknownOther 2,640 B, totaling 171,840 B across 421 copied REFs. This result applies to those seven admitted base allocations in the fixed view. It neither establishes zero static rendering nor identifies why those allocations did not reach the mutable-copy observer.

Same-ELF nonpacing On-minus-Off contrasts were +1.043604533/+1.037487295 ms for Off/On/Off and +0.982720269/+0.977878835 ms for the reverse order. Outer same-state spreads were 0.006117238/0.004841434 ms. These inclusive serialized completion/wait/control measurements price this observer; the roughly 0.06 ms difference between boot contrasts also prevents treating it as a universal constant tax. Common 1,536 owner guard checks and seven authored SceneObjectData snapshots run outside the Count spans, so their absolute application cost remains unknown. No cross-ELF timing subtraction, pure EE/GPU cost or production gain is claimed.

Both PS2 orders and both emulator orders completed 384 sample records with exact three-image RGB/converted-working-alpha parity within each boot. The independent pair audit verifies all 768 physical samples and all four source-launch attestations. Native exit 0, 495 inventoried inputs, 486 actual used native-mirror inputs and nine ancillary metadata files are separately pinned. The initial order-1 launch was explicitly rejected before freeze because root supplied the wrong configuration filename; its log is preserved and contributes no accepted samples. A fresh retry used the same frozen source/ELF and is archived separately.

Actual EE metadata proof records 20,512 B of root tables, 256 B Metrics, 72 B SnapshotScope, 5,120 B atlas, 25,600 B part guards, 112 B object guards and 2,352 B authored snapshots. These are metadata layouts, not net Qbuffer-class ABI or whole-application footprint. Host/source/native/runtime qualifications and the preserved V1a/V2/V3 record hashes are linked in the [machine record](tyrax2-static-owner-census-2026-10-04.json).

No production optimization or immutable lease was introduced. Raw unstamped writes and exact pointer/size/stamp ABA remain outside the private frozen-owner guard contract. The subsequent producer partition above completes that next step, preserving explicit Unknown and Mixed instead of estimating savings from static allocation inventory. This result supports neither 60 FPS nor a universal static-owner conclusion.


## Road-owner copied-REF census and observer pricing, 2026-10-03

The fixed physical night Scene0 workload published zero Road-class mutable REF bytes in every On sample (three On stages, 128 samples each). It still published 129,360 B/frame as BagDeclared, 39,840 B as Pool and 2,640 B as UnknownOther: 171,840 B across 421 copied REFs. The recorded ownership inventory was 66 road chunks, 198 ranges and 1,288,080 used bytes. That static inventory must not be substituted for copied volume. This negative result does not establish that roads were absent from rendering or that every Bag owner has been identified.

The private observer's own same-ELF nonpacing On-minus-Off contrasts were +1.074263944/+1.075823201 ms (ABA) and +1.072995795/+1.076002889 ms (BAB). Outer same-state spreads were 0.001559258/0.003007094 ms. These inclusive serialized measurements include completion/waits and observer/control effects; they are not pure EE/GPU costs, production savings or evidence of 60 FPS. No cross-ELF timing subtraction against V1a or V2 was performed. The common 1,536 owner guard checks per boot run outside the Count spans; their absolute application cost remains unknown.

All four archives independently validate the actual runtime protocol and exact three-image RGB/converted-alpha raster parity. Native exit0, 495 inventoried inputs and the actual486 used native mirror inputs plus9 ancillary metadata files are separately bound by root attestations. The final independent physical pair audit passed on all768 raw physical samples and all four source-launch attestations, binding the actual summary SHA256. Its initial ancillary tool attempt failed with KeyError (wrong metrics object); the original tool and reproduced failure are retained. This was an ancillary analysis-tool failure, not a game failure.

Actual EE metadata proof records four roots20,512 B, Metrics256 B, SnapshotScope72 B, atlas ranges5,120 B and owner guard records23,040 B. This is metadata layout evidence, not net Qbuffer-class ABI or full application footprint. Actual-header host parity ran2,234 cases/44,051 assertions at both O0 and O2; modeled Chunk/array guard controls ran49 assertions. Host/parser scope and raw unstamped-write/pointer-ABA limitations remain explicit in the pinned evidence.

V1a and V2 public machine records are preserved with their current SHA256s in the [machine record](tyrax2-road-owner-census-2026-10-03.json). The Road census is private instrumentation only: no production code was integrated and no owner lifetime/borrowing optimization was accepted. Future append-only storage requires a separate safe frame-bank reuse fence, must not reset on generic queue drain, and must avoid original delete[] ownership conflicts.

Next measure actual copied ranges from an explicit static GeoPart whitelist.
Do not implement original-road borrowing on the strength of atlas capacity: this
view has no copied-original-road opportunity. Derived road clipping in Pool and
existing baked-road traffic were not attributed by this census.


## Retained-command ownership coverage and observer pricing, 2026-10-03

This private experiment records independently qualified same-ELF physical Off/On/Off and reverse orders, plus two emulator orders. It is a private observer experiment. The native build exited 0; the selected ELF is `4d1646134dd773f97666596e0f94a8a5b047769e67ad9047aaf18f1b8fb356d4`. The independent physical-pair audit verified both raw measurement sets and all four source/launch attestations.

V2 adds exactly one ownership observation before appending a retained-HIT cached command block. The other 492 source/control inventory entries are byte-exact qualified V1a. Packet construction, actual REF resolution, Memo16, callbacks and the closed/drained stage-boundary root reset are unchanged. The actual inventory has 493 entries: 484 used native-mirror inputs plus 9 explicit ancillary metadata entries. Startup order config is archived separately.

Each boot completed 768 frozen Scene loops and 384 sampled records, with 128 warm and 128 sampled loops per stage. Real beginFrame/renderScene/endFrame/synchronizeFrame, two common Count reads and actual pacing-delta accounting remain in every arm. Three offclock framebuffer exports per boot matched exactly in RGB and converted working alpha; camera/light/object guards were stable. All three physical On stages yielded identical sample counts:

| Storage match | Mutable REFs/frame | Qwords/frame | Copied bytes/frame | Copied-byte share |
|---|---:|---:|---:|---:|
| UnknownOther |11|165|2640|1.5363%|
| Pool |181|2490|39840|23.1844%|
| BagDeclared |229|8085|129360|75.2793%|
| Total |421|10740|171840|100%|

Pool and Bag together match 410 of 421 mutable REF operations(97.3872%) and 169200 of 171840 copied bytes(98.4637%). This is storage classification, not immutable borrowing permission. Each frame also published 106 successful arena snapshots totaling 223328 snapshot bytes and 2151152 external borrowed bytes, with 410 ownership notes. Failed copies/snapshots, foreign/stale notes and invalid/overflow/conflict/lifecycle counters were zero in the physical sample blocks. Snapshot bytes already include copied payload plus root/CNT/alignment; borrowed bytes are external. These totals must not be added together or described as display completion counts.

The count-only comparison with the accepted V1a record shows 83 REF operations and 99408 bytes/frame moving from UnknownOther to BagDeclared. Pool volume, total 421 REFs/171840 mutable bytes, snapshot count/bytes and borrowed bytes are unchanged. This demonstrates the retained-HIT observation gap in this workload; it does not identify every generated array or prove the remaining 2640 unknown bytes belong to clipping. No timing was subtracted across the V1a/V2 ELFs.

Within V2's own physical orders, On increased mean nonpacing inclusive time by 0.944503/0.950303ms against Off in the first boot and 0.948154/0.949142ms in reverse order. Same-state outer spreads were 0.005800ms and 0.000988ms. Mean wall intervals remained near 33.327ms as pacing changed. These are aggregate observer costs under serialized real completion, not pure EE/GPU work, an optimization gain or a constant tax to subtract elsewhere. No 60 FPS or production-performance acceptance follows.

Target metadata remains four root tables totaling 16416 bytes, Metrics 208 bytes, SnapshotScope 56 bytes and 24 additional qbuffer fields bytes; those fields do not establish the full class's net ABI growth. All source/native/asset/ELF, raw archive, attestation, strict parser and independent review pointers are hashed in the [machine record](tyrax2-mutable-ref-retained-census-2026-10-03.json). The previous accepted public V1a record is referenced by its actual repository hash and remains unchanged.

A separate owner audit identifies two plausible bounded directions: sealed generated road-chunk arrays, or an explicit whitelist of nondeforming generated static-model GeoPart allocations. Their contribution to the 129360-byte Bag category is not measured. Registration must cover actual emitted ranges and preserve copied fallback; old allocations must survive pending readers through rebuild/LOD/scene teardown. Public MeshMaterialFrame pointers and shallow aliases permit writes, while VU0 skinning mutates position/normal outputs in place. Therefore neither every Bag nor every static-looking mesh can become borrowed memory by default. Content stamps are cache keys, not reader locks. Any separate append-only owner bank must avoid reuse on generic drain and adapt existing delete[]/dynamic-detach behavior explicitly.

The original V1 exit 2 missing-header failure and V1a's exact two-include repair remain historical evidence. V2 qualified runtime coverage is a new result for its own source and does not retroactively replace the earlier measurements. The renderer/default remain production decisions separate from this private census.



## Mutable REF ownership census and observer pricing, 2026-10-03

A private same-ELF fixed-night Scene census completed both stage orders on
PS2 and PCSX2: 384 samples and three exact RGB/converted-alpha rasters per boot.
Two common Count reads bracket normal rendering and real synchronization.
All three physical On sample windows report 421 copied mutable REFs and 171840
bytes per frame: UnknownOther 102048 (59.38%), Pool 39840 (23.18%), and declared
Bag storage 29952 (17.43%). There are 106 successful arena snapshots per frame,
223328 whole snapshot bytes and 2151152 separately counted borrowed external
bytes. These totals overlap; do not add them. Publication is arena acceptance,
not a DMA or display count. No sampled snapshot/copy or coverage error occurred.

On increases inclusive nonpacing by 0.841011–0.846886 ms in all four physical
comparisons, versus outer-control spreads 0.002011/0.003017 ms. Wall intervals
stay near 33.327 ms as pacing contracts. This is measured observer impact in
this serialized scene, with compiled-on Off scaffolding and existing waits;
it is neither pure EE/GPU cost nor a constant correction for another ELF.

The four-root metadata reset follows actual producer closure, synchronizeFrame
and drain; aggregate takes retain generations. Exact-start emission notes
classify actual copied ranges. Retained-command hits bypass the current note
hook, explaining a source coverage gap without proving that every Unknown REF
has one owner. Pool volume is a lower bound, not safe borrowing permission.
At this historical V1a stage, retained replay still needed notes and a new
count/observer qualification. The V2 result above closes that coverage seam.
Owner-attributed volume remains necessary before choosing an owner-bank experiment. Such a bank
must survive midframe drains and reuse storage only at a proven frame-bank
completion fence; qbuffer detachment must preserve allocator ownership.

The first private build failed because two generated files included a renamed
header; the corrected fixture changes only those includes. Its source inventory,
actual used build mirror, ELF/symbol pair, assets, configuration, strict protocol
and raw pair arithmetic are independently checked. The [machine record](tyrax2-mutable-ref-census-2026-10-03.json)
retains hashes, rejected history and limitations. No production renderer change
or 60 FPS gain is accepted here.

## Native sizing reuse: paired physical candidate controls (2026-10-03)

A private candidate reuses the DMA-tag count from a successful snapshot preflight
instead of walking that copied chain again to size native records. The old public
Snapshot ABI and uncounted compatibility path remain unchanged. Only eligible
native copies collect the count; failed attempts cannot publish it, retries use
the final successful copy, and existing capacity, emission and fence paths remain.

One native ELF completed 5,400 loops per physical boot in both Off/On/Off and
On/Off/On orders. Each boot has 384 accepted samples, with matching phase-relative
state, raw camera, resources and native workload. Independent source, symbol,
asset and sample-ledger checks agree. Each sampled frame records 106 submissions;
the candidate replaces all 106 sizing scans with counts from those snapshots.

| Order | Work means, ms | Candidate versus outer arms, ms | Outer-arm spread, ms |
| --- | --- | --- | --- |
| Off/On/Off | 18.166299 / 18.017179 / 18.175260 | -0.149119 / -0.158080 | 0.008961 |
| On/Off/On | 18.014179 / 18.164677 / 18.018049 | -0.150498 / -0.146628 | 0.003869 |

This is a repeatable net improvement in this controlled binary, including count
collection and its return ABI. It is not the pure cost of the removed scan.
Observed periods remain approximately 33.367 ms, so neither boot reaches 60 FPS.
The common diagnostic buffer, counters, Scene observer and FrameProfile level 2
remain present in every arm; uninstrumented production timing is still required.
The period field's job owner is unknown and it must not be attributed to current
EE, VU or GS work. A full emulator run also passes 5,400 loops; its timings and
different source workload are not physical performance evidence.

Actual-header host controls cover snapshot rollback, callback behavior, native
capacity/output parity and eligibility/retry decisions. Production integration,
moving-camera acceptance and final quiet controls remain separate checks. The
[paired record](tyrax2-native-sizing-reuse-2026-10-03.json) retains exact hashes,
counts, periods, controls and limitations. No production engine change is part
of this measurement milestone.

This audit separates current submission costs from consumer completion and identifies which remaining EE work could benefit from fewer passes or VU0 arithmetic.

## Physical night diagnostic, 2026-10-02

One private instrumented build used the corrected 1.169.1 stationary night vehicle fixture. Existing wait, cache, snapshot and conversion brackets were timed with COP0. A read-only FINISH observer sampled every eighth submission and before the existing completion fence; it introduced no new wait, bit clear, interrupt or GPU hold. Native build and PCSX2 reached frame 1700 before a fresh physical PS2 boot.

Ten neighboring 50-frame summaries (frames 1100 through 1550) each contain 50 completion samples and zero missed samples:

| Bracket | Mean ms/frame |
| --- | ---: |
| Existing VIF completion wait | 0.001234 |
| Existing FINISH wait | 0.001583 |
| Existing queue cache flush | 0.008730 |
| Source-chain snapshot | 2.723882 |
| Native-chain conversion | 0.742786 |
| Entire VIF1 submission | 3.665426 |
| Combined consumer completion observation, upper bound | 12.779332 |

Snapshot and conversion are **inside** submission; do not add them to its total. The exact 512 raw renderer-work samples, frames 1100–1611, average 18.305820 ms (p95 18.981954 ms), with all samples above 16.667 ms. Renderer work excludes presentation pacing; this diagnostic is not a new ordinary performance benchmark or a delivered-FPS result.

Nearly zero explicit waits point toward EE preparation on the observed critical path. They do not establish idle VU1/GS: the consumer runs concurrently, and the 12.779332 ms first-observation upper bound covers DMA/VIF/VU1/GIF/GS together. It is neither VU-only time nor utilization. Current cache timing also means the historical approximately 1.09 ms flush-removal estimate cannot be reused for this runtime and pose.

The [machine record](tyrax2-pipeline-attribution-2026-10-02.json) preserves exact windows, source/ELF/frozen-log hashes, preparation/analysis scripts, build/emulator evidence hashes and the full read-only VU0 audit. The owned ps2client remains serving the healthy night game; freezing evidence does not stop host-file service.

## VU0 already has owners

The local engine uses VU0 macro instructions for Vec4 arithmetic, matrix/camera transforms, plane helpers, some bounding-box reductions and skeletal skinning. Static source use does not yield a utilization percentage, but rules out treating VU0 as an unclaimed second CPU. This fixture has no authored custom VU0 kernels or mirrors; those optional micro-mode entry paths are absent, while macro use remains.

Skinning retains vector registers across assembly blocks and explicitly forbids helper calls between them. Existing raytracer and generated compute drivers execute synchronously. Their instance-local uploaded flags are not a shared micro-image ownership token: a new dispatcher must reload the correct program when switching owners and prohibit overlapping macro use. Asynchronous micro work therefore needs an explicit register/program/data/channel ownership contract before it can be considered safe.

## Ranked follow-up

1. Split the 2.724 ms snapshot into preflight, chain copy, mutable REF copy and fixups; split native conversion into decoding and writer stores. Count bytes, tags and resolver queries before choosing SPR or direct producer recording. The aligned copy already uses EE 128-bit LQ/SQ; pointer, tag and lease work is not vector floating-point arithmetic.
2. Price the narrow duplicate lookup in ImmutableSpanTable::borrow. A successful borrow currently searches the same address twice; two snapshot passes imply four searches per successful immutable REF. Removing one search per borrow preserves both validation passes and leases. Fewer searches are source evidence; hardware frame gain is still unmeasured.
3. Measure exclusive scalar object-space plane transforms, AABB planes visited and dynamic-light selection, including cache misses and candidate counts. Existing Vec4/matrix helpers already use macro mode; avoid moving work that is already there or skipped by caches.
4. If a math block is material, first compare a synchronous macro implementation with exact boundary/classification controls. Only then price micro-mode identity transfer/setup and a shared owner dispatcher. Include packing, launch, waits and readback in its cost.

The retained beam-state shortcut remains a separate private candidate. Preserve moving cameras/lights, visibility, levels and portal views and compare outputs before quiet physical timing. No VU0 rewrite, production night optimization or additional FPS gain is accepted by this audit.

## One-search borrowing trial, physical PS2

A narrow private variant removes the second lowerBound inside a successful ImmutableSpanTable::borrow. Public contains, both snapshot passes, lookup selection, retirement and reader-bank mutation remain unchanged. A differential oracle extracted the actual baseline and candidate classes: 55,163 operations / 38,018,254 assertions passed, including 50,000 randomized operations and range/mask/full-table/two-bank boundaries. A host count control gives 2,000 versus 1,000 searches for 1,000 successful borrows; this is not timing evidence.

The integrated candidate passed the existing 1,074 arena cases plus 10,000 malformed streams and 43 writer cases. Native build and PCSX2 2.9.93 passed all three phases, two scene reloads and 6,600 gameplay frames. A fresh physical boot ran baseline/candidate/baseline in the same ELF, with a shared runtime branch and no new profiling scopes. Each 2,200-frame phase uses 1,100 warmup frames and an exact 512-frame raw work window; neighboring period summaries remain separate.

| Variant | Mean renderer work (ms) | Delivered rate from period (Hz) |
| --- | ---: | ---: |
| Baseline before | 18.468173 | 29.94 |
| One-search borrowing | 18.435447 | 29.94 |
| Baseline restored | 18.556225 | 29.94 |

Apparent savings are 0.032727–0.120778 ms, while the controls themselves differ by 0.088052 ms. One sequential boot does not distinguish a stable small gain from control variation; no production integration or FPS improvement is accepted. The candidate remains private. Prioritize the bounded snapshot/native breakdown rather than calling this enough to restore the night frame budget. The [trial record](tyrax2-immutable-borrow-2026-10-02.json) preserves exact windows, patch, tests, preparation/analysis scripts and hashes. Its frozen log does not stop the owned client serving the restored baseline.

## Bounded snapshot/native breakdown, physical PS2

A private three-phase same-ELF diagnostic toggles aggregate COP0 clocks/counters off/on/off. Both host macro variants pass 10,047 actual-class callback/state differential cases, including partial writes on failure and unstable second-pass resolution. The actual fixture headers also pass the existing arena/writer harnesses; native compilation, PCSX2 and physical PS2 complete all 6,600 gameplay frames and both scene reloads. No new DMA wait, allocation, FINISH read/clear or GPU hold is introduced by the probe.

Warm exact 512-frame renderer-work windows give **19.164984 / 19.596451 / 19.344394 ms**, off/on/off. Enabling the observer changes work by **+0.252057 to +0.431467 ms** against these controls, whose spread is 0.179410 ms. These are one-boot ranges, not confidence intervals. Runtime-disabled probes may still have compiled scaffolding overhead; pristine and compiled-out physical controls remain separate gates. This instrumented result is not a new production benchmark or accepted optimization.

Ten neighboring 50-frame enabled summaries give the following diagnostic brackets:

| Bracket | Mean ms/frame |
| --- | ---: |
| Entire snapshot | 3.312289 |
| Preflight tag/range/callback checks | 1.104756 |
| Full source-chain copy | 0.265598 |
| Second-pass fixups, excluding mutable copy | 0.776857 |
| Mutable REF copying | 1.056222 |
| Native sizing pass | 0.158458 |
| Native decode / writer emission | 0.515674 |

Entire snapshot includes preflight, chain copy, second-pass fixups and mutable copying. Fixups above subtract the nested mutable-copy child; never add the inclusive second pass and its child. Native sizing and emission are sibling adapter brackets, still inside submission. Timings include counter/clock overhead and are not a pure instruction or bandwidth measurement.

Per warm frame, the observed source path visits 1606.33 tags in each pass, calls the readable resolver 964.20 times and the immutable resolver 1086.19 times. It copies 56.16 KiB of original chains and 167.93 KiB of mutable REF payload, while borrowing 2100.61 KiB. Published copied qwords equal full-chain plus mutable-copy qwords; borrowed bytes are excluded rather than subtracted a second time. Enabled windows have matching preflight/second-pass tags and successful snapshot attempts; native sizing and emission visit equal tag counts. Disabled windows retain zero metrics.

The [machine record](tyrax2-snapshot-breakdown-2026-10-02.json) preserves exact raw/profile/count/period windows, observer overhead, source/ELF/frozen-log hashes, preparation/analysis scripts and probe/test evidence. No production behavior changed. Further source changes need quiet physical controls and existing rollback, capacity, callback and bank-lease gates.

### What this changes next

The measured preflight plus fixups total about 1.882 ms; mutable copying adds 1.056 ms, versus only 0.266 ms for full-chain copying. These are instrumented costs, not additive gain estimates. Runtime-disabled work also differs from earlier pristine ELF measurements; cross-boot/layout differences cannot all be assigned to the runtime gate. Price pristine/compiled-out controls before deriving a production saving from these brackets.

The source audit identifies two additional scalar-math seams to census: a partial guard-band bag can transform the same eight clip planes twice in one render call; local spot preparation performs affine inverse and normalization inside sendObjectData, beyond the existing prepLight bracket. Measure activation/cache misses and exclusive cost before reusing results or writing a VU0 kernel. Existing object-cost export changes batching with per-object fences, and some TYRA_STAPIP_ATTRIB counters ignore the runtime telemetry gate, so those modes cannot silently stand in for an ordinary pipeline control. The full read-only census plan and source hashes are retained in the machine record.

## Exclusive EE math census, physical PS2

A private ordinary-batching probe completed 6,600 gameplay frames and two scene reloads in both PCSX2 2.9.93 and a fresh physical boot. The same ELF runs compiled-on/runtime-gate off, census enabled, then off again. Seven scopes preserve original arithmetic, early returns and rendering order; no new DMA wait or GPU hold is introduced. Probe host API/compile-out controls and source-insertion removal checks passed.

| Scoped call path | Diagnostic ms/frame | Calls/frame |
| --- | ---: | ---: |
| Engine six-plane transform, cache miss | 0.109074 | 25.844 |
| Generated coarse six-plane transform | 0.048262 | 13 |
| Eight clip-plane transform | 0.377498 | 74 |
| World bounding sphere | 0.131915 | 113 |
| Dynamic-light selection | 0.456520 | 114 |
| Local spot preparation | 0.229736 | 134 |
| Zero-influence check | 0.133415 | 71 |

These seven mutually exclusive scoped paths total 1.486420 ms in the enabled diagnostic; they are not a complete EE census or a potential saving. Helpers already include VU0 macro arithmetic. Local preparation and influence are children of sendObjectData, outside the old prepLight bracket. Do not add children to their existing parent totals.

Warm exact 512-frame work windows average 18.231584 / 18.250115 / 18.318051 ms; all three period windows remain 33.403 ms (29.94 Hz). Enabled work is +0.018531 / -0.067937 ms against the controls, whose spread is 0.086467 ms. This run does not resolve observer overhead or a speed gain. Gate-off controls retain compiled scaffold; pristine/compiled-out hardware controls remain separate.

At the transform-key seam, 135 calls/frame split into 108.156 reuse hits and 26.844 misses. Plane-cache counts are 105.156 hits / 25.844 misses. Guard attempts number 63, with 52 promotions and 11 repeated eight-plane transforms after a failed promotion. Light selection examines 912 actual candidates/frame across 114 calls. Local spot preparation has 71 enabled and 63 disabled calls. Disabled preparation is not proof that the preceding selection was unnecessary; bags can have different pick flags.

Prioritize exact per-render reuse of already calculated clip planes, with no cross-frame cache, and audit light-selection skips against tie ordering, zero scores, disabled slots, runtime changes and portal views. Compare actual-source boundary/classification/output controls before native, emulator and quiet physical candidate timing. No VU0 micro kernel or production math optimization is accepted. The [machine record](tyrax2-ee-math-2026-10-02.json) retains exact windows, counters, source/ELF/frozen-log hashes and probe/build/emulator evidence; the earlier paused emulator attempt is excluded from acceptance.

### Same-call clip-plane candidate, physical regression observed

The private candidate retains the planes already computed by the whole-bag guard and suppresses only the identical second transform after failed promotion. Its bool lives within one render call; subsequent frames/materials/views recompute as before. Actual-source host differential controls pass 240,328 calls with guard on/off, boundary-adjacent values, changing MVP/settings and nonfinite/signed-zero inputs; planes, masks, classification and packager updates match exactly.

Native compilation and a same-ELF off/on/off PCSX2 trial pass 6,600 frames and two same-scene reloads. A separate always-enabled emulator input fixture confirms vehicle movement/reverse, cameras 0/1/2 and main/dense/procedural/main transitions. Final captures were visually inspected, without a pixel differential claim. The quiet physical ELF and its original source are restored separately from the scripted motion ELF. The 0.377 ms census bucket covers all 74 transforms, not just the 11 duplicates. No production integration or speed gain is accepted. The [candidate record](tyrax2-clip-reuse-2026-10-02.json) preserves these distinct layers and hashes.

The fresh physical same-ELF control/candidate/control trial completes all 6,600 frames. Exact warm 512-frame work means are 18.247767 /19.137065 /18.411139 ms; all delivered-period windows remain 33.403 ms (29.94 Hz). The candidate is 0.889298 /0.725926 ms slower than the controls, whose spread is 0.163373 ms. This falsifies a gain claim for this run despite passing host/emulator equivalence. Keep the candidate private and do not integrate it. Existing update/vehicle summaries do not show a comparable change; the cause still needs attribution, not a guessed VU/register explanation. Preserved hashes/windows in the candidate record distinguish measured regression from unresolved cause.

Five interleave decision reports associated with neighboring timing markers fall within each warm region: control5 interleaved/0 plain, candidate2/3, restored4/1. These are reports rather than an exact frame duty cycle; their differing distribution is a confound to price with pinned-path controls if revisiting the candidate. No causal claim follows. The later motion rebuild overwrote the unstripped symbol ELF, so it is not used to attribute this frozen physical ELF; future trials must archive matching stripped and symbol files together.

### Enabled affine-inverse repeat census, physical PS2

A private count-only observer compares the 12 actual model input elements immediately before enabled local-light affine inverse calls. It resets adjacency at the existing frame-end seam and leaves all arithmetic and packets unchanged. Disabled local-light preparation performs no inverse and does not break enabled-call adjacency. Exact bits distinguish signed zero and NaN payloads; pointer identity is diagnostic only. The actual-source host oracle passes 100,039 output comparisons for each compiled-out/on macro setting, independent counter bookkeeping and exact baseline-source restoration.

Native build and a complete 6,600-frame emulator gateoff/on/off run pass with plain order fixed in every arm and two scene reloads. All three 512-frame raw windows and neighboring aggregate count windows are present. Enabled warm frames have 71 actual inverse calls: one first input, 60 finite identical inputs and 10 changed inputs. Of the repeats, 59 share a pointer and one uses a different pointer. No nonfinite input appears; one actual frame-end reset is observed per frame. This identifies repeated computation in the emulator pose, not its physical cost or a cache gain.

The fresh physical same-ELF run also completes 6,600 frames and all three raw/count windows. Activation matches the emulator exactly: 71 calls, 60 finite repeats, one first and 10 changed, with one actual frame-end reset. Work gateoff/on/off is 18.191/18.492/18.239 ms; enabled work increases by 0.253–0.300 ms against controls whose spread is 0.048 ms. Delivery remains approximately 30 Hz. This prices the instrumented census as a whole, not a future minimal cache key or a pure observer bill: sequential animated-light/time/cache effects remain possible. Matching symbol and stripped archives are retained.

Observer bit loads/comparisons/copies/count increments still cost EE work; gate-off retains compiled scaffold. Price actual inverse and key cost before implementing one-entry reuse. The earlier local preparation bucket includes other work and cannot be multiplied by the repeat fraction as a saving. A future inverse-only cache must keep current light transform, normalization and influence live. The [machine record](tyrax2-ee-inverse-2026-10-02.json) preserves host/native/emulator/physical evidence, source/ELF/frozen-log hashes and the visually normal emulator restored capture; no production engine change is accepted.


### Inverse and conservative-key cost census, physical PS2

A separate private five-block Off/CountOnly/Scoped/CountOnly/Off fixture retains
every original inverse. Each block has 2,200 frames with plain draw order; four
scene reloads occur outside warm windows. Scoped mode measures conservative
64-byte full-model key lookup/finite-miss snapshot and actual affine inversion
in separate brackets, with compiler memory clobbers. This does not implement
a result cache, output-store/hit-read cost, or arithmetic skip.

Native compilation and the full 11,000-frame emulator run pass all five raw
512-frame windows and 220 aggregate records. Warm CountOnly/Scoped/CountOnly
activation agrees: 71 calls/frame, 60 finite repeats, 11 finite misses, zero
nonfinite misses and one frame-end reset. Off counters and CountOnly ticks
remain zero. Strict parser controls reject incomplete or mismatched runs.
Emulator clock values are not physical EE costs. The
[machine record](tyrax2-ee-inverse-cost-2026-10-02.json) retains exact source,
ELF/symbol and frozen-log hashes. The fresh physical trial also completes all
11,000 frames with identical warm activation and no hang. Work
Off/CountOnly/Scoped/CountOnly/Off is 18.681 / 18.857 / 18.928 / 18.961 /
18.731 ms; every arm has a 33.403 ms period. Off control spread is 0.050 ms
and Count control spread 0.104 ms. Scoped-minus-Count is -0.033 to +0.071 ms,
so this run does not independently resolve clock overhead.

Scoped finite-repeat key/inverse costs are 0.106313 / 0.063533 ms per frame;
finite-miss key/inverse costs are 0.031846 / 0.012556 ms. Combined key work
(0.138159 ms) is greater than inverse work (0.076089 ms) in these diagnostic
brackets. These are instrumented child costs, not a cache candidate gain.
The repeated-inverse bucket is small despite 60 repeats/frame; this does not
support promoting the conservative-key cache. Original physical parsing
failed on interleaved host-service open messages; a separately hashed strict
v2 parser normalizes only known anchored complete rows, preserving the
original parser/log and passing three positive/twelve negative controls.
No inverse-only cache or production optimization is accepted. Even on PS2,
inverse-minus-key will not establish a frame saving or justify production reuse
without a separate candidate comparison.

## Native reference metadata reuse: paired physical controls, 2026-10-03

The [paired reference-memo record](tyrax2-native-reference-memo-2026-10-03.json)
accepts both orders of the same native ELF, 10,800 loops and 768 samples.
Keeping the first 16 nonempty reference classifications within one copy avoids
322 repeated immutable queries and 200 repeated readable-range queries per
sampled frame. Net observed work falls by 0.296–0.304 ms; periods remain about
33.367 ms. This includes the 128-byte local scratch and common diagnostic
counters. Generic callbacks remain unchanged, and additional references spill
to their original callbacks. Production integration has separate native and
ordinary-game checks; these diagnostic results do not predict a 60 FPS gain.

## Ordinary-clock cadence control before reference memo, 2026-10-03

The [quiet cadence record](tyrax2-quiet-cadence-2026-10-03.json) preserves the
authored progressive Hybrid output, vehicle HUD, audio, physics and visual
clocks. FrameProfile and HardwareTrace are compiled out, with no trace ring.
Both physical orders complete 5,400 loops each. The completed rendered-flip
period is approximately 33.367 ms, with no synthetic presentations in the
sampled windows. Sampled inclusive non-pacing time is approximately 19.2 ms.
It includes interrupts, other waits and deferred completion, rather than pure
EE computation. Automatic interleave choices differ across arms and boots;
negative sampler deltas therefore do not establish negative instruction cost
or a uniform observer correction. This is a different ELF and workload from
the fixed diagnostic controls and must not be subtracted from them.

## Post-Memo ordinary-clock night cadence, physical PS2

Both fresh physical orders of the retained quiet vehicle fixture complete
5,400 loops each: 384 raw samples and 30 chunk records in total. Independent
source review confirms current production count reuse and Memo16; source,
ELF/symbol, native provenance, assets and configuration match across boots.
The first start followed a physical reset; the reverse start was confirmed
after a network reset. Logs and file exports are frozen separately.

Rendered flip-return periods are 33.366664–33.366698 ms (about 29.97 FPS),
with no synthetic presentations. Inclusive non-pacing chunk means are
18.923650–19.037890 ms. ABA sampler On-minus-Off differences are
-0.027762/-0.103965 ms; BAB differences are -0.025819/+0.059199 ms.
Outer-control spreads are 0.076203/0.085018 ms. Ordinary state and adaptive
interleave drift remain; mixed signs do not establish an isolated observer
price, zero-cost sampling or a uniform correction. No historical cross-ELF
subtraction, pure EE cost or 60 FPS gain is accepted. See the
[paired machine record](tyrax2-postmemo-quiet-2026-10-03.json) and its independent
review for exact windows, provenance and limitations.
Repository JSON acceptance records use LF checkouts so cross-record SHA-256
links remain stable on Windows and Linux. Raw private device artifacts keep
their original bytes and hashes.

## Front/tail observer activation and compiler checks, 2026-10-03

A private boundary sampler completed both same-ELF orders in PCSX2: 5,400
loops per order, 384 whole-loop samples and 128/256 front/tail samples.
It adds one ordered Count read after Update and before beginFrame, reuses
existing presentation/pacing data, and checks exact whole=front+tail algebra.
The two common endpoints require 262 reads per phase; an active phase requires
391, including its prime: 129 additional reads. Source, ELF/symbol text and
runtime assets were checked independently. This validates activation and the
protocol, not the physical observer price. The common reference apparatus
and buffers are present in every arm and remain unpriced.

Authored Auto is retained as a separate rejected homogeneous-calibration
control. Its 129-loop windows cross all 48 probing intervals even when the
retained winning choice stays unchanged. Actual execution order is a different
state from that choice. The accepted private calibration pins plain order
in every arm, checks actual order/probing at both boundaries, and cannot be
used as an authored Auto performance result. Front/tail values remain inclusive
elapsed intervals, not pure EE arithmetic or VU/GS utilization. This private
extension is not enabled in production examples.

Both [physical pinned-plain orders](tyrax2-front-tail-2026-10-03.json) now
complete 5,400 loops each, 768 common samples and 384 split samples, with
exact whole/front/tail and pacing partitions. Front means vary from 1.863 to
4.219 ms while medians stay around 0.75 ms; non-pacing tail means range from
36.839 to 37.175 ms. These inclusive intervals point toward render/completion
work in this Showcase view, not pure EE or VU/GS utilization.

The optional boundary's net phase deltas are +0.890/+1.237 ms in ABA and
-0.122/+1.854 ms in BAB. BAB outer-control spread is 1.975 ms. Its physical
price remains unresolved; do not subtract a constant overhead or transfer
this pinned-plain result to authored Auto. Source review confirms that the
optional epoch/Count seam adds no file access or hardware wait. Existing
RemotePad `fopen` polling every four loops lies inside front and appears in
the log, but its contribution to spikes is not yet proved. A separately
qualified no-RemotePad calibration now completes both physical orders,
retaining ordinary quality, physics, audio and clocks. The common reference
remains unpriced; see the follow-up below.

### RemotePad-free front/tail control, 2026-10-03

The [paired physical record](tyrax2-front-tail-no-remotepad-2026-10-03.json)
contains 10,800 loops, 768 common samples and 384 split samples from one
fresh native ELF. Exactly three source inputs differ from the earlier
fixture: the generated RemotePad header and CPP become their disabled
stubs, and the project sets `remotePad=false`. The engine, sampler, ordinary
assets, physics, quality, audio and clocks are retained. Neither order
opens `livepad.bin`.

Active front means are 0.724–0.744 ms; non-pacing tail means are
36.376–36.384 ms. This narrows the inclusive workload toward render and
completion in this pinned-plain Showcase view. It does not isolate EE,
VU or GS arithmetic. The optional boundary remains unpriced: ABA deltas
are -1.776/-0.008 ms with 1.768 ms outer-control spread; BAB deltas are
+0.274/+0.016 ms with 0.259 ms spread. These changing signs do not represent
negative timer cost or permit a uniform subtraction. A cross-ELF comparison
does not establish RemotePad causality or a production renderer gain.

The private direct StaPip producer has since reached an independent
five-file source review, 547 actual-header host controls and 13 extracted
queue transaction controls. A compile-only probe with the actual EE
compiler confirms the expected 1,308-byte ledger layout. Host producer
ownership and queue surroundings are modeled; the full native macro matrix,
target lifecycle checks, actual fallback/prefix coverage and physical
same-ELF speed trials remain required. It is not integrated into production.

The private activation trial has now passed 5400 ordinary-clock night loops in
PCSX2 and physical PS2, with zero invalid packets and positive direct commits
after a drained Off-to-On boundary. The emulator reports 117,810 commits and
63,000 declines; PS2 reports 116,607 commits and 61,200 declines over the
1800-frame On observation. Unsupported packets keep the original submission.
These workloads use ordinary clocks, so counts need not match across devices.

The first activation attempt stopped at an invalid ledger. A separate diagnostic
version captured a REF tag at offset12: raw `30050096`, expected `30000096`,
with identical address and TTE words. The difference is solely SDK DMA padding
bits. The SDK setter writes named fields without clearing reserved bits16..25;
the existing semantic checker already ignores them. The corrected private
version masks only those bits in three capture-admissibility checks. It records
all four actual words and preserves their exact final comparison, so later
padding mutations remain invalid. V1's exact runtime tag was never captured;
the diagnostic proves the cause of V2's observed failure.

The [activation record](tyrax2-direct-producer-activation-2026-10-03.json) pins
all failed and successful versions, source/ELF/assets and actual SDK host
controls. Successful activation is not encoding or raster equivalence. Quiet
sampler mode changes alongside the producer in this smoke, so its phase times
are not an accepted candidate gain. Next qualify exact Off/On/Off raster pairs,
ordinary motion/scene/resource lifecycles and native macro profiles before
production integration or a 60 Hz claim.


## Direct-producer fixed raster and net route cost, 2026-10-03

The corrected private producer now passes both Off/On/Off and On/Off/On on
PCSX2 and physical PS2, using one ELF and the current Memo16 engine. Each boot
records 384 samples and three completed GS readbacks. All three rasters are
byte-identical within each boot; the two boots also match within each device.
The frozen camera, world/light guards, positive On commits and zero invalid
packets qualify this narrow plain-night workload. Working alpha is converted
with saturation; this does not establish raw GS alpha equivalence above 127.

The physical result rejects this candidate as a demonstrated optimization.
Off nonpacing work is about 26.68–26.69 ms; On is 27.51–27.52 ms. All four
within-boot contrasts add0.828–0.841ms, while outer same-policy means differ
by 0.0024/0.0065 ms. Total elapsed remains about 33.33 ms because pacing shrinks.
The span includes begin/render/end and actual VIF/GS synchronization, so this
is an inclusive route regression, not a pure producer EE instruction bill.
It cannot be priced by subtracting the separate replay scanner experiment.

The first native attempt failed on an undefined disabled-probe macro. Its
successor passed raster/protocol checks but was rejected for 16 host-log opens
inside the frozen interval. The final fixture suppresses the entire generated
contact-telemetry observer and only healthy SIF announcements in every arm;
warnings/counters and fatal guards remain. Both orders now have zero observed
host log opens between FREEZE and DONE. Common clocks and apparatus remain
unpriced; no cross-ELF observer correction is applied.

The [machine record](tyrax2-direct-producer-raster-2026-10-03.json) retains
all four accepted archives, the rejected attempts, raw statistics and source,
ELF, asset and independent-parser provenance. Motion, scene/resource lifetime,
native macro profiles and ordinary authored-night/Showcase FPS remain open.
Keep the gate private and Off by default. Audit remaining passes and native
encoding before expanding a slower candidate into production.


The subsequent [ordinary overlap trial](tyrax2-direct-producer-overlap-2026-10-03.json)
also completes both stage orders on PS2 and PCSX2. Sampler mode 0 is held
constant: six Count boundaries form five 64-loop chunks perphase, with no RAW
samples. The two physical boots retain 30 chunks/1920 measured loops. Off work
excluding recorded pacing is 19.47–19.52 ms; On is 20.20–20.25 ms. All four
descriptive within-boot contrasts regress by 0.676–0.780 ms, versus 0.027/0.050 ms
outer-control spreads. Completed rendered-presentation calls remain about
29.97Hz, with zero synthetic completions in the measured aggregates.

This trial preserves live game clocks and ordinary overlap while pinning the
plain route and excluding common logging observers. Live states can drift;
sparse contexts do not prove workload or raster parity. Phase-wide positive
commits cover 1800 loops, while timing covers 320 loops; they are not perchunk
encoding counts. Six common clocks and held apparatus remain unpriced.
The entry-to-entry intervals can include previous render completion, so their
nonpacing value is not pure EE execution. This is a second rejection of a
demonstrated gain, not an additive saving or a universal FPS claim. Production
stays unchanged. Audit actual mutable REF ownership/copy volume and true
producer emission before implementing another broad metadata path.

A proposed Showcase matrix trigonometry cleanup was also rejected before
integration. The actual optimized MIPS function already calls sinf and cosf
three times each; host wrappers that count calls prevented the compiler's
common-subexpression elimination and falsely suggested 18 calls. Verify the
actual target disassembly before treating a source-level call reduction as
removed EE work.

## Next 60 Hz candidates: host controls, 2026-10-03

An independent audit confirms that the retained post-Memo quiet vehicle ELF
contains the current production count-reuse and Memo16 implementation. Its
469-file manifest, ELF/symbol archives and launch inputs match. Private quiet
hooks, disabled diagnostic/control polling and the saved night start remain
explicit apparatus differences. A resumed reset and launch produced no fresh
confirmation; there is no new physical timing result from that attempt.

The earlier broad beam-assembly key has a concrete over-invalidation case:
the district lamp with brightness 1.15 and flicker 0.025 changes its raw level
while its effective beam color remains saturated at `kk = 1`. A private
variant keys actual eligibility and saturated brightness instead. The current
generator and generated-game baseline match the extracted oracle. Actual
O0 and O2 host runs each pass 3,126 steps and 43,989 assertions. This repairs
an activation case, not a measured optimization: the eight-lamp key still
contains 226/246 words for main/portal views, and moving the camera forces
misses. Do not integrate the broad cache as a demonstrated driving gain.
Pool FIX changes remain real visual changes; their rebuild reasons should be
counted rather than suppressing flicker. Replacing coronas with the particle
VU1 billboard path also changes partial-quad clipping and is not a transparent
shortcut.

A separate private direct-recording smoke uses the actual FrameVifWriter,
FrameChainArena and ImmutableSpanTable headers. Its first 24 checks preserve
ordered VIF commands and payload against an extracted existing adapter,
including capacity/END reservation, prior-prefix rollback, actual lease
refusal/retirement and mixed/mutable fallback. Raw DMA tags intentionally
differ: direct inline CNT replaces a REF to an owned snapshot. Producer
ownership and EE addresses are modeled; full queue integration, producer
coverage, cache publication and hardware performance are not verified.
An additional warning-clean host run passes 540 extended checks, including
full-registry refusal, both reader banks, later-reference failure and
65,535/65,536-qword writer boundaries. These remain host controls.

The proposed first target is a finalized StaPip packet containing only inline
uniform operations and registered immutable whole-baked-stream references.
An actual lease is required: a baked entry's `complete` flag does not prove
successful immutable registration. Unknown or mixed producers retain the
existing submit path. Inline payload consumes the bounded 128 KiB native
prefix, so bookkeeping cost, high-water, splits, fallback, teardown and both
physical arm orders must be checked before integration. Count reuse already
removed eligible native-sizing scans; do not advertise removing them again.

Private evidence remains under `F:/Projects/tyrax2-lab-20261001/`:
`resumed-60fps-20261003/postmemo-baseline-audit.json`,
`ee-resumed-beam-key-review/`, and
`resumed-60fps-20261003/direct-replay-prototype/`. These host controls establish
candidate behavior only; no additional FPS or production optimization is
accepted.

The private `ee-resumed-pool-reason-census/` also supplies a bounded,
default-off classifier for actual pool-key changes. Host assertion controls
pass with warning-clean O2 compilation. Its 536-byte metrics distinguish
membership count, ordered member identity, vertex/ST stamps, RGB and FIX;
mixed reasons remain mixed. Runtime Off returns before reading either key or
incrementing metrics. No target fixture includes it yet, and the branch,
traversal and count overhead still need paired physical pricing. Strict
captures must reject invalid/overflow counts and reconcile flushes, rebuilds
and mask-bin totals. The final root host qualification record is
`resumed-60fps-20261003/root-host-qualification.json` (SHA256
`8eeb2d5f9ff26290fca33ede58511f6ced89ea067f58ac43d9e804eb50826605`).

The [same-frame Core follow-up](tyrax2-minimal-scopes.md#scene-completion-and-core-aggregate-follow-up) places13.905–14.153ms in135 disjoint Core calls inside Scene17.321–17.445ms. Remaining Scene time is3.284–3.540ms including observer seams. Source-qualified completion minus existing pacing averages0.003120ms inclusive; this is not VU/GS utilisation. Narrow Core internals before selecting an offload kernel. Adaptive contexts change and common instrumentation/layout remain unpriced; these figures are not an optimization gain.

The [inner-work scopes](tyrax2-minimal-scopes.md#selected-inner-work-stripped-packages) and [dated record](tyrax2-inner-work-scopes-2026-10-04.json) qualify V5 stripped-package Work (1.126–1.149 ms) and V6 list-package Work (0.827–0.831 ms), plus the V7 dispatch tail. V7 dispatch-tail Work averages 7.167–7.329 ms; same-frame Scene minus that tail is 10.643–10.698 ms inclusive. Narrow the remaining routes using their own source/caller and dynamic clock controls before selecting an optimization. These are different intervals and apparatus versions: no cross-version gain, pure EE/GPU bill, uniform observer fee or ordinary 60 FPS acceptance.

The [Core prefix and Core-owned submit record](tyrax2-prefix-submit-scopes-2026-10-04.json) qualifies separate V8/V9 both-order physical pairs and emulator controls. Own On windows, actual dynamic reads, tax/chunks and sparse contexts remain bound to each source/ELF. No cross-version subtraction, optimization gain, pure EE/GPU bill, common cost or ordinary 60 FPS acceptance.
