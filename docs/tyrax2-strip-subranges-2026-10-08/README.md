# Terrain strip refinement evidence

This archive separates the private prototype from its production integration.
The [report](../tyrax2-strip-subranges.md) contains accepted results and limits.

- `prototype-perf`: twelve-phase physical and emulator A/B/A plus B/A/B,
  launch identities, input hashes, source binding and parked world-pixel checks.
- `prototype-oracle`: independent range and geometry-writer witnesses;
  correctness only, never performance evidence.
- `prototype-generators`: frozen pre-integration generator/helper versions.
- `integrated-perf`: the same protocol instrumenting production refinement,
  including all five generated terrain bag opt-ins and the Core-owned cache
  (the ground-shadow list path is unchanged).
- `integrated-oracle`: production range witnesses and geometry-writer checks,
  host sanitizers, renderer controls and analyzer rejection tests.
- `tools`: current generators, analyzers and host/capture helpers.
- `evidence-sha256.json`: payload hashes, excluding this manifest itself.

Fixtures remain in `F:/Projects/tyrax2-subranges-20261008-*`; ELFs and game
assets are intentionally outside the repository. Source/native manifests bind
all compiler inputs; selected exact generated/engine sources are included for
review, not a complete stand-alone source distribution. Tool paths assume this
host's configured SDK/WSL/PCSX2 setup. Rebuild the editor before regeneration.
The final native engine mirror is shared: verify each binding before rebuilding
another fixture, rather than trying to verify an older ELF against a newer mirror.

Images are visual smoke tests and a parked world-region comparison, not a
moving-frame GS output oracle. Emulator timing is not physical PS2 performance.
