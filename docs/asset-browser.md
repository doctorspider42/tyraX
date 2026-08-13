# Asset Browser

The Asset Browser is a file manager for the project's `res/` folder. Open it
with **Tools > Asset Browser** or **Project > Assets > Browse assets**.

![The Asset Browser showing the folder tree, asset filters, a rendered model thumbnail and the inspector.](img/asset-browser.png)

## Finding assets

- Pick a folder in the tree, then switch between grid and list view.
- **Sub-folders** includes the whole subtree.
- Type filters show their current counts.
- Search matches file names.
- The inspector shows size, type-specific details and every project reference.

Models and materials get rendered thumbnails; images show themselves. Large
folders fill in over a moment without blocking the editor.

## Importing

Click **Import** or drag files into `res/` in your normal file manager. There is
no separate asset database: files added outside TyraX appear automatically.

Model imports copy their textures and materials, validate the geometry and ask
for its real-world size. Large `.obj`, `.glb` and `.fbx` files import in the
background, so the editor stays usable.

## References

Select an asset to see where it is used. **Select** jumps to a referencing
object, even if it lives in another scene. An outlined tile has no project
references and is a likely cleanup candidate.

The scan covers objects, materials, terrain layers, HUD, menus, fonts, loading
screens, audio nodes and custom LODs. Asset settings such as texture quality or
animation clip edits are metadata, not usage.

## Move and rename

Drag files or folders in the browser, or use the context menu. TyraX updates
project references in the same edit.

Wavefront assets need one extra rule: `.obj`, `.mtl` and their textures use
sibling file names. Moving a model therefore moves its dependency group too.
If another file still needs a dependency, TyraX copies it instead. A move that
would leave a broken reference is refused.

Renaming updates `mtllib` and `map_Kd` lines. Paint layers (`.layers/`), custom
UVs (`.uvs`) and Drone Generator patches (`.drone`) follow their asset; baked
`.tmdl` files are regenerated on the next build.

You can also drag a model from the browser straight into the viewport.

## Delete

**Delete** lists remaining references before removing anything. Deletion is not
undoable. Objects keep missing model paths so you can repair them; materials
fall back to colour, and missing fonts use the built-in fallback.

## Generated files

Enable **Generated** to see baked menu sprites, text, font atlases and `.tmdl`
models. They are dimmed and read-only: edit their source instead.

Asset names are sanitized to `[A-Za-z0-9._-]`. Subfolders are supported
throughout `res/`; only generated font atlases stay at `res/fonts/`.
