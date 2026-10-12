# troubleshoot/tyra-engine-dev.md

The index of this skill's symptom files, plus the entries most likely to be hit.
Symptom first. The "Hard-won pitfalls (dead ends already explored — don't repeat
them)" from the old monolith now live in the files below; every symptom area has
its own file and the table routes to it. (A note on `PROGRESS 123` citations: they
point at numbered entries of `PROGRESS.md`, retired at ~15 800 lines; the file is in
git history and `docs/backlog.md` has the recipe. New work records itself in its
commit message and PR body instead.)

| Symptom area | File |
|---|---|
| Fork policy, build/flag/toolchain, stale `libtyra.a`, vclpp/CRLF, `Makefile.base`, audsrv source fork | `troubleshoot/tyra-engine-dev-build.md` |
| GS raster state, texture wrap, dither, alpha, flicker filter, 2D origin | `troubleshoot/tyra-engine-dev-raster.md` |
| Post-fx passes, portals, shadow/alpha-mask raster brackets | `troubleshoot/tyra-engine-dev-postfx.md` |
| Dynamic lights, the spot cone, DIMX, signed RGB SH, skin reuse | `troubleshoot/tyra-engine-dev-lighting.md` |
| VRAM allocator, texture footprint, hybrid colour depth, framebuffer PSM, env-map opt-in, triple buffering | `troubleshoot/tyra-engine-dev-vram.md` |
| Display modes (DTV/`InterlacedField`/`Pal576i`), fps counters, vblank queue, black frames | `troubleshoot/tyra-engine-dev-display.md` |
| Packages, coplanar passes, bbox cache, triangle strips, AABB classification | `troubleshoot/tyra-engine-dev-packages.md` |
| VIF1 submission queue, DMA REF lifetime, the slot pool, the baked VIF stream | `troubleshoot/tyra-engine-dev-vif1.md` |
| VU1/VU0 microcode, the host simulator, the packet tap, assemblers, micro memory | `troubleshoot/tyra-engine-dev-vu1.md` |
| VU1 clip family, billboards, micro-memory/tag rules | `troubleshoot/tyra-engine-dev-clip.md` |
| Audio playback, ADPCM, SPU2 reverb, WAV, vehicle sound loops | `troubleshoot/tyra-engine-dev-audio.md` |
| Paths, loaders, missing assets, soft errors | `troubleshoot/tyra-engine-dev-files.md` |
| Profiling, frame counters, the free-RAM probe, hardware timeline, A/B method | `troubleshoot/tyra-engine-dev-measurement.md` |
| Static submission, batching, EE cost, VU1/DMA cache cost | `troubleshoot/tyra-engine-dev-submission.md` |
| Retained static commands, `STAPIPMISS` | `troubleshoot/tyra-engine-dev-retained.md` |
| Baked VIF stream, content version, acceptance gate | `troubleshoot/tyra-engine-dev-baked.md` |
| BLSS: plain mode, the twin, per-scene, the raster redirect | `troubleshoot/tyra-engine-dev-blss.md` |
| BLSS proxies, emitter bags, the spatial split, the instruments | `troubleshoot/tyra-engine-dev-blss-proxy.md` |
| BLSS EE cost, the libm cuts, break-even, the heavy-overdraw regime | `troubleshoot/tyra-engine-dev-blss-cost.md` |
| BLSS shipped net, jitter, interlock, CV training | `troubleshoot/tyra-engine-dev-blss-net.md` |
| Assert/soft error/crash handler, the one-probe discipline, devkit probes | `troubleshoot/tyra-engine-dev-debug.md` |
| USB keyboard/mouse, the virtual pad, replay input | `troubleshoot/tyra-engine-dev-input.md` |
| Collision mesh, raycasts, physics | `troubleshoot/tyra-engine-dev-physics.md` |

## Objects render twice / giant smeared polygons

**Cause:** a bag submitted with `frustumCulling = None`. Off-screen geometry wraps
the GS 4096-px raster window. PCSX2's HW renderer often *masks* this; the SW
renderer and real hardware show it. This was the root cause of a long-standing
corruption bug — not the clipper patches (all were bisected; even pure upstream
reproduced it).
**Fix:** never submit a bag with `frustumCulling = None`. (The one legitimate
`None` is the StaPip billboard family, whose per-quad ADC replaces the wrap
protection.)

## Grey wedges bleed from under a reflective object / baked shadows fight z-index with the ground

**Cause:** two coplanar passes over ONE vertex array deriving different package
sizes from their program class (a reflective sphere with no `map_Kd`, anything under
the baked lightmap, an untextured terrain chunk under its layer passes), so
`StaPipCore::render` splits the array at different boundaries and routes one package
`as_is` and another through the EE clipper — differing in the last bits of z. They
then dither-fight.
**Fix:** set `StaPipBag::packageSize` (0 = derive) to the **MINIMUM** over an
object's passes (`pinPackageSize` in `templates.cpp`). Pinning a class above its
own derived size overflows its VU1 buffer. Pinning also measured **+4–6 %** frame
rate (126 → 133 FPS, vsync-off PCSX2).

## Dashed dark/light bands on a double-sided surface

**Cause:** two exactly coplanar faces. Neither the StaPip VU1 programs nor the GS
reject back faces (the "cull" family is about the frustum, not winding), so a
double-sided surface whose front and back share one plane dither-fights itself.
The Plane primitive hit this (its darker underside vs the lit top).
**Fix:** a small offset between the two faces (`addPlane` in `templates.cpp` /
`unitPlane` in `primmesh.cpp`, 0.01 local units). Same rule for any hand-built
double-sided geometry.

## The ground texture draws once around the origin, then streaks to the horizon

**Cause:** `GS_REG_CLAMP` left at CLAMP. ps2sdk's `draw_setup_environment()` ends
with *"Setup whole texture clamping"* and programs CLAMP/CLAMP at init, so every
3D mesh sampled clamped. Harmless for STs in 0..1; ruinous for terrain whose STs
are world position × tile factor.
**Fix:** `Path3::clearScreen` writes REPEAT into the PATH3 packet that always
precedes the frame's 3D pass, and `initDrawingEnvironment` writes it too. Settle
this class in one boot by swapping the ground texture for four saturated quadrant
colours with a black border.

## Every 4-bit palettised texture in the scene drew nothing / "BLSS deletes the terrain"

**Cause:** `zBuffer.mask == 0` for the whole run, so full-resolution passes stamped
depth 512×448 words past `ZBP` into the texture heap — the depth landed on the
8×2 CLUT, and `ATEST NOTEQUAL`/`AREF 0` discarded it. 24-bit textures kept drawing,
which made it look like a CLUT-descriptor bug. Broke TWICE: `configure()` assigned
the flag before the rebuild that clears it, and `RendererCorePostFx::apply()`'s
restore block hardcoded a mask of 0.
**Fix:** `allocateVramBuffers` DERIVES `zBuffer.mask` from the allocation it just
made; never assign it from a caller. **Grep `GS_SET_ZBUF` before adding a
full-screen pass**, and never write the mask from one. A second missing thing
(the crosshair sprite) is the tell.

## A `.vclpp` macro body vanished (frustum clip check silently absent)

**Cause:** anything between a `#macro` line and its first instruction — even a
comment — makes `vclpp` expand the macro to **nothing**: no error, exit 0, every
caller compiles green with the instructions absent. A note added inside
`PerformClipCheck` (`vcl_sml.i`) in `93a7657` removed the frustum clip check from
`mcpip_cull` until it was found on 2026-08-04.
**Fix:** put notes ABOVE the `#macro` line, in the `;//` block. Check expansion
with `vclpp ... | grep -c clipw`; a program ~27 instructions shorter lost a body.

## `relocation truncated to fit: R_MIPS_GPREL16 against Tyra::Info::writeLogsToFile`

**Cause:** the engine outgrew the 16-bit GP-relative `.sdata`/`.sbss` window. The
message names a one-byte bool, not the section that overflowed, so it reads as a
missing symbol. The DEBUG profile hits it first.
**Fix:** keep **`-G0`** in `vendor/tyra/Makefile.base`; a Docker flag change needs
`--build --rebuild` or you get objects compiled both ways and the same error from
the stale half.

## A Docker build says `Build OK` but the fix changed nothing

**Cause:** a failed engine `make` left the PREVIOUS `libtyra.a` in the volume with
the new sources synced beside it, so the next build skipped make and linked the
stale library (fixed in 1.81.1).
**Fix:** the Runner now deletes `libtyra.a` when make fails. If a build prints
`Build OK` without an `ar rcs bin/libtyra.a` line after you changed engine
sources, that is the symptom.

## The HUD sits too high in HD / "the fps counter in HD is drawn above the screen"

**Cause:** upstream pinned the sprite draw's vertical `XYOFFSET` to a bare
`SCREEN_CENTER`, only the top of the picture while the buffer is the stock 448
rows. On `HiDef1080i` (448x540) the HUD sat `(540 - 448) / 2 = 46` rows too high;
`Pal576i` (512x512) the same, 32 rows.
**Fix:** centre the authored 448-row space in `getRenderHeightF()` (zero in the
448-row modes). HALVE the space for `InterlacedField`, or every sprite jumps 112
rows up. Verify by BOOTING each mode.

## Console-only moire / rectangles the shape of whatever wrote alpha last

**Cause:** the interlaced flicker filter reads the frame's ALPHA.
ps2sdk's `graph_set_framebuffer_filtered` leaves `PMODE.MMOD = 0`, so the two read
circuits blend by the displayed buffer's per-pixel alpha. PCSX2 does not model it;
only `--capture-frame --alpha` and the television do.
**Fix:** `presentFrameBuffer` forces `MMOD = 1, ALP = 0x80` (1.70.3); keep it that
way.
