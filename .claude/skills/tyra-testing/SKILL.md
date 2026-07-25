---
name: tyra-testing
description: >
  How to build, run and VERIFY anything in this repo: compiling the editor
  (build.ps1), headless CLI project creation and game builds, checking code
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

```powershell
./build.ps1          # → build/tyrax-editor.exe (fetches missing vendor deps itself)
./build.ps1 -Run     # build + launch the GUI
./build.ps1 -Clean   # full rebuild
```

Needs `scoop install mingw cmake ninja`. This is also the compile check for
everything under `src/` — warnings matter, the build is expected to be clean.

**Third-party dependencies live in exactly one list: `deps.ps1`.** `setup.ps1`
fetches from it and `build.ps1` probes it before configuring, so adding a
dependency there is all it takes — the build guard picks it up for free and
`git clone`s it on the next build. Add one anywhere else and you recreate the
bug this arrangement exists to prevent: the two lists used to drift, and a
worktree that predated a new dependency reached cmake with the sources
missing.

So when a build dies with **`Cannot find source file: vendor/<something>`**
(usually followed by `No SOURCES given to target: tyrax-editor`), it is not a
corrupt checkout — that path simply isn't on disk yet. `./build.ps1` normally
fixes it by itself; run `./setup.ps1` directly if you want the fetch without a
build. The same applies after merging a branch that added a dependency: the
merge brings the CMake reference, not the clone. Probes are real source files,
so a half-finished clone reports as missing rather than sneaking through —
delete the directory and re-run setup when the guard says a probe is still
absent after a fetch.

## Layer 1 — headless CLI (no GUI needed)

```powershell
build\tyrax-editor.exe --new <name> <parentDir> [width] [depth] [empty|fpp]
build\tyrax-editor.exe --build <projectDir> [--run]   # exit code 0 = success
build\tyrax-editor.exe --resave <projectDir>          # load + save, no Docker
build\tyrax-editor.exe --refresh-gen <projectDir>     # regen sources, no Docker
build\tyrax-editor.exe --dump <projectDir>            # JSON project summary
build\tyrax-editor.exe --dump-graph <projectDir> <object> [scene]
build\tyrax-editor.exe --apply-graph <projectDir> <object> <g.json> [scene] [--append]
build\tyrax-editor.exe <projectDir|project.tyra>      # open GUI on a project
```

- `--new` scaffolds a complete game project (all generated sources, Makefile,
  Dockerfile) **without Docker** — instant way to get a fixture. `fpp` seeds a
  single Player entity; `empty` is an orbit-camera scene with no objects.
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
bugs). Link the same .obj set as the model harness plus `session`, `wire`, and
`-lws2_32`. For the interactive layer, run **two editor instances on one
machine** (the second without a project), host from A, join from B at
`127.0.0.1` — loopback is not blocked by Windows Firewall even when the LAN
prompt was declined.

## Layer 3 — full e2e: Docker build + PCSX2 boot

Prerequisites: Docker Desktop **running**, PCSX2 installed in
`Program Files\PCSX2` with a BIOS configured.

```powershell
build\tyrax-editor.exe --build <projectDir> --run
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
- PCSX2.ini is found portable-first, then in the Documents known folder —
  **Documents may be OneDrive-redirected** (e.g. `...\OneDrive - <org>\Dokumenty\PCSX2\`),
  not `%USERPROFILE%\Documents`. Logs and screenshots live next to it
  (`logs\emulog.txt`, `snaps\`).
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
- **Screenshots**: PCSX2's F8 via SendKeys is flaky. Use the bundled script —
  a GDI capture that works reliably:

  ```powershell
  powershell -File .claude/skills/tyra-testing/scripts/screenshot-window.ps1 `
      -ProcessName pcsx2-qt -OutFile <scratchpad>\shot.png
  ```

  It also works for the editor itself (`-ProcessName tyrax-editor`) — useful for
  verifying viewport rendering without a human.
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
- **Flow-graph logic, without a pad or a screenshot**: a debug build with the
  *Live Debugger* preference on (docs/live-debugger.md) writes
  `bin/livedbg.bin` every 6 frames - per-node hit counters, a ring of recent
  fires, the flow variables and save values, the halted flag - and reads
  `bin/livedbg.cmd` (breakpoint list, halt/step, force-fire). Both are small
  fixed-layout binaries (`src/livedbg.hpp`), so **a ~40-line Python probe
  replaces the whole editor for scripted verification**: read the snapshot to
  assert which nodes ran how often and what the variables hold, and write a
  command to prove Pause freezes the counters, Step frame advances exactly
  one frame, or a force-fire runs a trigger's branch (that is how entry 177
  was verified). The key -> object/node map is `src/gen/livedbg.sym`; the
  editor and the probe must not both drive `livedbg.cmd` at once (the editor
  rewrites it whenever it goes missing or its state changes).
- **The editor GUI needs a GPU-backed GL context.** On a session without one
  (PCSX2's log naming "Microsoft Basic Render Driver" is the tell) the editor
  window comes up **blank white** and no screenshot harness can help - verify
  with a main-branch build before blaming a change, and say so in PROGRESS
  rather than claiming a visual check that did not happen.
- **Audio**: EE-side logs are invisible, so use the Windows WASAPI session peak
  meter on the PCSX2 process (e.g. via `AudioMeterInformation`) — silence vs
  bursts at expected times proved music/sfx features before; a by-ear speaker
  check stays with the human.
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
| Editor UI / viewport | Layer 0 + run GUI + screenshot of the affected panel |
| Serialization (`.tyra`) | Layer 1 `--new` + reopen; round-trip save/load diff |
| Codegen / templates | Layer 2 grep or harness, then one Layer 3 boot |
| Engine (`vendor/tyra`) | Layer 3 always — compile happens only in Docker; SW-renderer screenshot for anything visual |
| Audio | Layer 3 + peak-meter check |
| ISO export | Export + mount the ISO on Windows + boot it in PCSX2 |
