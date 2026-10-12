# A25 retained-preparation clip consumer test

This private diagnostic retains EE clip preparation and changes only final
microprogram selection, to close the clipping-consumer question before returning
to architectural EE work.

## Protocol

Derive from bound A23, restoring its original qbuffer gather implementation.
StaPipCore is byte-identical to the source fixture. Mode 7 selects the ordinary
cull program at the original VU clipping dispatch; no early classification,
subdivision, strip expansion, input ownership or queue lifetime is bypassed.
Custom programs and billboards are rejected. Six trips run 0/7/0 in day/night,
with real-dt gas/camera motion, three vehicles, roads, EE paint and live captures.
Sparse observations exclude the observed and subsequent frames from pricing.

Each final held pose runs five cold raster arms 0/7/0/0/0. The final settled
frame hashes ordered clip input counts, masks, topology and all active streams;
all arms must agree. All baseline/return main and reflection rasters must be
exact. Candidate differences are allowed: the cull consumer discards crossing
triangles. Hash equality is a diagnostic witness, not a mathematical collision
proof; source binding also enforces unchanged preparation implementation.

The result cannot isolate VU cycles: program-dependent EE emission, scheduling,
GS output, waits and moving camera cadence remain coupled. No timing is inferred
from PCSX2. No production default or editor version changes.

Native binding matches 476 source files, ELF SHA256
`72e64418e8a8df459ab7c1cd01af5c04e2b6856beded47c778b0c6e46833ea3b`.
The unchanged StaPipCore SHA256 is
`4b7745838bfc097172d9b88f79e36158a7096ab60cd4eebffbce38e15f3f99cd`.
The input observer passes 56 actual-header host controls under ASan/UBSan.
Hidden emulator and physical PS2 complete all six trips and 60 raw GS images
each with equal cold inputs and exact return controls. Both readers reject 13
negative controls. Candidate images differ substantially, as expected. Physical
editor-protocol launch confirms owned client 21484; it was stopped after complete
export, as were owned emulator 368/Xvfb 357. Console/computer remain powered on.

## Physical result

| Light | Baseline work, first/return ms | Consumer-cull work ms | Saved mean / spatial ms | Candidate entry Hz |
|---|---:|---:|---:|---:|
| Day | 18.270390 / 18.346738 | 18.461150 | -0.152587 / -0.158010 | 36.306973 |
| Night | 20.285700 / 20.404840 | 20.446570 | -0.101300 / -0.121424 | 33.694280 |

Work is elapsed whole-loop ticks minus measured pacing; not pure EE arithmetic.
There is no useful gain from the final consumer swap. Night candidate p95 is
27.789513 ms versus controls approximately 27.46/27.59 ms. This closes this consumer-bypass
proposal and supports returning to EE preparation. It does not prove every VU
or GS operation is costless, nor locate a specific EE defect.

A24 full-route omission saved about 3 ms while this retained-preparation contrast
does not. Their ELFs differ: do NOT subtract them to publish an exact EE cost.
No default/production/version change follows from destructive missing geometry.

The [637-file immutable archive](consumer-v1/archive-sha256.json) preserves
bound sources, ELF, both complete logs/CSVs/readbacks, launch pins and the full
fixture/reader/tool closure. Both read-only archive reanalyses reproduce the
original JSON reports byte-for-byte, without changing the archive inventory.

## EE source audit

Do not conflate a shared owner with identical geometry. Terrain base/layers/AO/
emission bind the same vertices and pin their package boundaries, but ground
shadow geometry is a separate shaded-cell subset, lifted 0.02 world units. It
must retain its own classification. The current example source implements this
in game_physics.gen.cpp at 5521-5621; renderTerrain submits each existing pass.

The earlier accepted A02b physical ledger contains zero repeated complete keys
`phase/offset/view/vertices/sourceCount/version/stripped`. A sample base/env car
pair shares vertex address but has 3390/2025 inputs, with zero actual clip/cull
calls (replay). The ledger does not carry complete transforms/program capacities,
so even matching keys would not prove identical routing. This rejects blind
multi-pass route sharing as the next fix on that evidence; it does not clear
every future map/pass configuration.
The [read-only historical audit](ee-pass-audit.json) binds all 1,027 rows and
finds 14 shared-address groups, but zero complete recorded geometry-key groups.
Reproduce with tools/tyrax2-ee-pass-audit.py against the linked A02b CSV.

The queue already memoizes first-16 REF resolutions during counted snapshot
preflight, avoids a separate successful sizing scan, and references owned inline
payload during native emission. Fusing fixup/emission is C05's rejected physical
candidate; direct producers are H02's rejected candidate. No repeat by renaming.
Likewise clip constants already have a retained REF and mayClip gate; inactive
spot lights return before affine inversion. Do not propose deleting work that
the normal path already skips.

Next EE work must change a concrete active contract, with preserved output and
ownership, not assume every microprogram job or material pass is redundant.
