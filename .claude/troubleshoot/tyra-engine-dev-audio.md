# troubleshoot/tyra-engine-dev-audio.md

audsrv playback, ADPCM one-shots, the SPU2 reverb, WAV handling and the vehicle
sound-loop/conversion bake. Symptom first.

## audsrv basics

**Cause:** audsrv streams PCM only; ADPCM is for one-shots (`adpcm.tryPlay`), and an
ADPCM voice cannot be STOPPED — only started, or started over.
**Fix:** the channel budget of a generated game, per bus (every new sound goes to the
CURRENT room's bus): 0-15 for Play Sound, 16-23 for the sound emitters. The auto cycle
runs 16, not 24 — it used to run the full 24 and walked into the emitters' slots, so
one auto play in three stole an emitter's channel or bounced off it as "busy". Who
gets one when they are all busy is `docs/sound.md` (priority, then loudness); the
engine's part of it is `forcePlay` and `endedMask`.

## A sound must play over a busy channel

**Cause:** the refusal was always a software check in `audsrv_ch_play_adpcm`, not a
hardware limit: KON on a sounding voice restarts it.
**Fix:** `AudioAdpcm::forcePlay` uses `AUDSRV_ADPCM_FORCE`, a flag bit ORed into the
channel number, which the fork masks off and uses to skip its own ENDX busy check. The
bit rides in the channel because that number already travels EE -> RPC -> IOP
untouched (no new export, no signature change). **Whether a forced restart CLICKS is a
hardware question and is not settled** — PCSX2 is not a witness worth trusting on the
SPU2. If it clicks, the fixes in order are: drop the victim's volume and play on the
next frame, or KOFF first and let the ADSR release run (both cost a frame of latency).

## How to ask what is still playing

**Cause:** `AudioAdpcm::endedMask(core)` is the SPU2's own ENDX register, one bit per
voice, one IOP RPC.
**Fix:** ask it per PLAY REQUEST, never per frame (a play already costs two or three
RPCs; a per-frame poll is what the emitter loop's whole quantization exists to avoid).

## An audio-caused frame drop shows up in whatever code calls audsrv

**Cause:** a blocking audsrv RPC stops the WHOLE library, not just the caller. audsrv
has one RPC server thread on the IOP and one completion semaphore on the EE
(`audsrv_rpc.c`), so a call that blocks IOP-side holds both, and every other audsrv
call queues behind it. `audsrv_wait_audio(n)` sits in `WaitSema(queue_sema)` until the
ring has `n` bytes free. `AudioSong::work()` used it once per music chunk: **measured
~10 ms per frame in PCSX2 on `examples/showcase` — FRAME 20 -> 30 ms, a solid 25 FPS
for as long as the track's first pass**, while `SCENE`/`PART` never moved.
**Fix:** prefer the non-blocking query plus a sleep to any "wait" entry point —
`audsrv_available()` / `audsrv_queued` over `audsrv_wait_audio` — with
`Threading::sleep(1)` between tries: same condition, same order, lock taken in short
bursts. Four emitters retriggering every frame plus streaming music went from 28-30 ms
to 20.0 ms. Remember the symptom shape: the profiler blames the sound emitters for the
music's lock.

## Streamed music over ps2link stutters

**Cause:** it is the network, not the engine (docs/sound.md, "Music stutters over
ps2link"): the song's host: freads run TCP on the IOP beside audsrv, whose ring (10
feeds of 512 samples, ~107 ms, in INPUT bytes) then gets its refill late. With the song
in RAM the ring stays ~90% full.
**Fix:** do not "fix" it with smaller chunks — more audsrv RPCs on a starved IOP played
in slow motion — and `audsrv_play_audio` TRUNCATES what does not fit. A debug build
warns "Music ring ran low" when a refill finds the ring over 70% empty.

## The second sound of a project was silent on a console

**Cause:** an EE buffer handed to a SIF DMA must be written back to main memory first
(`SifWriteBackDCache(ptr, size)`), and `audsrv_load_adpcm` did not do it. The EE's data
cache is write-back, the DMA reads RAM, so a sample just read with `fread()` reaches
the IOP as *whatever was in that memory before*. **PCSX2 emulates no EE cache, so every
load looks perfect there.** Fixed 2026-08-06 in the vendored fork.
**Fix:** the same rule applies to ANY EE→IOP transfer you add. The tell is cheap:
audsrv reports the sample's header back, so a **nonsense `pitch` for a file whose
earlier load reported a sane one** identifies a corrupt upload in one log line (1881 vs
0x41C00000 was the actual pair).

## "It plays but I hear nothing"

**Cause:** `AudioAdpcm` logs why a sound did not play (debug builds): once per channel
per reason — no sample, channel busy, audsrv error — plus a one-line SPU2 dump
(per-voice volume/ADSR/start address, the core's VMIX masks, MMIX, master and reverb
volume) at each channel's first successful play.
**Fix:** diff a channel that works against one that does not. And
`audsrv_ch_play_adpcm` reports "I do not know this sample" as a POSITIVE
`AUDSRV_ERR_ARGS` (5), which no sign test can tell from a channel number — `tryPlay`
demands that an explicit channel comes back as itself.

## Loud crackle at correct pitch

**Cause:** WAV files: 8-bit PCM is unsigned (0x80 = silence) but audsrv mixes signed.
**Fix:** convert (XOR 0x80) or it wraps at every zero crossing. Mono/low-rate streams
need smaller chunk size + fill threshold or audsrv's ring buffer starves.

## The song player and its ids

**Cause:** the fork adds a WAV-header-aware song player.
**Fix:** `audio_song.cpp` intentionally keeps `rand()` for its ids — a separate namespace,
assigned off-thread — unlike render objects, which must use `generateUniqueId()` (see
`tyra-engine-dev-packages.md`).

## The SPU2's hardware reverb

**Cause:** audsrv exposes playback and nothing else.
**Fix:** the registers come from PS2SDK's **`ps2snd.irx` + `libps2snd`** — an EE-side
RPC client over the `libsd` the engine already embeds (`AudioReverb`,
`audio/audio_reverb.*`, docs/reverb.md). Both are stock PS2SDK (AFL 2.0, unlike audsrv
itself, which is LGPL v2 per every file header), so this cost one `.irx-em`, one loader
call and `-lps2snd` in `Makefile.base`. audsrv keeps talking to libsd directly on the
IOP; the two are ordinary co-clients of one driver.

## The MUSIC went silent while the sfx kept working

**Cause:** `sceSdInit()` clears libsd's transfer callbacks — the ones audsrv's
streaming ring installs. audsrv's own `sceSdInit(COLD)` resets the core attributes.
**Fix:** the reverb's RPC bind runs BEFORE `audsrv_init()` and the effect-enable bit
AFTER it. Get that order wrong and it points the investigation at completely the wrong
subsystem.

## Every voice was sent to the effect bus

**Cause:** libsd's defaults send EVERYTHING to the effect bus, music included:
`VMIXEL`/`VMIXER` come up with all 24 voices set, and `MMIX` bits 4/5 route the core
input (the streamed song) into the reverb.
**Fix:** a per-voice send normally REMOVES a voice, and keeping the music dry is an
explicit write. (Those bit meanings were confirmed against libsd's own block-transfer
handler, which clears bits 6/7 — the dry pair — when a stream ends.)

## A first reverb preset left noise circulating

**Cause:** `sceSdSetEffectAttr` only zeroes the work area if effects were ALREADY
enabled (`effects_disabled && clearram` in libsd's effect.c).
**Fix:** enable, then set.

## Reverb RPCs cost what audsrv's do

**Cause:** synchronous SIF calls sharing the bus with the music stream.
**Fix:** the generated game quantizes the wet depth to 64 steps and sends only real
changes — the discipline `updateSoundEmitters` already follows for volume/pan. A
per-frame RPC is measurable in frame rate. Reverb presets occupy 8-96 KB of SPU2 RAM at
the TOP of the 2 MB while audsrv loads ADPCM samples from 0x5010 upward, so they
collide only past ~1.9 MB of effects; changing a preset zeroes that area, which is why
the game only does it at zero wet level and never per frame.

## The audsrv fork plays voices on BOTH cores

**Cause:** upstream muted core 0's master outright, which made the SECOND reverb unit
unreachable — a reverb is per core and only that core's voices feed it.
**Fix:** channels 0-23 are core 1 (unchanged), 24-47 are core 0. Unmuting is the whole
routing change: core 1's `AVOL` (the core-0-into-core-1 volume) was already pinned at
0x7fff, and `cdrom.c` had always raised core 0's master for CDDA. `AudioReverb` exposes
the two units as `BusA` (core 1) / `BusB` (core 0), and the generated game cross-fades
rooms across them. **A voice is committed to a bus when it starts**, so every play site
must offset its channel by `ScriptContext::reverbBusBase` (0 or 24) or the sound lands
in the room the listener has left.

## audsrv is a SOURCE fork, not a blob

**Cause:** the IOP and EE sources are in-tree at `vendor/tyra/audsrv/` at ps2sdk
`e78a9cb2`, and nothing in the game build compiles audsrv.
**Fix:** change the sources, re-run `vendor/tyra/audsrv/build.sh` / `build.ps1` and
commit the three `bin/` artifacts (`audsrv.irx`, `libaudsrv.a`, `audsrv.h`) in the
same commit. The ps2sdk build TREE it fetches has a mirror fallback,
`PS2SDK_MIRROR` -> `doctorspider42/tyrax-vendor-ps2sdk` (rebuild ability only). The
from-source image compiles the EE half itself and skips the Runner's
overlay; the crossing header is `ee/src/sif-compat.h`. See
`tyra-engine-dev-build.md` for the full two-latency account.

## Vehicle loop encoding

**Cause:** arbitrary project WAVs assigned to idle, high revs or squeal are listed in
optional `inc/vehicle_sound_loops.gen.txt`.
**Fix:** `tools/toolchain/native-build.sh` (Windows bridges through
`native-build.ps1`) and the Docker fragment in Runner encode these with `-L`, plus
legacy `*-loop.wav`. Header byte
6 must match desired mode in both directions; mtime alone misses assigning or removing
a continuous role. Loop flags affect every use of a sample, so use a separate file for a
one-shot version. Source seam preparation remains authored.

## Sound effect conversion (1.157.0)

**Cause:** `wavconvert::soundIssue` requires canonical mono PCM16/22050; import and
Project > Sounds > Convert downmix too.
**Fix:** Runner calls `wavconvert::bakeSounds` AFTER texbake and BEFORE either build
backend, preparing `.res-baked/sfx` without modifying `res/sfx`. Both encoders read
this mirror, check cached APCM byte 5 for mono as well as byte 6 for loop intent, and
preserve the source path for role matching. A conversion error must fail the build. Test
stereo PCM16, PCM24, float, extended headers and an odd metadata chunk; verify duration,
source hashes, incremental mtime stability and ADPCM mono/loop headers in BOTH
backends. Decode the actual ADPCM and compare the waveform to the converted WAV: header
checks alone missed adpenc's corrupt stereo reader (`fread(wave+i, 2, ...)` advances by
one byte). Music conversion stays stereo unless its own mono option is chosen.
