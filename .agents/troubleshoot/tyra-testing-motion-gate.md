# troubleshoot/tyra-testing-motion-gate.md

The motion gate — does the picture survive being MOVED? Symptom first.

## Every check that freezes the camera is blind to a class of faults

**Cause:** four defects on the upscaler branch reached the owner because no harness could see them:

| defect | why every gate missed it |
|---|---|
| the jitter shake | the sampler used an **even frame stride**, so every capture landed on the same jitter phase and it reported a perfectly still picture |
| terrain streaking at grazing angles | nothing ever *looked* at a moving ground plane |
| BLSS x frame extrapolation tearing | **parked, all four arms are indistinguishable**; it exists only in motion |
| black frames with triple buffering | found because a human happened to watch an agent's emulator |

**Fix:** a frozen fixture buys repeatability at the price of a whole class of faults. Add the motion gate — two arms one knob apart:

```powershell
# one arm = one four-leg run of the fixed route
powershell -File .claude\skills\tyra-testing\scripts\motion-gate.ps1 `
    -Project $env:TEMP\tyra-editor-test\mgate -Out <scratch>\armB -NoAnalyse
# then compare it against the arm with ONE knob changed
python .claude\skills\tyra-testing\scripts\motion-gate.py <scratch>\armB `
    --baseline <scratch>\armC --bands 8
```

`motion-gate.ps1` captures (Windows; also `-ProcessId`, `-Count`, `-Legs`, `-LegFrames`, `-Pad`/`-NoPad`, `-Gdi`, `-Baseline`); `motion-gate.py` decides (and is the cross-platform half — any burst dir, `--box`, `--tiles-x/--tiles-y`). Exit 0 = nothing flagged, 1 = something flagged, 2 = the burst is not usable. A four-leg run is ~25 s of capture and ~1.5 GB of raw frames; analysing two arms is ~3 min.

## The fixture

**Cause:** `scripts/routecam.cpp` is the route: a **global script** (`TYRA_SCRIPT`, no attachment) that drives `ctx.cameraOverride`/`cameraEye`/`cameraAt` from its own **frame index**, in four 200-frame legs that close into a loop.

| leg | what it is | what it is for |
|---|---|---|
| `hold` | parked at one pose | the parked stability gate, as a leg: the noise floor |
| `pan` | 50 deg of yaw | a near-rigid lateral move - the cleanest case for the residual |
| `dolly` | 6 units forward, camera dropping 1.6 -> 0.9 | textured ground at a **grazing angle**, and a zoom with no global translation |
| `return` | the way back | reverse dolly plus reverse pan |

**Fix:** copy it into a fixture project's `src/scripts/`, fix the namespace, and set: **`displayMode: progressive`**, **`showFps`/`showMemory`/`showProfiler` off**, **`liveDebug` on** (the gate reads the game's frame counter out of `bin/livedbg.bin`), and the emitters left **RUNNING** — one of the four faults lives in exactly that combination. `examples/upscaler-lab` copied to a short path is the fixture this was built on.

## Determinism: a frame-indexed route, and the pad as a ONE-SHOT trigger

**Cause:** the Remote Pad refreshes at 25 Hz off the **host** wall clock, so a stick lands at a different frame offset in every run.
**Fix:** the route is a pure function of the script's frame index. The pad's only job is to zero that index once. Three things fell out of building it:
- **`getClicked()` did not reach a global script from `--pad`.** Use **`getPressed()` with its own edge** — `livepad::tick` raises `clicked` inside the frame it polls, so anything reading it on the wrong side of that call never sees a remote press.
- **Schedule the legs in GAME FRAMES, not seconds.** The emulator does not run at 100% (this fixture measured 50-61 fps, and **28.8 fps with frame extrapolation on**). Poll `livedbg.bin`'s frame counter between bursts.
- **A parked leg whose picture is bobbing measures as "moving".** Take parked-vs-moving from the ROUTE (the leg's name), never from the measurement.

## The statistic, which is the hard part

**Cause:** particles change more between frames than the artefact the parked gate hunts, and a walking camera moves every pixel; "% of pixels that changed" is a description of the route.
**Fix:** the gate measures **the part of that difference that a rigid move of the whole picture cannot explain**:

```
d       global displacement, by phase correlation (integer + sub-pixel)
raw     mean |A - B|                      <- dominated by the route
mc      mean |shift(A, round(d)) - B|     <- what the move did not explain
mc/raw  the unexplained FRACTION - scale-free
```

- **a bob** is a displacement on top of the route; on the `hold` leg it is the ENTIRE displacement, in pixels. `mc` compensates only the INTEGER part on purpose, so a sub-pixel bob stays in the residual.
- **a tear** is two displacements in one picture. Correlate horizontal BANDS and look for a **step**: the largest adjacent-band jump *in excess of* the typical one. A band votes only when its own correlation is at least 0.4 as confident as the whole frame's.
- **a black frame** is a luma collapse. No statistic; just say it.
- **streaking** is spatial, so every residual is reported PER BAND ("the ground band is 6x the sky band").
- **something that stopped being DRAWN** is a per-tile question: on the `hold` leg both arms are parked at the SAME pose, so their per-pixel MEDIAN frames compare tile by tile with no alignment — a tile that changed while its local variance collapsed **stopped being drawn** (hardcoded `ZBUF` mask case: the tell was the **crosshair** disappearing); changed with detail kept = merely different.

Runs are compared as DISTRIBUTIONS over the same route, per leg — never capture k against capture k. **Change ONE knob between the arms**; a verdict the baseline also produces on the same leg is dropped.

## Rules the branch paid for, and that the scripts encode

- **CONSECUTIVE CAPTURES, NEVER A STRIDE.** The capture loop reports the interval it achieved (measured: **48-59 Hz** through `PrintWindow` against a 50-60 fps game) and **warns when that lands within 0.04 of a whole number of frames**.
- **A ROUTE SAMPLED BY `--capture-frame` NEEDS A LENGTH COPRIME WITH THE ADVANCE.** A round length makes the two commensurate (a 1920-frame route sampled eight distinct poses; three builds produced digit-identical sequences). A prime route length (1913 was used) walks the whole route. Confirm the alignment: search a small offset window for the minimum mean |diff| and check it lands on 0.
- **`-PrintWindow` by default.** Select the emulator by the project on its command line (`-Project` does it).
- **INSTRUMENT OUTSIDE THE LOOP YOU ARE PERTURBING.** The game's frame counter is read from `bin/livedbg.bin` once before and once after a burst; the per-capture index is interpolated and labelled an estimate.
- **`Start-Process -ArgumentList` does not quote its array elements**; the `--pad` call lives inside the `.ps1`, and its stderr and exit code are read out loud.
- **A frame missing geometry entirely is a different question from a frame drawing it wrong.** The gate writes the two frames of its worst capture plus the 8x-amplified unexplained residual into `<leg>/look/`.
- **Take the crop ONCE** and never `-Trim`; it is found from the first frame of the first arm and reused (`--box`), and needs a *coverage* rule rather than a bounding box.

## What it caught, and what it could not (2026-08-11)

**Cause:** the acceptance test is retrospective — a gate that cannot flag a defect we already understand is not a gate. Fixture `examples/upscaler-lab` copied to `%TEMP%\tyra-editor-test\mgate`, PCSX2 software renderer, progressive 480p, emitters running, 110 captures per leg. **Arm C is the reference** (BLSS on, `blssJitter` off, no extrapolation) and every other arm is **one knob** away.

| arm | leg | measurement | verdict |
|---|---|---|---|
| **C** reference | hold | move **0.00 px**, raw **0.120/255** | the noise floor, with the emitters running |
| | pan | move 4.12 px, mc/raw 0.514 | - |
| | dolly | no global shift; band mc ramps 0.22 -> 14.7 sky to ground | the zoom signature |
| | (own flag) | PERIOD-2 on `pan`, 74/34 at 0.121 | present in EVERY arm, dropped from every comparison |
| **B** `blssJitter` **on** | hold | **5.34 px** of displacement per capture pair on a PARKED camera; mc **43.2x** the reference | `PICTURE MOVES`, `WORSE WHEN PARKED` |
| | pan | **2.41x** the reference's unexplained residual | `WORSE IN MOTION` |
| | dolly / return | mc/raw splits **53/56 at 12.1x** and **55/54 at 7.2x** | `PERIOD-2`, new against the reference |
| **D** BLSS **x** frame extrapolation | (whole run) | the world rate halves to **28.8 fps** by design | frame-scheduled legs absorbed it |
| | hold | 0.24 px on a parked camera; mc **26.7x** the reference | `PICTURE MOVES`, `WORSE WHEN PARKED` |
| | return | **2.24x** in motion; band mc **0.37 sky -> 29.6 ground, an 80x ratio** | `WORSE IN MOTION` - the grazing-angle ground artefact |
| | dolly | mc/raw splits **50/59 at 5.3x** | `PERIOD-2`, new against the reference |

**Run to run it repeats**: a second independent capture of arm D read **26.93x** where the first read 26.66x, and 0.23 px of parked displacement against 0.24. Three things worth more than the numbers: the jitter shake is caught on the parked leg AND in motion; BLSS x extrapolation separates even PARKED at 26.7x because the **emitters are running** (a synthesised frame freezes the particles, so the arm that froze the emitters is exactly the arm in which all four configurations look identical); and the gate flags the reference arm's own `pan` leg (74/34 at 0.121, 4.0x) as the baseline's own flag, subtracted from every comparison.

**Forcing the refused combination.** BLSS x frame extrapolation is a build-time `#error` in one generated TU: `--refresh-gen` writes `src/gen/blss_interlock.gen.cpp` (and deletes it when the clash is gone). A scratch fixture gets past it by **deleting that file and building in the container by hand** — `--build` would regenerate it:

```powershell
tyrax-editor --refresh-gen $P            # prints "[blss] BUILD WILL BE REFUSED"
Remove-Item "$P\src\gen\blss_interlock.gen.cpp"
docker compose --project-directory $P -f "$P\docker-compose.yml" up -d
docker compose ... exec -T compiler sh -c "rsync -a --delete --exclude=.git --exclude=obj --exclude=bin /host/ /src/"
docker compose ... exec -T compiler sh -c 'cd /src && make -j$(nproc)'
docker compose ... exec -T compiler sh -c "rsync -ac --include=*/ --include=bin/** --exclude=* /src/ /host/"
```

**What it could NOT do.** The black-frame / triple-buffering defect was not reproduced (its fix is three hunks inside `vendor/tyra`); the DETECTOR is verified instead, against a real burst with three captures blacked out — it named all three by capture index, route leg, time, game frame and file. "What stopped being drawn" refuses to answer on a bobbing leg (it prints `NOT ASKED` above 0.5 px of bob); read the tile list as a SHAPE, not a count. It cannot separate "the scene legitimately animated" from "the reconstruction hiccupped" inside one arm. **PCSX2 only** — never quote a GS-fill or per-function number from it.
