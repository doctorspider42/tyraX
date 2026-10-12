# tyra-editor-dev — devkit captures, cadence, timeline, submission scopes

From `## Render-cost capture (1.78)`, `## Configurable devkit cadence (1.90.0)`,
`## Hardware timeline capture`, `### Native hardware timeline (1.92)` and
`### Static object submission scopes (1.93)`. See `docs/devkit.md`,
`docs/hardware-profiler.md`, `docs/static-submission-batching.md`.

## Render-cost capture (1.78)
**Cause/rule:** Debugger command bit 7 requests one synchronized renderScene pass.
`templates.cpp` consumes it through the generated Live Debugger hook, measures
phases and object draws only while armed, and writes `bin/rendercost.txt`.
`livedbg.cpp/.hpp` validates bounded rows/footer and exports CSV; `devkit_ui.cpp`
owns the Render cost tab and baseline; `main.cpp` exposes `--profile-frame`.
**Fix:** keep the generated on/off hook declarations paired. Captures serialize
asynchronous rendering; their total is not normal frame time.

## Configurable devkit cadence (1.90.0)
**Cause/rule:** `ProjectSettings` stores `liveLinkPollFrames`,
`liveLogicPollFrames`, `liveDebugPollFrames`, `liveDebugSnapshotFrames` and
`timeMachineFrames`. Each is 0 (platform defaults) or 1..120 game updates; format
54 is additive.
**Fix:** keep model, equality, settings serialization, Preferences and templates in
sync. Link also controls texture polling. Command/report cadence never gates graph
execution, watch sampling, Remote Pad or replay input. Halted debugger commands
use two loop ticks, forced reports bypass cadence, and `devkit_ui.cpp` scales
heartbeat silence tolerance with reports/FPS.

## Hardware timeline capture
**Cause/rule:** use `tools/hardware-trace.py arm PROJECT` before a boot, then
export the complete `bin/hardware-trace.csv` to HTML/Perfetto. The engine captures
bounded RAM events without new drains and writes after sampling.
**Fix:** scope totals overlap; VIF1 DMA wait is not VU1 execution, and VIF/GIF
snapshots are not utilization. Compare unarmed/armed controls and reject dropped or
stale events. See docs/hardware-profiler.md for start-frame semantics and capture
limits.

## Native hardware timeline (1.92)
**Cause/rule:** `src/hardware_timeline.cpp` reads the same bounded CSV as the
offline exporter. Debugger > Hardware timeline arms the next boot and loads
completed captures on demand, with frame selection, zoom, raw marker tooltips and
inclusive totals.
**Fix:** no browser, Python or extra debugger polling is required. Engine detail
scopes separate package creation/classification, qbuffer copies and packet
construction. Use unarmed controls to rank performance.

## Static object submission scopes (1.93)
**Cause/rule:** the generated main Objects loop opens a StaPip submission batch
around owned per-object geometry. End and reopen around reflected-object probe
rendering and serialized per-object cost measurements, then end before the outer
Objects timing boundary and later passes.
**Fix:** do not move already-submitted stream writes or external GS/view operations
into the scope. End submits asynchronously; streams remain immutable through the
next VIF1 synchronization. Game-overridden programs, clipping/copy paths, large
bags and nonresident/non-REPEAT textures retain immediate submission.

## Retained static command data (1.96.0)
**Cause/rule:** the submission scope is also what makes RETAINED STATIC COMMAND
DATA worth having (docs/retained-static-commands.md): inside it a wholly visible
static bag's VU1 command block is captured once and replayed with a memcpy, so only
the MVP, the picked dynamic light and the frustum classification are rebuilt per
frame.
**Fix:** it is engine-only — no codegen, no project format, and the packet that
reaches VIF1 is byte-identical — so a generated game inherits it by rebuilding,
and nothing in src/ changed for it.

## Reference facts (upstream `devkit-runner-and-network.md`, capture/audit)
- Host side `devkit_ui.cpp` (~3.8k): `liveLinkTick`/`liveLogicTick`/`livedbgTick`/`livetimeTick`/`remotePadTick`/`replayTick`/`pollGameError`/`uiScriptTick`; `dbgReadCrashReport`, `dbgReadVuCapture`, `dbgReadFrameShot`. **"The file changed" is its TIMESTAMP, never its size** (two captures of one scene are the same length); a poll landing mid-write commits after a few tries; Copy image publishes the retained bitmap via `platform::copyImageToClipboard`.
- `std::filesystem` file times are NOT system-clock times (`file_clock`'s epoch sits in the FUTURE on libstdc++; use `LLONG_MIN`/optional for absent and `fs::file_time_type::clock::now()` for age, never the system clock).
- Release audit `elfsym.cpp/.hpp` (~320+120): ELF32 reader + `auditRelease()`; `TXDEVKIT-<layer>` markers (`__attribute__((used))`) + `kStringNeedles`; `--audit-release`; a new channel file is deleted before launch in BOTH Runner clean-up blocks (also `bin/frame.tga`); `platform::revealInFileManager` walks up to the nearest existing ancestor.
- Cadence (1.90.0) `ProjectSettings` `liveLinkPollFrames`/`liveLogicPollFrames`/`liveDebugPollFrames`/`liveDebugSnapshotFrames`/`timeMachineFrames`; debug profile + `ProjectSettings::liveLink`/`liveLogic`/`liveDebugEnabled`/`inputRecorder`; `pollGameError`; hardware timeline scope totals overlap (VIF1 DMA wait is not VU1 execution); `uiScriptTick`.
