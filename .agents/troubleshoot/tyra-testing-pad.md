# troubleshoot/tyra-testing-pad.md

Driving the game unattended — Remote Pad, synthetic input, the end-to-end recipe, and input replay. Symptom first.

## Driving the game: use the Remote Pad, not the emulator's keyboard

**Cause:** `tyrax-editor --pad <projectDir> "<script>"` writes the pad state the running game polls out of `bin/livepad.bin` (docs/remote-pad.md), so no window needs the focus on either OS.
**Fix:**

```powershell
build\tyrax-editor.exe --pad %TEMP%\tyra-editor-test\padtest `
    "stick r 110 0; wait 1.5; stick r 0 0; stick l 0 -127; wait 2.5; neutral"
```

`press cross [s]` / `hold up` / `release all` / `stick l|r <x> <y>` / `wait <s>` / `neutral` / `pad 1|2`, separated by `;`. Also `--pad <dir> --file <script.pad>` or `--stdin`. Needs a **debug** build with the *Remote Pad* preference on (default) — the driver warns on stderr when the project was built without the channel.

## A `hold` with no `wait` after it does nothing

**Cause:** the driver detaches on exit and the game lets go — on purpose, so a killed script cannot leave a direction held.
**Fix:** always follow a `hold` with a `wait`. The game reads **only the analog sticks** (`getLeftJoyPad`/`getRightJoyPad`, called from `updatePlayer`), so a held D-pad `Up` changes nothing (that is the game, and it looks exactly like a broken tool). The pad answers every 4th frame over ps2link instead of every frame; the state is dropped after ~2.4 s without a refresh, so a long hold needs the driver to stay alive (run `--pad` in the background with a long enough `wait`). Measured on the fpp fixture: 3 s idle changes 620 px (PCSX2 status bar only), a 1.5 s right-stick turn ~197k px, a 2.5 s forward walk ~1.4M, and 4 s after the script the frame is idle again.

## A clean pad script runs and the game ignores it

**Cause:** the poller is compiled in; a save is not enough. Or the channel was not built, or the wrong `bin/livepad.bin` is being written.
**Fix:** check in this order: is `src/gen/live_pad.gen.cpp` the real runtime or the "nothing to compile here" stub; was the game rebuilt since the preference changed; is `bin/livepad.bin` being written where the RUNNING ELF lives (`Get-CimInstance Win32_Process -Filter "name='pcsx2-qt.exe'"` prints the `-elf` path).

## `Start-Process -ArgumentList` mangles a `;`-separated script

**Cause:** it does not quote its array elements, so `-ArgumentList "--pad",$P,"wait 1; stick r 110 0; neutral"` reaches the editor as loose argv words, the script it parses is the single word `wait`, and it dies with `error: line 1: wait needs seconds` — on the **stderr of a hidden process**. The game then does exactly nothing.
**Fix:** put the whole `--pad` call in a `.ps1` with the script as a LITERAL and `Start-Process powershell -File` that (a `param()` does not help), or add the quotes to the array elements yourself. Same trap for any `;`-separated script argument, `--ui-script` included.

## `error: cannot replace ...livepad.bin: Permission denied`

**Cause:** the atomic replace of `livepad.bin` races the reader. `livepad::write` renames a `.tmp` over the target, and the running game re-opens that file about every frame through PCSX2's HostFs; Windows refuses to rename over a file another process has open, so a replace comes back `Access is denied` roughly **once per 200 writes** at 40 Hz (Linux is immune).
**Fix:** `write` now retries (5 tries, 4 ms apart) and only a **step** write is fatal; a lost *refresh* is a warning and the run says so (`pad released (27 refresh write(s) lost to the reader)`). On Windows, read the driver's stderr and its exit code, and treat "no motion, exit 1" as this rather than as a dead channel.

## Synthetic input into PCSX2 (the older, focus-dependent path)

**Cause:** the only way to reach PCSX2's OWN keys (F8, the pause hotkey).
**Fix:** On Linux use `wayland-control.py` (see `tyra-testing-capture.md`): click the render area once to focus it, then send pad keys with `keydown`/`keyup` where a direction has to be held. Per `[Pad1]`: **W/A/S/D = left stick**, **T/G/F/H = right stick**, **K = Cross**, `Return` = Start, arrows = D-pad. On Windows, plain `SetForegroundWindow` from a background shell silently fails — use the ALT-tap + `AttachThreadInput` trick and VERIFY `GetForegroundWindow` afterwards. `PostMessage` WM_KEYDOWN to the main window works for keys; mouse MOTION only registers via real cursor moves (`SetCursorPos` walks — PCSX2 recenters the captured cursor every frame); mouse BUTTONS were never seen from synthetic events at all — test buttons by hand. Posting synthetic WM_RBUTTONDOWN to the render child can wedge PCSX2's mouse input until relaunch.

## The unattended input test, end to end

**Cause:** every step is described above; this is the order that works.
**Fix:**

```powershell
$P = "$env:TEMP\tyra-editor-test\padtest"      # short path - PCSX2 needs it
$S = "<scratchpad>"
build\tyrax-editor.exe --new padtest "$env:TEMP\tyra-editor-test" 100 100 fpp
build\tyrax-editor.exe --build $P --run        # boot it
Start-Sleep 22                                 # Tyra logo + splash + scene load
$shot = ".claude\skills\tyra-testing\scripts\screenshot-window.ps1"
powershell -File $shot -ProcessName pcsx2-qt -OutFile "$S\idle1.png"
Start-Sleep 3
powershell -File $shot -ProcessName pcsx2-qt -OutFile "$S\idle2.png"   # CONTROL
build\tyrax-editor.exe --pad $P "stick r 110 0; wait 1.5; neutral"
powershell -File $shot -ProcessName pcsx2-qt -OutFile "$S\turned.png"
build\tyrax-editor.exe --pad $P "stick l 0 -127; wait 2.5; neutral"
powershell -File $shot -ProcessName pcsx2-qt -OutFile "$S\walked.png"
Start-Sleep 4
powershell -File $shot -ProcessName pcsx2-qt -OutFile "$S\idle3.png"   # RELEASED
```

Count changed pixels between the pairs with a few lines of PIL (`ImageChops.difference(...).get_flattened_data()`). Four things make this a real test: **the idle pair is the control** (3 s of idle changes ~600 px, all inside PCSX2's status bar); **turn before you walk** (a straight walk over the flat checkerboard maps the repeating pattern onto itself); **the trailing idle shot proves the RELEASE** (~1.6k px is status-bar-only); **crop the status bar out** for a cleaner number. The cheap version is one `-Watch` over the whole drive.

## Recording a run and proving it reproduces (docs/input-replay.md)

**Cause:** `--pad` can drive a game, but nothing afterwards could say whether the game did the same thing.
**Fix:** requires the debug profile and `"inputRecorder": true` in the `.tyra`:

```bash
tyrax-editor --record $P recordings/smoke.tyrarep \
    --pad "wait 6; stick l 0 -127; wait 3; press cross; wait 1; neutral" --seconds 14
tyrax-editor --replay $P recordings/smoke.tyrarep ; echo "exit=$?"
```

`--seconds` is a FLOOR. `--replay` exit code: 0 = reproduced exactly, 3 = diverged, 1 = could not run. The game logs its own verdict, every line prefixed `Replay:` (one anchor for a grep over `bin/log.txt`): `Replay: finished 705 frames, 0 divergences`, `Replay: finished 705 frames, 304 divergences (first at frame 400)`, `Replay: diverged at frame 400: pos (0 1.8 10.301) yaw 0 pitch 0, expected (0 1.8 10.5) yaw 0 pitch 0`.

## Three checks that make the replay a real test

**Cause:** a file of 705 all-neutral frames replays perfectly and proves nothing.
**Fix:** (1) **Read the recording back** — `--replay-dump <file.tyrarep>` prints the timeline without running anything (every button press and every fingerprint jump, frame numbers on both; `--replay-dump f a b` the raw per-frame view, sticks included). Read it against the `Portal:` / `Pick:` lines in `bin/log.txt`. On the FPP fixture: 208 frames with the stick off-centre, 7 with a button, 669 of 705 carrying a fingerprint, player z 0 → 20.8, `dt` 0.020..0.080. (2) **Tamper with it** — change ONE frame's axis byte and re-encode so the CRC stays valid; it must come back `exit=3` and name that exact frame. (3) **Fight it with the Remote Pad** — run `--pad "hold left; wait 8"` from a SECOND process; it must still report 0 divergences (`Pad::setState` beats `injectVirtual`). A fourth for procedural worlds: grep `bin/log.txt` for `Procedural <name>: N instances, seed S` after both runs and check S is identical (on `examples/cube`: 27 instances, seed 329243691, both times).
