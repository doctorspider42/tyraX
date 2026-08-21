# Dynamic shadows

Two runtime shadows, chosen **per object** in *Properties > Dynamic shadow*:

| | Blob | Projected silhouette |
| --- | --- | --- |
| What it is | one soft dark quad that follows the ground under the object | the object rendered a second time (64×64, from the sun) and projected under itself |
| Shape | none — a smudge | the real silhouette, animation included |
| Cost | one quad | a second render per frame, **four casters at a time** (the nearest to the camera win) |
| Good for | crowds, small props, anything you want grounded | hero objects |

They are not the same thing as **"Cast shadow"** further down that panel, which
is the *baked* ambient occlusion ([ambient-occlusion.md](ambient-occlusion.md)):
that one is painted into the lighting at build time and never moves. Nor is it
the flashlight's own shadow machinery, which is a separate system with its own
page ([flashlight.md](flashlight.md), "The shadow"). If what you want is a big,
plainly visible dynamic shadow, it is the **projected silhouette** below that
gives it: a torch held at the eye hides its own shadows behind whatever casts
them, by geometry rather than by any bug (flashlight.md, "How much of a volume
shadow you will actually SEE").

## The choice, and what "Default" means

*Properties > Dynamic shadow* offers **Default / None / Blob / Projected
silhouette**, and Default is what every project did before the choice existed:

- a **blob** under the things that MOVE — the third-person avatar, animated
  models and physics bodies — while *Preferences > Rendering > Blob shadows
  under moving objects* is on;
- a **projected silhouette** where the object's *Projected shadow (live)*
  checkbox is ticked (that checkbox is still there, and still what an existing
  `.tyra` carries).

Picking anything else overrides both, in both directions:

- **Blob** works on a **static** prop too, and with the project preference
  **off** — the moving-things rule only gates the default. This is the answer
  to "I want this model grounded, but not at the price of a silhouette render".
- **None** keeps an object out of both systems even when the preference is on
  and the checkbox is ticked.
- **Projected silhouette** casts one whether or not the old checkbox is set.

Lights and markers never cast either kind, whatever the mode says.

## What it costs

The projected system claims its VRAM at boot **only if some object asks for a
silhouette** (`PROJ_SHADOWS_USED`), and the blob system loads its sprite only if
some object asks for a blob or the preference is on (`BLOB_SHADOWS_USED`) — so a
project that uses neither pays for neither. The blob's alpha mask is the flare
glow sprite, baked into `res/hud/` when either half wants it.

Four projected casters are active per frame. They are chosen by distance to the
camera, so marking everything does not draw everything — it just makes which
four you get unpredictable. Blobs have no such limit; they are a quad each.

## On disk

`SceneObject::shadowMode` — `0` follow the project, `1` none, `2` blob, `3`
projected — written into the object's JSON only when it is not 0
(format v34, [format-versioning.md](format-versioning.md)). An untouched
project therefore resaves byte for byte, and an older editor reading a newer
file falls back to the `projShadow` flag it already understands.

Two objects that differ only in what they cast are **not** interchangeable as
spawn templates: the mode is part of the live-link recipe hash
([live-link.md](live-link.md)).
