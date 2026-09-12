# baked-shadows — a late afternoon, and ten shadows for one draw call

Every shadow in this scene is **baked into a projected decal**
([shadows.md](../../docs/shadows.md)): traced once on the desktop, packed into
one shared atlas page, and drawn by the console as ordinary static triangles.
Ten casters, **one submit**, 230 triangles, 13 KB of the executable, 256 KB of
GS VRAM. The PS2 does no shadow work at all — no silhouette render, no caster
slots, nothing per frame but one blended pass.

The sun sits at **18° of elevation**, due east of the yard. That is the whole
staging: shadow length is `height / tan(elevation)`, so an 18° sun makes a
4-unit post throw a **12-unit** shadow.

The spawn is placed to suit that, and placing it took two tries worth writing
down. It stands with the **sun behind your right shoulder**, looking across the
yard at 45°, so the shadows run away from you and to the left.

Both of the obvious alternatives are worse, for opposite reasons:

- **Facing the sun** — the first thing you would try — hides everything: a
  shadow points away from the light, so from the sunny side each one sits
  behind the post that throws it. That is a property of a low sun, not of this
  feature.
- **Facing square across the light** (down ±Z here) puts the shadows perfectly
  broadside, but this yard's casters are strung out along Z, so that is also
  the one axis that stacks them all behind the arch.

The diagonal gives up a little of the broadside and gets the whole yard back.

## What to look at

| | Where | Why it is here |
| --- | --- | --- |
| **The five posts** | down the row ahead | Different heights, one sun: five parallel shadows of five different lengths — the 4-unit post reaches the wall and climbs it, the 2-unit ones stop on the grass. All from one atlas page. This is the count the feature is about — as separate objects they would have cost ~1 ms of EE each ([prefabs.md](../../docs/prefabs.md)) and eaten half a PAL frame. |
| **The arch** | far end of the row | Its shadow has a **hole in it**, because the tile is traced rather than stamped: the light goes between the legs and under the lintel. A blob cannot do this and a silhouette render would not show you the gap this cleanly. |
| **The brick wall** | along the left | **Textured** — and that is the point. A baked lightmap receiver has to be untextured, because the lightmap needs the texture slot ([ambient-occlusion.md](../../docs/ambient-occlusion.md)), so this wall can take no static shadow by any other route. Watch the block's and the arch's shadows climb it. |
| **The paved plinth** | near end, left | A second textured surface at a different height. The block's shadow crosses grass, steps up onto the paving and keeps going — one continuous shape over three receivers. |
| **The ball** | on the plinth | A round caster, so the penumbra is easy to read: crisp where it touches the paving, softening as it leaves. That is the `Softness` setting (2.5°, a slightly hazy sun) opening up with distance, not a blur. |
| **The colour of the shade** | everywhere | Look at the grass: shaded grass is *darker green*, not grey. The tile is near black and blends as a per-pixel multiply, so a receiver keeps its own hue — which is the whole difference between shade and a wash. |
| **The grass past the wall** | behind the wall, on the left | Nothing. Deliberately: the posts' shadows reach the wall, climb it and **stop**, instead of printing on through onto ground the sun never reached ([shadows.md](../../docs/shadows.md), "Print through a wall"). Note what that leaves — that ground is *unshadowed*, not wall-shadowed, because the wall is a receiver here and not a caster. |

## Trying it

Turn on the spot with the right stick — the shadows sweep across the view and
stay exactly where they were baked, because they are geometry, not a render.
Walk behind the wall and they do not follow you; that is what "baked" means.

Re-bake from *Tools > Ambience Editor > **Baked lighting***, or headlessly:

```bash
tyrax-editor --bake-shadows examples/baked-shadows
```

It takes about a tenth of a second. The project has **Re-bake stale scenes
before every build** on, so moving anything here fixes itself on the next
build; the cache in `.res-baked/shadow/` is checked in so a fresh clone has its
shadows immediately.

## Things worth knowing before you copy this setup

- **Move the sun and every shadow is stale.** They are baked at one hour. A
  scene with a running [day/night cycle](../../docs/day-night-cycle.md) wants
  the projected silhouette instead — the editor says so in the panel.
- **Casters must stand still.** Physics bodies, carryables and animated models
  are refused by name, in *Properties* and in the bake log.
- **With global illumination on, the ground and the untextured props drop out**
  of the projection automatically — their sun shadow is already in the
  lightmap, and darkening it twice is the one thing this must not do. What
  stays is exactly the brick wall and the paving.
- The whole scene is primitives, and the two textures are generated, so there
  is nothing here to download.
