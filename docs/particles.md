# Particle library

The particle library is the one place a project's particle effects are made:
an effect - its motion, size, colour, blend and texture - is defined once in
*Tools > Particle Editor* and then picked by any emitter and by a vehicle's
tyre smoke, so "the campfire" is one thing to tune instead of ten emitters to
keep in step. The editor can also generate the textures: smoke puffs, flames
and glows, baked from a recipe into ordinary PNG + `.mtl` assets.

![The particle-lab example on the PS2 renderer: an additive flame with a generated texture, wood smoke from the fire and from a chimney (one library effect used twice), torch sparks and magic motes.](img/particles-lab.png)

`examples/particle-lab` is the reference scene.

## The editor

*Tools > Particle Editor* (window key `particles`):

- **New effect...** starts from a preset for each motion - Fire, Smoke, Fog,
  Sparks, Rain, Custom - with a matching generated texture already baked.
  **Duplicate**, **Delete** (linked emitters keep the look they last had,
  vehicles fall back to the built-in smoke) and **Place in scene** (an emitter
  linked to the effect, inserted where objects are inserted).
- **Behaviour**: the motion (the same six emitter kinds - Fire .. Rain are
  the built-in motions, Custom uses speed / spread / gravity / weight /
  lifetime / grow), count, size, tint, opacity and **Additive**.
- **Texture**: *Generate* picks a recipe - *Smoke puff*, *Flame*, *Glow /
  spark* - or *None* to use any existing material. A recipe has a resolution,
  a seed, softness, detail and noise scale, plus turbulence and heat for the
  flame and a colour for smoke and glow. Releasing a control re-bakes.
- **Preview**: the texture itself, and an animated 2D approximation of the
  motion. It is an approximation on purpose: the exact per-kind formulas run in
  the viewport on a placed emitter, and a third copy of them here would be a
  third thing to keep in sync.

Renaming an effect retargets every emitter and vehicle that names it.

## Emitters: linking an effect

*Properties > Particle emitter > Library* picks an effect (or *own settings*,
the emitter's own knobs as before). A linked emitter's knobs are greyed out
and **Edit...** opens the effect.

The link is by NAME (`SceneObject::particleEffect`, serialized as the
emitter's `"effect"`), and the effect's values are **copied into the emitter's
own fields** by `project::applyParticleEffects` - on every commit and at load.
That copy is the design: codegen, the viewport preview, Live Link, the time
machine and every other consumer keep reading ordinary emitter fields and do
not know the library exists. The price is that a linked emitter's own colour
and material are overwritten by the effect's on the next commit; unlink it to
edit them by hand.

## Additive blending

`Additive` (an effect's or an emitter's own, `SceneObject::emitterAdditive`,
`SceneObjectData::emitAdditive`) makes the particles ADD light: the bag's
`additiveBlendFix = 128`, i.e. `Cs * FIX + Cd` on the GS. Fire, sparks and
magic want it; smoke, dust and fog do not.

Two consequences, both twins in the viewport (`drawEmitterPreviews`):

- **The GS never reads alpha in an additive bag.** The fade over a particle's
  life therefore rides the vertex COLOUR (`rgb *= alpha / 128`), and a
  texture's shape must live in its RGB - every generated texture is
  premultiplied for exactly this reason. A hand-made texture whose silhouette
  is only in alpha draws as a square when additive.
- The viewport draws additive emitters with `GL_ONE, GL_ONE` so it shows what
  the console shows. It also masks ALPHA writes for every particle: the
  viewport image is composited by ImGui with its alpha, and a translucent puff
  that lowered it let the dark window background through - smoke previewed
  nearly black while the console drew it light grey.

Every emitter - additive or not - is now **depth-tested but never writes
depth** (`PipelineZTest_TestOnly`, the tyre smoke's rule). A translucent puff
that wrote Z carved its whole quad out of every puff drawn after it; with a
soft-edged texture that showed as hard circles inside the smoke column. This
changes the look of existing projects' emitters slightly, always in that
direction.

## Generated textures

`src/particletex.cpp` (host-only, deterministic) turns a `ParticleTexGen`
recipe into RGBA pixels; the editor writes them to
`res/materials/particles/<effect>.png` plus a one-material `<effect>.mtl`
that points at it, and the effect's material becomes that `.mtl`. Nothing
downstream knows a texture was generated - it is an ordinary material.

- **Smoke puff**: two fBm fields - large lobes warp the silhouette into a
  cloud, fine noise makes the billows - lit from above.
- **Flame**: a base-down teardrop whose sides are licked sideways by noise
  that grows with height, tongues at the tip, and a temperature ramp from a
  white core through yellow and orange to red.
- **Glow / spark**: a hot white core in a soft coloured halo.

Every kind reaches exactly zero before the quad's edge. The texture is pinned to **full colour** in `textureQuality` (a soft alpha ramp does not survive the
palettized CLUT bake - the same reason the AO maps are RGBA32), so budget it:
4 / 16 / 64 KB of GS VRAM at 32 / 64 / 128 texels. 64 is the default and the
right size for almost everything; sparks and motes look the same at 32.

### Flipbooks

*Frames* (1 / 2 / 4 / 8) bakes that many frames of one recipe and the console
swaps them at *Frames / s*. The noise travels through a SEAMLESS loop - two
copies of the field offset by one period, cross-faded by the loop phase and
rescaled so the mid-loop frames keep their contrast - so frame N would equal
frame 0 and the cycle never jumps. A flame's tongues climb and its height
breathes, smoke billows roll upward, a glow's core pulses.

Frame 0 is `<effect>.png/.mtl`, frame k is `<effect>-f<k>.png/.mtl`
(`particletex::framePath`, the one naming rule the bake, codegen and the
viewport share). Codegen lists the frames CONTIGUOUSLY after frame 0 in
`MATERIAL_PATHS` and bakes `emitFrames`/`emitFps` into the emitter's row; the
runtime picks `material + (int)(time * fps) % frames` and swaps the ONE bag's
texture pointer - no extra submit, no per-particle work. Every frame is its
own texture in GS VRAM (4 x 64x64 = 64 KB), which the editor prints under the
recipe. All particles of an emitter show the same frame at a time; the
variety comes from their sizes, ages and the mirror below.

### Orientation and mirroring

The camera basis the billboards are built on is screen-LEFT / screen-DOWN (the
world is viewed down +Z with +X on the left), so every quad used to be turned
180 degrees - invisible for the round textures emitters had, upside down for a
flame. The weights (`m00..m11`) are negated for every kind but rain, which
fixes every pass at once: the portal and split-screen views rebuild the basis
but never the weights. Odd particle slots are also mirrored left-right (not
rain, not fog, which spins), so one texture reads as two. The vehicle smoke,
the viewport preview and the editor's 2D preview follow the same rule.

Files are written only when their bytes change, so re-baking an unchanged
recipe touches nothing. Headless: `tyrax-editor --bake-particles <projectDir>`
re-bakes every effect with a recipe, re-syncs linked emitters, saves and
regenerates - run it twice and the second run must change no file.

## Vehicle tyre smoke

*Tools > Vehicle Editor > Tyre smoke* picks an effect for a definition
(`VehicleDef::smokeEffect`, "" = the built-in grey puffs). WHEN and WHERE puffs
spawn stays the sim's - the tyre slip decides, the rear wheels place them (see
[vehicles.md](vehicles.md), "Tyre smoke"). The effect supplies the look:

| effect field | smoke |
|---|---|
| tint | puff colour |
| opacity | peak alpha |
| size, grow (Custom) | start size, end size (other motions grow 2.4x) |
| lifetime | life multiplier (1.5 s = the built-in) |
| gravity < 0 (Custom) | rise multiplier |
| texture, additive | the POOL's texture and blend |

Codegen bakes one `VEHICLE_SMOKE_LOOKS` row per definition and each puff
remembers its car's row, so two cars with different smoke effects share the
pool without mixing looks. The pool is still ONE bag - one submit for every
car's smoke - so it has **one texture and one blend**: those of the first
definition whose effect names a texture (`VEHICLE_SMOKE_MATERIAL`,
`VEHICLE_SMOKE_ADDITIVE` in `model_data.gen.hpp`). A definition without an
effect keeps the built-in grey tone in that pool. The smoke texture's
material is appended to `MATERIAL_PATHS` after every object's (so a project
without one keeps its indices) and kept resident by the layer-residency pass,
because no scene object names it.

## Format

Format v62: the `"particleEffects"` section (`project::Section::Particles`,
flipbook `"frames"`/`"fps"` inside a recipe), an emitter's `"effect"`,
`"additive"`, `"frames"` and `"fps"`, a vehicle's `"smokeEffect"`. All are
written only when set, so a project that uses none resaves byte for byte.

## Limits

- A flipbook's frames advance together for the whole emitter (one bag, one
  texture pointer); per-particle frame phase would need a UV channel the
  billboard program does not have.
- The editor's animated preview approximates the preset motions; the placed
  emitter in the viewport is the exact preview.
- All cars share one smoke texture and blend (above).
- Renaming an effect keeps its texture file under the old name until the next
  re-bake writes the new one; the old file is left in place.
