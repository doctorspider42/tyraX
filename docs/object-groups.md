# Object groups

Object groups keep scene objects together for selection, movement, rotation,
copying and deletion, without adding a runtime parent or merging their geometry.

Select objects with Ctrl-click or a selection rectangle, then choose **Edit >
Group objects** (Ctrl+G), or **Group objects** in Properties. Clicking a member in the viewport or the outliner group header
selects the whole group, including members hidden by an outliner search. The
Project panel displays a collapsible named group; search also matches its name.
Rename it in **Properties > Group name**, confirming with Enter. Names are unique
within a scene. Grouping existing groups together makes one flat group.

Use the viewport Move/Rotate gizmos as usual. Rotation changes both each member's
position around the shared centroid and its orientation. The Properties Rotation
fields use the primary member's orientation as their numerical handle and apply
the same rigid rotation about the centroid. Position edits translate every member
by the same amount. A group containing lights can rotate too.

**Ctrl+C**, then **Ctrl+V**, copies all members into a new independent group;
click to place it (or press Ctrl+V again). References between copied members,
including paired portals and flow-node object references, follow the copies;
references to objects outside the copied selection stay unchanged. Delete removes
all members. **Edit > Ungroup objects** (Ctrl+Shift+G) or the Properties button
removes membership while preserving every object's current world transform.
Expand the group in the Project panel and click a child row to inspect and edit
that object alone, without ungrouping. Its own Name, transform and type-specific
Properties appear; viewport transforms apply to that one member too. Undo/redo
preserves this individual selection. Click the group header (or an object in
the viewport) to return to editing the complete group.
All these edits participate in undo/redo and explicit Save.

Groups are scene-local and flat. Streaming layers and prefab provenance remain
separate concepts; grouping does not change either. Members on different layers
can appear under more than one layer header, but selecting any member still selects
the whole group. Avoid nonuniform group scaling of differently rotated objects:
the scene stores TRS transforms and cannot represent shear.

The optional `editorGroup` string lives in each `objects/<id>.json` (format 44 after merging main).
It is included in object equality/history and collaboration serialization, and
has no PS2 runtime cost. Old files without it open with independent objects.

In [Aster](../examples/showcase/README.md), **Portal pavilion** contains the
building mesh (walls, roof and frame) and `surface-gate`. Its pivot is at the
doorway. Move or rotate this group to reposition the entrance; the cellar stays
where it was and the linked portal uses the entrance's new transform. Re-bake GI
after changing baked scene geometry. The return view's explicit object list may
need adjusting if you move the entrance to a different part of the map.
