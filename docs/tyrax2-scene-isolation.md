# TyraX2 scene preparation isolation

Scene preparation isolation is a private diagnostic ELF that separates a frozen scene's full render path from the same EE packet preparation with terminal software completion.

The experiment changes the question from pricing more nested intervals in the
ordinary game to checking a deliberately constrained workload. It does not
change the production renderer or establish 60 FPS. The
[machine record](tyrax2-scene-isolation-2026-10-03.json) retains exact artifacts,
qualification and outstanding physical gates.

## Workload and protocol

The retained vehicle-playground fixture boots night scene 0, warms through 600
ordinary loops with fixed 1/60 simulation steps and neutral input, then freezes
the camera, world and lighting. The frozen loop renders only the main scene,
including engine begin/end and completion. Update, HUD, bloom and other post
effects are excluded. Portals, mirrors, BLSS, field rendering and extrapolation
are excluded. This diagnostic uses Bits32 and two buffers; the original Hybrid
fixture was rejected before the timed loop rather than silently accepted.

One ELF supports two fresh-boot configurations:

- `0`: Full → Full, to measure control stability.
- `1`: Full → terminal preparation sink, with no return to hardware rendering.

Each stage has 128 warm iterations followed by 128 samples. Both arms reserve
the same 10,240-byte sample buffer. Each sample uses two ordered COP0 Count
reads around beginFrame, main-scene render, endFrame and synchronization of that
same job. Existing presentation stall accounting is read outside that interval.
Raw data is written only after all 512 iterations; a separate stdout completion
record after closing the file is mandatory for acceptance. Periodic arena
reports are suppressed in both arms after freeze. Earlier V2 runs retain
protocol evidence but their times are rejected because that report ran inside
the measured interval.

The sink preserves packet assembly, range/lease bookkeeping, cache publication
and logical queue completion, while suppressing actual VIF/GIF starts, FINISH
waits and hardware presentation. Counters reject unexpected fallback, warp or
buffer modes. It is deliberately terminal: CPU texture/program residency and
buffer state can advance without corresponding hardware work, so switching
back to Full would be unsafe without restoring that state.

## Acceptance and interpretation

The parser checks exact schemas, complete sample counts, hardware/software
start counters and post-close stdout completion. Sparse guards retain actual
camera words and camera/light/object-state digests; the parser independently
recomputes camera digests. Equal start counts establish submission cardinality,
not complete packet-byte or pixel identity.

Full includes consumer completion and pacing. The preparation sink includes EE
assembly, cache publication, accounting and software completion; it is not pure
arithmetic. Full-minus-sink measures the changed inclusive path and its
backpressure, not isolated VU or GS time. The common observer remains unpriced,
and error/audio activity can still interrupt execution. Emulator runs qualify
the protocol only. Physical runs and stable cross-boot guards are required
before reporting PS2 costs.

This frozen Bits32 scene is a different workload from ordinary Hybrid night
gameplay and pinned Showcase. Its numbers cannot establish their performance
gain or explain all their bottlenecks. Once physical controls are available,
the next useful experiment is to isolate a dominant preparation group or replay
a captured submission, with equivalent ownership and completion checks.

## Physical controls (2026-10-03)

Three fresh physical boots complete with the same ELF, sources and settled
camera/light/object guards. Each boot produces 256 accepted samples, zero
forbidden events and successful post-close completion. Full has one queued VIF
and one direct hardware transfer per sample; sink substitutes the same start
cardinality with software completion and one software presentation.

| Boot | Stage 0 elapsed / non-pacing | Stage 1 elapsed / non-pacing |
| --- | --- | --- |
| Initial Full → Full | 33.329369 / 26.166495 ms | 33.328117 / 26.166152 ms |
| Full → terminal sink | 33.328148 / 26.162854 ms | 15.480099 / 15.464801 ms |
| Return Full → Full | 33.328084 / 26.168389 ms | 33.327981 / 26.169977 ms |

Conversion uses the existing EE Count convention of 294,912 ticks/ms. Full
control means are stable to approximately 0.0014 ms elapsed and 0.0072 ms
non-pacing across these stages. This establishes repeatability of this
instrumented fixture, not a zero observer cost.

The software-completed preparation still costs approximately 15.48 ms, leaving
only about 1.19 ms of a nominal 16.67 ms frame budget before any omitted work.
The full path also costs substantially more than preparation alone: approximately
10.70 ms in the non-pacing brackets. Removing hardware consumption changes both
completion and backpressure, so that difference is not an isolated VU/GS timer.
Both EE preparation and the consumer/submission path deserve further isolation;
these results do not justify declaring either processor the sole bottleneck.

The initial empty-log reset attempt and its retry remain archived. A physical
power reset recovered the first launch, and the next two network resets accepted
fresh starts. The trial does not establish permanent reset reliability.

## Owned frame capture precursor

The [capture record](tyrax2-frame-capture-2026-10-03.json) qualifies a separate
private V4 ELF on PCSX2 and physical PS2. During frozen warm iteration 126,
before any sampled window, it copies the finalized native prefix and every REF
payload into its own bounded 4 MiB buffer. Later bank reuse cannot invalidate
that closure. Exact payload comparisons at capture, unchanged closure hashes,
strict host bounds and post-close completion validate the exported data.

The actual closure is 2,374,592 bytes: 1556 root quadwords, 393 CNT tags, 1160
owned REF tags and one END. These are source-chain records, not 1554 hardware
starts: the captured frame has one queued hardware start plus one direct
presentation transfer. The stream contains 3530 UNPACK, 152 MSCAL, 631 MSCNT and
24 DIRECT commands. It contains no MPG, BASE or OFFSET. The program image is
unchanged before/after; VIF double-buffer state changes. DIRECT contains no
FRAME writes or IMAGE transfers, but this does not inventory the GS commands
generated later by VU XGKICK.

The original snapshot filenames follow inverted names in the installed SDK
header: `ee-capture-data-*` was read from 0x11008000, which is VU1 program
memory; `ee-capture-micro-*` was read from 0x1100c000, which is VU1 data memory.
The [PCSX2 address map](https://github.com/PCSX2/pcsx2/blob/master/pcsx2/Memory.cpp#L353)
and actual instruction/data bytes establish this correction. Original files,
metadata and rejected interpretations remain untouched; the V3 host analyzer
adds an explicit address-based interpretation. The initially reported program
change was a labeling error, not evidence of program uploads.

Capture is not replay qualification. Repeated submission needs a checked VIF,
VU-data and GS startup capsule, a fixed target and real final completion. In
particular, the native frame omits init-only VU data and BASE/OFFSET. A bare
MSCAL to the draw-finish helper is unsafe: it XGKICKs the payload at TOP+10;
the actual `Path1::addDrawFinishTag` must supply its valid GIF packet first.
This helper cannot be used as an unprepared double-buffer toggle. Extra capture
allocation and metadata change layout/cache state, so V4 timings are not V3
prices or an ordinary gameplay optimization.

## Terminal replay qualification

The private V5b prototype captures a live closure in the same ELF, selects its
actual captured draw target, restores all 16 KiB of VU data and the observed VIF
double-buffer state, then repeats the native chain. The SDK draw-finish helper
supplies a valid payload before MSCAL. Every repetition waits for both helper
and native GS FINISH. Ordinary rendering never resumes.

PCSX2 completes 128 warm and 128 sampled repetitions: 512 hardware starts in
the loop, no software or direct submissions, and exact startup data/register
and resident program comparisons. These are protocol checks. The interval
also includes full EE VIF validation of the roughly 2.4 MB closure on every
submission; it is not a consumer-only or GPU-only timer.

The first V5 runtime was rejected by the SDK's packet alignment assertion.
With TTE enabled, opening a DMA tag advances the writer by 8 bytes. Sixteen
following 32-bit state words leave its cursor misaligned; two trailing NOP
words produce an aligned 80-byte CNT record with QWC 4. The narrow V5b fix
changes one of 496 compiler inputs. An independent SDK host check reproduces
the old assertion and exports the corrected 13-QW helper; its bytes match the
actual target export. The host check uses installed inline headers and an
upstream external implementation, so target bytes remain authoritative.

Raster equivalence is a separate gate. One terminal PrintWindow image
was mostly black, while another unchanged-ELF boot showed the complete scene.
Neither establishes equality with the original frozen frame. A new private
V6 fixture reads the ordinary and replay framebuffers directly from GS VRAM,
outside all timed windows. The [replay record](tyrax2-frame-replay-2026-10-03.json)
qualifies both PCSX2 and physical PS2: each pair contains identical 448-by-448
rasters, zero RGB differences and zero differences in exported working alpha.
Both complete the 256 repetitions, 128 samples and real FINISH checks. Alpha
is doubled and saturated to 255; raw alpha values above 127 are not distinguished.
Terminal VIF/VU snapshots precede the final image readback.

This compares the last ordinary frozen frame after 512 loops with replay of
the warm-126 capture, rather than assuming they match. No cross-environment
pixel equality is claimed. V6 adds no guessed GS restore and keeps the V5b
timed loop unchanged. The next same-ELF control must price repeated full
validation against prevalidated immutable ownership before using replay timing
to select a bottleneck. Physical reset/start succeeds here; permanent reset
reliability and ordinary gameplay gains remain unproved.
