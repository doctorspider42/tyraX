# A17 EE normal preparation census

This experiment checks whether the default environment-normal optimization
repeats per-vertex EE normalization because stable normal arrays collide in
its 32-slot direct-mapped table. It is a work census, not a timing experiment.

## Method

The generator verifies the completed A16 drive's source binding, clones it to
a fresh fixture, disables A16's rejected target gate, and leaves normalization,
VU programs, geometry, EE paint, clipping, batching and ownership unchanged.
The current-bag TEX1 correction remains active. Six complete full-throttle
routes repeat three daytime then three nighttime runs with 66 road chunks,
three vehicles and camera sweeps. All runs have the same normalization policy;
the inherited 0/6/0 labels do not select any candidate here.

The observer counts calls/hits/misses, empty slots, conflicting pointers,
same-pointer key changes, normal vertices visited and actual nonzero-normal
sqrt executions. A separate bounded history detects revisits with unchanged
pointer/count/bbox/content stamps. This is not an allocation-generation oracle.
It also counts normalization misses followed by whole-bag frustum rejection.
Counters are partitioned into warmup, 120 parked sampled frames and the driving
interval, including the terminal export frame. They are exported only at phase
completion. Do not divide phase totals by sampled rows as exact per-frame rates.

The additional history lookup and counters are intrusive; no elapsed times or
FPS from this build are accepted as a performance price. The decision gate is
whether recurring normalization exists in this workload at all.

## Qualification

- `verify-env-normal-churn.py`: actual production body, two stable arrays over
  100 iterations; 32 roots with distinct slots, 3,200 with a collision, 3,200
  under legitimate mutations. Host ASan/UBSan passes, not a map-frequency claim.
- `verify-normal-census.cpp`: host ASan/UBSan passes cold/hit/conflict/revisit,
  legitimate mutation, rejected-bag, bucket and reset accounting.
- V1 fails linking stale A16 TargetAudit counters after its gate is compiled
  out; never launched. V2 puts their definitions in the common probe header.
- V2 native binding matches 473 source files and ELF
  `3be433d5d8abe79336d4d5207a69c2bcc6127622079734e1498f867bf3e439d9`.
- Hidden PCSX2 and physical PS2 both complete all six routes. Each analyzer
  rejects missing rows, missing completion, missing census and overflow controls.
  The physical launch uses
  `tyrax2-deploy-ps2.ps1`, replacing verified owned PID 16444 with PID 19008.
  No manual console reset or shutdown was requested.

## Decision

The repeated-normalization hypothesis is CLOSED for this measured workload.
There is no justification for a normalization-policy fix to address its steady
driving/turning slowdown. The synthetic collision mechanism still exists; this
does not certify arbitrary future scenes, allocation layouts or stream mutations.

| Platform | Parked/turn calls | Driving calls | Post-warmup misses / roots | Completed sampled rows |
| --- | ---: | ---: | ---: | ---: |
| Physical PS2 | 1,440 | 2,010 | 0 / 0 | 2,580 (1,860 driving) |
| Hidden PCSX2 | 1,440 | 3,394 | 0 / 0 | 3,790 (3,070 driving) |

Every phase has four warmup misses and 10,344 normalized vertices/roots.
No unchanged-key repeat is observed, including warmup. A few slot replacements
occur during scene reloads, not recurring steady-state churn. Both platforms
have zero normalization followed by whole-bag rejection and zero history overflow.
Every route travels over 182 units, crosses z=108 and sweeps approximately
-60 to +60 degrees; nighttime has eight active lights. Captures and paint/env
submissions remain active. No image-quality or speed improvement is claimed.

Production defaults and both authored project hashes remain unchanged. The
owned emulator/Xvfb are stopped; PS2 and its owned host server remain running.

## Evidence

The `fixture/` archive preserves 1,108 files: bound sources, ELF, assets, launch
manifests, logs and CSV with a verified SHA256 inventory. `final-physical.json` and
`final-emulator.json` revalidate the immutable archive. The earlier physical
report may have a different log hash because the live server continued writing
an idle tail; the final report binds the archived snapshot, not that live file.
Tools are in `tools/` beside this report; V1's failed build log is retained.
