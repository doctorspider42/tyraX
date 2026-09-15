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

## 16-bit colour: the trail never fully fades

**Motion blur wants a 32-bit frame buffer** (the default). On a project that set
*Project Preferences > Display > Colour depth* to 16-bit, a ghost stays on
screen after the motion stops — at every amount, not just high ones.

It is arithmetic, not a bug to be tuned away. PSMCT16 stores 5 bits per channel;
the GS blends in 8-bit and truncates the write back to 5. The pass moves a pixel
by `floor(d * fix / 128)`, so a residual of one 5-bit step (`d = 8`) needs an
increment of 8 to reach the next storable value — which needs `fix >= 128`, the
weight that freezes the picture outright. **At every usable weight the last step
is permanent.** One step is only ~3% of the range, but it is a full 1/32 of what
16-bit colour can express, and on flat surfaces and gradients it reads as a
milky imprint of wherever the camera used to point.

Measured, one variable: the same fixture at the same 100% amount, changing
*only* `colorDepth`. At 32-bit the settled frame is the sharp scene; at 16-bit
it is a permanent imprint of the **loading screen** the game left minutes
earlier. The editor warns where the amount is edited.

Hardware may soften it — the GS dithers PSMCT16 writes and PCSX2 does not, so
dithering turns the truncation stochastic — but the fork's dither offsets are
non-negative (see `tyraxDitherMatrix`, 1.70.4), so it cannot close the gap in
both directions, and **none of this has been measured on a console**.

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
