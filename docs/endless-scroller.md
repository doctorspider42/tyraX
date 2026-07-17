# Endless scroller

A **Scroller** is a conveyor belt for scene geometry. You author a chunk of a
level once — a tunnel ring, a strip of road, a clump of trees — tell the
scroller how long that chunk is, and the game tiles copies of it along the belt
axis **forever**, sliding them past the camera. It is the classic "level
generator in a train window": the world streams by to the horizon without you
placing a single object past the first chunk.

Because the whole belt is built from a handful of authored templates that get
cloned at build time and repositioned each frame, it costs only the geometry of
the copies that are actually on screen — no runtime spawning, no procedural
mesh generation on the PlayStation 2.

A Scroller is invisible and intangible in the game (it is just a marker that
drives its clones). Use its **Rotation** to aim the belt: the belt runs along
the scroller's local **+Z** axis.

A ready-to-run demonstration lives in
[examples/endless-scroller](../examples/endless-scroller) — a first-person
endless tunnel streaming past at 7 units/s, built from a single 3-unit chunk.

## Segments

A scroller owns an ordered list of **segments**. A segment is:

- a **name** (for your reference),
- a **length** — how much belt space the chunk occupies before the next
  segment starts (0 = auto-measured from the members' extent along the axis),
- a list of **member objects** — scene objects you authored that make up the
  chunk (referenced by name, exactly like a Mirror's reflected-object list).

Segments repeat in list order, forever: `A B C A B C A B C …`. Give a scroller a
single segment for a uniform belt (a straight tunnel), or several for variety (a
road that cycles straight / curve / junction pieces).

The member objects are **templates**. In the editor they stay visible so you can
edit them; in the game they are hidden and the scroller draws sliding clones of
them instead. A member's authored position defines its place *inside* its chunk;
its perpendicular offset from the belt axis is preserved, so you can build a
chunk with real shape (rails to the sides, a ceiling above, props scattered
across the width).

> Members should be plain scenery. Any flow graph or attached script on a member
> does **not** run (the template is deactivated in the game); the clones are
> pure visual + collision copies. Keep logic on other objects.

## Belt settings

| Setting | What it does |
|---|---|
| **Speed** | Belt speed in units/second along +Z. Negative reverses the flow. |
| **Populate ahead** | How far in front of the scroller origin to keep the belt filled. |
| **Keep behind** | How far behind the origin an instance stays before it recycles to the front. |
| **Run at start** | On = the belt moves from scene start. Off = it waits for a *Start Scroller* flow node. |
| **Max clones** | Safety cap on baked clone objects. Past it the belt recycles fewer copies and a gap may appear (the editor warns). |
| **Seam overlap** | Anti-z-fighting: every clone is stretched this much along the belt (default 0.02), so consecutive pieces interpenetrate slightly instead of butting up with exactly coplanar end faces, which flicker. 0 = exact tiling. See *Seams* below. |

The Properties panel shows the live **clone cost** ("28 clone objects, 14
copies/segment, period 4.0") so you can see how heavy a belt is before you
build, plus a warning if the clone cap is trimming the belt or a member name is
missing.

## Building a scroller

1. Author your chunk — place the objects that make up one repeating unit
   (e.g. two rails and a floor tile), sized to the length you want.
2. **Insert > World > Scroller (endless)**, then aim it with Rotation so its +Z
   points down the belt.
3. In Properties, **+ Add segment**, set its **Length** (or leave 0 to
   auto-measure), and **+ Add object** each chunk member.
4. Set **Speed** and the **ahead / behind** window, then build.

Shortcut: select the objects that make up a chunk, and in the multi-object
Properties click **"Make endless scroller from selection"** — it creates a
scroller at the group's centroid with those objects as its first segment.

## Controlling the belt from logic

Three flow nodes (the **Scroller** category) target a scroller object:

- **Start Scroller** / **Stop Scroller** — run or freeze the belt.
- **Set Scroller Speed** — change the belt speed live (e.g. accelerate the
  tunnel as the game ramps up difficulty).

## How it works

The layout math lives in one shared host module, `src/scrollsim.cpp`, used by
both the editor preview and the build:

- The belt is a 1-D axis through the scroller origin. Each segment instance sits
  at a **phase** (`baseOffset + copy × patternLength`); as the belt scrolls, an
  instance's belt coordinate `u = wrapU(phase − beltScroll)` recycles it into
  the active window `[−behind, +ahead]` once it passes the back.
- At build, `templates::sceneDataContent` bakes enough clone objects to tile the
  window (`cellsPerSegment` copies of every segment's members) and **appends**
  them to the scene's object table — authored indices never move, so flow
  graphs, mirrors, scripts and the player still resolve. It emits three side
  tables (the same pattern as `MIRRORS`): `SCROLLERS` (per-belt state),
  `SCROLLER_CLONES` (each clone → its scroller + phase + home position) and
  `SCROLLER_HIDDEN` (the authored templates to deactivate).
- At runtime, the generated `ScrollerDirector` script (`scroller.gen.cpp`)
  advances each belt by the real frame time, wraps every clone with the same
  `wrapU` formula, slides it along the axis and hides the templates. A clone's
  geometry is only re-baked when it actually moves.

The editor viewport reads the identical `scrollsim` code to draw the animated,
semi-transparent **ghost belt** — so what you preview is exactly what ships.

## Seams: making the joints invisible

Where two copies of a chunk meet, faces can fight in the z-buffer and flicker.
Two rules keep the joints clean:

- **Build continuous surfaces from Planes (or open .obj meshes), not boxes.**
  A closed box has end caps; where two boxes butt, the hidden caps share the
  seam with the visible side faces and a flickering line can appear along the
  joint no matter how precisely they align (the cap's edge lies exactly on the
  surface). A Plane has no caps — floors and walls made of planes tile with no
  seam at all. Keep boxes for props that don't touch their neighbors (arches,
  pillars, obstacles). (The Plane primitive's own underside is already offset a
  hair below its top face — nothing on the PS2 backface-culls, so exactly
  coplanar front/back faces would dither-fight across the whole surface.)
- **Don't rest members exactly on the terrain plane.** A box or plane whose
  underside sits at exactly the terrain height is coplanar with the terrain and
  shimmers. Lift the chunk a little (e.g. floor top at 0.15) so the belt's
  surface is clearly above the ground.

The **Seam overlap** setting handles the rest: each clone is stretched slightly
along the belt so neighbors interpenetrate instead of merely touching. Raise it
if you still see cracks between pieces; lower it toward 0 if a *textured*
surface shimmers in the overlapped strip (the two copies' UVs disagree there).

## Notes and limits

- A **uniform** belt (identical segments) looks visually static while scrolling,
  because every piece is interchangeable. Vary the segments, or add a landmark
  member, to see the motion.
- Keep member objects off streaming **layers** — the scroller owns their
  residency (clones are always resident; templates are deactivated).
- Live Link cannot restripe a belt, so editing a scroller (or moving any of its
  members) flips the LIVE chip to "rebuild".
- Total on-screen geometry is `clones × member triangles`; the clone repositions
  re-bake primitive meshes each frame (the same cost as a Cutscene Director
  moving objects). Keep members light and the window tight for busy belts; the
  **Max clones** cap is the backstop.
