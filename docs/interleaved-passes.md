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

Every 100 frames the game runs 8 probe pairs: one frame interleaved, the next
plain. It keeps the order that won most pairs. Interleaving wins a pair only
by a clear 1%, because plain is the order everything else was measured in.

- **What is timed is the whole loop**, from one frame's batch pass to the
  next, minus the renderer's stall (vsync, display buffer; the new
  `RendererCore::getStallTotal()`). Timing only the passes that move missed
  the cost. Outdoors, interleaving costs nothing inside them and +0.19 ms at
  `endFrame`, where the GS tail that the object loop used to hide is waited
  for instead.
- **It counts pair wins instead of summing times.** A scene load or a
  texture-upload burst then decides one pair, not the verdict. The first
  version summed times, and a 33 ms load frame made it pick plain in the
  garage.
- **The hold is short on purpose.** 250 frames carried the garage's verdict
  into open ground, and that cost more than the probes do. The probes cost
  under 0.03 ms a frame.
- **Each decision is logged**, when it changes or with the debug profiler on:

      INTERLEAVE auto on=12478us off=13149us -> interleaved wins=8/8 heavy=34 drawn=42 blendAt=20

  `heavy` is how many batch and road bags were deferred, `drawn` how many
  objects they were spread over, and `blendAt` where the blend gate below
  flushed them.

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
  `ilAccount` / `interleaveEnd` and `objectMayBlend` in `src/templates.cpp`.
- The format is v64, and the key is written only when it is not `"auto"`.

The game never asks the VIF1 queue for its state. An adaptive prototype that
did, from inside the object loop, hung a physical PS2 so hard that ps2link
could not reset it ([ee-submission-rearchitecture.md](ee-submission-rearchitecture.md),
"Where the EE waits, pass by pass").
