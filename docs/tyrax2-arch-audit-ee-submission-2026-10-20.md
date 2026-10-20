# Architecture audit, EE submission items (2026-10-20)

Four items of the TyraX2 architecture audit (2026-10-20) that live in the
static pipeline's EE submission: **R3** producers writing into the frame bank,
**R1** static bags as immutable chains with a shared view-projection, **R5**
crossing bags replayed whole, and the **stamp slot** in the bag. Each was built
behind a runtime switch and measured on the physical PS2 with one ELF that
cycles through the arms (the `C:\tx\rig` night drive, see
[night EE notes](tyrax2-night-ee-2026-10-12.md), "Rig").

## Baseline (route profile)

Lean build, district spawn fixture, night, 15 cycles, per frame while
driving (`--routetime`):

| Route | Bags | ms | Each |
| --- | ---: | ---: | ---: |
| Stamp | 97.9 | 2.87 | 29 us |
| Partial list | 6.2 | 1.14 | 184 us |
| Partial strip | 6.7 | 1.17 | 175 us |
| Direct | 3.0 | 0.52 | 174 us |
| Whole replay | 4.3 | 0.36 | 84 us |
| `Vif1Queue::submit` | 32.7 | 0.89 | 27 us |

Frame: parked work 10.28 ms, driving 10.56 ms (medians over windows).

## R3: packets built in the frame bank

What changed (`TYRA_VIF1_OWNED_PACKETS`, `Vif1Queue::setOwnedPackets`):

- While a frame is recorded, `StaPipQBufferRenderer::startPacket` points the
  empty packet at a reservation of its worst case (784 qwords) cut from the
  current frame bank (`Vif1Queue::reservePacket`). The producer writes the
  packet once, where the DMA will read it.
- `sendPacket` hands it over with `Vif1Queue::submitOwned`. The unused tail
  goes back to the bank (`FrameChainArena::shrink`), and the single-pass
  recorder records every CNT payload as a REF to where it already lies
  (`recordPass<true>`) instead of copying it into the bank first.
- Lifetime needs nothing new: the bank is reused only after its last chain
  completed. A `drain()` mid-frame keeps an open reservation taken
  (`keepUntil`); a bank switch invalidates it, and a packet whose reservation
  went stale takes the ordinary (copying) submit.
- The 2D chain that `closeOpenChain` submits is copied into the bank AFTER the
  packet is committed, never over it.

packet2's float-at-a-time back-patching is not on the hot path any more: the
stamp replay and the object-data fast path (`emitUnpack`) write whole
quadwords, and in cached bank memory a back-patch costs what it costs in the
producer's own buffer.

Result: see "Measurements".

## R1: chain stamps and the shared VP block

What changed (`StaPipCore::setStampChain`, default on):

- A stamp whose head is the plain layout - FLUSHE, the MVP unpack, CNT blocks
  (no spot block, no wrap write), the body REF - keeps the part after the MVP
  as a VIF stream in DMA memory: two copies per stamp slot
  (`stampDmaHeads`, 12 qwords each, registered as an immutable span).
- A replay emits three quadwords for an identity-model bag: a REF to the
  frame's **VP block** (FLUSHE rides the REF tag; the block is the MVP unpack
  with the view-projection), a REF to the head copy, and the body REF. A bag
  with its own model matrix still inlines FLUSHE and its MVP (6 qwords).
- The replay keeps the fog words, TEX0 and the single colour as shadow values
  in the entry. When one changes, the OTHER copy is rewritten - only if no
  chain of the last three frames named it - otherwise the replay copies the
  head as before. VP blocks come from a 32-entry ring under the same rule.
- No VU1 address or microprogram changed; the VIF word stream differs only by
  VIF NOPs.

## Stamp slot in the bag

`StaPipBag::stampSlot` remembers the stamp-table slot; `findStamp` and
`hasStamp` check that slot's own bag pointer before the hash probe
(`setStampSlotHint`).

## R5: crossing bags replayed from immutable blocks

What changed (`StaPipCore::setPartialReplay`):

- A crossing bag (VU1 clipping, whole-package streaming) gets a second baked
  entry: every package formatted for the streamed clip program with all six
  planes. The plane mask is the only camera-dependent input of a clip block,
  so the variant is baked once (`beginPartialReplay`). The baked cache now
  tells passes apart by program as well, so the cull and clip variants of one
  array coexist.
- Mode 2, **whole**: the bag is ONE REF into the clip program. No EE
  classification at all; VU1 judges every triangle.
- Mode 1, **runs**: the packager still classifies per package (rejection pays
  for itself), but each run of same-route packages is one REF into the cull or
  the clip blocks. A run that follows the other program regenerates its first
  block's header (GS-state flag) and kick (MSCAL); a continuing run is one REF.
  No qbuffer, no per-package command writing.

## Measurements

(filled in below per hardware run)
