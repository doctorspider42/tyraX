# A20 frozen preparation split

This private experiment locates the A18 downstream preparation gap between
StaPip, queue ownership/recording and an explicit unclassified remainder.
It changes no production renderer policy and does not measure moving-game FPS.

## Question and controls

A18 physical day/night show a large ordinary-to-owned-command contrast, but
that contrast does not identify individual EE work or recoverable savings.
The October 6 queue audit measured an earlier moving workload with a different
observer. Do not reuse its numbers as prices for the current frozen scene.
A20 keeps the current A18 V5 frozen state, target, interleave and raster oracles,
but renders the ordinary scene in EVERY stage. No descriptor/command replay
intervention occurs. The captured closure remains only an unchanged oracle.

Stages 0..9 use Off/Off/Count/Timed/Count/Off/Count/Timed/Count/Off.
Each stage has 224 frames; frames 64..191 are exported (1,152 rows total).
Count-only uses the same scope nesting/bookkeeping without reading Count.
Off retains branches, common code/data and reset/export footprint. Differences
against adjacent controls bound net observer perturbation, not a universal
per-clock fee. The additional static sample array is 64,512 bytes.

## Boundaries

- StaPip scopes: `StaPipCore::render`, `StaPipCore::onFrameEnd`, and
  `StaPipQBufferRenderer::flushPendingPacket` for deferred producer callbacks.
- Queue scopes: submit after its initial open-chain closer; recordGif after
  its initial closer; begin/end recording, barrier, drain, waitFor, enqueue,
  and nonempty flushRecording. Snapshot lifetime checks/copies, native record
  construction and queue waits remain intact.
- A category switch charges elapsed time to the previous category. Same-kind
  nested scopes do not add clocks or double-count. A producer callback inside
  the queue switches back to StaPip until it returns. Unclassified gaps remain
  residual; they are not silently labelled StaPip or queue work.
- The first boundary is the existing A18 preparation stamp immediately before
  endFrame. Finish extends through endFrame, synchronizeFrame and the shared
  align2D fence. No scopes may cross that boundary. Finish is RAW elapsed and
  includes pacing: do not add it to already non-pacing whole-frame prices.

These are elapsed function scopes, including preemption and any waits inside
them, not retired EE instructions or isolated DMA/VU/GS costs. Cold image and
descriptor checks occur outside samples. One complete main/env image pair per
stage must agree exactly, including Off returns. No extra synchronization,
allocation policy, geometry, target or rendering defaults are changed.

## Implementation and gates

`tools/tyrax2-preparation-split.py` derives a fresh fixture from bound A18 V5;
it invalidates the inherited native binding before the new native build.
`verify-preparation-split.py` runs the actual observer with deterministic clocks
under ASan/UBSan, testing Off/Count/Timed, nesting, callbacks, wrap and invalid
boundaries. The existing command-price reader detects the bound split header,
requires all-ordinary routes and all split rows, rejects category overlap,
clocks in Count/Off, missing activation or drift, and reports prices only for
physical runs. Its negative controls cover the new failure modes.

Before physical deployment: native binding, complete hidden-emulator night,
equal rasters, stable category calls, all balances, parser negatives. Use the
editor deployment protocol once from the completed live A18 host; no assumed
user reset, no blind retry. If it wedges, retain evidence and stop hardware.
Never power off console or computer.

Decision: compare the two timed repetitions and adjacent observer controls.
Only a stable large bucket with observer perturbation smaller than the
distinction justifies selecting its implementation for further work. Preserve
residual and raw finish/pacing rather than claiming an algebraically forced
CPU bottleneck.

## Qualification checkpoint

Fixture `F:/Projects/tyrax2-preparation-split-20261010-v1` binds 478 native
source files; ELF SHA-256
`9866816e79bd967c0c15d53e768297e29557e93ae8f7823f5ae7fe67255e077e`.
Host sanitizer checks pass. Hidden night emulator completes all ten stages,
1,152 rows and 20 identical main/env images. Scope counts are stable in every
Count/Timed sample: preparation 183 StaPip / 104 queue entries, finish 2 / 10.
Same-kind nested entries are included in counts, not duplicated in elapsed.
The reader rejects 24 corrupted variants; older A18 V5 retains all 14 passing
rejection controls. No emulator milliseconds are reported.

After confirming completed A18 export and ownership of PID 17744, one normal
editor-protocol reset/deployment starts fresh loadelf as owned PID 37688.
No user-reset exception or blind retry is used.

## Accepted physical night result

All ten stages / 1,152 samples complete with unchanged launch inputs, zero
descriptor mismatches, no balance errors and 20 exactly equal main/env images.
Pose z=-56.974621, speed=14.770472, yaw=0; eight lights, 805 VU starts,
1,992,256-byte closure. Scope counts are constant in EVERY Count/Timed row:
preparation 185 StaPip / 104 queue, finish 2 / 10. The physical/emulator count
difference is not within-run drift; their pre-freeze real-dt poses differ.
Heap used/free remain constant after stage 1. No allocation failure occurs.

| Exclusive elapsed bucket (ms) | Timed stage 3 | Timed stage 7 |
| --- | ---: | ---: |
| Preparation StaPip, excluding nested queue | 12.884235 | 12.881707 |
| Preparation queue, excluding nested producer | 4.023375 | 4.011454 |
| Preparation unclassified remainder | 2.435697 | 2.448286 |
| Preparation total | 19.343307 | 19.341448 |
| Finish StaPip | 0.044368 | 0.044853 |
| Finish queue (includes completion waits) | 9.796847 | 9.797759 |
| Finish unclassified remainder | 4.128219 | 4.127696 |
| Finish raw elapsed | 13.969435 | 13.970308 |
| Pacing inside whole-frame elapsed | 3.296497 | 3.297637 |

Finish is raw elapsed; whole non-pacing work is 30.016245 / 30.014119 ms.
The roughly 9.8 ms finish queue scope is NOT priced EE packet construction:
it includes normal completion waits. There is no wait-only leaf observer in
this trial, so the exact wait/CPU split inside that bucket remains unknown.

Observer controls, whole non-pacing means: Off stages 1/5/9 are
29.553486 / 29.551709 / 29.552335 ms; Count stages 2/4/6/8 are
29.723187 / 29.724150 / 29.725707 / 29.725565 ms. Timed adds
0.288412..0.293057 ms versus adjacent Count controls, or
0.461783..0.464535 ms versus bracketing Off controls. Count alone adds
0.169702..0.173997 ms. Timed frames use 454 additional Count reads.
This is measurable but far smaller than the 8.86 ms StaPip/queue distinction.
Do not subtract the net perturbation uniformly from individual buckets;
Off still carries the compiled instrumentation footprint, and comparison to
another ELF does not price that common footprint.

The result prioritizes the roughly 12.88 ms StaPip region (about two thirds of
observed preparation), not DMA ownership as the largest preparation component.
Its 4.01..4.02 ms queue component is still material and includes normal checks,
copying, native translation and any nested queue waits; it is not memcpy-only.
No assertion of an exact pure EE/VU/GS bill or achieved gameplay FPS follows.

Next: separate StaPip per-object bounds/visibility/state preparation from the
geometry-package dispatch/fill/command-building region, retaining queue exclusion
and observer controls. Audit existing source and closed leads first. Do not
restart generic caching, normal-cache collision, fused queue or trivial
clip-plane memo trials merely because their functions lie in this bucket.
No production fix was made by A20; later-view/day/moving coverage is separate.

[Immutable evidence](night-v1/archive-sha256.json) contains 586 verified files,
including native sources/ELF, raw images/samples, launch pins and tools.
Read-only emulator and physical archive reanalysis both pass. The owned
ps2client PID 37688 remains alive after complete export. Hidden emulator and
its owned Xvfb were stopped; console and computer remain powered on.

## A21 object versus geometry continuation

The user authorizes narrowing A20's dominant StaPip bucket. A fresh A18 V5
derivative uses `tyrax2-preparation-split.py --geometry`: identical ordinary
routes and Off/Count/Timed controls, with one new category starting immediately
before the existing `Dispatch` marker in StaPipCore::render. It covers route
selection, baked/retained lookups, package creation/classification/fill, command
building and final buffer flushes through function return. The outer StaPip
category now means object setup (bounds, matrices, texture, lights, object
uniforms) and frame-end maintenance, excluding geometry and queue. Deferred
flushPendingPacket callbacks use Geometry even when triggered by setup/queue.
These are execution regions, not semantic instruction counts: Geometry includes
its cache policy, and Setup may include ordinary waits outside queue functions.

This avoids another generic cache or inner-package counter experiment. A
seven-word Part extends each sample from 56 to 72 bytes (82,944-byte array).
Existing A20 source-bound evidence remains immutable and readable. Host tests
exercise both layouts, including Geometry->Queue->Geometry callbacks, zero
clocks in Off/Count and exclusive accounting. Native/emulator/physical gates,
within-run stable calls, complete images, no extra synchronization and separate
finish/pacing rules remain unchanged. Reader tests add geometry activation,
overlap, Count-only clocks and drift rejection. One editor-protocol physical
night follows emulator qualification; stop hardware on failure, never retry
blindly or shut down either device. Result pending.

A21 V1 fixture is `F:/Projects/tyrax2-stapip-split-20261010-v1`, with 478
bound native files and ELF
`3220a1e3850a7bdc02afa7486856e12eefe5e4b4e6644415ca98a25e6e859dd1`.
Host sanitizer checks pass both layouts. Hidden night emulator completes ten
stages / 1,152 rows / 20 identical main/env images. Stable preparation counts:
119 Setup, 176 Geometry, 104 Queue; finish 1 Setup, 1 Geometry, 10 Queue.
No emulator prices. Old A20's 24 reader rejection controls still pass.
A21's 28 reader rejection controls also pass. One normal editor reset releases
completed A20 client PID 37688; fresh loadelf starts as owned PID 42440. No
retry or manual-reset exception is used. Physical series is in progress.

Closed-lead guard: register H03/H06/H08 already contain older dispatch/prefix,
bounds and object-data/inverse partitions. A21 reconciles the current frozen
workload under A20's exclusive queue/observer controls; it is not discovery
that those functions exist or authorization to repeat old failed memo trials.

### A21 accepted physical result

Night completes all ten stages / 1,152 rows with unchanged launch inputs,
20 identical main/env images, zero descriptor/balance errors and stable
within-run calls. Preparation: 121 Setup, 175 Geometry, 104 Queue entries;
finish: 1 Setup, 1 Geometry, 10 Queue. Timed frames make 696 additional Count
reads. Physical pose z=-56.974754, speed=14.770469, yaw=0, eight lights;
805 VU calls, 1,992,256-byte closure. Heap is stable after stage 1.

| Exclusive elapsed bucket (ms) | Stage 3 | Stage 7 |
| --- | ---: | ---: |
| Object setup / bounds / state, excluding geometry and queue | 5.600145 | 5.622154 |
| Geometry dispatch / package construction, excluding queue | 6.826243 | 6.807554 |
| Preparation queue | 4.035847 | 4.038068 |
| Preparation residual | 2.436158 | 2.433795 |
| Preparation total | 18.898393 | 18.901571 |
| Finish setup / maintenance | 0.042835 | 0.043669 |
| Finish geometry | 0.001636 | 0.001306 |
| Finish queue, including waits | 9.808595 | 9.806829 |
| Finish residual | 4.560880 | 4.558853 |
| Finish raw elapsed | 14.413947 | 14.410658 |
| Whole-frame pacing | 3.726157 | 3.724738 |

Whole non-pacing means: Off 28.784736 / 28.783349 / 28.787502 ms;
Count 29.089272 / 29.081454 / 29.087523 / 29.083386;
Timed 29.586182 / 29.587490. Timed-minus-adjacent-Count is
0.496910..0.504728 ms; Timed-minus-bracketing-Off is 0.799988..0.804141.
Count-minus-nearby-Off is 0.295884..0.304536 ms. Do not subtract those net
contrasts uniformly from individual scopes. A21 versus A20 is a different
compiled layout/observer and slightly different approach pose: the lower
Off/total values are NOT a renderer speedup or priced common instrumentation.

Decision: both Setup (~5.61 ms) and Geometry (~6.82 ms) are substantial.
Geometry is modestly larger in the observed split, not an overwhelming single
cause; the observation does not prove a specific optimization, cache or wait
is wrong. Stop broad scope proliferation. Audit a concrete avoidable operation
within those regions against closed H03/H06/H08/P03/P08/A17 leads, and only
then propose a bounded same-ELF intervention with output/lifetime and observer
controls. No production policy changed, no pure EE bill or live 60 FPS claim.

[A21 immutable archive](object-geometry-night-v1/archive-sha256.json) preserves
586 verified files. Both emulator and physical read-only archive reanalyses
pass; 28 negative reader controls and both host observer layouts pass. Owned
host PID 42440 remains alive after completed export. No further reset was sent;
the owned emulator was stopped and both physical devices remain powered on.
