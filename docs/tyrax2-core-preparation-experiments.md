# Core preparation optimization experiments

Private same-ELF experiments follow the [five-part bounds partition](tyrax2-core-prefix-partition.md#five-part-bounds-follow-up-kind15), preserving full night and existing waits. Each candidate toggles independently in Off/On/Off and On/Off/On phases; all enabled clocks remain ordinary sampler clocks. Sparse numerical checks run only at750/1155, outside the priced320-loop interval. Common helper, code-layout, stack and branch footprint is retained and unpriced.

## Exact matrix-key comparison (kind16)

An aligned64-byte matrix equality helper uses EE integer lq/pxor/por/sq instructions, compares every bit and does not use COP2 or claim VU ownership. The original pointer guard and model/view-projection key values remain. Each boot validates all512bit positions under8 patterns (4104 cases), including zeros, signed zeros and NaN payloads. Sparse real-key comparisons also check against memcmp; no additional scoped clock reads are introduced.

Both physical orders complete5400 loops,384 raw samples and15 chunks. Candidate On minus mean Off is +0.001925/-0.026646ms; outer spreads0.045971/0.024041ms. There is no stable resolved gain, so this candidate is not promoted. Both emulator orders complete with zero oracle failures and six reviewed, qualitatively normal phase images. Emulator clocks are not accepted as PS2 prices. No new phase-bound human console image is claimed.

## Clip-plane specialization (kind17)

The original loop computes eight object-space planes using four coefficient products per normal component and a separately leading distance offset. Exact near is -z+w and exact far is z+w. Preserve zero products, signed-zero behavior, addition order and the distance-leading term when testing a replacement.

| Variant | Qualification and result |
|---|---|
| V1 compile-time coefficients | Rejected before runtime: erroneous exact-far w sign. Host test found981498 mismatched plane outputs in8000000 comparisons. |
| V2 corrected coefficients | One million finite host matrices/eight million plane comparisons pass; both emulator orders pass with six reviewed normal phase images. Real PS2 reports451 mismatched plane outputs at phase1 offset750 and completes with valid=0. Rejected numerical candidate; its elapsed data is not an accepted price. |
| V4 row-shared products | Both physical orders pass the sparse bit oracle and complete5400 loops. On minus mean Off is +0.008243/-0.179929ms; outer spreads0.072246/0.025617ms. The gain does not repeat across the pair; additional confirmation is required before promotion. First reverse launch had no start evidence after network reset and is excluded; physical-reset retry completed. |
| V3 forced mul.s products | Preserves actual coefficient multiplies while allowing compiler reuse. Both physical orders pass their sparse bit oracle and complete5400 loops. Candidate regresses +0.132158/+0.172314ms, with outer spreads0.043577/0.048003ms. Not promoted. |

Target disassembly of V2 shows constant-product strength reduction in the specialized path while the original generic loop retains mul.s. Forcing those products restores agreement in the V3 cold witnesses. This is evidence about these compiled paths, not a complete standalone characterization of R5900 rounding or every FPU instruction. The first differing field/instruction was not captured. Host mathematical equivalence and emulator output cannot supersede a failed actual-target oracle.

V4 transposes work by matrix row and shares its hardware-rounded0/1/-1/band products across all eight planes, retaining forced mul.s and original add order. Its nonalias contract follows the private function's two callers, which pass local MVP and separate Core plane storage. Both target orders agree with the original plane bits, but physical improvement is not repeatable across this pair. No V4 emulator raster or new human raster qualification is claimed; V3's numbers cannot price V4.

## Evidence

[Source and runtime archive](tyrax2-core-preparation-experiments-2026-10-05/) binds private four-file postimages,501-input native manifests,298 assets, actual linked ABI and unchanged resident VU images, both physical matrix/V3/V4 orders, both emulator matrix/V2 orders, host parser controls and rejected V1/V2 evidence. Binaries are hashed rather than committed. No production renderer or default changes are made here. There is no60Hz qualification or general moving/walking/showcase claim.
