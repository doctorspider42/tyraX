# GS VRAM allocator, texture residency, eviction, texture lookup

Read when: you touch `RendererCoreGSVRam` (`renderer_core_gs_vram.*`): `allocate`/`allocateBuffer`/`free`/`getSize`, `RendererCoreTexture` (`makeRoomFor`, `pickVictim`, `findAllocation`, `isResident`, `updateTextureInfo`), `TextureRepository`, `Texture::links`/`vramResident`/`residentHint`, or chase thrash/missing textures. Full write-up: docs/gs-vram.md. Framebuffer sizes/headroom: display-framebuffers.md.

## Two regions, order-independent free
- `allocateBuffer()` (page-aligned) fills a **permanent** bump region at the bottom (both frame buffers, z, post-fx scratch, noise, env-map and camera-feed targets) — never released. Only `setDisplayOutput` may call it after init (and BLSS's `rebuildPermanentBuffers`).
- `allocate()` (block-aligned) serves textures from a **coalescing best-fit free list** above it. `free()` ignores addresses the heap never handed out — that protects the permanent region; **do not turn it into an assert**. Upstream `free()` was `pointer = address` (stack pop): freeing anything but the newest handed out live textures' memory (streaming-layer unloads: survivors rendering another object's texture).
- Budget after init buffers: **~1.08 MB at 32-bit colour, ~1.95 MB at 16-bit**, +256 KB when a project reserves neither optional render target (env map / cam feed, opt-in — renderer-features.md). A 256×256 32bpp texture = 23 % of the 32-bit heap; 512×512 = 93 %.
- **`getSize()` = real swizzled GS footprint**: whole pages above one page, whole blocks inside one, via the PSM's Morton block order (Z formats' orders are permuted → round to pages). It used to add a flat `1024 * 2` words to everything ("without this hack, textures are overlapping ourselves"): a 16-entry CLUT charged **8.25 KB** for 64 bytes, while extreme aspect ratios were still UNDER-allocated (512×32 PSMCT16 reaches word 15 360, was given 10 240). Host mirror: `menulayout.cpp`'s `gsWords` — keep them in agreement.
- An allocation is not an addressable extent (z-mask disaster, blss.md).

## Eviction
- `RendererCoreTexture::makeRoomFor()` evicts coldest-first (`pickVictim`: stale entries by LRU; when the whole resident set is in this frame's working set, the MOST recently bound — plain LRU makes a per-frame scan cycle its entire set).
- Never-evict textures aren't "pinned", they're not in the list: own `texbuffer_t` via `Texture::vramResident` (as `RendererCoreEnvMap`).
- Any texture miss/upload/update/free/eviction invokes the StaPip mutation barrier (submission-dma.md, batching).

## Seeing what a scene does
- **Debug** profile, grep the game's `bin/log.txt`:
  - `VRAMSTAT` per frame: binds/hits/uploads/**re-uploads**/evictions/resident/free MB/largest free block. `reup` per frame matters — each is a full PATH3 transfer.
  - `VRAMRES` (one line per resident allocation: name + words) and `VRAMEVICT` (victims since the last summary and what each was evicted for) NAME the problem — reach for them first ("12.17 re-uploads/frame" is not actionable; "one car's body texture is a third of the heap" is). An asset listing can't substitute (only part of a project is bound per view).
  - Census lives in an **anonymous namespace in the .cpp under `#ifndef NDEBUG`**, not a header field (a debug-only member is an ODR hazard if TUs disagree about `NDEBUG`). Victim list clears when PRINTED, not per frame (per-frame clearing missed the one eviction it existed for).
- `examples/showcase`: 6 allocations, 1.15 MB free (2.04 MB at 16-bit), never evicts — in a palettised project, measure before assuming a VRAM problem.
- **Attribute a thrash before touching policy.** Motor District garage: not policy, not per-frame allocation (28 uploads, zero re-uploads in 4 440 frames) but a working set at 84 % of a 0.75 MB heap, half of it three vehicle textures that bypassed palettisation. Opening the pause menu took it from 0.119 MB free / 121 KB largest block to 0.052 / 25 — the console's "four evictions + four re-uploads per frame".

## Lookup caches
- **`TextureRepository::findLinked`** (1.126.2): id → texture from 256 direct-mapped entries valid for one `Texture::linkGeneration`; every link/list change bumps it. **Change `Texture::links` only through `addLink`/`removeLink*`** (the vector is public; a direct `push_back` leaves stale answers). The uncached walk (every texture × links, per sprite) was 0.97 ms for an 85-sprite HUD on a PS2. Ids must be unique (engine-additions.md).
- **`Texture::residentHint`**: where the texture's entry sits in the resident list; `findAllocation`/`isResident` trust it only after checking the entry's id. New resident-list lookups go through them.
- HUD font prewarm in generated vehicle games uses normal evictable residency — no allocator/pinning change.

## Formats
Power-of-two sizes; PNG 32/24bpp or palettised 8/4bpp (palettised fastest). Non-paletted textures still have a `clut` `TextureData` with `clut->data == nullptr`.
