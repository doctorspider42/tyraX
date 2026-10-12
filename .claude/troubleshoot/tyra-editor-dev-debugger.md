# tyra-editor-dev — Live Debugger, self-screenshot, Remote Pad, recorder

From `### 3` (a new one-shot ASK, the self-screenshot's traps, Live Debugger,
Remote Pad, Input recorder). See `docs/live-debugger.md`, `docs/devkit.md`,
`docs/remote-pad.md`, `docs/input-replay.md`.

## A new one-shot ASK
**Cause/rule:** the Live Debugger's command carries five besides the breakpoints —
force-fire, a VU1 capture, its flush index, a free-RAM measurement and a
screenshot — and every one of them rides a spare bit of the flags word rather than
a longer header, so a game built before it existed reads the switches it knows and
ignores the rest. Bits 0-2 are the transport, 3 and 4 the VU capture and its
index, 5 the RAM measurement, 6 the screenshot, 8-23 the flush index and 24-31 the
fact-override count.
**Fix:** the editor half is four edits and no more: the field on `livedbg::Command`,
`sameStateAs`, the bit in `encodeCommand`, and the one-shot RESET in `livedbgTick`'s
write block beside `captureVu`/`measureRam` — miss that last one and the ask
re-fires on every resend.

## The self-screenshot's traps
**Cause/rule:** `writeFrameCapture` in templates.cpp. `framebuffer_t::address` is
in GS WORDS while libdebug wants BLOCKS, so a missing `/64` overflows SBP's 14
bits and reads buffer 0 with its pages scrambled; the buffer has to be
`getPreviousRealFrameBuffer()`, never the current one nor `getPreviousFrameBuffer()`
(which can be a synthesised extrapolated frame); the readback lands in RAM **behind
the EE's data cache**, so a line has to be flushed after every transfer or the
picture repeats rows on hardware (PCSX2 emulates no cache and shows nothing); and
`ps2_screenshot` REFUSES to run while VIF1's DMA channel is busy, reporting that
only through its return value, so a refusal must be counted rather than written out
as picture.
**Fix:** **the FILE is written by our own `fopen`/`fwrite` and libdebug's
`ps2_screenshot_file` must not come back** — it creates its output with
`open(O_CREAT|O_WRONLY)`, which over ps2link arrives at the `host:` server as a
**mkdir of the target name** (the host gets a directory called `frame.tga`, the
open returns -1, and the function returns 0 as though it had worked). The verdict
is the byte count actually written, never a return value. On the editor side,
stb_image here is built `STBI_ONLY_PNG` + `STBI_ONLY_JPEG` and answers *unknown
image type* to every TGA: `dbgReadFrameShot` decodes by hand.

## Live Debugger
**Cause/rule:** `App::livedbgTick` (each frame from `drawUI`;
docs/live-debugger.md) — Live Link's reverse channel, on the same `host:` files.
The generated runtime writes its boot/liveness snapshot unconditionally, but
periodic snapshots begin only after a valid `livedbg.cmd` proves an editor is
attached, so keep command polling alive (it is the attach mechanism). Debug
profile + `ProjectSettings::liveDebug`. Codegen (`debugSymbols` in templates.cpp)
assigns ONE KEY per instrumented flow-graph node by walking scenes → objects →
nodes; that enumeration is the single source of truth for four consumers (the
`livedbg::hit(key)` calls in `flow_graph.gen.cpp`, the runtime tables in
`src/gen/live_debug.gen.cpp`, the `src/gen/livedbg.sym` map the editor reads, and
the hash baked into the ELF that flips the chip to amber when the two disagree).
Instrumented = triggers + actions (`!t->pure`). The generated header
(`inc/scripts/live_debug.gen.hpp`) is ALWAYS emitted and is the on/off seam. The
halt itself is the existing menu pause.
**Fix:** a change that alters which nodes exist changes the keys — never hand-roll
a second enumeration, call `debugSymbols()`. Breakpoints live in
`Project::debugBreakpoints` as `"<objectId>:<nodeId>"` — editor state in the
`.tyra`, deliberately NOT a collaboration section. Anything new the Debugger should
watch goes into the ONE watch array: flow variables via `flowDbgReadVar`, then save
values read straight off `ScriptContext`; the sym file's per-entry `kind` is what
tells the editor which is which. **The gate is `liveDebugEnabled` (profile +
preference) and deliberately NOT `liveDebugOn`** (which also demands an
instrumented node) — the latter took the WHOLE channel away from any project
without a flow graph (symptom: `[ps2]` log lines keep arriving and the panel shows
nothing forever, because the game wrote no `livedbg.bin`). If you add a per-node
array to the runtime, remember `NODES` can now be 0 — `hits[]` is sized
`NODES > 0 ? NODES : 1`. **An empty panel must always SAY WHY**
(`App::dbgSilenceReason`, one string read by both the state block and the Stats
tab); say it in ONE line and put the paragraph in a `prefHelp` hover
(`dbgSilenceDetail`), with the REMEDY in the visible line. Use
`fs::file_time_type::clock::now()` for that age and never the system clock.

## A console's file channel dies without a sound
**Cause:** `Runner::deployToPs2`, `stopPs2` and `clean` all ran
`taskkill /F /IM ps2client.exe` — BY NAME, machine-wide — so deploying ANY project
took down the file server of every OTHER ps2link session on the machine. The
console keeps running, blocked on `host:`, its devkit files frozen at their last
write, while the UDP log stream carries on (it does not go through that server):
two transports, one dead, only the live one visible.
**Fix:** `Runner::claimPs2Channel` (docs/ps2link-setup.md "One file server at a
time"): ownership goes handle first (the `Process` we spawned, killed as a tree),
and only then a SEARCH over `platform::processesNamed("ps2client")`, deciding from
each process's own command line (`-h <ip>` = which console, `execee host:<name>.elf`
= which game). Ours and a stale one of the same project are reaped; one a running
editor owns is REFUSED with the project named; one no editor claims is an orphan
and is reaped. Two rules generalise: a process whose command line cannot be read
is NEVER a target (guessing is the bug), and "is that editor still alive" is
answered by `devsession` heartbeat OR a live editor pid, never the heartbeat alone.
`killEmulatorsFor` is the same idea for PCSX2 (`-elf <path>`).

## Remote Pad
**Cause/rule:** `App::remotePadTick` (each frame from `drawUI`;
docs/remote-pad.md) — the fourth direction, the only one carrying INPUT: debug
profile + `ProjectSettings::remotePad`. `bin/livepad.bin` is absolute pad STATE, so
both writers (the panel and `--pad`) must **keep rewriting it** — the game expires
an overlay whose `seq` stopped moving, which is what stops a killed driver leaving
a direction held.
**Fix:** the generated `livepad::tick` must stay at the TOP of both game loops but
**after** each `Pad::update()` (update rebuilds the state from hardware and would
discard an earlier overlay), and it uses `injectVirtual` **slot 1** because the USB
keyboard/mouse fold owns slot 0 — sharing one makes every held button re-click
every frame. The panel is deliberately the same encoder as the CLI
(`livepad::write`).

## Input recorder
**Cause/rule:** `App::replayTick` (each frame from `drawUI`;
docs/input-replay.md) — the fifth direction, the only one that reproduces a
SESSION: debug profile + `ProjectSettings::inputRecorder`, the one devkit switch
that defaults to **false** (it writes a file that GROWS). `inputreplay::tick` must
be the LAST stage of a frame's input in BOTH game-cpp loop templates — after
`Pad::update()`, after the keyboard fold and after `livepad::tick` — because a
replay uses the new `Pad::setState`/`KbdMouse::setState`, which OVERWRITE rather
than merge; anything running after it would undo the recording. That overwrite is
also what buys the feature ~30 pad read sites and the three kbdMouse workarounds
for free.
**Fix:** **`KbdMouse::forced` is not optional** — every reader gates on
`isEnabled()`, so without it a recorded keystroke is silently dropped on the
machine replaying it. **The fingerprint script must stamp the frame CURRENTLY
executing, not `frameNo`** (it runs mid-loop, after tick() has bumped the counter;
measured: 1 of 705 frames carried one before the fix, 669 after). And **the Runner
stages the channel, not the App**: `Runner::replay_` is consumed and cleared by
`stageReplayChannel`, which clears every `replay.*` file FIRST on every launch —
a leftover `replay.in` makes a fresh boot perform the last session's run.

## Reference facts (upstream `devkit-runner-and-network.md`, debugger/time machine/recorder)
- Live Debugger `livedbg.cpp/.hpp` (~450+280): `Symbols` (`src/gen/livedbg.sym`), `Snapshot` (`bin/livedbg.bin`), `Command` (`bin/livedbg.cmd`), `Timeline`; caps `kMaxNodes`/`kMaxBreakpoints`/`kMaxForced`/`kMaxEvents` read from this header; v4 stats block = 64 bytes (fps offsets 60/62 additive, 0 = not reported; `presented` counts flips); codegen `debugSymbols` assigns ONE KEY per node; `inc/scripts/live_debug.gen.hpp` always emitted; halt reuses `menuActive`/`menuOwnsPad`/`g_gameplayPaused`; `tickFromLoop` disables `tickFromScript`; breakpoints `Project::debugBreakpoints` (`"<objectId>:<nodeId>"`); watches via `flowDbgReadVar`; gate is `liveDebugEnabled` NOT `liveDebugOn`; `[ps2]` fallback.
- One-shot asks ride spare bits of the command flags word (bits: 0-2 transport, 3 VU capture, 4 index, 5 free-RAM, 6 screenshot, 7 render-cost, 8-23 flush index, 24-31 fact-override count); four edits: `livedbg::Command`, `sameStateAs`, `encodeCommand`, reset in `livedbgTick`. Self-screenshot traps (`writeFrameCapture`): GS WORDS -> BLOCKS (`/64`), `getPreviousRealFrameBuffer()`, cache flush, `ps2_screenshot`; own `fopen`/`fwrite`; `dbgReadFrameShot` decodes TGA by hand. Render-cost bit 7 -> `bin/rendercost.txt`, `--profile-frame`.
- Time machine `livetime.cpp/.hpp` (~160+115): `Snapshot` (`bin/livetime.bin` from game, `bin/livetime.rst` from editor) + `History`; layout hash; byte budget; capture walk in `liveTimeSource` (templates.cpp) is the list of everything a rewind restores.
- Remote Pad `livepad.cpp/.hpp` (~350+105): editor writes `bin/livepad.bin`; `kStaleFrames` (120); `kPadButtonNames`; `parseScript`; `padFromCli`; `drawRemotePadWindow`/`remotePadTick`; game twin `templates::livePadSource`; `injectVirtual` slot 1; AI `press_pad`.
- Input recorder `livereplay.cpp/.hpp` (~510+220): game writes `bin/replay.out`, `bin/replay.in`; chunked per-chunk CRC + terminal chunk; `Recording::truncated`; `encode(parse(x)) == x`; `inputreplay::seed`; `project::inputLayoutHash`; game twin `templates::inputReplaySource`; `ProjectSettings::inputRecorder` defaults false; `KbdMouse::forced`; `Runner::replay_`/`stageReplayChannel`.
- Game error catcher `App::pollGameError`: a failed `TYRA_ASSERT` dump bracketed by `======= TYRA =======` ... `================`; `extractLastTyraAssert`; `drawErrorModal` + `glfwRequestWindowAttention`/`glfwFocusWindow`; `TYRA_SOFT_ERROR`; dedupe `errorSeenSig_`/`errorGameLogSize_`/`errorRunnerLogSize_`; `EditorConfig::errorPopup`; `writeLogsToFile = !ps2link`. Hardware timeline `tools/hardware-trace.py arm PROJECT`, `src/hardware_timeline.cpp`.
- `devkit_ui.cpp` (~3.8k): Debugger window, time-machine panel, error modal, Remote Pad window, `uiScriptTick`, Render cost tab, Hardware timeline; readers `dbgReadCrashReport`/`dbgReadVuCapture`/`dbgReadFrameShot`; "file changed" is its TIMESTAMP not size; Copy image via `platform::copyImageToClipboard`.
