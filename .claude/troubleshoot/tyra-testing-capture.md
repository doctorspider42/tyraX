# troubleshoot/tyra-testing-capture.md

Capturing the game — the game's own framebuffer, window screenshots, watching over time, Wayland, and PCSX2 savestates. Symptom first.

## The game can photograph ITSELF (docs/devkit.md)

**Cause:** a host-side grab is in doubt (an occluded window, a locked session, a parallel worktree's emulator, or a console).
**Fix:** a debug build with *Live Debugger* on takes a one-shot on the same command channel as the VU1 capture — **flags bit 6** of `bin/livedbg.cmd`, or *Debugger > Screen > Capture frame* — reads its last finished frame out of GS VRAM and writes `bin/frame.tga`; the editor decodes that and keeps `screenshots/frame-<date>-<time>.png` in the project. Headless: `TYRAX --capture-frame <projectDir> -o shot.png` writes the command, waits for the file and decodes it. Console A/B recipe: `--build <dir> --run-ps2 <ip>` (the process that stays up IS the host: server), `--pad` to move, `--capture-frame` per vantage.

## A console capture leaves a directory named `frame.tga`

**Cause:** it works on a console since 1.55.1 and did not before: ps2sdk's `ps2_screenshot_file()` creates its output with `open(O_CREAT|O_WRONLY)`, and over ps2link that create arrives at the `host:` server as a **mkdir of the target name**, so the host gets a DIRECTORY called `frame.tga` and the open fails.
**Fix:** the runtime writes the file itself now (docs/devkit.md). Over ps2link one capture freezes the game for about three seconds; in PCSX2 it lands between two frames. Scripted through the editor's own encoder: `Command c; c.seq = n; c.captureFrame = true; livedbg::writeCommand(bin + "livedbg.cmd", c);` then wait for `frame.tga` and read it.

## Telling a finished `frame.tga` from a partial one

**Cause:** the file is an uncompressed 32-bit TGA, **BGRA, bottom row first**, exactly `18 + w*h*4` bytes; it is written a scan line at a time over `host:`, so mid-write reads are common.
**Fix:** decide "still writing" from the size still GROWING rather than from a number of tries (over ps2link the write takes SECONDS). PIL opens it directly. Its alpha is already opaque (the game writes 255). What comes back is the **GS raster**, not the television picture — no aspect correction, no letterbox. To cross-check against the emulator, crop the PrintWindow grab to its non-black columns and compare resized: measured **0.91/255** mean absolute difference on an fpp fixture.

## Screenshots: choosing the wrong back-end makes a run fiction

**Cause:** PCSX2's F8 via SendKeys is flaky. The bundled script has two capture back-ends.
**Fix:**

```powershell
powershell -File .claude/skills/tyra-testing/scripts/screenshot-window.ps1 `
    -ProcessName pcsx2-qt -OutFile <scratchpad>\shot.png              # GDI (default)
powershell -File .claude/skills/tyra-testing/scripts/screenshot-window.ps1 `
    -ProcessName pcsx2-qt -PrintWindow -Auto -OutFile <scratchpad>\shot.png
```

| | default (GDI `CopyFromScreen`) | `-PrintWindow` |
|---|---|---|
| reads | the SCREEN | the window's own content |
| occluded window | **captures whatever covers it, silently** | works, fully covered |
| focus | raises the window first (steals it) | raises nothing, moves nothing |
| fails by | looking perfectly fine | coming back black — and saying so |

**Use `-PrintWindow` whenever a human is at the machine, whenever anything might be in front of the window, and whenever you must not steal focus.** Use the GDI default when the window is demonstrably clear and you want the exact path every `-Watch` number was measured on. The script MEASURES the first `-PrintWindow` grab and warns once if it is entirely black; it never falls back to GDI on its own. Verified on PCSX2 v2.3.205 (software renderer, `examples/upscaler-lab`): `-PrintWindow -Auto` captures the render child (958x965, `-Trim` → 958x828) during live 50 FPS gameplay without the window coming to the front. It also works for the editor (`-ProcessName tyrax-editor`).

## `-ProcessName pcsx2-qt` captures somebody else's game

**Cause:** `-ProcessName` takes the first match, and parallel worktree sessions each run their own PCSX2.
**Fix:** use `-ProcessId <pid>`, selected off the `-elf` path: `Get-CimInstance Win32_Process -Filter "name='pcsx2-qt.exe'" | Where-Object CommandLine -like '*<project>*'`. The script warns when more than one window matches.

## Watching the game over time (Windows): `-Watch DIR`

**Cause:** a whole drive would otherwise cost one read per moment.
**Fix:** the Windows twin of `wayland-control.py watch` — same flag names, output lines and diff metric. It samples on an interval, keeps full-resolution frames as `DIR\frameNN.png` and reports one downscaled contact sheet plus a changed-pixel table:

```powershell
powershell -File .claude\skills\tyra-testing\scripts\screenshot-window.ps1 `
    -ProcessName pcsx2-qt -Watch <scratchpad>\w -Auto -Trim -Every 0.9 -Count 10 -Tile 224
```

Measured on the fpp fixture (1066x705 window, 831x623 picture): 10 tiles at 224 px = a 926x576 sheet, **~711 tokens** where ten full frames would be ~6 900. One run: four idle tiles at **0.000%**, then 41.9% / 29.1% / 22.5% / 6.9% while a turn and a walk were held, then 0.000% after release. `-Auto` is deterministic on Windows (takes the biggest visible CHILD window, so it works on a paused emulator/static menu). Add `-Trim` and the black pillar bars go (a 1050x623 widget came out **831x623**, exactly 4:3) — but never `-Trim` for a stability/motion measurement. `-Area X,Y,W,H`, `-Client` and `-NoActivate` also apply to single shots; `-Area` is window-relative and deliberately NOT cached. `-OnlyChanged PCT`, `-IdleStop K`/`-IdleBelow`, `-Every`, `-Count`/`-For S`, `-Tile`, `-Cols`, `-Sheet`, `-NoFrames` mirror the Wayland flags. It only CAPTURES, so it composes with a background `--pad`.

## A GDI grab captures the owner's browser instead of the emulator

**Cause:** a GDI grab reads the SCREEN, so an occluded window captures whatever is on top — silently, and for every frame of a long run. This has bitten twice (once during development, once in the field).
**Fix:** use `-PrintWindow` (reads the window's own content). Keep `-NoActivate` for the case where the window is already clear and you merely do not want focus stolen — but `-NoActivate` alone removes the raise **without** removing the hazard.

## On Linux/Wayland use `wayland-control.py`

**Cause:** the editor's GLFW window and PCSX2's Qt window are Wayland surfaces, so X11 tools see nothing (`xwininfo -root -tree` lists neither).
**Fix:** it talks to **mutter's own D-Bus APIs** (`org.gnome.Mutter.ScreenCast` for pixels over PipeWire, `org.gnome.Mutter.RemoteDesktop` for input). Needs `python3-gi` + `gstreamer1.0-pipewire`. Dead ends: `gnome-screenshot -f` exits 0 writing nothing, and `org.gnome.Shell.Screenshot`/`Introspect` → `AccessDenied`. Commands: `shot`, `watch`, `area`, `move`, `movrel`, `button`, `click [--at x,y] [--double] [--button b]`, `drag`, `scroll up|down|left|right [--steps N] [--at]`, `key <chord>...`, `keydown`/`keyup`, `type`, `sleep`, with global `--monitor` and `--cursor`.

```bash
python3 .claude/skills/tyra-testing/scripts/wayland-control.py shot -o <scratchpad>/shot.png
```

Drive a whole interaction with `script` (ONE mutter session — a chain of one-shot calls re-negotiates PipeWire each time and drops pointer state):

```bash
python3 .claude/skills/tyra-testing/scripts/wayland-control.py script - <<'EOF'
key ctrl+n
sleep 0.6
click --at 917,382
key ctrl+a
type WlTest_42
shot dialog.png --area 807,345,375,365
EOF
```

Coordinates are global screen pixels. Two limits: there is **no per-window capture** (`RecordWindow` needs ids only the denied `Introspect` gives, and probing random ids matched nothing — capture the monitor and `--area` crop; a parked camera under a static sky gave `415,425,1157,536` for the ground only, and `--aspect 4:3` grew it to `415,80,1157,868` against the true `415,93,1157,868`), and a **pointer-locked** client never sees absolute motion (use `movrel`, not `move`).

## Watching over time (Linux): `watch`

**Cause:** a minute of gameplay would otherwise cost thirty reads.
**Fix:**

```bash
python3 .claude/skills/tyra-testing/scripts/wayland-control.py \
    watch <scratchpad>/w --auto --aspect 4:3 --every 1 --for 20 --tile 224
```

Measured on the endless-runner fixture (1920x984 screen, 1157x868 render area): 9 tiles at 224 px = a 696x576 sheet, **~534 tokens** where nine full frames would be ~12 000. `--auto` finds the render area by MOTION and refuses anything under 2% of the screen or outside a 0.9-2.6 aspect; add `--aspect 4:3`; the rect is cached in `DIR/area.txt` (`area` prints it). `--only-changed PCT` drops frames that changed less than PCT **since the last KEPT frame** (beware saturation on a repeating scene); `--idle-stop K` ends after K consecutive frames under `--idle-below` (0.05% default). Also `--every`, `--count`/`--for`, `--tile W`, `--cols`, `--sheet`, `--no-frames`, `--settle`, `--trim` (shaves black borders). It composes with the Remote Pad.

## PCSX2 savestates and GS dumps, unattended (docs/emulator-captures.md)

**Cause:** you need savestates/GS dumps without focus or global input.
**Fix:** `scripts/pcsx2-capture.py run <elf> --stage --states 3 --gsdump` starts a **private** PCSX2 (`-datapath` under `%TEMP%\tyra-editor-test\pcsx2-capture-<slot>`, its own ini with PINE on), takes savestates over PINE and one single-frame GS dump, writes `report.txt` and stops only the PID it started. `gs <dump>` / `state <p2s>` analyse hand-taken captures of any title. Needs `pip install zstandard numpy`. Results go to `./test-results/<kind>-<timestamp>/` relative to cwd (run from the project root); only the emulator instance, staged ELF and savestate spool stay in `%TEMP%` (`--out` overrides). One `--slot` per parallel run; the user's global `PCSX2.ini` is never touched. A savestate lands at VSync, so VIF1 is always idle (shows at most the last ping-pong packet — use the VU1 tap for our chains). GS pixel counts are scissored coverage, not fill.
