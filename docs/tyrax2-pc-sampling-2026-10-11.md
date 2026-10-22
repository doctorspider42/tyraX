# A37 Interrupted-PC Feasibility

Goal remains substantial EE offload and stable60 under the preserved workload,
not another smaller counter optimization. A36 current V7 capture qualifies
structure, but source-vector shares do not identify EE instruction residency.

## Prior Leads Checked

Road classification/grouping/repacking P01..P09/C18..C20 and A19/A22 gather are
already completed. Queue code still uses counted snapshots, no second inline
payload copy, and sixteen-entry known-REF scratch. C05 generic snapshot/native
fusion regressed0.131..0.285ms; H05 lookup has no robust gain. Do not reopen them.
H13 PMU proposals have overflow/mode/preemption limitations and no qualified
runtime PMU trial. This proposal uses neither PMU nor call-entry instrumentation.
A13 VU paint remains a separately known gain, not a newly discovered missing
algorithm: current V7 deliberately keeps main EE paint while using folded VU
images for clipping. Re-enabling it is not an unexplored generic queue fix.

## Primary API Review

Installed SDK exposes timer_alarm_handler_t with an interrupted-PC argument.
Upstream [timer.c](https://github.com/ps2dev/ps2sdk/blob/master/ee/kernel/src/timer.c)
passes AddIntcHandler2's address to the timer callback. Its recurring callback
result advances the previous schedule; a delayed IRQ must schedule from actual
time to avoid catch-up samples all attributed to the same interrupted PC.
Do not commandeer timer2/3 registers: use the shared SDK counter lifecycle.

Upstream [prof.c](https://github.com/ps2dev/ps2sdk/blob/master/ee/libprofglue/src/prof.c)
does NOT use pc_value: it samples gp.pc, updated by __mcount at instrumented
function entry. Without -pg this is not an interrupted-instruction profiler;
with -pg it adds call hooks and can attribute time to stale function entries.
Do not use its flat profile as evidence for the desired question.
These upstream sources are API clues, not proof of the installed binary or
kernel address semantics. Pin/disassemble the actual SDK implementation and
calibrate on the target before accepting a renderer histogram.

## Qualification Contract

1. Private header/store only, no production default. No file IO/allocation in
   ISR. Bounded rows retain raw callback PC, CP0 EPC, scheduled/actual timestamps,
   declared region and phase. Track overflow and reject overflowed evidence.
2. First run two distinct noinline leaf loops with linked symbol/range witnesses.
   Require callbacks while each runs and matching interrupted-PC ranges, not
   only nonzero counts. Retain kernel/out-of-text PCs instead of masking aliases
   into game addresses. Identify branch-delay semantics explicitly.
3. Stop/release before exporting and verify no callback after stop. Fail closed
   if the shared timer is unavailable; never reset it or another user's timer.
   Repeat at distinct intervals to detect instruction/IRQ phase locking.
4. Then collect ordinary day/night moving work under the current common baseline,
   A35 OFF, with sampler-off return controls and held-pose main/env raster gates.
   Symbolize against the exact unstripped native ELF, including inline attribution
   and explicit kernel/unknown bins. No gprof call instrumentation.
5. Interpret as sampled instruction residency, NOT exclusive function cycles,
   miss/stall cycles, caller costs or proof of60fps impossibility. Interrupt
   masking/preemption/SDK alarm coalescing may bias it. Choose a substantial
   source intervention only if repeated distributions identify real hot code;
   price that intervention separately with the existing physical A/B protocol.

Initial implementation was the bounded SDK-independent row store and delay
policy, with host disabled/overflow/reset/order controls. Target calibration
has since completed below; a renderer histogram/performance result is still absent.

Host verification: g++ C++17/O2/Wall/Wextra/Werror PASS, then O1 ASan+UBSan
PASS. Checks include disabled capture, bounded ordered rows, full-buffer drop,
saturating drop count, reset, delayed-IRQ deadline, clock reversal, zero period,
deadline overflow and the SDK free-counter sentinel. Initial sanitizer command
needed a quoted comma-bearing flag in PowerShell; it did not compile/run until
that shell invocation was corrected. These checks qualify storage arithmetic
only, not any interrupt handler or unbiased sampling.

Calibration fixture implementation now clones bound V7 but returns from the
game loop into two120ms noinline leaf loops at997/1733us, using shared SDK
alarms. Raw callback address and CP0 EPC are retained. Alarm release is checked,
followed by another leaf spin to detect late callbacks; export occurs stopped.
Installed libkernel/header hashes are recorded. Native/target qualification is
complete as recorded below. This intentionally does not draw or price the scene.

[Calibration evidence](tyrax2-pc-sampling-2026-10-11/calibration-v1/README.md):
native490/ELFa87be09e...af2c3, hidden emulator375 samples and physical PS2372.
Actual callback leaf hit ratios97..100% across997/1733us periods, zero overflow
and callbacks after release, five reader mutations rejected per target. CP0
EPC inside callback has ZERO leaf hits; use SDK callback address, retain both
raw but never symbolize that CP0 value as interrupted game work. Installed and
native-mirror kernel library hash matches; actual binary callback forwards the
saved incoming address.519-file archive replays both readers byte-identically.
First native attempts fixed debug-header ordering and SDK u32 pointer ABI only.
No rendering, moving profile, observer-cost price or bias proof yet. Next use
the calibrated callback channel on preserved ordinary day/night work.

## Moving fixture (in preparation)

`tyrax2-pc-moving.py BOUND_V7 OUT` retains the common A31/A30 baseline and
keeps A35 OFF in all six trips. Day and night each use997us,1733us, then a
sampler-off moving return. Start only at gas; stop before held checkpoints and
CSV export.24576 bounded32-byte rows are reused per trip, never allocated in
IRQ. No -pg, no renderer policy change. Regions mark coarse outer-loop update,
render and loop-boundary states, not thread-exclusive execution: audio/SDK
preemption can share that label. Both raw callbackPC and diagnosticEPC retained.
Each held pose draws OFF/ON/OFF return with pinned target and main/env readback;
the common owner/residual/header contracts remain checked. Separate moving and
held CSVs prevent stopped IO/readback from contaminating driving distributions.
New independent qualification is required; do not reuse A35 activation reader.
Pending native, hidden, physical, symbolization and observer-return qualification.

The moving fixture now completes native490/ELF185560b0...e7b9a and BOTH targets.
Closed hidden30604/physical30492 samples;36 exact OFF/ON/OFF main/env rasters
each,15 reader controls, no overflow/late callback. Native debug/deployed .text
equal;623-file archive replays both qualifiers and physical symbolizer exactly:
[moving-v1](tyrax2-pc-sampling-2026-10-11/moving-v1/README.md).
First symbolization failed on Windows backslash sample-pin paths under WSL;
canonical POSIX relative keys repair it, no target source/ELF change.
Initial log watcher failed shell quoting and never observed anything; corrected
watcher then followed the same live process to completion, without restart.

Physical repeat distributions: VBlank30.93..34.04%; copyQwords5.00..6.29%;
generic Core3.38..4.08%, live head writer3.12..4.56%. No single giant non-wait
function is established. Unknown raw outside-text0.29..0.43% retained. Do not
equate shares with exclusive cycles or convert them directly to milliseconds.
Sampler-off day49.26/night41.18 entryHz, p95 non-pacing20.16/23.78ms. Sampled
trips slightly slower; observer is not free and OFF return is not cost A/B.
No60fps, impossibility or optimization promotion.

Source audit confirms actual448x448 Hybrid double-buffer mode and prior-job
completion/vsync/display-copy sequencing. Existing Hybrid triple path is a
known feature, not a newly invented fix, but prior general/synthetic correctness
does not establish current moving PS2 net performance. Next test that preserved
workload boundary with actual grant/rebuild/resource/VRAM/raster qualification,
retaining fences. Then copied-range provenance if necessary, not memcpy tuning.
