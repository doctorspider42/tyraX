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
./build.ps1          # → build/tyra-editor.exe (auto-clones vendor deps on first run)
./build.ps1 -Run     # build + launch the GUI
./build.ps1 -Clean   # full rebuild
```

Needs `scoop install mingw cmake ninja`. This is also the compile check for
everything under `src/` — warnings matter, the build is expected to be clean.

## Layer 1 — headless CLI (no GUI needed)

```powershell
build\tyra-editor.exe --new <name> <parentDir> [width] [depth] [orbit|fpp|showcase]
build\tyra-editor.exe --build <projectDir> [--run]   # exit code 0 = success
build\tyra-editor.exe <projectDir|solution.tyra>     # open GUI on a project
```

- `--new` scaffolds a complete game project (all generated sources, Makefile,
  Dockerfile) **without Docker** — instant way to get a fixture. Use the
  `showcase` template when you need every feature present in one scene.
- `--build` streams the whole Docker build log to stdout and returns a real
  exit code — the backbone of scripted e2e runs.
- Create scratch projects in the session scratchpad directory, not in the repo.

## Layer 2 — codegen checks without Docker

Most features live or die in the generated code, and you can inspect it
without building:

- `--new` writes every generated file; grep them for your new constants/logic.
- For an **existing** project, `project::refreshGenerated()` runs at the very
  start of `--build`, *before* Docker is contacted — so even with Docker
  stopped, a failed `--build` still refreshes `inc/scene_data.hpp`,
  `src/scripts/flow_graph.gen.cpp`, etc. for inspection. There is no
  `--no-docker` flag; the expected outcome is "Failed to start docker
  container..." + exit code 1 with fresh generated files on disk.
- When inspecting, remember the ownership split (see tyra-editor-dev): `.gen.*`
  files and `scene_data.hpp` are always rewritten — trust them after a refresh;
  `terrain_game.cpp` / `controls.hpp` / `script.hpp` regenerate only while their
  marker line is intact, so a stale-looking file there may be user-owned by
  design, not a codegen bug.
- To test a specific graph/scene shape, edit the project's `project.json`
  directly (it is the source of truth; the editor tolerates external edits and
  discards stale undo history), then refresh and inspect.
- `samples/script-demo/` is a checked-in generated project — a useful diff
  baseline, but only as fresh as its last regeneration. If codegen changed
  since, regenerate the sample first (its files drift silently); don't treat a
  stale copy as ground truth.

Past features were verified with throwaway "codegen harnesses" — a scratch
`main()` that builds a `Project` in code, calls `templates::generate()` and
asserts on the emitted strings. This works because `project.cpp`,
`templates.cpp` and `json.cpp` have no ImGui/GLFW dependency — they link into a
tiny host harness without the GUI. Fine pattern; keep such harnesses in the
scratchpad, not the repo.

## Layer 3 — full e2e: Docker build + PCSX2 boot

Prerequisites: Docker Desktop **running**, PCSX2 installed in
`Program Files\PCSX2` with a BIOS configured.

```powershell
build\tyra-editor.exe --build <projectDir> --run
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
  asset loading, check HostFs first.
- For ISO/cdrom0: testing use `Project > Export PS2 ISO`, then boot the ISO in
  PCSX2 (covers the path-conversion code that host: boots skip).

### Reading the results

- **emulog.txt**: success looks like `ELF <path> is executing`; failure signals
  are `Assertion` lines or an early exit. **`TYRA_LOG`/EE printf does NOT land
  in emulog** even with EnableEEConsole=true — on-screen assert text is the
  reliable failure signal, so screenshot the window.
- **Screenshots**: PCSX2's F8 via SendKeys is flaky. Use the bundled script —
  a GDI capture that works reliably:

  ```powershell
  powershell -File .claude/skills/tyra-testing/scripts/screenshot-window.ps1 `
      -ProcessName pcsx2-qt -OutFile <scratchpad>\shot.png
  ```

  It also works for the editor itself (`-ProcessName tyra-editor`) — useful for
  verifying viewport rendering without a human.
- **Rendering correctness**: switch PCSX2 to the **software renderer** before
  judging visuals — the HW renderer masks GS raster-window wrap bugs that real
  hardware shows. Give the game a few seconds to reach a steady state, then
  screenshot; compare against a known-good screenshot when hunting regressions.
- **Performance**: PCSX2's FPS overlay, software renderer, 3+ samples. Full PAL
  frame rate is 50 FPS — the generated showcase scenes hold it.
- **Audio**: EE-side logs are invisible, so use the Windows WASAPI session peak
  meter on the PCSX2 process (e.g. via `AudioMeterInformation`) — silence vs
  bursts at expected times proved music/sfx features before; a by-ear speaker
  check stays with the human.
- **Flow-graph / gameplay logic**: wire the behavior to an unattended trigger
  (`On Start`, `Every N Seconds`) so it fires without a pad; note in
  PROGRESS.md when the interactive path (pad buttons, mouse feel) still needs
  a hands-on human test — that's the established convention.

## Choosing the right depth

| Change | Minimum honest verification |
|---|---|
| Editor UI / viewport | Layer 0 + run GUI + screenshot of the affected panel |
| Serialization (project.json) | Layer 1 `--new` + reopen; round-trip save/load diff |
| Codegen / templates | Layer 2 grep or harness, then one Layer 3 boot |
| Engine (`vendor/tyra`) | Layer 3 always — compile happens only in Docker; SW-renderer screenshot for anything visual |
| Audio | Layer 3 + peak-meter check |
| ISO export | Export + mount the ISO on Windows + boot it in PCSX2 |
