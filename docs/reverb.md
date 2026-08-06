# Reverb (rooms for the sound effects)

Draw a box, tick one checkbox, and every sound effect the player hears while
standing inside it plays through a **real reverb** — a cave, a hall, a stairwell.
It costs the EE nothing: the PlayStation 2's sound chip has a hardware reverb
unit and this feature is the wiring that reaches it.

Reverb is authored on an [Area](areas.md), the same invisible volume that
already serves streaming zones, catch lists and the *In Area* trigger. Select
one, tick **This area is a room for the sound effects**, pick a preset and a
strength.

**There is a demo:** [`examples/reverb-rooms`](../examples/reverb-rooms) — four
rooms, four acoustics, one knock, and a button that plays that same knock dry
so you can hear the difference in place.

## What the hardware actually gives you

Everything below follows from one fact: the SPU2 has **one reverb unit per
core**, and it has two cores. So:

| | |
|---|---|
| **Two rooms at a time** | One per core. The listener's room owns a bus; crossing into another room hands it the free one and the two cross-fade. A *third* room entered mid-fade waits for the first to finish leaving. |
| **Which room a sound is in is decided when it STARTS** | A bus is reachable only by the voices on its core, so a sound already playing finishes in the room it started in. That is not a compromise — it is what a real room does to a sound you carry out of it. |
| **Zones themselves do not mix** | The listener is in exactly one room: the highest *Priority* zone containing them. The cross-fade is between the room you left and the room you entered, not between two rooms you are in. |
| **The send is per voice, but only on/off** | Any single emitter can opt out of the reverb entirely, but there is no per-emitter "30% wet" — only the room's own strength. |
| **Music stays dry** | Deliberately, and it takes an explicit register write to keep it that way: the sound driver's own defaults send everything into the reverb, music included. And the music is a core 1 input, so it could only ever reach one of the two units anyway. |

Nothing here is a TyraX limitation to be lifted later by better code — it is the
shape of the chip.

Getting at the second unit did take work, and it is worth knowing where it
lives: upstream audsrv used one core and left the other muted, so the
[audsrv fork](../vendor/tyra/audsrv/README.md) plays voices on both (channels
0-23 = core 1, 24-47 = core 0) and `Tyra::AudioReverb` drives the two units as
`BusA` / `BusB`. The generated game keeps a **bus base** on its script context:
every new sound — emitters and Play Sound alike, pinned channels included — is
played at that offset, which is how a sound ends up in the room the listener is
standing in.

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
being its maximum. The game ramps it over ~0.3 s whenever the target changes,
so walking through a doorway is a fade rather than a step.

**Every transition cross-fades, whatever presets the two zones use.** Two zones
sharing a preset ramp on the one bus; two zones with different presets hand the
incoming room the *other* unit, load it there while it is still silent, and then
ramp the two depths past each other. There is no dry gap in the middle and no
authoring rule to remember — a cave mouth at `Hall 0.35` opening into a cave at
`Hall 0.90` and a hall opening into a `Pipe` corridor both simply fade.

Two things follow from there being exactly two units:

- **A third room entered mid-fade waits.** The incoming room can only take a
  bus that has gone silent, so sprinting through three differently-presetted
  rooms in under half a second delays the third. It waits rather than glitching.
- **A sound is in the room it STARTED in.** New sounds go to the incoming
  room's bus; anything already ringing finishes on the outgoing one.

Outside every zone the reverb is off — and so is a zone whose preset is *Off*,
which is how you cut a dry pocket. Neither swaps buses: they just ramp the
current room down, so stepping in and out of a doorway costs nothing. There is
no scene-wide default either: a level that should be reverberant everywhere gets
one big box, which keeps the cost where an author can see it.

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

Because the send is one bit per voice and the emitters share eight channels,
the emitter that most recently triggered a channel owns that channel's setting.
In practice this is invisible; it only matters if you mix reverb-on and
reverb-off emitters that fire in lockstep.

**Which eight emitters those are is a ranking**, and there is a page about it:
[sound.md](sound.md) — the channel budget of a whole game, how the emitters and
the Play Sound nodes divide it, and what priority does when a scene asks for
more sounds than the chip can carry.

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
synchronous RPC to the sound processor per bus. A player standing still in a
room costs zero of those, and a cross-fade costs two for as long as it runs.
The mixing itself is done by the sound chip and never touches the EE.

Each bus's strength is quantized to 64 steps before it is sent, for exactly
this reason: the audio RPCs share one lock with the music stream, and an RPC
per frame measurably costs frame rate.

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
