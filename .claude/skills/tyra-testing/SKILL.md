---
name: tyra-testing
description: >
  How to build, run and VERIFY anything in this repo: compiling the editor
  (build.ps1 on Windows, build.sh on Linux), headless CLI project creation and
  game builds, checking code
  generation without Docker, full e2e in Docker + PCSX2 (boot, emulog.txt,
  reliable window screenshots via the bundled script), and audio verification.
  Use this skill EVERY time you need to test a change, run the editor, build a
  game, boot PCSX2, take a screenshot, create a scratch project, or decide
  "how do I know this works?" — including before writing a PROGRESS.md entry or
  claiming a change is verified. There is no unit-test suite in this repo; this
  skill is the testing story.
---

# Building, running and verifying

There is **no committed test suite** (no CTest, no test/ dir). Verification is
layered: compile → codegen inspection → PCSX2 boot → visual/log/audio checks.
Use the cheapest layer that actually exercises your change, and be honest in
PROGRESS.md about which layer you reached (the existing entries distinguish
"verified in PCSX2" from "compiles, needs a pad test by a human").

## Layer 0 — build the editor

The editor builds and runs on **Windows and Linux**. Pick the script for the
box you are on; they take the same flags and do the same things.

```powershell
./build.ps1          # → build/tyrax-editor.exe (fetches missing vendor deps itself)
./build.ps1 -Run     # build + launch the GUI
./build.ps1 -Clean   # full rebuild
```

```bash
./setup.sh --deps    # one-time on a bare box: toolchain + dev headers
./build.sh           # → build/tyrax-editor
./build.sh --run     # build + launch the GUI
./build.sh --clean   # full rebuild
```

Needs `scoop install mingw cmake ninja` (Windows) or `./setup.sh --deps`
(Linux — apt/dnf/pacman/zypper, the lists live in deps.sh). build.sh checks
the tools and the pkg-config headers up front, names the exact install command
for the distro it is on, and refuses to configure rather than failing later
inside cmake. This is also the compile check for everything under `src/` —
warnings matter, the build is expected to be clean.

**Only one platform's compiler runs at a time, so a cross-platform change is
only half-checked until the other side builds too.** Anything touching
`src/platform.*`, `wire.cpp`, the Runner or CMakeLists needs a build on both,
or say so in PROGRESS.md.

**Third-party dependencies live in exactly one list per platform: `deps.ps1`
and `deps.sh`.** `setup.ps1`/`setup.sh` fetch from them and `build.ps1`/
`build.sh` probe them before configuring, so adding a dependency **to both** is
all it takes — the build guard picks it up for free and `git clone`s it on the
next build. Add one anywhere else, or to only one of the two, and you recreate
the bug this arrangement exists to prevent: the lists used to drift, and a
worktree that predated a new dependency reached cmake with the sources missing.

So when a build dies with **`Cannot find source file: vendor/<something>`**
(usually followed by `No SOURCES given to target: tyrax-editor`), it is not a
corrupt checkout — that path simply isn't on disk yet. The build script
normally fixes it by itself; run `./setup.ps1` / `./setup.sh` directly if you
want the fetch without a build. The same applies after merging a branch that
added a dependency: the merge brings the CMake reference, not the clone. Probes
are real source files, so a half-finished clone reports as missing rather than
sneaking through — delete the directory and re-run setup when the guard says a
probe is still absent after a fetch.

## Layer 1 — headless CLI (no GUI needed)

(`build\tyrax-editor.exe` on Windows, `build/tyrax-editor` on Linux — written
`TYRAX` below.)

```
TYRAX --new <name> <parentDir> [width] [depth] [empty|fpp] [unitsPerMeter]
TYRAX --build <projectDir> [--run]   # exit code 0 = success
TYRAX --resave <projectDir>          # load + save, no Docker
TYRAX --refresh-gen <projectDir>     # regen sources, no Docker
TYRAX --dump <projectDir>            # JSON project summary
TYRAX --dump-graph <projectDir> <object> [scene]
TYRAX --apply-graph <projectDir> <object> <g.json> [scene] [--append]
TYRAX <projectDir|project.tyra>      # open GUI on a project
```

- `--new` scaffolds a complete game project (all generated sources, Makefile,
  Dockerfile) **without Docker** — instant way to get a fixture. `fpp` seeds a
  single Player entity; `empty` is an orbit-camera scene with no objects.
  Defaults match the *New Project* dialog: 100x100 terrain, 1 unit = 1 m, the
  **debug** profile with Live Link on, USB keyboard & mouse off. It echoes the
  terrain size and world scale, so `--new` + a grep over the `.tyra` is the
  cheapest check that a new-project default landed (a fixture that needs the
  old release-profile behavior has to set it explicitly).
- `--build` streams the whole Docker build log to stdout and returns a real
  exit code — the backbone of scripted e2e runs.
- `--resave` loads a project and writes the `.tyra` (+ heights) straight back
  out — **no Docker**. Because `project::load` runs every format migration,
  this is the clean way to test/round-trip a `.tyra`-format change headlessly:
  strip/alter a field, `--resave`, and inspect the rewritten file. Also the
  one-shot batch-migration tool for existing projects.
- `--refresh-gen` runs `project::refreshGenerated` directly — the clean way to
  check codegen without Docker (supersedes the "run --build and let it fail"
  trick below, which still works). **It does NOT run `texbake`**, so nothing
  under `.res-baked/` is rebuilt: quantized textures, atlas pages, the terrain
  AO map and the scene lightmap atlas all stay as the last `--build` left them.
  Comparing a baked image after a `--refresh-gen` compares a stale file against
  itself and silently "proves" that your change did nothing — use `--build`
  whenever the thing you are measuring is an asset. It also runs the **asset bakes that live
  inside refreshGenerated**: animated models into `res/models/*.tskl` and
  static ones into `res/models/*.tmdl` (docs/model-pipeline.md), each printing
  its problems as `[anim bake]` / `[model bake]` lines on stdout. So a model
  format / LOD change is verifiable headlessly: refresh, then read the file's
  bytes (a few lines of Python on the layout in `src/tmdl.hpp` /
  `glbparser.cpp` tell you the tier vertex counts). Note the texture bake
  (`.res-baked`, which decides what actually ships) runs in `--build`, not
  here. `--dump` / `--dump-graph` / `--apply-graph`
  are machine-readable project I/O (apply validates node types + link pin
  rules and saves) — handy for scripted graph fixtures; `--ai-graph` runs the
  whole AI generation (docs/ai-tools.md). To e2e-test the AI pipeline without
  a real backend, put a stub `claude.cmd` on PATH that swallows stdin
  (`findstr /r ".*" > nul`) and echoes a graph JSON — the Generator, parser,
  append-merge and save all exercise for real (see PROGRESS 65).
- Create scratch projects in a **short** path outside the repo — the
  convention is `%TEMP%\tyra-editor-test\<name>`. Do NOT use the session
  scratchpad for anything that will boot in PCSX2: its path is ~180+ chars
  and PS2 `loadelf` truncates long `host:` ELF paths (emulog shows a mangled
  `secname ...`), crashing to a null PC before the Tyra banner — which looks
  exactly like an engine bug and is not one.

## Layer 2 — codegen checks without Docker

Most features live or die in the generated code, and you can inspect it
without building:

- `--new` writes every generated file; grep them for your new constants/logic.
- For an **existing** project, `project::refreshGenerated()` runs at the very
  start of `--build`, *before* Docker is contacted — so even with Docker
  stopped, a failed `--build` still refreshes `inc/scene_data.hpp`,
  `src/gen/flow_graph.gen.cpp`, etc. for inspection. There is no
  `--no-docker` flag; the expected outcome is "Failed to start docker
  container..." + exit code 1 with fresh generated files on disk.
- When inspecting, remember the ownership split (see tyra-editor-dev): `.gen.*`
  files and `scene_data.hpp` are always rewritten — trust them after a refresh;
  `terrain_game.cpp` / `controls.hpp` / `script.hpp` regenerate only while their
  marker line is intact, so a stale-looking file there may be user-owned by
  design, not a codegen bug.
- To test a specific graph/scene shape, edit the project's `<name>.tyra` file
  directly (it is the source of truth; the editor tolerates external edits and
  discards the stale undo history in `<name>.history`), then refresh and inspect.
- `examples/script-demo/` is a checked-in generated project — a useful diff
  baseline, but only as fresh as its last regeneration. If codegen changed
  since, regenerate the sample first (its files drift silently); don't treat a
  stale copy as ground truth.

Past features were verified with throwaway "codegen harnesses" — a scratch
`main()` that builds a `Project` in code, calls `templates::generate()` and
asserts on the emitted strings. This works because `project.cpp`,
`templates.cpp` and `json.cpp` have no ImGui/GLFW dependency — they link into a
tiny host harness without the GUI. Fine pattern; keep such harnesses in the
scratchpad, not the repo.

**Collaboration sessions are headless-testable the same way**: `session.cpp` +
`wire.cpp` have no GUI dependency, so a harness can run a host `Session` and a
client `Session` **in one process over 127.0.0.1 with real sockets** (drain
`drainEvents()` in a sleep loop) to test join/transfer/cache/kick/refresh, and
drive `session::diffModel`/`applyEdit` directly on two `Project` replicas for
convergence property tests (random concurrent edits + the host relay rule →
assert byte-identical serializations; that test caught two real divergence
bugs). Link the same .obj set as the model harness plus `session`, `wire`, `platform`,
and `-lws2_32` on Windows (`-pthread` elsewhere; `wire.cpp` needs no extra
library on Linux). For the interactive layer, run **two editor instances on one
machine** (the second without a project), host from A, join from B at
`127.0.0.1` — loopback is not blocked by Windows Firewall even when the LAN
prompt was declined.

## Layer 3 — full e2e: Docker build + PCSX2 boot

Prerequisites: Docker **running** (Docker Desktop on Windows, `docker` + the
compose plugin on Linux) and PCSX2 with a BIOS configured — auto-detected in
`Program Files\PCSX2`, or on Linux from PATH / flatpak / an AppImage under
`~/Applications` or `~/Downloads`. Anything else: set the path in
*Edit > Preferences*.

```
TYRAX --build <projectDir> --run
```

What happens (see `src/runner.cpp`): generated files refresh → `docker compose
up -d --build` (container `<name>-compiler-1`) → engine sources checksum-synced
into the shared volume, `libtyra` rebuilt if changed → project rsynced → `make`
→ WAV sfx converted with `adpenc` → `bin/` synced back → existing PCSX2
processes killed → `HostFs = true` forced in PCSX2.ini → PCSX2 launched on the
ELF.

Notes:
- First-ever build downloads the `h4570/tyra` image and compiles the engine
  (minutes). Subsequent builds take seconds unless the engine changed.
- PCSX2.ini is found portable-first (an `inis/` next to the executable), then
  per OS: the Documents known folder on Windows — **Documents may be
  OneDrive-redirected** (e.g. `...\OneDrive - <org>\Dokumenty\PCSX2\`), not
  `%USERPROFILE%\Documents` — or `$XDG_CONFIG_HOME/PCSX2/inis` and the flatpak
  sandbox (`~/.var/app/net.pcsx2.PCSX2/config/PCSX2/inis`) on Linux. Logs and
  screenshots live next to it (`logs/emulog.txt`, `snaps/`).
- Missing `HostFs` = "Failed to load ...png" assert on the first fopen. The
  editor enforces it, but PCSX2 rewrites its ini on exit — if a game asserts on
  asset loading, check HostFs first. (The launcher also configures PCSX2's
  emulated USB ports — USB1 = HID keyboard, USB2 = HID mouse — whenever the
  project's *Keyboard & mouse controls* preference is on; `bin/log.txt` prints
  `KbdMouse: keyboard driver ready` / `mouse driver ready` when the game saw
  the devices. See `docs/keyboard-mouse.md`.)
- **Synthetic input into PCSX2** (scripted keyboard/mouse tests): plain
  `SetForegroundWindow` from a background shell silently fails — use the
  ALT-tap + `AttachThreadInput` trick and VERIFY `GetForegroundWindow`
  afterwards, or inputs go nowhere. `PostMessage` WM_KEYDOWN to the main
  window works for keys; mouse MOTION only registers via real cursor moves
  (`SetCursorPos` walks — PCSX2 recenters the captured cursor every frame, so
  park-and-hold offsets read as constant velocity), and mouse BUTTONS were
  never seen from synthetic events at all — test buttons by hand. Posting
  synthetic WM_RBUTTONDOWN to the render child can wedge PCSX2's mouse input
  until relaunch.
- For ISO/cdrom0: testing use `Project > Export PS2 ISO`, then boot the ISO in
  PCSX2 (covers the path-conversion code that host: boots skip).
- **Keep the fixture project's path short.** PCSX2's host: loader gives up on
  an ELF path over ~145 characters: emulog stops after `ELF Loading: ...`, the
  EE never reaches `is executing`, `bin/log.txt` is never written, and the
  window is black with no diagnostic. The editor warns, but a scratchpad path
  under `/tmp/claude-*/...` blows past the limit on its own - put e2e fixtures
  in `~/tyra-projects/<name>` (or the Windows equivalent) instead. If a boot
  produces nothing but `TLB Miss` spam, measure the path before debugging the
  game.
- **Docker on Linux runs the container as root**, so a `docker` group that was
  granted in the current login session is not yet active in an already-running
  shell. Either start a fresh session or accept that `docker` needs privilege
  there; the build itself needs no root once the socket is reachable.

### Reading the results

- **emulog.txt**: success looks like `ELF <path> is executing`; failure signals
  are `Assertion` lines or an early exit. **`TYRA_LOG`/EE printf does NOT land
  in emulog** even with EnableEEConsole=true. The reliable failure signal is the
  game's own **`bin/log.txt`** (host: fs) — TYRA_LOG output plus any assertion
  dump, bracketed by `======= TYRA =======` … `================`. A failed
  assert no longer paints the screen (the engine halts quietly, leaving the last
  frame up — see tyra-engine-dev), so grep `bin/log.txt` for the banner rather
  than screenshotting for assert text; the running editor also pops that dump in
  a copyable dialog. (A screenshot still shows *where* the game froze.)
- **Screenshots**: PCSX2's F8 via SendKeys is flaky. On Windows use the bundled
  script — a GDI capture that works reliably:

  ```powershell
  powershell -File .claude/skills/tyra-testing/scripts/screenshot-window.ps1 `
      -ProcessName pcsx2-qt -OutFile <scratchpad>\shot.png
  ```

  It also works for the editor itself (`-ProcessName tyrax-editor`) — useful for
  verifying viewport rendering without a human.

  **On Linux there may be no screen capture at all**: under a Wayland session
  the compositor refuses non-interactive capture, so `gnome-screenshot -f`
  exits 0 and writes nothing and the `org.gnome.Shell.Screenshot` D-Bus method
  answers `AccessDenied`. Do not spend time on it — for **editor viewport**
  work there is a better substitute anyway: an **offscreen GL harness**
  (PROGRESS 208). A hidden GLFW window (`GLFW_VISIBLE` false, `GLFW_INCLUDE_NONE`
  before glfw3.h so the loader's symbols win), `glInit()`, a real `Viewport`,
  real `render()` calls at a real panel size, then `glReadPixels` off
  `lastImageFbo_` into a PNG through the already-linked `stb_image_write`
  (do NOT define `STB_IMAGE_WRITE_IMPLEMENTATION` — menubake.cpp.o has it, and
  stbi_load comes from app.cpp.o, so link ALL of `build/`'s objects minus
  `main.cpp.o`, `-no-pie`). That isolates the viewport image from the UI, is
  measurable (bounding boxes, bar widths, pixel ratios) rather than
  eyeballed, and works with no display permissions at all. What it cannot
  cover — the surrounding UI and anything dragged by hand — say so in
  PROGRESS.md and leave it for a human.
- **Rendering correctness**: switch PCSX2 to the **software renderer** before
  judging visuals — the HW renderer masks GS raster-window wrap bugs that real
  hardware shows. Give the game a few seconds to reach a steady state, then
  screenshot; compare against a known-good screenshot when hunting regressions.
  **A strict pixel A/B between two RUNS usually cannot work** — an orbiting or
  auto-spinning camera is at a different phase in each boot, and the
  interlaced FIELD modes alternate fields, so window captures never line up
  (a "1.7M pixels differ" result is almost always this, not your change). Two
  ways out: freeze the camera/pose in the fixture (what the VU0-skinning
  entry did), or better, **compare the DATA on the console**: log an FNV hash
  (and the first few raw bit patterns) of whatever the change produces from
  inside the game, and diff the log. For a format change you can even load
  BOTH formats in one run and compare them against each other — one build,
  no camera dependency, and it isolates host-vs-EE float differences from
  real bugs (see PROGRESS 163).
- **Performance**: PCSX2's FPS overlay, software renderer, 3+ samples. Full PAL
  frame rate is 50 FPS — the generated showcase scenes hold it. For *where* a
  frame is spent, enable the built-in **frame profiler** (debug build profile +
  *Preferences > Build > Show frame profiler*): per-phase EE ms on the HUD
  (whole frame / scene / usable-highlight / particles). For **GS VRAM**
  specifically, a debug build prints `VRAMSTAT` lines into the game's
  `bin/log.txt` (texture binds/hits/uploads/re-uploads/evictions, resident
  count, free MB, largest free block) — the honest way to tell "the scene is
  thrashing textures" from "the scene is just heavy"; see
  [docs/gs-vram.md](../../../docs/gs-vram.md). A `--build --run` also kills
  every other PCSX2 instance, and parallel worktree sessions run their own —
  when several are up, `screenshot-window.ps1 -ProcessName pcsx2-qt` grabs
  whichever it finds first, so check the window title in the capture (or
  select the process by `MainWindowTitle`) before trusting a screenshot.
  For a finer breakdown,
  the manual COP0/HUD deep-dive (own the generated `terrain_game.cpp`, bracket
  phases with `mfc0 $9`, deterministic camera orbit, in-run A/B, engine-side
  counters) is written up in [docs/profiling.md](../../../docs/profiling.md) —
  frames are almost always EE-bound; `endFrame` time is mostly vsync idle, not
  GS load.
- **What is the OWNER debugging right now?** When the user asks about "my
  scene", "the last capture", "why does my model look like that", do NOT guess
  at project paths — their projects live wherever they put them, and a blind
  `Get-ChildItem -Recurse` over the disk finds nothing useful. Ask the machine:

  ```powershell
  build\tyrax-editor.exe --debug-state
  ```

  It prints, per project, the live devkit artifacts in `bin/` with **how old**
  each is, and decodes the interesting ones inline — `vucap.bin` as
  *"frame 661, flush 2/9, 16 mesh(es), 94 tris in, 512x448"*, `livedbg.bin` as
  *"frame 1111, scene 0, HALTED"*. The last line names the freshest artifact on
  the whole machine, which is almost always the thing being talked about. Then
  go in with `--dump-vucap <thatDir>` (or read `bin/log.txt`).

  Where the list comes from, and its limits:
  - **Running editors publish themselves** (`devsession.hpp`): one small file
    per process under `%LOCALAPPDATA%\tyra-editor\sessions\<pid>.ini`
    (`$XDG_STATE_HOME/tyra-editor/sessions/` off Windows), carrying the open
    project, the build profile, whether the game is live/halted and at which
    frame, and **which transport** — `pcsx2` or `ps2link`. A file per pid, so
    several editors at once each get a line. Liveness is the **heartbeat**
    (refreshed every ~4 s), not a pid probe: a session that stopped beating is
    shown as `stale` rather than hidden, because a crashed editor's last known
    project is information. This is the source that works when nothing else
    does — a project opened for the first time, or a game on real hardware with
    no local emulator process to find.
  - **`editor.ini`'s recent-project list** (`%LOCALAPPDATA%\tyra-editor\editor.ini`,
    machine-global, shared by every build and worktree). It is rewritten the
    moment a project is **opened**, so entry 0 is the last one opened — but a
    project created by `--new` and never opened in the GUI is not in it, and it
    has been seen to miss a project that WAS open (unreproduced; the session
    pointer is the reason that no longer matters).
  - **the default projects folder** (`~/TyraProjects` unless Preferences moved
    it), scanned for the projects the list misses.
  - Anything else needs the path: `--debug-state <projectDir>` reports one
    project directly.

  **A running game beats all of it** — that is a process query, not a file one:

  ```powershell
  Get-CimInstance Win32_Process -Filter "name='pcsx2-qt.exe'" | Select-Object CommandLine
  ```

  The `-elf` path is `<projectDir>\bin\<name>.elf`. Same trick for
  `tyrax-editor.exe`: its command line carries the project it was opened on,
  and its `MainWindowTitle` reads `TyraX - <project>`. And **never kill either
  process by name** — the owner may be sitting in front of one (a link step
  failing with *"cannot open output file tyrax-editor.exe: Permission denied"*
  means exactly that; ask them to close it, or link a check binary under
  another name).

  **On a real PS2 there is no game process to find, and the channel dies with
  the editor.** A ps2link deploy is served by a `ps2client.exe` that the RUNNER
  spawns (`src/runner.cpp`), so closing the editor takes the file server with
  it: the console keeps running but every devkit file freezes at its last
  write, and commands written to `livedbg.cmd` are never seen. Symptom:
  `livedbg.bin` stops advancing while the console still answers `ping`. The fix
  is a redeploy (*Run on PS2*, F6), not a retry — and it is why
  `--debug-state` reports the transport.

  That leaves a bind for **scripted** hardware debugging: a probe needs the file
  server alive, but the editor that hosts it also drives `livedbg.cmd`, and two
  writers on that file is a known hazard. Break it by hosting the server
  yourself. **A game that is still running does NOT need a redeploy** — it is
  blocked on `host:` and resumes the moment a server answers:

  ```bash
  cd <projectDir>/bin && ../../tools/ps2client/bin/ps2client.exe -h <ps2-ip> listen
  ```

  `listen` serves the console's file traffic and nothing else — within seconds
  `livedbg.bin` starts advancing again and captures work.

  **Do not send ps2client COMMANDS at a console with a game loaded.** Measured
  the hard way: with a game running (or starving), any client that connects is
  immediately conscripted as its file server and the command never reaches
  ps2link — `reset` and `dumpmem` both returned -1 while the game's `host:` opens
  scrolled past. Attaching a `listen` server first does let a command through,
  and **the one that got through froze the game**. The reliable sequence is the
  runner's: nothing else attached, `reset` (exit 0 only when the link is free),
  then `execee`. And ps2link's extra commands are **not** the free lunch the
  help text suggests: on the pinned build `dumpmem` answers `EE: pkoDumpMem()
  write failed` (the destination file is created, zero bytes) and `scrdump`
  exits 0 having written nothing — vestigial pko-era plumbing, not working
  tools. Anything wanted from them is a **ps2link patch** (the workflow exists:
  `tools/ps2link-usbhid/` clones a pinned ps2link, applies a patch and builds
  it in Docker), and hardware-only — PCSX2 runs no ps2link. (Verified: a session
  orphaned for 20 minutes came straight back, no reboot, no rebuild.) Only when
  the game is actually gone do you need the runner's two commands —
  `ps2client -h <ip> -t 10 reset`, then `ps2client -h <ip> execee host:<name>.elf`
  with `cwd = <project>/bin`, that second process being the file server for the
  whole session. Use the **main checkout's** copy of ps2client either way: a
  worktree path is a different binary to Windows Firewall and pops a prompt
  nobody is watching.

  With the server yours and the editor closed, a probe can pin each flush in
  turn and summarise the frame — which is how "my model shows 2 meshes" was
  traced to a bag flush full of terrain (PROGRESS 204). Since PROGRESS 205 the
  **flush map** makes that one read instead of a walk: `livedbg.bin` (snapshot
  **v4**) ends with a 64-byte stats block (FPS, flushes/quadwords/vertices to
  VU1, GS VRAM free/low-water/largest/resident, object counts, free EE RAM) and
  one 8-byte row per bag flush (qw, unpacks, verts, program). A ~60-line Python
  probe reads it — that is how the block was verified, cross-checking the VRAM
  figures against the game's own `VRAMSTAT` log line and the flush map's vertex
  counts against `--dump-vucap` of the same capture. **v3 snapshots still
  parse** (a console running an older build keeps working, minus stats), so
  when stats are missing the first question is "was the GAME rebuilt?".
  Free EE RAM is measured only when asked (command flags **bit 5**): the
  engine's measurement allocates every free block until malloc fails.
- **Flow-graph logic, without a pad or a screenshot**: a debug build with the
  *Live Debugger* preference on (docs/live-debugger.md) writes
  `bin/livedbg.bin` every 6 frames - per-node hit counters, a ring of recent
  fires, the flow variables and save values, the halted flag - and reads
  `bin/livedbg.cmd` (breakpoint list, halt/step, force-fire). Both are small
  fixed-layout binaries (`src/livedbg.hpp`), so **a ~40-line Python probe
  replaces the whole editor for scripted verification**: read the snapshot to
  assert which nodes ran how often and what the variables hold, and write a
  command to prove Pause freezes the counters, Step frame advances exactly
  one frame, or a force-fire runs a trigger's branch (that is how entry 191
  was verified). The key -> object/node map is `src/gen/livedbg.sym`; the
  editor and the probe must not both drive `livedbg.cmd` at once (the editor
  rewrites it whenever it goes missing or its state changes).
- **Inspect what went to VU1**: arm a capture (Debugger > VU, or command flag
  bit 3 in `livedbg.cmd`) and the game writes `bin/vucap.bin`;
  `tyrax-editor --dump-vucap <projectDir>` prints the decoded chain (DMA tags,
  VIF codes, UNPACK destinations, the MSCAL entry point) and the first vertices.
  That CLI is how the decoder is verified without the GUI - a healthy capture
  shows `UNPACK V4_32 -> VU1 addr 0` for the scales, referenced `num=21` blocks
  for the double-buffered vertex data, and model-space positions with w=1.0. It
  also lists the **meshes** in the flush (a bag flush carries a dozen; the GUI
  draws one at a time), which **bag flush of the frame** the capture is, the live
  render resolution, and the findings the panel paints amber. **A project with no
  runnable flow-graph node generates no devkit layer at all**, so a scratch
  fixture needs a graph (`--apply-graph`) before it can capture anything -
  `live_debug.gen.cpp` being a 220-byte comment is the tell. Consecutive captures
  of the same draw are byte-identical in LENGTH (same bag, fixed 16 KiB memory
  tail); tell them apart by mtime or the frame in the header, never by size. A
  scripted probe selects the flush the same way the panel does: flags bit 3 arms
  a capture, bit 4 means "index in bits 8-23", and without bit 4 each capture
  walks to the next flush (that is how PROGRESS 201 was verified). **Delete
  `livedbg.cmd` before booting a fixture** - a leftover command is applied at
  boot and eats the first capture, which reads exactly like an off-by-one in the
  walk. If
  you see tags like `refe qwc=32789` or float garbage, the walker lost sync (see
  the by-reference rule in tyra-engine-dev).
- **Symbolize an address from a running/crashed game**: `tyrax-editor
  --symbolize <projectDir> 0x...` runs the container's
  `mips64r5900el-ps2-elf-addr2line` against `bin/<name>.elf.sym`, the unstripped
  copy a DEBUG build keeps (`Makefile.base` writes it when the generated Makefile
  sets `KEEPSYM=1`; the shipped ELF is `strip --strip-all`ed, so a release
  project has nothing to resolve against). Needs the build container up.
- **Testing a crash is harder than it looks**: PCSX2 will not produce the
  exception for you - writing to address 0 does NOT fault on the PS2 (main RAM
  starts at 0) and a misaligned load went through unharmed. And installing the
  EE crash handler wedges the game under PCSX2 (entry 194), so that path is a
  hardware-only test today. What IS testable in the emulator: the report format
  (write a synthetic `bin/crash.txt` and let the editor parse + symbolize it),
  the TYRAX error block (a `.flownode` calling `TYRA_SOFT_ERROR` puts a real one
  in the game's log), and the heartbeat post-mortem (kill the game and watch the
  Debugger notice).
- **Prove a release build is devkit-free**: `tyrax-editor --audit-release
  <projectDir>` reads the built ELF and exits 0 (clean) / 1 (something leaked),
  printing text/data/bss so the debug-vs-release cost is a number. Every release
  build also runs it and logs the verdict. **Negative-test it too** - run it
  against a DEBUG ELF and confirm it fails and names the findings, otherwise a
  broken check reads exactly like a clean build (entry 193).
- **Live Logic patches without the GUI**: `livelogic.cpp` has no GUI
  dependency, so a ~100-line host harness (link it against the editor's
  `build/CMakeFiles/tyrax-editor.dir/src/*.obj` minus `app`/`viewport`/
  `gl_loader`/`main`, plus `vendor/ufbx/ufbx.c.obj` and your own
  `STB_IMAGE_IMPLEMENTATION` TU) can `project::load` a project, edit graph
  params in memory, `livelogic::compile` + `encode` and write
  `bin/livelogic.bin` - i.e. do exactly what `App::liveLogicTick` does. Combined
  with the Live Debugger's telemetry that gives a fully scripted end-to-end
  check of hot-patched logic: measure a node's fire RATE before and after the
  patch (entry 192). **When a patch half-works** (the visible effect lands but
  debug keys/state look wrong), suspect the wire STRIDE first, and check that
  `build/tyrax-editor.exe` was rebuilt before the `--build` that generated the
  game's parser - a stale editor binary generates a stale interpreter.
- **The editor window sometimes renders WHITE on this machine** (title bar
  only, capture is blank) - a GL present quirk, not a code regression. The
  check that settles it costs 30 seconds: capture
  `D:	yra-editoruild	yrax-editor.exe` (the main-repo baseline binary)
  the same way. If that is blank too, GUI visual verification is unavailable
  for this session: say so in PROGRESS rather than claiming a visual check
  that did not happen.
- **Audio**: EE-side logs are invisible, so meter the PCSX2 process instead —
  on Windows the WASAPI session peak meter (e.g. via `AudioMeterInformation`),
  on Linux `pactl list sink-inputs` (the PCSX2 sink input's volume/peak, or a
  short `parec` capture of the monitor source). Silence vs bursts at expected
  times proved music/sfx features before; a by-ear speaker check stays with the
  human.
- **Two-player modes** (docs/multiplayer.md): the split/shared toggle is
  testable with ONE keyboard: give the scene two Player objects and a pause
  menu with the "Player count" option block, then drive pad 1 via PostMessage
  (Start=Return opens the menu, Cross=K cycles the row) and screenshot — the
  frame visibly flips between full-screen and the top/bottom split (or the
  pulled-back shared camera). Pad-2 hot-join (Start on pad 2) needs a second
  pad configured in PCSX2's Pad2 slot — that part stays a hands-on test.
- **Flow-graph / gameplay logic**: wire the behavior to an unattended trigger
  (`On Start`, `Every N Seconds`) so it fires without a pad; note in
  PROGRESS.md when the interactive path (pad buttons, mouse feel) still needs
  a hands-on human test — that's the established convention.

## Choosing the right depth

| Change | Minimum honest verification |
|---|---|
| Editor UI / viewport | Layer 0 + run GUI + screenshot of the affected panel (Windows; see the screenshot note for what stands in on Linux) |
| Serialization (`.tyra`) | Layer 1 `--new` + reopen; round-trip save/load diff |
| Codegen / templates | Layer 2 grep or harness, then one Layer 3 boot |
| Engine (`vendor/tyra`) | Layer 3 always — compile happens only in Docker; SW-renderer screenshot for anything visual |
| Audio | Layer 3 + peak-meter check |
| ISO export | Export + mount the ISO on the host + boot it in PCSX2 |
