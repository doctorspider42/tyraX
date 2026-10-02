# Static-pipeline transform-reuse experiment

This page records a rejected experiment that widened the static pipeline's existing one-entry transform cache.

**Evidence correction:** the original fixture was later found to lack 66 PNGs
and 11 TMDL models. Its numbers below describe an incomplete scene and cannot
support the full-scene conclusion. See the independent
[full-asset PS2 recheck](performance-hardware-recheck.md).

The production cache already reuses an MVP and six object-space frustum planes for consecutive material bags. Its key includes the model-matrix pointer, all model-matrix values and all view-projection values, and it resets every frame. Those checks already protect matrices updated in place, allocator address reuse, portals, reflections and split-screen views.

The experiment replaced that entry with a four-entry per-frame working set and also cached the eight object-space guard-band clip planes. Clip-plane entries validated the renderer's near and far settings because those constants are read separately from the view-projection matrix. A last-entry fast path retained the consecutive-material case, while the remaining entries covered short non-consecutive revisits. MVP calculation was lazy so wholly rejected objects did not pay for an unused multiplication. Rewritten vertex buffers were irrelevant because bounds remain owned and invalidated separately by `StapipBagBBoxesCacher` and `StaPipBag::bboxVersion`.

The four entries required about 2.75 KiB per `StaPipCore`, including PS2 alignment, and a miss could examine four pointers. Two 960-row candidate captures were compared with a 960-row baseline on an isolated, fixed-script Motor District fixture with identical generated assets and quiet-debug settings. The final candidate changed mean included bounds cost by **+0.032 ms**, included preparation by **-0.050 ms**, submission by **-0.191 ms** and total by **-0.167 ms**. Results varied substantially by phase: preparation ranged from **-0.271 ms** to **+0.240 ms**, while three of four total phases remained pinned to the same vblank interval. These are PCSX2 CPU-path attribution numbers and not hardware FPS. A fresh physical-console retry after a user reset also failed before producing any host log or CSV: both the listener and `execee` client stayed alive, but the verified resident-IOP marker and fixture working directory received no response. No stale hardware telemetry was used.

The extra memory, consistently higher bounds cost and phase-dependent result did not justify the more complex hot path without a physical-PS2 A/B result. The multi-entry implementation was removed; the existing one-entry cache remains production behavior. A future retry should first record cache hit-distance telemetry to choose a working-set size, then run interleaved hardware A/B captures before changing production code.
