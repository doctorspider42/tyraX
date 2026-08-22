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

Four projected casters are active per frame, chosen by distance to the camera,
so marking everything does not draw everything. Blobs have no such limit; they
are a quad each.

A silhouette also fades out with distance on its own: it is dropped past **50
units** from the camera and dissolves over the last 15 of them, so backing away
from a caster loses its shadow smoothly rather than switching it off. Nothing
scales that with the caster's size — a building's shadow goes at the same range
a crate's does.

### The four slots change hands slowly

Marking twenty casters is not an error, but which four you get is then decided
by where you stand, and the answer must not change on a footstep. It is held,
on the same terms as the count band below:

- a caster that **stops qualifying** — hidden, streamed out, past the far cull,
  or unable to draw a shadow at all for half a second — releases its slot at
  once, because there is nothing left to flicker against;
- a **challenger** must be 15 % or 1.5 units nearer, whichever it reaches
  first, for ten consecutive frames before it may take a slot over;
- and the hand-over is a **cross-dissolve in time**: the outgoing shadow keeps
  its slot while it fades away, and only then does the challenger move in and
  fade up. About a third of a second each way, so an exchange reads as one
  shadow softening while another firms up rather than as two pops.

The **light** a silhouette is thrown from is held the same way. Each caster
picks its own — the scene sun or moon, any placed light in reach, and (with
*Flashlight shadow volumes* off) the player's torch — scored by how much that
light actually lands on it, so a lamp you are standing under really does throw
the shadow rather than the sky. A torch walking past a lamp crosses that line
twice in a couple of steps, which would swing the shadow round to the other
side of the prop and back. A caster keeps its light unless another is a fifth
brighter on it for ten frames; a light that drops out entirely (switched off,
streamed out, the caster leaves its cone) hands over immediately. Note this one
is only patient, not gradual — a shadow that changes light MOVES, and a
direction cannot be crossfaded ([day-night-cycle.md](day-night-cycle.md) makes
the same point about the sun/moon handover).

None of this is a reason to mark everything. Four is still four, and the
casters you did not want are still the ones the camera happens to be near — it
just no longer blinks while it decides.

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

**What gets carved is the lamp's ground pool and its light on the solids in
its cone** (1.70.0). The pool is the projected patch under the cone
(flashlight.md, "What the pool does"). The solids are the torch's **receiver
pass** on a scene lamp: the nearest three solids the cone touches - the
torch's rules, so thin things and grouping-cell sized things are skipped - are
rendered a second time, additively, with the lamp's projective STQ per vertex,
through the same mask the pool drew through, from a shared 3997-vertex budget
split fairly between them. What made that honest is a new engine lever:
`PipelineInfoBag::dynLightSkipSlot` names ONE scene light a bag's per-vertex
slot must ignore, and every wall-sized receiver (the torch's 1.4 u rule - a
crate lit all over reads better than one bright face and three black ones)
skips the carving lamp for as long as it is a receiver, so the wall takes that
lamp's light once, projected, with the shadow carved out of all of it. The
wall's falloff is half the torch's slope, because the lamp's pool beside it has
none - a wall that faded faster than the floor at its foot read as a seam.
Smaller solids keep their per-vertex light from the lamp AND get the projected
pass on top (the torch does the same). A statically batched receiver takes the
skip only when its batch holds nothing else, exactly as `setFlashSpotOff`.

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

- **Animated models and physics bodies are not receivers** (skinned buffers
  and local-space vertices - the torch's rule), and a receiver whose batch is
  shared keeps the lamp per vertex as well as the projected pass, so it can
  read brighter than its lone neighbour. One lamp must not unlight everything
  batched beside its wall.
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

### In the editor

The viewport previews the carve in **Solid** shading (the default): the spot
that would hold the slot in the game — the nearest to the editor camera among
the dynamic spots that resolve to "on" — shadows through the same casters the
game's `pickVolCasters` would pick (everything solid in its cone, nearest four
to the light, nothing grouping-cell sized, nothing whose *Dynamic shadow* is
None), while every other dynamic light keeps the older preview rule (its
nearest four *Cast shadow (projected)* objects). Two honest differences: the
preview's shadow is the analytic box or sphere the AO occluders use, where the
console extrudes a model's real triangles, so a tree throws a box; and there
is no hysteresis — an editor camera does not drift between two lamps frame to
frame. The same change uploads the occluders whenever a dynamic light is in
the scene, not only with ambient occlusion on (the lamp-shadow preview was
silently off without AO), and lets the ground take point and spot light with
GI *off* — the terrain's "never probe-lit" flag used to read as "lit by GI"
and skipped every lamp. **PS2 shading** is unchanged: the terrain is shaded
flat per cell there, so a per-pixel cone comes out as cells, and a spot's
gobo pool is not drawn in that mode ([ps2-viewport.md](ps2-viewport.md)).

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
