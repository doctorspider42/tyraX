# tyra-editor-dev — vehicle tuning, HUD font, live preview, sound

From `## Vehicle HUD font preparation (1.150.1)`, `## Vehicle tuning inheritance
(1.153.0)` and the vehicle sound-conversion note. See `docs/vehicles.md`.

## Vehicle HUD font preparation (1.150.1)
**Cause/rule:** `fontGlyphSprite` in the shared generated helpers owns one
persistent sprite and atlas link per font. `drawFontText` uses it; `setupVehicles`,
called inside `loadScene`, also prepares valid HUD fonts for driveable instances
and performs their first normal evictable `useTexture`.
**Fix:** AI-only/HUD-disabled instances do not trigger this prewarm. Scene
revisits must reuse the same font texture. The helper and vehicle runtime are
shared by the orbit and FPP generators. Runtime inline icons remain lazy until a
resolved, nonempty icon is drawn in the foreground pass; numeric vehicle strings
and unknown tokens must not load the sheet. Preserve glyph metrics, centering,
shadow and widescreen squeeze. This fixes HUD cold asset work, not first-visible
3D caches or steady FPS.

## Vehicle tuning inheritance (1.153.0)
**Cause/rule:** `Project::vehicleDefaults` is a singleton in `Section::Vehicles`,
serialized with `writeVehicleArray`/`readVehicleArray`. `VehicleDef::inheritDefaults`
is true for new definitions; pre-v85 files without an explicit inheritance flag
stay local, while vehicles-branch files with an explicit flag retain it. Both
branches reused v82/v83, so v85 is their unified format; normalization preserves
tuning. `visitVehicleTuning` is the shared registry for all tunable fields and
section ownership. `tuningOverrides` contains individual field keys; historical
group keys expand during resolution.
**Fix:** UI controls edit directly. Diff against the resolved pre-frame definition
creates only the changed keys. Section Use defaults clears its keys — skip those
keys in the same-frame diff or the reset recreates overrides. Geometry keys stay
local (`vehicleGeometryKey`). `applyVehicleDefaults` resolves at section-read,
commit and codegen. Include global asset refs in assetbrowser and particle-effect
renaming. Global UI reuses tuning tabs without import/test-drive tabs.
`collideVehicleCamera` runs in both game tails after shake and separation,
excluding the driven chassis.

## Vehicle Editor live preview
**Cause/rule:** it uses a separate lazily initialized Viewport owned by App. It
reuses the cached vehbake models, previews wheel spin/steering and the fast-wheel
hysteresis, and never moves scene entities. `audiopreview::EngineLoop` decodes
samples off the audio thread and owns host audition; atomics carry
pitch/revs/volume.
**Fix:** stop audio before destroying callback state and shut down the preview
before the GL context. Replacing/clearing vehicle draws must destroy their
VAO/VBOs; live preview drops its private texture cache on bake changes so reused
palette paths show edited colours. Triangle budgets require an explicit
editor-only unlock; reflection maps use project texture thumbnails.

## Wheel simplification
**Cause/rule:** it protects the outer tyre band; tiny budgets are soft and Import
details reports actual cost. `wheel_blur` is a reserved auxiliary node, excluded
from detection in every mode, including None and Automatic.
**Fix:** verify both automatic and authored modes at swap speed; body decimation
is independent.

## Vehicle sound selectors
**Cause/rule:** they show all project WAVs. Continuous roles (`engineSound`,
`engineHighSound`, `screechSound`) declare encoded loop intent through optional
`inc/vehicle_sound_loops.gen.txt`, generated from resolved definitions.
**Fix:** refresh must rewrite it and remove stale intent when roles disappear.
Legacy `-loop.wav` still works. Headlights belongs to Effects for both UI and Use
defaults; expand historical sounds group overrides to headlights too when reading
old data.

## Sound effect conversion (1.157.0)
**Cause/rule:** `wavconvert::soundIssue` requires canonical mono PCM16/22050;
import and Project > Sounds > Convert downmix too. Runner calls
`wavconvert::bakeSounds` AFTER texbake and BEFORE either build backend, preparing
`.res-baked/sfx` without modifying `res/sfx`. Both encoders read this mirror,
check cached APCM byte 5 for mono as well as byte 6 for loop intent, and preserve
the source path for role matching.
**Fix:** a conversion error must fail the build. Test stereo PCM16, PCM24, float,
extended headers and an odd metadata chunk; verify duration, source hashes,
incremental mtime stability and ADPCM mono/loop headers in BOTH backends. Decode
the actual ADPCM and compare the waveform to the converted WAV: header checks
alone missed adpenc's corrupt stereo reader (`fread(wave+i, 2, ...)` advances by
one byte). Music conversion stays stereo unless its own mono option is chosen.

## Reference facts (upstream `vehicles-and-roads.md`, tuning/preview/sound)
- `Project::vehicleDefaults` is a singleton in `Section::Vehicles` (`writeVehicleArray`/`readVehicleArray`). `VehicleDef::inheritDefaults` true for new definitions; pre-v85 files without an explicit flag stay local. `visitVehicleTuning` is the shared registry of tunable fields + section ownership; `tuningOverrides` holds changed field keys (historical group keys expand; old "sounds" group expands to headlights); geometry keys stay local (`vehicleGeometryKey`); `applyVehicleDefaults` resolves at section-read, commit and codegen.
- Live preview = separate lazily created `Viewport` owned by App (reuses cached vehbake models; wheel spin/steering; drops its private texture cache on bake changes). `audiopreview::EngineLoop` owns host audition. Sound selectors list all project WAVs; continuous roles declare loop intent via `inc/vehicle_sound_loops.gen.txt` (always overwritten); legacy `-loop.wav` works.
