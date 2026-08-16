# ambient-occlusion example

A village terrace on sculpted ground, built to show what
[baked ambient occlusion](../../docs/ambient-occlusion.md) does — and, just as
usefully, where it currently stops.

Open `ambient-occlusion.tyra` in TyraX and press **F5**, or build headless:
`tyrax-editor.exe --build <this folder> --run`.

## What to look at

The scene is arranged around the three things the bake can actually answer.

**The ground shadows itself.** The terrain is sculpted, which nothing else in
this repo does — a ravine cut down the west side, a long bare bank rising north,
and a shallow bowl south-east. Walk into the ravine and it darkens toward the
floor; walk up the bank and it does **not**, at any angle, because a bare slope
is not occluded by anything (see *What the terrain scan measures* in the doc).
The bowl darkens toward its centre. Those three together are the whole horizon
scan in one walk.

**Things sitting on things darken where they meet.** The terrace, the L-wall,
the pillars and the boulders are primitives, so they receive the bake **per
texel** through the scene lightmap atlas: the contact shadow where a pillar
meets the terrace is a smooth gradient, not a triangle edge.

**Everything casts.** The kit props — wagon, crates, fence, chimney, the house
walls — are imported models, and they darken the ground and the primitives
around them.

## The A/B

*Tools > Ambience Editor > Ambient occlusion* turns it off, and **AO strength**
scales it. Flip the toggle, rebuild, and compare: it is 48.8% of the frame at
this vantage. This example ships at strength **0.65**, radius **2.5**.

Raising the radius past the size of the things receiving it is the trap worth
knowing: at 4.5 the 3.2-unit wall is entirely inside its own ground-contact
band and goes uniformly dark instead of darkening at the base. Radius is the
distance the effect reaches, not its strength.

## What this example also shows: models do not receive

The props cast, but nothing darkens **on** them — that is a real limitation and
it is why the architecture here is primitives.

The reason is not the bake, it is the sampling. A model receives per **vertex**,
and a crate 1.06 units tall standing under a 2.5-unit AO radius has every one
of its corners inside the occluder's reach, with no interior vertices to carry
a gradient — so it darkens as a lump while its neighbour 20 cm away stays
bright. Measured on this scene, on the console. The fix is a per-model lightmap
unwrap (`src/uvunwrap.cpp` is most of it) or a response that falls off with the
occluder's solid angle rather than with distance alone; both are in
[docs/backlog.md](../../docs/backlog.md).

## Cost

| | |
|---|---|
| objects | 44 (43 casters) |
| terrain | 96 x 96 units, 65 x 65 grid, relief -6 .. +11 |
| host bake | ~0.5 s per build (terrain map 124 ms, atlas 356 ms) |
| VRAM | 256 KB terrain lightmap + 256 KB primitive atlas + five 128² textures |
| PS2 frame rate | **50 FPS** — the PAL cap |

## Assets

Models and textures are a slice of the **Medieval Village MegaKit** by
[Quaternius](https://quaternius.com), CC0 1.0. Textures are downsampled to
128², normal/roughness maps dropped, `map_Kd` rewritten to sibling names. See
[THIRD-PARTY-LICENSES.md](../../THIRD-PARTY-LICENSES.md).
