# Pool2 EE lazy-color source review v1

Read-only review of `wild-pool2-ee-physical-v1` against the frozen `wild-pool-table-physical-v3` 499-file source manifest. All 499 baseline files matched their recorded SHA-256. Exactly five candidate source files differ. `source-review.json` records every reviewed final source hash, including the three refined hashes that supersede `root-preparation.json`. That preparation record is not a final freeze.

## Verdict

No blocking omitted materialization was found on the currently routed render/copy/clip/packet consumers. This permits the next private native and actual-output qualification gates; it is not native, emulator, DMA/cache, framebuffer, or physical performance acceptance. No builds, device actions, shared cache operations or production edits were performed by this reviewer. The separate root host result of 312000 actual-header differential checks is reported evidence, not a test executed by this review.

## Consumer closure

- `stapip_qbuffer.cpp`: fillByCopy1By3, fillByCopy1By2 and fillByCopyMax materialize each source range before memcpy. fillStripChunk materializes its input before indexed expansion. Copied buffers have no source table-slice admission and already contain expanded colors.
- `stapip_qbuffer_renderer.cpp`: cull establishes program/admission before selecting the table path; every rejected path materializes before later packet construction. The enabled comparator explicitly materializes before expansionMatches. clip materializes before either raw VU clip submission or EE clipToPool interpolation; EE writeChunk consumes already interpolated engine-owned colors.
- Packager and fillByPointer only transport pointers; capturePoolTable copies coefficient values into the qbuffer-owned 48-byte Slice, validates current color generation/count and exact contiguous source pointers, and does not read expanded bytes.
- Cull TC's table writer consumes vertices, ST and its qbuffer-owned Slice, never lazy expanded colors. Ordinary cull, as-is, clip, override, billboard and lighting routes fail table admission and receive materialized source colors through cull/clip. Billboard and override also refuse retained/baked entry setup.
- Retained replay uses poolLayout (including the arm bit) and poolGeneration. Baked keys additionally retain per-stream contentVersion; its poolGeneration distinguishes new coefficient bytes even when geometry/bbox remains unchanged. Whole-bag replay reads sealed encoded bytes, not lazy source backing. Copy/clip/cull routing precedes ordinary encoded packet conversion/snapshot, so there is no newly deferred materializer callback at native snapshot time.

## Producer and identity

The producer's complete key keeps ordered source pool identity, geometry/ST stamps, RGB and FIX, plus compact arm state. Geometry-only key keeps ordered identity and both source stamps. Member swap, count/order change, rebuilt arrays or movement therefore invalidate geometry; coefficient-only updates leave geometry/ST data and bboxVersion stable. Compact updates construct one coefficient/member, resize persistent backing only as needed, and skip the vertex loop entirely when geometry is unchanged. Noncompact path retains eager expansion. Off/On transitions invalidate the complete key and select the corresponding source pointer/contentVersion; source descriptor expanded/ready pointers are updated after resize.

The backing and readiness arrays live inside TerrainGame::PoolBatch, not temporary stack locals. The coefficient table is producer-owned, but the renderer copies it into each admitted qbuffer Slice before DMA packetization. There is no new thread, asynchronous callback or readiness writer. Core render flushes qbuffers before returning; the existing frame ownership/snapshot/DMA cache discipline still needs target proof and is not re-proven by this source review. Scene reload/destruction and repeated poolBatchFlush must be included in actual target controls.

The lazy generation uses the same global counter as BagArray. Zero is skipped and all ready words are cleared if this particular assignment receives zero. As with the existing stamp system, arbitrary complete uint32 counter-wrap histories are outside the short fixture proof; a wrap occurring in another producer could recreate an old lazy ready generation. Do not claim indefinitely wrap-safe lazy readiness from this implementation.

## Alpha and diagnostic limits

Each coefficient and materialized expanded color is memcpy-identical to the existing `Color(R*FIX/128,G*FIX/128,B*FIX/128,128)` value, including alpha128. The Pool2 descriptor still rejects nonfinite/negative RGB and wrong alpha. This does not eliminate the separately documented legacy uniform alpha129 versus direct-table alpha128 target-output qualification.

`collectCounters=true` plus `poolTableSelected=true` forces full eligible-range materialization for expansionMatches. Such correctness windows restore the EE work being removed and cannot establish its removal or its physical saving. Quiet performance windows must keep that oracle disabled and qualify routing separately.

Two diagnostic readers remain outside lazy materialization: StaPipQBuffer::getPrint and StaPipBagPackage::getPrint read color elements directly. No call site was found in the private static pipeline. They can show stale/default colors for untouched source backing; do not use those dumps as a color oracle or introduce them into correctness/timed windows without fixing their read contract.

materializeRange deliberately accepts pointers outside the producer range as already-owned copies. That is sound only under the audited routing provenance; the helper is not a generic allocation/pointer validator. A misaligned pointer into the source range is also treated as external, so malformed metadata defenses are narrower than the real valid-pointer path proof.

## Remaining gates

Actual extracted producer controls must compare geometry/color output and invalidations under RGB/FIX changes, member identities/order/count, arm transitions, movement, clipping, fallback and scene rebuild. Native builds must bind these final five hashes, correct runtime source mirrors, matching symbols/ELF and configured macros. Actual VU/GS output controls must include ordinary/table, moving camera/light and both EE/VU clip fallbacks, plus native snapshot and warm retained/baked cases with oracle disabled. Physical both-order quiet tests remain required, with exact workload/source/ELF and observer provenance. No physical benefit is claimed.
