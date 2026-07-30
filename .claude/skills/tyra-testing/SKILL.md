---
name: tyra-testing
description: >
  How to build, run and VERIFY anything in this repo: compiling the editor
  (build.ps1 on Windows, build.sh on Linux), headless CLI project creation and
  game builds, checking code
  generation without Docker, full e2e in Docker + PCSX2 (boot, emulog.txt,
  reliable screenshots, and DRIVING both the game's controller (`--pad`) and the
  EDITOR's own UI (`--ui-script`, clicking widgets BY NAME) unattended — neither
  needs window focus — plus synthetic keyboard/mouse via the bundled scripts, GDI
  on Windows, mutter's D-Bus APIs on Wayland), and audio verification.
  Use this skill EVERY time you need to test a change, run the editor, build a
  game, boot PCSX2, take a screenshot, PRESS A BUTTON / move the player / drive
  the emulator or the editor without a human, create a scratch project, or decide
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
./build.ps1 -Dev     # -O1 iteration build → build-dev/tyrax-editor.exe
```

```bash
./setup.sh --deps    # one-time on a bare box: toolchain + dev headers
./build.sh           # → build/tyrax-editor
./build.sh --run     # build + launch the GUI
./build.sh --clean   # full rebuild
./build.sh --dev     # -O1 iteration build → build-dev/tyrax-editor
```

A clean Release build is ~1-1.5 min on 16 cores; a one-file edit to the UI is a
few seconds. **`-Dev`/`--dev` is for the edit-compile-look loop** — `-O1`
instead of `-O3` (which is about two thirds of the compile time), in its own
`build-dev/` so alternating with Release costs nothing. Two rules: never
benchmark or ship a Dev build, and **never use one to verify anything that
bakes** — gibake/matbake/aobake/pngquant are raytracers and quantizers, and at
-O1 a bake that takes seconds takes minutes, which reads as a hang rather than
as the flag you chose. Verify bakes with the Release build. If ccache/sccache
is on `PATH` CMake uses it automatically, which is what makes switching
worktrees cheap.

**Timing a build honestly:** back-to-back clean builds on a laptop drift ~20%
from thermals alone, so a single before/after pair proves nothing. Alternate
the two variants (A, B, A, B, …) and compare like rounds — and read
`build/.ninja_log` (`start_ms end_ms mtime output`) rather than guessing where
the time went: it tells you the per-target durations, and summing
`end-start` against the wall clock gives the real parallelism, which is how the
26k-line-app.cpp critical path and the libimgui.a ordering stall were found.

`build.cmd` / `setup.cmd` exist for plain `cmd.exe` and double-clicks. They are
**wrappers only** — they map `run`/`clean`/`dev` onto `-Run`/`-Clean`/`-Dev` and forward to
`build.ps1`/`setup.ps1` with `-ExecutionPolicy Bypass`. Never give them logic of
their own: they used to carry a hand-copied dependency list, it drifted behind
`deps.ps1`, and a fresh clone got "vendor\tyra is not an empty directory" from
setup plus "Cannot find source file: vendor/ufbx/ufbx.c" from cmake.

Needs `scoop install mingw cmake ninja` (Windows) or `./setup.sh --deps`
(Linux — apt/dnf/pacman/zypper, the lists live in deps.sh). **The Windows
compiler is MinGW-w64 GCC; MSVC is not a supported target and no flag makes it
one** — `src/templates.cpp` holds the PS2 templates as raw string literals far
past MSVC's hard 16380-byte cap per literal, so Visual Studio's default
`x64-Debug` CMake preset dies with a wall of *C2026 string too big* (plus
C2589/C2660 in `wire.cpp`, where `windows.h`'s `min` macro eats `std::min`).
Report of that error means "you configured with the wrong kit", not a code bug.
build.sh checks
the tools and the pkg-config headers up front, names the exact install command
for the distro it is on, and refuses to configure rather than failing later
inside cmake. This is also the compile check for everything under `src/` —
warnings matter, the build is expected to be clean.

**Only one platform's compiler runs at a time, so a cross-platform change is
only half-checked until the other side builds too.** Anything touching
`src/platform.*`, `wire.cpp`, the Runner, CMakeLists **or any of the paired
build scripts** needs a build on both, or say so in PROGRESS.md. The pairs that
must move together — `deps.ps1`/`deps.sh`, `setup.ps1`/`setup.sh`,
`build.ps1`/`build.sh`, the `if(WIN32)`/`else()` halves of CMakeLists, the
`#ifdef _WIN32`/`#else` halves of `platform.cpp` — are listed in
tyra-editor-dev ("Platform parity"). Editing one side only is the single most
repeated way a change lands broken on the platform its author doesn't use.

**On Windows, `build` is `build.cmd`.** PATHEXT resolves `.CMD` before `.PS1`,
so the bare command runs the wrapper, which just calls `build.ps1`. That is a
deliberate one-line delegation now — it used to be a full cmd translation with
its OWN four-entry dependency list, which is how a tree that built fine on
Linux died on Windows with `fatal error: miniaudio.h: No such file or
directory`: the guard that fetches missing dependencies was in `build.ps1`, and
`build.ps1` was not what ran (PROGRESS 214). When a Windows build fails on a
missing `vendor/` header, check WHICH script ran before suspecting the code.

**Third-party dependencies live in exactly one list per platform: `deps.ps1`
and `deps.sh`.** `setup.ps1`/`setup.sh` fetch from them and `build.ps1`/
`build.sh` probe them before configuring, so adding a dependency **to both** is
all it takes — the build guard picks it up for free and fetches it on the
next build. Add one anywhere else, or to only one of the two, and you recreate
the bug this arrangement exists to prevent: the lists used to drift, and a
worktree that predated a new dependency reached cmake with the sources missing.

Each entry is fetched at a **pinned commit**, not a branch (`git init` + `git
fetch --depth 1 <url> <sha>`, since `git clone --branch` refuses a SHA), with a
fallback to our mirror fork. This is what makes a build reproducible, so when
you are chasing "it worked yesterday", `git -C vendor/<dep> rev-parse HEAD`
should always equal the SHA in `deps.sh` — if it does not, that checkout
predates the pinning and is stale. Fix it by deleting the directory and
re-running setup, not by pulling in it.

`setup.ps1`/`setup.sh` also fetch ~45 MB of MakeHuman **CC0 data** into
`vendor/mh-assets` for the Character Generator (`$MhFiles`/`$MhAssets` in
`deps.ps1`, `tyrax_mh_files`/`MH_ASSETS_DIR` in `deps.sh` — the usual paired
lists, and both must describe the same 138 files). It is data, not sources, so
`build.ps1`/`build.sh` never block on it and the editor compiles without it —
the window just explains how to get it. A `chargen` harness or a Character
Generator test therefore needs setup to have run at least once; the harness
resolves the directory relative to the exe, or from `vendor/mh-assets` when run
from a repo root.

To check the two lists still agree after touching either, dump and diff them
rather than eyeballing: `bash -c '. ./deps.sh; tyrax_mh_files; printf "%s\n"
"${MH_FILES[@]}"' | sort` against the same projection of `$MhFiles` from
`deps.ps1`. They drifted once already — the Linux list was missing entirely,
so the Character Generator was quietly Windows-only.

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
TYRAX --new <name> <parentDir> [width] [depth] [empty|fpp|thirdperson] [unitsPerMeter] [--no-terrain]
TYRAX --build <projectDir> [--run]   # exit code 0 = success
TYRAX --resave <projectDir>          # load + save, no Docker
TYRAX --refresh-gen <projectDir>     # regen sources, no Docker
TYRAX --bake-gi <projectDir>         # bake global illumination, no Docker
TYRAX --dump <projectDir>            # JSON project summary
TYRAX --dump-graph <projectDir> <object> [scene]
TYRAX --apply-graph <projectDir> <object> <g.json> [scene] [--append]
TYRAX --pad <projectDir> "<script>"  # drive the RUNNING game's pad, no focus
TYRAX --ui-script [projectDir] "<script>"  # drive the EDITOR's own UI, no focus
TYRAX <projectDir|project.tyra>      # open GUI on a project
```

- `--new` scaffolds a complete game project (all generated sources, Makefile,
  Dockerfile) **without Docker** — instant way to get a fixture. `fpp` seeds a
  single Player entity in walk mode and `thirdperson` the same entity in
  third-person mode (both generate the SAME game sources — the mode is a
  per-object property); `empty` is an orbit-camera scene with no objects. The
  preset is the project's permanent `template` field, so it is also the way to
  fixture either game template — the editor deliberately offers no way to
  switch afterwards.
  Defaults match the *New Project* dialog: 100x100 terrain, 1 unit = 1 m, the
  **debug** profile with Live Link on, USB keyboard & mouse off — except the
  preset, which stays `empty` here while the dialog starts on `fpp`. It echoes the
  terrain size and world scale, so `--new` + a grep over the `.tyra` is the
  cheapest check that a new-project default landed (a fixture that needs the
  old release-profile behavior has to set it explicitly).
  **`--no-terrain`** (accepted anywhere among the optional arguments) scaffolds a
  scene with no ground at all (docs/terrain.md) — the fixture for the void-floor
  behavior, and the fast way to check the terrain-less codegen: the `.tyra` gets
  `"enabled": false` and `inc/scene_data.hpp` reads `TERRAIN_ENABLEDS = {false}`
  with `TERRAIN_TEXTURES = {-1}`. Verifying it in PCSX2 needs a floor object in
  the scene, or the player falls at boot — which is the feature, not a bug.
- `--build` streams the whole Docker build log to stdout and returns a real
  exit code — the backbone of scripted e2e runs.
- `--bake-gi` runs the whole global-illumination bake for every scene
  (docs/global-illumination.md) into `.res-baked/gi/` and then refreshes the
  generated files, so the probe table and the lightmap flags follow - **no
  Docker, no GUI**. It is the headless twin of the *Global illumination*
  tab in *Tools > Ambience Editor* and the only practical way to verify GI in a script: bake,
  grep `inc/ao_data.gen.hpp` for `SCENE_AO_ATLAS_GIS`/`SCENE_AO_MAP_GIS` (1 =
  the scene shipped GI), check `inc/probe_data.gen.hpp` has a
  `SCENE_PROBE_GRIDS` entry, then `--build --run` and A/B the screenshot
  against the same project with `"giEnabled": false` in its `.tyra`. Two
  traps worth knowing: the bake is NEVER part of a build (a build only READS
  the cache), so a change to the scene silently falls it back to the pre-GI
  lighting until you re-bake; and it prints per scene how long it took plus
  the atlas/terrain/probe dimensions, which is the fastest sanity check that
  it saw any geometry at all (`atlas 0` means no eligible receivers).
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
  its problems as `[anim bake]` / `[model bake]` lines on stdout, plus the
  **credits page strips** (docs/credits.md) into `res/credits/<roll>-<k>.png` -
  so a roll's typography and page count are checkable with no Docker and no GUI:
  refresh, stitch the pages back into one image and look at it, and read
  `CREDITS_PAGE_TOTAL` / `contentH` out of `inc/credits_data.gen.hpp`. So a model
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
- Both `--build` and `--refresh-gen` also run the **procedural bake** first
  (`procbake::bakeAll` - docs/procedural-generation.md): stale Procedural
  volumes are baked into their chunk meshes and the project is saved, printing
  `procedural: baked N volume(s) -> ...`. So a headless build of a project with
  a procedural volume MUTATES the `.tyra` (the chunk objects are real scene
  objects) - expect that diff, and use it: the fastest way to test a graph
  change is `--refresh-gen` + grep `inc/scene_data.hpp` for the
  `<volume>#<asset>-x<i>z<j>` chunk objects. The graph model itself is
  harness-testable (procgraph/procgen/procbake link without GUI, GL or
  templates.cpp - see PROGRESS 171 for the property list that caught three real
  bugs).
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

**`App`'s own private methods are reachable from such a harness too**, which is
the difference between testing a copy of the logic and testing the shipped
function. Link every `build/CMakeFiles/tyrax-editor.dir/**/*.o` except
`main.cpp.o` (plus `libimgui.a`, glfw and `-ldl -lrt -lm -lGLX -lOpenGL
-lpthread`, `-no-pie`), and reach the members with `#define private public`
before `#include "app.hpp"`. Two rules make it work: include **every header
app.hpp includes first, normally**, so the macro only ever reaches app.hpp's own
body (libstdc++'s `<sstream>` fails to compile otherwise — *redeclared with
different access*), and neutralise whatever pulls in GL. For the Material
Editor that is `matEdPaintW_ = 0`, which makes `matEdRegenLayer` /
`matEdComposite` / `matEdSavePaintTarget` early-return, so a real
`matEdSavePreset` → file → `matEdApplyPreset` round trip runs **with no GL
context at all** (PROGRESS 223). What it cannot cover is the panel around the
call — say so rather than implying a click-through happened.

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
- **Driving the game: use the Remote Pad, not the emulator's keyboard.**
  `tyrax-editor --pad <projectDir> "<script>"` writes the pad state the running
  game polls out of `bin/livepad.bin` (docs/remote-pad.md), so **no window needs
  the focus on either OS** and the whole class of problems below stops applying.
  It is the honest way to test anything pad-driven unattended:

  ```powershell
  build\tyrax-editor.exe --pad %TEMP%\tyra-editor-test\padtest `
      "stick r 110 0; wait 1.5; stick r 0 0; stick l 0 -127; wait 2.5; neutral"
  ```

  `press cross [s]` / `hold up` / `release all` / `stick l|r <x> <y>` /
  `wait <s>` / `neutral` / `pad 1|2`, separated by `;`. Needs a **debug** build
  with the *Remote Pad* preference on (default) - the driver warns on stderr
  when the project was built without the channel, which is the only way "nothing
  happened" can mean "the game cannot hear you". Four things worth knowing:
  a `hold` with no `wait` after it does nothing visible (the driver detaches on
  exit and the game lets go - on purpose, so a killed script cannot leave a
  direction held); the game reads **only the analog sticks**, so a held D-pad
  `Up` changes nothing (that is the game, and it looks exactly like a broken
  tool); the pad answers every 4th frame over ps2link instead of every frame;
  and the state is dropped after ~2.4 s without a refresh, so a long hold needs
  the driver to stay alive rather than one write. To hold something while
  another tool works, run `--pad` in the background with a long enough `wait`.
  Measured on the fpp fixture: 3 s idle changes 620 px (the PCSX2 status bar
  only), a 1.5 s right-stick turn changes ~197k px, a 2.5 s forward walk ~1.4M -
  and 4 s after the script the frame is idle again, which is what proves the
  release actually happened.
  When a script runs clean and the game ignores it, check in this order: is
  `src/gen/live_pad.gen.cpp` the real runtime or the "nothing to compile here"
  stub (that answers "was this ELF built with the channel" in one line), was the
  game rebuilt since the preference changed (the poller is compiled in - a save
  is not enough), and is `bin/livepad.bin` being written where the RUNNING ELF
  lives (`Get-CimInstance Win32_Process -Filter "name='pcsx2-qt.exe'"` prints the
  `-elf` path; a second fixture directory is the classic mix-up).
- **Synthetic input into PCSX2** (the older, focus-dependent path - still the
  only way to reach PCSX2's OWN keys, e.g. F8 or the pause hotkey). **On Linux
  this is the easy side**: `wayland-control.py` (see Screenshots below) injects
  through the compositor, so PCSX2 cannot tell the events from a real keyboard —
  click the render area once to focus it, then send pad keys, holding them with
  `keydown`/`keyup` where a direction has to be held. Per `[Pad1]` in PCSX2.ini
  the ones that matter are **W/A/S/D = left stick**, **T/G/F/H = right stick**,
  **K = Cross**, `Return` = Start, arrows = D-pad. Note the generated game reads
  **only the analog sticks** (`getLeftJoyPad`/`getRightJoyPad` in
  `updatePlayer`), so a held D-pad `Up` changes nothing — that is the game, not
  the injection, and it looks identical to a broken tool. Mouse buttons work
  too, and `movrel` covers the captured-cursor case. **On Windows** it is
  the mess below: plain
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

  **On Linux/Wayland use the bundled `wayland-control.py`** — screenshots *and*
  synthetic keyboard/mouse, no human in the loop:

  ```bash
  python3 .claude/skills/tyra-testing/scripts/wayland-control.py shot -o <scratchpad>/shot.png
  ```

  It talks straight to **mutter's own D-Bus APIs** (`org.gnome.Mutter.ScreenCast`
  for pixels over PipeWire, `org.gnome.Mutter.RemoteDesktop` for input). Neither
  prompts, and both work for native Wayland surfaces — which is the whole point:
  the editor's GLFW window and PCSX2's Qt window are Wayland surfaces, so X11
  tools see nothing at all (`xwininfo -root -tree` lists neither). Do not spend
  time on the paths that look obvious and are dead: `gnome-screenshot -f` exits 0
  and writes nothing, and `org.gnome.Shell.Screenshot` / `org.gnome.Shell.Introspect`
  answer `AccessDenied` to any plain session client. The mutter APIs one level
  below them are not gated. Needs `python3-gi` + `gstreamer1.0-pipewire`, both
  stock on Ubuntu GNOME.

  Drive a whole interaction with `script`, which runs it in ONE mutter session —
  a session dies with the process, so a chain of one-shot calls re-negotiates
  PipeWire every time (~0.6 s each) and drops pointer state in between:

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

  Coordinates are global screen pixels — the same space a full-screen `shot`
  returns, so read a target's position off one capture and click it in the next
  step. Verified on this box: menus and dialogs opened by clicking in the editor,
  `ctrl+n` / `ctrl+a`, typing that needs shift levels (`WlTest_42` arrives
  verbatim), right-drag and wheel orbiting/zooming the viewport, and `k` reaching
  PCSX2 as **pad 1 Cross** inside the emulated machine (the PS2 BIOS advanced
  past its language screen).

  Two limits. There is **no per-window capture**: mutter's `RecordWindow` wants a
  window id that only the denied `Shell.Introspect` hands out, and the ids are
  random-based rather than sequential (probing 0..79 matched nothing), so capture
  the monitor and `--area` crop instead — and remember an occluded window
  captures as whatever is on top of it. And a **pointer-locked** client (PCSX2
  with mouse capture on, a game grabbing the cursor) never sees absolute motion:
  use `movrel` there, not `move`.

  **To DRIVE the editor, use `--ui-script`** (docs/ui-scripting.md) - the editor
  runs for real and holds its own mouse and keyboard, naming WIDGETS instead of
  pixels, with **no window focus needed on either OS**:

  ```powershell
  build\tyrax-editor.exe --ui-script <projectDir> `
      'frames 20; click Tools; click "Remote Pad"; shot panel.png; quit'
  ```

  `click|rightclick|hover|doubleclick|hold|drag|wheel|key|text|wait|frames|shot|dump|log|quit`
  plus `expect` / `expect-not` / `expect-checked` / `expect-unchecked`; the exit
  code is 0 only if every step passed, so a scripted GUI run gates a shell
  script. **Always start with `dump`** (`--ui-script <dir> "frames 20; dump"`):
  it prints every widget on screen with its rect AND its checked/open state, so
  you neither guess a label nor read a value off a screenshot. Four things that
  save time: a step that names a target WAITS for it (menus need no sleeps, and a
  timeout prints what was on screen instead), a target is `"Window/Label"` with
  prefix matching (`"Remote/Cross"` works, and so does a menu entry without its
  `...`) — **quoted with DOUBLE quotes, the only kind the tokenizer strips**, so
  on PowerShell put the whole script in single quotes or the shell eats them and
  a two-word target arrives as two tokens, `shot` writes the same self-captured framebuffer as `TYRAX_SHOT`, and
  what it CANNOT name is anything not made of ImGui widgets - the 3D viewport
  (one big item: `drag` inside it, or work through the Project panel's list), the
  imnodes flow canvas and the ImGuizmo gizmo. Not all modals close on `escape` -
  click their `Cancel`; `dump` shows it. A **combo's dropdown** cannot be opened
  by name either: `BeginCombo` never calls ImGui's item-info hook, so `dump`
  shows the rect with an empty label - set the value another way and read the
  result off a `shot`.

  **`wheel <target> <notches>`** is how a canvas ZOOM is driven (no widget
  exposes one): it holds the cursor on the target and injects one notch per
  frame, and it is the only step that may resolve a bare WINDOW name, so
  `wheel "Flow Graph" -6` scrolls over the middle of the canvas. The way to
  verify a zoom is not a screenshot but `dump` at two zoom levels: the node
  widgets ARE ordinary items, so their rects give you the scale factor and the
  offsets between them give you whether the layout stayed self-similar
  (PROGRESS 233 measures both to under 0.1%).

  Two things about the node canvases specifically (PROGRESS 247, which needed a
  screenshot of a node tooltip). **A node's param widgets register only while
  its window is the FRONT tab.** Behind another tab the window is drawn with
  `SkipItems`, so `dump` lists the node rects (unlabelled - imnodes gives them
  no label) and *nothing inside them*, which reads exactly like "the canvas is
  unreachable". The Flow Graph ships docked behind Viewport in the Default
  layout, and dropping a window from the layout's `open` list does NOT help -
  Viewport is not optional and is not in that list. Set `"activeLayout"` in the
  project's `.tyra` to a layout whose recipe FOCUSES the window instead
  (`LayoutRecipe::Debugger` = index 3 focuses Flow Graph; `Procedural` focuses
  Procedural), then everything inside the nodes is nameable as
  `"Flow Graph/<Param>"`. **And `wheel` parks the cursor**, which is how you
  reach a tooltip that hangs off no widget at all: it holds the mouse at the
  target's centre while the notches arrive and ImGui keeps that position
  afterwards, so `wheel "Flow Graph" 1; wait 1.6; shot x.png` screenshots
  whatever the canvas shows for the point under the window's middle - put the
  node there (screen = canvas origin + model position x nodeScale, both readable
  off one `dump`) and you have the node-hover tooltip. What stays out of reach
  is the **add menu**, which needs a right-click on empty canvas.

  The editor can also **capture its own framebuffer** on a timer, with no
  display permissions at all: set `TYRAX_SHOT=<dir>` (and optionally
  `TYRAX_SHOT_EVERY=<seconds>`, default 2) and it writes `<dir>/shotNN.png` every
  interval (`App::captureFrameIfRequested`). It reads what the editor DREW rather
  than what was presented, so it is the one path that survives the AMD present
  quirk that leaves the window blank. It cannot click anything - before
  `--ui-script` existed the way to reach a panel behind a menu was to pre-open it
  by adding its key to the active layout's `open` list in the project's `.tyra`
  (`kLayoutWindowKeys` in app.cpp has the names - `"drone"`, `"tree"`,
  `"material"`, ...), which is how PROGRESS 210/211 verified whole tool windows.
  That trick still works and is fine for a pure "does it render" check; anything
  that needs a click or an assertion is now a UI script.

  For **editor viewport** work an **offscreen GL harness** (PROGRESS 208) is
  still the better instrument when you want numbers instead of a picture.
  A hidden GLFW window (`GLFW_VISIBLE` false, `GLFW_INCLUDE_NONE`
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

  **A screen effect that cannot be SEEN and one that is not happening look
  identical** (PROGRESS 231). A Camera Shake at amplitude 0.05 measured **0
  pixels differing** across four captures 250 ms apart — the ease-out cuts it to
  0.03, and a 3 cm camera translation is sub-pixel at 512x448 with nothing closer
  than 5.6 m. At amplitude 2.0 the horizon swept 215 px and 545k pixels differed.
  So before debugging a subtle visual, do the arithmetic on how many PIXELS the
  effect is worth at the render resolution, and re-run the fixture with the
  effect turned up past that. The flip side is useful too: a fixture whose frames
  are otherwise byte-identical between captures is a clean instrument — that zero
  is what lets a 200-pixel horizon shift mean something.

  **A capture is worth more when it is measured** (PROGRESS 132). Whichever
  tool produced the PNG, read pixel ROWS out of it rather than eyeballing:
  plateau widths and adjacent-step sizes are what told flat shading apart from
  Gouraud on a cylinder, and two engine builds A/B'd from a frozen fixture came
  out byte-identical, which is a much stronger statement than "looks the same".
  A few lines of PIL will do it.
  Driving PCSX2 by hand (rather than through `--build --run`) inherits none of
  the launcher's setup: the Runner is what forces `HostFs` and the USB ports in
  `PCSX2.ini`, so run through the editor at least once first, and set
  `Renderer = 13` (software) in that ini while PCSX2 is **closed** — it rewrites
  the file on exit.

  **When an A/B compares two objects in ONE frame, prove which is which**
  before reading anything into it. The screen's X runs opposite to world X in
  the generated game (a Player at yaw 0 looks along +Z), so "the left one" is
  the object at *positive* X. An hour went into a banding hypothesis about the
  wrong cylinder. The cheap disambiguator: force one object's colour to
  something absurd for a single run and see which one changes.
- **Rendering correctness**: switch PCSX2 to the **software renderer** before
  judging visuals — the HW renderer masks GS raster-window wrap bugs that real
  hardware shows. Give the game a few seconds to reach a steady state, then
  screenshot; compare against a known-good screenshot when hunting regressions.
  The reverse trap bites within ONE run: **an axis-aligned walk over the flat
  checkerboard terrain is nearly invisible to a pixel diff**, because
  translating along the grid maps the repeating pattern onto itself — a 2.5 s
  forward hold from the default FPP pose changed an 11-pixel band at the
  terrain's far edge and nothing else, which reads as "input never arrived".
  Turn the camera first (right stick), then walk: the same hold then moves
  ~150k pixels.
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
  tools. Anything wanted from them is a **ps2link patch**, which is the normal
  workflow here: the console always runs OUR ps2link (`tools/ps2link/` clones a
  pinned upstream, applies `tyrax.patch` and builds it in Docker via
  `build.sh`/`build.ps1` — see docs/ps2link-setup.md), and hardware-only —
  PCSX2 runs no ps2link. (Verified: a session
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

  **Prefer a C++ probe over a Python one here, and link the editor's own
  decoder**: `src/livedbg.cpp` has no GL/ImGui/project.hpp dependency, so a
  ~60-line `main()` compiled with **just that one file** (`g++ -std=c++20
  -static probe.cpp <repo>/src/livedbg.cpp -I<repo>/src` — `-static`, or the exe
  dies with `0xC0000139` looking for MinGW's DLLs) calls `loadSymbols` +
  `readSnapshot` and prints per-node hit counts with their node ids and types,
  every watch variable BY NAME, and the armed timers. A hand-rolled Python
  reader can disagree with the format; this one cannot. That turns "did this node
  fire, how often, and what did it leave behind" into a command — which is how
  the 84-node flow-graph expansion (entry 231) was verified: wire every new
  mechanism to an unattended trigger, have each write a **distinct predictable
  integer** into a flow variable, and read the lot back in one shot. `hashMatch`
  in that dump is the check that the ELF's symbol table is the one the editor
  thinks it is.

  Two things that dump makes clear and would otherwise read as bugs: a trigger
  whose exec output is **unwired** has a hit count of 0 (codegen does not
  instrument an empty chain) even though its bool/number outputs work fine; and
  an event's payload trailing the live counter is the **one-frame bus latency**
  showing up as a number, not a lost write.
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
- **Testing a crash is harder than it looks**: **PCSX2 cannot produce an EE
  exception at all**, so catching one is a HARDWARE-ONLY test (entry 246).
  Everything tried passed through unharmed there: a signed-overflow `add`, an
  illegal opcode, a write to address 0 (main RAM starts at 0) and a misaligned
  load. On a real console the reliable trigger is one line in a user-owned
  script - `volatile int a = 0x7FFFFFFF, b = 1; asm volatile("add %0, %1, %2" :
  "=r"(r) : "r"(a), "r"(b));` - which raises cause 12 and lands a real
  `bin/crash.txt`; `--symbolize` then names the exact source line. (Installing
  the handler no longer wedges anything - that was `ee_dbg_install(2)`, fixed in
  246.) What IS testable in the emulator: the report format
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
- **Audio**: the *editor's* own audio (the Drone Generator's audition,
  `src/audiopreview.cpp`) is testable directly — a host harness can open the
  device and print peak levels (PROGRESS 210), and the generator's DSP needs no
  device at all: link `dronegen.cpp` alone, render a preset and measure the
  samples (automation included — PROGRESS 211). For the GAME's audio, EE-side
  logs are invisible, so meter the PCSX2 process instead —
  on Windows the WASAPI session peak meter (e.g. via `AudioMeterInformation`),
  on Linux `pactl list sink-inputs` (the PCSX2 sink input's volume/peak, or a
  short `parec` capture of the monitor source). Silence vs bursts at expected
  times proved music/sfx features before; a by-ear speaker check stays with the
  human.
- **Two-player modes** (docs/multiplayer.md): the split/shared toggle is
  testable with no controller at all through the Remote Pad - give the scene two
  Player objects and a pause menu with the "Player count" option block, then
  `--pad <dir> "press start; wait 0.5; press cross"` and screenshot: the frame
  visibly flips between full-screen and the top/bottom split (or the pulled-back
  shared camera). **Pad-2 hot-join is now scriptable too** (`pad 2; press
  start`) - it no longer needs a second physical pad in PCSX2's Pad2 slot, since
  the overlay is applied to the game's own second connector.
- **Flow-graph / gameplay logic**: an `On Button` / `On Action` trigger is
  reachable unattended now (`--pad <dir> "press cross"`), so prefer that over
  rewiring the behavior to `On Start` / `Every N Seconds` just to test it. What
  still needs a human: mouse FEEL, analog ramps judged by eye, and the editor's
  own on-screen pad being CLICKED (the panel writes through the same
  `livepad::write` the CLI does, but a synthetic click into the editor is its own
  problem) - say so in PROGRESS.md, that's the established convention.

### The unattended input test, end to end

The whole recipe, on Windows, with nothing focused and no human. Every step is
described above; this is the order that works, and the shape a PROGRESS entry
about a pad-driven feature should be able to quote.

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

Then count changed pixels between the pairs with a few lines of PIL
(`ImageChops.difference(...).get_flattened_data()`). Four things make this a real
test rather than a screenshot:

- **the idle pair is the control.** Without it "the picture changed" proves
  nothing. Measured on this fixture: 3 s of idle changes ~600 px and every one of
  them is inside PCSX2's own status bar (the FPS/EE numbers) - so anything in the
  hundreds of thousands is unambiguously the game.
- **turn before you walk.** A straight walk over the flat checkerboard maps the
  repeating pattern onto itself and can change almost nothing; the yaw turn is
  the loud signal (~197k px here, vs ~1.4M for a 2.5 s walk after it).
- **the trailing idle shot proves the RELEASE.** Back to status-bar-only
  (~1.6k px) is what shows the pad was let go rather than the input having simply
  stopped arriving - the one failure mode a "did it move?" test cannot see.
- **crop the status bar out** if you want a cleaner number: it is the only thing
  moving in an idle frame, so its rows are pure noise for every comparison.

## Choosing the right depth

| Change | Minimum honest verification |
|---|---|
| Editor UI (a panel, a dialog, a toggle) | Layer 0 + a `--ui-script` run that opens it, does the thing and ASSERTS it (`expect`/`expect-checked`), plus a `shot` to look at. No focus, no coordinates, either OS |
| Editor viewport (rendering) | Layer 0 + a screenshot of the affected panel (`shot` from a UI script, `TYRAX_SHOT` on a timer, or `screenshot-window.ps1`/`wayland-control.py` from outside) - and measure the pixels rather than eyeballing |
| Serialization (`.tyra`) | Layer 1 `--new` + reopen; round-trip save/load diff |
| Codegen / templates | Layer 2 grep or harness, then one Layer 3 boot |
| Engine (`vendor/tyra`) | Layer 3 always — compile happens only in Docker; SW-renderer screenshot for anything visual |
| Audio | Layer 3 + peak-meter check |
| Anything a player DOES (buttons, walking, menus, two players) | Layer 3 + `--pad` (see the recipe above) — an idle control shot, then drive, then measure. No human, either OS |
| ISO export | Export + mount the ISO on the host + boot it in PCSX2 |
