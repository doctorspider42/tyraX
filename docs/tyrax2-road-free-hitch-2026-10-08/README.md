# Road-free hitch evidence, 2026-10-08

See [the report](../tyrax2-road-free-hitch.md).

- `localization`: three normal-reflection drives, local scopes only in phase 1.
- `preload-wrong-masks`: preserved failed intervention using already loaded masks.
- `preload-corrected`: phase 1 prepares the actual row-4 masks before sampling.
- `ground-pins-ab`: production whole-scene residency off/on/off.
- `ground-pins-final`: production residency always on, three full-length drives.
- `tools`: fixture generation, analysis and supporting launch/header sources.

Each trial contains source/native/ELF hashes, the private modified sources,
physical and emulator CSVs/logs, analyzer results and deployment records.
The ELF remains in the external scratch fixture identified by the report; this
archive stores its SHA-256 and the 467-file source/native binding, not binaries.
Screenshots are smoke checks, not a pixel-equivalence oracle. Emulator timings
are not physical PS2 performance. Inclusive scopes must not be summed.

The source project was not edited. Its manifest SHA-256 is
`5e09e8db4d57e0d01bc0b6d5850ac6e9bc03ce461aa7bfc5204332f89d2eb95a`.
All 303 staged input files per physical run were rechecked after execution.
Only owned host/emulator processes were stopped; no user emulator was touched.
