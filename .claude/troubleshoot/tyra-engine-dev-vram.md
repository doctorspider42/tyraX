# troubleshoot/tyra-engine-dev-vram.md

The GS VRAM allocator (`RendererCoreGSVRam`, `renderer_core_gs_vram.*`), texture
footprint and eviction, colour depth / framebuffer PSM, the optional render targets,
and triple-buffer headroom. Symptom first. The BLSS z-buffer allocation traps are in
`tyra-engine-dev-blss.md`.

## Freeing a texture handed out another object's memory

**Cause:** upstream's `free()` was `pointer = address` (a stack pop), so freeing
anything but the newest allocation handed out the memory of still-live textures;
streaming-layer unloads reproduced it as surviving objects rendering another
object's texture.
**Fix:** GS VRAM is two regions and `free()` is order-independent: `allocateBuffer()`
(page-aligned) fills a **permanent** bump region at the bottom — both frame buffers,
z, post-fx scratch, noise, the env-map and camera-feed targets — that is never
released; `allocate()` (block-aligned) serves textures from a **coalescing
best-fit free list** above it. `free()` ignores addresses the heap never handed out,
which is what protects the permanent region — do not "fix" that into an assert.
Budget after the init buffers is **~1.08 MB at 32-bit colour and ~1.95 MB at
16-bit**, plus 256 KB more when a project reserves neither optional render target —
a 256×256 32bpp texture is 23% of the 32-bit heap, a 512×512 is 93%.

## The texture allocator under-allocated an extreme aspect ratio

**Cause:** `getSize()` added a flat `1024 * 2` words to every allocation with the
comment *"without this hack, textures are overlapping ourselves"*, which charged a
16-entry CLUT **8.25 KB** for 64 bytes of data AND still UNDER-allocated extreme
aspect ratios (a 512×32 PSMCT16 strip reaches word 15 360 and was handed 10 240).
**Fix:** `getSize()` computes the real swizzled GS footprint — whole pages above one
page, whole blocks inside one, via the PSM's Morton block order (the Z formats'
orders are permuted variants for which that shortcut is invalid, so they round to
pages). `menulayout.cpp`'s `gsWords` is the editor's host-side mirror of this; the
two must agree.

## A scene thrashes VRAM on the next thing that binds

**Cause:** `RendererCoreTexture::makeRoomFor()` evicts coldest-first (`pickVictim`:
stale entries by LRU; when the whole resident set is in this frame's working set,
the MOST recently bound one — plain LRU makes a per-frame texture scan cycle its
entire set). The Motor District garage's real answer was neither the policy nor a
per-frame allocation but a working set 84% of a 0.75 MB heap, half of it three
vehicle textures that had bypassed the project's palettization. Opening the pause
menu took it from 0.119 MB free / 121 KB largest block to 0.052 / 25, which a
console reported as four evictions and four re-uploads per frame.
**Fix:** measure before assuming there is a problem. Build with the **debug**
profile and grep the game's `bin/log.txt` for `VRAMSTAT`
(binds/hits/uploads/**re-uploads**/evictions/resident/free MB/largest free block,
per frame); `reup` per frame is the number that matters, each one a full PATH3
transfer. `VRAMSTAT` counts; `VRAMRES`/`VRAMEVICT` NAME — one line per resident
allocation with its texture name and words, plus eviction victims since the last
summary and what each was given up for. The census is debug-only in an **anonymous
namespace inside the .cpp** under `#ifndef NDEBUG` (a debug-only member would be an
ODR hazard), and its victim list is cleared when PRINTED, not per frame.
`examples/showcase` sits at 6 allocations and 1.15 MB free (2.04 MB at 16-bit
colour) and never evicts anything. Textures that must never be evicted are not
"pinned" — they are not in the list at all: give them their own `texbuffer_t`
through `Texture::vramResident`, the way `RendererCoreEnvMap` does.

## A hardcoded `GS_PSM_32` decoded a 16-bit frame as garbage

**Cause:** the framebuffer PSM is a setting, not a constant.
`RendererSettings::getFrameBufferPsm()` returns PSMCT32 or PSMCT16 per the
project's colour depth.
**Fix:** everything that writes a `FRAME` register for the screen must use it:
`draw_setup_environment`, all of `RendererCorePostFx` (`fbPsm` / `psmFor` — the
low-res work buffers are allocated in the frame format so the blur chain never
converts, while the film-grain noise stays PSMCT32 because it is uploaded rather
than rendered), and the env-map / shadow-map brackets' restores. The **z buffer
FOLLOWS the colour depth** (`RendererCoreDepth`): a colour buffer and the z it is
tested against must share page geometry (64x32 pages at 32 bits, 64x64 at 16), so
16-bit colour runs a `PSMZ16` z with a 16-bit Z scale and pays in depth precision
at distance (docs/gs-vram.md has the table).

## Destination alpha is dead at 16-bit colour

**Cause:** ps2sdk's `draw_setup_environment` programs `FBA = 1` for a 16-bit frame
PSM (disassembled from `libdraw.a`: the register at `0x4A + context` gets
`(psm & ~8) == 2`), which forces the MSB of every written alpha to 1 and silently
kills anything that reads destination alpha — the flashlight's `TEST.DATE` shadow
mask read SHADOW over the whole raster and no DATE-gated torch pass drew.
**Fix:** `initDrawingEnvironment` writes `FBA = 0` straight after that call, and
`RendererCoreAlphaMask::begin()`/`maskClear()` re-assert it per frame. Related:
**ps2sdk's `GS_SET_DIMX` masks each entry with `0x03`** while a DIMX entry is 3-bit
SIGNED (-4..3), so the negative half of the standard dither matrix (encoded 4..7)
collapses to 0..3 and the dither comes out one-sided — `renderer_core_gs.cpp` packs
the qword by hand. And the GS only dithers **16-bit destinations**, so `DTHE` is
inert at 32bpp and the switch cannot be tested there.

## `ColorDepth::Hybrid`: draw 32, show 16

**Cause:** the draw and display buffers need different formats.
**Fix:** 1.126.0, docs/gs-vram.md "Hybrid". `frameBuffers[0]` is the only draw
buffer (PSMCT32 over a 32-bit z) and `frameBuffers[1]` a display buffer (PSMCT16).
With triple buffering, `frameBuffers[2]` is another PSMCT16 display target; only
slots 1/2 rotate, slot 0 stays the draw target, including after layout rebuilds.
`emitHybridPresent(target)` copies with DTHE armed only for the copy and restores
through `emitRasterRestore`. Keep three things: **no unwaited FINISH** (the
double-buffer blit has no `draw_finish`; triple buffering follows the blit with
`emitDrawTargetSwitch(0)` and consumes its FINISH before publishing the display
target to the vblank handler; the final DTHE restore tag must have EOP=1, or PATH3
stays open and starves a between-frame PATH1 FINISH, freezing video-mode changes
after a double-buffer hybrid fallback); **anything that reads VRAM between frames
must `sync.align2D()` first** (the frame capture hung on it); and **`hasRealFrame()`
stays false**, so motion blur, BLSS temporal and the warp stay off.
`getFrameBufferPsm()` is the DRAW format (32) and `isDitherActive()` is false.
Charge/allocate the third buffer using slot 1's PSMCT16 format, not the draw format.
With the limiter off, withdraw the pending copy under interrupt masking before
reusing its target; no handler may latch that target during the copy. Code that
wants the display format asks `isHybridOutput()`. The original two-buffer mode on
physical PS2 was EE-neutral (`work` within 0.02 ms), the frame intact.

## The env-map / camera-feed targets cost VRAM even when unused

**Cause:** two 128×128 targets plus their z buffers, 128 KB each, were reserved for
every project whether or not anything sampled them — a quarter of the 32-bit
texture heap.
**Fix:** they are opt-in. `RendererCoreEnvMap::setEnabled` (driven by
`EngineOptions::envMapTarget` / `camFeedTarget`, decided by the editor's
`projectNeedsEnvMap` / `projectNeedsCamFeed`) turns them off, and then `getTexture()`
returns **nullptr** and `begin()`/`end()` are no-ops. Any new caller must
null-check rather than assume a texture; the generated game drops the reflection
pass instead. If you add a third such target, make it opt-in from the start.

## The env-map render target for dynamic reflections

**Cause:** GT3-style dynamic reflections need a raster target and a place to bind it.
**Fix:** `RendererCoreEnvMap` is a 128×128 VRAM render target for `VU1_ENV_BASIS_ADDR`
(also a 128×128 VRAM render target for the StaPip `TCE` env program's camera basis). Its
FRAME/SCISSOR/XYOFFSET/ZBUF redirect bracket has a dedicated 128×128 z-buffer —
"reflected" scene objects submitted inside the bracket occlude correctly — plus
`RendererCore3D::pushEnvView/popEnvView`; it is exposed as a VRAM-resident
`Texture::vramResident` that `useTexture` binds without a PATH3 upload — see
`docs/reflective-materials.md`. Permanently allocated below every texture like the env
map, +128 KB VRAM.

## Triple buffering: "does it fit" is the wrong question, and asking it that way is a boot crash

**Cause:** the third display buffer is a full one — 229 376 words at 512x448x32,
262 144 in `Pal576i` — and at 512x512 three buffers plus z are **exactly** the
whole 4 MB: the allocation succeeds and the NEXT one fails, which is a live
`Out of VRAM for post fx buffers` assertion before the first frame.
**Fix:** what must survive the third buffer is everything `RendererCore::init` still
allocates AFTER `gs.init` — post fx ~12 288 words, the env-map target + its z
32 768, the camera feed + its z 32 768, the projected-shadow slots a game may claim
later ~20 480 — plus a texture heap worth having, so `allocateVramBuffers` checks
for headroom (`kThirdBufferReserveWords + kThirdBufferMinTextureWords`) and REFUSES
rather than warns. The same arithmetic is why the feature is in practice an
`InterlacedField` one. **Any future permanent allocation added to renderer init has
to be added to that reserve.** The upscaler's low-res target was exactly that miss:
`blss.allocate()` runs LATER (configure -> the VRAM rebuild -> allocate), so it is
in neither `getHeapWords()` nor the reserve, and the guard handed out the third
buffer against 114 688 words the scene was about to claim — a 128 KB texture heap
instead of 576 KB at 512x448 1x2. It is subtracted explicitly now, from the live
raster scale, and so is its host twin `project::tripleBufferingFit`. **The reserve
is a list of what is allocated after this line, and anything allocated after it in a
REBUILD counts too.**

## The BLSS colour-target reserve moved under a video-mode switch

**Cause:** the reserve must survive native-scene video-mode switches.
**Fix:** the BLSS colour-target reserve uses
`RendererCoreGS::setLowResTargetScale`, separately from pinned Z and active scene
raster. `configure()` sets it before `needsBufferRealloc()`: a mixed project must
recheck triple-buffer headroom even when Z never shrinks. Preserve the reserve
across native-scene video-mode switches; `setScene()` must not change it.

## A video-mode change left an asynchronous copy in flight

**Cause:** hybrid double-buffer presentation can still have an asynchronous copy in
flight when `RendererCore::setDisplayOutput` resets the GS.
**Fix:** drain PATH1 and PATH3 before it. Game scripts should set
`ScriptContext::requestDisplayMode` so video changes apply at the normal boundary
before `beginFrame`.

## GS headroom arithmetic is off by a page row

**Cause:** a total pixel count rounded to 2048 words is not the allocator's layout.
**Fix:** GS headroom uses whole page rows: 64x32 at 32 bits, 64x64 at 16 bits,
including the Z format. A 512x224 CT16 target costs 256 KiB, not 224 KiB.

## `TextureRepository` answered id -> texture from stale links

**Cause:** the id -> texture cache (`findLinked`, 1.126.2) is 256 direct-mapped
entries, valid for one `Texture::linkGeneration`; a direct `push_back` on the public
`Texture::links` vector leaves it answering from the old links.
**Fix:** change `Texture::links` only through `addLink`/`removeLink*`. The uncached
walk (every texture × its links, per sprite) was 0.97 ms of an 85-sprite HUD on a
PS2. `Texture::residentHint` remembers where the texture's entry sits in the
resident list; `RendererCoreTexture::findAllocation`/`isResident` trust it only
after checking the entry's id — any new resident-list lookup should go through them.
