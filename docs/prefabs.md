# Prefabs

*Tools > Prefabs* — a group of scene objects, their flow graphs included, saved
once and stamped into the world as many times as you like: by hand, by a
[procedural graph](procedural-runtime.md), or by a flow node while the game is
running.

Build a hut out of primitives, select it, press **Create from selection**. From
then on it is one thing with a name.

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
