# Pre-lit models (light baked into the texture)

*Select the model, then **Properties > Lighting > Bake lighting into texture**
(the two fields beside it are the output size and the rays per texel). Every
pre-lit object in a scene lives in **Tools > Baked Lighting > Pre-lit models**.
Headless: `tyrax-editor --bake-object-light <projectDir> <objectName> [size]
[rays]` for one, `tyrax-editor --bake-prelit <projectDir> [sceneName]` to
refresh every stale one.*

A textured surface cannot take this engine's
[lightmap](global-illumination.md), and that is not a limitation waiting to be
lifted. The GS blend unit computes

```
Cv = (A − B) × C + D      A, B, D ∈ {Cs, Cd, 0}      C ∈ {As, Ad, FIX}
```

`C` is always an **alpha**. So "texture × lightmap" — a colour times a colour —
is not expressible in a second pass at all, and the atlas pass this engine does
have is **additive**, which over a texture blows out its dark texels.

Which leaves exactly one way to put per-pixel static light on a textured model,
and it is the way the survival-horror games of the PS2 era did it: **bake the
light into the albedo** and ship a unique, pre-lit texture for that surface.
That is what this does.

## What it bakes

For one placed object, every texel of its UV islands is walked, turned into a
**world** position and normal through the object's own transform, and handed to
the same integrator the GI bake uses — sky through whatever openings exist, the
sun behind a shadow ray, emissive materials, baked point lights, and the solved
bounce radiosity of every surface a ray lands on, all over a triangle BVH of the
whole scene. The result multiplies the object's own albedo, and the product is
written as `res/materials/<name>-lit.png` with a one-entry `.mtl` beside it.

The object is then pointed at that material and marked **pre-lit**, which
switches its vertex colours to neutral: no ambient, no N·L, no baked point
lights, no emissive pools. Every one of those is already in the texture, per
pixel, and adding them again would light the surface twice.

**Dynamic light still lands on top** — the flashlight's
[projected pool](flashlight.md), its cone, and live point lights are all added
at run time. That is the whole arrangement: the static half per pixel in the
map, the moving half added over it.

The button runs on a worker thread - the scene build and the bounce solve are
the expensive half - and shows a progress bar with what it is doing. The result
is applied on the UI thread when it lands, so it goes through the editor's
commit/undo path like any other object edit, and the panel then says **"Pre-lit:
its texture carries its light"** for as long as that stays true.

## What it costs

A pre-lit surface needs **its own texture**, because its lighting is particular
to where it stands. Two identical sheds ten metres apart cannot share one. That
is the era's bargain and there is no way around it — spend it on the wall the
player walks up to, not on everything. Watch `VRAM` in the debug HUD
([gs-vram.md](gs-vram.md)); at 128² RGBA32 each pre-lit object is 64 KB of a
~1.33 MB budget.

## Managing pre-lit objects

One button per object is fine for one object. A scene with a dozen of them
needs to answer three questions the button cannot: *which* objects ship pre-lit,
*whether their textures still agree with the scene*, and *what that costs*.
**Tools > Baked Lighting > Pre-lit models** is where those live — the same tab
as [Model AO](ambient-occlusion.md#model-ao), because both are light computed on
your desktop and shipped as pixels inside textures the project already has.

### The intent, and the bake, are two different things

Each row carries a **tick**, stored on the object as `prelitWanted`: *this object
should ship pre-lit*. Ticking it bakes nothing — a bake is minutes, and it
belongs on a button you pressed on purpose. **Unticking it reverts**
immediately, because a pre-lit texture on an object nobody wants pre-lit is
exactly the state this panel exists to end.

- **Bake pending (N)** bakes every ticked object that is not already fresh.
- **Re-bake all (N)** bakes every ticked object regardless.

Both run one batch on a worker thread with a progress bar naming the object it
is on (`2/7: crate-3`) and a Cancel. The batch is what makes a scene-full
affordable: `gibake` builds the scene's triangle BVH and solves its bounces
**once**, however many objects come out of it — that solve is nearly all of the
wall clock, and the per-texel gather afterwards is seconds each. The whole batch
lands as **one undo step**.

### Fresh, stale, not baked

Every bake stamps the object with a **signature** (`prelitSig`) of everything
that could change what it produces:

- the scene's entire light — sky, sun, emissive materials, baked point lights,
  and every contributing object's transform, material and model **file
  content** (this is `gibake`'s own signature, so a repainted wall stales the
  objects that see it);
- this object's own position, rotation and scale;
- the model, its `.mtl` libraries and the source material, by content;
- the bake parameters (size, rays, padding, strength, floor, seed);
- the [Model AO](ambient-occlusion.md#model-ao) map, when it resolves on for
  that asset — because that map is multiplied into the albedo the bake reads.

A row reads **pre-lit** (green) while the stored signature still matches, and
**stale** (amber) the moment it does not. Content hashes, never modification
times: a `git clone` or a file copy is not an edit.

Two deliberate holes, both to stop a re-bake storm. The signature is computed
over the scene **as authored** — every pre-lit override normalized back to its
source material — or applying a bake would change the scene's signature and
stale the object it just baked, plus every other pre-lit object beside it. The
price is that bounce light picked up off a *neighbour's* new pre-lit texture
does not stale anything, which is a second-order term. And the bake parameters
are editor state, not project state, so the CLI verb uses their defaults
(128 px, 96 rays): bake from the panel at 256 and `--bake-prelit` will consider
it stale.

### Reverting

**Properties > Revert to source material**, or untick the object's row. The
object goes back to the `materialPath` it had **before its first bake**
(`prelitSource`, recorded then and never overwritten by a re-bake — an empty
value is a real one and means "the model's own `mtllib`"), and stops being
pre-lit. The `-lit.png` / `-lit.mtl` are **left on disk**: they are ordinary
assets, the [Asset Browser](asset-browser.md) already lists unused ones, and
deleting files behind your back on an undoable edit is the worse failure.

### The budget

The panel prints `N pre-lit textures ~ X KB of the ~1.33 MB GS budget`, counted
from each object's own baked image where it exists. That is the number to watch
— see [GS VRAM](gs-vram.md); at 128² RGBA32 each object is 64 KB, and the GS
pins a texture forever once it has drawn it.

An object the editor can prove moves at run time (physics, pickable, usable,
save-state, streamed, named by a flow graph) gets a **moves at runtime** marker.
It is a warning, not a refusal: a pre-lit texture glues the light to the
surface, so such an object carries contact shadows that match nothing the moment
it moves.

### Headless

```
tyrax-editor --bake-prelit <projectDir> [sceneName]
```

Re-bakes every `prelitWanted` object whose signature is stale, in every scene or
the named one, then saves and regenerates. It prints one line per object —
`baked` or `fresh` — plus a summary, and exits non-zero if any bake failed. Like
the other headless verbs that write the project, it refuses a project that needs
[a format migration](format-versioning.md). Running it twice is the check that
the staleness tracking is honest: the second run bakes nothing.

### Or let the build do it — for stale objects only

By default nothing bakes at build time, on purpose: a bake that takes seconds is
pressed, not implied (the [GI](global-illumination.md) rule). If you would rather
never ship a stale texture, tick **Re-bake stale pre-lit objects** in *Project >
Preferences > Build* (the same switch sits under the pre-lit table). Every build
— the toolbar, `--build`, Run on PS2 — then runs exactly what `--bake-prelit`
runs, before the procedural volumes are baked and the sources refreshed: only the
`prelitWanted` objects whose signature is stale, in every scene. A build with
everything fresh pays for one signature pass and nothing else; a build with three
moved objects pays one scene solve plus three gathers and says so in the status
bar and the build log. In the editor it lands as one undoable edit, and it never
touches GI (still explicit) or model AO (always automatic).

## Things that will catch you

- **Move the object and its texture is a lie.** The light was gathered where it
  used to stand, contact shadows and all. You no longer have to remember: the
  object reads **stale** in the panel above and `--bake-prelit` picks it up —
  or the build does, with the opt-in above.
  Same after changing the scene's lighting, or repainting a wall it can see.
- **Re-baking is idempotent** because the albedo is read from the model's own
  `.mtl`, never from the pre-lit material the last bake assigned. You cannot
  accidentally multiply light in twice.
- **The model's own AO comes along.** If
  [Model AO](ambient-occlusion.md#model-ao) resolves on for that asset, the
  pre-lit bake multiplies the same map into the albedo it reads — otherwise the
  object would lose its self-occlusion the moment it went pre-lit, which reads
  as the pre-lit bake having flattened it. The `-lit.png` output is in turn
  skipped by Model AO, so nothing is darkened twice.
- **The model needs UVs.** Unwrapped models only; the bake reports how many
  texels its islands covered, and zero means it has none.
- **A model's materials share one output texture.** Its islands must not
  overlap between them, which is true of any ordinary unwrap.
- **Night scenes bake dark**, faithfully. A pre-lit object in a scene with
  almost no static light is almost black until something dynamic lights it —
  which is exactly the Silent Hill arrangement, and exactly what you do *not*
  want if the object is supposed to read on its own. `floorLevel` in the bake
  parameters is the floor that keeps it from going fully black.

## When to use something else

| Situation | Better answer |
| --- | --- |
| Untextured static primitive | The [scene lightmap](global-illumination.md) — already per texel, costs no extra texture at all |
| Anything that moves | The probe grid; a pre-lit texture glues the light to the surface |
| A big flat wall the torch will sweep | Build it as a box: the [pool](flashlight.md) lands on it per pixel and it takes no cone |
| A model you have many copies of | Leave it on the probe path — one texture each is the wrong trade |

## See also

- [The flashlight](flashlight.md) — the dynamic half that lands on top.
- [Baked global illumination](global-illumination.md) — the integrator this
  borrows, and the per-texel route for untextured geometry.
- [Material map baking](material-baking.md) — the model's *own* AO/curvature
  into its texture; the same idea at the surface-detail scale.
