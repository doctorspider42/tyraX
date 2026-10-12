# Combined geometry reuse qualification

This archive records the October 8 combined partial-body replay and strip
expansion trial. The main interpretation and limitations are in
[the full-throttle report](../../tyrax2-full-throttle.md#combined-geometry-reuse-qualification-2026-10-08).

- `oracle`: six full routes with both actual-writer byte comparisons enabled.
- `emulator`: six full routes using the exact performance ELF later deployed.
- `physical`: six PS2 routes, Off/On/Off observed then On/Off/On counters off.
- `defaults`: production route predicates unchanged, both defaults on, byte
  oracles enabled; 7200 vehicle-free camera loops and scene changes through
  dense, procedural and main, with completion at frame9000.
- `analyzer-selftest.json`: valid synthetic reference accepted, sixteen malformed
  inputs rejected, including missing activation and leakage of either cache.
- `host-tests.log`: actual arena, writer, selector and partial-body controls.
- `lifecycle-host.log`: 59 extracted actual-cache ASan/UBSan checks with mocked
  DMA readers. This is not hardware concurrency emulation.

Native bindings and source manifests identify each private fixture and ELF.
Generated binaries and asset copies remain in the named external fixture paths.
Snapshots under `sources` preserve the relevant generated engine/game sources
for these accepted builds; `evidence-sha256.json` hashes this archive's payload.
The emulator post-route image is beyond the road endpoint, not an in-route
visual comparison. Sweep/restored-main images are visual smoke checks only.

The caches share the original 4 MiB budget. No reduced geometry, frozen captures,
longer TTL, omitted env barriers, or removed validation contributed to this gain.
The inherited sampler and disabled hook footprint remain common to both timed
arms. No uninstrumented absolute production price or stable60 claim is made.
The PS2 file server is deliberately left serving the completed test; no console
or host shutdown is part of this request.
