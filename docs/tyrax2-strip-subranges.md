# Terrain strip subranges

Terrain strip refinement avoids sending entire partially visible terrain strips
through tiny clip dispatches. It follows [producer attribution](tyrax2-producer-attribution.md)
and is enabled for generated terrain in 1.173.2. The integrated path passed the
same-ELF physical PS2 qualification below.

Only terrain partial strips in the ordinary VU1 path use the candidate. A bounded
CPU-only cache holds local-space bounds of the original overlapping strip vertex
subranges. Keys include source pointer/count, chunk size, bbox/content versions
and (in the private prototype) a scene-reload epoch. Production uses the terrain's
monotonic bbox stamp across reloads. No view decisions or DMA payload pointers are cached.

Outside ranges are conservatively rejected against the screen planes. Only
ranges strictly inside all eight guard-band planes use the cull program; adjacent
compatible ranges are combined up to the existing cull input capacity. Others
retain the existing clip chunk size and mask. Expansion uses the existing
versioned/leased cache and parity-preserving fallback. Near-plane uncertainty
falls back to clipping; VU output capacity is unchanged.

Twelve phases in one ELF: six rotation and six driving phases. Each workload
runs A/B/A with counters and B/A/B without counters; no scoped clocks. Both
previously promoted caches stay on. The previous vehicle-free and full-throttle
workload controls remain unchanged. A separate correctness ELF adds independent
per-vertex classification witnesses, fresh-bound checks, and both existing
actual-writer geometry oracles. Emulator timings never price the optimization.

## Production contract

`StaPipBag::refineStrips` defaults false. Generated base, paint, AO and emission
strip passes opt in together with identical package sizes. Ground-shadow bags
also opt in, but currently use separate lifted triangle lists and are unchanged.
The Core owns a lazily allocated 256-entry, 32-bounds-per-entry cache (202752
bytes on PS2). Version changes are mandatory when positions change or storage
is reused. Allocation failure, oversized ranges, billboards and custom programs
retain the old path. EE clipping is unchanged. No VU program or output capacity
changes. The existing expansion cache and fallback preserve all vertex streams.

## Private prototype results

Physical PS2, same-ELF counters-off B/A/B: rotation saves **0.943 / 0.930 ms**;
full-throttle driving saves **0.404 / 0.332 ms**. Counted A/B/A has the same
direction (rotation 0.915 / 0.961 ms; driving 0.264 / 0.311 ms).
Total rotation clip dispatches fall from 194.711 to 86.728 per frame.
The independent emulator oracle passed 400674 subrange witnesses and both
existing geometry-writer checks without failure. Host ASan/UBSan passed 190000
range witnesses, boundary/guard planes, overlapping tails, version invalidation,
nonfinite fallback and intentionally wrong-route rejection.

The parked B image and following A image match exactly outside the HUD
(x=1..640, y=70..504). This is one parked-view pixel check, not proof of full
moving-image identity. Emulator timings are not used for performance claims.
These improvements do not establish stable 60 fps in every scene or at night.

Tools: `tyrax2-strip-subranges.py`, `tyrax2-strip-subranges-analyze.py`,
`verify-strip-subranges.py --production`, and `tyrax2-strip-capture.py`.
The prototype generator/helper are preserved in the evidence archive; the live
generator instruments the production implementation, not a duplicate algorithm.

## Integrated PS2 qualification

The final production implementation retains the direction in all four paired
contrasts per workload. Timings are mean whole-loop work minus explicit pacing,
not pure EE instruction time and not a fixed price per VU dispatch.

| Workload | Counted A/B/A savings (ms) | Counters-off B/A/B savings (ms) |
| --- | --- | --- |
| Fixed-eye rotation, no vehicles | 0.978 / 1.041 | **0.962 / 0.988** |
| Full-throttle drive, three vehicles, normal captures | 0.385 / 0.357 | **0.311 / 0.209** |

Counters-off rotation work is 10.612 ms in A versus 9.650 / 9.624 ms in B;
driving is approximately 13.70 versus 13.39 / 13.49 ms. Mean measured loop
cadence during driving rises from 52.66 to 54.00 / 53.55 Hz, not stable 60.
The final 20-unit spatial bin of the second B drive is 0.54 ms slower than A;
the other nineteen paired drive bins improve. Do not claim every frame improves.
Driving p95 work improves from 18.42 to 17.92 / 17.80 ms.

Rotation clip dispatches are again 194.711 / 86.728 / 194.711 per frame,
with unchanged per-angle bag/package/paint/capture/rebuild ledgers. All twelve
physical phases and all twelve performance-emulator phases passed accounting,
pose, deferred ownership and observer-off checks. Both integrated native builds
match 467 source/mirror files and their preserved ELF hashes. The integrated
parked B and following A world-image crops also have zero differing pixels.

Performance ELF: `87ab75ce3aaadc47054df48ff7dfd07749e7de3d651710acabbb119e846c4f7a`.
Oracle ELF: `54fb921ef776a0156996a717b00f00007d2f3e7fc86f0dfc7d50902c6d164f34`.
The integrated oracle passed all twelve phases and **400614 independent range
witnesses**, with zero range or geometry-writer failures. Host ASan/UBSan and
renderer controls also pass. Analyzer negative controls reject row errors,
clock leakage, failed witnesses and missing successful completion.

Archived evidence separates
prototype from integrated sources, physical from emulator data, and correctness
from timing builds. Full-night, moving pixel identity and stable-60 qualification
remain open; this is a narrow dispatch reduction, not a renderer rewrite.
