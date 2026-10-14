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

## The heavy spot (2026-10-13/14)

The user parked the Ravager at (-119.3, 0.3, 0.3), where night driving fell
toward 30 fps. Lean, same-ELF A/Bs on a physical PS2, measured on the heaviest
150-frame drive window:

- **The waits were vsync, not the GPU.** An EE-side sampler of `VPU_STAT` and
  `GIF_STAT` during the frame-completion waits found the GPU already idle. The
  6–9 ms in `flushRecording` was the frame missing its vblank, so the frame is
  EE-bound here as well.
- **Ablations (EE work, heaviest window, base 17.3–17.5 ms):**

  | Removed | Saving |
  | --- | --- |
  | ground shadow maps | −1.7 ms |
  | beams + glows | −1.4 ms |
  | probe | −1.1 ms |
  | pools | −0.8 ms |
  | particles/smoke | ~0 |
  | lamp cones | ~0 |
  | terrain splat layers | ~0 |

  Day was −2.7 ms.
- **The PC habit found:** the baked SUN shadows (ground maps and decals) were
  drawn at night with the sun below the horizon. They are now gated
  ([shadows.md](shadows.md)). Result:

  | | Heaviest window | Parked |
  | --- | --- | --- |
  | Before | 17.32 ms, 86/150 late | 14.6 ms |
  | After | 14.43 ms, 14/150 late | 13.3 ms |

- **Slice clipping** ([vu1-clipping.md](vu1-clipping.md)) saves only ~0.2 ms
  lean, though the instrumented build had priced it at ~1.8 ms. COP0 scopes in
  hot per-call code inflate badly, so only lean A/Bs decide.
- **What `submit` copies:** at this spot, 85 snapshots a frame copy 53 KB of
  chain and 93 KB of mutable REF data. The D-misses from that are ~0.7–1 ms;
  the rest of `submit` is per-packet tag walking.

### State after the night of 2026-10-13/14

Lean, heaviest 150-frame drive window at the heavy spot:

| | Work | p50 | p95 | Late |
| --- | --- | --- | --- | --- |
| Start of the night | 17.3 ms | | | 86 |
| Now | 14.3 ms | 13.8 ms | 17.0–17.2 ms | 11–12 |

Parked: 13.2 ms, 0 late. Day and night now cost about the same. The mean
fits 60 fps comfortably; the late frames are spikes.

What the spikes are made of (instrumented, slow vs fast frames): a probe
phase, about +1.2 ms before the three-phase split; a terrain chunk build, about
+0.8 ms; and simply more geometry on screen (partial routes +1.5 ms, submit
+0.9 ms, objects +0.9 ms).

Measured and kept:

- the sun-shadow night gate;
- slice clipping;
- the three-phase probe.

Measured and dropped:

- **The probe ground stand-in** (`reflectionGroundProxy`). It made driving
  worse (p95 21.4 ms): its grid rebuilds every time the eye crosses a cell.
- **The sky dome in the main view.** Removing it saves ~0.5 ms parked and
  takes the heaviest window from 15 to 10 late frames. It was not replaced
  yet (see below).

Next leads, roughly by expected value:

1. **Terrain stream budget 1 instead of 2.** Built and unmeasured: the rig's
   `--budgetab` arm, `C:\tx\vpnQ2`. A chunk build is the second spike source.
2. **A sky card instead of the dome.** An untextured dome is a pure function
   of elevation, so a small grid just inside the far plane with per-vertex
   colours from the view ray's elevation draws the same gradient. Every
   vertex is in view, so there is no clip route and the bag takes a stamp.
   Needs the projection (fov, aspect, roll; the probe uses 110 degrees and a
   square target). Worth ~0.5 ms in the main view plus the probe's sky phase.
3. **Lamp coronas as VU1 billboards.** They are camera-facing quads rebuilt
   on the EE every moving frame, so the batch never replays. The engine
   already has VU1 billboard programs (particles, PR #118). The pull toward
   the camera that keeps the glow off the pole needs a VU-side equivalent, or
   a fixed small offset.
4. **The car headlight pool on the car's matrix**, like the wheels: today the
   patch is rebuilt in world space whenever the car moves.
5. **Zero-copy submission.** StaPip builds a packet in its own buffer and
   `submit` copies it, plus every mutable REF target, into the frame arena,
   then walks the tags three times. A per-frame data bank for the copy pool
   (REFs borrowed instead of copied) is the contained first step (~0.4 ms).
   Writing packets straight into the arena needs a second arena, because a
   packet is reserved for its worst case while the pool allocates.
6. **A VU1 strip clip program.** Strip partial packages are still expanded
   into lists on the EE and classified per block. A clip program that reads
   strips and streams its output with mid-program XGKICKs would turn a
   partial bag into one baked REF.

### Terrain chunk builds (2026-10-14)

The spike cluster at one road spot was terrain chunk builds. They had two
causes:

- **LOD band changes.** Turning LOD off removed these, but builds came back
  from the second cause.
- **The streaming ring.** It evicted chunks behind the car and rebuilt them on
  the way back, at 2.6–5.7 ms of EE apiece, on a 25-chunk map whose pool holds
  all 25.

The fix ([terrain.md](terrain.md), "A map that fits is not streamed"):

- a map that fits is built whole and never evicted;
- a LOD rebuild reads a per-node shade cache instead of relighting every
  vertex (build cost 1.9 → 1.2 ms in the frames that have one).

The heaviest window still has 7 late frames at LOD 70; with LOD off it has 4,
but LOD off costs +0.4 ms parked. The slowest frames (18–21 ms) are now
`Objects` at 6.7–7.9 ms on that stretch, which is the next target.

Rig notes for whoever picks this up:

- `cld_probe.hpp` holds 96 scope names (it was 48, and later names were
  silently dropped).
- The `.tyra` copied from the repo carries `"buildProfile": "debug"`. Set it
  to release before `--refresh-gen` in a rig copy, or the game polls
  host:livepad.bin every frame.
- Never reuse an arm name: a stale run log fools the monitor.
