# A06 road draw reduction evidence

This snapshot preserves a private geometry-reduction trial with completed host,
native and emulator checks but an interrupted physical PS2 series.

Source fixture: `F:/Projects/tyrax2-road-reduction-20261009-v1`.
ELF SHA256: `f9993a824b9a46bbd2bbb8a0bc461bacec5664e5e60d1b53bbdefa8965a92779`.
The native binding identifies 469 source files. Four road chunks keep their
original collision/receiver arrays and draw a validated 234 -> 72 vertex union
through separately owned arrays. This is not a production/default change.

`v1-interrupted/` includes source-bound files, ELF, host/surface reports,
six-phase emulator CSV/analysis/screenshots, partial physical CSV and deployment
logs. `manifest.json` hashes the exported snapshot, excluding itself. The
physical log is a point-in-time snapshot: the owned host server was intentionally
left alive. It may continue receiving repeated freepad errors after export.

Emulator work improves 0.185-0.192 ms during turning, but this is not a PS2
price. Physical phases 0 and 1 completed; the following scene load repeated
`freepad: DMA Busy`. No DRIVEDONE or second control exists, so the complete-run
analyzer rejects it. No retry/reset loop was issued. All 303 staged inputs and
both original authored map manifests match their prior hashes.

Stationary off/on pixel differences exceed the off/off control. The surface/UV
oracle does not establish GS depth equality, nonlinear light/fog interpolation
or moving raster equivalence. Nothing is promoted. See the
[scene work audit](../tyrax2-scene-work-audit.md) for details and the
[decision register](../tyrax2-performance-investigation.md) for the next step.
