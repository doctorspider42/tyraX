# troubleshoot/tyra-engine-dev-submission.md

Static submission on physical PS2 (1.78), bounded cross-bag batching (1.93), the
EE-submission bounding probes (2026-09-16), and the coarse AABB / package-bbox
cache. Retained commands and the baked stream are in
`tyra-engine-dev-retained.md` and `tyra-engine-dev-baked.md`. Symptom first.

## Static submission on physical PS2 (1.78)

**Cause:** the 2026-09-27 per-bag A/B kept two small `sendObjectData` cuts: omit the
three spot quads when the light is inactive, and omit the shared clip block unless the
bag can enter a VU1 clip program.
**Fix:** a project program override keeps both uploads; direct calls to
`sendObjectData` default to the old clip upload. Mode 24 saved 0.04..0.13 ms of `work`
over two hardware boots. Retaining copies of each bag's uniform groups instead added
0.30..0.80 ms and was rejected; do not transplant its DMA lifetime machinery into
production for this fixture. See docs/ee-submission-rearchitecture.md, "Round six".

## `StaPipQBufferRenderer::sendObjectData` prepares uniforms at the head of the packet

**Cause:** it prepares uniforms without waiting for the previous mesh, at the head of
the current double-buffered geometry packet.
**Fix:** the first buffer flush appends to that packet instead of resetting it, writes
one END and submits once after the ordinary VIF1 wait. A wholly culled mesh may replace
the unsent packet. Never let a draw escape `render()` with pending geometry that
references stack MVP/light data, and keep the packet capacity at least the worst-case
uniform chain plus one 16-group qbuffer half. The 1.93 explicit scope additionally
copies transient uniforms inline and requires retained external streams.

## A per-REF `SyncDCache` walk regressed a hardware capture

**Cause:** every submitted combined or geometry-only packet uses the SDK's full D-cache
writeback because its REF streams may point outside packet storage.
**Fix:** a trial walking every REF with `SyncDCache` regressed a hardware render-cost
capture from about 58 to 79 ms; it was discarded. Measure cache policies on EE. ps2sdk
is AFL-2.0, so a fork is permitted (this repo already vendors AFL-2.0 PS2SDK code in
`tools/toolchain/bin2s`), but `SyncDCache` is not the range operation its signature
suggests — `_SyncDCache` loops over all 128 cache indices in both ways.

## The coarse AABB / package-bbox cache

**Cause:** VU1 clipping references immutable source streams, like culling; legacy EE
clipping still copies into writable qbuffers.
**Fix:** coarse AABBs cover eight full packages (24 one-third bounds), follow
`bboxVersion` rebuilds, and only inherit whole-IN/OUT decisions; partial groups keep
exact child/guard-band tests. The package-bbox cache uses a fixed 256-bucket hash index
over buffer id and VU package capacity. Buckets and per-entry collision links store
vector indices, not pointers: vector relocation stays safe, hits allocate nothing, and
the index is rebuilt only when expiry compacts storage. Keep `bboxVersion`, vertex
count, capacity and the 250-frame unused lifetime checks intact; together they protect
rewritten buffers, capacity variants and recycled addresses. The index costs 1,024
bytes plus four bytes per retained entry on the 32-bit EE. Wholly visible bags skip
redundant package classification and, since 1.86.3, point each qbuffer directly at the
bag's contiguous source range instead of constructing unused pooled package descriptors
(partial and EE-clip paths still use packages and writable copy pools). Spatially
coherent triangle order makes the coarse level useful without altering triangle data. On
portal/mirror-free Aster this direct path was hardware-neutral across five settled
captures (Total 34.349 -> 34.342 ms, Objects 25.051 -> 24.951 ms) and ran beyond 3360
frames; describe it as removed work, not a proven FPS gain.

## The native combined first packet (1.86.4)

**Cause:** since 1.86.4 the native combined first packet removes one VIF1 kick and one
EE wait per visible bag.
**Fix:** five settled physical Aster captures moved median DMA submit 3.034 -> 2.029 ms,
Dispatch 16.827 -> 15.544 ms and VU1 wait 5.847 -> 5.130 ms; Total stayed fill-bound
near 34.3 ms. PCSX2 alone is not acceptance for this path: the rejected NEXT variant was
faster there and failed hardware. `Math::sqrtNonNegative` uses EE `sqrt.s` only for
known nonnegative squared lengths — do not substitute it for a general sqrt API with
errno/domain behavior. Render-cost telemetry includes both uniform and geometry VIF
waits; counting only the latter under-reports synchronization, and the counters overlap
stages.

## Bounded cross-bag submission batching (1.93)

**Cause:** `StaPipCore::beginSubmissionBatch()` / `endSubmissionBatch()` are an explicit
ownership scope.
**Fix:** generated callers may use them only around direct static bags whose vertex,
texture-coordinate, colour and normal streams remain alive and unchanged through the
next VIF1 synchronization. End submits asynchronously; it is not a completion fence.
Close the scope before view changes, GS operations, render-cost drains or pipeline
switches. The packet stays one native contiguous chain. Every new bag retains
`FLUSHE -> inline uniforms -> geometry`; never join separately allocated packets with
DMA `NEXT`/`CALL`. MVP, light matrices/directions and single colours are inline only for
retained bags. Partial-frustum classification, VU1/EE clipping, qbuffer copies,
billboards and game-supplied program overrides are excluded. Overrides have no bounded
qbuffer writer ABI, so the built-in eight-qword command bound cannot be assumed. Treat
any installed override as a global batching exclusion. `setOverride(slot, program)` does
not require the program's self-reported name to match `slot`, so querying the resolved
program name can miss the override; use the repository's cached any-override state.
Resident textures may batch only when both wrap axes are REPEAT. A texture miss, upload,
update, free or eviction invokes the active StaPip mutation barrier: submit pending
geometry and `align3D()` before VRAM contents or addresses change. Track
already-submitted textured batches too; `objectDataPending == false` does not mean the GS
finished sampling. Register this hook in `allocateOnUse`, clear it only for the owning
pipeline in `deallocateOnUse`, and drain readers before clearing it. Keep batching
bounded to four direct bags of at most 15 VU packages each. The physical-PS2 large-bag
experiment reduced submissions but regressed garage render-submission time from
37.286/43.546 ms to 38.264/44.580 ms. Packet build added about 0.2 ms and the garage VIF1
DMA wait rose about 0.6 ms. The final cached override flag and early eligibility gate
passed the physical four-pose gate: submission saved 0.95/1.14/0.37/0.34 ms. This is not
evidence of a whole-game FPS jump. See the report for the 240-eviction/12-switch stress
test.

## The EE-submission bounding probes (2026-09-16) — two directions CAPPED

**Cause:** `TYRA_STAPIP_PROBE_*` in `stapip_probes.hpp`, all default **0** and none of it
ships (same zero-cost gate as `TYRA_STAPIP_ATTRIB` — `#ifndef NDEBUG` is NOT the gate in
this engine). Physical PS2, six hashed ELFs, 0.135 ms repeatability floor. Full account:
`examples/vehicle-playground/authoring/ee-probes-2026-09-16/README.md` and
docs/ee-submission-rearchitecture.md.
**Fix / measured negatives:**
- **Per-package frustum rejection PAYS FOR ITSELF: it buys 2.60 ms and the whole
  `checkFrustum` bracket costs 1.79 ms** (garage day, `work`). Most of what rejection
  buys is VU1, not EE: 1.09 of the 2.60 ms is VIF1 wait.
- **CLASSIFYING MORE COARSELY IS A NET LOSS — do not re-open it.** One classification per
  existing eight-package coarse group, reused by all eight with rejection and clip
  routing intact, measured **+4.59 ms** while adding FEWER triangles than the accept-all
  arm (`packet` +1.08 ms, flushes +26).
- **A "1/3-bbox part" is one third of a PACKAGE.** `StaPipBagPackagesBBox` splits a bag
  into `maxVertCount / 3`-vertex parts and a full package spans exactly three of them, so
  "classify per part instead of per package" is three tests where there is one. The coarse
  level that DOES amortise already exists: one box per 24 parts = 8 packages.
- **An arm that forces `IN_FRUSTUM` without running the test is CORRUPT, not a probe.** It
  routes near-plane-crossing geometry to the cull programs, which do not clip. Map only
  the `OUTSIDE_FRUSTUM` verdict and leave `PARTIALLY_IN_FRUSTUM` alone.
- **`FlushCache` is worth 0.84 ms of the send bracket / 1.09 ms of work** — and
  **dropping it CORRUPTS THE PICTURE even with the packet allocated `P2_TYPE_UNCACHED`**:
  1 271 of 262 144 pixels, a 14-row band at the horizon. The same build that stays
  uncached and still calls `FlushCache` is BYTE-IDENTICAL to the control. **Unresolved —
  answer it before building anything that removes the flush.**
- **Uncached packets are SLOW: +7.55 ms** (packet2 writes floats one at a time and an
  uncached store does not gather).
- **`P2_TYPE_UNCACHED_ACCL` IS UNSAFE WITH THE STOCK ps2sdk BUILDER.**
  `packet2_vif_close_unpack_auto` does an `lbu` of byte 3 and an `sb` into byte 2 of the
  open unpack VIFcode (disassembled from `ps2sdk/ee/lib/libpacket2.a`); under UCAB those
  go through the EE's 128-byte write-gather buffer.
  `P2_TYPE_UNCACHED` has no write-gather buffer and is the safe arm.
- **`packet2_create` asserts `qwords % 4 == 0` for EITHER uncached type.** `packetSize` is
  784 today; keep `kWorstBagPacketSize * kSubmissionBatchSize` a multiple of 4.
- **A parked fixture cannot see one whole class of flush-removal bug**: a per-frame rebake
  that writes the SAME bytes every frame leaves stale lines indistinguishable from fresh
  ones. Re-run any flush experiment with `--keep-routes` before calling it correct.
