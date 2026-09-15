# Motion blur

The fourth built-in full-screen effect, next to bloom, colour grading and film
grain. It blends the **previous rendered frame** over the current one, which is
how the PS2 era did motion blur: there is no velocity buffer and no pixel
shader, but the other display buffer already holds the last frame, so the whole
effect is **one full-screen alpha blend** — no extra VRAM, no EE time, one
screen of GS fill.

Turn it on in *Tools > UI Editor* (the **Motion blur** entry of the screen
stack), per scene in *Scene > Scene Preferences > Post effects*, or from a flow
graph with **Set Motion Blur**.

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
blur does not leak it into the next scene.

## How it works

`RendererCorePostFx::apply()` (`vendor/tyra/engine/.../renderer_core_postfx.cpp`)
adds one sprite:

- source: `RendererCoreGS::getPreviousRealFrameBuffer()`, blitted 1:1 over the
  current display buffer,
- blend: `(Cs - Cd) * FIX >> 7 + Cd`, i.e. the plain alpha blend with `FIX` =
  the amount as a 0..128 byte,
- point sampling, because the blit is 1:1 and a bilinear tap would drift the
  trail half a texel per frame into a directional smear.

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

## 16-bit colour

A 16-bit frame buffer (*Project Preferences > Display > Colour depth*) stores 5
bits per channel, and an accumulator is the one pass that really cares, because
it feeds its own output back in at a loop gain of `1/(1-f)` — about ten at the
top of the slider. Two distinct things go wrong there, and both are fixed above
by different means.

**It used to leave a permanent ghost.** The obvious blend,
`Cd += (Cs - Cd) * fix >> 7`, moves a pixel by an *increment*; once the residual
is inside one 5-bit step that increment rounds to less than one storable level
and the error stops decaying for ever. Two changes, and **both** are needed:

1. **Darken, then add.** The pixel is rebuilt from two large terms —
   `Cd = Cd * (128 - fix) / 128`, then `Cd += Cs * fix / 128` — which is the
   same weighted average with no small increment in it. (Both equations were
   already in the file: the grading's gain and the bloom's add-back.)
2. **A lower cap** (below), which shortens the trail and with it the width of
   the band the ghost can hide in — see *Why the ghost is permanent*.

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

A 16-bit channel stores `k` in 0..31 and the GS reads it back as `8k`. With the
scene constant at `S` and weight `f` out of 128, one frame is

```
k' = ( floor(S*(128-f)/128) + floor(8k*f/128) ) >> 3
```

At `f = 80` and `S = 200` that is `k' = (75 + 5k) >> 3`, and iterating it from
below climbs 10 -> 15 -> 18 -> 20 -> 21 -> 22 -> **23**, where it stops. But 24
and 25 are *also* fixed (`195>>3 = 24`, `200>>3 = 25`). The map has a BAND of
fixed points, and which one a pixel lands on depends on where it started — that
is, on what used to be on screen. Nothing decays; the pixel is already home.

The band is about `1/(1-f/128)` quantization steps wide, which is why:

* it is far worse at 16-bit — the step is 1/32 of the range instead of 1/256;
* a stronger blur is worse — the band widens with the weight;
* the settled picture is also DARKER, because `floor` puts every fixed point at
  or below the true value;
* dithering removes it — a varying offset makes the update non-deterministic, so
  no value is stable and the pixel wanders to the truth — at the price of the
  shimmer above.

Settled-frame ghost over flat groundSettled-frame ghost over flat ground, 5th-to-95th percentile per channel:

| 16-bit variant | R | G | B |
|---|---|---|---|
| lerp, fixed matrix | 40 | 32 | 51 |
| lerp, rolled matrix | 20 | 13 | 29 |
| darken+add, fixed matrix | 33 | 30 | 7 |
| **darken+add, rolled matrix** | **15** | **23** | **15** |
| 32-bit control | 12 | 20 | 6 |

**And it BLEEDS BRIGHTNESS**, which is the more visible half and the one a ghost
metric cannot see. Every write truncates downward, the loop multiplies that bias
by `1/(1-f)`, and at PSMCT16 a write drops three bits — eight times the bias of a
32-bit one. The picture does not tint, it goes dark and stays dark. Dithering
pays back only part of it: unbiased rounding would want offsets averaging 3.5,
`DIMX` entries are **3-bit signed** so anything above 3 is negative (1.70.4), and
0..3 averages 1.5. A "full range 0..7" matrix was tried and is exactly the
darkening that note warns about.

Settled green against the same scene with the blur off:

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
16-bit that is a shorter trail. 100% there now costs about what 100% costs at
32-bit, and the settled picture is clean on flat ground and sky alike.

## Triple buffering

The accumulation chain does not care how many display buffers there are, and
that is worth stating because it is the obvious thing to worry about: with three
buffers the rotation is `shown -> finished -> free`, and
`getPreviousRealFrameBuffer()` is always the frame that just finished, so each
frame still blends its immediate predecessor. The buffer being drawn into holds
an image from two frames ago, which the frame clear overwrites.

What DOES change is the rolling dither: three buffers means three accumulator
states in flight at three different dither phases, so consecutive displayed
frames come from different phases. Measured as the difference between two
settled captures a second apart (mean per channel, 0..255):

| | 2 buffers | 3 buffers |
|---|---|---|
| 32-bit | 0.006 | 0.021 |
| 16-bit | 0.199 | **0.780** |
| 16-bit, blur off (control) | — | 0.007 |

So four times the shimmer at 16-bit — and still **0.3% of range**, with no pixel
outside PCSX2's own FPS readout moving by more than one 5-bit step. The ghost
level itself is identical to the two-buffer case (15/23/15). It reads as what
dither always reads as, and it is the price of the matrix that stops the ghost;
at 32-bit the roll is inert and there is nothing to see either way.

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
pass and about a third of what film grain costs (grain draws two). No EE work,
no VU work, no VRAM: the source is a buffer the renderer already owns.

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
| 100% (= 115/128, the cap) | **sharp** — a long trail, fully caught up |
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
