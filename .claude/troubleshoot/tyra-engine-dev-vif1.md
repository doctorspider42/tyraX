# troubleshoot/tyra-engine-dev-vif1.md

The VIF1 submission queue, DMA REF lifetime and the double-buffered copy-pool
slot. Symptom first. The retained/baked submission caches are in
`tyra-engine-dev-submission.md`.

## The VIF1 submission queue (1.125.3)

**Cause:** `Vif1Queue` (`renderer/core/paths/path1/vif1_queue.{hpp,cpp}`,
`TYRA_VIF1_QUEUE`, on by default) lets StaPip keep up to four packets in flight
instead of waiting for the previous transfer before every send. Each packet is still
its own complete chain ending in END, and the EE starts the next one at its next
submit or wait. Physical PS2: `work` -0.50..-0.72 ms in the four Motor District
poses (docs/ee-submission-rearchitecture.md).
**Fix (four rules any edit must keep):**
- **Every VIF1 user calls `Vif1Queue::drain()` before touching the channel**, and no
  longer calls `dma_channel_wait(DMA_CHANNEL_VIF1, 0)`. A plain channel wait can
  return between two queued chains and then start a chain under the queue. Every
  engine site and the generated game's `projClamp` barrier were converted. In the 0
  arm `drain()` IS the old wait, so code is written once.
- **"The other buffer" is gone, so stop writing `side ^ 1`, `sides 0 and 1` or
  `!context`.** Packet buffers and qbuffer copy-pool sides now number
  `kPacketCount` / `kPoolSides`. The one place that still said "0 or 1"
  (`deallocateDynamicData`'s pooled test) freed arrays the pool still owned, and the
  retained cache that reused that heap then fed VIF1 vertex floats as tags
  (`Unknown VifCmd 3f` in PCSX2). A chain validator walked in `sendPacket` (every
  CNT/REF payload against the UNPACK/DIRECT size its VIFcodes consume) found it in
  one boot. Build that validator again before debugging a VIF1 error by reading code.
- **Anything a chain REFs must stay unchanged until that chain has run, which is now
  up to three packets later.** That is why `sendObjectData` copies the MVP, the light
  matrix and directions, and the single colour inline in queue mode
  (`kInlineUniforms`) instead of REF'ing renderer storage the next bag rewrites. It
  costs +0.23 ms of `prepare` at night. The copy pools are covered by the
  per-context sides and baked arenas by their two-frame graveyard.
- **The write-back is lazy** (`TYRA_VIF1_QUEUE_LAZY_FLUSH`, on, -0.26..-0.51 ms):
  `Vif1Queue::start` calls `FlushCache(0)` only for a chain submitted after the last
  write-back, which then covers everything queued behind it. So nothing may write a
  SUBMITTED chain or anything it references. `start` must never run in an interrupt
  (FlushCache is a syscall), and a static_assert ties the two flags together.
  **Depth 8 measured worse than 4** (+0.19..+0.41 ms, all in `prepare`), so do not
  deepen it without a new reason.
- **2D sprites ride VIF1 too** (`TYRA_2D_VIF1_DIRECT`, on, 1.126.1): once VU1 is up
  `RendererCore2D` appends each sprite's GIF packet to a VIF1 chain as `NOP` +
  `DIRECT`, opened by `FLUSHA`, and submits it through the queue instead of draining
  PATH1 and sending over PATH3 (work -0.37..-0.50 ms on a PS2). So **every
  GIF-channel send calls `path3Fence()` first** (`paths/path3/path3_fence.hpp`); it is
  free when no 2D is pending. A new PATH3 sender that skips it can overtake queued
  sprites. An OPEN 2D chain is submitted by the next `Vif1Queue::submit()`/`drain()`
  from anyone (`setOpenChainCloser`), and `endFrame` fences before the vsync wait.

## `endFrame` left up to three chains queued and unstarted

**Cause:** 1.166.1. `path3Fence()` drains only in a frame that drew a sprite, so a
scene with NO 2D at all left up to `kPacketCount - 1` chains queued. The next frame's
first submit started them after the present, the draw-target switch and the clear had
gone out on PATH3 — so the frame's last bags drew into the NEXT back buffer. Opaque
z-tested bags survive; blended no-z-write ones vanish: **baked shadow decals and blob
shadows invisible in examples/baked-shadows**, with every byte the decal bag sent and
every VU1 memory word identical to a good frame.
**Fix:** `endFrame` drains the 3D queue too, not only the 2D fence. What found it was
a PCSX2 GS dump walked per draw (pcsx2-capture.py's `GsWalker`): the decal's 858
kicks sat right after the next frame's `FRAME_1` write. **"Every byte matches but
nothing shows" means look at WHEN the GS gets it, not what.** A new frame-boundary
PATH3 sender must come after this drain.

## `TYRA_VIF1_QUEUE_HOLD` is a measurement probe, never a mode

**Cause:** the queue only collects chains until the EE first waits, and `endFrame`
logs the frame's VU1+GS time alone (`GPUHOLD us`). At `TYRA_VIF1_QUEUE_DEPTH 128`
the extra packet buffers and pool sides threw `std::bad_alloc` on a console. Its
`work` is meaningless (the frame is serialised on purpose).
**Fix:** run it with `TYRA_VIF1_QUEUE_DEPTH 80`. It measured the GPU at ~40% of the
EE's time (docs/ee-submission-rearchitecture.md, "The GPU-only frame").

## A bag's texture wrap written by draining

**Cause:** `StaPipQBufferRenderer::setBagWrap` (1.126.3) writes `FLUSH` plus `DIRECT`
A+D `CLAMP_1` ahead of the bag's uniforms. The GS wrap cache (`currentTextureWrap`)
follows when the packet is SUBMITTED, not when it is built, because an unsent packet
(a culled bag) is thrown away with its write.
**Fix:** anything drawing over PATH3 that relies on the wrap must `align3D()` first,
as every current consumer does. The old lazy drain cost the EE 1.28 ms in Motor
District's garage night.

## Object data written by packet2 calls

**Cause:** 1.127.2, `emitUnpack` in stapip_qbuffer_renderer.cpp. Per `packet2_*`
open/close the cost was ~1.4 us a bag, the data itself little.
**Fix:** headers are produced once by packet2 into a scratch packet and cached per
(VU address, length). Change a uniform block's address or length and a new header is
made by itself; change packet2's layout conventions and the VIF-hash gate
(docs/baked-stream-acceptance-gate.md) is how you prove identity. The cache holds 32
headers and asserts when full.

## `popEnvView` rebuilt the frustum planes from the caller's camera

**Cause:** the shadow pass passed no `up`.
**Fix:** `popEnvView` restores the frustum planes its push saved.

## The slot pool is double-buffered (1.81.1) — the console-only sliver

**Cause:** the renderer's OWN copies are referenced too. `StaPipQBuffer::fillByCopyMax`
/ `fillByCopy1By2` merge small in-frustum packages by copying them into a per-slot
pool (both clipping modes), `fillByCopy1By3` / `writeChunk` copy for the EE clipper,
the packet's REF tags point at the pool, and `flushBuffers()` reset the slot indices
the moment the packet was *sent*. The next bag copied into slot 0 while slot 0 was
still being read, and the DMA picked up a vertex of the next object: a lamp's corona
drew as a sliver to the screen corner in 4 of 24 frames with Sony's `vcl` and up to
19 of 30 with openvcl, and the EE clipper drew screen-sized slabs. PCSX2 completes a
DMA before the EE runs on and shows none of it.
**Fix:** the pool now has two sides and `StaPipQBufferRenderer::sendPacket()` flips
the side with `context` (`StaPipQBuffer::flipPoolSide`); the wait before that send is
what makes the side being written the finished one. **Do not "fix" this class with an
EE-side `dma_channel_wait` per bag: measured at -4 FPS.** Two cheap barriers came with
it: a `FLUSHE` at the head of the StaPip and DynPip uniform chains, and a VIF1 wait
before the projected-shadow pass rewrites its shared `projClamp` buffer. A
hardware-only defect wants a **failure rate on a parked pose**, never a single frame;
and **bisect with barriers** (GS FINISH per bag, VIF FLUSH per bag, VIF-stream-only
FLUSH, EE wait at one site) before reading a line of microcode. Story, fixture and
bisection table: docs/vu1-clipping.md, "Real hardware: the slot-pool race".

## DMA REF lifetime (1.74.1)

**Cause:** `packet2_utils_vu_add_unpack_data` does NOT copy: ps2sdk emits a DMA REF to
the supplied pointer. SH initially passed a temporary/local array to asynchronous
submission, causing lighting flashes despite passing VU arithmetic tests.
**Fix:** never pass a temporary/local array to asynchronous submission. Both lighting
senders now use CNT/UNPACK with inline colour floats (four extra packet-storage
qwords; the same VU layout). DynPip waits before resetting its reusable packet; StaPip
keeps inline values in its double-buffered combined packet and does not reuse that
side until the intervening submission has completed. Before 1.93, StaPip allocated
185 qwords per side (57 for uniforms/barrier plus 128 for one 16-group half); 1.93
uses 784 qwords per side for bounded cross-bag submission; DynPip's uniform packet
capacity is 24 qwords. The 1.80 merge retains inline SH colour storage with deferred
StaPip uniforms; since 1.86.4 that data shares the first geometry packet. It is safe
to reset because sendPacket waits for the prior VIF1 DMA before flipping contexts and
the other packet is used while the submitted one drains; DynPip retains its own
wait-before-reset contract.

## Do NOT start chains from a DMAC interrupt on this engine

**Cause:** `TYRA_VIF1_QUEUE_ISR`, default 0. PCSX2 ran it clean. A physical PS2 under
ps2link took an EE exception twice, both times at the instruction right after
`EIntr()`. First, `ExitHandler()` (`ei` inside the handler) let a nested interrupt
corrupt the interrupted thread's `s0`. Then, without it, it was an instruction fetch
at address 0 with `Status.EXL` set, i.e. inside the kernel's interrupt dispatch.
**Fix:** leave it off. **Read an EE exception taken with EXL set as "in an
interrupt"**: EPC is not updated while EXL is set, so it names the interrupted
main-thread instruction, not the fault; `symbolize EPC` then points at innocent code.
