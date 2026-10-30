# Screenshots, the Remote Pad, synthetic input, record/replay

Read when: you drive the running game (`--pad`, held keys), take or watch screenshots from outside (screenshot-window.ps1 on Windows, wayland-control.py on Linux), run the unattended input test, or record/replay a play session. Editor UI driving: ui-script.md.

## Remote Pad: `TYRAX --pad <dir> "<script>"` (docs/remote-pad.md)

Writes the pad state the running game polls from `bin/livepad.bin` — **no focus on either OS**. Also `--pad <dir> --file <script.pad>` / `--stdin`.

```powershell
build\tyrax-editor.exe --pad %TEMP%\tyra-editor-test\padtest `
    "stick r 110 0; wait 1.5; stick r 0 0; stick l 0 -127; wait 2.5; neutral"
```
Steps: `press cross [s]`, `hold up`, `release up|all`, `stick l|r <x> <y>`, `wait <s>`, `neutral`, `pad 1|2`; separated by `;` or newlines, `#` comments.

- Needs a **debug** build with *Remote Pad* on (default); driver warns on stderr when the project was built without the channel.
- `hold` with no `wait` after it does nothing visible (driver detaches on exit, game lets go — on purpose).
- The generated game reads **only the analog sticks** (`getLeftJoyPad`/`getRightJoyPad` in `updatePlayer`) — a held D-pad `Up` changes nothing and looks like a broken tool.
- Answers every 4th frame over ps2link (every frame in PCSX2). State dropped after ~2.4 s without refresh → long holds need the driver alive; to hold while another tool works, background `--pad` with a long `wait`. Driver refreshes at 25–40 Hz off the **host** clock → a stick lands at a different frame each run; never drive a measurement camera with it (use a frame-indexed script).
- Measured on fpp: 3 s idle = 620 px (status bar only); 1.5 s right-stick turn ~197k px; 2.5 s forward walk ~1.4M; idle again 4 s after.
- Game ignores a clean script? Check in order: `src/gen/live_pad.gen.cpp` real runtime vs "nothing to compile here" stub; game rebuilt since the preference changed (poller is compiled in); `bin/livepad.bin` written where the RUNNING ELF lives (`-elf` of the process; a second fixture dir is the classic mix-up).
- A global script must read `getPressed()` with its own edge: `getClicked()` never sees a remote press (`livepad::tick` raises `clicked` inside the frame it polls).
- Pad-2 hot-join is scriptable (`pad 2; press start`), no second physical pad needed.
- **Windows traps** (both diagnosed 2026-08-03):
  - **`Start-Process -ArgumentList` does not quote array elements** → `"wait 1; stick r 110 0; neutral"` arrives as loose argv, script = `wait`, dies `error: line 1: wait needs seconds` on a hidden stderr; game does nothing. Put the call in a `.ps1` with the script as a LITERAL and `Start-Process powershell -File` it (a `param()` doesn't help), or add quotes yourself. Same for `--ui-script`.
  - **`livepad.bin` replace race** (fixed; stale binaries still have it): Windows refuses rename-over-open → `Access is denied` ~1 in 200 writes at 40 Hz; old driver aborted with `error: cannot replace ...livepad.bin: Permission denied`, exit 1 (one 9 s script in five). Now 5 retries 4 ms apart; only a **step** write is fatal; lost refreshes reported (`pad released (27 refresh write(s) lost to the reader)`). Read the driver's stderr + exit code; "no motion, exit 1" = this, not a dead channel. Linux immune.

## Synthetic keyboard/mouse into PCSX2 (focus-dependent; needed for PCSX2's own keys, e.g. F8, pause)

- **Linux**: `wayland-control.py` injects through the compositor (indistinguishable from real). Click the render area once, then keys; `keydown`/`keyup` to hold. `[Pad1]` in PCSX2.ini: **W/A/S/D = left stick**, **T/G/F/H = right stick**, **K = Cross**, `Return` = Start, arrows = D-pad. Mouse buttons work; `movrel` for a captured cursor. Verified: `k` reached PCSX2 as pad-1 Cross (BIOS advanced past language screen).
- **Windows**: plain `SetForegroundWindow` from a background shell silently fails — ALT-tap + `AttachThreadInput`, then VERIFY `GetForegroundWindow`. `PostMessage` WM_KEYDOWN to the main window works for keys; mouse MOTION only via real cursor moves (`SetCursorPos` walks; PCSX2 recentres a captured cursor every frame, so a parked offset reads as constant velocity); mouse BUTTONS never seen from synthetic events — test by hand. Synthetic WM_RBUTTONDOWN to the render child can wedge PCSX2's mouse until relaunch. One ini bound the left stick to **Z/Q/S/D** — read `[Pad1]`.
- **PCSX2 unfocused does not take injected keys**; read its status bar (FPS/VPS/Speed) to confirm the machine runs before concluding anything about motion.

## Windows: `scripts/screenshot-window.ps1`

```powershell
powershell -File .claude/skills/tyra-testing/scripts/screenshot-window.ps1 -ProcessName pcsx2-qt -OutFile <scratch>\shot.png              # GDI
powershell -File .claude/skills/tyra-testing/scripts/screenshot-window.ps1 -ProcessName pcsx2-qt -PrintWindow -Auto -OutFile <scratch>\shot.png
```

| | default (GDI `CopyFromScreen`) | `-PrintWindow` |
|---|---|---|
| reads | the SCREEN | the window's own content |
| occluded window | **captures whatever covers it, silently** | works, fully covered |
| focus | raises the window (steals focus) | raises/moves nothing |
| fails by | looking perfectly fine | coming back black — and warns |

- **Use `-PrintWindow` whenever a human is at the machine, anything may cover the window, or focus must not be stolen.** A GDI grab once captured the owner's browser instead of the emulator and nobody noticed. GDI is still the default only because a renderer that won't redraw on demand prints black under PrintWindow; the script warns once on an all-black first PrintWindow and never silently falls back. `-NoActivate` removes the raise but not the occlusion hazard.
- Verified PCSX2 v2.3.205 (software, `examples/upscaler-lab`): `-PrintWindow -Auto` captured the render child 958x965 (`-Trim` → 958x828) during 50 FPS play without raising; same rect + picture as GDI when unoccluded.
- Works for the editor (`-ProcessName tyrax-editor`).
- **`-ProcessId <pid>`**: `-ProcessName` takes the first match (warns if several) — with parallel worktrees that's someone else's game. `Get-CimInstance Win32_Process -Filter "name='pcsx2-qt.exe'" | Where-Object CommandLine -like '*<project>*'`.
- `-Auto` is deterministic: biggest visible CHILD window (PCSX2's render surface is a native child HWND) — works paused/parked/static. **`-Trim`** removes pillar bars (1050x623 → **831x623**, exactly 4:3) and makes idle = **0 px** (whole window: ~87 px/s of status-bar noise). Never `-Trim` for stability/motion measurements (see motion-and-ab-rigs.md). `-Client`, `-Area X,Y,W,H` (window-relative, not cached) also apply to single shots.
- **`-Watch DIR`** (twin of `wayland-control.py watch`, same flags/lines/metric): frames kept as `DIR\frameNN.png`, returns one contact sheet + changed-pixel table.
```powershell
powershell -File .claude\skills\tyra-testing\scripts\screenshot-window.ps1 -ProcessName pcsx2-qt -Watch <scratch>\w -Auto -Trim -Every 0.9 -Count 10 -Tile 224
```
  fpp fixture (1066x705 window, 831x623 picture): 10 tiles @224 = 926x576 sheet ≈ **711 tokens** vs ~6 900 for ten frames; ~0.1 s/frame overhead (0.9 s interval held). One run: four idle tiles 0.000%, then 41.9/29.1/22.5/6.9% during turn+walk, 0.000% after release. Over a 1.5 s right-stick hold: 17.6/27.7/21.5% then 0.000%.
  Flags: `-OnlyChanged PCT`, `-IdleStop K` / `-IdleBelow`, `-Every`, `-Count` or `-For S`, `-Tile`, `-Cols`, `-Sheet`, `-NoFrames`. Calibrate `-OnlyChanged` from a first run (5% kept 6 of 11). `-IdleStop 2` stopped on the 2nd consecutive 0.000%.

## Linux/Wayland: `scripts/wayland-control.py`

```bash
python3 .claude/skills/tyra-testing/scripts/wayland-control.py shot -o <scratch>/shot.png [--area X,Y,W,H]
python3 .claude/skills/tyra-testing/scripts/wayland-control.py script - <<'EOF'
key ctrl+n
sleep 0.6
click --at 917,382
key ctrl+a
type WlTest_42
shot dialog.png --area 807,345,375,365
EOF
```
- Uses **mutter's D-Bus APIs** (`org.gnome.Mutter.ScreenCast` via PipeWire, `org.gnome.Mutter.RemoteDesktop`); no prompts, works on native Wayland surfaces (editor GLFW + PCSX2 Qt are invisible to X11 tools: `xwininfo -root -tree` lists neither). Dead ends: `gnome-screenshot -f` exits 0 writing nothing; `org.gnome.Shell.Screenshot`/`Introspect` → `AccessDenied`. Needs `python3-gi` + `gstreamer1.0-pipewire`.
- Commands: `shot`, `watch`, `area`, `move`, `movrel`, `button`, `click [--at x,y] [--double] [--button b]`, `drag`, `scroll up|down|left|right [--steps N] [--at]`, `key <chord>...`, `keydown`/`keyup`, `type`, `sleep`; global `--monitor`, `--cursor`.
- `script` runs everything in ONE mutter session (one-shot calls renegotiate PipeWire ~0.6 s each and drop pointer state). Coordinates = global screen pixels (same as a full-screen `shot`). Verified: menus/dialogs, `ctrl+n`/`ctrl+a`, shifted typing verbatim, right-drag + wheel orbit/zoom in the viewport.
- **No per-window capture** (`RecordWindow` needs ids only the denied Introspect gives; ids random, probing 0..79 matched nothing): capture the monitor, crop with `--area`; occluded windows capture as whatever's on top. **Pointer-locked clients** never see absolute motion — use `movrel`.
- **`watch`**: samples off the negotiated stream, one contact sheet + changed-pixel table.
```bash
python3 .claude/skills/tyra-testing/scripts/wayland-control.py watch <scratch>/w --auto --aspect 4:3 --every 1 --for 20 --tile 224
```
  endless-runner fixture (1920x984 screen, 1157x868 render): 9 tiles @224 = 696x576 ≈ **534 tokens** vs ~12 000; ~1.2 s/frame overhead. Read the sheet, open single `frameNN.png`s afterwards.
  - `--auto` finds the render area by MOTION (four samples ~0.5 s apart, widest/tallest moving band). A parked camera under a static sky gave `415,425,1157,536` (ground only) → add **`--aspect 4:3`** to grow it from the bottom edge (`415,80,1157,868` vs true `415,93,1157,868`). Cached in `DIR/area.txt`. `area` prints the rect.
  - A still scene defeats it (day-night fixture: 500 px of 1.9M moved, winner was the `Speed: 64%` text). `--auto` refuses <2% of screen or aspect outside 0.9–2.6 → read the rect off one `shot`, pass `--area` (cached). `--trim` shaves black borders.
  - `--only-changed PCT` (vs last KEPT frame; dropped rows marked `-`). Metric SATURATES on repeating scenes (scrolling checkerboard never exceeded 25%; `--only-changed 25` kept one tile) — calibrate. `--idle-stop K` / `--idle-below` (default 0.05%) — verified stop after three 0.000% frames. `--every`, `--count`/`--for`, `--tile W` (the context-cost knob), `--cols`, `--sheet`, `--no-frames`, `--settle`.
- Widgets with only a `##` label (e.g. `##assetsearch`) are reachable by rect: `dump` rect + editor window screen offset (compare one dump rect with a full-screen `shot`; ~67,70 on this box) → `click --at x,y` / `type`.

## Editor self-capture: `TYRAX_SHOT`

`TYRAX_SHOT=<dir>` (+ `TYRAX_SHOT_EVERY=<s>`, default 2) writes `<dir>/shotNN.png` (`App::captureFrameIfRequested`) plus `shotNN-<window>.png` for each panel in an OS window of its own — what the editor DREW, not what was presented: survives the AMD present quirk. No clicks. Pre-open a panel by adding its key to the active layout's `open` list in `.tyra` (`kLayoutWindowKeys` in app.cpp: `"drone"`, `"tree"`, `"material"`, …; PROGRESS 210/211) — fine for "does it render"; anything with a click/assertion is a UI script.

**Editor window renders WHITE** (title bar only, blank capture) on some machines = GL present quirk. Capture the main-repo baseline binary the same way (e.g. `D:\tyra-editor\build\tyrax-editor.exe`); if also blank, GUI visual verification is unavailable this session — say so in the commit/PR, don't claim a visual check.

## The unattended input test, end to end (Windows)

```powershell
$P = "$env:TEMP\tyra-editor-test\padtest"; $S = "<scratch>"
build\tyrax-editor.exe --new padtest "$env:TEMP\tyra-editor-test" 100 100 fpp
build\tyrax-editor.exe --build $P --run
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
(Prefer `-PrintWindow -ProcessId` per above; one `-Watch` over the drive gives the same numbers in one sheet.) Count changed pixels with PIL (`ImageChops.difference(...).get_flattened_data()`):
- **idle pair = control** (~600 px, all in PCSX2's status bar);
- **turn before walk** (~197k vs ~1.4M);
- **trailing idle proves the RELEASE** (~1.6k px, status bar only);
- crop the status bar for cleaner numbers.

## Record / replay (docs/input-replay.md)

Debug profile + `"inputRecorder": true` in `.tyra`.
```bash
tyrax-editor --record $P recordings/smoke.tyrarep \
    --pad "wait 6; stick l 0 -127; wait 3; press cross; wait 1; neutral" --seconds 14   # --seconds is a FLOOR; also --pad-file, --clear-saves
tyrax-editor --replay $P recordings/smoke.tyrarep ; echo "exit=$?"   # 0 same, 3 diverged, 1 could not run; also --timeout, --keep-running, --clear-saves
tyrax-editor --replay-dump recordings/smoke.tyrarep [firstFrame lastFrame]
```
`bin/log.txt` lines start `Replay:` — `Replay: finished 705 frames, 0 divergences` / `..., 304 divergences (first at frame 400)` / `Replay: diverged at frame 400: pos (0 1.8 10.301) yaw 0 pitch 0, expected (0 1.8 10.5) ...`.
- **Read the recording before trusting a replay** (705 neutral frames replay perfectly and prove nothing). `--replay-dump` prints presses + fingerprint jumps; with a range, raw per-frame sticks/buttons/fingerprint. Pair with the game's `Portal:` / `Pick:` lines (docs/devkit.md) — turns "it sometimes throws me across the room" into "the grab is frame 390 and frames 391-407 move 0.75 of a unit each with the stick CENTRED"; recordings since 1.85.0 carry those events (dump prints "grabbed object 38"; replay names a missing one). FPP reference: 208 frames stick off-centre, 7 with a button, 669/705 fingerprinted (first ~36 boot), player z 0 → 20.8, `dt` 0.020..0.080.
- **Tamper**: change one frame's axis byte, re-encode (CRC valid) → `exit=3` naming that frame (frame 400 → `first at frame 400`).
- **Fight it**: `--pad "hold left; wait 8"` from a second process during replay → still 0 divergences (`Pad::setState` beats `injectVirtual`).
- Runtime procedural volume "new world every run": grep `Procedural <name>: N instances, seed S` after both runs, S identical (`examples/cube`: 27 instances, seed 329243691).

## Measuring a pad-driven run from outside — other notes

- Two-player (docs/multiplayer.md): two Player objects + pause menu with "Player count" → `--pad <dir> "press start; wait 0.5; press cross"`, screenshot: full screen ↔ top/bottom split (or pulled-back shared camera).
- Flow-graph `On Button`/`On Action` triggers: `--pad <dir> "press cross"` — don't rewire to `On Start` / `Every N Seconds` just to test. Still human-only: mouse FEEL, analog ramps judged by eye, clicking the editor's on-screen pad (writes through the same `livepad::write`) — say so in the commit.
- `Select-Object -First N` on a PowerShell script's output STOPS the script after N lines - a capture loop piped into it takes one shot and quits.
