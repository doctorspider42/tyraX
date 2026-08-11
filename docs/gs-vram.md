# GS VRAM residency

Developer note, not a user guide. Covers how the engine fork
(`vendor/tyra/engine/.../renderer/core/gs/renderer_core_gs_vram.*` and
`.../texture/renderer_core_texture.*`) hands out the PlayStation 2's 4 MB of
graphics memory, what the numbers actually are, and what happens when a scene
asks for more than fits.

## The budget

GS VRAM is 4 MB = 1 048 576 "words" (1 word = 4 bytes), and the allocator
counts in words. A project spends it like this — the two frame buffers are the
dominant term, which is why the colour depth below is the biggest single lever
anyone has over the texture budget:

| Region | Words (32bpp) | Words (16bpp) | Notes |
|---|---|---|---|
| Frame buffer × 2 (512×448) | 458 752 | 229 376 | halves again in the `InterlacedField` scan mode |
| Z buffer (512×448, always 32bpp) | 229 376 | 229 376 | |
| Post-fx scratch `lowVram[0..1]` | ~8 192 | ~4 096 | bloom/DoF blur chain; follows the frame format |
| Film-grain noise (always 32bpp) | 4 096 | 4 096 | uploaded, not rendered |
| Env-map target + its z (128×128) | 32 768 | 32 768 | **only if the project has a reflective `@sky` material** |
| Camera-feed target + its z (128×128) | 32 768 | 32 768 | **only if the project has a feed camera** |
| **Left for textures** | **~282 000 (1.08 MB)** | **~511 000 (1.95 MB)** | plus 65 536 more per unreserved target |

`Pal576i` costs ~380 KB more at 32bpp (three 512-line buffers); at 16bpp the
colour half of that comes back.

Measured on `examples/showcase` (4-bit textures), `VRAMSTAT`'s `freeMB`:

| Build | Free |
|---|---|
| before this work | 0.87 MB |
| exact sizing + neither optional target reserved | **1.15 MB** |
| ...and 16-bit colour | **2.04 MB** |

### What a texture costs

GS memory is **paged and swizzled**: a page is 2048 words (8 KB) and holds 32
blocks of 64 words, and the texels of one page are spread over those blocks in
a scrambled order. So a texture does not occupy `width * height` words — it
occupies every block up to the highest one its texels reach. `getSize()`
computes exactly that (whole pages once a texture is bigger than one, whole
blocks inside a single page). At 32bpp:

| Texture | Words | Of the 1.08 MB heap |
|---|---|---|
| 64×64 | 4 096 | 1.5% |
| 128×128 | 16 384 | 5.8% |
| 256×256 | 65 536 | 23% |
| 512×512 | 262 144 | 93% — one texture, basically the whole heap |

A **CLUT** is addressed by CBP in blocks, not pages: a 16-entry palette is one
block (64 words, 256 B) and a 256-entry one is four (256 words, 1 KB).

Palettizing is the single biggest lever after the colour depth:
*Preferences > Build > Textures* at 4-bit turns a 256×256 into ~1/8 of the
pixels plus a 64-byte CLUT (8 192 words instead of 65 536). It is why the
`showcase` example, which draws a whole village, never comes close to filling
VRAM.

> **This used to be a flat `+1024 * 2` words per allocation**, an upstream
> workaround carrying the comment *"without this hack, textures are
> overlapping ourselves"*. It was not derived from anything, and it was wrong
> in both directions: a 16-entry CLUT carrying 64 bytes of data was charged
> **8.25 KB** (so twenty palettized textures spent 160 KB of a 1 MB heap on
> palette padding alone), while an extreme aspect ratio was still
> UNDER-allocated — a 512×32 PSMCT16 strip spans 8 pages and reaches word
> 15 360, and was handed 10 240. The overlap the hack was named after was
> never fixed by it, only made less likely.

## Colour depth

*Preferences > Build > Colour depth* picks the frame buffers' pixel format
(`ProjectSettings::colorDepth`, `EngineOptions::colorDepth`):

- **32-bit** (`PSMCT32`) — the stock 8-8-8-8 buffer.
- **16-bit** (`PSMCT16`) — 5-5-5-1. Halves the two frame buffers and hands all
  of it to the texture heap, which roughly doubles. This is what makes the
  taller scan modes practical: 1080i at 32bpp leaves *less* texture VRAM than
  plain 480i does, and at 16bpp it leaves more than twice as much.

The **z buffer stays 32-bit either way**. A 16-bit z would save another
229 376 words, but at the projection's near/far ratio (0.1 / 51200) its
resolution collapses with distance — roughly 1.5 world units at 100 units out —
and terrain and baked shadows start z-fighting. That is a separate decision
with a real quality cost, not a free saving.

The cost of 16-bit colour is 32 levels per channel instead of 256, i.e.
banding in anything smooth — skies, fog, the post-fx blur. **GS ordered
dithering** (the `DTHE` + `DIMX` registers, *Preferences > Build > Dithering*,
on by default) trades that banding for fine noise a TV blurs away. The
hardware only dithers 16-bit destinations, so the switch does nothing at
32-bit. Measured on a `showcase` sky band (500×120 px of the captured frame):

| Build | Unique colours | High-frequency energy |
|---|---|---|
| 32-bit | 2 101 | 1.43 |
| 16-bit, dither off | 1 020 | 1.31 |
| 16-bit, dither on | 5 810 | 4.29 |

The middle row is the quantization showing up as exactly half the levels; the
last is the dither turning it back into detail, at the price of visible grain
up close.

> **Anything that writes a `FRAME` register for the screen must use the
> framebuffer's PSM**, not a hardcoded `GS_PSM_32`: the drawing environment,
> every post-fx blit, and the env-map / shadow-map brackets' restores. The
> post-fx work buffers are allocated in the frame format for the same reason;
> the film-grain noise texture stays PSMCT32 because it is uploaded rather
> than rendered (`RendererCorePostFx::psmFor` is what keeps those apart).
>
> **ps2sdk's `GS_SET_DIMX` cannot express the dither matrix.** Each DIMX entry
> is a 3-bit signed value (-4..3) and that macro masks the entries with `0x03`,
> so the negative half of the standard matrix (encoded 4..7) silently collapses
> to 0..3 and the dither comes out one-sided. `renderer_core_gs.cpp` packs the
> qword by hand.

## Optional render targets

Two 128×128 targets, each with its own z buffer, cost 128 KB apiece — a
quarter of the 32-bit texture heap between them — and used to be reserved for
every project whether or not anything read them:

- the **dynamic env map**, read only by a material whose `refl` is the `@sky`
  token (docs/reflective-materials.md);
- the **camera feed**, read only where a feed camera exists
  (docs/texture-feeds.md).

They are opt-in now (`EngineOptions::envMapTarget` / `camFeedTarget`,
`RendererCoreEnvMap::setEnabled`). The editor's codegen decides: it scans the
project's shipped `.mtl` / `.tmdl` / `.tskl` files for `@sky` and its scenes
for a feed camera (`projectNeedsEnvMap` / `projectNeedsCamFeed` in
`templates.cpp`). Looking at the files rather than the model is deliberate —
that token reaches the game through files it loads at run time, so a
hand-edited `.mtl` and a material only a spawn-pool prefab uses both count.

A disabled target owns no VRAM and `getTexture()` returns **nullptr**; the
generated game null-checks it and simply draws without the reflection pass. So
the failure mode of a detection miss is a missing reflection, never a sampled
address that belongs to something else.

The **projected shadow map** was already lazy in the same spirit — the game
calls `shadowMap.allocate()` only when a scene has "Cast shadow" objects.

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

The only thing that moves the floor between them is a display-mode switch,
which calls `vram.reset()` after evicting every texture and then re-runs the
whole init sequence (`RendererCore::setDisplayOutput`).

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

## Measuring it

`RendererCoreVRamStats` counts binds / hits / uploads / re-uploads / evictions
and the free-VRAM low-water mark. `RendererCoreTexture::traceFrame()` (called
from `RendererCore::endFrame`) prints a `VRAMSTAT` line to the game's
`bin/log.txt` on every frame that evicted something, plus a summary every 120
frames:

```
VRAMSTAT f=1440 bind=27280 (+2520) hit=27273 (+2520) up=7 (+0) reup=0 (+0)
         evict=0 (+0) runs=0 res=6 peak=6 oofree=0
         freeMB=1.15381 minFreeMB=1.15381 largestKB=1181
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

These are the eviction-policy numbers, measured when the free list replaced the
bump allocator; the heap was ~0.87 MB free on `showcase` at the time (it is
1.15 MB now — see the budget section).

| Scene | re-uploads/frame before | after |
|---|---|---|
| `examples/showcase` (4-bit palettized, 6 allocations) | 0 | 0 |
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
  ~1.08 MB of texture heap at 32-bit colour and every 128×128 target is 3% of
  it. And ask whether it can be **opt-in**: two of the existing ones now are,
  and between them that was a quarter of the heap for projects that read
  neither.
- A new buffer that holds screen-shaped pixels should take
  `settings->getFrameBufferPsm()`, not `GS_PSM_32` — see the colour-depth
  section. One that is uploaded rather than rendered into (a lookup texture,
  noise) stays whatever format its data is in.
- `getSize()` is the only place that knows the GS storage layout. If you add a
  pixel format to it, give it a page geometry AND a block order, or leave the
  order null and let it round to whole pages (which is what the Z formats do —
  their block orders are permuted variants for which the corner rule the
  sub-page path relies on does not hold).
- The editor keeps a **host-side mirror** of this arithmetic in
  `menulayout.cpp` (`gsWords`) for the Menu Editor's "does it fit" check. It
  and `getSize()` must agree, or the editor reports menus as too big when they
  fit.
