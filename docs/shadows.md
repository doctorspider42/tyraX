# Dynamic shadows

Two runtime shadows an OBJECT can cast, chosen **per object** in *Properties >
Dynamic shadow* — plus a third thing further down this page, which is about a
LIGHT rather than an object ("Spot-light shadow volumes"):

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

## Spot-light shadow volumes

> The runtime half of this lands in **1.67.0**. The setting, the per-light
> override and the generated constants are in place; a build made with an
> editor that has them and an engine that does not simply does not cast.

The two shadows above are about an OBJECT and the sun. This one is about a
**light**: a placed point light with *Spot (cone)* on
([flashlight.md](flashlight.md), "A scene light with the same trick") can carve
its own occlusion per pixel, exactly the way the player's torch does
(flashlight.md, "The shadow"). Without it a street lamp bolted to a wall lights
the wall and the alley behind it equally — the cone is a lighting term and
nothing stops it.

Switch it on project-wide in *Preferences > Rendering > **Spot light shadow
volumes***, and override it on any one light in *Properties > Point light >
**Shadow volumes*** — **Default (follow the project) / Off / On**, the same
tri-state idiom as *Dynamic shadow* above. The override only means anything
while *Spot (cone)* is on; a point light has no cone to carve.

### Only one spot casts per frame

Volumes are counted in a dedicated GS buffer — the **count band** — and a
counting bracket is per light per frame. There is one band, so a scene holding
six shadow-casting lamps does not cost six times a single lamp: **the one
nearest the camera is the one that casts**, and the others light their cones
without occluding them.

That is the reason the per-light override earns its place. In a room of lamps,
setting one to **On** and leaving the rest on *Default* in a project whose
switch is off is how you say *this* is the lamp whose shadow the scene is
about. Setting them all to On does not buy more shadows; it only makes which
one you get depend on where the camera happens to be.

### What it costs

**No extra VRAM next to the flashlight.** The torch and the frame's active spot
count into the **same** band, so a project that already has *Flashlight shadow
volumes* on has paid for it — the boot path allocates the band if *either* asks
(`(FLASH_SHADOW_VOLUMES && FLASHLIGHT_USED) || SPOT_SHADOW_VOLUMES_USED`), and
the Preferences VRAM warning is one warning about the pair. On its own the band
is 512 KB at 32-bit colour, 256 KB at 16-bit; that page is worth reading before
switching either on ([gs-vram.md](gs-vram.md)) — the symptom of running out is
not a missing shadow but every texture in the scene re-uploading once a frame.

What is left is the per-frame volume fill for the one active light, and the
same geometry rules the torch's volumes follow: models cast from their real
triangles (a decimated **shadow proxy** past 1200 of them), primitives from
their own unit mesh (flashlight.md, "The shadow").

A project that uses none of this generates none of it. `SPOT_SHADOW_VOLUMES_USED`
is **resolved**, not read off the project switch, because the two disagree in
both directions: a light overridden to On in a project with the setting off
still needs the band, and a project with the setting on but no spot light
anywhere must not allocate one.

## On disk

`SceneObject::shadowMode` — `0` follow the project, `1` none, `2` blob, `3`
projected — written into the object's JSON only when it is not 0
(format v34, [format-versioning.md](format-versioning.md)). An untouched
project therefore resaves byte for byte, and an older editor reading a newer
file falls back to the `projShadow` flag it already understands.

`ProjectSettings::spotShadowVolumes` (`"spotShadowVolumes"` in the manifest's
settings, written only when true) and `SceneObject::lightShadowVolumes`
(`"shadowVolumes"` inside a light object's `light` block, written only when it
is not 0) are format **v36**, on the same terms: both defaults are what every
earlier file meant, so a project that touches neither resaves byte for byte and
an older editor drops two keys whose absence *is* the old behaviour.

Two objects that differ only in what they cast are **not** interchangeable as
spawn templates: `shadowMode` and `lightShadowVolumes` are both part of the
live-link recipe hash ([live-link.md](live-link.md)). For the light field that
also means an edit of it flips the LIVE chip amber and asks for a rebuild
rather than streaming — it decides what the boot path allocates and which lamp
the frame counts into the band, which is not a thing a running game can be told
mid-flight.
