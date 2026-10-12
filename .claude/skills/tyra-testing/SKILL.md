---
name: tyra-testing
description: >
  How to build, run and VERIFY anything in this repo: compiling the editor
  (build.ps1 on Windows, build.sh on Linux), headless CLI project creation and
  game builds, checking code generation without a game build, full native or
  Docker-fallback e2e + PCSX2 (boot, emulog.txt, reliable screenshots, and
  DRIVING both the game's controller (`--pad`) and the EDITOR's own UI
  (`--ui-script`, clicking widgets BY NAME) unattended — neither needs window
  focus — plus synthetic keyboard/mouse via the bundled scripts, GDI on Windows,
  mutter's D-Bus APIs on Wayland), and audio verification. Use this skill EVERY
  time you need to test a change, run the editor, build a game, boot PCSX2, take
  a screenshot, PRESS A BUTTON / move the player / drive the emulator or the
  editor without a human, create a scratch project, or decide "how do I know
  this works?" — including before writing a commit message or claiming a change
  is verified. There is no unit-test suite in this repo; this skill is the
  testing story. When a check proves nothing or something is stuck, read the
  matching `troubleshoot/tyra-testing-*.md`.
---

# Building, running and verifying

There is **no committed test suite** (no CTest, no test/ dir). Verification is layered: compile → codegen inspection → PCSX2 boot → visual/log/audio checks. Use the cheapest layer that actually exercises your change, and be honest in your commit message and PR body about which layer you reached — the established wording distinguishes "verified in PCSX2" from "compiles, needs a pad test by a human". (`PROGRESS 123` / "entry 123" citations point at the retired `PROGRESS.md`, in git history — recipe in `docs/backlog.md`; new work records itself in commit messages and PR bodies.)

**If the thing you need for a clean test does not exist, ADD IT — now, in the same change, not in a backlog entry.** A constant that should be a setting, a state only reachable by a human at the pad, a value the game computes but never logs: each is a small, honest feature (or a `--flag`, or a `TYRA_LOG` line) that makes the test repeatable — and an A/B that depends on a hand-driven input is not an A/B. Ask "what would make this a file edit + one command?" and build that first.

**Frozen-camera fixture** (every PCSX2 A/B of a rendering change): on the Player object set `walkSpeed: 0`, `lookSpeed: 0`, the `position` that frames the shot and `rotation: [pitchDownDeg, headingDeg, 0]` (rotation X is the pitch, positive = down), plus `"keyboardMouse": false` in the `.tyra`. Then `--build <dir> --run`, wait ~12 s, `-PrintWindow` screenshot. Same frame every boot, no pad needed.

## The four layers

| Layer | What it is | Cost | Use it for |
|---|---|---|---|
| **0 — build** | `./build.ps1` / `./build.sh` (`-Dev`/`--dev` = `-O1`) | seconds–minutes | compile checks; the edit-compile-look loop |
| **1 — headless CLI** | `TYRAX --new/--build/--resave/--refresh-gen/--bake-*/--dump/--pad/--ui-script` (`--docker` for a Docker game build; `--blss-*` for the upscaler; `--pad`/`--ui-script` also take `--file`/`--stdin`) | seconds | codegen, bakes, fixtures, unattended runs |
| **2 — codegen** | `--refresh-gen` + grep `inc/*.gen.hpp` / `src/gen/*`, or a host harness, no Docker | seconds | constants and generated logic without a game build |
| **3 — full e2e** | `--build <dir> --run` + PCSX2 boot, screenshots, `bin/log.txt` / `logs/emulog.txt` | minutes | anything that only exists in the game |

**Only one platform's compiler runs at a time**, so a cross-platform change is only half-checked until the other side builds too. Anything touching `src/platform.*`, `wire.cpp`, the Runner, CMakeLists or a paired build script needs a build on both, or say so in the commit.

## Choosing the right depth

| Change | Minimum honest verification |
|---|---|
| A package or the update check | harness for the pure half, `--ui-script` for the modal, a real install (silent on Windows, unpack the tarball on Linux) + `--vu-check`/`--new` FROM it, then uninstall / a driven self-update |
| Editor UI (a panel, a dialog, a toggle) | Layer 0 + a `--ui-script` run that opens it, does the thing and ASSERTS it (`expect`/`expect-checked`), plus a `shot`. No focus, either OS |
| Editor viewport (rendering) | Layer 0 + a screenshot of the affected panel, and measure the pixels rather than eyeballing |
| Serialization (`.tyra`) | Layer 1 `--new` + reopen; round-trip save/load diff |
| Codegen / templates | Layer 2 grep or harness, then one Layer 3 boot |
| Engine (`vendor/tyra`) | Layer 3 always — native backend (and Docker too for compatibility-sensitive changes); SW-renderer screenshot for anything visual |
| Audio | Layer 3 + peak-meter check |
| A game feature with a gym suite (Gunner) | while building: `scripts\gym.cmd --only <gym>`; when finished: the full suite on the valid renderer (GS Software (13), no frame limiter, hidden window). A gym must pass alone (`--isolated`) |
| Anything a player DOES (buttons, walking, menus, two players) | Layer 3 + `--pad`: an idle control shot, then drive, then measure. No human, either OS |
| Anything that changes how a frame is BUILT or PRESENTED (upscaler, frame pacing, extrapolation, buffer counts, a full-screen pass) | Layer 3 + **the motion gate** (the `motion-gate.ps1/.py` pair + `routecam.cpp`), two arms one knob apart |
| A dynamic-shadow switch (spot/flashlight volumes, blob shadows, a per-object shadow mode) | Layer 3 + **the shadow A/B rig** (`make-shadow-fixture.ps1` then `shadow-ab.ps1 -Toggle <key> -Values a,b`); quote `report.md` deltas against a same-value control run |
| ISO export | Export + mount the ISO on the host + boot it in PCSX2 |

## A DEBUG BUILD OVER PS2LINK IS NOT THE PLAYER'S FRAME

Read this before quoting ANY frame time or FPS off a console. Three debug-only costs sit inside every measurement and each looks like a property of the scene. Measured on a physical PS2 with `TYRA_FRAME_PROFILE 2` at a parked vantage:

| devkit cost | cadence over ps2link | what it did to the frame |
|---|---|---|
| **Remote Pad** polls `host:livepad.bin` | every 4th frame (every frame in PCSX2) | a network round trip in `loop()` ahead of `beginFrame()` |
| **Live Debugger** polls `livedbg.cmd` / writes the snapshot | every 25th frame (6th in PCSX2) | the same, rarer |
| **HUD `MEM` readout** (`showMemory`) - FIXED in 1.123.10 | every 2 s of game time | `getAvailableRAM()` malloc-probed the heap: **+31 ms** on one pose, **+75 ms** on a fragmented one |
| **`TYRA_FRAME_PROFILE 1`** | every frame | the packet walker + telemetry: HUD SCENE 13.21 -> 17.70 ms |

The clean console measurement, in order:

1. In the project's `.tyra`: `remotePad`, `liveDebug`, `liveLink`, `liveLogic`, `timeMachine`, `inputRecorder` false (and `showMemory` on an engine older than 1.123.10).
2. `TYRA_FRAME_PROFILE 2` in `vendor/tyra/engine/inc/debug/frame_profile.hpp` — FRAMETIME alone, transparent. Level 1 is for WHAT a frame is made of, and only its deltas are quotable.
3. `--refresh-gen`, then `tools/toolchain/native-build.ps1` directly.
4. Deploy with a bare `execee` straight after a power cycle; read `FRAMETIME` off ps2client. `pre + work + stall` must come to `period`; `miss` high with `over20` low means the cost is OUTSIDE the render.
5. **Transparency check:** capture the same pose once uninstrumented; its HUD SCENE must match.

## SUBMITTED IS NOT SEEN — ASK BY REMOVAL

An inventory ranks a frame by what each producer hands the GS; it cannot say whether any of it reaches the screen. The game can already hide an object at runtime (`ctx.objects[i].visible`):

> hide exactly one object, photograph the frame, diff it against the control. **Zero pixels changed means the object contributed nothing by ANY path** — silhouette, projected shadow, baked AO or reflection — so it is free to cull. A non-zero count IS its on-screen contribution, and `triangles / visible pixels` ranks the population.

Three checks come BEFORE any verdict, because every claim of this shape is a claim about a zero and a broken instrument produces zeros too:

- **within-probe repeatability**, then **control drift** (photograph the control first AND last in the same boot; a run whose control moves is dead);
- **a positive control for every zero** — photograph the suspected occluder removed too. Do NOT read that pair against the control; compare pair against occluder.

## A fixture can lie, and so can a null A/B.

- A fixture generated by a **stale editor binary** measures a different game, and nothing in any log says so. Rebuild the editor for the control arm too and diff the two generated `terrain_game.cpp` before measuring (`keyL=`/`batchStatic=` are cheap canaries).
- `--refresh-gen` does NOT run `texbake`, and an editor binary can be older than HEAD: check the baked constant itself (`grep -n "stripRun = 7" <fixture>/src/terrain_game.cpp`) and the binary's mtime.
- `Copy-Item`/`cp -p`/`git checkout` carries the source timestamp, so a swapped file can be older than the object and `make` does nothing: stamp the file, delete the `.o` in both trees, then **hash the two ELFs and check they differ**. A null A/B is only evidence if the two arms were two builds.
- A parked fixture is the BEST case for a skip-when-unchanged change and must be labelled as one; `--keep-routes` is the moving realistic case. The district's NIGHT poses flicker and never settle — use a DAY pose for any pixel A/B.
- **A matching capture hash is not a fixture check** — a strip run changes how a surface is cut, not which pixels it covers.

## A GREEN CHECK ONLY MEANS SOMETHING IF YOU CAN MAKE IT RED.

Falsify every check before believing it:

- `--vu-check`: invert the branch or zero the term in the HANDWRITTEN program and confirm it reports `DIFFERENT`; use the 2x2 (old/new binary against old/new engine).
- `--audit-release`: run it against a DEBUG ELF and confirm it fails.
- `--road-index-verify`: shift the query one cell and confirm a first mismatch; an off-road query agrees trivially.
- The bag-array negative test: make `data()` non-const and it must go red on exactly the two guarded cases.
- `--pristine` ps2link tests must FAIL against the untouched upstream file.

## The scripts (`skills/tyra-testing/scripts/`)

| Script | What it does |
|---|---|
| `screenshot-window.ps1` | window capture (GDI default / `-PrintWindow`), `-Watch` contact sheets, `-ProcessId` |
| `wayland-control.py` | Linux capture + synthetic input over mutter D-Bus; `shot`, `script`, `watch` |
| `pcsx2-capture.py` | private PCSX2 savestates + GS dumps; `run` / `gym` / `gs` / `state` |
| `arm-vucap.py` | arm a VU1 packet capture without the GUI (`bin/livedbg.cmd`) |
| `motion-gate.ps1` | capture one four-leg motion-gate arm |
| `motion-gate.py` | decide the motion gate (cross-platform); exit 0/1/2 |
| `routecam.cpp` | the frame-indexed four-leg camera route (`hold`/`pan`/`dolly`/`return`) |
| `make-shadow-fixture.ps1` | build the shadow A/B fixture headlessly, short path |
| `shadow-ab.ps1` | run the shadow A/B per switch (`-Toggle`, `-Values`) |
| `shadow-ab.lib.ps1` | shared library for `shadow-ab.ps1` |

## When you are stuck

Read `../../troubleshoot/` before you debug; find yours by the symptom. The index is `../../troubleshoot/tyra-testing.md`; per-topic files are `../../troubleshoot/tyra-testing-<topic>.md`:

| Symptom shape | Read |
|---|---|
| Build/bake/native-incremental failures | `tyra-testing-build.md` |
| A headless verb does nothing / refuses / a migration is a no-op | `tyra-testing-cli.md` |
| A BLSS number cannot be reproduced; a `--vu-check` result is suspect | `tyra-testing-blss.md` |
| Codegen/template/menu-harness checks | `tyra-testing-codegen.md` |
| A PCSX2 boot/launch/gym/toolchain-image failure | `tyra-testing-e2e.md` |
| Pad ignored / mangled arg / the unattended recipe / input replay | `tyra-testing-pad.md` |
| A black or wrong-window screenshot; watch over time; Wayland; savestates | `tyra-testing-capture.md` |
| Driving the editor UI; the editor's own framebuffer/viewport | `tyra-testing-ui-script.md` |
| A pixel/visual/timing comparison proves nothing | `tyra-testing-measure.md` |
| Finding what the owner debugs; the console/devkit channel is dead | `tyra-testing-debug-state.md` |
| Scripted probes: flow-graph, VU1, crash, audit, logs, audio, multiplayer | `tyra-testing-probes.md` |
| A fault exists only while the picture MOVES | `tyra-testing-motion-gate.md` |
| A corruption only on real hardware, only some frames | `tyra-testing-flicker.md` |
| A dynamic-shadow switch | `tyra-testing-shadow-ab.md` |
| AI Assistant layers | `tyra-testing-ai.md` |
| Packages / installer / update check | `tyra-testing-packages.md` |
| Physics or particles | `tyra-testing-simulation.md` |
| Animated directional GI / RGB SH | `tyra-testing-gi.md` |
| Impostor data path / eight-view / picking / GPU bake | `tyra-testing-impostors.md` |
| Vehicle damage/contact/defaults/chase camera/sound | `tyra-testing-vehicles.md` |
| Roads / junctions / hull & reflection proxies | `tyra-testing-roads.md` |
| Triangle strips / road-terrain twins / oracles | `tyra-testing-strips.md` |
| A CONTENT change where the arm is the editor | `tyra-testing-content.md` |
| Render-cost capture / full-asset gate / hardware timeline | `tyra-testing-perf.md` |
| Retained commands / content version / baked VIF / static batching | `tyra-testing-stapip.md` |
| Pricing a culling change / bbox counters / collisions unchanged | `tyra-testing-culling.md` |
| A POP / a skip-when-unchanged change | `tyra-testing-skip.md` |
| Exact first-entry HUD / hybrid triple-buffer | `tyra-testing-hud.md` |
| Procedural placement / frozen bake / scene object iteration | `tyra-testing-procedural.md` |
| Model import / model picker acceptance | `tyra-testing-model-import.md` |
| The console frame (debug build is not the player's frame); ps2link launch/cadence | `tyra-testing-ps2link.md` |
| Submitted geometry may not be reaching the screen | `tyra-testing-visibility.md` |
