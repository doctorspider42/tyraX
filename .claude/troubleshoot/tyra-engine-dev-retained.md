# troubleshoot/tyra-engine-dev-retained.md

Retained static command data (1.96.0) and its `STAPIPMISS` instrument. The baked
VIF stream is in `tyra-engine-dev-baked.md`. Symptom first.

## Retained static command data (1.96.0)

**Cause:** the static pipeline stopped rebuilding, every frame, the DMA/VIF commands
that did not change. Full account: docs/retained-static-commands.md.
**Fix — five things any edit here must keep:**
- **The block is CAPTURED, not re-derived.** `addBuffersDataToPacket` runs the ordinary
  builders on a miss and then memcpys the bytes they just wrote **out of the packet**
  into the entry. That is what makes the replay byte-identical by construction — for
  every program class, for strips and lists, and for a game-supplied program the engine
  knows nothing about. Do NOT "simplify" it into a struct that describes the packet
  format.
- **Copying finished DMA tags is legal because none of them names its own position.** A
  `CNT` tag counts the quadwords that follow it, a `REF` names an absolute address
  outside the packet, and with TTE the VIF codes ride in the tag's upper half. `NEXT`
  and `CALL` are the exception and the pipeline writes neither.
- **The retained storage is EE-private and is NEVER referenced by DMA.** Its REF tags
  name the bag's own arrays exactly as before, so this adds no new DMA-lifetime exposure.
  Only the CULL route is retained: a clip buffer's count word carries a camera-dependent
  plane mask, and a copied/merged/strip-expanded buffer points into that pool.
- **The key is every input the block encodes** — stream pointers, count, package size,
  `bboxVersion`, the resolved VU1 program pointer, the prim state, the Z scale
  (`RendererCoreDepth::scale`, which a display-mode switch moves) and the
  single-colour/strip flags. If you add anything to what a package's commands contain,
  add it to the key in `beginRetainedBag` in the same edit. Two invalidations are
  unconditional because a pointer compare cannot see them: a pipeline teardown
  (`deallocateOnUse`) and a VU1 clipping-mode switch both clear the cache outright. A
  recycled heap address with the same layout produces the same block, because the block
  carries addresses and counts and no vertex data.
- **The same capture-and-replay covers the clipping chain.** `addClipChain`'s fifteen
  quadwords — 52 float stores **per mesh** — depend only on the renderer's near/far pair
  and the guard band, so they are captured once. `init()` and `setVU1Clipping()` are the
  only places those inputs move and both drop the capture. Since 1.127.5 the capture is
  ALSO kept as a plain VIF stream (`clipBlockVif`: its CNT tags' DMA halves zeroed into
  VIF NOPs) and every bag REFs that one copy instead of copying it. It is rewritten only
  after `Vif1Queue::drain()`, because chains still in flight REF it.

## A retained block carried a position-dependent flag

**Cause:** 1.127.1. The buffer header's `VU1_STAPIP_EMIT_STATE_FLAG` ("send the GS state
again", set only when the program changed) depends on its POSITION in the packet, so the
replay re-flags it (`StaPipVU1Program::emitsStateFlag`, `kPackedCountWord`). Replaying a
strip block captured mid-run right after an expanded clip LIST drew the strip as a
triangle list: holes in roads, per camera, in PCSX2 too.
**Fix:** a new header word that depends on the previous buffer needs the same treatment.
Symptom and bisection recipe: docs/roads.md, "Holes in the road".

## Counts and the measured win

**Cause:** `StaPipCore::takeRetainedCommandHits/Builds/getRetainedCommandBytes`, and a
debug engine logs `STAPIPRET` every 300 frames (which makes a debug build the only
in-engine consumer of those counters — they reset on read).
**Fix:** `TYRA_STAPIP_RETAINED_COMMANDS = 0` restores the previous construction exactly
and is the A/B control arm. Measured in PCSX2 on the Motor District benchmark fixture,
three boots per arm: **18 `--capture-frame` images hash to one value**, and garage day
goes 27.889 -> 30.769 median FPS (35.86 -> 32.50 ms, **-3.36 ms**) against a 0.222 FPS
control spread and a **0.001 FPS same-build repeatability**; 76 % of the frame's package
command blocks replay (`retained=772 rebuilt=246, cache=106 KB`). The first shape
reserved 8 quadwords per package and never evicted; it measured **1.35 FPS slower** than
the shipped 7-quadword + bounded-LRU one. **PCSX2 cannot price this change fully**: it
emulates no EE data cache, so an emulator delta is an upper bound on the hardware saving.

## `STAPIPMISS` answers WHY a bag rebuilt

**Cause:** a cache that never converges contaminates whatever is measured next, and "it
churns" is not actionable — the field that moved is.
**Fix:** `STAPIPMISS` tallies one reason per invalidation (`bbox`, `prim`, `streams`,
`program`, `size`, `new`, `incomplete`) and records the largest bag that moved, by
vertex and package count, so a mesh can be named by its size rather than by a heap
address. Measured on the Motor District garage, held pose: **one** `bboxVersion` bump and
**two** prim-state collisions per frame, zero everywhere else, costing 64 rebuilt
packages because those three bags are 31 and 51 packages each — and at the outer-road
pose, zero of everything with every drawn direct bag replayed. Both mechanisms are known
shapes: `bboxVersion` bumped unconditionally is the defect docs/wheel-rebake-skip.md
records, and a second pass over one vertex array thrashes the single entry the cache holds
per (array, package size). Keep this counter when you touch the key.

## `beginFrame`/`endFrame` sleep only while the frame yield is on

**Cause:** they sleep 0.5 ms each only while `RendererCore::setFrameYield(true)` — the
generated game sets it from `livedbg::attached()`. Off it is -1.05 ms a frame; a sleepless
loop with the Live Debugger attached hung the console (SIF stuck) and nobody knows why yet
(docs/backlog.md).
**Fix:** anything else that talks to host: every frame may need the same yield — test it
attached, on hardware, for minutes.

## 2D sprites in the VIF1 chain cache GS state

**Cause:** `TYRA_2D_CHAIN_FAST`, `RendererCore2D::renderIntoChain`: a sprite writes
XYOFFSET/TEX1/ALPHA/TEX0 only when they differ from what the open chain last set, and
`closeChain` restores the 3D XYOFFSET.
**Fix:** anything new that writes those GS registers mid-frame must close the chain first
(path3Fence does) or the next sprite inherits the wrong state.
