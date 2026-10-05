# Private Pool2 EE producer experiment

The second Pool2 experiment removes actual expanded-color preparation on EE and avoids repeated geometry/ST copies when only a pool's color changes. Its same-ELF physical trials show a small benefit in both orders: **0.113–0.171 ms per frame**, while presentation remains approximately **33.367 ms (30 fps)**. This is a private candidate, not a shipped renderer change or a 60 fps result.

The [earlier table experiment](tyrax2-pool2-colors.md) compressed the VU input but left the original expanded-color producer running. This continuation uses owner-local color generations and a persistent lazy backing array. Admitted packets read the compact per-pool table; copy and clipping fallbacks materialize only their consumed source ranges. Geometry and ST copies are skipped when their own identity/version keys remain unchanged. The measured candidate combines both changes; it does not isolate a pure color-write saving.

| Same-ELF order | Inclusive elapsed minus existing pacing, ms | Candidate minus its controls, ms |
| --- | --- | --- |
| Off / On / Off | 19.249732 / 19.080820 / 19.249790 | −0.168912 / −0.168970 |
| On / Off / On | 19.081464 / 19.252303 / 19.139024 | −0.170839 / −0.113280 |

Both trials completed 10,800 loops in total, with 768 raw samples and 30 bounded chunks. All four contrasts favor the candidate, although the outer candidate arms in reverse differ by 0.057559 ms. These are inclusive responses including existing routing and waits, not isolated EE/VU/GS costs. The common branch/layout/scaffold cost remains unpriced. Results must not be subtracted from a different ELF or added to earlier effect-removal savings.

## Verification and limits

Actual producer host controls passed at O0 and O2: 645,441 checks and 1,474 paired flushes per run, including shape/member fallback, partial materialization, arm changes, dirty geometry/ST, ignored alpha and local generation wrap. Native source, object, ELF text, unchanged TC image, resource and ABI checks passed. All existing program classes and billboards remain resident: 1,908 microinstructions with VU1 clipping, 1,708 with EE clipping, below draw-finish at 2,042.

Both ordinary emulator orders completed with six qualitatively normal phase images. The operator confirmed the retained full-night candidate on PS2 looked normal. These appearance checks establish neither pixel equality nor universal hardware precision.

A separate correctness ELF disables the cold expansion oracle in both arms, poisons the lazy backing, and authors a new color epoch on every iteration. Twelve paused captures cover 96/192 source vertices, both arms and one/two/three iterations. Six pairs of actual saved VU output match STQ/RGBAQ/XYZF2/ADC and material headers, and decoded RGBA agrees with the final requested epoch. The target's inside-case self-check confirms that lazy colors were not materialized. For the 192-vertex case, the first 75-vertex output is overwritten; only the surviving crossing 75 and tail 42 are accepted. Epoch changes force replay misses, so these captures do not prove stable warm hits. Clipped output remains outside this diagnostic's accepted scope. SaveState drains and MTVU-off correctness observations are separate from physical timing.

## Preserved rejected attempts

The first native fixture lacked authored resource metadata and published WAVs where the game expected ADPCM. A semantic asset check rejected it before any device launch. A new fixture includes both authored `res` and `.res-baked`; all 298 runtime assets match the earlier qualified fixture, including four native ADPCM conversions. A self-consistent asset manifest alone does not prove that expected runtime filenames exist.

The first emulator raster attempt was occluded by an owned SDL audio error modal. Its completed protocol and negative raster record are retained separately. Fresh profiles using host Null audio produced the six accepted screenshots with the same guest ELF and resource bytes.

The next ordered experiment is package-local unique-point transformation on VU1; GS SPRITE coronas follow it. Neither is covered by this result.

The [preserved source and evidence package](tyrax2-pool2-ee-2026-10-05/README.md)
contains five pricing postimages, two diagnostic postimages, raw physical logs,
native/host reviews, actual-output reports and six accepted emulator images.
Both source-only restorations were exercised against the verified previous
499-file base, producing the exact 499/500-file manifests. Build binaries,
SaveStates and runtime resources are excluded; their observed hashes remain.
