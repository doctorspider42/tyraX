# Raw evidence: what sets the VU1 package size (2026-09-16)

The two harnesses behind
[docs/render-submission-attribution.md](../../../../docs/render-submission-attribution.md),
"Round three". **No console was touched and no arm was booted** — this round is
a bound, not a change, so its evidence is one derivation run on the host plus a
re-read of the previous round's per-frame counters.

## Why this round exists

Round two left almost every term inside `dispatch` scaling with the number of
packages, and the garage-day frame is cut into 572.5 of them. Static geometry
ships as triangle strips chopped into runs of exactly 72 vertices with
`StaPipBag::packageSize` pinned to that, so a run *is* a package. If a package
could hold twice as many vertices there would be half as many packages, and
classification, descriptor construction, both submission loops and probably the
flush count would follow. **So the question was what sets 72 and whether it can
be raised**, and the answer is that it is very nearly the largest number the
hardware allows.

## `derive-package-size.cpp`

Reproduces `StaPipQBufferRenderer::setDoubleBuffer()` and
`StaPipVU1Program::getMaxVertCount()` verbatim, with each cull program's
`(elementsPerVertex, reglistCount)` read off its own constructor, and:

- prints the derived package size for every program class and both colour modes;
- reruns `TerrainGame::minPackageSize()`'s six reachable combinations (the two
  that pair lighting with per-vertex colours are refused by `StaPipCore::render`);
- sweeps `VU1_STAPIP_DBUFFER_END` from 906 to 1024;
- prints what a package of 72 / 81 / 90 / 108 / 144 vertices would cost in VU1
  data memory, double buffer and per-mesh constants included.

```bash
g++ -O2 -std=c++17 -o derive-package-size derive-package-size.cpp
./derive-package-size
```

It needs nothing from the tree — the constants are copied in and named — so it
is also the check to run after any edit to either function. What it reports on
the shipping configuration:

| | |
| --- | ---: |
| double-buffer half | 460 quadwords |
| usable after the 9-quadword GIF tag block | 451 |
| textured + per-vertex colours (6 qw/vertex) | 451/6 = **75** → **72** after the /9 |
| `minPackageSize()` over every reachable combination | **72** |
| smallest `DBUFFER_END` that still yields 72 | 906 (shipping is 944) |
| smallest `DBUFFER_END` that yields 81 | **1014**, against 80 quadwords of clip scratch |
| what 144 vertices per package would need | **1 770 of 1 024** quadwords |

## `summarize-packages.py`

Re-reads the previous round's archived CSVs and reports the package counters per
pose, which is what says the packages really are full 72-vertex runs rather than
short tails:

```bash
python summarize-packages.py ../bounds-attribution-2026-09-16
```

Only the `attrib-*` arms carry these counters; the `control-*` arms have
`TYRA_STAPIP_ATTRIB` compiled out and report zero, which the script says rather
than dividing by. Garage day, 240 warmed rows: **572.5 packages, 120.0 flushes,
40 502 GS primitives, 70.75 primitives per package.** A full 72-vertex strip run
is 70 primitives and a 72-vertex *list* package is 24, so every pose reading
about 70 is what closes the question — the count is `vertices / 72` and there is
nothing to reclaim below the run length.

## Limits

The derivation is exact and the counters are PCSX2. Nothing here measures
milliseconds, and nothing here changed the engine: the two costed ways past 72
are in the doc and in [the backlog](../../../../docs/backlog.md), not in this
tree.
