# Full-city daylight queue audit

The private selective PS2 observer separates frame snapshotting, native command
emission, queue bookkeeping and chain closure within the daylight driving trial.

Native-bound ELF SHA-256:
`6e6dd0f6b8b526c4ec188ef5788d12d17a6db6288a1c138f201b265f50ab9794`.
Source head at physical launch: `f91f587b0`. Native source/object/archive/ELF
binding, 5400 owned software-emulator loops with raster inspection, 17 parser
rejection controls and 5400 physical loops pass. Physical TV and audio are not
independently qualified. No production renderer or gameplay change is introduced.

The [strict physical result](physical/strict-result.json) and
[primary qualification](physical/root-qualification.json) retain 384 raw rows,
256 active partitions, whole-loop closure, preceding-loop completion ownership
and exactly two GIF wait calls covered once by existing presentation pacing.

## Physical result

These are exclusive elapsed buckets inside render, including diagnostic cost
and preemption. Producer-send, submit and seal are remainders after nested
categories and existing waits/pacing are removed. The unobserved bucket contains
the rest of rendering, not an additional queue cost.

| Queue region | All 128 driving rows, mean ms | 30 instrumented over-budget rows, mean ms |
| --- | ---: | ---: |
| Snapshot ownership/copy | 2.577651 | 3.565635 |
| Native command emission | 0.564533 | 0.762189 |
| Queue submit remainder | 0.392737 | 0.474825 |
| Producer sendPacket remainder | 0.298779 | 0.365123 |
| Seal/flush remainder | 0.012127 | 0.012826 |
| Existing cache writeback | 0.009335 | 0.009661 |
| Enqueue remainder | 0.006919 | 0.006971 |
| Extra native sizing pass | 0 | 0 |
| Open-chain callback | 0 | 0 |
| Rest of render, outside these queue scopes | 10.063547 | 12.325913 |

Snapshotting is about two thirds of the3.862081 ms observed queue/producer
subtotal in the driving cohort. It averages2.031746/1.960360 ms in the two
parked active cohorts. This scope wraps `copyForNativeRecording`, including
preflight, range/lifetime resolution, chain copying and REF fixup/payload copies.
It does **not** isolate memcpy or instruction retirement. The existing engine
memoizes the first16 REF resolutions and already uses quadword copy assembly;
neither should be proposed as a new optimization from this result.

The drive's7583 snapshot calls all use counted results and successfully return
through native recording, with zero failed snapshots/retries. They copy a total
29566848 bytes and borrow137755904 bytes: mean230991/1076218 bytes per driving
row. These are snapshot output accounting, not unique mesh bytes or physical DMA
traffic. There are177518 emitted records and333579 source qwords, with the
counted snapshot supplying sizing; zero extra sizing-loop entries are observed.

Every active driving row has **one flush and one VIF1 queue enqueue**:128 each.
Mean59.242188 submit calls are accumulated before that enqueue. The same
one-per-row relation holds in both parked active cohorts. This positively
witnesses one recorded VIF1 chain start in the measured ordinary path; it does
not claim that every DMA channel or presentation operation uses one chain.
Open-chain callbacks are not entered here, so their zero cost is an activation
observation rather than a cheap implementation verdict for other workloads.

Total driving non-pacing work averages15.309701 ms: render13.925629 ms,
pre-render update1.339778 ms, engine remainder0.038528 ms and explicit
VIF/GS waits0.004590/0.001176 ms. Work maximum is22.870005 ms. The320-loop
ordinary sampler has18.873287 ms mean entry period and a slowest64-loop chunk
of21.896940 ms. Both parked320-loop windows retain approximately59.94 Hz.
The30 over-budget diagnostic rows use16.68333 ms as a cohort threshold; they
are not a direct count of normal-game missed displays.

## Observer calibration and limits

The former dense game-stage/work APIs compile to empty inline bodies in this
fixture. Only selected queue scopes make new render clocks. Every scope entry
and exit costs one Count read; equal nested tags elide clocks. Wait and pacing
boundaries reuse the corrected enclosing balance's stamps. No extra DMA drain,
FINISH, SDK wait, polling or per-vertex clock is added.

Parked same-ELF controls are Off40/On48/Off40 and On40/Off48/On40. Observed
ON-minus-OFF work contrasts are **+0.786493/+0.415188 ms**; outer spreads are
0.005654/0.000803 ms. These are net perturbations in these controls, not a
single instruction bill or a uniform per-scope correction. The two active
parked cohorts have different submit counts (3336/48 versus4640/80); sparse
camera matching and bounded car drift do not prove pixel-identical workload.
Keep ordinary physics, visibility and recorded state in the interpretation.
Neither moving rows nor different ELF/selected-camera trials can be priced by
subtracting one parked contrast. This remains a diagnostic lead, not a speed gain.

There are125496 additional queue Count reads and3328 balance reads in256
enabled rows. Driving queue reads average477.9375 per row (maximum788).
OFF makes zero new clocks but retains branches, metadata, reset/copy work and
common footprint. Named linked buffers total42288 bytes: ordinary6144/420,
balance17408/136, queue17920/140 and call summaries120. Absolute common
code/data/cache cost remains unpriced. The inherited `countReads=262` phase
label describes only the ordinary sampler.

Next-entry minus prior whole-loop records inter-loop gaps independently. ON-minus-
OFF contrasts are+0.000592/+0.000952 ms; these include postlude/prelude and
preemption, not exact sample-copy instruction costs.

## Next useful experiment

Focus on `FrameChainArena::copyEngineKnownImpl`: preflight/range and lease
lookup versus chain copy and fixup/mutable payload copy. The existing
`ImmutableSpanTable::borrow` first calls `contains`, then repeats `lowerBound`
for a successful borrow. A single-lookup implementation preserving all range,
retirement and bank-reader semantics is a concrete source-level candidate;
this trial does not measure that lookup's share or prove a gain. Qualify it with
admission/lifetime boundary controls and direct same-ELF physical comparisons
before changing the public path. Do not remove ownership checks or borrow
mutable payload solely because the copy scope is large.

Snapshot savings alone are not established to fix every driving spike: the
unobserved render bucket still grows in heavy rows, and full night is outside
this daylight trial. Closure/seal, extra sizing and a presumed many-chain DMA
launch problem are poor priorities for this measured view.

## Triangle strips follow-up

The user's vertex-sharing question refers to triangle strips and the preceding
attribute-aware vertex welding. Two adjoining triangles can reuse two corners;
the strip sends four vertices instead of six. Fewer actual submitted vertices
can reduce packaging, snapshot traffic and VU work, but welding source positions
alone does not reduce an expanded triangle-list stream. Lit parts must retain
normal and UV discontinuities; the unlit wheel's no-normal key must not be used
for a lit body. See [model pipeline](../model-pipeline.md#triangle-strips).

A read-only [shipped asset inventory](strip-asset-inventory.json), bound to the
physical deployment manifest, finds Ravager body paint5892 list /3390 strip
vertices and its wheel480/270. Lamps120 and glass204 remain lists. Current CC96
body is11058/5010; the4212 figure in the older model-pipeline study describes a
historical asset, not this current file. These are base-mesh encoded counts,
not per-frame submissions or runtime strip admission. Roads, terrain and
eligible models already have strip paths. Additional gains need actual list/
strip/fallback admission by pass and eligibility review of remaining geometry,
not a second claim of savings already present in the baked assets.

## Evidence and reconstruction

Only three engine sources differ from the immutable parent render-audit v2:
the selective header, `vif1_queue.cpp` and `stapip_qbuffer_renderer.cpp`.
[Parent source references](parent-source-references.json) bind every unchanged
file to its existing archived path/hash; `source/` carries changed files.
Restore the parent source tree, overlay these changes, and retain the actual
resource set/native toolchain authority when rebuilding. Historical cache paths
inside native proofs are provenance, not current cache contents.

Preparation v1 was rejected at compile time by its incorrect148-byte size
guard. The actual row has140 bytes; v2 fixes the guard and bound buffer proof.
The failed header/source review/build log remain in `rejected-size-guard-v1/`;
v1 had no runtime and was never sent to PS2. Header preparation, v2 correction,
native binding/coverage/release, strict parsers and negative controls are under
`preparation/`. The runtime contract checks exact activation relationships,
exclusive sum, clock count, disabled zeros and scope/event summary agreement.

Physical stdout is frozen through successful completion; normalization removes
transport CR/LF only and extracts known bounded families. A live client is not
claimed to have exited. Exact owned emulator process absence is independently
verified after its stop request; Xvfb is retained. `inventory.json` pins this
archive and unchanged parent references are verified separately.
