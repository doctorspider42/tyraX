# Why the stationary night chase view misses 20 ms

This report records the initial causal investigation. The subsequent
[acceptance report](../fix/README.md) ships the bounded cache fix and adds
deep-counter-off, GS raster and dynamic-light probes. References below to
an unshipped candidate describe the state at these initial captures.

Physical PAL PS2, 2026-09-28. Same Ravager garage start, zero throttle and
speed, X 0 / Z -74.303497, ordinary mode-0 chase camera, parked traffic and
complete assets as the [continuous entry capture](../README.md).
The scene is quiet-debug; no editor/debugger connection polls during capture.
Each arm records all 480 consecutive frames and exports after sampling;
tables use **240 warm seated frames, 240–479**. Primitive counts include
strip degenerates and are not unique visible triangles.

## Measured causes

1. **Entering changes the camera's submission workload:** walking has 64
   static-pipeline calls and 22,425 submitted primitives; seated has 128 calls
   and 46,096. Vehicle simulation remains about 0.6 ms in the production
   capture. The slow stationary view does not require acceleration.
2. **The full body and reflective prefix thrash one bbox cache entry.**
   `applyVehicleEnvLimits()` leaves the base bag at its full vertex count and
   limits the coat to the painted prefix (Ravager 2,025; Pica 1,725 vertices).
   They share the vertex pointer, bbox version and VU package size. The cache
   key contains only pointer/package size, so the base changes the cached
   count back to full, then the coat changes it back to prefix. Its correct
   count-validation branch reallocates and rescans bounds every time.
   Each car causes **two replacements per frame**, four in total. Their own
   measured bbox rebuilding costs are **1.439 ms Ravager + 1.395 ms Pica**
   in the live-counter control. Walking has no fresh replacements.
3. **There is substantial remaining pipeline pressure.** After removing
   the collision, VIF waits are still 5.517 ms included in submission;
   terrain draw is 2.272 ms, light/shadow effects 1.953 ms and wheels 1.232 ms.
   Static-pipeline object-data preparation is 1.797 ms, texture preparation
   1.085 ms, package setup 0.761 ms and dynamic-light selection 0.699 ms.
   These are inclusive/nested costs, not an additive frame budget. DMA waits
   do not identify VU1 versus downstream GS utilization.
4. **First entry is a separate hitch.** The original exact entry frame costs
   165.300 ms, with 57.195 ms HUD and lazy font/icon asset reads. Warm frames
   have no uploads/reuploads. Removing the cache collision does not prewarm
   HUD assets or establish an acceptable first-entry latency.

## Same-ELF subtraction probes

Active work is `update + submit + finish`, excluding present wait; it includes
renderer/DMA stalls. All numbers below are **instrumented**, with
`TYRA_STAPIP_ATTRIB=1` and per-object snapshots. The production baseline is
23.923 ms from the parent report; do not substitute these diagnostic times
for that baseline or subtract instrumentation overhead as a fixed constant.

The first group used one ELF and changed only `bin/budget-probe.cfg` at boot.

| Arm | Mask | Median work | p95 | Frames >20 ms | Submitted primitives |
|---|---:|---:|---:|---:|---:|
| Control | 0 | 24.491 ms | 25.814 ms | 240/240 | 46,096 |
| No vehicle coat | 2 | 21.401 ms | 22.622 ms | 240/240 | 42,446 |
| Car headlights off | 1 | 24.373 ms | 25.614 ms | 240/240 | 46,070 |

Coat removal saves **3.090 ms**, but removes geometry/appearance and also
eliminates the cache collision. It cannot price the coat's intrinsic render
cost alone. Headlights save only **0.118 ms** in this group; this single small
delta is not a robust optimization estimate, but excludes them as the source
of the multi-millisecond sustained excess. Neither arm restores 50 FPS.

## Preserve the coat: bounded count-variant candidate

The second group uses a new, single ELF with a boot-only cache toggle. Its
SHA-256 is preserved separately in every arm and matches across the group.
The candidate keeps at most **two count variants per pointer/package-size
key**; a third count replaces a retained variant. Version changes still
recalculate the matching entry in place and expiry remains 250 frames.
It changes no vertices, draw order, textures, VU program or DMA ownership.

| Arm | Mask | Median work | p95 | Frames >20 ms | Submitted primitives |
|---|---:|---:|---:|---:|---:|
| Cache control | 0 | 24.656 ms | 26.063 ms | 240/240 | 46,096 |
| Count variants, coat retained | 128 | 22.027 ms | 23.196 ms | 240/240 | 46,096 |
| Cache control repeat | 0 | 24.921 ms | 26.032 ms | 240/240 | 46,096 |
| Count variants, projected light/shadow FX off | 132 | 20.490 ms | 21.698 ms | 236/240 | 45,711 |

The candidate saves **2.629–2.894 ms** against the two controls, preserving
their submitted primitive count. Controls differ by 0.264 ms; automatic
interleaving remains enabled and can choose a different order. Sub-bucket
attribution can therefore move between Objects, Batches and texture waits.
This is a clear multi-millisecond improvement, not a claim of exact
repeatability to the last microsecond.

| Cache metric | Control | Candidate | Control repeat |
|---|---:|---:|---:|
| Fresh replacements/allocations per frame | 4 | 0 | 4 |
| Hits per frame | 119 | 123 | 119 |
| Version recalculations per frame | 2 | 2 | 2 |
| Retained entries | 166 | 171 | 166 |
| Bbox rebuild time | 2.944 ms | 0.099 ms | 2.896 ms |
| Entire bbox-cache bracket | 3.403 ms | 0.546 ms | 3.347 ms |
| VIF wait, included in rendering | 5.198 ms | 5.517 ms | 5.396 ms |

Ravager and Pica each go from two fresh replacements to zero; their three
part submissions become three cache hits. The two legitimate version
recalculations remain. Some saved EE work becomes extra VIF wait, so the
frame gain is smaller than the bbox bracket's reduction.

Turning off projected light/shadow FX **after** the cache change saves
another **1.537 ms net**, less than their 1.953 ms inclusive bracket.
Wait moves into later work (Particles rises 0.640 → 1.169 ms), illustrating
why summing phase costs overstates savings. This arm removes pools,
projected shadows, blob shadows and beams; it is a diagnostic quality
reduction, not a proposed setting. Median FPS is 25 in every arm.

**Conclusion:** EE performs unnecessary bbox reconstruction, confirmed by
the caller, live per-car counters and same-ELF subtraction. It is a useful
first fix, but this prototype still misses 20 ms in every warm frame with
all effects retained. The remaining doubled submission workload and pipeline
waits need a measured submission/overlap improvement. These probes do not
establish an exclusively EE-, VU1- or GS-bound renderer.

## Evidence and reproduction

Each arm folder contains complete `frame-cost.csv`, `frame-attrib.csv`,
`drive-pose.csv`, per-object means, actual boot mask, host logs and
`summary.json`. All 3,360 frame rows are aligned and have zero speed and
unchanged car position; 1,680 warm rows underpin the tables. `asset-audit.json`
records SHA-256 for all 156 deployed PNG/TMDL/MTL/ADPCM files, identical to
the complete entry fixture. `object-map.json` gives runtime IDs and names.
Run `python analyze.py <arm-directory>` to regenerate summaries.

The first group's per-object `bbox_recalc_ms`, `bbox_hits`, `bbox_recalcs`,
`bbox_fresh` and `bbox_probes` are deferred-reader zeros, **not measurements**:
the cache folds live stats into telemetry only at `takeTelemetry()`. Its
global frame counters and per-object `bbox_cache_ms` are valid. The second
group snapshots live `cacher.stats`, fixing that diagnostic reader. Do not
compare zero deferred per-object counters with live ones.

For a new fixture, follow the parent report through `--stage stationary`.
Copy `vendor/tyra/engine` and `Makefile.base` into a scratch engine root,
with a separate native cache. From the scratch engine's `engine/` directory
apply `engine-probe.patch` using `git apply`; from the fixture root apply
`game-probe.patch`. The patches are diagnostic only and require macro 1.
Build natively via `tools/toolchain/native-build.ps1` / `.sh`, not via editor
regeneration. Start with fresh game objects when the attribution macro or
engine class layout changes. Never enable the diagnostic macro in production.

Write an integer mask to `bin/budget-probe.cfg` before each boot: 0 control,
1 car lights off, 2 vehicle coat off, 4 projected FX off, 8 wheels off,
16 roads off, 32 static batches off, 64 mesh dynamic-light selection off,
128 bounded bbox count variants. Only masks listed in the tables were
measured; flags can be combined. The read occurs before the timed loop.
Use the physical PS2 resident-IOP/host-server deployment procedure and wait
for all five CSVs (`drive-pose`, `frame-cost`, `frame-attrib`, `object-cost`,
`budget-probe`) before ending that arm's own host server. PCSX2 cannot price
the EE data-cache cost in milliseconds.

The candidate is **not shipped**. Production acceptance still needs
canonical-bounds/visibility checks, changing-count and dynamic-version
coverage, expiry/memory checks and a counters-out physical comparison, then
the driving and moving-traffic gates. Equal primitive totals alone are not
a visual-correctness proof. No 50 FPS or first-entry fix is claimed here.
The console was restored to the all-effects control after these probes.
