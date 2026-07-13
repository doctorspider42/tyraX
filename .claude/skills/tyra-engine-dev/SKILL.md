---
name: tyra-engine-dev
description: >
  Guide to editing the in-tree Tyra PS2 engine fork in vendor/tyra — the
  renderer/clipper/VU1 pipeline, audio (audsrv), file loading over PS2 host fs,
  and how engine changes reach running games through the Docker build. Use this
  skill whenever you touch ANY file under vendor/tyra, work on PS2-side
  rendering, clipping, VU1 microprograms, textures, audio playback or asset
  loading, or when diagnosing in-game symptoms like rendering corruption, giant
  smeared polygons, crackling/wrong-speed audio, or "Failed to load" asserts.
  The pitfalls section records expensive dead ends — read it before attempting
  any PS2 rendering or performance work, even if the change looks trivial.
---

# Working on the in-tree Tyra engine fork

## Fork policy

`vendor/tyra/engine` is a **versioned fork** of [h4570/tyra](https://github.com/h4570/tyra)
(Apache 2.0, forked at upstream commit `9273416`), maintained directly in this
repo. Edit it like normal project code — no patch machinery, no submodule.
Rules:

- Mark every departure from upstream with a `Modified by tyra-editor` comment
  near the top of the file (grep for existing examples:
  `audio_song.cpp`, `stapip_clipper.cpp`, `planes_clip_algorithm.cpp`,
  `stapip_qbuffer.cpp`, `render_bbox.cpp`, `vcl_sml.i`).
- **LF line endings only** under `vendor/tyra/**` — enforced by
  `.gitattributes`; the `vclpp` VU1 preprocessor chokes on CRLF. Don't fight it.
- The rest of `vendor/` (imgui, glfw, imguizmo, imnodes, stb, and tyra's
  non-engine parts) is git-ignored and cloned by `setup.ps1` — never edit those.

## How an engine change reaches the game

You don't rebuild the engine by hand. The editor's Runner (`src/runner.cpp`)
does it on every game build (F5 or `tyra-editor.exe --build <projectDir>`):

1. `vendor/tyra` is bind-mounted **read-only** at `/engine-src` in the
   project's container (service `compiler`, container `<name>-compiler-1`).
2. The Runner checksum-rsyncs `/engine-src` into the shared volume
   `tyra-engine-shared` (mounted at `/tyra`), shared by all projects.
3. If anything changed, `libtyra` is rebuilt once (VU1 microprograms are
   force-rebuilt too) and the game ELF is dropped so it relinks.
4. Unchanged engine → the rsync is a no-op and builds take seconds.

So the loop is: edit a file under `vendor/tyra/engine`, run a game build, and
the change is in the ELF. No container restarts needed. If the shared volume
gets into a weird state, `git checkout` inside `/tyra` restores originals (it's
a git checkout of the fork).

## Engine layout

`vendor/tyra/engine/{inc,src}/` mirror each other by subsystem:
`audio` (audsrv music + ADPCM sfx), `debug`, `file`, `info`, `irx`, `loaders`
(PNG, obj/md2), `math`, `pad`, `physics`, `renderer` (the big one — 2D, 3D
static/dynamic pipelines, core GS/VU1 code), `thread`, `time`; plus
`engine.hpp` / `game.hpp` and the VCL/VU1 sources under the renderer
(`*.i`, `*.vcl` — preprocessed by vclpp inside the container).

Editor-specific engine additions so far: Cohen–Sutherland outcodes in the EE
clipper, the StaPip `clip` VU1 program family (on-VU1 Sutherland–Hodgman
behind the hidden `"clipping": "vu1"` project mode — design + status in
`docs/vu1-clipping-plan.md`), static pools in `stapip_clipper.cpp` /
`stapip_qbuffer.cpp`,
`RendererCorePostFx` (bloom + film grain via GS blits), WAV-header-aware song
player, `bboxVersion` on `StaPipBag` for moving geometry, `LeanObjLoader`
(OBJ+MTL, host:/cdrom0:-safe; parsing semantics mirror the editor's
`src/objparser.cpp` — keep the two in sync), `physics/CollisionMesh` (XZ-grid
triangle collider) + `Ray::intersectTriangle`, a guard in `debug.cpp` so
TYRA_LOG never opens `cdrom0:LOG.TXT` for write (that wedged every ISO boot),
and `renderer/models/unique_id.hpp` (`generateUniqueId()`) replacing upstream's
`rand() % 1000000` object ids (see the pitfall below).

## Hard-won pitfalls (dead ends already explored — don't repeat them)

**Rendering**
- **Never submit bags with `frustumCulling = None`.** Off-screen geometry wraps
  the GS 4096-px raster window → "objects render twice / giant smeared
  polygons". PCSX2's HW renderer often *masks* this; the SW renderer and real
  hardware show it. This was the root cause of a long-standing corruption bug —
  not the clipper patches (all were bisected; even pure upstream reproduced it).
- **Widening the cull programs' ADC test is retired; real VU1 clipping is a
  separate program family.** Three attempts at a guard band inside
  `PerformClipCheck` all corrupted ADC bits (documented in `vcl_sml.i`); the
  working approach is the StaPip `clip` programs (hidden `"clipping": "vu1"`
  mode), which never derive ADC from clip flags. VU1 microcode traps already
  paid for there: `fcand` sets VI01 to 0/1 (any-bit), NOT the masked bit
  pattern; a vertex clipped to exactly |x| = w scales to GS coordinate 4096.0
  and wraps the 12.4 XYZ2 field (clip the sides at 0.9w, let the scissor
  finish); every VU1 vertex-loop count must be a multiple of 3 or the loop
  runs off into memory; overflowing the 2048-instruction micro memory is
  SILENT in release builds (the assert is compiled out) - check program sizes
  with nm on the .o files when adding VU1 code.
- The engine bbox cache is keyed by bag pointer — geometry that changes at
  runtime must bump `bboxVersion` on its `StaPipBag`, or culling uses stale
  boxes.
- Upstream's default `PlanesClipAlgorithm::clipMargin` pushes the near plane
  ~10 units from the camera; generated games override it.
- Judge rendering correctness on **PCSX2's software renderer** — it is the
  honest one. See tyra-testing for how.
- **Object ids must be unique, not random.** `Sprite`/`Mesh`/`MeshFrame`/
  `MeshMaterial`/`MeshMaterialFrame` share ONE lookup namespace in
  `TextureRepository`: `addLink(id)` binds a texture to a sprite/material and
  `getBySpriteId`/`getByMeshMaterialId` return the FIRST texture whose links
  contain that id. Upstream drew ids from `rand() % 1000000` (never seeded), so
  a collision bound the wrong texture to a sprite → garbled/black HUD sprites,
  worst right after opening a menu (a burst of new sprites raises the collision
  odds against the always-present debug-HUD glyph). Fixed with
  `renderer/models/unique_id.hpp` `generateUniqueId()`. Use it for any new
  id-bearing render object; don't reintroduce `rand()` ids. (`audio_song.cpp`
  intentionally keeps `rand()` — separate namespace, assigned off-thread.)
- **DTV display modes (480p/1080i)**: ps2sdk's `graph_set_screen` always
  programs the mode's full VCK width into DISPLAY.DW, and no 64-aligned
  framebuffer width divides the 1440/1920-VCK DTV rasters — the GS scans
  garbage past the buffer's right edge. `RendererCoreGS::setDtvDisplay`
  programs DISPLAY1/2 directly instead. Also: the gsKit/OPL 1080i recipe
  (interlaced FRAME mode + MagV--) **hard-crashes PCSX2 v2.3.205** (the
  process dies seconds after SetGsCrt, no crash dialog); 1080i in FIELD
  mode with MAGV=2x is visually equivalent (both fields step through every
  buffer line) and works.
- **Runtime display switching**: `RendererCore::setDisplayOutput(mode, ws)`
  (tyra-editor fork) switches the scan mode / widescreen between frames.
  A mode change resets the whole VRAM bump allocator (`vram.reset()`),
  rebuilds frame/z buffers + post fx, and `texture.evictAll()` drops every
  texture allocation (they lazily re-upload) — never call it mid-frame.
  The projection aspect lives in `RendererSettings::updateGeometry`
  (fixed 4:3-baseline look; widescreen scales it anamorphically).

**Audio**
- audsrv streams PCM only; ADPCM is for one-shots (`adpcm.tryPlay`), and ADPCM
  voices cannot be stopped — the editor round-robins SPU channels to avoid
  drop-outs. Channels 16–23 are reserved by generated games for sound emitters.
- WAV files: 8-bit PCM is unsigned (0x80 = silence) but audsrv mixes signed —
  convert (XOR 0x80) or it wraps at every zero crossing (loud crackle at
  correct pitch — that exact symptom happened).
- Mono/low-rate streams need smaller chunk size + fill threshold or audsrv's
  ring buffer starves.

**Files / assets**
- `fseek`/`ftell` are unreliable over the PS2 host filesystem — the WAV parser
  walks RIFF chunks in memory for this reason. Prefer read-into-memory parsing.
- Textures must be power-of-two sized; PNG 32/24bpp or palletized 8/4bpp
  (palletized is fastest on PS2).
- `cdrom0:` paths differ from `host:` paths — ISO9660 uses `\`, upper-case and
  a `;1` version suffix; `FileUtils::fromCwd` and the extension helpers handle
  the conversion. Test asset-loading changes on BOTH boot paths (host: via
  normal Build & Run, cdrom0: via Export PS2 ISO).

**Build environment**
- PS2SDK's `math3d.h` `#define`s names like `LIGHT_AMBIENT` — prefix your
  constants (the codebase uses `SCENE_*`).
- The compiler is `mips64r5900el-ps2-elf-g++` inside the `h4570/tyra` image;
  there is no way to compile engine code on the host. Even a syntax check
  requires a game build (see tyra-testing).

## Performance context

The 98k-vertex benchmark scene went 12 → 50 FPS through: outcode early-out in
the EE clipper, static pools (no per-call heap), and finally exact per-package
frustum classification. The StaPip packager's per-submit `new[]`/`delete[]`
package arrays are pooled too (grow-only vectors in `StaPipBagPackager`;
callers must not free the result) — measured worth only ~2% of the partial
branch in PCSX2: the real cost of PARTIALLY_IN_FRUSTUM geometry is the
per-package bbox classification + EE clipping, which scale with vertex count
(see PROGRESS entry 79: a single detail-16 box near the camera = 9.2k verts =
~36 ms of EE; the fix was authoring-side detail, not the allocator). Known
next target (from PROGRESS.md backlog): upstream's own TODO in
`stapip_clipper.hpp` — move clipping fully to VU1.
Measure with PCSX2's FPS display on the software renderer, 3+ samples, before
and after; pixel-compare screenshots to prove output is unchanged.
