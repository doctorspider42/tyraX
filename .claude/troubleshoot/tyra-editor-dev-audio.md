# tyra-editor-dev — editor audio: Drone Generator, preview output, WAV conversion

From upstream `audio.md`. SPU2 voices/reverb buses live in
`scene-objects-and-gameplay.md` "Areas".

## The Drone Generator output must be an ordinary asset
**Cause/rule:** `dronegen.cpp/.hpp` (~2.6k+470; Tools > Drone Generator,
`docs/drone-generator.md`) is host-only (no GL, no `Project`) and its output is a
16-bit PCM WAV in `res/audio/` + a `.drone` patch sidecar, so nothing downstream
knows the generator exists.
**Fix:** never add a project field for a patch.

## Reference facts (upstream `audio.md`)
- Audition IS the renderer: `Synth::render` is the only DSP; `LiveSynth` wraps it
  for the audio thread; offline `render()` runs the same loop then masters. Params
  determinism: every random stream derives from `seed` through `mix32`.
  `visitParams` is the ONE field list the `.drone` writer/reader walk.
- Seamless looping ADDS the rendered tail over the head (windowed to zero at its
  end, unity then raised cosine; an un-windowed add left an audible tick,
  PROGRESS 212). Reverb = 8-line FDN.
- Timeline: `paramTable()` walks `visitParams` (every saveable scalar automatable
  by byte offset); `Params` carries fixed-capacity `AutoLane`s; `applyAutomation`
  runs once per control block; tempo/format/mastering fields are excluded
  (`autoExcluded`, e.g. `barSeconds` read once per `render()`).
- Window `droneui.cpp` (~1.9k): rotary `knob()`/`knobInt`, arc-envelope editor,
  scope/analyzer/meters. A knob's label IS its ImGui ID (`Mix##delay`/`Mix##reverb`).
  `AutoWriteHook` automates all ~137 parameters without any knob call site knowing.
  Transport: Generate (free-running) and Record (stops; `Rec` arms keyframe
  writing); end-of-piece stop is POLLED on the UI thread. A seek must SETTLE
  (`Synth::setTime`, PROGRESS 216): snap notes to chord/attack, jump LFOs
  analytically, pre-roll 1-3 s; `settle=false` while dragging.
- Rules: every parameter edit ends in `dronePushParams()`; the render runs on
  `droneRenderThread_` and is read only after `droneRenderDone_`; `droneTickRender()`
  writes the WAV + `.drone` and calls `saveAll` (one sanctioned call site);
  `droneHeadSec_` is the ONE playhead; pixel literals go through `scaled()`.
  On close `droneStop()` and cancel+join the render (audio device first).
- Editor audio output `audiopreview.cpp/.hpp` (~160+70): the only audio path; a
  miniaudio (`vendor/miniaudio`, in deps.sh + deps.ps1) playback device pulling
  interleaved stereo floats; backends dlopen'd (WASAPI/ALSA/Pulse/JACK);
  `start()` returns false with no sound card; `stop()` joins via `ma_device_uninit`.
  `audiopreview::EngineLoop` is the vehicle audition.
- WAV conversion `wavconvert.cpp/.hpp` (~280+40): reads the fmt chunk (1 =
  integer PCM, 3 = float; `WAVE_FORMAT_EXTENSIBLE` resolved), converts IN PLACE to
  16-bit PCM. Sound effects: `wavconvert::soundIssue` requires canonical mono
  PCM16/22050; `wavconvert::bakeSounds` runs AFTER texbake and BEFORE either build
  backend, preparing `.res-baked/sfx` without touching `res/sfx`; both encoders
  read cached APCM byte 5 for mono and byte 6 for loop intent; a conversion error
  FAILS the build. Music stays stereo unless its own mono option is chosen.
  Vehicle loop intent `inc/vehicle_sound_loops.gen.txt`; legacy `-loop.wav` works.
