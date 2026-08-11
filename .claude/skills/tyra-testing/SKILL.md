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
  "how do I know this works?" — including before writing a commit message or
  claiming a change is verified. There is no unit-test suite in this repo; this
  skill is the testing story.
---

# Building, running and verifying

> **A note on `PROGRESS 123` citations.** They point at numbered entries of
> `PROGRESS.md`, retired at ~15 800 lines. They remain exact pointers — the file
> is in git history, and `docs/backlog.md` has the recipe. New work records
> itself in its commit message and PR body instead.

There is **no committed test suite** (no CTest, no test/ dir). Verification is
layered: compile → codegen inspection → PCSX2 boot → visual/log/audio checks.
Use the cheapest layer that actually exercises your change, and be honest in
your commit message and PR body about which layer you reached — the established
wording distinguishes "verified in PCSX2" from "compiles, needs a pad test by a
human". (That record used to live in `PROGRESS.md`, retired at ~15 800 lines;
the honesty convention outlived the file.)

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
build scripts** needs a build on both, or say so in the commit. The pairs that
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
TYRAX --build <projectDir> [--run] [--rebuild]   # exit code 0 = success
TYRAX --resave <projectDir>          # load + save, no Docker
TYRAX --migrate <projectDir>         # backup + apply format migrations
TYRAX --refresh-gen <projectDir>     # regen sources, no Docker
TYRAX --bake-gi <projectDir>         # bake global illumination, no Docker
TYRAX --dump <projectDir>            # JSON project summary
TYRAX --chat-prompt [projectDir]     # what the AI Assistant is told (docs/ai-chat.md)
TYRAX --list-nodes <projectDir>      # what the graph generator is told
TYRAX --dump-graph <projectDir> <object> [scene]
TYRAX --apply-graph <projectDir> <object> <g.json> [scene] [--append]
TYRAX --pad <projectDir> "<script>"  # drive the RUNNING game's pad, no focus
TYRAX --ui-script [projectDir] "<script>"  # drive the EDITOR's own UI, no focus
TYRAX <projectDir|project.tyra>      # open GUI on a project
```

VU1 microprogram work has its own layer, faster than everything below
(`docs/vu-framework.md`):

```
TYRAX --vu-check [engineDir]         # exit 0 = every program parsed AND every
                                     #   generated one matches its handwritten
                                     #   twin bit for bit, in the host simulator
TYRAX --vu-list <file.vclpp>         # expand + disassemble one microprogram
TYRAX --vu-emit <outDir>             # generate .vclpp + the EE program classes
TYRAX --vu-replay <projectDir>       # re-run a console VU1 capture on the host
```

`--vu-replay` is the only layer that checks the simulator against REAL hardware:
it reconstructs the input from `bin/vucap.bin` and diffs its own output against
the console's. `examples/vu-lab` is the fixture built for it. Two things to know
before trying it on some other project: only the LAST mesh of a chain can be
replayed (use the Debugger's flush picker to capture a flush carrying ONE mesh -
a terrain flush with fourteen chunks is not resolvable), and **a project with no
flow-graph node has no devkit layer at all** - `live_debug.gen.cpp` compiles to an
empty TU and the capture button does nothing, which looks like a broken feature
and is the zero-cost rule working.

**Run `--vu-check` after ANY change under `vendor/tyra/.../*.vclpp` or to
`src/vugen.cpp`.** It is the closest thing this repo has to a unit test: it
parses all 25 shipped microprograms, executes the described ones against their
handwritten originals on randomized input, and diffs every quadword of the GIF
packet the GS would receive. Milliseconds, no Docker, no PCSX2. It does not
replace the e2e pass - it models no cycle timing and no MAC/STATUS flags, and no
generated microcode has been built for hardware yet - but a program that fails
here will not work on the console either.

- `--new` scaffolds a complete game project (all generated sources, Makefile,
  docker-compose.yml) **without Docker** — instant way to get a fixture. `fpp` seeds a
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
- **Format versioning** (see `docs/format-versioning.md`): `--build`,
  `--resave`, `--refresh-gen`, `--apply-graph` and `--ai-graph` refuse a project
  whose `formatVersion` has pending REGISTERED migration steps (exit 1) —
  `--migrate` is the explicit tool (backs up `.tyra` + `objects/` + heights +
  splat + flow-nodes/screen-effects into `_backup/`, applies the steps, resaves
  the same file set as `--resave`; degrades to a plain resave when current). To
  test a version gate, hand-edit `"formatVersion"` in the manifest: a value
  above `version::kFormatVersion` must be refused by every path, a lower one
  opens silently unless a step is registered in `migrations.cpp`. With no step
  registered the prompt/backup path is unreachable, so exercising it means
  registering a temporary throwaway step. Two things a throwaway step is the only
  way to reach, and both are worth re-checking whenever the persisted file set
  grows: that the `_backup/` copy holds **every** file the post-migration save
  rewrites (drop a sentinel `terrain-<scene>.splat` in first — the save deletes it
  when the scene has no layers, so the backup is the only copy), and that a step
  returning `false` leaves every file **md5-identical**. `migrations::validate()`
  catches a mis-registered step (out of order, duplicate, out of range): `all()`
  prints `BROKEN MIGRATION REGISTRY` on stderr at the first command that consults
  it, and `run` aborts with disk untouched. **A step whose `kFormatVersion` bump
  you forgot produces NO steps to run** — that stderr line is the only thing
  between you and an afternoon of "my migration does nothing", so read it.
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

**Menu stylesheets have two cheap gates, and both found real bugs** (see
docs/menu-styles.md):

- **A harness over `menustyle.cpp` + `menulayout.cpp` alone** - neither needs GL,
  ImGui, `project.cpp` or a font, so it links in seconds:

  ```bash
  g++ -std=gnu++20 -I src -I vendor/stb -o h harness.cpp \
      src/menustyle.cpp src/menulayout.cpp && ./h
  ```

  Assert the cascade (element < class < state, menu scope wins), that
  `write(parse(t))` is STABLE (the Style tab saves through it, so an unstable
  writer rewrites people's files behind their backs), that a broken declaration
  does not eat the good one next to it, and the layout numbers. It caught a
  `content: "{{cross}} OK"` truncated at the first `}` (which also swallowed the
  next declaration) and a `row:disabled` cell baked for every row instead of only
  the gateable ones - 71% of the texture heap for one 6-row menu instead of 13%.
- **The pixel-identity gate for the Classic look.** A menu with no stylesheet
  must bake byte-identically to the pre-stylesheet baker. Build the previous
  editor in a worktree, `--refresh-gen` the same example copies with both, and
  diff `res/menus/*.png`:

  ```bash
  git worktree add /tmp/base HEAD && (cd /tmp/base && ./build.sh --dev)
  /tmp/base/build-dev/tyrax-editor --refresh-gen A/showcase
  ./build-dev/tyrax-editor        --refresh-gen B/showcase
  # compare A/*/res/menus/*.png with B/*/res/menus/*.png (PIL, 3 lines)
  ```

  It found a double-composited background (every translucent panel came out
  (7,10,21) instead of (8,12,24)) and a save menu that lost its "X SAVE O LOAD"
  hint line - neither visible without a diff.
- **Checking that something SCROLLS: never use a pattern aligned with the
  scroll.** A menu's animated background was measured as frozen through three
  rebuilds - correlating two frames found a shift of exactly 0 - because the test
  tile was diagonal stripes and the scroll vector ran along them. Vertical bars
  plus a pure horizontal scroll showed 53 px in one second, matching the declared
  speed. The emulator has a second trap in the same area: **PCSX2 unfocused does
  not take injected keys**, so click into its window before driving it, and read
  its status bar (FPS/VPS/Speed) to know the machine is actually running before
  concluding anything about motion.

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
up -d` (container `<name>-compiler-1`, straight from the stock image) → engine
sources checksum-synced into the shared volume, `libtyra` rebuilt if changed
(VU1 microprograms only when a VU source changed) → project rsynced → `make -j`
→ WAV sfx converted with `adpenc` → `bin/` synced back → existing PCSX2
processes killed → `HostFs = true` forced in PCSX2.ini → PCSX2 launched on the
ELF.

Notes:
- First-ever build downloads the `h4570/tyra` image and compiles the engine
  (minutes). Subsequent builds take seconds unless the engine changed.
- **The whole pipeline is incremental, so measure a build by what it
  RECOMPILED, not by the clock.** `grep -c 'elf-g++ .* -c -o'` over the build
  log is the number that means something: on `examples/showcase` (18 TUs, 6
  cores) a build with nothing changed is **0 compiles / ~5 s**, one edited game
  source is **1 / ~9 s**, a moved scene object is **13 / ~47 s** (the scene
  table is in a header most TUs include), an engine `.cpp` is **~7 s**, an
  engine `.vclpp` is **~2 min** (the microprograms), and `--rebuild` is
  **~5 min** from nothing. A "no changes" build that still compiles things is a
  regression with a specific cause — some step wrote a file the compiler reads
  (see the `runner.cpp` row in tyra-editor-dev); find it by comparing mtimes in
  the container against `/src/obj/*.d`.
- **`--rebuild` is the escape hatch** and the control when you suspect the
  incremental logic itself: it recreates the container and rebuilds the engine
  and the game from source. `Clean` is the other hammer — it also wipes the
  host's `bin/`.
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
  **Two Windows-only ways this looks broken when it is not** (both cost an
  afternoon on 2026-08-03, both diagnosed):
  - **`Start-Process -ArgumentList` does not quote its array elements**, so
    `-ArgumentList "--pad",$P,"wait 1; stick r 110 0; neutral"` reaches the
    editor as loose argv words, the script it parses is the single word `wait`,
    and it dies with `error: line 1: wait needs seconds` — on the **stderr of a
    hidden process**, where nobody ever sees it. The game then does exactly
    nothing, which reads as a dead pad channel. Put the whole `--pad` call in a
    `.ps1` with the script as a LITERAL and `Start-Process powershell -File`
    that (a `param()` does not help: the mangling happens at the outer boundary
    too), or add the quotes to the array elements yourself. Same trap for any
    `;`-separated script argument, `--ui-script` included.
  - **The atomic replace of `livepad.bin` races the reader** (fixed since; the
    symptom is worth recognising because a stale editor binary still has it).
    `livepad::write` renames a `.tmp` over the target, and the running game
    re-opens that same file about every frame through PCSX2's HostFs — Windows
    refuses to rename over or delete a file another process has open, so a
    replace comes back `Access is denied` roughly **once per 200 writes** at the
    driver's 40 Hz refresh (zero denials with the emulator stopped; Linux is
    immune, `rename(2)` over an open file is legal there). The driver used to
    abort on the FIRST failed write, so a long hold died with nothing but
    `error: cannot replace ...livepad.bin: Permission denied` and exit 1 — one 9 s
    script in five, measured, and every single one when the file was contended
    harder. `write` now retries (5 tries, 4 ms apart — absorbs a 60 ms exclusive
    hold) and only a **step** write is fatal: a lost *refresh* is a warning, and
    the run says so in its summary (`pad released (27 refresh write(s) lost to
    the reader)`). So on Windows: **read the driver's stderr and its exit code**,
    and treat "no motion, exit 1" as this rather than as a dead channel.
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
+
### Foot IK regression trace

Use the cross-platform runner when Foot IK movement is too subtle for a single
screenshot. It enables generator-only telemetry, builds and boots the project,
drives a deterministic Remote Pad script, captures a dense contact sheet, and
writes the physical/visual player root plus both feet to CSV:

```powershell
python .agents/skills/tyra-testing/scripts/foot-ik-regression.py `
    C:\Temp\foot-ik-fixture `
    --editor build\tyrax-editor.exe `
    --output C:\Temp\foot-ik-results `
    --pad-script "stick l 127 0; wait 8; neutral; wait 1"
```

The result directory contains `player-root.csv`, `feet.csv`,
`training-frames.csv`, `training-samples.csv`, `summary.json`, and
`frames/contact-sheet.png`. The summary reports the largest one-frame
physical and visual root steps, their ratio, contact transitions, and the
largest change in weighted foot correction. Neural-enabled fixtures also
report the maximum learned contact confidence and the count of frames where
the raycast-gated landing residual was actually accepted.
Surface-following builds additionally report maximum filtered swing clearance,
minimum support-normal Y and the number of frames with ankle alignment weight;
use a stair route for clearance and a sculpted-terrain route for the normal.
Contact-policy diagnostics count fast contact transitions, moving-gait replants
within six frames (the intentional idle-to-walk handoff is excluded) and
frames where the stair sweep requested clearance without recording a raised
support. A clean route should report zero for all three; these counters catch
the visible glue/pop cycle even when its eased ankle correction stays small.
Downstairs routes additionally report down-reach frames, the maximum reach
weight/gap, and how many real contacts followed a reach within 12 frames. This
distinguishes useful unlocked extension from a contact that merely happened
after the player root had already dropped.
`training-frames.csv` is the raw per-foot `FOOTTRAIN` sequence.
`training-samples.csv` uses the exact 16 normalized runtime features and labels
each unlocked frame from the next verified contact (at most 18 frames ahead),
plus the procedural clearance/release decision. Planted frames are excluded
unless they contain an emergency stair release. Combine several directions and
surface types into `tools/data/foot-neural-real.csv`, then run
`python tools/train-foot-neural.py`; its fixed seed makes committed weights
reproducible.
For the standard four-direction torture course and one merged training file,
run `foot-ik-gauntlet.py` with the same project/editor arguments. It boots clean
forward/lateral/diagonal/reverse runs and aggregates sweep clearance, planner
acceptance, correction jerk, target penetration, support shift, body tilt and
VU0/EE parity in `gauntlet-summary.json`.
Add `--neural-ee`
to regenerate the same project with `TYRAX_FOOT_NEURAL_EE=1`: dense products
then use the scalar EE twin instead of VU0 macro mode. Run identical pad scripts
for both variants and compare `feet.csv`. More importantly, every traced VU0
inference also evaluates the scalar twin on the **same feature vector** and the
summary reports `neural_vu0_ee_max_delta`; this is the deterministic arithmetic
parity check that two separately timed PCSX2 walks cannot provide. The
generated game logs
`PLAYERIK` / `FOOTIK` / `FOOTTRAIN` rows only when the runner builds with
`TYRAX_FOOT_IK_TRACE=1`; normal editor builds replace the trace constant with
`false`, so the compiler removes the logging and its sample bookkeeping.
Do not set the environment variable for performance captures. A user-owned
`terrain_game.cpp` cannot receive new generator instrumentation; use an
editor-owned scratch copy for this test.


- **emulog.txt**: success looks like `ELF <path> is executing`; failure signals
  are `Assertion` lines or an early exit. **`TYRA_LOG`/EE printf does NOT land
  in emulog** even with EnableEEConsole=true. The reliable failure signal is the
  game's own **`bin/log.txt`** (host: fs) — TYRA_LOG output plus any assertion
  dump, bracketed by `======= TYRA =======` … `================`. A failed
  assert no longer paints the screen (the engine halts quietly, leaving the last
  frame up — see tyra-engine-dev), so grep `bin/log.txt` for the banner rather
  than screenshotting for assert text; the running editor also pops that dump in
  a copyable dialog. (A screenshot still shows *where* the game froze.)
  **On a ps2link deploy there is no `bin/log.txt` at all** — the generated
  `main.cpp` logs to the EE console there instead (a host: write per line is a
  network round trip), and ps2link forwards it to `ps2client`. So the game's log
  is the `[ps2] …` lines of the runner output: the Output panel in the GUI, plain
  stdout from `--build <dir> --run-ps2 <ip>`, and the Debug window falls back to
  the same stream. A game built before 2026-08-06 logs NOTHING over ps2link (the
  EE's stdout was buffered and never flushed) — rebuild before believing silence.
- **Screenshots**: PCSX2's F8 via SendKeys is flaky. On Windows use the bundled
  script — a GDI capture that works reliably:

  ```powershell
  powershell -File .claude/skills/tyra-testing/scripts/screenshot-window.ps1 `
      -ProcessName pcsx2-qt -OutFile <scratchpad>\shot.png
  ```

  It also works for the editor itself (`-ProcessName tyrax-editor`) — useful for
  verifying viewport rendering without a human.

  **To WATCH the game over time, use `-Watch DIR`** — the Windows twin of
  `wayland-control.py watch` (below), with the same flag names, the same output
  lines and the same diff metric, so the numbers mean the same thing on both
  OSes. It samples the window on an interval, keeps the full-resolution frames on
  disk as `DIR\frameNN.png` and reports **one downscaled contact sheet** plus a
  changed-pixel table, so a whole drive costs about as much context as a single
  screenshot instead of one read per moment:

  ```powershell
  powershell -File .claude\skills\tyra-testing\scripts\screenshot-window.ps1 `
      -ProcessName pcsx2-qt -Watch <scratchpad>\w -Auto -Trim -Every 0.9 -Count 10 -Tile 224
  ```

  Measured on the fpp fixture (1066x705 window, 831x623 picture): 10 tiles at
  224 px = a 926x576 sheet, **~711 tokens** where the ten full frames would be
  ~6 900. A frame costs ~0.1 s of work on top of the interval, so a 0.9 s
  interval is held to the millisecond. One run put the whole drive in one image:
  four idle tiles at **0.000%**, then 41.9% / 29.1% / 22.5% / 6.9% while a turn
  and a walk were held, then 0.000% again after the release — the same
  idle/drive/release shape as the pixel-counting recipe below, in a single read.

  What differs from the Wayland version, and the traps:
  - **`-Auto` is deterministic here.** Windows HAS per-window capture, so it
    takes the biggest visible CHILD window (PCSX2's render surface is a native
    child HWND) instead of hunting for motion — it works on a paused emulator, a
    parked camera or a static menu, every one of which defeats the Linux
    heuristic. Add **`-Trim`** and the black pillar bars go with it: a 1050x623
    widget came out **831x623**, exactly 4:3, the PS2 frame and nothing else.
    That crop is also what makes an idle frame measure **0 px** instead of the
    status-bar noise the whole window carries (87 px in one second here) — a
    clean instrument, so any non-zero row means the game.
  - **A GDI grab reads the SCREEN, so an occluded window captures whatever is on
    top of it** — silently, and for every frame of a long run. This bit during
    development: a plain `SetForegroundWindow` from a background shell does
    nothing at all (the same trap as synthetic input above), and the "PCSX2
    screenshot" came back as another application's window. The script now raises
    the window with the ALT-tap + `AttachThreadInput` trick, VERIFIES
    `GetForegroundWindow` and **warns** when it still failed — if you see that
    warning the frames are worthless, so uncover the window, or pass
    `-NoActivate` when it is already clear and you do not want the focus stolen.
  - `-Area X,Y,W,H` is **window-relative** — the coordinates you read straight
    off a capture — and it is deliberately NOT cached the way Linux caches
    `area.txt`: the window may have moved since the last run, and its geometry is
    free to ask for. The same geometry flags work for a single `-OutFile` shot.
  - `-OnlyChanged PCT`, `-IdleStop K` / `-IdleBelow`, `-Every`, `-Count` or
    `-For S`, `-Tile`, `-Cols`, `-Sheet`, `-NoFrames` do exactly what their
    `--only-changed` … counterparts below do, including the saturation trap:
    calibrate `-OnlyChanged` against a first run's column instead of guessing
    (verified at 5%, which kept 6 of 11 frames and marked the rest `-`).
    `-IdleStop 2` verified on a static window: it stopped on the second
    consecutive 0.000% frame.
  - It only CAPTURES, so it composes with whatever drives the game. A background
    `--pad` is the usual partner — one `-Watch` over a 1.5 s right-stick hold
    read **17.6 / 27.7 / 21.5%** during the turn and **0.000%** after the
    release — but mind the two Windows traps in the `--pad` bullet above (the
    unquoted `Start-Process` script, and the replace race that used to abort long
    holds); both make a working channel look dead. PCSX2's own held keys are the
    fallback, and this ini binds the left stick to **Z/Q/S/D**, not the W/A/S/D
    the section above quotes. Held keys need the window in front, which `-Watch`
    arranges anyway.

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

  **To WATCH the game over time, use `watch`** — the same one-session trick
  applied to TIME. It samples the screen on an interval off the
  already-negotiated stream and reports **one downscaled contact sheet** plus a
  changed-pixel table, so a minute of gameplay costs about as much context as a
  single screenshot instead of thirty:

  ```bash
  python3 .claude/skills/tyra-testing/scripts/wayland-control.py \
      watch <scratchpad>/w --auto --aspect 4:3 --every 1 --for 20 --tile 224
  ```

  Measured on the endless-runner fixture (1920x984 screen, 1157x868 render
  area): 9 tiles at 224 px = a 696x576 sheet, **~534 tokens** where the nine
  full-resolution frames would be ~12 000, and the run costs ~1.2 s of wall
  clock per frame on top of the interval. The full-resolution crops stay on disk
  as `frameNN.png`, so the sheet is what you READ and a single interesting
  index is what you open afterwards — that split is the whole point.

  What the flags are for, and the traps:
  - **`--auto` finds the render area by MOTION** (there is no per-window
    capture, see above): it diffs four samples ~0.5 s apart and takes the widest
    and tallest contiguous band of moving columns/rows, so PCSX2's FPS readout
    loses to the picture. It reports only what MOVES — a parked camera under a
    static sky yielded `415,425,1157,536`, the ground half of the frame — so
    add **`--aspect 4:3`** to grow it back to the PS2 picture from its bottom
    edge (`415,80,1157,868` against a true `415,93,1157,868`: 13 px of menu bar
    in the tile, harmless). The rect is cached in `DIR/area.txt` and reused, so
    detection is a once-per-session cost.
  - **A still scene defeats motion detection**, and that failure had to be made
    loud: on the day-night fixture 1.5 s of a slow lighting gradient moved
    **500 pixels of 1.9 M**, and the winner was the `Speed: 64%` text — an 8x10
    box. `--auto` now refuses anything under 2% of the screen or outside a
    0.9-2.6 aspect and tells you to read the rect off one `shot` and pass
    `--area X,Y,W,H` (which is also cached). `--trim` shaves black letterbox
    borders off whatever rect you give.
  - **`--only-changed PCT`** drops frames that changed less than PCT **since the
    last KEPT frame** (they still appear in the table, marked `-`), which is how
    a long watch stays cheap. Beware that the metric SATURATES on a repeating
    scene, exactly like the axis-aligned-walk trap above: the scrolling
    checkerboard terrain never exceeded 25% between frames however long the gap,
    so `--only-changed 25` kept a single tile. Calibrate it against the
    frame-to-frame numbers of a first run, don't guess.
  - **`--idle-stop K`** ends the run after K consecutive frames under
    `--idle-below` (0.05% by default) — "watch until it settles" without a
    fixed count. Verified against a pure-black crop: three 0.000% frames and it
    stopped.
  - `--every S` / `--count N` or `--for S`, `--tile W` (tile width, the only
    real knob on context cost), `--cols N`, `--sheet NAME`, `--no-frames`.
    `wayland-control.py area` prints the detected rect and exits.

  It composes with the Remote Pad: run `--pad` in the background and `watch` in
  the foreground (or the reverse) and one sheet shows the whole drive — turn,
  walk, release — with the diff column as the numeric evidence the input
  arrived, which is the measured-not-eyeballed rule below applied to a sequence.

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
  `...`) — **quoted, with either `"` or `'`** (so on PowerShell wrap the whole
  script in one kind and use the other inside; an unquoted two-word target
  arrives as two tokens and fails). Quoting also makes the target OPAQUE, which
  is the part worth remembering: a `#` or `;` inside quotes is an ordinary
  character, so a name straight out of `dump` — ImGui child regions are
  registered as `Project/##objects_DC0BCE04`, `#` and all — can be pasted into a
  script verbatim, and a window name containing `/` resolves because every split
  point is tried. `shot` writes the same self-captured framebuffer as `TYRAX_SHOT`, and
  what it CANNOT name is anything not made of ImGui widgets - the 3D viewport
  (one big item: `drag` inside it, or work through the Project panel's list), the
  imnodes flow canvas and the ImGuizmo gizmo. Not all modals close on `escape` -
  click their `Cancel`; `dump` shows it. **A rect in `dump` is not a promise the
  click will land**: a window taller than the room it got still submits the items
  past its bottom edge, so they are listed with rects OUTSIDE the window, and
  `click` on one presses over nothing and still reports success - which reads as
  a broken feature when the code is fine. Compare the item's y against its
  window's rect (both are in the same `dump`) and **pair every state-changing
  click with `expect-checked` / `expect-unchecked`**, which turns a silent no-op
  into a failed run.

  **Combos and tabs ARE nameable now** (they were not, and both were silent
  gaps): `BeginCombo` never calls ImGui's item-info hook at all, and `TabItemEx`
  calls it BEFORE the box exists, so every combo and every tab in the editor
  dumped as `-`. `uiscript.cpp` closes both - a label reported ahead of its box is
  held until the box arrives, and a target that matches nothing by label is
  matched by RE-HASHING it the way ImGui would (`ImHashStr(label, 0,
  window->ID)`), which is what finds a widget whose label ImGui never reported.
  So `click 'Preview in'` and `click Style` work, and a combo's OPTION can be
  clicked by its own text once the dropdown is open. Two limits remain: the hash
  path needs the EXACT label (a hash has no prefixes) and only reaches widgets
  submitted at a window's own scope. **A widget whose whole label is
  hidden behind `##`** - a compact search field (`##assetsearch`,
  `##objsearch`) - still has nothing to name and dumps as `-` with a rect and
  nothing to name, so a filter row like the Scene panel's is only reachable by
  CLICKING ITS RECT: read the rect off `dump`, add the editor window's screen
  offset (compare one `dump` rect against one full-screen `shot` to get it -
  ~67,70 on this box) and drive it with `wayland-control.py click --at x,y` /
  `type`. Everything else about the run stays the same, and the assertion is
  the `shot` you crop afterwards.

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
  cover — the surrounding UI and anything dragged by hand — say so in the
  commit and leave it for a human.

  **A screen effect that cannot be SEEN and one that is not happening look
  identical** (retired PROGRESS entry 231). A Camera Shake at amplitude 0.05 measured **0
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
  `build.sh`/`build.ps1` — see docs/ps2link-setup.md).

  **ps2link is no longer hardware-only: it runs in PCSX2.** A SECOND, portable
  copy of the emulator (`-portable`, its own `inis/bios/logs`, so it cannot
  disturb a parallel session or the editor's own launches) with **DEV9 bridged**
  onto the LAN boots `ps2link.elf` and answers the real `ps2client` — same
  subnet, same ports, `reset` and `execee` included. A wedge then costs
  `Stop-Process` instead of a walk to the console, which is what makes a
  Run → Stop → Run A/B a two-minute scripted test: measured 2026-07-31, r4 falls
  through to the BIOS browser and drops off the LAN where r6 comes back and runs
  the payload again. The full recipe, the four gotchas (`[DEV9/Eth]` is the ini
  section, `EthDevice` is the bare adapter GUID, `host:` is served by PCSX2's
  HostFs relative to the `-elf` directory and NOT by ps2client, ping proves
  nothing in either direction) and the limits are in
  **docs/ps2link-setup.md — "Testing a change without a console"**. The limit
  that matters: **the emulator cannot produce an EE exception** — replayed the
  r4 `BadAddr 8` crash there and the `printf` returned normally, because main
  RAM starts at address 0, so a NULL dereference is an ordinary load. Faults,
  `crash.txt` and the crash handler stay hardware tests; sequencing, restarts,
  IOP reboots and the protocol do not.

  **ps2link is not entirely untestable any more.** `tools/ps2link/test/run.ps1`
  (`run.sh`) compiles the REAL patched `build/iop/net_fio.c` against stub
  IOP/lwip headers in `test/shim/` and drives it with a scripted fake socket —
  framing, EOF, short reads, buffer clamps. What makes it evidence rather than
  decoration is the `-Pristine` / `--pristine` mode: the same tests against the
  untouched upstream file, which must FAIL (it does, 7 of 18, three of them by
  spinning past a 2000-call `recv()` ceiling). Use this layer for any change to
  the `host:` protocol code before reaching for hardware; threads, the SIF and
  the GS are still console-only. Note the `#include "net_fio.c"` trick — the
  harness pulls the .c in so it can place `pko_fileio_sock`, which is static.

  Since r2 of the patch, several "the console is hung, hit Reset" failures are
  gone: a closed `ps2client` socket used to spin the IOP `host:` thread forever
  while holding its semaphore, an unexpected reply desynced the stream for good,
  and `pkoReset()` waited on an inverted `SifIopSync()` on every deploy. **r3**
  adds `spu2Silence()`, so the SPU2 no longer drones the dead game's voices
  through a reset — which means **"Stop on PS2" no longer deploys
  `tools/silencer/silencer.elf` and no longer sleeps ~7 s**; if you are timing
  Stop, that is why it got faster. The tool stays as a manual fallback for a
  pre-r3 console. Check the boot banner reads `TyraX ps2link r3` and the log has
  `SPU2 silenced` before blaming anything else. The
  `dumpmem`/`scrdump` uselessness above is NOT explained by any of that and is
  still open — r2 only stops the failure leaking a `host:` fd each attempt. (A
  tempting theory, recorded so it is not re-tried: that the EE side passes
  newlib `O_*` values where the wire wants `FIO_O_*` (`O_RDONLY` = 0, but
  `FIO_O_RDONLY` = 1). It is wrong — the engine's asset loads use plain
  `fopen("rb")` through the same path and work fine, so newlib values are what
  this `host:` expects.)

  (Verified: a session
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
- **The log panels' severity split** (docs/log-panels.md): `logview.cpp` is a
  pure function of text - no ImGui, no `Project` - so which bucket a line lands
  in is checkable from a 40-line harness (`g++ -std=c++20 -Isrc harness.cpp
  src/logview.cpp`) fed a realistic build log, no editor needed. Assert TWO
  things there, because both broke while it was written: that the counts are per
  ENTRY (a gcc error plus its snippet is one error, not four), and that parsing
  the log **one line at a time** through `parse(log, from, state, out)` +
  `appendPartial` gives byte-identical results to one-shot parsing - the panels
  classify incrementally while a build streams, and the carried continuation
  state is what a chunk boundary breaks. The panel itself is then a `--ui-script`
  job (`click 'Output/3 errors'`, `expect-unchecked 'Output/Select text'`), with
  one catch: the **Debug** window is a dock TAB behind Output in every built-in
  layout, and a docked tab is an unnamed item, so no script can select it -
  verify it by temporarily pointing that layout's `pendingFocusWindow_` at
  `"Debug"`, screenshotting, and reverting.
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
  problem) - say so in the commit message, that's the established convention.

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

The cheap version of the same run is one `-Watch` (above) over the whole drive:
it counts the pixels for you, per frame, and hands back one sheet instead of
five images — the four bullets below still apply, they are just columns then.

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

## Verifying the AI Assistant (docs/ai-chat.md)

An AI feature looks untestable and is not: three of its four layers need no
backend at all, and the fourth needs no tokens.

**0. Is the documentation searchable?** `TYRAX --search-docs "<query>" [page]` runs
the assistant's `search_docs` over the pages baked into the exe and exits 1 when
nothing matches - so a doc-facing change ("does anything still say
`--max-turns`?") is one command, no project and no backend.

**1. What is it told?** `TYRAX --chat-prompt [projectDir]` prints the whole system
prompt - the tool catalog built from `aichat::tools()`, the documentation index
derived from `docs/*.md`, the object type/property tables and the live project
context. This is the `--list-nodes` trick applied to the chat, and it is the
check for anything prompt-shaped ("is my new tool described?", "did that doc page
join the index?", "how big is the prompt?"). With no project argument you get the
no-project-open variant.

**2. The pure half, from a harness.** `aichat.cpp` has no ImGui, no GL and no
`App`, so the doc index, the reply parser, argument validation, every read tool
and the transcript trimming can be exercised from a host `main()`. The cheapest
way to link one is to reuse the objects a dev build already produced:

```bash
OBJS=$(ls build-dev/CMakeFiles/tyrax-editor.dir/src/*.o | grep -v /main.cpp.o)
g++ -std=c++20 -O0 -I src -I build-dev/generated -I vendor/imgui -I vendor/imguizmo/src \
    -I vendor/imnodes -I vendor/stb -I vendor/ufbx -I vendor/miniaudio \
    harness.cpp $OBJS build-dev/CMakeFiles/tyrax-editor.dir/vendor/*/*.o \
    build-dev/CMakeFiles/tyrax-editor.dir/vendor/ufbx/ufbx.c.o \
    build-dev/libimgui.a build-dev/vendor/glfw/src/libglfw3.a \
    -ldl -lrt -lm -lGLX -lOpenGL -lpthread -o harness
```

(Everything but `main.cpp.o`, plus a harness with its own `main()`. The GUI code
comes along unused - it costs a link, not a display.) The same recipe works for
any host-only module that a plain `g++ aichat.cpp` cannot link on its own because
it reaches `project.cpp`.

**3. The whole loop, with a FAKE backend and no tokens.** The backends are
external commands found on `PATH`, so a shell script called `claude` earlier on
`PATH` IS a backend. Have it drain stdin, count its invocations in a state file
and answer from a canned sequence, and the multi-step tool loop becomes
deterministic and free:

```sh
#!/bin/sh
cat > /dev/null                                     # the prompt arrives on stdin
n=$(cat "$STATE" 2>/dev/null || echo 0); n=$((n+1)); echo "$n" > "$STATE"
case "$n" in
1) echo '{ "say": "Placing it.", "calls": [ { "tool": "add_object", "args": { "type": "box", "name": "lever", "position": [4, 0, -6] } } ] }' ;;
*) echo '{ "say": "Done." }' ;;
esac
```

Drive the window with `--ui-script` (no focus needed, see above) and check the
RESULT in the project rather than on screen - that is what makes it a test:

```bash
STATE=/tmp/fake PATH=/tmp/fakebin:$PATH TYRAX --ui-script <proj> "
frames 20; click Tools; click 'AI Assistant'
click 'AI Assistant/Allow project edits'      # see the trap below
click 'AI Assistant/##chatinput'; text 'add a lever'; click 'AI Assistant/Send'
wait 12; key ctrl+s; shot /tmp/chat.png"
TYRAX --dump <proj>                            # is the lever there?
TYRAX --refresh-gen <proj>                     # did its graph compile?
```

A fake can also drive paths a real backend reaches only after a long, expensive
session: have it answer with a HUGE tool result (`read_doc` of the biggest page)
to push the conversation over its budget, and have it recognise the compaction
request by its prompt (`grep -q "You are compacting a conversation"`) so the whole
compact-then-continue loop runs deterministically. That is how the context meter
and compaction were checked - 63.6k of context down to 5.8k, verified in the saved
chat file (`role: ['summary', 'user', 'assistant']`) rather than by eye.

The same trick reaches the tools that are expensive or destructive: a canned
`build_game` proves the park-and-resume (the turn stops, the Runner runs for real,
the outcome is appended to the tool result already in the transcript and the loop
continues - measured end to end with one Docker build), and a canned `set_section`
that sends an EMPTY list proves the shrink guard refuses it, then that
`confirm_replace` gets through. Check the destructive ones against the SAVED
project (`--dump`, the `objects/*.json`, the manifest) rather than the chat: the
tool result says what it did, the file says what happened.

Worth building canned replies that MISBEHAVE, because that is the half a real
backend will not reproduce on demand: a fenced envelope with prose around it, a
`name`/`arguments` spelling instead of `tool`/`args`, an invented node type, an
unknown property, a wrong object name. All of those have honest paths and the
transcript's tool rows say which fired.

**Two traps specific to testing anything that RUNS the game from a chat.** A
hermetic `XDG_CONFIG_HOME` isolates PCSX2 as well - its BIOS, memory cards and
settings live under `$XDG_CONFIG_HOME/PCSX2`, so the emulator either fails to
start or comes up unconfigured and the run looks like a broken feature. Symlink
the real one into the scratch config (`ln -sfn ~/.config/PCSX2 $CFG/PCSX2`) and
keep the editor isolated. And remember that **a build compiles the IN-MEMORY
model**: an assistant that added an object and built it produced a correct ELF
while the `.tyra` on disk still knew nothing about it, so the NEXT session's build
regenerated an empty debug runtime and no channel ever appeared - which reads
exactly like a broken devkit. A multi-session test has to save.

**Four traps this cost.** Set `XDG_CONFIG_HOME` (or `LOCALAPPDATA`) to a scratch
directory for any run that touches the chat: history files and `editor.ini` both
live there, so a hermetic run neither pollutes the machine's config nor inherits
whatever the last run left switched. **A popup's items live in a window of their
own** (`##Popup_<hash>`), so `dump` lists them under that name and not under the
window that opened it - a `dump | grep` that stops at the first screenful will
miss them and read as "the popup never opened"; grep for the LABEL, and confirm
with `shot` before believing a popup is broken (that cost an hour here, and the
popup was working the whole time). And uiscript's `text` needs the widget to have
processed the keystrokes before the button that consumes them is clicked -
`ImGui::BeginDisabled` on an empty field means a Send clicked too early is a
no-op that still reports success, so put a `frames 5` between typing and
clicking. `chatAllowEdits_` is persisted in `editor.ini`, so a run
that toggled it leaves it that way for the NEXT run - a scripted click on the
checkbox is a toggle, not an assignment, so assert with `expect-checked` right
after (a whole loop reading `[failed]` on every edit is that switch, not a bug).
And the reply is consumed by `aiChatTick`, which runs from `drawUI` whether or not
the window is open - so a `wait` long enough for N backend invocations is what a
multi-step turn needs, and closing the window mid-turn does not strand it.

## Choosing the right depth

| Change | Minimum honest verification |
|---|---|
| Editor UI (a panel, a dialog, a toggle) | Layer 0 + a `--ui-script` run that opens it, does the thing and ASSERTS it (`expect`/`expect-checked`), plus a `shot` to look at. No focus, no coordinates, either OS |
| Editor viewport (rendering) | Layer 0 + a screenshot of the affected panel (`shot` from a UI script, `TYRAX_SHOT` on a timer, or `screenshot-window.ps1`/`wayland-control.py` from outside) - and measure the pixels rather than eyeballing |
| Serialization (`.tyra`) | Layer 1 `--new` + reopen; round-trip save/load diff |
| Codegen / templates | Layer 2 grep or harness, then one Layer 3 boot |
| Engine (`vendor/tyra`) | Layer 3 always — compile happens only in Docker; SW-renderer screenshot for anything visual |
| Audio | Layer 3 + peak-meter check |
| Anything a player DOES (buttons, walking, menus, two players) | Layer 3 + `--pad` (see the recipe above) — an idle control shot, then drive, then measure. No human, either OS; `watch` (Linux) / `-Watch` (Windows) collapses the whole drive into one contact sheet |
| ISO export | Export + mount the ISO on the host + boot it in PCSX2 |
