# Night EE investigation (2026-10-12/13)

Question: where does the EE spend the night frame of `examples/vehicle-playground`
on a real PS2? Which of that work is "done the PC way rather than the PS2 way"?

## Rig

The scratch rig lives in `C:\tx\rig`. It is private and not part of the
repository.

- **Project and build:** a copy of the example in `C:\tx\vpn<arm>` uses the
  release profile. The engine copy in `C:\tx\tyra<arm>` is built with
  `tools/toolchain/native-build.ps1` and its own cache.
- **Scene:** night is forced in the copy's `district_mood.cpp`. Real pads are
  neutralised.
- **Cycle:** 600 frames parked at the spawn, then a full-throttle drive up the
  boulevard, then a scene reset. The arm switches every cycle (`Cld::arm`), so
  one ELF yields interleaved A/B windows. Each ps2link reset risks a
  `freepad: DMA Busy` wedge.
- **Probe (`cld_probe.hpp`):**
  - loop period;
  - work, which is the period minus the renderer's stall total;
  - p50/p95 and late periods (> 18 ms);
  - COP0 scopes around `renderScene` phases and `StaPipCore::render` segments;
  - optional EE performance counters.
- **Output:** windows are printed over ps2link (`CLD`, `CLDS`, `CLDP` rows).
  Healthy `TYRA_LOG` lines are compiled out in all arms.

The instrumented builds (scopes plus PMU) add about 2.5 ms per frame. The
headline numbers come from `--lean` builds (loop statistics only).

## Measurements

### Baseline night, release build, lean

| Window | Work mean | p95 | Late |
| --- | --- | --- | --- |
| Parked at spawn | 18.1–18.3 ms | 18.7 ms | 300/300 (30 fps) |
| Driving, first 150 frames | 18.7–18.8 ms | 22.9 ms | 112/150 |

### Where it goes (instrumented, parked)

- **Scene:** 16.2 ms. Of that, 13.3 ms is inside `StaPipCore::render` for
  135 bags, about 98 µs per bag.
- **Bags per producer:**

  | Producer | Bags | Time |
  | --- | --- | --- |
  | Roads | 27 | 3.25 ms |
  | Terrain | 18 | 2.7 ms |
  | Static batches | 10 | 1.4 ms |
  | Sky | 5 | 0.8 ms |
  | Objects | 58 | 6.4 ms |
  | Effects | about 17 | about 4 ms |

- **Per bag:** about 10 200 instructions (EE PCR0 event 12), about 330 I-cache
  misses and about 310 D-cache misses.
- **Miss cost, calibrated on the console:**
  - an I-cache miss costs about 50 cycles, measured as 32 768 straight-line
    nops producing 2 189 misses;
  - a D-cache load miss costs about 42 cycles;
  - a store miss costs about 52 cycles.

  The COP0 Count register ticks at the EE clock.
- **A second ("warm") call of the same bag costs the same as the first.** One
  call's code and data footprint exceeds the 16 KB I-cache and the 8 KB
  D-cache. The hot functions total about 32 KB, scattered over about 380 KB
  of `.text`. `sendObjectData` alone is 12 KB, `StaPipCore::render` 7 KB.
- **Per segment and bag:**

  | Segment | µs | I-misses | D-misses | Instructions |
  | --- | --- | --- | --- | --- |
  | Bounds | 19 | 57 | 57 | 1345 |
  | Prepare | 22 | 64 | 49 | 1750 |
  | `sendObjectData` | 16 | 58 | 56 | 985 |
  | Baked-cache key + lookup + replay | 28 | 103 | 67 | 2056 |
  | Flush | 12 | 21 | 53 | 1540 |

- **Snapshot:** about 100 per frame, 49 KB of chain copy, 56–130 KB of
  mutable-REF copy and 2.25 MB borrowed.

### Not the lever

| Arm | Change | Effect |
| --- | --- | --- |
| D | `-O2`, asserts compiled out | −0.25 ms parked |
| E | `-Os` | Crashes at boot |
| L/M | Wider submission batching | −0.65 ms, not repeatable (one cycle 0) |
| P | `-fprofile-arcs` (PGO) | Crashes at boot |

E and P crash with the identical `BadVAddr C1D7A0E5` at different EPCs. A
float pattern loaded as a pointer suggests a layout-dependent memory
corruption during init. This is an open bug, unrelated to the frame cost.

## The PC-ism and the PS2 answer

Every draw rebuilds, through a large generic path, a head that is almost
identical to the previous frame's:

- it re-derives the baked-cache key and walks a hash bucket;
- it re-fetches bboxes and re-selects the program;
- it re-decides light and fog;
- it writes every uniform.

The bytes are then copied again into the frame arena.

On PS2 the EE should stamp: check that nothing changed, then append the prepared
chain. See [bag stamps](stapip-stamps.md).

### Stamps, lean A/B, same ELF

Stable over eight interleaved cycles after the stamp table fixes (destroyed-entry
log instead of a global invalidation, per-entry serial, 512 slots / 8 probes).
Work means; "late" is a period over 18 ms.

| Window | Baseline | Stamps + idle audio | + amortised probe, stream-first, probe lean |
| --- | --- | --- | --- |
| Parked | 18.3–18.6 ms, 300/300 late (30 fps) | 13.5 ms, 0–2/300 late | 13.5 ms, 0/300 late |
| Driving, first 150 frames | 19.0–19.3 ms, ~120/150 late | 15.1–15.2 ms, ~19/150 late | 15.3 ms (p50 14.8, p95 16.9–17.3), 11–14/150 late |
| Driving, next 150 frames | 13.9 ms, ~20/150 late | 12–14/150 late | 2/150 late |

The third column is what the generator now emits: stamps on
(`setStampEnabled(true)` in the frame pipeline setup), the audio idle sleep, and
the two-frame reflection capture with terrain streaming ahead of it
([reflective materials, "Amortised capture"](reflective-materials.md)).

### Tried during the run, about zero

| Change | Effect |
| --- | --- |
| Wider submission batching for stamps | ~0 |
| Wheels as local geometry + a matrix per wheel on VU1 | ~0 (the wheel cost is not the re-bake) |
| Sun/moon/stars as one bag on a matrix | ~0 |
| Probe cadence alone | a few late frames fewer, mean unchanged |
| Probe "lean" (fewer probe contents) | −0.3 ms |

## Remaining driving cost (instrumented, stamps on)

- **Partially visible bags:** terrain and roads near the camera, about 15 bags.
  These stay on the per-package classify + clip route, which costs 160–210 µs
  per bag, about 2.6 ms in total.
- **Wheels:** the wheels are world-space re-baked on the EE every moving frame
  (CPU skinning), about 1.2 ms. The PS2 answer is local immutable wheel
  geometry with a matrix per wheel; this was prototyped privately earlier.
- **Particles, glow, lamp glow:** about 1.5 ms.
- **Light pools, beams, decals:** about 2.3 ms.
- **Update:** about 1.5 ms.

The audio thread is a further "waiting for no reason" cost. With no song
playing, it polled every 0.5 ms at priority 5, which preempted the game about
2000 times a second. It now sleeps 16 ms while idle (`Audio::idleSleepMs`). An
earlier private prototype of the same change measured about −0.5 ms.

## Status

Night parked holds 60 fps. Night driving is at 15.3 ms mean with 11–14 late
frames in the heaviest 150-frame window - close, not yet stable. The sky dome's
partial route (~0.8 ms) cannot simply skip clipping: off-screen dome triangles
would wrap the GS 4096-pixel window. Headlight pools are inherent to the look.
The wheels' ~1 ms is explained and fixed (2026-10-13), see
[wheel-rebake-skip.md](wheel-rebake-skip.md):

- The driven car's wheels were re-baked on the EE (0.28 ms) and packed on the
  direct route (0.74 ms) every frame.
- The night's matrix-per-wheel arm measured ~0 only because of a rig bug: a
  per-wheel `bboxVersion` over a shared array made the four wheels evict each
  other's baked stream.
- Fixed, the matrix path costs 0.27–0.34 ms for the wheels.
- Resting batches now own their bag, so they take a stamp (440 -> 231 µs).

Visual equivalence of the stamps on a TV is still owed.
