# Texture atlasing

![Texture atlasing in Rendering preferences](img/project-preferences-rendering.png)

Texture atlasing packs small material textures into shared **256×256 pages**
at build time — *Project > Preferences > Rendering > Texture atlasing*
(default off). The GS then keeps **one VRAM allocation per page** instead of
one per texture, and draw batches switch textures less.

**Look at what it did before trusting it.** *Tools > Texture Atlas* shows every
page with its members and a preview, every texture the packer refused **with
the reason**, and the VRAM arithmetic — and it is where the two decisions that
are yours are made. The headless twin is

```bash
tyrax-editor --atlas-report <projectDir>
```

which prints the same thing plus a machine-readable
`[atlas] pages=… members=… excluded=… savedKb=…` line. Both exist because this
feature used to be one checkbox and one line in the boot log: nothing said
which textures shared a page, and nothing said when a texture was silently
disqualified. On the shipped `examples/night-walk` the honest answer to "what
did it pack?" was **nothing at all**, and finding that out meant reading the
packer.

## It does not always save VRAM — measure

A page is quantized **as one image**, so in a palettized project its members
ship at **8 bits per pixel** even if they were 4-bit on their own, while the
page itself is a full 256×256 allocation whatever it holds. Measured on
`examples/night-walk` (a 4-bit project, `--atlas-report`):

| | |
| --- | --- |
| members, unpacked | 8 KB |
| the page they share | 65 KB |
| **net** | **costs 57 KB** |

That estimate is the engine's own arithmetic, ported rather than approximated
(`RendererCoreGSVRam::getSize` - GS memory is paged *and* swizzled, so an image
occupies up to the highest block its texels reach), and it was checked against
the running game: `VRAMSTAT` reports **0.115 MB** free with atlasing off and
**0.060 MB** with it on, i.e. 56.5 KB against the predicted 57.

So on that project atlasing buys fewer allocations and fewer texture switches
per frame, and **spends** bytes. The window prints the same two numbers in
green or amber; there is no need to guess which case you are in.

## The page's depth

A page is quantized as one image, and **how deep** decides everything above.
It follows the project's own texture quality — a 4-bit project gets a 4-bit
page — and any texture can ask for more (*page: 4-bit / 8-bit / full* on its
row); the group takes the **highest** request its members make.

| page | VRAM | break-even (64×64 members) |
| --- | --- | --- |
| 8-bit, 256 colours | 65 KB | ~16 — and 16 is a **full** page |
| 4-bit, 16 colours | 32.25 KB | ~8 |

That is the difference between "atlasing a palettized project can never pay for
itself" and "it pays from half a page onwards". On `night-walk` the net cost
falls from 57 KB to 24 KB; `VRAMSTAT` reads 0.115234 MB free with no atlas,
0.0600586 with an 8-bit page and 0.092041 with a 4-bit one — 56.5 KB and
23.7 KB against predictions of 57 and 24.

**What the shared palette costs, measured** (mean absolute error against the
source art, on the worst case: a red door and a blue-grey garage door on one
page):

| | doors | wall_garage |
| --- | --- | --- |
| shipped alone, project's own 4 bits | 6.2 | 6.4 |
| **4-bit page** (16 colours shared) | 10.6 | 9.9 |
| **8-bit page** (256 colours shared) | **1.4** | **1.4** |

Two things worth taking from that. The default is a *modest* loss against
shipping the texture on its own — not the collapse "16 colours for everything"
suggests — because a 4-bit project's textures only had 16 colours each to begin
with. And an 8-bit page is a quality **upgrade** for a 4-bit project (256
shared colours beat 16 private ones) that you pay for in VRAM: pin one member
of the group to *page: 8-bit* when a page carries art that deserves it.

(An earlier version of this page claimed a flat "+~8 KB allocation overhead per
texture", which was true of an engine that charged every allocation a fixed
padding. `RendererCoreVram::getSize` computes the real swizzled footprint now,
so the saving has to come from sharing pages — not from an overhead that no
longer exists.)

`examples/texture-atlas` is the scene where it pays: thirty 32×32 crate
textures on one page, **65 KB** back, measured from the game's own `VRAMSTAT`
(0.2349 MB free with atlasing off, 0.2986 with it on) against the 65 KB the
report predicted. Its README explains why small palettized textures are so
wasteful on their own - the GS rounds a 4-bit texture's width up to 128 texels,
so a 32×32 prop holds 512 bytes and occupies 3.25 KB.

## What gets packed (conservative by design)

A texture joins a page only when **every consumer samples it with plain
0..1 UVs**:

- `map_Kd` textures of **static `.obj` models** whose textured submesh UVs
  stay within 0..1 (checked against the real mesh), and of **primitive
  objects** (box/sphere/cylinder/cone/plane — their generated UVs are 0..1
  by construction);
- baked size **≤128×128** (larger fills half a page alone — no sharing win);
- the token may point into a **subdirectory** (`map_Kd Textures/wall.png`) —
  the page is still written into the `.mtl`'s own folder, so the rewritten
  reference stays a same-directory token. Until 1.60.0 any token carrying a
  separator was refused, which disqualified every asset pack that keeps its
  images in a `Textures/` subfolder — silently, and that is exactly what
  `night-walk` was.

Excluded, and the window says which of these applied:

- **terrain** base/layer textures — they tile (UVs far beyond 0..1);
- **emitters** — particle corner UVs are fixed inside the VU1 program;
- **decals / mirrors / portals** — separate ST paths;
- **refl sphere maps** — STs are computed at runtime from normals;
- materials with a **tiling factor** (`map_Kd -s`);
- textures whose consumers carry a **per-asset quality override**
  (`textureQuality`) — a pinned quality is deliberate, and pages re-quantize
  as one image;
- a texture the author **kept out** (below);
- a texture outside `res/models`, `res/materials` and `res/textures` - the bake
  only rewrites materials and quantizes textures there, so packing one from
  anywhere else would drop its own PNG from the bake while its `.mtl` kept
  pointing at it: the model comes out **untextured**. Found by building the
  example above with its props in `res/props/` - thirty white boxes;
- **the only member of its group** — a lone texture on a 256×256 page pays a
  whole page and loses its own palette for nothing, so that page is dropped
  and the texture ships on its own;
- HUD/menu/font sprites — separate 2D pipelines with their own bakes.

## What you control

Both live per texture, in *Tools > Texture Atlas* (page members and refused
textures alike), and are stored in the project's `atlasControl` section.

- **Keep out** — never pack this texture. Reach for it when its colours must
  not share a page's palette, or when a streamed layer should be able to drop
  it on its own. (Pinning a per-asset texture quality has always had this side
  effect; this is the same decision said out loud.)
- **Group** — pack it with everything carrying the **same group name**,
  instead of with its `.mtl`'s folder. This is the important one: **a page is
  one allocation and one shared palette**, so what shares a page should be
  what is on screen together. The folder layout only approximates that, and a
  page whose members come from two rooms keeps both resident whenever either
  one is. The window warns when a page's members are used by more than one
  scene.

Pages are numbered per directory, and the page file always lands in the
`.mtl`'s own folder — a group only decides *who shares*, never *where the file
goes*.

## Palette buckets

Within a group, members are bucketed by a coarse average hue (six colour
buckets plus one for near-grey) and pages are cut per bucket, so a vivid red
sign does not spend a page's palette against a blue crate — which matters far
more at 4 bits than it ever did at 8. The split only
happens for a group with **more than one page's worth of content** — below
that, splitting a half-empty page costs more VRAM than the palette buys back.
Greys go together on purpose: they sit happily on any palette and would
otherwise scatter across every page.

## How it works

`src/texatlas.cpp` computes a **deterministic plan** (eligibility, grouping,
bucketing, shelf packing, page assignment) that every consumer reuses, so they
can never disagree within a build:

- **texbake** composites the member pixels into
  `.res-baked/<dir>/tyra-atlas-N.png` (each member keeps a 2-texel
  edge-dilated gutter, so bilinear filtering never reaches a neighbour),
  skips the members' individual baked PNGs, and **rewrites the baked
  `.mtl`**: `map_Kd` points at the page and a `# tyra-uvrect u0 v0 du dv`
  hint line follows. Sources in `res/` are never touched — the editor
  viewport keeps reading the originals.
- For **static models** the rect never reaches the console: the model bake
  multiplies it into the vertex UVs it writes to the `.tmdl`, per LOD level
  as well (see [model-pipeline.md](model-pipeline.md)). The hint line still
  goes into the baked `.mtl` for the material's other consumers, and the
  engine's `LeanObjLoader` still applies it on the fallback `.obj` path.
- `loadMtl` exposes the rect so the generated game's **primitive builders**
  remap their generated STs the same way (one multiply in `pushVert`).
- the boot log prints `Texture atlas: N textures in M page(s)`.

Pages quantize **as one image**: a palettized project gets a shared
**256-color CLUT per page** (the era-authentic trade — sharing a palette is
how PS2 games did it), a full-color project gets full-color pages.

## Notes

- The plan re-computes every build; adding/removing textures reshuffles
  pages safely (the baked `.mtl` rewrite and the pages always move
  together). The window's *Recompute* button re-reads it on demand.
- **Texture hot reload** (docs/live-link.md) skips atlased textures — a
  repaint of a page member needs a rebuild (the editor detects the missing
  individual PNG in `bin/` and simply does nothing).
- `bin/` is additive across builds, so previously shipped individual PNGs
  may linger there after enabling the atlas; they are unreferenced. A clean
  rebuild (or deleting `bin/`) clears them.
