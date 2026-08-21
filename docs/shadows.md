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

The two shadows above are about an OBJECT and the sun. This one is about a
**light**: a placed point light with *Spot (cone)* on
([flashlight.md](flashlight.md), "A scene light with the same trick") carves
its own occlusion per pixel, exactly the way the player's torch does
(flashlight.md, "The shadow"). Without it a street lamp bolted to a wall lights
the wall and the alley behind it equally — the cone is a lighting term and
nothing stops it.

Switch it on project-wide in *Preferences > Rendering > **Spot light shadow
volumes***, and override it on any one light in *Properties > Point light >
**Shadow volumes*** — **Default (follow the project) / Off / On**, the same
tri-state idiom as *Dynamic shadow* above. The override only means anything
while *Spot (cone)* is on; a point light has no cone to carve.

**What gets carved is the lamp's ground pool** — the projected patch under its
cone (flashlight.md, "What the pool does"). Everything the cone reaches that is
*not* that patch still takes its light per vertex, unshadowed; the torch's
second pass on walls and props has no spot equivalent yet. So the shape to
expect is the one in the picture a lamp throws on the floor.

### Only one spot casts per frame

Volumes are counted in a dedicated GS buffer — the **count band** — and a
counting bracket is per light per frame. There is one band, so a scene holding
six shadow-casting lamps does not cost six times a single lamp: **the one
nearest the camera is the one that casts**, and the others light their cones
without occluding them.

Precisely: among the lights that are active, visible, spot, asked for volumes
and actually **lit** (a lamp a *Set Light* node switched off, or one flickering
dark, is not a candidate) and whose reach is not wholly behind the eye, the
nearest to the camera holds the slot. The hand-over uses **hysteresis** — a
challenger has to be 15 % or 1.5 units nearer, whichever it reaches first, for
ten consecutive frames — so two lamps at nearly equal distance cannot trade the
slot every frame and make the scene's shadows blink. A lamp that loses its
qualification (switched off, streamed out, walked out of view) hands over at
once, because there is nothing left to flicker against.

That is the reason the per-light override earns its place. In a room of lamps,
setting one to **On** and leaving the rest on *Default* in a project whose
switch is off is how you say *this* is the lamp whose shadow the scene is
about. Setting them all to On does not buy more shadows; it only makes which
one you get depend on where the camera happens to be.

### What it will not do

- **Only the ground pool is carved** (above), and the reason is not effort. A
  wall pass would draw the lamp's light on the wall's own triangles a second
  time, additively, while the wall is *already* taking that lamp per vertex
  through the engine's dynamic-light slot — so the wall would read twice as
  bright and the carved shadow would only darken half of it, which looks like a
  bug rather than a shadow. The torch has no such problem: it turns its own
  cone off per receiver (`spotLit = false`, flashlight.md), and there is no
  per-object way to say "not *this* scene light". `dynLightPick = false` is the
  only lever and it removes **every** dynamic light from the bag. It is closer
  than it sounds — the engine picks ONE light per bag, so a wall inside a
  lamp's cone is usually lit by that lamp and nothing else — but which light a
  bag picked is decided on the console, so switching the pick off host-side can
  silently darken a wall that was being lit by a different lamp. That is the
  problem to solve before this pass can ship.
- **A scene with no terrain has no spot pool at all** ([terrain.md](terrain.md)),
  so there is nothing for a lamp to carve. The torch's pool is projected onto
  whatever its beam lands on and is unaffected.
- **The caster whose shadow you are standing in is dropped** for that frame.
  The count is z-*pass*: it asks how many volume faces sit in front of the
  scene's depth, which is the shadow's depth only while the eye is outside
  every volume. A torch is held at the eye and can never be inside one; a lamp
  on a wall throws a shadow you can walk into. Your own shadow fading as you
  step into it is a far smaller lie than the whole mask inverting.
- **No count band, no shadow.** If the GS refuses the band's VRAM the lamp
  lights its cone plainly. (The torch has a 1-bit fallback for that case; it
  works by interleaving each receiver's light with the volumes in front of it,
  and a lamp's receiver is one patch, so there is nothing to interleave.)
- **Casters follow the same rules as the torch's**: everything solid inside the
  cone, nearest first, at most four, nothing bigger than a grouping cell, and
  nothing whose *Dynamic shadow* is **None** — that setting now keeps an object
  out of the volumes too, for both lights.

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
their own unit mesh (flashlight.md, "The shadow"). The count bracket is
scissored to the volumes' own screen rectangle, so a lamp whose shadow is a
few hundred pixels costs a few hundred pixels of fill and not a full raster.
Measured on a scratch fixture (one lamp, one box, PCSX2 software renderer):
50.0/50 with the switch on and 50.0/50 with it off.

One thing a project that has never had a flashlight gains here: the lamp's
projected pool is drawn through the same **gobo** image the torch uses, and
that image used to be baked and loaded only for projects with a torch. A
project whose only user of it is a shadow-casting spot now bakes and loads it
too — a 128×128 texture, and without it there is no pool to carve.

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
