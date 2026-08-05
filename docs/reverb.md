# Reverb (rooms for the sound effects)

Draw a box, tick one checkbox, and every sound effect the player hears while
standing inside it plays through a **real reverb** — a cave, a hall, a stairwell.
It costs the EE nothing: the PlayStation 2's sound chip has a hardware reverb
unit and this feature is the wiring that reaches it.

Reverb is authored on an [Area](areas.md), the same invisible volume that
already serves streaming zones, catch lists and the *In Area* trigger. Select
one, tick **This area is a room for the sound effects**, pick a preset and a
strength.

## What the hardware actually gives you

Everything below follows from one fact: the SPU2 has **one reverb unit per
core**, and the generated game currently puts the streamed music and every
sound effect it plays on one of them. So:

| | |
|---|---|
| **One room at a time** | Zones do not mix. The one the listener is inside wins; overlapping zones are decided by *Priority*. |
| **Strength is continuous** | The wet level is an ordinary register. Crossing between two zones that share a preset is a smooth ramp. |
| **The preset is not** | Changing the algorithm means zeroing its work area in sound RAM, which cannot be done under a live tail. The game ramps the strength to zero, swaps, and ramps back — about 0.3 s. |
| **The send is per voice, but only on/off** | Any single emitter can opt out of the reverb entirely, but there is no per-emitter "30% wet" — only the room's own strength. |
| **Music stays dry** | Deliberately, and it takes an explicit register write to keep it that way: the sound driver's own defaults send everything into the reverb, music included. |

Nothing here is a TyraX limitation to be lifted later by better code — it is the
shape of the chip, with one exception that is now half-lifted.

**"One room at a time" is on its way out.** The SPU2 has two cores and therefore
*two* reverb units; upstream audsrv used only one and left the other core muted.
The [audsrv fork](../vendor/tyra/audsrv/README.md) now plays voices on both
(channels 0-23 = core 1, 24-47 = core 0) and `Tyra::AudioReverb` drives both
units as `BusA` / `BusB`. What is not written yet is the game side: choosing a
bus per room, moving new sounds onto it and ramping the two depths past each
other. Until that lands the generated game still drives bus A alone, so
everything in the table above holds as written — a preset change still cuts.
The design intended, recorded so it is not re-derived: a room owns a bus, new
sounds go to the incoming room's bus while the ones already playing finish on
the outgoing one, which is also what a real room does to a sound you carry out
of it.

## Presets

Ten, and they are the console's own — TyraX does not implement a reverb, it
selects one.

| Preset | What it is | Work area in sound RAM |
|---|---|---|
| Off | No reverb. Useful *inside* another zone — a sealed closet in a hall. | — |
| Room | A small, tight room. The subtle one. | 10 KB |
| Studio A / B / C | Treated rooms, progressively bigger. | 8 / 18 / 28 KB |
| Hall | A large hall — the obvious cave, church, hangar. | 43 KB |
| Space echo | A long, washed-out ambience. Very wet. | 62 KB |
| Echo | Discrete repeats. *Delay* and *Feedback* apply. | 96 KB |
| Delay | A single delay line. *Delay* and *Feedback* apply. | 96 KB |
| Pipe | A resonant tube — metallic, narrow. | 15 KB |

The work area is carved out of the 2 MB of sound RAM the ADPCM samples also
live in, at the top. A project would have to load ~1.9 MB of effects before the
two could collide.

**Delay and feedback are read by Echo and Delay only.** Every other preset
zeroes both before the registers are written — Pipe looks like an echo and is
not one. The editor greys them out accordingly.

## Strength, and what a transition sounds like

*Amount* is 0..1 and maps linearly onto the hardware's wet return level, 1.0
being its maximum. It is the only value that moves smoothly, and the game ramps
it over ~0.3 s whenever the target changes, so walking through a doorway is a
fade rather than a step.

That gives the authoring rule worth remembering:

- **Two zones with the SAME preset cross-fade.** A cave mouth that is
  `Hall 0.3` opening into a cave that is `Hall 0.9` sounds like one continuous
  space.
- **Two zones with DIFFERENT presets cut.** The reverb fades out, the algorithm
  is swapped, and it fades back in. Roughly 0.3 s of dry in the middle. The
  Properties panel warns you when a scene contains both, because it is not
  visible in the viewport and it is very audible in the game.

Outside every zone the reverb is off. There is no scene-wide default: a level
that should be reverberant everywhere gets one big box, which keeps the cost
where an author can see it.

## Priority

Zones overlap freely. The one with the highest **Priority** that contains the
listener wins; equal priorities go to the later object in the scene. So a small
dead room inside a cathedral is "cathedral priority 0, dead room priority 1" —
and the dead room can be `Off`, which is what makes the effect stop at its door.

Zones are read **live**, like every other area: a flow node that moves the box
drags the room with it, and a zone sitting on an unloaded streaming layer
catches nobody.

## Which sounds are affected

**Sound emitters** carry a *Reverb* checkbox (on by default). Turn it off for a
sound that must be identical everywhere — a UI beep, a voice line, or a sample
that was recorded with its own room already baked into it.

**Play Sound** flow nodes have the same switch as a *Dry* parameter (0 = the
room applies, 1 = never). It defaults to 0, so graphs written before this
existed keep behaving as one would expect.

Because the send is one bit per voice and the emitters share eight channels
round-robin, the emitter that most recently triggered a channel owns that
channel's setting. In practice this is invisible; it only matters if you mix
reverb-on and reverb-off emitters that fire in lockstep.

## Driving it from a flow graph

**Set Reverb** (Audio category) overrides the zones entirely — a scripted moment
that must sound like a cathedral wherever it happens, an underwater stretch, a
dream sequence. Its `set` pin forces a preset and amount; its `clear` pin hands
control back to the zones. Unlike most switch nodes the override is a **state**,
not an event: it stays in force until cleared.

Its *Amount* accepts a wired number, so a **Tween Value** into it ramps a room
up or down over time.

## Cost

Per frame, with any zone in the scene: a handful of dot products to find the
room the listener is in, and — only when a value actually changes — one
synchronous RPC to the sound processor. A player standing still in a room costs
zero of those. The mixing itself is done by the sound chip and never touches the
EE.

The strength is quantized to 64 steps before it is sent, for exactly this
reason: the audio RPCs share one lock with the music stream, and an RPC per
frame measurably costs frame rate.

## How it is wired (for the curious)

The EE cannot normally reach the sound chip's registers — the audio server the
engine uses exposes only playback (which is separate from the fork of that
server described in `vendor/tyra/audsrv/`; the reverb needs no audsrv change at
all, only the second bus does). The path this feature opens is PS2SDK's own
`ps2snd` RPC server over `libsd`, embedded and loaded alongside the existing
modules; the engine wraps it in `Tyra::AudioReverb`
(`vendor/tyra/engine/{inc,src}/audio/audio_reverb.*`). The audio server keeps
talking to `libsd` directly on the sound processor's side and is unaffected —
they are ordinary co-clients of the same driver.

One ordering constraint is load-bearing and commented at both ends: binding that
RPC ends in a driver init that **clears the transfer callbacks the music
streamer installs**, so it happens *before* audio comes up, never after. Doing it
in the other order silences the music with no error anywhere.

## Verifying it

Measured on a scratch fixture (one sound emitter retriggering every 1.5 s, one
`Hall 0.9` zone around it, PCSX2, recorded off the sound card):

| | Tail after each burst |
|---|---|
| Standing inside the zone | 450–500 ms of smooth decay |
| After walking out of it | 50 ms — a hard cut, i.e. below the measurement's resolution |

and, against the same fixture built with the zone removed, the reverb raised the
peak level by 75% and the mean by 50% at `Amount 0.9`. If you are checking a
change here, that decay-tail measurement is the honest instrument: a peak-level
comparison alone cannot tell a reverb from a louder sample.

**Not yet confirmed on real hardware.** PCSX2 emulates the SPU2's reverb, and
everything above was measured there; the sound chip is exactly the kind of
subsystem where an emulator is more forgiving than the console.
