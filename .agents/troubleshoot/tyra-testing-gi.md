# troubleshoot/tyra-testing-gi.md

Animated directional GI, and the animation / RGB SH regression checks. Symptom first.

## Verifying animated directional GI

**Cause:** a lighting change can look plausible while the per-instance directions are shared or the mode is wrong.
**Fix:** use `examples/probe-lighting` in a scratch directory. Bake with `--bake-gi DIR --gpu`, build and boot in PCSX2 software mode, then walk from courtyard through the doorway with Remote Pad. Compare frozen poses with the old dominant-lobe path, keeping the same baked table. Check two pose-sharing instances near the opposite side lights: their bags must retain separate directions. Check yaw and uniform scale, GI disabled/dead probes, and both viewport shading modes. The current signed RGB SH mode must retain independent coloured directions and darken back-facing normals; L2 and surface-transfer PRT are not implemented.

Verified on Windows: probe-lighting GPU bake, Docker/PCSX2 software boot, fixed-pose L1 A/B with a byte-identical return-to-new control, both editor shading modes, and a separately built/booted GI-disabled fallback. The example README records the sampled coefficients and limits; these are not hardware performance measurements.

## Animation and RGB SH regression checks

**Cause:** `--vu-check` covers microcode, not animation timings, and a single arithmetic test cannot expose a dangling REF.
**Fix:** `--vu-check` includes signed chromatic coefficients in the randomized comparisons and a numeric RGB oracle (opposing normals, rotation, both lighting modes, negative output and saturation). This still needs a PS2 Docker build and a software-renderer walk through `examples/probe-lighting`.

For animation timings enable `TYRA_SKEL_PROFILE` in `skel_instance.hpp`, rebuild, and inspect per-instance `SKELTIME`. Measure ordinary playback, not the frozen lighting comparison. For LOD stress, use three humanoids at meshLod 1.5 in a scratch copy, walk through the doorway and force all three tiers in generated code if camera distances do not cross each threshold. Distinguish a hang that is reproduced from an old observation that is not.

## A restored generated file is not recompiled

**Cause:** when restoring generated C++ with `Copy-Item`, `Copy-Item` preserves old timestamps, and `make` may otherwise relink the previous instrumented object despite different source.
**Fix:** touch its `LastWriteTime` or clean the scratch game objects.

## A lighting DMA lifetime regression

**Cause:** an arithmetic VU test and a single screenshot cannot expose a dangling REF to stack data; `--vu-check` validates microcode, not the EE source pointers.
**Fix:** lighting DMA lifetime regressions need temporal checks. Hold the camera/pose fixed and compare a sequence of frames (excluding the FPS HUD), then restore normal animation and walk the scene. Exercise the actual packet submission.
