# Partial baked geometry evidence

The candidate regenerates package headers and program kicks, borrowing only
geometry bodies from existing immutable baked arenas. It does not disable
clipping, reflection captures, vehicles or normal time integration.

- `oracle/`: separate per-replay actual-writer comparison ELF. All six passes
  completed; the last periodic report records 131,072 checks and zero failures.
  No failure reports occur. Two candidate screenshots were inspected. Its
  timings include verification and must not be used as candidate performance.
- `native-binding.json`, `source-manifest.json`, `build.log`: performance ELF
  provenance. SHA-256 `f69e7df53d2c0d18acdda4af91a7dc85dc8c8d2cd833256cac56def3ccd044a5`.
  Native mirror verification matched 466 source files and the output ELF.
- `physical/`: same-ELF Off/On/Off then On/Off/On full-throttle trial, with
  counters only in the first triplet; no new scope clocks, normal captures,
  TTL250, 1,200 warmup loops per phase. All six passes completed and the strict
  analyzer accepted the starting controls and complete routes. Counters-off
  work saves 0.259/0.286 ms; mean cadence rises from 49.07 to 50.98/50.30 Hz.
- `emulator/`: the performance ELF's separate invisible Xvfb97 session.
  Emulator timing is not physical cost evidence.
- `host-tests.log`: actual-source host checks, including the 50,000-case body
  comparison test. `analyzer-selftest.json` accepts the reference and rejects
  fourteen malformed controls.

The production gate remains default-off: this is a small positive hardware
candidate, not stable 60 Hz or broad production qualification. All six emulator
passes also completed with the same performance ELF. Owned invisible emulator
processes were stopped; the physical host client remains running.
The first oracle fixture (`...-check`) failed compilation because PS2 `u32`
and `uint32_t` have different underlying types. The corrected `...-check2`
fixture is the oracle archived here. Full build fixtures remain outside the
repo; the user's authored/generated example changes are not test edits.
