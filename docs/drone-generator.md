# Drone Generator

*Tools > Drone Generator* is an ambient / drone music generator built into the
editor. You dial a piece in against **live playback** — the knobs are heard as you
turn them — and **Render** writes `res/audio/<name>.wav` plus a re-editable
`<name>.drone` patch, adding the track to the project's Music list. Play it from
a flow graph with **On Start → Play Music** (Loop on).

The scope is deliberately narrow, the same way the [Tree
Generator](tree-generator.md) is: **not** a sequencer, not a tracker, and not a
runtime synthesizer. It is the tool for the job "this level needs a background
bed and I am not going to compose one in a DAW". You will not write Bach with
it. You can absolutely craft a piece you are happy to loop for twenty minutes
under a level.

## What it costs on the PS2: nothing new

The generator is a **host-only module** (`src/dronegen.cpp` — the
`treegen`/`matbake`/`stochtile` pattern: no GL, no `Project` dependency, pure
DSP over a `Params` struct). Its output is an ordinary asset: a 16-bit PCM WAV,
which is exactly what `Tyra::AudioSong` already streams. No new object type, no
serialization field, no codegen, no engine work — a generated track is
indistinguishable from one you bounced in Reaper.

That is also why the default render format is **22050 Hz stereo 16-bit**: it is
what `AudioSong::load` documents. The Music list's per-track *PS2 build* options
(rate / mono) still apply on top for a network deploy, see the tooltip there.

## The signal chain

```
 4 oscillator layers ─┐
 (chord voices,       │
  unison, drift)      ├─> drive ─> filter ─┐
 air bed (noise) ─────┘                    ├─> chorus ─> tape ─> delay ─> reverb ─┐
 bells ────────────────────────────────────┘   (+ their own delay/reverb sends)   │
                                                                                  ▼
                  master level <─ limiter <─ width / mono-bass <─ tilt / low+high cut
```

Two routing decisions are worth knowing because they are audible:

- **Bells bypass the master filter.** They are the one bright element in an
  otherwise dark chain, and a low-passed bell is a thud. They enter after the
  filter and carry their own *To delay* / *To reverb* sends.
- **The bass is summed to mono** below *Mono under* (140 Hz by default). A wide
  low end collapses badly through a TV, which is where this music will be heard.

### Layers

Four independent oscillator stacks. Each holds up to six voices — one per chord
degree — and each voice can be a **unison** stack of up to five detuned copies
fanned across the stereo field. `Plays:` picks which chord degrees a layer takes,
which is the difference between a sub (root only), a pad (all six) and a top
voice (the upper three).

Waves: Sine, Triangle, Saw, Square, Pulse, FM (2-operator) and Organ (a drawbar
partial set). Saw, Square and Pulse are **PolyBLEP** band-limited: at 22 kHz a
naive saw folds a full spectrum of aliasing back down into the audible range,
and a drone is precisely the signal that gives you all day to hear it. Organ
drops partials past Nyquist instead of folding them.

*Drift* is the quiet hero: a slow, per-voice random detuning of a few cents. It
is what keeps a chord held for thirty seconds from sounding frozen.

### Harmony

A **chord progression** of up to eight steps, each spelled as semitone offsets
from the root and held for a number of bars. Chord changes **glide** (portamento)
rather than jump — that is the drone idiom, and the *Glide* knob is how syrupy it
gets. Voices that a chord does not use fade out over the layer's *Release*; new
ones fade in over its *Attack*.

The *Scale* setting is only used by the bells; chords are spelled note by note,
because "minor" is not a useful abstraction once you want a suspended fourth in
bar nine.

### Motion

Three LFOs (sine / triangle / ramp / square / sample-and-hold / smooth random)
plus an **Arc** — a five-point envelope over the whole piece — and a **6-row
modulation matrix** routing any source at any destination (cutoff, resonance,
pitch, per-layer level, air level, bell density, chorus depth, delay mix, reverb
mix, shimmer, drive, width). This is where a static patch becomes a piece: put
the Arc on cutoff and bell density and the track opens up in the middle and
closes at the end.

An LFO set to **per bar** takes its rate in cycles per bar instead of Hz, so its
motion lines up with the progression — and with the loop point.

### Space

- **Reverb** is an 8-line **feedback delay network** with a normalized Hadamard
  mixing matrix, per-line damping, a low cut inside the loop, modulated line
  lengths, an input diffuser and a predelay. An FDN rather than the usual comb
  bank because the whole point here is a 30-second tail: comb filters ring
  metallic long before that, an orthogonal matrix stays smooth. *Decay* is an
  RT60 in seconds and goes to 40.
- **Shimmer** feeds a pitch-shifted copy of the tail back into the reverb — the
  sound everyone actually means by "ambient reverb". The shifter is a cheap
  two-window granular one, which is fine *only* inside a feedback path where the
  tail masks the window seams; it is never used on a dry signal.
- **Delay** is ping-pongable with a damped feedback loop and a musical time
  division (1/4 … 2 bars) or a free time in seconds.
- **Chorus** is a three-tap ensemble; **tape** adds wow, flutter and hiss.

## Seamless loops

Background music plays with `song.inLoop = true`, so a seam is audible forever.
*Seamless loop* (on by default) renders **past the end of the piece** and adds
that tail over the beginning of the file.

That is not a crossfade, and the difference matters: in a looping player you
literally hear the previous pass's reverb tail *at the same time* as the new
pass's opening, so **adding** the two is the physically correct wrap. The piece
keeps its own opening, and because the synth starts from silence (voices ramp in
over their attack), the sum is continuous. Measured on the *Deep Space Hum*
preset: RMS in the last second before the wrap 0.1545 vs 0.1530 at the start of
the file — inaudible. With the option off it is 0.1688 vs 0.0351, which is the
thud you have heard in a hundred games.

**The fold is windowed**, and that part is not optional. Adding the tail and
simply stopping where the window ends leaves a step discontinuity *inside* the
file - not at the seam, which is continuous by construction, but one *Loop tail*
into it, where the wrapped tail was cut off mid-decay. It is an audible tick, and
it was measured: a sample-to-sample jump of up to **0.37** against a 0.11 p99
neighbour jump on the same second of audio. So the fold applies unity for the
first half of the window and then a **raised cosine to zero** - zero value *and*
zero slope at the splice, so neither the sample nor its slope jumps. After the
fix the worst jump around the splice is **below each preset's own p99** neighbour
jump (0.026 vs 0.042 on *Init Drone*, i.e. 13x smaller than before) and no longer
sits at the boundary at all.

Unity-then-fade rather than a full-window Hann on purpose: a Hann would attenuate
the *loudest* part of the wrapped tail by 6 dB, which is audible as the wrap
losing its room.

Three things help further:

- *Harmony > Fit length to whole passes* rounds the length to a whole number of
  progression passes, so the loop never lands mid-chord.
- LFOs set to **per bar** return to the same phase at the loop point; free-Hz
  LFOs generally do not.
- **Keep *Loop tail* near the reverb decay.** The window removes the click at any
  length, but a 6-second tail under an 18-second reverb still fades out while the
  room is ringing - smooth, yet the wrap reads as losing its space. The Master tab
  says so when the tail is under 60% of the decay and offers **Match decay**.

## The timeline

*Timeline* tab. A drone that never changes is a texture, not a piece, so any
parameter can be given a **lane** of keyframes and the value follows them over
the length of the track.

The intended way to make one is not to add a lane by hand:

1. Switch the transport to **Record** mode.
2. Hit **Rec**. Playback starts at the playhead with keyframe writing armed.
3. Turn a knob. A lane appears for that parameter with a keyframe at the
   playhead, and the amber dot on the knob says it is automated from now on.

(The *Write keyframes* checkbox in the Timeline tab is the same arm, kept for
writing at a parked playhead: scrub to a moment, arm, turn a knob.)

That works for **every knob in the tool** without any of them knowing the
timeline exists: a knob binds straight to a field of the patch, so the pointer it
was handed *is* that parameter's address, which is all a lane needs (see
`AutoWriteHook` in droneui.cpp). The parameter list itself comes from the same
field walk the `.drone` writer uses, so a parameter is automatable the moment it
is saveable — **137 of them** today.

A knob dragged for a few seconds does not leave a smear: a keyframe within
0.35 s is **moved** rather than added, and once a lane hits its 24-point budget
the nearest point always wins, so a long take cannot overflow it. Lanes are
capped at 12 — `Params` is copied into the audio thread on every edit, so it
holds no heap memory and the budgets are fixed.

**Editing a lane**: drag a point (time and value at once), double-click an empty
spot to add one, right-click a point to delete it. The lane header shows the
value at the playhead, and the lane's vertical range auto-fits its own points.

**Read vs Write**, as in a DAW: with Write off, an automated knob *displays* the
value the timeline is playing and moving it does not stick — the next frame puts
the automated value back. Arm Write and the same gesture records a keyframe.

Automation sets the parameter's **base value**; the LFOs, the arc and the mod
matrix still apply on top of it. So a lane can walk the filter from 300 Hz to
5 kHz across the piece while LFO 1 keeps wobbling it — they compose rather than
fight.

Excluded on purpose: the file format (sample rate, length, stereo), the
mastering block (it applies once, after the render), the enums, the chord table,
and **tempo** — `barSeconds` is read once per `render()` call, so a tempo lane
would look like it worked and quietly do nothing.

### The transport bar

The strip under the preset row is the position bar: ticks (bars while they are
readable, otherwise round seconds), the elapsed fill, a **triangle for every
keyframe in the piece** so you can see where it changes, the playhead, and a
`0:27.0 / 1:00` readout. Click or drag it to move the playhead — it **seeks the
live playback** (`Synth::setTime`), so the timeline drives playback rather than
just reporting it. A seek keeps the delay and reverb tails (it is a jump in the
piece, not a reset) but re-latches held notes at the chord you landed on instead
of gliding in from the one you left.

Rendering applies the same lanes through the same code, so what you hear while
scrubbing is what lands in the WAV.

## Determinism

Everything is a pure function of `Params`. `seed` drives every random stream
(voice drift, noise, bell placement, dither), each derived through a mixer from
the patch seed, so:

- Re-rendering a patch produces a **byte-identical** WAV, automation included -
  a recorded knob move is data in the patch, not a performance you captured. A
  track is a file you can regenerate, not a take you have to keep.
- **Roll** re-seeds: same patch, different bell placement and drift. Changing a
  knob adjusts the piece instead of reshuffling it.
- The `.drone` sidecar is therefore a complete description of the audio. The
  harness check for this compares audio rendered from a patch against audio
  rendered from that patch's saved text.

## The transport: Generate and Record

The same synth is used two ways, so the transport has two modes:

- **Generate** — free-running sound design. *Play* keeps playing until you stop
  it and ignores the piece's length; nothing is written. This is the mode for
  dialling a patch in. *Restart* rewinds to bar 1 and re-seeds the random
  streams.
- **Record** — bound to the timeline. *Play* starts **at the playhead and stops
  at the end of the piece** instead of droning on; *Rec* does the same with
  keyframe writing armed; `|<` rewinds. Stopping parks the playhead where
  playback got to, so *Play* resumes rather than restarting.

The end-of-piece stop is polled on the UI thread, not fired from the audio
callback — a device must never be torn down from inside its own callback.


### The audio hand-off

*Play* opens the sound card (miniaudio: WASAPI on Windows,
ALSA/PulseAudio/JACK on Linux, all loaded at run time — no new system package)
and plays the patch live. The parameter hand-off is the important part:

- The UI thread pushes a parameter copy; the **audio thread picks it up under a
  `try_lock`**, never a blocking lock. A UI stalled inside a file dialog cannot
  starve the device — it just keeps playing the previous values for another
  block.
- The meters and the scope go the other way through plain atomics. A visualizer
  may race; a device may not.
- **What you hear is the same synthesizer that renders the file** — offline
  rendering runs the identical block loop, then adds the mastering (loop fold,
  fades, normalization). So a knob cannot sound different in the file than it did
  in the preview.

A machine with no audio device is an expected state, not an error: the transport
reports it and the tool keeps working as an offline renderer.

## A patch is a project asset

A `.drone` is not just a sidecar of a rendered WAV, it is the **audio project**:
the thing you twiddle and save. The generator's bottom bar is a document bar -
**New / Open... / Save**, the open patch's path, and a `*` while it has edits the
file does not.

- **Save** writes `res/audio/<name>.drone` (or back over the patch you opened).
  Saving over your own open patch is what "save" means and happens silently;
  saving onto a *different* existing patch asks first.
- **Open...** lists every `.drone` in the project, so a patch is picked by name
  rather than hunted for in a file dialog (the dialog is still there under
  *Browse for a file...* for patches from outside the project).
- **Render WAV** writes the track *and* saves the patch beside it, and the pair
  becomes the open document.

The Asset Browser treats it as its own kind, **"audio project"**: it counts under
the *Audio* filter, has its own tile colour, and its inspector reads the patch and
describes the piece - length, rate, how many layers are on, how many automation
lanes, whether it has been rendered yet - with an **Open in Drone Generator**
button. Double-clicking either the patch or its rendered track opens it. In
*Project > Music*, a track that has a patch next to it grows an **Edit** button.

It never ships: `texbake` keeps `.drone` out of the disc image, like the other
editor-only sidecars.

### Overwriting is a question, not a surprise

*Render WAV* refuses to touch an existing file until you say so - it names the
track, says that the patch beside it goes too, and offers Replace or Cancel. That
guard exists because a render writes **two** files under a name typed into a text
box: an accidental collision would take someone's patch with it, and a patch is
the only copy of the work (the WAV can always be re-rendered from it, never the
other way round).

## The .drone patch

Rendering writes `res/audio/<name>.drone` next to the WAV: `key = value` text,
one parameter per line, in the same house format as `.flownode` and `.screenfx`
files — diff-friendly, hand-editable, with unknown keys ignored and missing keys
left at their default (so an older file still loads). Every value a hand edit
could get wrong is clamped on load.

Both directions walk **one field list** (`visitParams` in dronegen.cpp), the same
single-source trick the project's section writers use: a field added there is
saved and loaded, with no second list to forget. That same walk builds
`paramTable()`, the automation list — so a new parameter is saveable and
automatable in one move.

Automation lanes are one line each, keyed by the parameter's name rather than by
its position in the table, so a lane survives the field list growing:

```
# automation
auto.filter.cutoff = 0:300, 27:5200, 60:900
auto.reverb.mix = 0:0.25, 42:0.75
```

An `auto.<key>` whose parameter no longer exists is dropped on load, like any
unknown key.

The sidecar is what makes a shipped track re-editable, so it travels with its
asset: `App::assetSidecars` lists it, which means the Asset Browser's
rename/move/delete carry or remove it with the WAV. **Double-clicking a track (or
the patch itself) in the Asset Browser reopens it in the generator.**

## Presets

Ten, meant as starting points rather than finished cues: *Init Drone*, *Deep
Space Hum*, *Cathedral Pad*, *Glacial Shimmer*, *Rust Wind*, *Dark Cave*,
*Underwater*, *Ritual Bells*, *Machine Room*, *Neon Dusk*. Loading one keeps
your **length and sample rate** — a preset is a sound, not a duration.

## Disc cost

A 60-second 22050 Hz stereo track is **2.5 MB** on the disc; the Master tab
shows the figure for the current settings and warns past ~24 MB. Ambient music
is the one genre where a short seamless loop is indistinguishable from a long
piece, so prefer 60–120 s and loop it. Mono halves it again and, for a bed this
wide, costs less than you would think (the low end is mono anyway).

## Where the code lives

| File | What |
|---|---|
| `src/dronegen.hpp/.cpp` | `Params`, the `Synth` (live, block-based), `LiveSynth` (thread hand-off), the offline `render()` + mastering, the WAV writer, the presets and the `.drone` text format. Host-only, no GL, no `Project`. |
| `src/audiopreview.hpp/.cpp` | The editor's only audio *output* path: a miniaudio playback device pulling from a callback. The only TU that includes miniaudio. |
| `src/droneui.cpp` | The window: the rotary knob widget, the arc editor, the chord table, the mod matrix, the scope/analyzer/meters, the timeline (position bar + lane editors + the write hook), and the render + patch I/O buttons. `App::` methods in their own TU, the `assetbrowser.cpp` precedent. |

Everything DSP is harness-testable without the GUI — link `dronegen.cpp` alone
and render presets to WAV (see the PROGRESS entry for the checks that harness
runs: determinism, `.drone` round trip, loop seam, DC, peak/RMS windows,
spectrum).
