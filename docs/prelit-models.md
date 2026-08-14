# Pre-lit models (light baked into the texture)

*Select the model, then **Properties > Lighting > Bake lighting into texture**
(the two fields beside it are the output size and the rays per texel). Headless:
`tyrax-editor --bake-object-light <projectDir> <objectName> [size] [rays]`.*

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

## Things that will catch you

- **Move the object and its texture is a lie.** The light was gathered where it
  used to stand, contact shadows and all. Re-bake after moving it, and re-bake
  after changing the scene's lighting.
- **Re-baking is idempotent** because the albedo is read from the model's own
  `.mtl`, never from the pre-lit material the last bake assigned. You cannot
  accidentally multiply light in twice.
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
