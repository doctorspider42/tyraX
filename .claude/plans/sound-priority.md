# Sound priority and voice stealing

A brief for a session **with a real PS2 attached**. Two of the three steps
below can only be finished honestly on hardware — the open question is whether
restarting a voice mid-sample clicks, and PCSX2 is not a witness worth trusting
on the SPU2.

Written after a console session hit `ADPCM not played on channel 2: busy` by
spamming a pinned sound, and asked whether sounds could carry a priority.

## Where things stand

The channel budget of a generated game, **per reverb bus** (docs/reverb.md — a
bus is one SPU2 core, and every new sound is played on the bus the current room
runs on, so this is what is available at once):

| channels | owner | allocation today |
|---|---|---|
| 0–15 | Play Sound flow nodes | round-robin, `sfxNextCh = (sfxNextCh + 1) % 16` |
| 16–23 | sound emitters | `16 + (i & 7)`, `i` = the object's index in the scene |

Both allocations are unaware of what is playing:

- **Play Sound** takes the next slot in the cycle whatever is on it. audsrv
  refuses a busy channel (`-AUDSRV_ERR_NO_MORE_CHANNELS`), so the new sound is
  simply dropped. The sound that loses is the one that arrived last — not the
  least important one.
- **Emitters** hash the object index to one of 8 slots. **Nine audible emitters
  means two share a slot, and the winner is decided by scene order** — not by
  distance, loudness or anything an author can see. An emitter standing next to
  the player can be silent because an object eight indices away holds the slot.
  This is the more damaging of the two and nothing in the editor hints at it.

Relevant code:

- `src/templates.cpp`, `TerrainGame::updateSoundEmitters()` — the emitter loop,
  `const int chIdx = i & 7;` and the `sndChVol` / `sndChPan` / `sndChBus[8]`
  caches next to it.
- `src/templates.cpp`, the `PlaySound` branch of `flowGraphScript` — emits the
  round-robin and the pinned-channel path.
- `vendor/tyra/audsrv/iop/src/adpcm.c`, `audsrv_ch_play_adpcm()` — the busy
  check that refuses an explicit channel.
- `vendor/tyra/engine/src/audio/audio_adpcm.cpp` — `tryPlay` and the
  once-per-channel failure logging.

## Step 1 — emitters pick slots by loudness (no fork change, no click risk)

The one that fixes a real bug and needs nothing from the hardware.

Replace the `i & 7` hash with a per-frame allocation: rank the audible emitters
by the volume the loop already computes and give the 8 slots to the loudest.
Everything else about the loop stays.

**The trap to design around:** naive re-ranking every frame makes two emitters
of near-equal volume swap slots repeatedly, which retriggers both and sounds
like stuttering. Two ways out, pick one and say why in the commit:

- a challenger must beat the holder by a margin (hysteresis, e.g. 10 volume
  units — the loop already quantizes volume to steps of 5), **or**
- a slot is only reassigned once its voice has ended (see `endedMask` in step 2
  — but that makes step 1 depend on step 2, so prefer the margin).

Also keep: a slot changing owner must invalidate that slot's `sndChVol` /
`sndChPan` cache, exactly like the bus switch already does through `sndChBus`.

**Verification:** ten emitters in a scene, all in range, at different distances;
walk past them and confirm the nearest is always audible. `--pad` plus a
recording off the sound card is enough — no console needed, though a console
run is welcome.

## Step 2 — forced play in the audsrv fork

Needed for any kind of stealing, and it also makes an existing promise true:
Play Sound's *Channel* parameter is documented as pinning a channel, and until
recently the tooltip claimed a new trigger CUTS OFF the previous one. It does
not — the play is skipped. This is what makes that true.

**Do it with a flag bit in the channel number, not a new export.** The channel
travels EE → RPC → IOP untouched (`audsrv_ch_play_adpcm` on the EE side is
`call_rpc_2(AUDSRV_PLAY_ADPCM, ch, id)`, and `rpc_server.c` dispatches
`audsrv_ch_play_adpcm(data[0], data[1])`), so:

```c
#define AUDSRV_ADPCM_FORCE 0x40   /* in both audsrv.h headers */
```

and in `iop/src/adpcm.c`, mask it off at the top of `audsrv_ch_play_adpcm` and
skip the `ENDX` busy check when it is set. No new export in `exports.tab`, no
new RPC id, no signature change on either side — which matters, because adding
an IRX export means touching the import lists of everything that links it.

`KON` on a voice that is already playing restarts it; the refusal is a software
check in audsrv, not a hardware limit. **Whether it also clicks is the open
question** (see below).

Then rebuild the module and commit the artifacts together with the sources:

```bash
./vendor/tyra/audsrv/build.sh
```

Engine side: an `AudioAdpcm::forcePlay(sample, ch)` next to `tryPlay`, and the
Play Sound codegen uses it when the node pins a channel. Fix the node tip in
`src/flowgraph.hpp` back to describing a cut-off once it is one.

## Step 3 — priorities, and stealing the lowest

**Model.** A `Priority` number, higher wins, same convention as a reverb zone's:

- `FlowNodeType` `PlaySound` gains a fourth num param (it has 3 today: Volume,
  Channel, Dry). Give it a `.numTips` entry.
- `SceneObject::soundPriority` for emitters, next to `soundReverb` — with the
  full chain that implies: `operator==`, `objectJson`/`parseObject`,
  `liveLinkRecipeHash`, the emitter block in `props_ui.cpp`, the
  `SceneObjectData` POD and its row emitter in `templates.cpp`. See the "a
  feature touches the whole chain" section of the `tyra-editor-dev` skill.
- Bump `kFormatVersion` and the editor semver (`src/version.hpp`).

**Runtime.** In the generated game, a table over the 16 flow-graph channels:
the priority of whatever is playing there. On a play request:

1. a channel whose voice has **ended** → take it;
2. else the lowest priority **strictly below** the incoming one → force-play
   over it (step 2);
3. else drop the sound — and do **not** log that as an error. It is the
   designed outcome, not a failure. (The once-per-channel diagnostic in
   `audio_adpcm.cpp` should stay for genuine errors; a priority drop should not
   reach it.)

"Has ended" comes from the SPU2 itself: `sceSdGetSwitch(core | SD_SWITCH_ENDX)`
returns 24 bits, one per voice. The game already links `libps2snd` for the
reverb, so this is available — wrap it as `AudioAdpcm::endedMask(core)`. It
costs one IOP RPC, so read it **only when the bank is contended**, never per
frame (the emitter loop's volume/pan writes are quantized for exactly this
reason — see the `updateSoundEmitters` comment).

Note the table is per bus. A room change moves new sounds to the other core;
the outgoing bus's voices are finishing there and should not be stolen from —
in practice the incoming bus's table starts empty, which is the correct
behaviour and needs no extra code, only a comment saying so.

## The hardware questions

These are why this brief wants a console:

1. **Does a forced `KON` over a playing voice click?** If it does, try in order:
   drop the victim's volume to 0 and play on the NEXT frame (costs a frame of
   latency), or `KOFF` first and let the ADSR release run (same latency, done in
   hardware). Measure rather than guess: record the sound card and look at the
   sample either side of the steal.
2. **Is the click masked in practice?** A gunshot stealing footsteps hides a lot
   under its own transient. A quiet voice line stealing another quiet one does
   not. If (1) clicks but the masking holds, that is a legitimate answer — write
   it down rather than paying a frame of latency for every steal.
3. **Does forced play behave the same on both cores?** Everything about core 0
   is newer than everything about core 1 (see the audsrv fork's README) and has
   only ever been exercised in PCSX2 plus one incidental console observation.

## Order, and what each step is worth

1. **Emitters by loudness** — fixes a bug that silently mutes the wrong sounds
   today. No fork change, no click risk, verifiable without a console.
2. **Forced play** — small, and it makes the pinned-channel promise real.
3. **Priorities** — the feature proper, and the largest chain (model → UI →
   codegen → runtime). It only has teeth once 2 is in.

Steps 1 and 3 are separate commits from 2: 2 changes `vendor/tyra/audsrv`, and
that directory is LGPL v2 with its own rebuild ritual (see its README).

## Documentation owed

Per `CLAUDE.md`, in the same commits: `docs/` (there is no audio page yet — a
`docs/sound.md` covering the channel budget, the emitter slots and priorities
would be the right home, with `docs/reverb.md` linking to it), the README
feature list, the audio section of the `tyra-engine-dev` skill (it currently
documents the budget and the pinned-channel behaviour — both change here), the
`tyra-editor-dev` skill if the chain grows a field, and
`examples/reverb-rooms` or a new example if priorities deserve a demo.

Tick this plan off in `docs/backlog.md`; the "make a pinned sound channel
actually retrigger" entry there is step 2 of this brief.
