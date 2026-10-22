# A12 V2 moving paint correctness

The corrected full-throttle fixture completes three day/night/day road trips
with integrated A11 paint, camera sweeps, real legacy/override fallback, forced
authored LOD and scene reloads. This is intrusive correctness evidence only.

ELF `5d1fb516212f1dc46bd5e439827a5dc2f511ab606337766bcd71678977714d43`,
471 bound native files. Three phases contain 511/489/507 sampled frames,
including 391/369/387 full-throttle frames. Each starts at z=-74.303497 and
crosses z=108 before the capacity limit, travelling over 182 units with maximum
speed about 27.57 and camera angles spanning approximately -60 to +60 degrees.
All three cars remain present, capture is not frozen and the straight route
retains zero steer. This is not a test of arbitrary steering/yaw manoeuvres.

Paint oracle: 8,981 visits, 557 accepted bases, 325 EE refreshes (8 held-basis),
all six modes and both authored tiers, zero errors. At speed >5, there are
1,138 paint visits: 541 VU and 597 EE. Final DRIVEDONE valid=1, all ten lifecycle
parser mutations and five travel mutations pass their rejection checks.
The corrected live-prefix light witness reports 0/8/0 lights at hour 12/0/12,
66 road chunks and 18 ground-shadow pins in every phase. V1's stale-slot return
to day is rejected by the corrected analyzer and preserved separately.

Three PINE captures at the parked pre-drive window show day/night/day; they
are visual smoke checks, not moving or pixel-matched raster evidence. The raw
CSV and moving-paint counters prove travel. No frame-time or physical 60 Hz
claim follows from the heavy every-vertex oracle or emulator HUD.

The full source binding, ELF, CSV, logs, captures, manifests and body TMDLs
used for the LOD inventory are included. Other assets remain in the local
fixture and are recorded by asset-hashes.json. Fresh generated headers/TUs
were already qualified and archived in A11; this run reuses its verified source.
PS2 remains wedged on A06 DMA Busy, with its owned host server retained.
