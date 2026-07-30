# mirror-room example

A working mirror on the PS2, done the era-correct way: a **Mirror object**
re-draws a hard list of scene objects a second time, reflected across the
glass plane — no render-to-texture, no stencil. The "room behind the glass"
is real geometry on the far side of the plane, revealed only through an
opening in the wall.

Open `mirror-room.tyra` in the editor and Build & Run (`F5`), or build
headless: `tyrax-editor.exe --build <this folder> --run`.

## What to do

You control a third-person wobbler standing in front of a gray wall with a
big glass rectangle in it. Walk around (left stick, right stick orbits the
camera): the crate, ball and pillar show up in the mirror, the room's own
walls reflect so the mirror world looks furnished — and **your avatar
follows you in the glass** (the *Reflect player* switch). Walk behind the
wall and you can see the trick from backstage: the "reflections" are plain
objects drawn on the other side.

## How it is wired

- **`mirror`** — a Mirror object (scale = the glass rectangle, +Z face
  toward the room). Its *Properties > Mirror* block lists
  `wall-left / wall-right / wall-top / crate / ball / pillar` as reflected
  objects, has **Reflect player** on and glass opacity 0.35 (the object
  color is the glass tint).
- **`wall-left` / `wall-right` / `wall-top`** — the wall is built in three
  pieces **around an opening**, and the mirror fills that opening. This is
  the load-bearing detail: the reflected copies are real geometry *behind*
  the plane, so a solid wall behind the glass would z-occlude all of them.
  The wall pieces are themselves on the mirror's list, which is what makes
  the mirror show a believable room instead of floating props.
- **`crate` / `ball` / `pillar`** — ordinary primitives; anything on the
  list reflects live (move or recolor one from a flow graph and the copy
  follows, since the game re-submits the object's current vertex buffers
  under a reflection matrix every frame — VU1 does all the work).
- **`player`** — a third-person Player whose avatar is the animated
  `wobbler.glb`; *Reflect player* mirrors it through the same path animated
  models use, live pose included. An FPP player has no body and casts no
  reflection (vampire rules).

The terrain needs no entry on the list: it extends behind the wall, so it
doubles as the mirror room's floor for free.
