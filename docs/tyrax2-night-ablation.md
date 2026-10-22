# TyraX2 joint night ablation

The joint night ablation experiment tests whether removing several effect families together lets the current renderer meet its ordinary presentation budget.

The [immutable private source snapshot](tyrax2-night-ablation-sources-2026-10-04/README.md) contains exact headers, source patches, analyzers and build/evidence helpers. Its documented reconstruction matched all 497 frozen inputs byte-for-byte. This archive does not add a production renderer setting or change shipped defaults.

## Experiment design

The first joint arm retains the car, map geometry, camera, HUD, sky and material environment passes. It gates live dynamic lighting, scene/vehicle light receiver and beam effects, runtime shadows, particle/smoke branches and post-processing together. Baked appearance and some retained family setup/iteration remain; this is a defined branch ablation rather than removing every byte associated with night. This differs from the October 2 single-family removal trials: effects may interact, so their separately measured differences are not added to predict the joint result.

Use one private ELF with an explicit three-phase plan. First hold the full-night mask unchanged and price the sampler Off/On/Off. Then compare Full/JointOff/Full and JointOff/Full/JointOff with sampling enabled in every phase. Warm frames, timed windows and deferred exports must remain distinct. Further add-back trials start from JointOff and restore selected families, bracketed by their own JointOff controls. A broader environment/sky-effect cut is a separately named fallback, not part of the first night cut.

The source baseline is the current production engine with narrowly reviewed quiet loop and presentation hooks; it must not inherit the rejected SPR/CALL banks, cache or scanner. Ordinary clocks, authored display settings and existing completion/presentation fences remain. Record actual rendered and synthetic presentation events separately. Engine flip-return periods do not measure television photons, particularly with queued triple buffering.

Family gates need positive activation evidence without per-object clocks or in-window host I/O. Gate counters are collected only on verification frames 750 and 1155, outside the tax/sample interval, and distinguish attempted, executed, skipped and submission-attempt work. Submission attempts do not prove emitted DMA or raster work. Preserve runtime light requests and time evolution where render effects are disabled. Warm after every mask transition; retain sparse scene/camera/pipeline/adaptive context rather than assuming exact interior state equality. Observer controls price incremental instrumentation and its routing response, not a universal correction for common compiled code, memory or layout.

## Acceptance and current status

Implementation, host controls, native provenance, emulator activation and physical both-order measurements are separate gates. The current private V2 source and native build passed independent qualification: 497 source inputs, 298 runtime assets (294 authored/baked bytes unchanged and four converted audio files), matching ELF/symbol text and actual target ABI. Thirty-two host transcripts completed 5400 loops each; malformed configurations and parser rejection controls passed. The V1 build remains preserved and unlaunched because numeric variadic print arguments had target type warnings; V2 explicitly casts those arguments, with no other source delta.

The V2 emulator Full/JointOff/Full run completed all 5400 loops and passed independent strict capture and raster review. Sparse witnesses show the selected branches were skipped under mask 31 and restored under mask 0; captures retain the car, base scene and HUD, remove the intended visible effects and restore full-night appearance.

Both physical sampler calibration orders completed. Net enabled contrasts were -0.018312/-0.003651 ms and -0.011540/+0.047956 ms against their own controls; the larger outer-control spread was 0.059496 ms. This bounds the observed net timing response, not a universal zero observer cost or a correction to subtract. One reverse sparse adaptive choice differed, while pipeline mode remained enabled. Common compiled observation work remains unpriced.

Both physical joint orders also completed the strict 5400-loop protocol. In Full/JointOff/Full, elapsed-minus-existing-pacing means were 18.8911/13.3906/18.7552 ms; in JointOff/Full/JointOff they were 13.3716/18.8581/13.3775 ms. Mean engine flip-return periods were approximately 33.3667 ms in full night and 16.6833 ms under the joint cut. The simplified stationary scene reaches the cadence associated with ordinary NTSC 59.94 Hz; this does not qualify production full-night quality at 60 FPS. Nominal configuration metadata reports 60000 mHz. TV photon timing and physical visual restoration remain unverified: the operator was away. Emulator restoration is qualified separately.

Eight completed physical boots are retained in the [dated machine bundle](tyrax2-night-ablation-2026-10-04.json). Each add-back below uses its own joint-off controls; these contrasts are not additive component bills.

| Restored families | Own elapsed-minus-pacing contrasts | Engine flip-return cadence |
|---|---:|---|
| Lighting and receiver/beam/glow extras (bits 3) | +3.9394 to +3.9876 ms | Mixed cadence; middle mean 30.8902 ms |
| Shadows, particles and post effects (bits 28) | +1.3888 to +1.4901 ms | Approximately 16.6833 ms |
| Lighting route alone (bit 1) | +1.0600 to +1.0615 ms | Approximately 16.6833 ms |
| Receiver/beam/glow extras alone (bit 2) | +1.9536 to +1.9599 ms | Approximately 16.6833 ms |

Only the first add-back order completed; reverse add-back orders remain open. The next reverse lighting/extras boot reached fresh loading but repeatedly reported `freepad: DMA Busy` before its first verification window. It is an incomplete boot, excluded from performance evidence. Physical visual restoration is still pending because the operator was away.

Original raw evidence and failed attempts remain preserved. Clock intervals are modulo-qualified; old boots lack independent wide-clock wrap exclusion. A host-only arrival recorder on later boots timestamps unchanged untimed verification markers without adding console commands. Network arrival spans provide a sanity check, not an absolute hardware-clock proof.

## Extra subsets

Private V3 splits extras into complete pool/receiver, beam/corona and vehicle/lamp glow entry gates while keeping the ordinary family mask zero. It retains the V2 sampler and quiet wrappers byte-for-byte and adds cold activation counters outside measurement windows. Its 497-input native build, target ABI, runtime assets, host controls and negative parser cases passed independent checks. All three extra subsets completed both emulator orders. Three phase captures are available for five boots; the first pool order has only a restored-full capture. Emulator rasters and timings do not qualify physical performance.

The [immutable V3 source archive](tyrax2-night-extras-sources-2026-10-04/README.md) reconstructs all 497 source inputs from V2 byte-for-byte. Its historical external `night-ablation-publication-v2` reference corresponds to the [V2 archive in this repository](tyrax2-night-ablation-sources-2026-10-04/README.md). Neither archive includes native ELF or runtime asset payloads. Archive metadata records the packaging-time state; later completed runs are qualified separately.

The [six emulator qualification records](tyrax2-night-subsets-2026-10-04.json) bind completed source/ELF evidence and available raster hashes. Their original logs, images and full proofs remain external LAB artifacts. Rear-view glow intensity is subtle in the images; positive entry and suppression witnesses support activation without a pixel-equality claim.

All six physical V3 boots completed on October 5, retaining the same ELF, ordinary masks zero and sampler On: 32,400 loops, 2,304 raw samples and 90 bounded chunks. The [dated physical record](tyrax2-night-extras-2026-10-05.json) binds their own full-night controls and sparse positive suppression/restoration witnesses.

| Complete subgroup cut | Own cut-minus-full contrast range |
|---|---:|
| Pools/receivers | -0.968 to -0.875 ms |
| Beams/coronas | -0.782 to -0.681 ms |
| Vehicle/lamp glow | -0.530 to -0.296 ms |

Engine flip-return periods remained approximately 33.367 ms in every arm: none of these individual cuts crossed the presentation threshold. The differences include existing waits and routing responses; they are not additive component bills, pure EE execution, GPU utilization or predicted cache gains. Glow varies more between orders; retain its full observed range rather than assigning one fixed cost. Sparse adaptive interleave choices differed despite matching camera/generation witnesses; their causal share is unmeasured. Common compiled observer work remains unpriced.

During the series the user confirmed that the car, map and HUD looked normal without visible glitches. The exact phase of that observation is unspecified, so it does not prove every cut or post-test full restoration. After the final forward glow run, full night was left running without a network reset for inspection. Exact pixel identity and TV photon timing remain unqualified.

## Appearance-preserving candidate

The [private pool color candidate](tyrax2-pool-color-candidate-2026-10-04/PUBLICATION.md) keeps vertex/ST storage, content stamps and bbox version when only a batch member's RGB/FIX changes. Membership, order and source geometry/ST changes still rebuild all streams. Colors retain the original expression and order and receive a fresh content stamp. Independent source/cache-contract review and exact-function host comparisons passed 1,322 cases and 11,622 checks each at O0/O2. The actual R5900 translation unit also compiled without diagnostics; no linked ELF or runtime acceptance is claimed.

Eligibility covers only existing non-volume gobo scene-spot batches, not all receivers or beams. At this October 4 source-only checkpoint, color-only activation was unmeasured. Historical pool preparation residue around 0.18–0.19 ms limits expectations: this candidate cannot be credited with the approximately 1.95 ms whole-extras cut. The separate follow-up below supplies later native/runtime/activation/pricing evidence without rewriting the original archive. Production code and defaults remain unchanged.

## Same-ELF cache follow-up

The [kind4 source archive](tyrax2-pool-cache-sources-2026-10-05/README.md) preserves a separate dual-arm experiment derived from V3: four changed inputs, 497 total. Its historical external V3 reference corresponds to the [V3 archive here](tyrax2-night-extras-sources-2026-10-04/README.md). Kind4 keeps all ordinary/extra masks zero and sampling enabled, switches baseline/candidate/baseline or the reverse, and preserves warm cache state. Cold reason/action counters run only at 750/1155; common classification, branches and layout remain unpriced.

Independent source/host/native/ABI/assets gates passed. The actual dual-arm oracle passed 1,329 cases and 11,722 checks at O0/O2; 20 complete protocol transcripts and 47 parser negatives passed. Both emulator orders completed with positive old/new action witnesses and qualitatively consistent phase rasters. These gates do not transfer the split-only candidate's earlier target-compile acceptance or establish pixel equality.

The [physical pair](tyrax2-pool-cache-2026-10-05.json) completed 10,800 loops, 768 raw samples and 30 bounded chunks. Candidate-minus-own-baseline contrasts were +0.014512/-0.103727 ms in forward order, with 0.118239 ms outer-control spread, and -0.020342/-0.047215 ms in reverse, with 0.026873 ms spread. Engine flip-return periods remained approximately 33.367 ms. The mixed small responses do not qualify a stable improvement or production promotion; no uniform observer fee or cross-version subtraction is applied.

Forward cold witnesses captured the old geometry rebuild for a color-only change but missed a new-arm update. Reverse witnesses positively captured the candidate's color-only update with geometry preserved for 768 vertices. Together these qualify actual target actions across the pair, not a full-window hit census or zero-hit claim for the first candidate arm. Earlier unproven-activation evidence remains preserved.

The user reported normal car, lights, shadows and HUD during this experiment, at an unspecified phase. The final reverse run was subsequently left in full night with the candidate flag enabled; that final state has no separate phase-bound visual confirmation. The source package's packaging-time boundary remains immutable; later pair results are bound by the separate physical record. Production code and defaults remain unchanged.

If the first joint arm improves normal presentation, split the expensive families using add-back trials and then split the selected family's lamps/objects. If it does not, use the broader effects control and then investigate retained base geometry/preparation/presentation. Candidate optimizations follow measured hypotheses; the experiment does not change production defaults or automatically remove authored features.
