# Materials: model preview, duplication and texture painting

The **Material Editor** (*Tools > Material Editor*) authors the project's
materials — plain Wavefront `.mtl` files under `res/materials/` (universal
libraries you assign to any object) and `res/models/` (a model's own library).
Every entry is a color (`Kd`), a brightness multiplier and an optional PNG
texture (`map_Kd`); primitives use a file's **first** entry, models resolve
their `usemtl` names against the assigned file, emitters take the first
entry's texture for their particles. Edits save to the file on every change —
the scene viewport updates live, and the PS2 game parses the very same file.

## Previewing on your actual model

The preview pane's shape picker offers the four unit primitives **and every
`.obj` under `res/models/`**. Pick a model and the open `.mtl` acts exactly as
it does in the game: it overrides the model's own libraries, entries are
matched to the model's `usemtl` names, and the entry you are editing shows
its **staged** values live while you drag sliders. Opening the editor from a
model object's *Properties > Material > Edit...* (or opening a model's own
`.mtl`, which auto-picks the sibling `.obj`) lands directly on the right mesh.

Camera: **drag** orbits (left button normally, **right button while
painting**), **mouse wheel** zooms, *Spin* keeps the turntable going (paused
while painting).

## Duplicating a material

**Duplicate** (next to the file path) copies the open `.mtl` under a fresh
`-copy` name — and copies every texture it references (once each, renamed
`<newname>-<texture>.png`), rewriting the `map_Kd` lines. The duplicate is
fully independent: recolor or repaint it without ever touching the original's
pixels. The copy opens immediately.

## Painting straight onto the mesh

Tick **Paint** in the preview pane to paint on the displayed mesh — strokes go
through the surface's UVs straight into the selected entry's texture, so what
you see is the flat PNG being authored in place. There is no separate bake
step: **the PNG on disk is updated on every mouse release**, and that flat
texture is exactly what the PS2 loads (the build's texture bake still
quantizes it like any other PNG).

- **Color brush** — a soft round brush; color, size (in texture pixels) and
  opacity.
- **Texture brush** — paints with another PNG as a pattern. The pattern is
  sampled in *texture space* and tiled (with a *Tile* density slider), so
  strokes reveal one continuous image instead of stamping it — think laying
  grass or rust onto parts of a prop.
- **Undo stroke** — a small per-stroke undo stack (assets are outside the
  project undo history, same as imports).

Painting works on the primitives too (they have generated UVs), but it shines
on models: only faces mapped to the entry you are editing take paint, so a
multi-material model masks itself naturally.

Notes on UVs:

- Wherever the model maps several faces to the same texels (mirrored or
  shared UVs — like a cube whose six faces reuse the 0..1 square), painting
  one face paints them all. That is a property of the UV layout, not the
  brush; give faces unique UV islands in your DCC tool for one-sided paint.
- Strokes wrap across texture edges (PS2 textures repeat), and a stroke that
  jumps across a UV seam breaks cleanly instead of smearing a line across
  unrelated texels.

## New paintable texture

*Texture > New paintable texture...* creates a blank power-of-two PNG
(64–512, pick a fill color) next to the `.mtl`, assigns it as the entry's
`map_Kd` and switches Paint on — the canvas for texturing an untextured
model from scratch, entirely inside the editor.
