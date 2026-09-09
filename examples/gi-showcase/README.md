# gi-showcase — the guided walk through baked global illumination

Five stations along one straight walk, one per thing
[baked GI](../../docs/global-illumination.md) actually changes. Third person on
purpose: the probe grid lights your avatar too, so the cat you are looking at
darkens when you step indoors.

The whole thing holds **50 FPS** (full PAL). The console does no extra work at
all — the lighting is a texture and a table, and the ray tracing happened on
your desktop.

## The tour (spawn faces down the walk)

| Station | Where | What it shows |
| --- | --- | --- |
| **1 — Colour bleeding** | straight ahead | A red wall and a green wall facing each other across a white floor. The back wall between them is *painted white* and comes out pink on one half, cyan-green on the other; each pillar takes the colour of the wall it stands near, on the side that faces it. This is the milestone the whole feature is for. A low platform on the left is there for the *un*baked half — see below. |
| **2 — The sky is a light** | left and right of the path | Two slots in the same white material: one shallow and open, one deep and narrow. Same sun, same paint — the deep one darkens toward its floor because less *sky* reaches it. A post stands in each so you can compare the same object twice. |
| **3 — An interior, and its doorway** | left, roofed | Walk in. The room falls off to near-black at the back, and the four identical white cubes marching away from the door fade with it. The first four metres of floor are orange — the only part the low sun still reaches — and everything warm deeper in is light that hit *that* and bounced. |
| **4 — An emissive material is a real area light** | ahead, roofed | A glowing plate along the left wall. The whole alcove goes warm, the ceiling most of all; the post in the middle throws a **soft-edged** shadow across the room, because the source has real area and the bake traces its actual geometry. |
| **5 — Bounce is the only light left** | at the end, roofed | A corridor open only at the near end. The far end is lit by nothing but light that bounced off the walls on the way in, and the blue back wall tints what little arrives. The wobbling mesh standing there is **animated**, so it reads that light from the probe grid once per frame, wherever it is — its twin outside in the sun is the same object with the same material. |

## The one thing here that is *not* baked

The cat casts a **live projected shadow** (*Properties > Projected shadow
(live)*): her silhouette is rendered from the sun into a 64×64 VRAM slot every
frame and projected onto the surface under her. Nothing about it is in the bake
— which is the point. The baked half cannot follow anything that moves; this is
what keeps her feet on the floor while she walks.

Step onto the low platform at station 1 and the shadow steps up with her. The
receiver patch is built on **whatever solid surface is under the caster**, not
on the terrain: in a level like this the terrain runs 5 cm under the floor slab
and is not the floor at all — and in an indoor game (a corridor, a hospital) it would
be metres below and never seen. The extents are the same box the walker stands
on, so the shadow lands exactly where the feet do.

## Turning it off

*Tools > Bake Global Illumination* → untick **Enable baked global illumination**,
then build. Same geometry, same sun, same sky: flat ambient shading, no bleed,
no sky occlusion, no soft shadow, and station 3's room as bright at the back as
at the door. Tick it back on and press **Bake this scene** (about 20 seconds for
this level).

## The bake ships with the example

`.res-baked/gi/scene0.gi` is committed on purpose. A build **never** bakes GI —
it only reads the cache — so without it a fresh clone would build this level
with the lighting silently switched off. The signature hashes file *content*,
not timestamps, so a checkout does not invalidate it.

Move anything and the bake goes **stale**: the Bake window says so per scene and
the build falls that scene back to classic lighting until you re-bake. Stale is
visible, never silently shipped.

## Two things worth knowing while you walk

- **The corridor in station 5 is genuinely dark**, and so is the mesh standing in
  it. That is the answer, not a missing light: nothing in there has a line of
  sight to the sun or the sky.
- **The avatar is probe-lit, the walls are lightmapped.** Colour and direction
  agree, but the walls carry per-texel contact shadows the 2-unit probe grid
  cannot resolve. That split is the whole design — see the routing table in
  [docs/global-illumination.md](../../docs/global-illumination.md).

## Controls

Standard template: left stick walks, right stick orbits the camera, X jumps.
