---
name: tyra-engine-dev
description: >
  Guide to editing the in-tree Tyra PS2 engine fork in vendor/tyra — the
  renderer/clipper/VU1 pipeline, audio (audsrv), file loading over PS2 host fs,
  and how engine changes reach running games through native and Docker builds. Use this
  skill whenever you touch ANY file under vendor/tyra, work on PS2-side
  rendering, clipping, VU1 microprograms, textures, audio playback or asset
  loading, or when diagnosing in-game symptoms like rendering corruption, giant
  smeared polygons, crackling/wrong-speed audio, or "Failed to load" asserts.
  The pitfalls section records expensive dead ends — read it before attempting
  any PS2 rendering or performance work, even if the change looks trivial.
---

# Working on the in-tree Tyra engine fork

`vendor/tyra/engine` is a **versioned fork** of [h4570/tyra](https://github.com/h4570/tyra) (Apache 2.0, forked at upstream `9273416`), edited like normal project code — no patch machinery, no submodule. This file = rules + pitfall index; details live in `references/` (table at the end). Read the matching reference before editing a subsystem.

`PROGRESS 123` citations point at entries of the retired `PROGRESS.md` (~15 800 lines; in git history — recipe in docs/backlog.md: `git log --diff-filter=D -- PROGRESS.md`). New work records itself in commit messages and PR bodies.

## Fork rules
- Mark every departure from upstream with a `Modified by TyraX` comment near the top of the file (examples: `audio_song.cpp`, `stapip_clipper.cpp`, `planes_clip_algorithm.cpp`, `stapip_qbuffer.cpp`, `render_bbox.cpp`; VU sources use `;// TyraX` notes, e.g. `vcl_sml.i`).
- **LF only under `vendor/tyra/**`** (`.gitattributes`). vclpp chokes on CRLF in files with a `#macro`, with a misleading `Preprocessor directive inside macro block: '#endmacro'`. Scripts: Python `Path.write_text()` writes CRLF on Windows — write bytes, check `grep -c $'\r'`.
- The rest of `vendor/` (imgui, glfw, imguizmo, imnodes, stb, tyra's non-engine parts) is git-ignored, cloned by `setup.ps1`/`setup.sh` — never edit. Exception: `vendor/tyra/audsrv/` is an in-tree source fork with committed binaries (references/audio.md).
- `vendor/tyra` is shared by every project on the machine: revert debug probes before you stop.
- Several engine pieces have **host twins that must change in the same commit**: `src/objparser.cpp` ↔ `LeanObjLoader`; `src/tmdl.hpp` ↔ `TmdlLoader`; `src/blss.cpp` + `src/blsscorpus.cpp` + docs/blss-reconstruction.md ↔ `RendererCoreBlss` (incl. `TYRA_BLSS_ACT_TABLE` ↔ host `actTable`); `src/vugen.cpp` ↔ the 15 StaPip `.vclpp` programs; `App::ps2ViewportOutput` ↔ `DisplayMode` table; `menulayout.cpp` `gsWords` ↔ `RendererCoreGSVRam::getSize`; `project::tripleBufferingFit` ↔ `allocateVramBuffers` headroom; `src/isoexport.cpp` skip ↔ `RESDIR_SKIP`.

## How an engine change reaches the game
No manual engine build: the Runner (`src/runner.cpp`) rebuilds it on every game build (F5 / `tyrax-editor --build <projectDir>`).
- **Native (default)**: `tools/toolchain/native-build.sh`/`.ps1` syncs the engine into `<configDir>/native-build/<engineVolumeName>` (`~/.config/tyra-editor` or `%LOCALAPPDATA%`), builds with pinned PS2DEV + openvcl; a toolchain stamp invalidates stale objects.
- **Docker fallback** (`--docker`, Sony `vcl`): `vendor/tyra` mounted read-only at `/engine-src`, checksum-rsynced into shared external volume `tyra-engine-<hash>` at `/tyra`; changed → `libtyra` rebuilt once, ELF relinked.
- VU1 microprograms rebuild only when a `.vclpp/.vcl/.vsm/.i/.h` changed (full set 109 s; a `.cpp` edit ~7 s). `--build --rebuild` throws away the compiled engine incl. VU objects — required after compiler-flag or toolchain-image changes.
- `vendor/tyra/Makefile.base` is shared by engine AND every generated game (`-G0`, `RESDIR_SKIP`, `src/vu*/` host-code exclusion). Details/traps: references/build-and-toolchain.md.
- Engine code cannot be compiled on the host; even a syntax check needs a game build (tyra-testing skill).

## TyraX additions at a glance
EE clipper with Cohen–Sutherland outcodes and static pools; StaPip `clip` VU1 family (on-VU1 Sutherland–Hodgman, **default** clipping since M4; EE clipper = "Precise clipping on EE (legacy)", still the load-time default for pre-M4 `.tyra` files without a `clipping` key; docs/vu1-clipping.md); post-fx (bloom, grain, DoF, god rays, custom effects, motion blur); env map / camera feeds / projected shadows / shadow-volume alpha mask; billboards; portals; split screen; VU0 ray tracer; BLSS neural upscaler; frame extrapolation warp; triple buffering, Hybrid colour depth, extra display modes; VIF1 queue, retained and baked command streams, triangle strips; `TmdlLoader`; USB kbd/mouse, two pads, rumble, input replay; quiet-halt asserts + `TYRA_SOFT_ERROR`; EE crash handler + VU1 packet tap; audsrv fork with both SPU2 cores + hardware reverb.

## Core rules (break one and you will find it the expensive way)
1. **Never `frustumCulling = None`** except billboard bags → GS 4096-px window wrap, "giant smeared polygons".
2. **Coplanar passes over one vertex array share one package size** (`StaPipBag::packageSize` = MIN over passes); nothing backface-culls, so never emit two exactly coplanar faces.
3. **Anything a DMA chain REFs must stay unchanged until that chain ran** — up to three packets later with `Vif1Queue`. `packet2_utils_vu_add_unpack_data` does not copy. No temporaries, no "other buffer" (`^ 1`) logic: use `kPacketCount`/`kPoolSides`.
4. **Every VIF1 user calls `Vif1Queue::drain()`**, never `dma_channel_wait(DMA_CHANNEL_VIF1, 0)`; **every PATH3 sender calls `path3Fence()`** first.
5. **Bump `bboxVersion` when positions change, `contentVersion` (via `BagArray`) when contents change in place — and never when nothing changed.** Add every new command input to the retained/baked cache keys in the same edit.
6. **GIF tags: NLOOP must equal the register writes exactly** (either direction hangs the GIF with a clean log).
7. **Global GS state you write, you own**: CLAMP must be REPEAT for 3D (per-bag clamp only via `setBagWrap`); never write `zBuffer.mask`/`GS_SET_ZBUF` from a full-screen pass; don't "restore" `TEXA`/`COLCLAMP` you didn't set; DTHE only for 16-bit destinations and `= 0` around arithmetic targets; `FBA = 0` after `draw_setup_environment`.
8. **Every raster redirect restores through `emitRasterRestore()`** (ZBUF last); `getCurrentFrameBuffer()` means the display buffer.
9. **Physical framebuffer sizes use `getRenderHeightF()`, game-facing layout `getHeight()`**; screen `FRAME` writes use `getFrameBufferPsm()`.
10. **Any new permanent VRAM allocation in renderer init (or a rebuild) joins the triple-buffer reserve**; new optional render targets are opt-in and callers null-check.
11. **VU1**: nothing between `#macro` and its first instruction; no nested macros; VU-side `#define`s are literals; loop counts multiple of 3; per-batch loads inside `begin:`; micro memory is a SET ceiling of 2042 words — measure with `nm`; a VU edit must build with BOTH openvcl and Sony `vcl`; hand-edits to the 15 StaPip programs need `vugen.cpp` too (`--vu-check`).
12. **audsrv: never block on an audsrv RPC** (it stops the whole library); `SifWriteBackDCache` before any EE→IOP DMA; play sites offset channels by `ScriptContext::reverbBusBase`.
13. **Loaders soft-fail** (`TYRA_SOFT_ERROR` + fallback), never assert on a missing file; test asset paths on both `host:` and `cdrom0:`.
14. **Ids from `generateUniqueId()`**, never `rand()`; texture links only via `addLink`/`removeLink*`.
15. **Verify**: correctness on PCSX2's software renderer; anything touching GS state, DMA timing, caches, dithering, USB or EE exceptions on real hardware (PCSX2 hides those classes). Timings: one ELF toggled at boot, same assembler, same-build control; never trust PCSX2 GS fill numbers (76x off).

## Pitfalls index (symptom → cause → full story)
**Rendering corruption / missing geometry**
- Objects render twice, giant smeared polygons → bag with `frustumCulling = None`, GS 4096-px wrap (SW renderer/hardware only) → static-pipeline.md
- Dashed bands / grey wedges / baked shadows z-fighting → coplanar faces or coplanar passes split at different package sizes → static-pipeline.md
- Hole in geometry at point-blank range → guard-band route without exact near/far planes 6..7 → static-pipeline.md
- Giant textured wedges near the near plane on strips → odd strip primitives expanded without swapping first two vertices → static-pipeline.md
- Holes in roads, per camera → retained block replayed with position-dependent `EMIT_STATE_FLAG` → submission-dma.md
- Culling with stale boxes after moving geometry → `bboxVersion` not bumped (cache keyed by vertex pointer) → static-pipeline.md
- Stale lighting from the baked stream → in-place colour rewrite without `contentVersion`/`BagArray` → submission-dma.md
- Corona sliver to the screen corner / screen-sized slabs, console only → slot-pool REF race (pool not double-buffered) → submission-dma.md
- Lighting flashes despite correct VU math → REF to a temporary array → submission-dma.md
- Blended decals/blob shadows invisible, every byte identical → queued chains started next frame after the clear (`endFrame` drain) → submission-dma.md
- `Unknown VifCmd 3f` → pool freed while still owned ("0 or 1" side logic), floats read as tags → submission-dma.md
- Every 4-bit palettised texture draws nothing; "BLSS deletes the terrain" → full-res pass stamped z past a shrunk z buffer onto the CLUT (`zBuffer.mask`) → blss.md
- Cutout foliage occludes things behind it along quad edges → `ATEST_KEEP_*` read backwards → gs-state.md
- Ground streaks converging on vanishing point → CLAMP left at clamp for 3D (REPEAT contract) → gs-state.md
- Whole picture hue-shifted only with post-fx → a pass "restored" `TEXA`/`COLCLAMP` to a guess → gs-state.md
- Terrain missing on every batch after the first (openvcl) → value loaded above `begin:` not live across the loop → vu1-microprograms.md
- Program silently missing a feature / ~27 instructions short → comment inside a `#macro` (expands to nothing) → vu1-microprograms.md
- Clipper keeps/drops wrong edges, scene-dependent → CLIP flag read too early (24-bit shift window) → vu1-microprograms.md
- Lit surface brightens when it touches the screen edge → colour clamped after S-H interpolation → vu1-microprograms.md
- Torch black up close, bright far away → `invSoft` sized off full range (cone term ∝ distance²) → vu1-microprograms.md
- Wrong texture on HUD sprites after opening a menu → `rand()` id collision → engine-additions.md
- Surviving objects render another object's texture after unloads → upstream stack-pop `free()` → textures-vram.md
- Black frame every third present, no HUD → `context ^ 1` with three buffers after a re-lay → display-framebuffers.md
- HUD 46/32 rows off in 1080i/Pal576i (or 112 in InterlacedField) → 2D origin vs framebuffer height → display-framebuffers.md
- Garbage right of the picture in 480p/1080i → `graph_set_screen` DISPLAY.DW → display-framebuffers.md
- Boot logo on random colour → `Color` default ctor uninitialised (`bgColor`) → engine-additions.md
- Feeds/raster targets upside down → GS rows top-down; flip V → renderer-features.md
- Sharp rectangles in DoF → DoF ran after 2D (sprites stamp max z) → renderer-features.md
- God rays wash the whole frame white → bright-pass threshold too low (96; use 150) → renderer-features.md

**Console-only GS artefacts (PCSX2 shows nothing)**
- Additive quads darken their footprint by half a step → negative DIMX entries / `GS_SET_DIMX` masking → gs-state.md
- Moiré/rectangles where alpha was written, TV only → flicker filter `PMODE.MMOD = 0` → gs-state.md
- Green slivers/halo or checkerboard in shadow masks → GS dithering count targets (DTHE on) → gs-state.md
- Checker on 32-bit frames → DTHE armed for PSMCT32/24 → gs-state.md
- DATE-gated passes never draw at 16-bit → ps2sdk sets `FBA = 1` → gs-state.md
- Marks along straight lines in count bands → slid FRAME.FBP / unclamped rects / TEX0 base slide → gs-state.md, renderer-features.md
- REGLIST/PACKED-tag compaction stalls first gameplay frame → only PCSX2 accepted it → gs-state.md

**Hangs and crashes**
- Hang on loading screen in `draw_wait_finish`, clean log → NLOOP miscount → gs-state.md
- Freeze on the first gameplay frame on hardware → fused/NEXT-joined DMA chains → submission-dma.md
- Console wedges until Reset → `FlushCache` suppressed for packets → submission-dma.md
- EE exception right after `EIntr()` → chains started from a DMAC interrupt (EXL ⇒ EPC lies) → submission-dma.md
- Debug builds freeze at boot with crash handler → `ee_dbg_install(2)` → devkit-debugging.md
- Boot freeze on Tyra logo under ps2link → USB HID drivers on stock ps2link → engine-additions.md
- Camera spins on hardware with a still mouse → `PS2MouseData` not zeroed → engine-additions.md
- Every ISO boot wedges → `TYRA_LOG` opening `cdrom0:LOG.TXT` → loaders-files.md
- `Out of VRAM for post fx buffers` before frame one → third buffer "fits" but the next alloc fails → display-framebuffers.md
- Mode switch freezes after Hybrid double-buffer fallback → final DTHE-restore tag without EOP → display-framebuffers.md
- `VU1 pipeline programs overflow into the draw-finish program` / silent overflow in release → micro-memory set over 2042 → vu1-microprograms.md
- `std::bad_alloc` with `TYRA_VIF1_QUEUE_HOLD` → depth 128; use 80 → submission-dma.md
- Console hangs with Live Debugger attached → sleepless loop (`setFrameYield`) → display-framebuffers.md

**Build**
- `relocation truncated to fit: R_MIPS_GPREL16 against Tyra::Info::writeLogsToFile` → small-data overflow, needs `-G0` → build-and-toolchain.md
- `Build OK` but engine fix "does nothing" → stale `libtyra.a`/VU objects (failed make, image swap, flag change w/o `--rebuild`) → build-and-toolchain.md
- `no opt table .. for <label>` → register allocator out of VF (31) → vu1-microprograms.md
- `can't find instruction` / `unresolved expression` → nested macro / two-level `#define` → vu1-microprograms.md
- `--docker` builds die in the engine after a VU edit → only openvcl's load sinking fitted it → vu1-microprograms.md
- First `src/vu*` host file breaks every build → missing from `Makefile.base` SOURCES exclusion → build-and-toolchain.md
- Constant clashes with `LIGHT_AMBIENT` etc. → PS2SDK `math3d.h` defines → build-and-toolchain.md

**Audio**
- Loud crackle at correct pitch → 8-bit WAV unsigned, needs XOR 0x80 → audio.md
- Second sound silent on console only → missing `SifWriteBackDCache` before SIF DMA → audio.md
- Music silent, sfx fine, no error → `sceSdInit()` after `audsrv_init()` cleared ring callbacks → audio.md
- Reverb noise on first preset → effect attr set before enabling effects → audio.md
- Frame drops blamed on sound emitters → blocking `audsrv_wait_audio` → audio.md
- Music stutters over ps2link → host: TCP starving the IOP; don't shrink chunks → audio.md
- Music turns into a buzz a few seconds in → audsrv EE callback thread below the busy-polling game thread → audio.md
- Sound plays in the wrong room → channel not offset by `reverbBusBase` → audio.md
- Play "succeeds" silently → `AUDSRV_ERR_ARGS` (5) looks like a channel → audio.md

**Files**
- PCSX2 "Denying access to path outside of ELF directory", game opens nothing → `getcwd()` without trailing slash → loaders-files.md
- Wrong offsets in parsed files over host: → `fseek`/`ftell` unreliable → loaders-files.md

**Measurement**
- Half the real FPS in progressive modes → T3 (H-BLNK) clock in old `getFps` → display-framebuffers.md
- `MEM 32.0/32 MB` with 13.6 MB free → old free-RAM probe → devkit-debugging.md
- "EE-bound" because `drain ≈ 0` → false discriminator → performance.md
- Same ELF, different `proxy` ms → scene settled at 201 vs 198 proxies → blss.md
- Benchmark suspiciously fast → missing `.res-baked`, placeholders → performance.md
- "VU1 is free" → instrumented a program the scene doesn't run → vu1-microprograms.md
- `--vu-check` green but branch wrong → harness can't see the gated path; prove it fails inverted → vu1-microprograms.md

**Measured dead ends — do not reopen without a new hypothesis**
coarser (8-package) frustum classification (+4.59 ms); package size above 75 (81 max, 144 impossible); 128-byte `BagArray` alignment; >4-bag or large-bag batching; Vif1Queue depth 8; DMAC-interrupt chain start; per-REF `SyncDCache`; uncached / UCAB packets; per-bag `dma_channel_wait`; transformed-light cache; retaining per-bag uniform copies; BLSS emitter spatial split; interleaved BLSS accumulators; a fourth BLSS libm win; BLSS+extrapolation history-buffer theory; k255/ADC-table VU image; REGLIST state compaction; widening the cull programs' ADC guard band.

## Reference index — read the file when you touch…
| File | Read when you touch |
|---|---|
| references/build-and-toolchain.md | Runner engine sync, native/Docker builds, `Makefile.base`, `-G0`, `RESDIR_SKIP`, toolchain image, openvcl vs Sony `vcl` |
| references/engine-additions.md | 2D sprites/aspect, dynamic-light pick, pads/USB kbd-mouse/replay, asserts & soft errors, object ids, collision, boot-time state, SH lighting, skin reuse |
| references/renderer-features.md | post-fx, env map/cam feeds, shadow map, alpha mask/shadow volumes, billboards (+residency), portals, split view, VU0 ray tracer, frame-extrapolation warp, live texture reload |
| references/blss.md | neural upscaler, raster redirect, z-buffer sizing/mask, BLSS measurements, nets and incompatibilities |
| references/vu1-microprograms.md | any `.vclpp`/`.i`, `vugen`/`--vu-check`, vclpp/VCL traps, micro memory, clip images, `VU1_OPTIONS_ADDR`, spot gate, VU1 cost |
| references/gs-state.md | GS registers, GIF tags, CLAMP/REPEAT, ATEST, FBA, DIMX/DTHE, flicker filter, TEXA/COLCLAMP, debugging an invisible GS pass |
| references/display-framebuffers.md | display modes, 2D origin, framebuffer PSM, Hybrid, triple buffering, vblank/present, frame pacing, FPS counters |
| references/textures-vram.md | VRAM allocator, `getSize`, eviction, `VRAMSTAT`/`VRAMRES`/`VRAMEVICT`, texture id cache, `residentHint` |
| references/static-pipeline.md | StaPip culling/clipping/packages, 75-vertex ceiling, guard band, bbox caches & version stamps, spot bounds, transform cache, strips, generated grids |
| references/submission-dma.md | packets, REF lifetime, slot pools, batching, `Vif1Queue`, 2D-in-VIF1, retained & baked commands, FlushCache/uncached experiments |
| references/performance.md | measuring anything: rules, telemetry (`FTCLIP`, `FTPKT`, `TYRA_STAPIP_ATTRIB`), hardware timeline, reusable cost constants |
| references/audio.md | audsrv fork, ADPCM, streaming, reverb, channel buses, sound conversion/loop flags |
| references/loaders-files.md | `LeanObjLoader`, `TmdlLoader`, soft-fail loaders, `host:`/`cdrom0:` paths, `fromCwd` |
| references/devkit-debugging.md | VU1 packet tap, EE crash handler, free-RAM probe |

## TyraX 2.0 integration

The editor is 2.0.0; the combined project format is 114. Read
[TyraX2 integration notes](references/tyrax2-integration.md) before changing
frame ownership, pipeline recording or the associated acceptance tooling.
The consolidated branch notes retain the full historical qualification record.
