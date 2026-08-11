# GS VRAM residency

Developer note, not a user guide. Covers how the engine fork
(`vendor/tyra/engine/.../renderer/core/gs/renderer_core_gs_vram.*` and
`.../texture/renderer_core_texture.*`) hands out the PlayStation 2's 4 MB of
graphics memory, what the numbers actually are, and what happens when a scene
asks for more than fits.

## The budget

GS VRAM is 4 MB = 1 048 576 "words" (1 word = 4 bytes), and the allocator
counts in words. A default project spends it like this:

| Region | Words | Notes |
|---|---|---|
| Frame buffer × 2 (512×448, 32bpp) | 458 752 | halves in the `InterlacedField` scan mode |
| Z buffer (512×448, 32bpp) | 229 376 | sized from the **raster**, so the [neural upscaler](neural-upscaler.md) shrinks it to 57 344 at 2×2 |
| Post-fx scratch `lowVram[0..1]` + film-grain noise | ~12 288 | bloom/DoF blur chain |
| Env-map target + its z buffer (128×128) | 32 768 | reflective materials |
| Camera-feed target + its z buffer (128×128) | 32 768 | texture feeds |
| **Left for textures** | **~282 000** | **≈ 1.08 MB** |

`Pal576i` costs ~380 KB more (three 512-line buffers) and leaves ~1 MB.

Each texture allocation also carries ~8 KB of padding (`getSize()` adds
`1024 * 2` words before block alignment — an upstream workaround for
overlapping textures), which is why many small textures are far more expensive
than their pixel count suggests. Concretely, at 32bpp:

| Texture | Words | Of the ~1.08 MB heap |
|---|---|---|
| 64×64 | 6 144 | 2% |
| 128×128 | 18 432 | 6.5% |
| 256×256 | 67 584 | 24% |
| 512×512 | 264 192 | 93% — one texture, basically the whole heap |

Palettizing is the single biggest lever: *Preferences > Rendering > Textures*
at 4-bit turns a 256×256 into ~1/8 of the pixels plus a small CLUT. It is why
the `showcase` example, which draws a whole village, never comes close to
filling VRAM.

## An allocation is whole PAGES, not pixels

`getSize()` counts a texture's **pixels**. The GS stores a texture in whole
8 KB **pages**, a row of pages at a time, so a texture that does not fill its
last page row still *owns* those pages — and the next allocation has to start
past them. For anything short and wide the two answers differ by a factor:

| texture | pixels (+ the 2 048-word pad) | pages actually spanned |
|---|---|---|
| 256×256 PSMCT32 | 65 536 + 2 048 | 4 × 8 = 65 536 ✔ |
| 64×64 PSMCT32 | 4 096 + 2 048 | 1 × 2 = 4 096 ✔ |
| **512×16 PSMCT32** (the debug HUD font) | **10 240** | **8 × 1 = 16 384** ✘ |

A PSMCT32 page is 64×32 texels. The 512×16 font is one page row of **eight**
pages, so it spans 16 384 words while being sized at 10 240 — and the next
texture was placed 10 240 words in, which is page 5 of the font's own 8. It
overwrote pages 5, 6 and 7: **every glyph from x = 320 rightwards**.

That is the bug behind *"opening the menu makes letters disappear"*. The HUD
read `V AM` because `R` sits at x = 480 and was the only glyph past x = 320 on
screen (`T`, at 496, was equally gone and simply not being drawn). The menu's
own font atlas is the allocation that lands on it — which is why a *menu
opening* triggers it, and why every scene that never opens one was fine.

Upstream's `// TODO: Without this hack, textures are overlapping ourselves`
sat on that 2 048-word pad. The pad is not the fix: it covers a width of 128
and nothing wider. `getSize()` now returns **at least** the page footprint,
`ceil(w / pageW) × ceil(h / pageH) × 2048`, with the page geometry per format
(PSMCT32 64×32, PSMT8 128×64, PSMT4 128×128, PSM16 64×64; the `8H`/`4HL`/`4HH`
formats are the high bits of a 32-bit buffer and keep 64×32).

It changes **nothing** for the frame and z buffers — those are page-aligned
already, and all five display modes come out identical either way — and
nothing for textures at least 32 rows tall. The one measured cost is the debug
font growing 10 240 → 16 384 words: **24 KB**, visible as the HUD's own `VRAM`
line moving 3.21 → 3.23 MB.

## Two regions

`RendererCoreGSVRam` splits VRAM in two:

- **The permanent region**, at the bottom, filled by `allocateBuffer()` during
  renderer init — both frame buffers, the z buffer, the post-fx scratch
  buffers, the noise texture, the env-map and camera-feed render targets. It is
  a plain bump region and is never released. `free()` does not know those
  addresses, so nothing — including a buggy caller — can reclaim them.
- **The texture heap** above it, managed by a **coalescing free list**.
  `allocate()` takes a best-fit block; `free()` returns it in **any order** and
  merges it with its neighbours.

Two things move the floor between them, and both work the same way — evict every
texture, `vram.reset()`, re-run the allocation sequence:

- a **display-mode switch** (`RendererCore::setDisplayOutput`), which re-runs the
  whole init including the video mode;
- **`blss.configure()`** turning the [neural upscaler](neural-upscaler.md) on,
  through `RendererCore::rebuildPermanentBuffers()` — the same branch minus the
  mode. The z buffer is sized from the raster and the raster scale is not known
  until `configure()` runs, which is *after* z was allocated. Generated games
  call it at the top of `init()`, before `buildScene()` loads a single asset, so
  the eviction has nothing to evict and the "permanent buffers before any
  texture" rule below still holds. **A new caller of it later in a frame would
  break that rule**, which is why the hook is gated on
  `RendererCoreGS::needsBufferRealloc()` and fires at most once.

  **A game whose SCENES disagree about the upscaler pins the layout instead**
  (per-scene BLSS, docs/neural-upscaler.md). `RendererCoreGS::setZRasterScale`
  fixes what the z buffer is sized for independently of the raster scale the
  projection is currently using, so a project with some scenes upscaled and some
  native sizes z for the FULL display once, at init, and `needsBufferRealloc()`
  stays false for the rest of the run. That is what makes a per-scene switch
  cost no eviction: it never asks for this branch at all. The price is the
  z-buffer saving — such a project keeps the low-res colour target resident
  through its native scenes (0.25 MB at 512x512) instead of trading it for a
  smaller z (0.75 MB).

VRAM-*resident* textures — the dynamic env map and the camera feed, whose
pixels are rendered into GS memory rather than uploaded — bind their own
`texbuffer_t` (`Texture::vramResident`) and never enter the heap or the
residency list at all.

## Why the free list exists (the bug it fixes)

Upstream's `free()` was one line:

```cpp
void RendererCoreGSVRam::free(const int& address) { pointer = address; }
```

a stack pop. Freeing the *newest* allocation is correct; freeing anything else
rewinds the bump pointer **underneath still-live textures**, and the next
allocation is handed memory that other textures are rendering from.

That is not theoretical — streaming layers hit it on every unload. A fixture
with six textured boxes, three of them in a layer that unloads at t=6 s and
loads again at t=10 s, showed on the old allocator:

```
free 821248, free 839680, free 858112   (newest live allocation: 931840)
freeMB 0.406 -> 0.727   (pointer rewound below three live textures)
freeMB 0.727 -> 0.516   (reload allocated straight over them)
```

and on screen two surviving boxes started drawing a different box's texture.
With the free list the same fixture round-trips exactly — `0.406 -> 0.617 ->
0.406`, largest free block unchanged at 416 KB — and the picture after the
reload is identical to the picture before the unload.

## Eviction policy

When an incoming texture does not fit, upstream deallocated **every** resident
texture and started over, so one over-budget texture cost a full re-upload of
the entire working set on the following frames.

`RendererCoreTexture::makeRoomFor()` instead evicts one allocation at a time
until the newcomer fits, choosing the victim in two tiers
(`pickVictim()`):

1. **Stale entries** — not bound in this frame *or* the one before it. Coldest
   first (lowest bind sequence), ties broken by the larger allocation. These
   are textures that went off-screen or belong to a layer that was streamed
   out, and they are always the cheapest thing to give up.
2. **Everything is in the live working set** — i.e. what the scene draws
   genuinely does not fit. Here LRU is the *worst* policy: a scene re-binds its
   textures in the same order every frame, so the coldest entry is precisely
   the one that will be requested first next frame, and the whole set cycles.
   The victim is the **most recently bound** allocation instead
   (scan-resistant MRU): the head of the scan stays resident and only the
   overflow tail keeps re-uploading.

The two-frame window in tier 1 is deliberate. "Not bound yet in this frame" is
not evidence of coldness — the tail of last frame's scan simply has not come
round again, and evicting it is a guaranteed miss milliseconds later.

Fragmentation is the risk a bump pointer trades away. Best-fit plus coalescing
keeps it small in practice (the streaming fixture above returns to a single
416 KB block), and when a request cannot be served despite enough *total* free
space, the eviction loop keeps going — worst case it empties the heap, which is
exactly the old behaviour, so the failure mode degrades rather than breaks.

## Reading it without a debug log

The debug HUD's `VRAM 3.21/4 MB` line, directly under `MEM` (*Preferences >
Build > Show memory usage*), answers "how much of the 4 MB is gone". It is
phrased as used-over-total so it reads the same way round as the line above it.

**`MEM` and `VRAM` are different pools, and only one of them moves with the
display mode.** Reported as *"I keep switching display modes and the editor
shows the same memory usage the whole time — shouldn't it drop when I go from
HD to 480p?"* `MEM` is the EE's 32 MB of main memory; a display mode never
touches it. The frame and z buffers live in the GS' separate 4 MB, and that is
the pool a taller mode eats. Measured on the same fixture, changing only
`displayMode`:

| mode | buffers + z | free for textures |
|---|---|---|
| 1080i (448×540) | 3.21 MB | **0.79 MB** |
| 480p (448×448) | 2.76 MB | **1.24 MB** |

0.45 MB between them — 448×92 px × 4 B × three buffers (two display + z), minus
page alignment. **HD costs about a third of the texture budget**, and on a
scene with almost nothing in it the reading is still ~3 MB: what fills VRAM
first is the framebuffers, not the scene.

## Measuring it

`RendererCoreVRamStats` counts binds / hits / uploads / re-uploads / evictions
and the free-VRAM low-water mark. `RendererCoreTexture::traceFrame()` (called
from `RendererCore::endFrame`) prints a `VRAMSTAT` line to the game's
`bin/log.txt` on every frame that evicted something, plus a summary every 120
frames:

```
VRAMSTAT f=240 bind=1859 (+1737) hit=1852 (+1734) up=7 (+3) reup=0 (+0)
         evict=0 (+0) runs=0 res=6 peak=6 oofree=0
         freeMB=0.870605 minFreeMB=0.870605 largestKB=891
```

- `(+n)` are deltas since the previous line.
- `up` / `reup` — texture uploads, and how many of those were re-sending a
  texture that had been evicted. **`reup` per frame is the number that
  matters**: each one is a PATH3 transfer of the whole texture.
- `evict` / `runs` — allocations dropped, and how many `makeRoomFor()` calls
  had to drop anything.
- `res` / `peak` — allocations resident now / high-water mark.
- `oofree` — frees of something that was not the newest allocation. Harmless
  now; on the old allocator every one of these was a corruption.
- `largestKB` — largest single free block, i.e. the fragmentation read-out.

The counters are always compiled (a few integer increments per bind); only the
logging is debug-only, since `TYRA_LOG` compiles away under `NDEBUG`. Use a
**debug** build profile to see it.

## Measured behaviour

All PCSX2, software renderer, PAL, 512×448.

| Scene | re-uploads/frame before | after |
|---|---|---|
| `examples/showcase` (4-bit palettized, 6 allocations, 0.87 MB free) | 0 | 0 |
| 3×256² + 3×128² 32bpp, just over budget | 9–10 | **3** |
| 6×256² 32bpp, ~2.4× over budget | 10 | 8 |

The showcase numbers are byte-identical before and after — a realistic project
never evicts anything, so this work changes nothing for it. The interesting
column is the middle one: a scene that is *slightly* over budget used to pay as
if it were massively over budget, and now pays roughly what it overspends. A
scene that is genuinely 2.4× over budget still thrashes, because no policy can
invent VRAM — the answer there is palettized textures, an atlas, or smaller
source images.

Frame cost on the just-over-budget fixture (vsync off, so the 50 Hz cap does
not hide it): 5.51 → 5.33 ms of EE time per frame, and PCSX2's GS thread load
dropped 39% → 23% — that thread is where emulated PATH3 transfers land, so it
is the closest proxy available for what the transfers cost a real console.

## Rules for engine work

- Anything allocated with `allocateBuffer()` is permanent. Allocate it during
  init, before any texture, and re-allocate it after `vram.reset()` if the
  display layout changes.
- Never add a texture to the residency list that must not be evicted — use
  `Texture::vramResident` with your own `texbuffer_t` instead (see
  `RendererCoreEnvMap`).
- `free()` ignores addresses it did not hand out. That is the guard rail for
  the permanent region; do not "fix" it into an assert.
- If you add a new permanent buffer, update the budget table above — it is
  ~1.08 MB of texture heap and every 128×128 target is 3% of it.
