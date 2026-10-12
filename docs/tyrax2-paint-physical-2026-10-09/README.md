# A13 integrated paint on physical PS2

This checkpoint records the first complete physical EE/VU/EE day/night driving
comparison of the integrated paint path, following the overnight correctness
work. The user then requested a pause, not another experiment. Neither console
nor computer may be powered off; earlier shutdown permission was revoked.

## Completed hardware series

All six phases complete with DRIVEDONE valid=1. The analyzer verifies 470 source
files, 303 launch inputs, the editor deployment identity and nine malformed-data
controls. ELF SHA-256 is
`996b459f73ca13173e741c373cef57b9fd459262e92dd38ab82b3cfb40b46162`.
Every trip retains all three cars, 66 road chunks, 18 pins, live reflection
captures, ordinary LOD/clipping, real simulation dt and camera sweeps of roughly
plus/minus 60 degrees. Authored day/night lights are 0/8. There are 2,598 rows,
of which 1,878 are full-throttle driving, each trip covering over 182 units.

| Mood / path | Mean work ms | p95 work ms | Entry/present Hz |
| --- | ---: | ---: | ---: |
| Day EE before | 18.658 | 25.351 | 36.075 |
| Day VU | 17.618 | 24.932 | 38.317 |
| Day EE after | 18.812 | 25.584 | 35.357 |
| Night EE before | 20.867 | 28.186 | 33.224 |
| Night VU | 19.840 | 27.311 | 34.235 |
| Night EE after | 20.942 | 28.684 | 33.274 |

Work means `(whole - pacing) / EE Count frequency`, NOT pure EE arithmetic.
Every driving row has one recorded presentation. Driving-phase mean savings
are 1.040/1.195 ms against the day controls and 1.027/1.103 ms at night.
Equal-weight common 20-unit distance bins give 0.926 ms day and 1.017 ms night
against the average of the two controls. Day improves in six of eight bins;
night in all eight. Do not claim a uniform per-frame gain or pair by row index:
real dt changes the number of samples and camera/geometry timing along the road.

The VU arms still contain 193/342 and 229/305 driving periods above 25 ms.
This is NOT sustained physical 60 Hz. The older private C16 roughly 2 ms result
is not the price of this integrated ordinary-LOD workload. This series supports
a useful offload, not a blanket conclusion that packet preparation is fixed or
that the remaining cost is all EE rather than downstream work/waits.

Production `TYRA_VU1_EXP_PAINT` remains OFF. Both controls here link PAINT images;
the contrast does not include the whole-image/code-size difference from flag0.
Light counters and normal logging remain, with unpriced common observer cost.
There is no standalone parked-turn test or new matched physical raster proof.
The manifest's inherited statement that hardware was unavailable describes its
overnight creation time, not this completed physical run.

## Evidence and pause

`forward/fixture/` preserves all bound sources, game/engine Makefiles, native
metadata, the ELF, complete physical launch inputs, final CSV/log snapshot and
analysis. `tools/` preserves the generator, analyzer and editor-equivalent
deployment helper. `forward/fixture/paint-qualification-physical.json` is the
machine-readable result; the CSV is under `physical/bin/drive-probe.csv`.
The final log is a read-shared snapshot of the still-owned file server; its hash
must match the analysis. The server is intentionally retained, not terminated.
The snapshot includes a later post-completion log tail than the first live
analysis. `forward/live-analysis-before-snapshot.json` preserves that report;
reanalyzing the complete archived fixture reproduces all results and changes
only logSha256 to the finalized snapshot hash.
Original A07-A13 overnight archives are unchanged.

`deferred-reverse/` records the VU/EE/VU preparation begun before the pause:
native ELF `7704395cd5d451a9e436bbb69fd5f342c6d37dd6c37dc59839c80c85a18c6fb5`,
470 bound files, differing from forward only in `motion_probe.hpp` mode order.
That changed header and full binding are retained with the ELF/build/symbols
and partial emulator evidence. Other source files are exactly the forward
binding. The emulator completed phase 0 before being deliberately stopped at
the user's pause request. No complete qualification or physical deployment is
claimed. Owned emulator, capture client and Xvfb were stopped; no user emulator
or hardware was shut down. Resume only when requested.

## Overnight conclusions

- A04 precomputed sky boxes reduced clipping calls but regressed physical work
  by 0.195-0.242 ms. Rejected, not enabled.
- A05/A06 road geometry found only four small reducible chunks. The private
  draw reduction is not promoted: physical series was interrupted by an IOP
  pad stall and visual/nonlinear light/fog equivalence was not established.
- A07-A10 qualified the VU paint output and integrated generated whole images
  plus a guarded public engine API. Enabled tests pass 54 exact nonblank GS
  readbacks across fog/clipping cases; disabled images stay separate.
- A08/A11 fixed a prototype EE/VU transition hazard: accepted paint basis and
  EE representation readiness must be tracked independently. Normal storage,
  count and content versions invalidate stale data. This is not a newly proven
  bug in the previously enabled default renderer.
- A11/A12 qualify generated FPP/orbit code, night scene reloads, fallbacks, LOD
  changes and full-throttle travel with intrusive correctness checks, not speed
  claims. An A12 light-count observer error was fixed without changing rendering.
- A13 removes heavy checks/forced modes for the physical contrast above.
  The integrated offload now has a useful hardware result, but no default
  promotion or claim that the broader frame-preparation problem is solved.

The next gate, after the pause, is a complete reverse-order qualification and
an explicit decision on remaining physical visual/default-off image-cost
checks. Do not automatically resume, re-open closed cache leads or disable
scene features merely to report 60 fps.
