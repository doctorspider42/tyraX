# Private night V3 extra-subset source snapshot

This immutable diagnostics archive contains the exact seven-file delta from frozenV2 to frozenV3, three exact changed headers, current analyzers/host source controls and root build/launch/evidence helpers. It contains no ELF, symbols, binaries or asset payload. It is a reviewable experiment, not a production optimization or portable installer.

## Reconstruction from the V2 archive

First reconstruct frozenV2 using `night-ablation-publication-v2/README.md`: start with its hash-matching fresh reference, apply its patch with command-local `git -c core.autocrlf=false -c core.eol=lf apply --whitespace=nowarn ...`, then copy its four exact new headers. Verify all497 entries against this archive's `metadata/frozen-v2-source-manifest.json`. Do not use global autocrlf conversion or normalize source bytes.

On a separate copy of that reconstructed V2 source, apply this archive's `patches/frozen-v2-to-frozen-v3.patch` from the source root using `git -c core.autocrlf=false -c core.eol=lf apply --whitespace=nowarn /absolute/path/to/patch`. The patch includes all seven changes; the three changed-headers postimages are redundant exact copies for review, not additional inputs. Verify each delta and all497 entries against `metadata/frozen-v3-source-manifest.json`. An external temporary reconstruction has passed exact497-byte-hash checks, with490 inputs unchanged. The archive prose/JSON uses UTF8 LF; original source bytes are preserved. No build/device action was run during packaging.

## Protocol and scope

Old kinds0/1/2 retain their ordinary masks and extraMask0. New config `3 order 0 extraDisabled` uses subset1..7: Pools1, BeamsCoronas2, VehicleLampGlow4. Ordinary mask remains0 and samplerOn throughout. Order0 is Full/Cut/Full; order1 Cut/Full/Cut. Exact NIGHTEXTRAPHASE and six NIGHTEXTRAGATES records identify extra masks and untimed750/1155 witnesses. Counter writes remain absent from tax800..1119 and other non-witness loops; the original sampler and Count seams are byte-identical. Runtime numeric printf/fprintf arguments retain explicit unsigned casts for the actual PS2 ABI. Pool cleanup and paired effect brackets are in the separately reviewed source delta; sparse counters prove reached branches/attempted submissions, not full-window eligibility, DMA emission or raster equality.

## Authority and limits

Fresh compact O0/O2 controls include28 complete5400-loop transcripts,24 parser rejects,17 malformed configs per transcript, actual262/6 Count-read accounting and an actual kind3 CLI invocation. The full older suite is not claimed rerun. Historical CLI draft omission is preserved externally and documented; current source/host pins are final. Actual frozenV3 native source497/mirrors488+9ancillary, target ABI, format-warning clearance and matching ELF/symbol text passed independent native review. The excerpt binds the complete external proof by SHA256 instead of duplicating its large dependency map.

PriorV2 has eight completed physical boots (observer and joint both orders, group addbacks only order0). Reverse addbacks remain absent. V3 hardware remains unmeasured at packaging; emulator evidence is separate and establishes no physical price. No per-family additive bill, cross-ELF subtraction, universal observer fee, pureEE/GPU attribution, shipping gain or exact60FPS/TV cadence is claimed. Helpers retain actual LAB/toolchain paths and root ownership; archived scripts are not automatically executed. `SHA256.json` covers every packaged file except itself; external native/assets/host/runtime authorities remain independently named and pinned.
