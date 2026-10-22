# Pool2 EE lazy-color private checkpoint

This package preserves the five pricing changes layered over the earlier Pool2
499-file source snapshot and the separate two-file diagnostic probe delta.
It is a private experiment checkpoint; production defaults remain separate.
Historical negative/stage records are preserved byte-for-byte rather than
rewritten into later acceptance. Root binds completed actual-output reports
after the twelve-case closure before running the packaging script.

Restore the earlier497-file base and then499-file pricing sources using the
prior `tyrax2-pool2-source-2026-10-05` package and its497→499 recipe. That exact
prior499 source-only directory is this checkpoint's input. It must contain no
extra runtime assets, resources, build outputs or manifests. Run:

```
python reconstruct.py --base PRIOR_499_SOURCE_ONLY --out NEW_PRICING --mode pricing
python reconstruct.py --base PRIOR_499_SOURCE_ONLY --out NEW_PROBE --mode probe
```

The helper rejects duplicate JSON keys, noncanonical/traversing paths, symlinks,
case collisions, extra/missing files, mismatched preimages and incorrect new-file
declarations. It verifies the exact final file set and all hashes, builds nothing
and accesses no device. Pricing499 manifest is769f1403389da4d3683a17806fce1b75b381a4cf4f8229ee4d3306a44ca806de.
The probe has500 files, with one added header and one changed init/loop source.

Authored `game/res` is an additional native asset-conversion input. Source-only
restoration intentionally does not supply it. V1 had four missing audio ADPCM
outputs because authored sfx inputs were absent; it was rejected before any
device launch. V2 restored authored assets without changing the499 source
identity. Runtime assets, authored resources, toolchains, ELFs, symbols, host
executables and SaveStates are not shipped in this package. Asset input/output
manifests and native/source identities are preserved as metadata.

`postimages` and reconstruction authority are portable source restoration
inputs. Preserved root scripts, native logs and helper files contain historical
lab/device paths and are evidence, not portable launch instructions. Pricing
physical raw logs, strict reparses, source/control metadata and six accepted
emulator images retain their original bytes. The rejected initial SDL/image
attempt's metadata is preserved separately and never substituted for acceptance.

Actual-output reports bind the twelve observed entry/READY/halt/SaveState cases
and six paired surviving VU-output comparisons. Final requested color epoch and
ordinary inside96/192 output scope must stay explicit. B192's first75 outputs
are overwritten in final banks; intermediate epochs are protocol-bound without
individual snapshots. The source-only CLIP design does not qualify clipping,
fallback output, universal game behavior, cache/DMA ownership or performance.

`metadata/preservation-map.json` identifies each original path, archived path
and SHA256. `SHA256.json` covers every package file except itself. No binary
payload is copied except the explicitly selected six accepted raster PNGs.
