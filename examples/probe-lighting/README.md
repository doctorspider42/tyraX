# Probe lighting — animated receivers under directional baked GI

Walk from a blue-sky courtyard into a roofed room with warm and cool side
sources. The cat avatar and three white, twisting meshes read the existing
RGB L1 probe grid. Their light direction now follows the local field rather
than the sun. The cylinder at the back is an explicitly dynamic-lit rigid
reference, using the same dominant-direction projection.

Left stick walks, right stick orbits, Cross jumps. Walk through the doorway,
approach each side source, and turn: the light should stay on the side facing
the source. The side panels are **baked emissive area lights**, not live point
lights; the coloured walls also contribute bounce. This intentionally gives
the directional test a clear signal, rather than claiming all colour is bounce.

The cached `.res-baked/gi/scene0.gi` ships with the example. Open and build;
rebake after scene edits with the GI window or:

```text
tyrax-editor --bake-gi examples/probe-lighting --gpu
```

The geometry is deliberately simple. Bloom, AO, live shadows and terrain are
off so they cannot obscure the probe response. The floor is a solid box.
The cat and wobbler are the existing gi-showcase assets; the wobbler's material
was made neutral to expose the incoming light's colour.

## What this demonstrates, and what it does not

The GPU performs the offline bake. PS2 does one weighted probe lookup per
visible animated instance per render pass, then uses its existing VU1 lighting.
Instances may share a pose, but retain separate lighting bags. The normal
matrix removes uniform instance scale, so a larger avatar does not receive a
stronger directional term merely because it is larger.

This is one dominant direction plus RGB ambient, **not full PRT**. Opposing
coloured directions cannot all survive that reduction, and the back side
retains ambient rather than reconstructing signed SH. No animated self-shadow
or runtime bounce tracing is implied. See
[global illumination](../../docs/global-illumination.md#directional-lighting-on-animated-receivers).

## Reproducible old/new comparison

Copy the project to a short scratch path and refresh generated files. Then run:

```text
python examples/probe-lighting/authoring/make_comparison.py SCRATCH_PROJECT
tyrax-editor --build SCRATCH_PROJECT --run
```

The helper takes ownership of the scratch `src/terrain_game.cpp`, freezes poses
and makes **L1 toggle the old sun projection / new probe direction**. Both arms
retain the corrected normal matrix to isolate the direction change. Start in
the new mode. Move into the room, release the sticks, capture a frame, press
L1 and capture again. `PROBECOMPARE` in `bin/log.txt` records the mode and each
instance's sampled colours/direction; the camera and pose stay identical.
The helper refuses to alter this checked-in example or already-owned source.
Restore the generated source in the scratch copy before testing animation.

For a no-GI control, disable GI in another scratch copy and build. For a normal
transform check, compare the same receiver at uniform scales 1 and 2 under
the same field: its light direction and normal-matrix column lengths must
remain unchanged. Nonuniform scale/shear is not covered by that guarantee.


## Verification

Windows Release editor build and PS2 Docker build passed. The demo boots in
PCSX2 software rendering and was driven through the doorway with Remote Pad.
Both viewport shading modes were opened and captured. A separate GI-disabled
copy also built and booted with classic scene lighting. A host C++ check of
the generated projection covered a side source, a reversed blue source, a
uniform field and negative ambient clamping.

In the fixed-pose L1 toggle comparison, the warm receiver's directional RGB
changed from `(0, 0.018, 0.078)` along the sun to `(0.244, 0, 0)` along the
local field, while its ambient stayed `(0.408, 0.129, 0.178)`. The warm/cool
instances retained opposite X directions (`-0.887` / `+0.741`). The image
comparison changed 9,452 pixels in the room crop; returning to the new mode
reproduced that crop byte-for-byte. This is a modest directional correction,
not a new lighting bake or an exposure boost.

| Old sun projection | Local probe direction |
| --- | --- |
| ![Old](../../docs/img/probe-lighting-before.png) | ![New](../../docs/img/probe-lighting-after.png) |

![PS2 shading preview in the editor](../../docs/img/probe-lighting-editor.png)

The game's PAL counter read 50 FPS during these captures; that capped counter
is not an incremental EE timing measurement. No physical PS2 or Linux run was
performed. Full signed SH, multiple chromatic directions and nonuniform-scale
normal correction remain future work.
