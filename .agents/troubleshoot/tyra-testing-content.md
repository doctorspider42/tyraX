# troubleshoot/tyra-testing-content.md

Measuring a CONTENT change, where the arm is the editor (1.105). Symptom first.

## A codegen change has no engine macro to flip

**Cause:** a codegen change has no engine macro to flip, so the two arms are two EDITOR binaries generating the same example.
**Fix:** build both from the worktree under test — `./build.ps1`, copy the exe aside, `git checkout <base> -- src/<the changed files>`, build again, copy aside, restore — and record BOTH hashes. `examples/vehicle-playground/authoring/road-lod-2026-09-16/build-arm.ps1` takes the editor as a parameter for exactly this reason, and holds the baked asset tree constant across the arms so the only difference is generated code.

## Three things that round taught

**Cause:** each is cheap to repeat.
**Fix:**
- **`--profile quiet-debug`, not `debug`.** The live tools' `host:` pollers cost 6.44 ms of `work` and put +3.5 to +4.4 ms outliers inside `update`. With them off the repeatability floor was **0.016 ms of `work`**, eight times tighter than the same rig at `debug`.
- **A map-wide count is not a frame.** The road reduction removed 9 798 triangles from the district and **614** from the garage-day frame. Measure the pose that is slow, not the inventory.
- **A quality criterion can usually be turned into a number instead of a screenshot.** "Coarse terrain must not bury a road" became the worst rise of the coarse ground above the road's own lift, at every dense sample of every road (`terrain-lod-burial.py`); "no distracting LOD transitions" became the worst mesh disagreement converted into pixels at the distance the band switches (`terrain-lod-step.py`). Both run in seconds with no console.

## Textured vehicles

**Cause:** palette UV fixup and baked textures have specific ownership.
**Fix:** `vehbake::Result::textures` holds bin-relative names and PNG bytes for source images; `bakeProject` and `vehicleRefreshBake` both write them. The viewport resolves `ModelPart::bakedTextureRel` per draw, never a cached GL name. Palette UV fixup must only visit palette parts (real UV V=-1 is valid). A wheel/body image and Kd match permits textured wheels in body distance tiers. Use the GGBot GLB in Motor District to check the PNG references, actual in-game texture, four detected wheels and the distant wheel silhouette. Rigid nodes sharing one material may collapse to one dominant owner during import; the example preparation retains one material slot per wheel.

## Shared dynamic env sampling

**Cause:** the level capture's world-up differs from the pitched chase camera's up.
**Fix:** shared dynamic env sampling must use the LEVEL capture's world-up, not the pitched chase camera's up. Static sphere-map images keep the view basis and reflected-ray probes keep their own basis. The viewport `envSt` shader mirrors this distinction. To diagnose a reflection, inspect the env target separately from the final car: populated target + unchanged car in a scenery hide/show comparison is a sampling problem, not proof that the capture failed.

## Mixed vehicle definitions

**Cause:** a palette car and a textured car must never sample through the last vehicle's image.
**Fix:** mixed vehicle definitions keep separate runtime wheel batches. Verify both cars together at near range; far tiers carry their own baked wheels.

## Wheel batches

**Cause:** PATH1 DMA can still read one definition while the next is being prepared.
**Fix:** wheel batches retain their per-definition position, colour and UV buffers until scene unload. The conservative CPU reject must cover the actual rig (wheel mesh radius, track, wheelbase, full suspension travel, steer, spin and body attitude), and must use the active camera/frustum plus the body's visibility, draw-distance and split-band gates. Check a near/far LOD crossing has neither duplicate nor missing wheels, then capture the separate `Wheels` render-cost row; it is a diagnostic phase, not a hardware FPS result.
