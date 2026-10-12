# troubleshoot/tyra-testing-vehicles.md

Vehicle terrain stability, and the vehicle defaults / chase-camera acceptance. Symptom first.

## Verifying vehicle damage (docs/vehicles.md)

**Cause:** damage is a per-hit computation with loose pieces, and the editor side needs no game.
**Fix:** `--vehicle-check`'s `-- damage --` block holds the host rules. In the game, `On Start -> Enter Vehicle` seats the player in vehicle-playground's CC96 facing +z; `--pad "hold r2; wait 12; release all"` drives it into the arena wall and `bin/log.txt` answers with a `VEHDMG ... hit` line (impact, damage %, vertices moved, microseconds). The throttle is R2 and, since 1.147.0, reverse is L2 held from a standstill (`hold l2`; it brakes first if rolling) — the left stick only steers. To SEE a front dent from the chase rig, back off a few units and HOLD R3 (the rear view puts the camera ahead of the nose) while `--capture-frame` runs. The editor side needs no game: Damage tab > Hit front with the car framed via the manifest's `editor.cam` (`[yaw, pitch, dist, tx, ty, tz]`, radians) and `selectedObject` — edit that key by its `editor` block, not with a global replace (vehicle definitions have a `"cam"` key too). Loose pieces: `VEHDMG <car> lost <kind> ...`; the Ravager into the arena wall from the start loses kind 5 and 1. To force a door off, park another car broadside across the start lane in a scratch copy and raise its `damageLoose`.

## Verifying the vehicle contact / transform properties

**Cause:** `--vehicle-check` is a host property test, not a PS2 frame-rate measurement.
**Fix:** run `--vehicle-check` after changing vehicle contact or transforms. Its bank fixture restrains horizontal translation but retains gravity (zero gravity would prevent initial clearance from settling). It covers six headings and 20/25/50/120 Hz, generic-renderer versus wheel-rig orientation including Euler singular headings, missing terrain contacts, and alternating 50/8.33 ms steps with only a bumper supported. The latter must not generate launch velocity. Build and drive vehicle-playground for runtime validation, and measure wheel-batch changes on the console/emulator with an unchanged mesh and camera.

## Whether a tyre touches the ground is a LOG LINE, not a screenshot

**Cause:** the eye cannot read millimetres of gap.
**Fix:** the game prints `VEHCONTACT car N def D parked|driven gap1000 ... roadlift1000 ...` (lowest drawn tyre vertex minus `groundSurfaceAt`, per wheel, in mm; a parked car once as it sleeps, the driven car every second, nothing on a far tier). 0 is on the ground; -119 on a road was the terrain-only contact bug, -7 a definition radius behind its baked wheel (docs/vehicles.md, "Wheels on the road surface"). Check a car on a road AND one on bare terrain — the road lift column tells them apart.

## Vehicle defaults and chase-camera acceptance (1.153.0)

**Cause:** inheritance, overrides and the chase boom each have silent failure modes.
**Fix:** Resave a legacy definition and verify its tuning stays local. In a scratch project, change global accel, damage and nosCapacity; verify inheritance, local accel and zero-capacity overrides plus a silent sounds override survive save/load and reach `VEHICLE_DEFS`. Global defaults must work without vehicles. UI scripts can target Global defaults, Inherit global defaults and Has nitrous. Boot a third-person Player beside a car, press Use, verify `VEH avatar 0` while seated, then exit and check visibility/door placement. A blocker behind the chase boom must reduce `boom100` below `want100`; removing it must restore the distance. Include rotated boxes, terrain, collision-none, self exclusion and far/rear views. The clearance pass runs after shake, not only on the unshaken rig.

## Vehicle Editor UX

**Cause:** the preview, audition and cost tabs each need an isolated fixture.
**Fix:** use an isolated vehicle-playground copy. Assert three car rows, `Vehicle preview canvas`, `Spin wheels`, and `Listen to engine`; set Speed above/below the fast-wheel threshold, orbit/zoom, capture normal/fast/steered previews for all three cars, and check audition stays checked (device/decoder opened), updates with Revs, and stops on selection/close. In Cost, attempting to change a triangle slider before the advanced checkbox must leave the saved budget unchanged; unlock and verify edit/save/reload. Reflection texture choices are thumbnails with Dynamic sky and Import PNG. Host audition differs from SPU2 ADPCM; a native build verifies the new idle WAV encodes as a loop.

## Vehicle inheritance

**Cause:** each override must add only its own field key.
**Fix:** create a new car, save and assert no `tuningOverrides` plus resolved global fields. Edit a single Driving/Driver/Sounds parameter and check only its field key is added. Change a global sibling field and check it reaches all cars while the edited field survives. Use defaults must remove only the current section's overrides, then survive save/reload. Pre-83 local/group files must preserve authored values, including empty sounds and zero volumes.

## Tyre reduction

**Cause:** wheel detection and the continuous outer tyre are separate properties.
**Fix:** test `@auto` at 32 triangles on all three models, inspect actual cost and the continuous outer tyre, then test authored `wheel_blur` at speed and from both sides. None/`@auto` must still detect exactly four wheels when the source GLB contains `wheel_blur`. Verify original GLB mesh/material/image prefixes remain unchanged and repeat authoring is byte-identical.

## Vehicle sound import

**Cause:** loops vs one-shots are decided by the filename and the encoder header.
**Fix:** select an ordinary WAV without `-loop.wav` in idle/high-rev and tyre-squeal dropdowns, save/reload and audition. Headlights must appear only in Effects; its Use defaults resets the Effects override without resetting Sounds. Verify optional `vehicle_sound_loops.gen.txt` is rewritten/deduplicated and deleted when the last ordinary continuous role is removed. Check native and Docker encoder header byte 6 through one-shot -> loop -> one-shot while WAV mtime is unchanged; gear-shift-only sounds stay one-shots, suffix loops stay loops. Include paths with spaces and an already-fresh encoded cache.

## The static-batch twin oracle is stale

**Cause:** `examples/vehicle-playground/authoring/verify-batch-twins.py` reads `src/templates.cpp`, but `buildStaticBatchList` now lives in `src/game_templates.inc` (included by `templates.cpp`), so it exits `FAIL: buildStaticBatchList not found in src/templates.cpp`.
**Fix:** point it at `game_templates.inc` before relying on it. Stage-1 eligibility needs no harness: `batchStatic` in generated `inc/scene_data.hpp` (87 of 142) and the game's `Static batching: eligible 87, solo 22` / `65 objects in 48 batches`. A MISSING-TEXTURE case is expected: `acquireTexture` returns nullptr, so missing textures batch with untextured primitives — a "fixing" twin is wrong.

## Sound effect conversion (1.157.0)

**Cause:** a conversion error must fail the build, and a header check alone missed adpenc's corrupt stereo reader.
**Fix:** `wavconvert::soundIssue` requires canonical mono PCM16/22050; import and Project > Sounds > Convert downmix too. Runner calls `wavconvert::bakeSounds` AFTER texbake and BEFORE either build backend, preparing `.res-baked/sfx` without modifying `res/sfx`. Both encoders read this mirror, check cached APCM byte 5 for mono as well as byte 6 for loop intent, and preserve the source path for role matching. Test stereo PCM16, PCM24, float, extended headers and an odd metadata chunk; verify duration, source hashes, incremental mtime stability and ADPCM mono/loop headers in BOTH backends. Decode the actual ADPCM and compare the waveform to the converted WAV (header checks alone missed `fread(wave+i, 2, ...)` advancing by one byte). Music conversion stays stereo unless its own mono option is chosen.
