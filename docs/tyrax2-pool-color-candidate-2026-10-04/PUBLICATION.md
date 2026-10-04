# Source-only candidate checkpoint

This archive preserves the private proposal and exact host controls. Executable host oracle files and the EE object are intentionally omitted; their hashes remain in original proof metadata. `proposal-manifest.json` is the historical external proposal inventory, not an assertion that omitted binaries are packaged. The separate actual R5900 translation-unit compile passed without diagnostics; this is not a linked ELF, device-runtime, color-only activation or performance acceptance. No production source or defaults are changed. Private scripts retain their original LAB paths and are not portable installers.

Reconstruct the frozen V2 diagnostic source first, then apply `pool-color-split.patch` with command-local `git -c core.autocrlf=false -c core.eol=lf apply`. Verify the candidate source hash against `source-proof.json`. `SHA256.json` binds every packaged file except itself.
