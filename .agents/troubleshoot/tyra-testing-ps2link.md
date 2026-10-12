# troubleshoot/tyra-testing-ps2link.md

Console frame measurement — the resident-IOP marker, the clean-console order, and devkit cadence. Symptom first.

## Manual ps2link launch: require the resident-IOP marker

**Cause:** `-ps2link` on `execee` alone is insufficient with this crt0. A missing marker resets IOP, removes the host filesystem/network service and can require a physical reboot; a later UDP poweroff cannot repair that loss.
**Fix:** prefer `--build PROJECT --run-ps2 IP`, which creates the marker automatically. If running ps2client manually, first create and verify **an absolute path** to `PROJECT/bin/ps2link.run` containing `ps2link`. Launch with `PROJECT/bin` as the working directory. Do not chain a failed marker write into an execee command; in PowerShell use `-ErrorAction Stop` and `Test-Path -LiteralPath` before launching. A working directory already ending in `bin` must not receive another relative `bin/`. Also stop an emulator serving the same project before hardware captures: its fresh `livedbg.bin`/`frame.tga` can disguise a disconnected console.

## A debug build over ps2link is NOT the player's frame (2026-09-23)

**Cause:** three separate debug-only costs sat inside every Motor District measurement for two days, and each looked like a property of the scene. All three were measured on a physical PS2 with `TYRA_FRAME_PROFILE 2` at a parked vantage.

| devkit cost | cadence over ps2link | what it did to the frame |
|---|---|---|
| **Remote Pad** polls `host:livepad.bin` | every 4th frame (every frame in PCSX2) | a network round trip in `loop()` ahead of `beginFrame()` |
| **Live Debugger** polls `livedbg.cmd` / writes the snapshot | every 25th frame (6th in PCSX2) | the same, rarer |
| **HUD `MEM` readout** (`showMemory`) - FIXED in 1.123.10 | every 2 s of game time | `getAvailableRAM()` malloc-probed the heap until it failed: **+31 ms** on one pose, **+75 ms** on a fragmented one. It now reads `mallinfo()` + `EndOfHeap() - sbrk(0)`; older builds still hitch |
| **`TYRA_FRAME_PROFILE 1`** | every frame | the packet walker + pipeline telemetry: HUD SCENE 13.21 -> 17.70 ms |

The first two together: the day frame read **26.3 ms with 15 of 50 frames missing their field; with Remote Pad and Live Debugger compiled out it read 20.4 ms and 0-1 of 50** — the shipped scene already held 50 FPS. 14.5-16.5 network round trips per 50 frames against 14-16 measured misses. The MEM probe is the regular **hitch every two seconds** (one 94 ms frame per 50 at 25 FPS; gone the moment `showMemory` is off).

**Fix:** the clean console measurement, in order:

1. In the project's `.tyra`: `remotePad`, `liveDebug`, `liveLink`, `liveLogic`, `timeMachine` and `inputRecorder` false (and `showMemory` too on an engine older than 1.123.10). (No Remote Pad also means no scripted day/night toggle — plan the poses.)
2. `TYRA_FRAME_PROFILE 2` in `vendor/tyra/engine/inc/debug/frame_profile.hpp` — FRAMETIME alone, transparent. Level 1 is for WHAT a frame is made of, and only its deltas are quotable.
3. `--refresh-gen`, then `tools/toolchain/native-build.ps1` directly.
4. Deploy with a bare `execee` straight after a power cycle (below), and read `FRAMETIME` off the ps2client stdout. `pre + work + stall` must come to `period`; `miss` high with `over20` low means the cost is OUTSIDE the render. `FTUPD` splits the update half of `pre` into ten sections — `in` large means the devkit's host polling.
5. **Transparency check:** capture the same pose once uninstrumented; its HUD SCENE must match the instrumented run's (13.21 vs 13.04 is a pass).
6. `FTRAW` gives every frame's `work`. A periodic spike is a TIMER — check the gaps: exactly equal gaps are frame-counted, gaps alternating 50/51 or 99/101 are seconds converted through `g_frameDt` (`everyFrames(s)`).

## Empty ps2client log, not even `loadelf:`

**Cause:** a RACE, not a wedge. ps2link reboots the IOP on `reset` and is deaf until it is back; an `execee` sent into that window is a lost UDP packet and ps2client then waits for ever. This began the moment a deploy script sent `execee` straight after `reset`.
**Fix:** kill the client and send `execee` again WITHOUT another reset — it boots. The deploy recipe: `reset`, wait ~8 s, `execee`, and on an empty log after ~20 s kill and re-send the `execee`.

## `freepad: DMA Busy ... ret = 0` repeating

**Cause:** a real IOP wedge on the next boot; only a power cycle clears it. Most `reset` + `execee` cycles of a debug build came back this way (or with an entirely empty ps2client log) and `livedbg.bin` stuck at frame 1. The prime suspect is the reset landing while the devkit's `host:` I/O is in flight; WHY it became frequent is not known — do not write a root cause.
**Fix:** straight after a power cycle ps2link is idle, so send a **bare `execee`** (a probe-`reset` followed by a launch-`reset` failed twice in a row). Budget one clean boot per power cycle for a debug build, and measure on a devkit-off build, which both resets cleanly and is the frame the player gets.

## Two arms must be looking at the same thing

**Cause:** a pad whose stick rests off centre moves the view on one boot and not the next (the car's glance camera did exactly that until 1.124.2 and faked a +2.7 ms regression).
**Fix:** compare `Loop_past_draw_count` / `Loop_past_coarse_count` in the two captures before comparing any millisecond, and take a `--capture-frame` of each arm.

## `--build --run` (PCSX2) deletes `bin/ps2link.run`

**Cause:** the next console deploy then launches without the marker.
**Fix:** re-create it before the next deploy (`printf 'ps2link' > <project>/bin/ps2link.run`), and keep the `execee` inside the script that checks it — a guard that only `exit`s is bypassed by the next command in a `;` chain.

## Devkit cadence overrides

**Cause:** the five cadence fields can be automatic, overridden, disabled or release.
**Fix:** round-trip all five cadence fields, including missing/default and out-of-range values. Overrides must only gate host I/O, never graph execution or replay/pad updates. Exercise Preferences input/save/reopen, compile and boot the generated debug game, verify snapshot frame deltas, then halt/resume and capture at a slow command interval. Check longer report intervals do not repeatedly flag a live game as hung.
