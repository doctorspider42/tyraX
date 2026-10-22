# Diagnostic native auditor adaptation

Private root-reviewed adapter of `audit-corona-native-v6-root-v1.py`. Invoke only after successful native provenance and a root cache hold. It performs no build, device action, source edit or cache mutation; only its new `--out` directory is written.

```text
python audit-probe-native.py --fixture <LAB>/wild-gs-sprite-corona-probe-physical-v1 --abi <LAB>/wild-gs-sprite-corona-probe-target-abi-v1 --out <LAB>/<new-unique-root-audit-folder>
```

Requires the frozen 500-source qualified pricing V2 manifest and precisely the authored terrain-game replacement plus the new probe header (501 sources / 492 mirrored). Every engine source and every actual linked VU image must match pricing, including TC506. Preserves 17 wrapper checks, all resident families and billboards (2040/1840, limit2042), actual dependency/object/archive/link checks, ten-word target ABI with CoronaCounter36, and all 298 exact pricing assets including four ADPCM files.

The actual diagnostic object and linked ELF must define global READY data (initial zero, u32) and executable HALT function. Exports include actual addresses, section flags/types, byte hashes and defining-object hash. Runtime launch metadata includes `diagnosticOnly: true`, actual ELF/symbol/source-manifest hashes, `readyAddress`, `haltAddress`, and `diagnosticExports`.

This audit proves source/native linkage and retained budget/assets. It does not prove HALT machine instruction semantics, probe reaching READY, VU acceptance, target output, raster equality or physical gain. Source is pinned exactly to the authored probe. Root must inspect the helper before invoking it; its preparation proof is not a native audit result.
