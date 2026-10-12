# troubleshoot/tyra-testing-shadow-ab.md

The shadow A/B rig: one command per switch. Symptom first.

## A dynamic-shadow switch needs a rig, not a pair of screenshots

**Cause:** the change is a **switch**, the evidence is **where the picture got darker**, and the honest test is the same authored frame rendered twice with one setting moved (docs/shadows.md, docs/flashlight.md).
**Fix:** two scripts make that one command:

```powershell
# 1. the fixture, once - headless, no Docker, SHORT path
powershell -File .claude\skills\tyra-testing\scripts\make-shadow-fixture.ps1 `
    -Editor build-dev\tyrax-editor.exe -Force

# 2. the A/B, per switch
powershell -File .claude\skills\tyra-testing\scripts\shadow-ab.ps1 `
    -Editor build-dev\tyrax-editor.exe `
    -Project $env:TEMP\tyra-editor-test\spotab `
    -Vantages $env:TEMP\tyra-editor-test\spotab\vantages.json `
    -Toggle spotShadowVolumes -Values true,false `
    -OutDir <scratchpad>\spotab
```

`-Toggle` is any key the manifest writes on a line of its own (`spotShadowVolumes`, `flashShadowVolumes`, `blobShadows`) and is **inserted** when the file does not carry it. **`bakedShadows` is the one toggle that needs a step in between**: it reads a CACHE, so run `tyrax-editor --bake-shadows <project>` once with the key true, then A/B it — the cache is content-hashed and survives the toggle going false and true again. For each (value x vantage) the rig patches the setting and the Player's pose, runs `--build --run` under a hard timeout, waits `-Settle` (14 s), screenshots **the emulator whose command line names this project**, greps the game's `bin/log.txt` for `Assertion` / `=======` banners, and writes `report.md` with a per-row table plus a delta table.

## The fixture

**Cause:** the interesting failure of a per-light feature is whether the second light starves (the count band is one buffer for the whole frame).
**Fix:** `%TEMP%\tyra-editor-test\spotab`, from the `fpp` template: flat 100x100 terrain, **two** lamp+caster+wall groups 20 u apart — a spot light 4 u up (half-angle 30, radius 12, `"shadowVolumes": 0`), a 1.5 u box 0.9 u off the cone axis, an 8x4x1 wall 3 u behind it — and a frozen Player with the flashlight on. `vantages.json` frames lamp 1 from 8 u, **both** groups from 20 u back midway between them, and lamp 2 from 8 u.

## Five things it had to get right

**Cause:** each produced a confident, wrong answer first.
**Fix:**
- **A torch that sits on the eye casts no visible shadow at all.** The camera occludes exactly what the light does (measured: **d centre 0.004, d lower third 0.000**). The fixture drops the torch by the clamp's full metre (`offsetDown`) and 0.6 m to the side.
- **A new project applies an AMBIENCE PRESET on top of its settings.** `--new` ships "Default" (blue sky, ambient 0.55) with `defaultAmbience: 0`, so a manifest that says `ambient: 0.1` still renders a sunny afternoon. The fixture sets `defaultAmbience: -1`.
- **`-Trim` cannot be used, and neither can a plain content bounding box.** The crop is found from the FIRST capture and reused; finding it needs a **coverage** rule (PCSX2 paints `FPS 58.35 [P]` inside the black letterbox, which stretched the measured picture from 959x691 to 959x829).
- **`powershell -File` does not split `-Values true,false`**, so the rig gets one string, writes `"key": true,false,` into the manifest, and the build fails with `spotab.tyra is malformed`. It splits on commas itself now.
- **A region metric reports 0.0000 for "nothing changed" AND for "the subject was not in my rectangle."** The `between` vantage frames two lamp groups, so both sit at the EDGES (its centre region read exactly 0.0000 for a switch the whole frame scored 0.00005 on). Read the whole-frame column first, then the regions to say *where*.

## Read the numbers, not the pictures

**Cause:** a black window measures as a beautiful shadow everywhere.
**Fix:** each row reports the mean luma (0..1) of the **whole frame**, the **centre region** (the caster and the wall behind it) and the **lower third** (the ground), plus the **ink coverage of the FPS strip** — a liveness check, NOT a reading of the counter (0.000 ink is what catches a black window).

## Pass the same value twice for the noise floor

**Cause:** a delta smaller than the noise floor is not a finding.
**Fix:** `-Values true,true` boots one configuration twice; the delta table is indexed by variant, so the second row is what two boots of an unchanged project differ by. Measured on this fixture (frozen camera, `progressive` display) the noise floor is **exactly 0.00000** — nine boots across three vantages produced byte-identical means for the two `false` arms, on every region.

## Pass ONE value and a line of vantages and the same rig is a WALK

**Cause:** "from where does this shadow stop being drawn at all" is a question no A/B of a switch can reach.
**Fix:** the vantages are then one subject seen from a sequence of standpoints; read `centre mean` against the vantage name. That is how the projected-shadow slot flicker (1.70.1) was found: `examples/night-walk` copied to a short path, its twelve `projShadow` casters left alone, the Player walked sideways past the shed at x = 0, 4, 6, 7, 7.5, 8, 9, 10, `-Toggle showFps -Values true` (a no-op key so the rig runs exactly one arm). The shed's ground shadow is a hard-edged black quad through x = 7.5 and entirely gone at x = 8.0 — frame mean 0.10289 → 0.11360, centre 0.187 → 0.216. Two things that made it work: aim every vantage at the same world point (compute pitch and yaw per standpoint), and RENAME the scratch copy's project (a Docker container is named after the project; renaming means fixing the namespace in `src/scripts/*.cpp` too).

## Everything that can reach Docker is bounded

**Cause:** a `docker` call on this machine blocks forever instead of failing.
**Fix:** it is killed with its children on timeout, and nothing is ever killed by name — the emulator the rig captures and closes is the one whose `-elf` names the project, the same discriminator `Runner::killEmulatorsFor` uses.

**Windows only so far**, like `motion-gate.ps1`: the capture is `screenshot-window.ps1`, the measurement is System.Drawing and the emulator is found through `Win32_Process`. Say which OS a reported number came from.

## Proven on the flashlight (2026-08-21)

**Cause:** the spot runtime does not exist yet, so the rig was proven on `flashShadowVolumes`, whose volumes have worked since 1.62.0.
**Fix:** nine boots (`-Values true,false,false` — the repeated value is the control), three vantages, PCSX2 software renderer, 959x691 picture, **all nine logs clean**:

| vantage | frame mean, `true` -> `false` | d frame | d centre | control (arm 2 - arm 1) |
|---|---|---|---|---|
| lamp1-8u | 0.14608 -> 0.14846 | **+0.00238** | **+0.0134** | 0.00000 |
| between  | 0.11111 -> 0.11115 | +0.00005 | 0.0000 | 0.00000 |
| lamp2-8u | 0.13705 -> 0.14004 | **+0.00299** | **+0.0173** | 0.00000 |

Turning the volumes OFF makes the picture BRIGHTER, which is the right sign. The control column is load-bearing — the two `false` arms came back byte-identical on every region of every vantage. `-Toggle spotShadowVolumes` is already wired end to end on the same fixture: flipping it and running `--refresh-gen` moves `SPOT_SHADOW_VOLUMES_USED` in `inc/scene_data.hpp` between `false` and `true`.
