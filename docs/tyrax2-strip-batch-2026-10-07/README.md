# Model strip audit and compact batch-packing experiment

This private follow-up tests the remaining triangle-strip opportunity without
changing the shipped renderer. Source head: `28628d8d7` on
`codex/tyrax2-foundation`. Native-bound ELF SHA-256:
`b43ca47cfb3617d0b7757e7ef8d93d74d8d9393849adccb79fe1d11be3a96ee2`.

## Existing coverage

The actual launch assets contain 121 static model files with 260 material parts
(buildings and props together). 208 parts already have strips. Their list streams
contain 77,646 vertices; selecting available strips leaves 59,346. These are
asset totals, not visible vertices or submitted geometry in one frame. Roads,
terrain and the existing vehicle strip paths were retained.

A private bidirectional strip walk, keeping the smaller of the existing and
candidate streams, improves only ten parts by 57 vertices in total. Its exact
unoriented triangle/normal/UV/AO multiset oracle passes for changed parts.
That tiny asset-level result does not justify replacing the existing stripifier.
The [inventory](preparation/strip-inventory-all.json) and
[candidate report](preparation/repack-report.json) preserve the evidence.

## Compact batch runs

`rebuildStaticBatch` currently pads every model member to a complete 75-vertex
run. The private candidate packs independent source runs together using
degenerate joins, repeating the two overlap vertices when a package fills.
Already full runs stay independent. The final run is padded only to a multiple
of three. The candidate is retained only if its array is smaller.

Position, baked color and ST are copied together through the same source index;
arrays are swapped before rebinding versioned bags and refreshing bounds. Run
order and each real triangle's final, provoking vertex are preserved, including
flat shading. There is no normal welding, diagonal change, material merge,
object removal, new DMA wait or hot-loop geometry packing. Packing runs only
during the existing batch rebuild. Temporary source-index and replacement-array
storage and rebuild time are additional costs outside the measured windows.

The [host oracle](preparation/pack-oracle.cpp) covers 40,000 randomized cases,
25,029,165 source vertices and run sizes 6, 9, 12 and 75. It compares exact
triangle identities and provoking vertices across every package boundary.
Its [result](preparation/pack-oracle.result.txt) is correctness evidence,
not a performance estimate for the city.

## Physical PS2 result

One ELF ran candidate/control/candidate, 1800 loops per arm. Every arm uses the
same full daylight city, paused noon clock, ordinary game update and fixed
ordinary spawn camera captured at frame 700. The startup controls card is
cleared once before all measurement windows. The cold variant rebuilds occur
at frames 1900/3700, well before their sampled windows.

FrameProfile and HardwareTrace are compiled out; the dense loop/queue observer
is disabled. The same ordinary sampler and common scaffolding remain resident
in every arm. No common observer-cost subtraction is made. EE Count conversion
uses 294,912,000 ticks/second, matching the existing physical analyzers.

| Arm | Resident batch vertices | Mean non-pacing elapsed work | Rendered cadence |
| --- | ---: | ---: | ---: |
| Candidate, first | 7,998 | 13.154897 ms | 59.9404 Hz |
| Existing packing | 9,096 | 13.168070 ms | 59.9398 Hz |
| Candidate, repeat | 7,998 | 13.149771 ms | 59.9401 Hz |

Each coarse window covers 320 loops in five 64-loop chunks. All arms retain 28
resident batches,21 stripped, and matching camera, vehicle and player position
witnesses. The archive retains384 raw rows and all 5400 loops complete.
Resident batch vertices fall by 1098 (12.071%). The observed mean work contrast
is only 0.015736 ms, about0.12%, with a 0.005126 ms spread between candidate arms.
These are elapsed observations including preemption, not retired EE instruction
costs or statistical confidence bounds.

The [strict result](physical/strict-result.json), [native proof](native-proof.json)
and [primary qualification](physical/root-qualification.json) bind the source,
actual native mirrors and objects, ELF, launch assets and raw completion prefix.
Eight invalid parser controls are rejected. The non-UTF8 loader prefix remains
in [raw bytes](physical/completed.stdout); normalization is documented in the
qualification, and typed measurement fields are ASCII.

## Acceptance and conclusion

The owned software emulator also completes5400 loops. Captures were inspected:
buildings, cars, road and HUD remain present without visible new geometry
artifacts. Captures are not pixel identical; candidate/candidate captures also
differ. The [comparison](emulator/raster-comparison.json) retains these limits.
There is no physical TV/audio qualification or emulator-to-PS2 timing transfer.
The first emulator attempt remains under `rejected-controls-card-v1`: its
startup controls card differed between arms, so it was not released for timing.

The experiment finds a real reduction in prepared batch arrays, but does not
establish the visible vertex/package reduction or its causal contribution to
the tiny time contrast. Both stationary daylight arms already sustain nominal
60 Hz. It does not test driving spikes, full night, portals or other maps and
does not solve those remaining performance cases. The candidate stays private;
no production packing or stripifier change ships for this result.
Snapshot ownership/copy and remaining render work from the
[queue audit](../tyrax2-queue-audit-2026-10-06/README.md) remain more substantial
measured leads. Existing strips are already doing useful work; further strip
packing is not supported as the missing route to stable night 60 FPS.

Four changed source files and 416 unchanged, hash-verified files from earlier
immutable archives are bound through [source binding](source-binding.json).
`inventory.json` pins this new archive.
