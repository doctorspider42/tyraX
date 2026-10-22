---
name: tyra-testing
description: >
  How to build, run and VERIFY anything in this repo: compiling the editor
  (build.ps1 on Windows, build.sh on Linux and macOS), headless CLI project creation and
  game builds, checking code
  generation without a game build, full native or Docker-fallback e2e + PCSX2 (boot, emulog.txt,
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

There is **no committed test suite** (no CTest, no test/ dir). Verification is layered: compile → codegen inspection → PCSX2 boot → visual/log/audio checks. Use the cheapest layer that exercises your change and state in the commit/PR which layer you reached ("verified in PCSX2" vs "compiles, needs a pad test by a human").

Detail lives in `references/` — see the index at the bottom. `PROGRESS 123` / "entry 123" citations point at the retired `PROGRESS.md` (in git history; recipe in `docs/backlog.md`); new work records itself in commit messages / PR bodies.

## Ground rules

- **If a clean test needs a hook that doesn't exist, ADD IT in the same change** (a setting instead of a constant, a `--flag`, a `TYRA_LOG` line). Ask "what makes this a file edit + one command?" An A/B that depends on hand-driven input is not an A/B (e.g. the Player's rotation X is the start pitch since 1.66.0, docs/player-start.md — that made the torch fixture a JSON edit).
- **Frozen-camera fixture** (every PCSX2 A/B of a rendering change): Player `walkSpeed: 0`, `lookSpeed: 0`, framing `position`, `rotation: [pitchDownDeg, headingDeg, 0]` (X = pitch, positive = down), `"keyboardMouse": false`, `displayMode: progressive`. `--build <dir> --run`, wait ~12 s, capture. Same frame every boot, no pad.
- **Short fixture paths outside the repo** (`%TEMP%\tyra-editor-test\<name>`, `~/tyra-projects/<name>`). PCSX2's host: loader fails on ELF paths > ~145 chars (black window, no `bin/log.txt`, `TLB Miss` spam); never boot from the session scratchpad.
- **Never kill PCSX2 / tyrax-editor / ps2client by name** — parallel worktree sessions and the owner use them. Select processes by the `-elf` / project on their command line.
- **Measure, don't eyeball**: count pixels/rows with PIL, read counters from logs, keep an idle/control capture beside every "it changed".
- Judge visuals in the PCSX2 **software renderer** (`Renderer = 13`, set with PCSX2 closed). PCSX2 is correctness evidence; GS-fill and EE-cache-sensitive timings need a console.

## The layers

| layer | what | cost |
|---|---|---|
| 0 | build the editor (`build.ps1` / `build.sh`) — also the warning-clean compile check of `src/` | seconds–1.5 min |
| 1 | headless CLI: `--new`, `--refresh-gen`, bakes, `--dump`, `--vu-check`, `--vehicle-check`, `--blss-*` | seconds |
| 2 | codegen inspection without a game build (`--new`/`--refresh-gen` + grep, host harnesses) | seconds |
| 3 | native (or `--docker`) game build + PCSX2 boot, `--pad`, `--capture-frame`, logs | 5 s–5 min + boot |

### Layer 0
```powershell
./build.ps1 [-Run|-Clean|-Dev]     # → build/tyrax-editor.exe   (-Dev: -O1 → build-dev/)
```
```bash
./setup.sh --deps                  # once on a bare Linux box (editor + game deps)
./build.sh [--run|--clean|--dev]   # → build/tyrax-editor
```
Never benchmark a Dev build or verify a bake with it (bakes take minutes at -O1). MinGW-w64 GCC only (MSVC dies with C2026). Deps live only in `deps.ps1` + `deps.sh` (pinned SHAs). Paired platform files change together. → `references/editor-build.md`.

### Layer 1 — CLI cheat sheet (`TYRAX` = `build\tyrax-editor.exe` / `build/tyrax-editor`; `TYRAX --help`)

| verb | purpose / key facts |
|---|---|
| `--new <name> <parentDir> [w] [d] [empty\|fpp\|thirdperson] [unitsPerMeter] [--no-terrain]` | complete project, no Docker; defaults 100x100, 1 u = 1 m, debug profile, Live Link on, preset `empty` |
| `--build <dir> [--run \| --run-ps2 [ip]] [--rebuild] [--docker]` | full build, streams log, real exit code; runs procedural + vehicle bakes (and GI/pre-lit auto-bakes when enabled) |
| `--refresh-gen <dir>` | regenerate sources, no toolchain; runs model/anim/credits/vehicle/procedural bakes but **NOT texbake** (`.res-baked` stays stale) |
| `--resave <dir>` / `--migrate <dir>` | load+save round trip / backup + apply format migrations |
| `--bake-gi <dir> [--gpu]`, `--gi-gpu-check` | GI + probes; `giAutoBake` re-bakes STALE scenes in `--build` |
| `--bake-status <dir>` | read-only cache freshness; exit 3 = stale |
| `--bake-model-ao <dir> [--texbake]`, `--bake-prelit <dir> [scene]`, `--bake-object-light <dir> <object> [size] [rays]`, `--bake-shadows <dir>`, `--bake-particles <dir>`, `--bake-impostor <dir> <model.obj> <stem> 4\|8\|16 [--gpu]` | per-feature bakes |
| `--dump <dir>`, `--list-nodes`, `--dump-graph <dir> <obj> [scene]`, `--apply-graph <dir> <obj> <g.json> [scene] [--append]`, `--ai-graph ...`, `--add-ai-support <dir> [claude] [codex] [copilot]` | project / graph I/O, AI tooling |
| `--atlas-report <dir>`, `--batch-report <dir> [scene]`, `--road-crossings <dir> [scene]` | what atlasing/batching did and why; road crossings (exit 1 = orphaned junction override) |
| `--import-model <dir> <model> [--split none\|top\|mesh] [--pivot ..] [--keep f] [--max-texture px] [--animated] [--no-static] [--textures dir] [--name stem] [--dry-run]` | the importer, headless |
| `--vu-check [engineDir]`, `--vu-list`, `--vu-emit`, `--vu-replay <dir>`, `--dump-vucap <dir> [--full] [--peek qw[,n]]` | VU1 host simulator / capture decode |
| `--vehicle-check` | drive-model property tests, exit 0 = pass |
| `--blss-train`, `--blss-eval`, `--blss-emit`, `--blss-coverage` | neural upscaler, headless |
| `--chat-prompt [dir]`, `--search-docs "<q>" [page]` | AI Assistant prompt / doc search (exit 1 = no match) |
| `--pad <dir> "<script>"` (or `--file`/`--stdin`) | drive the RUNNING game's pad, no focus |
| `--ui-script [dir] "<script>"` (or `--file`/`--stdin`) | drive the EDITOR's UI by widget name, no focus |
| `--record <dir> <out.tyrarep> --pad ".." [--seconds s]`, `--replay <dir> <file>` (0 same / 3 diverged / 1 error), `--replay-dump <file> [a b]` | input recording as a regression test |
| `--capture-frame <dir> [-o out.png] [--alpha a.png] [--timeout s]` | the game's own screenshot (works over ps2link) |
| `--profile-frame <dir> [-o report.csv]` | synchronized render costs |
| `--debug-state [--verbose] [dir]` | what is being debugged on this machine right now |
| `--symbolize <dir> 0x...`, `--audit-release <dir>` | addr2line on `bin/<name>.elf.sym`; prove a release ELF is devkit-free (exit 1 = leaked) |
| `<dir \| project.tyra>` | open the GUI |

`--build`, `--resave`, `--refresh-gen`, `--apply-graph`, `--ai-graph` refuse a project with pending registered migrations (exit 1).

### Layer 2
- `--new` writes every generated file; `--refresh-gen` refreshes an existing project. Grep `inc/scene_data.hpp`, `inc/*.gen.hpp`, `src/gen/*`.
- `.gen.*` + `scene_data.hpp` are always rewritten; `terrain_game.cpp`/`controls.hpp`/`script.hpp` only while their marker line is intact (user-owned otherwise).
- Edit `<name>.tyra` directly to shape a fixture (source of truth). Checked-in examples' generated files drift — regenerate before diffing.
- Host harnesses (pure modules, `App` privates, sessions, menus, strips…): `references/host-harnesses.md`.

### Layer 3
```
TYRAX --build <projectDir> --run
```
Refresh → provision PS2DEV/VU tools → sync engine, rebuild `libtyra` if changed → rsync → `make -j` → `adpenc` sfx → sync `bin/` → close PCSX2 instances running THIS ELF → force `HostFs = true` → launch. First build downloads PS2DEV v2.0.0 (minutes); then seconds. Measure a build by what it recompiled (`grep -c 'elf-g++ .* -c -o'`; no-change build = 0). → `references/e2e-pcsx2.md`.

## Reading results

- **`bin/log.txt`** (host: fs): `TYRA_LOG` output + assertion dump between `======= TYRA =======` and `================`. Grep it; a failed assert halts quietly with the last frame up. Feature counters (`VRAMSTAT`, `FTCLIP`, `VEHCONTACT`, `ROADSTRIP`, `Replay:`, …) land here.
- **emulog.txt** (beside PCSX2.ini, `logs/`): success = `ELF <path> is executing`. **`TYRA_LOG`/EE printf never lands here.** Stops after `ELF Loading:` = path too long; `Denying access`/`Failed to read ELF` = relative `-elf`.
- **ps2link**: no `bin/log.txt`; the log is the `[ps2] …` lines (Output panel / `--run-ps2` stdout). Games built before 2026-08-06 log nothing there.
- **`--capture-frame`**: debug build + Live Debugger on; game writes `bin/frame.tga` (BGRA, bottom-up, `18 + w*h*4` bytes) from GS VRAM — the GS raster, no window, no focus, the only capture on real hardware. Space calls > 1 s apart; delete `bin/frame.tga` / `bin/livedbg.cmd` between arms.
- Host-side PCSX2.ini traps: HostFs missing → "Failed to load ...png" assert; PCSX2 rewrites the ini on exit; Documents may be OneDrive-redirected.

## Driving and capturing (no human)

- **Game pad**: `TYRAX --pad <dir> "stick r 110 0; wait 1.5; neutral"` — steps `press cross [s]`, `hold up`, `release up|all`, `stick l|r x y`, `wait s`, `neutral`, `pad 1|2`. Debug build, *Remote Pad* on. The game reads only analog sticks. On Windows read the driver's stderr + exit code; `Start-Process -ArgumentList` mangles `;` scripts. → `references/screenshots-and-pad.md`.
- **Editor UI**: `TYRAX --ui-script <dir> "frames 20; dump"` first, then `click 'Window/Label'`, `expect-checked`, `drag <target> dx dy [left|right|middle]`, `wheel`, `text`, `key`, `shot`. Menus: `click 'TyraX menu'; click Tools; click 'Remote Pad'` (the logo button; a step naming a menu opens it itself). Pair state-changing clicks with an `expect`; select tabs before their contents; `frames 60`–`90` after `wheel`. PowerShell: script in double quotes, targets in single. → `references/ui-script.md`.
- **Screenshots from outside**: Windows `scripts/screenshot-window.ps1 -ProcessId <pid> -PrintWindow -Auto [-Trim]` (`-PrintWindow` whenever anything may cover the window — GDI reads the SCREEN and once captured the owner's browser); `-Watch DIR` = contact sheet + changed-pixel table. Linux/Wayland `scripts/wayland-control.py shot|script|watch` (mutter D-Bus; no per-window capture → `--area`). Editor self-capture: `TYRAX_SHOT=<dir>`.
- **Synthetic keys into PCSX2** (only for PCSX2's own hotkeys): wayland-control.py `key`/`keydown`/`keyup` on Linux; ALT-tap + `AttachThreadInput` + verified foreground on Windows.

### The idle-control pattern (every "did input arrive" test)
idle shot → wait 3 s → idle shot (**control**, ~600 px status-bar noise) → turn (right stick ~197k px) → walk (~1.4M px; an axis-aligned walk over the checkerboard alone barely changes anything) → wait 4 s → idle shot (**release** proven). Or one `-Watch`/`watch` over the whole drive.

## Choosing the right depth

| Change | Minimum honest verification | read |
|---|---|---|
| Editor UI (panel, dialog, toggle) | Layer 0 + `--ui-script` that opens it, does it and ASSERTS (`expect`/`expect-checked`) + a `shot`. Either OS, no focus | ui-script.md |
| Editor viewport rendering | Layer 0 + capture of the panel (`shot`, `TYRAX_SHOT`, or from outside) — measure pixels; offscreen GL harness for numbers | screenshots-and-pad.md, host-harnesses.md |
| Serialization (`.tyra`) | `--new` + reopen; `--resave` round-trip diff; migration gate tests | cli-and-codegen.md |
| Codegen / templates | Layer 2 grep or harness, then one Layer 3 boot | cli-and-codegen.md |
| Engine (`vendor/tyra`) | Layer 3 always — native backend (Docker too if compatibility-sensitive); SW-renderer capture for visuals; VU scheduling → console | e2e-pcsx2.md, vu-and-blss.md |
| VU1 microprogram | `--vu-check` (falsified red first), Layer 3, console for timing | vu-and-blss.md |
| Audio | Layer 3 + peak meter on the PCSX2 process; ADPCM header bytes | acceptance-physics-procedural-import.md |
| Anything a player DOES (buttons, walking, menus, two players) | Layer 3 + `--pad` with idle control / release shots; `watch`/`-Watch` sheet | screenshots-and-pad.md |
| "Does it do the same thing as last time" | `--record` + `--replay` exit code, plus tamper + fight checks | screenshots-and-pad.md |
| How a frame is BUILT or PRESENTED (upscaler, pacing, extrapolation, buffer counts, full-screen pass) | Layer 3 + **motion gate**, two arms one knob apart (parked A/Bs miss motion-only faults) | motion-and-ab-rigs.md |
| Dynamic-shadow switch | Layer 3 + **shadow A/B rig**, deltas vs a same-value control | motion-and-ab-rigs.md |
| Performance claim | Instrumented fixture with fresh assets + generated code from the editor under test; profile stated; console for absolute ms | performance-and-profiling.md |
| Package / update check | Harness for the pure half, `--ui-script` modal, real install + `--vu-check`/`--new` FROM it, uninstall / driven self-update | ai-and-packages.md |
| ISO export | *Project > Export PS2 ISO*, mount on host, boot in PCSX2 | e2e-pcsx2.md |

## Index — read `references/<file>` when you verify…

| file | when |
|---|---|
| `editor-build.md` | editor build failures, deps/pinned SHAs, platform parity, build timing, native incremental-build regressions, scene-object iteration |
| `cli-and-codegen.md` | `--new` fixtures, `--refresh-gen` vs `--build` (texbake!), GI / bake-status / model AO / pre-lit bakes, vehicle + procedural bakes in refresh, far vehicle tier, codegen inspection, format versioning & migrations |
| `host-harnesses.md` | link lines for scratch `main()` harnesses: pure modules, `App` privates, offscreen GL viewport, menu styles + Classic pixel identity, collaboration sessions, log panels, Live Logic, editor audio |
| `vu-and-blss.md` | `--vu-check` (falsify it), 2x2 skew rule, `--vu-replay`, VU1 packet capture (`arm-vucap.py`, `--dump-vucap --full/--peek`), BLSS train/eval/coverage, `--threads` determinism, interlock |
| `e2e-pcsx2.md` | Layer 3 pipeline, host prep, ADPCM loop byte, toolchain image, incremental-build evidence, PCSX2.ini/path traps, emulog vs log.txt, `--capture-frame` details, rendering-correctness rules, settle tests, savestates/GS dumps (`pcsx2-capture.py`), crashes |
| `screenshots-and-pad.md` | `--pad` (and its Windows traps), synthetic input, `screenshot-window.ps1` (GDI vs PrintWindow, `-Watch`), `wayland-control.py` (shot/script/watch), `TYRAX_SHOT`, unattended input test, record/replay, two-player |
| `ui-script.md` | `--ui-script` steps, targets, tabs/combos, Project Preferences, clipped items, trickle/wheel timing, quoting, node canvases |
| `devkit-and-ps2link.md` | `--debug-state`, Live Debugger probes, `--audit-release`, ps2link deploy/stop/ownership, hosting `ps2client listen`, ps2link in PCSX2 + its unit tests, IOP marker, console race vs wedge |
| `performance-and-profiling.md` | frame profiler, `TYRA_FRAME_PROFILE` levels, devkit costs on console, clean console measurement, render cost, hardware timeline, adaptive BLSS gate, fixture hygiene (stale assets/sources/editor binary, ELF hashing), removed benchmark tooling |
| `acceptance-renderer-pipeline.md` | removal visibility, scene-data identity, LOD/impostor pops, skip-when-unchanged, StaPip retained/baked stream/content-version/bbox cache, culling pricing, static submission batch, hull/reflection proxies, first-entry HUD, hybrid triple buffer |
| `motion-and-ab-rigs.md` | period-2 stability gate, motion gate (`motion-gate.ps1/.py`, `routecam.cpp`), console flicker failure-rate fixture, shadow A/B rig |
| `acceptance-rendering.md` | animated GI / RGB SH, skeletal animation timing, impostors (views, GPU bake, picking), triangle strips |
| `acceptance-vehicles-roads.md` | vehicle damage/contact/rendering/editor/inheritance, vehicle sounds + sfx conversion, road/terrain strips, road height index, spans, junctions, road editing, static-batch oracle |
| `acceptance-physics-procedural-import.md` | physics, particles, procedural placement + frozen bake, model import + picker (fake `zenity`), game audio metering |
| `ai-and-packages.md` | AI Assistant (prompt, harness, fake backend, traps), update check, Windows installer, Linux packages, self-update |

Bundled scripts (`.claude/skills/tyra-testing/scripts/`): `screenshot-window.ps1`, `wayland-control.py`, `arm-vucap.py`, `pcsx2-capture.py`, `motion-gate.ps1` + `motion-gate.py` + `routecam.cpp`, `make-shadow-fixture.ps1` + `shadow-ab.ps1` (+ `shadow-ab.lib.ps1`).

## TyraX 2.0 integration

The editor is 2.0.0; the combined project format is 114. Read
[TyraX2 integration notes](references/tyrax2-integration.md) before changing
frame ownership, pipeline recording or the associated acceptance tooling.
The consolidated branch notes retain the full historical qualification record.
