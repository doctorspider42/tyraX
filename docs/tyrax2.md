# TyraX2 frame pipeline

TyraX2 is the staged migration toward preparing frame N on the EE while VU1/GS
execute frame N-1, with explicit ownership of every submitted resource.
Version 1.169.1 implements one complete VIF1 chain per ordinary frame, built
natively and terminated once, including the final presentation FINISH.
Texture uploads, GS state and auxiliary passes are represented in order.
Explicit same-frame GPU reads, compatibility
operations and bounded-memory overflow may drain through a measured fallback.
Preferences > Display > **TyraX2 frame pipeline** controls ordered frame
recording and N/N-1 execution. Version 1.171.0 enables the request for new
projects. Format v95 always saves `settings.framePipeline`, including `false`;
older manifests without the field retain the ordinary queue and allocate no
frame banks. Generated initialization explicitly calls
`RendererCore::setFramePipeline(true)` or `false` for either project setting.

## New-project default and legacy preservation (1.171.0)

A missing request in format v94 or earlier remains false. A missing request in
v95 defaults true; an explicit boolean wins in every supported format. Saving
always writes the effective request, so a legacy project resaved as v95 remains
off and an explicit opt-out survives reopening. Legacy section-transfer blobs
without a format stamp or request also remain off. No destructive migration is
required. The engine compile-time default stays zero: generated projects make
the request after initialization, and standalone engine clients keep their
existing behavior.

The request still falls back synchronously for field rendering, active BLSS,
unlimited triple buffering and unavailable pipeline banks. New-project defaults
do not establish effective overlap, universal 60 FPS or button-to-TV latency.
The historical acceptance records below describe their original opt-in builds;
the default policy and example qualification are implemented; the physical
post-Memo quiet/Showcase reverse controls remain open. The existing
[ordinary-clock quiet record](tyrax2-quiet-cadence-2026-10-03.json) measured about
30 FPS in its authored pre-Memo night configuration; it is not a post-Memo
result or 60 FPS acceptance. Physical button-to-TV latency remains unmeasured.

## Portal interaction qualification (2026-10-03)

Fresh debug builds with the integrated engine completed actual pad-driven
portal circuits in PCSX2. Showcase entered its cellar and returned through
the paired portal, then completed Grand Tour and returned camera control.
A same-ELF replay completed 22,408 frames with zero divergences. The small
portal example completed pickup, both portal crossings while carrying the
sphere, and throw; its same-ELF replay completed 4,360 frames with zero
divergences. Complete canonical recordings passed the actual host reader,
declared-frame-count, CRC, terminal/footer and no-trailing-data gates. A
recoverable replay prefix alone is not complete-recording acceptance.

The small example initially stopped at an authored frame bar. Its standing
player box collision path uses yaw only: a tall box rolled by 90 degrees
looked horizontal but retained a vertical collision blocker. Expressing the
two overhead bars as unrotated horizontal boxes preserves their plain-color
rectangular geometry and leaves the side pylons collidable. This is a map
content correction, not a renderer or general collision algorithm change.
The original failed circuit and the corrected circuit are retained separately.

These debug interaction and replay checks do not establish shipping-profile
hardware FPS, audio quality, pixel identity on every transition frame or TV
latency. Broad native/example boot checks and the final generated release
source comparison are separate qualifications.

The [1.171.0 qualification record](tyrax2-release-2026-10-03.json) now contains
44 native example builds and 44 independently reviewed emulator boots/logs/
actual images. All checked-in examples explicitly request the pipeline and
have fresh generated sources. The source comparison checks 42 original
references, the separately built corrected Portal release, and a fresh Cube
build preserving two authored negative-zero prefab angles. Docker project
names are verified against the actual directory-derived generator formula.
Generated built-in font/HUD resources match the tested copies. The original
strict comparison rejection is retained, rather than hidden by broad path or
float normalization. Corrected Portal release additionally reached 4,800
reported frames and its automatic object cycle; this does not transfer the
debug player replay or physical performance claim to that release binary.

## Acceptance order

1. Archive current physical-PS2 day/night seated-start controls. Preserve the
   authored camera, video mode and quality; turn off host polling. Record mean,
   median, p95, over-budget frames and complete frame period separately.
2. Validate complete DMA/VIF chains before submission, independently of timing.
3. Introduce frame-owned packets/copy pools while retaining current submission.
   Establish RAM ceilings, reclaim fences and a safe overflow fallback first.
4. Give uploads, GS state and auxiliary render targets an explicit ordered
   representation, covering static/dynamic pipelines, HUD and screen effects.
5. Record one native ordered frame chain; do not fuse finished packet buffers
   by removing END tags or linking them with NEXT. Validate its complete extent
   and all referenced ownership before submission.
6. Enable N/N-1 execution experimentally. Compare input latency, RAM/VRAM,
   pixels, frame tails and streaming/scene transitions against the controls.

## Ownership seams for the arena stage

| Resource | Current protection | Required frame-arena contract |
| --- | --- | --- |
| StaPip packets | `packetSequence[context]` is waited before rotating into a reused buffer in `sendPacket` | Immutable packet slices until their final DMA reader completes; retain current send ordering first |
| QBuffer copied vertices/ST/colour/normals | `stapip_qbuffer.cpp` rotates one pool side per packet buffer | Allocate slices from the same submission owner as the packet; reclaim packet and payload together |
| MVP/light/colour uniforms | Queue mode copies transient values inline | Snapshot at recording time; never borrow mutable renderer state |
| Retained/baked command arenas | Content/version keys plus delayed destruction | Pin referenced entries to real completion, including replacement and scene unload |
| VIF1 HUD chains | `chainSeq[chainSide]` protects two alternating buffers | Keep DIRECT data alive and ordered behind the relevant 3D passes |
| Texture allocations/render targets | Mid-frame fences and GS state caches | Pin VRAM until the final GS reader, order upload/eviction and raster redirects explicitly |

DMA completion permits reuse of an EE source buffer; it is not proof that GS
has stopped sampling a VRAM texture. Conversely, counting two frame ticks is
not a completion fence. The arena stage must keep those lifetimes separate.
Do not broaden an allocation's lifetime merely by increasing queue depth:
depth 8 already regressed, and copy-pool memory scales with that depth.

The previous [architecture investigation](ee-submission-rearchitecture.md#a-frame-pipelined-engine-tyrax2---considered-2026-09-24-not-now)
records the hazards. Its older sweep's overlap estimate does not quantify the
current seated start. Fresh GPU-hold results are diagnostic segment measurements,
not ordinary frame times or an exact prediction of pipeline gain.

## Experimental chain arena (2026-10-02)

`TYRA_FRAME_CHAIN_ARENA=1` snapshots complete linear TTE packets and mutable/unowned
nonempty REF payloads into a bounded 1 MiB allocation. It preserves the original
tag structure and independent END terminators; it never joins chains with NEXT.
Producer packet/pool buffers still number four, while queue metadata can retain
128 independent chains. A snapshot releases the original packet and arrays for
reuse immediately; its returned DMA sequence still guards ordered consumers.
HUD keeps that sequence for its PATH3 fence even when its source buffer is free.

Engine-owned baked VIF streams can register an immutable span. References to
these bytes stay borrowed instead of being duplicated each frame; retirement
marks the span unavailable for new borrows and keeps its storage until both
bank-reader masks have cleared after actual DMA completion. Immediate unregister
still drains before destructive debug poisoning. The bounded
512-span table falls back to copying when full. Its hot lookup uses binary
search over disjoint sorted ranges; a linear full-table scan regressed the
first pinning arm despite avoiding ~2.15 MiB of duplicate data per frame. Unregistered user arrays,
mutable copy pools and transient state retain snapshot ownership. The cache's
historical two-frame graveyard alone is not the new lifetime guarantee.

The arena is reclaimed only after `drain()` has observed actual DMA completion.
If a snapshot will not fit, it drains previous readers and retries once. A
single oversized/unsupported packet or allocation failure retains ordinary
submission and source-buffer waits. Preflight checks source references and the
entire required extent before modifying storage. This does not change GS/VRAM
lifetimes, auxiliary-pass ordering, or presentation. It is the ownership stage,
not N/N-1 execution or an accepted optimization.

`FRAMEARENA` reports allocation, high-water bytes, overflow drains, fallback
counts, maximum copied/borrowed bytes per frame and remaining heap KiB. The first
1 MiB/libc-copy hardware arm was correct but regressed warmed night work to
34.016 ms versus the 19.912–19.918 ms control. It is not accepted. A 3 MiB
arm removes overflow drains: its first warmed night window is 33.032 ms,
with ~2.67 MiB maximum copied bytes per frame and ~9,989 KiB free heap.
Capacity alone recovers about 0.98 ms but leaves the copying regression. The R5900 quad-copy
arm uses aligned LQ/SQ loads/stores; guard builds compare each actual copied
span byte-for-byte before publishing it. Host memcpy checks alone cannot
verify that assembly arm. The 3 MiB quad-copy timing arm reached 29.604 ms,
still a regression. A correctness boot verified every copied span on actual
hardware across 200,704 accepted chains without rejection or fallback;
indexed pinning passed 565,248 hardware chains without rejection. The first indexed pinning
window reached 20.879 ms: ~0.96 ms above the control, with unchanged authored
pose/video/quality. It is an ownership experiment, not an accepted gain.
[The exploratory record](tyrax2-arena-2026-10-02.json) retains the successive
arms, hashes and known archive limits. `tools/verify-frame-arena.cpp` checks copied REF rebasing, independence
from later source mutations, independent snapshots, bounded overflow, rollback,
and unsafe/unsupported tags. Run it with the same host compiler/sanitizers as
the chain validator. Use `--mode arena-check` and `--mode arena-timing` fixtures
for separate correctness and hardware timing arms. These diagnostic switches stay off in ordinary generated games. The project
preference controls the runtime request independently of these historical diagnostic switches; new-project and legacy defaults follow the policy above.

## Native frame writer prototype

Native recording reuses the root-tag count from successful arena preflight.
`copyCounted()` supplies separate 16-byte EE metadata; public `Snapshot` remains
12 bytes and ordinary `copy()` does not increment a per-tag counter. The queue
selects counting only for an eligible owned native copy, reevaluates eligibility
after a drain/retry, and consumes only successful-copy metadata. The existing
scan remains when no counted copy is available. Capacity, emission, rollback,
sequence and completion fences are unchanged. Detailed `NativeSizing` spans
describe actual remaining scans; an elided scan has no synthetic timing span.
[Paired private physical controls](tyrax2-ee-vu0.md#native-sizing-reuse-paired-physical-candidate-controls-2026-10-03)
show a small net improvement; final production timing is a separate check.
`tools/verify-frame-arena.cpp` also checks mixed CNT/REF/END counts, zero-length
REFs, REFE termination, copied/borrowed byte parity and failed-copy rollback.

`frame_vif_writer.hpp` constructs clean CNT/REF records from VIF operations,
reserving one END that only `finish()` writes. It has explicit unsubmitted
checkpoints/rollback and refuses unsafe references and overflow. It does not
link prebuilt DMA buffers or patch END into NEXT. Its portable harness compares
the emitted ordered command/data words against an independent stream oracle,
including UNPACK, DIRECT and MPG records. `TYRA_NATIVE_VIF_RECORD=1` now routes owned queue inputs through those native
operation records, with one END per ordered VIF segment. Existing foreign-path
barriers still flush segments, so this is not one chain per full frame or N/N-1.
Logical source sequence numbers are retired by the actual batch completion
sequence, not by incrementing once per DMA. Sequence zero remains a no-fence
sentinel across wrap. Prefix reuse waits its real reader; overflow rewinds the
unsubmitted operation and flushes before retrying. Finalized native prefixes
force the appropriate lazy/eager write-back after recording. The segment guard accepted over 577,000 source chains on physical PS2.
The later ordered-frame guard ran over 6,700 frames: its 12 split frames occurred
during cold startup, with one batch per warmed frame thereafter.

## Ordered frames and overlapping execution

`TYRA_ORDERED_FRAME` is a diagnostic compile-time gate; the project preference
uses the runtime API without modifying engine headers. Engine GIF senders record
owned DIRECT operations in the same stream as static/dynamic/Minecraft VIF
commands, program uploads, render-target brackets, HUD and post effects. FLUSHA
orders VU1 and GIF paths in the recorded stream. Ordering-only CPU waits become
recorded barriers. Intermediate FINISH packets become EOP-only packets; the final
frame-end FLUSHA/DIRECT records the sole presentation FINISH inside that same
chain. The CPU consumes its event after DMA completion, rather than issuing a
second VIF1 draw-finish chain. Initialization and foreign
calls outside recording retain ordinary DMA behavior.

Two bounded banks each own 1 MiB of mutable source snapshots and a 128 KiB native
prefix. Inline snapshot payloads become REF operations into their owning bank,
avoiding a second copy. Baked immutable streams use a sorted lease registry;
retirement excludes new borrows and defers destruction until every bank reader
completes. Two graveyard slots organize collection, but age alone releases nothing. A bank is reused
only after its actual DMA sequence completes. The pending frame is completed and
presented before the new frame starts, including an overflow prefix. During that
handshake the unsubmitted new bank is protected from drain/reset, and its HUD
fence latch is suspended. Otherwise presenting the old job waits for the new
HUD prefix whose submission the callback itself blocks. The oversized DIRECT
regression fixture reproduces this deadlock before the fix and repeatedly
completes 9,000-qword borrowed transfers afterward.

The EE records using the next frame's logical draw context while the GPU still
owns the previous target. The presentation callback restores the previous job's
context and hands off to the anticipated target before the new chain starts.
Hybrid keeps its shared draw target and orders its presentation copy before the
next clear. True field rendering, active temporal upscaling and unlimited triple buffering
retain synchronous compatibility; warp presentation, display changes and permanent VRAM rebuilds
complete the pending job first. Between-frame GPU readbacks call
`renderer.core.synchronizeFrame()`; generated debugger captures already do so.
Low-memory refusal frees any partially allocated banks and keeps the ordinary path. A direct borrowed fallback completes
the previous presentation even when no prefix has been recorded yet, orders
its transfer after a completed recorded FLUSHA (including the VIF FIFO), and
waits its actual DMA before returning to a producer that may reuse the bytes.
Overflow, unsupported sources and immediate destructive unregister can split a
frame through a real synchronous fence. Ordinary baked-cache retirement no longer
flushes a frame; retired immutable storage retains its bank leases. These are explicit exceptions to one chain per ordinary
frame, not a promise that cold asset loads cannot split.

A first N/N-1 hardware guard accepted 98,304 source chains; the REF-prefix guard
accepted 204,800. Its 600-start counter recorded 576 starts with prior VIF DMA
still busy. Software-renderer captures retain the car, lights, shadows and HUD;
the measured scene ROI differed by less than 0.01/255 mean per channel from the
segment control. These are predecessor measurements; the completed runtime
acceptance and remaining hardware repeats are recorded below. The early timing
arm retained a ~33.37 ms night period;
its work counter mistakenly included prior presentation pacing and is archived
as such, not as active render work or a performance win.

Enabling this path costs about 2.25 MiB of EE RAM and queues one rendered frame
of latency. Timing must report full presentation period alongside recording work,
subtracting presentation pacing without treating the previous GPU tail as free.
Keep the independent chain guard off in timing builds. Use `ordered-check` /
`ordered-timing` and `pipeline-check` / `pipeline-timing` fixture modes to isolate
ordering and overlap respectively. Physical-console timing establishes performance;
PCSX2 establishes correctness only. The final `pipeline-*` fixtures enable
the public project preference with diagnostic recording defaults zero; other
fixture modes explicitly disable that preference. Initial boot remains synchronous;
later loading synchronizes pending presentation before its GPU handshakes. Foreign GPU transfers between frames
complete the pending presentation first; they cannot overwrite a previous
job's programs or targets while it is still executing.

## Runtime acceptance (2026-10-02)

The 2026-10-02 builds were experimental and off by default. Current new-project
and legacy behavior is described above. **Project > Preferences
> Display > TyraX2 frame pipeline** changes the saved request; rebuild the game.
The request
survives save/reopen; `getFramePipeline()` reports the requested setting, even
when the current display mode takes a synchronous compatibility path. This
version does not introduce the reference title's SPR staging scheme.

The [runtime record](tyrax2-runtime-2026-10-02.json) separates final-candidate
results from predecessor experiments and includes local artifact hashes:

| Physical PS2, seated day start | Baseline, two boots | Corrected 1.169.1, two boots |
| --- | --- | --- |
| Mean render critical-path work, 512 frames | 15.707 / 15.721 ms | 13.863 / 13.865 ms |
| Work above 16.667 ms | 8 / 7 of 512 | 0 / 0 of 512 |
| Mean complete presentation period, neighboring 500-frame window | 28.061 / 28.095 ms | 17.152 / 17.185 ms |
| Rate derived from that period | approximately 35.6 Hz | 58.30 / 58.19 Hz |

Critical-path work excludes presentation pacing but includes any unhidden
previous GPU tail; it is not pure EE computation. Full period is the performance
result. The raw work and neighboring period windows are different sample sets.
Each pose's corrected repeats use the SAME archived ELF after fresh physical
power cycles. Corrected night boots measured 18.114 / 18.173 ms critical-path
work, versus baseline 19.918 / 19.912 ms, but both retained 33.403 ms periods
(29.94 Hz). All 512 night work samples in each boot still exceeded 16.667 ms.
This reduces critical-path work without raising night FPS. Emulator FPS is not
PS2 timing. The experimental runtime's physical timing and repeated scene
transition gates passed for those builds. Their original release record left
input-to-display latency and promotion open; the new-project policy above does
not overwrite those historical measurements.

### Physical button-to-TV latency limitation

The historical latency fixtures used an opt-in renderer configuration. Real
button-to-TV latency remains unmeasured; synthetic event-to-display-register
results below document the measured tradeoff without claiming TV photons.
Existing Pad/Game/Present scopes and frame counters do not map an input edge to
the actual displayed buffer. Active submission may present the previous job,
and triple-buffer flip can queue a buffer for a later vblank. Requested pipeline
state also does not establish effective overlap in compatibility modes.

The prepared private test plan uses one parked-scene ELF with pipeline
off/on/restored-off blocks and a persistent high-contrast sprite toggled by a
reserved physical pad button. Track edge/game identity through native prefixes,
the pending job and the buffer selected for display; store bounded events in
RAM without new waits, readbacks or per-event prints. Start with progressive
two-buffer output, limiter enabled and no BLSS/synthetic presents. Record actual
active state, cadence and instrumentation overhead; exclude setup and mode
boundaries. Software establishes observed-input-to-display-register identity
and delay, while an external recording is required for button-to-TV-image
latency. Do not infer a fixed millisecond penalty from N/N-1 alone.

The [preparation record](tyrax2-latency-plan-2026-10-02.json) retains source hashes
and the detailed original private plan. A subsequent private implementation
passes 6,421 actual-header host checks and native compilation. It tags input/game,
prefix, pending job, FINISH and actual displayed-buffer identity in a bounded
event ring, retaining existing fences. The first built same-ELF off/on/off trial
uses explicit synthetic marker toggles for unattended software mapping; manual
Square input is a separate source-default mode. Normal progressive two-buffer
output, plain order and observer/marker settings are matched across blocks.
After the user unlocked the emulator, both PS2 and PCSX2 completed all 5,400
game loops. V1 software mapping was rejected: the observer assumed that the
synchronous off path queued a native prefix, while the actual default
ORDERED/native/arena flags are zero. It consumed an absent tag in the first
off block and a stale tag after returning from on to off. The console remained
healthy. Host checks had modeled the wrong off path and did not establish
actual target hook coverage. A private V2 observer uses the current render tag
for legacy synchronous presents and clears queued identity each render revision.
The [fixture record](tyrax2-latency-fixture-2026-10-02.json) retains the original
build hashes and rejected logs. V2 passes 36,241 actual-header legacy-off and
36,345 ordered-off host checks, native compilation, and all 39 synthetic edge
mappings across the full 5,400-loop emulator run. Cold phase-zero renders
(24 extra) and phase-one prefixes (five extra) are separate from input polls;
all blocks have zero invalid/dropped events. A separate initial-block snapshot
shows the white marker and normal scene. Emulator numerical delays are not
physical latency evidence. The fresh physical V2 trial also passes all 5,400
loops and 39 mappings, with no invalid/dropped events or hang. Synthetic
EE-event-to-display-register medians off/on/restored-off are
33.276 / 66.623 / 33.276 ms (13 events each). Means are 34.551 / 66.621 /
33.267 ms; the first off block includes one 49.960 ms sample. The on median is
about 33.35 ms later than either off control in this instrumented parked night,
progressive two-buffer configuration. This is not a universal fixed penalty:
physical button/TV photons, independent observer overhead and other output
modes remain separate acceptance gates.
These source/host/native gates did not establish button-to-TV latency or justify
the default policy by themselves. The current new-project default is described
above; physical button-to-TV latency remains unmeasured.

The morning physical guarded drive reached 2,160 recorded frames and 98,304
accepted source chains, switched to the dense scene and exercised cameras
0/1/2, then stalled while loading procedural. It had two split frames and zero
direct fallbacks. This pre-fix acceptance run FAILED despite valid source chains.
Quiet register diagnostics subsequently reproduced the direct-send ordering
race, corrected in 1.169.1; the repeated physical controls below close that
blocker. Preserve this failed run and earlier pre-gameplay `freepad: DMA Busy`
launches as separate historical evidence. The emulator's completed transition
test alone did not establish hardware acceptance.

Windows editor, native PS2 and Docker PS2 builds passed. Windows host checks and
Linux ASan/UBSan passed 1,074 arena cases plus 10,000 malformed streams and 43
native-writer cases against an independent ordered-word oracle. PCSX2 software
renderer checks cover static, dynamic and Minecraft pipelines; day/night vehicle
frames; scripted acceleration, steering, braking, reverse and camera changes;
all three Motor District scenes; display changes including field, PAL and
1080i; limiter and runtime toggles; allocation refusal; foreign 2D/3D handshakes;
and oversized 9,000-qword fallback transfers. The final guarded driving/scene
run reached 3,360 frames and 233,472 accepted source chains with no rejection,
four split frames and zero direct fallbacks. Its final shot is from the driven
first-person camera, rather than the parked benchmark camera.

A captured native prefix contains exactly one END and one DIRECT FINISH in
its final operation. Public generated-game Live Debugger readback also completed
with the runtime preference enabled. The disabled preference allocated no frame
banks; the day scene ROI comparison differed by less than 0.015/255 mean per
channel, with small timing/animation differences. These correctness checks do
not establish physical input-to-display latency. The completed physical
repeats are recorded separately above.

## Pre-submit chain guard

`TYRA_VIF1_CHAIN_CHECK=1` enables the guard in `Vif1Queue::submit`. Both StaPip
and the VIF1 HUD supply their complete packet size, including the terminator.
The default is 0: ordinary builds do not scan submitted payloads.

The bounded decoder accepts linear TTE chains containing CNT, REF, REFE and END.
It rejects missing/early terminators, inline overruns, unsafe reference ranges,
unknown VIF commands and unfinished command payloads before starting the chain.
Unexpected DMA tag IRQ/PCE controls are rejected too: TIE is enabled
on this channel, so a stray IRQ bit could terminate a transfer early.
Reserved bits 16–25 are ignored, matching the hardware/SDK contract:
`packet2_chain_set_dma_tag` assigns named bitfields without clearing padding.
Requiring zero padding falsely rejected a real night-scene packet during the
first control-bit guard boot; the host harness includes that regression case.
Traversal is bounded to 65,536 packet quadwords and 1,048,576 payload quadwords
(16 MiB including repeated REF reads), so malformed chains cannot expand into
unbounded diagnostic work.
It handles UNPACK widths, NUM=0, STCYCL fill/skip counts, IRQ command bits, MPG,
DIRECT/DIRECTHL and fixed-size register payloads. Each chain starts decoding at
CL=WL=1; current UNPACK writers establish STCYCL explicitly. A future writer
depending on inherited non-default cycle state needs an explicit initial-state
contract before using this validator.

NEXT/CALL/RET, REFS stall control and scratchpad references are deliberately
unsupported. Adding them requires bounded traversal and explicit resource
ownership; a validator must not follow arbitrary addresses in a broken chain.
The EE resolver permits only aligned ordinary RAM spans below 32 MiB. This is
a range check, **not an allocation/lifetime proof**. The caller still guarantees
the packet's readable extent; frame arenas must eventually establish ownership.
The guard does not validate GIF semantics or GS image correctness, and cannot
detect later writes, stale cache data or missing inter-path barriers.

Accepted-chain counts appear as `VIFCHECK accepted`. Rejection reports an error
enum, packet tag offset, last VIF command and pending word count, then parks
the EE thread without submitting the rejected chain. This is a correctness arm,
never a performance arm, even when all chains pass.

Run the portable malformed-input harness from the repository root:

```sh
g++ -std=c++17 -Wall -Wextra -Werror tools/verify-vif1-chain.cpp -o /tmp/vifcheck
/tmp/vifcheck
```

On Windows use the same compiler command with an absolute `.exe` output outside
the checkout, then run that executable. The harness includes truncated and
out-of-range chains, real command payload layouts and 10,000 malformed streams.

## Reproducible physical-console fixtures

```sh
python tools/tyrax2-fixture.py /absolute/scratch/day --pose day --mode timing
python tools/tyrax2-fixture.py /absolute/scratch/night --pose night --mode timing
```

The script copies the current example and engine outside the repository, leaves
the seated start intact, selects the authored district-night default and disables
Remote Pad, Live Debugger, Live Link/Logic, Time Machine, input recording and the
memory HUD. It records settings, source asset/engine hashes and source revision.
It refuses existing destinations. Run `--refresh-gen` on the copied `game`, then
build it through `tools/toolchain/native-build.ps1`/`.sh` using the copied `tyra`
and a dedicated cache. Use the usual [resident-IOP marker and deployment rules](ps2link-setup.md).

`--mode plain` is the uninstrumented transparency control; `timing` selects
`TYRA_FRAME_PROFILE=2`; `check` selects the chain guard; `hold` enables the
existing GPU-hold probe at queue depth 80. Hold deliberately serializes segments,
can alter memory pressure and omits some intermediate GS tails; its frame work
must never be quoted as shipped performance. A held-frame overflow invalidates
its measurement. Keep correctness, hold and timing captures separate.

Archive stdout, final ELF/resource hashes and the refreshed manifest after each
build. Compare the same warmed frame range and camera, with repeat boots; source
revision alone does not capture uncommitted changes. NTSC and PAL have different
budgets (16.667 and 20 ms); `FRAMETIME over20` is not an NTSC acceptance count.
No gain is claimed until a production candidate has passed these gates.

For an exact 512-frame warmed window from the current timing producer:

```sh
python tools/tyrax2-timing.py /scratch/day-boot1.log --first 1100 --frames 512 --hz 60
```

The parser joins ps2client's inserted tty-packet newlines, including splits
inside hexadecimal words, but preserves original token spaces. It requires
complete 64-value FTRAW records, rejects duplicate frames and refuses missing
samples in the requested window. The raw buffer dumps at a 50-frame summary
boundary after filling 512 slots; subsequent blocks start at frame 550, 1100,
1650, etc. Do not treat the gaps between blocks as measured data. Summaries
use nearest-rank p95 and the authored Hz budget. Full period and missed-field
counts still come from FRAMETIME, not from FTRAW active work.

## Initial physical-PS2 controls (2026-10-01)

The current seated garage start was booted twice per pose, with original NTSC
progressive/hybrid video settings, ordinary quality and host polling off.
Both boots use the same ELF within each pose. The warmed raw window is
frames 1100–1611 (512 frames):

| Pose | Mean work, boot 1 / 2 | p95, boot 1 / 2 | Work above 16.667 ms, boot 1 / 2 |
| --- | --- | --- | --- |
| Day | 15.707 / 15.721 ms | 16.389 / 16.304 ms | 8 / 7 of 512 |
| Night | 19.918 / 19.912 ms | 20.525 / 20.519 ms | 512 / 512 |

The matching FRAMETIME windows put the night period at 33.37 ms; the day
also misses many NTSC fields despite most render-work samples fitting 16.667 ms.
Simulation/input (`pre`), presentation and vsync are part of the full period.
These are baseline controls, not an optimization result. No plain-build
transparency or pixel-equivalence acceptance is claimed yet.

[The machine-readable record](tyrax2-baseline-2026-10-01.json) includes ELF hashes,
per-window periods and repeats; complete stdout and scratch builds remain in
the recorded local artifact directory. A later candidate must preserve the
same pose and include full-frame and visual gates as well as these work samples.

The first hardware guard build accepted 110,592 chains without rejection.
Subsequent payload-budget/control checks passed 32 host cases and 10,000
malformed streams on Windows and Linux ASan/UBSan. After correcting the
reserved-bit false rejection, the final guard reached the physical-PS2 night
scene and accepted at least 94,208 chains without rejection; the
archived stdout snapshot and ELF hashes are in the record. The GPU-hold launch stopped before
gameplay with `freepad: DMA Busy`, twice including after a user-confirmed
physical power cycle. It produced no usable GPU measurement. The pad initially
reported ready; no root cause is established. Do not infer pipeline performance
or classify this as a render-chain defect from the tty symptom alone.

Minecraft's program cache is now initialized after binding the renderer, rather
than dereferencing an uninitialized renderer from its constructor. This is
required to exercise its ordered uploads and pipeline switching safely.

The FINISH event is owned by the pending job until presentation consumes it.
Ordinary `RendererCoreSync::clear()` first completes that job, then clears for
its new handshake. During recording it preserves the old event. Synchronizing
only at a later physical send is too late: a prior clear could lose the event,
as the physical loading regression demonstrated. Explicit foreign 2D/3D
handshakes are covered by the mode fixture.

Further physical loading diagnostics on 2026-10-02 reproduced a startup hang
after a fresh power cycle, before gameplay activated the first watchdog.
A scratch build with startup watchdogs and loading-stage prints then completed
12,000 frames, all three scene transitions, all three cameras and 663,552
accepted source chains, with zero direct fallbacks or watchdog alarms. This is
diagnostic evidence only: the prints may hide a timing race. At that stage no
production fix or root cause had been established, and the earlier loading
failures remained qualification blockers. A quieter scratch build retains the
loading stage in RAM and prints registers only after prolonged waits. Its first soft-reset
launch stopped before gameplay with `freepad: DMA Busy`; a physical power cycle
was required. On its subsequent fresh physical boot, the quiet control hung
during the procedural load with VIF1 DMA still active and repeatedly identical
registers: CHCR `300001c5`, TADR `0092d9a0`, QWC `0000000f`, VIF STAT
`0e0000ca`, GIF CHCR `00000081`, GIF STAT `00000e00`, GS CSR `551c600c`.
The last completed ordered-frame summary was 2040; only the dense transition
had returned. The runtime record preserves the ELF and log hashes.

The ordering adapter has a concrete candidate race: a producer pre-waits GIF,
then `frameSendPacket()` synchronizes the previous frame, whose hybrid present
can start GIF DMA again. Sending immediately after that callback can overwrite
a busy channel. A scratch candidate adds a channel wait after synchronization,
immediately before the direct send, including unsupported recording fallbacks.
The correction is implemented in 1.169.1 without a project-format change.
The same quiet watchdog control then passed 10,920 frames, all three scene
transitions and cameras, with 671,744 accepted source chains and zero direct
fallbacks; the user confirmed normal car, lights, shadows and HUD on the PS2.
A subsequent physical build without watchdogs or loading-stage prints passed
10,440 frames and five complete scene cycles (15 transitions), with 765,952
accepted chains, zero rejection and zero direct fallback. Its 17 split frames
include bounded loading/driving overflow; this stress result does not claim
one chain for every exceptional frame. Mode/limiter/pipeline changes and
foreign 2D/3D handshakes also passed again in PCSX2 2.9.93. These controls close
the reproduced loading-race blocker. Renewed ordinary day/night timing then
passed two fresh physical boots per pose using the same ELF within each pose;
the table above reports the corrected results. Preserve the earlier failed
attempts as historical evidence. The final timing log is an immutable snapshot;
ps2client remains serving the last night game so ending measurement does not
strand its next host-file access.

## Night workload isolation (physical PS2, 2026-10-02)

The stationary Motor District garage night was tested in one fresh physical
boot with the corrected 1.169.1 engine. A scratch game removed one group at a
time, reloaded the same scene, and restored full night at the end. Each phase
ran for 2,200 measured gameplay frames; its warm 512-frame work window started
1,100 frames into the phase. The ordinary vehicle pose and camera were retained;
no driving was injected. Native compilation and a separate PCSX2 2.9.93 run
through all seven phases passed before accepting the hardware record.

| Variant | Mean work (ms) | p95 (ms) | Mean period (ms) | Delivered rate (Hz) |
| --- | ---: | ---: | ---: | ---: |
| Full night, before | 18.377 | 18.955 | 33.403 | 29.94 |
| No projected scene shadows | 18.389 | 18.991 | 33.403 | 29.94 |
| No scene light pools or beams | 16.507 | 16.941 | 33.370 | 29.97 |
| No registered live dynamic lights | 17.318 | 17.928 | 33.370 | 29.97 |
| No night dressing | 17.635 | 18.244 | 33.370 | 29.97 |
| No vehicle headlight ground pools | 18.357 | 18.974 | 33.403 | 29.94 |
| Full night, restored | 18.574 | 19.194 | 33.403 | 29.94 |

The full-night controls differ by 0.197 ms. Relative to those two controls,
removing scene pools/beams saves **1.870–2.067 ms**, registered live lighting
**1.059–1.256 ms**, and the eleven visible night-dressing boxes
**0.742–0.939 ms**. These are observed control ranges from one sequential boot,
not confidence intervals or additive costs. Projected shadows and the vehicle's
ground pools show no gain larger than the control spread in this stationary
view. This does not establish their cost while driving or in another scene.

The live-light arm clears the engine's dynamic-light registration after the
ordinary light update, before object lighting and rendering. It removes both
CPU `dynLightAt` pickup and per-bag selection of registered lights, while
retaining effect on/level state, baked lighting, and the separate camera spot.
The pool/beam arm skips their render/update functions; the dressing arm hides
only the eleven project-owned boxes. The vehicle arm retains emissive lamp
materials and glow, skipping only the headlight ground patches. Projected scene
shadows are a separate pass, not a vehicle-headlight shadow-map implementation.

No individual removal reaches the next presentation rung. Even the pool/beam
arm retains about **1.574 ms of game update** alongside its 16.507 ms renderer
work. Work measures the renderer's critical path excluding presentation pacing,
including unhidden GPU time; it cannot identify pure EE versus GPU cost.
The period is a separate average of ten neighboring 50-frame windows. Ordinary
automatic interleave selection remains active, so these results characterize
the actual runtime rather than a fixed routing microbenchmark.

The fixture completed all seven phases on PS2 and returned to normal night;
the frozen log's latest arena summary reports zero direct fallbacks. Loading
splits remain exceptional frames outside the warm timing windows. The
[machine-readable record](tyrax2-night-isolation-2026-10-02.json) contains the
ELF/log hashes, exact windows, source hashes, preparation/analysis scripts and
emulator evidence. The host server remains alive for subsequent file access.

The next optimization target is scene pools/beams: separate those two passes,
then measure receiver search, geometry rebuild, submission and overdraw before
choosing caching or batching changes. Registered live-light selection follows.
These measurements support investigating the night workload; they do not
justify removing visual effects or claiming a 60 Hz night fix.

`tools/tyrax2-timing.py` also recognizes the explicit unprefixed renderer
counter records appended after a raw block by ps2client tty fragmentation.
It still requires exactly 64 valid words per raw record, rejects duplicate or
missing frames, and rejects unknown trailers. Eleven synthetic boundary and
strictness controls passed; the archived final-night baseline remained identical.

## Separate pool and beam costs (physical PS2, 2026-10-02)

A second scratch fixture separates scene pools from beams/coronas. Five
2,200-frame phases retain the same stationary car/camera and scene reloads,
with warm 512-frame windows at phase*2200+1100. All phases passed a native
build, a separate PCSX2 2.9.93 run and a fresh physical PS2 boot. This fixture
also times the effect functions and their nested `stapip.core.render` calls
with COP0 counters, so its absolute work is diagnostic, not a new uninstrumented
production benchmark.

| Variant | Mean work (ms) | p95 (ms) | Mean period (ms) |
| --- | ---: | ---: | ---: |
| Full night, before | 18.345 | 18.887 | 33.403 |
| No scene pools | 17.328 | 17.610 | 33.403 |
| No scene beams/coronas | 17.575 | 18.216 | 33.370 |
| Neither group | 16.511 | 16.839 | 33.370 |
| Full night, restored | 18.459 | 19.015 | 33.403 |

The controls differ by 0.114 ms. Pool removal saves **1.017–1.131 ms**;
beam/corona removal **0.770–0.884 ms**; removing both **1.834–1.948 ms**.
These sequential control ranges are non-additive and not confidence intervals.
Every arm remains approximately 30 Hz. Game update adds about 1.60 ms even
when both groups are absent, so the 16.511 ms renderer work does not establish
a 60 Hz frame.

The two full-night controls give these neighboring 50-frame profile averages:

| Effect | Entire function (ms/frame) | Nested submission (ms/frame) | Other helper work (ms/frame) | Submits/frame |
| --- | ---: | ---: | ---: | ---: |
| Pools | 0.885–0.910 | 0.704–0.716 | 0.181–0.194 | 1 |
| Beams/coronas | 0.746–0.748 | 0.497–0.501 | 0.246–0.250 | 2 |

Submission is already included in the function total and covers EE packet/cache
work plus any waits. Subtracting it gives the surrounding helper work, not a
complete geometry-only measurement. The pool pass's three explicit receiver/box
collection call sites report 0.0000 ms at this precision; the instrumentation
does not cover every terrain query inside helpers. Pool batches rebuild about
0.25–0.32 times/frame; beam batches have **zero stamped rebuilds** in these warm
controls. Thus retained beam arrays already work, but assembling/comparing their
temporary data still costs time each frame. The deletion gains include wider
pipeline scheduling and unhidden GPU effects; they cannot be equated with pure
CPU brackets or used to identify exact GS overdraw.

The next candidate is a correctly invalidated unchanged-state shortcut for beam
assembly, followed by inspecting which pool-batch key fields cause rebuilds.
Preserve moving cameras/lights, visibility, flicker/levels and portal views;
compare bytes and pixels before timing without counters. The observed beam
helper bracket is only about 0.25 ms, not a promised path to 60 Hz. Submission
cost still dominates both functions and needs a separate bounded experiment.

The [split record](tyrax2-light-split-2026-10-02.json) preserves preparation and
analysis scripts, exact raw/profile/period windows and ELF/log/source hashes.
The run completed 11,000 measured gameplay frames; the frozen log's later
arena summary has zero direct fallbacks and five exceptional split frames.
Full night is restored and ps2client remains serving it. This is completed
attribution, with no production effect removal or accepted night optimization.

## EE preparation and VU0 follow-up, 2026-10-02

The [physical pipeline attribution and VU0 audit](tyrax2-ee-vu0.md) measures 2.724 ms source snapshots and 0.743 ms native conversion inside 3.665 ms submission, with only 0.003 ms combined existing VIF/FINISH waits. VU0 already handles vector/matrix/skin math in macro mode. Prioritize fewer packet/range/lease passes, then measure remaining scalar math before offloading. Combined consumer completion is a sampled upper bound, not VU-only time; no new optimization is accepted here.

## Authoring integration audit, 2026-10-02

The standard flowgraph and generated FPP/ORBIT loops already use the engine synchronization contracts. Scene unload, texture mutation, display changes, auxiliary passes and live-debugger captures do not require an unconditional wait around every node. Custom raw DMA, resource ownership and CPU readback still require explicit completion/lifetime handling; see [custom flow nodes](custom-flow-nodes.md) and [object scripts](object-scripts.md).

Version 1.169.2 fixes an existing Set Display Mode generator mismatch: legal full-height PAL value 4 was clamped to InterlacedField value 3. Scratch FPP and ORBIT fixtures preserve all five values, clamp outside 0..4 and retain the confirmation timeout. Native builds and PCSX2 runtime markers verify PAL4 at 50 Hz and timed rollback to 0, with the pipeline request retained. This is emulator control-flow validation, not physical video/pixel or latency acceptance. Existing video-modes example nodes only use 0..3, so their generated behavior is unchanged. The [integration record](tyrax2-integration-2026-10-02.json) preserves hashes and limitations.

Optional Set/Get Frame Pipeline nodes would be authoring conveniences, not required integration. Any future setter must defer its request until before beginFrame in both templates. getFramePipeline reports the requested setting, including during compatibility fallback; an actual-active/fallback query needs an explicit engine API. Physical button-to-TV latency remains unmeasured. New-project defaults follow the policy above; broad example validation is recorded separately. SPR/CALL staging remains separate work.

### Integrated engine-known reference memo

Native owned snapshot copies can retain the resolved pointer and immutable
classification for the first16 nonempty REF/REFE tags in a local128-byte EE
scratch array. Preflight still validates every tag and range before publishing
success; payload, tag order, counted native sizing, fallback, retries and fences
are preserved. Additional references spill to the original fixup callbacks.
Ordinary copy/copyCounted and arbitrary callbacks retain their existing behavior.
The specialized path is valid only for the engine pure RAM resolver and same-bank
idempotent reader-bit lease callback, with stable registry/source ownership for
both passes. No cross-frame cache or heap allocation is introduced.

The specialized path is integrated after actual-source parity and the paired
physical controls below. Those diagnostic controls do not establish an
ordinary-clock production gain or 60 FPS. Post-Memo quiet cadence and Showcase
reverse-order qualification remain separate checks at this documentation checkpoint.

### Engine-known reference memo physical control (2026-10-03)

The same-ELF ABA and reverse BAB controls accepted 384 samples per boot on the
selected fixed night workload. Memo16 reduced the diagnostic inclusive work
metric by 0.296–0.304 ms against both brackets in both orders. The frame period
remained approximately 33.367 ms (30 FPS), and the old diagnostic period's owner
remains unknown. The delta includes the candidate's 128-byte EE stack scratch
and all common diagnostic classification/query counters, Scene clocks and held
ring; it is not an isolated lookup cost, production-quiet gain or 60 FPS proof.
Current generic callback behavior is unchanged. The host arena runner separately
checks actual memo output/count/cursor parity, prefix/spill boundaries and
reader retirement/retry ownership; see `tyrax2-native-reference-memo-2026-10-03.json`
for the immutable physical evidence and limitations.


### Portable ordinary-clock quiet fixtures

Use `tools/tyrax2-quiet-fixture.py --project PROJECT --engine TYRA_ROOT --editor
EDITOR_EXECUTABLE --out NEW_DESTINATION --order 0` with absolute paths. It copies
the authored FPP project/current engine, refreshes generation only in that copy,
then applies uniquely anchored private sampler hooks. It performs no game build
or device launch. Unsupported templates, reused destinations and source drift
are rejected; do not regenerate an instrumented fixture.

Authored display/color/triple/pipeline request, product HUD, camera/scripts,
portals, audio, save values and ordinary clocks remain intact. Authored remote-pad/input-recorder/keyboard controls remain by default; explicit
`--disable-control-apparatus` disables only the named remotePad/inputRecorder
keys and records that apparatus control. Initial requests 0/1 and sparse request
changes are retained, including explicit false/legacy v94 synchronous ownership.
Mood/script state
is opaque: no save slot is changed. Diagnostic overlays and link/debug/logic polling are disabled
and FrameProfile/HardwareTrace compile out in the private copy. The same-ELF
Off/On/Off and reverse orders keep a 6144-byte sample buffer and 420-byte chunk
array. Two per-loop clocks are restricted to the warmed On window; common 64-loop
chunks bound Count wrap assumptions, and authoritative file exports follow all
timed windows.

Run `tools/tyrax2-quiet/analyze-quiet.py STDOUT --artifact FILE --expected-order
0 --environment ps2 -o REPORT` only on complete evidence. Preserve exact native
build/source/ELF/symbol/config/runtime-asset provenance for both boots. Missing
UDP/file records are not reconstructed. Completed rendered flip-return periods
and synthetic/zero/multiple events are separate; triple-buffer returns may mark
queue boundaries rather than TV scanout. Ordinary/adaptive state can drift, so
net sampler differences are not isolated observer instructions, pure CPU cost,
uniform corrections or 60 FPS certification. See
`tools/tyrax2-quiet/README.md` for bounds, limitations and actual-header/source
host controls; no extra fence/wait/register read is inserted.

The source identity hashes every file in engine/game source and include trees, engine resources and the optional game `vugen` framework, without an extension whitelist (including embedded `.irx-em`). Root build recipes/helpers and project data are also covered; generated `obj`/`bin` outputs are excluded. Runtime assets are recorded separately. PS2DEV/SDK/VCL/compiler binaries and SDK IRX modules named by `.irx-em` recipes remain external build dependencies and require separate native provenance; the manifest does not claim to hash those installed tools. Missing required source trees or recipes reject preparation.

Host VU dependency validation follows the native recipe exactly: only direct lowercase `.cpp` files in `game/src/vu` or `game/src/vu0` activate compilation of `game/vugen/*.cpp`. Active recipes require framework implementations and the literal quoted include closure resolved through source-local paths or `game/vugen`; unresolved or out-of-manifest local includes reject preparation. Empty, nested-only and name-only VU directories do not impose a framework requirement. Installed standard-library/compiler dependencies remain external native provenance.
