# Pool2 private source snapshot

This archive preserves the completed private Pool2 color-input experiment and its separate fixed-case VU correctness probe.

See [results and limits](../tyrax2-pool2-colors.md). Historical proof fields such as `nativeAccepted=false` or `runtime pending` describe their original stage; later native and runtime records close those stages without rewriting their source evidence. Pricing uses 499 frozen source files; the separate probe uses 500. Neither changes the production default.

## Restore source only

First reconstruct the exact 497-file base using the [existing base recipe](../tyrax2-wild-source-2026-10-05/metadata/base-v3-reconstruction.md). Its manifest SHA256 is `3e895d8224297c66169bf2a01cabc0dfd1fd1e0c9fee8a77c3c77de88bb14dad` and is preserved under `metadata/base-497-source-manifest.json`.

Run Python 3 with a verified base and a NEW output directory:

```text
python reconstruct.py --base PATH_TO_BASE --out NEW_PRICING_DIRECTORY --mode pricing
python reconstruct.py --base PATH_TO_BASE --out NEW_PROBE_DIRECTORY --mode probe
```

The helper checks every base hash, copies only source-manifest files, applies the pricing overlay (and probe overlay when requested), and checks every final hash and exact file set. It never builds, installs tools or touches a device. Runtime assets, binaries and native toolchains are separate inputs, not reconstructed here. `metadata/reconstruction-verification.json` records both exercised restorations.

`postimages/` is the reconstruction authority. Original source/control scripts under `root-helpers/` contain historical laboratory paths and are evidence, not portable launch instructions. `history/` preserves both failed native revisions; `assembly/` preserves accepted V3 tool output. `reviews/` separates independent source/native checks from later root runtime summaries.

`metadata/preservation-map.json` binds original laboratory text files to byte-exact archive copies. `SHA256.json` binds all files in this package except itself. No executable, object, ELF, image, SaveState or runtime asset payload is embedded.
