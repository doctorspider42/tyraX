# A08 paint lifecycle evidence

This archive preserves a private C16 lifecycle correction and correctness tests,
not a new physical performance measurement or production-default change.

V2 separates accepted basis from EE colour readiness and provides conservative
legacy/custom fallback. Host ASan/UBSan passes 6000 mode transitions and a stale
colour negative control. Three full-map emulator phases check 12,124 paint
submissions with zero errors, including 62 held-basis EE materializations.
All six requested modes and both actual authored LOD tiers are exercised. The
three authored vehicle body TMDLs are included to bind the LOD coverage claim.

The source binding covers 468 files. ELF SHA256:
`f1419187da7901ce5cb67c20f462ac3fa44797fd8c5b3777c12270f659622e7f`.
Raw CSV, phase witnesses, screenshots, source/ELF binding, host/build logs and
analyzer results are included. V1 generation failed before build/deploy on an
include anchor; its incomplete fixture remains outside this accepted snapshot.
`manifest.json` hashes all exported files except itself.

No PS2 deployment was attempted: A06's console still reports freepad DMA Busy.
No timing, full-scene pixel equality, arbitrary-custom-shader or night/fog
qualification is claimed. Same-pointer mutation is host-tested, not injected
into live geometry. See the [paint report](../tyrax2-reflection-turn.md) and
[decision register](../tyrax2-performance-investigation.md) for the next gates.
