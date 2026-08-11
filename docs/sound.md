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

### Emitters and streaming music no longer fight over audsrv

An emitter's *Interval* is how often it tries to start its sample again.
**Interval 0 means every frame** — that is what makes a loop seamless (the
retry is skipped while the voice is still busy, so the sample restarts on the
first frame after it ends), and it is also one audsrv call per frame for as
long as the emitter is audible. Playing music at the same time used to make
those calls ruinously expensive, and the fix is worth knowing about because the
symptom pointed nowhere near the cause.

audsrv has **one RPC server thread on the IOP and one completion semaphore on
the EE**, so every call in the program is serialized — including the music
stream's, which runs on the engine's audio thread. The song used to hand the
ring `audsrv_wait_audio()`, whose handler *blocks* until there is room; for as
long as that wait ran, nobody else could reach audsrv. An emitter asking for a
volume change or retriggering its sample paid the remainder of that wait.
Measured on `examples/showcase` in PCSX2 with the frame profiler (EE ms,
averaged over ~1 s):

| | whole frame | sound step |
|---|---|---|
| 1 emitter, interval 0, music streaming — **before** | 27–31 ms (25 FPS) | 9–10 ms |
| 4 emitters, interval 0, music streaming — **before** | 28–30 ms (25 FPS) | 7.7–11.8 ms |
| 4 emitters, interval 0, music streaming — **after** | 19.9–20.4 ms (50 FPS) | 0.25–0.54 ms |

Nothing else moved in any of those runs — scene, particles and scripts sat at
5.7 / 0.12 / 0.3 ms throughout, which is what made the profiler blame the sound
step for what was really the music holding a lock. The engine now polls
`audsrv_available()` instead of blocking (`AudioSong::work`, see the
`tyra-engine-dev` skill), so the lock is taken in short bursts and the game
thread slips in between.

What is left is ordinary: an audible emitter costs one IOP call per *interval*,
and interval 0 means per frame. That is affordable now — four of them measure
0.25 ms a frame — but it is still the most expensive way to hold a loop
together, so an ambience that does not need frame-exact restarting is better
authored with an interval **under its sample length** (a quarter second
restarts the loop inaudibly), and *Range* is worth setting to where the sound
is meant to be heard, because an emitter outside it costs nothing at all.

## When something is not heard

| Symptom | Cause |
|---|---|
| An emitter near the player is silent while distant ones play | It lost the ranking — check *Priority* on the ones that are playing. |
| An emitter fires the instant you walk into range | That is the intended reset of its interval; shorten the interval if you want it sooner. |
| The scene runs at 25 FPS while music plays and an emitter is in earshot | Fixed 2026-08 — the music stream no longer blocks audsrv for everyone else. If you see it again, you are on an engine build from before that; see *Emitters and streaming music* above. |
| A Play Sound node does nothing, sometimes | Its priority is not above anything currently playing, so it is being dropped. Raise it, or pin it a channel. |
| A pinned sound stopped layering over itself | That is the fix — pinning cuts off, as the parameter always claimed. Use auto if you want copies to overlap. |
| Nothing plays at all, on hardware, but PCSX2 is fine | Not this page. See the EE cache write-back in the `tyra-engine-dev` skill's audio section. |

A debug build says why a sound did not play, once per channel per reason, in
the game's log (`bin/log.txt`, or the `[ps2]` lines over ps2link — see
[ps2link-setup.md](ps2link-setup.md)): no sample, channel busy, an audsrv
error. A **priority drop is deliberately not among them.**
