# TyraX2 debugger execution map

The PCSX2 debugger provides a diagnostic map of which game and renderer functions consume emulated execution intervals before selected suspects are measured on PS2.

## Automated acquisition, 2026-10-04

Windows native accessibility could read the debugger tree, but screen capture and input geometry repeatedly failed. A separate Linux PCSX2 v2.9.93 session inside WSL, displayed on an owned Xvfb screen, accepted X11 Run/Pause input reliably. This controls only the research emulator. It does not require repeated user clicks or modification of the guest ELF.

Stock PINE supplies paused status and SaveState; it does not supply PC, cycle or Run/Pause. The acquisition therefore combines debugger Execute breakpoints, an X11 resume, exactly one new pause in the debugger log, PINE paused checks before/after SaveState and offline register decoding from that state. The official source commit is94d86c891b1621c0b252e4fc2e155bf90274dcc0. A layout probe compiled from the actual register types agrees on Linux64/Windows64: CPU1160bytes, PC680, cycle1088, SP464 and RA496. Actual ZIP method93 (Zstandard), archive sizes/CRC, version tag, unique CPU block and the complete guest text hash are checked. Two captures at the same stop have identical CPU bytes and registers.

SaveState serializes CPU registers before waiting for VU/GS and collecting its screenshot. Those waits drain queues and can change later execution. A stable paused CPU record does not price that observer or prove consumer state unchanged. Use the64-bit emulator scheduler cycle, not a lazily updated CP0 Count display. No interval here is hardware milliseconds or an ordinary FPS result.

## Qualified execution intervals

Independent audits redecoded29 coarse states across three frames,65 broader states across three frames, and357 states for one detailed per-bag frame. Every detailed core entry closes against its actual entry SP and RA; all135 core calls are accounted for. Sixty-five stops at return addresses lack a matching active call and are excluded. A shared return closes both a core tail call and its enclosing beam function. Tail-call wrappers must be followed; a direct-JAL-only return list misses submitHeavy and object/beam tails.

| Detailed sampled region | Inclusive emulated cycles | Inside core render | Outside core render |
|---|---:|---:|---:|
| Terrain submit |369132|362591|6541|
| Roads submit |319126|296751|22375|
| Wheel preparation and submit |243268|130009|113259|
| Static batches |133959|122295|11664|
| Light pools |111598|97259|14339|
| Light beams |121867|66653|55214|

Scene totals2129751cycles:135 disjoint core intervals sum1722875, leaving406876 outside core. The table contains selected children and is not a complete Scene sum. Core includes all its callees, packet work, copies and any waits; these totals cannot be assigned to pure bag assembly, native copying, VU or GPU time. Wheel preparation has a concrete residual worth measuring separately; most observed terrain/road time lies inside their renderer calls.

The large loop tail also includes completion and pacing. TerrainGame::loop tail-jumps to Renderer::endFrame; its real return is in Engine::realLoop. Do not invent a local post-call address or claim the waiting tail is the renderer bottleneck without splitting its owners. The V4 protocol has many more queue-draining snapshots than V2: subtracting their totals would measure different apparatus, not an optimization.

The [machine record](tyrax2-pcsx2-scopes-2026-10-04.json) pins all three independent proofs, exact ELF/text/image identity, layout and the source correction:0x110a70 is a legitimate conditional renderProcChunks return, not embedded data. It was unsuitable for this sampled-frame selection because no actual core-entry RA selected it. Partial/rejected setup attempts remain in the external research archive.

## Next physical check

Use a fresh private ordinary-cadence fixture with one selected function bracket, unchanged clocks and draw order, and same-ELF Off/On/Off plus reverse controls. Timing one selected invocation adds two Count reads to the existing two-read loop sampler; the common records, selection/reset branches and compiled-on layout remain present in both arms and are not priced by the incremental contrast. Export after measurement windows. Generic bag selection additionally needs verified producer/caller context; ordinal and vertex count alone are insufficient. No new physical scope result or renderer optimization is accepted by this debugger record.
