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

## Next subdivision

Private V3 splits extras into complete pool/receiver, beam/corona and vehicle/lamp glow entry gates while keeping the ordinary family mask zero. It retains the V2 sampler and quiet wrappers byte-for-byte and adds cold activation counters outside measurement windows. Its 497-input native build, target ABI, runtime assets, host controls and negative parser cases passed independent checks. Pool removal/restoration completed both emulator orders; three phase captures are available for the reverse order, while the first order has only a restored-full capture. Beam and glow cuts completed the forward emulator order with three phase captures each; reverse orders remain open. Emulator rasters and timings do not qualify physical performance. V3 physical trials and appearance-preserving optimization candidates remain separate pending gates.

The [immutable V3 source archive](tyrax2-night-extras-sources-2026-10-04/README.md) reconstructs all 497 source inputs from V2 byte-for-byte. Its historical external `night-ablation-publication-v2` reference corresponds to the [V2 archive in this repository](tyrax2-night-ablation-sources-2026-10-04/README.md). Neither archive includes native ELF or runtime asset payloads. Archive metadata records the packaging-time state; later completed runs are qualified separately.

If the first joint arm improves normal presentation, split the expensive families using add-back trials and then split the selected family's lamps/objects. If it does not, use the broader effects control and then investigate retained base geometry/preparation/presentation. Candidate optimizations follow measured hypotheses; the experiment does not change production defaults or automatically remove authored features.
