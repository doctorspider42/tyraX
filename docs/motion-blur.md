# Motion blur

The fourth built-in full-screen effect, next to bloom, colour grading and film
grain. It blends the **previous rendered frame** over the current one, which is
how the PS2 era did motion blur: there is no velocity buffer and no pixel
shader, but the other display buffer already holds the last frame, so the whole
effect is **one full-screen GS blend** — no extra VRAM, negligible EE time and
one screen of fill.

Turn it on in *Tools > UI Editor* (the **Motion blur** entry of the screen
stack), per scene in *Scene > Scene Preferences > Post effects*, or from a flow
graph with **Set Motion Blur**.

**Clear trail when camera settles** is on by default in the UI Editor. It drops
the blend for one frame after about 0.12 seconds of quiet view motion, which replaces the accumulated
16-bit history with the fresh picture. The blur returns at full authored strength
on the next frame even while the camera remains parked, so a moving object still
leaves a trail. Turn the option off for an uninterrupted drugged/dazed-style
accumulator.

![Motion blur off and at 85%, mid-turn](img/motion-blur.png)

Both frames are the same camera turn on the same fixture, PCSX2 software
renderer; the amount is exaggerated so a still picture can show it.

## What the number means

The amount is **a percentage everywhere it is shown** — it is a fraction of the
effect's own range, and "0.20" tells a reader nothing about how strong that is.
(A flow-graph *wire* still carries 0..1, like every other number on that plane.)

It is the weight the old frame gets: `out = mix(new, old, amount)`.

It **compounds**, and that is the thing to know before turning the slider up.
Each frame blends a predecessor that had already blended its own, so a still
object's contribution decays as `amount^n` — at 50% a trail is still a quarter
visible three frames later.

| Amount | Reads as |
|---|---|
| 0% | off |
| 15 – 25% | a light smear on fast motion, invisible when standing still |
| 30 – 45% | a clear trail — a dash, a hit, a drugged/dazed state |
| 60%+ | a long ghost; readable only for a deliberate effect |
| 100% | the strongest the editor offers — see below |

**100% is not the hardware's full weight, on purpose.** The GS blend byte goes
to 128, and at 128 the arithmetic is exact: the destination becomes its own
predecessor and the picture **stops updating for ever** while the game runs on
behind it. That is not a strong setting, it is a broken one — and a slider whose
top end is broken is a slider nobody can use the top half of. So the authored
0..100% maps onto 0..115 of 128 (90%), the strongest weight that still lets the
picture through. The number lives in one place, `kMotionBlurMaxFix` in
`project.hpp`, read by the scene table, the flow node's codegen and the Live
Logic interpreter alike.

Because the trail is **temporal and not directional**, it smears anything that
moves on screen — the world when the camera turns, and a moving object when it
does not. That is the honest limit of the technique: it cannot blur one object
and leave the rest sharp.

## Where it sits in the screen stack

Like bloom and grain, motion blur is an entry in the *UI Editor*'s screen stack
and composites right before the HUD sprite it sits under; sprites above it draw
crisp on top. **Unlike** them it defaults to the **bottom** of the stack rather
than the top, and that is not cosmetic:

> The blur's source is the **finished** previous frame — HUD, prompts and menus
> included. From the top of the stack, a HUD element that MOVES smears over the
> whole picture. Underneath the stack, the trail is the scene's and the UI
> redraws crisp over it every frame.

A static HUD element is unaffected either way (it blends with an identical copy
of itself), so the rule only bites on animated bars, tickers and moving
crosshairs — which is exactly when nobody is looking for the cause.

When several effects share one slot they composite **motion blur, then bloom +
grading, then grain**: the trail is the scene smearing, so this frame's own glow
and its fresh noise belong on top of it.

## From a flow graph

**Set Motion Blur** takes an `Amount` — a bounded 0–100% slider, so it cannot be
dragged to a negative or a value the console would clamp away — and takes effect
on the next frame. Its number input accepts a wire, so a **Tween** ramps it (a
wired number is 0..1, the flow graph's own convention for the number plane, not
0..100):

```
On Action "sprint"  ->  Tween 0 -> 0.35 over 0.4s  ->  Set Motion Blur (Amount wired)
On Action released  ->  Tween 0.35 -> 0 over 0.3s  ->  Set Motion Blur (Amount wired)
```

The node is in the `Scene` category with the rest of the post-effect family, and
**Live Logic can hot-patch it** — edit the amount and the running game follows
without a rebuild.

Two conditions on that, and both are easy to miss because the LIVE chip stays
green through either (it streams *object* edits and has never carried graph
logic — see [Live Logic](live-logic.md)):

- **Live Logic has to be ON** (*Project Preferences > Build*). With it off, a
  graph edit reaches nothing until the next build; the toolbar says so with an
  amber **LOGIC (off)**.
- **The graph has to be patchable.** A `Flip Flop` (or any other branching node)
  in the same graph puts it out of reach of the interpreter, whatever the Set
  Motion Blur node itself supports — the chip goes amber **LOGIC (rebuild)** and
  the Debugger's *Logic* tab names the node.

Scene changes re-apply the scene's authored amount, so a graph that raised the
blur does not leak it into the next scene. The idle clear is a one-frame override
of the renderer, not of that authored/runtime amount, so **Set Motion Blur** keeps
working after a clear instead of being silently overwritten by the camera gate.

## How it works

`RendererCorePostFx::apply()` (`vendor/tyra/engine/.../renderer_core_postfx.cpp`)
adds one full-screen sprite:

- source: `RendererCoreGS::getPreviousRealFrameBuffer()`, blitted 1:1 over the
  current display buffer,
- blend: `(Cs - Cd) * FIX >> 7 + Cd`, with the previous frame as `Cs` and the
  fresh frame as `Cd`,
- point sampling, because the blit is 1:1 and a bilinear tap would drift the
  trail half a texel per frame into a directional smear,
- UV coordinates biased by `+0.5` texel at both ends. A GS pixel is centred at
  `.0`, while a texel is centred at `.5`; sampling `0..size` sits on the
  boundaries. PCSX2 selected the intended neighbour, but a physical GS running
  the 16-bit feedback pass selected the lower/right texel and shifted history
  one pixel per frame. The growing diagonal copies of otherwise stationary HUD
  text were that sampling error, not quantization.

Two details are load-bearing:

- **`getPreviousRealFrameBuffer()`, never `getPreviousFrameBuffer()`.** With
  [frame extrapolation](frame-extrapolation.md) on, the newest finished frame is
  a synthesised warp half the time, and feeding a displaced image back into an
  accumulator compounds the displacement into a visible shake. The "real" one is
  the last frame the SCENE rendered — the same accessor the neural upscaler asks
  for, and for the same reason.
- **`RendererCoreGS::hasRealFrame()` gates the first frame.** Display buffers
  are never cleared at allocation, so the "previous" buffer after boot — or after
  a layout rebuild, such as a display-mode switch or the adaptive upscaler
  changing resolution — holds whatever was in that VRAM. The pass simply does not
  run until one real frame has been flipped.

## Colour depth and GS dithering

The renderer only writes `DTHE = 1` for a PSMCT16 framebuffer. This guard is
not optional: the GS manual says the result of dithering an RGBA32/RGB24 target
is not guaranteed and tells software to turn it off. PCSX2 commonly treats that
combination as a no-op; a physical GS instead exposed the 4x4 DIMX pattern as a
fine checkerboard over a 32-bit picture. VRAM usage still correctly reported a
32-bit framebuffer — the pattern came from an invalid dither state, not a
silent colour-depth fallback. Every path that restores DTHE, including the
alpha-mask bracket, uses `RendererSettings::isDitherActive()` so it cannot
re-arm the invalid combination behind the drawing environment.

## 16-bit colour

A 16-bit frame buffer (*Project Preferences > Display > Colour depth*) stores 5
bits per channel, and an accumulator is the one effect that really cares, because
it feeds its own output back in at a loop gain of `1/(1-f)` — about ten at the
top of the slider. Quantization is a real hardware limit, but it is not a reason
to leave a permanent trail on screen: the lower cap limits the error while the
optional idle clear cuts the feedback loop once the view settles.

The blend moves a pixel by an *increment*. Once the residual is inside one
5-bit step, that increment can round to less than one storable level and stop
decaying. The default idle clear is the exact way out: one frame does not blend
history at all, so there is no residual to quantize.

A two-pass workaround was tried before that policy existed: darken the fresh
frame, then add the history. It narrowed one measured ghost spread, but both
halves wrote the 16-bit framebuffer separately. That means **two quantizations
per frame**, with both downward errors fed back by the accumulator — the reason
the picture lost brightness while moving. Once the one-shot reset existed, the
workaround was strictly the wrong trade: the pass returned to a single lerp.

A dither matrix that ROLLS one cell per frame was tried and removed. It does
clear the last of the ghost (settled sky 6/0/0 against 19/5/4), because a moving
offset is exactly what breaks the fixed points below — but it puts the noise in
motion: **16.6% of the sky's pixels moved every frame** on `examples/showcase`,
against 0.0% with the matrix left alone. A shimmering sky is a worse artefact
than a faint static one.

### Why the ghost is permanent

The intuition that a still camera should simply overwrite it is right in real
arithmetic — the residual decays as `weight^n`. It is integer truncation that
breaks it, and the mechanism is worth knowing because it explains every other
number on this page.

A 16-bit channel stores `k` in 0..31 and the GS reads it back as `8k`. If the
fresh frame stores `n`, the previous frame stores `k`, the fixed dither entry is
`d`, and the weight is `f` out of 128, one frame is

```
k' = ( 8n + floor((8k-8n)*f/128) + d ) >> 3
```

The map can have a BAND of fixed points, and which one a pixel lands on depends
on where it started — that is, on what used to be on screen. Once it reaches
one, waiting longer changes nothing; the pixel is already home.

The band is about `1/(1-f/128)` quantization steps wide, which is why:

* it is far worse at 16-bit — the step is 1/32 of the range instead of 1/256;
* a stronger blur is worse — the band widens with the weight;
* a continuous accumulator tends darker, because `floor` puts fixed points at
  or below the truth;
* dithering removes it — a varying offset makes the update non-deterministic, so
  no value is stable and the pixel wanders to the truth — at the price of the
  shimmer above.

The historical experiments are still useful because they show why neither
"split the blend" nor "move the dither" is a free fix. Settled-frame ghost over
flat ground, 5th-to-95th percentile per channel:

| 16-bit variant | R | G | B |
|---|---|---|---|
| lerp, fixed matrix | 40 | 32 | 51 |
| lerp, rolled matrix | 20 | 13 | 29 |
| darken+add, fixed matrix | 33 | 30 | 7 |
| **darken+add, rolled matrix** | **15** | **23** | **15** |
| 32-bit control | 12 | 20 | 6 |

The two-pass experiment also exposed the more visible failure that a spread
metric missed: **brightness loss**. Every framebuffer write truncates downward,
the loop multiplies that bias by `1/(1-f)`, and the split form did it twice per
frame. The picture did not tint; it went dark and stayed dark. Dithering paid
back only part of it: unbiased rounding wants offsets averaging 3.5, while
`DIMX` entries are 3-bit signed and this engine keeps them in the safe 0..3
range. A "full range 0..7" matrix is half negative and darkens additive passes.

Settled green from that old two-pass experiment, against the same scene with the
blur off:

| amount | 16-bit | 32-bit |
|---|---|---|
| 35% | −4.2% | |
| 60% | −8.3% | |
| 75% | −11.3% | |
| 100%, old cap (115) | **−43.1%** | −10.4% |
| 100%, cap 80 | **−9.0%** | |

So the cap is **per colour depth** — `kMotionBlurMaxFix` 115, `kMotionBlurMaxFix16`
80, resolved by `motionBlurMaxFix()` and read by the scene table, the node's
codegen and the Live Logic interpreter alike. It is the same decision the 115
already was: the top of the slider has to be a value somebody can use, and at
16-bit that is a shorter trail. The cap remains conservative after returning to
one write per frame; with idle clearing enabled, the settled frame is exact
rather than merely close because no quantized history survives the clear.

### Why the idle clear is one shot

An earlier version scaled the blur continuously by camera speed and held it at
zero while the player stood still. That fixed the background but also disabled
the effect for a vehicle, projectile, enemy or particle moving past a parked
camera — precisely the content whole-frame accumulation is able to blur.

The shipped policy instead waits for about 0.12 seconds of quiet camera motion,
sets the renderer's amount to zero for **one frame**, then restores the authored
amount. Translation and turn rates are measured per second, not per rendered
frame. This matters on hardware: a 30 FPS frame contains twice the displacement
of a 60 FPS frame, and the old per-frame threshold mistook small physical-pad
drift for continuous camera movement, so the clear never fired. Camera bob,
float noise and normal stick drift sit below the meaningful-motion threshold.
A scene load also forces one
fresh frame so the previous scene cannot smear into the next one. This cannot
detect every later object-only transition; if an uninterrupted accumulator is
required, disable the option and accept the 16-bit fixed-point residue.

## Triple buffering

The accumulation chain does not care how many display buffers there are, and
that is worth stating because it is the obvious thing to worry about: with three
buffers the rotation is `shown -> finished -> free`, and
`getPreviousRealFrameBuffer()` is always the frame that just finished, so each
frame still blends its immediate predecessor. The buffer being drawn into holds
an image from two frames ago, which the frame clear overwrites.

The dither matrix stays fixed in screen space. Rolling it was measured and
removed: it cleared more of the residual, but made 16.6% of a parked sky change
every frame. Triple buffering therefore adds no special dither phase or shimmer.

## Interactions

- **[The neural upscaler (BLSS)](neural-upscaler.md)** samples the previous frame
  as its temporal history, so with both on, the history it reprojects is already
  blurred and the trail persists longer than the Amount suggests. It is not
  refused (nothing breaks), but tune the amount with the upscaler in the state it
  ships in, not without it.
- **[Frame extrapolation](frame-extrapolation.md)** composites nothing: a
  synthesised frame is a warp of an image that already carries the blur, and the
  pass is not re-run on it. The blur therefore updates at the render rate, not
  the presentation rate.
- **Film grain** in the previous frame is blended in as well, so grain plus a
  strong blur reads slightly smoother than grain alone.

## Cost

One full-screen alpha-blended sprite — the same GS fill as one bloom composite
and about half of film grain. No VU work and no VRAM: the source is a buffer the
renderer already owns. The idle gate adds only a few scalar camera comparisons
on the EE and draws nothing on its clear frame.

## Verifying it

A still camera sees nothing — the blur of a frozen picture is that picture. Move
the camera and read the pixels rather than eyeballing them: with the blur on, two
captures taken during the same turn differ **less** than with it off, because
each frame is partly the previous one. See the tyra-testing skill's motion gate
for the way to capture a moving picture repeatably.

**Mind the cadence.** A host-side capture loop manages roughly one frame every
0.3 s, which at 50 fps is ~15 game frames — and a trail at 45% has decayed to
`0.45^15`, i.e. nothing, by then. A pair of captures that far apart cannot see an
ordinary amount at all, and reports the feature as absent.

**And do not read "the two captures are identical" as "the picture is frozen".**
That inference is wrong twice over on this fixture, and it cost a round of
measurement:

- A heavy trail over a **repeating** pattern averages it FLAT. The `fpp`
  fixture's ground is a two-tone checkerboard; smeared over ten frames of a yaw
  turn it becomes near-uniform green, and a uniform green field looks the same
  from every heading. Consecutive captures then agree to 0.02% while the game is
  turning perfectly well — the same trap as the axis-aligned walk in the
  tyra-testing skill, one dimension over.
- A trail and a freeze are indistinguishable *while the camera moves*. The
  question is whether the picture CATCHES UP, so the test is: turn for 3 s, let
  go, and look at the settled frame.

That settle test is the honest instrument, and it is unambiguous — one look at
the image answers it:

| Weight | settled frame after the camera stops |
|---|---|
| 0% (control) | sharp |
| 100% (= the colour-depth cap), idle clear on | **sharp** — one fresh frame replaces the history |
| 100%, idle clear off | may retain a faint 16-bit fixed-point residue |
| 128/128 (only reachable by removing the cap) | **the TyraX boot splash**, thousands of frames later |

The last row is what the cap exists for: at 128 the blend is `Cd += (Cs-Cd)*128>>7`,
i.e. exactly `Cs`, so the error never decays and the first image the game ever
displayed stays on screen for ever while it runs on behind it. At 115 the same
error decays to zero in about 27 frames (`floor(d*115/128)` reaches 0), which is
the half-second the settled frame shows.

**And check what is driving the amount.** A `Set Motion Blur` node on an
`On Start` trigger overrides the scene's authored value on the first frame — two
arms of an A/B then run at whatever the graph says and measure identically,
which reads as "the effect does nothing".
