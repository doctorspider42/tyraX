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

The property column ends in a **Bake maps** block: a progressive raytraced
bake of the preview mesh (ambient occlusion, curvature, thickness and more)
that can land as a *"Baked AO" multiply layer* on the entry's texture — see
`docs/material-baking.md`.

## Duplicating and deleting a material

**Duplicate** (next to the file path) copies the open `.mtl` under a fresh
`-copy` name — and copies every texture it references (once each, renamed
`<newname>-<texture>.png`), rewriting the `map_Kd` lines. The duplicate is
fully independent: recolor or repaint it without ever touching the original's
pixels. The copy opens immediately.

**Delete...** removes the open `.mtl` from the project after a confirmation
(the same dialog as the Assets panel, with a usage count). Objects that used
it fall back to plain color, models to their own libraries, a terrain to the
checker preview; referenced textures stay on disk.

## Painting straight onto the mesh

Tick **Paint** in the preview pane to paint on the displayed mesh — strokes go
through the surface's UVs straight into the selected entry's texture, so what
you see is the flat PNG being authored in place. There is no separate bake
step: **the PNG on disk is updated on every mouse release**, and that flat
texture is exactly what the PS2 loads (the build's texture bake still
quantizes it like any other PNG).

- **Color** — a soft round brush; color, size (in texture pixels) and
  opacity.
- **Brush** — paints with a project brush image, GIMP-style: each dab
  **stamps the whole image**, scaled to the brush *Size* — the PNG's alpha
  is the dab's shape, its RGB is the paint (an irregular splat with soft
  alpha makes organic blotches; grayscale-on-transparent works like a GIMP
  grayscale brush). Brushes are PNGs in `res/brushes/` — **global to the
  project** and never shipped with the game; add one with *Import brush
  from PNG...* in the brush picker.
- **Eraser** — takes paint off the active layer (on the Background it makes
  the texture transparent — handy for authoring decal cutouts).
- **Spacing** — the distance between dabs along a stroke, as a % of the
  brush size (GIMP semantics; applies to every mode). Low values draw one
  continuous line; 100% and up drops clearly separated stamps — drag fast
  or slow, the spacing stays exact because the leftover distance carries
  across mouse samples.
- **Opacity / Vary** — *Opacity* sets how strongly each dab covers what is
  underneath; *Vary* randomly reduces every dab's opacity by up to the set
  percentage — strokes come out hand-worn and organic instead of a uniform
  coat (great with high Spacing for scattered, weathered detail).
- **Angle / Random** (brush images) — *Angle* rotates every dab by a fixed
  amount, so bricks, planks or arrows land in exactly the orientation you
  want; *Random* re-rolls the rotation per dab instead — organic scatter
  for leaves, splats, rubble.
- **Live dab** — with the toggle on (next to *Paint*), the stamp is
  previewed **under the cursor before you click**: one uncommitted dab is
  drawn on the preview each frame (nothing touches the layers or the file
  until you actually press the button). Line up a brick, see it land.
- **Undo** — the Material Editor keeps its own undo stack of paint strokes,
  layer add/remove AND committed property edits (color, brightness,
  texture...). Press **Ctrl+Z while the window is focused** (or the *Undo*
  button next to Duplicate/Delete) to step back; scene undo is untouched,
  and Ctrl+Z anywhere else still undoes scene changes as before. Assets are
  outside the project history, same as imports — this stack is the editor's
  own.

## Layers

Painting happens on a **layer stack** (the *Layers* box in the paint pane):
the Background plus any number of transparent layers above it, each with a
**blend mode** (Normal / Multiply / Add / Overlay), an **opacity** slider
and a visibility toggle. Strokes land on the *active* (selected) layer;
`+` adds a layer above it, `-` deletes it (undoable), Up/Down reorder.

What ships is always the **flattened composite** — the texture PNG next to
the `.mtl` is rebuilt after every change, so the PS2 pipeline sees a plain
texture and knows nothing about layers. The stack itself persists in a
`<texture>.png.layers/` sidecar folder (one PNG per layer + a tiny
manifest) that the texture bake skips — it never reaches the game or the
ISO, and it travels along when you Duplicate the material. A texture you
never added layers to stays a plain file, no sidecar.

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
