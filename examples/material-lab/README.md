# material-lab — the material pipeline on one pedestal

A small diorama built to show the whole Material Editor toolchain in one
place: a stone **altar** whose texture is a live **layer stack** (base stone
→ **Baked AO** → **smart masks**), brick **pillars** and tiled **orbs**
whose textures ship inside a shared **texture atlas**, and a reusable
**material preset**.

## What to look at in the game (Build & Run)

- The altar's shading is *in the texture*: contact darkening where the
  blocks meet (raytraced **baked AO**), grime pooled in the same crevices
  (an **Occlusion dirt** smart mask) and lightened block edges (an **Edge
  wear** mask broken up by world-space noise). No runtime lighting is doing
  any of that.
- The boot log prints `Texture atlas: 3 textures in 2 page(s)` — the
  altar, pillar and orb textures ship as two shared 256×256 pages (one GS
  VRAM allocation each) instead of three files. The `.mtl`s in `bin/` carry
  the `# tyra-uvrect` remap hints; sources in `res/` are untouched.

## What to look at in the editor

1. Open the project, then **Tools > Material Editor** and pick
   `models/altar.mtl` — the preview lands on the altar model.
2. Turn on **Paint**: the **Layers** list shows the full stack —
   `Background`, `Baked AO` (multiply), `Cavity grime *` and `Edge wear *`
   (the `*` marks generated smart-mask layers). Select a mask layer and
   tune the generator that appears below (Range, Breakup, Seed…) — the
   masks regenerate live from the bake.
3. The **Bake maps** block already carries this material's parameters
   (persisted in the `.mtl` as `# tyra-bake`): set *Preview* to
   `AO on material` or `Map view` and drag *Max distance* to watch the
   progressive re-bake; *Bake & add AO layer* refreshes the `Baked AO`
   layer in place.
4. **Presets** (next to the layer buttons) lists `worn-stone` — apply it to
   any other material (try `materials/pillar.mtl`) and the same wear recipe
   regenerates from *that* mesh's own bake.
5. Try the **display modes** next to *Spin*: the UV checker, the **PS2
   CLUT** preview with the memory budget line, and the **UV** panel with
   hover sync; **Validate UVs** reports the box primitives' by-design
   overlaps.
6. With the *debug* build profile, repaint any pillar texture while the
   game runs — **texture hot reload** pushes the stroke to the console
   within a fraction of a second (the altar texture is atlased, so it
   needs a rebuild — the log explains).

Docs: [material-baking.md](../../docs/material-baking.md),
[material-painting.md](../../docs/material-painting.md),
[texture-atlasing.md](../../docs/texture-atlasing.md),
[live-link.md](../../docs/live-link.md).
