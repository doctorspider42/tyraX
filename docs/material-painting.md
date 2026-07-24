# Materials: model preview, duplication and texture painting

The **Material Editor** (*Tools > Material Editor*) authors the project's
materials — plain Wavefront `.mtl` files under `res/materials/` (universal
libraries you assign to any object) and `res/models/` (a model's own library).
Every entry is a color (`Kd`), a brightness multiplier and an optional PNG
texture (`map_Kd`); primitives use a file's **first** entry, models resolve
their `usemtl` (or, for animated `.glb`/`.fbx`, their part) names against the
assigned file, emitters take the first entry's texture for their particles.
The override applies to animated models too — it is baked into the model at
build time (see [docs/animated-models.md](animated-models.md#material-override-mtl)). Edits save to the file on every change —
the scene viewport updates live, and the PS2 game parses the very same file.

## Previewing on your actual model

The preview pane's shape picker offers the four unit primitives **and every
model under `res/models/`** — static `.obj` and animated `.glb`/`.fbx` alike
(the animated ones are shown in bind pose). Pick a model and the open `.mtl`
acts exactly as it does in the game: it overrides the model's own libraries,
entries are matched to the model's material names (a `usemtl` for `.obj`, a
glTF/FBX part name for animated models), and the entry you are editing shows
its **staged** values live while you drag sliders. Opening the editor from a
model object's *Properties > Material > Edit...* lands directly on the right
mesh (for a static model's own `.mtl`, the sibling `.obj` is auto-picked).

**Multi-part models** (a character with skin/shirt/pants parts, the spider
with body/legs/jaw...) are edited **one entry at a time** — the *Entry*
combo at the top of the property column picks which part you are working
on (untextured entries are marked), and everything below follows: texture,
paint layers, smart masks, bake, the UV panel (other entries' islands stay
visible, dimmed). Two shortcuts make this fast: **click a part right in
the 3D preview** to jump to its entry (hover names it; painting keeps the
left button for the brush), and when an entry has no texture yet the
Layers box offers **Create texture for this entry** — one click and masks,
presets and painting have somewhere to land. "Mud only on the clothes" is:
click the shirt, create/apply, click the pants, apply again.

An animated model has no sibling `.mtl` to assign, so the material picker in
*Properties* has a **+ New material from this model...** entry: it extracts the
model's built-in materials (part names, base colors, embedded textures) into a
fresh `res/materials/<model>.mtl`, assigns it as the override and opens it here
previewed on the model. Editing that file is then how you recolor/retexture the
model on the PS2 — its built-in materials are the starting point, not a wall.
(A material is matched to a part by name; a model whose material is **unnamed**
can't take a name-keyed override — name it in your modelling tool first.)

Camera: **drag** orbits (left button normally; the **right button always**
orbits, painting or not), **mouse wheel** zooms, *Spin* keeps the turntable
going (paused while painting). **Grabbing the preview unchecks Spin** — the
moment you rotate by hand, the turntable stops fighting you and your
framing stays put; re-tick it to resume. The camera can dip to low angles
for hero shots.

The **splitter between the property column and the preview is draggable** —
grab the divider line and trade property width for preview width; the split
persists per machine (editor.ini), like the UI scale.

Next to *Spin* sit the **display mode** (Solid / Wireframe overlay /
**UV checker** — a generated checker replaces every texture so stretch and
texel density read at a glance / **PS2 CLUT** — see below) and the **UV**
toggle, which splits the preview: the 3D mesh on top, the **UV layout panel** below — the entry's
triangles drawn over its live texture (wheel zooms around the cursor, drag
pans). The two views hover-sync both ways: rest the mouse on a face in 3D
and its texture region lights up in the panel (with a dot on the exact
texel); hover a triangle in the panel and it is outlined on the mesh.
Overlapping UVs show themselves naturally — one 3D face lights up every
triangle sharing its region.

## The PS2 CLUT preview and the memory budget

The **PS2 CLUT** display mode shows the texture as the console will
actually sample it: palette-quantized through the *same median-cut
quantizer the build's texture bake ships* (16 or 256 colors — "Project
policy" resolves this material's per-asset override or the project's
texture-quantization preference), with a **dithering comparison** combo:
Floyd–Steinberg (what the bake uses), Ordered/Bayer (stable pattern) or
none (raw banding). Painting keeps working — strokes show up already
quantized, which is exactly how they will ship. The quantization happens
at GL-upload time only; the PNG on disk stays full color (texbake
quantizes at build, as always). A swatch strip shows the palette the
median cut settled on, and a live **budget line** (also under the texture
size in the property column) prices the texture in PS2 memory:
`128x128 4-bit = 8.0 KB + 64 B palette`.

Under the bake block sits **UV check** — *Validate UVs* inspects the
preview mesh's mapping: **overlapping islands** (painting one paints the
other — texel-exact, computed modulo the 0–1 wrap like the GS samples),
UVs **outside 0–1**, **flipped** (mirrored) and **degenerate** triangles,
and extreme **texel-density outliers** (4× above/below the mesh average).
Click a finding and the offending triangle(s) outline red in the UV panel
and on the mesh. Note the box primitive intentionally maps all six faces
onto the same square — its overlap findings are by design.

Next to it, **Unwrap UVs...** generates a mapping from scratch for the
preview model — the classic smart-project recipe: faces cluster into
**charts** by normal similarity (the *Chart angle* knob: low = many flat
islands for hard surfaces, high = fewer, more distorted charts for organic
shapes), each chart projects onto its plane and rotates to its tightest
box, and everything packs into 0..1 at **uniform texel density** with a
bleed *Margin*. A static `.obj` is rewritten in place — positions, normals,
materials and comments survive byte-for-byte, only the UVs change (unwrap
**before** texturing; an already-painted texture will no longer line up —
projects are git repositories, revert from there). An **animated model**
(`.glb`/`.fbx` — sources can't be rewritten) instead gets a
**`<model>.uvs` sidecar**: each part unwraps into its own 0..1 square
(parts carry their own textures), and the sidecar is folded in wherever
the model is parsed — editor previews, bakes, and the `.tskl` the game
ships (its LODs inherit the mapping too). Delete the sidecar to restore
the original mapping; a re-exported model whose geometry changed simply
ignores the stale entry. After the unwrap the validator re-runs
automatically and the UV panel opens on the fresh layout. Deterministic:
the same mesh and settings always produce the same mapping.

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

The **layer stack is always visible while the entry has a texture** — the
*Paint* checkbox only arms the brush. Stack edits (reorder, blend, opacity,
visibility), **smart masks** and **presets** all work with the brush
disarmed; painting happens on the stack's active layer:
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
