# Clip expansion and reflection ordering evidence

Two default-off candidates are tested separately and together in eight passes:
Off, expansion, barrier, both, both, barrier, expansion, Off. Counters stay on;
there are no additional scope clocks or frozen reflection captures. Earlier
partial baked-body reuse is off. All game passes retain real dt, three vehicles,
ordinary clipping, noon lighting and the complete full-throttle road traversal.

- `rejected-chunk-oracle/`: first per-clip-chunk expansion cache. At least
  425,984 byte comparisons passed, but residency controls failed. The cache
  reached 666 entries against the 512-entry immutable registry. Copy fallback
  persisted into control phases, so this is not qualified performance evidence.
  `rejection.log` preserves the strict analyzer's refusal.
- `oracle/`: revised whole-strip cache, with registration failure fallback and
  unsubmitted reservation rollback. All eight routes and parked controls pass,
  with at least 425,984 comparisons and zero failure reports. `clip-drive.png`
  was visually inspected during driving. No timing inference from this build.
- Root build/source/native binding records identify the performance ELF:
  `0ec57f6287c35374a10c3fcd16326bb00bd7485c160a98f20d8d69083b49c82c`.
  The native mirror/source/ELF verification matched 466 files.
- `physical/` and `emulator/`: same performance ELF, separate launch/input
  identities. All eight passes complete in each; strict analyzers accept all
  controls. Physical expansion-only saves 0.481/0.463 ms against adjacent outer
  controls, with 51.54/51.43 Hz versus 47.95/47.50 Hz. Barrier-only has no useful
  gain. Both combined are effectively the same mean work as expansion alone.
  Counters were always on; this does not price their observer cost.
- `analyzer-selftest.json`: reference accepted, eighteen malformed controls
  rejected. `host-tests.log` also includes 43 native frame-writer cases.

`envFallback` combines synchronous env calls, failed recording of GIF packets
and multi-batch frame splits. Zero excludes these three, not unrelated waits.
`envRecorded` and `barrierOmitted` count env begin/end separately. The analyzer
requires exactly two env operations per shared reflection capture.

The abandoned v1/v2 performance builds were never deployed to PS2. The console
was reset once before preparing these fixtures; the final v3 launch used bare
execee. All fixtures are private copies; checked-in authored/generated example
changes remain user-owned and untouched by this work.

The code remains default-off pending broader scene/mutation/cold-start and
previous-candidate combination coverage. Stable 60 Hz is not achieved. Owned
invisible emulators were stopped after completion. The user requested PS2 and
computer shutdown; any poweroff request record is a command-send record, not
proof of the PS2's standby LED or the computer's completed shutdown.
