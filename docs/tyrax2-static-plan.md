# Fixed-view static batch plan experiment

This private experiment separates repeated descriptor setup from view-dependent
package classification for generated Vehicle Playground static batches. It is
not a production optimization or a universal renderer test.

## Protocol

`tools/tyrax2-static-plan.py <new-folder> [--verify]` prepares private copies
through the quiet motion fixture. Both promoted geometry caches stay on;
prepared/pinned geometry stays off. No VU programs, clip capacity, geometry
quality or synchronization rules change.

This first stage is vehicle-free, noon paused, with three fixed camera views:
spawn forward, z=-20 forward, and z=-20 diagonally right. It does not price the
full-throttle vehicle/reflection workload. The target is bags explicitly
registered by `renderStaticBatches`, including their deferred submissions,
not every static object, road or terrain bag. Shared reflection rendering uses
a different producer path and is not covered. This narrows the originally
proposed view partition: there is no reflection timing claim in this fixture.

Each view has twelve phases: A/B/A/C/A/C observed, then B/A/B/A/C/A without
active counters/scoped clocks. Each phase warms300 frames, samples180 and
lasts540; all36 phases run in one boot. Outer/inner roles differ by triplet.

- A runs the original packager.
- B reuses package descriptors but runs original coarse/fine classification,
  clip masks and guard-band decisions.
- C additionally reuses decisions only when all semantic components of six
  view and eight clip planes and mask-enable state match exactly.

Descriptors use a package-call slot with bag, stream pointers, count, package
size, maximum size, bbox version and parent bbox index checked. Mismatch falls
back to original construction. Limits are512 slots/1 MiB record bytes. All arms
populate records during warmup. Deque/vector metadata and spare capacity are
additional to reported record bytes. This fixed-scene owner assumption is not
scene-reload or arbitrary mutable-owner qualification.

## Gates

The oracle executes original construction/classification and compares every
semantic package field against the candidate. Existing partial-body and strip
expansion actual-writer byte checks also run. This is not complete DMA-stream
or framebuffer equivalence. The performance ELF repeats descriptor comparisons
at offset250 outside sampling, requiring physical activation. Never price the
oracle ELF.

`tools/tyrax2-static-plan-analyze.py <bin> <runtime-log> [--oracle]` requires all
6480 sampled rows, exact phase/frame/camera/clock witnesses, positive warm hits,
no sampled candidate misses, identical classified package counts and observed
geometry/copy counters within each view, no mismatch or observer leakage, and
valid completion. Static Core elapsed has one scope per entered bag; it still
includes waits/preemption. Observer-off arms retain common private lookup,
selector and sampler footprint. B also retains per-package conditional branches.

`verify-static-plan.py` runs ASan/UBSan controls on the actual cache algorithm
with shim types, not DMA emulation. Supply a completed CSV/log to additionally
exercise malformed-input rejection against the analyzer.

## Results (2026-10-08)

All36 physical phases complete valid, with18 off-clock actual-original
comparisons, zero errors and identical observed geometry/copy/classification
counters within each view. Every sampled candidate hits its plan, with no warm
miss. The same performance ELF previously completed all36 emulator phases:
`cbb6177e3339931d770310d5ff573468d95a8eb21940ce9c1b9737a095c3c612`.
The oracle ELF is
`93dadd69fb5308b0e728b76631a467b524c74c879b06e33affb8b1fd8371f158`.
Both native source bindings match467 files. The oracle completes9720 plan
comparisons with zero mismatches. Through its stopped run, existing byte
oracles report1,974,272 expansion and360,448 partial comparisons, zero failures.
Host ASan/UBSan cache controls pass, and analyzer controls accept the reference
and reject11 malformed cases against oracle, performance-emulator and PS2 data.

Positive values below mean saved elapsed-minus-pacing work, with counters and
scoped clocks disabled. Paired values preserve the two adjacent controls or
candidates rather than hiding drift in their average.

| View | B descriptor saving, ms | C fixed-decision saving, ms | Observed A static Core, ms | Bags / partial bags / descriptors |
| --- | ---: | ---: | ---: | ---: |
| Spawn forward | +0.0111 / +0.0075 | +0.0220 / +0.0224 | 0.95-0.96 | 11 / 1 / 6 |
| Center forward | -0.0060 / +0.0125 | +0.0205 / -0.0128 | 0.93-0.97 | 13 / 1 / 6 |
| Center diagonal | -0.0029 / -0.0035 | +0.0128 / +0.0160 | about0.61 | 7 / 1 / 4 |

All variants retain approximately59.94 loop Hz in these stationary views.
The observed selected Core scope occupies roughly5-11% of non-pacing work,
but includes substantially more than descriptor/classifier work. It is not an
additive pure-EE upper bound for moving frames. Only one selected bag per view
uses package creation; the others already take direct whole-bag replay.
Record storage is240/240/160 bytes, excluding container metadata/capacity.
These descriptor counts are not counts of final VU clip chunks or DMA submits.

**Decision: no promotion and no driving follow-up for B.** B has no repeatable
gain across views; C gives only small net changes and its center contrast changes
sign. C is deliberately a fixed-view diagnostic, not a moving-camera fix.
Do not spend the optional repeat on resolving hundredths of a millisecond here.
No production renderer changes were made for this experiment.

This rejects these two private interventions as the next optimization of the
selected producer. It does not clear every static producer or prove a VU/GS
bottleneck: terrain, roads, individually submitted objects, reflections and
per-chunk packet dispatch remain outside this narrow conclusion. Before another
implementation, identify which actual producer accounts for the expensive
partial path in the moving workload; do not treat `renderStaticBatches` as a
synonym for all static geometry. Fixed-view success is not stable60 while moving.

Evidence and frozen sources are in
the archive. No physical assertion,
trap or warning was logged. Owned emulator processes were stopped; the completed
PS2 file server is left available. No console/host shutdown is requested.
