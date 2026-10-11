# Picture-stability gates and A/B rigs: period-2 test, motion gate, failure-rate fixture, shadow A/B rig

Read when: a change affects how a frame is BUILT or PRESENTED (upscaler, jitter, extrapolation, buffer counts, full-screen passes), you hunt a console-only flicker, or you A/B a dynamic-shadow switch.

## Is the picture STILL? Period-2 test, not a diff (docs/profiling.md "The stability gate")

- Freeze the camera **and every emitter** (on `examples/upscaler-lab` particles change more per frame than the artefact).
- Point at **textured** content (a quarter-pixel resample can't change an untextured box).
- Capture **back to back**, never on a stride (an even stride samples one phase forever).
- **Never `-Trim`**; take the crop once (trimming re-registers a shifted picture into an identical one).
- Report a **cross-correlation lag** plus pixel count; cluster by pairwise difference — two balanced clusters, ~0 within and large between, IS the alternation. Validate the verdict against a labelled A/B/C with known answers.
- Windows: select PCSX2 by project command line + `-ProcessId`; capture with **`-PrintWindow`** (GDI reads the screen → fictional "stable" tables); diff frame 0 of one arm against frame 0 of another before believing anything.
- Freezing is also what blinds it to motion-only faults → the motion gate.

## The motion gate

Four upscaler-branch defects reached the owner past frozen gates:

| defect | why gates missed it |
|---|---|
| jitter shake | sampler used an **even frame stride** → same jitter phase every capture |
| terrain streaking at grazing angles | nothing looked at a moving ground plane |
| BLSS x frame extrapolation tearing | parked, all four arms identical |
| black frames with triple buffering | a human happened to watch an agent's emulator |

```powershell
powershell -File .claude\skills\tyra-testing\scripts\motion-gate.ps1 -Project $env:TEMP\tyra-editor-test\mgate -Out <scratch>\armB -NoAnalyse
python .claude\skills\tyra-testing\scripts\motion-gate.py <scratch>\armB --baseline <scratch>\armC --bands 8
```
`motion-gate.ps1` captures (Windows; also `-ProcessId`, `-Count`, `-Legs`, `-LegFrames`, `-Pad`/`-NoPad`, `-Gdi`, `-Baseline`); `motion-gate.py` decides, cross-platform (any burst dir; `--box`, `--tiles-x/--tiles-y`, …). Exit 0 nothing flagged / 1 flagged / 2 burst unusable. Four-leg run ≈ 25 s capture, ~1.5 GB raw frames; two-arm analysis ≈ 3 min.

**Fixture**: `scripts/routecam.cpp` — global script (`TYRA_SCRIPT`) driving `ctx.cameraOverride`/`cameraEye`/`cameraAt` from its **frame index**, four 200-frame legs closing a loop: `hold` (parked: noise floor), `pan` (50° yaw), `dolly` (6 units forward, camera 1.6 → 0.9: grazing textured ground, zoom with no global translation), `return`. Copy into `src/scripts/`, fix the namespace; set `displayMode: progressive`, `showFps`/`showMemory`/`showProfiler` off, `liveDebug` on (frame counter from `bin/livedbg.bin`), emitters RUNNING. Built/measured on `examples/upscaler-lab` copied to a short path.

**Determinism**: route is a pure function of the frame index; the pad only zeroes it once (a one-shot shifts the whole route equally; no press → route still runs, phase unknown). Use `getPressed()` with own edge (`getClicked()` never sees a remote press). Schedule legs in GAME FRAMES (emulator ran 50–61 fps across legs, **28.8 fps** with extrapolation on) by polling `livedbg.bin` between bursts. Parked-vs-moving comes from the leg NAME, never the measurement.

**Statistic** — the part of a difference a rigid move can't explain:
```
d       global displacement, phase correlation (integer + sub-pixel)
raw     mean |A - B|                      <- dominated by the route
mc      mean |shift(A, round(d)) - B|     <- what the move did not explain
mc/raw  unexplained FRACTION, scale-free
```
- bob = displacement on top of the route (whole displacement on `hold`, in px); `mc` compensates only the INTEGER part so sub-pixel bob stays in the residual.
- tear = two displacements: correlate horizontal BANDS, flag the largest adjacent-band jump in EXCESS of the typical one (dolly naturally gives dy 0,0,0,0,+3,+6 sky→ground). A band votes only if its correlation ≥ 0.4x the whole frame's (good bands 0.65–0.85; bogus 30-px bands 0.26–0.33).
- black frame = luma collapse.
- streaking = per-band residuals ("ground band 6x sky band").
- stopped being DRAWN = per-tile on `hold`: both arms' per-pixel MEDIAN frames (removes particles) compared tile by tile; changed + local variance collapsed = **stopped being drawn**; changed + detail kept = merely different. (Hardcoded `ZBUF` mask case: the tell was the **crosshair** disappearing.) Prints `NOT ASKED` above 0.5 px of bob. Read the tile list as a SHAPE: scattered sky/edge tiles = translucent particles reconstructing differently; terrain/crosshair = contiguous block or lone sprite tile. `--tiles-x/--tiles-y` grain; 24x18 ≈ 40 px.
- Runs compared as per-leg DISTRIBUTIONS, never capture k vs k. ONE knob between arms; a verdict the baseline also shows on that leg is dropped.

**Rules the scripts encode**: consecutive captures, no stride (reports achieved rate — **48–59 Hz** PrintWindow vs 50–60 fps game — and warns within 0.04 of a whole number of frames); `-PrintWindow` by default; select emulator by project command line (`-Project`); read the frame counter only before/after a burst (24 per-frame log lines in the boot loop made the black-frame defect vanish; 45 HostFs reads/s is the same poking); `--pad` call lives in the `.ps1` with stderr/exit code read; worst capture's two frames + 8x-amplified residual written to `<leg>/look/`; crop once from first frame of first arm, reused (`--box`), found by a *coverage* rule (PCSX2's FPS text in the letterbox drags a bounding box over the bars).

**`--capture-frame` sampling of a frame-indexed route needs a length COPRIME with the advance**: the capture freezes the game a fixed spell, so a 1920-frame route sampled eight poses and three builds gave digit-identical scores. Prime length (1913) walks the whole route, same order every arm. Confirm alignment: min mean |diff| over a small offset window lands on 0.

**Results (2026-08-11)**, `examples/upscaler-lab` → `%TEMP%\tyra-editor-test\mgate`, software renderer, progressive 480p, emitters running, 110 captures/leg. Arm C = reference (BLSS on, `blssJitter` off, no extrapolation — shipped default):

| arm | leg | measurement | verdict |
|---|---|---|---|
| C | hold | move 0.00 px, raw 0.120/255 | noise floor |
| C | pan | move 4.12 px, mc/raw 0.514 | - |
| C | dolly | no global shift; band mc 0.22 → 14.7 sky→ground | zoom signature |
| C | own flag | PERIOD-2 on pan, 74/34 at 0.121 (4.0x) | in EVERY arm → dropped (24 fps animation under 60 fps camera, or BLSS temporal in motion — can't tell) |
| B `blssJitter` on | hold | 5.34 px per pair parked; mc 43.2x | `PICTURE MOVES`, `WORSE WHEN PARKED` |
| B | pan | 2.41x reference residual | `WORSE IN MOTION` |
| B | dolly / return | splits 53/56 at 12.1x, 55/54 at 7.2x | `PERIOD-2` |
| D BLSS x extrapolation | whole | world rate 28.8 fps by design | frame scheduling absorbed it |
| D | hold | 0.24 px parked; mc 26.7x | `PICTURE MOVES`, `WORSE WHEN PARKED` (synth frames freeze particles; extrapolation is period-2 by construction) |
| D | return | 2.24x; band mc 0.37 sky → 29.6 ground (80x) | `WORSE IN MOTION` = the grazing-ground artefact |
| D | dolly | splits 50/59 at 5.3x | `PERIOD-2` |

Repeat run of D: 26.93x vs 26.66x, 0.23 vs 0.24 px. Not reproduced: the black-frame defect (fix in `vendor/tyra`, not reverted); detector verified on a burst with three blacked-out captures (named by index, leg, time, game frame, file); the fix's diagnosis saw 3 of 8 consecutive PrintWindow captures black. Cannot separate "scene animated" from "reconstruction hiccupped" within one arm. PCSX2 only — correctness, never GS-fill/per-function numbers.

## Console-only flicker: the failure-rate fixture

For corruption only on real PS2 in SOME frames (1.81.1 slot-pool race: lamp corona sliver in 4–19 of 30 frames, never PCSX2):
1. **Pin the run**: input replay (`bin/replay.in`) pins `dt`; a script parks the camera on a fixed pose from a frame number on (pose A until frame 1400, pose B forever after; the cited `vantagecam.cpp` is not in the tree — see `zz_vantage.cpp` under `examples/large-terrain/authoring/` for the frame-indexed shape).
2. **Sample**: wait for `--debug-state` to report a frame past the park, then 24–30 free-running `--capture-frame`s seconds apart.
3. **Score vs ONE reference** (known-good build or PCSX2), HUD rows masked (`d[:70,:] = 0`), `pixels > 40` per sample → a **rate**; distinct values = number of interleavings (race: small discrete set; garbage: all different).
4. **Bisect with barriers**: one arm per sync point (GS FINISH after every bag, a VIF1 FLUSH the EE waits for, FLUSH in the VIF stream only, one wait at one site), 24+ samples each, read HUD FPS. Only EE-wait arms fixed 1.81.1 → located the writer.
Traps: both arms = same project directory (two copies with a moved object differed by one "PICK UP" prompt and cost a day); `--capture-frame` resumes a halted game (don't mix with halt/step); delete stale `bin/livedbg.bin` before launch or "past the park" returns at once.

## Shadow A/B rig (Windows; docs/shadows.md, docs/flashlight.md)

```powershell
powershell -File .claude\skills\tyra-testing\scripts\make-shadow-fixture.ps1 -Editor build-dev\tyrax-editor.exe -Force   # [-Project <dir>]
powershell -File .claude\skills\tyra-testing\scripts\shadow-ab.ps1 -Editor build-dev\tyrax-editor.exe `
    -Project $env:TEMP\tyra-editor-test\spotab -Vantages $env:TEMP\tyra-editor-test\spotab\vantages.json `
    -Toggle spotShadowVolumes -Values true,false -OutDir <scratch>\spotab      # [-Settle 14] [-BuildTimeout s] [-KeepRunning]
```
- `-Toggle` = any manifest key on its own line (`spotShadowVolumes`, `flashShadowVolumes`, `blobShadows`), **inserted** if absent. `bakedShadows` reads a CACHE: run `tyrax-editor --bake-shadows <project>` once with the key true (content-hashed, survives toggling).
- Per (value x vantage): patch setting + Player pose, `--build --run` under a hard timeout, wait `-Settle` (14 s), screenshot the emulator whose command line names this project, grep `bin/log.txt` for `Assertion`/`=======`, write `report.md` (per-row + delta tables); build logs + each boot's `log.txt` saved beside.
- Fixture (`%TEMP%\tyra-editor-test\spotab`, fpp): flat 100x100, **two** lamp+caster+wall groups 20 u apart (spot 4 u up, half-angle 30, radius 12, `"shadowVolumes": 0` so the PROJECT switch decides; 1.5 u box 0.9 u off-axis; 8x4x1 wall 3 u behind), frozen Player with flashlight. Two groups test starvation of the frame's shared count band. `vantages.json`: lamp 1 from 8 u, **both** from 20 u back midway (from 8 u they're 51° off-axis, outside a 60° frame), lamp 2 from 8 u.
- Traps it fixes: a torch ON the eye casts no visible shadow (d centre 0.004, lower third 0.000 with working volumes) → torch dropped by the clamp's full metre (`offsetDown`) and 0.6 m sideways; `--new` applies an ambience preset (`defaultAmbience: 0`, "Default", ambient 0.55) → fixture sets `defaultAmbience: -1`; crop found once from first capture with a coverage rule (PCSX2's `FPS 58.35 [P]` overlay stretched 959x691 to 959x829); `powershell -File` doesn't split `-Values true,false` (wrote `"key": true,false,` → `spotab.tyra is malformed`) — rig splits on commas; region metrics read 0.0000 when the subject is off-region → read the **whole-frame** column first.
- Columns: mean luma (0..1) of whole frame, centre (caster + wall), lower third (ground), FPS-strip **ink coverage** (liveness: a black window looks like perfect shadow; 0.000 ink catches it).
- **Noise floor**: `-Values true,true`; measured exactly **0.00000** (nine boots, three vantages, byte-identical).
- **One value + a line of vantages = a WALK** ("from where does this shadow stop?"): 1.70.1 projected-shadow slot flicker: `examples/night-walk` short-path copy (its twelve `projShadow` casters untouched), Player sideways past the shed at x = 0, 4, 6, 7, 7.5, 8, 9, 10 aimed at its base, `-Toggle showFps -Values true` (no-op, one arm). Shadow present through 7.5, gone at 8.0: frame 0.10289 → 0.11360, centre 0.187 → 0.216. Aim every vantage at the same world point (per-standpoint pitch/yaw). RENAME the scratch project (Docker container named after it; fix `src/scripts/*.cpp` namespaces or the Runner pre-flight refuses).
- Docker calls bounded and killed with children on timeout (`docker` can block forever); nothing killed by name (emulator selected by `-elf`, like `Runner::killEmulatorsFor`).
- Windows only (screenshot-window.ps1, System.Drawing, Win32_Process); Linux twin would use `wayland-control.py shot --area`, PIL, `/proc/<pid>/cmdline`. State the OS of any number.
- Proven on `flashShadowVolumes` (volumes working since 1.62.0; 2026-08-21; `-Values true,false,false`, software renderer, 959x691, all logs clean):

| vantage | frame mean true → false | d frame | d centre | control |
|---|---|---|---|---|
| lamp1-8u | 0.14608 → 0.14846 | +0.00238 | +0.0134 | 0.00000 |
| between | 0.11111 → 0.11115 | +0.00005 | 0.0000 | 0.00000 |
| lamp2-8u | 0.13705 → 0.14004 | +0.00299 | +0.0173 | 0.00000 |

  OFF = BRIGHTER (correct sign). `between` honest 0 (torch barely reaches at 20 u; that vantage is for the spot test). `-Toggle spotShadowVolumes` already moves `SPOT_SHADOW_VOLUMES_USED` in `inc/scene_data.hpp` after `--refresh-gen` (docs/backlog.md "Run the shadow A/B rig against the spot runtime" says the spot runtime has not landed; `src/game_templates.inc` now gates runtime paths on `SPOT_SHADOW_VOLUMES_USED` — whether the spot runtime is complete is unverified, run the rig to find out).
