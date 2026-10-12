# A37 Moving Raw-PC Evidence

Native490 bound files, ELF
`185560b0f31abc32e325c66023faefb52a2925e6c85464e51f2eedc5384e7b9a`.
Both targets complete six ordinary drives with roads/live reflections; night
has eight enabled dynamic lights. Common A31/A30 baseline, A35 OFF throughout.
Two periods997/1733us in both day/night followed by sampler-off moving return.
Each drive has independently held OFF/ON/OFF main/env image checks.

Closed hidden emulator30604 / physical30492 rows,36 exactly equal main/env
rasters per target,15 reader mutations rejected. No overflow or late callback.
Exact debug/deployed .text equality, retained raw unknown bins and inline
locations.623-file immutable inventory; this README is outside that inventory.
The copied qualifiers replay byte-identically on both targets, and copied
physical symbolizer replays byte-identically with the original native tools.
Full physical asset mirror is omitted with recorded hashes; not a runnable game.

## Physical Distribution

Shares below are sampled raw interrupted instruction residency, NOT exclusive
cycles, stall costs or caller costs. Interrupt masking/preemption/coalescing can
bias them. Coarse region is main-loop state, not thread ownership. CP0 EPC read
inside callback is NOT the interrupted game PC (see linked leaf calibration).

| Function | Day997 /1733us | Night997 /1733us |
|---|---|---|
| graph_wait_vsync |31.90 /30.93%|33.56 /34.04%|
| FrameChainArena::copyQwords |6.05 /6.29%|5.36 /5.00%|
| StaPipCore::render |3.68 /4.08%|3.62 /3.38%|
| StaPipQBufferRenderer::sendObjectData |3.12 /3.33%|4.56 /4.01%|
| copyForNativeRecording |2.81 /2.57%|2.36 /1.89%|
| CoreBBox::frustumCheckAABB |2.73 /3.04%|2.93 /2.97%|
| isImmutableSpan |2.16 /2.33%|2.22 /2.09%|

Raw out-of-text share0.29..0.43%; no address alias normalization.95-ish percent
of samples have the coarse render label, which also includes presentation.
Emulator's VBlank share51..58% is intentionally NOT physical performance proof.

Sampler-off returns: day49.2599 entryHz/14.0447ms non-pacing work/p9520.1634;
night41.1797/16.1662/p9523.7794. Sampled trips47.5847/48.0653 day and39.8463/
39.8456 night: observer is NOT free. One-order OFF returns are not bracketing
cost A/B; real-dt trajectories differ. Work minus pacing still includes other
waits/stalls, not pure EE cycles. No60fps or impossibility proof, no promotion.

## Source Audit / Next Decision

Actual queue is already counted snapshot, no extra inline-payload native copy,
borrowed immutable geometry, first16 REF scratch and R5900 quadword copy. Do not
reopen C05 fusion/H05 lookup or claim all borrowed bytes were copied. A copy-only
assembly tweak is not the substantial final solution implied by this profile.

Actual boot reports448x448 Hybrid, two buffers. completePipelineFrame waits
the prior VIF/GS job, then graph_wait_vsync before Hybrid's single display copy.
Existing Hybrid triple queues two CT16 display targets around one CT32 draw
target and removes that unconditional double-buffer vsync wait. Its additional
FINISH/VRAM/texture pressure can negate benefit; NEVER remove required fences.
Prior frame-pacing.md correctness/general synthetic-load tests do not measure
this current moving physical workload. Next qualify a same-ELF current-scene
double/triple/double net comparison, verify actual buffer grant/no quality change
and resource-rebuild lifecycle, not merely set a preference or unlimit output.

If that does not address the throughput gap, audit the provenance/lifetime of
the mutable ranges actually copied, then a producer-owned frame-bank contract.
Do not infer which producer caused copying from generic copyQwords samples.
