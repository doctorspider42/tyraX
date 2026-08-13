# Asset Browser

*Tools > Asset Browser* (also *Project > Assets > Browse assets...*) is the
file manager for everything a project ships: models, materials, textures,
audio, fonts. Folders, thumbnails, type filters, search, and the file
operations you would otherwise do in the system file manager - except here
they carry the project's references with them, so moving or renaming an
asset never leaves a scene pointing at a file that moved.

Before this window the Project panel listed `res/models` and `res/materials`
as one flat bullet list with the per-asset controls crammed onto each row.
That list is gone; the panel keeps a summary and the import buttons.

## What you see

```
┌ Import... ┬ New folder ┬ Search ─┬ All(30) Models(3) Materials(6) Textures(15) ... ┐
│ res / models / props                       Sub-folders  Generated  Sort  Grid  ── │
├───────────┬───────────────────────────────────────────────────────────────────────┤
│ res       │  ┌──────┐ ┌──────┐ ┌──────┐                                           │
│  audio    │  │ 3D   │ │ 3D   │ │ png  │   thumbnails, rendered once and cached    │
│  hud      │  └──────┘ └──────┘ └──────┘                                           │
│  materials│  altar.obj wobbler  ground.png                                        │
│  models   │                                                                       │
│   props   ├───────────────────────────────────────────────────────────────────────┤
│  sfx      │ altar.obj (static model, 2.2 KB)          Referenced 2 time(s):       │
│  textures │ 412 triangles, 2 material(s)              - main / altar (model)      │
│           │ [Add to scene] [4-bit] [LOD...]           - altar.mtl names it        │
└───────────┴───────────────────────────────────────────────────────────────────────┘
```

- **Folder tree** - every folder under `res/`, with the number of files
  inside. Click to list it, right-click for *New folder / Rename / Delete /
  Reveal in file manager*, drag a folder onto another to move the whole
  subtree.
- **Grid or list** - the grid shows a thumbnail per asset (see below), the
  list shows name / type / size / reference count / folder. The slider
  changes the tile size.
- **Type chips** count what is in the current scope, so `Textures (0)` tells
  you the folder has none without switching filters. *Sub-folders* widens
  the scope to the whole subtree - that plus the search box is how you find
  one file in a project with a deep `res/`.
- **Inspector** (bottom strip) - what the selected asset is, what is wrong
  with it if anything, its per-type actions, and **who references it**.

Assets are what the pickers elsewhere in the editor already offer: `res/` is
the database, there is no import registry. A file dropped into the folder by
hand appears here within a second or two, and anything the browser does is a
plain file operation someone else could have done in the file manager.

## Importing large models

Model import runs on a worker thread after the native file picker closes.
The modal stays responsive, with a spinner and a staged progress bar, while
the editor copies the model and its dependencies, parses and validates the
geometry or animation, and estimates animated-runtime memory. File copying
has byte-level progress; the parser and animation-bake stages hold at their
real stage boundary because those libraries expose no finer progress
callbacks, while the spinner shows work continues. When the worker finishes,
the imported dependency group is moved into the Asset Browser's current
model subfolder, caches are invalidated, and the normal real-world-size
dialog opens - reusing the bounds the importer already calculated instead of
parsing the same large model a third time. Opening **Size...** later is just
as cheap: the inspector already parsed the model for its triangle/clip
readout, so the dialog reuses those cached bounds instead of synchronously
parsing or baking the asset again and freezing the editor.

## Thumbnails

Static `.obj`, animated `.glb`/`.fbx` and `.mtl` libraries are **rendered**
into a 128x128 preview (a material rides a sphere), once per asset, and kept
as a small texture - a grid of hundreds costs nothing to draw afterwards.
Images are their own thumbnail; everything else (audio, fonts) gets a
colored plate with its extension. A few new thumbnails bake per frame, so a
folder full of models fills in over a moment instead of stalling.

## References: who uses this file

One pass over the project records everything that *uses* an asset: object
model / material / sound paths, terrain materials and painted layers, HUD
images, menu images, boot splashes, loading screens, fonts, custom LOD tiers
and the audio flow nodes. The inspector lists them (with a **Select** button
that jumps to the object, switching scenes if needed), and a tile whose file
nothing references gets a small ring in its corner - the honest answer to
"can I delete this?".

Materials referencing textures count too: those references live inside the
`.mtl` file, not in the project.

Per-asset **settings** deliberately do not count as a use: texture quality,
the recorded [real-world size](world-scale.md), music build options,
animation clip edits, and membership of the music/sound lists (which are
scanned off the disk, not chosen). They are metadata attached to the file -
counting them would mean no imported asset ever reads as unused, which is
the one question this is here to answer. They still follow the file when it
moves, and are cleaned up when it is deleted.

## Moving and renaming

**Drag files onto a folder** in the tree (or into the empty area of another
folder's listing) to move them; drag a **model onto the viewport** to place
it in the scene where the cursor points. Rename from the context menu.

Both operations update every project reference in the same edit, so nothing
breaks. Two rules shape what they do:

1. **A Wavefront reference is a bare sibling name.** `mtllib altar.mtl`,
   `map_Kd altar.png` - the PS2 resolves those next to the file that named
   them and cannot walk `..`. So a move takes the whole dependency group
   along: move `altar.obj` and its `.mtl` and that material's textures come
   too. A dependency that files left behind still need is **copied** rather
   than moved, so both folders keep working (the status line says how many).
   And a move that would still break a reference - dragging a texture out
   from under the material that names it - is **refused with the reason**
   instead of being half-applied.
2. **A rename inside one folder is safe**, so there the references are
   rewritten instead: siblings that name the file get their `mtllib` /
   `map_Kd` line updated. Renaming a model also renames the `.mtl` it
   exclusively owns (`tree.obj` + `tree.mtl` -> `oak.obj` + `oak.mtl`, how
   every import writes them); a shared library keeps its name and the model
   gets an explicit `mtllib` line so it still finds it.

A **`.drone` audio project** (see [drone-generator.md](drone-generator.md))
is its own asset kind: it counts under the *Audio* filter, its inspector
reads the patch and describes the piece, and double-clicking it - or the
track it rendered - opens it in the Drone Generator.

Editor-side sidecars travel with their asset: the Material Editor's paint
layers (`<texture>.png.layers/`), replacement UVs (`<model>.uvs`) and the
Drone Generator patch that produced a track (`<track>.drone`). The baked
`.tmdl` is deleted instead - the next build re-bakes it in the new place.

A WAV moved between `res/audio` and `res/sfx` changes role (streamed music
vs ADPCM one-shot) and moves between the Music and Sounds lists accordingly.

## Deleting

*Delete...* asks once for the whole selection (or a folder), listing which
files are still referenced and by how many things. Deleting a referenced
asset does what the older per-asset delete buttons did: objects keep their
path and show as missing, materials revert to plain color / the model's own
library, audio references are cleared, a font falls back to the built-in
chain. The file is removed from `res/` and that is not undoable.

## Generated files

The build writes some files into `res/`: baked menu panels (`res/menus/`),
text sprites, glyph atlases, the `.tmdl` binary models. They are hidden
until you tick **Generated**, shown dimmed, and cannot be moved, renamed or
deleted from here - edit the source they are baked from instead. The
built-in HUD sprites (save menu, USE prompt, loading, debug font) are
ordinary files you may replace; they count as referenced because the
generated game loads them by name, and the build writes them back if they go
missing.

## Notes

- The window remembers nothing per project; it is an editor view, not
  project data. Which windows a layout opens is part of the layout
  (`assets`).
- Asset names are sanitized the way imports sanitize them
  (`[A-Za-z0-9._-]`, anything else becomes `_`): these names ride shell
  command lines, Makefiles and ISO9660 paths.
- Subfolders are fine anywhere under `res/`: the texture bake, the audio
  scan, the model/material listings and the ISO writer all walk the tree
  recursively, and a font is referenced by its full project-relative path.
  Only the generated glyph atlases are pinned to the top of `res/fonts`
  (that is where the build writes and prunes them).
