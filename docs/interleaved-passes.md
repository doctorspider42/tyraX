# Interleaved passes

Interleaved passes are a draw-order option of the generated game. The static
batch and road bags are fed into the object loop instead of being drawn before
it, so the EE's work on the objects overlaps VU1's work on the batches and
roads. It is set in **Preferences > Rendering > Interleave batches and roads
with objects** (`settings.interleavePasses`):

| value | what the game does |
| --- | --- |
| **Auto** (default) | times both orders every couple of seconds and keeps the faster one |
| Always | always interleaves |
| Off | the plain order: batches, roads, then objects |

## Why it helps

The EE feeds VU1 through a queue of four chains
([ee-submission-rearchitecture.md](ee-submission-rearchitecture.md), "The
VIF1 submission queue"). How much of each pass is the EE waiting for VU1 was
measured on a physical PS2 (Motor District, garage day):

| pass | EE time in the pass | of it, waiting for VU1 |
| --- | ---: | ---: |
| static batches | 1.74 ms | 0.66 ms |
| roads | not bracketed | 0.89 ms |
| objects | 2.31 ms | 0.00 ms |

- **Batches and roads are cheap for the EE and heavy for the GPU.** Each is a
  whole-bag replay (one DMA `REF`), so the EE fills the queue and then waits.
- **Objects are the opposite.** Per-object game logic and per-bag setup keep
  the EE busy while VU1 has little to draw.
- **Drawn one after the other, neither overlaps.** Interleaving hands VU1 a
  few batch and road bags in front of each object. VU1 then works while the
  EE sets up the object, instead of before it.

Terrain is not deferred. Outdoors it is EE-heavy as well (1.97 ms of EE
against 0.01 ms of waiting), and deferring it cost more than it saved.

## Measured

Physical PS2, the Motor District timing fixture, **one ELF with the mode
chosen at boot** (a two-ELF A/B moves by code layout alone,
[ee-submission-rearchitecture.md](ee-submission-rearchitecture.md), "How to
A/B a change to GAME code"). Two boots per arm. The delta is against the same
ELF booted in Off with the same toggle file present: the file alone moves
the outdoor poses by +0.10 ms.

| mode | garage day | garage night | outer day | outer night |
| --- | ---: | ---: | ---: | ---: |
| Always | -0.56 / -0.56 | -0.58 / -0.57 | +0.20 / +0.19 | +0.08 / +0.10 |
| Auto | **-0.48 / -0.49** | **-0.50 / -0.49** | +0.01 / +0.01 | +0.02 / +0.04 |

`work` in ms. In the garage `vif_wait` falls 0.41 ms and `dispatch` 0.79 ms.
`prepare` rises 0.20 ms: the EE now alternates two working sets, and the
D-cache pays for it. Under PCSX2, which emulates no data cache, the same
change moves `prepare` by 0.03 ms.

**Why Auto matters.** Outdoors the object loop has only ~0.7 ms of EE work,
so there is little to overlap and only the cache cost is left. Always loses
there. A count cannot tell the two scenes apart: batch+road bags per drawn
object are 34/49 in the garage and 17/28 outside.

## The auto tuner

The generated selector compares four pairs of **settled same-order blocks**.
Each block discards its first two complete helper-to-helper intervals, then
averages four valid intervals. Alternate pairs reverse their first order
(interleaved/plain, then plain/interleaved) to reduce monotonic drift.
Interleaving must win at least three of four pairs by 1%; the choice is then
held for 100 valid observations before another probe. A complete uninterrupted
probe takes 48 intervals, rather than the historical 16 alternating frames.

The score is inclusive loop wall work minus presentation pacing, using the
existing COP0 mark and cumulative `getStallTotal()`. It includes EE work,
unlabelled waits, interrupts, pending completion and the next frame front end;
it is neither pure CPU execution nor per-job GPU cost. The renderer keeps at
most one pending pipeline frame and completes it before submitting the next
one. After two discarded whole intervals, every contributing adjacent frame
uses the same order. This also applies to compatibility rendering, without
adding a fence or a timer read. Native overflow prefixes remain pieces of the
same frame. See [the ownership proof and limits](tyrax2-interleave.md).

A generation counter independent of profiling increments at every renderer
2D/3D recording start and successful synthetic warp render start (after early
returns, before draw). Failed warp attempts do not increment; this is an
observation epoch, not a submission job identity. Loading, synthetic frames, skipped helpers
and repeated helpers in one recording cannot silently cross a sample.
Unsigned difference one accepts adjacent generation wrap. Independent adjacency
state retains the chosen order without selector clocks through recurring gaps
or repeated helpers; it does not perpetually restart the first probe block. ReleaseCamera and
director release also clear cameraSource. Custom overrides must provide a stable
opaque nonzero cameraSource (never dereferenced), or explicitly zero it when
identity is unknown. Unknown overrides discard partial evidence, retain the
last choice and return without clocks instead of restarting an interleaved
first block indefinitely. Because these are public fields, an uninstrumented
custom script that overwrites a known camera's pose/override in the same frame
without assigning cameraSource can still inherit that owner's identity; the
selector cannot detect such a write. Custom camera code must assign its own
identity or zero explicitly. Scene generation,
requested/effective pipeline mode, video/field/buffer/BLSS/limiter mode, split
views, camera source/vehicle rig changes, structural portal eligibility and
portal view-count changes reset the entire partial probe. The previous chosen
order survives a reset; stale pairs and means do not. Continuous camera eye,
aim, shake and portal distance motion do not themselves invalidate samples.

Zero or ambiguous long clock intervals and pacing deltas exceeding elapsed
wall time are rejected. Ordinary unsigned clock/stall-counter wrap is allowed.
Block averaging and reversed order reduce noise; moving scenes can still
change the workload. The old physical results above describe the historical
selector and do not establish performance of this revised selector. Its
recording counter, guards and longer probes require root-owned hardware checks.

Changed decisions, or debug-profiler decisions, print for example:

    INTERLEAVE settled on=12478us off=13149us -> interleaved wins=4/4 heavy=34 drawn=42 blendAt=20

`heavy`, `drawn` and `blendAt` retain their historical meaning. The log's
295-tick divisor is approximate; decisions compare raw ticks.

## What keeps the picture the same

- **The blend gate.** The first object that may blend with the framebuffer
  gets every deferred bag drawn in front of it. "May blend" means any of
  these:
  - a vertex, material or single-colour alpha under 128;
  - a texture with any texel alpha under 127, cutouts included (judged once
    per texture from the uploaded pixels, CLUT entries through the GS's CSM1
    order);
  - a blend equation (`additiveBlendFix` / `subtractiveBlendFix`);
  - a z-test that does not write z.

  Such an object therefore still finds everything behind it already drawn.
  Every object before it is opaque and z-tested, so its order against the
  batches and roads cannot change a pixel.
- **Only the main pass.** Split halves, portal views, mirrors and the
  reflection probe draw their batches and roads at once. Collection runs only
  between the batch pass and the road pass of the main view.
- **One expected difference.** A cutout batch, such as a tree, can now draw
  after an opaque object behind it. Its bilinear edge then blends onto that
  object instead of onto whatever was there before, and the object used to
  lose those pixels to the tree's z. That can only be closer to right. PCSX2
  captures of Off and Always in the four poses: the scene is identical apart
  from 45 pixels (at most 25/255) at one distant tree's edge in outer day.

## Where it lives

- The codegen emits `INTERLEAVE_PASSES` (0 off, 1 auto, 2 always) into
  `terrain_config.hpp`.
- The game code is `submitHeavy`, `dripHeavy`, `interleaveBegin` /
  `ilAccount` / `interleaveEnd` and `objectMayBlend` in `src/game_templates.inc`;
  paired FPP/ORBIT declarations and camera-source emitters are in `src/templates.cpp`.
- The format is v64, and the key is written only when it is not `"auto"`.

The selector reads only the queue's ordinary-memory `recordingPipelined()`
boolean accessor. It does not poll hardware or add a queue wait. The historical
adaptive prototype that queried hardware from inside the object loop hung a
physical PS2 ([ee-submission-rearchitecture.md](ee-submission-rearchitecture.md),
"Where the EE waits, pass by pass"); that mechanism is not reintroduced.

## Portable host checks

Run `python tools/verify-tyrax2-host.py` with a host GCC/Clang C++17 compiler
on PATH, or supply `--compiler /path/to/g++`. Use `--only adaptive` or
`--only count` to select one group. The runner mechanically extracts the actual
selector and both declarations, verifies parity, and includes the actual arena
header for counted/ordinary copy controls. Temporary type shims compile hardware
tracing out; temporary binaries are removed. It never builds an editor/PS2 game
or runs an emulator/console, and does not establish target performance.

The output guard compares exact region, DisplayMode, ColorDepth and actual
buffer count, plus field/BLSS/limiter/widescreen/dither/network state. Geometry
keys are logical width/height, physical render height, raster width/height,
BLSS low-resolution dimensions and GS FRAME width/scissor extents. These are
separate fields, not a hash or packed key. Invalid nonfinite/nonpositive geometry
retains the last choice without clocks. Rotating framebuffer addresses and
per-frame jitter XYOFFSET are deliberately excluded so ordinary buffering and
temporal sampling do not permanently reset adaptation. Host controls use the
actual renderer enum declarations and verify scan/color/size/scissor changes
independently while holding region, field mode and buffer count constant.

Additional adaptive epoch exclusions: exact frameYield state is part of the mode guard. The portal topology byte includes each portal's actual live bit, so swapping drawn portal identities invalidates evidence even if view count stays constant. Active HardwareTrace capture and serialized render-cost requests discard partial pairs, retain the previous choice and skip selector clocks; the actual renderScene costSeq is passed explicitly. The selector uses one dedicated COP0 read with a compiler memory clobber; other profiler clocks are unchanged. These guards avoid comparing diagnostic/serialization epochs, but do not turn the inclusive loop score into a pure CPU, GPU or arithmetic cost.
