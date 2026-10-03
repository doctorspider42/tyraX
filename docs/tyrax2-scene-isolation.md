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
