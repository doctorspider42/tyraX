# Importing animation from another file

A character is rigged once, but its animation usually arrives separately — one
`.fbx` per move from Mixamo, or a second export from an animator. *Tools >
Animation Editor > **Imported clips*** borrows those clips onto a model that
already lives in your project, so you do not have to merge the files in Blender
and re-export the character every time a move is added.

Nothing is rewritten. Neither the character nor the source file is modified; the
project records *"take these clips out of that file"* and the merge happens when
the model is parsed — for the editor's preview and again on the way into the
`.tskl`. Exactly like the trims and renames beside it, the record stays legible
and reversible, re-exporting either asset picks the change up, and the console
pays nothing at runtime.

## Using it

1. **Get the clip file into the project.** *Import file…* copies a `.glb`/`.fbx`
   into `res/models/` like any other asset, or pick one already there. A
   clip-only download has no useful mesh, but it still has to be a project asset
   — the build opens it.
2. **Read the skeleton match.** The percentage is how many of the source's
   animated bones have a bone of the same name on your model. Green (>85%) is
   fine. A low number means the two rigs do not share bone names, and the clip
   will play with the unmatched bones simply not moving.
3. **Add all clips.** They appear in the clip list immediately and animate in the
   preview.

An imported clip is an **ordinary clip of that model** from then on. It can be
renamed, trimmed, retimed and made in-place in the same panel, picked in any
clip combo, mapped to Player locomotion, and fired by a *Play Animation* node —
none of those know it came from somewhere else.

> The `.tskl` clip-name field is 32 bytes, and exporter names are long
> (`Armature|Armature|ArmatureAction`). If a prefix pushes a name past that,
> give the clip a short **Name in game** in the panel below — the rename is
> applied after the import, so it works on imported clips too.

## How the retarget works

Two paths, picked automatically per import (the panel says which):

- **copy** — the mapped bones' bind orientations agree (< ~3°): channels are
  rebound verbatim, bit-exact. Two exports of one rig, a renamed rig.
- **full retarget** — anything else. Clips are resampled: each mapped bone's
  world-space delta from *its own* bind lands on the target's reference pose,
  and the reference is the target's bind rotated into the donor's bind pose.
  That one construction is what makes an A-pose clip *look* like the A-pose on
  a T-pose character (instead of floating 45° high), cancels per-bone axis
  conventions (Blender's Y-along-bone vs Max's X), survives different chain
  counts, and converts units by construction (world space is model space).

Extras that ride the full path:

- **Facing** — the source rig's world yaw. *Auto* reads both rigs' facing from
  their own feet (ankles → toes); the combo states it outright when a rig has
  no readable feet.
- **Mirror** — import the clips flipped left↔right (world deltas reflect, every
  side bone drives its counterpart): `walk_right` out of `walk_left` for free.
- **Twist bones** — an unmapped bone between two mapped ones takes half its
  child's twist, exactly (the composed orientation is unchanged), so forearm
  skin rolls gradually instead of snapping.
- **Lean** — posture fine-tune in degrees: `+` pitches the torso forward, `-`
  back, about the character's lateral axis. The spine chain curves at the hips
  and the legs stay put — the knob for a retarget that walks well but stands a
  few degrees off.

What still is not here: **contact IK**. Different proportions can slide feet —
planting is planned as its own feature. Until then the *Test pose* shows
honestly what the bake will produce.

The translation policy below applies to the copy path (the full path always
keeps the target's bone translations and retargets the root alone):

| Translation | What keeps the source's positions |
| --- | --- |
| **Root bones only** *(default)* | Only the hips. Every other bone keeps **your model's** rest position — which is what its bone lengths *are*. |
| **Only where animated** | Bones whose position actually moves. For stretchy or IK-baked rigs. |
| **Copy everything** | All of it, proportions included. For two exports of one rig. |

Bone lengths live in bone positions, so copying them all rebuilds the *source's*
proportions on your character — a tall rig's clip visibly stretches a short one.
Dropping a translation channel is not a loss: the pose falls back to the
target's own rest transform, which is precisely "keep your own proportions".

Two more switches, both on by default:

- **Retarget root motion** scales the hip travel by the two rigs' hip heights and
  re-anchors it on your model's rest hip, so a clip authored on a taller
  character does not make yours skate. Units convert through model space, so a
  rig that keeps its bones in centimeters under a scaled armature node (Mixamo)
  retargets onto a meters-authored one without the hips flying off.
- **Ignore scale** drops the source's scale tracks — export noise in most rigs
  and a rig-breaker in a few.

Namespace prefixes (`mixamorig:`, `Armature|`) and letter case are ignored when
matching names, and an exact match always wins over a normalised one.

## Mapping bones by hand

When the two rigs' names genuinely differ, **Map bones…** (on each import row;
it also opens itself when *Add clips* lands a partial match) shows both
skeletons drawn from their bind poses, side by side. Green joints matched by
name; **amber** ones carry a suggestion; **red** ones matched nothing. Click a
red or amber joint, then the joint on the right it should drive; right-click
removes a pair; *Accept suggestions* takes every amber guess at once.
Suggestions are never applied on their own — a wrong guess bends the wrong
limb, so a person confirms them.

The suggester reads four signals, not just spelling: **names** (tokens run
through a bone-vocabulary table, so `LeftUpLeg` ≈ `thigh_l` across
Mixamo/UE/Rigify conventions), **structure** (chain depth, subtree size — once
the hips anchor, their children look among the mapped hips' children), **bind
geometry** (normalized joint positions and bone directions, which is also what
pairs a rig whose names say nothing at all), and **your own history** (every
pair you ever accepted lands in a machine-global alias book, so the next file
from the same pack suggests itself). A left token or a clearly-left position
still never pairs with a right one.

Two shortcuts sit above the fuzzy scores. When the rigs differ by one rename
pattern, an **Apply rule** button appears — `strip 'rigX:'+'X' (34)` — one
reviewable transformation instead of thirty scores. And **Test pose** poses
both skeletons through the current mapping with a clip of the source (slider +
Play): a wrong pair shows as a folded limb in a quarter of a second, which
beats every percentage this window prints.

**Ask AI** (optional — uses the backend from *Edit > Preferences*) sends both
hierarchies with bind positions to the model and lands its proposals in the
list as one more suggestion group, blue, each with its own accept button.
Nothing it says is applied on its own, exactly like the local suggestions.

Hovering any joint shows its link at once — the other end rings up and a line
ties the two, accent for a real mapping, amber for a pending suggestion. The
**wheel zooms to the cursor** and **middle-drag pans** (double middle-click
resets), which is what makes finger and toe joints clickable at all.

Beside the canvas, the **pair list** shows every hand-made pair, every pending
suggestion and the bones nothing matched; hovering a row rings both joints in
the drawing and ties them with a line, so "which bone is this" never needs
reading coordinates. `+` accepts one suggestion, `x` removes one pair.

The accepted pairs are stored on the import row (`boneMap` in the `.tyra`) and
consulted **before** any name matching, so a hand-made pair always wins. The
merge itself never guesses.

Everything here is fed from a shared parsed-skeleton cache and the scene
preview re-bakes **in the background** (a small `baking preview` spinner shows
in the Animation Editor while it does), so picking a source, adding clips and
applying a mapping keep the editor responsive.

A clip whose tracks all fail to match is **not added**, and the build says so
rather than shipping an empty clip. A name collision gains a `_1` suffix, so
importing twice can never silently replace anything.

## Where it happens

Both formats work as either end — `.fbx` clips onto a `.glb` character is the
common case and needs nothing special — because the merge operates on the
**parsed skeleton** (`animmerge.cpp`), not on a format-specific scene.

Two consumers apply it, and they must not disagree:

- **the build** — `templates::bakeAnimAssets` merges before anything else
  touches the skeleton, so the LOD pass, the clip edits and the `.tskl` writer
  all treat an imported clip like a native one;
- **the editor** — every preview goes through `animmerge::bakedWithImports`,
  which for a model with no imports is the untouched parser bake it always was,
  and for a model with imports poses the merged skeleton itself.

That second path is why an imported clip is visible in the editor at all: the
format parsers bake morph frames by posing their own source scene, which by
definition knows nothing about clips merged in afterwards. Posing the merged
skeleton instead makes the preview *more* faithful, not less — the skeleton is
what ships. Measured against the parser's own bake on the same file, the two
agree to 4×10⁻⁴ units; a self-merge (a model importing its own clips) is an
exact identity.

Importing also **recomputes the model's pose bounds**. Those bounds are what the
console frustum-culls and box-collides with, and they were computed from the
character's own clips — an imported jump or lunge that reaches further would
otherwise be culled while still on screen.

## Limits

- The full retarget handles differing bind poses, facings and units; what it
  does NOT solve is **contact** — feet can slide between rigs of different
  proportions until the planned IK pass lands. Check the *Test pose*.
- Blend shapes / morph targets are not carried across (they are not supported by
  the animated-model pipeline at all — see
  [animated-models.md](animated-models.md)).
- The source's mesh, materials and textures are ignored. Only clips are taken.
- Each import row is one source file. Add a row per file; a folder of Mixamo
  clips is a row each.

## Where the data lives

`Project::animImports` (manifest section `"animImports"`, format v19). A row
names the target model, the source file, optionally which clips and a name
prefix, plus the retarget flags — each written only when it differs from its
default, so a project that never imported anything has no such key at all.

Both paths are real asset references: the Asset Browser retargets them on a
move or rename, and a clip-only source counts as *used* so it is not offered up
for deletion.
