# global-illumination — a red wall, a green wall, and what the light does next

A first-person bay with a white floor between one red wall and one green wall,
open to a daylight sky. Everything except those two walls is white or grey, so
every tint you see anywhere else came off a wall — which is the whole point of
[baked global illumination](../../docs/global-illumination.md).

## What to look at

| Where | What it shows |
| --- | --- |
| The **back wall** | Pink on the left half, cyan-green on the right, meeting in the middle. It is painted white. |
| The **floor** | Pink along the red wall, blue-green along the green one, sky-blue in the open middle. |
| The two **pillars** | Each takes the colour of the wall it stands near — and only on the side that faces it. |
| Under the **block** and the pillars | Contact shadows, from real triangles: the cylinders cast round shadows, not boxes. |
| Inside the **roofed alcove** (behind the block, left) | Properly dark, and what light does reach the sphere in there arrives bounced off the alcove's own walls. The sphere is probe-lit, like everything that could move. |
| The **sky** | It is a light source, not a backdrop. Turn GI off and the same scene renders flat grey. |

**50 FPS** (full PAL) with GI on — the console does no extra work at all. The
lighting is a texture and a table; the ray tracing happened on your desktop.

## Turning it off to see the difference

*Tools > Bake Global Illumination* → untick **Enable baked global
illumination**, then build. Same geometry, same sun, same sky colours — flat
ambient shading, no bleed, no shadows beyond the analytic contact term. Tick
it back on and press **Bake this scene** to get the light back (about 10
seconds for this scene).

## The bake ships with the example

`.res-baked/gi/scene0.gi` is committed on purpose. The GI bake is *never* part
of a build (see the doc — a build that silently re-bakes lighting is a build
nobody runs), so without the cache in the repo a fresh clone would build this
project with GI quietly switched off. The bake signature hashes file
**content**, not timestamps, so a checkout doesn't invalidate it.

Edit anything — move a wall, repaint it, drag the sun — and the bake goes
**stale**: the Bake window says so per scene, and the build falls the scene
back to classic lighting until you re-bake. Deliberate: stale is visible,
never silently shipped.

## Controls

Standard FPP template: left stick walks, right stick looks, X jumps. Walk into
the alcove and back out to feel the probe grid work on the player's
surroundings.
