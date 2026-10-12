# troubleshoot/tyra-testing-impostors.md

Eight-view foliage impostors (and impostor picking). Symptom first.

## Verifying the impostor data path

**Cause:** an impostor is a data plus rendering feature, and the fields can be dropped silently.
**Fix:** use `examples/impostor-grove` (its README has reproducible generators). Verify `--resave` retains `impostor`, `impostorDistance` and `impostorBillboard`, `--refresh-gen` collects both model paths, then build/run a short-path copy. Show profiler logs representation transitions as `IMPOSTOR object=... far=...`; walk towards and away from a tree. Compare a frozen-camera control with every distance set to zero. Verify binary alpha and deterministic atlas bytes after regenerating assets. Rendering changes need a screenshot, not just the model table. Host fixture C++ helpers belong under `authoring/`: a root-level .cpp can expand the Makefile's unquoted find glob and leave SOURCES empty (`undefined reference to main`).

## Eight-view captures

**Cause:** a multi-view billboard changes view sector as the camera moves AROUND the object.
**Fix:** compare a frozen single-tree near/far pair and orbit it: `IMPOSTOR VIEW` logs sector changes with Show profiler enabled. Verify the 512x256 atlas, all eight ordered parts, yaw rotation and fallback for tilted transforms.

## Universal capture (Bake impostor)

**Cause:** the Properties path must assign to any multi-material OBJ.
**Fix:** select a non-tree multi-material OBJ, activate the Properties tab, click Bake impostor, save/reopen and inspect assignment plus alpha/Kd. Check material overrides and missing textures/reflection failures; no assignment should change on failure. The grove now includes Waystone for this path. Close floating tool windows (or remove their open flag in the scratch layout) before scripted scene selection; an overlapping Tree Generator can intercept the click.

## Selection: list vs viewport

**Cause:** picking must agree between the list and the viewport.
**Fix:** verify list selection shows the full model box, then a viewport click hits the same model. A frozen orbit camera aimed inside an empty part of a model AABB tests fallback. Put an alpha-cutout card in front of a smaller prop to check that transparent texels do not steal a surface hit. Test a distant billboard and rotated/scaled instances; no PS2 rebuild is required for editor-only picking.

## Injecting a viewport click

**Cause:** scene rows are not checkable widgets.
**Fix:** the viewport image is named `Viewport canvas` for `--ui-script` (1.74.1). Set a scratch project camera to aim at the test point, then `click 'Viewport canvas'` then `key ctrl+s` and check `editor.selectedObject` in the scratch manifest. This injects a real viewport click without OS focus.

Verified on Windows for 1.74.1: grove scene-list selection shows the whole Waystone bounds; a viewport click in its empty AABB selects it; a transparent foreground quad lets the mesh behind win, whereas its opaque version wins itself; a distant Waystone impostor remains clickable.

## Grove ground regression

**Cause:** a floor material's scale counts repeats per world unit, not over the whole map.
**Fix:** freeze the player at (48, 0, 48), heading -135, pitch -12. The old floor material (-s 16 16) loses detail there; -s 0.5 0.5 restores it in PCSX2 software rendering. Compare a fixed ground crop; do not infer a missing texture binding from a flat colour alone.

## GPU impostors

**Cause:** the CPU/GPU atlases must agree, and an invalid count must fail before output.
**Fix:** `--bake-impostor PROJECT MODEL OUTPUT_STEM 4|8|16 [--gpu]` prints backend and total time without assigning an object. Compare CPU/GPU atlases for all counts on a cutout tree and multi-material Waystone: same dimensions, binary alpha, close silhouette masks, matching Kd/interior colours; GL edge coverage is not byte-identical. Invalid counts fail before output. Verify Capture views in Properties only changes saved `impostorViews` after a successful bake, resave an old eight-view object, and boot mixed 4/8/16 assets in PCSX2 while orbiting. Run `--gi-gpu-check` after changes to the shared bake context and inspect the viewport after GPU baking to catch context restoration bugs.

Verified 1.75.0 on Windows: model UI baked/saved 16 GPU views, Tree Generator baked/saved 4 GPU views; choosing 16 without baking kept a legacy object's 8. The capture oracle passed all counts (Waystone IoU 1.0, oak >0.999); forced CPU fallback was byte-identical to CPU PNG output and invalid counts wrote nothing. GI's shared-context oracle still agreed (0.0022% relative mean error). The mixed 4/8/16 grove built and booted in PCSX2 software mode; Remote Pad motion produced capture-sector updates without allocation failures. Higher atlas counts consume VRAM: the tested mixed scene had about 35 KiB free after moving, with evictions during motion, so do not describe larger view counts as free.
