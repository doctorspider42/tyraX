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
