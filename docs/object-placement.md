# Placing objects: surface snapping and deferred paste

Two conveniences for building a scene by hand: new objects **rest on what is
under them** instead of sinking into it, and a paste **follows the cursor**
until you put it down.

## Surface snapping

With **Surface snap** on (the button in the viewport's tool row, or
*View > Placement > Snap to surface*), an object entering the scene is placed
so its underside touches the highest surface under its footprint: the
**terrain**, sampled with the same bilinear filter the game walks on, or the
**top of another object** whose ground footprint it overlaps. (A scene whose
terrain was removed — [docs/terrain.md](terrain.md) — has no ground, so only
the objects below count and an object over nothing keeps its height.)

So a box inserted over a table lands *on* the table; three boxes inserted in
the same spot stack. What counts as a surface is deliberately narrow: the
geometry primitives (box, sphere, cylinder, cone, plane), save points and
models — with **Collision** left on. Markers, lights, particle and sound
emitters, decals, mirrors and portals are not things you stand a crate on, so
they never lift anything.

It applies to `+ Add object` / the *Scene > Add* menu (every primitive and
model), imported models and generated trees, and the deferred paste below.

The setting is a machine preference (`editor.ini`, like the navigation
scheme), not project data — it changes how *you* author, not what the project
contains. Off, objects appear at their literal spawn position, as before.

### Drop to floor (`End`)

Independent of the preference: select any number of objects and press **End**
(or *Scene > Drop to floor*) to rest each one on the first surface **below**
it. Unlike the insert snap, nothing can lift an object — a prop hovering over
a shelf lands on the shelf, a prop already on the ground stays put. One undo
step for the whole selection.

### How the geometry is measured

Each object is reduced to its **world-space axis-aligned box**: the unit
primitive scaled and rotated (so a box rolled 45° correctly rests on its
corner, half a diagonal up), or a model's own mesh bounds — which is why a
character authored with its feet at the origin lands feet on the ground, not
half-buried. Terrain is sampled at the footprint's corners and center (inset
slightly, so a prop overhanging a cliff edge isn't lifted by the cliff).
Footprints that merely touch edge to edge do not stack.

Finding *which* surface the cursor is over (a dragged model, a staged paste)
uses the same bounds a click does — the object's drawn shape, not a unit cube
— so what you aim at is what you land on. Areas and procedural volumes are
skipped there: they're wireframe authoring regions with nothing to rest on,
and a map-sized box would otherwise catch the drop in mid-air.

There is no swept collision and no penetration solver — this is "drop it on
the floor", extended to props resting on other props. Moving an object with
the gizmo is still completely free; snapping happens when an object is
*placed*.

## Deferred paste

`Ctrl+V` no longer drops the copies into the scene straight away. The copies
are **staged**: they follow the mouse across the viewport, outlined like a
selection, snapped onto whatever is under the cursor, until you commit them
with a **left click** in the viewport or **`Ctrl+V` again** (the keyboard
twin — the same command that started the paste settles it). **`Esc`** throws
them away; nothing is added to the scene and no undo step is created until
they land, so a cancelled paste leaves no trace.

While a paste is in flight the gizmo and object picking are out of the way
(nothing is selected yet), but the camera still works: orbit, pan and zoom to
find the right spot. A multi-object paste moves as one rigid arrangement —
the first copy sits under the cursor and the group is lifted by the largest
offset any member needs, so a stack stays a stack.

Pasting with the cursor outside the viewport (Ctrl+V twice in a row without
ever passing over it) falls back to the old behavior: the copies land one
unit diagonally from the originals.
