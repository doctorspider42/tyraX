# Prefabs

*Tools > Prefabs* — a group of scene objects, their flow graphs included, saved
once and stamped into the world as many times as you like: by hand, by a
[procedural graph](procedural-runtime.md), or by a flow node while the game is
running.

Build a hut out of primitives, select it, press **Create from selection**. From
then on it is one thing with a name — and a **Notes** field next to it, which is
a wrapped paragraph you click to edit rather than a one-line box, because what a
prefab is for and how it wants to be placed does not fit in fifty characters.
The list shows the note as a tooltip, so a pool of a dozen prefabs is browsable
without selecting each one.

**A prefab of one object is a normal prefab**, and that is the answer to
"how do I scatter a primitive": a *Pick Prefab* row's picker can capture any
scene object under *Capture from the scene* without a trip to this window. The
captured box, sphere or placed model then merges, costs and spawns exactly like
a captured room does.

Working demo: [examples/cube](../examples/cube) — four room prefabs stamped
onto a 3×3×3 lattice on the console at boot.

---

## What a prefab is (and what it deliberately is not)

A prefab member is an ordinary **`SceneObject`**, not a lighter "prefab part".
That is the whole design: a prefab is a piece of SCENE, so everything the
editor, the build and the runtime already know how to do with an object — draw
it, collide it, attach a graph, stream it — keeps working after it comes out of
one. The only difference is the frame: member transforms are **local to the
prefab origin**, so instantiating is a yaw plus a translation.

The origin is the selection's horizontal centre at its **lowest** point, which
is what makes "click the ground to place it" land the way a person expects.

**Instances are not linked back.** *Insert into scene* stamps copies with fresh
identities and walks away; editing the prefab afterwards does not disturb what
you already placed (use *Replace from selection* to update the prefab itself).
This is a deliberate simplification: a live link would have to decide what
happens to an instance somebody has since moved, recoloured or scripted, and
every answer to that is worse than "the copy is yours now".

**Instances stay recognisable, though.** Every stamped object records the
prefab's name (`SceneObject::prefabSource`, editor bookkeeping — nothing
downstream reads it), and the Project panel folds them into one collapsible node
per prefab, the same shape the streaming layers use above them. Click the group
label to select the whole instance, the arrow to list its members. The
Properties panel says *From prefab: &lt;name&gt;* with an *Open in Prefabs*
shortcut and a *Forget* button for a member you have reworked into something
else. The mark travels through a copy/paste (a copy of a room is still a room)
and is dropped by *Create from selection*, which would otherwise file every
instance of the NEW prefab under the old one's name.

Flow graphs come along untouched, and **object-name references inside them are
not rewritten**. A graph built around *self* (the default for every
object-referencing node) is a working component in every instance; a graph that
names another object by name keeps talking to that object in the world. Both are
useful; neither is guessed at.

---

## What an instance costs

This is the part that decides whether prefabs are usable on this machine.

A PS2 static submit costs roughly **0.7–1.5 ms of fixed EE time whatever it
contains**. Twenty-four wall slabs drawn as twenty-four objects is 24 ms — a
frame and a half, for one room. So when the game spawns a prefab it **merges**:

| Member | What happens | Cost |
|---|---|---|
| plain static geometry — a primitive or a static `.obj` with no graph, no scripts, no physics, no layer, not usable/pickable/save-state | folded into a shared geometry bag with every other member of the same material | one submit for the whole group |
| anything with an identity something can address at runtime | spawned as a real object through the ordinary 32-slot clone pool | a slot and a submit each |

The Prefabs window states both numbers per prefab (*"20 merged into 1 draw
call, 1 spawned object"*) and lists which member is which, because the second
column is the one that runs out. Eight spawned members per instance is the hard
cap; the clone pool holds 32 in total across the whole scene.

**Merged geometry still collides.** A merged member whose collision is not
*None* contributes one conservative world AABB to a static collider list the
walker tests alongside objects — so a prefab room has walls you cannot walk
through even though it is one draw call. The boxes are axis-aligned on purpose:
rooms and blocks are, and an oriented test over hundreds of boxes is not
something the EE should do every frame. What merged geometry does *not* have is
per-instance identity: no runtime moving, hiding, recolouring or scripting.

---

## Bake to model

*Tools > Prefabs > **Bake to model*** flattens the mergeable members into one
`res/models/<name>.obj` plus a generated `.mtl` carrying their colours, and the
prefab stays put as the source.

Why you would, and it is the whole reason this exists: **a prefab instance costs
a record from the runtime pool** (`prefab::kMaxRuntimeInstances`, 48), so a few
dozen is the ceiling however cheap each one is. A **model costs none** - *Pick
Asset* merges it straight into the chunk bags. Assembling a shape out of
primitives and then scattering it by the hundred goes through here.

Measured on the console: the same 9x9x8 grid that built **48 of 648** through
*Pick Prefab* builds **648 of 648** as a baked model, at the 50 FPS PAL cap
(EE 39 %, VU 5 %, GS 8 %).

It is a **one-way** bake and the result is dumb geometry - no scripts, lights,
physics or per-member identity. Members that cannot merge are listed in the
window rather than silently dropped: a light or a scripted door has no
representation in a `.obj`. Colours become `newmtl` entries (one per distinct
colour x material, so the file carries one `usemtl` run per look, not one per
member), and a member material's texture is copied next to the output, because
the PS2 cannot walk `..`.

There is no Ctrl+Z for a bake - it writes files, not scene edits - but it is
repeatable (the prefab stays as the source; re-bake to update), guarded, and
takes itself back: re-baking only ever overwrites a previous bake's output,
and if `res/models/<name>.obj` already exists and was **not** written by a
bake, the bake refuses rather than clobber a hand-made model that happens to
share the name. **Delete bake...** next to the *Baked:* line deletes the
generated `.obj`/`.mtl` (and the derived `.tmdl`) after a confirm that counts
who still draws from the file - objects and *Pick Asset* rows keep the path
and would show as missing. Textures the bake copied in stay (they are copies
of sources that still exist). Renaming a prefab points the next bake at a NEW
file; the old one stays until you delete it.

The window always says whether the selected prefab **is baked**: the green
*Baked: res/models/<name>.obj* line is read from disk, so it survives an
editor restart - only the triangle/material numbers and the skipped-member
list belong to the session's fresh bake. No bake on disk reads *Not baked to
a model*. The line is per prefab; switching in the list does not carry it
along, and a file that merely shares the name (no bake marker in its first
line) does not count as baked.

## Asking the AI Assistant for one

The [AI Assistant](ai-chat.md) can do all three of the verbs on this page —
`create_prefab` (capture named objects, or whatever you have selected),
`insert_prefab` (stamp a copy) and `bake_prefab_model` — which matters most in
the middle of a [procedural](procedural-generation.md) job: a *Pick Prefab* row
can only name a prefab that already exists, so without those tools the assistant
had to build a graph, stop, and ask you to press *Create from selection* yourself.

It is told the 48-record cap and the escape hatch above, and told to choose
between them by the count you asked for: dozens of anything scripted stays a
prefab, hundreds of scenery gets baked to a model and scattered with *Pick
Asset*. The bake's skipped-member list comes back with the reply rather than
being swallowed, because a light or a scripted door silently missing from the
scatter is exactly the surprise this page exists to prevent.

## Spawning at runtime

**Spawn Prefab** (flow node) builds one instance at a linked position with a yaw
and a scale. **Despawn Prefab** clears every live instance of a prefab (an empty
name clears them all).

**Pick Prefab** (procedural node) assigns each point in a cloud a prefab from a
weighted pool — so "a shack every so often along this road" or "one of four
rooms in every cell of a 3D lattice" is a graph, not a script.

There is one important difference between the two. A prefab spawned **by a
procedural volume** merges its static members into *that volume's* chunk grid,
so 27 rooms cost the four draw calls the grid has rather than 27. A prefab
spawned **by a flow node** gets its own bags, because *Despawn Prefab* has to be
able to take that one instance's geometry away again.

Live instances are capped at `MAX_PREFAB_INSTANCES` (48).

---

## What ships

Prefab members' models, materials and sounds are collected into the project's
asset tables exactly like a placed object's, so a prefab's assets are baked and
shipped even when no scene has an instance placed. They are only kept **resident
in RAM** for scenes that can actually spawn that prefab (a *Spawn Prefab* node
or a *Pick Prefab* row somewhere in the scene) — the Prefabs window shows that
list under *Spawned at runtime in*.

Generated files: `inc/prefab_data.gen.hpp` (member table, the merge flags, the
per-scene usage lists). Members are written by the same emitter scene objects
use — a prefab is a piece of scene, and two writers for one struct drift.

---

## Limits (deliberate, for now)

- **No nesting.** A prefab cannot contain another prefab; capture the expanded
  result instead.
- **No instance link.** See above.
- **Merged members do not move.** If a member has to move at runtime, give it a
  graph (or physics, or a script) and it stops being merged — that is the switch,
  and it is the same one the static batcher uses.
- **Eight identity-carrying members per instance.**
- **Prefab members ignore streaming layers.** A prefab belongs to no scene, so
  it can be in no scene's layer; the field is kept but reads as "no layer".
- **Ctrl+Z will not bring a deleted prefab back.** Prefabs live outside the undo
  history (the snapshot holds scenes, like sequences and menus do), so *Delete*
  asks for confirmation first and warns when graphs still spawn the prefab by
  name. Placed copies are ordinary scene objects and stay. Editing prefabs is an
  ordinary unsaved edit like everything else — the toolbar save icon lights and
  the change reaches disk on Ctrl+S — so until you save, closing the project
  without saving is the one way back.
