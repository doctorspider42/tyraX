---
name: tyra-engine-dev
description: >
  Rules for editing the in-tree Tyra PS2 engine fork in vendor/tyra — the
  renderer/clipper/VU1 pipeline, audio (audsrv), VRAM and GS state, file loading
  over the PS2 host filesystem, and how an engine change reaches a running game
  through the native and Docker builds. Load it before touching ANY file under
  vendor/tyra, before PS2-side rendering, clipping, VU1/VU0 microprograms,
  texture/VRAM or audio work, and when diagnosing in-game symptoms such as
  rendering corruption, giant smeared polygons, crackling or wrong-speed audio,
  missing textures, or "Failed to load" asserts. Each rule points at
  troubleshoot/tyra-engine-dev*.md for the expensive dead ends behind it.
---

# Working on the in-tree Tyra engine fork

`vendor/tyra/engine` is a **versioned fork** of [h4570/tyra](https://github.com/h4570/tyra)
(Apache 2.0, forked at upstream commit `9273416`), maintained directly in this
repo. These are the rules an engine edit must not break; the failures each rule
prevents are in `../../troubleshoot/tyra-engine-dev*.md` (see "When you are stuck").

## Fork policy

- Mark every departure from upstream with a `Modified by TyraX` comment near the
  top of the file (existing examples: `audio_song.cpp`, `stapip_clipper.cpp`,
  `planes_clip_algorithm.cpp`, `stapip_qbuffer.cpp`, `render_bbox.cpp`; VU sources
  use `;// TyraX` notes, e.g. `vcl_sml.i`).
- **LF line endings only** under `vendor/tyra/**`, enforced by `.gitattributes`:
  the `vclpp` VU1 preprocessor chokes on CRLF (it fails only on files with a
  `#macro`, and names a fine line, so it reads as a source error). Editing tools
  respect this; a **script** may not — Python's `Path.write_text()` turns `\n`
  back into CRLF on Windows. Write bytes, or check `grep -c $'\r'` afterwards.
- The rest of `vendor/` (imgui, glfw, imguizmo, imnodes, stb, tyra's non-engine
  parts) is git-ignored and cloned by `setup.ps1` / `setup.sh` — never edit those.

## How an engine change reaches the game

You never rebuild the engine by hand: the editor's Runner (`src/runner.cpp`) does it on
every game build (F5 or `tyrax-editor.exe --build <projectDir>`). The native default syncs
`vendor/tyra/engine` into a user cache and rebuilds with pinned PS2DEV/OpenVCL; the Docker
fallback bind-mounts `vendor/tyra` read-only at `/engine-src`, checksum-rsyncs it into the
shared `tyra-engine-<hash>` volume (`external: true`), rebuilds `libtyra` once if anything
changed and drops the ELF so it relinks.

- The **VU1 microprograms** sit outside make's dependency tracking, so the Runner
  force-rebuilds them when a changed `.vclpp/.vcl/.vsm/.i/.h` is rsynced.
  **If you change what a VU source may include, widen that extension list** — a missed
  extension means stale microprograms, which look like a renderer bug (**109 s** the set).
- **Build > Rebuild** (`--build --rebuild`) throws away the whole compiled
  engine, VU1 objects included, and rebuilds it from source.
- A failed engine `make` used to leave the **previous `libtyra.a`** in the volume
  (fixed in 1.81.1). If a Docker build says `Build OK` without an
  `ar rcs bin/libtyra.a` line after engine sources changed, that is the symptom.
- `vendor/tyra/Makefile.base` is shared by the engine and every generated game.
  Keep **`-G0`** (the engine outgrew the 16-bit GP-relative `.sdata`/`.sbss`
  window; without it you get `relocation truncated to fit: R_MIPS_GPREL16` naming
  a one-byte bool), single-pass `-MMD -MP`, `| directories` order-only prereqs,
  `rsync -ru` for the resource copy minus `RESDIR_SKIP`, and **`src/vu*` (the
  `src/vu*/` and `src/vu0/` host-code dirs) excluded from `SOURCES`** (host C++, not
  PS2 code) — a new host-code directory must be added to that `find` exclusion.
- Docker's Runner only rebuilds the engine when engine SOURCES changed, so a
  **compiler-flag change needs `--build --rebuild`**; engine volumes are shared
  per source checkout, so another checkout needs its own rebuild.
- `RESDIR_SKIP` excludes `.res-baked`'s `gi/` and `shadow/` caches from the
  resource copy (no console can read them, and `isoexport` would burn them onto
  the disc). A new host-only cache joins that list **and** the matching skip in
  `src/isoexport.cpp`.
- `vendor/tyra/audsrv/` is a **source fork**, not a blob: changing its sources
  requires re-running `build.sh`/`build.ps1` and committing `bin/` in the same
  commit (nothing in the game build compiles audsrv). The from-source image
  compiles the EE half itself and skips the Runner's overlay.

## Engine layout (path → what it owns)

| Path | Owns |
|---|---|
| `vendor/tyra/engine/{inc,src}/{audio,debug,file,info,irx,loaders,math,pad,physics,renderer,thread,time}` | mirrored subsystems; `renderer` is the big one (2D, 3D static/dynamic, GS/VU1 core) |
| `renderer/**/*.vclpp, *.vcl, *.i` | VU1 microprograms, preprocessed by `vclpp` inside the container |
| `renderer/core/blss/` | neural upscaler console half (`RendererCoreBlss`) |
| `renderer/core/paths/path1/vif1_queue.*` | `Vif1Queue` (VIF1 submission queue) |
| `renderer/core/splitview/` | `RendererCoreSplitView` (split-screen raster bracket) |
| `renderer/rt/vu0_raytracer.*` | the only VU0 microprogram (raytraced reflections) |
| `renderer/models/unique_id.hpp` | `generateUniqueId()` — the object-id namespace |
| `physics/` | `CollisionMesh`, `Ray::intersectTriangle` |
| `debug/debug.hpp`, `debug/crash_handler.cpp` | `TyraDebug::trap`, `TYRA_ASSERT` / `TYRA_SOFT_ERROR`, `CrashHandler::install()` |
| `renderer/**/stapip_*`, `.../static/core/stapip_vu_tap.*` | StaPip bags/clipper/qbuffer/programs/telemetry, the VU1 packet tap (`g_vuPacketHook`, `g_vuMemHook`) |

TyraX engine additions (clip family, post-fx, shadow/alpha/env targets,
billboards, dyn lights, portals, USB input, split view, soft errors) are
catalogued in `../../troubleshoot/tyra-engine-dev-*.md`.

## The non-negotiable rules

1. **Judge rendering correctness on PCSX2's software renderer** — the honest one.
2. **Never submit bags with `frustumCulling = None`** — off-screen geometry wraps
   the 4096-px GS raster window ("giant smeared polygons"); the HW renderer masks it.
3. **Never emit two exactly coplanar faces** — nothing backface-culls, so they
   dither-fight; offset the two faces by 0.01 local units (`addPlane`/`unitPlane`).
4. **Coplanar passes over one vertex array must pin one `StaPipBag::packageSize`**
   (the MINIMUM over the object's passes) or their split boundaries differ and the
   passes dither-fight.
5. **Bump `bboxVersion` only when the vertex buffer actually changed** — the stamp
   disables both the package-bbox cache and the retained-command cache.
6. **Object ids must be unique** (`renderer/models/unique_id.hpp`
   `generateUniqueId()`), never `rand() % 1000000` — collisions bind the wrong texture.
7. **Do not derive the VU1 active-plane mask from `FrustumPlanes`** — the clipper
   uses the MVP plus `clipMargin`; transform the six clip half-spaces to object space.
8. **Never arm `DTHE` for PSMCT32/24** — unspecified on the GS; gate every restore
   site on `RendererSettings::isDitherActive()`.
9. **`zBuffer.mask` is owned by whoever made the allocation** (`allocateVramBuffers`
   derives it). Never write the mask from a full-screen pass — grep `GS_SET_ZBUF`.
10. **`getCurrentFrameBuffer()` means the display buffer**; the four raster
    brackets share one `emitRasterRestore()` — do not add a fifth that does not.
11. **`endScene()` restores `gs->getZMaskDefault()`, never a literal `1`.**
12. **BLSS: the pipeline gate is `wantsProxies()`, never `isEnabled()`** — the
    per-bag bounding-sphere work is already done before the call.
13. **Anything sizing/addressing the PHYSICAL framebuffer uses
    `getRenderHeightF()`; game-facing layout keeps `getHeight()`.**
14. **A `DisplayMode` change edits both the engine table and `App::ps2ViewportOutput`**
    (`app.cpp`), or the editor draws a picture the console does not.
15. **Never call `setDisplayOutput` mid-frame** — it resets the VRAM allocator and
    is the only caller allowed to `allocateBuffer()` after init.
16. **Every VIF1 user calls `Vif1Queue::drain()` before touching the channel** (no
    raw `dma_channel_wait`); **every GIF-channel send calls `path3Fence()` first**.
17. **Anything a chain `REF`s must stay unchanged until that chain has run** (up
    to three packets later).
18. **Never fuse finished uniform and geometry chains** — construct ONE native
    packet (leading `FLUSHE`, uniforms, geometry, one `END`).
19. **Do not start chains from a DMAC interrupt** (`TYRA_VIF1_QUEUE_ISR` default 0).
20. **Never let a blocking audsrv RPC be the thing you wait on** — it stops the
    whole library; prefer `audsrv_available`/`audsrv_queued` plus a sleep.
21. **An EE buffer handed to a SIF DMA must be written back first**
    (`SifWriteBackDCache`) — PCSX2 emulates no EE cache and hides the bug.
22. **Every play site must offset its channel by `ScriptContext::reverbBusBase`**
    (0 or 24) — a voice is committed to a bus when it starts.
23. **Never put anything between a `#macro` line and its first instruction** — not
    even a comment; `vclpp` silently expands the macro to nothing.
24. **Count VU words with `nm`, not by counting `.vsm` rows**; the ceiling is on
    the SET (2042 instructions), not the program.
25. **A VU edit is not done until BOTH assemblers take it** — the Docker fallback
    (`vcl`) is exercised by nobody; check `--vu-check` and a `--docker` build.
26. **Keep `vugen.cpp` and the handwritten `.vclpp` in sync** — `--vu-check`
    fails otherwise, which is the point.
27. **Clamp vertex colours BEFORE the VU1 clipper interpolates them** (float clamp
    only; `ftoi0` belongs in the emitter).
28. **Keep the prim giftag `NLOOP` at 6× the input count** (`gsVertexCount`) — an
    under- or over-counting NLOOP stalls the GIF.
29. **ps2sdk's `ATEST_KEEP_*` name what is PRESERVED, not what is written** — read
    them backwards and cutout texels stamp z.
30. **3D texture wrap is `REPEAT`, asserted once per frame**; add any new
    full-screen writer after the 3D pass to the three-place contract.
31. **Do not change what a retained block encodes without adding it to the key in
    `beginRetainedBag`** — the block is captured, never re-derived.
32. **Never hand-edit only one twin**: `.claude/skills/` is the source of truth,
    `.agents/skills/` is the two-substitution regeneration, same commit.
33. **Revert engine probes before anything else** — they live in the shared
    `vendor/tyra`, and a forgotten `FBMSK = 0` reads as a new rendering bug.
34. **A rule measured false stays false** — the dead ends (coarser classification,
    the BLSS spatial split, flush removal, growing the package) are recorded in the
    troubleshoot files; do not re-open them.
35. **Attribute performance on hardware, not PCSX2** — the emulator over-weights
    libm and under-reports GS fill by 76×.

## When you are stuck

| Symptom | File |
|---|---|
| Build/flag/toolchain, stale `libtyra.a`, vclpp/CRLF, fork policy | `../../troubleshoot/tyra-engine-dev-build.md` |
| GS raster state, texture wrap, dither, alpha, flicker, 2D origin | `../../troubleshoot/tyra-engine-dev-raster.md` |
| Post-fx passes, portals, shadow/alpha-mask raster brackets | `../../troubleshoot/tyra-engine-dev-postfx.md` |
| Lights, spot cone, DIMX, signed SH, skinning | `../../troubleshoot/tyra-engine-dev-lighting.md` |
| VRAM allocator, texture footprint, hybrid/PSM, env-map opt-in, triple buffer | `../../troubleshoot/tyra-engine-dev-vram.md` |
| Display modes, DTV/InterlacedField/PAL, fps counters, vblank, black frames | `../../troubleshoot/tyra-engine-dev-display.md` |
| Packages, coplanar passes, bbox cache, strips, AABB classification | `../../troubleshoot/tyra-engine-dev-packages.md` |
| VIF1 queue, REF lifetime, slot pool, baked stream | `../../troubleshoot/tyra-engine-dev-vif1.md` |
| VU1/VU0 microcode, simulator, packet tap, assemblers, micro memory | `../../troubleshoot/tyra-engine-dev-vu1.md` |
| VU1 clip family, billboards, micro-memory/tag rules | `../../troubleshoot/tyra-engine-dev-clip.md` |
| Audio playback, ADPCM, reverb, WAV, sound conversion | `../../troubleshoot/tyra-engine-dev-audio.md` |
| Paths, loaders, missing assets, soft errors | `../../troubleshoot/tyra-engine-dev-files.md` |
| Profiling, counters, free-RAM probe, hardware timeline, A/B method | `../../troubleshoot/tyra-engine-dev-measurement.md` |
| Static submission, batching, EE cost | `../../troubleshoot/tyra-engine-dev-submission.md` |
| Retained commands, `STAPIPMISS`; baked VIF stream, content version | `../../troubleshoot/tyra-engine-dev-retained.md`, `-baked.md` |
| BLSS upscaler: plain mode, per-scene, raster redirect | `../../troubleshoot/tyra-engine-dev-blss.md` |
| BLSS proxies, emitters, spatial split, instruments | `../../troubleshoot/tyra-engine-dev-blss-proxy.md` |
| BLSS EE cost/cuts/break-even; shipped net, jitter, interlock | `../../troubleshoot/tyra-engine-dev-blss-cost.md`, `-blss-net.md` |
| Assert/soft error/crash handler, devkit probes | `../../troubleshoot/tyra-engine-dev-debug.md` |
| USB keyboard/mouse, virtual pad, replay input | `../../troubleshoot/tyra-engine-dev-input.md` |
| Collision mesh, raycasts, physics | `../../troubleshoot/tyra-engine-dev-physics.md` |

## Debugging discipline: one probe, one question

A multi-stage GS pipeline that fails silently costs days in reasoning. Make each
stage VISIBLE, one build at a time, and make each probe answer exactly ONE
question: (1) is the consumer gated — force the mask to its extreme; (2) what does
the intermediate buffer hold — drop the write mask so it paints as COLOUR; (3)
which inputs reached the stage — one `TYRA_LOG` per frame-group; (4) A/B the whole
feature with its switch off and diff the frames. Instrument OUTSIDE the loop you
are perturbing, log once every N frames, and revert probes before anything else.
Full pattern and its traps: `../../troubleshoot/tyra-engine-dev-debug.md`.
