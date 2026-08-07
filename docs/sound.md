# Sound: voices, priority and who gets cut off

The PlayStation 2 plays a fixed number of sounds at once. Not "a lot" — a
number, and a small one. This page is what that number is, who spends it, and
what happens when a game asks for one more sound than the chip has left.

For the reverb those sounds are heard through, see [reverb.md](reverb.md).

## The budget

The SPU2 has 48 ADPCM voices, 24 per core. A core is also a **reverb bus**
(reverb.md), and a voice can only be heard through the reverb of its own core —
so every sound a game starts goes on the bus the room the listener is standing
in is using. That makes the number that matters **24, not 48**:

| channels | who owns them |
|---|---|
| 0–15 | **Play Sound** flow nodes |
| 16–23 | **sound emitters** (the Sound object in a scene) |

The other core's 24 voices are not spare capacity — they are the *other* room,
carrying whatever was still sounding when the player walked out of it.

Sixteen and eight are generous for a PS2 game and stingy for a bad frame: a
firefight in a room with a dozen ambiences will hit the wall. What follows is
what happens when it does.

## Sound emitters: the eight loudest win

Eight channels, and a scene may hold any number of emitters. Every frame the
game works out how loud each audible emitter should be — it already does, for
the volume — ranks them, and gives the eight channels to the winners:

1. **higher priority wins**, then
2. **louder wins**.

An emitter that loses its channel goes silent until it wins one back. Nothing
fades: an ADPCM voice cannot be stopped, so whatever it had already started
plays out and it simply does not retrigger.

**Priority** is the *Priority* field on the Sound object (Properties). 0 is
ordinary ambience. Raise it for the one sound a scene cannot afford to drop —
an alarm, a boss's loop, a hint the player is waiting on — and it keeps its
voice even while quieter than the water feature next to the player.

Two details worth knowing, because they are audible:

- **A challenger has to be clearly louder to take a channel** (10 volume units,
  where the volumes are quantized to 5). Without that margin two ambiences of
  near-equal loudness trade the channel every frame, and since taking a channel
  retriggers the sound, that is a stutter rather than a mix.
- **An emitter with no channel does not count down its interval either.** Walk
  up to a 10-second emitter and it starts its countdown when it becomes
  audible, instead of firing the moment it wins a voice with a timer it spent
  the last minute running down out of earshot.

Until 2026-08 this was not a ranking at all: an emitter's channel was a hash of
its position in the scene (`16 + (i & 7)`), so the ninth audible emitter was
starved by **scene order**. An emitter at the player's feet could be silent
because an unrelated object eight indices away held its channel, and nothing in
the editor said so.

## Play Sound: pinned, auto, and priority

The *Channel* parameter is the choice between two behaviours:

- **Pinned** (0–15) — this sound owns that voice. A new trigger **cuts off**
  the previous one. Pin a footstep, a UI beep, a weapon: the sound that must
  never layer over itself.
- **Auto** — the game finds a voice. It takes one that has finished if there is
  one; otherwise it cuts off the sound with the **lowest priority strictly
  below** the incoming one; otherwise the new sound is **dropped**.

That last case is the feature working, not a failure — it is what a priority is
for — so it is not reported as an error anywhere.

*Priority* is the node's fourth parameter, same convention as the emitter's: 0
is ordinary, raise it for a gunshot or a line of dialogue, lower it for chatter
you would rather lose. Sounds of **equal** priority never steal from each other,
which is what keeps a bank of ordinary effects behaving exactly as it did before
priorities existed.

Pinning writes the sound's priority onto that channel too, so an auto play
cannot steal a voice out from under a pinned one.

**"Cuts off" is a restart, not a fade.** The voice is keyed again while it is
still sounding. Whether that is audible as a click depends on where in the
waveform the old sample was — a transient masks it, a sustained quiet sample
may not — and it is a property of the hardware, not of the code: measure it in
your own game rather than assuming either answer.

## What it costs

The ranking is plain arithmetic over the scene's emitters — no calls into the
sound chip — and runs once per frame. The channel choice for a Play Sound costs
**one extra IOP call per trigger**: the game asks the SPU2 which voices have
finished (its `ENDX` register) rather than guessing. That is per *trigger*,
never per frame; a play already costs two or three such calls.

## When something is not heard

| Symptom | Cause |
|---|---|
| An emitter near the player is silent while distant ones play | It lost the ranking — check *Priority* on the ones that are playing. |
| An emitter fires the instant you walk into range | That is the intended reset of its interval; shorten the interval if you want it sooner. |
| A Play Sound node does nothing, sometimes | Its priority is not above anything currently playing, so it is being dropped. Raise it, or pin it a channel. |
| A pinned sound stopped layering over itself | That is the fix — pinning cuts off, as the parameter always claimed. Use auto if you want copies to overlap. |
| Nothing plays at all, on hardware, but PCSX2 is fine | Not this page. See the EE cache write-back in the `tyra-engine-dev` skill's audio section. |

A debug build says why a sound did not play, once per channel per reason, in
the game's log (`bin/log.txt`, or the `[ps2]` lines over ps2link — see
[ps2link-setup.md](ps2link-setup.md)): no sample, channel busy, an audsrv
error. A **priority drop is deliberately not among them.**
